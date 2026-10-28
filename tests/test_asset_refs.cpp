// test_asset_refs.cpp - Assets named by faces and entity keys: what a selection uses ("Show in
// ..." menus) and what uses an asset ("Find in map").

#include <doctest/doctest.h>

#include <sme/asset_refs.h>
#include <sme/brush.h>
#include <sme/entity_catalog.h>

#include <filesystem>
#include <fstream>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

Entity entity(MapDocument& doc, std::vector<std::pair<std::string, std::string>> keys) {
	Entity e;
	e.id = doc.newEntityId();
	e.keyValues = std::move(keys);
	return e;
}

} // namespace

TEST_CASE("asset refs: what a selection uses and what uses an asset") {
	CHECK(normalizedMaterial("Materials\\CONCRETE/Wall01.vmt") == "concrete/wall01");
	CHECK(normalizedMaterial("BRICK/BRICKWALL017A") == "brick/brickwall017a");

	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {64, 64, 64}), "BRICK/BRICKWALL017A"));
	doc.world.solids.push_back(makeBlock(doc, box({128, 0, 0}, {192, 64, 64}), "CONCRETE/WALL01"));
	doc.world.solids.back().sides.front().material = "brick/brickwall017a";   // case differs: still the same material
	doc.entities.push_back(entity(doc, {{"classname", "prop_physics"}, {"model", "models\\Props_C17\\Barrel01.mdl"}, {"origin", "0 0 0"}}));
	doc.entities.push_back(entity(doc, {{"classname", "ambient_generic"}, {"message", "ambient/alarm1.wav"}, {"origin", "0 0 0"}}));
	doc.entities.push_back(entity(doc, {{"classname", "info_particle_system"}, {"effect_name", "fire_large"}, {"origin", "0 0 0"}}));
	doc.entities.push_back(entity(doc, {{"classname", "env_soundscape"}, {"soundscape", "streetwar.city"}, {"origin", "0 0 0"}}));
	doc.entities.push_back(entity(doc, {{"classname", "info_overlay"}, {"material", "decals/arrow"}, {"origin", "0 0 0"}}));
	doc.entities.push_back(entity(doc, {{"classname", "prop_dynamic"}, {"model", "models/props_c17/barrel01.mdl"}, {"origin", "0 0 0"}}));
	const int prop = doc.entities[0].id, ambient = doc.entities[1].id, particles = doc.entities[2].id, scape = doc.entities[3].id,
	          overlay = doc.entities[4].id, dynamic = doc.entities[5].id;

	// One face: its material.
	Selection face;
	face.faces.push_back({{0, doc.world.solids[0].id}, doc.world.solids[0].sides[2].id});
	CHECK(selectionAssets(doc, face, nullptr) == std::vector<AssetRef>{{AssetKind::Material, "brick/brickwall017a"}});

	// A brush: its materials once each; entities: what their keys name.
	Selection mixed;
	mixed.solids.push_back({0, doc.world.solids[1].id});
	mixed.entities = {prop, ambient, particles, scape, overlay};
	const auto assets = selectionAssets(doc, mixed, nullptr);
	REQUIRE(assets.size() == 7);
	CHECK(assets[0] == AssetRef{AssetKind::Material, "brick/brickwall017a"});
	CHECK(assets[1] == AssetRef{AssetKind::Material, "concrete/wall01"});
	CHECK(assets[2] == AssetRef{AssetKind::Model, "models/props_c17/barrel01.mdl"});
	CHECK(assets[3] == AssetRef{AssetKind::Sound, "ambient/alarm1.wav"});
	CHECK(assets[4] == AssetRef{AssetKind::Particle, "fire_large"});
	CHECK(assets[5] == AssetRef{AssetKind::Soundscape, "streetwar.city"});
	CHECK(assets[6] == AssetRef{AssetKind::Material, "decals/arrow"});
	CHECK(selectionAssets(doc, mixed, nullptr, 3).size() == 3);

	// Find in map: every face with the material (any case), entities naming the asset.
	const auto bricks = objectsUsing(doc, {AssetKind::Material, "BRICK/BrickWall017a"}, nullptr);
	CHECK(bricks.faces.size() == 7);   // 6 sides of the first block, 1 of the second
	CHECK(bricks.entities.empty());
	CHECK(objectsUsing(doc, {AssetKind::Model, "models/props_c17/barrel01.mdl"}, nullptr).entities == std::vector<int>{prop, dynamic});
	CHECK(objectsUsing(doc, {AssetKind::Sound, "ambient/alarm1.wav"}, nullptr).entities == std::vector<int>{ambient});
	CHECK(objectsUsing(doc, {AssetKind::Material, "decals/arrow"}, nullptr).entities == std::vector<int>{overlay});
	CHECK(objectsUsing(doc, {AssetKind::Particle, "nothing"}, nullptr).empty());

	// With an FGD, keys of type sound / particlesystem count whatever their name.
	const auto dir = std::filesystem::temp_directory_path() / "sme_asset_refs";
	std::filesystem::create_directories(dir);
	{
		std::ofstream fgd(dir / "test.fgd");
		fgd << "@PointClass = npc_test : \"Test\"\n[\n\talertsound(sound) : \"Alert\" : \"\"\n\tglow(particlesystem) : \"Glow\" : \"\"\n]\n";
	}
	EntityCatalog catalog;
	REQUIRE(catalog.load({dir / "test.fgd"}));
	doc.entities.push_back(entity(doc, {{"classname", "npc_test"}, {"alertsound", "NPC_Test.Alert"}, {"glow", "glow_blue"}}));
	Selection npc;
	npc.entities.push_back(doc.entities.back().id);
	const auto npcAssets = selectionAssets(doc, npc, &catalog);
	CHECK(npcAssets == std::vector<AssetRef>{{AssetKind::Sound, "NPC_Test.Alert"}, {AssetKind::Particle, "glow_blue"}});
	std::filesystem::remove_all(dir);
}

TEST_CASE("model usage counts per model") {
	MapDocument doc;
	doc.entities.push_back(entity(doc, {{"classname", "prop_static"}, {"model", "models/props/Crate01.mdl"}}));
	doc.entities.push_back(entity(doc, {{"classname", "prop_physics"}, {"model", "models\\props\\crate01.mdl"}}));
	doc.entities.push_back(entity(doc, {{"classname", "prop_dynamic"}, {"model", "models/props/lamp.mdl"}}));
	doc.entities.push_back(entity(doc, {{"classname", "func_brush"}, {"model", "*3"}}));   // brush models do not count
	doc.entities.push_back(entity(doc, {{"classname", "env_sprite"}, {"model", "sprites/glow01.vmt"}}));
	const auto usage = modelUsage(doc);
	CHECK(usage.size() == 2);
	CHECK(usage.at("models/props/crate01.mdl") == 2);
	CHECK(usage.at("models/props/lamp.mdl") == 1);
	CHECK(modelUsage(MapDocument{}).empty());
}
