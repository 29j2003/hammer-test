// steam.h - Finds the Steam installation, its library folders and installed apps.
//
// Reads steamapps/libraryfolders.vdf and the appmanifest_<id>.acf files directly, so it
// works for any Steam root (including test fixtures) and needs no running Steam client.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sme {

using AppId = std::uint32_t;

/// One installed Steam app, as described by its appmanifest.
struct SteamApp {
	AppId appId = 0;
	std::string name;
	std::filesystem::path installDir; ///< <library>/steamapps/common/<installdir>
	std::filesystem::path library;    ///< Library root that holds the app
};

/// Snapshot of a Steam installation.
class SteamInstallation {
public:
	/// Locates Steam: SME_STEAM_ROOT environment variable, then the registry (Windows)
	/// or the usual install folders (Linux, macOS).
	[[nodiscard]] static std::optional<std::filesystem::path> detectRoot();

	/// Reads libraries and app manifests below the given Steam root.
	[[nodiscard]] static SteamInstallation load(const std::filesystem::path& root);

	/// detectRoot() + load(); empty installation if Steam is not found.
	[[nodiscard]] static SteamInstallation detect();

	/// Whether a Steam folder was found.
	[[nodiscard]] bool found() const { return !m_root.empty(); }
	/// Steam folder, its library folders and every installed app.
	[[nodiscard]] const std::filesystem::path& root() const { return m_root; }
	[[nodiscard]] const std::vector<std::filesystem::path>& libraries() const { return m_libraries; }
	[[nodiscard]] const std::vector<SteamApp>& apps() const { return m_apps; }
	/// The installed app with this id, or nullptr.
	[[nodiscard]] const SteamApp* findApp(AppId appId) const;

	/// Folder for Source mods (registry SourceModInstallPath on Windows, else <root>/steamapps/sourcemods).
	[[nodiscard]] const std::filesystem::path& sourceModsDir() const { return m_sourceModsDir; }

	/// Problems found while reading (unreadable manifests, missing libraries, ...).
	[[nodiscard]] const std::vector<std::string>& warnings() const { return m_warnings; }

private:
	std::filesystem::path m_root;
	std::filesystem::path m_sourceModsDir;
	std::vector<std::filesystem::path> m_libraries;
	std::vector<SteamApp> m_apps;
	std::vector<std::string> m_warnings;
};

} // namespace sme
