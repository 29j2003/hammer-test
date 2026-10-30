// edge_round.h - Rounding or bevelling the corner between two faces, also across brushes.
//
// The two faces may belong to one brush (an edge) or to two brushes that meet at a corner (an
// L of two walls). An outer corner is rounded by cutting the brushes with the planes of the arc
// (each brush stays convex, the arc runs on across both); an inner corner gets filled with a
// fan of small brushes up to the arc (a fillet). Where other brushes cover the corner on both
// sides (a platform sunk into a carved floor), the brushes are split at that surface and stay
// square below it, so no hole opens next to the neighbours; the cut face takes the
// neighbour's texture, so the floor runs on into the rounded corner. A radius that does not
// fit is reduced to the largest that does instead of failing. The plan is computed without
// changing the map, so the editor can show it live before applying it.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// What rounding the corner between two faces would do.
struct EdgeRoundPlan {
	/// A brush taken out and the pieces put back in its place (the first piece keeps its id).
	struct Replacement {
		SolidRef ref;
		std::vector<Solid> pieces;
	};
	std::vector<Replacement> replacements;
	std::vector<Solid> added;        ///< New brushes (the fillet of an inner corner)
	int addedEntity = 0;             ///< Where `added` goes (0 = the world)
	double requested = 0;            ///< Radius asked for
	double radius = 0;               ///< Radius used (smaller when the asked one does not fit)
	double largest = 0;              ///< Largest radius that fits
	int segments = 0;                ///< Segments used (fewer when they would get shorter than 2 units)
	bool clamped = false;            ///< The radius was reduced
	bool inner = false;              ///< Inner corner: filled instead of cut
	int keptSquare = 0;              ///< Brush parts left square where neighbours cover the corner
	std::string problem;             ///< Why nothing can be done; empty when the plan can be applied

	[[nodiscard]] bool ok() const { return problem.empty(); }
	/// Every brush as it would be afterwards (pieces and added brushes), for a preview.
	[[nodiscard]] std::vector<Solid> resultSolids() const;
	/// The brushes that would be replaced (hidden while the preview shows their pieces).
	[[nodiscard]] std::vector<SolidRef> replacedRefs() const;
};

/// Plans rounding (segments > 1) or bevelling (segments = 1) the corner between faces `a` and
/// `b`. Reads `doc` only; new ids come from `ids` (pass the document itself when applying, a
/// scratch document for a preview).
[[nodiscard]] EdgeRoundPlan planEdgeRound(const MapDocument& doc, MapDocument& ids, const FaceRef& a, const FaceRef& b, double radius, int segments);

/// Applies a plan inside a running transaction (the plan must come from the current document).
void applyEdgeRound(MapDocument& doc, Transaction& transaction, const EdgeRoundPlan& plan);
/// Applies a plan as one undo step; nullopt if the plan cannot be applied.
[[nodiscard]] std::optional<Change> applyEdgeRound(MapDocument& doc, const EdgeRoundPlan& plan, const std::string& name);

} // namespace sme
