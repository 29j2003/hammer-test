// prism_tidy.h - Clean pieces after cuts: brushes that stand side by side as upright prisms (one
// top, one bottom, upright sides - floor slabs, parking lanes, kerbs) are re-cut the way a mapper
// would cut them.
//
// Cutting a floor brush by many cutters (the road's curve pieces) leaves fans of thin wedges: every
// cutter's planes run on through the floor. Here the pieces of one kind (same top and bottom plane,
// same top material and texture) are joined into their outline first, then that outline is cut
// again only where it must be (at its inside corners): along a hint line through the corner when
// there is one (the road's cross lines: a curve's lane becomes one trapezoid per curve piece), else
// in the direction that keeps the best angles; cuts never make an edge shorter than `minEdge` or a
// corner sharper than needed. Neighbouring parts that make one convex brush are joined again.
// Edges of the outline shorter than a quarter unit (two cuts a hair apart; no brush may have them)
// are dropped, corners bulging less than three hundredths of a unit out of a line too.
// Faces on the outline keep the look of the face they lie on; faces between two new pieces are
// nodraw (nothing sees them). Brushes that are not such prisms, and outlines with a hole, stay as
// they were.

#pragma once

#include <string>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

/// A line seen from above (z is ignored) cuts may follow: a point on it and its direction.
struct CutHint {
	Vec3 point;
	Vec3 direction;
};

struct PrismTidyOptions {
	std::vector<CutHint> hints;
	double minEdge = 2;      ///< No new edge shorter (a cut ending this close to a corner goes to the corner)
	double minAngle = 10;    ///< Cuts keep corners at least this wide where they can (degrees)
	bool join = true;        ///< Join neighbouring parts that make one convex brush
};

/// The prisms among `pieces` re-cut cleanly (see the file comment); everything else unchanged.
/// New pieces take new ids from `ids` (callers give the old ids back as they need).
[[nodiscard]] std::vector<Solid> tidyPrisms(MapDocument& ids, const std::vector<Solid>& pieces, const PrismTidyOptions& options = {});

/// Convex parts of a simple polygon (seen from above, counter-clockwise, z ignored), cut at its
/// inside corners as tidyPrisms does. Exposed for tests.
[[nodiscard]] std::vector<std::vector<Vec3>> convexParts(const std::vector<Vec3>& polygon, const PrismTidyOptions& options = {});

} // namespace sme
