//========= mapbase-fidelity ==================================================//
//
// Purpose: The only add-on source file the client project lists (a "unity"
//          file): it compiles all the others.
//
// The project files Visual Studio uses are generated from the VPC scripts.
// Every file added to fidelity_client.vpc made the existing project outdated,
// and the next build stopped until XBLAH's "Configure" ran again. With this
// file the file list stays the same: a new .cpp is added below, never to the
// VPC, so pulling and building is enough.
//
// Each .cpp ends with tier0/memdbgon.h (debug allocator macros), so
// memdbgoff.h switches them off again before the next file's includes.
//
//=============================================================================//

#include "cbase.h"

#include "fidelity_screenmaterial.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_texture.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_model.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_ssao.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_godrays.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_grade.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_shadows.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_exposure.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_audio.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_reverb.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_liquid_proxy.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_wind.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_glow.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_sky.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_viewmodel.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_splatter.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_eyes.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_parallax_info.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_volumetric.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_effects.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_animation.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_flashes.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_dynlights.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_wetness.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_cubemaps.cpp"
#include "tier0/memdbgoff.h"
#include "fidelity_report.cpp"
