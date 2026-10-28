//========= mapbase-fidelity ==================================================//
//
// Purpose: Final image passes: FXAA anti-aliasing, then the "grade":
//          sharpening, clarity, colour balance, exposure, lift / gamma / gain,
//          contrast, saturation, vibrance and vignette.
//
// Registered as a screen-space effect, so it runs after the engine's bloom,
// tone mapping and colour correction and before the HUD - no change to the
// view rendering code is needed. FXAA lives in the same effect so that it
// always runs before the sharpening (the order of separate screen-space
// effects is not defined). The look values are not archived on purpose:
// the mod sets them in cfg/fidelity.cfg and maps may change them at runtime
// (e.g. with point_clientcommand) without overwriting the player's config.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_screenmaterial.h"
#include "ScreenSpaceEffects.h"
#include "view_scene.h"
#include "rendertexture.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/itexture.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_grade( "r_fidelity_grade", "1", FCVAR_ARCHIVE, "Final image pass (sharpen, colour, vignette). 0 = off." );
ConVar r_fidelity_fxaa( "r_fidelity_fxaa", "1", FCVAR_ARCHIVE, "FXAA anti-aliasing (smooths jagged edges). 0 = off." );
ConVar r_fidelity_fxaa_threshold( "r_fidelity_fxaa_threshold", "0.125", FCVAR_NONE, "FXAA: local contrast (relative to the brightest neighbour) that counts as an edge." );
ConVar r_fidelity_fxaa_threshold_min( "r_fidelity_fxaa_threshold_min", "0.05", FCVAR_NONE, "FXAA: contrast below which dark areas are left alone." );
ConVar r_fidelity_fxaa_sharpness( "r_fidelity_fxaa_sharpness", "8", FCVAR_NONE, "FXAA: how far along long edges it blends (lower = blurrier, 2..8)." );
ConVar r_fidelity_sharpen( "r_fidelity_sharpen", "0.35", FCVAR_ARCHIVE, "Sharpening amount, 0..1." );
ConVar r_fidelity_clarity( "r_fidelity_clarity", "0.35", FCVAR_SERVER_CAN_EXECUTE, "Clarity: local contrast over a few pixels, brings out bump maps and texture detail. 0 = off .. 1 = strong." );
ConVar r_fidelity_exposure( "r_fidelity_exposure", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Brightness multiplier." );
ConVar r_fidelity_contrast( "r_fidelity_contrast", "0.18", FCVAR_SERVER_CAN_EXECUTE, "Soft S-curve contrast, 0..1." );
ConVar r_fidelity_saturation( "r_fidelity_saturation", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Saturation, 1 = unchanged, 0 = greyscale." );
ConVar r_fidelity_vibrance( "r_fidelity_vibrance", "0.2", FCVAR_SERVER_CAN_EXECUTE, "Extra saturation for dull colours, 0..1." );
ConVar r_fidelity_temperature( "r_fidelity_temperature", "0", FCVAR_SERVER_CAN_EXECUTE, "Colour temperature, -1 = cold/blue .. 1 = warm/orange." );
ConVar r_fidelity_tint( "r_fidelity_tint", "0", FCVAR_SERVER_CAN_EXECUTE, "Tint, -1 = magenta .. 1 = green." );
ConVar r_fidelity_lift( "r_fidelity_lift", "0", FCVAR_SERVER_CAN_EXECUTE, "Lifts the shadows, 0..0.2." );
ConVar r_fidelity_gamma( "r_fidelity_gamma", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Mid-tone gamma, >1 brightens mid-tones." );
ConVar r_fidelity_gain( "r_fidelity_gain", "1.0", FCVAR_SERVER_CAN_EXECUTE, "Highlight gain." );
ConVar r_fidelity_vignette( "r_fidelity_vignette", "0.18", FCVAR_SERVER_CAN_EXECUTE, "Vignette strength, 0..1." );
ConVar r_fidelity_vignette_radius( "r_fidelity_vignette_radius", "0.55", FCVAR_SERVER_CAN_EXECUTE, "Where the vignette starts (0.5 = edge of the screen height)." );
ConVar r_fidelity_vignette_softness( "r_fidelity_vignette_softness", "0.45", FCVAR_SERVER_CAN_EXECUTE, "Width of the vignette falloff." );

//-----------------------------------------------------------------------------
// Screen-space effect that draws the grade material.
//-----------------------------------------------------------------------------
class CFidelityGradeEffect : public IScreenSpaceEffect
{
public:
	CFidelityGradeEffect() : m_bEnabled( true ), m_bTriedInit( false ) {}

	virtual void Init();
	virtual void Shutdown();
	virtual void SetParameters( KeyValues *params ) {}
	virtual void Render( int x, int y, int w, int h );
	virtual void Enable( bool bEnable ) { m_bEnabled = bEnable; }
	virtual bool IsEnabled() { return m_bEnabled; }

private:
	bool m_bEnabled;
	bool m_bTriedInit;
	CFidelityScreenMaterial m_Material;
	CFidelityScreenMaterial m_FXAA;
};

ADD_SCREENSPACE_EFFECT( CFidelityGradeEffect, fidelity_grade );

void CFidelityGradeEffect::Init()
{
	// Materials are created on first use, when the render targets surely exist.
	m_bTriedInit = false;
}

void CFidelityGradeEffect::Shutdown()
{
	m_Material.Shutdown();
	m_FXAA.Shutdown();
	m_bTriedInit = false;
}

void CFidelityGradeEffect::Render( int x, int y, int w, int h )
{
	if ( !m_bEnabled || building_cubemaps.GetBool() )
		return;

	Fidelity_PrintTargetInfo( FIDELITY_INFO_POST, x, y, w, h );

	if ( !r_fidelity_grade.GetBool() && !r_fidelity_fxaa.GetBool() )
		return;

	if ( !g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_b() )
		return;

	if ( !m_bTriedInit )
	{
		m_bTriedInit = true;
		m_Material.Init( "__fidelity_grade", "fidelity_grade_ps20b", "_rt_FullFrameFB" );
		m_FXAA.Init( "__fidelity_fxaa", "fidelity_fxaa_ps20b", "_rt_FullFrameFB" );
		if ( !m_Material.IsValid() || !m_FXAA.IsValid() )
			Warning( "[fidelity] Grade / FXAA material is missing - are the shaders in shaders/fxc?\n" );
	}

	if ( w <= 0 || h <= 0 )
		return;

	Rect_t srcRect;
	ITexture *pFrame = GetFullFrameFrameBufferTexture( 0 );
	const int nTexW = pFrame->GetActualWidth();
	const int nTexH = pFrame->GetActualHeight();

	// FXAA first: it needs the unsharpened edges.
	if ( r_fidelity_fxaa.GetBool() && m_FXAA.IsValid() )
	{
		UpdateScreenEffectTexture( 0, x, y, w, h, false, &srcRect );
		m_FXAA.SetConstant( 0, 1.0f / nTexW, 1.0f / nTexH, r_fidelity_fxaa_threshold.GetFloat(), r_fidelity_fxaa_threshold_min.GetFloat() );
		m_FXAA.SetConstant( 1, clamp( r_fidelity_fxaa_sharpness.GetFloat(), 1.0f, 16.0f ), 0.0f, 0.0f, 0.0f );
		Fidelity_DrawScreenPass( m_FXAA.Get(), x, y, w, h, srcRect.x, srcRect.y, srcRect.width, srcRect.height, nTexW, nTexH );
	}

	if ( !r_fidelity_grade.GetBool() || !m_Material.IsValid() )
		return;

	UpdateScreenEffectTexture( 0, x, y, w, h, false, &srcRect );

	// Temperature and tint as simple per-channel multipliers
	const float flTemp = clamp( r_fidelity_temperature.GetFloat(), -1.0f, 1.0f );
	const float flTint = clamp( r_fidelity_tint.GetFloat(), -1.0f, 1.0f );
	const float flRed = 1.0f + 0.10f * flTemp - 0.03f * flTint;
	const float flGreen = 1.0f + 0.08f * flTint;
	const float flBlue = 1.0f - 0.10f * flTemp - 0.03f * flTint;

	const float flGamma = MAX( 0.1f, r_fidelity_gamma.GetFloat() );

	m_Material.SetConstant( 0, 1.0f / nTexW, 1.0f / nTexH, clamp( r_fidelity_sharpen.GetFloat(), 0.0f, 1.0f ), clamp( r_fidelity_vignette.GetFloat(), 0.0f, 1.0f ) );
	m_Material.SetConstant( 1, clamp( r_fidelity_contrast.GetFloat(), -1.0f, 1.0f ), MAX( 0.0f, r_fidelity_saturation.GetFloat() ), r_fidelity_vibrance.GetFloat(), MAX( 0.01f, r_fidelity_vignette_softness.GetFloat() ) );
	const float flExposure = MAX( 0.0f, r_fidelity_exposure.GetFloat() );
	m_Material.SetConstant( 2, flRed * flExposure, flGreen * flExposure, flBlue * flExposure, clamp( r_fidelity_clarity.GetFloat(), 0.0f, 2.0f ) );
	m_Material.SetConstant( 3, r_fidelity_lift.GetFloat(), 1.0f / flGamma, r_fidelity_gain.GetFloat(), r_fidelity_vignette_radius.GetFloat() );

	Fidelity_DrawScreenPass( m_Material.Get(), x, y, w, h, srcRect.x, srcRect.y, srcRect.width, srcRect.height, nTexW, nTexH );
}
