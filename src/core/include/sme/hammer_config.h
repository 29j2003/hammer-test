// hammer_config.h - Imports game configurations from Hammer / Hammer++ (GameConfig.txt).
//
// Mappers already told Hammer where their FGDs and compile tools are; reading that
// saves setting it up again in this editor.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// One game configuration of Hammer's or Hammer++'s settings.
struct HammerGameConfig {
	std::string name;                               ///< Config name shown in Hammer
	std::filesystem::path gameDir;                  ///< Folder with gameinfo.txt
	std::vector<std::filesystem::path> fgds;        ///< GameData0, GameData1, ...
	std::filesystem::path gameExe;
	std::filesystem::path vbsp;
	std::filesystem::path vvis;
	std::filesystem::path vrad;
	std::filesystem::path mapDir;                   ///< Where .vmf files are saved
	std::filesystem::path bspDir;                   ///< Where compiled .bsp files go
	std::string defaultSolidEntity;
	std::string defaultPointEntity;
	std::string cordonTexture;
	double defaultTextureScale = 0.25;
	int defaultLightmapScale = 16;
};

/// A Hammer config file with all its game configurations.
struct HammerConfigFile {
	std::filesystem::path path;
	std::string flavor;                             ///< "Hammer" or "Hammer++"
	std::vector<HammerGameConfig> games;
};

/// Parses a GameConfig.txt / hammerplusplus_gameconfig.txt.
[[nodiscard]] std::optional<HammerConfigFile> parseHammerConfig(std::string_view text, const std::filesystem::path& path, std::string* error = nullptr);
[[nodiscard]] std::optional<HammerConfigFile> loadHammerConfig(const std::filesystem::path& path, std::string* error = nullptr);

/// Config files that exist below an app's install folder (bin/, bin/x64/, Hammer++ folder).
[[nodiscard]] std::vector<std::filesystem::path> findHammerConfigs(const std::filesystem::path& appInstallDir);

} // namespace sme
