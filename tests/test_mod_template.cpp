// test_mod_template.cpp - A new mod from the template: gameinfo.txt, folders, resource file
// (UTF-16), Mapbase's shared content, and the new mod being found by the game discovery.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include <sme/game_discovery.h>
#include <sme/gameinfo.h>
#include <sme/mod_template.h>
#include <sme/steam.h>

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
