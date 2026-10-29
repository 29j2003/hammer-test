// mod_base_files.cpp - Base game files a mod needs (see mod_base_files.h).

#include <sme/mod_base_files.h>

#include <sme/localization.h>
#include <sme/mod_menu.h>
#include <sme/text.h>

#include <fstream>
#include <system_error>
#include <unordered_set>

namespace sme {

namespace {

/// A token's identity in a file: name (lower case) and condition ("[$X360]" and the like).
std::string tokenKey(const LocalizedToken& token) {
	return toLower(token.token) + "|" + token.condition;
}

std::optional<LocalizationFile> readTexts(const ModFileReader& reader, const std::string& path) {
	if (!reader) return std::nullopt;
	const auto bytes = reader(path);
	if (!bytes || bytes->empty()) return std::nullopt;
	auto file = parseLocalizationFile(decodeTextFile(*bytes));
	if (file.tokens.empty()) return std::nullopt;
	return file;
}

std::optional<std::vector<std::byte>> readFirst(const ModBaseSources& sources, const std::vector<std::string>& templatePaths, const std::string& gamePath) {
	if (sources.templateFiles) {
		for (const auto& path : templatePaths) {
			if (auto bytes = sources.templateFiles(path); bytes && !bytes->empty()) return bytes;
		}
	}
	if (sources.gameFiles) {
		if (auto bytes = sources.gameFiles(gamePath); bytes && !bytes->empty()) return bytes;
	}
	return std::nullopt;
}

} // namespace

std::vector<std::string> modSkillFiles(bool episodic) {
	if (episodic) return {"cfg/skill.cfg", "cfg/skill_episodic.cfg", "cfg/skill_manifest.cfg"};
	return {"cfg/skill.cfg", "cfg/skill_manifest.cfg"};
}

ModBaseCheck checkModBaseFiles(const std::filesystem::path& modDir, std::string_view modFolder, bool episodic) {
	ModBaseCheck check;
	std::error_code ec;
	for (const auto& file : modSkillFiles(episodic)) {
		if (!std::filesystem::is_regular_file(modDir / file, ec)) check.missingFiles.push_back(file);
	}
	const auto texts = loadLocalizationFile(localizationPath(modDir, modFolder, "english"));
	check.missingTexts = !texts || texts->find(HL2_TEXT_PROBE) < 0;
	return check;
}

std::string skillManifestText(const std::vector<std::string>& skillFiles) {
	std::string text = "// skill_manifest.cfg - the game runs this at every map start; it loads the skill values\n"
	                   "// (damage, health, ammo limits) of the files below.\n";
	for (const auto& file : skillFiles) {
		const auto slash = file.find_last_of('/');
		const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
		if (name != "skill_manifest.cfg") text += "exec " + name + "\n";
	}
	return text;
}

ModBaseResult addModBaseFiles(const std::filesystem::path& modDir, std::string_view modFolder, bool episodic, const ModBaseSources& sources) {
	ModBaseResult result;
	std::error_code ec;
	std::filesystem::create_directories(modDir / "cfg", ec);
	const auto write = [&](const std::string& relative, const void* data, std::size_t size) {
		std::ofstream out(modDir / relative, std::ios::binary);
		out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		if (!out) {
			result.error = "Could not write " + (modDir / relative).string();
			return false;
		}
		result.added.push_back(relative);
		return true;
	};

	// Skill files: Valve's template (the Episode Two template has no skill.cfg: from Half-Life 2's),
	// then the game's own.
	std::vector<std::string> present;
	for (const auto& file : modSkillFiles(episodic)) {
		if (file == "cfg/skill_manifest.cfg") continue;
		if (std::filesystem::is_regular_file(modDir / file, ec)) {
			present.push_back(file);
			continue;
		}
		std::vector<std::string> templatePaths;
		if (episodic) templatePaths.push_back("episodic/" + file);
		templatePaths.push_back("hl2/" + file);
		if (const auto bytes = readFirst(sources, templatePaths, file)) {
			if (!write(file, bytes->data(), bytes->size())) return result;
			present.push_back(file);
		} else {
			result.notFound.push_back(file);
		}
	}
	// The manifest: always made here, so it runs exactly the files the mod has.
	if (!std::filesystem::is_regular_file(modDir / "cfg/skill_manifest.cfg", ec) && !present.empty()) {
		const std::string manifest = skillManifestText(present);
		if (!write("cfg/skill_manifest.cfg", manifest.data(), manifest.size())) return result;
	}

	// Texts, per language: sources from the strongest to the weakest.
	std::string title;   // the mod's own GameTitle (English), kept in every language
	for (const auto& language : sourceLanguages()) {
		std::vector<LocalizationFile> found;
		const auto fromTemplate = [&](const std::string& game) {
			if (auto file = readTexts(sources.templateFiles, game + "/resource/mod_" + game + "_" + language + ".txt")) found.push_back(std::move(*file));
		};
		const auto fromGame = [&](const std::string& name) {
			if (auto file = readTexts(sources.gameFiles, "resource/" + name + "_" + language + ".txt")) found.push_back(std::move(*file));
		};
		if (episodic) fromTemplate("episodic");
		fromTemplate("hl2");
		if (episodic) {
			fromGame("ep2");
			fromGame("episodic");
		}
		fromGame("hl2");
		if (found.empty()) {
			if (language == "english") result.notFound.push_back("resource/" + std::string(modFolder) + "_english.txt");
			continue;
		}
		const auto path = localizationPath(modDir, modFolder, language);
		const bool exists = std::filesystem::is_regular_file(path, ec);
		LocalizationFile file;
		if (exists) {
			auto loaded = loadLocalizationFile(path);
			if (!loaded) {
				result.error = "Could not read " + path.string();
				return result;
			}
			file = std::move(*loaded);
		} else {
			file.language = languageDisplayName(language);
			if (!title.empty()) file.set("GameTitle", title);
		}
		if (language == "english") title = file.get("GameTitle");
		std::unordered_set<std::string> keys;
		for (const auto& token : file.tokens) keys.insert(tokenKey(token));
		int added = 0;
		for (const auto& source : found) {
			for (const auto& token : source.tokens) {
				if (iequals(token.token, "GameTitle")) continue;   // the mod's own title
				const std::string key = tokenKey(token);
				if (keys.count(key) || keys.count(toLower(token.token) + "|")) continue;
				keys.insert(key);
				LocalizedToken copy = token;
				copy.comments.clear();
				file.tokens.push_back(std::move(copy));
				++added;
			}
		}
		if (added == 0) continue;
		std::string error;
		if (!saveLocalizationFile(path, file, &error)) {
			result.error = error;
			return result;
		}
		result.tokens += added;
		result.added.push_back("resource/" + path.filename().string());
	}
	return result;
}

} // namespace sme
