// map_budget.cpp - Counts a map against the engine limits (see map_budget.h).

#include <sme/map_budget.h>

#include <sme/text.h>

#include <cmath>
#include <unordered_set>

namespace sme {

namespace {

/// A plane as VBSP keeps it: it stores every plane together with its flipped twin, so a plane
/// and its opposite count once (as a pair). Rounded like VBSP's epsilons (normal 1e-5, dist 0.01).
struct PlaneKey {
	std::int64_t nx, ny, nz, d;
	bool operator==(const PlaneKey&) const = default;
};

struct PlaneKeyHash {
	std::size_t operator()(const PlaneKey& k) const noexcept {
		std::size_t h = std::hash<std::int64_t>{}(k.nx);
		for (const std::int64_t v : {k.ny, k.nz, k.d}) {
			h ^= std::hash<std::int64_t>{}(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		}
		return h;
	}
};

PlaneKey planeKey(const Plane& plane) {
	Vec3 n = plane.normal;
	double d = plane.dist;
	// The twin with the first non-zero component positive stands for the pair.
	const double first = std::abs(n.x) > 1e-6 ? n.x : (std::abs(n.y) > 1e-6 ? n.y : n.z);
	if (first < 0) {
		n = -n;
		d = -d;
	}
	return {std::llround(n.x * 1e4), std::llround(n.y * 1e4), std::llround(n.z * 1e4), std::llround(d * 100)};
}

/// The lights VRAD bakes (world lights); light_dynamic is a game entity, not a world light.
bool isWorldLight(const std::string& classname) {
	return iequals(classname, "light") || iequals(classname, "light_spot") || iequals(classname, "light_environment");
}

} // namespace

BudgetLimits budgetLimitsForApp(std::uint32_t appId) {
	BudgetLimits limits;
	switch (appId) {
		case 550:   // Left 4 Dead 2
		case 620:   // Portal 2
		case 630:   // Alien Swarm
		case 730:   // Counter-Strike: Global Offensive
			limits.entities = 16384;
			break;
		default:
			break;
	}
	return limits;
}

bool compiledAway(const Entity& entity) {
	const auto classname = entity.classname();
	// VBSP turns these into world data and drops them from the entity list.
	for (const char* cls : {"func_detail", "prop_static", "info_overlay", "info_overlay_transition", "env_cubemap", "func_viscluster",
	                        "info_lighting", "info_null", "prop_detail", "func_instance", "func_instance_parms", "func_instance_io_proxy",
	                        "info_no_dynamic_shadow", "infodecal"}) {
		if (iequals(classname, cls)) {
			return true;
		}
	}
	// The game deletes lights without a name at spawn (VRAD has baked them).
	if (isWorldLight(classname) && entity.get("targetname").empty()) {
		return true;
	}
	return false;
}

std::vector<BudgetItem> mapBudget(const MapDocument& doc, const BudgetLimits& limits) {
	std::size_t brushes = 0, sides = 0, displacements = 0;
	std::unordered_set<PlaneKey, PlaneKeyHash> planes;
	const auto countSolids = [&](const std::vector<Solid>& solids) {
		for (const auto& solid : solids) {
			++brushes;
			sides += solid.sides.size();
			for (const auto& side : solid.sides) {
				planes.insert(planeKey(side.plane()));
				if (side.dispInfo()) ++displacements;
			}
		}
	};
	countSolids(doc.world.solids);
	std::size_t edicts = 2;   // the world and the player
	std::size_t overlays = 0, cubemaps = 0, lights = 0, staticProps = 0;
	for (const auto& entity : doc.entities) {
		countSolids(entity.solids);
		const auto classname = entity.classname();
		if (!compiledAway(entity)) ++edicts;
		if (iequals(classname, "info_overlay")) ++overlays;
		if (iequals(classname, "env_cubemap")) ++cubemaps;
		if (iequals(classname, "prop_static")) ++staticProps;
		if (isWorldLight(classname)) ++lights;
	}

	std::vector<BudgetItem> items;
	items.push_back({"Brushes", brushes, limits.brushes, false, "World brushes and the brushes of brush entities (func_detail too)."});
	items.push_back({"Brush sides", sides, limits.brushSides, true, "Faces of all brushes; VBSP adds bevel sides, so the compiled count is higher."});
	items.push_back({"Planes", planes.size() * 2, limits.planes, true, "Different planes of all faces; VBSP stores each with its flipped twin."});
	items.push_back({"Map entities", doc.entities.size() + 1, limits.entities, false, "Every entity in the VMF plus worldspawn (VBSP's limit)."});
	items.push_back({"Edicts", edicts, limits.edicts, true,
	                 "Entities alive in the game at the start: without the ones compiled away (func_detail, prop_static, unnamed "
	                 "lights, ...), plus world and player. The game spawns more (weapons, NPC parts), keep room."});
	items.push_back({"Overlays", overlays, limits.overlays, false, "info_overlay entities."});
	items.push_back({"Displacements", displacements, limits.displacements, false, "Faces that are displacements."});
	items.push_back({"Cubemaps", cubemaps, limits.cubemaps, false, "env_cubemap entities."});
	items.push_back({"Lights", lights, limits.lights, false, "light, light_spot, light_environment (world lights VRAD bakes)."});
	items.push_back({"Static props", staticProps, limits.staticProps, false, "prop_static entities (no fixed limit; each costs memory and draw calls)."});
	return items;
}

} // namespace sme
