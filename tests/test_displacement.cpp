// test_displacement.cpp - Displacements: create, dispinfo as Hammer writes it, sculpting, sewing, undo.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/displacement.h>
#include <sme/editing.h>

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
