// map.cpp - VMF parsing and writing (see map.h).

#include <sme/map.h>

#include <sme/text.h>

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace sme {

namespace {

bool parseDouble(std::string_view text, double& out) {
	text = trim(text);
	if (text.starts_with('+')) {
		text.remove_prefix(1);
	}
	const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

int parseInt(std::string_view text, int fallback = 0) {
	double value = 0;
	return parseDouble(text, value) ? static_cast<int>(value) : fallback;
}

/// "(a b c) (d e f) (g h i)"
bool parsePlane(std::string_view text, std::array<Vec3, 3>& out) {
	std::size_t pos = 0;
	for (auto& point : out) {
		const auto open = text.find('(', pos);
		const auto close = text.find(')', open);
		if (open == std::string_view::npos || close == std::string_view::npos) {
			return false;
		}
		if (!parseVec3(text.substr(open + 1, close - open - 1), point)) {
			return false;
		}
		pos = close + 1;
	}
	return true;
}

/// "[x y z shift] scale"
bool parseAxis(std::string_view text, TextureAxis& out) {
	const auto open = text.find('[');
	const auto close = text.find(']');
	if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
		return false;
	}
	const auto inner = splitTrimmed(text.substr(open + 1, close - open - 1), ' ');
	if (inner.size() != 4) {
		return false;
	}
	return parseDouble(inner[0], out.axis.x) && parseDouble(inner[1], out.axis.y) && parseDouble(inner[2], out.axis.z) &&
	       parseDouble(inner[3], out.shift) && parseDouble(text.substr(close + 1), out.scale);
}

std::string formatAxis(const TextureAxis& axis) {
	return "[" + formatNumber(axis.axis.x) + " " + formatNumber(axis.axis.y) + " " + formatNumber(axis.axis.z) + " " +
	       formatNumber(axis.shift) + "] " + formatNumber(axis.scale);
}

Side parseSide(const KvNode& node) {
	Side side;
	bool hasPlane = false;
	for (const auto& child : node.children) {
		if (child.block) {
			side.extra.push_back(child);
		} else if (iequals(child.key, "id")) {
			side.id = parseInt(child.value);
		} else if (iequals(child.key, "plane")) {
			hasPlane = parsePlane(child.value, side.planePoints);
		} else if (iequals(child.key, "material")) {
			side.material = child.value;
		} else if (iequals(child.key, "uaxis")) {
			if (!parseAxis(child.value, side.u)) {
				side.extra.push_back(child);
			}
		} else if (iequals(child.key, "vaxis")) {
			if (!parseAxis(child.value, side.v)) {
				side.extra.push_back(child);
			}
		} else if (iequals(child.key, "rotation")) {
			parseDouble(child.value, side.rotation);
		} else if (iequals(child.key, "lightmapscale")) {
			side.lightmapScale = parseInt(child.value, 16);
		} else if (iequals(child.key, "smoothing_groups")) {
			side.smoothingGroups = parseInt(child.value);
		} else {
			side.extra.push_back(child);
		}
	}
	if (!hasPlane) {
		throw std::runtime_error("side " + std::to_string(side.id) + " has no valid plane");
	}
	return side;
}

Solid parseSolid(const KvNode& node) {
	Solid solid;
	for (const auto& child : node.children) {
		if (!child.block && iequals(child.key, "id")) {
			solid.id = parseInt(child.value);
		} else if (child.block && iequals(child.key, "side")) {
			solid.sides.push_back(parseSide(child));
		} else {
			solid.extra.push_back(child);
		}
	}
	return solid;
}

// An output line: key = output name, value = "target,input,parameter,delay,times to fire".
// Newer VMFs separate the fields with ESC (0x1B) instead of commas; the separator found is kept
// so a save writes the line back the same way. Missing fields get defaults (times to fire -1).
Connection parseConnection(const KvNode& node) {
	Connection c;
	c.output = node.key;
	c.separator = node.value.find('\x1B') != std::string::npos ? '\x1B' : ',';
	std::vector<std::string> parts;
	std::size_t start = 0;
	while (true) {
		const auto end = node.value.find(c.separator, start);
		parts.push_back(node.value.substr(start, end == std::string::npos ? std::string::npos : end - start));
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	parts.resize(std::max<std::size_t>(parts.size(), 5));
	c.target = parts[0];
	c.input = parts[1];
	c.parameter = parts[2];
	parseDouble(parts[3], c.delay);
	c.timesToFire = parseInt(parts[4], -1);
	return c;
}

Entity parseEntity(const KvNode& node) {
	Entity entity;
	for (const auto& child : node.children) {
		if (!child.block) {
			if (iequals(child.key, "id")) {
				entity.id = parseInt(child.value);
			} else {
				entity.keyValues.emplace_back(child.key, child.value);
			}
		} else if (iequals(child.key, "solid")) {
			entity.solids.push_back(parseSolid(child));
		} else if (iequals(child.key, "connections")) {
			for (const auto& output : child.children) {
				if (!output.block) {
					entity.connections.push_back(parseConnection(output));
				}
			}
		} else {
			entity.extra.push_back(child);
		}
	}
	return entity;
}

KvNode sideNode(const Side& side) {
	auto node = KvNode::makeBlock("side");
	node.children.push_back(KvNode::makeValue("id", std::to_string(side.id)));
	node.children.push_back(KvNode::makeValue("plane", "(" + formatVec3(side.planePoints[0]) + ") (" + formatVec3(side.planePoints[1]) +
	                                                       ") (" + formatVec3(side.planePoints[2]) + ")"));
	node.children.push_back(KvNode::makeValue("material", side.material));
	node.children.push_back(KvNode::makeValue("uaxis", formatAxis(side.u)));
	node.children.push_back(KvNode::makeValue("vaxis", formatAxis(side.v)));
	node.children.push_back(KvNode::makeValue("rotation", formatNumber(side.rotation)));
	node.children.push_back(KvNode::makeValue("lightmapscale", std::to_string(side.lightmapScale)));
	node.children.push_back(KvNode::makeValue("smoothing_groups", std::to_string(side.smoothingGroups)));
	for (const auto& extra : side.extra) {
		node.children.push_back(extra);
	}
	return node;
}

KvNode solidNode(const Solid& solid) {
	auto node = KvNode::makeBlock("solid");
	node.children.push_back(KvNode::makeValue("id", std::to_string(solid.id)));
	for (const auto& side : solid.sides) {
		node.children.push_back(sideNode(side));
	}
	for (const auto& extra : solid.extra) {
		node.children.push_back(extra);
	}
	return node;
}

KvNode entityNode(const Entity& entity, std::string_view blockName) {
	auto node = KvNode::makeBlock(std::string(blockName));
	node.children.push_back(KvNode::makeValue("id", std::to_string(entity.id)));
	for (const auto& [key, value] : entity.keyValues) {
		node.children.push_back(KvNode::makeValue(key, value));
	}
	if (!entity.connections.empty()) {
		auto connections = KvNode::makeBlock("connections");
		for (const auto& c : entity.connections) {
			const std::string sep(1, c.separator);
			connections.children.push_back(KvNode::makeValue(
				c.output, c.target + sep + c.input + sep + c.parameter + sep + formatNumber(c.delay) + sep + std::to_string(c.timesToFire)));
		}
		node.children.push_back(std::move(connections));
	}
	for (const auto& solid : entity.solids) {
		node.children.push_back(solidNode(solid));
	}
	for (const auto& extra : entity.extra) {
		node.children.push_back(extra);
	}
	return node;
}

void scanIds(const KvNode& node, int& maxSolid, int& maxSide, int& maxEntity) {
	for (const auto& child : node.children) {
		if (!child.block) {
			continue;
		}
		const int id = parseInt(child.get("id"));
		if (iequals(child.key, "solid")) {
			maxSolid = std::max(maxSolid, id);
		} else if (iequals(child.key, "side")) {
			maxSide = std::max(maxSide, id);
		} else if (iequals(child.key, "entity")) {
			maxEntity = std::max(maxEntity, id);
		}
		scanIds(child, maxSolid, maxSide, maxEntity);
	}
}

} // namespace

std::string formatNumber(double value) {
	if (value == 0) {
		return "0"; // also turns -0 into 0
	}
	char buffer[64];
	const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
	return std::string(buffer, result.ptr);
}

bool parseVec3(std::string_view text, Vec3& out) {
	text = trim(text);
	if (text.starts_with('(') && text.ends_with(')')) {
		text = text.substr(1, text.size() - 2);
	}
	const auto parts = splitTrimmed(text, ' ');
	return parts.size() == 3 && parseDouble(parts[0], out.x) && parseDouble(parts[1], out.y) && parseDouble(parts[2], out.z);
}

std::string formatVec3(const Vec3& v) {
	return formatNumber(v.x) + " " + formatNumber(v.y) + " " + formatNumber(v.z);
}

Plane Side::plane() const {
	const auto normal = normalize(cross(planePoints[0] - planePoints[1], planePoints[2] - planePoints[1]));
	return {normal, dot(normal, planePoints[0])};
}

const KvNode* Side::dispInfo() const {
	for (const auto& node : extra) {
		if (node.block && iequals(node.key, "dispinfo")) {
			return &node;
		}
	}
	return nullptr;
}

std::string Entity::get(std::string_view key, std::string_view fallback) const {
	for (const auto& [k, v] : keyValues) {
		if (iequals(k, key)) {
			return v;
		}
	}
	return std::string(fallback);
}

void Entity::set(std::string_view key, std::string value) {
	for (auto& [k, v] : keyValues) {
		if (iequals(k, key)) {
			v = std::move(value);
			return;
		}
	}
	keyValues.emplace_back(std::string(key), std::move(value));
}

void Entity::erase(std::string_view key) {
	std::erase_if(keyValues, [key](const auto& kv) { return iequals(kv.first, key); });
}

bool Entity::hasOrigin() const {
	Vec3 unused;
	return parseVec3(get("origin"), unused);
}

Vec3 Entity::origin() const {
	Vec3 out;
	if (!parseVec3(get("origin"), out)) {
		return {};
	}
	return out;
}

void Entity::setOrigin(const Vec3& origin) {
	set("origin", formatVec3(origin));
}

MapDocument::MapDocument() {
	world.id = 1;
	world.keyValues = {{"mapversion", "1"}, {"classname", "worldspawn"}, {"skyname", "sky_day01_01"},
	                   {"maxpropscreenwidth", "-1"}, {"detailvbsp", "detail.vbsp"}, {"detailmaterial", "detail/detailsprites"}};
	auto version = KvNode::makeBlock("versioninfo");
	version.set("editorversion", "400");
	version.set("editorbuild", "8000");
	version.set("mapversion", "1");
	version.set("formatversion", "100");
	version.set("prefab", "0");
	headerBlocks.push_back(std::move(version));
	headerBlocks.push_back(KvNode::makeBlock("visgroups"));
	auto view = KvNode::makeBlock("viewsettings");
	view.set("bSnapToGrid", "1");
	view.set("bShowGrid", "1");
	view.set("bShowLogicalGrid", "0");
	view.set("nGridSpacing", "64");
	view.set("bShow3DGrid", "0");
	headerBlocks.push_back(std::move(view));
}

MapDocument MapDocument::parse(std::string_view vmfText) {
	const auto root = parseKeyValues(vmfText);
	MapDocument doc;
	doc.headerBlocks.clear();
	doc.world = {};
	bool seenWorld = false;
	// Blocks before the world (versioninfo, visgroups, viewsettings, ...) are header blocks, everything
	// after the entities (cameras, cordons, unknown blocks) footer blocks; serialize() writes them back
	// in the same places, so nothing the editor does not understand is lost.
	for (const auto& node : root.children) {
		if (!node.block) {
			doc.footerBlocks.push_back(node);
		} else if (iequals(node.key, "world")) {
			doc.world = parseEntity(node);
			seenWorld = true;
		} else if (iequals(node.key, "entity")) {
			doc.entities.push_back(parseEntity(node));
		} else if (!seenWorld && doc.entities.empty()) {
			doc.headerBlocks.push_back(node);
		} else {
			doc.footerBlocks.push_back(node);
		}
	}
	if (!seenWorld) {
		throw std::runtime_error("not a VMF map: no world block");
	}
	doc.updateIdCounters();
	return doc;
}

std::string MapDocument::serialize() const {
	std::string out;
	out.reserve(1 << 20);
	for (const auto& block : headerBlocks) {
		writeKeyValueNode(out, block, 0);
	}
	writeKeyValueNode(out, entityNode(world, "world"), 0);
	for (const auto& entity : entities) {
		writeKeyValueNode(out, entityNode(entity, "entity"), 0);
	}
	for (const auto& block : footerBlocks) {
		writeKeyValueNode(out, block, 0);
	}
	return out;
}

// Finds the highest ids in use (also inside unknown blocks) so new objects get fresh ones. Brushes
// and entities share one counter, sides have their own.
void MapDocument::updateIdCounters() {
	int maxSolid = 0, maxSide = 0, maxEntity = std::max(1, world.id);
	const auto scanEntity = [&](const Entity& entity) {
		maxEntity = std::max(maxEntity, entity.id);
		for (const auto& solid : entity.solids) {
			maxSolid = std::max(maxSolid, solid.id);
			for (const auto& side : solid.sides) {
				maxSide = std::max(maxSide, side.id);
			}
		}
		for (const auto& extra : entity.extra) {
			scanIds(extra, maxSolid, maxSide, maxEntity);
		}
	};
	scanEntity(world);
	for (const auto& entity : entities) {
		scanEntity(entity);
	}
	for (const auto& block : footerBlocks) {
		scanIds(block, maxSolid, maxSide, maxEntity);
	}
	m_maxObjectId = std::max(maxSolid, maxEntity);
	m_maxSideId = maxSide;
}

Entity* MapDocument::findEntity(int entityId) {
	if (entityId == 0) {
		return &world;
	}
	for (auto& entity : entities) {
		if (entity.id == entityId) {
			return &entity;
		}
	}
	return nullptr;
}

const Entity* MapDocument::findEntity(int entityId) const {
	return const_cast<MapDocument*>(this)->findEntity(entityId);
}

Solid* MapDocument::findSolid(const SolidRef& ref) {
	auto* entity = findEntity(ref.entityId);
	if (!entity) {
		return nullptr;
	}
	for (auto& solid : entity->solids) {
		if (solid.id == ref.solidId) {
			return &solid;
		}
	}
	return nullptr;
}

const Solid* MapDocument::findSolid(const SolidRef& ref) const {
	return const_cast<MapDocument*>(this)->findSolid(ref);
}

std::size_t MapDocument::solidCount() const {
	std::size_t count = world.solids.size();
	for (const auto& entity : entities) {
		count += entity.solids.size();
	}
	return count;
}

} // namespace sme
