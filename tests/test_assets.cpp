// test_assets.cpp - Material categories, MDL reading, sound scripts, "did you mean", map checks and fixes.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/scene_export.h>
#include <sme/soundscape.h>
#include <sme/studio_model.h>
#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/compile_log.h>
#include <sme/game.h>
#include <sme/heightmap.h>
#include <sme/keyvalues.h>
#include <sme/material.h>
#include <sme/particles.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>
#include <fstream>

using namespace sme;

namespace {

std::vector<std::byte> makeModel(const std::vector<std::string>& sequences, const std::string& include) {
	return test::makeModelFile(sequences, include);
}

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

} // namespace

TEST_CASE("material categories") {
	Material blend;
	blend.name = "nature/blendgrassdirt";
	blend.shader = "WorldVertexTransition";
	CHECK(classifyMaterial(blend).blend);
	CHECK(classifyMaterial(blend).brush);

	Material model;
	model.name = "models/combine_soldier/combinesoldiersheet";
	model.shader = "VertexLitGeneric";
	CHECK(classifyMaterial(model).model);
	CHECK_FALSE(classifyMaterial(model).brush);

	Material glass;
	glass.name = "glass/glasswindow001a";
	glass.shader = "LightmappedGeneric";
	glass.params["$translucent"] = "1";
	CHECK(classifyMaterial(glass).translucent);

	Material nodraw;
	nodraw.name = "tools/toolsnodraw";
	nodraw.shader = "LightmappedGeneric";
	CHECK(classifyMaterial(nodraw).tool);

	Material sprite;   // editor icon (iconsprite), not a wall texture
	sprite.name = "editor/light_spot";
	sprite.shader = "UnlitGeneric";
	CHECK_FALSE(classifyMaterial(sprite).brush);
}

TEST_CASE("model header: sequences, activities, includes, materials") {
	const auto data = makeModel({"idle_unarmed", "walk_all"}, "models/humans/male_shared.mdl");
	std::string error;
	const auto info = readModelInfo(data, &error);
	REQUIRE_MESSAGE(info, error);
	CHECK(info->version == 48);
	CHECK(info->name == "test/model.mdl");
	CHECK(info->sequences == std::vector<std::string>{"idle_unarmed", "walk_all"});
	CHECK(info->activities[0] == "ACT_IDLE");
	CHECK(info->includeModels == std::vector<std::string>{"models/humans/male_shared.mdl"});
	CHECK(info->materials == std::vector<std::string>{"combine_soldier_sheet"});
	CHECK(info->materialDirs == std::vector<std::string>{"models/combine_soldier/"});
	CHECK(info->surfaceProp == "flesh");

	// Corrupt files never crash or read out of range.
	auto broken = data;
	std::int32_t huge = 0x7fffffff;
	std::memcpy(broken.data() + 192, &huge, 4);
	CHECK_FALSE(readModelInfo(broken));
	CHECK_FALSE(readModelInfo(std::vector<std::byte>(10)));
}

TEST_CASE("sequences are merged from include models; sound scripts") {
	test::Fixture f;
	test::createFixture(f);
	test::writeText(f.hl2 / "scripts/game_sounds_manifest.txt",
	                "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds.txt\"\n\t\"precache_file\" \"scripts/npc_sounds.txt\"\n}\n");
	test::writeText(f.hl2 / "scripts/game_sounds.txt", "\"Door.Open\"\n{\n\t\"wave\" \"doors/open.wav\"\n}\n\"Ambient.Wind\" { \"wave\" \"a.wav\" }\n");
	test::writeText(f.hl2 / "scripts/npc_sounds.txt", "\"NPC_Combine.Alert\" { \"wave\" \"b.wav\" }\n");
	const auto writeBytes = [](const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	};
	writeBytes(f.hl2 / "models/humans/group01/male_01.mdl", makeModel({"idle_subtle"}, "models/humans/male_shared.mdl"));
	writeBytes(f.hl2 / "models/humans/male_shared.mdl", makeModel({"walk_all", "idle_subtle"}, "models/humans/male_ss.mdl"));
	writeBytes(f.hl2 / "models/humans/male_ss.mdl", makeModel({"sitchair1", "lineidle01"}, "models/humans/male_shared.mdl"));

	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "220/hl2") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);
	const auto sequences = modelSequences(game->files, "models/humans/group01/male_01.mdl");
	CHECK(sequences == std::vector<std::string>{"idle_subtle", "walk_all", "sitchair1", "lineidle01"});
	CHECK(soundScriptNames(game->files) == std::vector<std::string>{"Ambient.Wind", "Door.Open", "NPC_Combine.Alert"});
}

TEST_CASE("sound scripts with their files and soundscapes") {
	test::Fixture f;
	test::createFixture(f);
	test::writeText(f.hl2 / "scripts/game_sounds_manifest.txt", "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/a.txt\"\n\t\"precache_file\" \"scripts/b.txt\"\n}\n");
	test::writeText(f.hl2 / "scripts/a.txt", "\"Door.Open\" { \"channel\" \"CHAN_ITEM\" \"rndwave\" { \"wave\" \")doors/open1.wav\" \"wave\" \"^doors/open2.wav\" } }\n"
	                                   "\"Old.Sound\" { \"wave\" \"old.wav\" }\n");
	test::writeText(f.hl2 / "scripts/b.txt", "\"old.sound\" { \"wave\" \"#new.wav\" }\n");
	test::writeText(f.hl2 / "scripts/soundscapes_manifest.txt", "soundscapes_manifest\n{\n\t\"file\" \"scripts/soundscapes_town.txt\"\n}\n");
	test::writeText(f.hl2 / "scripts/soundscapes_town.txt",
	          "\"town.inside\"\n{\n\t\"dsp\" \"1\"\n\t\"playlooping\" { \"volume\" \"0.5\" \"wave\" \"*ambient/hum.wav\" }\n"
	          "\t\"playrandom\" { \"rndwave\" { \"wave\" \"ambient/creak1.wav\" } }\n\t\"playsoundscape\" { \"name\" \"town.base\" }\n"
	          "\t\"playlooping\" { \"soundname\" \"Door.Open\" }\n}\n\"town.base\" { }\n");

	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "220/hl2") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);
	const auto scripts = soundScripts(game->files);
	REQUIRE(scripts.size() == 2);
	CHECK(scripts[0].name == "Door.Open");
	CHECK(scripts[0].channel == "CHAN_ITEM");
	CHECK(scripts[0].waves == std::vector<std::string>{"doors/open1.wav", "doors/open2.wav"});
	CHECK(scripts[1].waves == std::vector<std::string>{"new.wav"});       // the later script wins
	CHECK(scripts[1].script == "scripts/b.txt");

	const auto scapes = soundscapes(game->files);
	REQUIRE(scapes.size() == 2);
	CHECK(scapes[0].name == "town.inside");
	CHECK(scapes[0].dsp == 1);
	CHECK(scapes[0].sounds == std::vector<std::string>{"ambient/hum.wav", "ambient/creak1.wav", "Door.Open"});
	CHECK(scapes[0].includes == std::vector<std::string>{"town.base"});
	CHECK(stripSoundChars(")Weapons\\Pistol.wav") == "weapons/pistol.wav");
}

TEST_CASE("soundscape editing: read every rule, keep unknown keys, write back, manifest") {
	const std::string text =
		"// comment\n"
		"\"town.inside\"\n{\n\t\"dsp\" \"1\"\n\t\"fadetime\" \"2\"\n\t\"dsp_volume\" \"0.8\"\n"
		"\t\"playlooping\" { \"volume\" \"0.5\" \"pitch\" \"100\" \"wave\" \"*ambient/hum.wav\" \"attenuation\" \"1\" }\n"
		"\t\"playrandom\" { \"time\" \"5,10\" \"volume\" \"0.3,0.6\" \"soundlevel\" \"SNDLVL_70dB\" \"position\" \"0\"\n"
		"\t\t\"rndwave\" { \"wave\" \"ambient/creak1.wav\" \"wave\" \"ambient/creak2.wav\" } }\n"
		"\t\"playsoundscape\" { \"name\" \"town.base\" \"volume\" \"0.7\" \"positionoverride\" \"1\" }\n"
		"\t\"playmusic\" { \"wave\" \"music/x.mp3\" }\n}\n"
		"\"town.base\" { \"dsp\" \"0\" }\n";
	auto scapes = parseSoundscapes(text);
	REQUIRE(scapes.size() == 2);
	const auto& inside = scapes[0];
	CHECK(inside.name == "town.inside");
	CHECK(inside.dsp == "1");
	CHECK(inside.fadetime == "2");
	REQUIRE(inside.extra.size() == 1);
	CHECK(inside.extra[0].key == "dsp_volume");
	REQUIRE(inside.rules.size() == 4);
	using Kind = SoundscapeRule::Kind;
	CHECK(inside.rules[0].kind == Kind::Looping);
	CHECK(inside.rules[0].wave == "*ambient/hum.wav");
	CHECK(inside.rules[0].volume == "0.5");
	REQUIRE(inside.rules[0].extra.size() == 1);   // "attenuation" stays
	CHECK(inside.rules[1].kind == Kind::Random);
	CHECK(inside.rules[1].time == "5,10");
	CHECK(inside.rules[1].soundlevel == "SNDLVL_70dB");
	CHECK(inside.rules[1].position == "0");
	CHECK(inside.rules[1].waves == std::vector<std::string>{"ambient/creak1.wav", "ambient/creak2.wav"});
	CHECK(inside.rules[2].kind == Kind::Soundscape);
	CHECK(inside.rules[2].name == "town.base");
	CHECK(inside.rules[2].positionOverride == "1");
	CHECK(inside.rules[3].kind == Kind::Other);
	CHECK(inside.rules[3].key == "playmusic");

	// Written and read again: the same.
	const auto again = parseSoundscapes(writeSoundscape(inside));
	REQUIRE(again.size() == 1);
	CHECK(again[0].rules.size() == 4);
	CHECK(again[0].rules[1].waves == inside.rules[1].waves);
	CHECK(again[0].rules[3].key == "playmusic");
	REQUIRE(again[0].rules[3].extra.size() == 1);
	CHECK(again[0].rules[3].extra[0].value == "music/x.mp3");
	CHECK(again[0].extra.size() == 1);

	// Edit: a rule more, one less; replaced in place, the other soundscape stays.
	auto edited = inside;
	edited.rules.erase(edited.rules.begin() + 2);
	SoundscapeRule birds;
	birds.kind = Kind::Random;
	birds.time = "3,8";
	birds.waves = {"ambient/birds1.wav"};
	edited.rules.push_back(birds);
	edited.dsp = "17";
	const auto file = upsertSoundscape(text, edited);
	auto both = parseSoundscapes(file);
	REQUIRE(both.size() == 2);
	CHECK(both[0].name == "town.inside");
	CHECK(both[0].dsp == "17");
	CHECK(both[0].rules.size() == 4);
	CHECK(both[0].rules.back().waves == std::vector<std::string>{"ambient/birds1.wav"});
	CHECK(both[1].name == "town.base");
	// A new one goes to the end; removing takes only that one.
	SoundscapeScript fresh;
	fresh.name = "mymod.cave";
	const auto three = upsertSoundscape(file, fresh);
	CHECK(parseSoundscapes(three).size() == 3);
	CHECK(parseSoundscapes(three).back().name == "mymod.cave");
	const auto removed = parseSoundscapes(removeSoundscape(three, "TOWN.INSIDE"));
	REQUIRE(removed.size() == 2);
	CHECK(removed[0].name == "town.base");
	CHECK(upsertSoundscape("", fresh).find("\"mymod.cave\"") != std::string::npos);

	// Manifest: the mod's file first (its soundscapes win), only once.
	const std::string manifest = "soundscapes_manifest\n{\n\t\"file\" \"scripts/soundscapes_town.txt\"\n}\n";
	const auto withMod = addToSoundscapeManifest(manifest, "scripts/soundscapes_mymod.txt");
	const auto root = parseKeyValues(withMod);
	REQUIRE(root.children.size() == 1);
	REQUIRE(root.children[0].children.size() == 2);
	CHECK(root.children[0].children[0].value == "scripts/soundscapes_mymod.txt");
	CHECK(addToSoundscapeManifest(withMod, "scripts/soundscapes_mymod.txt") == withMod);
	CHECK(parseKeyValues(addToSoundscapeManifest("", "scripts/x.txt")).children[0].key == "soundscapes_manifest");
	CHECK(dspPresets().size() >= 29);
	CHECK(std::string(dspPresets()[17].name) == "Concrete, small");
}

TEST_CASE("model triangles from .mdl, .vvd and .vtx") {
	const auto files = test::makeStudioModel({{-24, -24, 0, 24, 24, 48}, {-8, -8, 48, 8, 8, 64}}, {"idle"}, "", "crate", "models/props/");
	std::string error;
	const auto model = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error);
	REQUIRE_MESSAGE(model, error);
	CHECK(model->vertices.size() == 48);
	CHECK(model->triangleCount() == 24);
	REQUIRE(model->meshes.size() == 1);
	CHECK(model->meshes[0].material == "models/props/crate");
	CHECK(model->meshes[0].indexCount == 72);
	CHECK(model->bounds.min.x == doctest::Approx(-24));
	CHECK(model->bounds.max.z == doctest::Approx(64));
	// The triangles keep the winding of the file (the viewport draws models without culling).
	for (std::size_t t = 0; t < model->indices.size(); t += 3) {
		const auto p = [&](int k) {
			const auto& v = model->vertices[model->indices[t + static_cast<std::size_t>(k)]];
			return Vec3{v.position[0], v.position[1], v.position[2]};
		};
		const auto& n = model->vertices[model->indices[t]].normal;
		const Vec3 normal = cross(p(1) - p(0), p(2) - p(0));
		CHECK(dot(normal, Vec3{n[0], n[1], n[2]}) > 0);
	}
	// The $cdmaterials folder that really has the material wins.
	const auto other = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, nullptr, [](const std::string& m) { return m == "models/props/crate"; });
	REQUIRE(other);
	CHECK(other->meshes[0].material == "models/props/crate");

	// Broken or mismatched files give an error, never a crash.
	CHECK_FALSE(buildModelGeometry({files.mdl, {}, files.vtx}, 0, &error));
	CHECK_FALSE(buildModelGeometry({files.mdl, files.vvd, {}}, 0, &error));
	for (std::size_t offset = 0; offset < files.vtx.size(); offset += 7) {
		auto broken = files.vtx;
		broken[offset] = std::byte{0xff};
		(void)buildModelGeometry({files.mdl, files.vvd, broken}, 0);
	}
	for (std::size_t offset = 0; offset < files.mdl.size(); offset += 5) {
		auto broken = files.mdl;
		broken[offset] = std::byte{0x7f};
		(void)buildModelGeometry({broken, files.vvd, files.vtx}, 0);
	}
	// Animation-only models (included by others) have no triangles, but load fine.
	const auto animations = test::makeModelFile({"walk_all"}, "");
	const auto empty = buildModelGeometry({animations, {}, {}}, 0, &error);
	REQUIRE(empty);
	CHECK(empty->empty());
	auto truncated = files.vvd;
	truncated.resize(200);
	CHECK_FALSE(buildModelGeometry({files.mdl, truncated, files.vtx}, 0, &error));
}

TEST_CASE("model body groups, skins and prop settings") {
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"box", {{-16, -16, 0, 16, 16, 32}}}}});
	spec.groups.push_back({"lid", {{"lid_closed", {{-16, -16, 32, 16, 16, 36}}}, {"blank", {}}}});
	spec.groups.push_back({"handle", {{"left", {{-20, -2, 10, -16, 2, 20}}}, {"right", {{16, -2, 10, 20, 2, 20}}}, {"both", {{-20, -2, 10, -16, 2, 20}, {16, -2, 10, 20, 2, 20}}}}});
	spec.textures = {"wood", "metal"};
	spec.staticProp = true;
	spec.mass = 35;
	spec.keyValues = "\"prop_data\" { \"base\" \"Wooden.Medium\" }";
	const auto files = test::makeStudioModel(spec);

	const auto info = readModelInfo(files.mdl);
	REQUIRE(info);
	CHECK(info->skinCount == 2);
	CHECK(info->staticProp);
	CHECK(info->mass == doctest::Approx(35));
	CHECK(info->keyValues.find("Wooden.Medium") != std::string::npos);
	REQUIRE(info->bodyGroups.size() == 3);
	CHECK(info->bodyGroups[1].name == "lid");
	CHECK(info->bodyGroups[1].options == std::vector<std::string>{"lid_closed", "blank"});
	CHECK(info->bodyGroups[2].options.size() == 3);
	// body = sum of choice * (product of the option counts before it): lid 1 -> 1, handle 2 -> 2*2.
	CHECK(info->bodyValue({}) == 0);
	CHECK(info->bodyValue({0, 1, 0}) == 1);
	CHECK(info->bodyValue({0, 1, 2}) == 5);
	CHECK(info->bodyValue({0, 9, 9}) == 5);   // out of range: clamped

	std::string error;
	const auto defaults = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error);
	REQUIRE_MESSAGE(defaults, error);
	CHECK(defaults->triangleCount() == 36);   // box, closed lid, left handle
	CHECK(defaults->meshes[0].material == "models/props/wood");
	const auto open = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error, {}, info->bodyValue({0, 1, 2}));
	REQUIRE_MESSAGE(open, error);
	CHECK(open->triangleCount() == 36);       // box, no lid, both handles
	CHECK(open->bounds.max.x == doctest::Approx(20));
	CHECK(open->bounds.min.x == doctest::Approx(-20));
	const auto metal = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 1, &error);
	REQUIRE(metal);
	CHECK(metal->meshes[0].material == "models/props/metal");
	// Every body number gives a model (or a clean error), never a crash.
	for (int body = -3; body < 40; ++body) {
		(void)buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, nullptr, {}, body);
	}
	// Plain models: one body group with one option, one skin, no prop settings.
	const auto plain = readModelInfo(test::makeStudioModel({{-8, -8, 0, 8, 8, 8}}, {"idle"}, "", "crate", "models/props/").mdl);
	REQUIRE(plain);
	CHECK(plain->skinCount == 1);
	CHECK_FALSE(plain->staticProp);
	CHECK(plain->bodyGroups.size() == 1);
	CHECK(plain->keyValues.empty());
}

TEST_CASE("compile output: errors, warnings, explanations, progress") {
	const auto leak = classifyCompileLine("**** leaked ****");
	REQUIRE(leak);
	CHECK(leak->severity == CompileMessage::Severity::Error);
	CHECK(leak->leak);
	CHECK_FALSE(leak->explanation.empty());
	const auto model = classifyCompileLine("Warning: Error loading studio model \"models/props/missing.mdl\"!");
	REQUIRE(model);
	CHECK(model->severity == CompileMessage::Severity::Warning);
	CHECK(model->explanation.find("model") != std::string::npos);
	CHECK(classifyCompileLine("Brush 12: WARNING, portal clipped away (ptr 0x0)")->severity == CompileMessage::Severity::Warning);
	CHECK(classifyCompileLine("Error: MAX_MAP_BRUSHES")->severity == CompileMessage::Severity::Error);
	// Harmless notes that sound alarming (VRAD without lights.rad, gameinfo without GameData).
	CHECK(classifyCompileLine("Warning: Couldn't open texlight file D:\\Games\\bin\\lights.rad.")->severity == CompileMessage::Severity::Info);
	CHECK(classifyCompileLine("Could not locate 'GameData' key in d:\\games\\hl2\\gameinfo.txt")->severity == CompileMessage::Severity::Info);
	CHECK(classifyCompileLine("Couldn't open map.prt")->severity == CompileMessage::Severity::Error);
	CHECK(classifyCompileLine("some unexpected error happened")->severity == CompileMessage::Severity::Error);
	CHECK(classifyCompileLine("some unexpected error happened")->explanation.empty());
	CHECK_FALSE(classifyCompileLine("0 errors, 0 warnings"));
	CHECK_FALSE(classifyCompileLine("ProcessBlock_Thread: 0...1...2...3"));
	CHECK_FALSE(classifyCompileLine("terrorist spawn"));    // "error" only as a word

	CHECK(compileProgress("BuildFacelights: 0...1...2...3...4") == 40);
	CHECK(compileProgress("...7...8...9...10") == 100);
	CHECK(compileProgress("5...6") == 60);
	CHECK_FALSE(compileProgress("Valve Software - vrad.exe (Jul 2023)"));
	CHECK(compilePhase("PortalFlow: 0...1...2") == "PortalFlow");
	CHECK(compilePhase("Loading c:/maps/demo.bsp").empty());
}

TEST_CASE("particle systems from .pcf files") {
	const auto pcf = test::makePcf({"fire_small", "smoke"});
	CHECK(particleSystemNames(pcf) == std::vector<std::string>{"fire_small", "smoke"});
	auto broken = pcf;
	broken.resize(broken.size() / 2);
	(void)particleSystemNames(broken);   // never crashes
	CHECK(particleSystemNames(std::vector<std::byte>(16, std::byte{0x41})).empty());

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
	const auto systems = particleSystems(game->files);
	REQUIRE(systems.size() == 3);
	CHECK(systems[0].name == "demo_fire_small");
	CHECK(systems[0].file == "particles/demo_fire.pcf");   // "!" of the manifest removed
	CHECK(systems[2].name == "demo_sparks");
	// Skies for the worldspawn "skyname": by their up face, only with the front face too.
	CHECK(skyboxNames(game->files) == std::vector<std::string>{"sky_demo01"});
}

TEST_CASE("did you mean") {
	const std::vector<std::string> names{"concrete/concretewall001a", "concrete/concretefloor002a", "tools/toolsnodraw"};
	CHECK(closestMatch("concrete/concretwall001a", names) == "concrete/concretewall001a");
	CHECK(closestMatch("tools/toolnodraw", names) == "tools/toolsnodraw");
	CHECK(closestMatch("metal/something_else", names).empty());
	CHECK(closestMatch("brick/brickwal001", {"demo/brickwall001", "brick/brickfloor001"}) == "demo/brickwall001");
	CHECK(editDistance("Kitten", "sitting") == 3);
}

TEST_CASE("map checks find problems and autofix them") {
	test::Fixture f;
	test::createFixture(f);
	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "220/hl2") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);
	std::vector<std::string> materialNames;
	for (const auto& path : game->files.list("materials", ".vmt")) {
		materialNames.push_back(normalizeMaterialName(path));
	}

	MapDocument doc;
	auto [c1, floor] = edit::createBlock(doc, box({-256, -256, -16}, {256, 256, 0}), "CONCRETE/WALL01");
	auto [c2, typo] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "CONCRETE/WAL01");
	doc.findSolid(typo)->sides[0].u.scale = 0;
	auto [c3, lightId] = edit::placeEntity(doc, "lihgt", {0, 0, 128}, nullptr);
	auto [c4, triggerId] = edit::placeEntity(doc, "trigger_once", {0, 0, 0}, nullptr);
	auto [c5, relayId] = edit::placeEntity(doc, "light", {64, 0, 128}, nullptr);
	auto* relay = doc.findEntity(relayId);
	relay->set("targetname", "lamp");
	Connection good{"OnUser1", "lamp", "TurnOff", "", 0, -1, ','};
	Connection missing{"OnUser1", "nobody", "TurnOn", "", 0, -1, ','};
	Connection badInput{"OnUser2", "lamp", "TurnOf", "", 0, -1, ','};
	relay->connections = {good, missing, badInput};

	const CheckContext context{&game->entities, &game->files, &materialNames};
	auto problems = checkMap(doc, context);
	const auto find = [&](Problem::Kind kind) -> const Problem* {
		for (const auto& p : problems) {
			if (p.kind == kind) return &p;
		}
		return nullptr;
	};
	REQUIRE(find(Problem::Kind::MissingMaterial));
	CHECK(find(Problem::Kind::MissingMaterial)->to == "CONCRETE/WALL01");
	CHECK(find(Problem::Kind::MissingMaterial)->objects.faces.size() == 6);
	REQUIRE(find(Problem::Kind::UnknownClass));
	CHECK(find(Problem::Kind::UnknownClass)->to == "light");
	CHECK(find(Problem::Kind::BrushEntityWithoutBrushes));
	REQUIRE(find(Problem::Kind::OutputTargetMissing));
	REQUIRE(find(Problem::Kind::UnknownInput));
	CHECK(find(Problem::Kind::UnknownInput)->to == "TurnOff");
	CHECK(find(Problem::Kind::BadTextureScale));
	CHECK(find(Problem::Kind::NoPlayerStart));
	CHECK_FALSE(find(Problem::Kind::NoLights));
	CHECK_FALSE(find(Problem::Kind::InvalidBrush));

	// Like the Problems panel: fix one problem, check again (fixes can change other problems).
	History history;
	for (int round = 0; round < 20; ++round) {
		const auto fixable = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return !p.fixLabel.empty(); });
		if (fixable == problems.end()) {
			break;
		}
		history.push(applyFix(doc, *fixable, &game->entities));
		problems = checkMap(doc, context);
	}
	for (const auto& p : problems) {
		CHECK_MESSAGE(p.fixLabel.empty(), p.message);
	}
	CHECK(doc.findSolid(typo)->sides[1].material == "CONCRETE/WALL01");
	CHECK(doc.findEntity(lightId)->classname() == "light");
	CHECK_FALSE(doc.findEntity(triggerId));
	CHECK(doc.findEntity(relayId)->connections.size() == 2);
	CHECK(doc.findEntity(relayId)->connections[1].input == "TurnOff");
	const bool hasStart = std::any_of(doc.entities.begin(), doc.entities.end(), [](const Entity& e) { return e.classname() == "info_player_start"; });
	CHECK(hasStart);
	(void)c1; (void)c2; (void)c3; (void)c4; (void)c5;
}

TEST_CASE("leak pointfile names the leaking entity") {
	MapDocument doc;
	auto [c, id] = edit::placeEntity(doc, "info_player_start", {100, 0, 0}, nullptr);
	const auto line = readPointfile("100 0 0\n200.5 0 0\n\n900 0 0\n");
	REQUIRE(line.size() == 3);
	const auto problem = leakProblem(doc, line, nullptr);
	CHECK(problem.entityId == id);
	CHECK(problem.message.find("900 0 0") != std::string::npos);
	CHECK(problem.line.size() == 3);
	(void)c;
}

TEST_CASE("map export to glTF and OBJ") {
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
	const auto text = readFileText(f.mod / "mapsrc/demo.vmf");
	REQUIRE(text);
	const auto doc = MapDocument::parse(*text);
	std::string error;
	ExportOptions options;
	const auto gltf = exportMap(doc, &game->entities, &game->files, f.root / "export/demo.gltf", options, &error);
	REQUIRE_MESSAGE(gltf, error);
	CHECK(gltf->triangles > 100);
	CHECK(gltf->models >= 3);            // crates, barrels, the citizen
	CHECK(gltf->textures >= 5);
	const auto json = readFileText(f.root / "export/demo.gltf");
	REQUIRE(json);
	CHECK(json->find("\"version\":\"2.0\"") != std::string::npos);
	CHECK(json->find("demo_textures/demo_brickwall001.png") != std::string::npos);
	CHECK(std::filesystem::exists(f.root / "export/demo_textures/demo_brickwall001.png"));
	CHECK(json->find("tools_toolsskybox") == std::string::npos);   // tool textures left out

	options.format = ExportFormat::Obj;
	const auto obj = exportMap(doc, &game->entities, &game->files, f.root / "export/demo.obj", options, &error);
	REQUIRE(obj);
	const auto objText = readFileText(f.root / "export/demo.obj");
	const auto mtlText = readFileText(f.root / "export/demo.mtl");
	REQUIRE(objText);
	REQUIRE(mtlText);
	CHECK(objText->find("mtllib demo.mtl") != std::string::npos);
	CHECK(std::count(objText->begin(), objText->end(), 'f') > 100);
	CHECK(mtlText->find("map_Kd demo_textures/") != std::string::npos);
	// Without the game's files: only the brushes, no textures.
	const auto bare = exportMap(doc, nullptr, nullptr, f.root / "export/bare.gltf", ExportOptions{}, &error);
	REQUIRE(bare);
	CHECK(bare->textures == 0);
	CHECK(bare->models == 0);
}

namespace {

void put16(std::vector<std::byte>& out, int v) {
	out.push_back(std::byte(v & 0xff));
	out.push_back(std::byte((v >> 8) & 0xff));
}
void put32(std::vector<std::byte>& out, std::uint32_t v) {
	put16(out, int(v & 0xffff));
	put16(out, int(v >> 16));
}
void putTag(std::vector<std::byte>& out, const char* tag) {
	for (int i = 0; i < 4; ++i) out.push_back(std::byte(tag[i]));
}

/// RIFF WAVE file from a "fmt " body, the data and an optional cue point.
std::vector<std::byte> wavFile(const std::vector<std::byte>& fmt, const std::vector<std::byte>& data, std::optional<std::uint32_t> cue = {}) {
	std::vector<std::byte> out;
	putTag(out, "RIFF");
	put32(out, 0);
	putTag(out, "WAVE");
	putTag(out, "fmt ");
	put32(out, std::uint32_t(fmt.size()));
	out.insert(out.end(), fmt.begin(), fmt.end());
	if (cue) {
		putTag(out, "cue ");
		put32(out, 28);
		put32(out, 1);
		for (std::uint32_t v : {0u, 0u, 0x61746164u, 0u, 0u, *cue}) put32(out, v);
	}
	putTag(out, "data");
	put32(out, std::uint32_t(data.size()));
	out.insert(out.end(), data.begin(), data.end());
	const auto size = std::uint32_t(out.size() - 8);
	for (int i = 0; i < 4; ++i) out[4 + i] = std::byte((size >> (8 * i)) & 0xff);
	return out;
}

std::vector<std::byte> fmtChunk(int tag, int channels, int rate, int blockAlign, int bits, const std::vector<std::byte>& extra = {}) {
	std::vector<std::byte> f;
	put16(f, tag);
	put16(f, channels);
	put32(f, std::uint32_t(rate));
	put32(f, std::uint32_t(rate * blockAlign));
	put16(f, blockAlign);
	put16(f, bits);
	if (!extra.empty()) {
		put16(f, int(extra.size()));
		f.insert(f.end(), extra.begin(), extra.end());
	}
	return f;
}

std::vector<int> sine(int count, double frequency, int rate) {
	std::vector<int> out;
	for (int i = 0; i < count; ++i) out.push_back(int(std::sin(i * frequency * 2 * 3.14159265 / rate) * 12000));
	return out;
}

} // namespace

TEST_CASE("sound decoding: WAV PCM, IMA ADPCM, MS ADPCM, loops, waveform") {
	constexpr int RATE = 22050;
	const auto wave = sine(1000, 440, RATE);
	{
		std::vector<std::byte> data;
		for (const int v : wave) put16(data, v);
		const auto file = wavFile(fmtChunk(1, 1, RATE, 2, 16), data, 250u);
		std::string error;
		const auto audio = decodeAudio(file, "loop.wav", &error);
		REQUIRE_MESSAGE(audio, error);
		CHECK(audio->channels == 1);
		CHECK(audio->sampleRate == RATE);
		REQUIRE(audio->frames() == 1000);
		CHECK(audio->samples[123] == wave[123]);
		CHECK(audio->format == "PCM 16 bit");
		REQUIRE(audio->loopStart);
		CHECK(*audio->loopStart == 250);
		CHECK(audio->seconds() == doctest::Approx(1000.0 / RATE));
		const auto peaks = waveformPeaks(*audio, 10);
		REQUIRE(peaks.size() == 10);
		CHECK(peaks[3].second == doctest::Approx(12000 / 32768.0).epsilon(0.02));
		CHECK(peaks[3].first == doctest::Approx(-12000 / 32768.0).epsilon(0.02));
	}
	{
		// 8-bit unsigned stereo.
		std::vector<std::byte> data;
		for (int i = 0; i < 100; ++i) {
			data.push_back(std::byte(128 + wave[std::size_t(i)] / 256));
			data.push_back(std::byte(128));
		}
		const auto audio = decodeAudio(wavFile(fmtChunk(1, 2, RATE, 2, 8), data));
		REQUIRE(audio);
		CHECK(audio->channels == 2);
		CHECK(audio->frames() == 100);
		CHECK(std::abs(audio->samples[40] - wave[20]) < 300);
		CHECK(audio->samples[41] == 0);
	}
	{
		// IMA ADPCM, encoded here with the standard algorithm; decoding must follow the wave closely.
		constexpr int STEPS[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
		                           130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
		                           1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
		                           7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
		constexpr int INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
		constexpr int BLOCK = 256, PER_BLOCK = (BLOCK - 4) * 2 + 1;   // 505 samples
		std::vector<std::byte> data;
		std::vector<int> expected;   // what a decoder must give back exactly
		int predictor = 0, index = 0;
		for (int start = 0; start + PER_BLOCK <= int(wave.size()); start += PER_BLOCK) {
			predictor = wave[std::size_t(start)];
			expected.push_back(predictor);
			put16(data, predictor);
			data.push_back(std::byte(index));
			data.push_back(std::byte(0));
			std::vector<int> nibbles;
			for (int i = 1; i < PER_BLOCK; ++i) {
				const int step = STEPS[index];
				int diff = wave[std::size_t(start + i)] - predictor, code = 0;
				if (diff < 0) { code = 8; diff = -diff; }
				int delta = step >> 3;
				if (diff >= step) { code |= 4; diff -= step; delta += step; }
				if (diff >= step >> 1) { code |= 2; diff -= step >> 1; delta += step >> 1; }
				if (diff >= step >> 2) { code |= 1; delta += step >> 2; }
				predictor = std::clamp(predictor + ((code & 8) ? -delta : delta), -32768, 32767);
				index = std::clamp(index + INDEX[code], 0, 88);
				nibbles.push_back(code);
				expected.push_back(predictor);
			}
			for (std::size_t i = 0; i + 1 < nibbles.size(); i += 2) data.push_back(std::byte(nibbles[i] | (nibbles[i + 1] << 4)));
		}
		std::vector<std::byte> extra;
		put16(extra, PER_BLOCK);
		const auto audio = decodeAudio(wavFile(fmtChunk(0x11, 1, RATE, BLOCK, 4, extra), data));
		REQUIRE(audio);
		CHECK(audio->format == "IMA ADPCM");
		REQUIRE(audio->frames() == PER_BLOCK);
		int same = 0;
		for (std::size_t i = 0; i < audio->frames(); ++i) same += audio->samples[i] == expected[i] ? 1 : 0;
		CHECK(same == PER_BLOCK);
	}
	{
		// MS ADPCM with the first predictor (coefficients 256, 0: "same as the last sample").
		constexpr int ADAPT[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
		constexpr int BLOCK = 128, PER_BLOCK = (BLOCK - 7) * 2 + 2;   // 244 samples
		std::vector<std::byte> data;
		std::vector<int> expected;
		int delta = 64;
		for (int start = 0; start + PER_BLOCK <= int(wave.size()); start += PER_BLOCK) {
			int s1 = wave[std::size_t(start + 1)], s2 = wave[std::size_t(start)];
			expected.push_back(s2);
			expected.push_back(s1);
			data.push_back(std::byte(0));
			put16(data, delta);
			put16(data, s1);
			put16(data, s2);
			std::vector<int> nibbles;
			for (int i = 2; i < PER_BLOCK; ++i) {
				const int predicted = s1;
				const int code = std::clamp(int(std::lround(double(wave[std::size_t(start + i)] - predicted) / delta)), -8, 7);
				const int sample = std::clamp(predicted + code * delta, -32768, 32767);
				s2 = s1;
				s1 = sample;
				const int n = code & 15;
				delta = std::max(16, ADAPT[n] * delta / 256);
				nibbles.push_back(n);
				expected.push_back(sample);
			}
			for (std::size_t i = 0; i + 1 < nibbles.size(); i += 2) data.push_back(std::byte((nibbles[i] << 4) | nibbles[i + 1]));
		}
		std::vector<std::byte> extra;
		put16(extra, PER_BLOCK);
		put16(extra, 7);
		for (const auto& [a, b] : std::vector<std::pair<int, int>>{{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}}) {
			put16(extra, a);
			put16(extra, b);
		}
		const auto audio = decodeAudio(wavFile(fmtChunk(2, 1, RATE, BLOCK, 4, extra), data));
		REQUIRE(audio);
		CHECK(audio->format == "MS ADPCM");
		REQUIRE(audio->frames() == std::size_t(PER_BLOCK) * 4);
		int same = 0;
		double worst = 0;
		for (std::size_t i = 0; i < audio->frames(); ++i) {
			same += audio->samples[i] == expected[i] ? 1 : 0;
			worst = std::max(worst, std::abs(double(audio->samples[i] - wave[i])));
		}
		CHECK(same == PER_BLOCK * 4);
		CHECK(worst < 4000);   // and it still sounds like the wave
	}
	// Broken or foreign data never crashes.
	std::string error;
	CHECK_FALSE(decodeAudio(std::vector<std::byte>(64, std::byte(0x7f)), "x.mp3", &error));
	CHECK_FALSE(decodeAudio(std::vector<std::byte>{std::byte('O'), std::byte('g'), std::byte('g'), std::byte('S'), std::byte(0)}, "x.ogg", &error));
	auto truncated = wavFile(fmtChunk(1, 1, RATE, 2, 16), std::vector<std::byte>(10, std::byte(1)));
	truncated.resize(30);
	CHECK_FALSE(decodeAudio(truncated, "x.wav", &error));
}

TEST_CASE("sound script entries: write, replace, manifest") {
	SoundScriptEntry entry;
	entry.name = "MyMod.Door";
	entry.channel = "CHAN_STATIC";
	entry.volume = "0.8";
	entry.soundlevel = "SNDLVL_75dB";
	entry.waves = {"doors/door1.wav", "doors/door2.wav"};
	const std::string original = "// comment\n\"MyMod.Other\"\n{\n\t\"channel\" \"CHAN_AUTO\"\n\t\"wave\" \"other.wav\"\n}\n"
	                             "\"MyMod.Door\"\n{\n\t\"wave\" \"old.wav\"\n}\n";
	const auto text = upsertSoundScriptEntry(original, entry);
	const auto root = parseKeyValues(text);
	REQUIRE(root.children.size() == 2);
	CHECK(root.children[0].key == "MyMod.Other");
	const auto& door = root.children[1];
	CHECK(door.key == "MyMod.Door");
	CHECK(door.get("channel") == "CHAN_STATIC");
	CHECK(door.get("volume") == "0.8");
	CHECK(door.get("pitch") == "PITCH_NORM");
	REQUIRE(door.find("rndwave"));
	CHECK(door.find("rndwave")->children.size() == 2);
	CHECK(text.find("old.wav") == std::string::npos);

	const std::string manifest = "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds.txt\"\n}\n";
	const auto added = addToSoundManifest(manifest, "scripts/game_sounds_mymod.txt");
	const auto files = parseKeyValues(added).children.front().children;
	REQUIRE(files.size() == 2);
	CHECK(files[1].value == "scripts/game_sounds_mymod.txt");
	CHECK(addToSoundManifest(added, "scripts/game_sounds_mymod.txt") == added);
	CHECK(parseKeyValues(addToSoundManifest("", "scripts/a.txt")).children.front().key == "game_sounds_manifest");
}

TEST_CASE("material editing keeps the rest of the VMT") {
	const std::string vmt = "// a comment\n\"LightmappedGeneric\"\n{\n\t\"$BaseTexture\" \"brick/wall\"\n\t\"$surfaceprop\" \"brick\"\n"
	                        "\t\"$envmap\" \"env_cubemap\" [$WIN32]\n"
	                        "\t\"Proxies\"\n\t{\n\t\t\"AnimatedTexture\"\n\t\t{\n\t\t\t\"animatedtexturevar\" \"$basetexture\"\n\t\t}\n\t}\n}\n";
	const auto edited = editMaterialText(vmt, "VertexLitGeneric",
	                                     {{"$basetexture", "brick/wall2"}, {"$bumpmap", "brick/wall_normal"}, {"$surfaceprop", std::nullopt}});
	const auto material = parseMaterial(edited, "brick/wall");
	REQUIRE(material);
	CHECK(material->shader == "VertexLitGeneric");
	CHECK(material->param("$basetexture") == "brick/wall2");
	CHECK(material->param("$bumpmap") == "brick/wall_normal");
	CHECK(material->param("$surfaceprop").empty());
	CHECK(edited.find("AnimatedTexture") != std::string::npos);
	CHECK(edited.find("$BaseTexture") != std::string::npos);   // the spelling of the key stays
	CHECK(edited.find("\"env_cubemap\" [$WIN32]") != std::string::npos);

	// Proxies: the liquid shader's proxy is added next to the others and taken away again.
	const auto liquid = setMaterialProxy(edited, "FidelityLiquid", true);
	const auto proxies = parseKeyValues(liquid).children.back().find("Proxies");
	REQUIRE(proxies);
	CHECK(proxies->children.size() == 2);
	CHECK(proxies->find("FidelityLiquid"));
	CHECK(setMaterialProxy(liquid, "fidelityliquid", true) == liquid);   // never twice
	const auto plain = setMaterialProxy(liquid, "FidelityLiquid", false);
	CHECK(plain.find("FidelityLiquid") == std::string::npos);
	CHECK(plain.find("AnimatedTexture") != std::string::npos);
	const auto alone = setMaterialProxy("\"Fidelity_Liquid\"\n{\n}\n", "FidelityLiquid", true);
	CHECK(parseKeyValues(alone).children.front().find("Proxies")->find("FidelityLiquid"));
	CHECK(setMaterialProxy(alone, "FidelityLiquid", false).find("Proxies") == std::string::npos);   // the empty block goes too

	// Patch materials: keys of the included material go to "replace", new ones to "insert".
	Material base;
	base.params["$basetexture"] = "brick/wall";
	const std::string patch = "patch\n{\n\tinclude \"materials/brick/wall.vmt\"\n\tinsert\n\t{\n\t\t\"$envmap\" \"env_cubemap\"\n\t}\n}\n";
	const auto patched = parseKeyValues(editMaterialText(patch, "", {{"$basetexture", "brick/other"}, {"$detail", "detail/noise"}}, &base));
	const auto& block = patched.children.front();
	CHECK(block.key == "patch");
	REQUIRE(block.find("replace"));
	CHECK(block.find("replace")->get("$basetexture") == "brick/other");
	CHECK(block.find("insert")->get("$detail") == "detail/noise");
	CHECK(block.find("insert")->get("$envmap") == "env_cubemap");

	const auto fresh = parseMaterial(newMaterialText("UnlitGeneric", {{"$basetexture", "a/b"}, {"$translucent", "1"}}), "a/b");
	REQUIRE(fresh);
	CHECK(fresh->shader == "UnlitGeneric");
	CHECK(fresh->flag("$translucent"));
}

TEST_CASE("height maps for relief shaders") {
	// A tiling relief (bumps), its normal map as Source's tools make it (green along v, down).
	constexpr unsigned SIZE = 64;
	std::vector<double> truth(SIZE * SIZE);
	for (unsigned y = 0; y < SIZE; ++y) {
		for (unsigned x = 0; x < SIZE; ++x) {
			truth[y * SIZE + x] = std::sin(x * 2 * 3.14159265 * 4 / SIZE) * std::cos(y * 2 * 3.14159265 * 3 / SIZE);
		}
	}
	ImageRgba normals;
	normals.width = normals.height = SIZE;
	normals.pixels.resize(SIZE * SIZE * 4);
	const double scale = 3.0;   // slope steepness
	for (unsigned y = 0; y < SIZE; ++y) {
		for (unsigned x = 0; x < SIZE; ++x) {
			const double dx = (truth[y * SIZE + (x + 1) % SIZE] - truth[y * SIZE + (x + SIZE - 1) % SIZE]) * 0.5 * scale;
			const double dy = (truth[((y + 1) % SIZE) * SIZE + x] - truth[((y + SIZE - 1) % SIZE) * SIZE + x]) * 0.5 * scale;
			const double length = std::sqrt(dx * dx + dy * dy + 1);
			auto* p = &normals.pixels[(y * SIZE + x) * 4];
			p[0] = std::byte(static_cast<unsigned char>((-dx / length + 1) * 127.5));
			p[1] = std::byte(static_cast<unsigned char>((-dy / length + 1) * 127.5));
			p[2] = std::byte(static_cast<unsigned char>((1 / length + 1) * 127.5));
			p[3] = std::byte(255);
		}
	}
	std::string error;
	auto heights = heightFromNormalMap(normals, false, &error);
	REQUIRE_MESSAGE(heights, error);
	REQUIRE(heights->width == SIZE);
	// The integrated heights follow the relief (correlation close to 1).
	double mean = 0, meanTruth = 0;
	for (std::size_t i = 0; i < truth.size(); ++i) {
		mean += heights->values[i];
		meanTruth += truth[i];
	}
	mean /= double(truth.size());
	meanTruth /= double(truth.size());
	double cov = 0, var = 0, varTruth = 0;
	for (std::size_t i = 0; i < truth.size(); ++i) {
		cov += (heights->values[i] - mean) * (truth[i] - meanTruth);
		var += (heights->values[i] - mean) * (heights->values[i] - mean);
		varTruth += (truth[i] - meanTruth) * (truth[i] - meanTruth);
	}
	CHECK(cov / std::sqrt(var * varTruth) > 0.95);

	// Zones: flat stays on top, inverted turns round.
	HeightMap map = *heights;
	std::vector<std::uint8_t> zones(4, 0);   // 2 x 2: top left flat, top right inverted
	zones[0] = std::uint8_t(HeightZone::Flat);
	zones[1] = std::uint8_t(HeightZone::Inverted);
	applyHeightZones(map, zones, 2, 2);
	CHECK(map.at(5, 5) == 1.f);
	CHECK(map.at(40, 5) == doctest::Approx(1.f - heights->at(40, 5)));
	CHECK(map.at(40, 40) == heights->at(40, 40));
	invertHeights(map);
	CHECK(map.at(5, 5) == 0.f);

	// Written as an I8 .vtf and read back.
	const auto vtf = encodeHeightVtf(*heights);
	REQUIRE_FALSE(vtf.empty());
	const auto info = readTextureInfo(vtf);
	REQUIRE(info);
	CHECK(info->format == "I8");
	CHECK(info->mipCount > 1);
	const auto image = decodeTexture(vtf);
	REQUIRE(image);
	CHECK(std::abs(std::to_integer<int>(image->pixels[(10 * SIZE + 20) * 4]) - int(heights->at(20, 10) * 255 + 0.5f)) <= 1);

	// From a base texture: bright stones high, dark joints low.
	ImageRgba bricks;
	bricks.width = bricks.height = SIZE;
	bricks.pixels.resize(SIZE * SIZE * 4);
	for (unsigned y = 0; y < SIZE; ++y) {
		for (unsigned x = 0; x < SIZE; ++x) {
			const bool joint = y % 16 < 2 || x % 32 < 2;
			for (int k = 0; k < 3; ++k) bricks.pixels[(y * SIZE + x) * 4 + std::size_t(k)] = std::byte(joint ? 40 : 180);
			bricks.pixels[(y * SIZE + x) * 4 + 3] = std::byte(255);
		}
	}
	const auto fromColor = heightFromColor(bricks, &error);
	REQUIRE_MESSAGE(fromColor, error);
	CHECK(fromColor->at(16, 8) > fromColor->at(16, 0) + 0.3f);
	CHECK_FALSE(heightFromNormalMap(ImageRgba{}, false, &error));
}

TEST_CASE("relief like mapbase-fidelity's deploy.bat: which walls, from what, how deep") {
	const auto material = [](const std::string& text) {
		auto m = parseMaterial(text, "test");
		REQUIRE(m);
		return *m;
	};
	// A bump-mapped wall: from its normal map.
	auto source = fidelityReliefSource("concrete/wall01", material("LightmappedGeneric { $basetexture concrete/wall01 $bumpmap concrete/wall01_normal }"));
	REQUIRE(source);
	CHECK(source->kind == ReliefSource::Kind::NormalMap);
	CHECK(source->texture == "concrete/wall01_normal");
	CHECK(source->depth == doctest::Approx(0.02));
	CHECK(fidelityReliefSource("concrete/wall01", material("LightmappedGeneric { $basetexture a $bumpmap b $ssbump 1 }"))->kind ==
	      ReliefSource::Kind::SSBump);
	// Bricks without a bump map: from the texture, half as deep (courses).
	source = fidelityReliefSource("brick/brickwall001a", material("LightmappedGeneric { $basetexture brick/brickwall001a }"));
	REQUIRE(source);
	CHECK(source->kind == ReliefSource::Kind::Color);
	CHECK(source->depth == doctest::Approx(0.01));
	// Already on the shader: its own height map and depth.
	source = fidelityReliefSource("x/y", material("Fidelity_Parallax { $basetexture a $parallaxmap a_fidh $parallaxdepth 0.035 }"));
	REQUIRE(source);
	CHECK(source->kind == ReliefSource::Kind::HeightMap);
	CHECK(source->texture == "a_fidh");
	CHECK(source->depth == doctest::Approx(0.035));
	// Not: plain names, wood, models, see-through, other shaders.
	CHECK_FALSE(fidelityReliefSource("concrete/floor01", material("LightmappedGeneric { $basetexture concrete/floor01 }")));
	CHECK_FALSE(fidelityReliefSource("wood/woodwall009a", material("LightmappedGeneric { $basetexture wood/woodwall009a $bumpmap wood/n }")));
	CHECK_FALSE(fidelityReliefSource("models/props/crate", material("VertexLitGeneric { $basetexture a $bumpmap b }")));
	CHECK_FALSE(fidelityReliefSource("glass/window01", material("LightmappedGeneric { $basetexture a $bumpmap b }")));
	CHECK_FALSE(fidelityReliefSource("brick/fence", material("LightmappedGeneric { $basetexture a $bumpmap b $alphatest 1 }")));
	CHECK_FALSE(fidelityReliefSource("brick/wall", material("UnlitGeneric { $basetexture brick/wall }")));
}

TEST_CASE("particle systems in full and their preview simulation") {
	const auto definitions = readParticleDefinitions(test::makeDemoFirePcf());
	REQUIRE(definitions.size() == 2);
	const auto& fire = definitions[0].name == "demo_fire_small" ? definitions[0] : definitions[1];
	CHECK(fire.name == "demo_fire_small");
	CHECK(fire.material() == "particle/demo_flame");
	CHECK(fire.properties.number("max_particles", 0) == doctest::Approx(200));
	REQUIRE(fire.emitters.size() == 1);
	CHECK(fire.emitters[0].name == "emit_continuously");
	CHECK(fire.emitters[0].number("emission_rate", 0) == doctest::Approx(40));
	CHECK(fire.initializers.size() == 5);
	CHECK(fire.operators.size() == 5);
	REQUIRE(fire.renderers.size() == 1);
	CHECK(fire.renderers[0].name == "render_animated_sprites");
	const auto* color = fire.initializers[4].find("color1");
	REQUIRE(color);
	CHECK(color->text == "255 210 90 255");
	CHECK(fire.operators[0].vector("gravity", {}).z == doctest::Approx(30));
	REQUIRE(fire.children.size() == 1);
	CHECK(fire.children[0].name == "demo_smoke_column");
	CHECK(fire.children[0].delay == doctest::Approx(0.3));

	// Broken files: nothing, never a crash.
	const auto whole = test::makeDemoFirePcf();
	for (const std::size_t cut : {whole.size() / 3, whole.size() / 2, whole.size() - 5}) {
		auto part = whole;
		part.resize(cut);
		(void)readParticleDefinitions(part);
	}
	CHECK(readParticleDefinitions(std::vector<std::byte>(64, std::byte{0x41})).empty());

	// The fire: particles appear, rise, fade; the smoke child starts after its delay.
	ParticleSimulation simulation(definitions, "demo_fire_small", 7);
	REQUIRE(simulation.valid());
	REQUIRE(simulation.systems().size() == 2);
	CHECK(simulation.systems()[1].name == "demo_smoke_column");
	simulation.step(0.2);
	CHECK(simulation.particleCount() > 3);
	for (const auto& s : simulation.sprites()) CHECK(s.system == 0);   // no smoke yet
	simulation.step(1.0);
	std::size_t smoke = 0;
	double highest = 0;
	for (const auto& s : simulation.sprites()) {
		smoke += s.system == 1 ? 1 : 0;
		highest = std::max(highest, s.position.z);
		CHECK(s.alpha >= 0);
		CHECK(s.alpha <= 1);
	}
	CHECK(smoke > 0);
	CHECK(highest > 20);   // rises
	const auto count = simulation.particleCount();
	CHECK(count < 120);    // decays, does not pile up
	// Deterministic for a seed.
	ParticleSimulation again(definitions, "demo_fire_small", 7);
	again.step(1.2);
	CHECK(again.particleCount() == count);

	// Sparks: one burst that falls and then starts again (the preview loops).
	const auto sparks = readParticleDefinitions(test::makeDemoSparksPcf());
	ParticleSimulation burst(sparks, "demo_sparks", 1);
	CHECK(burst.systems()[0].trail);
	burst.step(1.0 / 60);
	CHECK(burst.particleCount() == 60);
	burst.step(0.3);
	double lowest = 0;
	for (const auto& s : burst.sprites()) lowest = std::min(lowest, s.velocity.z);
	CHECK(lowest < -50);   // gravity pulls down
	burst.step(1.0);
	burst.step(1.0 / 60);
	CHECK(burst.particleCount() > 0);   // started again
	CHECK(burst.time() < 1.0);

	// Unknown system: nothing to simulate.
	CHECK_FALSE(ParticleSimulation(sparks, "nope").valid());
}

namespace {

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& data) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

} // namespace

TEST_CASE("loop detection, prop class and sound loop checks with autofix") {
	constexpr int RATE = 22050;
	std::vector<std::byte> pcm;
	for (const int v : sine(500, 440, RATE)) put16(pcm, v);
	const auto looped = wavFile(fmtChunk(1, 1, RATE, 2, 16), pcm, 100u);
	const auto once = wavFile(fmtChunk(1, 1, RATE, 2, 16), pcm);
	CHECK(soundFileLoops(looped) == true);
	CHECK(soundFileLoops(once) == false);
	{
		// A sampler loop ("smpl" with one loop) counts as well.
		std::vector<std::byte> smpl(once.begin(), once.begin() + 12);
		putTag(smpl, "smpl");
		put32(smpl, 60);
		for (int i = 0; i < 7; ++i) put32(smpl, 0);   // manufacturer ... SMPTE offset
		put32(smpl, 1);                                // one loop
		put32(smpl, 0);                                // no sampler data
		for (std::uint32_t v : {0u, 0u, 50u, 400u, 0u, 0u}) put32(smpl, v);
		smpl.insert(smpl.end(), once.begin() + 12, once.end());
		CHECK(soundFileLoops(smpl) == true);
	}
	const std::vector<std::byte> ogg{std::byte('O'), std::byte('g'), std::byte('g'), std::byte('S')};
	CHECK_FALSE(soundFileLoops(ogg).has_value());
	CHECK(soundFileLoops(std::vector<std::byte>(16, std::byte(0xff))) == false);   // MP3 frames never loop

	CHECK(recommendedPropClass({true, false}) == "prop_static");
	CHECK(recommendedPropClass({false, true}) == "prop_physics");
	CHECK(recommendedPropClass({false, false}) == "prop_dynamic");
	CHECK(betterPropClass("prop_static", {false, true}) == "prop_dynamic");
	CHECK(betterPropClass("prop_physics", {true, false}) == "prop_static");
	CHECK(betterPropClass("prop_physics", {false, true}).empty());
	CHECK(betterPropClass("prop_dynamic", {false, false}).empty());

	const auto dir = std::filesystem::temp_directory_path() / "sme_prop_sound_checks";
	std::filesystem::remove_all(dir);
	test::StudioModelSpec spec;
	spec.groups = {{"body", {{"box", {{-8, -8, 0, 8, 8, 16}}}}}};
	spec.staticProp = true;
	test::writeStudioModel(dir / "models/props/static_crate.mdl", test::makeStudioModel(spec));
	spec.staticProp = false;
	spec.mass = 20;
	test::writeStudioModel(dir / "models/props/barrel.mdl", test::makeStudioModel(spec));
	test::writeText(dir / "models/props/barrel.phy", "collision");
	writeBytes(dir / "sound/ambient/hum_loop.wav", looped);
	writeBytes(dir / "sound/ambient/bell.wav", once);
	test::writeText(dir / "scripts/game_sounds_manifest.txt", "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds_test.txt\"\n}\n");
	test::writeText(dir / "scripts/game_sounds_test.txt", "\"Test.Hum\"\n{\n\t\"wave\" \")ambient/hum_loop.wav\"\n}\n");
	GameFileSystem fs;
	fs.addDirectory(dir, {"game"});
	CHECK(readPropModelFlags(fs, "models/props/static_crate.mdl") == PropModelFlags{true, false});
	CHECK(readPropModelFlags(fs, "models/props/barrel.mdl") == PropModelFlags{false, true});
	CHECK_FALSE(readPropModelFlags(fs, "models/props/missing.mdl"));
	CHECK(soundFileOf("#ambient/bell.wav", nullptr) == "ambient/bell.wav");
	CHECK(soundLoops(fs, "Test.Hum", nullptr) == std::nullopt);   // needs the sound scripts
	const auto scripts = soundScripts(fs);
	CHECK(soundLoops(fs, "Test.Hum", &scripts) == true);

	MapDocument doc;
	const auto add = [&](std::vector<std::pair<std::string, std::string>> keys) {
		Entity e;
		e.id = doc.newEntityId();
		e.keyValues = std::move(keys);
		e.set("origin", "0 0 0");
		doc.entities.push_back(e);
		return e.id;
	};
	const int physicsStatic = add({{"classname", "prop_physics"}, {"model", "models/props/static_crate.mdl"}});
	const int staticBarrel = add({{"classname", "prop_static"}, {"model", "models/props/barrel.mdl"}});
	const int goodBarrel = add({{"classname", "prop_physics"}, {"model", "models/props/barrel.mdl"}});
	const int loopFlagged = add({{"classname", "ambient_generic"}, {"message", "ambient/hum_loop.wav"}, {"spawnflags", "33"}});
	const int bellLooping = add({{"classname", "ambient_generic"}, {"message", "ambient/bell.wav"}, {"spawnflags", "0"}});
	const int scriptOk = add({{"classname", "ambient_generic"}, {"message", "Test.Hum"}, {"spawnflags", "1"}});
	add({{"classname", "sky_camera"}});
	add({{"classname", "sky_camera"}});
	add({{"classname", "info_player_start"}});

	CheckCache cache;
	CheckContext context;
	context.files = &fs;
	context.cache = &cache;
	auto problems = checkMap(doc, context);
	const auto about = [&](Problem::Kind kind, int entityId) -> const Problem* {
		for (const auto& p : problems) {
			if (p.kind == kind && (entityId == 0 || p.entityId == entityId)) return &p;
		}
		return nullptr;
	};
	REQUIRE(about(Problem::Kind::PropClassMismatch, physicsStatic));
	CHECK(about(Problem::Kind::PropClassMismatch, physicsStatic)->to == "prop_static");
	REQUIRE(about(Problem::Kind::PropClassMismatch, staticBarrel));
	CHECK(about(Problem::Kind::PropClassMismatch, staticBarrel)->fixLabel == "Make prop_dynamic");
	CHECK_FALSE(about(Problem::Kind::PropClassMismatch, goodBarrel));
	REQUIRE(about(Problem::Kind::SoundLoopMismatch, loopFlagged));
	CHECK(about(Problem::Kind::SoundLoopMismatch, loopFlagged)->to == "1");
	REQUIRE(about(Problem::Kind::SoundLoopMismatch, bellLooping));
	CHECK(about(Problem::Kind::SoundLoopMismatch, bellLooping)->to == "32");
	CHECK_FALSE(about(Problem::Kind::SoundLoopMismatch, scriptOk));
	REQUIRE(about(Problem::Kind::MultipleSkyCameras, 0));
	CHECK(about(Problem::Kind::MultipleSkyCameras, 0)->objects.entities.size() == 2);
	CHECK(cache.models.size() == 2);
	CHECK(cache.soundLoops.size() == 3);

	History history;
	for (int round = 0; round < 10; ++round) {
		const auto fixable = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return !p.fixLabel.empty(); });
		if (fixable == problems.end()) break;
		history.push(applyFix(doc, *fixable, nullptr));
		problems = checkMap(doc, context);
	}
	CHECK(doc.findEntity(physicsStatic)->classname() == "prop_static");
	CHECK(doc.findEntity(staticBarrel)->classname() == "prop_dynamic");
	CHECK(doc.findEntity(loopFlagged)->get("spawnflags") == "1");
	CHECK(doc.findEntity(bellLooping)->get("spawnflags") == "32");
	CHECK_FALSE(about(Problem::Kind::PropClassMismatch, 0));
	CHECK_FALSE(about(Problem::Kind::SoundLoopMismatch, 0));
	std::filesystem::remove_all(dir);
}
