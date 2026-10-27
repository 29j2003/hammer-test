// sky_presets.h - Sun and ambient light that fit each of the games' 2D skies (values from the
// maps that use them, after Valve's "List of skies"). Choosing a sky takes these over into the
// map's light_environment, env_sun, shadow_control and env_global_light; the mapper can change
// them afterwards like any other key.

#pragma once

#include <span>
#include <string>
#include <string_view>

#include <sme/editing.h>

namespace sme {

struct SkyPreset {
	static constexpr double none = -1000;   ///< number not given

	const char* name;              ///< "sky_day01_01" (materials/skybox/<name>up.vmt)
	const char* game;              ///< where it comes from: "Half-Life 2", ...
	const char* angles;            ///< light_environment angles, nullptr = not given
	double pitch;                  ///< light_environment pitch (negative = from above)
	const char* light;             ///< _light "r g b brightness"
	const char* ambient;           ///< _ambient
	const char* lightHdr;          ///< _lightHDR, nullptr = not given
	double lightScaleHdr;          ///< _lightscaleHDR
	const char* ambientHdr;        ///< _ambientHDR
	double ambientScaleHdr;        ///< _AmbientScaleHDR
	double sunSpread;              ///< SunSpreadAngle (soft shadows)
};

/// All known skies, grouped by game.
[[nodiscard]] std::span<const SkyPreset> skyPresets();
/// The preset for a sky; also matches with "_hdr" added or removed. nullptr = not known.
[[nodiscard]] const SkyPreset* findSkyPreset(std::string_view skyName);

/// Sets worldspawn's skyname and, if the sky is known, its light: light_environment (made near
/// info_player_start, else at `place`, when the map has none), env_sun, shadow_control and
/// env_global_light (only where the map has them). One undo step.
[[nodiscard]] Change applySky(MapDocument& doc, const std::string& skyName, const Vec3& place);

} // namespace sme
