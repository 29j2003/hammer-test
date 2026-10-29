// mod_install.h - The mod's features (mapbase-fidelity) installed from Mapsmith itself: no
// compiler, no git, no scripts to start by hand.
//
// The add-on's code lives in this repository (mod/, exported from the mod's source by
// tools/export_mod_addon.py); CI builds it on Mapbase and the Windows package carries the result
// as a "feature bundle" next to Mapsmith.exe (features/): the client and server DLLs for HL2 and
// Episodic mods, the shader DLL, VRAD with baked ambient occlusion, the add-on's game files
// (shaders, cfg) and its PowerShell scripts (deploy.ps1 for the generated materials, report.ps1
// for Make report). manifest.txt lists every file with its SHA-256.
//
// installFeatures() copies the files into a mod: bin/client.dll, bin/server.dll and
// bin/game_shader_dx9.dll (Mapbase's own shaders, SDK_*) of the mod's branch (Episodic when
// gameinfo.txt mounts ep2/episodic, else HL2), bin/game_shader_dx6.dll (the add-on's shaders), the
// game files, the "exec fidelity" line in cfg/autoexec.cfg (the same line deploy.ps1 writes) and,
// when the SDK's bin folder is known, VRAD (the files there before go into bin/fidelity_backup,
// the folder update.ps1 uses, so either can put them back). A file it replaces that is not its
// own is kept once as <file>.mapsmith-backup. cfg/mapsmith_installed.txt records the version,
// every file written with its SHA-256 and the backups - so an update replaces only its own files,
// removeFeatures() puts back what was there before, and nothing of the mapper's is touched.

#pragma once

#include <filesystem>
#include <map>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/mod_code.h>

namespace sme {

/// Lowercase hex SHA-256 of `data`.
[[nodiscard]] std::string sha256Hex(std::string_view data);
/// SHA-256 of a file, nullopt if it cannot be read.
[[nodiscard]] std::optional<std::string> fileSha256(const std::filesystem::path& file);

/// manifest.txt of a bundle: "mapsmith-features 1", "version <v>", then "<sha256>  <path>" lines.
inline constexpr std::string_view BUNDLE_MANIFEST = "manifest.txt";

/// One file of the bundle.
struct FeatureBundleFile {
	std::string path;     ///< Relative to the bundle, forward slashes ("bin/episodic/client.dll")
	std::string sha256;
};

/// The features Mapsmith brings along (see the file comment).
struct FeatureBundle {
	std::filesystem::path root;
	std::string version;   ///< "0.1.0+52465da": the add-on's version and the exported commit
	std::vector<FeatureBundleFile> files;

	[[nodiscard]] const FeatureBundleFile* find(std::string_view path) const;
};

/// Reads a bundle folder (its manifest); nullopt with the reason when it is missing or broken.
/// Paths leaving the folder ("..", absolute) make the whole manifest invalid.
[[nodiscard]] std::optional<FeatureBundle> readFeatureBundle(const std::filesystem::path& root, std::string* error = nullptr);
/// The manifest text for a version and files (what the CI's collect step writes).
[[nodiscard]] std::string writeBundleManifest(std::string_view version, const std::vector<FeatureBundleFile>& files);
/// Checks every file of the bundle against its SHA-256; the first bad one in `error`.
[[nodiscard]] bool verifyFeatureBundle(const FeatureBundle& bundle, std::string* error = nullptr);

/// The scripts in the bundle (fidelity/deploy.ps1 and report.ps1; no update.ps1 - nothing is built).
[[nodiscard]] ModCodeScripts bundledScripts(const FeatureBundle& bundle);

/// Which game's DLLs a mod needs.
enum class ModBranch { Episodic, Hl2 };
/// From gameinfo.txt: Episodic when it mounts ep2 or episodic (comments ignored), else HL2.
[[nodiscard]] ModBranch modBranch(std::string_view gameinfo);
[[nodiscard]] std::string_view modBranchName(ModBranch branch);   ///< "episodic", "hl2"

/// cfg/mapsmith_installed.txt of a mod (relative path).
inline constexpr std::string_view INSTALLED_FILE = "cfg/mapsmith_installed.txt";
/// The autoexec.cfg line that runs the add-on's cfg (written exactly like deploy.ps1 writes it).
inline constexpr std::string_view FIDELITY_EXEC_LINE = "exec fidelity\t// mapbase-fidelity";
/// The suffix of a file kept from before the install.
inline constexpr std::string_view BACKUP_SUFFIX = ".mapsmith-backup";

/// What an install put into a mod (cfg/mapsmith_installed.txt).
struct InstalledFeatures {
	std::string version;
	ModBranch branch = ModBranch::Episodic;
	std::map<std::string, std::string> files;   ///< Mod-relative path -> SHA-256 written
	std::vector<std::string> backups;           ///< Mod-relative paths kept as <path>.mapsmith-backup
	bool autoexecLine = false;                  ///< The install added the exec line to autoexec.cfg
	std::filesystem::path vradDir;              ///< SDK bin folder VRAD went into (empty: none)
};
[[nodiscard]] std::optional<InstalledFeatures> readInstalledFeatures(const std::filesystem::path& modDir);
[[nodiscard]] std::string writeInstalledFeatures(const InstalledFeatures& installed);

/// One file of an install: bundle path -> mod path.
struct FeatureCopy {
	std::string from;   ///< In the bundle
	std::string to;     ///< In the mod
};
/// The files that go into a mod of this branch (VRAD is separate: it goes into the SDK).
[[nodiscard]] std::vector<FeatureCopy> featureInstallPlan(const FeatureBundle& bundle, ModBranch branch);

/// What an install or removal did.
struct FeatureInstallResult {
	bool ok = false;
	std::string error;                  ///< Why it stopped (plain words)
	std::vector<std::string> written;   ///< Mod-relative paths written (or restored/removed for a removal)
	std::vector<std::string> backedUp;  ///< Files kept from before
	bool vrad = false;                  ///< VRAD was installed (or put back)
	bool changed = false;               ///< Anything in the mod changed
};

/// Installs (or updates) the bundle's files into a mod (see the file comment). `sdkBin`: the bin
/// folder of Source SDK Base 2013 Singleplayer for VRAD (empty: VRAD is left out). Files that
/// are already current are not touched; a file that cannot be written (the game running) stops
/// the install with a plain message - what was written so far is recorded.
[[nodiscard]] FeatureInstallResult installFeatures(const FeatureBundle& bundle, const std::filesystem::path& modDir,
                                                   const std::filesystem::path& sdkBin = {});
/// Takes the installed files out again: backups go back, the other files are removed if nobody
/// changed them since, the autoexec line goes, VRAD's backup goes back.
[[nodiscard]] FeatureInstallResult removeFeatures(const std::filesystem::path& modDir);

/// The installed version is not the bundle's (an update is due).
[[nodiscard]] bool featuresOutdated(const FeatureBundle& bundle, const InstalledFeatures& installed);
/// Files of the bundle's install plan for the mod's branch that the mod does not have (mod-relative,
/// e.g. "bin/game_shader_dx9.dll" after an install from an older bundle without it): the install
/// is incomplete and should run again.
[[nodiscard]] std::vector<std::string> missingFeatureFiles(const FeatureBundle& bundle, const std::filesystem::path& modDir);

/// The link time in a Windows DLL's PE header (seconds since 1970), nullopt if it is not a PE file.
[[nodiscard]] std::optional<std::uint32_t> peLinkTime(std::span<const std::byte> data);
[[nodiscard]] std::optional<std::uint32_t> peLinkTimeOfFile(const std::filesystem::path& file);

/// The DLLs a Mapbase mod needs in its bin folder: the game code and Mapbase's shaders. Without
/// game_shader_dx9.dll the game knows no SDK_* shader: such materials and the add-on's screen
/// passes fall back to wireframe (black lines across the view).
[[nodiscard]] const std::vector<std::string>& mapbaseModDlls();   ///< "bin/client.dll", "bin/server.dll", "bin/game_shader_dx9.dll"

/// Fills a Mapbase mod's bin folder from Mapbase's own game folder next to it
/// (`sourceModsDir`/mapbase_episodic/bin or mapbase_hl2/bin, by the mod's branch): copies the
/// mapbaseModDlls() the mod does not have yet (never overwrites). Returns the mod-relative files
/// copied; used when Mapsmith brings no features (or they could not be installed).
[[nodiscard]] std::vector<std::string> copyMapbaseBin(const std::filesystem::path& sourceModsDir, const std::filesystem::path& modDir);

/// What a Mapbase mod's bin folder lacks.
struct ModBinState {
	std::vector<std::string> missing;    ///< Mod-relative DLLs that are not there
	std::vector<std::string> outdated;   ///< bin/game_shader_dx9.dll when older (PE link time) than the bundle's
	[[nodiscard]] bool ok() const { return missing.empty() && outdated.empty(); }
};
/// Checks a Mapbase mod's bin folder: mapbaseModDlls(), plus bin/game_shader_dx6.dll when Mapsmith
/// installed the features there; "outdated" compares game_shader_dx9.dll's PE link time with the
/// bundle's for the mod's branch (no bundle: only missing ones; the game code's version is
/// featuresOutdated()'s business).
[[nodiscard]] ModBinState checkModBin(const std::filesystem::path& modDir, const FeatureBundle* bundle);

} // namespace sme
