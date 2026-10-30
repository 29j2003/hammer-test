// optimization_checks.cpp - Checks about engine limits and compile time (see optimization_checks.h).

#include <sme/optimization_checks.h>

#include <sme/brush.h>
#include <sme/map_budget.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sme {

namespace {

constexpr double NEAR_LIMIT = 0.85;    ///< Share of a limit from which the check warns
constexpr std::size_t ROUND_SIDES = 9; ///< A brush with more sides is a round shape (a box has 6, a wedge 5)
constexpr double SMALL_SIZE = 16;      ///< A brush whose largest extent is at most this is a small piece

/// Tool brushes keep their own job (hint, skip, clip, trigger, areaportal, occluder, sky, ...).
bool usesToolMaterial(const Solid& solid) {
	return std::any_of(solid.sides.begin(), solid.sides.end(), [](const Side& side) {
		const std::string m = toLower(side.material);
		return m.find("tools") != std::string::npos && m.find("toolsnodraw") == std::string::npos;
	});
}

std::string percent(double fraction) {
	char text[16];
	std::snprintf(text, sizeof text, "%.0f %%", fraction * 100.0);
	return text;
}

/// What to do about one limit (by the budget item's name, see map_budget).
std::string limitAdvice(const std::string& name) {
	const std::string n = toLower(name);
	if (n.find("side") != std::string::npos || n.find("plane") != std::string::npos || n == "brushes") {
		return "Make round shapes and small pieces func_detail (Problems lists them), merge boxes that lie side by side, "
		       "and turn very detailed brushwork into models.";
	}
	if (n.find("entit") != std::string::npos) {
		return "Remove unused entities; logic that runs once can often be one logic_relay; props that never move can be prop_static.";
	}
	if (n.find("overlay") != std::string::npos) return "Use fewer info_overlay entities, or merge several decals into one overlay texture.";
	if (n.find("displacement") != std::string::npos) return "Use fewer, larger displacements (power 3 or 4 instead of many small ones).";
	if (n.find("cubemap") != std::string::npos) return "Use fewer env_cubemap entities: one per room or area is enough.";
	if (n.find("light") != std::string::npos) return "Remove lights that add little; one bright light often replaces several dim ones.";
	return "Simplify the map in that respect.";
}

} // namespace

std::vector<SolidRef> detailCandidates(const MapDocument& doc) {
	std::vector<SolidRef> refs;
	for (const auto& solid : doc.world.solids) {
		if (solid.sides.empty() || usesToolMaterial(solid)) continue;
		if (solid.sides.size() >= ROUND_SIDES) {
			refs.push_back({0, solid.id});
			continue;
		}
		Bounds box;
		for (const auto& face : buildBrushGeometry(solid).faces) {
			for (const auto& v : face.vertices) box.add(v);
		}
		if (box.empty()) continue;
		const Vec3 size = box.max - box.min;
		if (std::max({size.x, size.y, size.z}) <= SMALL_SIZE) refs.push_back({0, solid.id});
	}
	return refs;
}

std::vector<Problem> checkOptimization(const MapDocument& doc, const CheckContext& context) {
	std::vector<Problem> problems;
	const BudgetLimits limits = context.limits ? *context.limits : BudgetLimits{};
	for (const auto& item : mapBudget(doc, limits)) {
		if (item.limit == 0 || item.fraction() < NEAR_LIMIT) continue;
		Problem p;
		p.kind = Problem::Kind::NearEngineLimit;
		p.severity = item.over() ? Problem::Severity::Error : Problem::Severity::Warning;
		p.message = item.name + ": " + std::to_string(item.count) + " of " + std::to_string(item.limit) + " (" + percent(item.fraction()) + ") - " +
		            (item.over() ? "over the engine limit: VBSP stops with MAX_MAP_..." : "close to the engine limit (VBSP stops with MAX_MAP_... above it).") +
		            (item.estimate ? " The count is an estimate (the compiler adds or removes a few)." : "");
		p.howToFix = limitAdvice(item.name) + " View > Map Budget shows every limit.";
		problems.push_back(std::move(p));
	}

	const auto candidates = detailCandidates(doc);
	if (!candidates.empty()) {
		Problem p;
		p.kind = Problem::Kind::DetailCandidates;
		p.severity = Problem::Severity::Info;
		p.objects.solids = candidates;
		p.message = std::to_string(candidates.size()) + (candidates.size() == 1 ? " world brush is" : " world brushes are") +
		            " only detail (round shapes or small pieces) - as world brushes they cut the map into many vis leaves: VVIS gets slow.";
		p.howToFix = "Tie them to a func_detail (they still block and cast shadows, but vis ignores them). Keep a brush world if it closes "
		             "the map off from the void or separates two rooms - detail brushes do not seal (a leak check after compiling shows it).";
		p.fixLabel = "Make func_detail";
		problems.push_back(std::move(p));
	}
	return problems;
}

Change applyOptimizationFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities) {
	if (problem.kind != Problem::Kind::DetailCandidates || problem.objects.solids.empty()) return {};
	Selection selection;
	for (const auto& ref : problem.objects.solids) {
		if (ref.entityId == 0 && doc.findSolid(ref)) selection.solids.push_back(ref);
	}
	if (selection.solids.empty()) return {};
	const auto* definition = entities ? entities->find("func_detail") : nullptr;
	auto change = edit::tieToEntity(doc, selection, "func_detail", definition).first;
	change.name = "Make details func_detail";
	return change;
}

} // namespace sme
