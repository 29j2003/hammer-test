// editing.cpp - Undoable editor operations (see editing.h).

#include <sme/editing.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>

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

std::pair<Change, int> placeEntity(MapDocument& doc, const std::string& classname, const Vec3& origin, const EntityClass* definition) {
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

std::pair<Change, int> tieToEntity(MapDocument& doc, const Selection& selection, const std::string& classname, const EntityClass* definition) {
	Transaction t(doc, "Tie to " + classname);
	Entity entity;
	entity.id = doc.newEntityId();
	t.touchEntity(entity.id);
	entity.set("classname", classname);
	if (definition) {
		for (const auto& property : definition->properties) {
			if (property.kind == EntityProperty::Kind::Plain && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
				entity.set(property.name, property.defaultValue);
			}
		}
	}
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
			entity->set(key, *value);
		} else {
			entity->erase(key);
		}
	}
	return t.finish();
}

Change setConnections(MapDocument& doc, int entityId, const std::vector<Connection>& connections) {
	Transaction t(doc, "Edit outputs");
	if (auto* entity = doc.findEntity(entityId)) {
		t.touchEntity(entityId);
		entity->connections = connections;
	}
	return t.finish();
}

} // namespace edit
} // namespace sme
