//========= mapbase-fidelity ==================================================//
//
// Purpose: Calmer automatic exposure (HDR eye adaptation).
//
// Source measures the screen's brightness every frame (a histogram, one
// bucket at a time) and sets a new exposure goal from it. Looking from a dark
// wall to a bright window and back makes that goal jump, and the image pumps
// brighter and darker with small turns of the head. This filter sits between
// the measurement and the engine:
//
//   - it works in stops (log2), so a change from 1 to 2 feels like 2 to 4
//   - changes smaller than r_fidelity_exposure_deadzone stops are ignored, so
//     small head movements no longer change the brightness at all
//   - beyond that it follows smoothly, like eyes: quickly when it gets bright
//     (r_fidelity_exposure_darken_time), slowly when it gets dark
//     (r_fidelity_exposure_brighten_time); frame rate does not matter
//
// The map's limits (env_tonemap_controller min / max) still apply. After a
// level load, a reset or a pause of more than a second it jumps straight to
// the goal.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_exposure.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_exposure_smooth( "r_fidelity_exposure_smooth", "1", FCVAR_ARCHIVE, "Calmer automatic exposure (dead zone, eye-like adaptation). 0 = engine default." );
ConVar r_fidelity_exposure_deadzone( "r_fidelity_exposure_deadzone", "0.2", FCVAR_SERVER_CAN_EXECUTE, "Exposure changes smaller than this (in stops) are ignored." );
ConVar r_fidelity_exposure_darken_time( "r_fidelity_exposure_darken_time", "0.5", FCVAR_SERVER_CAN_EXECUTE, "Seconds to adapt when the view gets brighter (exposure goes down)." );
ConVar r_fidelity_exposure_debug( "r_fidelity_exposure_debug", "0", FCVAR_NONE, "Shows the exposure goal of the engine and what the filter makes of it." );
ConVar r_fidelity_exposure_brighten_time( "r_fidelity_exposure_brighten_time", "1.5", FCVAR_SERVER_CAN_EXECUTE, "Seconds to adapt when the view gets darker (exposure goes up)." );

static float s_flExposureStops = 0.0f;
static float s_flLastTime = -1.0f;
static bool s_bJump = true;

void Fidelity_ResetExposure()
{
	s_bJump = true;
}

float Fidelity_StabilizeExposure( float flGoal, float flMin, float flMax )
{
	if ( !r_fidelity_exposure_smooth.GetBool() || flGoal <= 0.0f )
	{
		s_bJump = true;
		if ( r_fidelity_exposure_debug.GetBool() )
			engine->Con_NPrintf( 3, "[fidelity exposure] engine goal %.3f (filter off)", flGoal );
		return flGoal;
	}

	const float flNow = gpGlobals->realtime;
	const float flDelta = flNow - s_flLastTime;
	const float flGoalStops = log2f( flGoal );
	s_flLastTime = flNow;

	if ( s_bJump || flDelta < 0.0f || flDelta > 1.0f )
	{
		s_flExposureStops = flGoalStops;
		s_bJump = false;
	}
	else
	{
		float flDiff = flGoalStops - s_flExposureStops;
		const float flDeadZone = MAX( r_fidelity_exposure_deadzone.GetFloat(), 0.0f );
		if ( fabsf( flDiff ) > flDeadZone )
		{
			// Only the part outside the dead zone counts, so it settles at its edge.
			flDiff -= ( flDiff > 0.0f ) ? flDeadZone : -flDeadZone;
			const float flTime = ( flDiff > 0.0f ) ? r_fidelity_exposure_brighten_time.GetFloat() : r_fidelity_exposure_darken_time.GetFloat();
			s_flExposureStops += flDiff * ( 1.0f - expf( -flDelta / MAX( flTime, 0.01f ) ) );
		}
	}

	const float flResult = clamp( exp2f( s_flExposureStops ), flMin, flMax );
	if ( r_fidelity_exposure_debug.GetBool() )
		engine->Con_NPrintf( 3, "[fidelity exposure] engine goal %.3f -> filtered %.3f  (map range %.2f .. %.2f)", flGoal, flResult, flMin, flMax );
	return flResult;
}
