// map_tools.cpp - Cordons, track chains, drop to floor (see map_tools.h).

#include <sme/map_tools.h>

#include <sme/brush.h>
#include <sme/entity_catalog.h>
#include <sme/shapes.h>
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

} // namespace edit

} // namespace sme
