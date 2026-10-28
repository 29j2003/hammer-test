//========= mapbase-fidelity ==================================================//
//
// Purpose: Muzzle flashes and explosions as short projected-texture lights
//          (lit like the flashlight: bump maps, specular, models) - as
//          Entropy : Zero 2 does it.
//
//=============================================================================//

#ifndef FIDELITY_FLASHES_H
#define FIDELITY_FLASHES_H
#ifdef _WIN32
#pragma once
#endif

// A muzzle flash at vecOrigin (fidelity_glow.cpp's Fidelity_MuzzleFlashAt):
// the player's own (bFirstPerson, shining along the view) or an NPC's
// (along its weapon's muzzle). vecColor 0..1. True when a light was started
// (the engine light of the flash is dimmed then).
bool Fidelity_ProjectedMuzzleFlash( const Vector &vecOrigin, const Vector &vecColor, ClientEntityHandle_t hEntity, bool bFirstPerson );

// An explosion's light on the ground around it (from above, Fidelity_ExplosionLight)
void Fidelity_ProjectedExplosion( const Vector &vecOrigin );

#endif // FIDELITY_FLASHES_H
