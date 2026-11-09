// optimization_checks.h - Checks about engine limits and compile time (part of checkMap, see checks.h):
//
// - a map close to an engine limit (85 % or more of MAX_MAP_BRUSHES, brush sides, planes, entities,
//   overlays, ...: see map_budget) or over one. Hammer only says so when VBSP stops with
//   "MAX_MAP_..." after a long compile; here it shows while building, with what to do.
// - world brushes that are only details: round shapes (many sides, e.g. cylinders and arches) and
//   small pieces (trim, steps, frames). As world brushes they cut the map into many small vis
//   leaves: VVIS gets slow, and the map gets more brush sides and planes. As func_detail they are
//   left out of vis. The fix ties them to one func_detail. Info only: a detail brush does not seal
//   the map, so a candidate that closes the map off from the void must stay world.
// - faces nobody sees that VBSP keeps: a face of a func_detail brush lying flat against a world
//   or func_detail brush, and a world face lying flat against a func_detail brush (VBSP drops
//   world faces against world brushes itself, but detail brushes are not part of the BSP that
//   decides which faces exist). They get lightmaps and count as faces; the fix makes them nodraw.
//   Only faces fully covered by one opaque face of a static brush: never brush entities that
//   can move or vanish (doors, func_brush), glass, water, fences or tool brushes.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <sme/checks.h>

namespace sme {

/// The checks listed in the file comment, for checkMap.
[[nodiscard]] std::vector<Problem> checkOptimization(const MapDocument& doc, const CheckContext& context);
/// The fixes of those checks (empty change for other kinds), for applyFix.
[[nodiscard]] Change applyOptimizationFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities);

/// World brushes that are only details (see the file comment), in map order.
[[nodiscard]] std::vector<SolidRef> detailCandidates(const MapDocument& doc);

/// Faces nobody sees that VBSP keeps (see the file comment), in map order. `seeThrough` says
/// whether a material lets you see what is behind it (translucent, alpha tested, water); without
/// it the material's name decides (glass, water, window, fence, grate, ...).
[[nodiscard]] std::vector<FaceRef> hiddenFaces(const MapDocument& doc, const std::function<bool(const std::string&)>& seeThrough = {});
/// Whether a material's name says it is see-through (glass, water, window, fence, grate, ...).
[[nodiscard]] bool seeThroughByName(std::string_view material);

} // namespace sme
