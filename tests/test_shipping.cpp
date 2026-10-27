// test_shipping.cpp - What a map needs, share and mod packages, ZIP files, Workshop items.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/editing.h>
#include <sme/game.h>
#include <sme/shipping.h>
#include <sme/text.h>

#include <algorithm>
#include <fstream>

using namespace sme;

namespace {

const ShipFile* findFile(const MapDependencies& deps, std::string_view path) {
	for (const auto& file : deps.files) {
		if (file.path == path) return &file;
	}
	return nullptr;
}

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

} // namespace

TEST_CASE("what a map needs, and where each file comes from") {
	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "sourcemods/mymod") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);

	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEMO/BRICKWALL001");
	doc.findSolid(ref)->sides[0].material = "concrete/wall01";
	doc.findSolid(ref)->sides[1].material = "nowhere/missing";
	doc.world.set("skyname", "sky_demo01");
	const auto place = [&](const char* classname, std::initializer_list<std::pair<const char*, const char*>> keys) {
		Entity entity;
		entity.id = doc.newEntityId();
		entity.set("classname", classname);
		for (const auto& [k, v] : keys) entity.set(k, v);
		doc.entities.push_back(entity);
	};
	place("prop_static", {{"model", "models/humans/group01/male_07.mdl"}});
	place("ambient_generic", {{"message", "Demo.Wind"}, {"targetname", "Demo.Alarm"}});   // a name is no sound
	place("env_soundscape", {{"soundscape", "demo.outside"}});
	place("info_particle_system", {{"effect_name", "demo_sparks"}});

	const auto deps = mapDependencies(doc, game->files, f.mod);
	const auto custom = [&](std::string_view path) {
		const auto* file = findFile(deps, path);
		INFO(path);
		REQUIRE(file);
		CHECK(file->origin == ShipFile::Origin::Custom);
		CHECK(file->size > 0);
		CHECK(fileOnDisk(game->files, *file));
		return file;
	};
	// Faces: the material, the textures its parameters name.
	CHECK(custom("materials/demo/brickwall001.vmt")->kind == ShipFile::Kind::Material);
	CHECK(custom("materials/demo/brickwall001.vtf")->kind == ShipFile::Kind::Texture);
	custom("materials/demo/brickwall001_normal.vtf");
	const auto* base = findFile(deps, "materials/concrete/wall01.vmt");
	REQUIRE(base);
	CHECK(base->origin == ShipFile::Origin::BaseGame);
	const auto* missing = findFile(deps, "materials/nowhere/missing.vmt");
	REQUIRE(missing);
	CHECK(missing->origin == ShipFile::Origin::Missing);
	CHECK(missing->neededBy == "face material");
	CHECK(std::any_of(deps.problems.begin(), deps.problems.end(), [](const std::string& p) { return p.find("nowhere/missing") != std::string::npos; }));
	// The sky: six faces with their textures.
	for (const char* face : {"rt", "lf", "bk", "ft", "up", "dn"}) {
		custom(std::string("materials/skybox/sky_demo01") + face + ".vmt");
		custom(std::string("materials/skybox/sky_demo01") + face + ".vtf");
	}
	// The model, its other files and its material.
	CHECK(custom("models/humans/group01/male_07.mdl")->kind == ShipFile::Kind::Model);
	custom("models/humans/group01/male_07.vvd");
	custom("models/humans/group01/male_07.dx90.vtx");
	custom("models/humans/male_shared.mdl");   // include model
	CHECK(custom("materials/models/humans/male_sheet.vmt")->neededBy.find("male_07") != std::string::npos);
	// Sounds through the sound script, the soundscape and its waves, the particle file.
	CHECK(custom("sound/ambient/wind1.wav")->kind == ShipFile::Kind::Sound);
	custom("sound/ambient/alarm1.wav");   // played by the soundscape
	CHECK(custom("scripts/soundscapes_demo.txt")->kind == ShipFile::Kind::Soundscape);
	CHECK(custom("particles/demo_sparks.pcf")->kind == ShipFile::Kind::Particles);
	CHECK(deps.count(ShipFile::Origin::Missing) >= 1);
	CHECK(deps.bytes(ShipFile::Origin::Custom) > 0);
	// Each path once, sorted.
	CHECK(std::is_sorted(deps.files.begin(), deps.files.end(), [](const ShipFile& a, const ShipFile& b) { return a.path < b.path; }));

	// Share package: the map, every custom file, a readme - as a ZIP and as a folder.
	const auto bsp = f.mod / "maps/demo.bsp";
	test::writeText(bsp, "VBSP fake map");
	const auto entries = sharePackage(bsp, deps, game->files, "Have fun!");
	const auto zip = f.root / "out/demo_share.zip";
	std::string error;
	std::size_t lastProgress = 0;
	REQUIRE_MESSAGE(writeZip(zip, entries, &error, [&](std::size_t done, std::size_t) { lastProgress = done; }), error);
	CHECK(lastProgress == entries.size());
	const auto names = zipEntryNames(zip);
	CHECK(names.size() == entries.size());
	for (const char* expected : {"maps/demo.bsp", "materials/demo/brickwall001.vmt", "models/humans/group01/male_07.mdl", "sound/ambient/wind1.wav",
	                             "readme.txt"}) {
		CHECK(std::find(names.begin(), names.end(), expected) != names.end());
	}
	CHECK(std::find(names.begin(), names.end(), "materials/concrete/wall01.vmt") == names.end());   // players have it
	REQUIRE(writeFolder(f.root / "out/folder", entries, &error));
	CHECK(std::filesystem::exists(f.root / "out/folder/maps/demo.bsp"));
	CHECK(readFileText(f.root / "out/folder/readme.txt") == "Have fun!");
	// Names that would leave the package are refused.
	CHECK_FALSE(writeZip(f.root / "out/bad.zip", {PackageEntry{"../evil.txt", {}, {}}}, &error));
	CHECK_FALSE(writeZip(f.root / "out/bad.zip", {PackageEntry{"a.txt", {}, {}}, PackageEntry{"A.txt", {}, {}}}, &error));

	// bspzip list: game path, then disk path.
	const auto list = bspzipFileList({{"materials/a.vmt", std::filesystem::path("/x/materials/a.vmt")}});
	CHECK(list.starts_with("materials/a.vmt\n"));
	CHECK(list.find("a.vmt\n", 16) != std::string::npos);
}

TEST_CASE("mod package without the mapper's files") {
	CHECK(developmentFileReason("mapsrc/demo.vmf") == "sources");
	CHECK(developmentFileReason("maps/demo.vmf") == "map sources");
	CHECK(developmentFileReason("maps/demo.prt") == "compile leftovers");
	CHECK(developmentFileReason("cfg/config.cfg") == "your own settings");
	CHECK(developmentFileReason("cfg/autoexec.cfg").empty());
	CHECK(developmentFileReason("maps/demo.bsp").empty());
	CHECK(developmentFileReason("console.log") == "log");
	CHECK(developmentFileReason("modelsounds.cache") == "cache");

	test::Fixture f;
	test::createFixture(f);
	test::writeText(f.mod / "maps/demo.bsp", "VBSP");
	test::writeText(f.mod / "maps/demo.log", "log");
	test::writeText(f.mod / "mapsrc/demo.vmf", "versioninfo{}");
	test::writeText(f.mod / "screenshots/a.jpg", "x");
	test::writeText(f.mod / "cfg/config.cfg", "bind w +forward");
	const auto plan = planModPackage(f.mod);
	const auto has = [&](const char* path) { return std::find(plan.files.begin(), plan.files.end(), std::filesystem::path(path)) != plan.files.end(); };
	CHECK(has("gameinfo.txt"));
	CHECK(has("maps/demo.bsp"));
	CHECK_FALSE(has("maps/demo.log"));
	CHECK_FALSE(has("cfg/config.cfg"));
	const auto excluded = [&](const char* path) {
		return std::any_of(plan.excluded.begin(), plan.excluded.end(), [&](const auto& e) { return e.first == std::filesystem::path(path); });
	};
	CHECK(excluded("mapsrc"));        // the whole folder at once
	CHECK(excluded("screenshots"));
	CHECK(excluded("maps/demo.log"));
	CHECK(plan.bytes > 0);
	const auto entries = modPackage(f.mod, plan);
	REQUIRE_FALSE(entries.empty());
	CHECK(std::all_of(entries.begin(), entries.end(), [](const PackageEntry& e) { return e.name.starts_with("mymod/"); }));
	std::string error;
	REQUIRE(writeZip(f.root / "mod.zip", entries, &error));
	const auto names = zipEntryNames(f.root / "mod.zip");
	CHECK(std::find(names.begin(), names.end(), "mymod/gameinfo.txt") != names.end());
}

TEST_CASE("Steam Workshop item for steamcmd") {
	WorkshopItem item;
	item.appId = 220;
	item.contentFolder = std::filesystem::path("C:/work/content");
	item.previewFile = std::filesystem::path("C:/work/preview.jpg");
	item.visibility = 2;
	item.title = "My \"great\" map";
	item.description = "Line one\nback\\slash";
	const auto vdf = workshopItemVdf(item);
	CHECK(vdf.starts_with("\"workshopitem\"\n{\n"));
	CHECK(vdf.find("\"appid\"\t\t\"220\"") != std::string::npos);
	CHECK(vdf.find("\"publishedfileid\"\t\t\"0\"") != std::string::npos);
	CHECK(vdf.find("\"contentfolder\"\t\t\"C:/work/content\"") != std::string::npos);
	CHECK(vdf.find("\"visibility\"\t\t\"2\"") != std::string::npos);
	CHECK(vdf.find("\"title\"\t\t\"My \\\"great\\\" map\"") != std::string::npos);
	CHECK(vdf.find("back\\\\slash") != std::string::npos);
	item.visibility = 9;
	CHECK(workshopItemVdf(item).find("\"visibility\"\t\t\"3\"") != std::string::npos);
}
