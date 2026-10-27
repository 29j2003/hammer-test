// text.cpp - String and path helpers (see text.h).

#include <sme/text.h>

#include <algorithm>
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
