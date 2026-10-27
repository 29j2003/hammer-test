// shipping.h - Getting a map or the whole mod ready to share: which files a map needs and where
// they come from (the mod's own folder, another mod folder, the base game, or nowhere), a list
// for bspzip to pack them into the BSP, a ZIP writer for share packages and mod packages, and
// the item file steamcmd uploads to the Steam Workshop.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/filesystem.h>
#include <sme/map.h>

namespace sme {

/// One file a map needs.
struct ShipFile {
	enum class Kind { Material, Texture, Model, Sound, Particles, Soundscape, Other };
	/// Custom: in the mod's own folder (players do not have it). Shared: another folder outside the
	/// installed games (e.g. a shared content mod). BaseGame: the game the mod is built on.
	enum class Origin { Custom, Shared, BaseGame, Missing };

	std::string path;                   ///< Game path, lowercase ("materials/demo/brick.vmt")
	Kind kind = Kind::Other;
	Origin origin = Origin::Missing;
	std::string neededBy;               ///< Why: "face material", "model models/x.mdl", ...
	std::optional<std::size_t> mount;   ///< Mount that provides it
	std::uint64_t size = 0;             ///< Bytes (0 when missing)
};

/// Every file a map needs, with where it comes from, plus the problems found.
struct MapDependencies {
	std::vector<ShipFile> files;        ///< Sorted by path, each path once
	std::vector<std::string> problems;  ///< Missing files and why they are needed

	/// How many files / bytes come from `origin`.
	[[nodiscard]] std::size_t count(ShipFile::Origin origin) const;
	[[nodiscard]] std::uint64_t bytes(ShipFile::Origin origin) const;
};

/// Every file the map needs: face, overlay, decal and sprite materials with the textures and
/// materials their parameters name (also through patch includes), the 2D sky (all six faces, with
/// the _hdr set if there is one), models with their .vvd/.vtx/.phy/.ani files, include models and
/// materials, sounds from entity keys (wave files and sound script entries), particle files of the
/// used effects, soundscapes with their sounds and detail sprites. `modDir` is the mod's own folder
/// (decides Custom against Shared); empty = every folder outside the installed games is Custom.
[[nodiscard]] MapDependencies mapDependencies(const MapDocument& doc, const GameFileSystem& fs, const std::filesystem::path& modDir = {});

/// Where a file of a directory mount really is on disk (the file system ignores case; the disk may
/// not). nullopt for files in VPKs or when it cannot be found.
[[nodiscard]] std::optional<std::filesystem::path> fileOnDisk(const GameFileSystem& fs, const ShipFile& file);

/// The file list for `bspzip -addlist <bsp> <list> <out bsp>`: game path, then disk path, per file.
[[nodiscard]] std::string bspzipFileList(const std::vector<std::pair<std::string, std::filesystem::path>>& files);

/// One entry of a package: its name inside the package and its contents (bytes, or a file on disk).
struct PackageEntry {
	std::string name;                       ///< "maps/demo.bsp" (forward slashes)
	std::filesystem::path file;             ///< Read from disk when set
	std::vector<std::byte> data;            ///< Otherwise these bytes
};

/// Writes a ZIP (stored, no compression - BSPs and textures hardly shrink, and every tool opens it).
/// `progress(done, total)` may be empty. False with `error` on failure.
[[nodiscard]] bool writeZip(const std::filesystem::path& zip, const std::vector<PackageEntry>& entries, std::string* error = nullptr,
                            const std::function<void(std::size_t, std::size_t)>& progress = {});
/// Names of the entries of a ZIP written by writeZip (for checks); empty if it cannot be read.
[[nodiscard]] std::vector<std::string> zipEntryNames(const std::filesystem::path& zip);
/// The same entries as plain files below `folder` (for a Workshop content folder).
[[nodiscard]] bool writeFolder(const std::filesystem::path& folder, const std::vector<PackageEntry>& entries, std::string* error = nullptr);

/// Package of one map for players: maps/<name>.bsp, every Custom file and an optional readme.txt.
[[nodiscard]] std::vector<PackageEntry> sharePackage(const std::filesystem::path& bsp, const MapDependencies& deps, const GameFileSystem& fs,
                                                     std::string_view readme = {});

/// The mod folder for players, without what only the mapper needs.
struct ModPackagePlan {
	std::vector<std::filesystem::path> files;                          ///< Relative to the mod folder
	std::vector<std::pair<std::filesystem::path, std::string>> excluded; ///< Relative path, reason
	std::uint64_t bytes = 0;
};
/// Why a mod file stays out of a package ("map sources", "log", ...), or empty when it goes in.
[[nodiscard]] std::string developmentFileReason(const std::filesystem::path& relative);
[[nodiscard]] ModPackagePlan planModPackage(const std::filesystem::path& modDir);
/// Entries for writeZip: every planned file below a folder named like the mod ("mymod/gameinfo.txt").
[[nodiscard]] std::vector<PackageEntry> modPackage(const std::filesystem::path& modDir, const ModPackagePlan& plan);

/// A Steam Workshop item for `steamcmd +workshop_build_item <file>`.
struct WorkshopItem {
	std::uint32_t appId = 0;
	std::uint64_t publishedFileId = 0;     ///< 0 = a new item
	std::filesystem::path contentFolder;
	std::filesystem::path previewFile;
	int visibility = 0;                    ///< 0 public, 1 friends only, 2 private, 3 unlisted
	std::string title, description, changeNote;
};
/// The item file (VDF) for `steamcmd +workshop_build_item`.
[[nodiscard]] std::string workshopItemVdf(const WorkshopItem& item);

} // namespace sme
