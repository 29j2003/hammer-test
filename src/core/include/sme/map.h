// map.h - The map document: world brushes, entities and everything else in a .vmf file.
//
// Fields the editor works with are parsed into typed members; everything else (editor
// blocks, displacement data, visgroups, cameras, cordons, unknown keys) is kept as
// KeyValues and written back unchanged, so opening and saving a map never loses data.
// Each object also remembers how it stood in the file (`source`: key order and the exact
// text of its values), so what was not edited is written back byte for byte - the same
// numbers, order, line endings - and a diff of the saved file shows only real changes.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sme/keyvalues.h>
#include <sme/math.h>

namespace sme {

/// Texture projection axis: "[x y z shift] scale".
struct TextureAxis {
	Vec3 axis;
	double shift = 0;
	double scale = 0.25;
	bool operator==(const TextureAxis&) const = default;
};

/// One entry of a block as it stood in the file: a key with its text, a typed child block
/// (a side of a solid, a solid or the outputs of an entity) or the next entry kept in `extra`.
struct LayoutEntry {
	enum class Kind : std::uint8_t { Value, Child, Extra };
	Kind kind = Kind::Value;
	std::string key;     ///< Spelled as in the file (Value, Child)
	std::string text;    ///< The value's text as read (Value)
};
/// A block's entries in file order (see LayoutEntry); shared by copies, never changed.
using Layout = std::vector<LayoutEntry>;

/// One face of a brush.
struct Side {
	int id = 0;
	std::array<Vec3, 3> planePoints{};   ///< As stored in the VMF; normal = cross(p0 - p1, p2 - p1) points outwards
	std::string material;                ///< As stored (Hammer writes upper case, e.g. "TOOLS/TOOLSNODRAW")
	TextureAxis u{{1, 0, 0}};
	TextureAxis v{{0, -1, 0}};
	double rotation = 0;
	int lightmapScale = 16;
	int smoothingGroups = 0;
	std::vector<KvNode> extra;           ///< dispinfo and unknown entries, in file order
	std::shared_ptr<const Layout> source;   ///< As read from the file (null for new sides)

	/// The plane through the three plane points (normal points out of the brush).
	[[nodiscard]] Plane plane() const;
	/// The side's dispinfo block, or nullptr if it is not a displacement.
	[[nodiscard]] const KvNode* dispInfo() const;
};

/// A brush: its sides plus everything else of its VMF block.
struct Solid {
	int id = 0;
	std::vector<Side> sides;
	std::vector<KvNode> extra;           ///< editor block (color, visgroups) and unknown entries
	std::shared_ptr<const Layout> source;   ///< As read from the file (null for new brushes)
};

/// One entity output: "OnTrigger" -> "target,Input,param,delay,times".
struct Connection {
	std::string output;
	std::string target;
	std::string input;
	std::string parameter;
	double delay = 0;
	int timesToFire = -1;
	char separator = ',';                ///< ',' (Source 2013) or '\x1B' (newer branches)
	std::string sourceText;              ///< The value as read; written again while the fields still match it
};

/// An entity with its keys, outputs and (for brush entities) brushes. The world is an Entity too.
struct Entity {
	int id = 0;
	std::vector<std::pair<std::string, std::string>> keyValues;   ///< Ordered, including classname/origin
	std::vector<Connection> connections;
	std::vector<Solid> solids;
	std::vector<KvNode> extra;           ///< hidden solids, editor block, unknown blocks
	std::shared_ptr<const Layout> source;   ///< As read from the file (null for new entities)

	/// Value of a key (case-insensitive), or `fallback`.
	[[nodiscard]] std::string get(std::string_view key, std::string_view fallback = {}) const;
	/// Sets a key (case-insensitive; keeps its place if it exists, else appends it).
	void set(std::string_view key, std::string value);
	/// Removes a key.
	void erase(std::string_view key);
	[[nodiscard]] std::string classname() const { return get("classname"); }
	/// Whether it has an "origin" key (brush entities usually do not).
	[[nodiscard]] bool hasOrigin() const;
	/// The "origin" key as a vector (0 0 0 if missing).
	[[nodiscard]] Vec3 origin() const;
	/// Writes the "origin" key.
	void setOrigin(const Vec3& origin);
};

/// Where a solid lives: in the world (entityId == 0) or in a brush entity.
struct SolidRef {
	int entityId = 0;
	int solidId = 0;
	constexpr bool operator==(const SolidRef&) const = default;
};

/// What loading a damaged VMF had to repair or leave out (Hammer often cannot open such files).
struct VmfLoadReport {
	int repairedLines = 0;      ///< Lines with stray quotes inside a value (the quotes became ')
	int skippedSolids = 0;      ///< Brushes with a side that has no valid plane
	int renumbered = 0;         ///< Brushes/entities whose id was missing or used twice (they got new ones)
	std::vector<std::string> notes;   ///< One line per repair, for the log
	[[nodiscard]] bool clean() const { return repairedLines == 0 && skippedSolids == 0 && renumbered == 0; }
};

/// A whole VMF map: world, entities and the other blocks, kept so a save writes back what was read.
class MapDocument {
public:
	MapDocument();

	/// Parses VMF text. Damage that can be contained is repaired or left out and listed in
	/// `report`: quotes inside a value (a typed `"` breaks Hammer's files) and brushes with a
	/// broken side. Throws std::runtime_error when the structure itself is broken.
	[[nodiscard]] static MapDocument parse(std::string_view vmfText, VmfLoadReport* report = nullptr);
	/// Writes VMF text.
	[[nodiscard]] std::string serialize() const;

	Entity world;                        ///< classname "worldspawn", id 1 by convention
	std::vector<Entity> entities;
	std::vector<KvNode> headerBlocks;    ///< versioninfo, visgroups, viewsettings (before world)
	std::vector<KvNode> footerBlocks;    ///< cameras, cordon(s), hidden entities and unknown blocks (after the world)
	std::string lineEnding = "\n";       ///< As in the file ("\r\n" from Hammer on Windows)
	bool byteOrderMark = false;          ///< The file started with a UTF-8 byte order mark
	bool finalNewline = true;            ///< The file ended with a line break

	// Hammer numbers solids and entities from one counter and sides from another.
	[[nodiscard]] int newSolidId() { return ++m_maxObjectId; }
	[[nodiscard]] int newEntityId() { return ++m_maxObjectId; }
	[[nodiscard]] int newSideId() { return ++m_maxSideId; }
	/// Recomputes the id counters from the content (after loading or bulk changes).
	void updateIdCounters();

	[[nodiscard]] Entity* findEntity(int entityId);     ///< 0 = world
	// Same, read-only.
	[[nodiscard]] const Entity* findEntity(int entityId) const;
	/// The brush at `ref` (world or brush entity) or nullptr.
	[[nodiscard]] Solid* findSolid(const SolidRef& ref);
	[[nodiscard]] const Solid* findSolid(const SolidRef& ref) const;

	/// Brushes in the world and in all brush entities.
	[[nodiscard]] std::size_t solidCount() const;

private:
	int m_maxObjectId = 1;
	int m_maxSideId = 0;
	/// Order of world, entities and footer blocks in the file (`Child` "world"/"entity", `Extra`
	/// = the next footer block), so hidden entities and cordons stay where they were.
	std::shared_ptr<const Layout> m_layout;
};

/// Differences between two VMF (or other KeyValues) texts: keys, values and block structure in
/// order, as readable lines ("world > solid #2 (id 5) > side #1 (id 7) > uaxis: \"…\" -> \"…\"").
/// Layout (line endings, indentation, quoting) is not compared. At most `limit` lines.
[[nodiscard]] std::vector<std::string> keyValueDifferences(std::string_view before, std::string_view after, std::size_t limit = 20);

/// Shortest text for a number that reads back to the same double ("64", "0.25", "-1e-05").
[[nodiscard]] std::string formatNumber(double value);
/// Parses "(x y z)" or "x y z"; returns false on bad input.
[[nodiscard]] bool parseVec3(std::string_view text, Vec3& out);
[[nodiscard]] std::string formatVec3(const Vec3& v);

} // namespace sme
