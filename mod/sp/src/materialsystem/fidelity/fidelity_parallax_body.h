//========= mapbase-fidelity ==================================================//
//
// Purpose: The body of the parallax shader, compiled twice by
//          fidelity_parallax.cpp: as Fidelity_Parallax and as
//          Fidelity_WorldVertexTransition (FIDELITY_PARALLAX_SHADER names it).
//          No include guard on purpose.
//
//=============================================================================//

BEGIN_VS_SHADER( FIDELITY_PARALLAX_SHADER, "World surface with parallax occlusion mapping (mapbase-fidelity)" )

	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Normal map." )
		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the normal map." )
		SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "Normal map texture coordinate transform." )
		SHADER_PARAM( PARALLAXMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Height map (white = top), in the normal map's coordinates." )
		SHADER_PARAM( PARALLAXDEPTH, SHADER_PARAM_TYPE_FLOAT, "0.01", "Depth of the relief, in normal map texture units (0.01 of its size)." )
		SHADER_PARAM( PARALLAXAO, SHADER_PARAM_TYPE_FLOAT, "0.25", "How much darker the deepest parts are, 0..1." )
		SHADER_PARAM( PARALLAXFADE, SHADER_PARAM_TYPE_FLOAT, "1000", "Distance (units) from which the relief fades out over the same again." )
		SHADER_PARAM( SSBUMP, SHADER_PARAM_TYPE_INTEGER, "0", "The bump map is self-shadowed (the three lightmap weights)." )
		SHADER_PARAM( PARALLAXMODULATE, SHADER_PARAM_TYPE_INTEGER, "0", "A decal multiplied onto the wall, as DecalModulate (bullet holes from the decal atlas)." )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Cubemap reflection (env_cubemap: the map's nearest)." )
		SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the cubemap." )
		SHADER_PARAM( ENVMAPMASK, SHADER_PARAM_TYPE_TEXTURE, "", "Where the wall reflects (only without a bump map, as LightmappedGeneric)." )
		SHADER_PARAM( ENVMAPMASKFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the reflection mask." )
		SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Colour of the reflection." )
		SHADER_PARAM( ENVMAPCONTRAST, SHADER_PARAM_TYPE_FLOAT, "0", "0 = normal, 1 = colour squared." )
		SHADER_PARAM( ENVMAPSATURATION, SHADER_PARAM_TYPE_FLOAT, "1", "0 = grey, 1 = its own colour." )
		SHADER_PARAM( FRESNELREFLECTION, SHADER_PARAM_TYPE_FLOAT, "1", "Reflection seen straight on (1 = no Fresnel)." )
		// Parallax-corrected cubemaps, as Mapbase's LightmappedGeneric (same names: VBSP writes them for a
		// parallax_obb, client.dll's fidelity_cubemaps.cpp for every env_cubemap in a room without one)
		SHADER_PARAM( ENVMAPPARALLAX, SHADER_PARAM_TYPE_INTEGER, "0", "1 = the reflection is corrected to the room's box." )
		SHADER_PARAM( ENVMAPPARALLAXOBB1, SHADER_PARAM_TYPE_VEC4, "[1 0 0 0]", "World to the box (0..1 inside), row 1." )
		SHADER_PARAM( ENVMAPPARALLAXOBB2, SHADER_PARAM_TYPE_VEC4, "[0 1 0 0]", "Row 2." )
		SHADER_PARAM( ENVMAPPARALLAXOBB3, SHADER_PARAM_TYPE_VEC4, "[0 0 1 0]", "Row 3." )
		SHADER_PARAM( ENVMAPORIGIN, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Where the env_cubemap is." )
		SHADER_PARAM( DETAIL, SHADER_PARAM_TYPE_TEXTURE, "", "Detail texture (base x detail x 2)." )
		SHADER_PARAM( DETAILFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the detail texture." )
		SHADER_PARAM( DETAILSCALE, SHADER_PARAM_TYPE_FLOAT, "4", "Size of the detail texture relative to the base texture." )
		SHADER_PARAM( DETAILBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1", "How much of the detail texture." )
		SHADER_PARAM( DETAILBLENDMODE, SHADER_PARAM_TYPE_INTEGER, "0", "0 = base x detail x 2, 1 = base + detail." )
		SHADER_PARAM( DETAILTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Colour of the detail texture." )
		SHADER_PARAM( SELFILLUMTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Colour of the self-illumination ($selfillum: the base texture's alpha)." )
		// Blended surfaces (WorldVertexTransition: displacements painted from one texture to the other)
		SHADER_PARAM( BASETEXTURE2, SHADER_PARAM_TYPE_TEXTURE, "", "Second base texture (painted blend: vertex alpha)." )
		SHADER_PARAM( FRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the second base texture." )
		SHADER_PARAM( BASETEXTURETRANSFORM2, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "Second base texture coordinate transform." )
		SHADER_PARAM( BUMPMAP2, SHADER_PARAM_TYPE_TEXTURE, "", "Second normal map (in the first normal map's coordinates, as WorldVertexTransition)." )
		SHADER_PARAM( BUMPFRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "Frame of the second normal map." )
		SHADER_PARAM( PARALLAXMAP2, SHADER_PARAM_TYPE_TEXTURE, "", "Second height map (in $bumpmap2's coordinates, without one in $basetexture2's)." )
		SHADER_PARAM( PARALLAXBLEND, SHADER_PARAM_TYPE_FLOAT, "0.2", "Height blend: how far the higher layer shows through near the painted transition (0 = plain cross-fade)." )
		SHADER_PARAM( BLENDMODULATETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Sharpens the painted blend (red: width, green: middle), as WorldVertexTransition." )
		SHADER_PARAM( BASETEXTURENOENVMAP, SHADER_PARAM_TYPE_BOOL, "0", "The first layer does not reflect." )
		SHADER_PARAM( BASETEXTURE2NOENVMAP, SHADER_PARAM_TYPE_BOOL, "0", "The second layer does not reflect." )
		SHADER_PARAM( ENVMAPWETONLY, SHADER_PARAM_TYPE_BOOL, "0", "The cubemap shows only where the wall is wet from rain (deploy.bat gives walls without one env_cubemap for that)." )
		SHADER_PARAM( RAINMAP, SHADER_PARAM_TYPE_TEXTURE, "fidelity/rain_map", "Where the rain reaches (client.dll's fidelity_wetness.cpp makes it)." )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// The flashlight texture, as LightmappedGeneric sets it
		params[ FLASHLIGHTTEXTURE ]->SetStringValue( g_pHardwareConfig->SupportsBorderColor() ? "effects/flashlight_border" : "effects/flashlight001" );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		if ( params[ BUMPMAP ]->IsDefined() )
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		SET_PARAM_INT_IF_NOT_DEFINED( BUMPFRAME, 0 );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( PARALLAXDEPTH, 0.01f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( PARALLAXAO, 0.25f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( PARALLAXFADE, 1000.0f );
		SET_PARAM_INT_IF_NOT_DEFINED( SSBUMP, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( PARALLAXMODULATE, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( ENVMAPFRAME, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( ENVMAPMASKFRAME, 0 );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( ENVMAPCONTRAST, 0.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( ENVMAPSATURATION, 1.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( FRESNELREFLECTION, 1.0f );
		SET_PARAM_INT_IF_NOT_DEFINED( DETAILFRAME, 0 );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( DETAILSCALE, 4.0f );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( DETAILBLENDFACTOR, 1.0f );
		SET_PARAM_INT_IF_NOT_DEFINED( DETAILBLENDMODE, 0 );
		if ( !params[ ENVMAPTINT ]->IsDefined() )
			params[ ENVMAPTINT ]->SetVecValue( 1.0f, 1.0f, 1.0f );
		if ( !params[ DETAILTINT ]->IsDefined() )
			params[ DETAILTINT ]->SetVecValue( 1.0f, 1.0f, 1.0f );
		if ( !params[ SELFILLUMTINT ]->IsDefined() )
			params[ SELFILLUMTINT ]->SetVecValue( 1.0f, 1.0f, 1.0f );
		SET_PARAM_INT_IF_NOT_DEFINED( FRAME2, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( BUMPFRAME2, 0 );
		SET_PARAM_FLOAT_IF_NOT_DEFINED( PARALLAXBLEND, 0.2f );
		SET_PARAM_INT_IF_NOT_DEFINED( BASETEXTURENOENVMAP, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( BASETEXTURE2NOENVMAP, 0 );
		SET_PARAM_INT_IF_NOT_DEFINED( ENVMAPWETONLY, 0 );
		SET_PARAM_STRING_IF_NOT_DEFINED( RAINMAP, "fidelity/rain_map" );
		// as WorldVertexTransition: a second normal map only with a first
		if ( !params[ BUMPMAP ]->IsDefined() )
			params[ BUMPMAP2 ]->SetUndefined();
		if ( !params[ BASETEXTURE2 ]->IsDefined() )
		{
			params[ BUMPMAP2 ]->SetUndefined();
			params[ PARALLAXMAP2 ]->SetUndefined();
			params[ BLENDMODULATETEXTURE ]->SetUndefined();
		}

		// As LightmappedGeneric: env_cubemap only means something once VBSP
		// put the map's cubemaps in; no reflections with mat_specular 0
		if ( params[ ENVMAP ]->IsDefined() && !CanUseEditorMaterials() && !Q_stricmp( params[ ENVMAP ]->GetStringValue(), "env_cubemap" ) )
			params[ ENVMAP ]->SetUndefined();
		if ( params[ ENVMAP ]->IsDefined() && !g_pConfig->UseSpecular() && params[ BASETEXTURE ]->IsDefined() )
			params[ ENVMAP ]->SetUndefined();
		if ( !params[ BASETEXTURE ]->IsDefined() )
			CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
		if ( params[ BUMPMAP ]->IsDefined() || !params[ ENVMAP ]->IsDefined() )
			params[ ENVMAPMASK ]->SetUndefined();
		// modes 0 and 1 are drawn here (LightmappedGeneric itself draws only
		// 0, 1, 10 and 11); deploy.bat leaves the others alone
		if ( params[ DETAILBLENDMODE ]->GetIntValue() != 0 && params[ DETAILBLENDMODE ]->GetIntValue() != 1 )
			params[ DETAIL ]->SetUndefined();
	}

	SHADER_FALLBACK
	{
		// (without a bump map the height map comes from the base texture)
		// (a blended surface: WorldVertexTransition - also for VBSP, which
		// gives blend materials on brushes, not displacements, a plain copy
		// only when the shader's name says WorldVertexTransition)
		if ( !g_pHardwareConfig->SupportsShaderModel_3_0() || ( !params[ BUMPMAP ]->IsDefined() && !params[ PARALLAXMAP ]->IsDefined() ) )
		{
			if ( params[ BASETEXTURE2 ]->IsDefined() )
				return "WorldVertexTransition";
			return "LightmappedGeneric";
		}
		return 0;
	}

	SHADER_INIT
	{
		if ( params[ BASETEXTURE ]->IsDefined() )
		{
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
			// as LightmappedGeneric: a base texture without alpha masks nothing
			// (else $selfillum lit the whole wall and the reflection was gone)
			ITexture *pBase = params[ BASETEXTURE ]->GetTextureValue();
			if ( pBase && !pBase->IsTranslucent() )
			{
				CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
				CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
			}
		}
		if ( params[ BUMPMAP ]->IsDefined() )
			LoadBumpMap( BUMPMAP );
		if ( params[ PARALLAXMAP ]->IsDefined() )
			LoadTexture( PARALLAXMAP );
		LoadTexture( FLASHLIGHTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ ENVMAP ]->IsDefined() )
			LoadCubeMap( ENVMAP, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0 );
		if ( params[ DETAIL ]->IsDefined() )
			LoadTexture( DETAIL, params[ DETAILBLENDMODE ]->GetIntValue() == 1 ? TEXTUREFLAGS_SRGB : 0 );	// (added in linear light, as LightmappedGeneric)
		if ( params[ ENVMAPMASK ]->IsDefined() )
			LoadTexture( ENVMAPMASK );
		if ( params[ BASETEXTURE2 ]->IsDefined() )
			LoadTexture( BASETEXTURE2, TEXTUREFLAGS_SRGB );
		if ( params[ BUMPMAP2 ]->IsDefined() )
			LoadBumpMap( BUMPMAP2 );
		if ( params[ PARALLAXMAP2 ]->IsDefined() )
			LoadTexture( PARALLAXMAP2 );
		if ( params[ BLENDMODULATETEXTURE ]->IsDefined() )
			LoadTexture( BLENDMODULATETEXTURE );
		if ( params[ RAINMAP ]->IsDefined() )
			LoadTexture( RAINMAP );
	}

	// A step of (1, 0) and of (0, 1) in the coordinates of texture transform
	// nFrom, in those of nTo (x, y and z, w): A_to x inverse( A_from ), the
	// transforms' 2 x 2 parts (the offsets do not move a step)
	void LayerStep( IMaterialVar **params, int nFrom, int nTo, float vOut[ 4 ] )
	{
		const VMatrix &from = params[ nFrom ]->GetMatrixValue();
		const VMatrix &to = params[ nTo ]->GetMatrixValue();
		const float flDet = from[ 0 ][ 0 ] * from[ 1 ][ 1 ] - from[ 0 ][ 1 ] * from[ 1 ][ 0 ];
		if ( fabsf( flDet ) < 1e-8f )
		{
			vOut[ 0 ] = 1.0f; vOut[ 1 ] = 0.0f; vOut[ 2 ] = 0.0f; vOut[ 3 ] = 1.0f;
			return;
		}
		// inverse of [ a b ; c d ] = [ d -b ; -c a ] / det
		const float i00 = from[ 1 ][ 1 ] / flDet, i01 = -from[ 0 ][ 1 ] / flDet;
		const float i10 = -from[ 1 ][ 0 ] / flDet, i11 = from[ 0 ][ 0 ] / flDet;
		// columns of to x inverse
		vOut[ 0 ] = to[ 0 ][ 0 ] * i00 + to[ 0 ][ 1 ] * i10;
		vOut[ 1 ] = to[ 1 ][ 0 ] * i00 + to[ 1 ][ 1 ] * i10;
		vOut[ 2 ] = to[ 0 ][ 0 ] * i01 + to[ 0 ][ 1 ] * i11;
		vOut[ 3 ] = to[ 1 ][ 0 ] * i01 + to[ 1 ][ 1 ] * i11;
	}

	// A blended surface: the second base texture is there
	bool IsBlended( IMaterialVar **params )
	{
		return params[ BASETEXTURE2 ]->IsTexture();
	}

	//-------------------------------------------------------------------------
	// The second layer of a blended surface, both passes: its base texture
	// (sBase2), normal map or flat (sBump2), height map or white (sHeight2),
	// blend modulation (sModulate); c20..c22, the vertex shader's transforms
	// (constants 8 and 10); and both the self-illumination (c23) and the
	// detail mode (c24), which every surface sets
	//-------------------------------------------------------------------------
	void BindLayer2( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, Sampler_t sBase2, Sampler_t sBump2, Sampler_t sHeight2, Sampler_t sModulate )
	{
		const bool bBlended = IsBlended( params );
		const bool bBump2 = bBlended && params[ BUMPMAP2 ]->IsTexture();
		const bool bHeight2 = bBlended && params[ PARALLAXMAP2 ]->IsTexture();
		// (as WorldVertexTransition: no sharpening on self-illuminated surfaces)
		const bool bModulate = bBlended && params[ BLENDMODULATETEXTURE ]->IsTexture() && !IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
		if ( bBlended )
			BindTexture( sBase2, BASETEXTURE2, FRAME2 );
		else
			pShaderAPI->BindStandardTexture( sBase2, TEXTURE_WHITE );
		if ( bBump2 )
			BindTexture( sBump2, BUMPMAP2, BUMPFRAME2 );
		else
			pShaderAPI->BindStandardTexture( sBump2, TEXTURE_NORMALMAP_FLAT );
		if ( bHeight2 )
			BindTexture( sHeight2, PARALLAXMAP2 );
		else
			pShaderAPI->BindStandardTexture( sHeight2, TEXTURE_WHITE );
		if ( bModulate )
			BindTexture( sModulate, BLENDMODULATETEXTURE );
		else
			pShaderAPI->BindStandardTexture( sModulate, TEXTURE_WHITE );

		// The second base texture's coordinates, and those of its height and
		// normal map: with $bumpmap2 the first normal map's (as
		// WorldVertexTransition), else the second base texture's
		// (no ?: - MSVC rejects it with two shader parameters)
		const bool bTransform2 = bBlended && params[ BASETEXTURETRANSFORM2 ]->IsDefined();
		if ( bTransform2 )
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, BASETEXTURETRANSFORM2 );
		else
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_8, BASETEXTURETRANSFORM );
		if ( bBump2 )
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, BUMPTRANSFORM );
		else if ( bTransform2 )
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, BASETEXTURETRANSFORM2 );
		else
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, BASETEXTURETRANSFORM );

		// Hammer draws displacement blends reversed (Mapbase's fix, as in its LightmappedGeneric)
		// (height blend at most 0.5: above it the second layer showed where only the first is painted)
		const float vBlend[ 4 ] = { bBlended ? 1.0f : 0.0f, bModulate ? 1.0f : 0.0f, ( bBlended && UsingEditor( params ) ) ? 1.0f : 0.0f,
			bHeight2 ? clamp( params[ PARALLAXBLEND ]->GetFloatValue(), 0.0f, 0.5f ) : 0.0f };
		pShaderAPI->SetPixelShaderConstant( 20, vBlend, 1 );
		const float vLayer2[ 4 ] = { bBump2 ? 1.0f : 0.0f, ( bBlended && params[ BASETEXTURENOENVMAP ]->GetIntValue() != 0 ) ? 1.0f : 0.0f,
			( bBlended && params[ BASETEXTURE2NOENVMAP ]->GetIntValue() != 0 ) ? 1.0f : 0.0f, bHeight2 ? 1.0f : 0.0f };
		pShaderAPI->SetPixelShaderConstant( 21, vLayer2, 1 );
		float vHeight2Texel[ 4 ] = { 1.0f / 512.0f, 1.0f / 512.0f, 0.0f, 0.0f };
		if ( bHeight2 )
		{
			ITexture *pHeight2 = params[ PARALLAXMAP2 ]->GetTextureValue();
			vHeight2Texel[ 0 ] = 1.0f / MAX( pHeight2->GetActualWidth(), 1 );
			vHeight2Texel[ 1 ] = 1.0f / MAX( pHeight2->GetActualHeight(), 1 );
		}
		pShaderAPI->SetPixelShaderConstant( 22, vHeight2Texel, 1 );

		// How a step in the first height map's coordinates moves in the second
		// layer's (c25) and its base texture's (c26): all come from the same
		// base coordinate through their transforms, so it is one matrix per
		// material - per pixel it cost the shader four derivatives and a
		// division (no ?: - MSVC rejects it with two shader parameters)
		int nFrom = BASETEXTURETRANSFORM;
		if ( params[ BUMPMAP ]->IsTexture() )
			nFrom = BUMPTRANSFORM;
		int nBase2 = BASETEXTURETRANSFORM;
		if ( bTransform2 )
			nBase2 = BASETEXTURETRANSFORM2;
		int nHeight2 = nBase2;
		if ( bBump2 )
			nHeight2 = BUMPTRANSFORM;
		float vToHeight2[ 4 ], vToBase2[ 4 ];
		LayerStep( params, nFrom, nHeight2, vToHeight2 );
		LayerStep( params, nFrom, nBase2, vToBase2 );
		pShaderAPI->SetPixelShaderConstant( 25, vToHeight2, 1 );
		pShaderAPI->SetPixelShaderConstant( 26, vToBase2, 1 );

		float vSelfIllum[ 4 ] = { 1.0f, 1.0f, 1.0f, IS_FLAG_SET( MATERIAL_VAR_SELFILLUM ) ? 1.0f : 0.0f };
		params[ SELFILLUMTINT ]->GetVecValue( vSelfIllum, 3 );
		pShaderAPI->SetPixelShaderConstant( 23, vSelfIllum, 1 );
		const float vDetailMode[ 4 ] = { params[ DETAILBLENDMODE ]->GetIntValue() == 1 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
		pShaderAPI->SetPixelShaderConstant( 24, vDetailMode, 1 );
	}

	//-------------------------------------------------------------------------
	// Textures and constants both passes share: base texture (sBase), normal
	// map or flat (sBump), height map (sHeight), detail texture or grey
	// (sDetail); c1 parallax, c4 (.z ssbump, .w bumped), detail (nDetailReg),
	// c6 detail scale, c7 one texel of the height map, c9 reference plane
	// and most shift; the texture
	// transforms of the vertex shader (constants 0 and 2).
	//-------------------------------------------------------------------------
	void BindShared( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, Sampler_t sBase, Sampler_t sBump, Sampler_t sHeight,
		Sampler_t sDetail, int nDetailReg, float flSaturation, float flFresnel )
	{
		if ( params[ BASETEXTURE ]->IsTexture() )
			BindTexture( sBase, BASETEXTURE, FRAME );
		else
			pShaderAPI->BindStandardTexture( sBase, TEXTURE_WHITE );
		// Without a bump map: one plain lightmap, the normal from the height map
		const bool bBumped = params[ BUMPMAP ]->IsTexture();
		if ( bBumped )
			BindTexture( sBump, BUMPMAP, BUMPFRAME );
		else
			pShaderAPI->BindStandardTexture( sBump, TEXTURE_NORMALMAP_FLAT );
		const bool bHeight = params[ PARALLAXMAP ]->IsTexture();
		if ( bHeight )
			BindTexture( sHeight, PARALLAXMAP );
		else
			pShaderAPI->BindStandardTexture( sHeight, TEXTURE_WHITE );
		const bool bDetail = params[ DETAIL ]->IsTexture();
		if ( bDetail )
			BindTexture( sDetail, DETAIL, DETAILFRAME );
		else
			pShaderAPI->BindStandardTexture( sDetail, TEXTURE_GREY );

		SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, BASETEXTURETRANSFORM );
		// the height map lies in the bump map's coordinates - without one it
		// was made from the base texture and lies in its (a scaled wall texture
		// had its relief at another scale)
		// (no ?: - MSVC rejects it with two shader parameters)
		if ( bBumped )
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, BUMPTRANSFORM );
		else
			SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, BASETEXTURETRANSFORM );

		const float flFade = MAX( params[ PARALLAXFADE ]->GetFloatValue(), 1.0f );
		const float vParallax[ 4 ] = { bHeight ? MAX( params[ PARALLAXDEPTH ]->GetFloatValue(), 0.0f ) : 0.0f,
			clamp( params[ PARALLAXAO ]->GetFloatValue(), 0.0f, 1.0f ), flFade, 1.0f / flFade };
		pShaderAPI->SetPixelShaderConstant( 1, vParallax, 1 );
		const float vOptions[ 4 ] = { flSaturation, flFresnel, params[ SSBUMP ]->GetIntValue() != 0 ? 1.0f : 0.0f, bBumped ? 1.0f : 0.0f };
		pShaderAPI->SetPixelShaderConstant( 4, vOptions, 1 );
		float vDetail[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f };
		params[ DETAILTINT ]->GetVecValue( vDetail, 3 );
		vDetail[ 3 ] = bDetail ? params[ DETAILBLENDFACTOR ]->GetFloatValue() : 0.0f;
		pShaderAPI->SetPixelShaderConstant( nDetailReg, vDetail, 1 );
		// $detailscale: one number or two (u, v)
		float vDetailScale[ 4 ] = { 4.0f, 4.0f, 0.0f, 0.0f };
		if ( params[ DETAILSCALE ]->GetType() == MATERIAL_VAR_TYPE_VECTOR )
			params[ DETAILSCALE ]->GetVecValue( vDetailScale, 2 );
		else
			vDetailScale[ 0 ] = vDetailScale[ 1 ] = params[ DETAILSCALE ]->GetFloatValue();
		pShaderAPI->SetPixelShaderConstant( 6, vDetailScale, 1 );
		// one texel of the height map (its slopes give the normal without a bump map)
		float vHeightTexel[ 4 ] = { 1.0f / 512.0f, 1.0f / 512.0f, 0.0f, 0.0f };
		if ( bHeight )
		{
			ITexture *pHeight = params[ PARALLAXMAP ]->GetTextureValue();
			vHeightTexel[ 0 ] = 1.0f / MAX( pHeight->GetActualWidth(), 1 );
			vHeightTexel[ 1 ] = 1.0f / MAX( pHeight->GetActualHeight(), 1 );
		}
		pShaderAPI->SetPixelShaderConstant( 7, vHeightTexel, 1 );
		// reference plane (walls, floors) and most shift - decals keep the top
		// on the wall, or they would slide off where they were shot
		float vPlane[ 4 ] = { 1.0f, 1.0f, MAX( r_fidelity_parallax_maxshift.GetFloat(), 0.0f ), 0.0f };
		if ( !IS_FLAG_SET( MATERIAL_VAR_DECAL ) && params[ PARALLAXMODULATE ]->GetIntValue() == 0 )
		{
			vPlane[ 0 ] = clamp( r_fidelity_parallax_plane.GetFloat(), 0.0f, 1.0f );
			vPlane[ 1 ] = clamp( r_fidelity_parallax_plane_floor.GetFloat(), 0.0f, 1.0f );
		}
		pShaderAPI->SetPixelShaderConstant( 9, vPlane, 1 );

		float vEyePos[ 4 ] = { 0, 0, 0, 0 };
		pShaderAPI->GetWorldSpaceCameraPosition( vEyePos );
		pShaderAPI->SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos, 1 );
		pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );
	}

	//-------------------------------------------------------------------------
	// The light pass (flashlight, projected textures, the sun's shadows),
	// added on top, as Mapbase's DrawFlashlight_dx90 sets it up
	//-------------------------------------------------------------------------
	void DrawLightPass( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow )
	{
		SHADOW_STATE
		{
			SetInitialShadowState();
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
			SetAdditiveBlendingShadowState( BASETEXTURE, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// the light's texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// base texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );		// normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );		// height map
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );		// shadow noise
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );		// shadow map
			pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER7 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );		// detail texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, params[ DETAILBLENDMODE ]->GetIntValue() == 1 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );		// blended: second base texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER9, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER10, true );		// blended: second normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER11, true );		// blended: second height map
			pShaderShadow->EnableTexture( SHADER_SAMPLER12, true );		// blended: blend modulation
			pShaderShadow->EnableSRGBWrite( true );
			// blended: the painted blend in the vertex colour's alpha
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | ( IsBlended( params ) ? VERTEX_COLOR : 0 ), 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_parallax_flashlight_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_parallax_flashlight_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_parallax_flashlight_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_parallax_flashlight_ps30 );
			FogToBlack();
		}
		DYNAMIC_STATE
		{
			VMatrix worldToTexture;
			ITexture *pDepthTexture = NULL;
			const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pDepthTexture );
			if ( !pDepthTexture )
			{
				// as Mapbase: the shadow map of this light's quality
				const int nShadowIndex = ( state.m_nShadowQuality >> 16 ) - 1;
				if ( nShadowIndex >= 0 && nShadowIndex <= ( INT_FLASHLIGHT_DEPTHTEXTURE_FALLBACK_LAST - INT_FLASHLIGHT_DEPTHTEXTURE_FALLBACK_FIRST ) )
					pDepthTexture = (ITexture *)pShaderAPI->GetIntRenderingParameter( INT_FLASHLIGHT_DEPTHTEXTURE_FALLBACK_FIRST + nShadowIndex );
			}

			const float vLightPos[ 4 ] = { state.m_vecLightOrigin[ 0 ], state.m_vecLightOrigin[ 1 ], state.m_vecLightOrigin[ 2 ], 0.0f };
			pShaderAPI->SetPixelShaderConstant( PSREG_FRESNEL_SPEC_PARAMS, vLightPos, 1 );
			SetFlashLightColorFromState( state, pShaderAPI );
			BindTexture( SHADER_SAMPLER0, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );

			const bool bShadows = pDepthTexture && g_pConfig->ShadowDepthTexture() && state.m_bEnableShadows;
			if ( bShadows )
			{
				BindTexture( SHADER_SAMPLER7, pDepthTexture, 0 );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_SHADOW_NOISE_2D );
				float vTweaks[ 4 ];
				vTweaks[ 0 ] = ShadowFilterFromState( state );
				vTweaks[ 1 ] = ShadowAttenFromState( state );
				HashShadow2DJitter( state.m_flShadowJitterSeed, &vTweaks[ 2 ], &vTweaks[ 3 ] );
				pShaderAPI->SetPixelShaderConstant( PSREG_ENVMAP_TINT__SHADOW_TWEAKS, vTweaks, 1 );
				int nWidth, nHeight;
				pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
				const float vScreenScale[ 4 ] = { nWidth / 32.0f, nHeight / 32.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_SCREEN_SCALE, vScreenScale, 1 );
			}

			BindShared( params, pShaderAPI, SHADER_SAMPLER1, SHADER_SAMPLER2, SHADER_SAMPLER3, SHADER_SAMPLER8, 0, 1.0f, 1.0f );
			BindLayer2( params, pShaderAPI, SHADER_SAMPLER9, SHADER_SAMPLER10, SHADER_SAMPLER11, SHADER_SAMPLER12 );
			pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, worldToTexture.Base(), 4 );
			const float vAtten[ 4 ] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
			pShaderAPI->SetPixelShaderConstant( PSREG_FLASHLIGHT_ATTENUATION, vAtten, 1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_parallax_flashlight_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( BLEND, IsBlended( params ) );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_parallax_flashlight_vs30 );
			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_parallax_flashlight_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, bShadows );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, clamp( g_pHardwareConfig->GetShadowFilterMode(), 0, 2 ) );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( BLEND, IsBlended( params ) );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_parallax_flashlight_ps30 );
		}
		Draw();
	}

	SHADER_DRAW
	{
		// A modulate decal (bullet holes from the decal atlas) only darkens the
		// wall: the light lands on the wall below it, no light pass (as Mapbase's DecalModulate)
		const bool bModulate = params[ PARALLAXMODULATE ]->GetIntValue() != 0;
		if ( bModulate && UsingFlashlight( params ) )
			return;

		// Flashlight, projected textures, the sun's shadows: a pass with the same shift
		if ( UsingFlashlight( params ) )
		{
			DrawLightPass( params, pShaderAPI, pShaderShadow );
			return;
		}

		SHADOW_STATE
		{
			SetInitialShadowState();
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );		// base texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, !bModulate );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );		// lightmap
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );		// normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );		// height map
			pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );		// cubemap
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE );
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );		// detail texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, params[ DETAILBLENDMODE ]->GetIntValue() == 1 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );		// reflection mask
			pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );		// blended: second base texture
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, !bModulate );
			pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );		// blended: second normal map
			pShaderShadow->EnableTexture( SHADER_SAMPLER9, true );		// blended: second height map
			pShaderShadow->EnableTexture( SHADER_SAMPLER10, true );		// blended: blend modulation
			pShaderShadow->EnableTexture( SHADER_SAMPLER11, true );		// where the rain reaches (data)
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER11, false );
			if ( bModulate )
			{
				// as DecalModulate: wall x decal x 2 (grey changes nothing), in
				// gamma space, only where the decal is, fog grey
				pShaderShadow->EnableSRGBWrite( false );
				pShaderShadow->EnableAlphaTest( true );
				pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.0f );
				pShaderShadow->EnableDepthWrites( false );
				pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_DECAL );
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_DST_COLOR, SHADER_BLEND_SRC_COLOR );
				pShaderShadow->DisableFogGammaCorrection( true );
				pShaderShadow->EnableAlphaWrites( false );
			}
			else
			{
				pShaderShadow->EnableSRGBWrite( true );
				// Bullet holes (decals, $translucent) blend as LightmappedGeneric;
				// walls write their depth into alpha (SSAO, light shafts)
				SetDefaultBlendingShadowState( BASETEXTURE, true );
				pShaderShadow->EnableAlphaWrites( !IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) );
			}

			// texcoord 0: base, 1: lightmap, 2: lightmap offset (bumped lightmaps);
			// blended: the painted blend in the vertex colour's alpha
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T | ( IsBlended( params ) ? VERTEX_COLOR : 0 ), 3, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( fidelity_parallax_vs30 );
			SET_STATIC_VERTEX_SHADER( fidelity_parallax_vs30 );
			DECLARE_STATIC_PIXEL_SHADER( fidelity_parallax_ps30 );
			SET_STATIC_PIXEL_SHADER( fidelity_parallax_ps30 );

			if ( bModulate )
				FogToGrey();
			else
				DefaultFog();
		}
		DYNAMIC_STATE
		{
			BindShared( params, pShaderAPI, SHADER_SAMPLER0, SHADER_SAMPLER2, SHADER_SAMPLER3, SHADER_SAMPLER5, 5,
				params[ ENVMAPSATURATION ]->GetFloatValue(), params[ FRESNELREFLECTION ]->GetFloatValue() );
			pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_LIGHTMAP );
			BindLayer2( params, pShaderAPI, SHADER_SAMPLER7, SHADER_SAMPLER8, SHADER_SAMPLER9, SHADER_SAMPLER10 );

			// Reflection: a neutral cube and switched off by the constants when the material has none
			const bool bEnvmap = params[ ENVMAP ]->IsTexture();
			if ( bEnvmap )
				BindTexture( SHADER_SAMPLER4, ENVMAP, ENVMAPFRAME );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER4, TEXTURE_NORMALIZATION_CUBEMAP );
			if ( params[ ENVMAPMASK ]->IsTexture() )
				BindTexture( SHADER_SAMPLER6, ENVMAPMASK, ENVMAPMASKFRAME );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_WHITE );

			// Modulation colour times the lightmap scale (as LightmappedGeneric)
			float vTint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
			ComputeModulationColor( vTint );
			const float flLightmapScale = pShaderAPI->GetLightMapScaleFactor();
			vTint[ 0 ] *= flLightmapScale;
			vTint[ 1 ] *= flLightmapScale;
			vTint[ 2 ] *= flLightmapScale;
			pShaderAPI->SetPixelShaderConstant( 0, vTint, 1 );

			const float vModulate[ 4 ] = { bModulate ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 8, vModulate, 1 );
			SetEnvMapTintPixelShaderDynamicState( 2, ENVMAPTINT, -1 );	// (black with mat_specular 0, mat_fullbright 2)
			const bool bWetOnly = params[ ENVMAPWETONLY ]->GetIntValue() != 0;
			const float vEnvmap[ 4 ] = { bEnvmap && !bWetOnly ? 1.0f : 0.0f, IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK ) ? 1.0f : 0.0f,
				IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK ) ? 1.0f : 0.0f, params[ ENVMAPCONTRAST ]->GetFloatValue() };
			pShaderAPI->SetPixelShaderConstant( 3, vEnvmap, 1 );
			// parallax-corrected cubemap: c13 its place and on, c14..c16 the box
			float vCubemapPos[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float vBox[ 12 ] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
			if ( bEnvmap && params[ ENVMAPPARALLAX ]->GetIntValue() > 0 )
			{
				params[ ENVMAPORIGIN ]->GetVecValue( vCubemapPos, 3 );
				vCubemapPos[ 3 ] = 1.0f;
				params[ ENVMAPPARALLAXOBB1 ]->GetVecValue( &vBox[ 0 ], 4 );
				params[ ENVMAPPARALLAXOBB2 ]->GetVecValue( &vBox[ 4 ], 4 );
				params[ ENVMAPPARALLAXOBB3 ]->GetVecValue( &vBox[ 8 ], 4 );
			}
			pShaderAPI->SetPixelShaderConstant( 13, vCubemapPos, 1 );
			pShaderAPI->SetPixelShaderConstant( 14, vBox, 3 );
			// relief shadows from the baked light (not on decals: they sit on the wall's own light)
			const float vBakedShadow[ 4 ] = { bModulate ? 0.0f : clamp( r_fidelity_parallax_bakedshadow.GetFloat(), 0.0f, 1.0f ), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 27, vBakedShadow, 1 );
			// the nearest dynamic lights from client.dll (fidelity_dynlights.cpp): c32.. position and
			// radius, colour; none on decals (they sit on the wall's own light)
			float vLights[ 4 * 2 * FIDELITY_DYNAMIC_LIGHTS ] = {};
			for ( int n = 0; n < FIDELITY_DYNAMIC_LIGHTS && !bModulate; n++ )
			{
				const Vector vecPos = pShaderAPI->GetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * n );
				const Vector vecColor = pShaderAPI->GetVectorRenderingParameter( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * n + 1 );
				float *pLight = &vLights[ 8 * n ];
				pLight[ 0 ] = vecPos.x;
				pLight[ 1 ] = vecPos.y;
				pLight[ 2 ] = vecPos.z;
				pLight[ 3 ] = pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII + n );
				pLight[ 4 ] = vecColor.x;
				pLight[ 5 ] = vecColor.y;
				pLight[ 6 ] = vecColor.z;
			}
			pShaderAPI->SetPixelShaderConstant( 32, vLights, 2 * FIDELITY_DYNAMIC_LIGHTS );
			// rain (fidelity_wetness.cpp): c40 how wet, the rain map's corner, 1 / its size; c41 its heights,
			// the cubemap only for wet, a cubemap there; none on decals
			const float flRainSize = pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_SIZE );
			const bool bRainMap = params[ RAINMAP ]->IsTexture() && flRainSize > 0.0f && !bModulate;
			if ( bRainMap )
				BindTexture( SHADER_SAMPLER11, RAINMAP );
			else
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER11, TEXTURE_BLACK );
			const float vWet[ 8 ] = {
				bRainMap ? clamp( pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_WETNESS ), 0.0f, 1.0f ) : 0.0f,
				pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_X ),
				pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_Y ),
				bRainMap ? 1.0f / flRainSize : 0.0f,
				pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_BASE ),
				pShaderAPI->GetFloatRenderingParameter( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE ),
				bWetOnly ? 1.0f : 0.0f,
				bEnvmap ? 1.0f : 0.0f };
			pShaderAPI->SetPixelShaderConstant( 40, vWet, 2 );

			DECLARE_DYNAMIC_VERTEX_SHADER( fidelity_parallax_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( BLEND, IsBlended( params ) );
			SET_DYNAMIC_VERTEX_SHADER( fidelity_parallax_vs30 );
			DECLARE_DYNAMIC_PIXEL_SHADER( fidelity_parallax_ps30 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, pShaderAPI->ShouldWriteDepthToDestAlpha() && !IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) && !bModulate );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( BLEND, IsBlended( params ) );
			SET_DYNAMIC_PIXEL_SHADER( fidelity_parallax_ps30 );
		}
		Draw();
	}

END_SHADER
