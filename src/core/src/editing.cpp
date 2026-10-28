// editing.cpp - Undoable editor operations (see editing.h).

#include <sme/editing.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sme {

bool Selection::contains(const SolidRef& ref) const {
	return std::find(solids.begin(), solids.end(), ref) != solids.end();
}

bool Selection::containsEntity(int entityId) const {
	return std::find(entities.begin(), entities.end(), entityId) != entities.end();
}

bool Selection::contains(const FaceRef& ref) const {
	return std::find(faces.begin(), faces.end(), ref) != faces.end();
}

void Selection::toggle(const SolidRef& ref) {
	if (const auto it = std::find(solids.begin(), solids.end(), ref); it != solids.end()) {
		solids.erase(it);
	} else {
		solids.push_back(ref);
	}
}

void Selection::toggleEntity(int entityId) {
	if (const auto it = std::find(entities.begin(), entities.end(), entityId); it != entities.end()) {
		entities.erase(it);
	} else {
		entities.push_back(entityId);
	}
}

void Selection::toggle(const FaceRef& ref) {
	if (const auto it = std::find(faces.begin(), faces.end(), ref); it != faces.end()) {
		faces.erase(it);
	} else {
		faces.push_back(ref);
	}
}

void Selection::prune(const MapDocument& doc) {
	std::erase_if(solids, [&](const SolidRef& ref) { return !doc.findSolid(ref); });
	std::erase_if(entities, [&](int id) { return id == 0 || !doc.findEntity(id); });
	std::erase_if(faces, [&](const FaceRef& face) {
		const auto* solid = doc.findSolid(face.solid);
		return !solid || std::none_of(solid->sides.begin(), solid->sides.end(), [&](const Side& s) { return s.id == face.sideId; });
	});
}

namespace edit {

namespace {

constexpr double PI = 3.14159265358979323846;

bool isPointEntity(const Entity& entity) {
	return entity.solids.empty();
}

Vec3 parseAnglesKey(const Entity& entity) {
	Vec3 angles;
	if (!parseVec3(entity.get("angles"), angles)) {
		return {};
	}
	return angles;
}

std::vector<double> parseNumbers(std::string_view text) {
	std::vector<double> out;
	for (const auto& part : splitTrimmed(text, ' ')) {
		out.push_back(std::atof(part.c_str()));
	}
	return out;
}

Entity* entityOrNull(MapDocument& doc, int id) {
	return id == 0 ? nullptr : doc.findEntity(id);
}

} // namespace

Bounds entityBounds(const Entity& entity, const EntityCatalog* catalog) {
	Bounds box;
	if (!entity.solids.empty()) {
		for (const auto& solid : entity.solids) {
			box.add(buildBrushGeometry(solid).bounds);
		}
		return box;
	}
	const Vec3 origin = entity.origin();
	if (catalog) {
		// A loaded model: its box, turned like the entity.
		if (const auto model = entityModel(entity, catalog); !model.empty()) {
			if (const auto* local = catalog->modelBounds(model); local && !local->empty()) {
				const Mat3 rotation = anglesToMatrix(parseAnglesKey(entity));
				for (int i = 0; i < 8; ++i) {
					const Vec3 corner{(i & 1) ? local->max.x : local->min.x, (i & 2) ? local->max.y : local->min.y, (i & 4) ? local->max.z : local->min.z};
					box.add(origin + rotation * corner);
				}
				return box;
			}
		}
	}
	Vec3 mins{-8, -8, -8}, maxs{8, 8, 8};
	if (catalog) {
		if (const auto* definition = catalog->find(entity.classname())) {
			const auto size = definition->helper("size");
			if (const auto comma = size.find(','); comma != std::string::npos) {
				const auto a = parseNumbers(std::string_view(size).substr(0, comma));
				const auto b = parseNumbers(std::string_view(size).substr(comma + 1));
				if (a.size() == 3 && b.size() == 3) {
					mins = {a[0], a[1], a[2]};
					maxs = {b[0], b[1], b[2]};
				}
			} else if (const auto n = parseNumbers(size); n.size() == 3) {
				mins = {-n[0] / 2, -n[1] / 2, -n[2] / 2};
				maxs = {n[0] / 2, n[1] / 2, n[2] / 2};
			}
		}
	}
	box.add(origin + mins);
	box.add(origin + maxs);
	return box;
}

std::string classSprite(const EntityClass& cls) {
	auto sprite = cls.helper("iconsprite");
	if (sprite.size() >= 2 && sprite.front() == '"' && sprite.back() == '"') {
		sprite = sprite.substr(1, sprite.size() - 2);
	}
	return sprite.empty() ? std::string() : normalizeMaterialName(sprite);
}

std::string entitySprite(const Entity& entity, const EntityCatalog* catalog) {
	const auto* definition = catalog ? catalog->find(entity.classname()) : nullptr;
	if (!definition || !entity.solids.empty()) {
		return {};
	}
	if (auto sprite = classSprite(*definition); !sprite.empty()) {
		return sprite;
	}
	const bool usesOwnModel = std::any_of(definition->helpers.begin(), definition->helpers.end(), [](const EntityHelper& helper) {
		return helper.name == "sprite" || (helper.name == "iconsprite" && helper.arguments.empty());
	});
	const auto model = entity.get("model");
	if (usesOwnModel && (endsWithI(model, ".vmt") || endsWithI(model, ".spr"))) {
		return normalizeMaterialName(model);
	}
	return {};
}

std::string entityModel(const Entity& entity, const EntityCatalog* catalog) {
	if (!entity.solids.empty()) {
		return {};
	}
	auto model = normalizePath(entity.get("model"));
	if (endsWithI(model, ".mdl")) {
		return model;
	}
	if (catalog) {
		if (const auto* definition = catalog->find(entity.classname())) {
			for (const char* helper : {"studio", "studioprop"}) {
				auto argument = definition->helper(helper);
				if (argument.size() >= 2 && argument.front() == '"' && argument.back() == '"') {
					argument = argument.substr(1, argument.size() - 2);
				}
				if (endsWithI(argument, ".mdl")) {
					return normalizePath(argument);
				}
			}
		}
	}
	return {};
}

Bounds selectionBounds(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog) {
	Bounds box;
	for (const auto& ref : selection.solids) {
		if (const auto* solid = doc.findSolid(ref)) {
			box.add(buildBrushGeometry(*solid).bounds);
		}
	}
	for (const int id : selection.entities) {
		if (const auto* entity = doc.findEntity(id); entity && id != 0) {
			box.add(entityBounds(*entity, catalog));
		}
	}
	for (const auto& face : selection.faces) {
		if (const auto* solid = doc.findSolid(face.solid)) {
			for (const auto& f : buildBrushGeometry(*solid).faces) {
				if (solid->sides[static_cast<std::size_t>(f.sideIndex)].id == face.sideId) {
					for (const auto& v : f.vertices) {
						box.add(v);
					}
				}
			}
		}
	}
	return box;
}

Mat3 anglesToMatrix(const Vec3& a) {
	// Source: forward = yaw around Z, then pitch (positive looks down) around Y, then roll around X.
	return Mat3::rotation(2, a.y) * Mat3::rotation(1, a.x) * Mat3::rotation(0, a.z);
}

Vec3 matrixToAngles(const Mat3& m) {
	// Inverse of anglesToMatrix (R = Rz(yaw) * Ry(pitch) * Rx(roll)).
	const double sp = -m.m[2][0];
	double pitch, yaw, roll;
	if (std::abs(sp) > 0.999999) {
		pitch = sp > 0 ? 90 : -90;
		yaw = std::atan2(-m.m[0][1], m.m[1][1]) * 180 / PI;
		roll = 0;
	} else {
		pitch = std::asin(sp) * 180 / PI;
		yaw = std::atan2(m.m[1][0], m.m[0][0]) * 180 / PI;
		roll = std::atan2(m.m[2][1], m.m[2][2]) * 180 / PI;
	}
	const auto clean = [](double v) {
		const double r = std::round(v * 1000) / 1000;
		return r == 0 ? 0.0 : r;
	};
	return {clean(pitch), clean(yaw), clean(roll)};
}

Change translate(MapDocument& doc, const Selection& selection, const Vec3& delta, bool textureLock) {
	Transaction t(doc, "Move");
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* solid = doc.findSolid(ref)) {
			translateSolid(*solid, delta, textureLock);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			translateSolid(solid, delta, textureLock);
		}
		if (entity->hasOrigin()) {
			entity->setOrigin(entity->origin() + delta);
		}
	}
	return t.finish();
}

Change rotate(MapDocument& doc, const Selection& selection, const Mat3& rotation, const Vec3& center, bool textureLock) {
	Transaction t(doc, "Rotate");
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* solid = doc.findSolid(ref)) {
			rotateSolid(*solid, rotation, center, textureLock);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			rotateSolid(solid, rotation, center, textureLock);
		}
		if (entity->hasOrigin()) {
			entity->setOrigin(rotation * (entity->origin() - center) + center);
		}
		if (isPointEntity(*entity)) {
			entity->set("angles", formatVec3(matrixToAngles(rotation * anglesToMatrix(parseAnglesKey(*entity)))));
		}
	}
	return t.finish();
}

Change scale(MapDocument& doc, const Selection& selection, const Bounds& from, const Bounds& to) {
	Transaction t(doc, "Resize");
	const auto mapPoint = [&](const Vec3& p) {
		Vec3 out;
		for (int i = 0; i < 3; ++i) {
			const double size = from.max[i] - from.min[i];
			out[i] = size > 1e-6 ? to.min[i] + (p[i] - from.min[i]) * (to.max[i] - to.min[i]) / size : p[i] + (to.min[i] - from.min[i]);
		}
		return out;
	};
	const auto scaleOne = [&](Solid& solid) {
		Solid copy = solid;
		if (scaleSolid(copy, from, to) && isConvexValid(copy)) {
			solid = std::move(copy);
		}
	};
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* solid = doc.findSolid(ref)) {
			scaleOne(*solid);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			scaleOne(solid);
		}
		if (entity->hasOrigin()) {
			entity->setOrigin(mapPoint(entity->origin()));
		}
	}
	return t.finish();
}

Change remove(MapDocument& doc, const Selection& selection) {
	Transaction t(doc, "Delete");
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* entity = doc.findEntity(ref.entityId)) {
			std::erase_if(entity->solids, [&](const Solid& s) { return s.id == ref.solidId; });
		}
	}
	for (const int id : selection.entities) {
		if (id == 0) {
			continue;
		}
		t.touchEntity(id);
		std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
	}
	return t.finish();
}

namespace {

void mirrorSolid(Solid& solid, int axis, const Vec3& center, bool textureLock) {
	const auto reflect = [&](Vec3 p) {
		p[axis] = 2 * center[axis] - p[axis];
		return p;
	};
	for (auto& side : solid.sides) {
		for (auto& p : side.planePoints) {
			p = reflect(p);
		}
		std::swap(side.planePoints[0], side.planePoints[2]);   // a mirror turns the winding round: keep normals outwards
		if (textureLock) {
			for (TextureAxis* t : {&side.u, &side.v}) {
				Vec3 mirrored = t->axis;
				mirrored[axis] = -mirrored[axis];
				if (t->scale != 0) {
					t->shift += (dot(center, t->axis) - dot(center, mirrored)) / t->scale;
				}
				t->axis = mirrored;
			}
		}
	}
}

/// The material most faces of a brush have (lower case).
std::string mainMaterial(const Solid& solid) {
	std::map<std::string, int> counts;
	for (const auto& side : solid.sides) {
		++counts[toLower(side.material)];
	}
	std::string best;
	int most = 0;
	for (const auto& [material, count] : counts) {
		if (count > most) {
			most = count;
			best = material;
		}
	}
	return best;
}

} // namespace

Change mirror(MapDocument& doc, const Selection& selection, int axis, const Vec3& center, bool textureLock) {
	Transaction t(doc, "Flip");
	Mat3 m;
	m.m[axis][axis] = -1;
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* solid = doc.findSolid(ref)) {
			mirrorSolid(*solid, axis, center, textureLock);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			mirrorSolid(solid, axis, center, textureLock);
		}
		if (entity->hasOrigin()) {
			Vec3 origin = entity->origin();
			origin[axis] = 2 * center[axis] - origin[axis];
			entity->setOrigin(origin);
		}
		if (isPointEntity(*entity) && !entity->get("angles").empty()) {
			// Forward and up are mirrored; the side axis turns round so it stays a rotation.
			Mat3 side;
			side.m[1][1] = -1;
			entity->set("angles", formatVec3(matrixToAngles(m * anglesToMatrix(parseAnglesKey(*entity)) * side)));
		}
	}
	return t.finish();
}

Selection similarObjects(const MapDocument& doc, const Selection& selection) {
	std::set<std::string> classes, models, materials;
	for (const int id : selection.entities) {
		if (const auto* entity = doc.findEntity(id)) {
			classes.insert(toLower(entity->classname()));
			if (const auto model = entity->get("model"); !model.empty() && entity->solids.empty()) {
				models.insert(toLower(model));
			}
		}
	}
	for (const auto& ref : selection.solids) {
		if (const auto* solid = doc.findSolid(ref)) {
			materials.insert(mainMaterial(*solid));
		}
	}
	for (const auto& face : selection.faces) {
		if (const auto* solid = doc.findSolid(face.solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == face.sideId) materials.insert(toLower(side.material));
			}
		}
	}
	Selection out;
	for (const auto& solid : doc.world.solids) {
		if (!materials.empty() && materials.contains(mainMaterial(solid))) out.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		if (!classes.contains(toLower(entity.classname()))) continue;
		const auto model = toLower(entity.get("model"));
		// Props: only the same model; brush entities and others: the whole class.
		if (!models.empty() && entity.solids.empty() && !model.empty() && !models.contains(model)) continue;
		out.entities.push_back(entity.id);
	}
	return out;
}

Selection objectWithId(const MapDocument& doc, int id) {
	Selection out;
	for (const auto& solid : doc.world.solids) {
		if (solid.id == id) out.solids.push_back({0, id});
	}
	for (const auto& entity : doc.entities) {
		if (entity.id == id) {
			out.entities.push_back(id);
			continue;
		}
		for (const auto& solid : entity.solids) {
			if (solid.id == id) out.entities.push_back(entity.id);   // a brush of an entity: the entity
		}
	}
	return out;
}

std::pair<Change, Selection> duplicate(MapDocument& doc, const Selection& selection, const Vec3& offset) {
	Transaction t(doc, "Duplicate");
	Selection copies;
	std::vector<Solid> newSolids;
	for (const auto& ref : selection.solids) {
		if (const auto* solid = doc.findSolid(ref)) {
			auto copy = cloneSolid(doc, *solid);
			translateSolid(copy, offset, true);
			const SolidRef newRef{ref.entityId, copy.id};
			t.touchSolid(newRef);
			copies.solids.push_back(newRef);
			if (auto* entity = doc.findEntity(ref.entityId)) {
				entity->solids.push_back(std::move(copy));
			}
		}
	}
	std::vector<Entity> newEntities;
	for (const int id : selection.entities) {
		const auto* entity = entityOrNull(doc, id);
		if (!entity) {
			continue;
		}
		Entity copy = *entity;
		copy.id = doc.newEntityId();
		for (auto& solid : copy.solids) {
			solid = cloneSolid(doc, solid);
			translateSolid(solid, offset, true);
		}
		if (copy.hasOrigin()) {
			copy.setOrigin(copy.origin() + offset);
		}
		t.touchEntity(copy.id);
		copies.entities.push_back(copy.id);
		newEntities.push_back(std::move(copy));
	}
	for (auto& entity : newEntities) {
		doc.entities.push_back(std::move(entity));
	}
	return {t.finish(), copies};
}

std::pair<Change, SolidRef> createBlock(MapDocument& doc, const Bounds& box, const std::string& material) {
	Transaction t(doc, "Create block");
	auto solid = makeBlock(doc, box, material);
	const SolidRef ref{0, solid.id};
	t.touchSolid(ref);
	doc.world.solids.push_back(std::move(solid));
	return {t.finish(), ref};
}

std::pair<Change, int> placeEntity(MapDocument& doc, const std::string& classname, const Vec3& origin, const EntityClass* definition,
                                   const std::vector<std::pair<std::string, std::string>>& keys) {
	Transaction t(doc, "Place " + classname);
	Entity entity;
	entity.id = doc.newEntityId();
	t.touchEntity(entity.id);
	entity.set("classname", classname);
	if (definition) {
		for (const auto& property : definition->properties) {
			if (property.kind == EntityProperty::Kind::Flags) {
				std::uint64_t flags = 0;
				for (const auto& flag : property.flags) {
					flags |= flag.enabledByDefault ? flag.value : 0;
				}
				entity.set(property.name, std::to_string(flags));
			} else if (!property.defaultValue.empty() && !iequals(property.name, "origin")) {
				entity.set(property.name, property.defaultValue);
			}
		}
	}
	for (const auto& [key, value] : keys) {
		entity.set(key, value);
	}
	entity.setOrigin(origin);
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "220 30 220");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	entity.extra.push_back(std::move(editor));
	const int id = entity.id;
	doc.entities.push_back(std::move(entity));
	return {t.finish(), id};
}

Change applyMaterial(MapDocument& doc, const Selection& selection, const std::string& material) {
	Transaction t(doc, "Apply material");
	if (!selection.faces.empty()) {
		for (const auto& face : selection.faces) {
			t.touchSolid(face.solid);
			if (auto* solid = doc.findSolid(face.solid)) {
				for (auto& side : solid->sides) {
					if (side.id == face.sideId) {
						side.material = material;
					}
				}
			}
		}
		return t.finish();
	}
	for (const auto& ref : selection.solids) {
		t.touchSolid(ref);
		if (auto* solid = doc.findSolid(ref)) {
			for (auto& side : solid->sides) {
				side.material = material;
			}
		}
	}
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity || entity->solids.empty()) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			for (auto& side : solid.sides) {
				side.material = material;
			}
		}
	}
	return t.finish();
}

std::vector<FaceRef> facesWithMaterial(const MapDocument& doc, const std::string& material) {
	std::vector<FaceRef> out;
	const auto scan = [&](const Entity& entity, int entityId) {
		for (const auto& solid : entity.solids) {
			for (const auto& side : solid.sides) {
				if (iequals(side.material, material)) {
					out.push_back({{entityId, solid.id}, side.id});
				}
			}
		}
	};
	scan(doc.world, 0);
	for (const auto& entity : doc.entities) {
		scan(entity, entity.id);
	}
	return out;
}

std::vector<FaceRef> coplanarConnectedFaces(const MapDocument& doc, const FaceRef& seed) {
	struct Candidate {
		FaceRef ref;
		Bounds bounds;
	};
	const auto* seedSolid = doc.findSolid(seed.solid);
	if (!seedSolid) {
		return {};
	}
	Plane plane;
	bool found = false;
	for (const auto& side : seedSolid->sides) {
		if (side.id == seed.sideId) {
			plane = side.plane();
			found = true;
		}
	}
	if (!found) {
		return {};
	}
	// Every face in the same plane (same normal and distance), with its polygon's bounds.
	std::vector<Candidate> candidates;
	const auto scan = [&](const Entity& entity, int entityId) {
		for (const auto& solid : entity.solids) {
			const auto geometry = buildBrushGeometry(solid);
			for (const auto& face : geometry.faces) {
				const auto& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
				const auto p = side.plane();
				if (dot(p.normal, plane.normal) < 0.999 || std::abs(p.dist - plane.dist) > 0.05) {
					continue;
				}
				Candidate c;
				c.ref = {{entityId, solid.id}, side.id};
				for (const auto& v : face.vertices) {
					c.bounds.add(v);
				}
				candidates.push_back(c);
			}
		}
	};
	scan(doc.world, 0);
	for (const auto& entity : doc.entities) {
		scan(entity, entity.id);
	}
	// Grow from the seed through faces whose bounds touch.
	const auto touches = [](const Bounds& a, const Bounds& b) {
		constexpr double EPS = 0.1;
		for (int i = 0; i < 3; ++i) {
			if (a.min[i] > b.max[i] + EPS || b.min[i] > a.max[i] + EPS) {
				return false;
			}
		}
		return true;
	};
	std::vector<bool> taken(candidates.size(), false);
	std::vector<std::size_t> queue;
	for (std::size_t i = 0; i < candidates.size(); ++i) {
		if (candidates[i].ref == seed) {
			taken[i] = true;
			queue.push_back(i);
		}
	}
	std::vector<FaceRef> out;
	while (!queue.empty()) {
		const auto current = queue.back();
		queue.pop_back();
		out.push_back(candidates[current].ref);
		for (std::size_t i = 0; i < candidates.size(); ++i) {
			if (!taken[i] && touches(candidates[current].bounds, candidates[i].bounds)) {
				taken[i] = true;
				queue.push_back(i);
			}
		}
	}
	return out;
}

namespace {

/// Brush entity with the FGD's default key values (like Hammer's "Tie to Entity").
Entity newBrushEntity(MapDocument& doc, const std::string& classname, const EntityClass* definition) {
	Entity entity;
	entity.id = doc.newEntityId();
	entity.set("classname", classname);
	if (definition) {
		for (const auto& property : definition->properties) {
			if (property.kind == EntityProperty::Kind::Plain && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
				entity.set(property.name, property.defaultValue);
			}
		}
	}
	return entity;
}

} // namespace

std::string brushEntityMaterial(std::string_view classname) {
	const auto name = toLower(classname);
	if (name.starts_with("trigger_") || name == "func_buyzone" || name == "func_bomb_target" || name == "func_hostage_rescue") {
		return "tools/toolstrigger";
	}
	if (name.starts_with("func_areaportal")) {
		return "tools/toolsareaportal";
	}
	if (name == "func_occluder") {
		return "tools/toolsoccluder";
	}
	if (name == "func_viscluster") {
		return "tools/toolsskip";
	}
	if (name == "func_ladder") {
		return "tools/toolsinvisibleladder";
	}
	if (name == "func_clip_vphysics" || name == "func_playerclip") {
		return "tools/toolsclip";
	}
	return {};
}

std::pair<Change, int> createBrushEntity(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& classname,
                                         const EntityClass* definition, const std::string& material) {
	Transaction t(doc, "Create " + classname);
	Entity entity = newBrushEntity(doc, classname, definition);
	t.touchEntity(entity.id);
	entity.solids = makeShape(doc, box, options, material);
	if (entity.solids.empty()) {
		return {Change{}, 0};
	}
	const int id = entity.id;
	doc.entities.push_back(std::move(entity));
	return {t.finish(), id};
}

std::pair<Change, int> tieToEntity(MapDocument& doc, const Selection& selection, const std::string& classname, const EntityClass* definition) {
	Transaction t(doc, "Tie to " + classname);
	Entity entity = newBrushEntity(doc, classname, definition);
	t.touchEntity(entity.id);
	for (const auto& ref : selection.solids) {
		if (ref.entityId != 0) {
			continue;
		}
		t.touchSolid(ref);
		auto& solids = doc.world.solids;
		const auto it = std::find_if(solids.begin(), solids.end(), [&](const Solid& s) { return s.id == ref.solidId; });
		if (it != solids.end()) {
			entity.solids.push_back(std::move(*it));
			solids.erase(it);
		}
	}
	if (entity.solids.empty()) {
		return {Change{}, 0};
	}
	const int id = entity.id;
	doc.entities.push_back(std::move(entity));
	return {t.finish(), id};
}

Change moveToWorld(MapDocument& doc, const Selection& selection) {
	Transaction t(doc, "Move to world");
	for (const int id : selection.entities) {
		auto* entity = entityOrNull(doc, id);
		if (!entity || entity->solids.empty()) {
			continue;
		}
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			t.touchSolid({0, solid.id});
			doc.world.solids.push_back(std::move(solid));
		}
		std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
	}
	return t.finish();
}

Change setKeyValue(MapDocument& doc, const std::vector<int>& entityIds, const std::string& key, const std::optional<std::string>& value) {
	Transaction t(doc, "Edit " + key);
	for (const int id : entityIds) {
		auto* entity = doc.findEntity(id);
		if (!entity) {
			continue;
		}
		t.touchEntity(id);
		if (value) {
			entity->set(key, withoutQuotes(*value));
		} else {
			entity->erase(key);
		}
	}
	return t.finish();
}

std::string withoutQuotes(std::string text) {
	std::replace(text.begin(), text.end(), '"', '\'');
	return text;
}

Change setConnections(MapDocument& doc, int entityId, const std::vector<Connection>& connections) {
	Transaction t(doc, "Edit outputs");
	if (auto* entity = doc.findEntity(entityId)) {
		t.touchEntity(entityId);
		entity->connections = connections;
	}
	return t.finish();
}

// --- Shapes and brush tools -------------------------------------------------------------

namespace {

/// Selected brushes: world brushes and the brushes of selected brush entities (or faces' brushes).
std::vector<SolidRef> selectedSolids(const MapDocument& doc, const Selection& selection) {
	std::vector<SolidRef> refs = selection.solids;
	for (const int id : selection.entities) {
		if (const auto* entity = doc.findEntity(id); entity && id != 0) {
			for (const auto& solid : entity->solids) {
				refs.push_back({id, solid.id});
			}
		}
	}
	for (const auto& face : selection.faces) {
		if (std::find(refs.begin(), refs.end(), face.solid) == refs.end()) {
			refs.push_back(face.solid);
		}
	}
	return refs;
}

} // namespace

std::pair<Change, std::vector<SolidRef>> createShape(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& material) {
	Transaction t(doc, "Create " + shapeName(options.kind));
	std::vector<SolidRef> refs;
	for (auto& solid : makeShape(doc, box, options, material)) {
		const SolidRef ref{0, solid.id};
		t.touchSolid(ref);
		doc.world.solids.push_back(std::move(solid));
		refs.push_back(ref);
	}
	return {t.finish(), refs};
}

std::pair<Change, std::vector<SolidRef>> replaceSolids(MapDocument& doc, const std::vector<SolidRef>& remove, std::vector<Solid> add, const std::string& name) {
	Transaction t(doc, name);
	const int owner = remove.empty() ? 0 : remove.front().entityId;
	for (const auto& ref : remove) {
		t.touchSolid(ref);
		if (auto* entity = doc.findEntity(ref.entityId)) {
			std::erase_if(entity->solids, [&](const Solid& solid) { return solid.id == ref.solidId; });
		}
	}
	std::vector<SolidRef> added;
	auto* target = doc.findEntity(owner);
	if (!target) {
		target = &doc.world;
	}
	for (auto& solid : add) {
		const SolidRef ref{target == &doc.world ? 0 : owner, solid.id};
		t.touchSolid(ref);
		target->solids.push_back(std::move(solid));
		added.push_back(ref);
	}
	return {t.finish(), added};
}

std::pair<Change, Selection> hollow(MapDocument& doc, const Selection& selection, double thickness) {
	Transaction t(doc, "Hollow");
	Selection walls;
	for (const auto& ref : selectedSolids(doc, selection)) {
		const auto* solid = doc.findSolid(ref);
		if (!solid) {
			continue;
		}
		auto pieces = hollowSolid(doc, *solid, thickness);
		if (pieces.empty()) {
			continue;   // too thin for these walls: left as it is
		}
		t.touchSolid(ref);
		auto* entity = doc.findEntity(ref.entityId);
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == ref.solidId; });
		for (auto& piece : pieces) {
			const SolidRef pieceRef{ref.entityId, piece.id};
			t.touchSolid(pieceRef);
			entity->solids.push_back(std::move(piece));
			if (ref.entityId == 0) {
				walls.solids.push_back(pieceRef);
			} else if (!walls.containsEntity(ref.entityId)) {
				walls.entities.push_back(ref.entityId);
			}
		}
	}
	return {t.finish(), walls};
}

Change carve(MapDocument& doc, const Selection& selection) {
	Transaction t(doc, "Carve");
	const auto cutters = selectedSolids(doc, selection);
	std::vector<Solid> cutterCopies;
	for (const auto& ref : cutters) {
		if (const auto* solid = doc.findSolid(ref)) {
			cutterCopies.push_back(*solid);
		}
	}
	// Every other brush (world and brush entities) that a cutter touches.
	std::vector<SolidRef> targets;
	const auto collect = [&](const Entity& entity, int entityId) {
		for (const auto& solid : entity.solids) {
			const SolidRef ref{entityId, solid.id};
			if (std::find(cutters.begin(), cutters.end(), ref) == cutters.end()) {
				targets.push_back(ref);
			}
		}
	};
	collect(doc.world, 0);
	for (const auto& entity : doc.entities) {
		collect(entity, entity.id);
	}
	for (const auto& ref : targets) {
		const auto* original = doc.findSolid(ref);
		if (!original) {
			continue;
		}
		std::vector<Solid> pieces{*original};
		bool changed = false;
		for (const auto& cutter : cutterCopies) {
			std::vector<Solid> next;
			for (const auto& piece : pieces) {
				auto parts = subtractSolid(doc, piece, cutter);
				if (parts.size() != 1 || parts.front().id != piece.id || parts.front().sides.size() != piece.sides.size()) {
					changed = true;
				}
				next.insert(next.end(), parts.begin(), parts.end());
			}
			pieces = std::move(next);
		}
		if (!changed) {
			continue;
		}
		t.touchSolid(ref);
		auto* entity = doc.findEntity(ref.entityId);
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == ref.solidId; });
		for (auto& piece : pieces) {
			t.touchSolid({ref.entityId, piece.id});
			entity->solids.push_back(std::move(piece));
		}
	}
	return t.finish();
}

std::optional<MergeOutcome> merge(MapDocument& doc, const Selection& selection) {
	const auto refs = selectedSolids(doc, selection);
	std::vector<Solid> solids;
	for (const auto& ref : refs) {
		if (const auto* solid = doc.findSolid(ref)) {
			solids.push_back(*solid);
		}
	}
	auto result = mergeSolids(doc, solids);
	if (!result) {
		return std::nullopt;
	}
	auto [change, added] = replaceSolids(doc, refs, {std::move(result->solid)}, "Merge brushes");
	return MergeOutcome{std::move(change), added.front(), result->exact};
}

std::optional<Change> roundEdge(MapDocument& doc, const FaceRef& a, const FaceRef& b, double radius, int segments) {
	if (!(a.solid == b.solid)) {
		return std::nullopt;
	}
	const auto* solid = doc.findSolid(a.solid);
	if (!solid) {
		return std::nullopt;
	}
	auto rounded = sme::roundEdge(doc, *solid, a.sideId, b.sideId, radius, segments);
	if (!rounded) {
		return std::nullopt;
	}
	Transaction t(doc, segments > 1 ? "Round edge" : "Bevel edge");
	t.touchSolid(a.solid);
	*doc.findSolid(a.solid) = std::move(*rounded);
	return t.finish();
}

std::vector<EdgeFaces> edgesAtCorners(const MapDocument& doc, const Selection& selection, const std::vector<Vec3>& corners) {
	const auto picked = [&](const Vec3& v) {
		return std::any_of(corners.begin(), corners.end(), [&](const Vec3& c) { return length(c - v) < 0.01; });
	};
	const auto has = [](const std::vector<Vec3>& vertices, const Vec3& v) {
		return std::any_of(vertices.begin(), vertices.end(), [&](const Vec3& w) { return length(w - v) < 0.01; });
	};
	std::vector<EdgeFaces> edges;
	for (const auto& ref : selectedSolids(doc, selection)) {
		const auto* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		for (std::size_t f = 0; f < geometry.faces.size(); ++f) {
			const auto& face = geometry.faces[f];
			for (std::size_t i = 0; i < face.vertices.size(); ++i) {
				const Vec3 a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
				if (!picked(a) || !picked(b)) continue;
				// The other face along this edge comes later in the list (each edge once).
				for (std::size_t g = f + 1; g < geometry.faces.size(); ++g) {
					const auto& other = geometry.faces[g];
					if (has(other.vertices, a) && has(other.vertices, b)) {
						edges.push_back({FaceRef{ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id},
						                 FaceRef{ref, solid->sides[static_cast<std::size_t>(other.sideIndex)].id}});
						break;
					}
				}
			}
		}
	}
	return edges;
}

std::optional<Change> shapeEdges(MapDocument& doc, const std::vector<EdgeFaces>& edges, EdgeShape shape, double size, int segments) {
	Transaction t(doc, shape == EdgeShape::Wedge ? "Wedge edge" : shape == EdgeShape::Bevel ? "Bevel edge" : "Round edge");
	int done = 0;
	// One after another: sides keep their ids, so later edges of the same brush are still found.
	for (const auto& [a, b] : edges) {
		if (!(a.solid == b.solid)) continue;
		const auto* solid = doc.findSolid(a.solid);
		if (!solid) continue;
		std::optional<Solid> result;
		switch (shape) {
			case EdgeShape::Wedge: result = cutEdge(doc, *solid, a.sideId, b.sideId, std::nullopt); break;
			case EdgeShape::Bevel: result = cutEdge(doc, *solid, a.sideId, b.sideId, size); break;
			case EdgeShape::Round: result = sme::roundEdge(doc, *solid, a.sideId, b.sideId, size, std::max(2, segments)); break;
		}
		if (!result) continue;
		t.touchSolid(a.solid);
		*doc.findSolid(a.solid) = std::move(*result);
		++done;
	}
	if (done == 0) {
		return std::nullopt;
	}
	return t.finish();
}

std::pair<Change, Selection> clip(MapDocument& doc, const Selection& selection, const Plane& plane, ClipKeep keep) {
	Transaction t(doc, "Clip");
	Selection result;
	for (const auto& ref : selectedSolids(doc, selection)) {
		const auto* solid = doc.findSolid(ref);
		if (!solid) {
			continue;
		}
		// The new face gets the texture of the side facing most like the cut.
		const Side* cap = &solid->sides.front();
		for (const auto& side : solid->sides) {
			if (dot(side.plane().normal, plane.normal) > dot(cap->plane().normal, plane.normal)) {
				cap = &side;
			}
		}
		auto [front, back] = splitSolid(doc, *solid, plane, *cap);
		if (!front || !back) {
			// Not cut: only removed when it lies entirely on the side that is thrown away.
			const bool removeIt = (keep == ClipKeep::Back && front && !back) || (keep == ClipKeep::Front && back && !front);
			if (removeIt) {
				t.touchSolid(ref);
				std::erase_if(doc.findEntity(ref.entityId)->solids, [&](const Solid& s) { return s.id == ref.solidId; });
			} else {
				result.solids.push_back(ref);
			}
			continue;
		}
		t.touchSolid(ref);
		auto* entity = doc.findEntity(ref.entityId);
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == ref.solidId; });
		for (auto* part : {keep != ClipKeep::Front ? &*back : nullptr, keep != ClipKeep::Back ? &*front : nullptr}) {
			if (!part) {
				continue;
			}
			const SolidRef partRef{ref.entityId, part->id};
			t.touchSolid(partRef);
			entity->solids.push_back(std::move(*part));
			result.solids.push_back(partRef);
		}
	}
	return {t.finish(), result};
}

Change justifyFaces(MapDocument& doc, const std::vector<FaceRef>& faces, Justify mode, bool treatAsOne,
                    const std::function<std::pair<int, int>(const std::string&)>& textureSize) {
	Transaction t(doc, "Justify texture");
	// Corners of every face (for "treat as one" the extents of all of them count).
	std::vector<std::vector<Vec3>> corners(faces.size());
	std::vector<Side*> sides(faces.size(), nullptr);
	for (std::size_t i = 0; i < faces.size(); ++i) {
		t.touchSolid(faces[i].solid);
		auto* solid = doc.findSolid(faces[i].solid);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		for (const auto& face : geometry.faces) {
			auto& side = solid->sides[static_cast<std::size_t>(face.sideIndex)];
			if (side.id == faces[i].sideId) {
				corners[i] = face.vertices;
				sides[i] = &side;
			}
		}
	}
	const Side* first = nullptr;
	for (auto* side : sides) {
		if (side) {
			first = side;
			break;
		}
	}
	if (!first) {
		return t.finish();
	}
	const Side reference = *first;
	for (std::size_t i = 0; i < faces.size(); ++i) {
		Side* side = sides[i];
		if (!side) continue;
		// One surface: faces in the same plane direction take the first face's texture axes.
		if (treatAsOne && dot(side->plane().normal, reference.plane().normal) > 0.999) {
			side->u.axis = reference.u.axis;
			side->v.axis = reference.v.axis;
			side->u.scale = reference.u.scale;
			side->v.scale = reference.v.scale;
			side->rotation = reference.rotation;
		}
		double minU = 1e30, maxU = -1e30, minV = 1e30, maxV = -1e30;
		for (std::size_t j = 0; j < faces.size(); ++j) {
			if (!treatAsOne && j != i) continue;
			for (const auto& p : corners[j]) {
				minU = std::min(minU, dot(p, side->u.axis));
				maxU = std::max(maxU, dot(p, side->u.axis));
				minV = std::min(minV, dot(p, side->v.axis));
				maxV = std::max(maxV, dot(p, side->v.axis));
			}
		}
		const auto [width, height] = textureSize ? textureSize(side->material) : std::pair<int, int>{512, 512};
		const double w = std::max(1, width), h = std::max(1, height);
		auto& u = side->u;
		auto& v = side->v;
		if (u.scale == 0) u.scale = 0.25;
		if (v.scale == 0) v.scale = 0.25;
		switch (mode) {
			case Justify::Fit:
				u.scale = (maxU - minU) / w;
				v.scale = (maxV - minV) / h;
				if (u.scale == 0) u.scale = 0.25;
				if (v.scale == 0) v.scale = 0.25;
				u.shift = -minU / u.scale;
				v.shift = -minV / v.scale;
				break;
			case Justify::Left: u.shift = -minU / u.scale; break;
			case Justify::Right: u.shift = w - maxU / u.scale; break;
			case Justify::Top: v.shift = -minV / v.scale; break;
			case Justify::Bottom: v.shift = h - maxV / v.scale; break;
			case Justify::Center:
				u.shift = w / 2 - (minU + maxU) / 2 / u.scale;
				v.shift = h / 2 - (minV + maxV) / 2 / v.scale;
				break;
		}
		// Keep the shift small (same picture).
		u.shift = std::fmod(u.shift, w);
		v.shift = std::fmod(v.shift, h);
	}
	return t.finish();
}

VertexMoveOutcome moveVertices(MapDocument& doc, const std::vector<SolidRef>& solids, const std::vector<Vec3>& corners, const Vec3& delta) {
	VertexMoveOutcome outcome;
	std::vector<std::pair<SolidRef, Solid>> results;
	for (const auto& ref : solids) {
		const auto* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto own = solidCorners(*solid);
		const bool touched = std::any_of(own.begin(), own.end(), [&](const Vec3& c) {
			return std::any_of(corners.begin(), corners.end(), [&](const Vec3& k) { return length(c - k) < 0.01; });
		});
		if (!touched) continue;
		auto edit = sme::moveVertices(doc, *solid, corners, delta);
		if (!edit) {
			outcome.refused = true;
			return outcome;
		}
		outcome.cornersLost = outcome.cornersLost || edit->cornersLost;
		results.emplace_back(ref, std::move(edit->solid));
	}
	Transaction t(doc, "Move vertices");
	for (auto& [ref, solid] : results) {
		t.touchSolid(ref);
		*doc.findSolid(ref) = std::move(solid);
	}
	outcome.change = t.finish();
	return outcome;
}

} // namespace edit
} // namespace sme
