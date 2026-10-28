//========= mapbase-fidelity ==================================================//
//
// Purpose: Light shafts ("god rays") from the map's env_sun or env_global_light.
//
//=============================================================================//

#ifndef FIDELITY_GODRAYS_H
#define FIDELITY_GODRAYS_H
#ifdef _WIN32
#pragma once
#endif

class CViewSetup;
class ITexture;

//-----------------------------------------------------------------------------
// Adds light shafts around the sun to the frame buffer. Call it for the main
// view after the opaque scene is drawn and before translucent objects; pDepth
// holds the linear depth in alpha (Fidelity_UpdateDepthTexture). Does nothing
// when the map has no active env_sun / env_global_light or the sun is far off
// screen.
//-----------------------------------------------------------------------------
void Fidelity_RenderGodRays( const CViewSetup &view, ITexture *pDepth );

// True when shafts are drawn in this view (on, a sun, on screen): otherwise
// the caller can skip the depth copy.
bool Fidelity_GodRaysWillDraw( const CViewSetup &view );

//-----------------------------------------------------------------------------
// How much of a light in direction vecToLight (up to flDistance away) comes
// through glass and plastic in front of the eye: (1, 1, 1) when nothing
// see-through is in the way, else tinted in its colour and weaker. Used for
// the light shafts and, from CGlowOverlay::Draw (glow_overlay.cpp, line
// marked "mapbase-fidelity"), for the engine's sun glow. A light at no
// distance (flDistance MAX_TRACE_LENGTH: the sun) also dims behind the
// drifting clouds (Fidelity_SunGlowThroughClouds), or, while the sky draws
// the sun behind its clouds (Fidelity_SkyDrawsSun), gives nothing: the
// engine's sun sprites are hidden then.
//-----------------------------------------------------------------------------
class Vector;
Vector Fidelity_GlassTransmission( const Vector &vecToLight, float flDistance );

//-----------------------------------------------------------------------------
// The map's env_sun as its sprites draw it: direction, colour of the core and
// of the glow around it (HDR scale included, gamma) and their sizes (angular
// radius, radians). False without an env_sun that is on and has a direction.
// The sky draws this sun behind its clouds (fidelity_sky.cpp).
//-----------------------------------------------------------------------------
bool Fidelity_SunSprite( Vector &vecDirection, Vector &vecCore, float &flCoreSize, Vector &vecGlow, float &flGlowSize );

// Direction to the sun of the map (env_sun, else env_global_light); false if it has none
bool Fidelity_SunDirection( Vector &vecDirection );

// Direction towards env_sun only (on, with a direction - not its default
// straight up); false without one. The sun's light follows it (fidelity_shadows.cpp).
bool Fidelity_EnvSunDirection( Vector &vecDirection );

// For fidelity_report: env_sun (direction, sizes, colours), env_global_light's
// direction and how far apart they are, the shafts' settings
class CUtlBuffer;
void Fidelity_SunReport( CUtlBuffer &info );

#endif // FIDELITY_GODRAYS_H
