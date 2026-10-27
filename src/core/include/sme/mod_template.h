// mod_template.h - A new Source mod from scratch: the gameinfo.txt for the chosen base (Source SDK
// Base 2013 Singleplayer or Multiplayer, optionally with Mapbase's shared content), the usual
// folders, a cfg/autoexec.cfg and the resource file with the game's title (UTF-16, as the
// engine expects). Used by the editor's "New mod ..." dialog; no Qt here, so it can be tested.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// What the new mod is built on.
enum class ModBase {
	Singleplayer,   ///< Source SDK Base 2013 Singleplayer (Half-Life 2 + Episodes content)
	Multiplayer,    ///< Source SDK Base 2013 Multiplayer (Half-Life 2: Deathmatch content)
	Mapbase,        ///< Singleplayer plus Mapbase's shared content (mapbase_shared next to the mod)
};

/// Everything the template needs to know about the new mod.
struct ModTemplate {
	std::string title;                      ///< Shown in Steam and the main menu ("My Mod")
	std::string folder;                     ///< Folder name in sourcemods ("mymod")
	ModBase base = ModBase::Singleplayer;
	/// Extra search path lines (path id, path) placed before the base game's content, e.g.
	/// Mapbase's shared content found by mapbaseMounts().
	std::vector<std::pair<std::string, std::string>> extraMounts;
};

/// True if `folder` can be a mod folder: 1-64 characters, letters, digits, '_' and '-' only,
/// not starting with '-'.
[[nodiscard]] bool isValidModFolder(std::string_view folder);

/// A folder name made from a title ("My Cool Mod!" -> "my_cool_mod"); may be empty.
[[nodiscard]] std::string suggestModFolder(std::string_view title);

/// gameinfo.txt for the template.
[[nodiscard]] std::string gameInfoText(const ModTemplate& mod);

/// The folders a new mod gets (relative, forward slashes): maps, mapsrc, materials, ...
[[nodiscard]] const std::vector<std::string>& modFolders();

/// resource/<folder>_english.txt: the "lang" file with GameTitle, as UTF-16 LE with BOM.
[[nodiscard]] std::vector<std::byte> modResourceFile(const ModTemplate& mod);

/// cfg/autoexec.cfg of a new mod (comments only, plus developer settings that help mapping).
[[nodiscard]] std::string modAutoexec(const ModTemplate& mod);

/// Mapbase's shared content in `sourceModsDir`/mapbase_shared as search path lines
/// ("game+mod", "|gameinfo_path|../mapbase_shared/shared_misc", ...). Empty if it is not there.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> mapbaseMounts(const std::filesystem::path& sourceModsDir);

/// Result of createMod().
struct CreatedMod {
	std::filesystem::path folder;           ///< The new mod's folder (empty on failure)
	std::vector<std::filesystem::path> copiedMaps;   ///< Where the maps went (mapsrc/...)
	std::string error;                      ///< Why it failed (empty on success)
};

/// Creates the mod in `sourceModsDir`/<folder>: gameinfo.txt, folders, autoexec, resource file,
/// and copies `maps` (.vmf files) into its mapsrc folder. Refuses an existing folder.
[[nodiscard]] CreatedMod createMod(const std::filesystem::path& sourceModsDir, const ModTemplate& mod,
                                   const std::vector<std::filesystem::path>& maps = {});

} // namespace sme
