// test_csg.cpp - Brush construction and cutting: hulls, shapes, split, carve, hollow, merge,
// rounded edges, clipping and "treat as one" texture justification.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/edge_round.h>
#include <sme/editing.h>
#include <sme/entity_catalog.h>
#include <sme/map_tools.h>
#include <sme/shapes.h>

#include <algorithm>
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

double totalVolume(const std::vector<Solid>& solids) {
	double sum = 0;
	for (const auto& solid : solids) {
		sum += solidVolume(solid);
	}
	return sum;
}

const Side* sideFacing(const Solid& solid, const Vec3& normal) {
	for (const auto& side : solid.sides) {
		if (dot(side.plane().normal, normal) > 0.999) {
			return &side;
		}
	}
	return nullptr;
}

} // namespace

TEST_CASE("convex hull and brushes from points") {
	MapDocument doc;
	std::vector<Vec3> corners;
	for (int i = 0; i < 8; ++i) {
		corners.push_back({(i & 1) ? 64.0 : 0.0, (i & 2) ? 32.0 : 0.0, (i & 4) ? 16.0 : 0.0});
	}
	corners.push_back({32, 16, 8});   // inside: ignored
	CHECK(convexHull(corners).size() == 6);
	const auto solid = solidFromPoints(doc, corners, "BRICK/WALL");
	REQUIRE(solid);
	CHECK(isConvexValid(*solid));
	CHECK(solidVolume(*solid) == doctest::Approx(64 * 32 * 16));
	CHECK_FALSE(solidFromPoints(doc, {{0, 0, 0}, {64, 0, 0}, {0, 64, 0}, {64, 64, 0}}, "X"));   // flat
}

TEST_CASE("shapes of the block tool") {
	MapDocument doc;
	const auto area = box({0, 0, 0}, {256, 256, 128});
	const auto shape = [&](ShapeOptions::Kind kind, int sides = 8) {
		ShapeOptions options;
		options.kind = kind;
		options.sides = sides;
		return makeShape(doc, area, options, "DEV/DEV_MEASUREGENERIC01");
	};
	for (const auto& s : shape(ShapeOptions::Kind::Block)) CHECK(isConvexValid(s));
	const auto wedge = shape(ShapeOptions::Kind::Wedge);
	REQUIRE(wedge.size() == 1);
	CHECK(wedge[0].sides.size() == 5);
	CHECK(solidVolume(wedge[0]) == doctest::Approx(256.0 * 256 * 128 / 2));
	const auto cylinder = shape(ShapeOptions::Kind::Cylinder, 12);
	REQUIRE(cylinder.size() == 1);
	CHECK(cylinder[0].sides.size() == 14);
	const auto spike = shape(ShapeOptions::Kind::Spike, 6);
	REQUIRE(spike.size() == 1);
	CHECK(spike[0].sides.size() == 7);
	const auto sphere = shape(ShapeOptions::Kind::Sphere, 12);
	REQUIRE(sphere.size() == 1);
	CHECK(isConvexValid(sphere[0]));
	// Arch: half circle of 6 pieces, every piece a valid convex brush.
	ShapeOptions arch;
	arch.kind = ShapeOptions::Kind::Arch;
	arch.sides = 6;
	arch.arc = 180;
	arch.wallWidth = 32;
	const auto pieces = makeShape(doc, area, arch, "X");
	REQUIRE(pieces.size() == 6);
	for (const auto& piece : pieces) {
		CHECK(isConvexValid(piece));
		CHECK(buildBrushGeometry(piece).bounds.min.y >= 128 - 1);   // 0..180 degrees: the +Y half
	}
	// Spiral: every piece higher than the one before.
	arch.addHeight = 16;
	const auto spiral = makeShape(doc, area, arch, "X");
	REQUIRE(spiral.size() == 6);
	CHECK(buildBrushGeometry(spiral[5]).bounds.min.z == doctest::Approx(80));
	ShapeOptions stairs;
	stairs.kind = ShapeOptions::Kind::Stairs;
	stairs.steps = 8;
	const auto steps = makeShape(doc, area, stairs, "X");
	REQUIRE(steps.size() == 8);
	CHECK(buildBrushGeometry(steps[7]).bounds.max.z == doctest::Approx(128));
	CHECK(buildBrushGeometry(steps[0]).bounds.max.z == doctest::Approx(16));

	// Through the editor: one undo step, all pieces selected.
	History history;
	auto [change, refs] = edit::createShape(doc, area, arch, "X");
	CHECK(change.name == "Create arch");
	CHECK(refs.size() == 6);
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.world.solids.empty());
}

TEST_CASE("split, carve and hollow") {
	MapDocument doc;
	const Solid cube = makeBlock(doc, box({0, 0, 0}, {128, 128, 128}), "BRICK");
	auto [front, back] = splitSolid(doc, cube, Plane{{1, 0, 0}, 32}, cube.sides.front());
	REQUIRE(front);
	REQUIRE(back);
	CHECK(solidVolume(*back) == doctest::Approx(32.0 * 128 * 128));
	CHECK(solidVolume(*front) == doctest::Approx(96.0 * 128 * 128));
	CHECK(back->id == cube.id);         // the part behind keeps the brush
	CHECK(front->id != cube.id);
	auto [none, all] = splitSolid(doc, cube, Plane{{1, 0, 0}, 500}, cube.sides.front());
	CHECK_FALSE(none);
	CHECK(all);

	// Carve a hole straight through: four pieces around it.
	const Solid hole = makeBlock(doc, box({32, 32, -16}, {96, 96, 144}), "NODRAW");
	const auto pieces = subtractSolid(doc, cube, hole);
	CHECK(pieces.size() == 4);
	CHECK(totalVolume(pieces) == doctest::Approx(128.0 * 128 * 128 - 64.0 * 64 * 128));
	for (const auto& piece : pieces) CHECK(isConvexValid(piece));
	{
		// A window in a wall: full-width slabs above and below, two short pieces at the sides
		// (whatever order the cutter lists its sides in).
		const Solid wall = makeBlock(doc, box({0, 0, 0}, {256, 16, 128}), "BRICK");
		Solid window = makeBlock(doc, box({96, -8, 32}, {160, 24, 96}), "NODRAW");
		std::reverse(window.sides.begin(), window.sides.end());
		const auto parts = subtractSolid(doc, wall, window);
		REQUIRE(parts.size() == 4);
		int fullWidth = 0;
		for (const auto& part : parts) {
			const auto bounds = buildBrushGeometry(part).bounds;
			if (bounds.max.x - bounds.min.x > 255) ++fullWidth;
		}
		CHECK(fullWidth == 2);
		CHECK(totalVolume(parts) == doctest::Approx(256.0 * 16 * 128 - 64.0 * 16 * 64));
		// A notch cut out of a corner: two pieces, not three.
		const Solid notch = makeBlock(doc, box({192, -8, 64}, {300, 24, 200}), "NODRAW");
		const auto corner = subtractSolid(doc, wall, notch);
		CHECK(corner.size() == 2);
		CHECK(totalVolume(corner) == doctest::Approx(256.0 * 16 * 128 - 64.0 * 16 * 64));
		// A cut straight across: the two halves stay two (they are not one convex brush).
		const Solid slot = makeBlock(doc, box({120, -8, -8}, {136, 24, 200}), "NODRAW");
		CHECK(subtractSolid(doc, wall, slot).size() == 2);
	}
	const Solid far = makeBlock(doc, box({500, 0, 0}, {600, 64, 64}), "X");
	const auto untouched = subtractSolid(doc, cube, far);
	REQUIRE(untouched.size() == 1);
	CHECK(untouched[0].id == cube.id);
	const Solid big = makeBlock(doc, box({-8, -8, -8}, {200, 200, 200}), "X");
	CHECK(subtractSolid(doc, cube, big).empty());

	// Hollow: six walls of 16 units inside, or outside with a negative thickness.
	const auto walls = hollowSolid(doc, cube, 16);
	CHECK(walls.size() == 6);
	CHECK(totalVolume(walls) == doctest::Approx(128.0 * 128 * 128 - 96.0 * 96 * 96));
	const auto outside = hollowSolid(doc, cube, -16);
	CHECK(outside.size() == 6);
	CHECK(totalVolume(outside) == doctest::Approx(160.0 * 160 * 160 - 128.0 * 128 * 128));
	CHECK(hollowSolid(doc, cube, 70).empty());   // walls thicker than half the brush
}

TEST_CASE("carve, hollow, merge and clip through the editor") {
	MapDocument doc;
	auto [c1, wall] = edit::createBlock(doc, box({0, 0, 0}, {256, 16, 128}), "BRICK");
	auto [c2, door] = edit::createBlock(doc, box({96, -8, 0}, {160, 24, 96}), "NODRAW");
	Selection cutter;
	cutter.solids.push_back(door);
	History history;
	history.push(edit::carve(doc, cutter));
	CHECK(doc.world.solids.size() == 4);   // door brush + three pieces of the wall (left, right, above)
	history.undo(doc);
	CHECK(doc.world.solids.size() == 2);

	Selection room;
	room.solids.push_back(wall);
	auto [hollowed, walls] = edit::hollow(doc, room, 4);
	CHECK(walls.solids.size() == 6);
	history.push(std::move(hollowed));
	history.undo(doc);

	// Merge two touching blocks: exact; with a gap between them: gaps get filled.
	MapDocument m;
	auto [a1, left] = edit::createBlock(m, box({0, 0, 0}, {64, 64, 64}), "A");
	auto [a2, right] = edit::createBlock(m, box({64, 0, 0}, {128, 64, 64}), "B");
	Selection both;
	both.solids = {left, right};
	const auto merged = edit::merge(m, both);
	REQUIRE(merged);
	CHECK(merged->exact);
	CHECK(m.world.solids.size() == 1);
	CHECK(solidVolume(m.world.solids.front()) == doctest::Approx(128.0 * 64 * 64));
	CHECK(sideFacing(m.world.solids.front(), {1, 0, 0})->material == "B");   // textures come along
	auto [a3, far] = edit::createBlock(m, box({0, 128, 0}, {64, 192, 64}), "C");
	Selection apart;
	apart.solids = {merged->merged, far};
	const auto filled = edit::merge(m, apart);
	REQUIRE(filled);
	CHECK_FALSE(filled->exact);

	// Clip: keep both halves.
	MapDocument c;
	auto [c3, block] = edit::createBlock(c, box({0, 0, 0}, {64, 64, 64}), "X");
	Selection one;
	one.solids.push_back(block);
	auto [clipped, halves] = edit::clip(c, one, Plane{normalize(Vec3{1, 1, 0}), 64 / std::sqrt(2.0)}, edit::ClipKeep::Both);
	CHECK(halves.solids.size() == 2);
	CHECK(totalVolume(c.world.solids) == doctest::Approx(64.0 * 64 * 64));
	auto [clipped2, front] = edit::clip(c, halves, Plane{{0, 0, 1}, 32}, edit::ClipKeep::Back);
	CHECK(totalVolume(c.world.solids) == doctest::Approx(64.0 * 64 * 32));
}

TEST_CASE("rounded and bevelled edges") {
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {128, 128, 128}), "X");
	const auto* solid = doc.findSolid(ref);
	const int top = sideFacing(*solid, {0, 0, 1})->id, east = sideFacing(*solid, {1, 0, 0})->id, west = sideFacing(*solid, {-1, 0, 0})->id;
	const auto round = edit::roundEdge(doc, {ref, top}, {ref, east}, 32, 4);
	REQUIRE(round);
	solid = doc.findSolid(ref);
	CHECK(solid->sides.size() == 10);
	CHECK(isConvexValid(*solid));
	// Rounded corner: between the full block and a block missing the whole 32x32 corner.
	const double full = 128.0 * 128 * 128, notch = 32.0 * 32 * 128;
	CHECK(solidVolume(*solid) < full);
	CHECK(solidVolume(*solid) > full - notch);
	// Opposite faces share no edge.
	CHECK_FALSE(edit::roundEdge(doc, {ref, east}, {ref, west}, 16, 1));
	// Bevel: one flat cut.
	auto [c2, other] = edit::createBlock(doc, box({512, 0, 0}, {576, 64, 64}), "X");   // apart: inside another brush it would be covered
	const auto* cube = doc.findSolid(other);
	const int top2 = sideFacing(*cube, {0, 0, 1})->id, north = sideFacing(*cube, {0, 1, 0})->id;
	REQUIRE(edit::roundEdge(doc, {other, top2}, {other, north}, 16, 1));
	CHECK(doc.findSolid(other)->sides.size() == 7);
}

namespace {

/// Whether any brush of the world holds the point.
bool filled(const MapDocument& doc, const Vec3& p) {
	return std::any_of(doc.world.solids.begin(), doc.world.solids.end(), [&](const Solid& solid) {
		return std::all_of(solid.sides.begin(), solid.sides.end(), [&](const Side& side) {
			const Plane plane = side.plane();
			return dot(plane.normal, p) - plane.dist <= 0.01;
		});
	});
}

bool allValid(const MapDocument& doc) {
	return std::all_of(doc.world.solids.begin(), doc.world.solids.end(), [](const Solid& s) { return isConvexValid(s) && solidVolume(s) > 1; });
}

} // namespace

TEST_CASE("round edge: across two brushes of an L, with the preview, clamped radius and undo") {
	// Julius (30.10.2026): the outer corner of an L of two walls could not be rounded ("do not
	// share an edge of one brush").
	MapDocument doc;
	auto [c1, along] = edit::createBlock(doc, box({0, 0, 0}, {256, 16, 128}), "WALL");
	auto [c2, across] = edit::createBlock(doc, box({0, 16, 0}, {16, 256, 128}), "WALL");
	const FaceRef south{along, sideFacing(*doc.findSolid(along), {0, -1, 0})->id};
	const FaceRef west{across, sideFacing(*doc.findSolid(across), {-1, 0, 0})->id};
	const std::string before = doc.serialize();

	// The plan changes nothing and uses its own ids (preview).
	MapDocument scratch;
	const auto preview = planEdgeRound(doc, scratch, south, west, 40, 8);
	REQUIRE(preview.ok());
	CHECK_FALSE(preview.inner);
	CHECK_FALSE(preview.clamped);
	CHECK(preview.replacements.size() == 2);
	CHECK(doc.serialize() == before);

	History history;
	auto change = edit::roundEdge(doc, south, west, 40, 8);
	REQUIRE(change);
	history.push(std::move(*change));
	CHECK(allValid(doc));
	CHECK(doc.world.solids.size() == 2);
	// The outer corner is gone, the arc's middle and the inner corner are still wall.
	CHECK_FALSE(filled(doc, {1, 1, 64}));
	const double middle = 40 - 40 / std::sqrt(2.0);
	CHECK(filled(doc, {middle + 1, middle + 1, 64}));
	CHECK(filled(doc, {15, 15, 64}));
	CHECK(filled(doc, {8, 100, 64}));
	CHECK(filled(doc, {100, 8, 64}));
	history.undo(doc);
	CHECK(doc.serialize() == before);

	// Too big for the 16 units thick walls: made smaller instead of refused.
	const auto big = planEdgeRound(doc, scratch, south, west, 200, 8);
	REQUIRE(big.ok());
	CHECK(big.clamped);
	CHECK(big.radius < 200);
	CHECK(big.radius >= 40);
	REQUIRE(edit::roundEdge(doc, south, west, 200, 8));
	CHECK(allValid(doc));
	CHECK(filled(doc, {15, 15, 64}));   // the walls still meet
}

TEST_CASE("round edge: a platform sunk into a carved floor leaves no hole") {
	// Julius (30.10.2026, screenshot): rounding the corner of a raised platform showed a dark hole
	// next to it. The platform reached down into a floor carved around it; cutting its corner all
	// the way down opened the space the floor had left for it.
	MapDocument doc;
	auto [cp, platform] = edit::createBlock(doc, box({0, 0, -32}, {128, 128, 16}), "PLATFORM");
	for (const auto& [lo, hi] : std::vector<std::pair<Vec3, Vec3>>{{{-256, -256, -32}, {0, 384, 0}},
	                                                                 {{128, -256, -32}, {384, 384, 0}},
	                                                                 {{0, -256, -32}, {128, 0, 0}},
	                                                                 {{0, 128, -32}, {128, 384, 0}}}) {
		auto created = edit::createBlock(doc, box(lo, hi), "FLOOR");
	}
	const FaceRef west{platform, sideFacing(*doc.findSolid(platform), {-1, 0, 0})->id};
	const FaceRef south{platform, sideFacing(*doc.findSolid(platform), {0, -1, 0})->id};
	MapDocument scratch;
	const auto plan = planEdgeRound(doc, scratch, west, south, 40, 8);
	REQUIRE(plan.ok());
	CHECK(plan.keptSquare == 1);
	REQUIRE(edit::roundEdge(doc, west, south, 40, 8));
	CHECK(allValid(doc));
	CHECK(doc.world.solids.size() == 6);   // the platform in two: square in the floor, round above
	CHECK(filled(doc, {1, 1, -16}));        // below the floor: still filled, no hole
	CHECK_FALSE(filled(doc, {1, 1, 8}));    // above: rounded
	CHECK(filled(doc, {64, 64, 8}));
	// The square part's top shows the floor's texture where the rounding left it uncovered.
	bool floorOnTop = false;
	for (const auto& solid : doc.world.solids) {
		const auto* top = sideFacing(solid, {0, 0, 1});
		const auto bounds = buildBrushGeometry(solid).bounds;
		if (top && bounds.min.x >= -0.01 && bounds.max.x <= 128.01 && bounds.max.z == doctest::Approx(0)) floorOnTop = top->material == "FLOOR";
	}
	CHECK(floorOnTop);
}

TEST_CASE("round edge: an inner corner is filled up to the arc") {
	MapDocument doc;
	auto [cf, floor] = edit::createBlock(doc, box({0, 0, -16}, {256, 256, 0}), "FLOOR");
	auto [cw, wall] = edit::createBlock(doc, box({256, 0, -16}, {272, 256, 128}), "WALL");
	const FaceRef top{floor, sideFacing(*doc.findSolid(floor), {0, 0, 1})->id};
	const FaceRef face{wall, sideFacing(*doc.findSolid(wall), {-1, 0, 0})->id};
	MapDocument scratch;
	const auto plan = planEdgeRound(doc, scratch, top, face, 32, 4);
	REQUIRE(plan.ok());
	CHECK(plan.inner);
	CHECK(plan.added.size() == 4);
	REQUIRE(edit::roundEdge(doc, top, face, 32, 4));
	CHECK(doc.world.solids.size() == 6);
	CHECK(allValid(doc));
	CHECK(filled(doc, {254, 128, 2}));            // in the corner
	CHECK_FALSE(filled(doc, {256 - 31, 128, 31}));  // outside the arc
	// Parallel faces cannot make a corner: a reason, no change.
	const FaceRef bottom{floor, sideFacing(*doc.findSolid(floor), {0, 0, -1})->id};
	CHECK_FALSE(planEdgeRound(doc, scratch, top, bottom, 16, 4).ok());
}

TEST_CASE("textures across several faces (treat as one)") {
	MapDocument doc;
	auto [c1, left] = edit::createBlock(doc, box({0, 0, 0}, {128, 16, 128}), "WALL");
	auto [c2, right] = edit::createBlock(doc, box({128, 0, 0}, {256, 16, 128}), "WALL");
	const auto south = [&](const SolidRef& ref) { return FaceRef{ref, sideFacing(*doc.findSolid(ref), {0, -1, 0})->id}; };
	const std::vector<FaceRef> faces{south(left), south(right)};
	const auto size = [](const std::string&) { return std::pair<int, int>{512, 512}; };
	(void)edit::justifyFaces(doc, faces, edit::Justify::Fit, true, size);
	// One texture over both faces: u = 0 at the left end of the first face, 512 at the right end of the second.
	const auto* a = sideFacing(*doc.findSolid(left), {0, -1, 0});
	const auto* b = sideFacing(*doc.findSolid(right), {0, -1, 0});
	const auto u = [](const Side& side, Vec3 p) { return textureCoordinate(side, p).u; };
	const double start = std::min(u(*a, {0, 0, 0}), u(*a, {128, 0, 0}));
	const double end = std::max(u(*b, {128, 0, 0}), u(*b, {256, 0, 0}));
	CHECK(std::fmod(start + 5120, 512.0) == doctest::Approx(0).epsilon(0.001));
	CHECK(end - start == doctest::Approx(512));
	CHECK(u(*a, {128, 0, 64}) == doctest::Approx(u(*b, {128, 0, 64})));   // seamless where they meet
	// Without "treat as one" each face gets the whole texture.
	(void)edit::justifyFaces(doc, faces, edit::Justify::Fit, false, size);
	a = sideFacing(*doc.findSolid(left), {0, -1, 0});
	CHECK(std::abs(u(*a, {128, 0, 0}) - u(*a, {0, 0, 0})) == doctest::Approx(512));
}

TEST_CASE("vertex tool keeps brushes convex") {
	MapDocument doc;
	const Solid cube = makeBlock(doc, box({0, 0, 0}, {64, 64, 64}), "X");
	const auto top = sideFacing(cube, {0, 0, 1})->id;
	// Pull one top edge up: a sloped roof, the top side keeps its id.
	const auto roof = moveVertices(doc, cube, {{0, 0, 64}, {64, 0, 64}}, {0, 0, 32});
	REQUIRE(roof);
	CHECK(isConvexValid(roof->solid));
	CHECK_FALSE(roof->cornersLost);
	CHECK(solidVolume(roof->solid) == doctest::Approx(64.0 * 64 * 64 + 64.0 * 64 * 32 / 2));
	CHECK(std::any_of(roof->solid.sides.begin(), roof->solid.sides.end(), [&](const Side& s) { return s.id == top; }));
	// Pushing a corner inside folds it away: the brush stays convex, a corner is lost.
	const auto folded = moveVertices(doc, cube, {{64, 64, 64}}, {-48, -48, -48});
	REQUIRE(folded);
	CHECK(folded->cornersLost);
	CHECK(isConvexValid(folded->solid));
	CHECK(solidCorners(cube).size() == 8);
}

TEST_CASE("cordons, track chains, drop to floor") {
	MapDocument doc;
	auto [c1, near] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "X");
	auto [c2, far] = edit::createBlock(doc, box({2000, 0, 0}, {2064, 64, 64}), "X");
	CordonSet cordons;
	cordons.active = true;
	cordons.cordons.push_back({"main", box({-128, -128, -128}, {256, 256, 256}), true});
	writeCordons(doc, cordons);
	const auto read = readCordons(doc);
	REQUIRE(read.cordons.size() == 1);
	CHECK(read.active);
	CHECK(read.cordons[0].name == "main");
	CHECK(read.cordons[0].box.max.x == doctest::Approx(256));
	// Save/load keeps them.
	const auto reloaded = MapDocument::parse(doc.serialize());
	CHECK(readCordons(reloaded).cordons.size() == 1);
	const auto cut = cordonedCopy(doc, read, nullptr);
	CHECK(cut.world.solids.size() == 1 + 6);   // the near block and six sealing slabs
	CHECK(doc.world.solids.size() == 2);        // the map itself is untouched
	// The old single-cordon format still loads.
	MapDocument old;
	auto block = KvNode::makeBlock("cordon");
	block.set("mins", "(0 0 0)");
	block.set("maxs", "(64 64 64)");
	block.set("active", "1");
	old.footerBlocks.push_back(block);
	CHECK(readCordons(old).active);

	// A loop of path_tracks and a train that follows it.
	const auto track = [&](const char* name, const char* target) {
		Entity e;
		e.id = doc.newEntityId();
		e.set("classname", "path_track");
		e.set("targetname", name);
		e.set("target", target);
		e.setOrigin({0, 0, 0});
		doc.entities.push_back(e);
		return e.id;
	};
	const int a = track("p1", "p2"), b = track("p2", "p3"), c = track("p3", "p1");
	track("other", "");
	const auto chain = trackChain(doc, b);
	CHECK(chain == std::vector<int>{a, b, c});

	// Drop to floor: a crate floating above the block lands on it.
	auto [c3, crate] = edit::createBlock(doc, box({8, 8, 200}, {40, 40, 232}), "X");
	Selection selection;
	selection.solids.push_back(crate);
	(void)edit::dropToFloor(doc, selection, nullptr);
	const auto landed = buildBrushGeometry(*doc.findSolid(crate)).bounds;
	CHECK(landed.min.z == doctest::Approx(64));
}

TEST_CASE("brush entities drawn with the entity tool") {
	MapDocument doc;
	CHECK(edit::brushEntityMaterial("trigger_multiple") == "tools/toolstrigger");
	CHECK(edit::brushEntityMaterial("func_areaportal") == "tools/toolsareaportal");
	CHECK(edit::brushEntityMaterial("func_door").empty());
	ShapeOptions options;
	auto [change, id] = edit::createBrushEntity(doc, box({0, 0, 0}, {128, 64, 96}), options, "trigger_once", nullptr, "tools/toolstrigger");
	REQUIRE(id != 0);
	REQUIRE(doc.entities.size() == 1);
	const auto& entity = doc.entities.front();
	CHECK(entity.classname() == "trigger_once");
	REQUIRE(entity.solids.size() == 1);
	CHECK(solidVolume(entity.solids.front()) == doctest::Approx(128 * 64 * 96));
	CHECK(entity.solids.front().sides.front().material == "tools/toolstrigger");
	CHECK(entity.solids.front().id != entity.id);
	CHECK(change.name == "Create trigger_once");
	CHECK(doc.world.solids.empty());
}

TEST_CASE("editor sprites of entities") {
	const auto dir = std::filesystem::temp_directory_path() / "sme-sprite-test";
	std::filesystem::create_directories(dir);
	const auto fgd = dir / "sprites.fgd";
	{
		std::ofstream out(fgd);
		out << "@PointClass iconsprite(\"editor/Light.vmt\") = light : \"Light\" []\n"
		       "@PointClass sprite() = env_sprite : \"Sprite\" [ model(sprite) : \"Sprite\" : \"sprites/glow01.spr\" ]\n"
		       "@PointClass studio(\"models/editor/playerstart.mdl\") = info_player_start : \"Start\" []\n";
	}
	EntityCatalog catalog;
	REQUIRE(catalog.load({fgd}));
	CHECK(edit::classSprite(*catalog.find("light")) == "editor/light");
	Entity glow;
	glow.set("classname", "env_sprite");
	glow.set("model", "sprites/glow01.spr");
	CHECK(edit::entitySprite(glow, &catalog) == "sprites/glow01");
	Entity start;
	start.set("classname", "info_player_start");
	CHECK(edit::entitySprite(start, &catalog).empty());   // shown with its model instead
	std::filesystem::remove_all(dir);
}

TEST_CASE("cubemap room boxes (parallax correction)") {
	MapDocument doc;
	ShapeOptions block;
	// A room 512 x 384 x 256 inside walls 16 thick, a trigger in it that must not count.
	for (auto solid : makeShape(doc, box({-16, -16, -16}, {528, 400, 272}), block, "BRICK/WALL")) {
		doc.world.solids.push_back(std::move(solid));
	}
	Selection all;
	all.solids.push_back({0, doc.world.solids.front().id});
	(void)edit::hollow(doc, all, 16);
	auto [triggerChange, triggerId] = edit::createBrushEntity(doc, box({100, 100, 0}, {140, 140, 64}), block, "trigger_once", nullptr, "TOOLS/TOOLSTRIGGER");
	REQUIRE(triggerId != 0);
	auto [placed, cubemapId] = edit::placeEntity(doc, "env_cubemap", {128, 96, 64}, nullptr);
	const Bounds room = roomAround(doc, {128, 96, 64});
	CHECK(room.min.x == doctest::Approx(0));
	CHECK(room.max.x == doctest::Approx(512));
	CHECK(room.min.y == doctest::Approx(0));
	CHECK(room.max.y == doctest::Approx(384));
	CHECK(room.min.z == doctest::Approx(0));
	CHECK(room.max.z == doctest::Approx(256));

	auto [change, boxId] = edit::setCubemapRoomBox(doc, cubemapId, room, nullptr);
	REQUIRE(boxId != 0);
	const Entity* cubemap = doc.findEntity(cubemapId);
	const Entity* obb = cubemapRoomBox(doc, *cubemap);
	REQUIRE(obb);
	CHECK(obb->id == boxId);
	CHECK(obb->classname() == "parallax_obb");
	CHECK(cubemap->get("parallaxobb") == obb->get("targetname"));
	REQUIRE(obb->solids.size() == 1);
	CHECK(obb->solids.front().sides.size() == 6);   // one box brush, as Mapbase reads it
	CHECK(solidVolume(obb->solids.front()) == doctest::Approx(512.0 * 384 * 256));

	// Resizing keeps the same entity and name.
	const std::string name = obb->get("targetname");
	auto [resize, sameId] = edit::setCubemapRoomBox(doc, cubemapId, box({0, 0, 0}, {256, 384, 128}), nullptr);
	CHECK(sameId == boxId);
	CHECK(cubemapRoomBox(doc, *doc.findEntity(cubemapId))->get("targetname") == name);
	CHECK(solidVolume(doc.findEntity(boxId)->solids.front()) == doctest::Approx(256.0 * 384 * 128));
	// Its own box does not count as a wall when fitting again.
	CHECK(roomAround(doc, {128, 96, 64}).max.z == doctest::Approx(256));

	const auto removed = edit::removeCubemapRoomBox(doc, cubemapId);
	CHECK_FALSE(removed.empty());
	CHECK_FALSE(doc.findEntity(boxId));
	CHECK(doc.findEntity(cubemapId)->get("parallaxobb").empty());
}
