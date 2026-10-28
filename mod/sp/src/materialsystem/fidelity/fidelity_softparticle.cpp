//========= mapbase-fidelity ==================================================//
//
// Purpose: Fidelity_SoftParticle - the game's old particles (CParticleMgr:
//          smoke, dust, impact puffs, our muzzle smoke and rain splashes)
//          fade out where they meet a wall or the floor, instead of cutting
//          into it along a hard straight line (Black Mesa's "soft particles").
//
// Valve's new particles (.pcf, SpriteCard) already do this ($depthblend, on
// by default). The old ones are drawn with UnlitGeneric ($vertexcolor
// $vertexalpha), which cannot. deploy.bat writes copies of such particle
// materials with this shader (and client.dll's own effects use them); it
// draws them as UnlitGeneric does - base texture x vertex colour and alpha,
// translucent or $additive, fogged - and multiplies the alpha with Valve's
// DepthFeathering against the scene's depth in the frame buffer's alpha
// (_rt_FullFrameDepth, copied by the engine after the opaque scene).
// $depthblendscale as SpriteCard's: lower = softer, longer fade.
//
//=============================================================================//

#include "BaseVSShader.h"

#include "fidelity_softparticle_vs30.inc"
#include "fidelity_softparticle_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Fidelity_SoftParticle, "Old particles that fade where they meet walls (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DEPTHBLENDSCALE, SHADER_PARAM_TYPE_FLOAT, "50.0", "As SpriteCard's: how quickly the particle comes back away from a wall (lower = softer)." )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[ DEPTHBLENDSCALE ]->IsDefined() )
			params[ DEPTHBLENDSCALE ]->SetFloatValue( 50.0f );
		SET_FLAGS( MATERIAL_VAR_VERTEXCOLOR );
		SET_FLAGS( MATERIAL_VAR_VERTEXALPHA );
		SET_FLAGS( MATERIAL_VAR_TRANSLUCENT );
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
	}

	SHADER_INIT
	{
		if ( params[ BASETEXTURE ]->IsDefined() )
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
	}

	SHADER_FALLBACK
	{
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() )
			return "UnlitGeneric";
		return 0;
	}

	SHADER_DRAW
	{
		const bool bAdditive = IS_FLAG_SET( MATERIAL_VAR_ADDITIVE );
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableCulling( false );
			if ( bAdditive )
				EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			else
				EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// base
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// scene depth (data)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableSRGBWrite( true );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_COLOR, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_softparticle_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_softparticle_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_softparticle_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_softparticle_ps30 );

			DefaultFog();	// additive: towards black
		}
		DYNAMIC_STATE
		{
			if ( params[ BASETEXTURE ]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_FRAME_BUFFER_FULL_DEPTH );

			const MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_softparticle_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_softparticle_vs30 );
			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_softparticle_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_softparticle_ps30 );

			LoadViewportTransformScaledIntoVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10 );
			SetModulationPixelShaderDynamicState_LinearColorSpace( 0 );
			const float vOptions[ 4 ] = { bAdditive ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 1, vOptions, 1 );
			pShaderAPI->SetDepthFeatheringPixelShaderConstant( 2, params[ DEPTHBLENDSCALE ]->GetFloatValue() );
			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );
			float vEyePos[ 4 ] = { 0, 0, 0, 0 };
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
			pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos, 1 );
		}
		Draw();
	}
END_SHADER
