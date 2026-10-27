// map_tools.h - Map-wide helpers from Hammer and Hammer++: cordons (compile only a part of
// the map), selecting a whole path_track chain, dropping objects onto what is below them.

#pragma once

#include <string>
#include <vector>

#include <sme/editing.h>
#include <sme/map.h>
#include <sme/math.h>

namespace sme {

class EntityCatalog;

/// Hammer's cordons: boxes that limit what gets compiled (several at once, like CS:GO Hammer).
struct Cordon {
	std::string name = "cordon";
	Bounds box;
	bool active = true;
};

/// The map's cordons (stored in the VMF's cordons block).
struct CordonSet {
	bool active = false;           ///< Cordon mode on (compile only what is inside)
	std::vector<Cordon> cordons;
};

/// Reads the "cordons" block (Hammer 2013) or the older single "cordon" block.
[[nodiscard]] CordonSet readCordons(const MapDocument& doc);
/// Writes the cordons as a "cordons" block (replacing old ones).
void writeCordons(MapDocument& doc, const CordonSet& cordons);

/// The map as it is compiled with cordons on: only brushes and entities touching an active
/// cordon, each cordon sealed by six brushes of `sealMaterial` around it.
[[nodiscard]] MapDocument cordonedCopy(const MapDocument& doc, const CordonSet& cordons, const EntityCatalog* catalog,
                                       const std::string& sealMaterial = "TOOLS/TOOLSSKYBOX");

/// All entities of the path the entity belongs to (path_track, path_corner, ...): following
/// "target" forward and the entities that target it backward.
[[nodiscard]] std::vector<int> trackChain(const MapDocument& doc, int entityId);

/// The parallax_obb an env_cubemap uses for parallax correction (Mapbase: its "parallaxobb" key
/// names it), or nullptr.
[[nodiscard]] const Entity* cubemapRoomBox(const MapDocument& doc, const Entity& cubemap);

/// The room around `point`: the distance to the next wall in +X, -X, +Y, -Y, +Z and -Z (world
/// brushes and solid brush entities; triggers and clips do not count), at most `maxDistance` each way.
[[nodiscard]] Bounds roomAround(const MapDocument& doc, const Vec3& point, double maxDistance = 2048);

namespace edit {

/// Gives an env_cubemap a room box for parallax-corrected reflections (Mapbase): creates the
/// parallax_obb (one box brush, named after the cubemap) or resizes the one it has, and sets
/// the cubemap's "parallaxobb". One undo step; returns the box entity's id.
[[nodiscard]] std::pair<Change, int> setCubemapRoomBox(MapDocument& doc, int cubemapId, const Bounds& box, const EntityClass* obbClass);
/// Removes the cubemap's room box and its "parallaxobb" key.
[[nodiscard]] Change removeCubemapRoomBox(MapDocument& doc, int cubemapId);

/// Moves the selected objects straight down until they rest on what is below them
/// (brushes, entity boxes; unselected only). Objects with nothing below stay.
[[nodiscard]] Change dropToFloor(MapDocument& doc, const Selection& selection, const EntityCatalog* catalog);

} // namespace edit

} // namespace sme
