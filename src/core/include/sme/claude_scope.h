// claude_scope.h - What Claude may touch through Mapsmith's Claude integration (Project › Claude,
// the local MCP bridge `mapsmith-mcp`). Everything Claude sends is checked here before the app
// reads or writes a file: names in the fixed exchange folder (no way out of it: no "..", no
// absolute or drive paths, no UNC, no ":" streams, no reserved Windows names, no links that lead
// elsewhere - checked on the canonical path), materials of the open mod (only existing .vmt files
// in <mod>/materials, never a VPK), parameters and values of a VMT, lines of the decision file,
// and how often Claude may change something (a rate limit). Nothing here knows Qt, so all of it
// is tested with doctest (tests/test_claude_scope.cpp).

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sme {

/// Longest name Claude may give for a file in the exchange folder (with sub-folders).
inline constexpr std::size_t CLAUDE_NAME_MAX = 200;
/// Largest file Claude may write into the exchange folder.
inline constexpr std::size_t CLAUDE_WRITE_MAX = 1024 * 1024;
/// Largest file Claude may read from the exchange folder (text).
inline constexpr std::size_t CLAUDE_READ_MAX = 1024 * 1024;
/// Longest VMT value and decision note Claude may set.
inline constexpr std::size_t CLAUDE_VALUE_MAX = 256;

/// A relative name in the exchange folder ("notes/today.txt"), checked: '/' or '\' separated,
/// at most 4 levels, each part a plain name. Returns the name with '/' separators, or nullopt and
/// why (English, for the tool result).
[[nodiscard]] std::optional<std::string> checkExchangeName(std::string_view name, std::string* why = nullptr);
/// Whether Claude may write a file of this name: only plain data (.txt .md .csv .json .log .vmt).
[[nodiscard]] bool exchangeExtensionAllowed(std::string_view name);
/// One part of a path is a name Windows treats specially (CON, PRN, AUX, NUL, COM1-9, LPT1-9,
/// also with an extension or trailing dots and spaces) or ends with a dot or space.
[[nodiscard]] bool reservedWindowsName(std::string_view part);

/// `relative` inside `base`: the path, if it stays inside `base` after following every link and
/// junction that exists (canonical paths; case-insensitive on Windows). `base` must exist.
/// nullopt (and why) when the name leads outside or `base` is missing.
[[nodiscard]] std::optional<std::filesystem::path> resolveInside(const std::filesystem::path& base, std::string_view relative,
                                                                 std::string* why = nullptr);
/// Whether `path` lies inside `base` (both made canonical as far as they exist).
[[nodiscard]] bool pathInside(const std::filesystem::path& base, const std::filesystem::path& path);

/// A material name of the mod ("brick/brickwall01"; "materials/" and ".vmt" are taken off, '\'
/// becomes '/'), checked like an exchange name; lower case. nullopt and why when it is not one.
[[nodiscard]] std::optional<std::string> checkMaterialName(std::string_view name, std::string* why = nullptr);
/// The mod's own VMT of a material Claude may change: <modDir>/materials/<name>.vmt must exist as a
/// regular file inside <modDir>/materials (not through a link that leads elsewhere). VPKs and the
/// base game's files are never offered.
[[nodiscard]] std::optional<std::filesystem::path> modMaterialFile(const std::filesystem::path& modDir, std::string_view name, std::string* why = nullptr);

/// A material parameter Claude may set: "$" followed by 1..63 letters, digits or "_" (lower case
/// returned). Proxies, patches and other blocks are not reachable.
[[nodiscard]] std::optional<std::string> checkVmtParameter(std::string_view key, std::string* why = nullptr);
/// A value for it: at most CLAUDE_VALUE_MAX characters, printable, no quotes, braces, line
/// breaks or "//" (that would start a comment or a new key in the file).
[[nodiscard]] bool checkVmtValue(std::string_view value, std::string* why = nullptr);

/// One line for the decision file from Claude: a material name or wildcard and its keys
/// (role, windows, floors, windowrects, scale, parallax, depth, pattern, bump, bumpstrength,
/// note). Returns the line's text ("brick/* parallax=off") when the decision file reads it back
/// with no problem and exactly this pattern, else nullopt and why. Values: no spaces (note may
/// have them and is quoted), no quotes, no "//", no line breaks.
[[nodiscard]] std::optional<std::string> decisionLineFromClaude(std::string_view pattern,
                                                                const std::vector<std::pair<std::string, std::string>>& keys,
                                                                std::string* why = nullptr);

/// How often Claude may change something: at most `count` changes in `window` (a sliding window).
class ClaudeRateLimit {
public:
	using Clock = std::chrono::steady_clock;
	explicit ClaudeRateLimit(int count = 30, std::chrono::seconds window = std::chrono::seconds(60)) : m_count(count), m_window(window) {}
	/// Whether one more change is allowed now (counts it when it is).
	bool allow(Clock::time_point now = Clock::now());
	/// Seconds until the next change is allowed (0 when one is allowed now).
	[[nodiscard]] int waitSeconds(Clock::time_point now = Clock::now()) const;

private:
	int m_count;
	std::chrono::seconds m_window;
	std::deque<Clock::time_point> m_times;
};

/// Compares two strings in time that does not depend on where they differ (for the token).
[[nodiscard]] bool constantTimeEquals(std::string_view a, std::string_view b);

} // namespace sme
