// test_editing.cpp - Editor operations: create, move, rotate, resize, copy, delete, entities, materials.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/clipboard.h>
#include <sme/fidelity_look.h>
#include <sme/visibility.h>
#include <sme/editing.h>

#include <set>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

} // namespace

TEST_CASE("angles round trip") {
	for (const Vec3 angles : {Vec3{0, 90, 0}, Vec3{30, 45, 0}, Vec3{-20, 170, 10}, Vec3{0, -90, 0}}) {
		const auto back = edit::matrixToAngles(edit::anglesToMatrix(angles));
		CHECK(back.x == doctest::Approx(angles.x));
		CHECK(back.y == doctest::Approx(angles.y));
		CHECK(back.z == doctest::Approx(angles.z));
	}
	const auto forward = edit::anglesToMatrix({45, 0, 0}) * Vec3{1, 0, 0};
	CHECK(forward.z < 0);   // positive pitch looks down, as in Source
}

TEST_CASE("create, move, duplicate, delete with undo") {
	MapDocument doc;
	History history;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	history.push(std::move(create));
	REQUIRE(doc.findSolid(ref));

	Selection sel;
	sel.solids.push_back(ref);
	history.push(edit::translate(doc, sel, {64, 0, 0}, true));
	CHECK(edit::selectionBounds(doc, sel, nullptr).min == Vec3{64, 0, 0});

	auto [dup, copies] = edit::duplicate(doc, sel, {0, 128, 0});
	history.push(std::move(dup));
	REQUIRE(copies.solids.size() == 1);
	CHECK(doc.world.solids.size() == 2);
	CHECK(edit::selectionBounds(doc, copies, nullptr).min == Vec3{64, 128, 0});

	history.push(edit::remove(doc, copies));
	CHECK(doc.world.solids.size() == 1);
	history.undo(doc);
	CHECK(doc.world.solids.size() == 2);
	history.undo(doc);
	history.undo(doc);
	CHECK(edit::selectionBounds(doc, sel, nullptr).min == Vec3{0, 0, 0});
}

TEST_CASE("rotate and resize keep brushes valid") {
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {128, 32, 64}), "X");
	Selection sel;
	sel.solids.push_back(ref);
	auto bounds = edit::selectionBounds(doc, sel, nullptr);
	(void)edit::rotate(doc, sel, Mat3::rotation(2, 90), bounds.center(), true);
	bounds = edit::selectionBounds(doc, sel, nullptr);
	CHECK(bounds.size().x == doctest::Approx(32));
	CHECK(bounds.size().y == doctest::Approx(128));

	(void)edit::scale(doc, sel, bounds, box(bounds.min, bounds.max + Vec3{32, 0, 64}));
	bounds = edit::selectionBounds(doc, sel, nullptr);
	CHECK(bounds.size().x == doctest::Approx(64));
	CHECK(bounds.size().z == doctest::Approx(128));
	CHECK(isConvexValid(*doc.findSolid(ref)));
}

TEST_CASE("entities: place with FGD defaults, rotate, keys, outputs") {
	EntityClass light;
	light.name = "light";
	light.classType = "PointClass";
	EntityProperty brightness;
	brightness.name = "_light";
	brightness.defaultValue = "255 255 255 200";
	EntityProperty flags;
	flags.kind = EntityProperty::Kind::Flags;
	flags.name = "spawnflags";
	flags.flags = {{1, "Initially dark", false}, {2, "Other", true}};
	light.properties = {brightness, flags};

	MapDocument doc;
	History history;
	auto [place, id] = edit::placeEntity(doc, "light", {0, 0, 64}, &light);
	history.push(std::move(place));
	auto* entity = doc.findEntity(id);
	REQUIRE(entity);
	CHECK(entity->get("_light") == "255 255 255 200");
	CHECK(entity->get("spawnflags") == "2");
	CHECK(entity->origin() == Vec3{0, 0, 64});

	Selection sel;
	sel.entities.push_back(id);
	history.push(edit::rotate(doc, sel, Mat3::rotation(2, 90), {0, 0, 0}, true));
	CHECK(doc.findEntity(id)->get("angles") == "0 90 0");

	history.push(edit::setKeyValue(doc, {id}, "targetname", std::string("lamp")));
	CHECK(doc.findEntity(id)->get("targetname") == "lamp");
	history.push(edit::setKeyValue(doc, {id}, "targetname", std::nullopt));
	CHECK(doc.findEntity(id)->get("targetname").empty());

	Connection c;
	c.output = "OnUser1";
	c.target = "lamp";
	c.input = "TurnOff";
	history.push(edit::setConnections(doc, id, {c}));
	CHECK(doc.findEntity(id)->connections.size() == 1);
	history.undo(doc);
	CHECK(doc.findEntity(id)->connections.empty());

	const auto entityBox = edit::entityBounds(*doc.findEntity(id), nullptr);
	CHECK(entityBox.size() == Vec3{16, 16, 16});
}

TEST_CASE("materials on faces and brushes; tie to entity and back") {
	MapDocument doc;
	History history;
	auto [c1, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "OLD");
	auto [c2, b] = edit::createBlock(doc, box({128, 0, 0}, {192, 64, 64}), "OLD");
	history.push(std::move(c1));
	history.push(std::move(c2));

	Selection faces;
	faces.faces.push_back({a, doc.findSolid(a)->sides[0].id});
	history.push(edit::applyMaterial(doc, faces, "NEW"));
	CHECK(doc.findSolid(a)->sides[0].material == "NEW");
	CHECK(doc.findSolid(a)->sides[1].material == "OLD");

	Selection both;
	both.solids = {a, b};
	auto [tie, entityId] = edit::tieToEntity(doc, both, "func_detail", nullptr);
	history.push(std::move(tie));
	CHECK(doc.world.solids.empty());
	REQUIRE(doc.findEntity(entityId));
	CHECK(doc.findEntity(entityId)->solids.size() == 2);

	Selection entity;
	entity.entities.push_back(entityId);
	history.push(edit::applyMaterial(doc, entity, "TOOLS/TOOLSNODRAW"));
	CHECK(doc.findEntity(entityId)->solids[1].sides[3].material == "TOOLS/TOOLSNODRAW");

	history.push(edit::moveToWorld(doc, entity));
	CHECK(doc.world.solids.size() == 2);
	CHECK_FALSE(doc.findEntity(entityId));

	history.undo(doc);
	CHECK(doc.world.solids.empty());
	CHECK(doc.findEntity(entityId));
	history.undo(doc);
	history.undo(doc);
	CHECK(doc.world.solids.size() == 2);
	CHECK(doc.entities.empty());

	Selection stale;
	stale.entities.push_back(entityId);
	stale.solids.push_back({0, 999});
	stale.prune(doc);
	CHECK(stale.empty());
}

TEST_CASE("face edits") {
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "X");
	const FaceRef face{ref, doc.findSolid(ref)->sides[2].id};
	(void)edit::modifyFaces(doc, {face}, "Scale", [](Side& side) {
		side.u.scale = 0.5;
		side.v.scale = 0.5;
	});
	CHECK(doc.findSolid(ref)->sides[2].u.scale == 0.5);
	CHECK(doc.findSolid(ref)->sides[1].u.scale == 0.25);
}

TEST_CASE("faces by material, whole walls, merged paint strokes") {
	MapDocument doc;
	// Two blocks side by side form one wall at y = 0; a third block stands apart.
	auto [c1, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "WALL");
	auto [c2, b] = edit::createBlock(doc, box({64, 0, 0}, {128, 64, 64}), "WALL");
	auto [c3, far] = edit::createBlock(doc, box({512, 0, 0}, {576, 64, 64}), "OTHER");
	CHECK(edit::facesWithMaterial(doc, "wall").size() == 12);
	CHECK(edit::facesWithMaterial(doc, "OTHER").size() == 6);

	int southA = 0;
	for (const auto& side : doc.findSolid(a)->sides) {
		if (side.plane().normal.y < -0.9) southA = side.id;
	}
	const auto wall = edit::coplanarConnectedFaces(doc, {a, southA});
	CHECK(wall.size() == 2);                       // both south faces, not the far block's
	(void)c1; (void)c2; (void)c3; (void)far; (void)b;

	History history;
	for (const auto& face : wall) {
		history.push(edit::modifyFaces(doc, {face}, "Paint", [](Side& s) { s.material = "PAINT"; }), true);
	}
	CHECK(edit::facesWithMaterial(doc, "PAINT").size() == 2);
	history.undo(doc);                             // one stroke = one undo step
	CHECK(edit::facesWithMaterial(doc, "PAINT").empty());
}

TEST_CASE("copy and paste map objects: in place, at a point, several copies, unique names") {
	MapDocument doc;
	History history;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	history.push(std::move(create));
	auto [placeDoor, door] = edit::placeEntity(doc, "func_door_rotating", {32, 32, 0}, nullptr, {{"targetname", "door"}});
	history.push(std::move(placeDoor));
	auto [placeButton, button] = edit::placeEntity(doc, "func_button", {0, 0, 0}, nullptr, {{"targetname", "button"}, {"target", "door"}});
	history.push(std::move(placeButton));
	doc.findEntity(button)->connections.push_back({"OnPressed", "door", "Open", "", 0, -1});

	Selection sel;
	sel.solids.push_back(ref);
	sel.entities = {door, button};
	const std::string text = edit::copyObjects(doc, sel);
	CHECK(edit::looksLikeMapObjects(text));
	CHECK_FALSE(edit::looksLikeMapObjects("just some words"));
	CHECK_FALSE(edit::looksLikeMapObjects(""));
	CHECK(edit::copyObjects(doc, Selection{}).empty());

	// In place: same spot, new ids, names unchanged.
	{
		auto pasted = edit::pasteObjects(doc, text, PasteOptions{}, nullptr);
		REQUIRE(pasted);
		CHECK(pasted->second.solids.size() == 1);
		CHECK(pasted->second.entities.size() == 2);
		const auto* solid = doc.findSolid(pasted->second.solids[0]);
		REQUIRE(solid);
		CHECK(solid->id != ref.solidId);
		CHECK(buildBrushGeometry(*solid).bounds.min.x == doctest::Approx(0));
		CHECK(doc.findEntity(pasted->second.entities[0])->get("targetname") == "door");
		history.push(std::move(pasted->first));
		history.undo(doc);
		CHECK(doc.world.solids.size() == 1);
		CHECK(doc.entities.size() == 2);
	}
	// At a point, three copies 128 apart along x, names made unique, outputs follow the names.
	{
		PasteOptions options;
		options.center = Vec3{1000, 0, 32};
		options.copies = 3;
		options.offset = {128, 0, 0};
		options.uniqueNames = true;
		auto pasted = edit::pasteObjects(doc, text, options, nullptr);
		REQUIRE(pasted);
		REQUIRE(pasted->second.solids.size() == 3);
		const auto first = buildBrushGeometry(*doc.findSolid(pasted->second.solids[0])).bounds;
		const auto third = buildBrushGeometry(*doc.findSolid(pasted->second.solids[2])).bounds;
		CHECK(third.min.x - first.min.x == doctest::Approx(256));
		std::set<std::string> names;
		for (const int id : pasted->second.entities) {
			const auto* entity = doc.findEntity(id);
			names.insert(entity->get("targetname"));
			if (entity->classname() == "func_button") {
				// The button of each copy opens the door of the same copy.
				REQUIRE(entity->connections.size() == 1);
				CHECK(entity->connections[0].target == entity->get("target"));
				CHECK(entity->get("target") != "door");
			}
		}
		CHECK(names.size() == 6);
		CHECK_FALSE(names.contains("door"));
		CHECK(names.contains("door_2"));
		CHECK(names.contains("door_4"));
	}
	// Rotated copies: a quarter turn each, around the copied objects' center.
	{
		Selection one;
		one.solids.push_back(ref);
		PasteOptions options;
		options.rotation = {0, 0, 90};
		auto pasted = edit::pasteObjects(doc, edit::copyObjects(doc, one), options, nullptr);
		REQUIRE(pasted);
		CHECK(isConvexValid(*doc.findSolid(pasted->second.solids[0])));
	}
	std::string error;
	CHECK_FALSE(edit::pasteObjects(doc, "world { }", PasteOptions{}, nullptr, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("object kinds for hiding: brushes, tool and sky faces, entities") {
	MapDocument doc;
	auto [c1, wall] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "BRICK/BRICKWALL001A");
	auto [c2, clip] = edit::createBlock(doc, box({100, 0, 0}, {164, 64, 64}), "TOOLS/TOOLSCLIP");
	auto [c3, sky] = edit::createBlock(doc, box({0, 0, 200}, {64, 64, 216}), "TOOLS/TOOLSSKYBOX");
	doc.findSolid(wall)->sides[0].material = "TOOLS/TOOLSNODRAW";
	const int nodrawSide = doc.findSolid(wall)->sides[0].id;
	auto [c4, light] = edit::placeEntity(doc, "light", {0, 0, 0}, nullptr);
	auto [c5, prop] = edit::placeEntity(doc, "prop_static", {0, 0, 0}, nullptr, {{"model", "models/props/crate.mdl"}});
	auto [c6, relay] = edit::placeEntity(doc, "logic_relay", {0, 0, 0}, nullptr);
	auto [c7, cubemap] = edit::placeEntity(doc, "env_cubemap", {0, 0, 0}, nullptr);
	auto [c8, npc] = edit::placeEntity(doc, "npc_citizen", {0, 0, 0}, nullptr);
	Selection triggerBrushes;
	auto [c9, triggerBlock] = edit::createBlock(doc, box({0, 300, 0}, {64, 364, 64}), "TOOLS/TOOLSTRIGGER");
	triggerBrushes.solids.push_back(triggerBlock);
	auto [c10, trigger] = edit::tieToEntity(doc, triggerBrushes, "trigger_once", nullptr);

	CHECK(objectsOfKinds(doc, 0, nullptr).empty());
	const auto tools = objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::ToolBrushes), nullptr);
	CHECK(tools.solids == std::vector<SolidRef>{clip});
	REQUIRE(tools.faces.size() == 1);
	CHECK(tools.faces[0].sideId == nodrawSide);
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Sky), nullptr).solids == std::vector<SolidRef>{sky});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Lights), nullptr).entities == std::vector<int>{light});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Models), nullptr).entities == std::vector<int>{prop});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Helpers), nullptr).entities == std::vector<int>{relay});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Cubemaps), nullptr).entities == std::vector<int>{cubemap});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Npcs), nullptr).entities == std::vector<int>{npc});
	CHECK(objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::Triggers), nullptr).entities == std::vector<int>{trigger});
	const auto world = objectsOfKinds(doc, static_cast<std::uint32_t>(ObjectKind::World), nullptr);
	CHECK(world.solids == std::vector<SolidRef>{wall});
	CHECK(world.faces.empty());
}

TEST_CASE("mapbase-fidelity look values per map: logic_auto -> point_clientcommand") {
	MapDocument doc;
	History history;
	CHECK(mapLookValues(doc).empty());
	REQUIRE(findLookSetting("r_fidelity_sky_clouds"));
	CHECK(findLookSetting("R_FIDELITY_SKY_CLOUDS"));
	CHECK_FALSE(findLookSetting("sv_cheats"));

	history.push(setMapLookValues(doc, {{"r_fidelity_sky_clouds", "0.4"}, {"r_fidelity_temperature", "0.3"}}, {64, 0, 32}));
	REQUIRE(doc.entities.size() == 2);
	const auto values = mapLookValues(doc);
	CHECK(values.size() == 2);
	CHECK(values.at("r_fidelity_sky_clouds") == "0.4");
	const Entity* relay = nullptr;
	for (const auto& entity : doc.entities) {
		if (entity.classname() == "logic_auto") relay = &entity;
	}
	REQUIRE(relay);
	CHECK(relay->get("targetname") == "fidelity_look");
	CHECK(relay->origin().x == doctest::Approx(64));
	REQUIRE(relay->connections.size() == 2);
	CHECK(relay->connections[0].output == "OnMapSpawn");
	CHECK(relay->connections[0].input == "Command");
	CHECK(relay->connections[0].delay > 0);

	// Changing keeps the entities and other outputs of the relay; an empty set removes them.
	doc.entities[0].connections.push_back({"OnMapSpawn", "door", "Open", "", 0, -1});
	history.push(setMapLookValues(doc, {{"r_fidelity_sky_clouds", "0.7"}}, {}));
	CHECK(doc.entities.size() == 2);
	CHECK(mapLookValues(doc).at("r_fidelity_sky_clouds") == "0.7");
	CHECK(mapLookValues(doc).size() == 1);
	history.undo(doc);
	CHECK(mapLookValues(doc).at("r_fidelity_sky_clouds") == "0.4");
	history.redo(doc);
	history.push(setMapLookValues(doc, {}, {}));
	CHECK(doc.entities.empty());
	history.undo(doc);
	CHECK(doc.entities.size() == 2);

	// What players get without a map value: cfg/fidelity.cfg.
	const auto cfg = parseCfgValues("// clouds\nr_fidelity_sky_clouds 0.85\n  r_fidelity_godrays_tint \"1.0 0.92 0.8\" // colour\nexec other\n\n");
	CHECK(cfg.at("r_fidelity_sky_clouds") == "0.85");
	CHECK(cfg.at("r_fidelity_godrays_tint") == "1.0 0.92 0.8");
	CHECK(cfg.at("exec") == "other");
}
