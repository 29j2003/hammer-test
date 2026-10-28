//========= mapbase-fidelity ==================================================//
//
// Purpose: Steadier, softer sun shadows from Mapbase's env_global_light, and
//          the same shadow filter size for env_projectedtexture.
//
// env_global_light renders one orthographic shadow map around the player and
// moves it with every step. Two things made its shadows look worse than they
// need to, both independent of where the player looks:
//
//   1. The shadow map slides continuously over the world, so its texel grid
//      moves and shadow edges crawl and shimmer while walking. Snapping the
//      light's position to whole texels in its image plane keeps the grid
//      fixed in the world (the usual "stable shadow map" technique).
//   2. The soft shadow filter is sized as filter size / map resolution, and
//      Mapbase passes 8192 as the resolution (cl_globallight_depthres), while
//      the real map has r_flashlightdepthres texels (2048 by default). The
//      filter was a quarter texel wide: hard, stair-stepped edges. Here it
//      gets the real resolution and a filter size in texels.
//
// Both only change the frustum position by less than one texel and the filter
// width; what casts and receives shadows stays the same.
//
// Cascades (r_fidelity_sunshadow_cascade): one shadow map has to cover the
// whole env_global_light area ("Ortho size"), so near the player it is coarse.
// The env_global_light's own shadow becomes the near cascade, a quarter of the
// size (four times sharper), and a second shadow light here covers the full
// area. Source adds the light of both, so they must not overlap: each gets a
// mask texture made at run time ("cookie", the flashlight texture). The near
// one fades out towards its edge, the far one has a hole exactly there with
// the opposite fade - together always exactly one sun. Both snap to the far
// cascade's texel grid, so masks and grids stay aligned while walking. The
// masks replace the default border texture (effects/flashlight_border); maps
// with their own texture (cloud shadows ...) keep one cascade.
//
// Clouds drifting in front of the sun (fidelity_sky.cpp) dim the sun's light,
// and so its shadows, by up to 15 %, over seconds.
//
// The shadows start SUN_DEPTH_BIAS_UNITS behind their caster in both
// cascades: with the engine's tiny bias a sloped surface shadowed itself in
// the sharp near cascade (it looked different close up than from afar).
//
// Cost: a second shadow map render and a second sun lighting pass over the
// far area. One of Mapbase's five shadow depth textures (-numshadowtextures).
//
// env_projectedtexture has the second problem too: it sets the filter size
// from its "Shadow Filter" keyvalue but never the resolution, which stays at
// the FlashlightState_t default of 2048 while the maps have 4096 texels
// (fidelity.cfg). Its edges came out twice as wide as the flashlight's for the
// same value. With the real resolution, the keyvalue means texels for both.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_shadows.h"
#include "fidelity_sky.h"
#include "fidelity_godrays.h"
#include "igamesystem.h"
#include "iclientshadowmgr.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_sunshadow( "r_fidelity_sunshadow", "1", FCVAR_ARCHIVE, "Steadier, softer sun shadows from env_global_light (fixed texel grid, correct filter size). 0 = Mapbase default." );
ConVar r_fidelity_projshadow( "r_fidelity_projshadow", "1", FCVAR_ARCHIVE, "env_projectedtexture shadow filter uses the real shadow map resolution (Shadow Filter = texels). 0 = Mapbase default." );
ConVar r_fidelity_sunshadow_softness( "r_fidelity_sunshadow_softness", "1.2", FCVAR_SERVER_CAN_EXECUTE, "Width of the sun shadow edge filter in shadow map texels." );
ConVar r_fidelity_sun_follow( "r_fidelity_sun_follow", "1", FCVAR_ARCHIVE, "The sun's light and shadows (env_global_light) come from where env_sun is in the sky. 0 = env_global_light's own direction." );
ConVar r_fidelity_sunshadow_cascade( "r_fidelity_sunshadow_cascade", "1", FCVAR_ARCHIVE, "Sun shadows in two cascades: four times sharper near the player. 0 = one shadow map (cheaper)." );

extern ConVar r_flashlightdepthres;

// The near cascade covers 1 / CASCADE_RATIO of the far one (per side).
#define CASCADE_RATIO		4.0f
// Part of the near cascade (from its edge) where it fades into the far one.
#define CASCADE_BLEND		0.2f
// Part of the far cascade (from its edge) where the sun fades out, like
// effects/flashlight_border.
#define CASCADE_BORDER		0.1f
// The sun's shadows start this many units behind what casts them (see Fidelity_AdjustGlobalLight)
#define SUN_DEPTH_BIAS_UNITS	1.5f

//-----------------------------------------------------------------------------
// The cascade masks: near = 1 inside, fading to 0 at its edge; far = the
// opposite of that (in its own, larger coordinates) and a soft outer border.
// Written gamma encoded, because the shaders read flashlight textures as sRGB:
// after decoding, near + far is exactly 1 everywhere.
//-----------------------------------------------------------------------------
static float Fidelity_NearCascadeWeight( float t )	// t: 0 centre .. 1 edge of the near cascade
{
	if ( t >= 1.0f )
		return 0.0f;
	if ( t <= 1.0f - CASCADE_BLEND )
		return 1.0f;
	return ( 1.0f - t ) / CASCADE_BLEND;
}

class CFidelityCascadeMask : public ITextureRegenerator
{
public:
	explicit CFidelityCascadeMask( bool bFar ) : m_bFar( bFar ) {}

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		const int nWidth = pVTFTexture->Width();
		const int nHeight = pVTFTexture->Height();
		const int nBytes = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		unsigned char *pData = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pData || nBytes < 3 )
			return;
		for ( int y = 0; y < nHeight; y++ )
		{
			for ( int x = 0; x < nWidth; x++ )
			{
				// Distance from the centre, 1 at the edge of this cascade (square)
				const float u = ( ( x + 0.5f ) / nWidth ) * 2.0f - 1.0f;
				const float v = ( ( y + 0.5f ) / nHeight ) * 2.0f - 1.0f;
				const float t = MAX( fabsf( u ), fabsf( v ) );
				float flWeight;
				if ( m_bFar )
				{
					const float flBorder = clamp( ( 1.0f - t ) / CASCADE_BORDER, 0.0f, 1.0f );
					flWeight = ( 1.0f - Fidelity_NearCascadeWeight( t * CASCADE_RATIO ) ) * flBorder;
				}
				else
				{
					flWeight = Fidelity_NearCascadeWeight( t );
				}
				const unsigned char nValue = (unsigned char)clamp( (int)( powf( flWeight, 1.0f / 2.2f ) * 255.0f + 0.5f ), 0, 255 );
				unsigned char *pPixel = pData + ( y * nWidth + x ) * nBytes;
				pPixel[ 0 ] = pPixel[ 1 ] = pPixel[ 2 ] = nValue;
				if ( nBytes >= 4 )
					pPixel[ 3 ] = 255;
			}
		}
	}
	virtual void Release() {}

private:
	bool m_bFar;
};

static CFidelityCascadeMask s_NearMask( false );
static CFidelityCascadeMask s_FarMask( true );
static ITexture *s_pNearMask = NULL;
static ITexture *s_pFarMask = NULL;

static ITexture *Fidelity_MakeMask( const char *pName, int nSize, CFidelityCascadeMask *pRegenerator )
{
	ITexture *pTexture = materials->CreateProceduralTexture( pName, TEXTURE_GROUP_OTHER, nSize, nSize, IMAGE_FORMAT_RGBA8888,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL );
	if ( pTexture )
	{
		pTexture->SetTextureRegenerator( pRegenerator );
		pTexture->Download();
	}
	return pTexture;
}

// What the env_global_light sent last (before our changes), for the report.
struct FidelitySunInfo_t
{
	int m_nFrame;				// frame of the last update, -1 = never
	Vector m_vecColor;			// light colour times brightness, linear
	bool m_bShadows;
	float m_flOrthoSize;
	char m_szTexture[ MAX_PATH ];
	const char *m_pszCascades;	// two cascades, or why not
};
static FidelitySunInfo_t s_SunInfo = { -1 };

// The far cascade's shadow light: made, moved and removed here.
static ClientShadowHandle_t s_hFarCascade = CLIENTSHADOW_INVALID_HANDLE;
static int s_nFarCascadeFrame = -1;

static void Fidelity_RemoveFarCascade()
{
	if ( s_hFarCascade != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->DestroyFlashlight( s_hFarCascade );
		s_hFarCascade = CLIENTSHADOW_INVALID_HANDLE;
	}
}

// Removes the far cascade when the env_global_light stops updating it (off,
// gone, other map).
class CFidelityCascadeKeeper : public CAutoGameSystemPerFrame
{
public:
	CFidelityCascadeKeeper() : CAutoGameSystemPerFrame( "CFidelityCascadeKeeper" ), m_flReportAt( -1.0f ) {}
	virtual void LevelInitPostEntity() { m_flReportAt = gpGlobals->realtime + 3.0f; }
	virtual void LevelShutdownPreEntity() { Fidelity_RemoveFarCascade(); m_flReportAt = -1.0f; }
	// The mask textures point at regenerators in this DLL: let go of them
	// before it is unloaded (like Valve's camo material proxy).
	virtual void Shutdown()
	{
		ITexture *pMasks[ 2 ] = { s_pNearMask, s_pFarMask };
		for ( int i = 0; i < 2; i++ )
		{
			if ( !pMasks[ i ] )
				continue;
			pMasks[ i ]->SetTextureRegenerator( NULL );
			pMasks[ i ]->DecrementReferenceCount();
		}
		s_pNearMask = s_pFarMask = NULL;
	}
	virtual void Update( float frametime )
	{
		if ( s_hFarCascade != CLIENTSHADOW_INVALID_HANDLE && gpGlobals->framecount - s_nFarCascadeFrame > 2 )
			Fidelity_RemoveFarCascade();
		if ( m_flReportAt >= 0.0f && gpGlobals->realtime >= m_flReportAt )
		{
			m_flReportAt = -1.0f;
			Report();
		}
	}

public:
	// Once per map (and with r_fidelity_sun_info) in the console: what the
	// env_global_light really sends, and why there might be nothing to see.
	static void Report()
	{
		bool bFound = false;
		for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity && !bFound; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
		{
			ClientClass *pClass = pEntity->GetClientClass();
			bFound = pClass && !Q_strcmp( pClass->m_pNetworkName, "CGlobalLight" );
		}
		if ( !bFound )
		{
			Msg( "[fidelity] Sun shadows: this map has no env_global_light, so no dynamic sun shadows (Mapbase draws them only with one in the map).\n" );
			return;
		}
		if ( s_SunInfo.m_nFrame < 0 || gpGlobals->framecount - s_SunInfo.m_nFrame > 10 )
		{
			Msg( "[fidelity] Sun shadows: env_global_light found but switched off (keyvalue Enabled = No, or input TurnOff).\n" );
			return;
		}
		const Vector &c = s_SunInfo.m_vecColor;
		Msg( "[fidelity] Sun shadows: env_global_light on - colour x brightness %.2f %.2f %.2f, Enable Shadows %s, Ortho Size %.0f, texture %s, %s.\n",
			c.x, c.y, c.z, s_SunInfo.m_bShadows ? "yes" : "NO", s_SunInfo.m_flOrthoSize, s_SunInfo.m_szTexture,
			s_SunInfo.m_pszCascades ? s_SunInfo.m_pszCascades : "one shadow map" );
		if ( !s_SunInfo.m_bShadows )
			Msg( "[fidelity]   -> no shadows: set \"Enable Shadows\" to Yes in Hammer (Mapbase's default is No).\n" );
		if ( MAX( c.x, MAX( c.y, c.z ) ) < 0.1f )
			Msg( "[fidelity]   -> almost no light: the 4th number of \"Light Color\" is the brightness (0..255), e.g. 255 240 220 255; or raise \"Brightness Scale\".\n" );
		static ConVarRef s_DepthTexture( "r_flashlightdepthtexture" );
		if ( s_DepthTexture.IsValid() && !s_DepthTexture.GetBool() )
			Msg( "[fidelity]   -> r_flashlightdepthtexture is 0: shadow maps are off (set it to 1).\n" );
	}

private:
	float m_flReportAt;
};

static CFidelityCascadeKeeper s_FidelityCascadeKeeper;

// Default border texture of env_global_light? (Only then the masks may replace it.)
static bool Fidelity_IsDefaultSunTexture( ITexture *pTexture )
{
	if ( !pTexture )
		return true;
	const char *pName = pTexture->GetName();
	return !Q_stricmp( pName, "effects/flashlight_border" ) || !Q_stricmp( pName, "effects/flashlight001" );
}

CON_COMMAND( r_fidelity_sun_info, "Prints what the map's env_global_light sends (colour, shadows, size) and why sun shadows might not show." )
{
	CFidelityCascadeKeeper::Report();
}

void Fidelity_AdjustGlobalLight( FlashlightState_t &state, const Vector &vRightIn, const Vector &vUpIn )
{
	Vector vRight = vRightIn, vUp = vUpIn;

	// One sun: its light and shadows come from where env_sun stands in the sky.
	// env_global_light has no Pitch field of its own (only "Pitch Yaw Roll"):
	// left at 0 the sun shone flat across the ground, through rooms, on
	// ceilings - while env_sun stood 35 degrees up (Julius 26.10.2026: "die
	// Richtung ergibt keinen Sinn"). The light keeps the point it was aimed at
	// (around the player) and its distance.
	Vector vecToSun;
	if ( r_fidelity_sun_follow.GetBool() && Fidelity_EnvSunDirection( vecToSun ) )
	{
		matrix3x4_t mat;
		QuaternionMatrix( state.m_quatOrientation, mat );
		Vector vecOldForward;
		MatrixGetColumn( mat, 0, vecOldForward );
		const Vector vecForward = -vecToSun;
		if ( DotProduct( vecOldForward, vecForward ) < 0.9998f )	// more than about 1 degree apart
		{
			const float flDistance = state.m_FarZ * 0.5f;
			const Vector vecTarget = state.m_vecLightOrigin + vecOldForward * flDistance;
			QAngle angSun;
			VectorAngles( vecForward, angSun );
			Vector vecF;
			AngleVectors( angSun, &vecF, &vRight, &vUp );
			BasisToQuaternion( vecF, vRight, vUp, state.m_quatOrientation );
			state.m_vecLightOrigin = vecTarget - vecF * flDistance;
		}
	}

	s_SunInfo.m_nFrame = gpGlobals->framecount;
	s_SunInfo.m_vecColor.Init( state.m_Color[ 0 ], state.m_Color[ 1 ], state.m_Color[ 2 ] );
	s_SunInfo.m_bShadows = state.m_bEnableShadows;
	s_SunInfo.m_flOrthoSize = state.m_bOrtho ? state.m_fOrthoRight : 0.0f;
	Q_strncpy( s_SunInfo.m_szTexture, state.m_pSpotlightTexture ? state.m_pSpotlightTexture->GetName() : "-", sizeof( s_SunInfo.m_szTexture ) );

	// A cloud in front of the sun dims its light, and with it its shadows (a little)
	const float flClouds = Fidelity_SunLightThroughClouds();
	for ( int i = 0; i < 3; i++ )
		state.m_Color[ i ] *= flClouds;

	s_SunInfo.m_pszCascades = "one shadow map (r_fidelity_sunshadow 0)";
	if ( !r_fidelity_sunshadow.GetBool() )
	{
		Fidelity_RemoveFarCascade();
		return;
	}

	// The shadow manager keeps r_flashlightdepthres at the size of the depth textures it made.
	const float flResolution = MAX( r_flashlightdepthres.GetFloat(), 1.0f );
	state.m_flShadowMapResolution = flResolution;
	state.m_flShadowFilterSize = MAX( r_fidelity_sunshadow_softness.GetFloat(), 0.0f );

	s_SunInfo.m_pszCascades = "one shadow map (Ortho Size 0: perspective)";
	if ( !state.m_bOrtho )
	{
		Fidelity_RemoveFarCascade();
		return;
	}

	// Depth bias in world units, the same for both cascades: env_global_light
	// gives 0.00001 of its depth range (about 0.2 units from 10000 away) and a
	// slope bias that shrinks with the texel - in the near cascade (texels a
	// quarter as big) a sloped surface shadowed itself: the wooden ramp came
	// out brown and busy close up, sunny from afar (Julius 26.10.2026: "zwei
	// verschiedene Farbarten" near and far). SUN_DEPTH_BIAS_UNITS lifts the
	// shadows off the surfaces that cast them (a shadow starts that much later -
	// nothing one sees at the sun's scale).
	const float flDepthRange = MAX( state.m_FarZ - state.m_NearZ, 1.0f );
	state.m_flShadowDepthBias = MAX( state.m_flShadowDepthBias, SUN_DEPTH_BIAS_UNITS / flDepthRange );

	// World units per shadow map texel (the map is square) of the full area.
	const float flTexel = ( state.m_fOrthoRight - state.m_fOrthoLeft ) / flResolution;
	if ( flTexel <= 0.0f )
		return;

	// Moving along the light direction changes nothing in an orthographic
	// projection, so only the two image plane coordinates are snapped. The
	// near cascade's texels divide these, so its grid stays fixed too.
	const float flRight = DotProduct( state.m_vecLightOrigin, vRight );
	const float flUp = DotProduct( state.m_vecLightOrigin, vUp );
	const float flSnappedRight = floorf( flRight / flTexel + 0.5f ) * flTexel;
	const float flSnappedUp = floorf( flUp / flTexel + 0.5f ) * flTexel;
	state.m_vecLightOrigin += vRight * ( flSnappedRight - flRight ) + vUp * ( flSnappedUp - flUp );

	if ( !r_fidelity_sunshadow_cascade.GetBool() || !state.m_bEnableShadows || !Fidelity_IsDefaultSunTexture( state.m_pSpotlightTexture ) )
	{
		s_SunInfo.m_pszCascades = !r_fidelity_sunshadow_cascade.GetBool() ? "one shadow map (r_fidelity_sunshadow_cascade 0)" :
			( !state.m_bEnableShadows ? "one shadow map (no shadows)" : "one shadow map (own texture)" );
		Fidelity_RemoveFarCascade();
		return;
	}
	if ( !s_pNearMask )
	{
		s_pNearMask = Fidelity_MakeMask( "_rt_FidelitySunNear", 256, &s_NearMask );
		s_pFarMask = Fidelity_MakeMask( "_rt_FidelitySunFar", 512, &s_FarMask );
	}
	if ( !s_pNearMask || !s_pFarMask || s_pNearMask->IsError() || s_pFarMask->IsError() )
	{
		s_SunInfo.m_pszCascades = "one shadow map (the cascade mask textures could not be made)";
		return;
	}

	// Far cascade: the full area, with the hole
	FlashlightState_t farState = state;
	farState.m_pSpotlightTexture = s_pFarMask;
	farState.m_nSpotlightTextureFrame = 0;
	if ( s_hFarCascade == CLIENTSHADOW_INVALID_HANDLE )
	{
		s_hFarCascade = g_pClientShadowMgr->CreateFlashlight( farState );
	}
	else
	{
		g_pClientShadowMgr->UpdateFlashlightState( s_hFarCascade, farState );
		g_pClientShadowMgr->UpdateProjectedTexture( s_hFarCascade, true );
	}
	s_nFarCascadeFrame = gpGlobals->framecount;
	s_SunInfo.m_pszCascades = ( s_hFarCascade != CLIENTSHADOW_INVALID_HANDLE ) ? "two cascades" : "one shadow map (the second shadow light could not be made)";
	if ( s_hFarCascade == CLIENTSHADOW_INVALID_HANDLE )
		return;

	// Near cascade: the env_global_light itself, a quarter of the size
	state.m_fOrthoLeft /= CASCADE_RATIO;
	state.m_fOrthoRight /= CASCADE_RATIO;
	state.m_fOrthoTop /= CASCADE_RATIO;
	state.m_fOrthoBottom /= CASCADE_RATIO;
	state.m_pSpotlightTexture = s_pNearMask;
	state.m_nSpotlightTextureFrame = 0;
}

void Fidelity_AdjustProjectedTexture( FlashlightState_t &state )
{
	if ( r_fidelity_projshadow.GetBool() )
		state.m_flShadowMapResolution = MAX( r_flashlightdepthres.GetFloat(), 1.0f );
}
