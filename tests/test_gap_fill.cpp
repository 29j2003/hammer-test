// test_gap_fill.cpp - "Fill gap": the space between selected brushes becomes a brush that
// overlaps nothing, continues the neighbours' textures and is one undo step.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/edge_round.h>
#include <sme/gap_fill.h>
#include <sme/history.h>

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

SolidRef addBlock(MapDocument& doc, Vec3 a, Vec3 b, const std::string& material = "CONCRETE/FLOOR") {
	doc.world.solids.push_back(makeBlock(doc, box(a, b), material));
	return {0, doc.world.solids.back().id};
}

double volume(const std::vector<Solid>& solids) {
	double sum = 0;
	for (const auto& s : solids) sum += solidVolume(s);
	return sum;
}

/// Whether the fill overlaps any brush of the map.
bool overlapsMap(const MapDocument& doc, const std::vector<Solid>& fill) {
	for (const auto& f : fill) {
		for (const auto& s : doc.world.solids) {
			if (sharedVolume(f, s) > 0.5) return true;
		}
	}
	return false;
}

/// A road piece turned left by `angleDeg` around the inner corner `pivot` of the piece before it:
/// 512 long, 256 wide, 16 thick, starting at the pivot and reaching to its right.
Solid turnedBlock(MapDocument& doc, double angleDeg, const Vec3& pivot) {
	Solid s = makeBlock(doc, box({0, -256, -16}, {512, 0, 0}), "ROAD/ASPHALT");
	const double a = angleDeg * 3.14159265358979323846 / 180.0;
	std::vector<Vec3> corners;
	for (auto c : solidCorners(s)) {
		const double x = c.x * std::cos(a) - c.y * std::sin(a);
		const double y = c.x * std::sin(a) + c.y * std::cos(a);
		corners.push_back(Vec3{x, y, c.z} + pivot);
	}
	return *solidFromPoints(doc, corners, "ROAD/ASPHALT");
}

} // namespace

TEST_CASE("fill gap: the space between two blocks, nothing more") {
	MapDocument doc;
	const auto a = addBlock(doc, {0, 0, 0}, {256, 256, 32});
	const auto b = addBlock(doc, {320, 0, 0}, {576, 256, 32});
	for (const auto kind : {GapFillOptions::Kind::Hull, GapFillOptions::Kind::Box}) {
		GapFillOptions options;
		options.kind = kind;
		MapDocument scratch = doc;
		const auto plan = planGapFill(doc, scratch, {a, b}, options);
		REQUIRE(plan.ok());
		CHECK(volume(plan.fill) == doctest::Approx(64.0 * 256 * 32).epsilon(0.001));
		CHECK_FALSE(overlapsMap(doc, plan.fill));
		for (const auto& s : plan.fill) CHECK(brushProblem(s).empty());
		// The top runs on from the blocks' top (same material, same alignment).
		const auto& top = *std::max_element(plan.fill.front().sides.begin(), plan.fill.front().sides.end(),
		                                    [](const Side& x, const Side& y) { return x.plane().normal.z < y.plane().normal.z; });
		CHECK(top.material == "CONCRETE/FLOOR");
		CHECK(top.extra.empty());
	}
}

TEST_CASE("fill gap: a wedge where two road pieces meet at an angle") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({-512, -128, -16}, {0, 128, 0}), "ROAD/ASPHALT"));
	const SolidRef a{0, doc.world.solids.back().id};
	doc.world.solids.push_back(turnedBlock(doc, 30, {0, 128, 0}));
	const SolidRef b{0, doc.world.solids.back().id};
	CHECK(sharedVolume(doc.world.solids[0], doc.world.solids[1]) < 1);
	MapDocument scratch = doc;
	const auto plan = planGapFill(doc, scratch, {a, b}, {});
	REQUIRE(plan.ok());
	CHECK_FALSE(overlapsMap(doc, plan.fill));
	// The open wedge on the outside of the turn (the inside of the L stays open): a triangle of two
	// 256 long sides 30 degrees apart, 16 high.
	const double wedge = 0.5 * 256 * 256 * std::sin(30 * 3.14159265358979323846 / 180) * 16;
	CHECK(volume(plan.fill) == doctest::Approx(wedge).epsilon(0.02));
	// The inside of the turn stays open.
	for (const auto& s : plan.fill) CHECK_FALSE(solidContains(s, {-50, 200, -8}, -0.01));
	for (const auto& s : plan.fill) {
		CHECK(brushProblem(s).empty());
		for (const auto& side : s.sides) CHECK(side.material == "ROAD/ASPHALT");
	}
}

TEST_CASE("fill gap: a step between two heights becomes a slope (hull) or stays flat (box)") {
	MapDocument doc;
	const auto low = addBlock(doc, {0, 0, 0}, {256, 256, 16});
	const auto high = addBlock(doc, {384, 0, 0}, {640, 256, 64});
	MapDocument scratch = doc;
	GapFillOptions hull;
	const auto slope = planGapFill(doc, scratch, {low, high}, hull);
	REQUIRE(slope.ok());
	CHECK_FALSE(overlapsMap(doc, slope.fill));
	// Between the low block's end (16 high) and the high one's start (64 high): a slope.
	CHECK(volume(slope.fill) == doctest::Approx((128.0 * 16 + 0.5 * 128 * 48) * 256).epsilon(0.01));
	GapFillOptions boxed;
	boxed.kind = GapFillOptions::Kind::Box;
	const auto flat = planGapFill(doc, scratch, {low, high}, boxed);
	REQUIRE(flat.ok());
	CHECK(volume(flat.fill) == doctest::Approx(128.0 * 256 * 16).epsilon(0.001));
}

TEST_CASE("fill gap: brushes already in the gap are cut out, the outside of the hull is left alone") {
	MapDocument doc;
	const auto a = addBlock(doc, {0, 0, 0}, {256, 256, 32});
	const auto b = addBlock(doc, {512, 0, 0}, {768, 256, 32});
	addBlock(doc, {320, 64, 0}, {384, 128, 32}, "METAL/PILLAR");   // a post standing in the gap
	MapDocument scratch = doc;
	const auto plan = planGapFill(doc, scratch, {a, b}, {});
	REQUIRE(plan.ok());
	CHECK_FALSE(overlapsMap(doc, plan.fill));
	CHECK(volume(plan.fill) == doctest::Approx((256.0 * 256 - 64 * 64) * 32).epsilon(0.001));
	for (const auto& s : plan.fill) CHECK(brushProblem(s).empty());
}

TEST_CASE("fill gap: nothing to fill, one brush, applied as one undo step") {
	MapDocument doc;
	const auto a = addBlock(doc, {0, 0, 0}, {256, 256, 32});
	const auto b = addBlock(doc, {256, 0, 0}, {512, 256, 32});
	MapDocument scratch = doc;
	CHECK_FALSE(planGapFill(doc, scratch, {a}, {}).ok());
	const auto touching = planGapFill(doc, scratch, {a, b}, {});
	CHECK_FALSE(touching.ok());
	CHECK_FALSE(touching.notes.empty());

	const auto c = addBlock(doc, {640, 0, 0}, {896, 256, 32});
	History history;
	const auto plan = planGapFill(doc, doc, {b, c}, {});
	REQUIRE(plan.ok());
	const std::size_t before = doc.world.solids.size();
	auto [change, refs] = applyGapFill(doc, plan);
	CHECK(refs.size() == plan.fill.size());
	CHECK(doc.world.solids.size() == before + plan.fill.size());
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.world.solids.size() == before);
}
