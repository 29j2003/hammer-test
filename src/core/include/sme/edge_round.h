// edge_round.h - Rounding or bevelling corners between faces, also across several brushes.
//
// The selected faces are grouped into corner lines: faces in one plane form a side, two sides
// that meet along a line form a corner. Each side may be made of several brushes (a wall stacked
// from bands, of different heights on the two sides); the whole corner line gets one curve, cut
// into every brush along it, so the pieces line up seamlessly. Faces that touch no corner are
// left out (and counted). Several corners at once all get the same radius and segments.
//
// An outer corner is rounded by cutting its brushes with the planes of the arc (each brush stays
// one convex brush). Where other brushes cover the corner on both sides (a platform sunk into a
// carved floor), the space the rounding opens next to them is filled with small brushes that
// take the neighbour's texture, so no hole opens; the rounded brush itself is not split. An
// inner corner gets filled up to the arc. Fills are few, well shaped strips (no slivers meeting
// in one point): against one face up to the middle of the arc, against the other from there, and
// one block in the corner.
//
// Nothing is applied that is not sound: every brush of a plan is checked (convex, closed, no
// paper-thin parts or tiny edges, no overlap with other brushes); a radius or a number of
// segments that does not fit is reduced instead of failing, and the plan says so. Corners of
// several brushes that cannot be rounded apart can be merged into one brush first, but only when
// asked for (EdgeRoundOptions::allowMerge). Plans are computed without changing the map, so the
// editor can show them live before applying.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// What rounding corners would do.
struct EdgeRoundPlan {
	/// A brush taken out and the pieces put back in its place (the first piece keeps its id; no
	/// pieces = the brush goes, e.g. merged into another).
	struct Replacement {
		SolidRef ref;
		std::vector<Solid> pieces;
	};
	std::vector<Replacement> replacements;
	std::vector<Solid> added;        ///< New brushes (fills of inner corners and next to neighbours)
	std::vector<int> addedEntities;  ///< Where each of `added` goes (0 = the world)
	double requested = 0;            ///< Radius asked for
	double radius = 0;               ///< Radius used (smaller when the asked one does not fit)
	double largest = 0;              ///< Largest radius that fits
	int segments = 0;                ///< Segments used (fewer when pieces would get too thin)
	int requestedSegments = 0;       ///< Segments asked for
	bool clamped = false;            ///< The radius was reduced
	bool inner = false;              ///< An inner corner was filled
	int corners = 0;                 ///< Corner lines rounded
	int ignoredFaces = 0;            ///< Selected faces that touch no corner (left out)
	int gapFills = 0;                ///< Brushes filling the space next to covering neighbours
	int innerFills = 0;              ///< Brushes filling inner corners
	bool merged = false;             ///< Brushes were merged into one first (allowMerge)
	bool needsMerge = false;         ///< Only possible after merging; `problem` asks for it
	std::string note;                ///< Simpler result taken (fewer segments, no fill ...), if any
	std::string problem;             ///< Why nothing can be done; empty when the plan can be applied

	[[nodiscard]] bool ok() const { return problem.empty(); }
	/// Every brush as it would be afterwards (pieces and added brushes), for a preview.
	[[nodiscard]] std::vector<Solid> resultSolids() const;
	/// The brushes that would be replaced (hidden while the preview shows their pieces).
	[[nodiscard]] std::vector<SolidRef> replacedRefs() const;
};

struct EdgeRoundOptions {
	/// Corners of several brushes that can only be rounded as one brush are merged first.
	bool allowMerge = false;
};

/// Plans rounding (segments > 1) or bevelling (segments = 1) every corner the faces form. Reads
/// `doc` only; new ids come from `ids` (pass the document itself when applying, a scratch
/// document for a preview).
[[nodiscard]] EdgeRoundPlan planEdgeRounds(const MapDocument& doc, MapDocument& ids, const std::vector<FaceRef>& faces, double radius, int segments,
                                           const EdgeRoundOptions& options = {});
/// The corner between faces `a` and `b` (see planEdgeRounds).
[[nodiscard]] EdgeRoundPlan planEdgeRound(const MapDocument& doc, MapDocument& ids, const FaceRef& a, const FaceRef& b, double radius, int segments);

/// A sensible number of segments for a radius (about one per 12 units of arc, 2..16).
[[nodiscard]] int defaultRoundSegments(double radius);

/// Applies a plan inside a running transaction (the plan must come from the current document).
void applyEdgeRound(MapDocument& doc, Transaction& transaction, const EdgeRoundPlan& plan);
/// Applies a plan as one undo step; nullopt if the plan cannot be applied.
[[nodiscard]] std::optional<Change> applyEdgeRound(MapDocument& doc, const EdgeRoundPlan& plan, const std::string& name);

/// What is wrong with a brush for Source, or empty: not convex or not closed, a face without
/// area, an edge shorter than a quarter unit, or thinner than half a unit anywhere.
[[nodiscard]] std::string brushProblem(const Solid& solid);
/// Volume two convex brushes share (0 when they only touch).
[[nodiscard]] double sharedVolume(const Solid& a, const Solid& b);

} // namespace sme
