// map.h - The map document: world brushes, entities and everything else in a .vmf file.
//
// Fields the editor works with are parsed into typed members; everything else (editor
// blocks, displacement data, visgroups, cameras, cordons, unknown keys) is kept as
// KeyValues and written back unchanged, so opening and saving a map never loses data.

#pragma once

#include <array>
#include <cstdint>
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
};

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
};

/// An entity with its keys, outputs and (for brush entities) brushes. The world is an Entity too.
struct Entity {
	int id = 0;
	std::vector<std::pair<std::string, std::string>> keyValues;   ///< Ordered, including classname/origin
	std::vector<Connection> connections;
	std::vector<Solid> solids;
	std::vector<KvNode> extra;           ///< hidden solids, editor block, unknown blocks

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

/// A whole VMF map: world, entities and the other blocks, kept so a save writes back what was read.
class MapDocument {
public:
	MapDocument();

	/// Parses VMF text. Throws std::runtime_error on broken syntax.
	[[nodiscard]] static MapDocument parse(std::string_view vmfText);
	/// Writes VMF text.
	[[nodiscard]] std::string serialize() const;

	Entity world;                        ///< classname "worldspawn", id 1 by convention
	std::vector<Entity> entities;
	std::vector<KvNode> headerBlocks;    ///< versioninfo, visgroups, viewsettings (before world)
	std::vector<KvNode> footerBlocks;    ///< cameras, cordon(s) and unknown blocks (after entities)

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
};

/// Shortest text for a number that reads back to the same double ("64", "0.25", "-1e-05").
[[nodiscard]] std::string formatNumber(double value);
/// Parses "(x y z)" or "x y z"; returns false on bad input.
[[nodiscard]] bool parseVec3(std::string_view text, Vec3& out);
[[nodiscard]] std::string formatVec3(const Vec3& v);

} // namespace sme
