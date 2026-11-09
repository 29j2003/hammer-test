// grid_snap.h - Moving brush corners onto the grid ("Snap vertices to grid") and finding brushes
// whose corners are off it.
//
// Arches, rounded edges and roads leave corners at fractions of a unit. VBSP rounds plane
// points while compiling; neighbours that met exactly in the editor can then open hairline
// cracks, and every slightly different plane counts against MAX_MAP_PLANES. Hammer users fix
// this by dragging each vertex onto the grid. Here every corner of a brush moves to the nearest
// grid point and each face gets the plane through its moved corners - but only when the brush
// keeps all its faces and corners and passes brushProblem(); otherwise it stays as it is.
// Neighbouring brushes that shared a corner share the snapped corner too.

#pragma once

#include <optional>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// Corners of the brush that are more than `tolerance` units off the `grid`.
[[nodiscard]] int offGridCorners(const Solid& solid, double grid = 1, double tolerance = 0.01);

/// The brush with its corners on the grid. nullopt if it is on the grid already, is a
/// displacement (its grid would move), or would lose a face or corner or get broken.
[[nodiscard]] std::optional<Solid> snapSolidToGrid(const Solid& solid, double grid = 1);

namespace edit {

/// Snaps the selected brushes (and the brushes of selected brush entities) to the grid. One
/// undo step; `snapped` / `refused` count the brushes moved and those left as they were because
/// snapping would break them.
[[nodiscard]] Change snapToGrid(MapDocument& doc, const Selection& selection, double grid, int* snapped = nullptr, int* refused = nullptr);

} // namespace edit

} // namespace sme
