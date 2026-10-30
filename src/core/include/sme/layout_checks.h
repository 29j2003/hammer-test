// layout_checks.h - Checks about where things are in the map (part of checkMap, see checks.h):
//
// - an info_player_start stuck in a brush (the player spawns stuck, or falls through the floor),
// - point entities outside every world brush (in the void: VBSP reports a leak from them),
// - faces with an extreme lightmap scale (below 4: very costly lightmaps; above 128: blurry
//   light, and 0 or less is invalid),
// - props (static, dynamic, physics) whose origin is inside a brush: models are lit at their origin,
//   so they turn black in the game (a common Hammer surprise),
// - NPCs in a map without info_node: they cannot find paths and stand still or walk straight
//   into walls. The fix places a few nodes around every NPC, on free floor.
//
// The geometry tests are simple: brushes are the world's and func_detail's (tool brushes that do
// not block, like hint and skip, are left out), the player is a 32 x 32 x 72 box above its origin.

#pragma once

#include <vector>

#include <sme/checks.h>

namespace sme {

/// The checks listed in the file comment, for checkMap.
[[nodiscard]] std::vector<Problem> checkLayout(const MapDocument& doc, const CheckContext& context);
/// The fixes of those checks (empty change for other kinds), for applyFix.
[[nodiscard]] Change applyLayoutFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities);

/// Whether a box overlaps a brush that blocks the player (world brushes and func_detail).
[[nodiscard]] bool boxInSolid(const MapDocument& doc, const Bounds& box);
/// Where the info_node fix would put nodes around an NPC standing at `feet` (free floor only).
[[nodiscard]] std::vector<Vec3> nodeSpotsAround(const MapDocument& doc, const Vec3& feet);

} // namespace sme
