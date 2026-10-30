// text.cpp - String and path helpers (see text.h).

#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sme {

namespace {

char lowerChar(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

std::string toLower(std::string_view text) {
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(), lowerChar);
	return out;
}

bool iequals(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return lowerChar(x) == lowerChar(y);
	});
}

bool startsWithI(std::string_view text, std::string_view prefix) {
	return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

bool endsWithI(std::string_view text, std::string_view suffix) {
	return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

std::string_view trim(std::string_view text) {
	constexpr std::string_view WHITESPACE = " \t\r\n";
	const auto first = text.find_first_not_of(WHITESPACE);
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = text.find_last_not_of(WHITESPACE);
	return text.substr(first, last - first + 1);
}

std::vector<std::string> splitTrimmed(std::string_view text, char separator) {
	std::vector<std::string> parts;
	std::size_t start = 0;
	while (start <= text.size()) {
		auto end = text.find(separator, start);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		const auto part = trim(text.substr(start, end - start));
		if (!part.empty()) {
			parts.emplace_back(part);
		}
		start = end + 1;
	}
	return parts;
}

std::string shortSummary(std::string_view text, std::size_t fullLimit, std::size_t maxChars) {
	text = trim(text);
	if (text.size() <= fullLimit) {
		return std::string(text);
	}
	const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
	for (std::size_t i = 0; i + 1 < text.size() && i < maxChars; ++i) {
		if (text[i] != '.' && text[i] != '!' && text[i] != '?') {
			continue;
		}
		std::size_t next = i + 1;
		if (!isSpace(text[next])) {
			continue;
		}
		while (next < text.size() && isSpace(text[next])) {
			++next;
		}
		const char c = next < text.size() ? text[next] : 'A';
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '(' || c == '"' || c == '<') {
			return std::string(text.substr(0, i + 1));
		}
	}
	std::size_t cut = maxChars;
	while (cut > 0 && !isSpace(text[cut])) {
		--cut;
	}
	if (cut == 0) {
		cut = maxChars;   // one long word: cut it anyway
	}
	std::string_view head = trim(text.substr(0, cut));
	while (!head.empty() && (head.back() == ',' || head.back() == ';' || head.back() == ':')) {
		head.remove_suffix(1);
	}
	return std::string(head) + "\u2026";
}

namespace {

bool isWordChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

/// At `pos` a word ends: "(s)" / "(es)" after it, or "/other" (vertex/vertices). Returns the
/// length of that marker part and fills in the plural ending or the plural word.
bool pluralMarker(std::string_view text, std::size_t wordStart, std::size_t wordEnd, bool one, std::string& out, std::size_t& end) {
	const std::string_view word = text.substr(wordStart, wordEnd - wordStart);
	for (const std::string_view suffix : {std::string_view("(s)"), std::string_view("(es)")}) {
		if (text.substr(wordEnd, suffix.size()) == suffix) {
			out = std::string(word);
			if (!one) out += suffix.substr(1, suffix.size() - 2);
			end = wordEnd + suffix.size();
			return true;
		}
	}
	if (wordEnd < text.size() && text[wordEnd] == '/') {
		std::size_t other = wordEnd + 1;
		while (other < text.size() && isWordChar(text[other])) ++other;
		if (other > wordEnd + 1) {
			out = std::string(one ? word : text.substr(wordEnd + 1, other - wordEnd - 1));
			end = other;
			return true;
		}
	}
	return false;
}

} // namespace

std::string englishPlural(std::string_view text, int n) {
	const std::size_t at = text.find("%n ");
	if (at == std::string_view::npos || n < 0) return std::string(text);
	const bool one = n == 1;
	std::string result(text.substr(0, at + 3));
	std::size_t pos = at + 3;
	bool found = false;
	// The counted noun: one of the next three words (a few words like "new" or "entity" may come first).
	for (int words = 0; words < 3 && pos < text.size(); ++words) {
		std::size_t end = pos;
		while (end < text.size() && isWordChar(text[end])) ++end;
		if (end == pos) break;
		std::string replaced;
		std::size_t after = 0;
		if (pluralMarker(text, pos, end, one, replaced, after)) {
			result += replaced;
			pos = after;
			found = true;
			break;
		}
		result.append(text.substr(pos, end - pos));
		pos = end;
		if (pos >= text.size() || text[pos] != ' ') break;
		result += ' ';
		++pos;
	}
	// "face(s) or overlay(s)": the nouns joined with " or " count the same.
	while (found && text.substr(pos, 4) == " or ") {
		std::size_t start = pos + 4, end = start;
		while (end < text.size() && isWordChar(text[end])) ++end;
		std::string replaced;
		std::size_t after = 0;
		if (end == start || !pluralMarker(text, start, end, one, replaced, after)) break;
		result += " or " + replaced;
		pos = after;
	}
	result.append(text.substr(pos));
	return result;
}

std::string normalizePath(std::string_view path) {
	std::string out;
	out.reserve(path.size());
	for (const char c : path) {
		const char n = (c == '\\') ? '/' : lowerChar(c);
		if (n == '/' && (out.empty() || out.back() == '/')) {
			continue;
		}
		out.push_back(n);
	}
	while (out.starts_with("./")) {
		out.erase(0, 2);
	}
	if (out == ".") {
		out.clear();
	}
	if (!out.empty() && out.back() == '/') {
		out.pop_back();
	}
	return out;
}

std::filesystem::path toNativePath(std::string_view path) {
	std::string cleaned;
	cleaned.reserve(path.size());
	for (std::size_t i = 0; i < path.size(); ++i) {
		const char c = (path[i] == '\\') ? '/' : path[i];
		// Keep a leading "//" (UNC share), collapse every other doubled separator.
		if (c == '/' && !cleaned.empty() && cleaned.back() == '/' && cleaned.size() > 1) {
			continue;
		}
		cleaned.push_back(c);
	}
	return std::filesystem::path(cleaned).lexically_normal();
}

std::string displayPath(const std::filesystem::path& path) {
	auto text = path.generic_u8string();
	return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::optional<std::string> readFileText(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

std::optional<std::filesystem::path> findChildI(const std::filesystem::path& dir, std::string_view name) {
	std::error_code ec;
	const auto direct = dir / std::filesystem::path(std::string(name));
	if (std::filesystem::exists(direct, ec)) {
		return direct;
	}
	for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
		if (iequals(displayPath(it->path().filename()), name)) {
			return it->path();
		}
	}
	return std::nullopt;
}

std::optional<std::filesystem::path> resolveI(const std::filesystem::path& base, std::string_view relative) {
	std::string slashed(relative);
	std::replace(slashed.begin(), slashed.end(), '\\', '/');
	std::filesystem::path current = base;
	for (const auto& segment : splitTrimmed(slashed, '/')) {
		if (segment == ".") {
			continue;
		}
		if (segment == "..") {
			current = current.parent_path();
			continue;
		}
		auto next = findChildI(current, segment);
		if (!next) {
			return std::nullopt;
		}
		current = *next;
	}
	return current;
}

} // namespace sme
