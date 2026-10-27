// fidelity_look.h - The look values of mapbase-fidelity (the mod's graphics add-on) that a map
// may set: drifting clouds, light shafts, colour grading, water. The game reads them as console
// variables; a map sets them when it loads through a logic_auto that sends the commands to a
// point_clientcommand (the add-on allows exactly these, FCVAR_SERVER_CAN_EXECUTE). The editor
// keeps both entities itself (names below) and shows the values in the 3D view where it can.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/editing.h>

namespace sme {

struct LookSetting {
	std::string cvar;          ///< "r_fidelity_sky_clouds"
	std::string group;         ///< "Clouds", "Light shafts", "Image", "Water"
	std::string label;
	std::string description;
	double minimum = 0, maximum = 1, fallback = 0;
	int decimals = 2;
};

/// Every look value a map can set, in the order they are shown.
[[nodiscard]] const std::vector<LookSetting>& lookSettings();
[[nodiscard]] const LookSetting* findLookSetting(std::string_view cvar);

/// "cvar value" lines of a .cfg (cfg/fidelity.cfg: what players get when a map sets nothing).
[[nodiscard]] std::map<std::string, std::string> parseCfgValues(std::string_view text);

/// Names of the two entities the editor keeps.
inline constexpr std::string_view LOOK_RELAY_NAME = "fidelity_look";
inline constexpr std::string_view LOOK_COMMAND_NAME = "fidelity_look_command";

/// The look values the map sets (cvar -> value text).
[[nodiscard]] std::map<std::string, std::string> mapLookValues(const MapDocument& doc);
/// Sets the map's look values (all of them; an empty map removes both entities) as one undo
/// step "Map look". New entities stand at `place` (inside the map, or VBSP reports a leak).
[[nodiscard]] Change setMapLookValues(MapDocument& doc, const std::map<std::string, std::string>& values, const Vec3& place);

} // namespace sme
