// shape_fit.h - Shapes of the block tool fitted into the map around them ("Fit to surroundings").
//
// A shape is drawn into a box; on its own it ignores what is already there. Fitting it does three
// things, like the corner rounding does for edges (edge_round):
// - fill: an arch drawn against walls (a rounded room corner, a curved wall along a wall) gets the
//   space between its outer side and those walls filled, so no void stays behind it; a shape
//   standing in a hole of a floor (its box reaches below the floor's top, the floor lies against
//   its sides) gets the floor's top filled around it, with the floor's texture;
// - carve: brushes the shape reaches into (a floor it is sunk into, a wall it is drawn through)
//   are cut around it, so nothing overlaps;
// - never refuse: settings the box cannot take (a wall wider than the arch, more pieces or steps
//   than fit) are reduced to what fits, and the plan says what was limited.
// The plan is computed without changing the map, for a live preview; applying it is one undo step.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/history.h>
#include <sme/map.h>
#include <sme/shapes.h>

namespace sme {

/// Settings reduced to what the box can take.
struct ShapeLimits {
	ShapeOptions used;                 ///< The settings the shape is built with
	std::vector<std::string> notes;    ///< What was limited ("wall width limited to 32 units")
	[[nodiscard]] bool limited() const { return !notes.empty(); }
};

/// Reduces settings the box cannot take: arch wall width (at most half the box), arch pieces and
/// round sides (no side shorter than 2 units), stairs steps (each at least 1 unit deep and high).
[[nodiscard]] ShapeLimits limitShapeOptions(const Bounds& box, const ShapeOptions& options);

/// What creating a shape would do.
struct ShapePlan {
	/// A brush taken out and the pieces left of it (carved; the first piece keeps its id).
	struct Replacement {
		SolidRef ref;
		std::vector<Solid> pieces;
	};
	std::vector<Solid> shape;                  ///< The shape's brushes
	std::vector<Solid> fill;                   ///< Brushes filling gaps around it
	std::vector<Replacement> carved;           ///< Neighbours cut around the shape
	ShapeLimits limits;
	bool fitted = false;                       ///< Fit to surroundings was applied

	[[nodiscard]] bool ok() const { return !shape.empty(); }
	/// The brushes as they would be afterwards (shape, fill, carved neighbours), for a preview.
	[[nodiscard]] std::vector<Solid> resultSolids() const;
	/// Neighbours that would change (hidden while the preview stands in for them).
	[[nodiscard]] std::vector<SolidRef> replacedRefs() const;
};

/// Plans the shape in `box` with `options` (reduced by limitShapeOptions). With `options.fit`
/// (and any kind but a plain block) it is fitted into the brushes around it. Reads `doc` only; new
/// ids come from `ids` (the document itself when applying, a scratch document for a preview).
[[nodiscard]] ShapePlan planShape(const MapDocument& doc, MapDocument& ids, const Bounds& box, const ShapeOptions& options, const std::string& material);

/// Applies a plan (made from the current document) as one undo step; returns the change and
/// the shape's and fill's brushes (to select). Nothing changes if the plan has no shape.
[[nodiscard]] std::pair<Change, std::vector<SolidRef>> applyShapePlan(MapDocument& doc, const ShapePlan& plan, const std::string& name);

} // namespace sme
