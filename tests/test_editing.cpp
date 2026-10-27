// test_editing.cpp - Editor operations: create, move, rotate, resize, copy, delete, entities, materials.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/editing.h>

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
