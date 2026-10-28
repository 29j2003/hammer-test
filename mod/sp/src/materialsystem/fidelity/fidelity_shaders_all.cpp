//========= mapbase-fidelity ==================================================//
//
// Purpose: The only shader source file the fidelity shader DLL project lists
//          (a "unity" file): it compiles all the others. A new shader is added
//          below, never to game_shader_dx9_fidelity.vpc, so pulling and
//          building stays enough (no XBLAH "Configure").
//
// Each shader ends with tier0/memdbgon.h, so memdbgoff.h switches the debug
// allocator macros off again before the next file's includes.
//
//=============================================================================//

#include "fidelity_liquid.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_sky.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_parallax.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_volumetric.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_softparticle.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_water.cpp"
