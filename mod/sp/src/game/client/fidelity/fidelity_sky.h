//========= mapbase-fidelity ==================================================//
//
// Purpose: Drifting clouds in the skybox (fidelity_sky.cpp).
//
//=============================================================================//

#ifndef FIDELITY_SKY_H
#define FIDELITY_SKY_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// How much the drifting clouds cover the sun right now: 0 clear .. 1 thick
// (smoothed, 0 without clouds or sun). The sun's light, shadows, glow and
// light shafts dim with it.
//-----------------------------------------------------------------------------
float Fidelity_SunCloudCover();

// Share of the sun's light that gets through the clouds, for its light and
// shadows (they dim a little, never go out) and for its glow and light shafts
// (which dim more). The light only by 15 %: by 55 % every sunlit wall and floor
// got darker and lighter again as clouds passed - it looked like light coming
// and going without a reason (Julius 26.10.2026: "Licht kommt rein von der
// Seite", the wood "darker coming closer"; fidelity_report showed the sun's
// colour between 0.35 and 0.64)
inline float Fidelity_SunLightThroughClouds() { return 1.0f - 0.15f * Fidelity_SunCloudCover(); }
inline float Fidelity_SunGlowThroughClouds() { return 1.0f - 0.8f * Fidelity_SunCloudCover(); }

// True while the sky draws the env_sun itself, behind its clouds: the
// engine's sun sprites are hidden then (they lay on top of the clouds)
bool Fidelity_SkyDrawsSun();

#endif // FIDELITY_SKY_H
