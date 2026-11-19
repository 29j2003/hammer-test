//========= mapbase-fidelity ==================================================//
//
// Purpose: Glow only while a model plays certain animations (Mapsmith's
//          "Glow ... > only during an animation"), with real light.
//
//=============================================================================//

#ifndef FIDELITY_ANIMGLOW_H
#define FIDELITY_ANIMGLOW_H
#ifdef _WIN32
#pragma once
#endif

class IMaterial;
class C_BaseAnimating;

//-----------------------------------------------------------------------------
// The glow of one material, read once from the keys Mapsmith writes:
//   "$mapsmith_glow"          "1"
//   "$mapsmith_glowcolor"     "[r g b]"      colour x strength
//   "$mapsmith_glowsettings"  "...;when=animation;anims=act_vm_reload,fire;
//                              from=0;to=1;fade=0.1;pulse=0;period=1;low=0.25"
// Returns its index, or -1 when the material has no glow tied to animations.
// Looked up once per material (a short list, by material pointer).
//-----------------------------------------------------------------------------
int Fidelity_AnimGlowOf( IMaterial *pMaterial );

// Its colour x strength ($mapsmith_glowcolor)
const Vector &Fidelity_AnimGlowColor( int nGlow );

//-----------------------------------------------------------------------------
// How much it glows right now on this model (0..1): one of its animations
// (activity or sequence name) plays, inside its cycle window (with fades at
// the edges), times the pulse. The name check is cached per model and
// sequence, so a frame does no string work.
//-----------------------------------------------------------------------------
float Fidelity_AnimGlowAmount( int nGlow, C_BaseAnimating *pAnimating );

#endif // FIDELITY_ANIMGLOW_H
