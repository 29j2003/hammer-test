// map_tools.cpp - Cordons, track chains, drop to floor (see map_tools.h).

#include <sme/map_tools.h>

#include <sme/brush.h>
#include <sme/entity_catalog.h>
#include <sme/shapes.h>
#include <sme/sky_presets.h>
#include <sme/text.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <set>

namespace sme {

namespace {

bool readBox(const KvNode& node, Bounds& box) {
	const auto parse = [](std::string text, Vec3& out) {
		std::erase_if(text, [](char c) { return c == '(' || c == ')'; });
		return parseVec3(text, out);
	};
	Vec3 mins, maxs;
	if (!parse(node.get("mins"), mins) || !parse(node.get("maxs"), maxs)) {
		return false;
	}
	box = {};
	box.add(mins);
	box.add(maxs);
	return !box.empty();
}

std::string boxText(const Vec3& v) {
	return "(" + formatVec3(v) + ")";
}

bool overlaps(const Bounds& a, const Bounds& b) {
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] < b.min[i] || b.max[i] < a.min[i]) {
			return false;
		}
	}
	return true;
}

} // namespace

CordonSet readCordons(const MapDocument& doc) {
	CordonSet set;
	for (const auto& block : doc.footerBlocks) {
		if (iequals(block.key, "cordons")) {
			set.active = block.get("active", "0") == "1";
			for (const auto& child : block.children) {
				if (!child.block || !iequals(child.key, "cordon")) {
					continue;
				}
				Cordon cordon;
				cordon.name = child.get("name", "cordon");
				cordon.active = child.get("active", "1") == "1";
				if (const auto* box = child.find("box"); box && readBox(*box, cordon.box)) {
					set.cordons.push_back(std::move(cordon));
				}
			}
		} else if (iequals(block.key, "cordon")) {
			// Older Hammer: one cordon with mins/maxs directly in the block.
			Cordon cordon;
			if (readBox(block, cordon.box)) {
				set.active = block.get("active", "0") == "1";
				set.cordons.push_back(std::move(cordon));
			}
		}
	}
	return set;
}

void writeCordons(MapDocument& doc, const CordonSet& cordons) {
	std::erase_if(doc.footerBlocks, [](const KvNode& block) { return iequals(block.key, "cordons") || iequals(block.key, "cordon"); });
	if (cordons.cordons.empty()) {
		return;
	}
	auto block = KvNode::makeBlock("cordons");
	block.set("active", cordons.active ? "1" : "0");
	for (const auto& cordon : cordons.cordons) {
		auto node = KvNode::makeBlock("cordon");
		node.set("name", cordon.name);
		node.set("active", cordon.active ? "1" : "0");
		auto box = KvNode::makeBlock("box");
		box.set("mins", boxText(cordon.box.min));
		box.set("maxs", boxText(cordon.box.max));
		node.children.push_back(std::move(box));
		block.children.push_back(std::move(node));
	}
	doc.footerBlocks.push_back(std::move(block));
}

MapDocument cordonedCopy(const MapDocument& doc, const CordonSet& cordons, const EntityCatalog* catalog, const std::string& sealMaterial) {
	MapDocument copy = doc;
	std::vector<Bounds> boxes;
	for (const auto& cordon : cordons.cordons) {
		if (cordon.active) {
			boxes.push_back(cordon.box);
		}
	}
	if (!cordons.active || boxes.empty()) {
		return copy;
	}
	const auto inside = [&](const Bounds& b) {
		return std::any_of(boxes.begin(), boxes.end(), [&](const Bounds& box) { return overlaps(b, box); });
	};
	std::erase_if(copy.world.solids, [&](const Solid& solid) { return !inside(buildBrushGeometry(solid).bounds); });
	std::erase_if(copy.entities, [&](const Entity& entity) { return !inside(edit::entityBounds(entity, catalog)); });
	// Seal every cordon with six slabs just outside it, like Hammer does.
	constexpr double THICKNESS = 16;
	for (const auto& box : boxes) {
		const Vec3 lo = box.min, hi = box.max;
		for (int axis = 0; axis < 3; ++axis) {
			for (int side = 0; side < 2; ++side) {
				Bounds slab;
				Vec3 a = lo - Vec3{THICKNESS, THICKNESS, THICKNESS}, b = hi + Vec3{THICKNESS, THICKNESS, THICKNESS};
				if (side == 0) {
					b[axis] = lo[axis];
				} else {
					a[axis] = hi[axis];
				}
				slab.add(a);
				slab.add(b);
				copy.world.solids.push_back(makeBlock(copy, slab, sealMaterial));
			}
		}
	}
	return copy;
}

std::vector<int> trackChain(const MapDocument& doc, int entityId) {
	std::vector<int> chain;
	std::set<int> seen;
	std::vector<int> todo{entityId};
	while (!todo.empty()) {
		const int id = todo.back();
		todo.pop_back();
		if (!seen.insert(id).second) {
			continue;
		}
		const auto* entity = doc.findEntity(id);
		if (!entity || id == 0) {
			continue;
		}
		chain.push_back(id);
		const auto name = entity->get("targetname");
		const std::vector<std::string> next{entity->get("target"), entity->get("altpath")};
		for (const auto& other : doc.entities) {
			const auto otherName = other.get("targetname");
			const bool forward = !otherName.empty() && std::any_of(next.begin(), next.end(), [&](const std::string& n) { return iequals(n, otherName); });
			const bool backward = !name.empty() && (iequals(other.get("target"), name) || iequals(other.get("altpath"), name));
			if (forward || backward) {
				todo.push_back(other.id);
			}
		}
	}
	std::sort(chain.begin(), chain.end());
	return chain;
}

const Entity* cubemapRoomBox(const MapDocument& doc, const Entity& cubemap) {
	const auto name = cubemap.get("parallaxobb");
	if (name.empty()) {
		return nullptr;
	}
	for (const auto& entity : doc.entities) {
		if (iequals(entity.classname(), "parallax_obb") && iequals(entity.get("targetname"), name)) {
			return &entity;
		}
	}
	return nullptr;
}

namespace {

/// Brushes that do not stop the room search: volumes and helpers, not walls.
bool passable(const Entity& owner, const Solid& solid) {
	const auto cls = toLower(owner.classname());
	if (cls.starts_with("trigger_") || cls == "parallax_obb" || cls.starts_with("func_areaportal") || cls == "func_occluder" ||
	    cls == "func_viscluster" || cls == "func_ladder" || cls == "func_clip_vphysics" || cls == "func_buyzone") {
		return true;
	}
	if (solid.sides.empty()) {
		return true;
	}
	for (const auto& side : solid.sides) {
		const auto material = toLower(side.material);
		const bool helper = material == "tools/toolstrigger" || material == "tools/toolsclip" || material == "tools/toolsplayerclip" ||
		                    material == "tools/toolsnpcclip" || material == "tools/toolsareaportal" || material == "tools/toolshint" ||
		                    material == "tools/toolsskip" || material == "tools/toolsoccluder" || material == "tools/toolsinvisible" ||
		                    material == "tools/toolsinvisibleladder";
		if (!helper) {
			return false;
		}
	}
	return true;
}

/// Distance along the ray to where it enters the convex brush (nullopt: misses, or starts inside).
std::optional<double> rayEnters(const Solid& solid, const Vec3& origin, const Vec3& direction) {
	double enter = 0, exit = std::numeric_limits<double>::max();
	bool inside = true;
	for (const auto& side : solid.sides) {
		const Plane plane = side.plane();
		const double startDistance = plane.distanceTo(origin);
		const double along = dot(plane.normal, direction);
		if (startDistance > 1e-6) inside = false;
		if (std::abs(along) < 1e-12) {
			if (startDistance > 0) return std::nullopt;   // parallel and outside
			continue;
		}
		const double t = -startDistance / along;
		if (along < 0) {
			enter = std::max(enter, t);
		} else {
			exit = std::min(exit, t);
		}
		if (enter > exit) return std::nullopt;
	}
	if (inside) return std::nullopt;
	return enter;
}

} // namespace

Bounds roomAround(const MapDocument& doc, const Vec3& point, double maxDistance) {
	double reach[6];
	std::fill(std::begin(reach), std::end(reach), maxDistance);
	const auto test = [&](const Entity& owner, const Solid& solid) {
		if (passable(owner, solid)) return;
		for (int d = 0; d < 6; ++d) {
			Vec3 direction{};
			direction[d / 2] = d % 2 == 0 ? 1.0 : -1.0;
			if (const auto t = rayEnters(solid, point, direction); t && *t < reach[d]) {
				reach[d] = *t;
			}
		}
	};
	for (const auto& solid : doc.world.solids) test(doc.world, solid);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) test(entity, solid);
	}
	Bounds box;
	box.add({point.x - reach[1], point.y - reach[3], point.z - reach[5]});
	box.add({point.x + reach[0], point.y + reach[2], point.z + reach[4]});
	return box;
}

namespace edit {

std::pair<Change, int> setCubemapRoomBox(MapDocument& doc, int cubemapId, const Bounds& box, const EntityClass* obbClass) {
	Entity* cubemap = doc.findEntity(cubemapId);
	if (!cubemap || cubemapId == 0 || box.empty() || box.size().x < 1 || box.size().y < 1 || box.size().z < 1) {
		return {Change{}, 0};
	}
	Transaction t(doc, "Cubemap room box");
	const Entity* existing = cubemapRoomBox(doc, *cubemap);
	int obbId = existing ? existing->id : 0;
	ShapeOptions block;   // a plain box: Mapbase reads the box from one brush
	auto solids = makeShape(doc, box, block, "TOOLS/TOOLSTRIGGER");
	if (solids.size() != 1) {
		return {Change{}, 0};
	}
	if (obbId != 0) {
		t.touchEntity(obbId);
		doc.findEntity(obbId)->solids = std::move(solids);
	} else {
		Entity obb;
		obb.id = doc.newEntityId();
		t.touchEntity(obb.id);
		obb.set("classname", "parallax_obb");
		if (obbClass) {
			for (const auto& property : obbClass->properties) {
				if (property.kind == EntityProperty::Kind::Plain && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
					obb.set(property.name, property.defaultValue);
				}
			}
		}
		// Named after the cubemap, unique in the map.
		std::string base = cubemap->get("targetname");
		base = (base.empty() ? "cubemap_" + std::to_string(cubemapId) : base) + "_box";
		std::string name = base;
		for (int n = 2; std::any_of(doc.entities.begin(), doc.entities.end(), [&](const Entity& e) { return iequals(e.get("targetname"), name); }); ++n) {
			name = base + std::to_string(n);
		}
		obb.set("targetname", name);
		obb.solids = std::move(solids);
		obbId = obb.id;
		t.touchEntity(cubemapId);
		doc.findEntity(cubemapId)->set("parallaxobb", name);
		doc.entities.push_back(std::move(obb));
	}
	return {t.finish(), obbId};
}

Change removeCubemapRoomBox(MapDocument& doc, int cubemapId) {
	Entity* cubemap = doc.findEntity(cubemapId);
	if (!cubemap) {
		return {};
	}
	Transaction t(doc, "Remove cubemap room box");
	if (const Entity* box = cubemapRoomBox(doc, *cubemap)) {
		const int id = box->id;
		t.touchEntity(id);
		std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
	}
	t.touchEntity(cubemapId);
	doc.findEntity(cubemapId)->erase("parallaxobb");
	return t.finish();
}

Change dropToFloor(MapDocument& doc, const Selection& selection, const EntityCatalog* catalog) {
	Transaction t(doc, "Drop to floor");
	// Everything that is not selected can be landed on.
	std::vector<const Solid*> solids;
	std::vector<Bounds> boxes;
	const auto collect = [&](const Entity& entity, int entityId) {
		for (const auto& solid : entity.solids) {
			if (!selection.contains(SolidRef{entityId, solid.id}) && !selection.containsEntity(entityId)) {
				solids.push_back(&solid);
			}
		}
	};
	collect(doc.world, 0);
	for (const auto& entity : doc.entities) {
		collect(entity, entity.id);
		if (entity.solids.empty() && !selection.containsEntity(entity.id)) {
			boxes.push_back(entityBounds(entity, catalog));
		}
	}
	// Five rays down from the bottom of the object (corners and middle): the shortest hit wins.
	const auto fall = [&](const Bounds& b) -> double {
		double best = 1e30;
		const Vec3 lo = b.min, hi = b.max;
		const double inset = 0.5;
		const Vec3 starts[5] = {{lo.x + inset, lo.y + inset, lo.z}, {hi.x - inset, lo.y + inset, lo.z}, {hi.x - inset, hi.y - inset, lo.z},
		                        {lo.x + inset, hi.y - inset, lo.z}, {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z}};
		for (const auto& start : starts) {
			const Ray ray{start + Vec3{0, 0, 0.01}, {0, 0, -1}};
			for (const auto* solid : solids) {
				if (const auto hit = intersect(ray, *solid); hit && hit->distance >= 0) {
					best = std::min(best, hit->distance - 0.01);
				}
			}
			for (const auto& box : boxes) {
				if (const double d = intersect(ray, box); d >= 0) {
					best = std::min(best, d - 0.01);
				}
			}
		}
		return best < 1e29 ? std::max(0.0, best) : 0.0;
	};
	for (const auto& ref : selection.solids) {
		auto* solid = doc.findSolid(ref);
		if (!solid) continue;
		const double d = fall(buildBrushGeometry(*solid).bounds);
		if (d > 0.01) {
			t.touchSolid(ref);
			translateSolid(*solid, {0, 0, -std::round(d * 100) / 100}, true);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = id == 0 ? nullptr : doc.findEntity(id);
		if (!entity) continue;
		const double d = fall(entityBounds(*entity, catalog));
		if (d <= 0.01) continue;
		const Vec3 delta{0, 0, -std::round(d * 100) / 100};
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			translateSolid(solid, delta, true);
		}
		if (entity->hasOrigin()) {
			entity->setOrigin(entity->origin() + delta);
		}
	}
	return t.finish();
}

WaterResult makeWater(MapDocument& doc, const Selection& selection, const std::string& waterMaterial, const EntityCatalog* catalog,
                      double cubemapRange) {
	WaterResult result;
	// The brushes: selected world brushes, brushes of selected brush entities and of selected faces.
	std::vector<SolidRef> refs = selection.solids;
	for (const int id : selection.entities) {
		if (const auto* entity = doc.findEntity(id)) {
			for (const auto& solid : entity->solids) refs.push_back({id, solid.id});
		}
	}
	for (const auto& face : selection.faces) refs.push_back(face.solid);
	std::sort(refs.begin(), refs.end(), [](const SolidRef& a, const SolidRef& b) {
		return std::pair(a.entityId, a.solidId) < std::pair(b.entityId, b.solidId);
	});
	refs.erase(std::unique(refs.begin(), refs.end()), refs.end());

	Transaction t(doc, "Make water");
	Bounds surface;
	std::string waterSides;
	for (const auto& ref : refs) {
		Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		// The face pointing up the most is the surface.
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* top = nullptr;
		for (const auto& face : geometry.faces) {
			if (face.normal.z > 0.7 && (!top || face.normal.z > top->normal.z)) top = &face;
		}
		if (!geometry.valid || !top) continue;
		t.touchSolid(ref);
		for (std::size_t i = 0; i < solid->sides.size(); ++i) {
			solid->sides[i].material = int(i) == top->sideIndex ? waterMaterial : "TOOLS/TOOLSNODRAW";
		}
		for (const auto& v : top->vertices) surface.add(v);
		waterSides += (waterSides.empty() ? "" : " ") + std::to_string(solid->sides[std::size_t(top->sideIndex)].id);
		++result.brushes;
	}
	if (result.brushes == 0) {
		result.change = t.finish();
		return result;
	}

	const auto newEntity = [&](const std::string& classname, const Vec3& origin) -> Entity& {
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
		entity.setOrigin(snap(origin, 1));
		auto editor = KvNode::makeBlock("editor");
		editor.set("color", "220 30 220");
		editor.set("visgroupshown", "1");
		editor.set("visgroupautoshown", "1");
		entity.extra.push_back(std::move(editor));
		doc.entities.push_back(std::move(entity));
		return doc.entities.back();
	};
	const Vec3 middle = surface.center();
	const bool hasLod = std::any_of(doc.entities.begin(), doc.entities.end(), [](const Entity& e) { return iequals(e.classname(), "water_lod_control"); });
	if (!hasLod) {
		auto& lod = newEntity("water_lod_control", middle + Vec3{0, 0, 32});
		if (lod.get("cheapwaterstartdistance").empty()) lod.set("cheapwaterstartdistance", "1000");
		if (lod.get("cheapwaterenddistance").empty()) lod.set("cheapwaterenddistance", "2000");
		result.lodControlId = lod.id;
	}
	const Vec3 probe = middle + Vec3{0, 0, 64};
	const bool cubemapNear = std::any_of(doc.entities.begin(), doc.entities.end(), [&](const Entity& e) {
		return iequals(e.classname(), "env_cubemap") && length(e.origin() - probe) <= cubemapRange;
	});
	if (!cubemapNear) {
		auto& cubemap = newEntity("env_cubemap", probe);
		cubemap.set("sides", waterSides);
		result.cubemapId = cubemap.id;
	}
	result.change = t.finish();
	return result;
}

Change starterRoom(MapDocument& doc, const StarterRoomMaterials& materials, const Vec3& size) {
	Transaction t(doc, "Starter room");
	const double wall = 16;
	const Vec3 half{size.x / 2, size.y / 2, 0};
	const Bounds inside{{-half.x, -half.y, 0}, {half.x, half.y, size.z}};
	const auto add = [&](const Bounds& box, const std::string& material) {
		Solid solid = makeBlock(doc, box, material);
		t.touchSolid({0, solid.id});
		doc.world.solids.push_back(std::move(solid));
	};
	const Vec3 lo = inside.min, hi = inside.max;
	add({{lo.x - wall, lo.y - wall, lo.z - wall}, {hi.x + wall, hi.y + wall, lo.z}}, materials.floor);
	add({{lo.x - wall, lo.y - wall, hi.z}, {hi.x + wall, hi.y + wall, hi.z + wall}}, materials.ceiling);
	add({{lo.x - wall, lo.y - wall, lo.z}, {lo.x, hi.y + wall, hi.z}}, materials.walls);
	add({{hi.x, lo.y - wall, lo.z}, {hi.x + wall, hi.y + wall, hi.z}}, materials.walls);
	add({{lo.x, lo.y - wall, lo.z}, {hi.x, lo.y, hi.z}}, materials.walls);
	add({{lo.x, hi.y, lo.z}, {hi.x, hi.y + wall, hi.z}}, materials.walls);
	const auto place = [&](const std::string& classname, const Vec3& origin, std::vector<std::pair<std::string, std::string>> keys) {
		Entity entity;
		entity.id = doc.newEntityId();
		t.touchEntity(entity.id);
		entity.set("classname", classname);
		entity.set("origin", formatVec3(origin));
		for (auto& [key, value] : keys) entity.set(key, value);
		doc.entities.push_back(std::move(entity));
	};
	place("info_player_start", {0, -size.y / 4, 1}, {{"angles", "0 90 0"}});
	place("light", {0, 0, size.z - 32}, {{"_light", "255 240 220 400"}});
	place("env_cubemap", {0, 0, 64}, {});
	return t.finish();
}

std::string pickSoundscape(const std::vector<std::string>& names, SoundscapeArea area) {
	// Words in a soundscape's name that tell where it fits, with their weight.
	static const std::vector<std::pair<const char*, int>> INDOOR = {
		{"indoor", 5}, {"inside", 5}, {"interior", 5}, {"room", 3}, {"house", 3}, {"building", 3}, {"hall", 2}, {"office", 2}, {"apartment", 2}};
	static const std::vector<std::pair<const char*, int>> TUNNEL = {
		{"tunnel", 5}, {"sewer", 3}, {"underground", 3}, {"pipe", 2}, {"drip", 2}, {"cave", 2}};
	static const std::vector<std::pair<const char*, int>> OUTDOOR = {
		{"outdoor", 5}, {"outside", 5}, {"city", 3}, {"street", 3}, {"town", 2}, {"plaza", 2}, {"courtyard", 2}, {"wind", 2}, {"yard", 1}};
	const auto& words = area == SoundscapeArea::Indoor ? INDOOR : (area == SoundscapeArea::Tunnel ? TUNNEL : OUTDOOR);
	std::vector<std::string> sorted = names;
	std::sort(sorted.begin(), sorted.end());
	std::string best;
	int bestScore = 0;
	for (const auto& name : sorted) {
		const std::string lower = toLower(name);
		int score = 0;
		for (const auto& [word, weight] : words) {
			if (lower.find(word) != std::string::npos) score += weight;
		}
		if (score > bestScore) {
			best = name;
			bestScore = score;
		}
	}
	return best;
}

Change tutorialMap(MapDocument& doc, const TutorialMapOptions& options) {
	Transaction t(doc, "Tutorial map");
	const auto& m = options.materials;
	const TutorialMapLayout layout;
	const double w = 16;   // wall thickness
	const auto add = [&](const Bounds& box, const std::string& material, int lightmapScale = 16) {
		Solid solid = makeBlock(doc, box, material);
		for (auto& side : solid.sides) side.lightmapScale = lightmapScale;
		t.touchSolid({0, solid.id});
		doc.world.solids.push_back(std::move(solid));
	};
	const auto place = [&](const std::string& classname, const Vec3& origin, std::vector<std::pair<std::string, std::string>> keys = {}) {
		Entity entity;
		entity.id = doc.newEntityId();
		t.touchEntity(entity.id);
		entity.set("classname", classname);
		entity.set("origin", formatVec3(origin));
		for (auto& [key, value] : keys) entity.set(key, value);
		doc.entities.push_back(std::move(entity));
	};
	const std::string sky = "TOOLS/TOOLSSKYBOX";

	// Inside: a hall with a doorway in the north wall to the tunnel.
	const Vec3 in0 = layout.indoor.min, in1 = layout.indoor.max;
	const Bounds& tunnel = layout.tunnel;
	const double doorTop = tunnel.max.z, doorX = tunnel.max.x;
	add({{in0.x - w, in0.y - w, in0.z - w}, {in1.x + w, in1.y + w, in0.z}}, m.floor);
	add({{in0.x - w, in0.y - w, in1.z}, {in1.x + w, in1.y + w, in1.z + w}}, m.ceiling);
	add({{in0.x - w, in0.y - w, in0.z}, {in0.x, in1.y + w, in1.z}}, m.walls);
	add({{in1.x, in0.y - w, in0.z}, {in1.x + w, in1.y + w, in1.z}}, m.walls);
	add({{in0.x, in0.y - w, in0.z}, {in1.x, in0.y, in1.z}}, m.walls);
	add({{in0.x, in1.y, in0.z}, {-doorX, in1.y + w, in1.z}}, m.walls);
	add({{doorX, in1.y, in0.z}, {in1.x, in1.y + w, in1.z}}, m.walls);
	add({{-doorX, in1.y, doorTop}, {doorX, in1.y + w, in1.z}}, m.walls);

	// The tunnel between the doorways.
	add({{-doorX - w, tunnel.min.y, tunnel.min.z - w}, {doorX + w, tunnel.max.y, tunnel.min.z}}, m.tunnel);
	add({{-doorX - w, tunnel.min.y, tunnel.max.z}, {doorX + w, tunnel.max.y, tunnel.max.z + w}}, m.ceiling);
	add({{-doorX - w, tunnel.min.y, tunnel.min.z}, {-doorX, tunnel.max.y, tunnel.max.z}}, m.tunnel);
	add({{doorX, tunnel.min.y, tunnel.min.z}, {doorX + w, tunnel.max.y, tunnel.max.z}}, m.tunnel);

	// Outside: ground with a path, walls up to 384, sky above them and on top.
	const Vec3 out0 = layout.outdoor.min, out1 = layout.outdoor.max;
	const double wallTop = 384, pathEnd = 2400;
	const double y0 = out0.y - w;   // the south wall (with the doorway) starts where the tunnel ends
	add({{out0.x - w, y0, out0.z - w}, {-doorX, out1.y + w, out0.z}}, m.ground, 32);
	add({{doorX, y0, out0.z - w}, {out1.x + w, out1.y + w, out0.z}}, m.ground, 32);
	add({{-doorX, y0, out0.z - w}, {doorX, pathEnd, out0.z}}, m.path, 32);
	add({{-doorX, pathEnd, out0.z - w}, {doorX, out1.y + w, out0.z}}, m.ground, 32);
	add({{out0.x - w, y0, out1.z}, {out1.x + w, out1.y + w, out1.z + w}}, sky, 32);
	const auto wall = [&](const Bounds& box) {   // brick below wallTop, sky above
		add({box.min, {box.max.x, box.max.y, wallTop}}, m.outerWalls, 32);
		add({{box.min.x, box.min.y, wallTop}, box.max}, sky, 32);
	};
	wall({{out0.x - w, y0, out0.z}, {out0.x, out1.y + w, out1.z}});
	wall({{out1.x, y0, out0.z}, {out1.x + w, out1.y + w, out1.z}});
	wall({{out0.x, out1.y, out0.z}, {out1.x, out1.y + w, out1.z}});
	add({{out0.x, y0, out0.z}, {-doorX, out0.y, wallTop}}, m.outerWalls, 32);
	add({{doorX, y0, out0.z}, {out1.x, out0.y, wallTop}}, m.outerWalls, 32);
	add({{-doorX, y0, doorTop}, {doorX, out0.y, wallTop}}, m.outerWalls, 32);
	add({{out0.x, y0, wallTop}, {out1.x, out0.y, out1.z}}, sky, 32);

	// Ground variation: a platform with steps and a ramp up to it.
	const Bounds platform{{256, 2304, 0}, {768, 2816, 64}};
	add(platform, m.platform);
	for (int step = 1; step <= 3; ++step) {
		add({{384, platform.min.y - 32 * (4 - step), 0}, {640, platform.min.y - 32 * (3 - step), 16.0 * step}}, m.platform);
	}
	ShapeOptions ramp;
	ramp.kind = ShapeOptions::Kind::Wedge;
	for (auto& solid : makeShape(doc, {{272, 1984, 0}, {368, platform.min.y, platform.max.z}}, ramp, m.platform)) {
		t.touchSolid({0, solid.id});
		doc.world.solids.push_back(std::move(solid));
	}

	// The sky and its light.
	t.touchWorld();
	const Vec3 sunAt{0, (out0.y + out1.y) / 2, 512};
	place("light_environment", sunAt);
	place("env_sun", sunAt + Vec3{64, 0, 0}, {{"use_angles", "1"}, {"size", "16"}, {"overlaysize", "-1"}});
	place("shadow_control", sunAt + Vec3{128, 0, 0}, {{"color", "64 64 64"}, {"distance", "75"}});
	if (options.globalLight) {
		place("env_global_light", sunAt + Vec3{192, 0, 0},
		      {{"color", "255 250 230 255"}, {"distance", "10000"}, {"fov", "5"}, {"nearz", "512"}, {"northoffset", "200"},
		       {"orthosize", "1000"}, {"texturename", "effects/flashlight001"}, {"enableshadows", "1"}, {"enabled", "1"}});
	}
	applySky(doc, t, options.skyName, sunAt);
	if (options.skybox3d) {
		// 1/16 of the world around the sky camera: ground at the world's height 0 and hills
		// around the outside area, in a sealed room of sky brushes below the map.
		const Vec3 c{0, 0, -2048};
		const Bounds room{c + Vec3{-384, -256, -64}, c + Vec3{384, 512, 448}};
		add({{room.min.x - w, room.min.y - w, room.min.z - w}, {room.max.x + w, room.max.y + w, c.z}}, m.ground, 64);
		add({{room.min.x - w, room.min.y - w, room.max.z}, {room.max.x + w, room.max.y + w, room.max.z + w}}, sky, 64);
		add({{room.min.x - w, room.min.y - w, c.z}, {room.min.x, room.max.y + w, room.max.z}}, sky, 64);
		add({{room.max.x, room.min.y - w, c.z}, {room.max.x + w, room.max.y + w, room.max.z}}, sky, 64);
		add({{room.min.x, room.min.y - w, c.z}, {room.max.x, room.min.y, room.max.z}}, sky, 64);
		add({{room.min.x, room.max.y, c.z}, {room.max.x, room.max.y + w, room.max.z}}, sky, 64);
		add({c + Vec3{-384, 256, 0}, c + Vec3{384, 512, 96}}, m.rock, 64);
		add({c + Vec3{128, -64, 0}, c + Vec3{384, 256, 64}}, m.rock, 64);
		add({c + Vec3{-384, -64, 0}, c + Vec3{-128, 256, 80}}, m.rock, 64);
		place("sky_camera", c + Vec3{0, 0, 16}, {{"scale", "16"}, {"fogenable", "0"}});
	}

	// Lights inside and in the tunnel, a soundscape and a cubemap in each area.
	place("light", {-256, 0, in1.z - 32}, {{"_light", "255 240 220 250"}});
	place("light", {256, 0, in1.z - 32}, {{"_light", "255 240 220 250"}});
	const Vec3 tunnelMiddle{0, (tunnel.min.y + tunnel.max.y) / 2, 0};
	place("light", tunnelMiddle + Vec3{0, 0, tunnel.max.z - 24}, {{"_light", "255 220 170 80"}});
	const auto soundscape = [&](const Vec3& at, const std::string& name, int radius) {
		if (!name.empty()) place("env_soundscape", at, {{"soundscape", name}, {"radius", std::to_string(radius)}});
	};
	soundscape({0, 0, 128}, options.indoorSoundscape, 768);
	soundscape(tunnelMiddle + Vec3{0, 0, 80}, options.tunnelSoundscape, 320);
	soundscape({0, 2048, 160}, options.outdoorSoundscape, 1600);
	place("env_cubemap", {0, -128, 64});
	place("env_cubemap", tunnelMiddle + Vec3{0, 0, 64});
	place("env_cubemap", {0, 2048, 96});

	// The player: HEV suit and weapons on the start spot (taken on the first frame), ammo crates.
	const Vec3 start{0, -384, 1};
	place("info_player_start", start, {{"angles", "0 90 0"}});
	place("item_suit", start + Vec3{0, 0, 16}, {{"spawnflags", "1"}});   // short logon
	int height = 16;
	for (const char* weapon : {"weapon_crowbar", "weapon_pistol", "weapon_smg1", "weapon_shotgun"}) {
		height += 8;
		place(weapon, start + Vec3{0, 0, static_cast<double>(height)});
	}
	place("item_ammo_crate", {in1.x - 48, -320, 0}, {{"angles", "0 180 0"}, {"AmmoType", "0"}});   // pistol
	place("item_ammo_crate", {in1.x - 48, -192, 0}, {{"angles", "0 180 0"}, {"AmmoType", "1"}});   // SMG
	place("item_ammo_crate", {in1.x - 48, -64, 0}, {{"angles", "0 180 0"}, {"AmmoType", "4"}});    // shotgun

	// A friendly citizen inside (fixed model, no weapon) and a target outside that never fights.
	place("npc_citizen", {-320, 192, 1},
	      {{"targetname", "tutorial_citizen"}, {"angles", "0 315 0"}, {"citizentype", "4"}, {"model", "models/Humans/Group03/male_07.mdl"},
	       {"additionalequipment", "0"}});
	place("npc_combine_s", {-384, 2560, 1},
	      {{"targetname", "tutorial_target"}, {"angles", "0 270 0"}, {"additionalequipment", "0"}, {"NumGrenades", "0"}, {"spawnflags", "8192"}});
	place("ai_relationship", {-416, 2560, 96},
	      {{"targetname", "tutorial_target_peace"}, {"subject", "tutorial_target"}, {"target", "!player"}, {"disposition", "4"}, {"rank", "99"},
	       {"StartActive", "1"}, {"Reciprocal", "0"}});
	place("ai_relationship", {-352, 2560, 96},
	      {{"targetname", "tutorial_target_citizen"}, {"subject", "tutorial_target"}, {"target", "tutorial_citizen"}, {"disposition", "4"},
	       {"rank", "99"}, {"StartActive", "1"}, {"Reciprocal", "1"}});

	// Nodes, so the NPCs can find their way (inside, through the tunnel, outside).
	const Vec3 nodes[] = {{-256, -256, 8}, {256, -256, 8}, {-256, 256, 8}, {256, 256, 8}, {0, 0, 8}, {0, 640, 8}, {0, 912, 8},
	                      {0, 1408, 8}, {-640, 1408, 8}, {640, 1408, 8}, {0, 2048, 8}, {-640, 2048, 8}, {0, 2688, 8}, {-640, 2688, 8}};
	for (const Vec3& at : nodes) place("info_node", at);

	// Props to push, shoot and climb.
	place("prop_physics", {-640, 1600, 28}, {{"model", "models/props_c17/oildrum001.mdl"}});
	place("prop_physics", {-576, 1648, 28}, {{"model", "models/props_c17/oildrum001.mdl"}});
	place("prop_physics", {640, 1536, 24}, {{"model", "models/props_junk/wood_crate001a.mdl"}});
	place("prop_physics", {640, 1600, 64}, {{"model", "models/props_junk/wood_crate001a.mdl"}});
	place("prop_physics", {-384, 2432, 24}, {{"model", "models/props_junk/wood_crate001a.mdl"}});
	return t.finish();
}

} // namespace edit

} // namespace sme
