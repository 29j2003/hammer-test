//========= mapbase-fidelity ==================================================//
//
// Purpose: The audio mixer (fidelity_audio.cpp) hears of explosions.
//
//=============================================================================//

#ifndef FIDELITY_AUDIO_H
#define FIDELITY_AUDIO_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// An explosion at vecOrigin (from Fidelity_ExplosionLight): nearby it numbs
// the ears for a moment (snd_fidelity_shock).
//-----------------------------------------------------------------------------
void Fidelity_AudioExplosion( const Vector &vecOrigin );

#endif // FIDELITY_AUDIO_H
