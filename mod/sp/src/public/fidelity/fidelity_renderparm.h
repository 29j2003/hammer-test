//========= mapbase-fidelity ==================================================//
//
// Purpose: The rendering parameters client.dll hands to the fidelity shader
//          DLL every frame (IMatRenderContext::SetVectorRenderingParameter /
//          SetFloatRenderingParameter, read by IShaderDynamicAPI::Get...).
//
// The engine keeps MAX_VECTOR_RENDER_PARMS (20) vectors and
// MAX_FLOAT_RENDER_PARMS (20) floats per render context, in the render
// thread's order; renderparm.h names only the first few (Mapbase: up to
// VECTOR_RENDERPARM_WIND_DIRECTION = 10, FLOAT_RENDERPARM_MINIMUMLIGHTING = 0).
// The rest is ours. Zero (the engine's start value) means "nothing": a shader
// DLL newer than the client draws as before.
//
//=============================================================================//

#ifndef FIDELITY_RENDERPARM_H
#define FIDELITY_RENDERPARM_H
#ifdef _WIN32
#pragma once
#endif

#include "renderparm.h"

// The nearest dynamic lights (per pixel in Fidelity_Parallax: their relief and highlight)
#define FIDELITY_DYNAMIC_LIGHTS					4
// vectors: light n's position at FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2n, its colour (linear) at + 2n + 1
#define FIDELITY_VECTOR_RENDERPARM_LIGHTS		( VECTOR_RENDERPARM_WIND_DIRECTION + 1 )
// floats: light n's radius at FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII + n (0 = no light)
#define FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII	( FLOAT_RENDERPARM_MINIMUMLIGHTING + 1 )
// how wet the world is from rain, 0..1 (Fidelity_Parallax)
#define FIDELITY_FLOAT_RENDERPARM_WETNESS		( FIDELITY_FLOAT_RENDERPARM_LIGHT_RADII + FIDELITY_DYNAMIC_LIGHTS )
// where the rain reaches (texture fidelity/rain_map, fidelity_wetness.cpp): its corner in the world (x, y),
// its size (units), the height of its value 0 and the heights it holds
#define FIDELITY_FLOAT_RENDERPARM_RAINMAP_X		( FIDELITY_FLOAT_RENDERPARM_WETNESS + 1 )
#define FIDELITY_FLOAT_RENDERPARM_RAINMAP_Y		( FIDELITY_FLOAT_RENDERPARM_WETNESS + 2 )
#define FIDELITY_FLOAT_RENDERPARM_RAINMAP_SIZE	( FIDELITY_FLOAT_RENDERPARM_WETNESS + 3 )
#define FIDELITY_FLOAT_RENDERPARM_RAINMAP_BASE	( FIDELITY_FLOAT_RENDERPARM_WETNESS + 4 )
#define FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE	( FIDELITY_FLOAT_RENDERPARM_WETNESS + 5 )
// the sun for Water_Fidelity's glint: the direction towards it (vector), its colour x glint strength
// (linear, floats r g b; 0 = no sun) and the strength of the foam at the shore (float)
#define FIDELITY_VECTOR_RENDERPARM_SUN_DIRECTION	( FIDELITY_VECTOR_RENDERPARM_LIGHTS + 2 * FIDELITY_DYNAMIC_LIGHTS )
#define FIDELITY_FLOAT_RENDERPARM_SUN_R			( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE + 1 )
#define FIDELITY_FLOAT_RENDERPARM_SUN_G			( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE + 2 )
#define FIDELITY_FLOAT_RENDERPARM_SUN_B			( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE + 3 )
#define FIDELITY_FLOAT_RENDERPARM_WATER_FOAM	( FIDELITY_FLOAT_RENDERPARM_RAINMAP_RANGE + 4 )

#if defined( _MSC_VER ) || __cplusplus >= 201103L
static_assert( FIDELITY_VECTOR_RENDERPARM_SUN_DIRECTION < MAX_VECTOR_RENDER_PARMS, "too many fidelity vectors" );
static_assert( FIDELITY_FLOAT_RENDERPARM_WATER_FOAM < MAX_FLOAT_RENDER_PARMS, "too many fidelity floats" );
#endif

#endif // FIDELITY_RENDERPARM_H
