// test_overlay_tools.cpp - Overlays over several faces, overlay strips along a chain of faces
// (a curved road), dragging an overlay's corner.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/decals.h>
#include <sme/editing.h>
#include <sme/overlay_tools.h>
#include <sme/road_builder.h>

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

double area(const std::vector<Vec3>& polygon) {
	double sum = 0;
	for (std::size_t i = 2; i < polygon.size(); ++i) sum += 0.5 * length(cross(polygon[i - 1] - polygon[0], polygon[i] - polygon[0]));
	return sum;
}

int topSide(const MapDocument& doc, const SolidRef& ref) {
	const Solid* solid = doc.findSolid(ref);
	for (const auto& face : buildBrushGeometry(*solid).faces) {
		if (face.normal.z > 0.9) return solid->sides[static_cast<std::size_t>(face.sideIndex)].id;
	}
	return 0;
}

double patchArea(const std::vector<SurfacePatch>& patches) {
	double sum = 0;
	for (const auto& p : patches) sum += area(p.vertices);
	return sum;
}

} // namespace

TEST_CASE("overlay on several faces: one overlay covers both, tiled at the texture's proportions") {
	MapDocument doc;
	auto [m1, a] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "CONCRETE/FLOOR");
	auto [m2, b] = edit::createBlock(doc, box({256, 0, 0}, {512, 256, 16}), "CONCRETE/FLOOR");
	(void)m1;
	(void)m2;
	const auto faces = collectSurfaceFaces(doc);
	int dropped = -1;
	const auto o = overlayOnFaces(faces, {topSide(doc, a), topSide(doc, b)}, 2.0, 128, &dropped);
	REQUIRE(o);
	CHECK(dropped == 0);
	CHECK(o->sides.size() == 2);
	CHECK(o->normal.z == doctest::Approx(1));
	// The overlay covers the whole 512 x 256 top exactly.
	CHECK(patchArea(overlayPatches(faces, *o)) == doctest::Approx(512 * 256).epsilon(0.001));
	// Tiling: one repeat is 128 across the texture's height and 256 along its width.
	const double across = std::abs(o->corners[1].y - o->corners[0].y), along = std::abs(o->corners[3].x - o->corners[0].x);
	CHECK(o->endV == doctest::Approx(across / 128));
	CHECK(o->endU == doctest::Approx(along / 256));
	// The keys read back as the same overlay.
	Entity entity;
	entity.set("classname", "info_overlay");
	for (auto& [k, v] : overlayKeys(*o, "DECALS/CRACK01")) entity.set(k, v);
	const auto back = readOverlay(entity);
	REQUIRE(back);
	CHECK(back->sides == o->sides);
	CHECK(length(back->origin - o->origin) < 0.01);
	CHECK(back->endU == doctest::Approx(o->endU).epsilon(0.001));
}

TEST_CASE("overlay on several faces: faces turned away are left out") {
	MapDocument doc;
	auto [m1, a] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "CONCRETE/FLOOR");
	(void)m1;
	const Solid* solid = doc.findSolid(a);
	int bottom = 0, side = 0;
	for (const auto& face : buildBrushGeometry(*solid).faces) {
		const int id = solid->sides[static_cast<std::size_t>(face.sideIndex)].id;
		if (face.normal.z < -0.9) bottom = id;
		if (face.normal.x > 0.9) side = id;
	}
	const auto faces = collectSurfaceFaces(doc);
	int dropped = 0;
	// Top and bottom cancel out: no average normal.
	CHECK_FALSE(overlayOnFaces(faces, {topSide(doc, a), bottom}, 1, 0, &dropped));
	// A thin side face next to the big top: left out, the top keeps the overlay.
	const auto o = overlayOnFaces(faces, {topSide(doc, a), side}, 1, 0, &dropped);
	REQUIRE(o);
	CHECK(dropped == 1);
	CHECK(o->sides == std::vector<int>{topSide(doc, a)});
	CHECK(o->endU == doctest::Approx(1));   // stretched once
}

TEST_CASE("overlay strip: along a curved road the pieces meet without gap, the texture runs on, one side stays left") {
	MapDocument doc;
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	o.width = 256;
	o.innerRadius = 128;
	o.segments = 8;
	o.fit = false;
	const TextureSizeFn square = [](const std::string&) { return std::pair<int, int>{512, 512}; };
	const auto plan = planRoadBuild(doc, doc, {RoadPath{{{0, 0, 0}, {1024, 0, 0}, {1024, 1024, 0}}}}, o, square);
	auto [change, refs] = edit::applyRoadBuild(doc, plan);
	(void)change;
	REQUIRE(refs.size() == 10);
	std::vector<int> tops;
	for (const auto& ref : refs) tops.push_back(topSide(doc, ref));
	// Shuffled: the chain is found from the shared edges, not the order of the selection.
	std::reverse(tops.begin(), tops.end());
	std::swap(tops[2], tops[7]);
	const auto faces = collectSurfaceFaces(doc);
	const auto path = faceChainPath(faces, tops);
	CHECK(path.order.size() == 10);
	CHECK(path.points.size() == 11);

	OverlayStripOptions s;
	s.width = 32;
	s.offset = 64;
	s.textureAspect = 4;
	const auto strip = planOverlayStrip(faces, tops, s);
	REQUIRE(strip.size() == 10);
	double total = 0;
	for (std::size_t i = 0; i < strip.size(); ++i) {
		const auto& piece = strip[i];
		CHECK_FALSE(piece.sides.empty());
		CHECK(piece.sides.size() <= OVERLAY_MAX_SIDES);
		const auto patches = overlayPatches(faces, piece);
		total += patchArea(patches);
		if (i + 1 < strip.size()) {
			const auto a = overlayCorners(piece), b = overlayCorners(strip[i + 1]);
			// End of this piece = start of the next: left (uv3 -> uv0), right (uv2 -> uv1).
			CHECK(length(a[3] - b[0]) < 0.01);
			CHECK(length(a[2] - b[1]) < 0.01);
			CHECK(piece.endU == doctest::Approx(strip[i + 1].startU));
		}
		// uv0 (the texture's top) lies to the left of the direction of travel.
		const auto c = overlayCorners(piece);
		const Vec3 dir = c[3] - c[0];
		CHECK(dot(cross(Vec3{0, 0, 1}, dir), c[0] - c[1]) > 0);
	}
	// The band is 32 wide along a centre line of (768 + pi/2 * 192 + 768 - ...) - check its area
	// against width x length of the middle line.
	const double bandLength = strip.back().endU * 32 * 4;
	CHECK(total == doctest::Approx(32 * bandLength).epsilon(0.03));

	// Along the left edge: the outer corners lie on the road's edge (128 from the middle line).
	s.align = OverlayStripOptions::Align::Left;
	s.offset = 0;
	const auto edgeStrip = planOverlayStrip(faces, tops, s);
	REQUIRE(edgeStrip.size() == 10);
	for (std::size_t i = 0; i < edgeStrip.size(); ++i) {
		const auto c = overlayCorners(edgeStrip[i]);
		CHECK(length(c[0] - path.points[i]) == doctest::Approx(128).epsilon(0.01));
		CHECK(std::abs(length(c[1] - c[0]) - 32) < 0.5);
	}

	std::vector<int> ids;
	auto placed = edit::placeOverlays(doc, strip, "DECALS/LINE_WHITE", nullptr, &ids);
	CHECK_FALSE(placed.empty());
	REQUIRE(ids.size() == 10);
	const auto back = readOverlay(*doc.findEntity(ids[3]));
	REQUIRE(back);
	CHECK(back->sides == strip[3].sides);
	CHECK(doc.findEntity(ids[3])->classname() == "info_overlay");
}

TEST_CASE("overlay strip: one face gives a strip along its long side") {
	MapDocument doc;
	auto [m1, a] = edit::createBlock(doc, box({0, 0, 0}, {512, 128, 16}), "CONCRETE/FLOOR");
	(void)m1;
	const auto faces = collectSurfaceFaces(doc);
	OverlayStripOptions s;
	s.width = 16;
	const auto strip = planOverlayStrip(faces, {topSide(doc, a)}, s);
	REQUIRE(strip.size() == 1);
	CHECK(patchArea(overlayPatches(faces, strip[0])) == doctest::Approx(512 * 16).epsilon(0.001));
}

TEST_CASE("overlay corner drag: the overlay grows onto the next face in the same plane") {
	MapDocument doc;
	auto [m1, a] = edit::createBlock(doc, box({0, 0, 0}, {256, 256, 16}), "CONCRETE/FLOOR");
	auto [m2, b] = edit::createBlock(doc, box({256, 0, 0}, {512, 256, 16}), "CONCRETE/FLOOR");
	auto [m3, c] = edit::createBlock(doc, box({256, 0, 64}, {512, 256, 80}), "CONCRETE/FLOOR");
	(void)m1;
	(void)m2;
	(void)m3;
	const auto faces = collectSurfaceFaces(doc);
	auto o = overlayOnFaces(faces, {topSide(doc, a)}, 1, 0);
	REQUIRE(o);
	REQUIRE(o->sides.size() == 1);
	// Drag the corner nearest to +X +Y out to (400, 200).
	int corner = 0;
	double best = -1e9;
	const auto world = overlayCorners(*o);
	for (int i = 0; i < 4; ++i) {
		const double score = world[static_cast<std::size_t>(i)].x + world[static_cast<std::size_t>(i)].y;
		if (score > best) {
			best = score;
			corner = i;
		}
	}
	const auto moved = moveOverlayCorner(faces, *o, corner, {400, 200, 16});
	const auto after = overlayCorners(moved);
	CHECK(after[static_cast<std::size_t>(corner)].x == doctest::Approx(400));
	CHECK(after[static_cast<std::size_t>(corner)].y == doctest::Approx(200));
	CHECK(moved.sides.size() == 2);   // the second floor joined, not the higher block
	CHECK(std::find(moved.sides.begin(), moved.sides.end(), topSide(doc, b)) != moved.sides.end());
}
