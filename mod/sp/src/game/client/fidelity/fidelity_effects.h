//========= mapbase-fidelity ==================================================//
//
// Purpose: Small world effects: blood pools under the dead, smoke from the
//          barrel after sustained fire, dust that hangs after bullet impacts,
//          the echo of shots and explosions far away, rain splashing.
//
//=============================================================================//

#ifndef FIDELITY_EFFECTS_H
#define FIDELITY_EFFECTS_H
#ifdef _WIN32
#pragma once
#endif

class CGameTrace;

//-----------------------------------------------------------------------------
// Called by PerformCustomEffects (fx_impact.cpp, line marked
// "mapbase-fidelity") for every bullet impact: a puff of dust that hangs for
// a moment on stone, plaster, tiles, dirt and wood.
//-----------------------------------------------------------------------------
void Fidelity_ImpactDust( const Vector &vecOrigin, const CGameTrace &tr, int iMaterial, int iScale );

//-----------------------------------------------------------------------------
// A shot of the player's weapon (from Fidelity_MuzzleFlashAt): after
// sustained fire the barrel smokes for a moment.
//-----------------------------------------------------------------------------
void Fidelity_MuzzleSmokeShot( int nMuzzleType );

//-----------------------------------------------------------------------------
// A shot (muzzle flash type, from Fidelity_MuzzleFlashAt) or an explosion (-1,
// from Fidelity_ExplosionLight) at vecOrigin: far away, its far-away echo
// (sound/fidelity/distant, made by deploy.bat) follows, delayed as sound is.
//-----------------------------------------------------------------------------
void Fidelity_DistantSound( const Vector &vecOrigin, int nMuzzleType );

//-----------------------------------------------------------------------------
// Called by CClient_Precipitation::SimulateRain (c_effects.cpp, line marked
// "mapbase-fidelity") when a raindrop hits something: a small splash on
// floors, roofs and walls too (the game splashes on water only).
//-----------------------------------------------------------------------------
void Fidelity_RainHit( const Vector &vecFrom, const Vector &vecTo );

#endif // FIDELITY_EFFECTS_H
