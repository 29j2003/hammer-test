//========= mapbase-fidelity ==================================================//
//
// Purpose: Weapons in the hand pull back a little near walls, and have some
//          weight: they swing behind when turning, lean when strafing and
//          dip when jumping and landing.
//
// Half-Life 2 draws the weapon on top of everything, so close to a wall it
// seems to stick into it. Here a short trace from the eye along the view
// finds how close a wall (or a large prop) is; the closer, the further the
// weapon moves back towards the player and the more it tilts down (at most
// r_fidelity_weapon_wall_distance units and 10 degrees), smoothly in and out.
// Only the drawn weapon moves: aiming, bullets and the crosshair stay as they
// are. Its glow light moves with it (fidelity_glow.cpp follows the model).
//
// Weight (as in MMod, the HL2 mod most praised for how weapons feel): turning
// the view leaves the weapon a little behind - it turns after, rolls into the
// turn (at most 2.5 degrees) and settles; strafing leans it (at most 1.5
// degrees); a jump and above all a landing let it dip and spring back (a
// damped spring, stronger the harder the landing). Only the drawn weapon
// moves, the view never (no camera roll: it makes people sick). Stock HL2's
// own lag and bob stay. r_fidelity_weapon_sway 0 = off,
// r_fidelity_weapon_sway_scale = how much.
//
//=============================================================================//

#include "cbase.h"
#include "fidelity_viewmodel.h"
#include "c_baseviewmodel.h"
#include "view.h"
#include "view_shared.h"
#include "model_types.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar r_fidelity_weapon_wall( "r_fidelity_weapon_wall", "1", FCVAR_ARCHIVE, "Weapons in the hand pull back a little near walls. 0 = off." );
ConVar r_fidelity_weapon_wall_distance( "r_fidelity_weapon_wall_distance", "8", FCVAR_ARCHIVE, "How far (units) a weapon pulls back at most, right at a wall." );

// From this distance of a wall on, the weapon starts to pull back.
#define WEAPON_WALL_REACH		36.0f
#define WEAPON_WALL_TILT		10.0f		// degrees down, right at the wall
#define WEAPON_WALL_SPEED		8.0f		// how fast it follows (1 / seconds)

ConVar r_fidelity_weapon_sway( "r_fidelity_weapon_sway", "1", FCVAR_ARCHIVE, "Weapons in the hand swing behind when turning, lean when strafing, dip when landing. 0 = off." );
ConVar r_fidelity_weapon_sway_scale( "r_fidelity_weapon_sway_scale", "1.0", FCVAR_SERVER_CAN_EXECUTE, "How much weapons in the hand swing, lean and dip (1 = default)." );

#define SWAY_PER_DEGREE			0.008f		// degrees behind per degree/second of turning
#define SWAY_MAX				2.5f		// degrees, at most (Julius: "nicht zu stark")
#define SWAY_SPEED				9.0f		// how fast it catches up (1 / seconds)
#define SWAY_ROLL				0.4f		// roll into the turn, share of the yaw behind
#define LEAN_MAX				1.5f		// degrees of lean at full strafing speed
#define LEAN_SPEED_FULL			190.0f		// units / second: strafing that leans fully
#define LEAN_SPEED				6.0f
#define DIP_STIFFNESS			140.0f		// the spring that brings the weapon back after a dip
#define DIP_DAMPING				15.0f
#define DIP_MAX					3.0f		// units down, at most
#define DIP_LAND				30.0f		// push down on a hard landing (units / second)
#define DIP_LAND_FALL_MIN		150.0f		// falling speed from which landings dip
#define DIP_LAND_FALL_FULL		550.0f		// ... and dip fully
#define DIP_JUMP				10.0f

//-----------------------------------------------------------------------------
// Weight of the weapon (see top): once a frame, from the view's turning and
// the player's movement
//-----------------------------------------------------------------------------
struct FidelitySway_t
{
	QAngle m_angLast;		// the view last frame
	QAngle m_angBehind;		// how far the weapon is behind (pitch, yaw)
	float m_flLean;
	float m_flDip;			// units down
	float m_flDipSpeed;
	float m_flLastFall;		// vertical speed last frame
	bool m_bOnGround;
	bool m_bValid;
};

static FidelitySway_t s_Sway;

static void Fidelity_UpdateSway( C_BasePlayer *pOwner, const QAngle &angEye )
{
	const float dt = MIN( gpGlobals->frametime, 0.05f );
	if ( dt <= 0.0f )
		return;	// paused
	const bool bOnGround = pOwner && pOwner->GetGroundEntity() != NULL;
	const Vector vecVelocity = pOwner ? pOwner->GetAbsVelocity() : vec3_origin;
	// after a gap (a level change, a save loaded, the weapon put away) start
	// afresh: the turn since then is no turn
	static int s_nLastFrame = -100;
	if ( gpGlobals->framecount - s_nLastFrame > 2 )
		s_Sway.m_bValid = false;
	s_nLastFrame = gpGlobals->framecount;
	if ( !s_Sway.m_bValid )
	{
		s_Sway.m_angLast = angEye;
		s_Sway.m_angBehind.Init();
		s_Sway.m_flLean = s_Sway.m_flDip = s_Sway.m_flDipSpeed = 0.0f;
		s_Sway.m_flLastFall = vecVelocity.z;
		s_Sway.m_bOnGround = bOnGround;
		s_Sway.m_bValid = true;
		return;
	}

	// Turning: behind by the turning speed, catching up
	const float flPitchSpeed = AngleDiff( angEye.x, s_Sway.m_angLast.x ) / dt;
	const float flYawSpeed = AngleDiff( angEye.y, s_Sway.m_angLast.y ) / dt;
	s_Sway.m_angLast = angEye;
	const float flCatch = MIN( dt * SWAY_SPEED, 1.0f );
	s_Sway.m_angBehind.x += ( clamp( -flPitchSpeed * SWAY_PER_DEGREE, -SWAY_MAX, SWAY_MAX ) - s_Sway.m_angBehind.x ) * flCatch;
	s_Sway.m_angBehind.y += ( clamp( -flYawSpeed * SWAY_PER_DEGREE, -SWAY_MAX, SWAY_MAX ) - s_Sway.m_angBehind.y ) * flCatch;

	// Strafing: lean
	Vector vecRight;
	AngleVectors( angEye, NULL, &vecRight, NULL );
	const float flSide = bOnGround ? DotProduct( vecVelocity, vecRight ) / LEAN_SPEED_FULL : 0.0f;
	s_Sway.m_flLean += ( clamp( flSide, -1.0f, 1.0f ) * LEAN_MAX - s_Sway.m_flLean ) * MIN( dt * LEAN_SPEED, 1.0f );

	// Jumping and landing: a push on a damped spring
	if ( bOnGround && !s_Sway.m_bOnGround )
		s_Sway.m_flDipSpeed += DIP_LAND * clamp( ( -s_Sway.m_flLastFall - DIP_LAND_FALL_MIN ) / ( DIP_LAND_FALL_FULL - DIP_LAND_FALL_MIN ), 0.0f, 1.0f );
	else if ( !bOnGround && s_Sway.m_bOnGround && vecVelocity.z > 100.0f )
		s_Sway.m_flDipSpeed += DIP_JUMP;
	s_Sway.m_bOnGround = bOnGround;
	s_Sway.m_flLastFall = vecVelocity.z;
	// two half steps (the spring is stiff)
	for ( int i = 0; i < 2; i++ )
	{
		const float h = dt * 0.5f;
		s_Sway.m_flDipSpeed += ( -DIP_STIFFNESS * s_Sway.m_flDip - DIP_DAMPING * s_Sway.m_flDipSpeed ) * h;
		s_Sway.m_flDip = clamp( s_Sway.m_flDip + s_Sway.m_flDipSpeed * h, -DIP_MAX, DIP_MAX );
	}
}

// The weapon's weight on its place (see top)
static void Fidelity_ApplySway( Vector &vecOrigin, QAngle &angAngles )
{
	const float flScale = MAX( r_fidelity_weapon_sway_scale.GetFloat(), 0.0f );
	Vector vecUp, vecRight;
	AngleVectors( angAngles, NULL, &vecRight, &vecUp );
	angAngles.x += s_Sway.m_angBehind.x * flScale + s_Sway.m_flDip * 1.2f * flScale;
	angAngles.y += s_Sway.m_angBehind.y * flScale;
	angAngles.z += ( s_Sway.m_angBehind.y * SWAY_ROLL + s_Sway.m_flLean ) * flScale;
	vecOrigin -= vecUp * ( s_Sway.m_flDip * flScale );
	vecOrigin -= vecRight * ( s_Sway.m_angBehind.y * 0.08f * flScale );
}

void Fidelity_ViewModelPlace( C_BasePlayer *pOwner, const Vector &vecEye, const QAngle &angEye, Vector &vecOrigin, QAngle &angAngles )
{
	static float s_flPull = 0.0f;
	static int s_nFrame = -1;
	const bool bNewFrame = s_nFrame != gpGlobals->framecount;
	s_nFrame = gpGlobals->framecount;

	if ( !r_fidelity_weapon_sway.GetBool() )
		s_Sway.m_bValid = false;
	else
	{
		if ( bNewFrame )
			Fidelity_UpdateSway( pOwner, angEye );
		Fidelity_ApplySway( vecOrigin, angAngles );
	}

	if ( !r_fidelity_weapon_wall.GetBool() )
	{
		s_flPull = 0.0f;
		return;
	}

	// Once a frame (there can be more than one view model)
	if ( bNewFrame )
	{
		Vector vecForward;
		AngleVectors( angEye, &vecForward );
		trace_t tr;
		UTIL_TraceLine( vecEye, vecEye + vecForward * WEAPON_WALL_REACH, MASK_SOLID, pOwner, COLLISION_GROUP_NONE, &tr );
		const float flTarget = ( tr.fraction < 1.0f && !tr.startsolid ) ? 1.0f - tr.fraction : 0.0f;
		s_flPull += ( flTarget - s_flPull ) * MIN( gpGlobals->frametime * WEAPON_WALL_SPEED, 1.0f );
	}
	if ( s_flPull <= 0.001f )
		return;

	Vector vecForward, vecUp;
	AngleVectors( angAngles, &vecForward, NULL, &vecUp );
	const float flPull = SimpleSpline( s_flPull );
	vecOrigin -= vecForward * ( flPull * MAX( r_fidelity_weapon_wall_distance.GetFloat(), 0.0f ) );
	vecOrigin -= vecUp * ( flPull * 1.5f );
	angAngles.x += flPull * WEAPON_WALL_TILT;
}
