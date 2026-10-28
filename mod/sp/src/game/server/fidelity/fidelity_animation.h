//========= mapbase-fidelity ==================================================//
//
// Purpose: NPC gestures ease in and out instead of popping
//          (npc_fidelity_gesture_blend, on by default).
//
// A gesture is an animation layered over the NPC's body animation: reloading
// or shooting while running, hand signals, flinching when hit, drawing and
// holstering the weapon, opening a door. CBaseAnimatingOverlay can fade a
// layer in at its start and out at its end (m_flBlendIn / m_flBlendOut, as a
// share of the gesture, with a smooth curve), but nothing in the game sets
// them: every gesture starts and ends at full strength, and the arms jump.
//
// Fidelity_GestureBlend gives each new gesture (CBaseAnimatingOverlay::
// AddGesture, also RestartGesture) npc_fidelity_gesture_blend_in seconds of
// fading in and npc_fidelity_gesture_blend_out seconds of fading out. Attacks
// start at full strength - their kick is the point - and only fade out;
// flinches come in quickly (npc_fidelity_gesture_blend_flinch: at full
// strength in one frame, they made a hit soldier jerk, Julius 25.10.2026).
// Left as they are:
//   - looping gestures (a fade at each loop's end would make them pulse),
//   - gestures that stay until removed (autokill off: a fisherman's hat),
//   - layers someone blends by hand (scenes, the blended motor's turns add
//     their layers another way and are not touched),
//   - an NPC's own activities (the strider's death, the hunter's charge):
//     only the shared ones below LAST_SHARED_ACTIVITY.
// Only NPCs, not the player.
//
// Header only (included once, by BaseAnimatingOverlay.cpp): no new file in
// the server's project.
//
//=============================================================================//

#ifndef FIDELITY_ANIMATION_H
#define FIDELITY_ANIMATION_H
#ifdef _WIN32
#pragma once
#endif

static ConVar npc_fidelity_gesture_blend( "npc_fidelity_gesture_blend", "1", FCVAR_ARCHIVE, "NPC gestures (reload, signals, flinches, shooting while moving) fade in and out instead of popping. 0 = as Half-Life 2." );
static ConVar npc_fidelity_gesture_blend_in( "npc_fidelity_gesture_blend_in", "0.15", FCVAR_NONE, "Seconds a new NPC gesture fades in (attacks at once, flinches: npc_fidelity_gesture_blend_flinch)." );
static ConVar npc_fidelity_gesture_blend_flinch( "npc_fidelity_gesture_blend_flinch", "0.08", FCVAR_NONE, "Seconds a flinch gesture of an NPC hit fades in." );
static ConVar npc_fidelity_gesture_blend_out( "npc_fidelity_gesture_blend_out", "0.25", FCVAR_NONE, "Seconds an NPC gesture fades out at its end." );

// Called once for each new gesture layer; flDuration: its length in seconds
inline void Fidelity_GestureBlend( CBaseAnimatingOverlay *pOwner, CAnimationLayer &layer, float flDuration )
{
	if ( !npc_fidelity_gesture_blend.GetBool() || !pOwner->IsNPC() || flDuration <= 0.0f )
		return;
	if ( !layer.IsAutokill() || layer.m_bLooping || layer.IsAutoramp() )
		return;
	if ( layer.m_nActivity <= ACT_INVALID || layer.m_nActivity >= LAST_SHARED_ACTIVITY )
		return;
	const char *pszName = CAI_BaseNPC::GetActivityName( layer.m_nActivity );
	if ( !pszName )
		return;

	// at most a third of the gesture fading in, two fifths fading out
	float flInTime = npc_fidelity_gesture_blend_in.GetFloat();
	if ( Q_stristr( pszName, "ATTACK" ) != NULL )
		flInTime = 0.0f;
	else if ( Q_stristr( pszName, "FLINCH" ) != NULL )
		flInTime = npc_fidelity_gesture_blend_flinch.GetFloat();
	const float flIn = clamp( flInTime / flDuration, 0.0f, 0.3f );
	const float flOut = clamp( npc_fidelity_gesture_blend_out.GetFloat() / flDuration, 0.0f, 0.4f );
	layer.m_flBlendIn = flIn;
	layer.m_flBlendOut = flOut;
	// no frame at full strength before the first StudioFrameAdvance
	if ( flIn > 0.0f )
		layer.m_flWeight = 0.0f;
}

#endif // FIDELITY_ANIMATION_H
