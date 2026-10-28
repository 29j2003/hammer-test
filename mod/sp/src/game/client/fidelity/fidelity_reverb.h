//========= mapbase-fidelity ==================================================//
//
// Purpose: Automatic room reverb where the soundscape sets none.
//
//=============================================================================//

#ifndef FIDELITY_REVERB_H
#define FIDELITY_REVERB_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Called by C_SoundscapeSystem (lines marked "mapbase-fidelity"):
// a new soundscape starts (NULL = none), and a soundscape sets its own "dsp".
// While the active soundscape has a "dsp" key, the automatic reverb stays out
// of the way; mappers keep full control.
//-----------------------------------------------------------------------------
void Fidelity_SoundscapeStarted();
void Fidelity_SoundscapeSetDSP( int nRoomType );

#endif // FIDELITY_REVERB_H
