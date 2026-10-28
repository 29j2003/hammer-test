//========= mapbase-fidelity ==================================================//
//
// Purpose: Light in the air of the flashlight and of projected textures.
//
//=============================================================================//

#ifndef FIDELITY_VOLUMETRIC_H
#define FIDELITY_VOLUMETRIC_H
#ifdef _WIN32
#pragma once
#endif

#include "iclientshadowmgr.h"

struct FlashlightState_t;
class VMatrix;
class ITexture;
class CViewSetup;

//-----------------------------------------------------------------------------
// Called by CClientShadowMgr::ComputeShadowDepthTextures (clientshadowmgr.cpp,
// line marked "mapbase-fidelity") for every light whose shadow map was just
// rendered: its state, world-to-texture matrix and shadow map, for this frame.
//-----------------------------------------------------------------------------
void Fidelity_VolumetricShadow( ClientShadowHandle_t hShadow, const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pShadowDepth );

//-----------------------------------------------------------------------------
// In the main view after the opaque scene (from Fidelity_RenderSSAO, with the
// depth copy): adds the light in the air of this frame's nearest lights.
//-----------------------------------------------------------------------------
void Fidelity_RenderVolumetric( const CViewSetup &view, ITexture *pDepth );

// Will Fidelity_RenderVolumetric draw this frame (then the depth copy is needed)?
bool Fidelity_VolumetricWillDraw();

// fidelity_report: the lights with a shadow map of the last frame (flashlight,
// env_projectedtexture, the sun's), and which of them showed light in the air
class CUtlBuffer;
void Fidelity_VolumetricReport( const Vector &vecEye, CUtlBuffer &info );

#endif // FIDELITY_VOLUMETRIC_H
