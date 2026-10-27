// test_assets.cpp - Material categories, MDL reading, sound scripts, "did you mean", map checks and fixes.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/assets.h>
#include <sme/studio_model.h>
#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/compile_log.h>
#include <sme/game.h>

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
