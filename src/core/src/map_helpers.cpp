// map_helpers.cpp - Ropes and ladders in one step (see map_helpers.h).

#include <sme/map_helpers.h>

#include <sme/brush.h>
#include <sme/doors.h>
#include <sme/layout_checks.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sme {

namespace {

constexpr int ROPE_SEGMENTS = 12;      ///< Lines per span in the preview
constexpr double LADDER_GAP = 18;      ///< Climbing line in front of the ladder (half a player plus a little)
constexpr double DISMOUNT_AHEAD = 40;  ///< Bottom dismount: steps back from the ladder
constexpr double DISMOUNT_BEHIND = 32; ///< Top dismount: onto the ledge behind the top of the ladder

/// A new point entity with the FGD's defaults (like the entity tool places it).
Entity& addEntity(MapDocument& doc, Transaction& t, const std::string& classname, const Vec3& origin, const EntityCatalog* catalog) {
	Entity entity;
	entity.id = doc.newEntityId();
	t.touchEntity(entity.id);
	entity.set("classname", classname);
	if (const auto* definition = catalog ? catalog->find(classname) : nullptr) {
		for (const auto& property : definition->properties) {
			if (property.kind != EntityProperty::Kind::Flags && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
				entity.set(property.name, property.defaultValue);
			}
		}
	}
	entity.setOrigin(origin);
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "220 30 220");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	entity.extra.push_back(std::move(editor));
	doc.entities.push_back(std::move(entity));
	return doc.entities.back();
}

Bounds solidBox(const Solid& solid) {
	Bounds box;
	for (const auto& face : buildBrushGeometry(solid).faces) {
		for (const auto& v : face.vertices) box.add(v);
	}
	return box;
}

Vec3 roundVec(const Vec3& v) {
	return {std::round(v.x), std::round(v.y), std::round(v.z)};
}

} // namespace

double ropeSag(const Vec3& a, const Vec3& b, double slack) {
	const double span = length(b - a);
	if (span < 1e-6 || slack <= 0) return 0;
	return std::sqrt(3.0 * span * slack / 8.0);
}

std::vector<RopeCurve> ropeCurves(const MapDocument& doc) {
	std::vector<RopeCurve> curves;
	std::map<std::string, const Entity*> byName;
	for (const auto& entity : doc.entities) {
		const auto name = toLower(entity.get("targetname"));
		if (!name.empty() && (iequals(entity.classname(), "keyframe_rope") || iequals(entity.classname(), "move_rope"))) byName.emplace(name, &entity);
	}
	for (const auto& start : doc.entities) {
		if (!iequals(start.classname(), "move_rope")) continue;
		RopeCurve curve;
		curve.startEntityId = start.id;
		std::set<int> seen{start.id};
		const Entity* current = &start;
		curve.points.push_back(current->origin());
		while (true) {
			const auto next = byName.find(toLower(current->get("NextKey")));
			if (next == byName.end() || seen.count(next->second->id)) break;   // end of the chain, or a loop
			const Vec3 a = current->origin(), b = next->second->origin();
			double slack = 25;
			try {
				const auto value = current->get("Slack");
				if (!value.empty()) slack = std::stod(value);
			} catch (...) {
			}
			const double sag = ropeSag(a, b, slack);
			for (int i = 1; i <= ROPE_SEGMENTS; ++i) {
				const double t = double(i) / ROPE_SEGMENTS;
				Vec3 p = a + (b - a) * t;
				p.z -= 4 * sag * t * (1 - t);   // parabola: deepest in the middle
				curve.points.push_back(p);
			}
			seen.insert(next->second->id);
			current = next->second;
		}
		if (curve.points.size() >= 2) curves.push_back(std::move(curve));
	}
	return curves;
}

namespace edit {

std::vector<Vec3> ropePointsOf(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog) {
	std::vector<Vec3> points;
	for (const int id : selection.entities) {
		const auto* entity = doc.findEntity(id);
		if (!entity || id == 0) continue;
		if (entity->solids.empty()) {
			points.push_back(entity->origin());
		} else {
			Bounds box;
			for (const auto& solid : entity->solids) box.add(solidBox(solid));
			if (!box.empty()) points.push_back({box.center().x, box.center().y, box.max.z});
		}
	}
	(void)catalog;
	for (const auto& ref : selection.solids) {
		for (const auto& solid : doc.world.solids) {
			if (ref.entityId == 0 && solid.id == ref.solidId) {
				const Bounds box = solidBox(solid);
				if (!box.empty()) points.push_back({box.center().x, box.center().y, box.max.z});
			}
		}
	}
	return points;
}

RopeResult makeRope(MapDocument& doc, const std::vector<Vec3>& points, const EntityCatalog* catalog, double slack) {
	RopeResult result;
	if (points.size() < 2) return result;
	const std::string base = uniqueTargetName(doc, "move_rope");
	std::set<std::string> taken;
	for (const auto& entity : doc.entities) taken.insert(toLower(entity.get("targetname")));
	std::vector<std::string> names;
	for (std::size_t i = 0; i < points.size(); ++i) {
		std::string name = i == 0 ? base : base + "_" + std::to_string(i + 1);
		for (int n = 2; taken.count(toLower(name)); ++n) name = base + "_" + std::to_string(i + 1) + "_" + std::to_string(n);
		taken.insert(toLower(name));
		names.push_back(name);
	}
	Transaction t(doc, "Make rope");
	for (std::size_t i = 0; i < points.size(); ++i) {
		Entity& e = addEntity(doc, t, i == 0 ? "move_rope" : "keyframe_rope", roundVec(points[i]), catalog);
		e.set("targetname", names[i]);
		if (i + 1 < points.size()) {
			e.set("NextKey", names[i + 1]);
		}
		e.set("Slack", std::to_string(int(std::lround(slack))));
		if (e.get("Width").empty()) e.set("Width", "2");
		if (e.get("RopeMaterial").empty()) e.set("RopeMaterial", "cable/cable.vmt");
		if (e.get("Subdiv").empty()) e.set("Subdiv", "2");
		result.entityIds.push_back(e.id);
	}
	result.change = t.finish();
	return result;
}

LadderResult makeLadder(MapDocument& doc, const Bounds& ladder, const EntityCatalog* catalog) {
	LadderResult result;
	if (ladder.empty() || ladder.size().z < 32) return result;
	// The thin horizontal axis is the ladder's depth; the front is the side with room for a player.
	const Vec3 size = ladder.size();
	const Vec3 axis = size.x <= size.y ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
	const Vec3 center = ladder.center();
	const double halfDepth = (size.x <= size.y ? size.x : size.y) / 2;
	const auto roomAt = [&](const Vec3& direction) {
		const Vec3 spot = center + direction * (halfDepth + LADDER_GAP);
		Bounds body;
		body.add({spot.x - 14, spot.y - 14, ladder.min.z + 8});
		body.add({spot.x + 14, spot.y + 14, ladder.min.z + 64});
		return !boxInSolid(doc, body);
	};
	Vec3 front = axis;
	if (!roomAt(axis) && roomAt(axis * -1.0)) front = axis * -1.0;
	result.front = front;

	const Vec3 line = center + front * (halfDepth + LADDER_GAP);
	const Vec3 bottom{line.x, line.y, ladder.min.z};
	const Vec3 top{line.x, line.y, ladder.max.z};
	Transaction t(doc, "Make ladder");
	Entity& rung = addEntity(doc, t, "func_useableladder", roundVec(bottom), catalog);
	rung.set("point0", formatVec3(roundVec(bottom)));
	rung.set("point1", formatVec3(roundVec(top)));
	result.ladderId = rung.id;
	// Get off at the bottom (a step back from the ladder) and at the top (over the ledge behind it).
	const Vec3 low = bottom + front * DISMOUNT_AHEAD;
	const Vec3 high = Vec3{center.x, center.y, ladder.max.z} - front * DISMOUNT_BEHIND;
	for (const Vec3& spot : {high, low}) {
		Entity& d = addEntity(doc, t, "info_ladder_dismount", roundVec(spot), catalog);
		result.dismountIds.push_back(d.id);
	}
	result.change = t.finish();
	return result;
}

} // namespace edit

} // namespace sme
