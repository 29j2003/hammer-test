// autosave.cpp - Autosave names and bookkeeping (see autosave.h).

#include <sme/autosave.h>

#include <sme/text.h>

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace sme {

namespace {

/// Reads `count` digits at `pos`; -1 if any of them is not a digit.
int digits(std::string_view text, std::size_t pos, std::size_t count) {
	int value = 0;
	for (std::size_t i = pos; i < pos + count; ++i) {
		if (text[i] < '0' || text[i] > '9') return -1;
		value = value * 10 + (text[i] - '0');
	}
	return value;
}

constexpr std::size_t STAMP_LENGTH = 15;   // "YYYYMMDD-HHMMSS"

} // namespace

std::string AutosaveStamp::text() const {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%04d%02d%02d-%02d%02d%02d", year, month, day, hour, minute, second);
	return buffer;
}

std::optional<AutosaveStamp> AutosaveStamp::parse(std::string_view text) {
	if (text.size() != STAMP_LENGTH || text[8] != '-') {
		return std::nullopt;
	}
	AutosaveStamp stamp{digits(text, 0, 4), digits(text, 4, 2), digits(text, 6, 2), digits(text, 9, 2), digits(text, 11, 2), digits(text, 13, 2)};
	if (stamp.year < 0 || stamp.month < 1 || stamp.month > 12 || stamp.day < 1 || stamp.day > 31 || stamp.hour < 0 || stamp.hour > 23 ||
	    stamp.minute < 0 || stamp.minute > 59 || stamp.second < 0 || stamp.second > 60) {
		return std::nullopt;
	}
	return stamp;
}

std::filesystem::path autosaveFolder(const std::filesystem::path& mapFile) {
	return mapFile.parent_path() / "autosave";
}

std::string autosaveFileName(std::string_view mapName, const AutosaveStamp& stamp) {
	return std::string(mapName) + "_" + stamp.text() + ".vmf";
}

std::optional<std::pair<std::string, AutosaveStamp>> splitAutosaveName(std::string_view name) {
	// "<mapName>_<stamp>.vmf" (older: "<mapName>.<stamp>.vmf") - the map name itself may
	// contain dots and underscores.
	if (name.size() < STAMP_LENGTH + 6 || !iequals(name.substr(name.size() - 4), ".vmf")) {
		return std::nullopt;
	}
	const std::string_view rest = name.substr(0, name.size() - 4);
	const std::size_t dot = rest.size() - STAMP_LENGTH - 1;
	if (rest[dot] != '.' && rest[dot] != '_') {
		return std::nullopt;
	}
	const auto stamp = AutosaveStamp::parse(rest.substr(dot + 1));
	if (!stamp || dot == 0) {
		return std::nullopt;
	}
	return std::pair{std::string(rest.substr(0, dot)), *stamp};
}

std::optional<AutosaveEntry> parseAutosaveName(const std::filesystem::path& file) {
	const auto u8 = file.filename().u8string();
	const auto split = splitAutosaveName(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
	if (!split) {
		return std::nullopt;
	}
	return AutosaveEntry{file, split->first, split->second};
}

std::vector<AutosaveEntry> listAutosaves(const std::filesystem::path& folder, std::string_view mapName) {
	std::vector<AutosaveEntry> out;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
		if (!it->is_regular_file(ec)) continue;
		if (auto entry = parseAutosaveName(it->path()); entry && iequals(entry->mapName, mapName)) {
			out.push_back(std::move(*entry));
		}
	}
	std::sort(out.begin(), out.end(), [](const AutosaveEntry& a, const AutosaveEntry& b) { return a.stamp > b.stamp; });
	return out;
}

std::vector<std::filesystem::path> autosavesToRemove(const std::vector<AutosaveEntry>& list, std::size_t keep) {
	std::vector<std::filesystem::path> out;
	for (std::size_t i = keep; i < list.size(); ++i) {
		out.push_back(list[i].path);
	}
	return out;
}

std::optional<std::filesystem::path> autosaveOriginal(const std::filesystem::path& file) {
	const auto entry = parseAutosaveName(file);
	if (!entry || !iequals(file.parent_path().filename().string(), "autosave")) {
		return std::nullopt;
	}
	const std::u8string name(reinterpret_cast<const char8_t*>(entry->mapName.data()), entry->mapName.size());
	return file.parent_path().parent_path() / (name + u8".vmf");
}

std::string withoutAutosave(std::string_view path) {
	const auto slash = path.find_last_of("/\\");
	if (slash == std::string_view::npos) return std::string(path);
	const auto split = splitAutosaveName(path.substr(slash + 1));
	const std::string_view folder = path.substr(0, slash);
	const auto above = folder.find_last_of("/\\");
	if (!split || above == std::string_view::npos || !iequals(folder.substr(above + 1), "autosave")) return std::string(path);
	return std::string(folder.substr(0, above + 1)) + split->first + ".vmf";
}

std::filesystem::path backupPath(const std::filesystem::path& mapFile) {
	auto path = mapFile;
	path.replace_extension(".vmx");
	return path;
}

} // namespace sme
