// map.cpp - VMF parsing and writing (see map.h).

#include <sme/map.h>

#include <sme/text.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
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

KvNode sideNode(const Side& side);
KvNode solidNode(const Solid& solid);
KvNode entityNode(const Entity& entity, std::string_view blockName);
std::string formatAxis(const TextureAxis& axis);
bool isCorners(const KvNode& node);

// Keys of a side the editor reads into Side's members.
enum class SideKey { None, Id, Plane, Material, UAxis, VAxis, Rotation, LightmapScale, Smoothing };

SideKey sideKey(std::string_view key) {
	if (iequals(key, "id")) return SideKey::Id;
	if (iequals(key, "plane")) return SideKey::Plane;
	if (iequals(key, "material")) return SideKey::Material;
	if (iequals(key, "uaxis")) return SideKey::UAxis;
	if (iequals(key, "vaxis")) return SideKey::VAxis;
	if (iequals(key, "rotation")) return SideKey::Rotation;
	if (iequals(key, "lightmapscale")) return SideKey::LightmapScale;
	if (iequals(key, "smoothing_groups")) return SideKey::Smoothing;
	return SideKey::None;
}

Side parseSide(const KvNode& node) {
	Side side;
	bool hasPlane = false;
	bool seen[9] = {};
	Layout layout;
	layout.reserve(node.children.size());
	for (const auto& child : node.children) {
		const SideKey key = child.block ? SideKey::None : sideKey(child.key);
		bool typed = key != SideKey::None && !seen[static_cast<int>(key)];
		if (typed) {
			// A value that does not read as its type stays as it was (kept in extra).
			switch (key) {
				case SideKey::Id: side.id = parseInt(child.value); break;
				case SideKey::Plane: hasPlane = parsePlane(child.value, side.planePoints); typed = hasPlane; break;
				case SideKey::Material: side.material = child.value; break;
				case SideKey::UAxis: typed = parseAxis(child.value, side.u); break;
				case SideKey::VAxis: typed = parseAxis(child.value, side.v); break;
				case SideKey::Rotation: typed = parseDouble(child.value, side.rotation); break;
				case SideKey::LightmapScale: side.lightmapScale = parseInt(child.value, 16); break;
				case SideKey::Smoothing: side.smoothingGroups = parseInt(child.value); break;
				case SideKey::None: break;
			}
		}
		if (typed) {
			seen[static_cast<int>(key)] = true;
			layout.push_back({LayoutEntry::Kind::Value, child.key, child.value});
		} else {
			// Blocks (dispinfo, Hammer++'s vertices_plus), unknown keys and a second copy of a key.
			side.extra.push_back(child);
			layout.push_back({LayoutEntry::Kind::Extra, {}, {}});
		}
	}
	if (!hasPlane) {
		throw std::runtime_error("side " + std::to_string(side.id) + " has no valid plane");
	}
	// Only a side not written the way serialize() would write it keeps its layout (saves memory);
	// one with Hammer++'s corners always does (they stay only with the plane they belong to).
	if (std::any_of(side.extra.begin(), side.extra.end(), isCorners) || sideNode(side).children != node.children) {
		side.source = std::make_shared<const Layout>(std::move(layout));
	}
	return side;
}

/// Whether a solid's or entity's file layout is the one serialize() writes without it: the id
/// first (as written for that number), then `keys` keys, the child blocks in `childOrder` (all
/// `children` of the last kind; at most one of the others), then extras.
bool standardLayout(const Layout& layout, int id, std::size_t keys, std::initializer_list<std::string_view> childOrder, std::size_t children) {
	if (layout.empty() || layout[0].kind != LayoutEntry::Kind::Value || layout[0].key != "id" || layout[0].text != std::to_string(id)) {
		return false;
	}
	std::size_t i = 1;
	for (std::size_t k = 0; k < keys; ++k, ++i) {
		if (i >= layout.size() || layout[i].kind != LayoutEntry::Kind::Value) return false;
	}
	std::size_t childCount = 0;
	for (const auto name : childOrder) {
		const bool last = name == *(childOrder.end() - 1);
		std::size_t count = 0;
		while (i < layout.size() && layout[i].kind == LayoutEntry::Kind::Child && layout[i].key == name) {
			++i;
			++count;
		}
		if (!last && count > 1) return false;
		if (last) childCount = count;
	}
	if (childCount != children) return false;
	for (; i < layout.size(); ++i) {
		if (layout[i].kind != LayoutEntry::Kind::Extra) return false;
	}
	return true;
}

/// A brush, or nothing when one of its sides is broken (the rest of the map still loads).
std::optional<Solid> parseSolid(const KvNode& node, VmfLoadReport* report) {
	Solid solid;
	bool seenId = false;
	Layout layout;
	for (const auto& child : node.children) {
		if (!child.block && !seenId && iequals(child.key, "id")) {
			solid.id = parseInt(child.value);
			seenId = true;
			layout.push_back({LayoutEntry::Kind::Value, child.key, child.value});
		} else if (child.block && iequals(child.key, "side")) {
			try {
				solid.sides.push_back(parseSide(child));
				layout.push_back({LayoutEntry::Kind::Child, child.key, {}});
			} catch (const std::exception& e) {
				if (report) {
					++report->skippedSolids;
					report->notes.push_back("brush " + std::to_string(solid.id) + " left out: " + e.what());
				}
				return std::nullopt;
			}
		} else {
			solid.extra.push_back(child);
			layout.push_back({LayoutEntry::Kind::Extra, {}, {}});
		}
	}
	if (!standardLayout(layout, solid.id, 0, {"side"}, solid.sides.size())) {
		solid.source = std::make_shared<const Layout>(std::move(layout));
	}
	return solid;
}

/// A line `"key" "value"` whose value holds more quotes (`"message" "say "hi""`) breaks the
/// KeyValues syntax; the inner quotes become apostrophes. Other lines stay as they are.
std::string repairQuotes(std::string_view text, VmfLoadReport* report) {
	std::string out;
	out.reserve(text.size());
	std::size_t lineNumber = 0;
	std::size_t start = 0;
	while (start <= text.size()) {
		const auto end = text.find('\n', start);
		const std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
		++lineNumber;
		const auto quotes = std::count(line.begin(), line.end(), '"');
		const auto first = line.find_first_not_of(" \t");
		const auto last = line.find_last_not_of(" \t\r");
		bool repaired = false;
		if (quotes > 4 && first != std::string_view::npos && line[first] == '"' && line[last] == '"') {
			const auto keyEnd = line.find('"', first + 1);
			const auto valueStart = keyEnd == std::string_view::npos ? keyEnd : line.find('"', keyEnd + 1);
			if (valueStart != std::string_view::npos && valueStart < last) {
				std::string value(line.substr(valueStart + 1, last - valueStart - 1));
				std::replace(value.begin(), value.end(), '"', '\'');
				out.append(line.substr(0, valueStart + 1)).append(value).append(line.substr(last));
				repaired = true;
				if (report) {
					++report->repairedLines;
					report->notes.push_back("line " + std::to_string(lineNumber) + ": quotes inside the value of \"" +
					                        std::string(line.substr(first + 1, keyEnd - first - 1)) + "\" replaced by '");
				}
			}
		}
		if (!repaired) out.append(line);
		if (end == std::string_view::npos) break;
		out.push_back('\n');
		start = end + 1;
	}
	return out;
}

// An output line: key = output name, value = "target,input,parameter,delay,times to fire".
// Newer VMFs separate the fields with ESC (0x1B) instead of commas; the separator found is kept
// so a save writes the line back the same way. Missing fields get defaults (times to fire -1).
// The text as read is kept when writing it from the fields would read differently ("0.00" for
// a delay, a sixth field), so an output nobody changed is saved exactly as it was.
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
	c.sourceText = node.value;
	return c;
}

/// The fields of an output as one value, the way Hammer writes it.
std::string formatConnection(const Connection& c) {
	const std::string sep(1, c.separator);
	return c.target + sep + c.input + sep + c.parameter + sep + formatNumber(c.delay) + sep + std::to_string(c.timesToFire);
}

/// What an output is written as: its text from the file while the fields still say the same.
std::string connectionValue(const Connection& c) {
	if (!c.sourceText.empty()) {
		const Connection read = parseConnection(KvNode::makeValue(c.output, c.sourceText));
		if (read.target == c.target && read.input == c.input && read.parameter == c.parameter && read.delay == c.delay &&
		    read.timesToFire == c.timesToFire && read.separator == c.separator) {
			return c.sourceText;
		}
	}
	return formatConnection(c);
}

Entity parseEntity(const KvNode& node, VmfLoadReport* report = nullptr) {
	Entity entity;
	bool seenId = false, seenConnections = false;
	Layout layout;
	for (const auto& child : node.children) {
		if (!child.block) {
			if (!seenId && iequals(child.key, "id")) {
				entity.id = parseInt(child.value);
				seenId = true;
			} else {
				entity.keyValues.emplace_back(child.key, child.value);
			}
			layout.push_back({LayoutEntry::Kind::Value, child.key, child.value});
			continue;
		}
		const bool plainOutputs = std::none_of(child.children.begin(), child.children.end(), [](const KvNode& n) { return n.block; });
		if (iequals(child.key, "solid")) {
			if (auto solid = parseSolid(child, report)) {
				entity.solids.push_back(std::move(*solid));
				layout.push_back({LayoutEntry::Kind::Child, child.key, {}});
			}
		} else if (iequals(child.key, "connections") && !seenConnections && plainOutputs) {
			for (const auto& output : child.children) {
				entity.connections.push_back(parseConnection(output));
			}
			seenConnections = true;
			layout.push_back({LayoutEntry::Kind::Child, child.key, {}});
		} else {
			// hidden, editor, group and unknown blocks (also a second or odd "connections" block).
			entity.extra.push_back(child);
			layout.push_back({LayoutEntry::Kind::Extra, {}, {}});
		}
	}
	// An empty "connections" block is not written without a layout.
	if (!standardLayout(layout, entity.id, entity.keyValues.size(), {"connections", "solid"}, entity.solids.size()) ||
	    (seenConnections && entity.connections.empty())) {
		entity.source = std::make_shared<const Layout>(std::move(layout));
	}
	return entity;
}

/// Side's typed keys in Hammer's order, for keys a side's file layout did not have.
constexpr SideKey SIDE_ORDER[] = {SideKey::Id, SideKey::Plane, SideKey::Material, SideKey::UAxis, SideKey::VAxis,
                                  SideKey::Rotation, SideKey::LightmapScale, SideKey::Smoothing};

const char* sideKeyName(SideKey key) {
	switch (key) {
		case SideKey::Id: return "id";
		case SideKey::Plane: return "plane";
		case SideKey::Material: return "material";
		case SideKey::UAxis: return "uaxis";
		case SideKey::VAxis: return "vaxis";
		case SideKey::Rotation: return "rotation";
		case SideKey::LightmapScale: return "lightmapscale";
		case SideKey::Smoothing: return "smoothing_groups";
		case SideKey::None: break;
	}
	return "";
}

std::string formatPlane(const std::array<Vec3, 3>& points) {
	return "(" + formatVec3(points[0]) + ") (" + formatVec3(points[1]) + ") (" + formatVec3(points[2]) + ")";
}

/// A typed side value as text: `read` (from the file) while it still stands for the current value.
std::string sideValue(const Side& side, SideKey key, std::string_view read = {}) {
	const bool hasRead = !read.empty();
	switch (key) {
		case SideKey::Id:
			return hasRead && parseInt(read, -1) == side.id ? std::string(read) : std::to_string(side.id);
		case SideKey::Plane: {
			std::array<Vec3, 3> points{};
			return hasRead && parsePlane(read, points) && points == side.planePoints ? std::string(read) : formatPlane(side.planePoints);
		}
		case SideKey::Material:
			return side.material;
		case SideKey::UAxis:
		case SideKey::VAxis: {
			const TextureAxis& axis = key == SideKey::UAxis ? side.u : side.v;
			TextureAxis parsed;
			return hasRead && parseAxis(read, parsed) && parsed == axis ? std::string(read) : formatAxis(axis);
		}
		case SideKey::Rotation: {
			double value = 0;
			return hasRead && parseDouble(read, value) && value == side.rotation ? std::string(read) : formatNumber(side.rotation);
		}
		case SideKey::LightmapScale:
			return hasRead && parseInt(read, -1) == side.lightmapScale ? std::string(read) : std::to_string(side.lightmapScale);
		case SideKey::Smoothing:
			return hasRead && parseInt(read, -1) == side.smoothingGroups ? std::string(read) : std::to_string(side.smoothingGroups);
		case SideKey::None: break;
	}
	return {};
}

/// Whether a typed value differs from what a side without the key has (a key the file did
/// not have is only written once it means something).
bool sideValueSet(const Side& side, SideKey key) {
	static const Side defaults;
	switch (key) {
		case SideKey::Id: return side.id != defaults.id;
		case SideKey::Plane: return side.planePoints != defaults.planePoints;
		case SideKey::Material: return side.material != defaults.material;
		case SideKey::UAxis: return !(side.u == defaults.u);
		case SideKey::VAxis: return !(side.v == defaults.v);
		case SideKey::Rotation: return side.rotation != defaults.rotation;
		case SideKey::LightmapScale: return side.lightmapScale != defaults.lightmapScale;
		case SideKey::Smoothing: return side.smoothingGroups != defaults.smoothingGroups;
		case SideKey::None: break;
	}
	return false;
}

/// Hammer++ stores the exact corners of a face in a "vertices_plus" block. They hold only while
/// the plane is the one they were read with; a moved, cut or new face drops them (Hammer++ then
/// takes the corners from the plane, as for any VMF from Valve's Hammer).
bool keepsCorners(const Side& side) {
	if (!side.source) return false;
	for (const auto& entry : *side.source) {
		if (entry.kind == LayoutEntry::Kind::Value && sideKey(entry.key) == SideKey::Plane) {
			std::array<Vec3, 3> points{};
			return parsePlane(entry.text, points) && points == side.planePoints;
		}
	}
	return false;
}

bool isCorners(const KvNode& node) {
	return node.block && iequals(node.key, "vertices_plus");
}

KvNode sideNode(const Side& side) {
	auto node = KvNode::makeBlock("side");
	if (!side.source) {
		for (const SideKey key : SIDE_ORDER) {
			node.children.push_back(KvNode::makeValue(sideKeyName(key), sideValue(side, key)));
		}
		for (const auto& extra : side.extra) {
			if (!isCorners(extra)) node.children.push_back(extra);
		}
		return node;
	}
	const bool corners = keepsCorners(side);
	// The file's order and text; a typed key the file did not have comes after the last one it
	// had (unless it still has a side's default value), extras added since then at the end.
	bool written[9] = {};
	std::size_t nextExtra = 0;
	std::vector<KvNode> missing;
	for (const SideKey key : SIDE_ORDER) {
		const bool inFile = std::any_of(side.source->begin(), side.source->end(), [key](const LayoutEntry& e) {
			return e.kind == LayoutEntry::Kind::Value && sideKey(e.key) == key;
		});
		if (!inFile && sideValueSet(side, key)) missing.push_back(KvNode::makeValue(sideKeyName(key), sideValue(side, key)));
	}
	const auto lastValue = std::find_if(side.source->rbegin(), side.source->rend(), [](const LayoutEntry& e) {
		return e.kind == LayoutEntry::Kind::Value;
	});
	const std::size_t flushAt = lastValue == side.source->rend() ? 0 : std::size_t(side.source->rend() - lastValue);
	const auto flushMissing = [&] {
		for (auto& value : missing) node.children.push_back(std::move(value));
		missing.clear();
	};
	// A typed value written now replaces an unreadable copy that was kept as an extra.
	const auto replacedByTyped = [&](const KvNode& extra) {
		if (extra.block) return false;
		const SideKey key = sideKey(extra.key);
		return key != SideKey::None && !written[static_cast<int>(key)] &&
		       std::none_of(side.source->begin(), side.source->end(), [key](const LayoutEntry& e) {
			       return e.kind == LayoutEntry::Kind::Value && sideKey(e.key) == key;
		       }) && sideValueSet(side, key) && key != SideKey::Id;
	};
	if (flushAt == 0) flushMissing();
	for (std::size_t i = 0; i < side.source->size(); ++i) {
		const auto& entry = (*side.source)[i];
		if (entry.kind == LayoutEntry::Kind::Value) {
			const SideKey key = sideKey(entry.key);
			node.children.push_back(KvNode::makeValue(entry.key, sideValue(side, key, entry.text)));
			written[static_cast<int>(key)] = true;
		} else if (nextExtra < side.extra.size()) {
			const auto& extra = side.extra[nextExtra++];
			if (!replacedByTyped(extra) && (corners || !isCorners(extra))) node.children.push_back(extra);
		}
		if (i + 1 == flushAt) flushMissing();
	}
	flushMissing();
	for (; nextExtra < side.extra.size(); ++nextExtra) {
		const auto& extra = side.extra[nextExtra];
		if (!replacedByTyped(extra) && (corners || !isCorners(extra))) node.children.push_back(extra);
	}
	return node;
}

KvNode solidNode(const Solid& solid) {
	auto node = KvNode::makeBlock("solid");
	const auto idValue = [&](std::string_view read) {
		return !read.empty() && parseInt(read, -1) == solid.id ? std::string(read) : std::to_string(solid.id);
	};
	if (!solid.source) {
		node.children.push_back(KvNode::makeValue("id", idValue({})));
		for (const auto& side : solid.sides) {
			node.children.push_back(sideNode(side));
		}
		for (const auto& extra : solid.extra) {
			node.children.push_back(extra);
		}
		return node;
	}
	// The file's order; sides and extras fill their places in turn, new ones follow the last of
	// their kind (sides after the last side, extras at the end).
	std::size_t nextSide = 0, nextExtra = 0;
	std::size_t lastSide = solid.source->size();
	for (std::size_t i = 0; i < solid.source->size(); ++i) {
		if ((*solid.source)[i].kind == LayoutEntry::Kind::Child) lastSide = i;
	}
	if (std::none_of(solid.source->begin(), solid.source->end(), [](const LayoutEntry& e) { return e.kind == LayoutEntry::Kind::Value; })) {
		node.children.push_back(KvNode::makeValue("id", idValue({})));
	}
	for (std::size_t i = 0; i < solid.source->size(); ++i) {
		const auto& entry = (*solid.source)[i];
		if (entry.kind == LayoutEntry::Kind::Value) {
			node.children.push_back(KvNode::makeValue(entry.key, idValue(entry.text)));
		} else if (entry.kind == LayoutEntry::Kind::Child) {
			if (nextSide < solid.sides.size()) node.children.push_back(sideNode(solid.sides[nextSide++]));
		} else if (nextExtra < solid.extra.size()) {
			node.children.push_back(solid.extra[nextExtra++]);
		}
		if (i == lastSide) {
			for (; nextSide < solid.sides.size(); ++nextSide) node.children.push_back(sideNode(solid.sides[nextSide]));
		}
	}
	for (; nextSide < solid.sides.size(); ++nextSide) node.children.push_back(sideNode(solid.sides[nextSide]));
	for (; nextExtra < solid.extra.size(); ++nextExtra) node.children.push_back(solid.extra[nextExtra]);
	return node;
}

KvNode connectionsNode(const Entity& entity) {
	auto connections = KvNode::makeBlock("connections");
	for (const auto& c : entity.connections) {
		connections.children.push_back(KvNode::makeValue(c.output, connectionValue(c)));
	}
	return connections;
}

KvNode entityNode(const Entity& entity, std::string_view blockName) {
	auto node = KvNode::makeBlock(std::string(blockName));
	const auto idValue = [&](std::string_view read) {
		return !read.empty() && parseInt(read, -1) == entity.id ? std::string(read) : std::to_string(entity.id);
	};
	if (!entity.source) {
		node.children.push_back(KvNode::makeValue("id", idValue({})));
		for (const auto& [key, value] : entity.keyValues) {
			node.children.push_back(KvNode::makeValue(key, value));
		}
		if (!entity.connections.empty()) {
			node.children.push_back(connectionsNode(entity));
		}
		for (const auto& solid : entity.solids) {
			node.children.push_back(solidNode(solid));
		}
		for (const auto& extra : entity.extra) {
			node.children.push_back(extra);
		}
		return node;
	}
	// The file's order. Keys are found by name (a key removed since is left out); keys added since
	// follow the last key of the file, outputs added to an entity without any after the keys,
	// brushes fill the places of brushes in turn (new ones after the last), extras likewise.
	const Layout& layout = *entity.source;
	std::vector<bool> keyWritten(entity.keyValues.size(), false);
	std::size_t nextSolid = 0, nextExtra = 0;
	bool wroteId = false, wroteConnections = false;
	std::size_t lastValue = layout.size(), lastSolid = layout.size(), connectionsAt = layout.size();
	for (std::size_t i = 0; i < layout.size(); ++i) {
		const auto& e = layout[i];
		if (e.kind == LayoutEntry::Kind::Value) lastValue = i;
		if (e.kind == LayoutEntry::Kind::Child && iequals(e.key, "solid")) lastSolid = i;
		if (e.kind == LayoutEntry::Kind::Child && iequals(e.key, "connections")) connectionsAt = i;
	}
	const bool hasConnections = connectionsAt != layout.size();
	// Brushes added to an entity that had none follow its outputs (or its keys), as Hammer writes them.
	const std::size_t solidsAt = lastSolid != layout.size() ? lastSolid : hasConnections ? connectionsAt : lastValue;
	const bool hasId = std::any_of(layout.begin(), layout.end(), [](const LayoutEntry& e) {
		return e.kind == LayoutEntry::Kind::Value && iequals(e.key, "id");
	});
	if (!hasId) {
		node.children.push_back(KvNode::makeValue("id", idValue({})));
		wroteId = true;
	}
	const auto flushKeys = [&] {
		for (std::size_t k = 0; k < entity.keyValues.size(); ++k) {
			if (!keyWritten[k]) {
				node.children.push_back(KvNode::makeValue(entity.keyValues[k].first, entity.keyValues[k].second));
				keyWritten[k] = true;
			}
		}
		if (!hasConnections && !wroteConnections && !entity.connections.empty()) {
			node.children.push_back(connectionsNode(entity));
			wroteConnections = true;
		}
	};
	const auto flushSolids = [&] {
		for (; nextSolid < entity.solids.size(); ++nextSolid) node.children.push_back(solidNode(entity.solids[nextSolid]));
	};
	if (lastValue == layout.size()) flushKeys();
	for (std::size_t i = 0; i < layout.size(); ++i) {
		const auto& e = layout[i];
		if (e.kind == LayoutEntry::Kind::Value) {
			if (!wroteId && iequals(e.key, "id")) {
				node.children.push_back(KvNode::makeValue(e.key, idValue(e.text)));
				wroteId = true;
			} else {
				for (std::size_t k = 0; k < entity.keyValues.size(); ++k) {
					if (!keyWritten[k] && iequals(entity.keyValues[k].first, e.key)) {
						node.children.push_back(KvNode::makeValue(entity.keyValues[k].first, entity.keyValues[k].second));
						keyWritten[k] = true;
						break;
					}
				}
			}
		} else if (e.kind == LayoutEntry::Kind::Child && iequals(e.key, "connections")) {
			if (!wroteConnections) {
				node.children.push_back(connectionsNode(entity));
				wroteConnections = true;
			}
		} else if (e.kind == LayoutEntry::Kind::Child) {
			if (nextSolid < entity.solids.size()) node.children.push_back(solidNode(entity.solids[nextSolid++]));
		} else if (nextExtra < entity.extra.size()) {
			node.children.push_back(entity.extra[nextExtra++]);
		}
		if (i == lastValue) flushKeys();
		if (i == solidsAt) flushSolids();
	}
	flushKeys();
	flushSolids();
	for (; nextExtra < entity.extra.size(); ++nextExtra) node.children.push_back(entity.extra[nextExtra]);
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

MapDocument MapDocument::parse(std::string_view vmfText, VmfLoadReport* report) {
	VmfLoadReport local;
	VmfLoadReport* const r = report ? report : &local;
	const auto root = parseKeyValues(repairQuotes(vmfText, r));
	MapDocument doc;
	doc.headerBlocks.clear();
	doc.world = {};
	// How the file was written, so a save writes it the same way (Hammer on Windows: CRLF).
	doc.byteOrderMark = vmfText.starts_with("\xEF\xBB\xBF");
	if (const auto newline = vmfText.find('\n'); newline != std::string_view::npos) {
		doc.lineEnding = newline > 0 && vmfText[newline - 1] == '\r' ? "\r\n" : "\n";
	}
	const auto lastText = vmfText.find_last_not_of(" \t");
	doc.finalNewline = lastText == std::string_view::npos || vmfText[lastText] == '\n';
	bool seenWorld = false;
	Layout layout;
	// Blocks before the world (versioninfo, visgroups, viewsettings, ...) are header blocks, everything
	// else that is not an entity (cameras, cordons, hidden entities, unknown blocks) footer blocks; the
	// layout remembers where each stood, so serialize() writes them back in the same places.
	for (const auto& node : root.children) {
		if (node.block && iequals(node.key, "world") && !seenWorld) {
			doc.world = parseEntity(node, r);
			seenWorld = true;
			layout.push_back({LayoutEntry::Kind::Child, node.key, {}});
		} else if (node.block && iequals(node.key, "entity")) {
			doc.entities.push_back(parseEntity(node, r));
			layout.push_back({LayoutEntry::Kind::Child, node.key, {}});
		} else if (node.block && !seenWorld && doc.entities.empty()) {
			doc.headerBlocks.push_back(node);
		} else {
			doc.footerBlocks.push_back(node);
			layout.push_back({LayoutEntry::Kind::Extra, {}, {}});
		}
	}
	if (!seenWorld) {
		throw std::runtime_error("not a VMF map: no world block");
	}
	doc.m_layout = std::make_shared<const Layout>(std::move(layout));
	doc.updateIdCounters();
	// Brushes and entities are found by their id while editing: one without an id, or with an id
	// another one has too (written by tools, not by Hammer), gets a new one.
	std::set<int> solidIds, entityIds;
	const auto fixSolids = [&](Entity& entity) {
		for (auto& solid : entity.solids) {
			if (solid.id > 0 && solidIds.insert(solid.id).second) continue;
			const int old = solid.id;
			solid.id = doc.newSolidId();
			solidIds.insert(solid.id);
			++r->renumbered;
			r->notes.push_back("brush id " + std::to_string(old) + (old > 0 ? " was used twice" : " was missing") + ": now " +
			                   std::to_string(solid.id));
		}
	};
	fixSolids(doc.world);
	for (auto& entity : doc.entities) {
		fixSolids(entity);
		if (entity.id > 0 && entity.id != doc.world.id && entityIds.insert(entity.id).second) continue;
		const int old = entity.id;
		entity.id = doc.newEntityId();
		entityIds.insert(entity.id);
		++r->renumbered;
		r->notes.push_back("entity id " + std::to_string(old) + (old > 0 ? " was used twice" : " was missing") + ": now " +
		                   std::to_string(entity.id));
	}
	return doc;
}

std::string MapDocument::serialize() const {
	std::string out;
	out.reserve(1 << 20);
	if (byteOrderMark) out += "\xEF\xBB\xBF";
	for (const auto& block : headerBlocks) {
		writeKeyValueNode(out, block, 0);
	}
	// In the file's order; entities added since follow the last entity, footer blocks added since
	// come at the end (a new map: world, entities, footer blocks).
	std::size_t nextEntity = 0, nextFooter = 0;
	bool wroteWorld = false;
	const auto writeRemainingEntities = [&] {
		for (; nextEntity < entities.size(); ++nextEntity) writeKeyValueNode(out, entityNode(entities[nextEntity], "entity"), 0);
	};
	if (m_layout) {
		std::size_t lastEntity = m_layout->size();
		for (std::size_t i = 0; i < m_layout->size(); ++i) {
			const auto& e = (*m_layout)[i];
			if (e.kind == LayoutEntry::Kind::Child && iequals(e.key, "entity")) lastEntity = i;
		}
		for (std::size_t i = 0; i < m_layout->size(); ++i) {
			const auto& e = (*m_layout)[i];
			if (e.kind == LayoutEntry::Kind::Child && iequals(e.key, "world")) {
				writeKeyValueNode(out, entityNode(world, e.key), 0);
				wroteWorld = true;
			} else if (e.kind == LayoutEntry::Kind::Child) {
				if (nextEntity < entities.size()) writeKeyValueNode(out, entityNode(entities[nextEntity++], e.key), 0);
			} else if (nextFooter < footerBlocks.size()) {
				writeKeyValueNode(out, footerBlocks[nextFooter++], 0);
			}
			if (i == lastEntity || (lastEntity == m_layout->size() && wroteWorld && i + 1 < m_layout->size() &&
			                        (*m_layout)[i].kind == LayoutEntry::Kind::Child && iequals(e.key, "world"))) {
				writeRemainingEntities();
			}
		}
	}
	if (!wroteWorld) {
		writeKeyValueNode(out, entityNode(world, "world"), 0);
	}
	writeRemainingEntities();
	for (; nextFooter < footerBlocks.size(); ++nextFooter) {
		writeKeyValueNode(out, footerBlocks[nextFooter], 0);
	}
	if (!finalNewline && out.ends_with('\n')) out.pop_back();
	if (lineEnding == "\n") return out;
	// "\r\n": every line break that is not one already (a value spanning lines keeps its own).
	std::string converted;
	converted.reserve(out.size() + out.size() / 16);
	for (std::size_t i = 0; i < out.size(); ++i) {
		if (out[i] == '\n' && (i == 0 || out[i - 1] != '\r')) converted += '\r';
		converted += out[i];
	}
	return converted;
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

namespace {

/// "solid #2 (id 5)": a block's name, its place among blocks of that name and its id.
std::string blockLabel(const KvNode& node, int index) {
	std::string label = node.key + " #" + std::to_string(index);
	if (const auto id = node.get("id"); !id.empty()) label += " (id " + id + ")";
	return label;
}

std::string shortText(const std::string& text) {
	std::string out;
	for (const char c : text.size() > 80 ? text.substr(0, 77) + "..." : text) {
		out += c == '\x1B' ? std::string("<ESC>") : std::string(1, c);
	}
	return "\"" + out + "\"";
}

void compareChildren(const KvNode& a, const KvNode& b, const std::string& path, std::vector<std::string>& out, std::size_t limit) {
	std::map<std::string, int> counts;
	const std::size_t common = std::min(a.children.size(), b.children.size());
	for (std::size_t i = 0; i < common && out.size() < limit; ++i) {
		const auto& x = a.children[i];
		const auto& y = b.children[i];
		const int index = ++counts[toLower(x.key)];
		const std::string here = (path.empty() ? "" : path + " > ") + (x.block ? blockLabel(x, index) : x.key);
		if (x.block != y.block || x.key != y.key) {
			out.push_back(here + ": became " + (y.block ? "block " : "key ") + y.key);
			return;   // everything after this point is shifted; one line says enough
		}
		if (x.block) {
			compareChildren(x, y, here, out, limit);
		} else if (x.value != y.value) {
			out.push_back(here + ": " + shortText(x.value) + " -> " + shortText(y.value));
		}
	}
	if (out.size() >= limit) return;
	const std::string where = path.empty() ? std::string("top level") : path;
	if (a.children.size() > common) {
		out.push_back(where + ": " + std::to_string(a.children.size() - common) + " entries missing, first: " + a.children[common].key);
	} else if (b.children.size() > common) {
		out.push_back(where + ": " + std::to_string(b.children.size() - common) + " entries added, first: " + b.children[common].key);
	}
}

} // namespace

std::vector<std::string> keyValueDifferences(std::string_view before, std::string_view after, std::size_t limit) {
	std::vector<std::string> out;
	compareChildren(parseKeyValues(before), parseKeyValues(after), {}, out, limit);
	return out;
}

} // namespace sme
