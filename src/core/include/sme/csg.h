// csg.h - Building and cutting convex brushes: convex hulls (shapes, merging), splitting by
// a plane (clipping), subtracting one brush from another (carve), hollowing, rounding or
// bevelling an edge. Every result is a valid convex Source brush (isConvexValid) or nothing.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

/// A side for `plane` (outward normal) with fresh id; textures copied from `textureFrom` if given,
/// else world aligned with `material`.
[[nodiscard]] Side makeSide(MapDocument& doc, const Plane& plane, const std::string& material, const Side* textureFrom = nullptr);

/// Removes sides that do not make a face (planes that no longer touch the brush).
void removeUnusedSides(Solid& solid);

/// Rewrites the plane points from the face corners (nicer numbers, same planes).
void tidyPlanePoints(Solid& solid);

/// Outward planes of the convex hull of the points (empty if the points are flat or too few).
/// Each plane comes with three hull points in VMF order: (p0 - p1) x (p2 - p1) = outward normal.
struct HullPlane {
	Plane plane;
	std::array<Vec3, 3> points;
};
[[nodiscard]] std::vector<HullPlane> convexHull(const std::vector<Vec3>& points);

/// Brush around the points (world-aligned textures). nullopt if the points do not span a volume.
[[nodiscard]] std::optional<Solid> solidFromPoints(MapDocument& doc, const std::vector<Vec3>& points, const std::string& material);

/// Splits a brush by a plane: {part in front of the plane, part behind}. A part is missing if
/// the plane does not cut through. New faces copy their texture from `capTexture`.
[[nodiscard]] std::pair<std::optional<Solid>, std::optional<Solid>> splitSolid(MapDocument& doc, const Solid& solid, const Plane& plane,
                                                                               const Side& capTexture);

/// Carve: the parts of `solid` outside `cutter` (empty if the cutter swallows it; `solid`
/// unchanged if they do not touch).
[[nodiscard]] std::vector<Solid> subtractSolid(MapDocument& doc, const Solid& solid, const Solid& cutter);

/// Hollow: walls of `thickness` units inside the brush (negative: outside). Empty if too thin.
[[nodiscard]] std::vector<Solid> hollowSolid(MapDocument& doc, const Solid& solid, double thickness);

/// Merge: one brush around all of them. `exact` tells whether the brushes filled their hull
/// (true) or the result also fills gaps between them (false).
struct MergeResult {
	Solid solid;
	bool exact = false;
};
[[nodiscard]] std::optional<MergeResult> mergeSolids(MapDocument& doc, const std::vector<Solid>& solids);

/// Rounds (segments > 1) or bevels (segments = 1) the edge between sides `sideA` and `sideB`
/// (side ids) with the given radius. nullopt if the sides do not share an edge.
[[nodiscard]] std::optional<Solid> roundEdge(MapDocument& doc, const Solid& solid, int sideA, int sideB, double radius, int segments);

/// Vertex tool: moves the brush corners at `corners` by `delta`. The brush stays convex (the
/// result is the hull of the new corners); sides keep id and texture where they stay recognisable.
/// `cornersLost` tells whether corners vanished because the shape folded in.
struct VertexEdit {
	Solid solid;
	bool cornersLost = false;
};
[[nodiscard]] std::optional<VertexEdit> moveVertices(MapDocument& doc, const Solid& solid, const std::vector<Vec3>& corners, const Vec3& delta);

/// Unique corners of a brush.
[[nodiscard]] std::vector<Vec3> solidCorners(const Solid& solid);

/// Volume of a convex brush (cubic units).
[[nodiscard]] double solidVolume(const Solid& solid);

} // namespace sme
