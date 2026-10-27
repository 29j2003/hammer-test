// test_workflow.cpp - Everyday safety and search helpers: autosave names and rotation, the .vmx
// backup name, finding entities, replacing materials.

#include <doctest/doctest.h>

#include <sme/autosave.h>
#include <sme/editing.h>
#include <sme/entity_search.h>
#include <sme/history.h>
#include <sme/material_replace.h>

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

Entity makeEntity(MapDocument& doc, const std::string& classname, std::vector<std::pair<std::string, std::string>> keys) {
	Entity entity;
	entity.id = doc.newEntityId();
	entity.set("classname", classname);
	for (auto& [key, value] : keys) entity.set(key, value);
	doc.entities.push_back(entity);
	return entity;
}

} // namespace

TEST_CASE("autosave names, listing newest first, keeping the newest few, .vmx backup") {
	const AutosaveStamp stamp{2026, 10, 27, 21, 30, 5};
	CHECK(stamp.text() == "20261027-213005");
	CHECK(AutosaveStamp::parse("20261027-213005") == stamp);
	CHECK_FALSE(AutosaveStamp::parse("20261327-213005"));   // month 13
	CHECK_FALSE(AutosaveStamp::parse("2026102-2130050"));
	CHECK(autosaveFileName("de.test", stamp) == "de.test.20261027-213005.vmf");

	const auto parsed = parseAutosaveName("folder/de.test.20261027-213005.vmf");
	REQUIRE(parsed);
	CHECK(parsed->mapName == "de.test");   // dots in the map name survive
	CHECK(parsed->stamp == stamp);
	CHECK_FALSE(parseAutosaveName("folder/de_test.vmf"));
	CHECK_FALSE(parseAutosaveName("folder/.20261027-213005.vmf"));

	CHECK(autosaveFolder("maps/src/de_test.vmf") == std::filesystem::path("maps/src/autosave"));
	CHECK(backupPath("maps/src/de_test.vmf") == std::filesystem::path("maps/src/de_test.vmx"));

	const auto folder = std::filesystem::temp_directory_path() / "sme_autosave_test";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	for (const int minute : {1, 5, 3, 9, 7}) {
		std::ofstream(folder / autosaveFileName("mymap", {2026, 1, 2, 3, minute, 0})) << "x";
	}
	std::ofstream(folder / autosaveFileName("other", {2026, 1, 2, 3, 59, 0})) << "x";
	std::ofstream(folder / "notes.txt") << "x";
	const auto list = listAutosaves(folder, "MyMap");   // map names compare without case
	REQUIRE(list.size() == 5);
	CHECK(list.front().stamp.minute == 9);
	CHECK(list.back().stamp.minute == 1);
	const auto remove = autosavesToRemove(list, 3);
	REQUIRE(remove.size() == 2);
	CHECK(parseAutosaveName(remove[0])->stamp.minute == 3);
	CHECK(parseAutosaveName(remove[1])->stamp.minute == 1);
	CHECK(autosavesToRemove(list, 10).empty());
	CHECK(listAutosaves(folder / "missing", "mymap").empty());
	std::filesystem::remove_all(folder);
}

TEST_CASE("history: names and position of every step") {
	MapDocument doc;
	History history;
	auto [first, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "A");
	history.push(std::move(first));
	auto [second, b] = edit::createBlock(doc, box({128, 0, 0}, {192, 64, 64}), "B");
	history.push(std::move(second));
	CHECK(history.size() == 2);
	CHECK(history.index() == 2);
	CHECK(history.nameAt(0) == history.nameAt(1));
	CHECK(history.nameAt(5).empty());
	history.undo(doc);
	CHECK(history.index() == 1);
	CHECK(history.size() == 2);   // the undone step stays listed until something new comes
}

TEST_CASE("wildcards and finding entities by class, name, key=value, output target, model, sound") {
	CHECK(wildcardMatch("prop_*", "prop_physics"));
	CHECK(wildcardMatch("PROP_?hysics", "prop_physics"));
	CHECK_FALSE(wildcardMatch("prop_*", "func_door"));
	CHECK(wildcardMatch("*", ""));
	CHECK(looseMatch("door", "func_door_rotating"));
	CHECK_FALSE(looseMatch("door?", "func_door"));

	MapDocument doc;
	const auto door = makeEntity(doc, "func_door", {{"targetname", "gate_main"}, {"speed", "100"}});
	auto relay = makeEntity(doc, "logic_relay", {{"targetname", "relay_open"}});
	doc.entities.back().connections.push_back({"OnTrigger", "gate_*", "Open", "", 0, -1});
	const auto prop = makeEntity(doc, "prop_physics", {{"model", "models/props_c17/oildrum001.mdl"}});
	const auto sound = makeEntity(doc, "ambient_generic", {{"message", "ambient/wind/wind1.wav"}, {"targetname", "wind"}});

	CHECK(findEntities(doc, EntityField::Class, "door") == std::vector<int>{door.id});
	CHECK(findEntities(doc, EntityField::Name, "relay_*") == std::vector<int>{relay.id});
	CHECK(findEntities(doc, EntityField::KeyValue, "speed=1*") == std::vector<int>{door.id});
	CHECK(findEntities(doc, EntityField::KeyValue, "speed") == std::vector<int>{door.id});
	CHECK(findEntities(doc, EntityField::OutputTarget, "gate_*") == std::vector<int>{relay.id});
	CHECK(findEntities(doc, EntityField::Model, "oildrum") == std::vector<int>{prop.id});
	CHECK(findEntities(doc, EntityField::Sound, "wind1") == std::vector<int>{sound.id});
	// The query syntax: several terms, all must match.
	CHECK(findEntities(doc, EntityField::Anything, "class:func_* name:gate*") == std::vector<int>{door.id});
	CHECK(findEntities(doc, EntityField::Anything, "oildrum").size() == 1);
	CHECK(findEntities(doc, EntityField::Anything, "").size() == 4);
	CHECK(findEntities(doc, EntityField::Anything, "nothing_like_this").empty());
}

TEST_CASE("replace materials: whole map, wildcards, selection only, overlays, undo") {
	MapDocument doc;
	History history;
	auto [c1, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	history.push(std::move(c1));
	auto [c2, b] = edit::createBlock(doc, box({128, 0, 0}, {192, 64, 64}), "CONCRETE/CONCRETEWALL001");
	history.push(std::move(c2));
	makeEntity(doc, "info_overlay", {{"material", "concrete/concretewall001"}});

	MaterialReplaceOptions options;
	options.find = "materials/dev/dev_measuregeneric01.vmt";   // any spelling of the name
	options.replaceWith = "brick/brickwall001";
	const Selection none;
	CHECK(countMaterialMatches(doc, none, options) == 6);
	history.push(edit::replaceMaterials(doc, none, options));
	CHECK(doc.findSolid(a)->sides.front().material == "BRICK/BRICKWALL001");
	CHECK(doc.findSolid(b)->sides.front().material == "CONCRETE/CONCRETEWALL001");

	// Wildcards, only the selection: the second block and (not selected) the overlay stay.
	options.find = "concrete/*";
	options.wildcards = true;
	options.onlySelection = true;
	Selection onlyA;
	onlyA.solids.push_back(a);
	CHECK(countMaterialMatches(doc, onlyA, options) == 0);
	Selection onlyB;
	onlyB.solids.push_back(b);
	CHECK(countMaterialMatches(doc, onlyB, options) == 6);
	history.push(edit::replaceMaterials(doc, onlyB, options));
	CHECK(doc.findSolid(b)->sides.front().material == "BRICK/BRICKWALL001");
	CHECK(doc.entities.front().get("material") == "concrete/concretewall001");

	// Whole map again: the overlay follows.
	options.onlySelection = false;
	options.replaceWith = "metal/metal001";
	options.find = "concrete/*";
	CHECK(countMaterialMatches(doc, none, options) == 1);
	history.push(edit::replaceMaterials(doc, none, options));
	CHECK(doc.entities.front().get("material") == "metal/metal001");

	// Face mode: only the chosen face.
	options.find = "brick/brickwall001";
	options.wildcards = false;
	options.onlySelection = true;
	Selection oneFace;
	oneFace.faces.push_back({a, doc.findSolid(a)->sides.front().id});
	CHECK(countMaterialMatches(doc, oneFace, options) == 1);
	history.push(edit::replaceMaterials(doc, oneFace, options));
	CHECK(doc.findSolid(a)->sides[0].material == "METAL/METAL001");
	CHECK(doc.findSolid(a)->sides[1].material == "BRICK/BRICKWALL001");

	// Nothing matched: nothing recorded.
	options.find = "no/such_material";
	CHECK(edit::replaceMaterials(doc, none, options).empty());

	// Undo brings everything back.
	while (history.canUndo() && history.index() > 2) history.undo(doc);
	CHECK(doc.findSolid(a)->sides.front().material == "DEV/DEV_MEASUREGENERIC01");
	CHECK(doc.findSolid(b)->sides.front().material == "CONCRETE/CONCRETEWALL001");
	CHECK(doc.entities.front().get("material") == "concrete/concretewall001");
}
