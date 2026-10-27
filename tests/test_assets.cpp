// test_assets.cpp - Material categories, MDL reading, sound scripts, "did you mean", map checks and fixes.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/checks.h>
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
