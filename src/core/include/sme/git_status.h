// git_status.h - Reading what the system's git reports, for the Source control panel: the
// branch and its upstream, changed files (`git status --porcelain=v2 --branch -z`), recent
// commits (`git log` with gitLogFormat()) and the web page of a remote. Author e-mail addresses
// are never asked for, so they cannot show up anywhere.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// One changed file in `git status`.
struct GitFileChange {
	enum class Kind { Changed, Renamed, Untracked, Conflicted, Ignored };

	Kind kind = Kind::Changed;
	std::string path;            ///< Relative to the repository root, '/' separated
	std::string originalPath;    ///< Renames and copies: where it came from
	char staged = ' ';           ///< Index state: 'M', 'A', 'D', 'R', 'C', 'T' or ' '
	char unstaged = ' ';         ///< Work tree state, same letters

	/// Has changes in the index (staged).
	[[nodiscard]] bool isStaged() const { return staged != ' '; }
	/// One letter for lists: 'A' added, 'M' modified, 'D' deleted, 'R' renamed, 'U' conflict, '?' new.
	[[nodiscard]] char letter() const;
};

/// The repository state: branch, upstream, ahead/behind and the changed files.
struct GitStatus {
	std::string branch;          ///< Empty when detached
	std::string commit;          ///< Current commit id, empty before the first commit
	std::string upstream;        ///< "origin/main", empty without one
	int ahead = 0, behind = 0;
	bool detached = false;
	std::vector<GitFileChange> files;
};

/// Parses `git status --porcelain=v2 --branch -z`. nullopt if the text is not that format.
[[nodiscard]] std::optional<GitStatus> parseGitStatus(std::string_view porcelainZ);

/// One commit of `git log` (no e-mail addresses are read).
struct GitCommit {
	std::string hash;            ///< Short id
	std::string author;          ///< Name only
	std::string when;            ///< "3 days ago"
	std::string subject;
};

/// The `--format=` for `git log` that parseGitLog() reads (fields split by 0x1f, commits by 0x1e).
[[nodiscard]] std::string_view gitLogFormat();
[[nodiscard]] std::vector<GitCommit> parseGitLog(std::string_view text);

/// Browser address of a remote: the scp form "user@host:owner/repo.git", "ssh://user@host/owner/repo"
/// and "https://user:token@host/owner/repo.git" all become "https://host/owner/repo" (log-ins are
/// dropped). Empty for local paths and anything else.
[[nodiscard]] std::string remoteWebUrl(std::string_view remote);

} // namespace sme
