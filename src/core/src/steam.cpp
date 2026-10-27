// steam.cpp - Steam root detection and library/app manifest parsing (see steam.h).

#include <sme/steam.h>

#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#include <kvpp/KV1.h>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace sme {

namespace {

#ifdef _WIN32
std::optional<std::filesystem::path> readRegistryPath(HKEY root, const wchar_t* key, const wchar_t* value) {
	wchar_t buffer[MAX_PATH * 2]{};
	DWORD size = sizeof(buffer);
	if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
		return std::nullopt;
	}
	return std::filesystem::path(buffer).lexically_normal();
}
#endif

bool isSteamRoot(const std::filesystem::path& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path / "steamapps", ec);
}

std::optional<std::filesystem::path> envPath(const char* name) {
	if (const char* value = std::getenv(name); value && *value) {
		return std::filesystem::path(value);
	}
	return std::nullopt;
}

void addLibrary(std::vector<std::filesystem::path>& libraries, const std::filesystem::path& library) {
	std::error_code ec;
	auto canonical = std::filesystem::weakly_canonical(library, ec);
	if (ec) {
		canonical = library;
	}
	if (std::find(libraries.begin(), libraries.end(), canonical) == libraries.end()) {
		libraries.push_back(canonical);
	}
}

} // namespace

std::optional<std::filesystem::path> SteamInstallation::detectRoot() {
	if (auto env = envPath("SME_STEAM_ROOT")) {
		return env;
	}
#ifdef _WIN32
	if (auto path = readRegistryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"); path && isSteamRoot(*path)) {
		return path;
	}
	if (auto path = readRegistryPath(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam", L"InstallPath"); path && isSteamRoot(*path)) {
		return path;
	}
	for (const char* fallback : {"C:/Program Files (x86)/Steam", "C:/Program Files/Steam"}) {
		if (isSteamRoot(fallback)) {
			return std::filesystem::path(fallback);
		}
	}
#else
	if (auto home = envPath("HOME")) {
	#ifdef __APPLE__
		const std::filesystem::path candidates[] = {*home / "Library/Application Support/Steam"};
	#else
		const std::filesystem::path candidates[] = {
			*home / ".steam/steam",
			*home / ".local/share/Steam",
			*home / ".var/app/com.valvesoftware.Steam/data/Steam",
			*home / "snap/steam/common/.local/share/Steam",
		};
	#endif
		for (const auto& candidate : candidates) {
			if (isSteamRoot(candidate)) {
				std::error_code ec;
				auto canonical = std::filesystem::weakly_canonical(candidate, ec);
				return ec ? candidate : canonical;
			}
		}
	}
#endif
	return std::nullopt;
}

SteamInstallation SteamInstallation::detect() {
	if (auto root = detectRoot()) {
		return load(*root);
	}
	return {};
}

SteamInstallation SteamInstallation::load(const std::filesystem::path& root) {
	SteamInstallation steam;
	std::error_code rootEc;
	steam.m_root = std::filesystem::weakly_canonical(std::filesystem::absolute(root, rootEc), rootEc);
	if (rootEc || steam.m_root.empty()) {
		steam.m_root = root;
	}
	addLibrary(steam.m_libraries, steam.m_root);

	// libraryfolders.vdf: new format has "0" { "path" "..." }, the old one "1" "D:\\Library".
	if (auto text = readFileText(steam.m_root / "steamapps" / "libraryfolders.vdf")) {
		const kvpp::KV1<> kv(*text, true);
		for (const auto& block : kv) {
			if (!iequals(block.getKey(), "libraryfolders")) {
				continue;
			}
			for (const auto& entry : block) {
				if (!entry.getValue().empty() && !entry.getKey().empty() && std::isdigit(static_cast<unsigned char>(entry.getKey()[0]))) {
					addLibrary(steam.m_libraries, toNativePath(entry.getValue()));
				} else if (const auto& path = entry["path"]; !path.isInvalid()) {
					addLibrary(steam.m_libraries, toNativePath(path.getValue()));
				}
			}
		}
	} else {
		steam.m_warnings.push_back("libraryfolders.vdf not found in " + displayPath(steam.m_root / "steamapps"));
	}

	for (const auto& library : steam.m_libraries) {
		const auto steamapps = library / "steamapps";
		std::error_code ec;
		if (!std::filesystem::is_directory(steamapps, ec)) {
			steam.m_warnings.push_back("Steam library missing: " + displayPath(library));
			continue;
		}
		for (std::filesystem::directory_iterator it(steamapps, ec), end; !ec && it != end; it.increment(ec)) {
			const auto name = displayPath(it->path().filename());
			if (!startsWithI(name, "appmanifest_") || !endsWithI(name, ".acf")) {
				continue;
			}
			const auto text = readFileText(it->path());
			if (!text) {
				steam.m_warnings.push_back("Unreadable manifest: " + name);
				continue;
			}
			const kvpp::KV1<> kv(*text, true);
			const auto& state = kv["AppState"];
			if (state.isInvalid()) {
				continue;
			}
			SteamApp app;
			app.appId = static_cast<AppId>(state["appid"].getValue<int64_t>());
			app.name = std::string(state["name"].getValue());
			const auto installDirName = std::string(state["installdir"].getValue());
			if (app.appId == 0 || installDirName.empty()) {
				continue;
			}
			const auto common = steamapps / "common";
			app.installDir = findChildI(common, installDirName).value_or(common / installDirName);
			app.library = library;
			if (!steam.findApp(app.appId)) {
				steam.m_apps.push_back(std::move(app));
			}
		}
	}
	std::sort(steam.m_apps.begin(), steam.m_apps.end(), [](const SteamApp& a, const SteamApp& b) {
		return a.appId < b.appId;
	});

	steam.m_sourceModsDir = steam.m_root / "steamapps" / "sourcemods";
#ifdef _WIN32
	if (!envPath("SME_STEAM_ROOT")) {
		if (auto path = readRegistryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SourceModInstallPath")) {
			steam.m_sourceModsDir = *path;
		}
	}
#endif
	return steam;
}

const SteamApp* SteamInstallation::findApp(AppId appId) const {
	const auto it = std::find_if(m_apps.begin(), m_apps.end(), [appId](const SteamApp& app) {
		return app.appId == appId;
	});
	return it == m_apps.end() ? nullptr : &*it;
}

} // namespace sme
