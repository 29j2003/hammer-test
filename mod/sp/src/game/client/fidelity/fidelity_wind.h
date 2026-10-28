//========= mapbase-fidelity ==================================================//
//
// Purpose: The current wind, for other effects (drifting clouds).
//
//=============================================================================//

#ifndef FIDELITY_WIND_H
#define FIDELITY_WIND_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Wind of this frame in the scale of the tree sway (env_wind speed / 25, so
// about 1 for a breeze): the map's env_wind, else the breeze of
// fidelity_wind.cpp, zero when both are off.
//-----------------------------------------------------------------------------
const Vector &Fidelity_WindVector();

#endif // FIDELITY_WIND_H
