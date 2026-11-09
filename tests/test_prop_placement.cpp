// test_prop_placement.cpp - Props along a path (spacing, turned with the curve, side offset),
// scattered over surfaces (density, slope, spacing, keep away, same seed = same result), the
// path from selected edges, the entities written, and the measure tool's numbers.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/history.h>
#include <sme/prop_placement.h>

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

/// The two triangles of a flat square at height z.
std::vector<ScatterTriangle> square(double size, double z = 0) {
	return {{{0, 0, z}, {size, 0, z}, {size, size, z}}, {{0, 0, z}, {size, size, z}, {0, size, z}}};
}

int sideIdFacing(const Solid& solid, const Vec3& normal) {
	for (const auto& side : solid.sides) {
		if (dot(side.plane().normal, normal) > 0.999) return side.id;
	}
	return 0;
}

} // namespace

TEST_CASE("props along a path: spacing, turned along the path, offset to the side") {
	PathPropOptions options;
	options.spacing = 100;
	const auto straight = propsAlongPath({{0, 0, 0}, {450, 0, 0}}, options);
	REQUIRE(straight.size() == 5);   // 0 100 200 300 400
	CHECK(straight[3].origin.x == doctest::Approx(300));
	CHECK(straight[0].yaw == doctest::Approx(0));
	options.includeEnd = true;
	CHECK(propsAlongPath({{0, 0, 0}, {450, 0, 0}}, options).size() == 6);
	options.includeEnd = false;

	// Round a corner: the distance runs on through it, the props turn with the path.
	const auto corner = propsAlongPath({{0, 0, 0}, {250, 0, 0}, {250, 250, 0}}, options);
	REQUIRE(corner.size() == 6);   // 0 100 200 | 300 400 500 -> (250,50), (250,150), (250,250)
	CHECK(corner[3].origin.x == doctest::Approx(250));
	CHECK(corner[3].origin.y == doctest::Approx(50));
	CHECK(corner[3].yaw == doctest::Approx(90));

	// Beside the path: right of travel along +x is -y.
	options.sideOffset = 64;
	options.yawOffset = 90;
	const auto beside = propsAlongPath({{0, 0, 0}, {300, 0, 0}}, options);
	CHECK(beside[1].origin.y == doctest::Approx(-64));
	CHECK(beside[1].yaw == doctest::Approx(90));

	// Ramps keep the height of the path.
	options = {};
	options.spacing = 50;
	const auto ramp = propsAlongPath({{0, 0, 0}, {100, 0, 100}}, options);
	REQUIRE(ramp.size() >= 2);
	CHECK(ramp[1].origin.z == doctest::Approx(50 / std::sqrt(2.0)).epsilon(0.01));
	CHECK(propsAlongPath({{0, 0, 0}}, options).empty());
}

TEST_CASE("props along a path: random turn and scale stay within their range and repeat with the seed") {
	PathPropOptions options;
	options.spacing = 10;
	options.randomYaw = 15;
	options.scaleMin = 0.8;
	options.scaleMax = 1.2;
	const auto a = propsAlongPath({{0, 0, 0}, {1000, 0, 0}}, options);
	const auto b = propsAlongPath({{0, 0, 0}, {1000, 0, 0}}, options);
	REQUIRE(a.size() == b.size());
	bool turned = false;
	for (std::size_t i = 0; i < a.size(); ++i) {
		CHECK(a[i].yaw == b[i].yaw);
		const double yaw = a[i].yaw > 180 ? a[i].yaw - 360 : a[i].yaw;
		CHECK(std::abs(yaw) <= 15.01);
		CHECK(a[i].scale >= 0.79);
		CHECK(a[i].scale <= 1.21);
		turned = turned || std::abs(yaw) > 1;
	}
	CHECK(turned);
}

TEST_CASE("scatter: density, spacing, slope and keeping off a road") {
	ScatterOptions options;
	options.density = 4;   // per 512x512
	options.minSpacing = 32;
	bool limited = true;
	const auto flat = scatterProps(square(1024), {}, options, &limited);
	CHECK_FALSE(limited);
	CHECK(flat.size() == 16);
	for (std::size_t i = 0; i < flat.size(); ++i) {
		CHECK(flat[i].origin.x >= 0);
		CHECK(flat[i].origin.x <= 1024);
		CHECK(flat[i].origin.z == doctest::Approx(0));
		for (std::size_t j = i + 1; j < flat.size(); ++j) CHECK(length(flat[i].origin - flat[j].origin) >= 32);
	}
	// The same seed gives the same props; another seed others.
	const auto again = scatterProps(square(1024), {}, options);
	CHECK(again.size() == flat.size());
	CHECK(again[3].origin.x == flat[3].origin.x);
	options.seed = 7;
	CHECK(scatterProps(square(1024), {}, options)[3].origin.x != flat[3].origin.x);

	// Too steep: nothing.
	const std::vector<ScatterTriangle> steep{{{0, 0, 0}, {512, 0, 512}, {512, 512, 512}}, {{0, 0, 0}, {512, 512, 512}, {0, 512, 0}}};
	options.maxSlope = 35;
	CHECK(scatterProps(steep, {}, options).empty());
	options.maxSlope = 50;
	CHECK_FALSE(scatterProps(steep, {}, options).empty());

	// A road through the middle (x 448..576) and 64 units kept off it.
	MapDocument doc;
	const Solid road = makeBlock(doc, box({448, -64, -16}, {576, 1088, 0}), "ROAD/ASPHALT");
	options = {};
	options.density = 20;
	options.minSpacing = 16;
	options.keepAway = 64;
	const auto beside = scatterProps(square(1024), {&road}, options);
	CHECK(beside.size() > 20);
	for (const auto& p : beside) CHECK((p.origin.x < 448 - 63.9 || p.origin.x > 576 + 63.9));

	// More than allowed: cut short and said so.
	options = {};
	options.density = 100;
	options.minSpacing = 0;
	options.maxCount = 50;
	const auto many = scatterProps(square(1024), {}, options, &limited);
	CHECK(limited);
	CHECK(many.size() == 50);
}

TEST_CASE("scatter: tilted with the ground, upright otherwise") {
	ScatterOptions options;
	options.density = 8;
	options.maxSlope = 40;
	options.minSpacing = 0;
	options.tiltWithGround = true;
	// A slope rising 30 degrees towards +x.
	const double rise = std::tan(30 * 3.14159265358979323846 / 180) * 512;
	const std::vector<ScatterTriangle> slope{{{0, 0, 0}, {512, 0, rise}, {512, 512, rise}}, {{0, 0, 0}, {512, 512, rise}, {0, 512, 0}}};
	const auto tilted = scatterProps(slope, {}, options);
	REQUIRE_FALSE(tilted.empty());
	for (const auto& p : tilted) {
		// The model's up axis (Source angles) must be the slope's normal.
		const double pr = p.pitch * 3.14159265358979323846 / 180, yr = p.yaw * 3.14159265358979323846 / 180,
		             rr = p.roll * 3.14159265358979323846 / 180;
		const Vec3 up{std::cos(rr) * std::sin(pr) * std::cos(yr) + std::sin(rr) * std::sin(yr),
		              std::cos(rr) * std::sin(pr) * std::sin(yr) - std::sin(rr) * std::cos(yr), std::cos(rr) * std::cos(pr)};
		const Vec3 normal = normalize(Vec3{-std::sin(30 * 3.14159265358979323846 / 180), 0, std::cos(30 * 3.14159265358979323846 / 180)});
		CHECK(dot(up, normal) > 0.999);
	}
	options.tiltWithGround = false;
	for (const auto& p : scatterProps(slope, {}, options)) {
		CHECK(p.pitch == 0);
		CHECK(p.roll == 0);
	}
}

TEST_CASE("scatter surface from faces: tops of brushes and displacement shapes, not walls") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, -16}, {512, 512, 0}), "NATURE/GRASS"));
	const Solid& floor = doc.world.solids.back();
	const SolidRef ref{0, floor.id};
	const auto top = scatterSurface(doc, {{ref, sideIdFacing(floor, {0, 0, 1})}});
	CHECK(top.size() == 2);
	CHECK(scatterSurface(doc, {{ref, sideIdFacing(floor, {1, 0, 0})}}).empty());
	CHECK(scatterSurface(doc, {{ref, sideIdFacing(floor, {0, 0, -1})}}).empty());
	CHECK(scatterSurface(doc, {{{0, 999}, 1}}).empty());

	// A displacement: its grid's triangles, at the sculpted height.
	const int topId = sideIdFacing(floor, {0, 0, 1});
	(void)edit::createDisplacements(doc, {{ref, topId}}, 2, 32);
	const auto disp = scatterSurface(doc, {{ref, topId}});
	REQUIRE(disp.size() == 4 * 4 * 2);
	CHECK(disp[0].a.z == doctest::Approx(32));
}

TEST_CASE("edge chain: selected kerb edges become one path in order") {
	CHECK(chainSegments({{{100, 0, 0}, {200, 0, 0}}, {{0, 0, 0}, {100, 0, 0}}, {{200, 0, 0}, {200, 100, 0}}}).size() == 4);
	const auto chain = chainSegments({{{100, 0, 0}, {200, 0, 0}}, {{0, 0, 0}, {100, 0, 0}}, {{5000, 0, 0}, {5010, 0, 0}}});
	REQUIRE(chain.size() == 3);
	CHECK(std::abs(chain.front().x - chain.back().x) == doctest::Approx(200));

	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {512, 64, 16}), "CONCRETE/KERB"));
	const Solid& kerb = doc.world.solids.back();
	const SolidRef ref{0, kerb.id};
	const FaceRef topFace{ref, sideIdFacing(kerb, {0, 0, 1})};
	const FaceRef side{ref, sideIdFacing(kerb, {0, -1, 0})};
	const auto path = edgeChainPath(doc, {{topFace, side}});
	REQUIRE(path.size() == 2);
	CHECK(length(path[0] - path[1]) == doctest::Approx(512));
	CHECK(path[0].y == doctest::Approx(0));
	CHECK(path[0].z == doctest::Approx(16));
}

TEST_CASE("prop entities: keys written, one undo step") {
	MapDocument doc;
	PropPlacement p;
	p.origin = {10.5, -20, 0};
	p.yaw = 90;
	p.scale = 1.25;
	auto entities = makePropEntities(doc, {p, PropPlacement{}}, "prop_static", "models/props_c17/fence01a.mdl", {{"solid", "6"}, {"skin", ""}});
	REQUIRE(entities.size() == 2);
	CHECK(entities[0].classname() == "prop_static");
	CHECK(entities[0].get("model") == "models/props_c17/fence01a.mdl");
	CHECK(entities[0].get("angles") == "0 90 0");
	CHECK(entities[0].get("origin") == "10.5 -20 0");
	CHECK(entities[0].get("modelscale") == "1.25");
	CHECK(entities[0].get("solid") == "6");
	CHECK(entities[1].get("modelscale").empty());
	CHECK(entities[0].id != entities[1].id);

	History history;
	const std::size_t before = doc.entities.size();
	auto [change, ids] = applyPropPlacements(doc, std::move(entities), "Place props");
	CHECK(ids.size() == 2);
	CHECK(doc.entities.size() == before + 2);
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.entities.size() == before);
}

TEST_CASE("measure: distance, height, slope and what the player can do") {
	const auto flat = measure({0, 0, 0}, {300, 400, 0});
	CHECK(flat.distance == doctest::Approx(500));
	CHECK(flat.horizontal == doctest::Approx(500));
	CHECK(flat.slope == doctest::Approx(0));
	CHECK(flat.walkable);

	const auto step = measure({0, 0, 0}, {16, 0, 18});
	CHECK(step.stepUp);
	CHECK_FALSE(measure({0, 0, 0}, {16, 0, 19}).stepUp);
	CHECK(measure({0, 0, 0}, {0, 0, 56}).jumpUp);
	CHECK_FALSE(measure({0, 0, 0}, {0, 0, 57}).jumpUp);

	const auto ramp = measure({0, 0, 0}, {100, 0, 100});
	CHECK(ramp.slope == doctest::Approx(45));
	CHECK(ramp.walkable);
	CHECK_FALSE(measure({0, 0, 0}, {100, 0, 110}).walkable);

	const auto door = measure({0, 0, 0}, {0, 0, 72});
	CHECK(door.playerFits);
	CHECK(door.height == doctest::Approx(72));
	CHECK_FALSE(measure({0, 0, 0}, {0, 0, 70}).playerFits);
	CHECK(measure({0, 0, 0}, {0, 0, 40}).crouchFits);
	CHECK_FALSE(measure({0, 0, 0}, {0, 30, 0}).playerWide);
}
