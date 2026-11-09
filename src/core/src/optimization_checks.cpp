// optimization_checks.cpp - Checks about engine limits and compile time (see optimization_checks.h).

#include <sme/optimization_checks.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/filesystem.h>
#include <sme/map_budget.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <numeric>
#include <set>

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

/// A brush that hides what lies flat against it: no tool material but nodraw (and toolsblack).
bool opaqueBrush(const Solid& solid) {
	return std::none_of(solid.sides.begin(), solid.sides.end(), [](const Side& side) {
		const std::string m = toLower(side.material);
		return m.starts_with("tools/") && m != "tools/toolsnodraw" && m != "tools/toolsblack";
	});
}

/// Whether point `p` lies inside the convex polygon `face` (on its plane), with a small tolerance.
bool insidePolygon(const FaceGeometry& face, const Vec3& p) {
	const auto& v = face.vertices;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const Vec3& a = v[i];
		const Vec3& b = v[(i + 1) % v.size()];
		const Vec3 edge = b - a;
		const double len = length(edge);
		if (len < 1e-9) continue;
		// Counter-clockwise seen from outside: the inside is to the left of each edge.
		if (dot(cross(edge, p - a), face.normal) / len < -0.05) return false;
	}
	return true;
}

/// A static brush the hidden-face check looks at.
struct StaticBrush {
	SolidRef ref;
	const Solid* solid = nullptr;
	BrushGeometry geometry;
	bool detail = false;
	bool opaque = false;
};

} // namespace

bool seeThroughByName(std::string_view material) {
	const std::string m = toLower(material);
	for (const char* word : {"glass", "water", "window", "fence", "grate", "chain", "translucent", "lattice", "mesh", "wire", "decal", "overlay", "foliage",
	                         "leaves", "vent", "screen"}) {
		if (m.find(word) != std::string::npos) return true;
	}
	return false;
}

std::vector<FaceRef> hiddenFaces(const MapDocument& doc, const std::function<bool(const std::string&)>& seeThrough) {
	const auto through = [&](const std::string& material) { return seeThrough ? seeThrough(material) : seeThroughByName(material); };
	std::vector<StaticBrush> brushes;
	const auto add = [&](int entityId, const Solid& solid, bool detail) {
		if (solid.sides.empty()) return;
		StaticBrush b;
		b.ref = {entityId, solid.id};
		b.solid = &solid;
		b.geometry = buildBrushGeometry(solid);
		if (!b.geometry.valid) return;
		b.detail = detail;
		b.opaque = opaqueBrush(solid);
		brushes.push_back(std::move(b));
	};
	for (const auto& solid : doc.world.solids) add(0, solid, false);
	for (const auto& entity : doc.entities) {
		if (!iequals(entity.classname(), "func_detail")) continue;
		for (const auto& solid : entity.solids) add(entity.id, solid, true);
	}
	// Sweep along X: only brushes whose boxes touch are compared.
	std::vector<std::size_t> order(brushes.size());
	std::iota(order.begin(), order.end(), std::size_t{0});
	std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return brushes[a].geometry.bounds.min.x < brushes[b].geometry.bounds.min.x; });
	constexpr double EPS = 0.05;
	const auto touch = [](const Bounds& a, const Bounds& b) {
		return a.min.x <= b.max.x + EPS && b.min.x <= a.max.x + EPS && a.min.y <= b.max.y + EPS && b.min.y <= a.max.y + EPS && a.min.z <= b.max.z + EPS &&
		       b.min.z <= a.max.z + EPS;
	};
	std::set<std::pair<std::size_t, int>> hidden;   // (brush, side index)
	// Faces of `a` covered by an opaque face of `b`.
	const auto cover = [&](std::size_t ai, std::size_t bi) {
		const auto& a = brushes[ai];
		const auto& b = brushes[bi];
		if (!b.opaque || (!a.detail && !b.detail)) return;   // world against world: VBSP drops those faces itself
		for (const auto& face : a.geometry.faces) {
			const Side& side = a.solid->sides[static_cast<std::size_t>(face.sideIndex)];
			if (face.vertices.size() < 3 || side.dispInfo() || toLower(side.material).starts_with("tools/") || through(side.material)) continue;
			if (hidden.count({ai, face.sideIndex})) continue;
			const double d = dot(face.normal, face.vertices.front());
			for (const auto& other : b.geometry.faces) {
				const Side& otherSide = b.solid->sides[static_cast<std::size_t>(other.sideIndex)];
				if (other.vertices.size() < 3 || dot(face.normal, other.normal) > -0.9999) continue;
				if (std::abs(dot(face.normal, other.vertices.front()) - d) > EPS) continue;
				if (otherSide.dispInfo() || through(otherSide.material)) continue;
				if (std::all_of(face.vertices.begin(), face.vertices.end(), [&](const Vec3& p) { return insidePolygon(other, p); })) {
					hidden.insert({ai, face.sideIndex});
					break;
				}
			}
		}
	};
	for (std::size_t i = 0; i < order.size(); ++i) {
		const auto& bi = brushes[order[i]].geometry.bounds;
		for (std::size_t j = i + 1; j < order.size(); ++j) {
			const auto& bj = brushes[order[j]].geometry.bounds;
			if (bj.min.x > bi.max.x + EPS) break;
			if (!touch(bi, bj)) continue;
			cover(order[i], order[j]);
			cover(order[j], order[i]);
		}
	}
	std::vector<FaceRef> out;
	for (std::size_t i = 0; i < brushes.size(); ++i) {
		for (std::size_t s = 0; s < brushes[i].solid->sides.size(); ++s) {
			if (hidden.count({i, static_cast<int>(s)})) out.push_back({brushes[i].ref, brushes[i].solid->sides[s].id});
		}
	}
	return out;
}

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
	std::function<bool(const std::string&)> seeThrough;
	if (context.files) {
		seeThrough = [&](const std::string& name) {
			const std::string key = toLower(name);
			if (context.cache) {
				if (const auto it = context.cache->seeThrough.find(key); it != context.cache->seeThrough.end()) return it->second;
			}
			const auto material = loadMaterial(*context.files, name);
			bool through = seeThroughByName(name);
			if (material) {
				const std::string shader = toLower(material->shader);
				through = material->flag("$translucent") || material->flag("$alphatest") || material->flag("$additive") ||
				          shader.find("water") != std::string::npos || shader.find("refract") != std::string::npos ||
				          (!material->param("$alpha").empty() && std::atof(material->param("$alpha").c_str()) < 1.0);
			}
			if (context.cache) context.cache->seeThrough.emplace(key, through);
			return through;
		};
	}
	const auto hidden = hiddenFaces(doc, seeThrough);
	if (!hidden.empty()) {
		Problem p;
		p.kind = Problem::Kind::HiddenFaces;
		p.severity = Problem::Severity::Info;
		p.objects.faces = hidden;
		p.message = std::to_string(hidden.size()) + (hidden.size() == 1 ? " face lies" : " faces lie") +
		            " flat against a func_detail brush (or is the face of one lying against another brush) - nobody sees "
		            + (hidden.size() == 1 ? "it" : "them") + ", but VBSP keeps such faces with their lightmaps.";
		p.howToFix = "Make them nodraw: the map gets fewer faces and lightmaps and compiles a little quicker. Only faces fully covered by an "
		             "opaque, static brush are listed (never doors, glass, water or tool brushes).";
		p.fixLabel = "Make nodraw";
		problems.push_back(std::move(p));
	}
	return problems;
}

Change applyOptimizationFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities) {
	if (problem.kind == Problem::Kind::HiddenFaces) {
		Selection faces;
		for (const auto& ref : problem.objects.faces) {
			if (const auto* solid = doc.findSolid(ref.solid);
			    solid && std::any_of(solid->sides.begin(), solid->sides.end(), [&](const Side& side) { return side.id == ref.sideId; })) {
				faces.faces.push_back(ref);
			}
		}
		if (faces.faces.empty()) return {};
		auto change = edit::applyMaterial(doc, faces, "TOOLS/TOOLSNODRAW");
		change.name = "Make hidden faces nodraw";
		return change;
	}
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
