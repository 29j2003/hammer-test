// filesystem.h - The game's virtual file system: folders and VPK archives in search order.
//
// Mirrors what the engine does with gameinfo.txt: the first mount that has a file wins.
// All lookups are case-insensitive and use forward slashes, on every platform.
// Reading is thread-safe so asset thumbnails can load in the background.

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/gameinfo.h>

namespace sme {

/// One folder or VPK in the search order.
struct Mount {
	enum class Kind { Directory, Vpk };

	Kind kind = Kind::Directory;
	std::filesystem::path path;         ///< Folder, or the *_dir.vpk file
	std::vector<std::string> pathIds;   ///< Lowercase search path ids ("game", "mod", ...)
	std::string source;                 ///< gameinfo.txt line it came from (for diagnostics)
	bool present = false;               ///< false if the folder/VPK does not exist or failed to open
	std::string problem;                ///< Why it is not present
};

/// The game's files as the engine sees them: folders and VPKs in search path order, first one wins.
/// Reads are safe from several threads (the background loaders share one instance).
class GameFileSystem {
public:
	GameFileSystem();
	~GameFileSystem();
	GameFileSystem(GameFileSystem&&) noexcept;
	GameFileSystem& operator=(GameFileSystem&&) noexcept;

	/// Mounts every resolved search path of `info` in order. Missing paths are kept
	/// (present = false) so the user can see what is wrong.
	[[nodiscard]] static GameFileSystem fromGameInfo(const GameInfo& info);

	/// Appends a folder. Like the engine, a pak01_dir.vpk inside the folder is mounted right before it.
	void addDirectory(const std::filesystem::path& dir, std::vector<std::string> pathIds, std::string source = {});

	/// Appends a VPK; accepts "name.vpk" or "name_dir.vpk".
	void addVpk(const std::filesystem::path& vpk, std::vector<std::string> pathIds, std::string source = {});

	/// Every mount in order, with whether it was found.
	[[nodiscard]] const std::vector<Mount>& mounts() const;

	/// Index of the mount that provides `path`, or empty if no mount has it.
	/// `pathId` filters mounts by search path id ("game" matches "game+mod" etc.); empty = all.
	[[nodiscard]] std::optional<std::size_t> locate(std::string_view path, std::string_view pathId = "game") const;

	/// Whether a mount has the file (path relative to the game, '/' separated, case-insensitive).
	[[nodiscard]] bool exists(std::string_view path, std::string_view pathId = "game") const;
	/// The whole file from the first mount that has it, or nullopt.
	[[nodiscard]] std::optional<std::vector<std::byte>> read(std::string_view path, std::string_view pathId = "game") const;
	/// Like read(), as text.
	[[nodiscard]] std::optional<std::string> readText(std::string_view path, std::string_view pathId = "game") const;
	/// The file as the game has it without `folder` (the mod's own folder and VPKs in it): the
	/// original a mod's copy replaces. nullopt if only that folder has it.
	[[nodiscard]] std::optional<std::string> readOriginal(std::string_view path, const std::filesystem::path& folder,
	                                                      std::string_view pathId = "game") const;

	/// All files below `dir` (normalized, lowercase, unique, sorted) with the given extension
	/// (e.g. ".vmt"; empty = any). Searches recursively.
	[[nodiscard]] std::vector<std::string> list(std::string_view dir, std::string_view extension = {}, std::string_view pathId = "game") const;

	/// Tells the file system about a file the editor just wrote into one of the game's folders
	/// (folder contents are indexed once; without this the new file would stay unseen).
	void noteWrittenFile(const std::filesystem::path& file);
	/// The same for a file the editor deleted: the next mount's copy (the game's original) counts again.
	void noteRemovedFile(const std::filesystem::path& file);

	/// Number of files a mount provides (builds its index if needed).
	[[nodiscard]] std::size_t fileCount(std::size_t mountIndex) const;

private:
	struct MountData;
	std::vector<Mount> m_mounts;
	std::vector<std::unique_ptr<MountData>> m_data;

	[[nodiscard]] bool mountMatches(std::size_t index, std::string_view pathId) const;
	[[nodiscard]] std::optional<std::vector<std::byte>> readMount(std::size_t mountIndex, const std::string& relative) const;
	void noteFile(const std::filesystem::path& file, bool exists);
};

} // namespace sme
