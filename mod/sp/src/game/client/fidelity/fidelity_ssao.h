//========= mapbase-fidelity ==================================================//
//
// Purpose: Screen-space ambient occlusion for the main view.
//
//=============================================================================//

#ifndef FIDELITY_SSAO_H
#define FIDELITY_SSAO_H
#ifdef _WIN32
#pragma once
#endif

class CViewSetup;

//-----------------------------------------------------------------------------
// Applies ambient occlusion to the frame buffer, then the light shafts
// (fidelity_godrays.h). Call it for the main view right after the opaque world
// and models are drawn and before translucent objects (viewrender.cpp, next to
// UpdateFullScreenDepthTexture()).
//-----------------------------------------------------------------------------
void Fidelity_RenderSSAO( const CViewSetup &view );

#endif // FIDELITY_SSAO_H
