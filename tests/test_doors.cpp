// test_doors.cpp - Doors (keys, hinge, slide, preview motion, Make door) and one-step connections.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/doors.h>
#include <sme/editing.h>
#include <sme/io_links.h>

#include <algorithm>
#include <cmath>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

/// A world block selected for Make door.
Selection block(MapDocument& doc, Vec3 a, Vec3 b) {
	auto [change, ref] = edit::createBlock(doc, box(a, b), "DEV/DOOR");
	Selection selection;
	selection.solids.push_back(ref);
	return selection;
}

long long flags(const Entity& e) {
	return std::stoll(e.get("spawnflags", "0"));
}

/// Where the free edge ends up after the first turn of the preview.
Vec3 openedEdge(const DoorMotion& m, double turn) {
	const Vec3 tip = m.leaf * m.leafLength;
	return m.hinge + Mat3::rotation(2, turn) * tip;
}

} // namespace

TEST_CASE("door frame, hinge point and movedir") {
	// 128 wide along X, 8 thin along Y: the front faces +Y; looking at it, right is -X.
	const auto frame = doorFrame(box({-64, -4, 0}, {64, 4, 128}));
	CHECK(frame.front.y == 1);
	CHECK(frame.right.x == -1);
	CHECK(frame.width == 128);
	CHECK(frame.thickness == 8);
	const Vec3 left = hingePoint(frame, DoorHinge::Left);
	CHECK(left.x == 64);
	CHECK(left.y == 0);
	CHECK(left.z == 64);
	CHECK(hingePoint(frame, DoorHinge::Right).x == -64);
	// Thin along X: right is +Y.
	const auto side = doorFrame(box({0, 0, 0}, {8, 96, 128}));
	CHECK(side.front.x == 1);
	CHECK(side.right.y == 1);
	CHECK(hingePoint(side, DoorHinge::Right).y == 96);

	CHECK(moveDirAngles({0, 0, 1}) == "-90 0 0");
	CHECK(moveDirAngles({0, 0, -1}) == "90 0 0");
	CHECK(moveDirAngles({-1, 0, 0}) == "0 180 0");
	CHECK(moveDirAngles({0, -1, 0}) == "0 270 0");
	const auto up = moveDirVector("-90 0 0");
	REQUIRE(up);
	CHECK(up->z == doctest::Approx(1));
	const auto west = moveDirVector("0 180 0");
	CHECK(west->x == doctest::Approx(-1));
	CHECK_FALSE(moveDirVector("nonsense"));

	// CBaseDoor: the size minus 2 minus the lip.
	const Vec3 offset = slideOffset(box({-64, -4, 0}, {64, 4, 128}), {0, 0, 1}, 4);
	CHECK(offset.z == doctest::Approx(122));
	CHECK(offset.x == 0);
}

TEST_CASE("make a swinging door: hinge origin, one-way direction, undo") {
	MapDocument doc;
	const auto selection = block(doc, {-32, -4, 0}, {32, 4, 112});
	DoorSettings s;
	s.type = DoorType::Swinging;
	s.hinge = DoorHinge::Right;
	s.swing = DoorSwing::Front;
	s.angle = 100;
	s.closeAfter = -1;
	s.sounds = {"Doors.Move1", "", "Doors.FullClose1", "Doors.Locked1"};
	auto [change, ids] = edit::makeDoor(doc, selection, s);
	REQUIRE(ids.size() == 1);
	CHECK(doc.world.solids.empty());
	const Entity& door = *doc.findEntity(ids[0]);
	CHECK(door.classname() == "func_door_rotating");
	CHECK(door.solids.size() == 1);
	CHECK(door.origin().x == -32);   // right edge, seen from the front (+Y)
	CHECK(door.origin().y == 0);
	CHECK(door.get("distance") == "100");
	CHECK(door.get("wait") == "-1");
	CHECK((flags(door) & 16) != 0);    // one-way
	CHECK((flags(door) & 256) != 0);   // use opens
	CHECK(door.get("noise1") == "Doors.Move1");
	CHECK(door.get("noise2") == "Doors.FullClose1");
	CHECK(door.get("closesound") == "Doors.FullClose1");
	CHECK(door.get("startclosesound").empty());

	// The free edge (at x 32) swings to the front (+Y).
	const auto motion = doorMotion(door);
	REQUIRE(motion);
	REQUIRE(motion->turns.size() == 1);
	CHECK(motion->leafLength == doctest::Approx(64));
	const Vec3 edge = openedEdge(*motion, motion->turns[0]);
	CHECK(edge.y > 40);

	// Reading gives back what was written.
	const auto read = readDoor(door);
	CHECK(read.type == DoorType::Swinging);
	CHECK(read.hinge == DoorHinge::Right);
	CHECK(read.swing == DoorSwing::Front);
	CHECK(read.angle == 100);
	CHECK(read.closeAfter == -1);

	History history;
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.entities.empty());
	CHECK(doc.world.solids.size() == 1);
}

TEST_CASE("door box edits: flip hinge, swing back, switch to sliding") {
	MapDocument doc;
	const auto selection = block(doc, {0, 0, 0}, {8, 64, 112});   // thin along X
	DoorSettings s;
	auto [made, ids] = edit::makeDoor(doc, selection, s);
	const int id = ids.at(0);
	CHECK(readDoor(*doc.findEntity(id)).swing == DoorSwing::AwayFromPlayer);
	CHECK(doorMotion(*doc.findEntity(id))->turns.size() == 2);   // either way

	s = readDoor(*doc.findEntity(id));
	s.hinge = DoorHinge::Right;
	s.swing = DoorSwing::Back;
	(void)edit::setDoor(doc, id, s);
	const Entity& door = *doc.findEntity(id);
	CHECK(door.origin().y == 64);   // right of the front (+X) is +Y
	const auto read = readDoor(door);
	CHECK(read.hinge == DoorHinge::Right);
	CHECK(read.swing == DoorSwing::Back);
	const auto motion = doorMotion(door);
	REQUIRE(motion->turns.size() == 1);
	CHECK(openedEdge(*motion, motion->turns[0]).x < -40);   // swings to the back (-X)

	s.type = DoorType::Sliding;
	s.slide = DoorSlide::Left;
	(void)edit::setDoor(doc, id, s);
	const Entity& slider = *doc.findEntity(id);
	CHECK(slider.classname() == "func_door");
	CHECK(slider.get("movedir") == "0 270 0");   // left of the front (+X) is -Y
	CHECK(readDoor(slider).slide == DoorSlide::Left);
	CHECK(doorMotion(slider)->slide.y == doctest::Approx(-62));
	CHECK((flags(slider) & 16) == 0);
}

TEST_CASE("double doors: two halves, one name, opening from the middle") {
	MapDocument doc;
	const auto selection = block(doc, {-64, -4, 0}, {64, 4, 128});
	DoorSettings s;
	s.doubleDoor = true;
	s.swing = DoorSwing::Front;
	auto [change, ids] = edit::makeDoor(doc, selection, s);
	REQUIRE(ids.size() == 2);
	const Entity& a = *doc.findEntity(ids[0]);
	const Entity& b = *doc.findEntity(ids[1]);
	CHECK(a.get("targetname") == "door_1");
	CHECK(b.get("targetname") == "door_1");
	CHECK(entityBrushBounds(a).min.x == doctest::Approx(0));   // the left half, seen from the front
	CHECK(entityBrushBounds(b).max.x == doctest::Approx(0));
	CHECK(a.origin().x == 64);
	CHECK(b.origin().x == -64);
	// Both free edges meet in the middle and both swing to the front.
	for (const Entity* half : {&a, &b}) {
		const auto m = doorMotion(*half);
		CHECK(openedEdge(*m, m->turns[0]).y > 40);
	}

	// Sliding halves part to the sides.
	MapDocument doc2;
	const auto sel2 = block(doc2, {-64, -4, 0}, {64, 4, 128});
	s.type = DoorType::Sliding;
	s.slide = DoorSlide::Left;
	auto [change2, ids2] = edit::makeDoor(doc2, sel2, s);
	REQUIRE(ids2.size() == 2);
	CHECK(doorMotion(*doc2.findEntity(ids2[0]))->slide.x > 0);
	CHECK(doorMotion(*doc2.findEntity(ids2[1]))->slide.x < 0);
}

TEST_CASE("prop door stands at the hinge, turned to the block") {
	MapDocument doc;
	const auto selection = block(doc, {0, 0, 16}, {8, 56, 128});   // thin along X
	DoorSettings s;
	s.type = DoorType::Prop;
	s.model = "models/props_demo/door01.mdl";
	s.hinge = DoorHinge::Right;   // right of the front (+X) is +Y
	const Bounds local = box({-1, 0, -2}, {1, 56, 110});   // leaf along +Y, origin 2 above the bottom
	auto [change, ids] = edit::makeDoor(doc, selection, s, nullptr, &local);
	REQUIRE(ids.size() == 1);
	CHECK(doc.world.solids.empty());
	const Entity& door = *doc.findEntity(ids[0]);
	CHECK(door.classname() == "prop_door_rotating");
	CHECK(door.get("model") == s.model);
	CHECK(door.origin().y == 56);
	CHECK(door.origin().z == 18);
	CHECK(door.get("angles") == "0 180 0");   // local +Y onto world -Y (towards the other edge)
	const auto motion = doorMotion(door, &local);
	REQUIRE(motion);
	CHECK(motion->leaf.y == doctest::Approx(-1));
	CHECK(motion->turns.size() == 2);
	CHECK(motion->closed.min.y == doctest::Approx(0));
	CHECK(motion->closed.max.y == doctest::Approx(56));
}

TEST_CASE("door sounds, models and names") {
	const std::vector<std::string> sounds = {"Weapon_Pistol.Single", "Doors.Squeak", "Doors.Move1", "DoorSound.Null", "Doors.FullClose1", "Doors.Locked1"};
	const auto picked = defaultDoorSounds(DoorType::Sliding, sounds);
	REQUIRE(picked.size() == doorSoundSlots(DoorType::Sliding).size());
	CHECK(picked[0] == "Doors.Move1");
	CHECK(picked[2] == "Doors.FullClose1");
	CHECK(picked[3] == "Doors.Locked1");
	CHECK(defaultDoorSounds(DoorType::Prop, sounds).empty());
	CHECK(isDoorSound("doors.squeak"));
	CHECK_FALSE(isDoorSound("Demo.Wind"));

	const auto models = doorModels({"models/props_c17/door01_left.mdl", "models/props_c17/door_frame01.mdl", "models/props_c17/oildrum001.mdl",
	                                "models/props_doors/doorknob01.mdl"});
	REQUIRE(models.size() == 1);
	CHECK(models[0] == "models/props_c17/door01_left.mdl");

	MapDocument doc;
	CHECK(uniqueTargetName(doc, "func_door_rotating") == "door_1");
	Entity named;
	named.id = doc.newEntityId();
	named.set("classname", "func_door");
	named.set("targetname", "door_1");
	doc.entities.push_back(named);
	CHECK(uniqueTargetName(doc, "prop_door_rotating") == "door_2");
	CHECK(uniqueTargetName(doc, "ambient_generic") == "sound_1");
	CHECK(uniqueTargetName(doc, "logic_relay") == "relay_1");
	CHECK(uniqueTargetName(doc, "trigger_multiple") == "trigger_1");
}

TEST_CASE("areaportal next to a door gets linked") {
	MapDocument doc;
	auto [made, ids] = edit::makeDoor(doc, block(doc, {-32, -4, 0}, {32, 4, 112}), DoorSettings{});
	auto [portalChange, portalId] = edit::tieToEntity(doc, block(doc, {-32, -1, 0}, {32, 1, 112}), "func_areaportal", nullptr);
	const Entity& door = *doc.findEntity(ids[0]);
	const Entity* portal = doorAreaportal(doc, door);
	REQUIRE(portal);
	CHECK(portal->id == portalId);
	CHECK_FALSE(areaportalLinked(*portal, door));
	// The Problems list offers the same link as an autofix.
	const auto problems = checkMap(doc, CheckContext{});
	const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::DoorAreaportalNotLinked; });
	REQUIRE(it != problems.end());
	CHECK(it->fixLabel == "Link areaportal");
	(void)applyFix(doc, *it, nullptr);
	CHECK(doc.findEntity(ids[0])->get("targetname") == "door_1");
	CHECK(areaportalLinked(*doc.findEntity(portalId), *doc.findEntity(ids[0])));
	const auto after = checkMap(doc, CheckContext{});
	CHECK(std::none_of(after.begin(), after.end(), [](const Problem& p) { return p.kind == Problem::Kind::DoorAreaportalNotLinked; }));
}

TEST_CASE("connections: likely pairs first, target named in the same step") {
	EntityClass trigger;
	trigger.name = "trigger_once";
	trigger.outputs = {{"OnStartTouch", "void", ""}, {"OnTrigger", "void", ""}, {"OnUser1", "void", ""}};
	EntityClass door;
	door.name = "func_door";
	door.inputs = {{"Close", "void", ""}, {"Lock", "void", ""}, {"Open", "void", ""}, {"Kill", "void", ""}};
	const auto pairs = suggestConnections("trigger_once", &trigger, "func_door", &door);
	REQUIRE(!pairs.empty());
	CHECK(pairs[0].output == "OnTrigger");
	CHECK(pairs[0].input == "Open");
	CHECK(rankedInputs("func_door", &door).back() == "Kill");
	CHECK(rankedOutputs("func_button", nullptr).front() == "OnPressed");
	CHECK(rankedInputs("ambient_generic", nullptr).front() == "PlaySound");
	CHECK(rankedInputs("light_spot", nullptr).front() == "Toggle");

	MapDocument doc;
	auto [c1, sourceId] = edit::placeEntity(doc, "func_button", {0, 0, 0}, nullptr);
	auto [c2, targetId] = edit::placeEntity(doc, "ambient_generic", {64, 0, 0}, nullptr);
	Connection connection;
	connection.output = "OnPressed";
	connection.input = "PlaySound";
	connection.delay = 0.5;
	auto [change, name] = edit::connect(doc, sourceId, targetId, connection);
	CHECK(name == "sound_1");
	CHECK(doc.findEntity(targetId)->get("targetname") == "sound_1");
	REQUIRE(doc.findEntity(sourceId)->connections.size() == 1);
	CHECK(doc.findEntity(sourceId)->connections[0].target == "sound_1");
	CHECK(doc.findEntity(sourceId)->connections[0].delay == 0.5);
	History history;
	history.push(std::move(change));
	history.undo(doc);   // one step takes back both
	CHECK(doc.findEntity(sourceId)->connections.empty());
	CHECK(doc.findEntity(targetId)->get("targetname").empty());
	CHECK(edit::connect(doc, sourceId, 12345, connection).second.empty());
}
