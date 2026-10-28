// map_helpers.h - Ropes and ladders in one step (things mappers set up by hand in Hammer).
//
// Ropes: Source draws a rope from a move_rope through keyframe_ropes linked by "NextKey"
// (target names); "Slack" is the extra length that makes it sag. makeRope() builds that chain
// through the given points; ropeCurves() gives the sagging curves for the views (Hammer only
// draws straight lines).
//
// Ladders (Half-Life 2): a climbable ladder is a func_useableladder - a point entity with the
// bottom ("point0") and top ("point1") of the climbing line, in world coordinates, just in front
// of the ladder - and info_ladder_dismount entities where the player gets off. makeLadder() places
// them for a selected ladder brush or model box: the front is the open side of the box.

#pragma once

#include <string>
#include <vector>

#include <sme/editing.h>

namespace sme {

/// One rope for the views: the points of its sagging curve.
struct RopeCurve {
	std::vector<Vec3> points;
	int startEntityId = 0;   ///< The move_rope it starts at
};

/// Sagging curves of every rope chain (move_rope → keyframe_rope ... via NextKey).
[[nodiscard]] std::vector<RopeCurve> ropeCurves(const MapDocument& doc);

/// How far a rope of `slack` extra length hangs down between two points (parabola approximation:
/// length ≈ span + 8·sag²/(3·span)).
[[nodiscard]] double ropeSag(const Vec3& a, const Vec3& b, double slack);

namespace edit {

/// What makeRope() made.
struct RopeResult {
	Change change;
	std::vector<int> entityIds;   ///< move_rope first, then the keyframe_ropes
};

/// A rope through `points` (at least 2): a move_rope at the first, keyframe_ropes at the others,
/// named "<name>_1", "<name>_2" ... and linked by NextKey; FGD defaults, then `slack`. One undo step.
[[nodiscard]] RopeResult makeRope(MapDocument& doc, const std::vector<Vec3>& points, const EntityCatalog* catalog, double slack = 25);

/// The points makeRope() uses for a selection: a point entity's origin, a brush's or brush
/// entity's top centre - in selection order (brushes after entities).
[[nodiscard]] std::vector<Vec3> ropePointsOf(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog);

/// What makeLadder() made.
struct LadderResult {
	Change change;
	int ladderId = 0;              ///< The func_useableladder (0: nothing made)
	std::vector<int> dismountIds;  ///< info_ladder_dismount at the top and the bottom
	Vec3 front;                    ///< The side the player climbs from
};

/// A climbable ladder for the box of a ladder brush or model (see the file comment): the side of
/// the box's thin horizontal axis that is free of brushes is the front. One undo step.
[[nodiscard]] LadderResult makeLadder(MapDocument& doc, const Bounds& ladder, const EntityCatalog* catalog);

} // namespace edit

} // namespace sme
