// recent_projects.cpp - The list of recent projects (see recent_projects.h).

#include <sme/recent_projects.h>

#include <sme/text.h>

#include <algorithm>

namespace sme {

namespace {

/// The map path as compared: forward slashes, lower case (Windows paths are not case sensitive).
std::string pathKey(std::string_view path) {
	std::string key = toLower(path);
	std::replace(key.begin(), key.end(), '\\', '/');
	return key;
}

} // namespace

std::vector<RecentProject> rememberProject(std::vector<RecentProject> list, RecentProject project, std::size_t limit) {
	if (project.mapPath.empty()) return list;
	list = forgetProject(std::move(list), project.mapPath);
	list.insert(list.begin(), std::move(project));
	if (list.size() > limit) list.resize(limit);
	return list;
}

std::vector<RecentProject> forgetProject(std::vector<RecentProject> list, std::string_view mapPath) {
	const std::string key = pathKey(mapPath);
	std::erase_if(list, [&key](const RecentProject& p) { return pathKey(p.mapPath) == key; });
	return list;
}

std::string writeRecentProjects(const std::vector<RecentProject>& list) {
	std::string out;
	for (const auto& p : list) {
		// Tabs and line breaks cannot be in the fields (they would break the line format).
		if (p.gameKey.find_first_of("\t\r\n") != std::string::npos || p.mapPath.find_first_of("\t\r\n") != std::string::npos) continue;
		out += std::to_string(p.time) + '\t' + p.gameKey + '\t' + p.mapPath + '\n';
	}
	return out;
}

std::vector<RecentProject> parseRecentProjects(std::string_view text) {
	std::vector<RecentProject> list;
	for (const auto& line : splitTrimmed(text, '\n')) {
		const auto first = line.find('\t');
		const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
		if (second == std::string::npos) continue;
		RecentProject p;
		try {
			p.time = std::stoll(line.substr(0, first));
		} catch (...) {
			continue;
		}
		p.gameKey = line.substr(first + 1, second - first - 1);
		p.mapPath = line.substr(second + 1);
		if (p.mapPath.empty()) continue;
		list.push_back(std::move(p));
	}
	return list;
}

} // namespace sme
