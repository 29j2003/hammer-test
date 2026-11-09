// gap_fill.h - "Fill gap": a brush that closes the space between the selected brushes.
//
// Two blocks with a gap between them, a wedge left open where two road pieces meet at an angle,
// a step between two floors of different height: select the brushes around the gap and the fill
// takes the space between them, nothing else.
// - Hull ("follow the brushes"): for each pair, the hull of the faces turned towards each other
//   (a wedge at an angled joint, a slope between two heights; the inside of an L stays open);
// - Box: only the box between the brushes (square, flat top at the lower of the two heights).
// Brushes already standing in the gap are cut out of the fill (it never overlaps anything), and
// pieces that would be slivers or broken are left out. Each side of the fill
// continues the texture of a selected brush side lying in the same plane (a floor's top runs on
// over the fill), the others get the material most of the selection uses.
// The plan is computed without changing the map (for a live preview); applying is one undo step.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sme/history.h>
#include <sme/map.h>

namespace sme {

struct GapFillOptions {
	enum class Kind { Hull, Box };
	Kind kind = Kind::Hull;
	std::string material;   ///< Material for sides with no matching neighbour; empty: what the selection uses most
};

struct GapFillPlan {
	std::vector<Solid> fill;              ///< The new brushes
	std::vector<std::string> notes;       ///< What was left out and why
	std::string material;                 ///< The material sides without a neighbour got

	[[nodiscard]] bool ok() const { return !fill.empty(); }
	/// Volume of the fill (cubic units).
	[[nodiscard]] double volume() const;
};

/// Plans the fill between the brushes `selected` (two or more). Reads `doc` only; ids come from
/// `ids` (the document itself when applying, a scratch document for a preview).
[[nodiscard]] GapFillPlan planGapFill(const MapDocument& doc, MapDocument& ids, const std::vector<SolidRef>& selected, const GapFillOptions& options);

/// Adds the fill to the world as one undo step; returns the change and the new brushes.
[[nodiscard]] std::pair<Change, std::vector<SolidRef>> applyGapFill(MapDocument& doc, const GapFillPlan& plan, const std::string& name = "Fill gap");

} // namespace sme
