//========= mapbase-fidelity ==================================================//
//
// Purpose: Light from glowing things and from muzzle flashes.
//
//=============================================================================//

#ifndef FIDELITY_GLOW_H
#define FIDELITY_GLOW_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Called by CTempEnts::MuzzleFlash (lines marked "mapbase-fidelity"): a short
// flash of light on the world at the muzzle. The engine's own muzzle flash
// light (muzzleflash_light) only lights models.
//-----------------------------------------------------------------------------
void Fidelity_MuzzleFlash( int nType, ClientEntityHandle_t hEntity, int nAttachment, bool bFirstPerson );
void Fidelity_MuzzleFlashAt( const Vector &vecOrigin, int nType, ClientEntityHandle_t hEntity, bool bFirstPerson );

//-----------------------------------------------------------------------------
// Called by the "BoltImpact" effect (c_stickybolt.cpp, line marked
// "mapbase-fidelity"): a crossbow bolt stuck at vecOrigin, flying along
// vecDirection, glows on there and cools down.
//-----------------------------------------------------------------------------
void Fidelity_BoltImpact( const Vector &vecOrigin, const Vector &vecDirection );

//-----------------------------------------------------------------------------
// Called by C_BaseExplosionEffect::Create (fx_explosion.cpp, line marked
// "mapbase-fidelity", after the debris - the SDK never calls its own
// CreateDynamicLight, the hook first sat there and was never reached): a
// fireball light that fades from hot yellow to red in ~1 s (bLight: not with
// TE_EXPLFLAG_NODLIGHTS), and the mixer and the far-away echo hear of it.
//-----------------------------------------------------------------------------
void Fidelity_ExplosionLight( const Vector &vecOrigin, bool bLight );

//-----------------------------------------------------------------------------
// Called by the muzzle flashes of NPC and vehicle guns that only had a light
// for models (helicopter/APC, gunship, hunter, strider, airboat; lines marked
// "mapbase-fidelity"): the same short light on the world as
// Fidelity_MuzzleFlash, in the colour of pszMaterial (the flash sprite) or
// vecColor.
//-----------------------------------------------------------------------------
void Fidelity_MuzzleLight( const Vector &vecOrigin, const char *pszMaterial, const Vector &vecColor, float flRadius, ClientEntityHandle_t hEntity );

//-----------------------------------------------------------------------------
// Called by the tracers (FX_AR2Tracer, FX_GunshipTracer, FX_StriderTracer,
// FX_HunterTracer, helicopter and airboat tracers; lines marked
// "mapbase-fidelity"): a light flying along with the tracer, in the colour of
// pszMaterial (NULL: the blue-white of pulse rounds); flScale for thick
// tracers (radius and brightness).
//-----------------------------------------------------------------------------
void Fidelity_TracerLight( const Vector &vecStart, const Vector &vecDir, float flSpeed, float flDistance, const char *pszMaterial = NULL, float flScale = 1.0f );

//-----------------------------------------------------------------------------
// Called by PerformCustomEffects (fx_impact.cpp) where a bullet throws
// sparks off metal, and by FX_ElectricSpark (fx_sparks.cpp; env_spark, broken
// Combine machines) - lines marked "mapbase-fidelity": a tiny, very short
// light in front of the surface, warm or (bElectric) blue-white.
//-----------------------------------------------------------------------------
void Fidelity_SparkLight( const Vector &vecOrigin, const Vector &vecNormal, bool bElectric = false );

//-----------------------------------------------------------------------------
// Called by C_NPC_Vortigaunt::ReceiveMessage (c_vort_charge_token.cpp, lines
// marked "mapbase-fidelity"): a vortigaunt's zap strikes vecHit coming from
// vecFrom (its hands); a green flash there (flScale 1: and at the hands;
// smaller for the beams of its charge, flScale < 1).
//-----------------------------------------------------------------------------
void Fidelity_ZapLight( const Vector &vecFrom, const Vector &vecHit, float flScale );

//-----------------------------------------------------------------------------
// A dead body: a ragdoll, or an NPC that is no longer alive (its glow goes
// dark, fidelity_eyes.cpp darkens its glowing eyes)
//-----------------------------------------------------------------------------
class C_BaseEntity;
bool Fidelity_IsDead( C_BaseEntity *pEntity );

#endif // FIDELITY_GLOW_H
