// test_mod_template.cpp - A new mod from the template: gameinfo.txt, folders, resource file
// (UTF-16), Mapbase's shared content, the new mod being found by the game discovery, and the
// base game files it needs (skill values, HUD texts).

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <map>

#include <sme/game_discovery.h>
#include <sme/gameinfo.h>
#include <sme/localization.h>
#include <sme/mod_base_files.h>
#include <sme/mod_menu.h>
#include <sme/mod_template.h>
#include <sme/steam.h>
#include <sme/text.h>

#include "fixture.h"

using namespace sme;

namespace {

std::vector<char> readAll(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("mod template: folder names, gameinfo.txt, resource file") {
	CHECK(isValidModFolder("my_mod-2"));
	CHECK_FALSE(isValidModFolder(""));
	CHECK_FALSE(isValidModFolder("-mod"));
	CHECK_FALSE(isValidModFolder("my mod"));
	CHECK_FALSE(isValidModFolder("../evil"));
	CHECK(suggestModFolder("My Cool Mod!") == "my_cool_mod");
	CHECK(suggestModFolder("  ") == "");

	ModTemplate mod{"My \"Cool\" Mod", "coolmod", ModBase::Singleplayer, {}};
	const std::string text = gameInfoText(mod);
	CHECK(text.find("game\t\t\"My 'Cool' Mod\"") != std::string::npos);   // no escapes in the engine's files
	CHECK(text.find("singleplayer_only") != std::string::npos);
	CHECK(text.find("SteamAppId\t\t243730") != std::string::npos);
	// The mod's own folder comes before the base game's content.
	CHECK(text.find("|gameinfo_path|.") < text.find("hl2/hl2_textures.vpk"));
	const auto parsed = GameInfo::parse(text, "/tmp/coolmod");
	REQUIRE(parsed);
	CHECK(parsed->game == "My 'Cool' Mod");
	CHECK(parsed->steamAppId == 243730);
	CHECK(parsed->searchPaths.size() > 10);

	mod.base = ModBase::Multiplayer;
	const std::string mp = gameInfoText(mod);
	CHECK(mp.find("multiplayer_only") != std::string::npos);
	CHECK(mp.find("243750") != std::string::npos);
	CHECK(mp.find("hl2mp") != std::string::npos);

	// UTF-16 LE with BOM; "Ü" stays one code unit.
	ModTemplate umlaut{"Über", "ueber", ModBase::Singleplayer, {}};
	const auto resource = modResourceFile(umlaut);
	REQUIRE(resource.size() > 4);
	CHECK(resource[0] == std::byte{0xFF});
	CHECK(resource[1] == std::byte{0xFE});
	std::u16string units;
	for (std::size_t i = 2; i + 1 < resource.size(); i += 2) {
		units += static_cast<char16_t>(std::to_integer<unsigned>(resource[i]) | (std::to_integer<unsigned>(resource[i + 1]) << 8));
	}
	CHECK(units.find(u"\"GameTitle\"\t\"Über\"") != std::u16string::npos);
	CHECK(units.find(u"\"lang\"") == 0);

	const auto& folders = modFolders();
	for (const char* needed : {"maps", "mapsrc", "materials", "models", "sound", "scripts", "cfg", "resource"}) {
		CHECK(std::find(folders.begin(), folders.end(), needed) != folders.end());
	}
}

TEST_CASE("mod template: a new Mapbase mod is created and discovered") {
	test::Fixture f;
	test::createFixture(f);
	const auto sourceMods = f.steam / "steamapps/sourcemods";
	const auto mounts = mapbaseMounts(sourceMods);
	REQUIRE_FALSE(mounts.empty());
	CHECK(mounts.front().second == "|gameinfo_path|../mapbase_shared/shared_misc");
	CHECK(mapbaseMounts(f.root / "nothing").empty());

	test::writeText(f.root / "work/first.vmf", "versioninfo\n{\n}\n");
	ModTemplate mod{"Second Mod", "secondmod", ModBase::Mapbase, mounts};
	const auto created = createMod(sourceMods, mod, {f.root / "work/first.vmf"});
	REQUIRE(created.error.empty());
	CHECK(std::filesystem::exists(created.folder / "gameinfo.txt"));
	CHECK(std::filesystem::is_directory(created.folder / "mapsrc"));
	CHECK(std::filesystem::exists(created.folder / "cfg/autoexec.cfg"));
	CHECK(std::filesystem::exists(created.folder / "resource/secondmod_english.txt"));
	REQUIRE(created.copiedMaps.size() == 1);
	CHECK(readAll(created.copiedMaps.front()).size() == readAll(f.root / "work/first.vmf").size());
	// Not twice, and not with a bad name.
	CHECK_FALSE(createMod(sourceMods, mod).error.empty());
	CHECK_FALSE(createMod(sourceMods, ModTemplate{"x", "../x", ModBase::Singleplayer, {}}).error.empty());

	const auto found = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& game : found.games) {
		if (game.key() == "sourcemods/secondmod") entry = &game;
	}
	REQUIRE(entry);
	CHECK(entry->title == "Second Mod");
	CHECK(entry->isSourceMod);
	CHECK(entry->usesMapbase);
}

namespace {

/// A file reader over a map of path -> text (UTF-16 for .txt files, as the game ships them).
ModFileReader readerOf(std::map<std::string, std::string> files) {
	return [files = std::move(files)](std::string_view path) -> std::optional<std::vector<std::byte>> {
		const auto it = files.find(std::string(path));
		if (it == files.end()) return std::nullopt;
		if (path.ends_with(".txt")) return encodeUtf16Le(it->second);
		std::vector<std::byte> bytes;
		for (const char c : it->second) bytes.push_back(static_cast<std::byte>(c));
		return bytes;
	};
}

std::string langFile(std::string_view language, std::string_view tokens) {
	return "\"lang\"\n{\n\"Language\" \"" + std::string(language) + "\"\n\"Tokens\"\n{\n" + std::string(tokens) + "}\n}\n";
}

} // namespace

TEST_CASE("mod base files: skill values and HUD texts of the base game, never replacing the mod's") {
	test::Fixture f;
	test::createFixture(f);
	const auto sourceMods = f.root / "sourcemods";
	const auto created = createMod(sourceMods, ModTemplate{"Cool Mod", "coolmod", ModBase::Singleplayer, {}});
	REQUIRE(created.error.empty());
	const auto& dir = created.folder;
	// The template has GameTitle only: no skill files (ammo limits 0), no game texts.
	auto check = checkModBaseFiles(dir, "coolmod", true);
	CHECK(check.missingFiles == std::vector<std::string>{"cfg/skill.cfg", "cfg/skill_episodic.cfg", "cfg/skill_manifest.cfg"});
	CHECK(check.missingTexts);
	CHECK(modSkillFiles(false) == std::vector<std::string>{"cfg/skill.cfg", "cfg/skill_manifest.cfg"});

	// Nothing to take them from: named, nothing written.
	auto result = addModBaseFiles(dir, "coolmod", true, {});
	CHECK(result.error.empty());
	CHECK(result.added.empty());
	CHECK(result.notFound == std::vector<std::string>{"cfg/skill.cfg", "cfg/skill_episodic.cfg", "resource/coolmod_english.txt"});

	// The mapper renamed a weapon already: that text stays.
	auto english = loadLocalizationFile(dir / "resource/coolmod_english.txt");
	REQUIRE(english);
	english->set("HL2_SMG1", "My SMG");
	REQUIRE(saveLocalizationFile(dir / "resource/coolmod_english.txt", *english));

	ModBaseSources sources;
	// Valve's template: Half-Life 2's skill.cfg, Episode Two's skill_episodic.cfg (no skill.cfg there).
	sources.templateFiles = readerOf({
		{"hl2/cfg/skill.cfg", "sk_max_ar2\t\t\"60\"\n"},
		{"episodic/cfg/skill_episodic.cfg", "sk_zombie_soldier_health\t\"100\"\n"},
		{"episodic/resource/mod_episodic_english.txt", langFile("English", "\"HL2_Pulse_Rifle\" \"PULSE RIFLE\"\n\"GameTitle\" \"HALF-LIFE 2\"\n")},
	});
	// The game: texts of Half-Life 2 and Episode Two, a German file, a console-only token.
	sources.gameFiles = readerOf({
		{"cfg/skill.cfg", "sk_max_ar2\t\t\"1\"\n"},   // the template's wins
		{"resource/hl2_english.txt", langFile("English", "\"HL2_Pulse_Rifle\" \"OLD\"\n\"HL2_SMG1\" \"SMG\"\n\"HL2_357Handgun\" \".357\"\n"
		                                                 "\"HL2_Hint\" \"Press A\" [$X360]\n\"HL2_Hint\" \"Press E\" [$WIN32]\n")},
		{"resource/ep2_english.txt", langFile("English", "\"EP2_Magnusson\" \"Magnusson\"\n")},
		{"resource/hl2_german.txt", langFile("German", "\"HL2_Pulse_Rifle\" \"PULSGEWEHR\"\n\"GameTitle\" \"HALF-LIFE 2\"\n")},
	});
	result = addModBaseFiles(dir, "coolmod", true, sources);
	CHECK(result.error.empty());
	CHECK(result.notFound.empty());
	CHECK(readAll(dir / "cfg/skill.cfg").size() == std::string("sk_max_ar2\t\t\"60\"\n").size());
	CHECK(std::filesystem::exists(dir / "cfg/skill_episodic.cfg"));
	const auto manifest = readFileText(dir / "cfg/skill_manifest.cfg");
	REQUIRE(manifest);
	CHECK(manifest->find("exec skill.cfg\nexec skill_episodic.cfg\n") != std::string::npos);

	english = loadLocalizationFile(dir / "resource/coolmod_english.txt");
	REQUIRE(english);
	CHECK(english->get("GameTitle") == "Cool Mod");           // the mod's title, not Half-Life 2's
	CHECK(english->get("HL2_SMG1") == "My SMG");             // the mapper's text stays
	CHECK(english->get("HL2_Pulse_Rifle") == "PULSE RIFLE"); // the template over the game
	CHECK(english->get("HL2_357Handgun") == ".357");
	CHECK(english->get("EP2_Magnusson") == "Magnusson");
	int hints = 0;
	for (const auto& token : english->tokens) hints += token.token == "HL2_Hint";
	CHECK(hints == 2);                                       // both platform variants
	const auto german = loadLocalizationFile(dir / "resource/coolmod_german.txt");
	REQUIRE(german);
	CHECK(german->language == "German");
	CHECK(german->get("GameTitle") == "Cool Mod");
	CHECK(german->get("HL2_Pulse_Rifle") == "PULSGEWEHR");
	CHECK_FALSE(std::filesystem::exists(dir / "resource/coolmod_french.txt"));   // English covers it
	CHECK(result.tokens == 5 + 1);
	CHECK(checkModBaseFiles(dir, "coolmod", true).complete());

	// Again: nothing to do, nothing changes.
	const auto before = readAll(dir / "resource/coolmod_english.txt");
	result = addModBaseFiles(dir, "coolmod", true, sources);
	CHECK(result.added.empty());
	CHECK(result.tokens == 0);
	CHECK(readAll(dir / "resource/coolmod_english.txt") == before);
	CHECK(skillManifestText({"cfg/skill.cfg"}).ends_with("exec skill.cfg\n"));
}
