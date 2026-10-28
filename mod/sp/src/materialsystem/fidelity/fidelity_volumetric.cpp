//========= mapbase-fidelity ==================================================//
//
// Purpose: Fidelity_Volumetric - the light a flashlight or projected texture
//          leaves in the air (fidelity_volumetric_ps30.fxc), drawn by
//          client.dll (fidelity_volumetric.cpp) as a screen pass into a
//          quarter-size target, one material per light.
//
// Nobody writes this shader into a .vmt. The client sets every value each
// frame: the scene depth ($depthtexture), the light's spot texture
// ($spottexture) and shadow map ($shadowdepth, $shadows 0/1), and the view
// and light as vectors ($eye ... $mist, one pixel shader constant each,
// c0..c13). Additive, no depth test, linear in and out (as the other fidelity
// screen passes). The shadow map is read like the engine's flashlight shaders
// read it (SetShadowDepthFiltering, the hardware's filter mode as a combo).
//
//=============================================================================//

#include "BaseVSShader.h"

#include "fidelity_volumetric_vs30.inc"
#include "fidelity_volumetric_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Fidelity_Volumetric, "Light in the air of a flashlight, projected texture or the sun (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DEPTHTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameDepth", "Set by client.dll: the scene's linear depth in alpha." )
		SHADER_PARAM( SPOTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "effects/flashlight001", "Set by client.dll: the light's spot texture." )
		SHADER_PARAM( SHADOWDEPTH, SHADER_PARAM_TYPE_TEXTURE, "", "Set by client.dll: the light's shadow map." )
		SHADER_PARAM( SHADOWS, SHADER_PARAM_TYPE_INTEGER, "0", "Set by client.dll: 1 = read $shadowdepth." )
		SHADER_PARAM( EYE, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c0: eye, depth scale." )
		SHADER_PARAM( FORWARD, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c1: view forward, near z." )
		SHADER_PARAM( RIGHT, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c2: view right x tan(fov x / 2), target width." )
		SHADER_PARAM( UP, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c3: view up x tan(fov y / 2), target height." )
		SHADER_PARAM( TOLIGHT0, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c4: world to the light's texture space, row 0." )
		SHADER_PARAM( TOLIGHT1, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c5: row 1." )
		SHADER_PARAM( TOLIGHT2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c6: row 2." )
		SHADER_PARAM( TOLIGHT3, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c7: row 3." )
		SHADER_PARAM( LIGHTPOS, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c8: light origin, far z." )
		SHADER_PARAM( LIGHTCOLOR, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c9: colour x density, reach." )
		SHADER_PARAM( ATTEN, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c10: constant, linear, quadratic attenuation." )
		SHADER_PARAM( SCREEN, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "c11: texture coordinate to 0..1 over the view: scale, offset." )
		SHADER_PARAM( LIGHTDIR, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c12: direction the light shines, 1 = parallel rays (the sun)." )
		SHADER_PARAM( MIST, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "c13: mist near the ground: base height, 1 / thickness, extra density (0 = none)." )
	END_SHADER_PARAMS

	SHADER_INIT
	{
		if ( params[ DEPTHTEXTURE ]->IsDefined() )
			LoadTexture( DEPTHTEXTURE );
		if ( params[ SPOTTEXTURE ]->IsDefined() )
			LoadTexture( SPOTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ SHADOWDEPTH ]->IsDefined() )
			LoadTexture( SHADOWDEPTH );
	}

	SHADER_FALLBACK
	{
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() )
			return "Wireframe";
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( false );
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// scene depth (data)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// spot texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );		// shadow map
			pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER2 );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_volumetric_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_volumetric_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_volumetric_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_volumetric_ps30 );
		}
		DYNAMIC_STATE
		{
			if ( params[ DEPTHTEXTURE ]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, DEPTHTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			if ( params[ SPOTTEXTURE ]->IsTexture() )
				BindTexture( SHADER_SAMPLER1, SPOTTEXTURE );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_WHITE );
			const bool bShadows = params[ SHADOWS ]->GetIntValue() != 0 && params[ SHADOWDEPTH ]->IsTexture() && g_pConfig->ShadowDepthTexture();
			if ( bShadows )
				BindTexture( SHADER_SAMPLER2, SHADOWDEPTH );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_WHITE );

			// c0..c13 in the order of the parameters
			const int nVectors[] = { EYE, FORWARD, RIGHT, UP, TOLIGHT0, TOLIGHT1, TOLIGHT2, TOLIGHT3, LIGHTPOS, LIGHTCOLOR, ATTEN, SCREEN, LIGHTDIR, MIST };
			for ( int i = 0; i < ARRAYSIZE( nVectors ); i++ )
			{
				float vValue[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
				params[ nVectors[ i ] ]->GetVecValue( vValue, 4 );
				pShaderAPI->SetPixelShaderConstant( i, vValue, 1 );
			}

			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_volumetric_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_volumetric_vs30 );
			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_volumetric_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( SHADOWS, bShadows );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, clamp( g_pHardwareConfig->GetShadowFilterMode(), 0, 2 ) );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_volumetric_ps30 );
		}
		Draw();
	}
END_SHADER
