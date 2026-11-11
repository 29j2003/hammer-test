// test_displacement.cpp - Displacements: create, dispinfo as Hammer writes it, sculpting, sewing, undo.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/displacement.h>
#include <sme/history.h>
#include <sme/checks.h>
#include <sme/shapes.h>
#include <sme/csg.h>
#include <sme/editing.h>

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

/// The side of the brush whose face points up.
FaceRef topFace(const MapDocument& doc, const SolidRef& ref) {
	const Solid* solid = doc.findSolid(ref);
	for (const auto& face : buildBrushGeometry(*solid).faces) {
		if (face.normal.z > 0.9) return {ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id};
	}
	return {};
}

const Side& sideOf(const MapDocument& doc, const FaceRef& ref) {
	for (const auto& side : doc.findSolid(ref.solid)->sides) {
		if (side.id == ref.sideId) return side;
	}
	FAIL("side missing");
	return doc.findSolid(ref.solid)->sides.front();
}

FaceGeometry faceOf(const MapDocument& doc, const FaceRef& ref) {
	const Solid* solid = doc.findSolid(ref.solid);
	for (const auto& face : buildBrushGeometry(*solid).faces) {
		if (solid->sides[static_cast<std::size_t>(face.sideIndex)].id == ref.sideId) return face;
	}
	return {};
}

} // namespace

TEST_CASE("displacement: create writes Hammer's dispinfo, renders flat, survives save and load, undo removes it") {
	MapDocument doc;
	History history;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "NATURE/BLENDGROUNDTOGRASS");
	history.push(std::move(create));
	const FaceRef top = topFace(doc, ref);
	REQUIRE(top.sideId != 0);

	int skipped = -1;
	history.push(edit::createDisplacements(doc, {top}, 3, 0, &skipped));
	CHECK(skipped == 0);
	const KvNode* info = sideOf(doc, top).dispInfo();
	REQUIRE(info);
	CHECK(info->get("power") == "3");
	CHECK(info->get("startposition") == "[0 0 16]");
	CHECK(info->get("elevation") == "0");
	CHECK(info->get("subdiv") == "0");
	// Children in Hammer's order.
	std::vector<std::string> order;
	for (const auto& child : info->children) order.push_back(child.key);
	CHECK(order == std::vector<std::string>{"power", "startposition", "flags", "elevation", "subdiv", "normals", "distances", "offsets",
	                                       "offset_normals", "alphas", "triangle_tags", "allowed_verts"});
	CHECK(info->find("normals")->children.size() == 9);
	CHECK(info->find("normals")->get("row0") == "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1 0 0 1 0 0 1 0 0 1 0 0 1");
	CHECK(info->find("distances")->get("row4") == "0 0 0 0 0 0 0 0 0");
	CHECK(info->find("triangle_tags")->children.size() == 8);
	CHECK(info->find("triangle_tags")->get("row0") == "9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9");
	CHECK(info->find("allowed_verts")->get("10") == "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1");

	// The renderer's mesh: a flat 9x9 grid over the top face.
	const auto mesh = buildDisplacement(sideOf(doc, top), faceOf(doc, top));
	REQUIRE(mesh);
	CHECK(mesh->size == 9);
	for (const auto& p : mesh->positions) CHECK(p.z == doctest::Approx(16));

	// A face that already is a displacement is skipped; a five-sided face can't be one.
	history.push(edit::createDisplacements(doc, {top}, 2, 0, &skipped));
	CHECK(skipped == 1);
	CHECK(sideOf(doc, top).dispInfo()->get("power") == "3");

	// Save and load: identical dispinfo, and saving again gives identical text.
	const std::string text = doc.serialize();
	auto loaded = MapDocument::parse(text);
	CHECK(loaded.serialize() == text);
	const auto again = readDisplacement(sideOf(loaded, top));
	REQUIRE(again);
	CHECK(again->size() == 9);
	CHECK(again->triangleTags.size() == 128);

	history.undo(doc);   // the skipped one (no change was recorded) - or the create
	if (sideOf(doc, top).dispInfo()) history.undo(doc);
	CHECK_FALSE(sideOf(doc, top).dispInfo());
}

TEST_CASE("displacement: sculpt raise, lower, smooth, flatten, noise, alpha; destroy") {
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "NATURE/BLENDGROUNDTOGRASS");
	const FaceRef top = topFace(doc, ref);
	(void)edit::createDisplacements(doc, {top}, 3);
	const FaceGeometry face = faceOf(doc, top);
	auto d = *readDisplacement(sideOf(doc, top));
	const std::size_t middle = 4 * 9 + 4;

	SculptBrush brush;
	brush.center = {128, 128, 16};
	brush.radius = 100;
	brush.strength = 10;
	CHECK(sculpt(d, face, SculptMode::Raise, brush));
	CHECK(d.distances[middle] == doctest::Approx(10));
	CHECK(d.normals[middle] == Vec3{0, 0, 1});
	CHECK(d.distances[0] == doctest::Approx(0));   // outside the circle
	const auto raised = displacementPositions(d, face);
	CHECK(raised[middle].z == doctest::Approx(26));
	// Falloff: halfway out less than in the middle.
	CHECK(raised[4 * 9 + 6].z < raised[middle].z);
	CHECK(raised[4 * 9 + 6].z > 16);
	// The steep middle loses its walkable tags? Gentle slope stays walkable.
	CHECK(d.triangleTags[(3 * 8 + 3) * 2] != 0);

	brush.invert = true;
	CHECK(sculpt(d, face, SculptMode::Raise, brush));
	brush.invert = false;
	CHECK(d.distances[middle] == doctest::Approx(0).epsilon(1e-6));

	// Smooth pulls a spike towards its neighbours.
	d.setVector(middle, {0, 0, 40});
	brush.strength = 0.5;
	brush.radius = 20;   // only the middle vertex is inside (vertices are 32 apart)
	CHECK(sculpt(d, face, SculptMode::Smooth, brush));
	CHECK(d.vector(middle).z == doctest::Approx(20));   // half way to its flat neighbours

	// Flatten moves towards the height.
	d.setVector(middle, {0, 0, 30});
	brush.flattenHeight = 8;
	brush.strength = 1;
	CHECK(sculpt(d, face, SculptMode::Flatten, brush));
	CHECK(d.vector(middle).z == doctest::Approx(8));

	// Noise changes heights within the strength, the same way for the same seed.
	auto n1 = d, n2 = d;
	brush.radius = 300;
	brush.strength = 5;
	brush.seed = 7;
	CHECK(sculpt(n1, face, SculptMode::Noise, brush));
	CHECK(sculpt(n2, face, SculptMode::Noise, brush));
	for (std::size_t i = 0; i < n1.distances.size(); ++i) {
		CHECK(n1.vector(i).z == doctest::Approx(n2.vector(i).z));
		CHECK(std::abs(n1.vector(i).z - d.vector(i).z) <= 5.0001);
	}

	// Alpha painting: up to 255, erasing back to 0.
	brush.strength = 300;
	CHECK(sculpt(d, face, SculptMode::PaintAlpha, brush));
	CHECK(d.alphas[middle] == doctest::Approx(255));
	brush.invert = true;
	CHECK(sculpt(d, face, SculptMode::PaintAlpha, brush));
	CHECK(d.alphas[middle] == doctest::Approx(0));

	// Stored back and read again: the same numbers.
	Side side = sideOf(doc, top);
	storeDisplacement(side, d);
	const auto back = readDisplacement(side);
	REQUIRE(back);
	for (std::size_t i = 0; i < d.distances.size(); ++i) CHECK(back->vector(i).z == doctest::Approx(d.vector(i).z).epsilon(0.001));

	// Destroy turns it back into a flat face.
	History history;
	history.push(edit::destroyDisplacements(doc, {top}));
	CHECK_FALSE(sideOf(doc, top).dispInfo());
	history.undo(doc);
	CHECK(sideOf(doc, top).dispInfo());
}

TEST_CASE("displacement: elevation and sewing two neighbours") {
	MapDocument doc;
	auto [a, refA] = edit::createBlock(doc, box({0, 0, 0}, {128, 128, 16}), "DEV/DEV_MEASUREGENERIC01");
	auto [b, refB] = edit::createBlock(doc, box({128, 0, 0}, {256, 128, 16}), "DEV/DEV_MEASUREGENERIC01");
	const FaceRef topA = topFace(doc, refA), topB = topFace(doc, refB);
	(void)edit::createDisplacements(doc, {topA}, 2, 8);
	(void)edit::createDisplacements(doc, {topB}, 2);
	CHECK(displacementPositions(*readDisplacement(sideOf(doc, topA)), faceOf(doc, topA))[0].z == doctest::Approx(24));

	// The shared edge x = 128 has a step of 8: sewing meets in the middle.
	int moved = 0;
	History history;
	history.push(edit::sewDisplacements(doc, edit::displacementFaces(doc, Selection{{refA, refB}, {}, {}}), &moved));
	CHECK(moved == 10);   // five vertices on each side
	const auto pa = displacementPositions(*readDisplacement(sideOf(doc, topA)), faceOf(doc, topA));
	const auto pb = displacementPositions(*readDisplacement(sideOf(doc, topB)), faceOf(doc, topB));
	int shared = 0;
	for (const auto& p : pa) {
		for (const auto& q : pb) {
			if (std::abs(p.x - q.x) < 0.01 && std::abs(p.y - q.y) < 0.01) {
				CHECK(p.z == doctest::Approx(q.z));
				CHECK(p.z == doctest::Approx(20));
				++shared;
			}
		}
	}
	CHECK(shared == 5);
	history.undo(doc);
	CHECK(displacementPositions(*readDisplacement(sideOf(doc, topB)), faceOf(doc, topB))[0].z == doctest::Approx(16));
	CHECK(sculptFalloff(0, 10) == doctest::Approx(1));
	CHECK(sculptFalloff(10, 10) == doctest::Approx(0));
}

TEST_CASE("displacement: an imported dispinfo is read, and an untouched store keeps its numbers") {
	// dispinfo as Hammer writes it (power 2), on the top face of a 64 block.
	const std::string vmf = R"vmf(world
{
	"id" "1"
	"classname" "worldspawn"
	solid
	{
		"id" "2"
		side
		{
			"id" "1"
			"plane" "(0 0 64) (0 64 64) (64 64 64)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
			dispinfo
			{
				"power" "2"
				"startposition" "[0 0 64]"
				"flags" "0"
				"elevation" "0"
				"subdiv" "0"
				normals
				{
					"row0" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row1" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row2" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row3" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row4" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
				}
				distances
				{
					"row0" "0 0 0 0 0"
					"row1" "0 0 0 0 0"
					"row2" "0 0 12.5 0 0"
					"row3" "0 0 0 0 0"
					"row4" "0 0 0 0 0"
				}
				alphas
				{
					"row0" "0 0 0 0 0"
					"row1" "0 0 0 0 0"
					"row2" "0 0 255 0 0"
					"row3" "0 0 0 0 0"
					"row4" "0 0 0 0 0"
				}
			}
		}
		side
		{
			"id" "2"
			"plane" "(0 64 0) (0 0 0) (64 0 0)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "3"
			"plane" "(0 0 0) (0 64 0) (0 64 64)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "4"
			"plane" "(64 64 0) (64 0 0) (64 0 64)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "5"
			"plane" "(0 64 0) (64 64 0) (64 64 64)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "6"
			"plane" "(64 0 0) (0 0 0) (0 0 64)"
			"material" "DEV/DEV_BLENDMEASURE"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
	}
}
)vmf";
	auto doc = MapDocument::parse(vmf);
	const Side& side = doc.world.solids.front().sides.front();
	const auto d = readDisplacement(side);
	REQUIRE(d);
	CHECK(d->power == 2);
	CHECK(d->distances[12] == doctest::Approx(12.5));
	CHECK(d->alphas[12] == doctest::Approx(255));
	CHECK(d->offsets[12] == Vec3{});   // missing blocks read as zero
	// Stored without a change: the rows it had keep their exact text; missing blocks are added.
	Side copy = side;
	storeDisplacement(copy, *d);
	CHECK(copy.dispInfo()->find("distances")->get("row2") == "0 0 12.5 0 0");
	CHECK(copy.dispInfo()->find("normals")->get("row0") == side.dispInfo()->find("normals")->get("row0"));
	CHECK(copy.dispInfo()->find("triangle_tags"));
	const auto again = readDisplacement(copy);
	REQUIRE(again);
	CHECK(again->distances == d->distances);
	CHECK(again->alphas == d->alphas);
}

TEST_CASE("displacement: stretching and turning a displacement brush keeps its start corner and turns its normals") {
	// Julius' report (01.11.2026): a blend wall copied and stretched kept the old start position,
	// 672 units off its displaced face.
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {576, 16, 336}), "DEV/DEV_BLENDMEASURE2");
	(void)create;
	FaceRef front;
	{
		const Solid* solid = doc.findSolid(ref);
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			if (face.normal.y > 0.9) front = {ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id};
		}
	}
	REQUIRE(front.sideId != 0);
	(void)edit::createDisplacements(doc, {front}, 3);
	CHECK(sideOf(doc, front).dispInfo()->get("startposition") == "[0 16 0]");

	// Stretched along y: the displaced face (y = 16) moves to y = 688, the start corner with it.
	Solid* solid = doc.findSolid(ref);
	REQUIRE(scaleSolid(*solid, box({0, 0, 0}, {576, 16, 336}), box({0, 0, 0}, {576, 688, 336})));
	CHECK(sideOf(doc, front).dispInfo()->get("startposition") == "[0 688 0]");
	const auto corners = displacementCorners(faceOf(doc, front), Vec3{0, 688, 0});
	CHECK(std::any_of(corners.begin(), corners.end(), [](const Vec3& c) { return length(c - Vec3{0, 688, 0}) < 1e-6; }));
	auto mesh = buildDisplacement(sideOf(doc, front), faceOf(doc, front));
	REQUIRE(mesh);
	for (const auto& p : mesh->positions) CHECK(p.y == doctest::Approx(688));

	// Turned 90° about z: the start corner and the normals turn too.
	rotateSolid(*solid, Mat3::rotation(2, 90.0), Vec3{0, 0, 0}, false);
	Vec3 start;
	const auto text = sideOf(doc, front).dispInfo()->get("startposition");
	REQUIRE(parseVec3(std::string_view(text).substr(1, text.size() - 2), start));
	CHECK(start.x == doctest::Approx(-688));
	CHECK(start.y == doctest::Approx(0).epsilon(1e-6));
	const auto normals = sideOf(doc, front).dispInfo()->find("normals")->get("row0");
	Vec3 n;
	REQUIRE(parseVec3(normals.substr(0, normals.find(' ', normals.find(' ', normals.find(' ') + 1) + 1)), n));
	CHECK(n.x == doctest::Approx(-1));
	mesh = buildDisplacement(sideOf(doc, front), faceOf(doc, front));
	REQUIRE(mesh);
	for (const auto& p : mesh->positions) CHECK(p.x == doctest::Approx(-688));
}

TEST_CASE("displacement sculpt: Raise along another axis pushes sideways; Flatten along it levels to the stroke start") {
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "NATURE/GRASS");
	(void)create;
	const FaceRef top = topFace(doc, ref);
	const FaceGeometry face = faceOf(doc, top);
	auto d = makeDisplacement(face, 2);
	REQUIRE(d);
	const auto before = displacementPositions(*d, face);
	SculptBrush brush;
	brush.center = {128, 128, 16};
	brush.radius = 100;
	brush.strength = 10;
	brush.axis = {1, 0, 0};
	REQUIRE(sculpt(*d, face, SculptMode::Raise, brush));
	const auto after = displacementPositions(*d, face);
	bool movedX = false;
	for (std::size_t i = 0; i < after.size(); ++i) {
		CHECK(after[i].z == doctest::Approx(before[i].z));
		CHECK(after[i].y == doctest::Approx(before[i].y));
		movedX = movedX || after[i].x > before[i].x + 1;
	}
	CHECK(movedX);
	// Flatten along X towards x = 128: the middle vertex moves back onto x = 128.
	brush.flattenPoint = {128, 128, 16};
	brush.strength = 1;
	for (int k = 0; k < 20; ++k) sculpt(*d, face, SculptMode::Flatten, brush);
	const auto flat = displacementPositions(*d, face);
	CHECK(flat[12].x == doctest::Approx(128).epsilon(0.01));   // the centre of a 5 x 5 grid
	// No axis: as before, along the face normal.
	auto e = makeDisplacement(face, 2);
	SculptBrush up;
	up.center = {128, 128, 16};
	up.radius = 100;
	up.strength = 10;
	REQUIRE(sculpt(*e, face, SculptMode::Raise, up));
	CHECK(displacementPositions(*e, face)[12].z == doctest::Approx(26));
}

TEST_CASE("blend along selection: alpha by distance to the road, soft border, hand paint kept, one undo step") {
	MapDocument doc;
	History history;
	auto [ground, groundRef] = edit::createBlock(doc, box({0, 0, 0}, {512, 512, 16}), "NATURE/BLENDGRASSDIRT");
	history.push(std::move(ground));
	const FaceRef top = topFace(doc, groundRef);
	history.push(edit::createDisplacements(doc, {top}, 3));
	auto [road, roadRef] = edit::createBlock(doc, box({0, 0, 16}, {64, 512, 24}), "ROAD/ASPHALT");
	history.push(std::move(road));
	// Hand paint far from the road stays.
	{
		Solid* solid = doc.findSolid(groundRef);
		for (auto& side : solid->sides) {
			if (side.id != top.sideId) continue;
			auto d = readDisplacement(side);
			const auto at = displacementPositions(*d, faceOf(doc, top));
			for (std::size_t i = 0; i < at.size(); ++i) {
				if (std::abs(at[i].x - 512) < 0.01 && std::abs(at[i].y) < 0.01) d->alphas[i] = 200;   // the far corner
			}
			storeDisplacement(side, *d);
		}
	}
	CHECK(distanceToBrush(buildBrushGeometry(*doc.findSolid(roadRef)), {128, 100, 16}) == doctest::Approx(64));
	CHECK(distanceToBrush(buildBrushGeometry(*doc.findSolid(roadRef)), {10, 100, 20}) == doctest::Approx(0));
	CHECK(distanceToBrush(buildBrushGeometry(*doc.findSolid(roadRef)), {100, 600, 24}) == doctest::Approx(std::hypot(36.0, 88.0)));

	BlendNearOptions options;
	options.width = 32;
	options.softness = 64;
	Selection selection;
	selection.solids = {roadRef};
	int count = 0;
	history.push(edit::blendAlongSelection(doc, selection, options, &count));
	CHECK(count == 1);
	const auto d = readDisplacement(sideOf(doc, top));
	REQUIRE(d);
	// Vertices every 64 units along x (columns run along the face's second corner direction; find by position).
	const auto positions = displacementPositions(*d, faceOf(doc, top));
	for (std::size_t i = 0; i < positions.size(); ++i) {
		const double x = positions[i].x;
		if (x <= 64.01) {
			CHECK(d->alphas[i] == doctest::Approx(255));
		} else if (std::abs(x - 128) < 0.01) {
			CHECK(d->alphas[i] == doctest::Approx(127.5));
		} else if (x >= 191.99 && std::abs(x - 512) > 0.01) {
			CHECK(d->alphas[i] == doctest::Approx(0));
		}
	}
	bool handPaintKept = false;
	for (std::size_t i = 0; i < positions.size(); ++i) {
		if (std::abs(positions[i].x - 512) < 0.01 && d->alphas[i] == doctest::Approx(200)) handPaintKept = true;
	}
	CHECK(handPaintKept);
	CHECK(history.undo(doc));
	const auto undone = readDisplacement(sideOf(doc, top));
	CHECK(std::count_if(undone->alphas.begin(), undone->alphas.end(), [](double a) { return a > 0; }) == 1);
	// Nothing selected but displacements: nothing to measure from.
	Selection none;
	none.faces = {top};
	CHECK(edit::blendAlongSelection(doc, none, options, &count).empty());
}

TEST_CASE("blend along selection: the first material near the road when asked, noise frays the border") {
	BlendNearOptions o;
	o.width = 0;
	o.softness = 100;
	o.secondMaterialNear = false;
	CHECK(blendNearAlpha(0, o) == doctest::Approx(0));
	CHECK(blendNearAlpha(50, o) == doctest::Approx(127.5));
	CHECK(blendNearAlpha(200, o) == doctest::Approx(255));
	o.secondMaterialNear = true;
	o.softness = 0;
	o.width = 10;
	CHECK(blendNearAlpha(10, o) == doctest::Approx(255));
	CHECK(blendNearAlpha(10.5, o) == doctest::Approx(0));
}

TEST_CASE("displacement: cutting a displacement brush never leaves a face without four corners (VBSP: non-quad displacement)") {
	// Julius' map of 11.11.2026: pieces of his blend floor, cut by round shapes and by the road
	// builder, kept the floor's dispinfo on three- and five-cornered faces - VBSP stopped.
	MapDocument doc;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, -16}, {512, 512, 16}), "NATURE/BLENDGROUNDTOGRASS");
	const FaceRef top = topFace(doc, ref);
	(void)edit::createDisplacements(doc, {top}, 3);
	REQUIRE(sideOf(doc, top).dispInfo());
	const Solid floor = *doc.findSolid(ref);

	// A round post through the floor (a cylinder of 12 sides) carves a hole whose pieces have
	// slanted edges: none of them may keep the grid on a face that is not four-cornered.
	ShapeOptions round;
	round.kind = ShapeOptions::Kind::Cylinder;
	round.sides = 12;
	const auto cutters = makeShape(doc, box({100, 140, -64}, {260, 300, 64}), round, "TOOLS/TOOLSNODRAW");
	REQUIRE(!cutters.empty());
	std::vector<Solid> pieces{floor};
	for (const auto& cutter : cutters) {
		std::vector<Solid> next;
		for (const auto& piece : pieces) {
			for (auto& part : subtractSolid(doc, piece, cutter)) next.push_back(std::move(part));
		}
		pieces = std::move(next);
	}
	REQUIRE(pieces.size() > 2);
	int kept = 0;
	for (const auto& piece : pieces) {
		for (const auto& face : buildBrushGeometry(piece).faces) {
			const Side& side = piece.sides[static_cast<std::size_t>(face.sideIndex)];
			if (!side.dispInfo()) continue;
			++kept;
			CHECK(face.vertices.size() == 4);
		}
	}
	// A cut through the face drops the grid (it belonged to the whole face).
	CHECK(kept == 0);

	// A plane that does not touch the top face keeps it (only the bottom half is cut away).
	auto [upper, lower] = splitSolid(doc, floor, Plane{{0, 0, 1}, 0}, floor.sides.front());
	REQUIRE(upper);
	REQUIRE(lower);
	const auto stillDisplaced = [](const Solid& s) { return std::any_of(s.sides.begin(), s.sides.end(), [](const Side& side) { return side.dispInfo() != nullptr; }); };
	CHECK(stillDisplaced(*upper));
	CHECK_FALSE(stillDisplaced(*lower));
}

TEST_CASE("displacement: non-quad displacements are found, made flat in one step, and an edit never leaves one") {
	MapDocument doc;
	History history;
	auto [create, ref] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "NATURE/BLENDGROUNDTOGRASS");
	history.push(std::move(create));
	const FaceRef top = topFace(doc, ref);
	history.push(edit::createDisplacements(doc, {top}, 2));
	CHECK(nonQuadDisplacements(doc).empty());
	// Cut a corner off by editing the planes directly (what the old cutting did): the top now has five corners.
	const std::string before = doc.serialize();
	{
		Transaction t(doc, "Cut a corner");
		t.touchSolid(ref);
		Solid* solid = doc.findSolid(ref);
		Side corner = solid->sides.front();
		corner.id = doc.newSideId();
		corner.extra.clear();
		corner.planePoints = {Vec3{256, 200, 16}, Vec3{200, 256, 16}, Vec3{200, 256, 0}};
		solid->sides.push_back(corner);
		Change change = t.finish();
		const auto bad = nonQuadDisplacements(doc);
		REQUIRE(bad.size() == 1);
		CHECK(bad.front().corners == 5);
		CHECK(bad.front().face == top);
		// The check reports it as an error with a fix.
		CheckContext context;
		const auto problems = checkMap(doc, context);
		const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::NonQuadDisplacement; });
		REQUIRE(it != problems.end());
		CHECK(it->severity == Problem::Severity::Error);
		CHECK(it->objects.faces.size() == 1);
		CHECK(it->message.find("non-quad") != std::string::npos);
		// The commit guard drops it and the change's after state follows (redo gives the flat face).
		CHECK(dropNonQuadDisplacements(doc, change) == 1);
		CHECK(nonQuadDisplacements(doc).empty());
		REQUIRE(change.solids.size() == 1);
		REQUIRE(change.solids.front().after);
		const auto& after = *change.solids.front().after;
		CHECK(std::none_of(after.sides.begin(), after.sides.end(), [](const Side& s) { return s.dispInfo() != nullptr; }));
		history.push(std::move(change));
	}
	history.undo(doc);
	CHECK(doc.serialize() == before);
	CHECK(sideOf(doc, top).dispInfo());
	history.redo(doc);
	CHECK(nonQuadDisplacements(doc).empty());
	CHECK_FALSE(sideOf(doc, top).dispInfo());

	// The autofix on its own: one undo step, only the listed faces, four-cornered ones stay.
	history.undo(doc);
	{
		Transaction t(doc, "Cut a corner again");
		t.touchSolid(ref);
		Solid* solid = doc.findSolid(ref);
		Side corner = solid->sides.front();
		corner.id = doc.newSideId();
		corner.extra.clear();
		corner.planePoints = {Vec3{256, 200, 16}, Vec3{200, 256, 16}, Vec3{200, 256, 0}};
		solid->sides.push_back(corner);
		history.push(t.finish());
	}
	auto [other, otherRef] = edit::createBlock(doc, box({512, 0, 0}, {768, 256, 16}), "NATURE/BLENDGROUNDTOGRASS");
	history.push(std::move(other));
	const FaceRef good = topFace(doc, otherRef);
	history.push(edit::createDisplacements(doc, {good}, 2));
	int count = -1;
	Change fix = edit::fixNonQuadDisplacements(doc, {}, &count);
	CHECK(count == 1);
	CHECK_FALSE(fix.empty());
	CHECK(nonQuadDisplacements(doc).empty());
	CHECK(sideOf(doc, good).dispInfo());
	// The file still reads back the same.
	const std::string text = doc.serialize();
	CHECK(MapDocument::parse(text).serialize() == text);
}

TEST_CASE("displacement: a displacement too large for its lightmap is found, gets a fitting lightmap scale, and an edit never leaves one") {
	// The blend floor of Julius' report 1523: 2082.6 x 400 units, V along X, lightmap scale 16.
	// VBSP: "Bad surface extents ... (dimension: 1, 131>126)".
	MapDocument doc;
	History history;
	auto [create, ref] = edit::createBlock(doc, box({-3136, -1128, -32}, {-1053.3638216927648, -728, 8}), "CONCRETE/BLENDROAD_BROKEN01");
	history.push(std::move(create));
	const FaceRef top = topFace(doc, ref);
	{
		Transaction t(doc, "Align");
		t.touchSolid(ref);
		for (auto& side : doc.findSolid(ref)->sides) {
			if (side.id != top.sideId) continue;
			side.u = TextureAxis{{0, 1, 0}, 2887.68, 0.390625};
			side.v = TextureAxis{{1, 0, 0}, 5529.6, 0.390625};
		}
		history.push(t.finish());
	}
	const auto faceOf = [&]() -> FaceGeometry {
		const Solid* solid = doc.findSolid(ref);
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			if (solid->sides[static_cast<std::size_t>(face.sideIndex)].id == top.sideId) return face;
		}
		return {};
	};
	// A flat face this size is fine (VBSP cuts it); as a displacement it is not.
	CHECK(oversizedDisplacements(doc).empty());
	const auto luxels = lightmapLuxels(faceOf(), sideOf(doc, top), 16);
	CHECK(luxels[0] == 26);
	CHECK(luxels[1] == 131);   // exactly VBSP's number
	const std::string flat = doc.serialize();
	Change made = edit::createDisplacements(doc, {top}, 3);
	// The guard of the commit gives it a fitting scale at once (and the change's after state follows).
	{
		MapDocument copy = MapDocument::parse(doc.serialize());
		const auto big = oversizedDisplacements(copy);
		REQUIRE(big.size() == 1);
		CHECK(big.front().face == top);
		CHECK(big.front().luxels == 131);
		CHECK(big.front().scale == 16);
		CHECK(big.front().neededScale == 17);
		// The check reports it as an error with a fix.
		CheckContext context;
		const auto problems = checkMap(copy, context);
		const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::DisplacementLightmap; });
		REQUIRE(it != problems.end());
		CHECK(it->severity == Problem::Severity::Error);
		CHECK(it->objects.faces.size() == 1);
		CHECK(it->message.find("Bad surface extents") != std::string::npos);
		// The autofix on its own: one undo step, 17 fits with room.
		int count = -1;
		Change fix = edit::fixOversizedDisplacements(copy, {}, &count);
		CHECK(count == 1);
		CHECK_FALSE(fix.empty());
		CHECK(oversizedDisplacements(copy).empty());
		CHECK(sideOf(copy, top).lightmapScale == 17);
		CHECK(sideOf(copy, top).dispInfo());
	}
	CHECK(fitDisplacementLightmaps(doc, made) == 1);
	REQUIRE(made.solids.size() == 1);
	REQUIRE(made.solids.front().after);
	history.push(std::move(made));
	CHECK(oversizedDisplacements(doc).empty());
	CHECK(sideOf(doc, top).lightmapScale == 17);
	CHECK(lightmapLuxels(faceOf(), sideOf(doc, top), 17)[1] <= MAX_DISPLACEMENT_LUXELS - 2);
	history.undo(doc);
	CHECK(doc.serialize() == flat);
	history.redo(doc);
	CHECK(sideOf(doc, top).lightmapScale == 17);
	CHECK(sideOf(doc, top).dispInfo());
	// A short displacement keeps its scale; tool faces have no lightmap.
	auto [small, smallRef] = edit::createBlock(doc, box({0, 0, 0}, {512, 512, 16}), "NATURE/BLENDGROUNDTOGRASS");
	history.push(std::move(small));
	Change smallDisp = edit::createDisplacements(doc, {topFace(doc, smallRef)}, 2);
	CHECK(fitDisplacementLightmaps(doc, smallDisp) == 0);
	CHECK(sideOf(doc, topFace(doc, smallRef)).lightmapScale == 16);
	const std::string text = doc.serialize();
	CHECK(MapDocument::parse(text).serialize() == text);
}
