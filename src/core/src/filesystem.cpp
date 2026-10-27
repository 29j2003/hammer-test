// filesystem.cpp - Mounted game file system over folders and VPKs (see filesystem.h).

#include <sme/filesystem.h>

#include <sme/text.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <system_error>
#include <unordered_map>

#include <vpkpp/format/VPK.h>

namespace sme {

namespace {

/// Files larger than this are never read into memory (no Source asset comes close).
constexpr std::uintmax_t MAX_FILE_SIZE = 512ull * 1024 * 1024;

/// Normalized path, or empty if it tries to leave its mount ("..") or is empty.
std::string safeRelativePath(std::string_view path) {
	auto normalized = normalizePath(path);
	while (normalized.starts_with('/')) {
		normalized.erase(0, 1);
	}
	for (const auto& segment : splitTrimmed(normalized, '/')) {
		if (segment == "..") {
			return {};
		}
	}
	return normalized;
}

std::string lowerExtension(std::string_view path) {
	const auto dot = path.rfind('.');
	const auto slash = path.rfind('/');
	if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
		return {};
	}
	return toLower(path.substr(dot));
}

/// "x.vpk" -> "x_dir.vpk" if that exists; otherwise the path as given.
std::filesystem::path vpkDirFile(const std::filesystem::path& vpk) {
	auto name = displayPath(vpk.filename());
	if (endsWithI(name, "_dir.vpk")) {
		return vpk;
	}
	if (endsWithI(name, ".vpk")) {
		const auto dirName = name.substr(0, name.size() - 4) + "_dir.vpk";
		if (auto found = findChildI(vpk.parent_path(), dirName)) {
			return *found;
		}
		if (auto found = findChildI(vpk.parent_path(), name)) {
			return *found;
		}
	}
	return vpk;
}

/// True for the data parts of multi-file VPKs (pak01_000.vpk), which are not mounted on their own.
bool isVpkArchivePart(std::string_view name) {
	if (!endsWithI(name, ".vpk") || name.size() < 9) {
		return false;
	}
	const auto stem = name.substr(0, name.size() - 4);
	const auto underscore = stem.rfind('_');
	if (underscore == std::string_view::npos || stem.size() - underscore != 4) {
		return false;
	}
	return std::all_of(stem.begin() + static_cast<std::ptrdiff_t>(underscore) + 1, stem.end(), [](char c) {
		return c >= '0' && c <= '9';
	});
}

} // namespace

struct GameFileSystem::MountData {
	std::mutex mutex;
	std::unique_ptr<vpkpp::PackFile> vpk;
	// Directory mounts: lowercase relative path -> real path, built on first use.
	std::once_flag indexOnce;
	std::shared_mutex indexMutex;   ///< Readers share it; noteWrittenFile() adds entries
	std::unordered_map<std::string, std::filesystem::path> index;

	void buildIndex(const std::filesystem::path& root) {
		std::call_once(indexOnce, [&] {
			std::error_code ec;
			const auto options = std::filesystem::directory_options::skip_permission_denied;
			for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code fileEc;
				if (!it->is_regular_file(fileEc)) {
					continue;
				}
				const auto relative = it->path().lexically_relative(root);
				index.emplace(normalizePath(displayPath(relative)), it->path());
			}
		});
	}
};

GameFileSystem::GameFileSystem() = default;
GameFileSystem::~GameFileSystem() = default;
GameFileSystem::GameFileSystem(GameFileSystem&&) noexcept = default;
GameFileSystem& GameFileSystem::operator=(GameFileSystem&&) noexcept = default;

GameFileSystem GameFileSystem::fromGameInfo(const GameInfo& info) {
	GameFileSystem fs;
	for (const auto& path : info.searchPaths) {
		if (!path.conditionalActive) {
			continue;
		}
		if (!path.problem.empty()) {
			Mount missing;
			missing.kind = path.isVpk ? Mount::Kind::Vpk : Mount::Kind::Directory;
			missing.pathIds = path.pathIds;
			missing.source = path.raw;
			missing.problem = path.problem;
			fs.m_mounts.push_back(std::move(missing));
			fs.m_data.push_back(std::make_unique<MountData>());
			continue;
		}
		if (path.isWildcard) {
			// Every subfolder and single-file VPK, in name order (as the engine does for custom/*).
			std::vector<std::filesystem::path> children;
			std::error_code ec;
			for (std::filesystem::directory_iterator it(path.resolved, ec), end; !ec && it != end; it.increment(ec)) {
				children.push_back(it->path());
			}
			std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
				return toLower(displayPath(a.filename())) < toLower(displayPath(b.filename()));
			});
			for (const auto& child : children) {
				std::error_code childEc;
				const auto name = displayPath(child.filename());
				if (std::filesystem::is_directory(child, childEc)) {
					fs.addDirectory(child, path.pathIds, path.raw);
				} else if (endsWithI(name, ".vpk") && !isVpkArchivePart(name)) {
					fs.addVpk(child, path.pathIds, path.raw);
				}
			}
		} else if (path.isVpk) {
			fs.addVpk(path.resolved, path.pathIds, path.raw);
		} else {
			fs.addDirectory(path.resolved, path.pathIds, path.raw);
		}
	}
	return fs;
}

void GameFileSystem::addDirectory(const std::filesystem::path& dir, std::vector<std::string> pathIds, std::string source) {
	std::error_code ec;
	const bool exists = std::filesystem::is_directory(dir, ec);

	// The engine mounts <dir>/pak01_dir.vpk ahead of the loose files of a folder.
	if (exists) {
		if (auto pak = findChildI(dir, "pak01_dir.vpk")) {
			addVpk(*pak, pathIds, source);
		}
	}

	// Same folder listed twice (e.g. once per id): merge the ids, keep the first position.
	for (auto& mount : m_mounts) {
		if (mount.kind == Mount::Kind::Directory && mount.path == dir) {
			for (auto& id : pathIds) {
				if (std::find(mount.pathIds.begin(), mount.pathIds.end(), id) == mount.pathIds.end()) {
					mount.pathIds.push_back(id);
				}
			}
			return;
		}
	}

	Mount mount;
	mount.kind = Mount::Kind::Directory;
	mount.path = dir;
	mount.pathIds = std::move(pathIds);
	mount.source = std::move(source);
	mount.present = exists;
	if (!exists) {
		mount.problem = "Folder not found";
	}
	m_mounts.push_back(std::move(mount));
	m_data.push_back(std::make_unique<MountData>());
}

void GameFileSystem::addVpk(const std::filesystem::path& vpk, std::vector<std::string> pathIds, std::string source) {
	const auto file = vpkDirFile(vpk);
	for (auto& mount : m_mounts) {
		if (mount.kind == Mount::Kind::Vpk && mount.path == file) {
			for (auto& id : pathIds) {
				if (std::find(mount.pathIds.begin(), mount.pathIds.end(), id) == mount.pathIds.end()) {
					mount.pathIds.push_back(id);
				}
			}
			return;
		}
	}

	Mount mount;
	mount.kind = Mount::Kind::Vpk;
	mount.path = file;
	mount.pathIds = std::move(pathIds);
	mount.source = std::move(source);
	auto data = std::make_unique<MountData>();

	std::error_code ec;
	if (!std::filesystem::is_regular_file(file, ec)) {
		mount.problem = "VPK not found";
	} else {
		try {
			data->vpk = vpkpp::VPK::open(displayPath(file));
		} catch (const std::exception& e) {
			mount.problem = std::string("VPK could not be opened: ") + e.what();
		}
		if (!data->vpk && mount.problem.empty()) {
			mount.problem = "VPK could not be opened";
		}
	}
	mount.present = data->vpk != nullptr;
	m_mounts.push_back(std::move(mount));
	m_data.push_back(std::move(data));
}

const std::vector<Mount>& GameFileSystem::mounts() const {
	return m_mounts;
}

bool GameFileSystem::mountMatches(std::size_t index, std::string_view pathId) const {
	const auto& mount = m_mounts[index];
	if (!mount.present) {
		return false;
	}
	if (pathId.empty()) {
		return true;
	}
	return std::any_of(mount.pathIds.begin(), mount.pathIds.end(), [pathId](const std::string& id) {
		return iequals(id, pathId);
	});
}

std::optional<std::size_t> GameFileSystem::locate(std::string_view path, std::string_view pathId) const {
	const auto relative = safeRelativePath(path);
	if (relative.empty()) {
		return std::nullopt;
	}
	for (std::size_t i = 0; i < m_mounts.size(); ++i) {
		if (!mountMatches(i, pathId)) {
			continue;
		}
		auto& data = *m_data[i];
		if (m_mounts[i].kind == Mount::Kind::Vpk) {
			if (data.vpk->hasEntry(relative)) {
				return i;
			}
		} else {
			data.buildIndex(m_mounts[i].path);
			const std::shared_lock lock(data.indexMutex);
			if (data.index.contains(relative)) {
				return i;
			}
		}
	}
	return std::nullopt;
}

bool GameFileSystem::exists(std::string_view path, std::string_view pathId) const {
	return locate(path, pathId).has_value();
}

std::optional<std::vector<std::byte>> GameFileSystem::read(std::string_view path, std::string_view pathId) const {
	const auto index = locate(path, pathId);
	if (!index) {
		return std::nullopt;
	}
	const auto relative = safeRelativePath(path);
	auto& data = *m_data[*index];
	if (m_mounts[*index].kind == Mount::Kind::Vpk) {
		const std::lock_guard lock(data.mutex);
		return data.vpk->readEntry(relative);
	}

	std::filesystem::path file;
	{
		const std::shared_lock lock(data.indexMutex);
		file = data.index.at(relative);
	}
	std::error_code ec;
	const auto size = std::filesystem::file_size(file, ec);
	if (ec || size > MAX_FILE_SIZE) {
		return std::nullopt;
	}
	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		return std::nullopt;
	}
	std::vector<std::byte> bytes(static_cast<std::size_t>(size));
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!stream) {
		return std::nullopt;
	}
	return bytes;
}

std::optional<std::string> GameFileSystem::readText(std::string_view path, std::string_view pathId) const {
	auto bytes = read(path, pathId);
	if (!bytes) {
		return std::nullopt;
	}
	return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

std::vector<std::string> GameFileSystem::list(std::string_view dir, std::string_view extension, std::string_view pathId) const {
	const auto prefixDir = safeRelativePath(dir);
	const auto prefix = prefixDir.empty() ? std::string{} : prefixDir + "/";
	const auto wantedExtension = toLower(extension);
	std::set<std::string> found;
	const auto consider = [&](std::string path) {
		if (!prefix.empty() && !path.starts_with(prefix)) {
			return;
		}
		if (!wantedExtension.empty() && lowerExtension(path) != wantedExtension) {
			return;
		}
		found.insert(std::move(path));
	};

	for (std::size_t i = 0; i < m_mounts.size(); ++i) {
		if (!mountMatches(i, pathId)) {
			continue;
		}
		auto& data = *m_data[i];
		if (m_mounts[i].kind == Mount::Kind::Vpk) {
			const std::lock_guard lock(data.mutex);
			data.vpk->runForAllEntries([&](const std::string& entryPath, const vpkpp::Entry&) {
				consider(normalizePath(entryPath));
			});
		} else {
			data.buildIndex(m_mounts[i].path);
			const std::shared_lock lock(data.indexMutex);
			for (const auto& [relative, real] : data.index) {
				consider(relative);
			}
		}
	}
	return {found.begin(), found.end()};
}

std::size_t GameFileSystem::fileCount(std::size_t mountIndex) const {
	if (mountIndex >= m_mounts.size() || !m_mounts[mountIndex].present) {
		return 0;
	}
	auto& data = *m_data[mountIndex];
	if (m_mounts[mountIndex].kind == Mount::Kind::Vpk) {
		const std::lock_guard lock(data.mutex);
		return data.vpk->getEntryCount();
	}
	data.buildIndex(m_mounts[mountIndex].path);
	const std::shared_lock lock(data.indexMutex);
	return data.index.size();
}

void GameFileSystem::noteWrittenFile(const std::filesystem::path& file) {
	for (std::size_t i = 0; i < m_mounts.size(); ++i) {
		if (m_mounts[i].kind != Mount::Kind::Directory || !m_mounts[i].present) {
			continue;
		}
		// Canonical and case-insensitive: Windows paths may differ in case or use short names.
		const auto key = [](const std::filesystem::path& path) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(path, ec);
			return normalizePath(displayPath(ec ? path.lexically_normal() : canonical));
		};
		const std::string root = key(m_mounts[i].path), full = key(file);
		if (full.size() <= root.size() + 1 || !full.starts_with(root + "/")) {
			continue;
		}
		const std::string text = full.substr(root.size() + 1);
		auto& data = *m_data[i];
		data.buildIndex(m_mounts[i].path);
		const std::unique_lock lock(data.indexMutex);
		data.index.emplace(text, file);
	}
}

} // namespace sme
