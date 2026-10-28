//========= mapbase-fidelity ==================================================//
//
// Purpose: Light in the air of the flashlight and of projected textures
//          (volumetric light), with the shadows of what stands in it.
//
// The flashlight lit only the surfaces it hit. Real light also shows in the
// air - a cone of haze, cut by the shadows of everything standing in it (as
// Black Mesa's new lights, Portal 2: Community Edition's volumetric projected
// textures, HL2 RTX). This lives in the world: each light's own frustum and
// shadow map, so it looks the same from wherever it is seen (Julius wants no
// effects that depend on what happens to be on screen).
//
// clientshadowmgr.cpp (line marked "mapbase-fidelity") hands over every light
// whose shadow map was rendered (Fidelity_VolumetricShadow): its state, its
// world-to-texture matrix and the shadow map. In the main view after the
// opaque scene (with SSAO and the light shafts, Fidelity_RenderVolumetric)
// the nearest r_fidelity_volumetric_lights of this frame's lights are walked
// in a quarter-size target (shader Fidelity_Volumetric of the shader DLL, one
// material per light, additive), then blurred 4 x 4 at quarter size into the
// second target (fidelity_volumetric_blur_ps20b) and added onto the frame
// (fidelity_volumetric_composite_ps20b).
//
// The sun: Mapbase's env_global_light is an ortho light with a shadow map
// around the player. Its haze (r_fidelity_volumetric_sun, much thinner than a
// flashlight's: it is everywhere outside) shows shafts through windows and
// between buildings from its shadow map - in the world, unlike the screen-space
// light shafts - scattering forward (strongest looking towards the sun), even
// light (no cookie, no falloff), up to r_fidelity_volumetric_sun_reach. With
// the cascaded sun shadows the widest cascade is taken.
//
// Mist near the ground (r_fidelity_mist, as Black Mesa's height fog, but lit
// by the sun and the lights, with their shadows): the haze is denser low down,
// 1 + r_fidelity_mist x exp( -(height above the base) / r_fidelity_mist_height ).
// The base is a height in the world: the floor below the player when the map
// is first drawn (where it starts), or r_fidelity_mist_base if the map sets it
// (point_clientcommand). The mist stays in the world, wherever one looks.
//
// Cost: per light one quarter-size pass of 24 steps, plus one quarter-size
// blur and one full-size composite; nothing without lights or with neither
// haze (density 0) nor sun haze. Without the shader DLL (deploy.bat not
// run) it stays off, with one message.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_volumetric.h"
#include "fidelity_screenmaterial.h"
#include "view_shared.h"
#include "view.h"
#include "rendertexture.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "mathlib/vmatrix.h"
#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar r_fidelity_ssao_depthrange;	// fidelity_ssao.cpp: the depth range in destination alpha

ConVar r_fidelity_volumetric( "r_fidelity_volumetric", "1", FCVAR_ARCHIVE, "Light in the air of the flashlight and of projected textures with shadows. 0 = off." );
ConVar r_fidelity_volumetric_lights( "r_fidelity_volumetric_lights", "2", FCVAR_ARCHIVE, "How many lights at most (the nearest) show light in the air (each costs a quarter-size pass)." );
ConVar r_fidelity_volumetric_density( "r_fidelity_volumetric_density", "0.0006", FCVAR_SERVER_CAN_EXECUTE, "How much light the air scatters per unit (haze). 0.0003 = clear air, 0.0015 = dusty." );
ConVar r_fidelity_volumetric_distance( "r_fidelity_volumetric_distance", "1200", FCVAR_SERVER_CAN_EXECUTE, "Lights further away than this (units) show no light in the air." );
ConVar r_fidelity_volumetric_sun( "r_fidelity_volumetric_sun", "0.00004", FCVAR_SERVER_CAN_EXECUTE, "Haze of the sun (env_global_light) per unit: shafts through windows. 0 = none." );
ConVar r_fidelity_mist( "r_fidelity_mist", "2", FCVAR_SERVER_CAN_EXECUTE, "Mist near the ground: how much denser the haze is at the base (lit by the sun and lights). 0 = none." );
ConVar r_fidelity_mist_height( "r_fidelity_mist_height", "80", FCVAR_SERVER_CAN_EXECUTE, "Mist near the ground: units above the base over which it thins out." );
ConVar r_fidelity_mist_base( "r_fidelity_mist_base", "auto", FCVAR_SERVER_CAN_EXECUTE, "Mist near the ground: its base height in the world; auto = the floor where the map starts." );
ConVar r_fidelity_volumetric_sun_reach( "r_fidelity_volumetric_sun_reach", "1500", FCVAR_SERVER_CAN_EXECUTE, "How far (units) the sun's haze is gathered along the view - at most as far as the depth in destination alpha reaches (r_fidelity_ssao_depthrange)." );

#define VOLUMETRIC_SUN_SCATTER		0.35f	// forward scattering of the sun's haze (Henyey-Greenstein g): 0.5 made
											// the open air towards the sun 6 x as bright as across, a wide glow of
											// the env_global_light's colour round the sun - on top of the sky's

#define FIDELITY_VOLUMETRIC_RT	"_rt_FidelityRays0"		// quarter size, made by the light shafts (fidelity_godrays.cpp)
#define FIDELITY_VOLUMETRIC_BLUR_RT	"_rt_FidelityRays1"	// the same, for the blurred image
#define MAX_VOLUMETRIC_SLOTS	4						// materials (one per light drawn in a frame)
#define MAX_VOLUMETRIC_LIGHTS	16						// lights remembered per frame

// The parameters of Fidelity_Volumetric in the order of its constants c0..c11
static const char *s_pVolumetricVectors[] = { "$eye", "$forward", "$right", "$up", "$tolight0", "$tolight1", "$tolight2", "$tolight3", "$lightpos", "$lightcolor", "$atten", "$screen", "$lightdir", "$mist" };

struct FidelityVolumetricLight_t
{
	ClientShadowHandle_t m_hShadow;
	FlashlightState_t m_State;
	VMatrix m_WorldToTexture;
	ITexture *m_pShadowDepth;
	int m_nFrame;
	float m_flDistance;		// from the view, while sorting
};

//-----------------------------------------------------------------------------
// Owns the materials and this frame's lights.
//-----------------------------------------------------------------------------
class CFidelityVolumetric : public CAutoGameSystem
{
public:
	CFidelityVolumetric() : CAutoGameSystem( "CFidelityVolumetric" ), m_bReady( false ), m_bFailed( false ), m_bHaveMistBase( false ), m_flMistBase( 0.0f ) {}

	virtual void Shutdown();
	virtual void LevelInitPreEntity() { m_Lights.RemoveAll(); m_bHaveMistBase = false; }

	void AddLight( ClientShadowHandle_t hShadow, const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pShadowDepth );
	void Render( const CViewSetup &view, ITexture *pDepth );
	int CollectLights( const Vector &vecView );
	void Report( const Vector &vecEye, CUtlBuffer &info );

private:
	bool EnsureMaterials();
	void SetVector( IMaterial *pMaterial, int nIndex, float x, float y, float z, float w );

	CUtlVector< FidelityVolumetricLight_t > m_Lights;
	CUtlVector< int > m_Drawn;			// this frame: indices into m_Lights, nearest first
	CMaterialReference m_March[ MAX_VOLUMETRIC_SLOTS ];
	CFidelityScreenMaterial m_Blur;
	CFidelityScreenMaterial m_Composite;
	bool m_bReady;
	bool m_bFailed;
	bool m_bHaveMistBase;
	float m_flMistBase;		// the floor where the map started (world height)
	float MistBase();
};

static CFidelityVolumetric s_FidelityVolumetric;

// The mist's base: the map's own value, else the floor below the player when first asked
float CFidelityVolumetric::MistBase()
{
	const char *pszBase = r_fidelity_mist_base.GetString();
	if ( pszBase[ 0 ] && Q_stricmp( pszBase, "auto" ) )
		return atof( pszBase );
	if ( !m_bHaveMistBase )
	{
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer )
			return MainViewOrigin().z - 64.0f;
		trace_t tr;
		const Vector vecFeet = pPlayer->GetAbsOrigin();
		UTIL_TraceLine( vecFeet + Vector( 0, 0, 8 ), vecFeet - Vector( 0, 0, 512 ), MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &tr );
		m_flMistBase = tr.fraction < 1.0f ? tr.endpos.z : vecFeet.z;
		m_bHaveMistBase = true;
	}
	return m_flMistBase;
}

void CFidelityVolumetric::Shutdown()
{
	for ( int i = 0; i < MAX_VOLUMETRIC_SLOTS; i++ )
		m_March[ i ].Shutdown();
	m_Blur.Shutdown();
	m_Composite.Shutdown();
	m_bReady = false;
}

bool CFidelityVolumetric::EnsureMaterials()
{
	if ( m_bReady )
		return true;
	if ( m_bFailed )
		return false;

	for ( int i = 0; i < MAX_VOLUMETRIC_SLOTS; i++ )
	{
		KeyValues *pKV = new KeyValues( "Fidelity_Volumetric" );
		pKV->SetString( "$depthtexture", FIDELITY_DEPTH_RT );
		pKV->SetString( "$spottexture", "effects/flashlight001" );
		char szName[ 64 ];
		Q_snprintf( szName, sizeof( szName ), "__fidelity_volumetric%d", i );
		// FindProceduralMaterial takes ownership of the KeyValues
		m_March[ i ].Init( materials->FindProceduralMaterial( szName, TEXTURE_GROUP_OTHER, pKV ) );
	}
	m_Blur.Init( "__fidelity_volumetric_blur", "fidelity_volumetric_blur_ps20b", FIDELITY_VOLUMETRIC_RT );
	m_Composite.Init( "__fidelity_volumetric_composite", "fidelity_volumetric_composite_ps20b", FIDELITY_VOLUMETRIC_BLUR_RT );
	if ( m_Composite.IsValid() )
		m_Composite.Get()->SetMaterialVarFlag( MATERIAL_VAR_ADDITIVE, true );

	ITexture *pTarget = materials->FindTexture( FIDELITY_VOLUMETRIC_RT, TEXTURE_GROUP_RENDER_TARGET, false );
	ITexture *pBlurTarget = materials->FindTexture( FIDELITY_VOLUMETRIC_BLUR_RT, TEXTURE_GROUP_RENDER_TARGET, false );
	m_bReady = m_Blur.IsValid() && m_Composite.IsValid() && pTarget && !pTarget->IsError() && pBlurTarget && !pBlurTarget->IsError();
	for ( int i = 0; i < MAX_VOLUMETRIC_SLOTS && m_bReady; i++ )
	{
		IMaterial *pMaterial = m_March[ i ];
		m_bReady = pMaterial && !pMaterial->IsErrorMaterial() && !Q_stricmp( pMaterial->GetShaderName(), "Fidelity_Volumetric" );
	}
	if ( !m_bReady )
	{
		Warning( "[fidelity] Light in the air (r_fidelity_volumetric) needs the shader DLL (bin\\game_shader_dx6.dll, deploy.bat) and the light shafts' targets - off until the game restarts.\n" );
		m_bFailed = true;
	}
	return m_bReady;
}

void CFidelityVolumetric::AddLight( ClientShadowHandle_t hShadow, const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pShadowDepth )
{
	if ( !r_fidelity_volumetric.GetBool() || !pShadowDepth )
		return;
	int nIndex = -1;
	for ( int i = 0; i < m_Lights.Count() && nIndex < 0; i++ )
	{
		if ( m_Lights[ i ].m_hShadow == hShadow )
			nIndex = i;
	}
	if ( nIndex < 0 )
	{
		// a free place: one not seen this frame or the frame before
		for ( int i = 0; i < m_Lights.Count() && nIndex < 0; i++ )
		{
			if ( m_Lights[ i ].m_nFrame < gpGlobals->framecount - 1 )
				nIndex = i;
		}
		if ( nIndex < 0 )
		{
			if ( m_Lights.Count() >= MAX_VOLUMETRIC_LIGHTS )
				return;
			nIndex = m_Lights.AddToTail();
		}
	}
	FidelityVolumetricLight_t &light = m_Lights[ nIndex ];
	light.m_hShadow = hShadow;
	light.m_State = state;
	light.m_WorldToTexture = worldToTexture;
	light.m_pShadowDepth = pShadowDepth;
	light.m_nFrame = gpGlobals->framecount;
}

// This frame's lights within reach and bright enough, nearest first, at most
// r_fidelity_volumetric_lights; then the sun (the widest ortho light)
int CFidelityVolumetric::CollectLights( const Vector &vecView )
{
	m_Drawn.RemoveAll();
	if ( !r_fidelity_volumetric.GetBool() )
		return 0;
	// without haze the lights add nothing (the sun has its own)
	const bool bHaze = r_fidelity_volumetric_density.GetFloat() > 0.0f;
	const float flReach = r_fidelity_volumetric_distance.GetFloat();
	int nSun = -1;
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		FidelityVolumetricLight_t &light = m_Lights[ i ];
		if ( light.m_nFrame != gpGlobals->framecount )
			continue;
		if ( light.m_State.m_bOrtho )
		{
			const float flWidth = light.m_State.m_fOrthoRight - light.m_State.m_fOrthoLeft;
			if ( nSun < 0 || flWidth > m_Lights[ nSun ].m_State.m_fOrthoRight - m_Lights[ nSun ].m_State.m_fOrthoLeft )
				nSun = i;
			continue;
		}
		if ( !bHaze )
			continue;
		const float flBright = MAX( light.m_State.m_Color[ 0 ], MAX( light.m_State.m_Color[ 1 ], light.m_State.m_Color[ 2 ] ) ) * light.m_State.m_fBrightnessScale;
		light.m_flDistance = vecView.DistTo( light.m_State.m_vecLightOrigin );
		if ( flBright <= 0.01f || light.m_flDistance > flReach + light.m_State.m_FarZ * 0.5f )
			continue;
		// sorted in: nearest first
		int nAt = 0;
		while ( nAt < m_Drawn.Count() && m_Lights[ m_Drawn[ nAt ] ].m_flDistance <= light.m_flDistance )
			nAt++;
		m_Drawn.InsertBefore( nAt, i );
	}
	const int nMax = clamp( r_fidelity_volumetric_lights.GetInt(), 0, MAX_VOLUMETRIC_SLOTS - 1 );
	while ( m_Drawn.Count() > nMax )
		m_Drawn.RemoveMultipleFromTail( 1 );
	if ( nSun >= 0 && r_fidelity_volumetric_sun.GetFloat() > 0.0f )
		m_Drawn.AddToTail( nSun );
	return m_Drawn.Count();
}

void CFidelityVolumetric::SetVector( IMaterial *pMaterial, int nIndex, float x, float y, float z, float w )
{
	bool bFound = false;
	IMaterialVar *pVar = pMaterial->FindVar( s_pVolumetricVectors[ nIndex ], &bFound, false );
	if ( bFound && pVar )
		pVar->SetVecValue( x, y, z, w );
}

void CFidelityVolumetric::Render( const CViewSetup &view, ITexture *pDepth )
{
	if ( !pDepth || CollectLights( view.origin ) == 0 || !EnsureMaterials() )
		return;
	ITexture *pTarget = materials->FindTexture( FIDELITY_VOLUMETRIC_RT, TEXTURE_GROUP_RENDER_TARGET, false );
	ITexture *pBlurTarget = materials->FindTexture( FIDELITY_VOLUMETRIC_BLUR_RT, TEXTURE_GROUP_RENDER_TARGET, false );
	const int nTargetW = pTarget->GetActualWidth(), nTargetH = pTarget->GetActualHeight();
	const int w = view.width, h = view.height;
	if ( w <= 0 || h <= 0 || nTargetW <= 0 || nTargetH <= 0 )
		return;
	const int qw = clamp( ( w + 3 ) / 4, 1, nTargetW );
	const int qh = clamp( ( h + 3 ) / 4, 1, nTargetH );

	// The depth: our full-size copy holds the view at its own place, the
	// engine's smaller one (fallback) the whole view scaled
	const int nDepthW = pDepth->GetActualWidth(), nDepthH = pDepth->GetActualHeight();
	const bool bFullSize = nDepthW >= view.x + w && nDepthH >= view.y + h && pDepth != GetFullFrameDepthTexture();
	const int nSrcX = bFullSize ? view.x : 0, nSrcY = bFullSize ? view.y : 0;
	const int nSrcW = bFullSize ? w : nDepthW, nSrcH = bFullSize ? h : nDepthH;

	// The view: eye, and the ray for one unit of view depth (as fidelity_common.h decodes depth)
	const float flNear = view.zNear;
	const float flFar = MAX( view.zFar, flNear + 1.0f );
	const float flDepthScale = r_fidelity_ssao_depthrange.GetFloat() * ( flFar - flNear ) / flFar;
	Vector vecForward, vecRight, vecUp;
	AngleVectors( view.angles, &vecForward, &vecRight, &vecUp );
	const float flTanX = tanf( DEG2RAD( clamp( view.fov, 1.0f, 179.0f ) * 0.5f ) );
	const float flTanY = flTanX * (float)h / (float)w;
	const float flDensity = MAX( r_fidelity_volumetric_density.GetFloat(), 0.0f );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( pTarget );
	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false );
	for ( int n = 0; n < m_Drawn.Count(); n++ )
	{
		const FidelityVolumetricLight_t &light = m_Lights[ m_Drawn[ n ] ];
		const FlashlightState_t &state = light.m_State;
		IMaterial *pMaterial = m_March[ n ];
		bool bFound = false;
		IMaterialVar *pVar = pMaterial->FindVar( "$depthtexture", &bFound, false );
		if ( bFound && pVar )
			pVar->SetTextureValue( pDepth );
		pVar = pMaterial->FindVar( "$spottexture", &bFound, false );
		if ( bFound && pVar && state.m_pSpotlightTexture )
			pVar->SetTextureValue( state.m_pSpotlightTexture );
		pVar = pMaterial->FindVar( "$shadowdepth", &bFound, false );
		if ( bFound && pVar )
			pVar->SetTextureValue( light.m_pShadowDepth );
		pVar = pMaterial->FindVar( "$shadows", &bFound, false );
		if ( bFound && pVar )
			pVar->SetIntValue( state.m_bEnableShadows ? 1 : 0 );

		SetVector( pMaterial, 0, view.origin.x, view.origin.y, view.origin.z, flDepthScale );
		SetVector( pMaterial, 1, vecForward.x, vecForward.y, vecForward.z, flNear );
		SetVector( pMaterial, 2, vecRight.x * flTanX, vecRight.y * flTanX, vecRight.z * flTanX, (float)qw );
		SetVector( pMaterial, 3, vecUp.x * flTanY, vecUp.y * flTanY, vecUp.z * flTanY, (float)qh );
		for ( int r = 0; r < 4; r++ )
			SetVector( pMaterial, 4 + r, light.m_WorldToTexture[ r ][ 0 ], light.m_WorldToTexture[ r ][ 1 ], light.m_WorldToTexture[ r ][ 2 ], light.m_WorldToTexture[ r ][ 3 ] );
		SetVector( pMaterial, 8, state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, state.m_FarZ );
		// the walk ends where the light can no longer reach (seen along the view)
		const bool bSun = state.m_bOrtho;
		const float flReach = bSun ? MAX( r_fidelity_volumetric_sun_reach.GetFloat(), 1.0f ) : light.m_flDistance + state.m_FarZ;
		const float flScale = ( bSun ? MAX( r_fidelity_volumetric_sun.GetFloat(), 0.0f ) : flDensity ) * state.m_fBrightnessScale;
		SetVector( pMaterial, 9, state.m_Color[ 0 ] * flScale, state.m_Color[ 1 ] * flScale, state.m_Color[ 2 ] * flScale, flReach );
		SetVector( pMaterial, 10, state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, bSun ? VOLUMETRIC_SUN_SCATTER : 0.0f );
		// the direction the light shines (the sun: parallel rays)
		matrix3x4_t matOrientation;
		QuaternionMatrix( state.m_quatOrientation, matOrientation );
		Vector vecShine;
		MatrixGetColumn( matOrientation, 0, vecShine );
		SetVector( pMaterial, 12, vecShine.x, vecShine.y, vecShine.z, bSun ? 1.0f : 0.0f );
		SetVector( pMaterial, 13, MistBase(), 1.0f / MAX( r_fidelity_mist_height.GetFloat(), 1.0f ), MAX( r_fidelity_mist.GetFloat(), 0.0f ), 0.0f );
		SetVector( pMaterial, 11, (float)nDepthW / nSrcW, (float)nDepthH / nSrcH, -(float)nSrcX / nSrcW, -(float)nSrcY / nSrcH );

		Fidelity_DrawScreenPass( pMaterial, 0, 0, qw, qh, nSrcX, nSrcY, nSrcW, nSrcH, nDepthW, nDepthH );
	}
	pRenderContext->PopRenderTargetAndViewport();

	// Blurred 4 x 4 (quarter size) and added onto the frame
	const float flMaxU = ( qw - 0.5f ) / nTargetW, flMaxV = ( qh - 0.5f ) / nTargetH;
	m_Blur.SetConstant( 0, 1.0f / nTargetW, 1.0f / nTargetH, flMaxU, flMaxV );
	pRenderContext->PushRenderTargetAndViewport( pBlurTarget );
	Fidelity_DrawScreenPass( m_Blur.Get(), 0, 0, qw, qh, 0, 0, qw, qh, nTargetW, nTargetH );
	pRenderContext->PopRenderTargetAndViewport();

	m_Composite.SetConstant( 0, 1.0f / nTargetW, 1.0f / nTargetH, 1.0f, 0.0f );
	m_Composite.SetConstant( 1, flMaxU, flMaxV, 0.0f, 0.0f );
	Fidelity_DrawScreenPass( m_Composite.Get(), view.x, view.y, view.width, view.height, 0, 0, qw, qh, nTargetW, nTargetH );
}

void Fidelity_VolumetricShadow( ClientShadowHandle_t hShadow, const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pShadowDepth )
{
	s_FidelityVolumetric.AddLight( hShadow, state, worldToTexture, pShadowDepth );
}

void Fidelity_RenderVolumetric( const CViewSetup &view, ITexture *pDepth )
{
	s_FidelityVolumetric.Render( view, pDepth );
}

bool Fidelity_VolumetricWillDraw()
{
	return s_FidelityVolumetric.CollectLights( MainViewOrigin() ) > 0;
}

void CFidelityVolumetric::Report( const Vector &vecEye, CUtlBuffer &info )
{
	info.Printf( "\nlights with a shadow map (last frame; flashlight, env_projectedtexture, sun):\n" );
	int nCount = 0;
	for ( int i = 0; i < m_Lights.Count(); i++ )
	{
		const FidelityVolumetricLight_t &light = m_Lights[ i ];
		if ( light.m_nFrame < gpGlobals->framecount - 1 )
			continue;
		const FlashlightState_t &state = light.m_State;
		nCount++;
		info.Printf( "  at %.0f %.0f %.0f (%.0f units away), %s, fov %.0f x %.0f, far %.0f, colour %.2f %.2f %.2f x %.2f, atten %.2f %.2f %.4f, light in the air: %s\n",
			state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, vecEye.DistTo( state.m_vecLightOrigin ),
			state.m_bOrtho ? "ortho (sun)" : "perspective", state.m_fHorizontalFOVDegrees, state.m_fVerticalFOVDegrees, state.m_FarZ,
			state.m_Color[ 0 ], state.m_Color[ 1 ], state.m_Color[ 2 ], state.m_fBrightnessScale,
			state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, m_Drawn.Find( i ) != m_Drawn.InvalidIndex() ? "yes" : "no" );
	}
	if ( nCount == 0 )
		info.Printf( "  none\n" );
}

void Fidelity_VolumetricReport( const Vector &vecEye, CUtlBuffer &info )
{
	s_FidelityVolumetric.Report( vecEye, info );
}
