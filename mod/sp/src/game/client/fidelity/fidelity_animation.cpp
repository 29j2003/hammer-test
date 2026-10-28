//========= mapbase-fidelity ==================================================//
//
// Purpose: Longer cross-fades between an NPC's body animations
//          (cl_fidelity_npc_blend, on by default).
//
// When an NPC's sequence changes (standing at ease -> flinch -> aiming), the
// client fades the old pose out over MIN( old sequence's fadeout, new one's
// fadein ) - 0.2 s by default, as the model sets it. Seen
// in game (Julius 25.10.2026, a video at 30 fps) the soldier went from at ease
// to his flinch in two frames. Fidelity_SequenceBlend stretches every such
// fade of an NPC to at least cl_fidelity_npc_blend_time. Left as they are:
//   - fades that are longer already,
//   - cuts the model asks for (a fade of 0; for STUDIO_SNAP and teleports,
//     EF_NOINTERP, the queue is emptied before this is called),
//   - dying NPCs (Mapbase's death pose code returns before),
//   - everything that is not an NPC (the player's view model, props, ragdolls).
// Only the look changes: the server, its hit boxes and the AI are untouched.
//
// Cost: a short loop over the fading sequences (one or two) per NPC set up.
//
//=============================================================================//

#include "cbase.h"
#include "c_baseanimating.h"
#include "sequence_Transitioner.h"
#include "fidelity_animation.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_fidelity_npc_blend( "cl_fidelity_npc_blend", "1", FCVAR_ARCHIVE, "NPCs cross-fade between their body animations (flinch, aim, run) at least cl_fidelity_npc_blend_time long. 0 = as Half-Life 2." );
ConVar cl_fidelity_npc_blend_time( "cl_fidelity_npc_blend_time", "0.3", FCVAR_SERVER_CAN_EXECUTE, "Shortest cross-fade in seconds between an NPC's body animations.", true, 0.0f, true, 1.0f );

void Fidelity_SequenceBlend( C_BaseAnimating *pAnimating, CSequenceTransitioner &transitioner )
{
	if ( !cl_fidelity_npc_blend.GetBool() || !pAnimating->IsNPC() )
		return;

	// the last entry is the current sequence; the ones before it are fading out
	const float flMin = cl_fidelity_npc_blend_time.GetFloat();
	for ( int i = 0; i < transitioner.m_animationQueue.Count() - 1; i++ )
	{
		C_AnimationLayer &layer = transitioner.m_animationQueue[ i ];
		if ( layer.m_flLayerFadeOuttime > 0.0f && layer.m_flLayerFadeOuttime < flMin )
			layer.m_flLayerFadeOuttime = flMin;
	}
}
