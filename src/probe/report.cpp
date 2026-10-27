// report.cpp - Setup report for sme-probe (see report.h).

#include "report.h"

#include <sme/game.h>
#include <sme/material.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace sme::probe {

namespace {

/// Minimal JSON writer: objects/arrays with automatic commas and indentation.
class Json {
public:
	Json& beginObject(std::string_view key = {}) { return open(key, '{'); }
	Json& endObject() { return close('}'); }
	Json& beginArray(std::string_view key = {}) { return open(key, '['); }
	Json& endArray() { return close(']'); }

	Json& value(std::string_view key, std::string_view text) {
		prefix(key);
		quote(maskPersonal(std::string(text.begin(), text.end())));
		return *this;
	}
	Json& value(std::string_view key, const char* text) { return value(key, std::string_view(text)); }
	Json& value(std::string_view key, const std::string& text) { return value(key, std::string_view(text)); }
	Json& value(std::string_view key, const std::filesystem::path& path) { return value(key, displayPath(path)); }
	Json& value(std::string_view key, bool flag) {
		prefix(key);
		m_out << (flag ? "true" : "false");
		return *this;
	}
	Json& value(std::string_view key, std::uint64_t number) {
		prefix(key);
		m_out << number;
		return *this;
	}
	Json& item(std::string_view text) { return value({}, text); }
	Json& item(const std::string& text) { return value({}, std::string_view(text)); }
	Json& item(const std::filesystem::path& path) { return value({}, path); }

	[[nodiscard]] std::string str() const { return m_out.str() + "\n"; }

private:
	Json& open(std::string_view key, char bracket) {
		prefix(key);
		m_out << bracket;
		m_first.push_back(true);
		return *this;
	}
	Json& close(char bracket) {
		const bool empty = m_first.back();
		m_first.pop_back();
		if (!empty) {
			newline();
		}
		m_out << bracket;
		return *this;
	}
	void prefix(std::string_view key) {
		if (!m_first.empty()) {
			if (!m_first.back()) {
				m_out << ',';
			}
			m_first.back() = false;
			newline();
		}
		if (!key.empty()) {
			quote(key);
			m_out << ": ";
		}
	}
	void newline() {
		m_out << '\n' << std::string(m_first.size() * 2, ' ');
	}
	void quote(std::string_view text) {
		m_out << '"';
		for (const char c : text) {
			switch (c) {
				case '"': m_out << "\\\""; break;
				case '\\': m_out << "\\\\"; break;
				case '\n': m_out << "\\n"; break;
				case '\r': m_out << "\\r"; break;
				case '\t': m_out << "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
						m_out << buffer;
					} else {
						m_out << c;
					}
			}
		}
		m_out << '"';
	}

	std::ostringstream m_out;
	std::vector<bool> m_first;
};

std::string homeDir() {
	for (const char* name : {"USERPROFILE", "HOME"}) {
		if (const char* value = std::getenv(name); value && *value) {
			return displayPath(std::filesystem::path(value));
		}
	}
	return {};
}

const char* platformName() {
#if defined(_WIN32)
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#else
	return "linux";
#endif
}

void writeGame(Json& json, const GameEntry& entry, const SteamInstallation& steam) {
	json.beginObject();
	json.value("title", entry.title).value("key", entry.key()).value("dir", entry.gameDir);
	json.value("owner_app", static_cast<std::uint64_t>(entry.ownerAppId)).value("base_app", static_cast<std::uint64_t>(entry.baseAppId));
	json.value("sourcemod", entry.isSourceMod).value("mapbase", entry.usesMapbase).value("branch", entry.branch);
	json.value("hammer_config", entry.hammerConfig);
	json.value("mapbase_fgd", entry.mapbaseFgd);
	json.beginArray("fgds");
	for (const auto& fgd : entry.fgds) {
		json.item(fgd);
	}
	json.endArray();
	json.beginArray("available_fgds");
	for (const auto& fgd : entry.availableFgds) {
		json.item(fgd);
	}
	json.endArray();
	json.beginArray("notes");
	for (const auto& note : entry.notes) {
		json.item(note);
	}
	json.endArray();

	std::string error;
	const auto game = loadGame(entry, steam, entry.fgds, &error);
	if (!game) {
		json.value("error", error).endObject();
		return;
	}
	json.beginArray("search_paths");
	for (const auto& path : game->info.searchPaths) {
		json.beginObject();
		std::string ids;
		for (const auto& id : path.pathIds) {
			ids += (ids.empty() ? "" : "+") + id;
		}
		json.value("ids", ids).value("raw", path.raw).value("conditional", path.conditional).value("active", path.conditionalActive);
		json.value("resolved", path.resolved).value("problem", path.problem);
		json.endObject();
	}
	json.endArray();

	json.beginArray("mounts");
	for (std::size_t i = 0; i < game->files.mounts().size(); ++i) {
		const auto& mount = game->files.mounts()[i];
		std::string ids;
		for (const auto& id : mount.pathIds) {
			ids += (ids.empty() ? "" : "+") + id;
		}
		json.beginObject();
		json.value("kind", mount.kind == Mount::Kind::Vpk ? "vpk" : "dir").value("path", mount.path).value("ids", ids);
		json.value("present", mount.present).value("problem", mount.problem);
		json.value("files", static_cast<std::uint64_t>(game->files.fileCount(i)));
		json.endObject();
	}
	json.endArray();

	// Quick content checks: can we see materials and decode a tool texture?
	json.beginObject("checks");
	json.value("materials", static_cast<std::uint64_t>(game->files.list("materials", ".vmt").size()));
	json.value("models", static_cast<std::uint64_t>(game->files.list("models", ".mdl").size()));
	json.value("sounds", static_cast<std::uint64_t>(game->files.list("sound", ".wav").size() + game->files.list("sound", ".mp3").size()));
	if (const auto material = loadMaterial(game->files, "tools/toolsnodraw", &error)) {
		const auto texture = material->previewTexture();
		json.value("nodraw_shader", material->shader).value("nodraw_texture", texture);
		if (const auto index = game->files.locate("materials/" + texture + ".vtf")) {
			json.value("nodraw_texture_from", game->files.mounts()[*index].path);
		}
		if (const auto image = loadTexture(game->files, texture, 64, &error)) {
			json.value("nodraw_decoded", std::to_string(image->width) + "x" + std::to_string(image->height));
		} else {
			json.value("nodraw_decoded", "failed: " + error);
		}
	} else {
		json.value("nodraw", "failed: " + error);
	}
	json.endObject();

	json.beginObject("entities");
	json.value("classes", static_cast<std::uint64_t>(game->entities.size()));
	json.value("placeable", static_cast<std::uint64_t>(game->entities.placeable().size()));
	json.endObject();

	json.beginArray("problems");
	for (std::size_t i = 0; i < game->problems.size() && i < 50; ++i) {
		json.item(game->problems[i]);
	}
	json.endArray();
	json.endObject();
}

} // namespace

std::string maskPersonal(std::string text) {
	static const std::string home = homeDir();
	if (!home.empty()) {
		for (auto pos = text.find(home); pos != std::string::npos; pos = text.find(home, pos + 1)) {
			text.replace(pos, home.size(), "~");
		}
	}
	static const std::regex users(R"(([\\/])(Users|home)([\\/])[^\\/"]+)", std::regex::icase);
	return std::regex_replace(text, users, "$1$2$3<user>");
}

std::string buildReport(const DiscoveryResult& discovery) {
	Json json;
	json.beginObject();
	json.value("tool", "sme-probe").value("platform", platformName());

	const auto& steam = discovery.steam;
	json.beginObject("steam");
	json.value("found", steam.found()).value("root", steam.root()).value("sourcemods", steam.sourceModsDir());
	json.beginArray("libraries");
	for (const auto& library : steam.libraries()) {
		json.item(library);
	}
	json.endArray();
	json.beginArray("warnings");
	for (const auto& warning : steam.warnings()) {
		json.item(warning);
	}
	json.endArray();
	json.endObject();

	json.beginArray("source_apps");
	for (const auto& app : steam.apps()) {
		const bool hasGame = std::any_of(discovery.games.begin(), discovery.games.end(), [&](const GameEntry& g) {
			return g.ownerAppId == app.appId || g.baseAppId == app.appId;
		});
		if (!hasGame && !findKnownApp(app.appId)) {
			continue;
		}
		json.beginObject();
		json.value("id", static_cast<std::uint64_t>(app.appId)).value("name", app.name).value("dir", app.installDir);
		json.endObject();
	}
	json.endArray();

	json.beginArray("games");
	for (const auto& game : discovery.games) {
		writeGame(json, game, steam);
	}
	json.endArray();

	json.beginArray("hammer_configs");
	for (const auto& file : discovery.hammerConfigs) {
		json.beginObject();
		json.value("path", file.path).value("flavor", file.flavor);
		json.beginArray("games");
		for (const auto& config : file.games) {
			json.beginObject();
			json.value("name", config.name).value("game_dir", config.gameDir);
			json.beginArray("fgds");
			for (const auto& fgd : config.fgds) {
				json.item(fgd);
			}
			json.endArray();
			json.value("vbsp", config.vbsp).value("vvis", config.vvis).value("vrad", config.vrad).value("game_exe", config.gameExe);
			json.value("map_dir", config.mapDir).value("bsp_dir", config.bspDir);
			json.endObject();
		}
		json.endArray();
		json.endObject();
	}
	json.endArray();

	json.beginArray("warnings");
	for (const auto& warning : discovery.warnings) {
		json.item(warning);
	}
	json.endArray();
	json.endObject();
	return json.str();
}

} // namespace sme::probe
