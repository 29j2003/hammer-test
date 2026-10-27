// test_core.cpp - Tests for Steam/game discovery, search paths, file system, materials, textures, FGDs.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/entity_catalog.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/game_discovery.h>
#include <sme/gameinfo.h>
#include <sme/hammer_config.h>
#include <sme/material.h>
#include <sme/steam.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <algorithm>

using namespace sme;
using sme::test::Fixture;

namespace {

const GameEntry& findGame(const DiscoveryResult& result, std::string_view key) {
	for (const auto& game : result.games) {
		if (game.key() == key) {
			return game;
		}
	}
	FAIL("game not found: " << key);
	throw;
}

std::unique_ptr<LoadedGame> openGame(const Fixture& f, std::string_view key, DiscoveryResult& result) {
	result = discoverGames(SteamInstallation::load(f.steam));
	const auto& entry = findGame(result, key);
	std::string error;
	auto game = loadGame(entry, result.steam, entry.fgds, &error);
	REQUIRE_MESSAGE(game, error);
	return game;
}

} // namespace

TEST_CASE("text helpers") {
	CHECK(normalizePath("Materials\\Concrete//Wall.VMT") == "materials/concrete/wall.vmt");
	CHECK(normalizePath("./a/b/") == "a/b");
	CHECK(iequals("GameInfo", "gameinfo"));
	CHECK(splitTrimmed(" a, b ,,c ", ',') == std::vector<std::string>{"a", "b", "c"});
	CHECK(normalizeMaterialName("materials\\Tools/ToolsNodraw.vmt") == "tools/toolsnodraw");
}

TEST_CASE("steam libraries and app manifests") {
	Fixture f;
	test::createFixture(f);
	const auto steam = SteamInstallation::load(f.steam);
	CHECK(steam.found());
	CHECK(steam.libraries().size() == 2);
	REQUIRE(steam.findApp(220));
	CHECK(steam.findApp(220)->name == "Half-Life 2");
	REQUIRE(steam.findApp(243730));
	CHECK(std::filesystem::equivalent(steam.findApp(243730)->installDir, f.sdkBase));
	CHECK(std::filesystem::equivalent(steam.sourceModsDir(), f.steam / "steamapps" / "sourcemods"));
}

TEST_CASE("game discovery finds games, mods, Mapbase and Hammer++ FGDs") {
	Fixture f;
	test::createFixture(f);
	const auto result = discoverGames(SteamInstallation::load(f.steam));

	const auto& hl2 = findGame(result, "220/hl2");
	CHECK(hl2.title == "HALF-LIFE 2");
	CHECK(hl2.branch == "Source SDK 2013");
	REQUIRE(hl2.fgds.size() == 1);
	CHECK(hl2.fgds[0].filename() == "halflife2.fgd");          // main FGD, not hl2mp.fgd
	CHECK(hl2.availableFgds.size() == 3);

	const auto& mod = findGame(result, "sourcemods/mymod");
	CHECK(mod.isSourceMod);
	CHECK(mod.usesMapbase);
	CHECK(mod.baseAppId == 243730);
	CHECK(mod.branch == "Source SDK 2013 (Mapbase)");
	REQUIRE(mod.fgds.size() == 2);                             // from the Hammer++ config
	CHECK(mod.fgds[1].filename() == "mymod.fgd");
	CHECK(mod.hammerConfig.filename() == "hammerplusplus_gameconfig.txt");

	CHECK(findGame(result, "243730/hl2").title == "Half-Life 2");
	CHECK(result.games.back().isSourceMod);                    // games first, mods last
}

TEST_CASE("relative Steam path still matches the Hammer++ config") {
	Fixture f;
	test::createFixture(f);
	// Work from the fixture folder so the relative path stays on one drive (Windows).
	struct CurrentDir {
		std::filesystem::path previous = std::filesystem::current_path();
		~CurrentDir() { std::filesystem::current_path(previous); }
	} restore;
	std::filesystem::current_path(f.root);
	const std::filesystem::path relative = "Steam";
	const auto result = discoverGames(SteamInstallation::load(relative));
	CHECK(findGame(result, "sourcemods/mymod").fgds.size() == 2);
	CHECK(SteamInstallation::load(relative).root().is_absolute());
}

TEST_CASE("gameinfo search paths resolve tokens, wildcards, conditionals") {
	Fixture f;
	test::createFixture(f);
	const auto steam = SteamInstallation::load(f.steam);
	auto info = GameInfo::load(f.mod);
	REQUIRE(info);
	CHECK(info->steamAppId == 243730);
	resolveSearchPaths(*info, makeSearchPathContext(*info, &steam));
	REQUIRE(info->searchPaths.size() == 7);
	CHECK(info->searchPaths[0].isWildcard);
	CHECK(info->searchPaths[1].resolved == f.mod);
	CHECK(info->searchPaths[4].isVpk);
	CHECK(std::filesystem::equivalent(info->searchPaths[5].resolved, f.sdkBase / "hl2"));
	CHECK(info->searchPaths[6].resolved.filename() == "hl2_misc.vpk");           // |appid_220|
	CHECK(info->searchPaths[6].resolved.parent_path().parent_path().filename() == "Half-Life 2");

	auto hl2 = GameInfo::load(f.hl2);
	REQUIRE(hl2);
	resolveSearchPaths(*hl2, makeSearchPathContext(*hl2, &steam));
	const auto& x360 = hl2->searchPaths.back();
	CHECK(x360.conditional == "$X360");
	CHECK_FALSE(x360.conditionalActive);
	CHECK(evaluateConditional("!$X360"));
}

TEST_CASE("file system: search order, loose overrides, VPKs, case-insensitive, listing") {
	Fixture f;
	test::createFixture(f);
	DiscoveryResult result;
	const auto game = openGame(f, "220/hl2", result);
	const auto& fs = game->files;

	// custom/* first, then hl2/ (loose), then VPKs.
	REQUIRE(fs.locate("materials/custom/override.vmt"));
	CHECK(fs.mounts()[*fs.locate("materials/custom/override.vmt")].path.filename() == "mypack");

	const auto wall = fs.readText("MATERIALS\\concrete\\WALL01.vmt");
	REQUIRE(wall);
	CHECK(wall->find("loose") != std::string::npos);
	CHECK(fs.readText("scripts/test.txt") == std::optional<std::string>("from misc vpk"));
	CHECK_FALSE(fs.exists("../gameinfo.txt"));                 // never leaves a mount
	CHECK_FALSE(fs.exists("materials/missing.vmt"));
	CHECK(fs.exists("resource/readme.txt", "platform"));
	CHECK_FALSE(fs.exists("resource/readme.txt", "game"));

	const auto materials = fs.list("materials", ".vmt");
	CHECK(std::count(materials.begin(), materials.end(), "materials/concrete/wall01.vmt") == 1);
	CHECK(std::find(materials.begin(), materials.end(), "materials/custom/override.vmt") != materials.end());
	CHECK(fs.list("materials/tools", ".vtf").size() == 3);

	// hl2_lv is a game_lv path and missing: listed but never used for "game".
	const auto lv = std::find_if(fs.mounts().begin(), fs.mounts().end(), [](const Mount& m) {
		return m.path.filename() == "hl2_lv.vpk";
	});
	REQUIRE(lv != fs.mounts().end());
	CHECK_FALSE(lv->present);
}

TEST_CASE("mod mounts the base game and reports missing Mapbase content") {
	Fixture f;
	test::createFixture(f);
	DiscoveryResult result;
	const auto game = openGame(f, "sourcemods/mymod", result);
	CHECK(game->files.exists("materials/tools/toolsnodraw.vmt"));
	CHECK(game->files.exists("scripts/test.txt"));                 // through |appid_220|
	const bool reportsShared = std::any_of(game->problems.begin(), game->problems.end(), [](const std::string& p) {
		return p.find("shared_misc") != std::string::npos;
	});
	CHECK(reportsShared);
	CHECK(game->entities.find("logic_mymod"));
	CHECK(game->entities.find("light"));
}

TEST_CASE("materials: params, patch, tools, loops") {
	Fixture f;
	test::createFixture(f);
	DiscoveryResult result;
	const auto game = openGame(f, "sourcemods/mymod", result);

	std::string error;
	const auto patched = loadMaterial(game->files, "mymod/patched", &error);
	REQUIRE_MESSAGE(patched, error);
	CHECK(patched->shader == "LightmappedGeneric");
	CHECK(patched->param("$surfaceprop") == "metal");
	CHECK(patched->param("$DETAIL") == "detail/noise");
	CHECK(patched->previewTexture() == "concrete/wall01");
	CHECK(patched->includeChain == std::vector<std::string>{"concrete/wall01"});

	const auto nodraw = loadMaterial(game->files, "materials/tools/toolsnodraw.vmt");
	REQUIRE(nodraw);
	CHECK(nodraw->isTool());
	CHECK_FALSE(patched->isTool());

	CHECK_FALSE(loadMaterial(game->files, "mymod/loop1", &error));
	CHECK(error.find("too deep") != std::string::npos);
}

TEST_CASE("textures decode (RGBA and DXT1) and pick small mips") {
	Fixture f;
	test::createFixture(f);
	DiscoveryResult result;
	const auto game = openGame(f, "220/hl2", result);

	const auto full = loadTexture(game->files, "dev/dev_measuregeneric01");
	REQUIRE(full);
	CHECK(full->width == 128);
	CHECK(full->pixels.size() == 128u * 128u * 4u);
	CHECK(full->pixels[0] == std::byte(230));

	const auto thumb = loadTexture(game->files, "dev/dev_measuregeneric01", 32);
	REQUIRE(thumb);
	CHECK(thumb->width == 32);

	const auto dxt = loadTexture(game->files, "concrete/floor01");
	REQUIRE(dxt);
	CHECK(dxt->width == 64);
	const auto info = readTextureInfo(*game->files.read("materials/concrete/floor01.vtf"));
	REQUIRE(info);
	CHECK(info->format == "DXT1");

	CHECK_FALSE(decodeTexture(std::vector<std::byte>(16)));
	CHECK(encodePng(*thumb).size() > 8);
}

TEST_CASE("FGD: includes, base classes, helpers, choices, flags, IO") {
	Fixture f;
	test::createFixture(f);
	EntityCatalog catalog;
	std::vector<std::string> errors;
	REQUIRE(catalog.load({f.sdkBase / "bin/halflife2.fgd", f.mod / "mymod.fgd"}, &errors));
	CHECK(errors.empty());

	const auto* light = catalog.find("LIGHT");
	REQUIRE(light);
	CHECK(light->classType == "PointClass");
	CHECK(light->property("targetname"));                          // Parentname -> Targetname
	CHECK(light->property("parentname"));
	CHECK(light->helper("iconsprite") == "\"editor/light.vmt\"");
	REQUIRE(light->property("style"));
	CHECK(light->property("style")->choices.size() == 2);
	REQUIRE(light->property("spawnflags"));
	CHECK(light->property("spawnflags")->flags.size() == 1);
	const auto hasKill = std::any_of(light->inputs.begin(), light->inputs.end(), [](const EntityIo& io) { return io.name == "Kill"; });
	CHECK(hasKill);
	CHECK(light->inputs.size() == 3);

	const auto* start = catalog.find("info_player_start");
	REQUIRE(start);
	CHECK(start->helper("studio").find("playerstart") != std::string::npos);
	CHECK(start->helper("size") == "-16 -16 0, 16 16 72");

	CHECK(catalog.find("func_detail")->isSolid());
	CHECK(catalog.find("Targetname")->isBase());
	const auto placeable = catalog.placeable();
	CHECK(std::none_of(placeable.begin(), placeable.end(), [](const EntityClass* e) { return e->isBase(); }));
	CHECK(catalog.find("logic_mymod"));
}

TEST_CASE("Hammer GameConfig.txt with Windows paths") {
	const auto config = parseHammerConfig(R"("Configs"
{
	"Games"
	{
		"Half-Life 2"
		{
			"GameDir"		"C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2\hl2"
			"Hammer"
			{
				"GameData0"		"C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2\bin\halflife2.fgd"
				"BSP"		"C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2\bin\vbsp.exe"
				"DefaultTextureScale"		"0.250000"
			}
		}
	}
	"SDKVersion"		"5"
})", "GameConfig.txt");
	REQUIRE(config);
	REQUIRE(config->games.size() == 1);
	const auto& game = config->games[0];
	CHECK(game.name == "Half-Life 2");
	CHECK(displayPath(game.gameDir) == "C:/Program Files (x86)/Steam/steamapps/common/Half-Life 2/hl2");
	REQUIRE(game.fgds.size() == 1);
	CHECK(game.fgds[0].filename() == "halflife2.fgd");
	CHECK(game.vbsp.filename() == "vbsp.exe");
	CHECK(game.defaultTextureScale == doctest::Approx(0.25));
	CHECK(config->flavor == "Hammer");
}
