//========= mapbase-fidelity ==================================================//
//
// Purpose: NPCs cross-fade between their body animations a little longer, so
//          a soldier hit while standing at ease does not snap into his flinch
//          and his aim (cl_fidelity_npc_blend).
//
//=============================================================================//

#ifndef FIDELITY_ANIMATION_H
#define FIDELITY_ANIMATION_H
#ifdef _WIN32
#pragma once
#endif

class C_BaseAnimating;
class CSequenceTransitioner;

// Called by C_BaseAnimating::MaintainSequenceTransitions right after a
// possible sequence change was queued: stretches the fade of the sequences
// being left to at least cl_fidelity_npc_blend_time (NPCs only).
void Fidelity_SequenceBlend( C_BaseAnimating *pAnimating, CSequenceTransitioner &transitioner );

#endif // FIDELITY_ANIMATION_H
