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

/// What makeWater() did.
struct WaterResult {
	Change change;
	int brushes = 0;          ///< Brushes made into water (0: nothing selected that could be)
	int lodControlId = 0;     ///< The water_lod_control it added (0: the map had one)
	int cubemapId = 0;        ///< The env_cubemap it added (0: one was close enough)
};

/// Hammer's water recipe in one undo step: the top face of every selected brush (world brushes,
/// brushes of selected brush entities, brushes of selected faces) gets `waterMaterial`, all other
/// faces tools/toolsnodraw; a water_lod_control is added if the map has none, and an env_cubemap
/// 64 units above the middle of the surface if no cubemap is within `cubemapRange`; its "sides"
/// names the water faces so they reflect that cubemap.
[[nodiscard]] WaterResult makeWater(MapDocument& doc, const Selection& selection, const std::string& waterMaterial, const EntityCatalog* catalog,
                                    double cubemapRange = 512);

/// Materials of the starter room (the game's own when it has them).
struct StarterRoomMaterials {
	std::string floor = "DEV/DEV_MEASUREGENERIC01B";
	std::string walls = "DEV/DEV_MEASUREWALL01A";
	std::string ceiling = "DEV/DEV_MEASUREGENERIC01";
};

/// A first map to start from: a closed room (floor, four walls, ceiling - no leak) of `size`
/// inside, a light under the ceiling, an info_player_start on the floor and an env_cubemap.
/// One undo step "Starter room".
[[nodiscard]] Change starterRoom(MapDocument& doc, const StarterRoomMaterials& materials, const Vec3& size = {768, 768, 256});

/// Materials of the tutorial map (the game's own when it has them, see the app's pick).
struct TutorialMapMaterials {
	std::string floor = "DEV/DEV_MEASUREGENERIC01B";     ///< Inside: floor
	std::string walls = "DEV/DEV_MEASUREWALL01A";        ///< Inside: walls
	std::string ceiling = "DEV/DEV_MEASUREGENERIC01";    ///< Inside and tunnel: ceiling
	std::string tunnel = "DEV/DEV_MEASUREWALL01A";       ///< Tunnel: walls and floor
	std::string ground = "DEV/DEV_MEASUREGENERIC01B";    ///< Outside: the ground (grass)
	std::string path = "DEV/DEV_MEASUREGENERIC01";       ///< Outside: the path from the tunnel
	std::string outerWalls = "DEV/DEV_MEASUREWALL01A";   ///< Outside: the walls around (below the sky)
	std::string platform = "DEV/DEV_MEASUREGENERIC01";   ///< Outside: platform and steps
	std::string rock = "DEV/DEV_MEASUREWALL01A";         ///< 3D skybox: hills
};

/// Where a soundscape plays in the tutorial map.
enum class SoundscapeArea { Indoor, Tunnel, Outdoor };

/// The best fitting soundscape of `names` for an area (by words in the name: "indoor",
/// "tunnel", "city" ...; ties: alphabetical). Empty when none fits.
[[nodiscard]] std::string pickSoundscape(const std::vector<std::string>& names, SoundscapeArea area);

/// What the tutorial map has (all of it in every Source SDK Base 2013 Singleplayer install).
struct TutorialMapOptions {
	TutorialMapMaterials materials;
	std::string skyName = "sky_day01_01";
	/// A 3D skybox: a small room below the map with sky_camera (scale 16) and hills around the
	/// outside area, drawn behind the 2D sky.
	bool skybox3d = false;
	/// env_global_light: Mapbase's sun shadow (a projected texture from the sun over the whole
	/// map; dynamic shadows of props and NPCs outside). Only Mapbase's game code has it - other
	/// games print an error for the unknown entity.
	bool globalLight = false;
	std::string indoorSoundscape = "d1_trainstation.util_city";
	std::string tunnelSoundscape = "d1_canals.util_drips";
	std::string outdoorSoundscape = "d1_trainstation.util_city";
};

/// Brushes of the tutorial map, for tests: the rooms' interiors (x/y/z bounds, walkable air).
struct TutorialMapLayout {
	Bounds indoor{{-512, -512, 0}, {512, 512, 256}};
	Bounds tunnel{{-96, 528, 0}, {96, 1024, 160}};
	Bounds outdoor{{-1024, 1040, 0}, {1024, 3072, 1024}};
};

/// A first map that teaches by example (replaces the starter room): an indoor room, a tunnel and
/// an outside area under the sky, sealed by walls, ceiling and sky brushes (no leak).
/// - Sky: worldspawn's skyname, tools/toolsskybox above the outside walls, optional 3D skybox;
///   light_environment, env_sun, shadow_control (values that fit the sky), env_global_light
///   optional; lights inside and in the tunnel.
/// - env_soundscape in each area, env_cubemap in each area.
/// - info_player_start inside with item_suit (short logon: the HEV suit from the first moment),
///   crowbar, pistol, SMG and shotgun on the same spot (taken at once, so no NPC takes them);
///   item_ammo_crate for pistol, SMG and shotgun ammo.
/// - A friendly npc_citizen (fixed model, unarmed) inside, and a target: an unarmed Combine
///   soldier outside without grenades that ai_relationship keeps neutral to the player and the
///   citizen - it never shoots back.
/// - Outside: a path, a platform with steps, a ramp and props (barrels, crates, barriers).
/// One undo step "Tutorial map".
[[nodiscard]] Change tutorialMap(MapDocument& doc, const TutorialMapOptions& options = {});

} // namespace edit

} // namespace sme
