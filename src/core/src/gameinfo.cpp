// gameinfo.cpp - gameinfo.txt parsing and search path resolution (see gameinfo.h).

#include <sme/gameinfo.h>

#include <sme/text.h>

#include <system_error>

#include <kvpp/KV1.h>

namespace sme {

namespace {

constexpr std::string_view TOKEN_GAMEINFO = "|gameinfo_path|";
constexpr std::string_view TOKEN_ENGINE = "|all_source_engine_paths|";
constexpr std::string_view TOKEN_APPID = "|appid_";

bool platformSymbol(std::string_view symbol) {
#if defined(_WIN32)
	return iequals(symbol, "$WIN32") || iequals(symbol, "$WINDOWS") || iequals(symbol, "$WIN64");
#elif defined(__APPLE__)
	return iequals(symbol, "$OSX") || iequals(symbol, "$POSIX");
#else
	return iequals(symbol, "$LINUX") || iequals(symbol, "$POSIX");
#endif
}

} // namespace

bool evaluateConditional(std::string_view conditional) {
	// Supports the forms Valve uses in gameinfo files: "$X", "!$X" and "$X || $Y".
	const auto text = trim(conditional);
	if (text.empty()) {
		return true;
	}
	std::size_t start = 0;
	while (start <= text.size()) {
		auto end = text.find("||", start);
		if (end == std::string_view::npos) {
			end = text.size();
		}
		auto term = trim(text.substr(start, end - start));
		bool negate = false;
		if (term.starts_with('!')) {
			negate = true;
			term = trim(term.substr(1));
		}
		if (platformSymbol(term) != negate) {
			return true;
		}
		start = end + 2;
	}
	return false;
}

std::optional<GameInfo> GameInfo::load(const std::filesystem::path& gameDir, std::string* error) {
	const auto file = findChildI(gameDir, "gameinfo.txt");
	const auto text = file ? readFileText(*file) : std::nullopt;
	if (!text) {
		if (error) {
			*error = "gameinfo.txt not found in " + displayPath(gameDir);
		}
		return std::nullopt;
	}
	return parse(*text, gameDir, error);
}

std::optional<GameInfo> GameInfo::parse(std::string_view text, const std::filesystem::path& gameDir, std::string* error) {
	const kvpp::KV1<> kv(text);
	const auto& root = kv["GameInfo"];
	if (root.isInvalid()) {
		if (error) {
			*error = "gameinfo.txt has no GameInfo block";
		}
		return std::nullopt;
	}

	GameInfo info;
	info.gameDir = gameDir;
	info.game = std::string(root["game"].getValue());
	info.title = std::string(root["title"].getValue());
	info.type = std::string(root["type"].getValue());

	const auto& fileSystem = root["FileSystem"];
	if (fileSystem.isInvalid()) {
		info.warnings.emplace_back("gameinfo.txt has no FileSystem block");
		return info;
	}
	info.steamAppId = static_cast<AppId>(fileSystem["SteamAppId"].getValue<int64_t>());
	info.toolsAppId = static_cast<AppId>(fileSystem["ToolsAppId"].getValue<int64_t>());

	const auto& searchPaths = fileSystem["SearchPaths"];
	if (searchPaths.isInvalid()) {
		info.warnings.emplace_back("gameinfo.txt has no SearchPaths block");
		return info;
	}
	for (const auto& entry : searchPaths) {
		if (entry.getChildCount() > 0) {
			continue; // Nested blocks belong to newer branches (e.g. Strata) and are not search paths.
		}
		SearchPath path;
		path.pathIds = splitTrimmed(toLower(entry.getKey()), '+');
		path.raw = std::string(trim(entry.getValue()));
		auto conditional = trim(entry.getConditional());
		if (conditional.starts_with('[') && conditional.ends_with(']')) {
			conditional = conditional.substr(1, conditional.size() - 2);
		}
		path.conditional = std::string(conditional);
		path.conditionalActive = evaluateConditional(path.conditional);
		const auto normalized = normalizePath(path.raw);
		path.isWildcard = normalized.ends_with("/*") || normalized == "*";
		path.isVpk = !path.isWildcard && normalized.ends_with(".vpk");
		info.searchPaths.push_back(std::move(path));
	}
	return info;
}

SearchPathContext makeSearchPathContext(const GameInfo& info, const SteamInstallation* steam) {
	SearchPathContext context;
	context.baseDir = info.gameDir.parent_path();
	if (steam) {
		if (const auto* app = steam->findApp(info.steamAppId)) {
			// A game that lives inside its own app folder keeps that folder as base;
			// a sourcemod uses the folder of the base game it runs on.
			const auto parent = info.gameDir.parent_path();
			std::error_code ec;
			if (!std::filesystem::equivalent(parent, app->installDir, ec)) {
				context.baseDir = app->installDir;
			}
		}
		context.appInstallDir = [steam](AppId appId) -> std::optional<std::filesystem::path> {
			if (const auto* app = steam->findApp(appId)) {
				return app->installDir;
			}
			return std::nullopt;
		};
	}
	return context;
}

void resolveSearchPaths(GameInfo& info, const SearchPathContext& context) {
	for (auto& path : info.searchPaths) {
		path.problem.clear();
		std::string value = path.raw;
		std::filesystem::path base;
		std::string rest;

		if (startsWithI(value, TOKEN_GAMEINFO)) {
			base = info.gameDir;
			rest = value.substr(TOKEN_GAMEINFO.size());
		} else if (startsWithI(value, TOKEN_ENGINE)) {
			base = context.baseDir;
			rest = value.substr(TOKEN_ENGINE.size());
		} else if (startsWithI(value, TOKEN_APPID)) {
			const auto close = value.find('|', TOKEN_APPID.size());
			const auto idText = close == std::string::npos ? std::string{} : value.substr(TOKEN_APPID.size(), close - TOKEN_APPID.size());
			AppId appId = 0;
			try {
				appId = static_cast<AppId>(std::stoul(idText));
			} catch (...) {
				path.problem = "Invalid app id token";
				continue;
			}
			const auto dir = context.appInstallDir ? context.appInstallDir(appId) : std::nullopt;
			if (!dir) {
				path.problem = "App " + std::to_string(appId) + " is not installed";
				continue;
			}
			base = *dir;
			rest = value.substr(close + 1);
		} else {
			const auto native = toNativePath(value);
			if (native.is_absolute()) {
				path.resolved = native;
				continue;
			}
			base = context.baseDir;
			rest = value;
		}

		std::string slashed;
		for (const char c : rest) {
			slashed.push_back(c == '\\' ? '/' : c);
		}
		while (slashed.starts_with('/')) {
			slashed.erase(0, 1);
		}
		if (path.isWildcard) {
			slashed.erase(slashed.size() - 1); // drop "*"
		}
		if (slashed.empty() || slashed == "." || slashed == "./") {
			path.resolved = base;
			continue;
		}
		// Prefer the real spelling on disk; keep the written one if it does not exist (reported later).
		path.resolved = resolveI(base, slashed).value_or(base / std::filesystem::path(slashed));
		path.resolved = path.resolved.lexically_normal();
	}
}

} // namespace sme
