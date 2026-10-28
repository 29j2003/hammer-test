// doors.cpp - Door settings <-> keys, the door frame, the preview motion, Make door (see doors.h).

#include <sme/doors.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <set>

namespace sme {

namespace {

constexpr double DEG = std::numbers::pi / 180.0;

// func_door / func_door_rotating spawnflags.
constexpr long long FLAG_REVERSE = 2;       // Rotating: turn the other way
constexpr long long FLAG_ONE_WAY = 16;      // Rotating: do not open away from the player
constexpr long long FLAG_TOGGLE = 32;
constexpr long long FLAG_USE_OPENS = 256;
constexpr long long FLAG_TOUCH_OPENS = 1024;
constexpr long long FLAG_LOCKED = 2048;     // Also prop_door_rotating

/// Number in a key, or `fallback` when it is missing or not a number.
double number(const Entity& entity, std::string_view key, double fallback) {
	const std::string text = entity.get(key);
	if (text.empty()) {
		return fallback;
	}
	char* end = nullptr;
	const double value = std::strtod(text.c_str(), &end);
	return end == text.c_str() ? fallback : value;
}

long long spawnflags(const Entity& entity) {
	return std::strtoll(entity.get("spawnflags", "0").c_str(), nullptr, 10);
}

void setFlag(long long& flags, long long bit, bool on) {
	flags = on ? (flags | bit) : (flags & ~bit);
}

/// Unit direction after turning `v` by 90 degrees counter-clockwise (seen from above).
Vec3 turnedLeft(const Vec3& v) {
	return {-v.y, v.x, 0};
}

/// From the hinge to the free edge of a brush door with this hinge.
Vec3 leafDirection(const DoorFrame& frame, DoorHinge hinge) {
	return hinge == DoorHinge::Left ? frame.right : frame.right * -1.0;
}

/// Whether turning counter-clockwise (positive yaw) swings the free edge to the front.
bool positiveTurnOpensFront(const DoorFrame& frame, DoorHinge hinge) {
	return dot(turnedLeft(leafDirection(frame, hinge)), frame.front) > 0;
}

/// Local direction and length of a door model's leaf: the longer horizontal side of its box,
/// pointing away from the origin (the hinge). HL2's door models run along +Y.
std::pair<Vec3, double> modelLeaf(const Bounds* local) {
	if (!local || local->empty()) {
		return {{0, 1, 0}, 54};
	}
	const Vec3 size = local->size();
	const Vec3 center = local->center();
	if (size.y >= size.x) {
		return {{0, center.y >= 0 ? 1.0 : -1.0, 0}, std::max(std::abs(local->min.y), std::abs(local->max.y))};
	}
	return {{center.x >= 0 ? 1.0 : -1.0, 0, 0}, std::max(std::abs(local->min.x), std::abs(local->max.x))};
}

double yawOf(const Vec3& v) {
	return std::atan2(v.y, v.x) / DEG;
}

/// Angle in [0, 360), rounded to 1/100 degree so keys stay readable.
double normalizedYaw(double yaw) {
	yaw = std::fmod(yaw, 360.0);
	if (yaw < 0) yaw += 360;
	yaw = std::round(yaw * 100) / 100;
	return yaw >= 360 ? 0 : yaw;
}

Vec3 parseAngles(const Entity& entity) {
	Vec3 angles;
	if (!parseVec3(entity.get("angles", "0 0 0"), angles)) {
		angles = {};
	}
	return angles;
}

std::vector<Vec3> boxCorners(const Bounds& box) {
	std::vector<Vec3> corners;
	for (int i = 0; i < 8; ++i) {
		corners.push_back({(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z});
	}
	return corners;
}

/// A new entity of `classname` with the FGD's defaults (like Hammer when it creates one).
Entity newEntity(MapDocument& doc, const std::string& classname, const EntityClass* definition) {
	Entity entity;
	entity.id = doc.newEntityId();
	entity.set("classname", classname);
	if (definition && iequals(definition->name, classname)) {
		for (const auto& property : definition->properties) {
			if (property.kind == EntityProperty::Kind::Plain && !property.defaultValue.empty() && !iequals(property.name, "origin")) {
				entity.set(property.name, property.defaultValue);
			}
			if (property.kind == EntityProperty::Kind::Flags) {
				std::uint64_t bits = 0;
				for (const auto& flag : property.flags) {
					if (flag.enabledByDefault) bits |= flag.value;
				}
				entity.set(property.name, std::to_string(bits));
			}
		}
	}
	return entity;
}

/// Writes the settings to a door's keys. `frame` is the brush box's frame (brush doors).
/// `placeHinge` moves a swinging door's origin to the hinge edge.
void applyDoor(Entity& door, const DoorSettings& s, const DoorFrame& frame, bool placeHinge) {
	const auto setNumber = [&](std::string_view key, double value) { door.set(key, formatNumber(value)); };
	long long flags = spawnflags(door);
	const std::vector<DoorSoundSlot> slots = doorSoundSlots(s.type);
	if (s.type == DoorType::Prop) {
		setNumber("speed", s.speed);
		setNumber("distance", std::abs(s.angle));
		setNumber("returndelay", s.closeAfter);
		door.set("opendir", s.swing == DoorSwing::Front ? "1" : s.swing == DoorSwing::Back ? "2" : "0");
		setNumber("hardware", s.hardware);
		if (!s.model.empty()) {
			door.set("model", s.model);
		}
		setFlag(flags, FLAG_LOCKED, s.locked);
	} else {
		door.set("classname", doorClass(s.type));
		setNumber("speed", s.speed);
		setNumber("wait", s.toggle ? -1 : s.closeAfter);
		setFlag(flags, FLAG_USE_OPENS, s.useOpens);
		setFlag(flags, FLAG_TOUCH_OPENS, s.touchOpens);
		setFlag(flags, FLAG_TOGGLE, s.toggle);
		setFlag(flags, FLAG_LOCKED, s.locked);
		if (s.type == DoorType::Swinging) {
			setNumber("distance", std::abs(s.angle));
			if (placeHinge) {
				door.setOrigin(hingePoint(frame, s.hinge));
			}
			const bool oneWay = s.swing != DoorSwing::AwayFromPlayer;
			setFlag(flags, FLAG_ONE_WAY, oneWay);
			setFlag(flags, FLAG_REVERSE, oneWay && positiveTurnOpensFront(frame, s.hinge) != (s.swing == DoorSwing::Front));
		} else {
			setFlag(flags, FLAG_ONE_WAY, false);
			setFlag(flags, FLAG_REVERSE, false);
			if (s.slide != DoorSlide::Other) {
				door.set("movedir", moveDirAngles(slideDirection(frame, s.slide)));
			}
			setNumber("lip", s.lip);
		}
	}
	door.set("spawnflags", std::to_string(flags));
	if (s.sounds.size() == slots.size()) {
		for (std::size_t i = 0; i < slots.size(); ++i) {
			for (const auto& key : slots[i].keys) {
				if (s.sounds[i].empty()) {
					door.erase(key);
				} else {
					door.set(key, s.sounds[i]);
				}
			}
		}
	}
}

/// Prop door at the hinge of `frame`'s box: the model's leaf turned onto the box, its bottom on the floor.
void placePropDoor(Entity& door, const DoorFrame& frame, DoorHinge hinge, const Bounds* modelBounds) {
	const Vec3 leafWorld = leafDirection(frame, hinge);
	const auto [leafLocal, length] = modelLeaf(modelBounds);
	(void)length;
	const Vec3 h = hingePoint(frame, hinge);
	const double floor = frame.box.min.z - (modelBounds && !modelBounds->empty() ? modelBounds->min.z : 0);
	door.setOrigin({h.x, h.y, floor});
	door.set("angles", "0 " + formatNumber(normalizedYaw(yawOf(leafWorld) - yawOf(leafLocal))) + " 0");
}

} // namespace

bool isDoor(const Entity& entity) {
	return doorType(entity.classname()).has_value();
}

std::optional<DoorType> doorType(std::string_view classname) {
	if (iequals(classname, "func_door")) return DoorType::Sliding;
	if (iequals(classname, "func_door_rotating")) return DoorType::Swinging;
	if (iequals(classname, "prop_door_rotating")) return DoorType::Prop;
	return std::nullopt;
}

std::string doorClass(DoorType type) {
	switch (type) {
	case DoorType::Sliding: return "func_door";
	case DoorType::Swinging: return "func_door_rotating";
	case DoorType::Prop: return "prop_door_rotating";
	}
	return "func_door";
}

DoorFrame doorFrame(const Bounds& box) {
	DoorFrame frame;
	frame.box = box;
	const Vec3 size = box.size();
	frame.height = size.z;
	if (size.y <= size.x) {   // thin along Y: the front faces +Y, seen from the front, right is -X
		frame.front = {0, 1, 0};
		frame.right = {-1, 0, 0};
		frame.width = size.x;
		frame.thickness = size.y;
	} else {                  // thin along X: the front faces +X, right is +Y
		frame.front = {1, 0, 0};
		frame.right = {0, 1, 0};
		frame.width = size.y;
		frame.thickness = size.x;
	}
	return frame;
}

Bounds entityBrushBounds(const Entity& entity) {
	Bounds box;
	for (const auto& solid : entity.solids) {
		box.add(buildBrushGeometry(solid).bounds);
	}
	return box;
}

Vec3 hingePoint(const DoorFrame& frame, DoorHinge hinge) {
	return frame.box.center() + frame.right * (frame.width * (hinge == DoorHinge::Right ? 0.5 : -0.5));
}

std::string moveDirAngles(const Vec3& direction) {
	const Vec3 d = normalize(direction);
	if (d.z > 0.999) return "-90 0 0";
	if (d.z < -0.999) return "90 0 0";
	return "0 " + formatNumber(normalizedYaw(yawOf(d))) + " 0";
}

std::optional<Vec3> moveDirVector(std::string_view angles) {
	Vec3 a;
	if (!parseVec3(angles, a)) {
		return std::nullopt;
	}
	const double pitch = a.x * DEG, yaw = a.y * DEG;
	Vec3 d{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
	for (int i = 0; i < 3; ++i) {
		if (std::abs(d[i]) < 1e-9) d[i] = 0;
	}
	return d;
}

Vec3 slideOffset(const Bounds& box, const Vec3& direction, double lip) {
	const Vec3 size = box.size() - Vec3{2, 2, 2};
	const Vec3 d = normalize(direction);
	return d * (std::abs(dot(d, size)) - lip);
}

Vec3 slideDirection(const DoorFrame& frame, DoorSlide slide) {
	switch (slide) {
	case DoorSlide::Up: return {0, 0, 1};
	case DoorSlide::Down: return {0, 0, -1};
	case DoorSlide::Left: return frame.right * -1.0;
	case DoorSlide::Right: return frame.right;
	case DoorSlide::Other: break;
	}
	return {};
}

std::vector<DoorSoundSlot> doorSoundSlots(DoorType type) {
	if (type == DoorType::Prop) {
		return {{"Opening", {"soundopenoverride"}}, {"Moving", {"soundmoveoverride"}}, {"Closed", {"soundcloseoverride"}}, {"Locked", {"soundlockedoverride"}}};
	}
	// noise1 plays while it moves (also while closing unless startclosesound is set),
	// noise2/closesound when it stops at the open/closed end.
	return {{"Opening", {"noise1"}}, {"Closing", {"startclosesound"}}, {"Stops", {"noise2", "closesound"}}, {"Locked", {"locked_sound"}}};
}

bool isDoorSound(std::string_view name) {
	return toLower(name).find("door") != std::string::npos;
}

std::vector<std::string> defaultDoorSounds(DoorType type, const std::vector<std::string>& soundNames) {
	if (type == DoorType::Prop) {
		return {};
	}
	std::vector<std::string> doorSounds;
	for (const auto& name : soundNames) {
		const std::string lower = toLower(name);
		if (isDoorSound(name) && lower.find("null") == std::string::npos) doorSounds.push_back(name);
	}
	std::sort(doorSounds.begin(), doorSounds.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
	const auto pick = [&](std::initializer_list<std::string_view> words) -> std::string {
		for (const auto word : words) {
			for (const auto& name : doorSounds) {
				if (toLower(name).find(word) != std::string::npos) return name;
			}
		}
		return {};
	};
	return {pick({"move", "open"}), "", pick({"fullclose", "arrive", "stop", "close"}), pick({"locked", "lock"})};
}

std::vector<std::string> doorModels(const std::vector<std::string>& modelNames) {
	std::vector<std::string> out;
	for (const auto& name : modelNames) {
		const std::string lower = toLower(name);
		if (!lower.ends_with(".mdl") || lower.find("door") == std::string::npos) continue;
		bool skip = false;
		for (const std::string_view word : {"frame", "handle", "knob", "gib", "chunk", "break", "damage", "trim"}) {
			if (lower.find(word) != std::string::npos) skip = true;
		}
		if (!skip) out.push_back(name);
	}
	return out;
}

DoorSettings readDoor(const Entity& door) {
	DoorSettings s;
	s.type = doorType(door.classname()).value_or(DoorType::Sliding);
	const long long flags = spawnflags(door);
	s.speed = number(door, "speed", 100);
	s.locked = (flags & FLAG_LOCKED) != 0;
	for (const auto& slot : doorSoundSlots(s.type)) {
		s.sounds.push_back(door.get(slot.keys.front()));
	}
	if (s.type == DoorType::Prop) {
		s.angle = std::abs(number(door, "distance", 90));
		s.closeAfter = number(door, "returndelay", -1);
		const int opendir = static_cast<int>(number(door, "opendir", 0));
		s.swing = opendir == 1 ? DoorSwing::Front : opendir == 2 ? DoorSwing::Back : DoorSwing::AwayFromPlayer;
		s.model = door.get("model");
		s.hardware = static_cast<int>(number(door, "hardware", 1));
		return s;
	}
	s.closeAfter = number(door, "wait", 4);
	s.useOpens = (flags & FLAG_USE_OPENS) != 0;
	s.touchOpens = (flags & FLAG_TOUCH_OPENS) != 0;
	s.toggle = (flags & FLAG_TOGGLE) != 0;
	s.lip = number(door, "lip", 0);
	const DoorFrame frame = doorFrame(entityBrushBounds(door));
	if (s.type == DoorType::Swinging) {
		const double distance = number(door, "distance", 90);
		s.angle = std::abs(distance);
		s.hinge = door.hasOrigin() && dot(door.origin() - frame.box.center(), frame.right) > 0 ? DoorHinge::Right : DoorHinge::Left;
		if ((flags & FLAG_ONE_WAY) != 0) {
			const bool positive = (distance >= 0) != ((flags & FLAG_REVERSE) != 0);
			s.swing = positive == positiveTurnOpensFront(frame, s.hinge) ? DoorSwing::Front : DoorSwing::Back;
		}
		return s;
	}
	const Vec3 dir = moveDirVector(door.get("movedir", "0 0 0")).value_or(Vec3{1, 0, 0});
	s.slide = DoorSlide::Other;
	for (const DoorSlide slide : {DoorSlide::Up, DoorSlide::Down, DoorSlide::Left, DoorSlide::Right}) {
		if (dot(dir, slideDirection(frame, slide)) > 0.999) s.slide = slide;
	}
	return s;
}

std::optional<DoorMotion> doorMotion(const Entity& door, const Bounds* modelBounds) {
	const auto type = doorType(door.classname());
	if (!type) {
		return std::nullopt;
	}
	const DoorSettings s = readDoor(door);
	DoorMotion m;
	m.type = *type;
	if (*type == DoorType::Prop) {
		Bounds local;
		if (modelBounds && !modelBounds->empty()) {
			local = *modelBounds;
		} else {
			local.add({-1, 0, 0});
			local.add({1, 54, 108});
		}
		const Mat3 rotation = edit::anglesToMatrix(parseAngles(door));
		const Vec3 origin = door.origin();
		for (const auto& corner : boxCorners(local)) {
			m.corners.push_back(origin + rotation * corner);
			m.closed.add(m.corners.back());
		}
		const auto [leafLocal, length] = modelLeaf(&local);
		Vec3 leaf = rotation * leafLocal;
		leaf.z = 0;
		m.leaf = normalize(leaf);
		m.leafLength = length;
		m.hinge = {origin.x, origin.y, m.closed.center().z};
		// CPropDoorRotating: opening forward turns by -distance, backward by +distance.
		if (s.swing != DoorSwing::Back) m.turns.push_back(-s.angle);
		if (s.swing != DoorSwing::Front) m.turns.push_back(s.angle);
		return m;
	}
	m.closed = entityBrushBounds(door);
	if (m.closed.empty()) {
		return std::nullopt;
	}
	m.corners = boxCorners(m.closed);
	const DoorFrame frame = doorFrame(m.closed);
	if (*type == DoorType::Sliding) {
		const Vec3 dir = moveDirVector(door.get("movedir", "0 0 0")).value_or(Vec3{1, 0, 0});
		m.slide = slideOffset(m.closed, dir, s.lip);
		return m;
	}
	const Vec3 origin = door.origin();   // without an origin key Source turns it around the map origin
	m.hinge = {origin.x, origin.y, m.closed.center().z};
	const Vec3 toCenter = Vec3{m.closed.center().x, m.closed.center().y, m.hinge.z} - m.hinge;
	m.leaf = dot(toCenter, frame.right) >= 0 ? frame.right : frame.right * -1.0;
	const Vec3 freeEdge = m.closed.center() + m.leaf * (frame.width / 2);
	m.leafLength = std::max(1.0, dot(freeEdge - m.hinge, m.leaf));
	const long long flags = spawnflags(door);
	const double distance = number(door, "distance", 90);
	const double sign = ((distance >= 0) != ((flags & FLAG_REVERSE) != 0)) ? 1.0 : -1.0;
	m.turns.push_back(sign * s.angle);
	if ((flags & FLAG_ONE_WAY) == 0) {
		m.turns.push_back(-sign * s.angle);   // opens away from the player: either way
	}
	return m;
}

double doorTravelTime(const DoorMotion& motion, double speed) {
	if (speed <= 0) {
		speed = 100;
	}
	double travel = length(motion.slide);
	for (const double turn : motion.turns) {
		travel = std::max(travel, std::abs(turn));
	}
	return std::clamp(travel / speed, 0.1, 10.0);
}

std::string uniqueTargetName(const MapDocument& doc, std::string_view classname) {
	const std::string cls = toLower(classname);
	std::string prefix;
	if (doorType(cls)) {
		prefix = "door";
	} else if (cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button") {
		prefix = "button";
	} else if (cls == "ambient_generic") {
		prefix = "sound";
	} else if (cls.starts_with("light")) {
		prefix = "light";
	} else if (cls.starts_with("trigger_")) {
		prefix = "trigger";
	} else if (cls.starts_with("npc_")) {
		prefix = cls.substr(4);
	} else {
		prefix = cls;
		for (const std::string_view strip : {"func_", "env_", "logic_", "point_", "info_", "game_", "filter_", "math_", "prop_"}) {
			if (prefix.starts_with(strip) && prefix.size() > strip.size()) {
				prefix = prefix.substr(strip.size());
				break;
			}
		}
	}
	if (prefix.empty()) {
		prefix = "entity";
	}
	std::set<std::string> taken;
	for (const auto& entity : doc.entities) {
		taken.insert(toLower(entity.get("targetname")));
	}
	for (int n = 1;; ++n) {
		std::string name = prefix + "_" + std::to_string(n);
		if (!taken.contains(name)) return name;
	}
}

const Entity* doorAreaportal(const MapDocument& doc, const Entity& door) {
	Bounds box;
	if (doorType(door.classname()) == DoorType::Prop) {
		if (const auto motion = doorMotion(door)) box = motion->closed;
	} else {
		box = entityBrushBounds(door);
	}
	if (box.empty()) {
		return nullptr;
	}
	constexpr double reach = 4;
	box.min = box.min - Vec3{reach, reach, reach};
	box.max = box.max + Vec3{reach, reach, reach};
	for (const auto& entity : doc.entities) {
		if (!iequals(entity.classname(), "func_areaportal")) continue;
		const Bounds portal = entityBrushBounds(entity);
		if (portal.empty()) continue;
		bool overlaps = true;
		for (int i = 0; i < 3; ++i) {
			overlaps = overlaps && portal.min[i] <= box.max[i] && portal.max[i] >= box.min[i];
		}
		if (overlaps) return &entity;
	}
	return nullptr;
}

bool areaportalLinked(const Entity& areaportal, const Entity& door) {
	const std::string name = door.get("targetname");
	return !name.empty() && iequals(areaportal.get("target"), name);
}

namespace edit {

std::pair<Change, std::vector<int>> makeDoor(MapDocument& doc, const Selection& selection, const DoorSettings& settings, const EntityClass* definition,
                                             const Bounds* modelBounds) {
	std::vector<Solid> solids;
	Bounds box;
	for (const auto& ref : selection.solids) {
		if (ref.entityId != 0) continue;
		if (const Solid* solid = doc.findSolid(ref)) {
			box.add(buildBrushGeometry(*solid).bounds);
			solids.push_back(*solid);
		}
	}
	if (solids.empty() || box.empty()) {
		return {Change{}, {}};
	}
	const DoorFrame frame = doorFrame(box);
	Transaction t(doc, "Make door");
	for (const auto& solid : solids) {
		t.touchSolid({0, solid.id});
		std::erase_if(doc.world.solids, [&](const Solid& s) { return s.id == solid.id; });
	}
	const std::string cls = doorClass(settings.type);
	const std::string sharedName = settings.doubleDoor ? uniqueTargetName(doc, cls) : std::string{};

	// One half per hinge: the left half hinges on the left and the right half on the right, so
	// both open from the middle (sliding halves part to the sides).
	struct Half {
		DoorFrame frame;
		DoorHinge hinge;
		std::vector<Solid> solids;
	};
	std::vector<Half> halves;
	if (!settings.doubleDoor) {
		halves.push_back({frame, settings.hinge, std::move(solids)});
	} else {
		const Vec3 center = box.center();
		const Plane cut{frame.right, dot(frame.right, center)};
		Half left{frame, DoorHinge::Left, {}}, right{frame, DoorHinge::Right, {}};
		left.frame.box.max = box.max;
		right.frame.box.min = box.min;
		for (int i = 0; i < 3; ++i) {   // each half's box ends at the cut
			if (std::abs(frame.right[i]) > 0.5) {
				(frame.right[i] > 0 ? left.frame.box.max[i] : left.frame.box.min[i]) = center[i];
				(frame.right[i] > 0 ? right.frame.box.min[i] : right.frame.box.max[i]) = center[i];
			}
		}
		left.frame.width = right.frame.width = frame.width / 2;
		if (settings.type != DoorType::Prop) {
			for (const auto& solid : solids) {
				auto [front, back] = splitSolid(doc, solid, cut, solid.sides.front());
				if (front) right.solids.push_back(std::move(*front));
				if (back) left.solids.push_back(std::move(*back));
			}
		}
		halves.push_back(std::move(left));
		halves.push_back(std::move(right));
	}

	std::vector<int> ids;
	for (auto& half : halves) {
		if (settings.type != DoorType::Prop && half.solids.empty()) continue;
		Entity door = newEntity(doc, cls, definition);
		t.touchEntity(door.id);
		DoorSettings s = settings;
		s.hinge = half.hinge;
		if (settings.doubleDoor && settings.type == DoorType::Sliding && (s.slide == DoorSlide::Left || s.slide == DoorSlide::Right)) {
			s.slide = half.hinge == DoorHinge::Left ? DoorSlide::Left : DoorSlide::Right;
		}
		if (settings.type == DoorType::Prop) {
			placePropDoor(door, half.frame, half.hinge, modelBounds);
			if (!sharedName.empty()) door.set("slavename", sharedName);   // the other half opens with it
		} else {
			door.solids = std::move(half.solids);
		}
		if (!sharedName.empty()) door.set("targetname", sharedName);
		applyDoor(door, s, half.frame, true);
		ids.push_back(door.id);
		doc.entities.push_back(std::move(door));
	}
	if (ids.empty()) {
		return {Change{}, {}};
	}
	return {t.finish(), ids};
}

Change setDoor(MapDocument& doc, int doorId, const DoorSettings& settings) {
	Entity* door = doorId == 0 ? nullptr : doc.findEntity(doorId);
	if (!door || !isDoor(*door)) {
		return {};
	}
	const DoorSettings old = readDoor(*door);
	DoorSettings s = settings;
	if ((old.type == DoorType::Prop) != (s.type == DoorType::Prop)) {
		s.type = old.type;   // a brush can not become a model or back
	}
	if (s.sounds.size() != doorSoundSlots(s.type).size()) {
		s.sounds.clear();   // slots of the other brush type line up, but only write a full set
	}
	Transaction t(doc, "Edit door");
	t.touchEntity(doorId);
	door = doc.findEntity(doorId);
	const DoorFrame frame = doorFrame(entityBrushBounds(*door));
	const bool placeHinge = s.type == DoorType::Swinging && (!door->hasOrigin() || old.type != DoorType::Swinging || old.hinge != s.hinge);
	const auto keysBefore = door->keyValues;
	applyDoor(*door, s, frame, placeHinge);
	if (door->keyValues == keysBefore) {
		return {};   // nothing changed (an editor reporting the value it already had): no undo step
	}
	return t.finish();
}

Change linkAreaportal(MapDocument& doc, int doorId, int areaportalId) {
	Entity* door = doorId == 0 ? nullptr : doc.findEntity(doorId);
	const Entity* portal = areaportalId == 0 ? nullptr : doc.findEntity(areaportalId);
	if (!door || !portal) {
		return {};
	}
	Transaction t(doc, "Link areaportal");
	t.touchEntity(doorId);
	t.touchEntity(areaportalId);
	door = doc.findEntity(doorId);
	std::string name = door->get("targetname");
	if (name.empty()) {
		name = uniqueTargetName(doc, door->classname());
		door->set("targetname", name);
	}
	doc.findEntity(areaportalId)->set("target", name);
	return t.finish();
}

} // namespace edit

} // namespace sme
