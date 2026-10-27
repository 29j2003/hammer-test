// game_discovery.h - Finds every Source 1 game and mod on this PC.
//
// Nothing is hardcoded to Half-Life 2.
// Any installed Steam app folder or sourcemod with a gameinfo.txt becomes a game entry;
// a small table of known app ids only adds friendly names and engine branch hints.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sme/hammer_config.h>
#include <sme/steam.h>

namespace sme {

/// Extra knowledge about well-known Source 1 apps.
struct KnownApp {
	AppId appId;
	std::string_view name;
	std::string_view branch;   ///< Engine branch, e.g. "Source SDK 2013", "Xengine (Black Mesa)"
	std::string_view mainFgd;  ///< FGD file name (without .fgd) the game's Hammer uses by default
};

/// Known app by id, or nullptr.
[[nodiscard]] const KnownApp* findKnownApp(AppId appId);

struct GameEntry {
	std::string title;                          ///< From gameinfo.txt (falls back to the folder name)
	std::string label;                          ///< Unique name for menus ("Half-Life 2 (episodic)")
	std::string appName;                        ///< Steam app the game belongs to / the mod is based on
	std::filesystem::path gameDir;              ///< Folder with gameinfo.txt
	AppId ownerAppId = 0;                       ///< App the folder belongs to (0 for sourcemods)
	AppId baseAppId = 0;                        ///< SteamAppId from gameinfo.txt
	bool isSourceMod = false;
	bool usesMapbase = false;                   ///< Search paths mention Mapbase content (or the app is built on Mapbase)
	bool baseContent = false;                   ///< Shared content of another game in the same app (e.g. hl2/ inside
	                                            ///< Entropy : Zero 2): no client library of its own, not playable
	std::string branch;                         ///< Engine branch hint (may be empty)
	std::vector<std::filesystem::path> fgds;    ///< Suggested FGDs (Hammer config first, then the game's main FGD)
	std::vector<std::filesystem::path> availableFgds; ///< Every .fgd found next to the game and in the base game's bin/
	std::filesystem::path hammerConfig;         ///< Config file the FGDs came from, if any
	std::filesystem::path mapbaseFgd;           ///< Mapbase mods: the Mapbase FGD that was found (empty if none)
	std::vector<std::string> notes;

	/// Short unique id for settings ("sourcemods/mymod", "220/hl2", ...).
	[[nodiscard]] std::string key() const;
	/// Folder name of the game ("hl2", "episodic", "mymod").
	[[nodiscard]] std::string folder() const;
};

struct DiscoveryResult {
	SteamInstallation steam;
	std::vector<GameEntry> games;               ///< Sorted: games first, then mods, by title
	std::vector<HammerConfigFile> hammerConfigs;
	std::vector<std::string> warnings;
};

/// Files an FGD pulls in with @include (resolved next to it).
[[nodiscard]] std::vector<std::filesystem::path> fgdIncludes(const std::filesystem::path& fgd);

/// True if the FGD (or an FGD it @includes, a few levels deep) is Mapbase's: it mentions Mapbase.
[[nodiscard]] bool isMapbaseFgd(const std::filesystem::path& fgd);

/// Scans the given Steam installation (use SteamInstallation::detect() for the real one).
[[nodiscard]] DiscoveryResult discoverGames(SteamInstallation steam);

} // namespace sme
