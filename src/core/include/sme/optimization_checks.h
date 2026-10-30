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

#pragma once

#include <vector>

#include <sme/checks.h>

namespace sme {

/// The checks listed in the file comment, for checkMap.
[[nodiscard]] std::vector<Problem> checkOptimization(const MapDocument& doc, const CheckContext& context);
/// The fixes of those checks (empty change for other kinds), for applyFix.
[[nodiscard]] Change applyOptimizationFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities);

/// World brushes that are only details (see the file comment), in map order.
[[nodiscard]] std::vector<SolidRef> detailCandidates(const MapDocument& doc);

} // namespace sme
