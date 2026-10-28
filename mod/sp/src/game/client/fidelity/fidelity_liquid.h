//========= mapbase-fidelity ==================================================//
//
// Purpose: Containers with liquid (Fidelity_Liquid materials).
//
//=============================================================================//

#ifndef FIDELITY_LIQUID_H
#define FIDELITY_LIQUID_H
#ifdef _WIN32
#pragma once
#endif

struct model_t;

//-----------------------------------------------------------------------------
// Called by C_PhysicsProp::OnInternalDrawModel (c_physicsprop.cpp, line
// marked "mapbase-fidelity"): true for models with a liquid material, which
// keep the normal lighting when they come to rest. A sleeping physics prop
// otherwise gets its lighting baked into the vertices (r_PhysPropStaticLighting)
// - seen in game: the bottle went dark or vanished for a moment when it came
// to rest; its liquid needs the light around it, not the baked one.
//-----------------------------------------------------------------------------
bool Fidelity_KeepsDynamicLight( const model_t *pModel );

#endif // FIDELITY_LIQUID_H
