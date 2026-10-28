//========= mapbase-fidelity ==================================================//
//
// Purpose: Fidelity_Sky - the 2D skybox like Valve's Sky shaders, plus a thin
//          layer of drifting clouds (see fidelity_sky_ps30.fxc).
//
// Nobody writes this shader into a .vmt: client.dll (fidelity_sky.cpp) switches
// the skybox materials of the map over to it at run time, with the parameters
// of their own .vmt, and sets the cloud values every frame:
//   $cloudparams  [amount scale offset_x offset_y]   (amount 0 = no clouds)
//   $cloudshape   [coverage softness darkness 0]
// Without the client part, or on hardware without shader model 3.0, the sky
// stays as it was.
//
// Texture handling follows Sky_DX9 / Sky_HDR_DX9: $basetexture (8 bit or
// float), $hdrcompressedtexture (RGBS) and $hdrbasetexture. The 3-texture HDR
// method B ($hdrcompressedtexture0..2) is not supported; such skies are not
// switched over.
//
//=============================================================================//

#include "BaseVSShader.h"

#include "fidelity_sky_vs30.inc"
#include "fidelity_sky_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Fidelity_Sky, "Skybox with drifting clouds (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( HDRBASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "base texture when running with HDR enabled" )
		SHADER_PARAM( HDRCOMPRESSEDTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "base texture (compressed RGBS) when running with HDR enabled" )
		SHADER_PARAM_OVERRIDE( COLOR, SHADER_PARAM_TYPE_VEC3, "[ 1 1 1]", "color multiplier", SHADER_PARAM_NOT_EDITABLE )
		SHADER_PARAM( CLOUDPARAMS, SHADER_PARAM_TYPE_VEC4, "[0.85 1 0 0]", "Set by client.dll: cloud amount, scale, drift offset x, y." )
		SHADER_PARAM( CLOUDSHAPE, SHADER_PARAM_TYPE_VEC4, "[0.42 0.22 0.5 0]", "Set by client.dll: cloud coverage, softness, darkness." )
		SHADER_PARAM( CLOUDSUN, SHADER_PARAM_TYPE_VEC4, "[0 0 1 0]", "Set by client.dll: direction to the sun (world), 1 = there is one (lights the clouds)." )
		SHADER_PARAM( SUNDISC, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by client.dll: the env_sun drawn behind the clouds - colour of its core (linear), its size (radians); 0 = the engine draws it." )
		SHADER_PARAM( SUNHALO, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Set by client.dll: colour (linear) and size (radians) of the env_sun's glow around it." )
	END_SHADER_PARAMS

	// 0 = 8 bit / float $basetexture, 1 = $hdrcompressedtexture (RGBS), 2 = $hdrbasetexture
	static int SkyMode( IMaterialVar **params )
	{
		if ( g_pHardwareConfig->GetHDRType() != HDR_TYPE_NONE )
		{
			if ( params[ HDRCOMPRESSEDTEXTURE ]->IsDefined() )
				return 1;
			if ( params[ HDRBASETEXTURE ]->IsDefined() )
				return 2;
		}
		return 0;
	}

	static int SkyTexture( int nMode )
	{
		// Explicit ints: the parameters are CShaderParam objects, BASETEXTURE an enum (MSVC refuses to mix them in ?:).
		if ( nMode == 1 )
			return (int)HDRCOMPRESSEDTEXTURE;
		if ( nMode == 2 )
			return (int)HDRBASETEXTURE;
		return (int)BASETEXTURE;
	}

	static bool IsFloatTexture( IMaterialVar *pVar )
	{
		if ( !pVar->IsTexture() )
			return false;
		const ImageFormat fmt = pVar->GetTextureValue()->GetImageFormat();
		return fmt == IMAGE_FORMAT_RGBA16161616F || fmt == IMAGE_FORMAT_RGBA16161616;
	}

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS( MATERIAL_VAR_NOFOG );
		SET_FLAGS( MATERIAL_VAR_IGNOREZ );
		if ( !params[ CLOUDPARAMS ]->IsDefined() )
			params[ CLOUDPARAMS ]->SetVecValue( 0.85f, 1.0f, 0.0f, 0.0f );
		if ( !params[ CLOUDSHAPE ]->IsDefined() )
			params[ CLOUDSHAPE ]->SetVecValue( 0.42f, 0.22f, 0.5f, 0.0f );
		if ( !params[ CLOUDSUN ]->IsDefined() )
			params[ CLOUDSUN ]->SetVecValue( 0.0f, 0.0f, 1.0f, 0.0f );
		if ( !params[ SUNDISC ]->IsDefined() )
			params[ SUNDISC ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
		if ( !params[ SUNHALO ]->IsDefined() )
			params[ SUNHALO ]->SetVecValue( 0.0f, 0.0f, 0.0f, 0.0f );
	}

	SHADER_FALLBACK
	{
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() )
			return "Sky";
		return 0;
	}

	SHADER_INIT
	{
		const int nMode = SkyMode( params );
		const int nTexture = SkyTexture( nMode );
		if ( !params[ nTexture ]->IsDefined() )
			return;
		if ( nMode == 1 )
		{
			LoadTexture( nTexture, 0 );		// RGBS is linear
		}
		else
		{
			// Load first to see the format: float textures are read linear
			LoadTexture( nTexture, TEXTUREFLAGS_SRGB );
			if ( IsFloatTexture( params[ nTexture ] ) )
				LoadTexture( nTexture, 0 );
		}
	}

	SHADER_DRAW
	{
		const int nMode = SkyMode( params );
		const int nTexture = SkyTexture( nMode );

		SHADOW_STATE
		{
			SetInitialShadowState();
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, nMode != 1 && !IsFloatTexture( params[ nTexture ] ) );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_sky_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_sky_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_sky_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_sky_ps30 );

			// we are writing linear values from this shader.
			pShaderShadow->EnableSRGBWrite( true );
			pShaderShadow->EnableAlphaWrites( true );
		}
		DYNAMIC_STATE
		{
			if ( params[ nTexture ]->IsTexture() )
				BindTexture( SHADER_SAMPLER0, nTexture, FRAME );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );

			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, BASETEXTURETRANSFORM );

			float c0[ 4 ] = { 1, 1, 1, 1 };
			if ( params[ COLOR ]->IsDefined() )
				memcpy( c0, params[ COLOR ]->GetVecValue(), 3 * sizeof( float ) );
			float c1[ 4 ] = { 0, 0, 1, 1 };
			if ( nMode == 1 && params[ nTexture ]->IsTexture() )
			{
				// As Sky_HDR_DX9: RGBS is filtered by hand, its scale is 8.
				ITexture *pTexture = params[ nTexture ]->GetTextureValue();
				const float w = pTexture->GetActualWidth();
				const float h = pTexture->GetActualHeight();
				const float flFudge = 0.01f / MAX( w, h );	// per ATI
				c1[ 0 ] = 0.5f / w - flFudge;
				c1[ 1 ] = 0.5f / h - flFudge;
				c1[ 2 ] = w;
				c1[ 3 ] = h;
				c0[ 0 ] *= 8.0f;
				c0[ 1 ] *= 8.0f;
				c0[ 2 ] *= 8.0f;
			}
			else if ( params[ nTexture ]->IsTexture() )
			{
				const ImageFormat fmt = params[ nTexture ]->GetTextureValue()->GetImageFormat();
				if ( fmt == IMAGE_FORMAT_RGBA16161616 || ( fmt == IMAGE_FORMAT_RGBA16161616F && g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER ) )
				{
					c0[ 0 ] *= 16.0f;
					c0[ 1 ] *= 16.0f;
					c0[ 2 ] *= 16.0f;
				}
			}
			pShaderAPI->SetPixelShaderConstant( 0, c0, 1 );
			pShaderAPI->SetPixelShaderConstant( 1, c1, 1 );

			float vClouds[ 4 ], vShape[ 4 ], vSun[ 4 ], vDisc[ 4 ], vHalo[ 4 ];
			params[ CLOUDPARAMS ]->GetVecValue( vClouds, 4 );
			params[ CLOUDSHAPE ]->GetVecValue( vShape, 4 );
			params[ CLOUDSUN ]->GetVecValue( vSun, 4 );
			params[ SUNDISC ]->GetVecValue( vDisc, 4 );
			params[ SUNHALO ]->GetVecValue( vHalo, 4 );
			pShaderAPI->SetPixelShaderConstant( 2, vClouds, 1 );
			pShaderAPI->SetPixelShaderConstant( 3, vShape, 1 );
			pShaderAPI->SetPixelShaderConstant( 4, vSun, 1 );
			pShaderAPI->SetPixelShaderConstant( 5, vDisc, 1 );
			pShaderAPI->SetPixelShaderConstant( 6, vHalo, 1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_sky_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_sky_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_sky_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( MODE, nMode );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( CLOUDS, vClouds[ 0 ] > 0.0f );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_sky_ps30 );
		}
		Draw();
	}

END_SHADER
