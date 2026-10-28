// recent_projects.h - What the start page offers to continue: the maps worked on lately, each
// with the game or mod it belongs to. Newest first, one entry per map, a handful at most. Kept
// as plain text lines ("<unix time>\t<game key>\t<map path>") in the editor's settings.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// One project worked on: a map of a game or mod.
struct RecentProject {
	std::string gameKey;   ///< "sourcemods/mymod" (as Editor::openGame takes it)
	std::string mapPath;   ///< The .vmf, absolute
	std::int64_t time = 0; ///< Last opened or saved (seconds since 1970)
	bool operator==(const RecentProject&) const = default;
};

/// `list` with `project` on top: an older entry of the same map is replaced (paths compared
/// without case and slash direction), the list is cut to `limit`.
[[nodiscard]] std::vector<RecentProject> rememberProject(std::vector<RecentProject> list, RecentProject project, std::size_t limit = 12);
/// `list` without the map (it was deleted or moved).
[[nodiscard]] std::vector<RecentProject> forgetProject(std::vector<RecentProject> list, std::string_view mapPath);

[[nodiscard]] std::string writeRecentProjects(const std::vector<RecentProject>& list);
/// Reads what writeRecentProjects wrote; broken lines are skipped, the order is kept.
[[nodiscard]] std::vector<RecentProject> parseRecentProjects(std::string_view text);

} // namespace sme
