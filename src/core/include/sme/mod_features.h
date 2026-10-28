// mod_features.h - Which features of the mod's graphics and sound add-on (mapbase-fidelity) the
// mod has: one checklist in the editor (Project > Features) instead of console variables and
// .bat files. Every feature is on unless the mapper unticks it.
//
// The game side: the add-on's on/off switches are console variables saved in the player's
// config (FCVAR_ARCHIVE); its fidelity.cfg never sets them on purpose. The editor writes
// cfg/mapsmith_features.cfg with each switch (1 or 0) and runs it from autoexec.cfg after the
// player's config, so the mod starts with exactly the chosen features (a player can still change
// a switch in the console until the next start). deploy.ps1 neither writes nor removes this file
// or its autoexec line (it only touches lines ending in "// mapbase-fidelity"). Only on/off
// switches go there - look and mix values stay in fidelity.cfg and the maps.
//
// The material features (liquid in bottles, swaying foliage, water, walls with depth) are made
// by the Game enhancements (fidelity_effects.h); their checkbox is the effect's mod-wide switch.
//
// "Fresh install": generatedModFiles() lists what deploy.ps1 and the editor made in the mod (by
// their markers and manifests), so the editor can show it before deploy.ps1 -Remove takes it out
// and the install runs again; the mapper's own files are never in the list.

#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <sme/fidelity_effects.h>

namespace sme {

/// Where a feature is listed.
enum class FeatureGroup { Graphics, Effects, Sound, Materials };
/// English name of a group ("Graphics").
[[nodiscard]] std::string_view featureGroupName(FeatureGroup group);

/// One feature of the add-on.
struct ModFeature {
	const char* id;                     ///< Stable name ("ssao"), used in the editor's settings
	FeatureGroup group;
	const char* title;                  ///< "Ambient occlusion (SSAO)"
	const char* description;            ///< One plain line: what the player sees or hears
	std::vector<const char*> convars;   ///< Its on/off switches (FCVAR_ARCHIVE, 1 = on, 0 = off)
	std::optional<GameEffect> effect;   ///< The material effect it switches (Game enhancements)
};

/// Every feature, in the order the checklist shows them (grouped).
[[nodiscard]] const std::vector<ModFeature>& modFeatures();
[[nodiscard]] const ModFeature* findModFeature(std::string_view id);

/// First line of cfg/mapsmith_features.cfg.
inline constexpr std::string_view FEATURES_MARKER = "// mapsmith: features of this mod";
/// The line autoexec.cfg runs it with (the comment marks it as the editor's).
inline constexpr std::string_view FEATURES_EXEC_LINE = "exec mapsmith_features\t// mapsmith";

/// cfg/mapsmith_features.cfg of a mod folder.
[[nodiscard]] std::filesystem::path featuresCfgFile(const std::filesystem::path& modDir);

/// The file's text: every switch of every feature with a console variable, 1 or 0.
[[nodiscard]] std::string writeFeaturesCfg(const std::set<std::string>& off);
/// The features switched off in such a text (a feature is off when any of its switches is 0).
[[nodiscard]] std::set<std::string> parseFeaturesCfg(std::string_view text);

/// The features switched off in the mod, or nullopt when it has no choice yet (no file).
[[nodiscard]] std::optional<std::set<std::string>> loadFeatureChoice(const std::filesystem::path& modDir);
/// Writes the file; with `hook` also the line in cfg/autoexec.cfg that runs it (only for a mod
/// that has the add-on: other mods would print "unknown command" for each switch).
bool saveFeatureChoice(const std::filesystem::path& modDir, const std::set<std::string>& off, bool hook);
/// Adds the autoexec line when the mod has a choice but autoexec.cfg does not run it yet (the
/// add-on arrived after the choice was made). True when it changed the file.
bool hookFeatureChoice(const std::filesystem::path& modDir);

/// The Game enhancements' settings with each material effect on or off as the choice says (left-out
/// models and materials stay).
[[nodiscard]] EnhancementSettings withFeatureEffects(EnhancementSettings settings, const std::set<std::string>& off);
/// The features whose material effect the settings switch off.
[[nodiscard]] std::set<std::string> featuresOffBySettings(const EnhancementSettings& settings);

/// autoexec.cfg text with / without the line (other lines and the line ends stay).
[[nodiscard]] bool runsFeatures(std::string_view autoexec);
[[nodiscard]] std::string withFeaturesLine(std::string_view autoexec);
[[nodiscard]] std::string withoutFeaturesLine(std::string_view autoexec);

/// A file deploy.ps1 or the editor made in the mod.
struct GeneratedFile {
	std::filesystem::path path;   ///< Relative to the mod folder, forward slashes
	std::string what;             ///< "material made by deploy.ps1", "shader", ...
	bool byEditor = false;        ///< Made by the editor (the editor removes it itself)
};
/// What a fresh install removes, sorted by path: generated materials (marker in the first line),
/// the files listed in deploy.ps1's manifests that still exist, the add-on's shaders, its cfg and
/// shader DLL. The mod's own files are never listed.
[[nodiscard]] std::vector<GeneratedFile> generatedModFiles(const std::filesystem::path& modDir);
/// Removes the files the editor made (byEditor) - deploy.ps1 -Remove takes out its own. Returns
/// how many went.
int removeEditorFiles(const std::filesystem::path& modDir, const std::vector<GeneratedFile>& files);

} // namespace sme
