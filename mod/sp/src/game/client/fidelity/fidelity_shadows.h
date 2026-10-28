//========= mapbase-fidelity ==================================================//
//
// Purpose: Steadier, softer sun shadows from Mapbase's env_global_light (with
//          a second, larger cascade), and the same shadow filter size for
//          env_projectedtexture.
//
//=============================================================================//

#ifndef FIDELITY_SHADOWS_H
#define FIDELITY_SHADOWS_H
#ifdef _WIN32
#pragma once
#endif

struct FlashlightState_t;

//-----------------------------------------------------------------------------
// Adjusts the shadow state of env_global_light before it is handed to the
// shadow manager. vRight and vUp are the light's image plane axes. Called from
// C_GlobalLight::ClientThink (line marked "mapbase-fidelity").
//-----------------------------------------------------------------------------
void Fidelity_AdjustGlobalLight( FlashlightState_t &state, const Vector &vRight, const Vector &vUp );

//-----------------------------------------------------------------------------
// Same filter fix for env_projectedtexture: its "Shadow Filter" keyvalue is
// then measured in shadow map texels, like the player's flashlight. Called
// from C_EnvProjectedTexture::UpdateLight (line marked "mapbase-fidelity").
//-----------------------------------------------------------------------------
void Fidelity_AdjustProjectedTexture( FlashlightState_t &state );

#endif // FIDELITY_SHADOWS_H
