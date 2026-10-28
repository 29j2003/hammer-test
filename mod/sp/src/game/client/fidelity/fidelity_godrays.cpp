//========= mapbase-fidelity ==================================================//
//
// Purpose: Light shafts ("god rays") from the map's env_sun or
//          env_global_light, as screen-space volumetric light scattering.
//
// The sun's direction comes from the env_sun entity (C_Sun); maps without one
// (or with an env_sun that has no direction) use Mapbase's env_global_light.
// Its position on screen is the projection of that direction. Passes:
//   1. copy the frame buffer colour to _rt_FullFrameFB (depth: see Fidelity_UpdateDepthTexture)
//   2. mask (quarter size): bright sky around the sun      -> _rt_FidelityRays0
//   3. radial blur towards the sun, long steps              -> _rt_FidelityRays1
//   4. radial blur towards the sun, short steps             -> _rt_FidelityRays0
//   5. composite: scene + shafts                            -> frame buffer
//   6. a faint lens flare (r_fidelity_lensflare): a few soft ghosts on the
//      line from the sun through the middle, only as much as sky is seen at
//      the sun (as Black Mesa: on the brightest light only, never blinding)
// Passes 2-4 run at a quarter of the resolution in each direction, so they
// cost about as much as one full-size pass together. Everything is skipped
// while the sun is behind the camera or far off screen.
//
// The pass runs inside the main view after the opaque scene (called from
// Fidelity_RenderSSAO), so the shafts are lit by the engine's bloom and tone
// mapping, and translucent objects, the viewmodel and the HUD stay in front.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_godrays.h"
#include "fidelity_screenmaterial.h"
#include "fidelity_sky.h"
#include "c_sun.h"
#include "dt_recv.h"
#include "view_scene.h"
#include "view_shared.h"
#include "view.h"
#include "rendertexture.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/itexture.h"
#include "mathlib/vmatrix.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static Vector Fidelity_GlassOnly( const Vector &vecToLight, float flDistance );

ConVar r_fidelity_godrays( "r_fidelity_godrays", "1", FCVAR_ARCHIVE, "Light shafts from the sun (env_sun or env_global_light). 0 = off, 1 = on, 2 = show only the shafts (debug view)." );
ConVar r_fidelity_godrays_intensity( "r_fidelity_godrays_intensity", "0.8", FCVAR_SERVER_CAN_EXECUTE, "Brightness of the light shafts." );
ConVar r_fidelity_godrays_length( "r_fidelity_godrays_length", "0.85", FCVAR_SERVER_CAN_EXECUTE, "How far the shafts reach towards the sun, 0..1 of the distance." );
ConVar r_fidelity_godrays_decay( "r_fidelity_godrays_decay", "0.94", FCVAR_SERVER_CAN_EXECUTE, "How quickly samples further along a shaft lose weight, 0..1." );
ConVar r_fidelity_godrays_threshold( "r_fidelity_godrays_threshold", "0.55", FCVAR_SERVER_CAN_EXECUTE, "Sky brightness from which it casts shafts, 0..1." );
ConVar r_fidelity_godrays_radius( "r_fidelity_godrays_radius", "0.1", FCVAR_SERVER_CAN_EXECUTE, "Size of the glowing area around the sun, in screen heights." );
ConVar r_fidelity_godrays_tint( "r_fidelity_godrays_tint", "1.0 0.92 0.8", FCVAR_SERVER_CAN_EXECUTE, "Colour of the shafts (r g b), multiplied with the sky colour." );
ConVar r_fidelity_lensflare( "r_fidelity_lensflare", "0.1", FCVAR_SERVER_CAN_EXECUTE, "Faint lens flare from the sun (with the light shafts). 0 = none." );
ConVar r_fidelity_godrays_skytest( "r_fidelity_godrays_skytest", "1", FCVAR_SERVER_CAN_EXECUTE, "Only sky pixels cast shafts (depth test). Set 0 if a map's 3D skybox shows no shafts." );

#define FIDELITY_RAYS_RT0 "_rt_FidelityRays0"
#define FIDELITY_RAYS_RT1 "_rt_FidelityRays1"

#define SUN_APART_DEGREES	3.0f	// env_sun and env_global_light further apart than this: a warning

// Samples per blur pass, must match NUM_SAMPLES in fidelity_godrays_ps2x.fxc
static const float s_flBlurSamples = 16.0f;

//-----------------------------------------------------------------------------
// Owns the render targets and materials of the light shafts.
//-----------------------------------------------------------------------------
class CFidelityGodRays : public CAutoGameSystem
{
public:
	CFidelityGodRays() : CAutoGameSystem( "CFidelityGodRays" ), m_bReady( false ), m_bFailed( false ), m_nGlobalLightDirOffset( -1 ), m_nGlobalLightEnabledOffset( -1 ), m_flNextSunSearch( 0.0f ), m_flNextDebugMsg( 0.0f ), m_bWarnedApart( false ) {}

	virtual bool Init();
	virtual void Shutdown();
	virtual void LevelInitPreEntity();

	void Render( const CViewSetup &view, ITexture *pDepth );
	bool SunOnScreen( const CViewSetup &view, float &flNdcX, float &flNdcY, float &flIntensity );
	bool SunDirection( Vector &vecDirection ) { return GetSunDirection( vecDirection ); }
	C_Sun *Sun() { FindLightSources(); return m_hSun.Get(); }
	bool GlobalLightDirection( Vector &vecDirection );

private:
	bool EnsureMaterials();
	void FindLightSources();
	bool GetSunDirection( Vector &vecDirection );
	void DebugMsg( const char *pMsg );

	bool m_bReady;
	bool m_bFailed;		// materials missing: not tried again this session (the ConVar stays as the player set it)
	CTextureReference m_RaysTexture0;
	CTextureReference m_RaysTexture1;

	CFidelityScreenMaterial m_Mask;
	CFidelityScreenMaterial m_BlurLong;
	CFidelityScreenMaterial m_BlurShort;
	CFidelityScreenMaterial m_Composite;
	CFidelityScreenMaterial m_Flare;		// lens flare (optional: the shafts work without it)

	CHandle<C_Sun> m_hSun;
	EHANDLE m_hGlobalLight;
	int m_nGlobalLightDirOffset;		// offset of C_GlobalLight::m_shadowDirection, -1 = unknown
	int m_nGlobalLightEnabledOffset;	// offset of C_GlobalLight::m_bEnabled, -1 = unknown
	float m_flNextSunSearch;
	float m_flNextDebugMsg;
	bool m_bWarnedApart;	// told once per map that env_sun and env_global_light point apart
};

static CFidelityGodRays s_FidelityGodRays;

bool CFidelityGodRays::Init()
{
	if ( !g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_b() )
	{
		DevMsg( "[fidelity] Light shafts need pixel shader 2.0b, disabled.\n" );
		return true;
	}

	materials->BeginRenderTargetAllocation();
	m_RaysTexture0.InitRenderTarget( 1, 1, RT_SIZE_HDR, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, false, FIDELITY_RAYS_RT0 );
	m_RaysTexture1.InitRenderTarget( 1, 1, RT_SIZE_HDR, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, false, FIDELITY_RAYS_RT1 );
	materials->EndRenderTargetAllocation();

	return true;
}

void CFidelityGodRays::Shutdown()
{
	m_Mask.Shutdown();
	m_BlurLong.Shutdown();
	m_BlurShort.Shutdown();
	m_Composite.Shutdown();
	m_Flare.Shutdown();
	m_RaysTexture0.Shutdown();
	m_RaysTexture1.Shutdown();
	m_bReady = false;
	m_bFailed = false;
}

void CFidelityGodRays::LevelInitPreEntity()
{
	m_hSun = NULL;
	m_hGlobalLight = NULL;
	m_flNextSunSearch = 0.0f;
	m_bWarnedApart = false;
}

bool CFidelityGodRays::EnsureMaterials()
{
	if ( m_bReady )
		return true;
	if ( m_bFailed )
		return false;

	if ( !m_RaysTexture0.IsValid() || !m_RaysTexture1.IsValid() )
		return false;

	m_Mask.Init( "__fidelity_godrays_mask", "fidelity_godrays_mask_ps20b", "_rt_FullFrameFB", FIDELITY_DEPTH_RT );
	m_BlurLong.Init( "__fidelity_godrays_blur0", "fidelity_godrays_blur_ps20b", FIDELITY_RAYS_RT0 );
	m_BlurShort.Init( "__fidelity_godrays_blur1", "fidelity_godrays_blur_ps20b", FIDELITY_RAYS_RT1 );
	m_Composite.Init( "__fidelity_godrays_composite", "fidelity_godrays_composite_ps20b", FIDELITY_RAYS_RT0, "_rt_FullFrameFB", FIDELITY_DEPTH_RT );
	m_Flare.Init( "__fidelity_lensflare", "fidelity_flare_ps20b", FIDELITY_DEPTH_RT, "_rt_FullFrameFB" );
	if ( m_Flare.IsValid() )
		m_Flare.Get()->SetMaterialVarFlag( MATERIAL_VAR_ADDITIVE, true );

	m_bReady = m_Mask.IsValid() && m_BlurLong.IsValid() && m_BlurShort.IsValid() && m_Composite.IsValid();
	if ( !m_bReady )
	{
		Warning( "[fidelity] Light shaft materials are missing - are the shaders in shaders/fxc? Light shafts are off until the game restarts.\n" );
		m_bFailed = true;
	}
	return m_bReady;
}

// Offset of a networked variable in a client class (top level of its table), -1 if missing.
static int FindRecvPropOffset( ClientClass *pClass, const char *pName )
{
	RecvTable *pTable = pClass ? pClass->m_pRecvTable : NULL;
	for ( int i = 0; pTable && i < pTable->GetNumProps(); i++ )
	{
		RecvProp *pProp = pTable->GetProp( i );
		if ( pProp && !Q_strcmp( pProp->GetName(), pName ) )
			return pProp->GetOffset();
	}
	return -1;
}

//-----------------------------------------------------------------------------
// The map's env_sun and env_global_light. The entity list is searched at most
// once a second while neither is known, every few seconds while one of them
// is missing (stopping at the first one found missed the env_sun of maps
// whose env_global_light came first - the sky then could not draw the sun),
// so maps without a sun cost next to nothing. C_GlobalLight is only declared in its .cpp, so its direction is
// read through the offsets of its networked variables.
//-----------------------------------------------------------------------------
void CFidelityGodRays::FindLightSources()
{
	if ( m_hSun.Get() && m_hGlobalLight.Get() )
		return;

	if ( gpGlobals->realtime < m_flNextSunSearch )
		return;
	m_flNextSunSearch = gpGlobals->realtime + ( ( m_hSun.Get() || m_hGlobalLight.Get() ) ? 3.0f : 1.0f );

	for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
	{
		ClientClass *pClass = pEntity->GetClientClass();
		if ( !pClass )
			continue;

		if ( !m_hSun.Get() && !Q_strcmp( pClass->m_pNetworkName, "CSun" ) )
		{
			m_hSun = static_cast<C_Sun *>( pEntity );
		}
		else if ( !m_hGlobalLight.Get() && !Q_strcmp( pClass->m_pNetworkName, "CGlobalLight" ) )
		{
			m_nGlobalLightDirOffset = FindRecvPropOffset( pClass, "m_shadowDirection" );
			m_nGlobalLightEnabledOffset = FindRecvPropOffset( pClass, "m_bEnabled" );
			if ( m_nGlobalLightDirOffset >= 0 )
				m_hGlobalLight = pEntity;
		}
	}
}

//-----------------------------------------------------------------------------
// Direction from the viewer towards the sun. env_sun first; env_global_light
// when there is no env_sun or it has no direction (straight up is env_sun's
// default when neither "UseAngles" nor a target is set).
//-----------------------------------------------------------------------------
bool CFidelityGodRays::GetSunDirection( Vector &vecDirection )
{
	FindLightSources();

	bool bSunPointsUp = false;
	C_Sun *pSun = m_hSun.Get();
	if ( pSun && pSun->m_bOn )
	{
		vecDirection = pSun->m_vDirection;
		if ( VectorNormalize( vecDirection ) > 0.5f )
		{
			bSunPointsUp = vecDirection.z > 0.999f;
			if ( !bSunPointsUp )
			{
				// The sky draws the sun where env_sun is; shadows and the sun's haze in the air
				// come from env_global_light. Apart, there are two suns (Julius 25.10.2026: "gedoppelt").
				Vector vecLight;
				if ( !m_bWarnedApart && GlobalLightDirection( vecLight ) && DotProduct( vecLight, vecDirection ) < cosf( DEG2RAD( SUN_APART_DEGREES ) ) )
				{
					m_bWarnedApart = true;
					Warning( "[fidelity] env_sun and env_global_light point %.0f degrees apart: the sun's light and shadows follow env_sun (r_fidelity_sun_follow). env_global_light has no Pitch field of its own - its pitch is the first number of \"Pitch Yaw Roll\" (angles); give it the same as env_sun.\n",
						RAD2DEG( acosf( clamp( DotProduct( vecLight, vecDirection ), -1.0f, 1.0f ) ) ) );
				}
				return true;
			}
		}
	}

	if ( GlobalLightDirection( vecDirection ) )
		return true;

	if ( bSunPointsUp )
	{
		DebugMsg( "the env_sun points straight up - turn on its \"UseAngles\" and give it the light_environment's pitch and yaw." );
		vecDirection = Vector( 0.0f, 0.0f, 1.0f );
		return true;
	}

	DebugMsg( "no active env_sun or env_global_light in this map." );
	return false;
}

//-----------------------------------------------------------------------------
// Direction towards the sun of an active env_global_light
//-----------------------------------------------------------------------------
bool CFidelityGodRays::GlobalLightDirection( Vector &vecDirection )
{
	FindLightSources();
	C_BaseEntity *pGlobalLight = m_hGlobalLight.Get();
	if ( !pGlobalLight )
		return false;
	const char *pBase = reinterpret_cast<const char *>( pGlobalLight );
	const bool bEnabled = m_nGlobalLightEnabledOffset < 0 || *reinterpret_cast<const bool *>( pBase + m_nGlobalLightEnabledOffset );
	if ( !bEnabled )
		return false;
	// m_shadowDirection points from the sun down into the world
	vecDirection = -*reinterpret_cast<const Vector *>( pBase + m_nGlobalLightDirOffset );
	return VectorNormalize( vecDirection ) > 0.5f;
}

//-----------------------------------------------------------------------------
// In the debug view (r_fidelity_godrays 2), says why there are no shafts.
// At most every few seconds, so the console stays readable.
//-----------------------------------------------------------------------------
void CFidelityGodRays::DebugMsg( const char *pMsg )
{
	if ( r_fidelity_godrays.GetInt() != 2 || gpGlobals->realtime < m_flNextDebugMsg )
		return;
	m_flNextDebugMsg = gpGlobals->realtime + 3.0f;
	Msg( "[fidelity] Light shafts: %s\n", pMsg );
}

//-----------------------------------------------------------------------------
// Whether shafts are drawn in this view: where the sun is on screen
// (normalised device coordinates) and how strong they are. Cheap, so the
// depth copy can be skipped when nothing is drawn.
//-----------------------------------------------------------------------------
bool CFidelityGodRays::SunOnScreen( const CViewSetup &view, float &flNdcX, float &flNdcY, float &flIntensity )
{
	if ( !r_fidelity_godrays.GetBool() || building_cubemaps.GetBool() )
		return false;

	Vector vecSunDir;
	if ( !GetSunDirection( vecSunDir ) )
		return false;

	// Where the sun is on screen: project the direction (a point at infinity).
	VMatrix matWorldToView, matViewToProjection, matWorldToProjection, matWorldToPixels;
	render->GetMatricesForView( view, &matWorldToView, &matViewToProjection, &matWorldToProjection, &matWorldToPixels );
	Vector4D vecClip;
	Vector4DMultiply( matWorldToProjection, Vector4D( vecSunDir.x, vecSunDir.y, vecSunDir.z, 0.0f ), vecClip );
	if ( vecClip.w <= 0.001f )
	{
		DebugMsg( "the sun is behind you - look towards it." );
		return false;
	}

	flNdcX = vecClip.x / vecClip.w;
	flNdcY = vecClip.y / vecClip.w;

	// Full strength while the sun is on screen, gone when it is far outside.
	const float flOffScreen = MAX( fabsf( flNdcX ), fabsf( flNdcY ) );
	const float flFade = clamp( ( 1.6f - flOffScreen ) / 0.6f, 0.0f, 1.0f );
	flIntensity = r_fidelity_godrays_intensity.GetFloat() * flFade;
	return flIntensity >= 0.01f || r_fidelity_godrays.GetInt() == 2;
}

void CFidelityGodRays::Render( const CViewSetup &view, ITexture *pDepth )
{
	float flNdcX, flNdcY, flIntensity;
	if ( !SunOnScreen( view, flNdcX, flNdcY, flIntensity ) )
		return;
	const bool bDebug = r_fidelity_godrays.GetInt() == 2;

	if ( !EnsureMaterials() )
		return;

	m_Mask.SetTexture( 1, pDepth );
	m_Composite.SetTexture( 2, pDepth );

	CMatRenderContextPtr pRenderContext( materials );

	// 1. Snapshot the colour (depth is in pDepth already).
	Rect_t srcRect;
	UpdateScreenEffectTexture( 0, view.x, view.y, view.width, view.height, false, &srcRect );
	ITexture *pFrame = GetFullFrameFrameBufferTexture( 0 );
	const int nTexW = pFrame->GetActualWidth();
	const int nTexH = pFrame->GetActualHeight();
	const int w = srcRect.width;
	const int h = srcRect.height;
	const int nRaysW = m_RaysTexture0->GetActualWidth();
	const int nRaysH = m_RaysTexture0->GetActualHeight();
	if ( w <= 0 || h <= 0 || nRaysW <= 0 || nRaysH <= 0 )
		return;

	// Quarter-size working area in the top left corner of the shaft textures.
	const int qw = clamp( ( w + 3 ) / 4, 1, nRaysW );
	const int qh = clamp( ( h + 3 ) / 4, 1, nRaysH );

	// Sun position in the scene texture (uv) and in the shaft textures.
	const float flSunX = ( srcRect.x + ( flNdcX * 0.5f + 0.5f ) * w ) / nTexW;
	const float flSunY = ( srcRect.y + ( 0.5f - flNdcY * 0.5f ) * h ) / nTexH;
	const float flSceneToRaysX = ( (float)qw / nRaysW ) / ( (float)w / nTexW );
	const float flSceneToRaysY = ( (float)qh / nRaysH ) / ( (float)h / nTexH );

	// 2. Mask
	const float flRadius = MAX( 0.01f, r_fidelity_godrays_radius.GetFloat() ) * (float)h / nTexH;
	const float flThreshold = clamp( r_fidelity_godrays_threshold.GetFloat(), 0.0f, 0.99f );
	m_Mask.SetConstant( 0, flSunX, flSunY, 1.0f / flRadius, (float)nTexW / nTexH );
	m_Mask.SetConstant( 1, flThreshold, 1.0f / ( 1.0f - flThreshold ), r_fidelity_godrays_skytest.GetBool() ? 0.99f : 0.0f, 1.0f / nTexW );

	pRenderContext->PushRenderTargetAndViewport( m_RaysTexture0 );
	Fidelity_DrawScreenPass( m_Mask.Get(), 0, 0, qw, qh, srcRect.x, srcRect.y, w, h, nTexW, nTexH );
	pRenderContext->PopRenderTargetAndViewport();

	// 3. + 4. Radial blur: the second pass takes steps as long as the gaps of the first.
	const float flLength = clamp( r_fidelity_godrays_length.GetFloat(), 0.0f, 1.0f );
	const float flDecay = clamp( r_fidelity_godrays_decay.GetFloat(), 0.5f, 1.0f );
	const float flRaysSunX = flSunX * flSceneToRaysX;
	const float flRaysSunY = flSunY * flSceneToRaysY;
	const float flMaxU = ( qw - 0.5f ) / nRaysW;
	const float flMaxV = ( qh - 0.5f ) / nRaysH;

	m_BlurLong.SetConstant( 0, flRaysSunX, flRaysSunY, flLength / s_flBlurSamples, flDecay );
	m_BlurLong.SetConstant( 1, flMaxU, flMaxV, 0.0f, 0.0f );
	pRenderContext->PushRenderTargetAndViewport( m_RaysTexture1 );
	Fidelity_DrawScreenPass( m_BlurLong.Get(), 0, 0, qw, qh, 0, 0, qw, qh, nRaysW, nRaysH );
	pRenderContext->PopRenderTargetAndViewport();

	m_BlurShort.SetConstant( 0, flRaysSunX, flRaysSunY, flLength / ( s_flBlurSamples * s_flBlurSamples ), flDecay );
	m_BlurShort.SetConstant( 1, flMaxU, flMaxV, 0.0f, 0.0f );
	pRenderContext->PushRenderTargetAndViewport( m_RaysTexture0 );
	Fidelity_DrawScreenPass( m_BlurShort.Get(), 0, 0, qw, qh, 0, 0, qw, qh, nRaysW, nRaysH );
	pRenderContext->PopRenderTargetAndViewport();

	// 5. Composite
	float flTint[ 3 ] = { 1.0f, 1.0f, 1.0f };
	sscanf( r_fidelity_godrays_tint.GetString(), "%f %f %f", &flTint[ 0 ], &flTint[ 1 ], &flTint[ 2 ] );
	// Through glass or plastic in front of the sun: tinted and weaker
	Vector vecSunDir;
	if ( GetSunDirection( vecSunDir ) )
	{
		// (the clouds: where the sky draws the sun, the shafts start from its gaps by themselves)
		const Vector vecGlass = Fidelity_GlassOnly( vecSunDir, MAX_TRACE_LENGTH ) * ( Fidelity_SkyDrawsSun() ? 1.0f : Fidelity_SunGlowThroughClouds() );
		flTint[ 0 ] *= vecGlass.x;
		flTint[ 1 ] *= vecGlass.y;
		flTint[ 2 ] *= vecGlass.z;
	}
	const float flStrength = bDebug ? MAX( flIntensity, 1.0f ) : flIntensity;
	m_Composite.SetConstant( 0, flTint[ 0 ] * flStrength, flTint[ 1 ] * flStrength, flTint[ 2 ] * flStrength, bDebug ? 0.0f : 1.0f );
	m_Composite.SetConstant( 1, flSceneToRaysX, flSceneToRaysY, 0.0f, 0.0f );

	Fidelity_DrawScreenPass( m_Composite.Get(), view.x, view.y, view.width, view.height, srcRect.x, srcRect.y, w, h, nTexW, nTexH );

	// 6. Lens flare, as strong as the shafts' light (through glass, behind clouds);
	// only while the sun itself is on screen (its taps would read past the
	// edge of the view), fading out just before the edge
	const float flFlare = r_fidelity_lensflare.GetFloat() * flIntensity * ( flTint[ 0 ] + flTint[ 1 ] + flTint[ 2 ] ) / 3.0f
		* clamp( ( 0.97f - MAX( fabsf( flNdcX ), fabsf( flNdcY ) ) ) / 0.2f, 0.0f, 1.0f );
	if ( !bDebug && flFlare > 0.001f && m_Flare.IsValid() )
	{
		m_Flare.SetTexture( 0, pDepth );
		m_Flare.SetConstant( 0, flSunX, flSunY, (float)w / h, flFlare );
		m_Flare.SetConstant( 1, ( srcRect.x + 0.5f * w ) / nTexW, ( srcRect.y + 0.5f * h ) / nTexH, 0.004f * h / nTexH, flThreshold );
		Fidelity_DrawScreenPass( m_Flare.Get(), view.x, view.y, view.width, view.height, srcRect.x, srcRect.y, w, h, nTexW, nTexH );
	}
}

//-----------------------------------------------------------------------------
// Light coming through glass and plastic (Julius: the sun shone straight
// through a red bottle as if it was not there). The engine's sun glow and the
// light shafts only test the depth buffer, which see-through things do not
// write. So a few rays go from the eye towards the light (the middle and four
// around it, about the size of the sun disc): every see-through thing on the
// way - a model with translucent materials, or a glass brush - lets through
// GLASS_TRANSMISSION of the light in its own colour (saturated, brightest
// channel 1). The result is the average of the rays (a piece of glass over half
// the sun dims half of it). Opaque things are left to the engine's test.
// Cached for a frame per direction (the glow draws several sprites).
//-----------------------------------------------------------------------------
#define GLASS_RAYS			5
#define GLASS_RAY_SPREAD	0.012f	// radians off the middle
#define GLASS_TRANSMISSION	0.45f
#define GLASS_MAX_LAYERS	3

// The colour a see-through surface tints light with, or false if it is not see-through
static bool Fidelity_GlassTint( const trace_t &tr, Vector &vecTint )
{
	IMaterial *pMaterial = NULL;
	C_BaseEntity *pEntity = tr.m_pEnt;
	if ( pEntity && !pEntity->IsWorld() )
	{
		const model_t *pModel = pEntity->GetModel();
		if ( !pModel || !modelinfo->IsTranslucent( pModel ) )
			return false;
		modelinfo->GetModelMaterials( pModel, 1, &pMaterial );
	}
	else
	{
		if ( !tr.surface.name || !tr.surface.name[ 0 ] )
			return false;
		pMaterial = materials->FindMaterial( tr.surface.name, TEXTURE_GROUP_WORLD, false );
		if ( !pMaterial || pMaterial->IsErrorMaterial() || !pMaterial->IsTranslucent() )
			return false;
	}
	Vector vecColor( 1, 1, 1 );
	if ( pMaterial )
		pMaterial->GetReflectivity( vecColor );
	// Saturated a little, brightest channel 1: the glass's colour, not its brightness
	const float flGray = ( vecColor.x + vecColor.y + vecColor.z ) / 3.0f;
	vecColor = Vector( flGray, flGray, flGray ) + ( vecColor - Vector( flGray, flGray, flGray ) ) * 1.5f;
	vecColor.x = MAX( vecColor.x, 0.0f );
	vecColor.y = MAX( vecColor.y, 0.0f );
	vecColor.z = MAX( vecColor.z, 0.0f );
	const float flMax = MAX( vecColor.x, MAX( vecColor.y, vecColor.z ) );
	vecTint = ( flMax > 0.001f ? vecColor / flMax : Vector( 1, 1, 1 ) ) * GLASS_TRANSMISSION;
	return true;
}


Vector Fidelity_GlassTransmission( const Vector &vecToLight, float flDistance )
{
	if ( flDistance >= MAX_TRACE_LENGTH )
	{
		// The sun (a light at no distance): drawn by the sky behind its clouds,
		// the engine's sprites stay away; else they dim behind the clouds
		if ( Fidelity_SkyDrawsSun() )
			return vec3_origin;
		return Fidelity_GlassOnly( vecToLight, flDistance ) * Fidelity_SunGlowThroughClouds();
	}
	return Fidelity_GlassOnly( vecToLight, flDistance );
}

bool Fidelity_SunSprite( Vector &vecDirection, Vector &vecCore, float &flCoreSize, Vector &vecGlow, float &flGlowSize )
{
	C_Sun *pSun = s_FidelityGodRays.Sun();
	if ( !pSun || !pSun->m_bOn )
		return false;
	vecDirection = pSun->m_vDirection;
	if ( VectorNormalize( vecDirection ) < 0.5f || vecDirection.z > 0.999f )
		return false;	// no direction set (straight up): the light shafts take env_global_light's
	// As big as the sprites: half their size at a distance of 100 (glow_overlay.cpp),
	// the glow six times bigger while one looks at the sun (C_SunGlowOverlay)
	vecCore = pSun->m_Overlay.m_Sprites[ 0 ].m_vColor * pSun->m_Overlay.m_flHDRColorScale;
	vecGlow = pSun->m_GlowOverlay.m_Sprites[ 0 ].m_vColor * pSun->m_GlowOverlay.m_flHDRColorScale;
	const float flSize = MAX( (float)pSun->m_nSize, 1.0f );
	flCoreSize = atanf( flSize / 100.0f );
	flGlowSize = atanf( ( pSun->m_nOverlaySize > 0 ? pSun->m_nOverlaySize : flSize ) * 6.0f / 100.0f );
	return true;
}

static Vector Fidelity_GlassOnly( const Vector &vecToLight, float flDistance )
{
	static Vector s_vecLastDir( 0, 0, 0 ), s_vecLastResult( 1, 1, 1 );
	static int s_nLastFrame = -1;
	if ( s_nLastFrame == gpGlobals->framecount && s_vecLastDir == vecToLight )
		return s_vecLastResult;
	s_nLastFrame = gpGlobals->framecount;
	s_vecLastDir = vecToLight;

	Vector vecDir = vecToLight;
	if ( VectorNormalize( vecDir ) < 0.001f )
		return s_vecLastResult = Vector( 1, 1, 1 );
	Vector vecRight, vecUp;
	VectorVectors( vecDir, vecRight, vecUp );
	static const float s_flOffsets[ GLASS_RAYS ][ 2 ] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	const Vector &vecEye = CurrentViewOrigin();
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	const float flLength = MIN( flDistance, MAX_TRACE_LENGTH * 0.5f );

	Vector vecSum( 0, 0, 0 );
	for ( int nRay = 0; nRay < GLASS_RAYS; nRay++ )
	{
		Vector vecRay = vecDir + ( vecRight * s_flOffsets[ nRay ][ 0 ] + vecUp * s_flOffsets[ nRay ][ 1 ] ) * GLASS_RAY_SPREAD;
		VectorNormalize( vecRay );
		Vector vecThrough( 1, 1, 1 );
		Vector vecStart = vecEye;
		C_BaseEntity *pSkip = NULL;
		for ( int nLayer = 0; nLayer < GLASS_MAX_LAYERS; nLayer++ )
		{
			trace_t tr;
			CTraceFilterSkipTwoEntities filter( pPlayer, pSkip, COLLISION_GROUP_NONE );
			UTIL_TraceLine( vecStart, vecEye + vecRay * flLength, MASK_SHOT | CONTENTS_WINDOW, &filter, &tr );
			Vector vecTint;
			if ( tr.fraction >= 1.0f || tr.startsolid || !Fidelity_GlassTint( tr, vecTint ) )
				break;
			vecThrough.x *= vecTint.x;
			vecThrough.y *= vecTint.y;
			vecThrough.z *= vecTint.z;
			// On through it: past the entity, or a little through the glass brush
			pSkip = ( tr.m_pEnt && !tr.m_pEnt->IsWorld() ) ? tr.m_pEnt : pSkip;
			vecStart = tr.endpos + vecRay * 2.0f;
		}
		vecSum += vecThrough;
	}
	return s_vecLastResult = vecSum / GLASS_RAYS;
}

bool Fidelity_EnvSunDirection( Vector &vecDirection )
{
	C_Sun *pSun = s_FidelityGodRays.Sun();
	if ( !pSun || !pSun->m_bOn )
		return false;
	vecDirection = pSun->m_vDirection;
	return VectorNormalize( vecDirection ) > 0.5f && vecDirection.z < 0.999f;
}

bool Fidelity_SunDirection( Vector &vecDirection )
{
	vecDirection.Init( 0.0f, 0.0f, 1.0f );
	return s_FidelityGodRays.SunDirection( vecDirection );
}

void Fidelity_SunReport( CUtlBuffer &info )
{
	C_Sun *pSun = s_FidelityGodRays.Sun();
	Vector vecSun( 0, 0, 0 ), vecLight;
	QAngle ang;
	if ( !pSun )
		info.Printf( "\nsun: no env_sun\n" );
	else
	{
		vecSun = pSun->m_vDirection;
		VectorNormalize( vecSun );
		VectorAngles( vecSun, ang );
		info.Printf( "\nsun: env_sun %s, towards pitch %.1f yaw %.1f, size %d, overlay size %d, HDR colour scale %.2f\n", pSun->m_bOn ? "on" : "off",
			ang.x, ang.y, pSun->m_nSize, pSun->m_nOverlaySize, pSun->m_Overlay.m_flHDRColorScale );
		const Vector &vecCore = pSun->m_Overlay.m_Sprites[ 0 ].m_vColor;
		const Vector &vecGlow = pSun->m_GlowOverlay.m_Sprites[ 0 ].m_vColor;
		info.Printf( "  core colour %.2f %.2f %.2f, glow colour %.2f %.2f %.2f, drawn by the sky: %s\n", vecCore.x, vecCore.y, vecCore.z,
			vecGlow.x, vecGlow.y, vecGlow.z, Fidelity_SkyDrawsSun() ? "yes" : "no (the engine's sprites)" );
	}
	if ( !s_FidelityGodRays.GlobalLightDirection( vecLight ) )
		info.Printf( "  env_global_light: none or off\n" );
	else
	{
		VectorAngles( vecLight, ang );
		info.Printf( "  env_global_light towards pitch %.1f yaw %.1f", ang.x, ang.y );
		if ( pSun )
			info.Printf( ", %.1f degrees from env_sun", RAD2DEG( acosf( clamp( DotProduct( vecSun, vecLight ), -1.0f, 1.0f ) ) ) );
		info.Printf( "\n" );
	}
	info.Printf( "  shafts %s, intensity %s, radius %s, threshold %s, lens flare %s\n", r_fidelity_godrays.GetString(), r_fidelity_godrays_intensity.GetString(),
		r_fidelity_godrays_radius.GetString(), r_fidelity_godrays_threshold.GetString(), r_fidelity_lensflare.GetString() );
}

void Fidelity_RenderGodRays( const CViewSetup &view, ITexture *pDepth )
{
	s_FidelityGodRays.Render( view, pDepth );
}

bool Fidelity_GodRaysWillDraw( const CViewSetup &view )
{
	float flNdcX, flNdcY, flIntensity;
	return s_FidelityGodRays.SunOnScreen( view, flNdcX, flNdcY, flIntensity );
}
