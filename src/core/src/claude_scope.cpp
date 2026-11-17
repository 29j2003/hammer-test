// claude_scope.cpp - Checks for everything Claude sends through the Claude integration (see
// claude_scope.h): names, paths, materials, VMT parameters, decision lines and the rate limit.

#include <sme/claude_scope.h>

#include <sme/material_decisions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <system_error>

namespace sme {

namespace {

namespace fs = std::filesystem;

void say(std::string* why, std::string text) {
	if (why) *why = std::move(text);
}

std::string lower(std::string_view text) {
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return out;
}

/// Splits on '/' and '\'.
std::vector<std::string> splitParts(std::string_view name) {
	std::vector<std::string> parts;
	std::string part;
	for (const char c : name) {
		if (c == '/' || c == '\\') {
			parts.push_back(part);
			part.clear();
		} else {
			part += c;
		}
	}
	parts.push_back(part);
	return parts;
}

/// A part of a name: printable ASCII or UTF-8, none of the characters Windows forbids in names.
bool plainPart(const std::string& part, std::string* why) {
	if (part.empty()) return say(why, "empty folder or file name"), false;
	if (part == "." || part == "..") return say(why, "\".\" and \"..\" are not allowed"), false;
	if (part.size() > 100) return say(why, "a name is longer than 100 characters"), false;
	for (const char ch : part) {
		const auto c = static_cast<unsigned char>(ch);
		if (c < 0x20 || c == 0x7f) return say(why, "control characters are not allowed"), false;
		if (std::string_view("<>:\"|?*").find(char(c)) != std::string_view::npos) {
			return say(why, std::string("the character '") + char(c) + "' is not allowed"), false;
		}
	}
	if (reservedWindowsName(part)) return say(why, "\"" + part + "\" is a reserved name on Windows"), false;
	return true;
}

/// Canonical as far as the path exists (weakly_canonical), with a fallback that never throws.
fs::path canonicalish(const fs::path& path) {
	std::error_code ec;
	fs::path out = fs::weakly_canonical(path, ec);
	if (ec) out = fs::absolute(path, ec).lexically_normal();
	return out;
}

fs::path fromUtf8(const std::string& text) {
	return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

/// Compares path parts (case-insensitive on Windows, where the file system is).
bool samePart(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
	return lower(a.string()) == lower(b.string());
#else
	return a == b;
#endif
}

} // namespace

bool reservedWindowsName(std::string_view part) {
	if (part.empty()) return false;
	if (part.back() == '.' || part.back() == ' ') return true;
	// The part before the first dot, without trailing spaces (Windows ignores them: "CON .txt").
	std::string stem = lower(part.substr(0, part.find('.')));
	while (!stem.empty() && stem.back() == ' ') stem.pop_back();
	static const std::set<std::string> names = {"con", "prn", "aux", "nul", "conin$", "conout$"};
	if (names.count(stem)) return true;
	if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9') return true;
	// COM¹ COM² COM³ (superscript digits, UTF-8 C2 B9 / C2 B2 / C2 B3) are reserved as well.
	if (stem.size() == 5 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && static_cast<unsigned char>(stem[3]) == 0xC2) {
		const auto d = static_cast<unsigned char>(stem[4]);
		if (d == 0xB9 || d == 0xB2 || d == 0xB3) return true;
	}
	return false;
}

std::optional<std::string> checkExchangeName(std::string_view name, std::string* why) {
	if (name.empty()) return say(why, "no file name given"), std::nullopt;
	if (name.size() > CLAUDE_NAME_MAX) return say(why, "the name is too long"), std::nullopt;
	if (name.front() == '/' || name.front() == '\\') return say(why, "absolute paths are not allowed - give a name inside the exchange folder"), std::nullopt;
	if (name.size() >= 2 && name[1] == ':') return say(why, "drive paths are not allowed - give a name inside the exchange folder"), std::nullopt;
	const auto parts = splitParts(name);
	if (parts.size() > 4) return say(why, "at most three sub-folders"), std::nullopt;
	std::string out;
	for (const auto& part : parts) {
		if (!plainPart(part, why)) return std::nullopt;
		if (!out.empty()) out += '/';
		out += part;
	}
	return out;
}

bool exchangeExtensionAllowed(std::string_view name) {
	const std::string n = lower(name);
	const auto dot = n.rfind('.');
	if (dot == std::string::npos || n.find('/', dot) != std::string::npos) return false;
	static const std::set<std::string> allowed = {".txt", ".md", ".csv", ".json", ".log", ".vmt"};
	return allowed.count(n.substr(dot)) > 0;
}

bool pathInside(const fs::path& base, const fs::path& path) {
	const fs::path b = canonicalish(base);
	const fs::path p = canonicalish(path);
	auto bi = b.begin();
	auto pi = p.begin();
	for (; bi != b.end(); ++bi, ++pi) {
		if (bi->empty()) continue;   // trailing separator
		if (pi == p.end() || !samePart(*bi, *pi)) return false;
	}
	return true;
}

std::optional<fs::path> resolveInside(const fs::path& base, std::string_view relative, std::string* why) {
	std::error_code ec;
	if (!fs::is_directory(base, ec)) return say(why, "the folder is missing"), std::nullopt;
	const auto name = checkExchangeName(relative, why);
	if (!name) return std::nullopt;
	const fs::path root = fs::canonical(base, ec);
	if (ec) return say(why, "the folder cannot be read"), std::nullopt;
	const fs::path relativePath = fromUtf8(*name);
	const fs::path target = root / relativePath;
	// Every existing step: no link or junction may lead out (checked on the canonical path).
	if (!pathInside(root, target)) return say(why, "the name leads outside the allowed folder"), std::nullopt;
	fs::path step = root;
	for (const auto& part : relativePath) {
		step /= part;
		const auto status = fs::symlink_status(step, ec);
		if (ec || status.type() == fs::file_type::not_found) break;
		// Links, junctions (MSVC reports them as their own type) and devices are not followed.
		if (status.type() != fs::file_type::directory && status.type() != fs::file_type::regular) {
			return say(why, "links and special files are not followed"), std::nullopt;
		}
	}
	return canonicalish(target);
}

std::optional<std::string> checkMaterialName(std::string_view name, std::string* why) {
	std::string n = lower(name);
	std::replace(n.begin(), n.end(), '\\', '/');
	while (!n.empty() && n.front() == '/') n.erase(0, 1);
	if (n.compare(0, 10, "materials/") == 0) n.erase(0, 10);
	if (n.size() > 4 && n.compare(n.size() - 4, 4, ".vmt") == 0) n.resize(n.size() - 4);
	if (n.empty()) return say(why, "no material name given"), std::nullopt;
	if (n.find(':') != std::string::npos) return say(why, "\":\" is not allowed in a material name"), std::nullopt;
	if (n.find('*') != std::string::npos || n.find('?') != std::string::npos) return say(why, "wildcards are not allowed here - name one material"), std::nullopt;
	const auto checked = checkExchangeName(n, why);
	if (!checked) return std::nullopt;
	if (checked->size() > 150) return say(why, "the material name is too long"), std::nullopt;
	return checked;
}

std::optional<fs::path> modMaterialFile(const fs::path& modDir, std::string_view name, std::string* why) {
	const auto material = checkMaterialName(name, why);
	if (!material) return std::nullopt;
	std::error_code ec;
	const fs::path materials = modDir / "materials";
	if (!fs::is_directory(materials, ec)) return say(why, "the mod has no materials folder"), std::nullopt;
	const auto file = resolveInside(materials, *material + ".vmt", why);
	if (!file) return std::nullopt;
	const auto status = fs::symlink_status(*file, ec);
	if (ec || !fs::is_regular_file(status)) {
		return say(why, "the mod has no own file materials/" + *material + ".vmt (materials in VPKs and the base game cannot be changed)"), std::nullopt;
	}
	return file;
}

std::optional<std::string> checkVmtParameter(std::string_view key, std::string* why) {
	if (key.size() < 2 || key.size() > 64 || key.front() != '$') return say(why, "a parameter starts with $ and has 1 to 63 more characters"), std::nullopt;
	for (std::size_t i = 1; i < key.size(); ++i) {
		const auto c = static_cast<unsigned char>(key[i]);
		if (!std::isalnum(c) && c != '_') return say(why, "a parameter has only letters, digits and _"), std::nullopt;
	}
	return lower(key);
}

bool checkVmtValue(std::string_view value, std::string* why) {
	if (value.size() > CLAUDE_VALUE_MAX) return say(why, "the value is too long"), false;
	for (const char ch : value) {
		const auto c = static_cast<unsigned char>(ch);
		if (c < 0x20 || c == 0x7f) return say(why, "line breaks and control characters are not allowed"), false;
		if (c == '"' || c == '{' || c == '}') return say(why, "quotes and braces are not allowed in a value"), false;
	}
	if (value.find("//") != std::string_view::npos) return say(why, "\"//\" is not allowed in a value"), false;
	return true;
}

std::optional<std::string> decisionLineFromClaude(std::string_view pattern, const std::vector<std::pair<std::string, std::string>>& keys, std::string* why) {
	static const std::set<std::string> known = {"role", "windows", "floors", "windowrects", "scale", "parallax", "depth", "pattern", "bump", "bumpstrength", "note"};
	if (pattern.empty() || pattern.size() > 150) return say(why, "give a material name or wildcard (at most 150 characters)"), std::nullopt;
	for (const char ch : pattern) {
		const auto c = static_cast<unsigned char>(ch);
		if (c <= 0x20 || c == 0x7f || c == '"' || c == '=') return say(why, "the material name or wildcard has spaces, quotes or '='"), std::nullopt;
	}
	if (pattern.find("//") != std::string_view::npos || pattern.find("..") != std::string_view::npos) {
		return say(why, "\"//\" and \"..\" are not allowed in a material name"), std::nullopt;
	}
	if (keys.empty()) return say(why, "no keys given"), std::nullopt;
	std::string line(pattern);
	std::set<std::string> seen;
	for (const auto& [rawKey, value] : keys) {
		const std::string key = lower(rawKey);
		if (!known.count(key)) return say(why, "unknown key \"" + rawKey + "\""), std::nullopt;
		if (!seen.insert(key).second) return say(why, "the key \"" + key + "\" is given twice"), std::nullopt;
		if (value.empty() || value.size() > CLAUDE_VALUE_MAX) return say(why, "the value of " + key + " is empty or too long"), std::nullopt;
		for (const char ch : value) {
			const auto c = static_cast<unsigned char>(ch);
			if (c < 0x20 || c == 0x7f || c == '"') return say(why, "the value of " + key + " has quotes or line breaks"), std::nullopt;
			if (key != "note" && c == ' ') return say(why, "the value of " + key + " has a space"), std::nullopt;
		}
		if (value.find("//") != std::string::npos) return say(why, "\"//\" is not allowed in a value"), std::nullopt;
		line += ' ' + key + '=' + (key == "note" ? '"' + value + '"' : value);
	}
	// The decision file must read it back exactly: one entry, this pattern, no problem.
	const MaterialDecisions parsed = MaterialDecisions::parse(line);
	if (!parsed.problems().empty()) return say(why, parsed.problems().front().message), std::nullopt;
	if (parsed.entryCount() != 1 || parsed.lines().size() != 1 || parsed.lines().front().pattern != decisionKey(pattern)) {
		return say(why, "the decision file would not read this line back"), std::nullopt;
	}
	if (!parsed.lines().front().decision.extra.empty()) return say(why, "unknown key"), std::nullopt;
	return line;
}

bool ClaudeRateLimit::allow(Clock::time_point now) {
	while (!m_times.empty() && now - m_times.front() >= m_window) m_times.pop_front();
	if (int(m_times.size()) >= m_count) return false;
	m_times.push_back(now);
	return true;
}

int ClaudeRateLimit::waitSeconds(Clock::time_point now) const {
	if (int(m_times.size()) < m_count) return 0;
	const auto oldest = m_times[m_times.size() - std::size_t(m_count)];
	const auto left = std::chrono::duration_cast<std::chrono::seconds>(oldest + m_window - now).count();
	return int(std::max<long long>(0, left + 1));
}

bool constantTimeEquals(std::string_view a, std::string_view b) {
	unsigned char diff = a.size() == b.size() ? 0 : 1;
	const std::size_t n = std::max(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i) {
		const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
		const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
		diff |= static_cast<unsigned char>(x ^ y);
	}
	return diff == 0;
}

} // namespace sme
