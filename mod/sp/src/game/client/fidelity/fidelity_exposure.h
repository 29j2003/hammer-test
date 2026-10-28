//========= mapbase-fidelity ==================================================//
//
// Purpose: Calmer automatic exposure (HDR eye adaptation).
//
//=============================================================================//

#ifndef FIDELITY_EXPOSURE_H
#define FIDELITY_EXPOSURE_H
#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Called from viewpostprocess.cpp (lines marked "mapbase-fidelity"):
// SetToneMapScale passes each new exposure goal through
// Fidelity_StabilizeExposure, ResetToneMapping calls Fidelity_ResetExposure.
//-----------------------------------------------------------------------------
float Fidelity_StabilizeExposure( float flGoal, float flMin, float flMax );
void Fidelity_ResetExposure();

#endif // FIDELITY_EXPOSURE_H
