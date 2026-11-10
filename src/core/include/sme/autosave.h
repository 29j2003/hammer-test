// autosave.h - Names and bookkeeping of automatic backups of a map (no Qt, no file writing).
//
// The editor writes a copy of the open map every few minutes into an "autosave" folder next to
// the map: <map>_<YYYYMMDD-HHMMSS>.vmf (older versions wrote <map>.<stamp>.vmf; both are read).
// No dot before the stamp: the compile tools take everything after the last dot for the file
// extension. Only the newest few are kept. After a crash the newest
// copy that is younger than the map file is offered back. On every save the previous version of
// the map is kept as <map>.vmx (as Hammer does).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sme {

/// A point in time as written into autosave names (local time of the PC).
struct AutosaveStamp {
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

	/// "20261027-213005": sorts like the time it stands for.
	[[nodiscard]] std::string text() const;
	/// Reads "YYYYMMDD-HHMMSS"; nullopt if the text is not exactly that.
	[[nodiscard]] static std::optional<AutosaveStamp> parse(std::string_view text);
	[[nodiscard]] auto operator<=>(const AutosaveStamp&) const = default;
};

/// One autosave file found on disk.
struct AutosaveEntry {
	std::filesystem::path path;
	std::string mapName;      ///< The map's file name without ".vmf" ("de_test")
	AutosaveStamp stamp;
};

/// Folder the autosaves of a map file go to: "<map folder>/autosave".
[[nodiscard]] std::filesystem::path autosaveFolder(const std::filesystem::path& mapFile);
/// File name of an autosave: "<mapName>_<stamp>.vmf".
[[nodiscard]] std::string autosaveFileName(std::string_view mapName, const AutosaveStamp& stamp);
/// Splits an autosave file name ("<mapName>_<stamp>.vmf" or the older "<mapName>.<stamp>.vmf")
/// back into map name and stamp; nullopt for other files.
[[nodiscard]] std::optional<AutosaveEntry> parseAutosaveName(const std::filesystem::path& file);
/// Autosaves of one map in `folder`, newest first. A missing folder gives an empty list.
[[nodiscard]] std::vector<AutosaveEntry> listAutosaves(const std::filesystem::path& folder, std::string_view mapName);
/// The autosaves to delete so that only the newest `keep` remain (`list` newest first).
[[nodiscard]] std::vector<std::filesystem::path> autosavesToRemove(const std::vector<AutosaveEntry>& list, std::size_t keep);
/// Map name and stamp of an autosave file name (just the name, UTF-8); nullopt for other names.
[[nodiscard]] std::optional<std::pair<std::string, AutosaveStamp>> splitAutosaveName(std::string_view fileName);
/// autosaveOriginal for a UTF-8 path string (either slash; the result keeps the slashes used):
/// the map's path if `path` is an autosave, else `path` unchanged.
[[nodiscard]] std::string withoutAutosave(std::string_view path);
/// The map an autosave belongs to, if `file` is one: an autosave name inside a folder called
/// "autosave" gives "<folder above>/<mapName>.vmf". Opening such a file edits that map (Save
/// writes it), and the recent list keeps the map, never the autosave. nullopt for other files.
[[nodiscard]] std::optional<std::filesystem::path> autosaveOriginal(const std::filesystem::path& file);
/// Where the previous version of a map is kept on save: the same name with ".vmx".
[[nodiscard]] std::filesystem::path backupPath(const std::filesystem::path& mapFile);

} // namespace sme
