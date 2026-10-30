// snapping.h - Snapping onto other objects while moving, resizing and drawing (Source 2 Hammer's
// corner squares): the corners of everything else in the map, the corner nearest to the mouse,
// and where a side being dragged should stop so it lines up with a corner or side of another
// brush - also off the grid (a brush at x = 4.5 is met exactly, without changing the grid).
//
// Distances on screen are the caller's business (it knows the camera): nearestCorner() takes a
// function, snapFacePlane() a tolerance in world units (pixels times units per pixel).

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <sme/editing.h>
#include <sme/math.h>

namespace sme {

/// Corners of every brush (also of brush entities) and of the box of every point entity that is
/// neither in `skip` (what is being dragged) nor in `hidden`: what a drag can snap onto.
[[nodiscard]] std::vector<Vec3> snapCorners(const MapDocument& doc, const Selection& skip, const Selection& hidden, const EntityCatalog* catalog);

/// The corner with the smallest `distance` (e.g. pixels from the mouse on screen; nullopt: not
/// visible), if it is below `radius`.
[[nodiscard]] std::optional<Vec3> nearestCorner(const std::vector<Vec3>& corners, const std::function<std::optional<double>(const Vec3&)>& distance,
                                                double radius);

/// Where a side being dragged snaps to.
struct PlaneSnap {
	double value = 0;   ///< New position of the side along its axis (a corner's coordinate)
	Vec3 corner;        ///< The corner it lines up with (to highlight it)
};

/// A side of a box being dragged along `axis`: it would be at `raw`; across the other two axes it
/// covers `face` (only those two axes of the box count). Snaps onto the corner whose coordinate on
/// `axis` lies within `tolerance` of `raw` - nearest along the axis first, then the one closest to
/// the side - among the corners at most `reach` beside the side (beyond that, a corner of a brush
/// far away would pull the side for no visible reason). Nothing within reach: nullopt.
[[nodiscard]] std::optional<PlaneSnap> snapFacePlane(const std::vector<Vec3>& corners, int axis, double raw, const Bounds& face, double tolerance,
                                                     double reach);

/// `start` with its side on `axis` (`isMax`: the upper one) moved to `value`, but never thinner
/// than `minimum` (the side stops there).
[[nodiscard]] Bounds resizedBounds(const Bounds& start, int axis, bool isMax, double value, double minimum);

} // namespace sme
