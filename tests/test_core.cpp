// test_core.cpp - Tests for Steam/game discovery, search paths, file system, materials, textures, FGDs.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/compile_options.h>
#include <sme/entity_catalog.h>
#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/game_discovery.h>
#include <sme/gameinfo.h>
#include <sme/hammer_config.h>
#include <sme/material.h>
#include <sme/steam.h>
#include <sme/text.h>
#include <sme/version.h>
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
	// From the Hammer++ config, plus Mapbase's FGD (the config only has the stock one).
	REQUIRE(mod.fgds.size() == 3);
	CHECK(mod.fgds[0].parent_path().filename() == "bin");
	CHECK(mod.fgds[1] == mod.mapbaseFgd);
	CHECK(mod.fgds[1].filename() == "halflife2.fgd");
	CHECK(mod.fgds[1].parent_path().parent_path().filename() == "shared_misc");
	CHECK(mod.fgds[2].filename() == "mymod.fgd");
	CHECK(isMapbaseFgd(mod.mapbaseFgd));
	CHECK_FALSE(isMapbaseFgd(mod.fgds[0]));
	CHECK(mod.hammerConfig.filename() == "hammerplusplus_gameconfig.txt");

	CHECK(findGame(result, "243730/hl2").title == "Half-Life 2");
	// Same title in several places: the menu names tell them apart.
	CHECK(hl2.label == "Half-Life 2 (Half-Life 2, hl2)");
	CHECK(findGame(result, "243730/hl2").label == "Half-Life 2 (Source SDK Base 2013 Singleplayer, hl2)");
	CHECK(hl2.appName == "Half-Life 2");
	CHECK(mod.appName == "Source SDK Base 2013 Singleplayer");
	for (const auto& game : result.games) {
		CHECK(std::count_if(result.games.begin(), result.games.end(), [&](const GameEntry& g) { return g.label == game.label; }) == 1);
	}
	CHECK(result.games.back().isSourceMod);                    // games first, mods last
}

TEST_CASE("a game with base content next to it (Entropy : Zero 2)") {
	Fixture f;
	test::createFixture(f);
	const auto app = f.steam / "steamapps/common/EntropyZero2";
	test::writeText(f.steam / "steamapps/appmanifest_1583720.acf",
	                "\"AppState\"\n{\n\t\"appid\"\t\t\"1583720\"\n\t\"name\"\t\t\"Entropy : Zero 2\"\n\t\"installdir\"\t\t\"EntropyZero2\"\n}\n");
	const auto gameInfo = [](std::string_view title, int appId) {
		return "\"GameInfo\"\n{\n\tgame \"" + std::string(title) + "\"\n\tFileSystem\n\t{\n\t\tSteamAppId " + std::to_string(appId) +
		       "\n\t\tSearchPaths\n\t\t{\n\t\t\tgame+mod\t|gameinfo_path|.\n\t\t\tgame\t|all_source_engine_paths|hl2\n\t\t}\n\t}\n}\n";
	};
	test::writeText(app / "hl2/gameinfo.txt", gameInfo("HALF-LIFE 2", 220));
	test::writeText(app / "EntropyZero2/gameinfo.txt", gameInfo("Entropy : Zero 2", 1583720));
	test::writeText(app / "EntropyZero2/bin/client.dll", "not a real dll");
	test::writeText(app / "bin/base.fgd", "// Mapbase base FGD\n@PointClass = env_global_light : \"Sun shadows\" []\n");
	test::writeText(app / "bin/halflife2.fgd", "@include \"base.fgd\"\n@PointClass = npc_citizen : \"Citizen\" []\n");
	test::writeText(app / "bin/ez2.fgd", "@include \"halflife2.fgd\"\n@PointClass = npc_wilson : \"Wilson\" []\n");

	const auto result = discoverGames(SteamInstallation::load(f.steam));
	const auto& ez2 = findGame(result, "1583720/entropyzero2");
	const auto& content = findGame(result, "1583720/hl2");
	CHECK_FALSE(ez2.baseContent);
	CHECK(content.baseContent);
	CHECK(ez2.usesMapbase);
	CHECK(ez2.label == "Entropy : Zero 2");
	CHECK(content.label.ends_with("base content"));
	REQUIRE(ez2.fgds.size() == 1);
	CHECK(ez2.fgds[0].filename() == "ez2.fgd");                // includes halflife2.fgd and Mapbase's base.fgd
	CHECK(isMapbaseFgd(ez2.fgds[0]));
	// Base content comes after the real games in the list.
	const auto at = [&](const GameEntry& game) { return &game - result.games.data(); };
	CHECK(at(content) > at(ez2));
	CHECK(at(content) > at(findGame(result, "220/hl2")));
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
	CHECK(findGame(result, "sourcemods/mymod").fgds.size() == 3);
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
	// Every key knows the class that defines it (the panel groups keys by it).
	CHECK(light->property("targetname")->origin == "Targetname");
	CHECK(light->property("parentname")->origin == "Parentname");
	CHECK(light->property("style")->origin == "light");

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

TEST_CASE("versions and checksums for updates") {
	CHECK(compareVersions("0.2.0", "0.1.9") > 0);
	CHECK(compareVersions("v0.10.0", "0.9.3") > 0);
	CHECK(compareVersions("0.2", "0.2.0") == 0);
	CHECK(compareVersions("V1.0.0", "1.0.1") < 0);
	CHECK(compareVersions("1.2.0-beta", "1.2.0") == 0);
	const std::string sums = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789ABCDEF  Mapsmith-0.2.0-windows.zip\n"
	                         "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff *other.zip\n";
	CHECK(checksumFor(sums, "Mapsmith-0.2.0-windows.zip") == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
	CHECK(checksumFor(sums, "other.zip").has_value());
	CHECK_FALSE(checksumFor(sums, "missing.zip"));
	CHECK_FALSE(checksumFor("not a checksum file", "x"));
}

TEST_CASE("file system sees files the editor writes") {
	Fixture f;
	test::createFixture(f);
	DiscoveryResult result;
	auto game = openGame(f, "sourcemods/mymod", result);
	CHECK_FALSE(game->files.exists("scripts/game_sounds_new.txt"));   // builds the folder index
	test::writeText(f.mod / "scripts/game_sounds_new.txt", "\"A\" { \"wave\" \"a.wav\" }");
	CHECK_FALSE(game->files.exists("scripts/game_sounds_new.txt"));   // not seen yet
	game->files.noteWrittenFile(f.mod / "scripts/game_sounds_new.txt");
	REQUIRE(game->files.exists("scripts/game_sounds_new.txt"));
	CHECK(game->files.readText("scripts/game_sounds_new.txt").value_or("").find("a.wav") != std::string::npos);
	game->files.noteWrittenFile(f.root / "elsewhere.txt");   // outside every folder: ignored
}

TEST_CASE("compile settings: steps, presets, game window, extra parameters, saving") {
	using sme::CompileSettings;
	using sme::CompileTool;
	const auto has = [](const std::vector<std::string>& args, std::string_view value) {
		return std::find(args.begin(), args.end(), value) != args.end();
	};
	CompileSettings settings;
	CHECK(settings.preset() == CompileSettings::Preset::Normal);
	auto steps = sme::compileCommands(settings, "/maps/src/arena", "/game/mod");
	REQUIRE(steps.size() == 5);
	CHECK(steps[0].name == "VBSP");
	CHECK(steps[0].arguments == std::vector<std::string>{"-game", "/game/mod", "/maps/src/arena"});
	CHECK(steps[1].name == "VVIS");
	CHECK(steps[2].name == "VRAD");
	CHECK(steps[3].tool == CompileTool::Copy);
	CHECK(steps[3].arguments == std::vector<std::string>{"/maps/src/arena.bsp", "/game/mod/maps/arena.bsp"});
	// The game: console, windowed, 1920x1080 and no intro video by default.
	const auto& game = steps[4];
	CHECK(game.detached);
	CHECK(sme::joinArguments(game.arguments) == "-console -windowed -w 1920 -h 1080 -novid -game /game/mod +map arena");

	// The game's own launch options: what the settings decide is not doubled, the rest stays.
	steps = sme::compileCommands(settings, "/maps/src/arena", "/game/mod", "-novid -dev -console -windowed -w 800 -h 600");
	CHECK(sme::joinArguments(steps.back().arguments) == "-console -windowed -w 1920 -h 1080 -novid -dev -game /game/mod +map arena");
	settings.console = false;
	settings.windowed = false;
	settings.width = settings.height = 0;
	steps = sme::compileCommands(settings, "/maps/src/arena", "/game/mod", "-console");
	CHECK(sme::joinArguments(steps.back().arguments) == "-novid -game /game/mod +map arena");
	settings = {};

	// Presets.
	settings.applyPreset(CompileSettings::Preset::Fast);
	CHECK(settings.preset() == CompileSettings::Preset::Fast);
	CHECK(has(sme::toolArguments(settings, CompileTool::Vvis), "-fast"));
	CHECK(sme::joinArguments(sme::toolArguments(settings, CompileTool::Vrad)) == "-bounce 2 -noextra");
	settings.applyPreset(CompileSettings::Preset::Final);
	CHECK(sme::joinArguments(sme::toolArguments(settings, CompileTool::Vrad)) == "-final");
	settings.visFast = true;
	CHECK(settings.preset() == CompileSettings::Preset::Custom);
	settings = {};
	settings.light = sme::LightLevel::Both;
	settings.detail = false;
	settings.water = false;
	CHECK(has(sme::toolArguments(settings, CompileTool::Vrad), "-both"));
	CHECK(sme::joinArguments(sme::toolArguments(settings, CompileTool::Vbsp)) == "-nodetail -nowater");

	// Steps switched off are left out; cubemaps need the copy and run once per light level.
	settings.vvis = false;
	settings.run = false;
	settings.cubemaps = true;
	settings.cubemapLevel = sme::LightLevel::Both;
	steps = sme::compileCommands(settings, "/maps/src/arena", "/game/mod");
	REQUIRE(steps.size() == 5);
	CHECK(steps[1].name == "VRAD");
	CHECK(steps[3].name == "Cubemaps (LDR)");
	CHECK(steps[4].name == "Cubemaps (HDR)");
	CHECK_FALSE(steps[3].detached);
	CHECK(has(steps[3].arguments, "-buildcubemaps"));
	CHECK(sme::joinArguments(steps[4].arguments).find("+mat_hdr_level 2 +map arena -buildcubemaps") != std::string::npos);
	settings.copy = false;
	steps = sme::compileCommands(settings, "/maps/src/arena", "/game/mod");
	CHECK(std::none_of(steps.begin(), steps.end(), [](const auto& s) { return s.tool == CompileTool::Cubemaps || s.tool == CompileTool::Copy; }));

	// Extra parameters, one with a value and one quoted.
	settings = {};
	settings.extraFor(CompileTool::Vrad) = {"-lights fidelity_lights.rad", "-StaticPropLighting"};
	settings.extraFor(CompileTool::Game) = {"+map_background \"two words\""};
	CHECK(sme::joinArguments(sme::toolArguments(settings, CompileTool::Vrad)) == "-lights fidelity_lights.rad -StaticPropLighting");
	CHECK(has(sme::toolArguments(settings, CompileTool::Game), "two words"));
	CHECK(sme::splitArguments("  -a \"b c\"  d ") == std::vector<std::string>{"-a", "b c", "d"});
	CHECK_FALSE(sme::knownParameters(CompileTool::Vrad).empty());
	CHECK(std::any_of(sme::knownParameters(CompileTool::Vbsp).begin(), sme::knownParameters(CompileTool::Vbsp).end(),
	                  [](const auto& p) { return p.name == "-onlyents"; }));

	// Saving and reading back; unknown or broken lines keep the defaults.
	settings.applyPreset(CompileSettings::Preset::Final);
	settings.cubemaps = true;
	settings.width = 2560;
	settings.height = 1440;
	const auto back = CompileSettings::parse(settings.serialize());
	CHECK(back.serialize() == settings.serialize());
	CHECK(back.preset() == CompileSettings::Preset::Final);
	const auto broken = CompileSettings::parse("width=abc\nnonsense\nunknown=1\nvrad+=\n");
	CHECK(broken.serialize() == CompileSettings{}.serialize());
}
