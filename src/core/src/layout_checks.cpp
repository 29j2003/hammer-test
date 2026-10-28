// layout_checks.cpp - Checks about where things are in the map (see layout_checks.h).

#include <sme/layout_checks.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sme {

namespace {

constexpr double EPS = 0.5;                  ///< Touching is not overlapping
constexpr double PLAYER_HALF = 16;           ///< Player hull: 32 x 32 x 72 above the origin
constexpr double PLAYER_HEIGHT = 72;
constexpr int LIGHTMAP_FINE = 4;             ///< Below: very costly lightmaps
constexpr int LIGHTMAP_COARSE = 128;         ///< Above: light gets blurry
constexpr double NODE_SPACING = 96;          ///< Between the nodes the fix places
constexpr double NODE_MIN_DISTANCE = 48;     ///< Closer to an existing node: left out

/// An outward plane of a brush side.
struct Plane3 {
	Vec3 normal;
	double dist = 0;
};

std::vector<Plane3> planesOf(const Solid& solid) {
	std::vector<Plane3> planes;
	for (const auto& side : solid.sides) {
		const auto& p = side.planePoints;
		const Vec3 n = cross(p[0] - p[1], p[2] - p[1]);
		const double len = length(n);
		if (len < 1e-9) continue;
		const Vec3 unit = n / len;
		planes.push_back({unit, dot(unit, p[1])});
	}
	return planes;
}

/// Brushes that do not block the player: hint/skip and other tool brushes that are not clips or
/// nodraw (nodraw and clips block), and sky (it is a wall too, keeps it).
bool blocksPlayer(const Solid& solid) {
	bool anyBlocking = false;
	for (const auto& side : solid.sides) {
		const std::string m = toLower(side.material);
		const bool passable = m.find("toolshint") != std::string::npos || m.find("toolsskip") != std::string::npos ||
		                      m.find("toolstrigger") != std::string::npos || m.find("toolsareaportal") != std::string::npos ||
		                      m.find("toolsfog") != std::string::npos || m.find("toolsoccluder") != std::string::npos ||
		                      m.find("water") != std::string::npos || m.find("toolsnpcclip") != std::string::npos;
		if (!passable) anyBlocking = true;
	}
	return anyBlocking;
}

/// Box against a convex brush: separated by one of the brush planes or by the box's axes.
bool boxOverlaps(const Bounds& box, const std::vector<Plane3>& planes, const Bounds& brushBox) {
	if (brushBox.empty()) return false;
	if (box.max.x <= brushBox.min.x + EPS || box.min.x >= brushBox.max.x - EPS || box.max.y <= brushBox.min.y + EPS ||
	    box.min.y >= brushBox.max.y - EPS || box.max.z <= brushBox.min.z + EPS || box.min.z >= brushBox.max.z - EPS) {
		return false;
	}
	for (const auto& plane : planes) {
		// The box corner deepest along the normal; if even it is outside, they are apart.
		const Vec3 corner{plane.normal.x > 0 ? box.min.x : box.max.x, plane.normal.y > 0 ? box.min.y : box.max.y,
		                  plane.normal.z > 0 ? box.min.z : box.max.z};
		if (dot(plane.normal, corner) - plane.dist >= -EPS) return false;
	}
	return true;
}

bool pointInBrush(const Vec3& point, const std::vector<Plane3>& planes) {
	if (planes.empty()) return false;
	return std::all_of(planes.begin(), planes.end(), [&](const Plane3& plane) { return dot(plane.normal, point) - plane.dist < -EPS; });
}

struct SolidShape {
	std::vector<Plane3> planes;
	Bounds box;
};

/// The brushes that block: world and func_detail.
std::vector<SolidShape> blockingShapes(const MapDocument& doc) {
	std::vector<SolidShape> shapes;
	const auto add = [&](const Solid& solid) {
		if (!blocksPlayer(solid)) return;
		SolidShape shape;
		shape.planes = planesOf(solid);
		for (const auto& face : buildBrushGeometry(solid).faces) {
			for (const auto& v : face.vertices) shape.box.add(v);
		}
		if (!shape.box.empty()) shapes.push_back(std::move(shape));
	};
	for (const auto& solid : doc.world.solids) add(solid);
	for (const auto& entity : doc.entities) {
		if (!iequals(entity.classname(), "func_detail")) continue;
		for (const auto& solid : entity.solids) add(solid);
	}
	return shapes;
}

bool boxInShapes(const std::vector<SolidShape>& shapes, const Bounds& box) {
	return std::any_of(shapes.begin(), shapes.end(), [&](const SolidShape& s) { return boxOverlaps(box, s.planes, s.box); });
}

Bounds playerBox(const Vec3& feet) {
	Bounds box;
	box.add({feet.x - PLAYER_HALF, feet.y - PLAYER_HALF, feet.z});
	box.add({feet.x + PLAYER_HALF, feet.y + PLAYER_HALF, feet.z + PLAYER_HEIGHT});
	return box;
}

std::string describe(const Entity& e) {
	const auto name = e.get("targetname");
	return e.classname() + (name.empty() ? "" : " \"" + name + "\"") + " (id " + std::to_string(e.id) + ")";
}

/// Flying NPCs use air nodes; makers and helpers are no NPCs that walk.
bool isWalkingNpc(const std::string& classname) {
	if (!startsWithI(classname, "npc_")) return false;
	static const std::set<std::string> notWalking = {
		"npc_maker", "npc_template_maker", "npc_antlion_template_maker", "npc_bullseye", "npc_enemyfinder", "npc_enemyfinder_combinecannon",
		"npc_launcher", "npc_heli_avoidbox", "npc_heli_avoidsphere", "npc_heli_nobomb", "npc_helicopter", "npc_combinegunship",
		"npc_combinedropship", "npc_cscanner", "npc_clawscanner", "npc_manhack", "npc_crow", "npc_seagull", "npc_pigeon", "npc_strider",
		"npc_apcdriver", "npc_vehicledriver", "npc_turret_floor", "npc_turret_ceiling", "npc_turret_ground", "npc_rollermine",
		"npc_barnacle", "npc_sniper", "npc_combine_camera", "npc_grenade_frag", "npc_satchel", "npc_tripmine", "npc_puppet",
		"npc_furniture", "npc_spotlight", "npc_missiledefense", "npc_rocket_turret", "npc_stalker", "npc_ichthyosaur"};
	return !notWalking.count(toLower(classname));
}

bool isNode(const std::string& classname) {
	return iequals(classname, "info_node") || iequals(classname, "info_node_hint") || iequals(classname, "info_hint") ||
	       iequals(classname, "info_node_climb");
}

} // namespace

bool boxInSolid(const MapDocument& doc, const Bounds& box) {
	return boxInShapes(blockingShapes(doc), box);
}

std::vector<Vec3> nodeSpotsAround(const MapDocument& doc, const Vec3& feet) {
	const auto shapes = blockingShapes(doc);
	std::vector<Vec3> spots;
	for (int i = -1; i <= 1; ++i) {
		for (int j = -1; j <= 1; ++j) {
			const Vec3 spot{std::round(feet.x + i * NODE_SPACING), std::round(feet.y + j * NODE_SPACING), std::round(feet.z)};
			// Room for a human NPC above, and floor under it (a little box just below the feet).
			Bounds body;
			body.add({spot.x - 13, spot.y - 13, spot.z + 2});
			body.add({spot.x + 13, spot.y + 13, spot.z + 64});
			Bounds below;
			below.add({spot.x - 2, spot.y - 2, spot.z - 12});
			below.add({spot.x + 2, spot.y + 2, spot.z - 1});
			if (!boxInShapes(shapes, body) && boxInShapes(shapes, below)) spots.push_back(spot);
		}
	}
	return spots;
}

std::vector<Problem> checkLayout(const MapDocument& doc, const CheckContext& /*context*/) {
	std::vector<Problem> problems;
	if (doc.world.solids.empty()) return problems;
	const auto shapes = blockingShapes(doc);

	Bounds world;
	for (const auto& solid : doc.world.solids) {
		for (const auto& face : buildBrushGeometry(solid).faces) {
			for (const auto& v : face.vertices) world.add(v);
		}
	}

	int npcs = 0, nodes = 0;
	Selection npcSelection;
	for (const auto& entity : doc.entities) {
		const std::string classname = entity.classname();
		if (isNode(classname)) ++nodes;
		if (!entity.solids.empty() || entity.get("origin").empty()) continue;
		const Vec3 origin = entity.origin();
		if (isWalkingNpc(classname)) {
			++npcs;
			npcSelection.entities.push_back(entity.id);
		}
		// In the void: outside every world brush's box (a 3D skybox is part of the world too).
		if (!world.empty() && (origin.x < world.min.x - 1 || origin.y < world.min.y - 1 || origin.z < world.min.z - 1 || origin.x > world.max.x + 1 ||
		                       origin.y > world.max.y + 1 || origin.z > world.max.z + 1)) {
			Problem p;
			p.kind = Problem::Kind::EntityOutsideWorld;
			p.severity = Problem::Severity::Error;
			p.entityId = entity.id;
			p.objects.entities.push_back(entity.id);
			p.message = describe(entity) + " is outside the map (in the void) - VBSP reports a leak from it and it does nothing in the game.";
			p.howToFix = "Move it inside the map, or delete it.";
			problems.push_back(std::move(p));
			continue;
		}
		if (iequals(classname, "info_player_start") && boxInShapes(shapes, playerBox(origin))) {
			Problem p;
			p.kind = Problem::Kind::PlayerStartInSolid;
			p.severity = Problem::Severity::Error;
			p.entityId = entity.id;
			p.objects.entities.push_back(entity.id);
			p.message = describe(entity) + " is inside a brush - the player spawns stuck (or falls through the floor).";
			p.howToFix = "The player needs 32 x 32 x 72 units of free space above the start. Move it up onto the floor.";
			p.fixLabel = "Move onto the floor";
			problems.push_back(std::move(p));
		}
	}

	if (npcs > 0 && nodes == 0) {
		Problem p;
		p.kind = Problem::Kind::NpcsWithoutNodes;
		p.severity = Problem::Severity::Warning;
		p.objects = npcSelection;
		p.message = std::to_string(npcs) + (npcs == 1 ? " NPC" : " NPCs") +
		            " but no info_node in the map - NPCs cannot find paths: they stand still, or walk straight into walls.";
		p.howToFix = "Place info_node entities on the floor wherever NPCs should be able to walk (about every 128-256 units, at doors and "
		             "corners). The fix places a few around every NPC to start with.";
		p.fixLabel = "Place nodes around the NPCs";
		problems.push_back(std::move(p));
	}

	// Lightmap scales: tool, sky and nodraw faces get no lightmap and do not count.
	Problem fine, coarse;
	for (const auto& solid : doc.world.solids) {
		for (const auto& side : solid.sides) {
			if (startsWithI(side.material, "tools/") || startsWithI(side.material, "tools\\")) continue;
			const FaceRef ref{{0, solid.id}, side.id};
			if (side.lightmapScale < LIGHTMAP_FINE) fine.objects.faces.push_back(ref);
			else if (side.lightmapScale > LIGHTMAP_COARSE) coarse.objects.faces.push_back(ref);
		}
	}
	if (!fine.objects.faces.empty()) {
		fine.kind = Problem::Kind::LightmapScale;
		fine.severity = Problem::Severity::Warning;
		fine.message = std::to_string(fine.objects.faces.size()) +
		               " faces have a lightmap scale below 4 - very fine light that costs a lot of memory and VRAD time (0 or less is invalid).";
		fine.howToFix = "Use 16 (the default); 8 only where shadows must be sharp (Face mode, Lightmap scale; View > Lightmap density shows it).";
		fine.fixLabel = "Set to 16";
		fine.to = "16";
		problems.push_back(std::move(fine));
	}
	if (!coarse.objects.faces.empty()) {
		coarse.kind = Problem::Kind::LightmapScale;
		coarse.severity = Problem::Severity::Info;
		coarse.message = std::to_string(coarse.objects.faces.size()) + " faces have a lightmap scale above 128 - their light and shadows are very blurry.";
		coarse.howToFix = "Fine for faces far away or never in light; else use 16-64.";
		coarse.fixLabel = "Set to 64";
		coarse.to = "64";
		problems.push_back(std::move(coarse));
	}
	return problems;
}

Change applyLayoutFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities) {
	switch (problem.kind) {
		case Problem::Kind::PlayerStartInSolid: {
			const auto* start = doc.findEntity(problem.entityId);
			if (!start) return {};
			const auto shapes = blockingShapes(doc);
			// Step up to the top of what it is stuck in, a few times (stacked brushes).
			Vec3 feet = start->origin();
			for (int round = 0; round < 8 && boxInShapes(shapes, playerBox(feet)); ++round) {
				double top = feet.z;
				const Bounds box = playerBox(feet);
				for (const auto& shape : shapes) {
					if (boxOverlaps(box, shape.planes, shape.box)) top = std::max(top, shape.box.max.z);
				}
				if (top <= feet.z + EPS) top = feet.z + 8;
				feet.z = std::ceil(top) + 1;
			}
			if (boxInShapes(shapes, playerBox(feet))) return {};
			auto change = edit::setKeyValue(doc, {problem.entityId}, "origin", formatVec3(feet));
			change.name = "Move player start onto the floor";
			return change;
		}
		case Problem::Kind::LightmapScale: {
			const int scale = std::atoi(problem.to.c_str());
			if (scale <= 0) return {};
			auto change = edit::modifyFaces(doc, problem.objects.faces, "Set lightmap scale", [scale](Side& side) { side.lightmapScale = scale; });
			return change;
		}
		case Problem::Kind::NpcsWithoutNodes: {
			std::vector<Vec3> spots;
			for (const int id : problem.objects.entities) {
				const auto* npc = doc.findEntity(id);
				if (!npc) continue;
				for (const auto& spot : nodeSpotsAround(doc, npc->origin())) {
					const bool close = std::any_of(spots.begin(), spots.end(), [&](const Vec3& s) { return length(s - spot) < NODE_MIN_DISTANCE; });
					if (!close) spots.push_back(spot);
				}
			}
			if (spots.empty()) return {};
			const auto* definition = entities ? entities->find("info_node") : nullptr;
			Transaction t(doc, "Place nodes around the NPCs");
			for (const auto& spot : spots) {
				Entity node;
				node.id = doc.newEntityId();
				t.touchEntity(node.id);
				node.set("classname", "info_node");
				if (definition) {
					for (const auto& property : definition->properties) {
						if (property.kind != EntityProperty::Kind::Flags && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
							node.set(property.name, property.defaultValue);
						}
					}
				}
				node.setOrigin(spot);
				auto editor = KvNode::makeBlock("editor");
				editor.set("color", "220 30 220");
				editor.set("visgroupshown", "1");
				editor.set("visgroupautoshown", "1");
				node.extra.push_back(std::move(editor));
				doc.entities.push_back(std::move(node));
			}
			return t.finish();
		}
		default:
			return {};
	}
}

} // namespace sme
