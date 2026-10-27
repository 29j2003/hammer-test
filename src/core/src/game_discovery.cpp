// game_discovery.cpp - Source 1 game and mod discovery (see game_discovery.h).

#include <sme/game_discovery.h>

#include <sme/gameinfo.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace sme {

namespace {

constexpr std::array KNOWN_APPS = {
	KnownApp{215, "Source SDK Base 2006", "Source 2006", "halflife2"},
	KnownApp{218, "Source SDK Base 2007", "Source 2007", "halflife2"},
	KnownApp{220, "Half-Life 2", "Source SDK 2013", "halflife2"},
	KnownApp{240, "Counter-Strike: Source", "Source SDK 2013 MP", "cstrike"},
	KnownApp{280, "Half-Life: Source", "Source SDK 2013", "halflife2"},
	KnownApp{300, "Day of Defeat: Source", "Source SDK 2013 MP", "dod"},
	KnownApp{320, "Half-Life 2: Deathmatch", "Source SDK 2013 MP", "hl2mp"},
	KnownApp{340, "Half-Life 2: Lost Coast", "Source SDK 2013", "halflife2"},
	KnownApp{380, "Half-Life 2: Episode One", "Source SDK 2013", "halflife2"},
	KnownApp{400, "Portal", "Source SDK 2013", "portal"},
	KnownApp{420, "Half-Life 2: Episode Two", "Source SDK 2013", "halflife2"},
	KnownApp{440, "Team Fortress 2", "Team Fortress 2", "tf"},
	KnownApp{500, "Left 4 Dead", "Left 4 Dead", "left4dead"},
	KnownApp{550, "Left 4 Dead 2", "Left 4 Dead 2", "left4dead2"},
	KnownApp{620, "Portal 2", "Portal 2", "portal2"},
	KnownApp{4000, "Garry's Mod", "Garry's Mod", "garrysmod"},
	KnownApp{243730, "Source SDK Base 2013 Singleplayer", "Source SDK 2013", "halflife2"},
	KnownApp{243750, "Source SDK Base 2013 Multiplayer", "Source SDK 2013 MP", "hl2mp"},
	KnownApp{362890, "Black Mesa", "Xengine (Black Mesa)", "bms"},
	KnownApp{440000, "Portal 2: Community Edition", "Strata Source", "p2ce"},
	KnownApp{1583720, "Entropy : Zero 2", "Source SDK 2013 (Mapbase)", "halflife2"},
};

/// Folders inside a game install that have a gameinfo.txt-like role but are not games.
bool skipFolder(std::string_view name) {
	for (const char* skip : {"bin", "platform", "sdktools", "hl2_complete", "steam_shader_cache"}) {
		if (iequals(name, skip)) {
			return true;
		}
	}
	return false;
}

bool hasFile(const std::filesystem::path& dir, std::string_view name) {
	std::error_code ec;
	const auto found = findChildI(dir, name);
	return found && std::filesystem::is_regular_file(*found, ec);
}

std::vector<std::filesystem::path> fgdsIn(const std::filesystem::path& dir) {
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
		if (endsWithI(displayPath(it->path().filename()), ".fgd")) {
			out.push_back(it->path());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

/// FGDs anywhere below `dir` (a few levels deep, asset folders skipped: they hold thousands of files).
std::vector<std::filesystem::path> fgdsBelow(const std::filesystem::path& dir, int depth) {
	auto out = fgdsIn(dir);
	if (depth <= 0) {
		return out;
	}
	std::error_code ec;
	for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code dirEc;
		const auto name = toLower(displayPath(it->path().filename()));
		if (!it->is_directory(dirEc) || name == "materials" || name == "models" || name == "sound" || name == "maps" || name == "particles" ||
		    name == "scenes" || name == "media" || name == "cfg" || name == "download" || name == "save") {
			continue;
		}
		auto found = fgdsBelow(it->path(), depth - 1);
		out.insert(out.end(), found.begin(), found.end());
	}
	return out;
}

bool mentionsMapbase(const std::filesystem::path& fgd) {
	const auto text = readFileText(fgd);
	return text && toLower(*text).find("mapbase") != std::string::npos;
}

/// Comparable form of a folder path (absolute, resolved, lowercase, forward slashes).
std::string pathKey(const std::filesystem::path& path) {
	std::error_code ec;
	auto resolved = std::filesystem::weakly_canonical(path, ec);
	return normalizePath(displayPath(ec ? path.lexically_normal() : resolved));
}

GameEntry makeEntry(const std::filesystem::path& gameDir, AppId ownerAppId, bool isSourceMod) {
	GameEntry entry;
	entry.gameDir = gameDir;
	entry.ownerAppId = ownerAppId;
	entry.isSourceMod = isSourceMod;
	std::string error;
	if (const auto info = GameInfo::load(gameDir, &error)) {
		entry.title = !info->game.empty() ? info->game : info->title;
		entry.baseAppId = info->steamAppId;
		for (const auto& path : info->searchPaths) {
			if (toLower(path.raw).find("mapbase") != std::string::npos) {
				entry.usesMapbase = true;
			}
		}
		for (const auto& warning : info->warnings) {
			entry.notes.push_back(warning);
		}
	} else {
		entry.notes.push_back(error);
	}
	if (entry.title.empty()) {
		entry.title = displayPath(gameDir.filename());
	}
	const auto* known = findKnownApp(isSourceMod ? entry.baseAppId : ownerAppId);
	if (known) {
		entry.branch = std::string(known->branch);
	}
	if (entry.usesMapbase && entry.branch.find("Mapbase") == std::string::npos) {
		entry.branch += entry.branch.empty() ? "Mapbase" : " (Mapbase)";
	}
	return entry;
}

} // namespace

bool isMapbaseFgd(const std::filesystem::path& fgd) {
	if (mentionsMapbase(fgd)) {
		return true;
	}
	// Mapbase's halflife2.fgd mostly adds to its base.fgd: look into the @includes next to it.
	const auto text = readFileText(fgd);
	if (!text) {
		return false;
	}
	std::size_t at = 0;
	while ((at = toLower(*text).find("@include", at)) != std::string::npos) {
		const auto open = text->find('"', at);
		const auto close = open == std::string::npos ? open : text->find('"', open + 1);
		if (close == std::string::npos) {
			break;
		}
		const auto included = fgd.parent_path() / text->substr(open + 1, close - open - 1);
		if (mentionsMapbase(included)) {
			return true;
		}
		at = close;
	}
	return false;
}

const KnownApp* findKnownApp(AppId appId) {
	for (const auto& app : KNOWN_APPS) {
		if (app.appId == appId) {
			return &app;
		}
	}
	return nullptr;
}

std::string GameEntry::key() const {
	const auto name = toLower(folder());
	return isSourceMod ? "sourcemods/" + name : std::to_string(ownerAppId) + "/" + name;
}

std::string GameEntry::folder() const {
	return displayPath(gameDir.filename());
}

DiscoveryResult discoverGames(SteamInstallation steam) {
	DiscoveryResult result;
	result.steam = std::move(steam);
	if (!result.steam.found()) {
		result.warnings.emplace_back("Steam was not found");
		return result;
	}

	// Installed apps: every first-level folder with a gameinfo.txt (Source 2 uses gameinfo.gi and is skipped).
	for (const auto& app : result.steam.apps()) {
		std::error_code ec;
		bool anyGame = false;
		for (std::filesystem::directory_iterator it(app.installDir, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code dirEc;
			if (!it->is_directory(dirEc) || skipFolder(displayPath(it->path().filename()))) {
				continue;
			}
			if (hasFile(it->path(), "gameinfo.txt")) {
				result.games.push_back(makeEntry(it->path(), app.appId, false));
				anyGame = true;
			}
		}
		if (anyGame) {
			for (const auto& configPath : findHammerConfigs(app.installDir)) {
				std::string error;
				if (auto config = loadHammerConfig(configPath, &error)) {
					result.hammerConfigs.push_back(std::move(*config));
				} else {
					result.warnings.push_back(displayPath(configPath) + ": " + error);
				}
			}
		}
	}

	// Source mods (Mapbase mods live here).
	std::error_code ec;
	for (std::filesystem::directory_iterator it(result.steam.sourceModsDir(), ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code dirEc;
		if (it->is_directory(dirEc) && hasFile(it->path(), "gameinfo.txt")) {
			result.games.push_back(makeEntry(it->path(), 0, true));
		}
	}

	// FGD suggestions: the user's Hammer/Hammer++ config for this exact folder wins
	// (Hammer++ before Hammer), then .fgd files found next to the game and in the base game's bin/.
	for (auto& game : result.games) {
		const auto gameKey = pathKey(game.gameDir);
		const HammerGameConfig* best = nullptr;
		const HammerConfigFile* bestFile = nullptr;
		for (const auto& file : result.hammerConfigs) {
			for (const auto& config : file.games) {
				if (pathKey(config.gameDir) != gameKey || config.fgds.empty()) {
					continue;
				}
				if (!best || (file.flavor == "Hammer++" && bestFile->flavor != "Hammer++")) {
					best = &config;
					bestFile = &file;
				}
			}
		}
		auto local = fgdsBelow(game.gameDir, 2);
		game.availableFgds.insert(game.availableFgds.end(), local.begin(), local.end());
		if (game.usesMapbase) {
			// Mapbase ships its FGDs with the shared content (sourcemods/mapbase_shared).
			if (auto shared = findChildI(game.gameDir.parent_path(), "mapbase_shared")) {
				auto found = fgdsBelow(*shared, 3);
				game.availableFgds.insert(game.availableFgds.end(), found.begin(), found.end());
			}
		}
		const auto baseApp = game.isSourceMod ? game.baseAppId : game.ownerAppId;
		if (const auto* app = result.steam.findApp(baseApp)) {
			if (auto bin = findChildI(app->installDir, "bin")) {
				auto found = fgdsIn(*bin);
				game.availableFgds.insert(game.availableFgds.end(), found.begin(), found.end());
			}
		}
		// The Mapbase FGD for Mapbase mods: halflife2.fgd preferred (it includes Mapbase's base.fgd).
		if (game.usesMapbase) {
			for (const auto& fgd : game.availableFgds) {
				if (isMapbaseFgd(fgd) && (game.mapbaseFgd.empty() || iequals(displayPath(fgd.filename()), "halflife2.fgd"))) {
					game.mapbaseFgd = fgd;
				}
			}
		}
		const auto addMapbase = [&] {
			// Without it Mapbase's own entities (env_global_light, logic_script, ...) are missing.
			if (!game.usesMapbase || game.mapbaseFgd.empty()) {
				return;
			}
			if (std::any_of(game.fgds.begin(), game.fgds.end(), [](const auto& fgd) { return isMapbaseFgd(fgd); })) {
				return;
			}
			// After the game's base FGD (Mapbase replaces its classes), before the mod's own FGDs.
			auto at = game.fgds.begin();
			while (at != game.fgds.end() && !pathKey(*at).starts_with(pathKey(game.gameDir))) {
				++at;
			}
			game.fgds.insert(at, game.mapbaseFgd);
			game.notes.push_back("Mapbase FGD added: " + displayPath(game.mapbaseFgd));
		};
		if (best) {
			game.fgds = best->fgds;
			game.hammerConfig = bestFile->path;
			addMapbase();
			continue;
		}
		// No Hammer config: FGDs that ship with the mod itself, then Mapbase's shared FGD
		// (sourcemods/mapbase_shared/shared_misc/bin/halflife2.fgd, it includes base.fgd),
		// else the base game's main FGD.
		game.fgds = local;
		addMapbase();
		if (game.fgds.empty()) {
			if (const auto* known = findKnownApp(baseApp)) {
				for (const auto& fgd : game.availableFgds) {
					const auto name = toLower(displayPath(fgd.filename()));
					if (name == std::string(known->mainFgd) + ".fgd") {
						game.fgds.push_back(fgd);
						break;
					}
				}
			}
		}
		if (game.fgds.empty()) {
			game.notes.emplace_back("No FGD found - add one in the game settings");
		}
	}

	// Names for menus: many games and mods share a title ("HALF-LIFE 2" in hl2/, in a mod made
	// from the SDK template, ...). Titles in capitals are shown in title case; a shared title
	// gets the folder name so every entry can be told apart.
	for (auto& game : result.games) {
		const AppId app = game.isSourceMod ? game.baseAppId : game.ownerAppId;
		if (const auto* steamApp = result.steam.findApp(app); steamApp && !steamApp->name.empty()) {
			game.appName = steamApp->name;
		} else if (const auto* known = findKnownApp(app)) {
			game.appName = std::string(known->name);
		} else if (app != 0) {
			game.appName = "App " + std::to_string(app);
		}
		game.label = game.title;
		const bool upper = std::none_of(game.label.begin(), game.label.end(), [](unsigned char c) { return std::islower(c); });
		if (upper) {
			bool word = true;
			for (auto& c : game.label) {
				const auto u = static_cast<unsigned char>(c);
				c = static_cast<char>(word ? std::toupper(u) : std::tolower(u));
				word = !std::isalnum(u) && c != '\'';
			}
		}
	}
	const auto clashes = [&](const GameEntry& game, const auto& name) {
		return std::count_if(result.games.begin(), result.games.end(), [&](const GameEntry& other) { return iequals(name(other), name(game)); }) > 1;
	};
	std::vector<std::string> labels;
	for (const auto& game : result.games) {
		std::string label = game.label;
		if (clashes(game, [](const GameEntry& g) { return g.title; })) {
			label += " (" + game.folder() + ")";
			// Same title and folder (hl2/ of Half-Life 2 and of Source SDK Base 2013): name the app.
			if (clashes(game, [](const GameEntry& g) { return g.title + "/" + g.folder(); })) {
				label = game.label + " (" + (game.isSourceMod ? "sourcemods" : game.appName) + ", " + game.folder() + ")";
			}
		}
		labels.push_back(std::move(label));
	}
	for (std::size_t i = 0; i < labels.size(); ++i) {
		result.games[i].label = std::move(labels[i]);
	}

	std::stable_sort(result.games.begin(), result.games.end(), [](const GameEntry& a, const GameEntry& b) {
		if (a.isSourceMod != b.isSourceMod) {
			return !a.isSourceMod;
		}
		return toLower(a.title) < toLower(b.title);
	});
	return result;
}

} // namespace sme
