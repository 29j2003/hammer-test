// map_tools.h - Map-wide helpers from Hammer and Hammer++: cordons (compile only a part of
// the map), selecting a whole path_track chain, dropping objects onto what is below them.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sme/editing.h>
#include <sme/map.h>
#include <sme/math.h>
#include <sme/soundscape.h>

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
	/// The box of a model around its origin (the .mdl's hull), when the game has the model: things
	/// whose origin is not at their bottom (ammo crates, the jeep) are set onto the floor with it.
	/// Without it (or for an unknown model) the known Half-Life 2 sizes are used.
	std::function<std::optional<Bounds>(const std::string& model)> modelBounds;
};

/// Where the areas of the tutorial map are: the rooms' interiors (walkable air), for tests and
/// for placing things. The tunnel bends twice (north, east, north): straight pieces and corners.
struct TutorialMapLayout {
	Bounds indoor{{-256, -256, 0}, {256, 256, 192}};
	std::vector<Bounds> tunnel{
		{{-80, 272, 0}, {80, 784, 144}},     // north from the room
		{{-80, 784, 0}, {80, 944, 144}},     // first corner
		{{80, 784, 0}, {592, 944, 144}},     // east
		{{592, 784, 0}, {752, 944, 144}},    // second corner
		{{592, 944, 0}, {752, 1360, 144}},   // north to the outside
	};
	Bounds outdoor{{-1024, 1376, 0}, {2048, 4448, 1024}};
	/// The raised ground in the north-west where the Combine soldiers come from.
	Bounds plateau{{-896, 3712, 0}, {-128, 4352, 96}};
	/// The platform with steps in the north-east.
	Bounds platform{{1280, 3328, 0}, {1792, 3840, 64}};
	/// True when `p` is in the air of one of the areas (on top of the plateau and platform too).
	[[nodiscard]] bool contains(const Vec3& p) const;
};

/// The tutorial map's own soundscapes, quiet and made for its three areas (a room tone, a tunnel
/// with drips and a draught, wind and birds outside), named `<prefix>.tutorial_indoor`,
/// `.tutorial_tunnel` and `.tutorial_outdoor`. Only sounds for which `exists` ("ambient/...wav",
/// below sound/) says yes are used; a soundscape without any is left out.
[[nodiscard]] std::vector<SoundscapeScript> tutorialSoundscapes(const std::string& prefix, const std::function<bool(const std::string&)>& exists);

/// A first map that teaches by example (replaces the starter room): a small room, a tunnel that
/// bends twice and a large outside area under the sky, sealed by walls, ceiling and sky brushes.
/// - Sky: worldspawn's skyname, tools/toolsskybox above the outside walls, optional 3D skybox;
///   light_environment, env_sun, shadow_control (values that fit the sky), env_global_light
///   optional (shadow area 2048 units around the player); lights inside and in the tunnel;
///   lightmap scale 8 inside and in the tunnel, 16 outside.
/// - env_soundscape in each area, env_cubemap in each area.
/// - info_player_start inside with item_suit (short logon: the HEV suit from the first moment),
///   crowbar, pistol, SMG and shotgun on the same spot (taken at once, so no NPC takes them);
///   item_ammo_crate for pistol, SMG and shotgun ammo, standing on the floor.
/// - A friendly npc_citizen (fixed model, unarmed) inside, in the player's squad: it follows,
///   and USE makes it wait or follow again (npc_citizen_auto_player_squad_allow_use, set on map
///   start by point_servercommand); a hint tells the player. A target outside: an unarmed
///   Combine soldier that ai_relationship keeps neutral - it never shoots back.
/// - Outside: a path, a platform with steps, a ramp, a plateau with a ramp, low walls for cover,
///   props, a jeep (prop_vehicle_jeep) and an npc_maker on the plateau that sends one Combine
///   soldier with an SMG at a time, again and again.
/// One undo step "Tutorial map".
[[nodiscard]] Change tutorialMap(MapDocument& doc, const TutorialMapOptions& options = {});

} // namespace edit

} // namespace sme
