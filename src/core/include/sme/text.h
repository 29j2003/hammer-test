// text.h - Small string and path helpers shared by the core library.
//
// Source games treat file paths case-insensitively and accept both slash styles, so
// every lookup in the core goes through normalizePath() first.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// ASCII lowercase copy (Source paths and keys are ASCII in practice).
[[nodiscard]] std::string toLower(std::string_view text);

/// Case-insensitive ASCII comparison.
[[nodiscard]] bool iequals(std::string_view a, std::string_view b);
[[nodiscard]] bool startsWithI(std::string_view text, std::string_view prefix);
[[nodiscard]] bool endsWithI(std::string_view text, std::string_view suffix);

/// Strips leading/trailing whitespace.
[[nodiscard]] std::string_view trim(std::string_view text);

/// Splits on a single character, trimming each part and dropping empty parts.
[[nodiscard]] std::vector<std::string> splitTrimmed(std::string_view text, char separator);

/// Normalizes a game-relative path for lookups: forward slashes, lowercase,
/// no leading "./" or "/", no duplicate or trailing slashes.
[[nodiscard]] std::string normalizePath(std::string_view path);

/// Builds a filesystem path from a path string written by Valve tools
/// (backslashes, doubled separators) without changing its case.
[[nodiscard]] std::filesystem::path toNativePath(std::string_view path);

/// UTF-8 string of a filesystem path with forward slashes (for display, logs and JSON).
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

/// Reads a whole file as bytes-in-a-string; empty optional if it cannot be opened.
[[nodiscard]] std::optional<std::string> readFileText(const std::filesystem::path& path);

/// Finds an existing child of `dir` whose name matches `name` case-insensitively.
/// Needed on Linux/macOS, where "HL2" and "hl2" are different folders.
[[nodiscard]] std::optional<std::filesystem::path> findChildI(const std::filesystem::path& dir, std::string_view name);

/// Resolves a relative path below `base` segment by segment, case-insensitively.
[[nodiscard]] std::optional<std::filesystem::path> resolveI(const std::filesystem::path& base, std::string_view relative);

} // namespace sme
