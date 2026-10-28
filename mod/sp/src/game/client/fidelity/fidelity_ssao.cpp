//========= mapbase-fidelity ==================================================//
//
// Purpose: Screen-space ambient occlusion (SSAO) for Mapbase singleplayer.
//
// The pass runs inside the main view right after opaque geometry is drawn, so
// translucent objects, particles, the viewmodel and the HUD are not darkened.
// Depth comes from the frame buffer's destination alpha, which the engine
// fills with linear depth for the main view. Fidelity_UpdateDepthTexture
// copies it at full size to _rt_FidelityDepth (the engine's own copy,
// _rt_FullFrameDepth, can be much smaller than the screen and is only the
// fallback). The copy of the frame buffer in _rt_FullFrameFB is only used for
// colour: its alpha is not guaranteed to hold the depth. That depth is 8 bit
// and covers about 512 units in front of the camera, so the effect is
// near-field "contact" occlusion that fades out with distance.
//
// Passes (all full resolution):
//   1. copy the frame buffer colour to _rt_FullFrameFB
//   2. occlusion           depth                    -> _rt_FidelityAO0
//   3. horizontal blur     _rt_FidelityAO0          -> _rt_FidelityAO1
//   4. vertical blur and composite (scene colour x occlusion) -> frame buffer
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_ssao.h"
#include "fidelity_volumetric.h"
#include "fidelity_godrays.h"
#include "fidelity_screenmaterial.h"
#include "view_scene.h"
#include "view_shared.h"
#include "rendertexture.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/itexture.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_ssao( "r_fidelity_ssao", "1", FCVAR_ARCHIVE, "Screen-space ambient occlusion. 0 = off, 1 = on, 2 = show only the occlusion (debug view)." );
ConVar r_fidelity_ssao_quality( "r_fidelity_ssao_quality", "1", FCVAR_ARCHIVE, "SSAO quality: 0 = fast (4 sample pairs), 1 = high (8 sample pairs)." );
ConVar r_fidelity_ssao_radius( "r_fidelity_ssao_radius", "28", FCVAR_SERVER_CAN_EXECUTE, "SSAO sampling radius in world units." );
ConVar r_fidelity_ssao_strength( "r_fidelity_ssao_strength", "1.6", FCVAR_SERVER_CAN_EXECUTE, "SSAO darkening strength." );
ConVar r_fidelity_ssao_bias( "r_fidelity_ssao_bias", "1.5", FCVAR_SERVER_CAN_EXECUTE, "Depth difference in units that is ignored (hides 8 bit depth steps)." );
ConVar r_fidelity_ssao_fade_start( "r_fidelity_ssao_fade_start", "300", FCVAR_SERVER_CAN_EXECUTE, "Distance in units where SSAO starts to fade out." );
ConVar r_fidelity_ssao_fade_end( "r_fidelity_ssao_fade_end", "460", FCVAR_SERVER_CAN_EXECUTE, "Distance in units where SSAO is gone. Keep below r_fidelity_ssao_depthrange." );
ConVar r_fidelity_ssao_max_pixels( "r_fidelity_ssao_max_pixels", "0.05", FCVAR_SERVER_CAN_EXECUTE, "Largest sampling radius on screen, as a fraction of the screen height." );
ConVar r_fidelity_ssao_lumaprotect( "r_fidelity_ssao_lumaprotect", "0.45", FCVAR_SERVER_CAN_EXECUTE, "How much bright (directly lit) pixels are spared from occlusion. 0 = not at all." );
ConVar r_fidelity_ssao_blur_sharpness( "r_fidelity_ssao_blur_sharpness", "12", FCVAR_SERVER_CAN_EXECUTE, "How strongly the blur stops at depth edges." );
ConVar r_fidelity_ssao_depthrange( "r_fidelity_ssao_depthrange", "512", FCVAR_NONE, "Depth range the engine encodes in destination alpha (matches mat_dof_constant)." );

#define FIDELITY_AO_RT0 "_rt_FidelityAO0"
#define FIDELITY_AO_RT1 "_rt_FidelityAO1"

//-----------------------------------------------------------------------------
// Owns the render targets and materials of the SSAO pass.
//-----------------------------------------------------------------------------
class CFidelitySSAO : public CAutoGameSystem
{
public:
	CFidelitySSAO() : CAutoGameSystem( "CFidelitySSAO" ), m_bReady( false ), m_bFailed( false ) {}

	virtual bool Init();
	virtual void Shutdown();

	void Render( const CViewSetup &view, ITexture *pDepth );

private:
	bool EnsureMaterials();

	bool m_bReady;
	bool m_bFailed;		// materials missing: not tried again this session (the ConVar stays as the player set it)
	CTextureReference m_AOTexture0;
	CTextureReference m_AOTexture1;

	CFidelityScreenMaterial m_OcclusionHigh;
	CFidelityScreenMaterial m_OcclusionLow;
	CFidelityScreenMaterial m_BlurH;
	CFidelityScreenMaterial m_Composite;
};

static CFidelitySSAO s_FidelitySSAO;

bool CFidelitySSAO::Init()
{
	if ( !g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_b() )
	{
		DevMsg( "[fidelity] SSAO needs pixel shader 2.0b, disabled.\n" );
		return true;
	}

	// Same allocation pattern the client shadow manager uses for its depth textures.
	materials->BeginRenderTargetAllocation();
	m_AOTexture0.InitRenderTarget( 1, 1, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, false, FIDELITY_AO_RT0 );
	m_AOTexture1.InitRenderTarget( 1, 1, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, false, FIDELITY_AO_RT1 );
	materials->EndRenderTargetAllocation();

	return true;
}

void CFidelitySSAO::Shutdown()
{
	m_OcclusionHigh.Shutdown();
	m_OcclusionLow.Shutdown();
	m_BlurH.Shutdown();
	m_Composite.Shutdown();
	m_AOTexture0.Shutdown();
	m_AOTexture1.Shutdown();
	m_bReady = false;
	m_bFailed = false;
}

bool CFidelitySSAO::EnsureMaterials()
{
	if ( m_bReady )
		return true;
	if ( m_bFailed )
		return false;

	if ( !m_AOTexture0.IsValid() || !m_AOTexture1.IsValid() )
		return false;

	m_OcclusionHigh.Init( "__fidelity_ssao", "fidelity_ssao_ps20b", FIDELITY_DEPTH_RT );
	m_OcclusionLow.Init( "__fidelity_ssao_low", "fidelity_ssao_low_ps20b", FIDELITY_DEPTH_RT );
	m_BlurH.Init( "__fidelity_ssao_blur", "fidelity_ssao_blur_ps20b", FIDELITY_AO_RT0, FIDELITY_DEPTH_RT );
	m_Composite.Init( "__fidelity_ssao_composite", "fidelity_ssao_composite_ps20b", FIDELITY_AO_RT1, FIDELITY_DEPTH_RT, "_rt_FullFrameFB" );

	m_bReady = m_OcclusionHigh.IsValid() && m_OcclusionLow.IsValid() && m_BlurH.IsValid() && m_Composite.IsValid();
	if ( !m_bReady )
	{
		Warning( "[fidelity] SSAO materials are missing - are the shaders in shaders/fxc? SSAO is off until the game restarts.\n" );
		m_bFailed = true;
	}
	return m_bReady;
}

// On, and darkening something (a map may set the strength to 0 for a scene:
// then its three passes are skipped; the debug view always draws)
static bool Fidelity_SSAOWillDraw()
{
	if ( !r_fidelity_ssao.GetBool() || building_cubemaps.GetBool() )
		return false;
	return r_fidelity_ssao.GetInt() == 2 || r_fidelity_ssao_strength.GetFloat() > 0.0f;
}

void CFidelitySSAO::Render( const CViewSetup &view, ITexture *pDepth )
{
	if ( !Fidelity_SSAOWillDraw() )
		return;

	if ( !EnsureMaterials() )
		return;

	m_OcclusionHigh.SetTexture( 0, pDepth );
	m_OcclusionLow.SetTexture( 0, pDepth );
	m_BlurH.SetTexture( 1, pDepth );
	m_Composite.SetTexture( 1, pDepth );

	CMatRenderContextPtr pRenderContext( materials );

	// 1. Snapshot the colour of the opaque scene (depth is in pDepth already).
	Rect_t srcRect;
	UpdateScreenEffectTexture( 0, view.x, view.y, view.width, view.height, false, &srcRect );
	ITexture *pFrame = GetFullFrameFrameBufferTexture( 0 );
	const int nTexW = pFrame->GetActualWidth();
	const int nTexH = pFrame->GetActualHeight();
	const int w = srcRect.width;
	const int h = srcRect.height;
	if ( w <= 0 || h <= 0 )
		return;

	// Depth decoding, see fidelity_common.h
	const float flNear = view.zNear;
	const float flFar = MAX( view.zFar, flNear + 1.0f );
	const float flDepthScale = r_fidelity_ssao_depthrange.GetFloat() * ( flFar - flNear ) / flFar;

	// Pixels per world unit at depth 1 (horizontal fov across the view width).
	const float flTanHalfFov = tanf( DEG2RAD( clamp( view.fov, 1.0f, 179.0f ) * 0.5f ) );
	const float flProjScale = 0.5f * (float)w / flTanHalfFov;

	const float flFadeStart = r_fidelity_ssao_fade_start.GetFloat();
	const float flFadeEnd = MAX( r_fidelity_ssao_fade_end.GetFloat(), flFadeStart + 1.0f );
	const float flMaxPixels = MAX( 2.0f, r_fidelity_ssao_max_pixels.GetFloat() * (float)h );

	// 2. Occlusion
	CFidelityScreenMaterial &occlusion = r_fidelity_ssao_quality.GetInt() > 0 ? m_OcclusionHigh : m_OcclusionLow;
	occlusion.SetConstant( 0, 1.0f / nTexW, 1.0f / nTexH, (float)nTexW, (float)nTexH );
	occlusion.SetConstant( 1, flDepthScale, flNear, flProjScale, MAX( 1.0f, r_fidelity_ssao_radius.GetFloat() ) );
	occlusion.SetConstant( 2, r_fidelity_ssao_strength.GetFloat(), r_fidelity_ssao_bias.GetFloat(), flFadeStart, 1.0f / ( flFadeEnd - flFadeStart ) );
	occlusion.SetConstant( 3, flMaxPixels, 1.5f, 0.0f, 0.0f );

	pRenderContext->PushRenderTargetAndViewport( m_AOTexture0 );
	Fidelity_DrawScreenPass( occlusion.Get(), 0, 0, w, h, srcRect.x, srcRect.y, w, h, nTexW, nTexH );
	pRenderContext->PopRenderTargetAndViewport();

	// 3. Horizontal blur
	const float flSharpness = r_fidelity_ssao_blur_sharpness.GetFloat();
	m_BlurH.SetConstant( 0, 1.0f / nTexW, 0.0f, flSharpness, 0.0f );
	m_BlurH.SetConstant( 1, flDepthScale, flNear, 0.0f, 0.0f );

	pRenderContext->PushRenderTargetAndViewport( m_AOTexture1 );
	Fidelity_DrawScreenPass( m_BlurH.Get(), 0, 0, w, h, srcRect.x, srcRect.y, w, h, nTexW, nTexH );
	pRenderContext->PopRenderTargetAndViewport();

	// 4. Vertical blur + composite into the frame buffer
	m_Composite.SetConstant( 0, 0.0f, 1.0f / nTexH, flSharpness, 0.0f );
	m_Composite.SetConstant( 1, flDepthScale, flNear, 0.0f, 0.0f );
	m_Composite.SetConstant( 2, r_fidelity_ssao.GetInt() == 2 ? 1.0f : 0.0f, r_fidelity_ssao_lumaprotect.GetFloat(), 0.0f, 0.0f );

	Fidelity_DrawScreenPass( m_Composite.Get(), view.x, view.y, view.width, view.height, srcRect.x, srcRect.y, w, h, nTexW, nTexH );
}

void Fidelity_RenderSSAO( const CViewSetup &view )
{
	Fidelity_PrintTargetInfo( FIDELITY_INFO_SCENE, view.x, view.y, view.width, view.height );

	// The depth copy costs a full-screen pass: only when a pass reads it.
	if ( !Fidelity_SSAOWillDraw() && !Fidelity_GodRaysWillDraw( view ) && !Fidelity_VolumetricWillDraw() )
		return;

	// Before any pass draws: they overwrite destination alpha.
	ITexture *pDepth = Fidelity_UpdateDepthTexture( view.x, view.y, view.width, view.height );

	s_FidelitySSAO.Render( view, pDepth );

	// Light shafts need the same moment (opaque scene done, depth still in
	// destination alpha), so they share the one hook in viewrender.cpp.
	Fidelity_RenderGodRays( view, pDepth );

	// Light in the air of the flashlight and projected textures (after the
	// shafts: it takes their quarter-size target)
	Fidelity_RenderVolumetric( view, pDepth );
}
