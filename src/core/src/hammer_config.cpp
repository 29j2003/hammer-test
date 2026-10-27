// hammer_config.cpp - GameConfig.txt import (see hammer_config.h).

#include <sme/hammer_config.h>

#include <sme/text.h>

#include <system_error>

#include <kvpp/KV1.h>

namespace sme {

namespace {

std::filesystem::path pathValue(const kvpp::KV1ElementReadable<>& block, std::string_view key) {
	const auto value = trim(block[key].getValue());
	return value.empty() ? std::filesystem::path{} : toNativePath(value);
}

} // namespace

std::optional<HammerConfigFile> parseHammerConfig(std::string_view text, const std::filesystem::path& path, std::string* error) {
	// Hammer writes paths with single backslashes, so escape sequences must stay off.
	const kvpp::KV1<> kv(text);
	const auto* games = &kv["Configs"]["Games"];
	if (games->isInvalid()) {
		games = &kv["GameConfig.txt"]["Games"]; // very old files used the file name as root
	}
	if (games->isInvalid()) {
		if (error) {
			*error = "no Configs/Games block";
		}
		return std::nullopt;
	}

	HammerConfigFile file;
	file.path = path;
	file.flavor = toLower(displayPath(path)).find("hammerplusplus") != std::string::npos ? "Hammer++" : "Hammer";
	for (const auto& game : *games) {
		if (game.getChildCount() == 0) {
			continue;
		}
		HammerGameConfig config;
		config.name = std::string(game.getKey());
		config.gameDir = pathValue(game, "GameDir");
		const auto& hammer = game["Hammer"];
		if (!hammer.isInvalid()) {
			for (unsigned i = 0;; ++i) {
				const auto& fgd = hammer["GameData" + std::to_string(i)];
				if (fgd.isInvalid()) {
					break;
				}
				if (!trim(fgd.getValue()).empty()) {
					config.fgds.push_back(toNativePath(trim(fgd.getValue())));
				}
			}
			config.gameExe = pathValue(hammer, "GameExe");
			config.vbsp = pathValue(hammer, "BSP");
			config.vvis = pathValue(hammer, "Vis");
			config.vrad = pathValue(hammer, "Light");
			config.mapDir = pathValue(hammer, "MapDir");
			config.bspDir = pathValue(hammer, "BSPDir");
			config.defaultSolidEntity = std::string(hammer["DefaultSolidEntity"].getValue());
			config.defaultPointEntity = std::string(hammer["DefaultPointEntity"].getValue());
			config.cordonTexture = std::string(hammer["CordonTexture"].getValue());
			if (const auto& scale = hammer["DefaultTextureScale"]; !scale.isInvalid()) {
				config.defaultTextureScale = scale.getValue<float>();
			}
			if (const auto& lightmap = hammer["DefaultLightmapScale"]; !lightmap.isInvalid()) {
				config.defaultLightmapScale = lightmap.getValue<int32_t>();
			}
		}
		file.games.push_back(std::move(config));
	}
	return file;
}

std::optional<HammerConfigFile> loadHammerConfig(const std::filesystem::path& path, std::string* error) {
	const auto text = readFileText(path);
	if (!text) {
		if (error) {
			*error = "cannot read " + displayPath(path);
		}
		return std::nullopt;
	}
	return parseHammerConfig(*text, path, error);
}

std::vector<std::filesystem::path> findHammerConfigs(const std::filesystem::path& appInstallDir) {
	std::vector<std::filesystem::path> found;
	for (const char* relative : {
			"bin/GameConfig.txt",
			"bin/hammerplusplus/hammerplusplus_gameconfig.txt",
			"bin/x64/GameConfig.txt",
			"bin/x64/hammerplusplus/hammerplusplus_gameconfig.txt",
		}) {
		if (auto path = resolveI(appInstallDir, relative)) {
			std::error_code ec;
			if (std::filesystem::is_regular_file(*path, ec)) {
				found.push_back(*path);
			}
		}
	}
	return found;
}

} // namespace sme
