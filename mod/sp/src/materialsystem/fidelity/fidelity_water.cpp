//========= mapbase-fidelity ==================================================//
//
// Purpose: Water_Fidelity - the game's water (Mapbase's SDK_Water, Valve's
//          water shader) with the sun glinting on the waves and foam where
//          the water is shallow at the shore.
//
// deploy.bat writes copies of the game's water materials with this shader
// (the name starts with "Water": VBSP gives such surfaces no lightmap, as it
// does Valve's water; the engine treats the material as water by its
// $reflecttexture/$refracttexture, $forcecheap ... as before). Their
// Water_DX9_HDR block is merged in by deploy.bat (such blocks only apply to
// shaders of that name).
//
// Expensive water (reflection and/or refraction): fidelity_water_vs30/ps30,
// Mapbase's SDK_Water_vs20/ps2x as shader model 3.0 with the combos dynamic,
// plus, above the water:
//   - the sun's glint: a sharp highlight towards the sun (client.dll hands
//     its direction and colour over as rendering parameters, see
//     fidelity_renderparm.h and fidelity_dynlights.cpp), only where the
//     reflection is bright (under a bridge the sun is not mirrored)
//   - foam where the bottom comes close under the surface (the refraction's
//     depth, which Valve's water fog uses), in the pattern of the waves
// Cheap water (far away, $forcecheap, Hammer) is drawn exactly as Mapbase
// does: with Mapbase's own precompiled SDK_WaterCheap shaders.
//
// Without shader model 3.0 the material falls back to "Water" (Mapbase's).
//
//=============================================================================//

#include "BaseVSShader.h"
#include "mathlib/vmatrix.h"
#include "common_hlsl_cpp_consts.h"
#include "convar.h"
#include "fidelity/fidelity_renderparm.h"

#include "fidelity_water_vs30.inc"
#include "fidelity_water_ps30.inc"
// Mapbase's cheap water (stdshaders/fxctmp9, its .vcs files are Mapbase's)
#include "SDK_WaterCheap_vs20.inc"
#include "SDK_WaterCheap_ps20b.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Water_Fidelity, "The game's water with the sun's glint and foam at the shore (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( REFRACTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_WaterRefraction", "" )
		SHADER_PARAM( REFLECTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_WaterReflection", "" )
		SHADER_PARAM( REFRACTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0", "" )
		SHADER_PARAM( REFRACTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "refraction tint" )
		SHADER_PARAM( REFLECTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0.8", "" )
		SHADER_PARAM( REFLECTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "reflection tint" )
		SHADER_PARAM( NORMALMAP, SHADER_PARAM_TYPE_TEXTURE, "dev/water_normal", "normal map" )
		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $bumpmap" )
		SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "$bumpmap texcoord transform" )
		SHADER_PARAM( SCALE, SHADER_PARAM_TYPE_VEC2, "[1 1]", "" )
		SHADER_PARAM( TIME, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( WATERDEPTH, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( CHEAPWATERSTARTDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "This is the distance from the eye in inches that the shader should start transitioning to a cheaper water shader." )
		SHADER_PARAM( CHEAPWATERENDDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "This is the distance from the eye in inches that the shader should finish transitioning to a cheaper water shader." )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "env_cubemap", "envmap" )
		SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( FOGCOLOR, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( FORCECHEAP, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( FORCEEXPENSIVE, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTENTITIES, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( FOGSTART, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FOGEND, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( ABOVEWATER, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( NOFRESNEL, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( NOLOWENDLIGHTMAP, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( SCROLL1, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( SCROLL2, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( BLURREFRACT, SHADER_PARAM_TYPE_BOOL, "0", "Cause the refraction to be blurry on ps2b hardware" )
		SHADER_PARAM( FIDELITYGLINT, SHADER_PARAM_TYPE_FLOAT, "1", "How strongly the sun glints on this water (x r_fidelity_water_glint)." )
		SHADER_PARAM( FIDELITYFOAM, SHADER_PARAM_TYPE_FLOAT, "1", "How much foam this water shows at the shore (x r_fidelity_water_foam)." )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[ ABOVEWATER ]->IsDefined() )
		{
			Warning( "***need to set $abovewater for material %s\n", pMaterialName );
			params[ ABOVEWATER ]->SetIntValue( 1 );
		}
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		if ( !params[ CHEAPWATERSTARTDISTANCE ]->IsDefined() )
			params[ CHEAPWATERSTARTDISTANCE ]->SetFloatValue( 500.0f );
		if ( !params[ CHEAPWATERENDDISTANCE ]->IsDefined() )
			params[ CHEAPWATERENDDISTANCE ]->SetFloatValue( 1000.0f );
		if ( !params[ SCALE ]->IsDefined() )
			params[ SCALE ]->SetVecValue( 1.0f, 1.0f );
		if ( !params[ SCROLL1 ]->IsDefined() )
			params[ SCROLL1 ]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[ SCROLL2 ]->IsDefined() )
			params[ SCROLL2 ]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[ FOGCOLOR ]->IsDefined() )
		{
			params[ FOGCOLOR ]->SetVecValue( 1.0f, 0.0f, 0.0f );
			Warning( "material %s needs to have a $fogcolor.\n", pMaterialName );
		}
		if ( !params[ REFLECTENTITIES ]->IsDefined() )
			params[ REFLECTENTITIES ]->SetIntValue( 0 );
		if ( !params[ REFLECTBLENDFACTOR ]->IsDefined() )
			params[ REFLECTBLENDFACTOR ]->SetFloatValue( 1.0f );
		if ( !params[ FIDELITYGLINT ]->IsDefined() )
			params[ FIDELITYGLINT ]->SetFloatValue( 1.0f );
		if ( !params[ FIDELITYFOAM ]->IsDefined() )
			params[ FIDELITYFOAM ]->SetFloatValue( 1.0f );

		// as Mapbase's: expensive unless the material says otherwise
		if ( !params[ FORCEEXPENSIVE ]->IsDefined() )
			params[ FORCEEXPENSIVE ]->SetIntValue( 1 );
		if ( params[ FORCEEXPENSIVE ]->GetIntValue() && params[ FORCECHEAP ]->GetIntValue() )
			params[ FORCEEXPENSIVE ]->SetIntValue( 0 );

		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		if ( g_pConfig->UseBumpmapping() && params[ NORMALMAP ]->IsDefined() )
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
	}

	SHADER_FALLBACK
	{
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() )
			return "Water";
		return 0;
	}

	SHADER_INIT
	{
		if ( params[ REFRACTTEXTURE ]->IsDefined() )
			LoadTexture( REFRACTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ REFLECTTEXTURE ]->IsDefined() )
			LoadTexture( REFLECTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ ENVMAP ]->IsDefined() )
		{
			LoadCubeMap( ENVMAP, TEXTUREFLAGS_SRGB );
			// as Mapbase: a missing cubemap becomes the default one
			if ( mat_specular_disable_on_missing.GetBool() && params[ ENVMAP ]->GetTextureValue()->IsError() )
			{
				params[ ENVMAP ]->SetStringValue( "engine/defaultcubemap" );
				LoadCubeMap( ENVMAP, TEXTUREFLAGS_SRGB );
			}
		}
		if ( params[ NORMALMAP ]->IsDefined() )
			LoadBumpMap( NORMALMAP );
		if ( params[ BASETEXTURE ]->IsDefined() )
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
	}

	inline void DrawReflectionRefraction( IMaterialVar **params, IShaderShadow *pShaderShadow,
		IShaderDynamicAPI *pShaderAPI, bool bReflection, bool bRefraction )
	{
		const bool bBaseTexture = params[ BASETEXTURE ]->IsTexture();
		Vector4D vecScroll1;
		params[ SCROLL1 ]->GetVecValue( vecScroll1.Base(), 4 );
		const bool bMultiTexture = fabs( vecScroll1.x ) > 0.0f && !bBaseTexture;	// (the shaders have no combo with both)

		SHADOW_STATE
		{
			SetInitialShadowState();
			if ( bRefraction )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			}
			if ( bReflection )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, true );
			}
			if ( bBaseTexture )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );		// lightmap
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );
			}
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );			// normal map

			// texcoord0: base, texcoord1: lightmap, texcoord2: lightmap offset
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T,
				bBaseTexture ? 3 : 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_water_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_water_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_water_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_water_ps30 );

			FogToFogColor();
			pShaderShadow->EnableSRGBWrite( true );	// linear values, as Mapbase's
			pShaderShadow->EnableAlphaWrites( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();
			if ( bRefraction )
				BindTexture( SHADER_SAMPLER0, REFRACTTEXTURE, -1 );
			if ( bReflection )
				BindTexture( SHADER_SAMPLER2, REFLECTTEXTURE, -1 );
			BindTexture( SHADER_SAMPLER4, NORMALMAP, BUMPFRAME );
			if ( bBaseTexture )
			{
				BindTexture( SHADER_SAMPLER1, BASETEXTURE, FRAME );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_LIGHTMAP );
			}

			if ( bRefraction )
				SetPixelShaderConstantGammaToLinear( 1, REFRACTTINT );
			if ( bReflection )
			{
				if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
				{
					// x 4 in linear space: the reflection was premultiplied by .25 (overbright), as in Mapbase's
					float vGamma[ 3 ];
					params[ REFLECTTINT ]->GetVecValue( vGamma, 3 );
					const float vLinear[ 4 ] = { GammaToLinear( vGamma[ 0 ] ) * 4.0f, GammaToLinear( vGamma[ 1 ] ) * 4.0f, GammaToLinear( vGamma[ 2 ] ) * 4.0f, 1.0f };
					pShaderAPI->SetPixelShaderConstant( 4, vLinear, 1 );
				}
				else
				{
					SetPixelShaderConstantGammaToLinear( 4, REFLECTTINT );
				}
			}

			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, BUMPTRANSFORM );

			const float flTime = pShaderAPI->CurrentTime();
			float vScroll[ 4 ];
			float vValue[ 4 ];
			params[ SCROLL1 ]->GetVecValue( vValue, 4 );
			vScroll[ 0 ] = flTime * vValue[ 0 ];
			vScroll[ 1 ] = flTime * vValue[ 1 ];
			params[ SCROLL2 ]->GetVecValue( vValue, 4 );
			vScroll[ 2 ] = flTime * vValue[ 0 ];
			vScroll[ 3 ] = flTime * vValue[ 1 ];
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, vScroll, 1 );

			// the sun (client.dll, fidelity_dynlights.cpp): its direction for the vertex shader, w = there is one
			Vector vecSun = pShaderAPI->GetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_SUN_DIRECTION );
			const float flGlint = MAX( params[ FIDELITYGLINT ]->GetFloatValue(), 0.0f );
			float vSunColor[ 4 ] = {
				MAX( pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_R ), 0.0f ) * flGlint,
				MAX( pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_G ), 0.0f ) * flGlint,
				MAX( pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_SUN_B ), 0.0f ) * flGlint,
				0.0f };
			const bool bSun = vecSun.LengthSqr() > 0.25f && ( vSunColor[ 0 ] + vSunColor[ 1 ] + vSunColor[ 2 ] ) > 0.0f;
			if ( bSun )
				VectorNormalize( vecSun );
			else
				vecSun.Init( 0.0f, 0.0f, 1.0f );	// (never a zero vector: the pixel shader normalizes it)
			const float vSunDir[ 4 ] = { vecSun.x, vecSun.y, vecSun.z, bSun ? 1.0f : 0.0f };
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, vSunDir, 1 );
			pShaderAPI->SetPixelShaderConstant( 10, vSunColor, 1 );
			const float vFoam[ 4 ] = { clamp( pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_WATER_FOAM ), 0.0f, 2.0f ) * MAX( params[ FIDELITYFOAM ]->GetFloatValue(), 0.0f ), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 11, vFoam, 1 );

			const float c0[ 4 ] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1 );
			const float c2[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
			pShaderAPI->SetPixelShaderConstant( 2, c2, 1 );
			const float c3[ 4 ] = { 1.0f, 0.0f, 0.0f, 0.0f };	// fresnel
			pShaderAPI->SetPixelShaderConstant( 3, c3, 1 );
			const float c5[ 4 ] = { params[ REFLECTAMOUNT ]->GetFloatValue(), params[ REFLECTAMOUNT ]->GetFloatValue(),
				params[ REFRACTAMOUNT ]->GetFloatValue(), params[ REFRACTAMOUNT ]->GetFloatValue() };
			pShaderAPI->SetPixelShaderConstant( 5, c5, 1 );
			SetPixelShaderConstantGammaToLinear( 6, FOGCOLOR );
			float c7[ 4 ] = { params[ FOGSTART ]->GetFloatValue(), params[ FOGEND ]->GetFloatValue() - params[ FOGSTART ]->GetFloatValue(), 1.0f, 0.0f };
			if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
				c7[ 2 ] = 4.0f;	// water overbright factor
			pShaderAPI->SetPixelShaderConstant( 7, c7, 1 );
			pShaderAPI->SetPixelShaderFogParams( 8 );
			float vEyePos[ 4 ];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
			vEyePos[ 3 ] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 9, vEyePos, 1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_water_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( BASETEXTURE, bBaseTexture );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( MULTITEXTURE, bMultiTexture );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_water_vs30 );
			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_water_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( BASETEXTURE, bBaseTexture );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( MULTITEXTURE, bMultiTexture );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( REFLECT, bReflection );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( REFRACT, bRefraction );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( ABOVEWATER, params[ ABOVEWATER ]->GetIntValue() != 0 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( BLURRY_REFRACT, params[ BLURREFRACT ]->GetIntValue() != 0 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_water_ps30 );
		}
		Draw();
	}

	// Mapbase's cheap water, unchanged (its own shaders)
	inline void DrawCheapWater( IMaterialVar **params, IShaderShadow *pShaderShadow,
		IShaderDynamicAPI *pShaderAPI, bool bBlend, bool bRefraction )
	{
		SHADOW_STATE
		{
			SetInitialShadowState();
			if ( UsingEditor( params ) )
				pShaderShadow->EnableCulling( false );
			if ( bBlend )
				EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// envmap
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// normal map
			if ( bRefraction && bBlend )
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );	// refraction (for alpha)
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );		// normalizing cube map
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( sdk_watercheap_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( BLEND, bBlend && bRefraction );
			SET_STATIC_VERTEX_SHADER( sdk_watercheap_vs20 );

			Vector4D vecScroll1;
			params[ SCROLL1 ]->GetVecValue( vecScroll1.Base(), 4 );
			DECLARE_STATIC_PIXEL_SHADER( sdk_watercheap_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( FRESNEL, params[ NOFRESNEL ]->GetIntValue() == 0 );
			SET_STATIC_PIXEL_SHADER_COMBO( BLEND, bBlend );
			SET_STATIC_PIXEL_SHADER_COMBO( REFRACTALPHA, bRefraction );
			SET_STATIC_PIXEL_SHADER_COMBO( HDRTYPE, g_pHardwareConfig->GetHDRType() );
			SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE, fabs( vecScroll1.x ) > 0.0f );
			SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, 0 );
			SET_STATIC_PIXEL_SHADER( sdk_watercheap_ps20b );

			if ( g_pHardwareConfig->GetHDRType() != HDR_TYPE_NONE )
				pShaderShadow->EnableSRGBWrite( true );
			FogToFogColor();
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();
			BindTexture( SHADER_SAMPLER0, ENVMAP, ENVMAPFRAME );
			BindTexture( SHADER_SAMPLER1, NORMALMAP, BUMPFRAME );
			if ( bRefraction && bBlend )
				BindTexture( SHADER_SAMPLER2, REFRACTTEXTURE, -1 );
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );

			SetPixelShaderConstant( 0, FOGCOLOR );
			const float flStart = params[ CHEAPWATERSTARTDISTANCE ]->GetFloatValue();
			const float flEnd = params[ CHEAPWATERENDDISTANCE ]->GetFloatValue();
			const float vCheap[ 4 ] = { flStart * VSHADER_VECT_SCALE, flEnd * VSHADER_VECT_SCALE,
				PSHADER_VECT_SCALE / ( flEnd - flStart ), flStart / ( flEnd - flStart ) };
			pShaderAPI->SetPixelShaderConstant( 1, vCheap, 1 );
			float vEyePos[ 4 ];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
			vEyePos[ 3 ] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 4, vEyePos, 1 );
			if ( g_pConfig->bShowSpecular )
			{
				SetPixelShaderConstant( 2, REFLECTTINT, REFLECTBLENDFACTOR );
			}
			else
			{
				const float vZero[ 4 ] = { 0.0f, 0.0f, 0.0f, params[ REFLECTBLENDFACTOR ]->GetFloatValue() };
				pShaderAPI->SetPixelShaderConstant( 2, vZero, 1 );
			}
			pShaderAPI->SetPixelShaderFogParams( 3 );
			if ( params[ SCROLL1 ]->IsDefined() )
			{
				const float flTime = pShaderAPI->CurrentTime();
				float vScroll[ 4 ];
				float vValue[ 4 ];
				params[ SCROLL1 ]->GetVecValue( vValue, 4 );
				vScroll[ 0 ] = flTime * vValue[ 0 ];
				vScroll[ 1 ] = flTime * vValue[ 1 ];
				params[ SCROLL2 ]->GetVecValue( vValue, 4 );
				vScroll[ 2 ] = flTime * vValue[ 0 ];
				vScroll[ 3 ] = flTime * vValue[ 1 ];
				pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, vScroll, 1 );
			}

			DECLARE_DYNAMIC_VERTEX_SHADER( sdk_watercheap_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( sdk_watercheap_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( HDRENABLED, IsHDREnabled() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER( sdk_watercheap_ps20b );
		}
		Draw();
	}

	SHADER_DRAW
	{
		// Mapbase's console variable (its shader DLL makes it)
		static ConVarRef r_waterforceexpensive( "r_waterforceexpensive", true );
		bool bForceExpensive = r_waterforceexpensive.IsValid() && r_waterforceexpensive.GetBool();
		const bool bForceCheap = params[ FORCECHEAP ]->GetIntValue() != 0 || UsingEditor( params );
		if ( bForceCheap )
			bForceExpensive = false;
		else
			bForceExpensive = bForceExpensive || params[ FORCEEXPENSIVE ]->GetIntValue() != 0;

		const bool bRefraction = params[ REFRACTTEXTURE ]->IsTexture();
		const bool bReflection = bForceExpensive && params[ REFLECTTEXTURE ]->IsTexture();
		bool bDrewSomething = false;
		if ( !bForceCheap && ( bReflection || bRefraction ) )
		{
			bDrewSomething = true;
			DrawReflectionRefraction( params, pShaderShadow, pShaderAPI, bReflection, bRefraction );
		}
		// as Mapbase: no cheap pass on decals (no env_cubemap for them)
		if ( !bReflection && params[ ENVMAP ]->IsTexture() && !IS_FLAG_SET( MATERIAL_VAR_DECAL ) )
		{
			bDrewSomething = true;
			DrawCheapWater( params, pShaderShadow, pShaderAPI, !bForceCheap, bRefraction );
		}
		if ( !bDrewSomething )
			Draw();	// (in the tools: something, not wireframe)
	}

END_SHADER
