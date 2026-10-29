//========= mapbase-fidelity ==================================================//
//
// Purpose: Fidelity_Parallax - world surfaces with parallax occlusion mapping
//          (real depth in bricks, stones, tiles), in place of LightmappedGeneric.
//
// deploy.bat writes such materials for the game's bump-mapped wall and floor
// materials (with a height map it makes from the normal map), see
// docs/ENTWICKLUNG.md:
//
//   "Fidelity_Parallax"
//   {
//       "$basetexture"      "brick/brickwall031a"
//       "$bumpmap"          "brick/brickwall031a_normal"
//       "$parallaxmap"      "brick/brickwall031a_normal_height"
//       "$parallaxdepth"    "0.02"
//   }
//
// Lighting as LightmappedGeneric with a bump map: the three bumped lightmaps,
// weighted by the normal (or by a self-shadowed bump map, $ssbump). As there
// also the reflection of the map's cubemaps ($envmap, $envmaptint,
// $envmapcontrast, $envmapsaturation, $fresnelreflection, masked by
// $normalmapalphaenvmapmask or $basealphaenvmapmask) and a detail texture
// ($detail, $detailscale, $detailblendfactor, $detailtint; blend mode 0):
// most of the game's bump-mapped walls have one of them. Walls without a bump
// map (most bricks and stones of a map) work too: deploy.bat makes their
// height map from the base texture, they keep their one plain lightmap.
//
// The flashlight, projected textures and the sun's shadows (env_global_light)
// have a pass of their own (fidelity_parallax_flashlight_vs30/ps30, after
// Mapbase's flashlight pass): the same parallax shift, the relief's normal
// facing the light and its own soft shadows - with Mapbase's pass the light
// lay on a flat copy of the wall (Julius: "bei Licht bricht die Illusion").
// Bullet holes from the decal atlas ($parallaxmodulate, in place of
// DecalModulate) are multiplied onto the wall as there, with the hole's depth.
// Bullet holes (decals with $translucent) work too: blended as
// LightmappedGeneric blends them, the light pass adds light only where the
// decal is (its alpha). Without shader model 3.0, or with neither bump map
// nor height map, the material is a normal LightmappedGeneric.
//
// Blended surfaces (displacements painted from $basetexture to
// $basetexture2, WorldVertexTransition): deploy.bat gives them the name
// Fidelity_WorldVertexTransition - the same shader, compiled twice from
// fidelity_parallax_body.h. Both layers have depth ($parallaxmap2 in
// $bumpmap2's or $basetexture2's coordinates), blended by the painted vertex
// alpha ($blendmodulatetexture sharpens it, as WorldVertexTransition) and by
// their heights ($parallaxblend). Also as LightmappedGeneric: $selfillum
// (with $selfillumtint) and the additive detail texture ($detailblendmode 1).
//
// Why not in Mapbase's own LightmappedGeneric: its pixel shader's combo
// space is about 900 million (shader model 3.0; SDK_lightmappedgeneric_ps30.inc)
// - even after its skips far too many to recompile with every change; so
// Mapbase's precompiled shaders stay as they are.
//
//=============================================================================//

#include "BaseVSShader.h"
#include "cpp_shader_constant_register_map.h"
#include "fidelity/fidelity_renderparm.h"

#include "fidelity_parallax_vs30.inc"
#include "fidelity_parallax_ps30.inc"
#include "fidelity_parallax_flashlight_vs30.inc"
#include "fidelity_parallax_flashlight_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Reference plane of the relief (fidelity_parallax_common.h): which height
// lies on the polygon, 1 = the top (the relief cut in, as before), 0 = the
// bottom (it stands out). Lower: in inside corners less of a floor's relief
// runs on under the wall (Julius), the tops slide a little against the
// lightmap. Floors and ceilings meet walls in such corners all along their
// edges, so they get less. Decals stay at 1 (they must sit where they were shot).
static ConVar r_fidelity_parallax_plane( "r_fidelity_parallax_plane", "0.5", FCVAR_SERVER_CAN_EXECUTE, "Parallax reference plane on walls: 1 = relief cut into the wall, 0 = standing out of it." );
static ConVar r_fidelity_parallax_plane_floor( "r_fidelity_parallax_plane_floor", "0.2", FCVAR_SERVER_CAN_EXECUTE, "Parallax reference plane on floors and ceilings: 1 = relief cut in, 0 = standing out." );
// Relief shadows from the baked light: the three bumped lightmaps hold the
// light from three directions, so where they differ the main light comes
// from one side - the relief throws short shadows away from it (as the
// flashlight pass does for its light). World space and baked: they lie the
// same from wherever they are seen. 0.35: at 0.6 every tile of a bump-mapped
// floor had a hard black strip along one side (Julius, 29.10.2026).
static ConVar r_fidelity_parallax_bakedshadow( "r_fidelity_parallax_bakedshadow", "0.35", FCVAR_SERVER_CAN_EXECUTE, "Relief shadows from the baked light on bump-mapped parallax walls, 0..1 (0 = none)." );
static ConVar r_fidelity_parallax_maxshift( "r_fidelity_parallax_maxshift", "4", FCVAR_SERVER_CAN_EXECUTE, "Most the parallax moves the texture at flat angles, in world units." );

// The shader itself, twice: once for walls and floors, once for blended
// surfaces (displacements painted from $basetexture to $basetexture2).
// deploy.bat names the blended ones Fidelity_WorldVertexTransition: VBSP
// gives a blend material on a brush (not a displacement) a plain copy without
// the second texture only if its shader's name says WorldVertexTransition -
// else such a brush face showed the second texture (no painted blend there).
#define FIDELITY_PARALLAX_SHADER Fidelity_Parallax
#include "fidelity_parallax_body.h"
#undef FIDELITY_PARALLAX_SHADER

#define FIDELITY_PARALLAX_SHADER Fidelity_WorldVertexTransition
#include "fidelity_parallax_body.h"
#undef FIDELITY_PARALLAX_SHADER

