//========= mapbase-fidelity ==================================================//
//
// Purpose: Weapons in the hand pull back a little near walls and have some
//          weight (swing, lean, dip).
//
//=============================================================================//

#ifndef FIDELITY_VIEWMODEL_H
#define FIDELITY_VIEWMODEL_H
#ifdef _WIN32
#pragma once
#endif

class C_BasePlayer;

//-----------------------------------------------------------------------------
// Called by CBaseViewModel::CalcViewModelView (baseviewmodel_shared.cpp, line
// marked "mapbase-fidelity") right before the view model gets its place:
// lets it swing behind turns, lean and dip, and moves it back and tilts it
// down a little while a wall is right in front.
//-----------------------------------------------------------------------------
void Fidelity_ViewModelPlace( C_BasePlayer *pOwner, const Vector &vecEye, const QAngle &angEye, Vector &vecOrigin, QAngle &angAngles );

#endif // FIDELITY_VIEWMODEL_H
