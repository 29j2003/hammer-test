//========= mapbase-fidelity ==================================================//
//
// Purpose: Fidelity_Liquid - a glass container partly filled with liquid, in
//          one material on the container's own mesh (bottles, jars, jugs).
//
// Usage in a .vmt (deploy.bat writes such files for the glass bottles of
// Half-Life 2 and its episodes automatically, see docs/ENTWICKLUNG.md):
//
//   "Fidelity_Liquid"
//   {
//       "$basetexture"  "models/props_junk/garbage_glassbottle001a"   // optional
//       "$liquidcolor"  "[0.45 0.22 0.06]"                               // optional
//       "Proxies" { "FidelityLiquid" { } }
//   }
//
// The FidelityLiquid proxy (client.dll, fidelity_liquid_proxy.cpp) finds the
// fill height from the volume inside the container (the share $fill of it,
// for the prop's current pose), gives every prop its own fill level, takes the colour from the prop's Render Color when one is
// set, and lets the liquid slosh (tilt and small waves) when the prop is moved
// or turned. Without the proxy the container just looks like empty glass.
//
// Kinds of liquid and glass (deploy.bat writes them by name and texture):
//   "$bubbles"      "[0.5 1.1 0.5 1]"    density, size, speed, sway (medical: big and slow)
//   "$bubbles"      "[0.35 0.3 4 0.2]"   (fizzy drink: many small, fast)
//   "$viscosity"    "0.7"                thick liquid sloshes slower and heavier
//   "$tintedglass"  "0.8"                dark glass, liquid only faintly visible
//   "$liquidkey"    "[0.25 0.8 0.15]"    only the green parts hold liquid (medical), the rest stays as it is
//   "$splash"       "1"                  splashes when the prop breaks
//   "$spill"        "1"                  open bottle: pours out when tipped over
//   no $liquidcolor                      the texture's own colour is the liquid's
//
// The proxy also hands over the container's profile ($liquidaxis, $liquiddir,
// $liquidradii, $liquidradii2): the shader draws the surface where the view
// through the glass meets it.
//
// Three passes: the solid parts opaque with depth writes (both sides), then - alpha
// blended, culling off, no depth writes - the container's back faces, then
// its front faces (see fidelity_liquid_ps30.fxc). Needs shader model
// 3.0; older hardware falls back to VertexLitGeneric.
//
//=============================================================================//

#include "BaseVSShader.h"
#include "cpp_shader_constant_register_map.h"

#include "fidelity_liquid_vs30.inc"
#include "fidelity_liquid_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Fidelity_Liquid, "Glass container with liquid inside (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( LIQUIDCOLOR, SHADER_PARAM_TYPE_VEC3, "[-1 -1 -1]", "Colour of the liquid (a prop's Render Color overrides it). Not set: the texture's own colour." )
		SHADER_PARAM( LIQUIDALPHA, SHADER_PARAM_TYPE_FLOAT, "0.85", "Opacity of the liquid, 0..1." )
		SHADER_PARAM( SURFACECOLOR, SHADER_PARAM_TYPE_COLOR, "[0.8 0.85 0.85]", "Colour of the liquid surface and the foam line." )
		SHADER_PARAM( FOAMWIDTH, SHADER_PARAM_TYPE_FLOAT, "0.4", "Height of the foam line under the surface, in units." )
		SHADER_PARAM( GLASSCOLOR, SHADER_PARAM_TYPE_COLOR, "[0.75 0.85 0.8]", "Tint of the glass." )
		SHADER_PARAM( GLASSALPHA, SHADER_PARAM_TYPE_FLOAT, "0.3", "Opacity of the empty glass, 0..1." )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "env_cubemap", "Reflections (default: nearest env_cubemap)." )
		SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( ENVMAPSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "0.35", "Strength of the reflections." )
		SHADER_PARAM( FILL, SHADER_PARAM_TYPE_FLOAT, "-1", "Fill level 0..1: share of the container's volume. -1 = a different level for every prop." )
		SHADER_PARAM( FILLMIN, SHADER_PARAM_TYPE_FLOAT, "0.25", "Lowest random fill level." )
		SHADER_PARAM( FILLMAX, SHADER_PARAM_TYPE_FLOAT, "0.8", "Highest random fill level." )
		SHADER_PARAM( WOBBLE, SHADER_PARAM_TYPE_FLOAT, "1.0", "How much the liquid sways when the prop moves. 0 = never." )
		SHADER_PARAM( LABELTHRESHOLD, SHADER_PARAM_TYPE_FLOAT, "0.8", "Texture alpha above this is a solid label / cap (only for textures with alpha)." )
		SHADER_PARAM( LIQUIDPLANE, SHADER_PARAM_TYPE_VEC4, "[0 0 1 -100000]", "Set by the FidelityLiquid proxy: world space surface plane (normal, distance)." )
		SHADER_PARAM( LIQUIDWAVE, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by the FidelityLiquid proxy: wave height on the surface (units), time, how far the bubbles rose, fizz 0..1." )
		SHADER_PARAM( BUBBLES, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Rising bubbles: density 0..1 (0 = none), size (units), speed (units/s), sway 0..1." )
		SHADER_PARAM( TINTEDGLASS, SHADER_PARAM_TYPE_FLOAT, "0", "Dark glass 0..1: the texture's own colour over everything, the liquid only faintly visible." )
		SHADER_PARAM( VISCOSITY, SHADER_PARAM_TYPE_FLOAT, "0", "0 = water .. 1 = thick (slower, heavier sloshing, smaller waves). Read by the proxy." )
		SHADER_PARAM( LIQUIDKEY, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Only texels of this colour (its hue) hold liquid, e.g. [0.2 0.8 0.1] for the green tube of a health vial; the rest stays as it is. [0 0 0] = the whole container." )
		SHADER_PARAM( SPLASH, SHADER_PARAM_TYPE_INTEGER, "0", "1 = the liquid splashes when the prop breaks (read by client.dll)." )
		SHADER_PARAM( SPILL, SHADER_PARAM_TYPE_INTEGER, "0", "1 = an open bottle: tipped over, the liquid pours out of its neck (read by client.dll)." )
		SHADER_PARAM( LIQUIDCENTER, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Set by the FidelityLiquid proxy: fill point in world space (for the bubbles)." )
		SHADER_PARAM( LIQUIDMASK, SHADER_PARAM_TYPE_TEXTURE, "", "Set by the FidelityLiquid proxy: where the texture holds liquid (white), made from $liquidkey." )
		SHADER_PARAM( LIQUIDAXIS, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by the FidelityLiquid proxy: container profile - start of its axis (world), length of a slice." )
		SHADER_PARAM( LIQUIDDIR, SHADER_PARAM_TYPE_VEC4, "[0 0 1 0]", "Set by the FidelityLiquid proxy: direction of the axis (world), w = 1 when the profile is known." )
		SHADER_PARAM( LIQUIDRADII, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by the FidelityLiquid proxy: radius of the first 4 of 8 slices along the axis." )
		SHADER_PARAM( LIQUIDRADII2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by the FidelityLiquid proxy: radius of the last 4 of 8 slices along the axis." )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// A container that only holds liquid where its colour says ($liquidkey:
		// a medkit's vial) is solid otherwise and stays opaque - a translucent
		// material casts no shadow, the medkit's shadow was only its vial's
		// (Julius 26.10.2026). The glass bottles are see-through.
		float vKey[ 3 ] = { 0.0f, 0.0f, 0.0f };
		if ( params[ LIQUIDKEY ]->IsDefined() )
			params[ LIQUIDKEY ]->GetVecValue( vKey, 3 );
		if ( vKey[ 0 ] + vKey[ 1 ] + vKey[ 2 ] <= 0.0f )
			SET_FLAGS( MATERIAL_VAR_TRANSLUCENT );
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );

		SET_PARAM_STRING_IF_NOT_DEFINED( ENVMAP, "env_cubemap" );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( LIQUIDALPHA, 0.85f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( FOAMWIDTH, 0.4f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( GLASSALPHA, 0.3f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( ENVMAPSTRENGTH, 0.35f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( FILL, -1.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( FILLMIN, 0.25f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( FILLMAX, 0.8f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( WOBBLE, 1.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( LABELTHRESHOLD, 0.8f );
		if ( !params[ LIQUIDCOLOR ]->IsDefined() )
			params[ LIQUIDCOLOR ]->SetVecValue( -1.0f, -1.0f, -1.0f );	// the texture's colour
		if ( !params[ SURFACECOLOR ]->IsDefined() )
			params[ SURFACECOLOR ]->SetVecValue( 0.8f, 0.85f, 0.85f );
		if ( !params[ GLASSCOLOR ]->IsDefined() )
			params[ GLASSCOLOR ]->SetVecValue( 0.75f, 0.85f, 0.8f );
		if ( !params[ LIQUIDPLANE ]->IsDefined() )
			params[ LIQUIDPLANE ]->SetVecValue( 0.0f, 0.0f, 1.0f, -100000.0f );
		if ( !params[ LIQUIDWAVE ]->IsDefined() )
			params[ LIQUIDWAVE ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
		if ( !params[ BUBBLES ]->IsDefined() )
			params[ BUBBLES ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( TINTEDGLASS, 0.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( VISCOSITY, 0.0f );
		SET_PARAM_INT_IF_NOT_DEFINED( SPLASH, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( SPILL, 0 );
		if ( !params[ LIQUIDKEY ]->IsDefined() )
			params[ LIQUIDKEY ]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[ LIQUIDCENTER ]->IsDefined() )
			params[ LIQUIDCENTER ]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[ LIQUIDAXIS ]->IsDefined() )
			params[ LIQUIDAXIS ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
		if ( !params[ LIQUIDDIR ]->IsDefined() )
			params[ LIQUIDDIR ]->SetVecValue( 0.0f, 0.0f, 1.0f, 0.0f );	// no profile: the surface only on back faces
		if ( !params[ LIQUIDRADII ]->IsDefined() )
			params[ LIQUIDRADII ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
		if ( !params[ LIQUIDRADII2 ]->IsDefined() )
			params[ LIQUIDRADII2 ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
	}

	SHADER_FALLBACK
	{
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() )
			return "VertexLitGeneric";
		return 0;
	}

	SHADER_INIT
	{
		if ( params[ BASETEXTURE ]->IsDefined() )
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ ENVMAP ]->IsDefined() )
			LoadCubeMap( ENVMAP, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
	}

	// Every snapshot must declare the full vertex format, also the one taken for
	// the flashlight; otherwise the engine refuses the model's (compressed)
	// vertices: "does not support vertex format used by the mesh".
	static void SetVertexFormat( IShaderShadow *pShaderShadow )
	{
		int nTexCoordDims[ 1 ] = { 2 };
		pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED, 1, nTexCoordDims, 0 );
	}

	SHADER_DRAW
	{
		// Translucent: the flashlight pass would only add a second copy. As in
		// Valve's shaders, the snapshot is always the normal one and only the
		// actual flashlight draw is skipped (one empty draw per pass).
		if ( !pShaderShadow && UsingFlashlight( params ) )
		{
			Draw( false );
			Draw( false );
			Draw( false );
			return;
		}

		// Pass 0: solid parts, opaque with depth; 1: container back faces; 2: its front faces (blended)
		for ( int nPass = 0; nPass < 3; nPass++ )
		{
			SHADOW_STATE
			{
				SetInitialShadowState();
				if ( nPass == 0 )
				{
					pShaderShadow->EnableCulling( false );	// both sides: caps seen from inside through the glass
					pShaderShadow->EnableDepthWrites( true );
					pShaderShadow->EnableBlending( false );
				}
				else
				{
					pShaderShadow->EnableCulling( false );
					pShaderShadow->EnableDepthWrites( false );
					pShaderShadow->EnableBlending( true );
					pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
				}
				// The solid parts of a keyed (opaque) container write the depth into
				// destination alpha like any opaque model: SSAO reads it there, and
				// the medkit got the occlusion of the corner behind it, a dark line
				// across the kit (Julius 26.10.2026: "SSAO-Strich durch das Medkit")
				pShaderShadow->EnableAlphaWrites( nPass == 0 && !IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) );

				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// base
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// reflections
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );		// liquid mask (from the proxy)
				pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );		// normalization cube map
				pShaderShadow->EnableSRGBWrite( true );

				SetVertexFormat( pShaderShadow );

				DECLARE_STATIC_VERTEX_SHADER( fidelity_liquid_vs30 );
				SET_STATIC_VERTEX_SHADER( fidelity_liquid_vs30 );
				DECLARE_STATIC_PIXEL_SHADER( fidelity_liquid_ps30 );
				SET_STATIC_PIXEL_SHADER( fidelity_liquid_ps30 );

				DefaultFog();
			}
			DYNAMIC_STATE
			{
				if ( params[ BASETEXTURE ]->IsTexture() )
					BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );

				float flReflection = params[ ENVMAPSTRENGTH ]->GetFloatValue();
				if ( params[ ENVMAP ]->IsTexture() )
				{
					BindTexture( SHADER_SAMPLER1, ENVMAP, ENVMAPFRAME );
				}
				else
				{
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );
					flReflection = 0.0f;
				}
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );
				const bool bMask = params[ LIQUIDMASK ]->IsTexture();
				if ( bMask )
					BindTexture( SHADER_SAMPLER2, LIQUIDMASK );
				else
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );

				LightState_t lightState = { 0, false, false };
				pShaderAPI->GetDX9LightState( &lightState );

				const MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
				DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_liquid_vs30 );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
				SET_DYNAMIC_VERTEX_SHADER_COMBO( NUM_LIGHTS, lightState.m_nNumLights );
				// Static props: baked per-vertex light from the second vertex stream (no ambient cube then)
				SET_DYNAMIC_VERTEX_SHADER_COMBO( STATIC_LIGHT, lightState.m_bStaticLight ? 1 : 0 );
				SET_DYNAMIC_VERTEX_SHADER( fidelity_liquid_vs30 );

				DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_liquid_ps30 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( NUM_LIGHTS, lightState.m_nNumLights );
				SET_DYNAMIC_PIXEL_SHADER( fidelity_liquid_ps30 );

				SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, BASETEXTURETRANSFORM );
				SetModulationPixelShaderDynamicState_LinearColorSpace( 1 );
				// No ambient cube for the vertex shader: lighting is per pixel (below).
				pShaderAPI->SetPixelShaderStateAmbientLightCube( PSREG_AMBIENT_CUBE, !lightState.m_bAmbientLight );
				pShaderAPI->CommitPixelShaderLighting( PSREG_LIGHT_INFO_ARRAY );
				pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );

				float vEyePos[ 4 ] = { 0, 0, 0, 0 };
				pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
				pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos, 1 );

				// Colours are given in gamma space like Valve's $color; the shader works linear.
				float vPlane[ 4 ], vLiquid[ 4 ], vSurface[ 4 ], vGlass[ 4 ], vWave[ 4 ], vBubbles[ 4 ], vCenter[ 4 ], vKey[ 4 ];
				params[ LIQUIDPLANE ]->GetVecValue( vPlane, 4 );
				params[ LIQUIDWAVE ]->GetVecValue( vWave, 4 );
				params[ BUBBLES ]->GetVecValue( vBubbles, 4 );
				if ( vBubbles[ 1 ] <= 0.01f )
					vBubbles[ 0 ] = 0.0f;	// no size, no bubbles
				params[ LIQUIDCENTER ]->GetVecValue( vCenter, 3 );
				vCenter[ 3 ] = clamp( params[ TINTEDGLASS ]->GetFloatValue(), 0.0f, 1.0f );
				params[ LIQUIDKEY ]->GetVecValue( vKey, 3 );
				// 0: no key (the whole container), 1: key colour in the texture, 2: the proxy's mask
				vKey[ 3 ] = ( vKey[ 0 ] + vKey[ 1 ] + vKey[ 2 ] > 0.0f ) ? ( bMask ? 2.0f : 1.0f ) : 0.0f;
				params[ LIQUIDCOLOR ]->GetVecValue( vLiquid, 3 );
				params[ SURFACECOLOR ]->GetVecValue( vSurface, 3 );
				params[ GLASSCOLOR ]->GetVecValue( vGlass, 3 );
				// No liquid colour (negative): the texture's own colour is the liquid
				// (the proxy normally sets one: the average of the container's texture)
				const float flTextureColor = ( vLiquid[ 0 ] < 0.0f ) ? 1.0f : 0.0f;
				for ( int i = 0; i < 3; i++ )
				{
					vLiquid[ i ] = GammaToLinear( MAX( vLiquid[ i ], 0.0f ) );
					vKey[ i ] = GammaToLinear( vKey[ i ] );
					vSurface[ i ] = GammaToLinear( vSurface[ i ] );
					vGlass[ i ] = GammaToLinear( vGlass[ i ] );
				}
				vLiquid[ 3 ] = params[ LIQUIDALPHA ]->GetFloatValue();
				vSurface[ 3 ] = params[ FOAMWIDTH ]->GetFloatValue();
				vGlass[ 3 ] = params[ GLASSALPHA ]->GetFloatValue();
				// Textures without alpha read as alpha 1: no labels then, or it would all be solid.
				ITexture *pBase = params[ BASETEXTURE ]->IsTexture() ? params[ BASETEXTURE ]->GetTextureValue() : NULL;
				const float flLabelThreshold = ( pBase && pBase->IsTranslucent() ) ? params[ LABELTHRESHOLD ]->GetFloatValue() : 2.0f;
				const float vPass[ 4 ] = { (float)nPass, flReflection, flLabelThreshold, flTextureColor };

				pShaderAPI->SetPixelShaderConstant( 32, vPlane, 1 );
				pShaderAPI->SetPixelShaderConstant( 33, vLiquid, 1 );
				pShaderAPI->SetPixelShaderConstant( 34, vSurface, 1 );
				pShaderAPI->SetPixelShaderConstant( 35, vGlass, 1 );
				pShaderAPI->SetPixelShaderConstant( 36, vPass, 1 );
				pShaderAPI->SetPixelShaderConstant( 37, vWave, 1 );
				pShaderAPI->SetPixelShaderConstant( 38, vBubbles, 1 );
				pShaderAPI->SetPixelShaderConstant( 39, vCenter, 1 );
				pShaderAPI->SetPixelShaderConstant( 40, vKey, 1 );

				// The container's profile, for the surface seen through the glass
				float vProfile[ 4 ][ 4 ];
				params[ LIQUIDAXIS ]->GetVecValue( vProfile[ 0 ], 4 );
				params[ LIQUIDDIR ]->GetVecValue( vProfile[ 1 ], 4 );
				params[ LIQUIDRADII ]->GetVecValue( vProfile[ 2 ], 4 );
				params[ LIQUIDRADII2 ]->GetVecValue( vProfile[ 3 ], 4 );
				pShaderAPI->SetPixelShaderConstant( 41, vProfile[ 0 ], 4 );

				// depth into destination alpha (solid parts of an opaque container, see above)
				const float vDepthAlpha[ 4 ] = { ( nPass == 0 && !IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) && pShaderAPI->ShouldWriteDepthToDestAlpha() ) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 45, vDepthAlpha, 1 );
			}
			Draw();
		}
	}

END_SHADER
