// mod_base_files.h - The files a Half-Life 2 based mod needs from its base game, which the
// engine does not take from the game's own folders:
//   - cfg/skill.cfg (+ skill_episodic.cfg for Episode Two mods) and cfg/skill_manifest.cfg: the
//     game code runs "exec skill_manifest.cfg" at every map start and reads the sk_* values from
//     them - weapon damage, NPC health and the ammo limits (sk_max_*). Without them every limit
//     is 0: impulse 101, ammo crates and pickups give weapons but no ammo ("'skill.cfg' not
//     present; not executing" in the console).
//   - resource/<mod>_<language>.txt with the game's texts: the HUD shows weapon names and hints
//     through tokens ("#HL2_Pulse_Rifle") looked up in the mod's own text file only. A language
//     without its own file falls back to English, so English always gets every token.
// Valve's mod template (sp/game/mod_hl2, mod_episodic of the SDK) has both; Mapsmith's feature
// bundle carries it as template/hl2/... and template/episodic/... . Where the bundle lacks a
// file, the game's own files (cfg/skill.cfg, resource/hl2_english.txt, episodic_..., ep2_...)
// are used.
//
// Adding never replaces: existing files stay, existing tokens keep their text (GameTitle and
// the mapper's own texts), only missing ones are added - safe to run on any mod.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// Reads a file by its relative path ("cfg/skill.cfg"); nullopt when there is none.
using ModFileReader = std::function<std::optional<std::vector<std::byte>>(std::string_view path)>;

/// Where the base files come from (either reader may be empty).
struct ModBaseSources {
	/// Valve's mod template, paths below its folder with the game first ("hl2/cfg/skill.cfg",
	/// "episodic/resource/mod_episodic_english.txt"), as in the bundle's template/ folder.
	ModFileReader templateFiles;
	/// The base game's files (the mod's search paths): "cfg/skill.cfg", "resource/hl2_english.txt".
	ModFileReader gameFiles;
};

/// The cfg files a mod needs (relative paths, skill_manifest.cfg last).
[[nodiscard]] std::vector<std::string> modSkillFiles(bool episodic);

/// A token every Half-Life 2 text file has (the AR2's name in the HUD); a mod text file without
/// it lacks the game's texts.
inline constexpr std::string_view HL2_TEXT_PROBE = "HL2_Pulse_Rifle";

/// What a mod lacks of its base files.
struct ModBaseCheck {
	std::vector<std::string> missingFiles;   ///< "cfg/skill.cfg" ...
	bool missingTexts = false;               ///< resource/<mod>_english.txt lacks the game's tokens
	[[nodiscard]] bool complete() const { return missingFiles.empty() && !missingTexts; }
};
[[nodiscard]] ModBaseCheck checkModBaseFiles(const std::filesystem::path& modDir, std::string_view modFolder, bool episodic);

/// The text a missing skill_manifest.cfg gets: an exec line for each skill file the mod has.
[[nodiscard]] std::string skillManifestText(const std::vector<std::string>& skillFiles);

/// What addModBaseFiles() did.
struct ModBaseResult {
	std::vector<std::string> added;      ///< Files written or extended (relative)
	std::vector<std::string> notFound;   ///< Needed files no source had
	int tokens = 0;                      ///< Tokens added to the mod's text files
	std::string error;                   ///< A file could not be written
};

/// Adds what checkModBaseFiles() finds missing, from `sources` (see the file comment):
/// skill files (template first, then the game), skill_manifest.cfg (made when no source has
/// one), and for every language a source has texts in: the tokens the mod's
/// resource/<modFolder>_<language>.txt lacks (template over Episode Two over Episode One over
/// Half-Life 2; English for every mod, other languages only where a source has them).
ModBaseResult addModBaseFiles(const std::filesystem::path& modDir, std::string_view modFolder, bool episodic, const ModBaseSources& sources);

} // namespace sme
