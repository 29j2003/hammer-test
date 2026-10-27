// gameinfo.h - Reads a game's or mod's gameinfo.txt and resolves its search paths.
//
// The search paths decide where the engine (and so the editor) finds every material,
// model and sound. They are resolved exactly once here; GameFileSystem mounts the result.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sme/steam.h>

namespace sme {

/// One line of the SearchPaths block, e.g. `game+mod  |gameinfo_path|.`.
struct SearchPath {
	std::vector<std::string> pathIds;   ///< Lowercase ids: "game", "mod", "platform", ...
	std::string raw;                    ///< Value as written in gameinfo.txt
	std::string conditional;            ///< e.g. "$WIN32" (without brackets), empty if none
	bool conditionalActive = true;      ///< false if the conditional excludes this platform
	bool isVpk = false;                 ///< Path ends in .vpk (mounted as <name>_dir.vpk)
	bool isWildcard = false;            ///< Path ends in /* (every subfolder/VPK is mounted)
	std::filesystem::path resolved;     ///< Absolute path after token replacement
	std::string problem;                ///< Why the path could not be resolved, if it could not
};

/// Parsed gameinfo.txt.
struct GameInfo {
	std::filesystem::path gameDir;      ///< Folder that contains gameinfo.txt
	std::string game;                   ///< "game" key (window title)
	std::string title;
	std::string type;                   ///< "singleplayer_only", "multiplayer_only", ...
	AppId steamAppId = 0;               ///< FileSystem/SteamAppId: the base game the files come from
	AppId toolsAppId = 0;
	std::vector<SearchPath> searchPaths;
	std::vector<std::string> warnings;

	/// Parses gameDir/gameinfo.txt (search paths stay unresolved).
	[[nodiscard]] static std::optional<GameInfo> load(const std::filesystem::path& gameDir, std::string* error = nullptr);

	/// Parses gameinfo.txt contents (used by load() and the tests).
	[[nodiscard]] static std::optional<GameInfo> parse(std::string_view text, const std::filesystem::path& gameDir, std::string* error = nullptr);
};

/// Everything needed to turn the tokens in a search path into real folders.
struct SearchPathContext {
	/// Folder that |all_source_engine_paths| and plain relative paths point at:
	/// the install folder of the base game (e.g. ".../common/Source SDK Base 2013 Singleplayer").
	std::filesystem::path baseDir;
	/// Install folder lookup for |appid_<id>| tokens (optional).
	std::function<std::optional<std::filesystem::path>(AppId)> appInstallDir;
};

/// Builds the context for a game: base folder = install folder of SteamAppId if Steam knows it,
/// otherwise the parent folder of the game folder (games that ship inside their own app folder).
[[nodiscard]] SearchPathContext makeSearchPathContext(const GameInfo& info, const SteamInstallation* steam);

/// Fills SearchPath::resolved / problem for every entry.
void resolveSearchPaths(GameInfo& info, const SearchPathContext& context);

/// True if `[$WIN32]`-style conditionals hold on the platform the editor runs on.
[[nodiscard]] bool evaluateConditional(std::string_view conditional);

} // namespace sme
