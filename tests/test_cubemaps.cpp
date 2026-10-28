// test_cubemaps.cpp - env_cubemap probes for the reflection preview: collecting them with their
// room boxes, which probe a face reflects, box projection and the cube face layout.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/cubemaps.h>
#include <sme/map.h>

#include <cmath>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

Entity cubemap(MapDocument& doc, Vec3 origin, const std::string& sides = {}) {
	Entity e;
	e.id = doc.newEntityId();
	e.set("classname", "env_cubemap");
	e.setOrigin(origin);
	if (!sides.empty()) e.set("sides", sides);
	return e;
}

} // namespace

TEST_CASE("cubemap probes: collected with room boxes, faces get the listed or the nearest probe") {
	MapDocument doc;
	doc.entities.push_back(cubemap(doc, {0, 0, 64}));
	auto second = cubemap(doc, {512, 0, 64}, "7 12");
	second.set("parallaxobb", "room_b");
	doc.entities.push_back(second);
	Entity obb;
	obb.id = doc.newEntityId();
	obb.set("classname", "parallax_obb");
	obb.set("targetname", "room_b");
	obb.solids.push_back(makeBlock(doc, box({384, -128, 0}, {640, 128, 256}), "TOOLS/TOOLSTRIGGER"));
	doc.entities.push_back(obb);

	const auto probes = collectCubemaps(doc);
	REQUIRE(probes.size() == 2);
	CHECK_FALSE(probes[0].box);
	REQUIRE(probes[1].box);
	CHECK(probes[1].box->min.x == doctest::Approx(384));
	CHECK(probes[1].box->max.z == doctest::Approx(256));
	CHECK(probes[1].sides == std::vector<int>{7, 12});

	CHECK(nearestProbe(probes, {100, 0, 0}) == 0);
	CHECK(nearestProbe(probes, {400, 0, 0}) == 1);
	CHECK(nearestProbe({}, {0, 0, 0}) == -1);
	// A face listed in "sides" uses that cubemap even when another one is nearer.
	CHECK(probeForFace(probes, 12, {0, 0, 0}) == 1);
	CHECK(probeForFace(probes, 13, {0, 0, 0}) == 0);
	CHECK(probeForFace({}, 12, {0, 0, 0}) == -1);
}

TEST_CASE("cubemap probes: box projection and the cube face layout") {
	// Box projection: a ray from inside the room hits the wall; the probe looks at that point.
	const Bounds room = box({-100, -100, 0}, {100, 100, 200});
	const Vec3 probe{0, 0, 100};
	const Vec3 dir = boxProjectedDirection({50, 0, 100}, {1, 0, 0}, probe, room);
	CHECK(dir.x == doctest::Approx(1));
	CHECK(std::abs(dir.y) < 1e-9);
	// Diagonally from (50, 0): hits x = 100 at y = 50, seen from the probe at 45 degrees.
	const Vec3 diagonal = boxProjectedDirection({50, 0, 100}, normalize(Vec3{1, 1, 0}), probe, room);
	CHECK(diagonal.y / diagonal.x == doctest::Approx(0.5).epsilon(1e-6));
	const Vec3 corner = boxProjectedDirection({90, 0, 100}, normalize(Vec3{1, 1, 0}), probe, room);
	CHECK(corner.y / corner.x == doctest::Approx(0.1).epsilon(1e-6));   // hits (100, 10)
	// Outside the box: unchanged.
	const Vec3 outside = boxProjectedDirection({500, 0, 100}, {1, 0, 0}, probe, room);
	CHECK(outside.x == doctest::Approx(1));

	// Each face's own direction lands in its middle; the view's camera for a face (forward, up,
	// right = forward x up) maps a point at (a, b) of its 90 degree picture to s, t = (a+1)/2, (b+1)/2.
	for (int face = 0; face < 6; ++face) {
		const Vec3 forward = cubeFaceForward(face), up = cubeFaceUp(face), right = cross(forward, up);
		CHECK(std::abs(dot(forward, up)) < 1e-9);
		const auto middle = cubeFaceCoordinates(forward);
		CHECK(middle.face == face);
		CHECK(middle.s == doctest::Approx(0.5));
		CHECK(middle.t == doctest::Approx(0.5));
		for (const double a : {-0.8, -0.2, 0.6}) {
			for (const double b : {-0.7, 0.3, 0.9}) {
				const auto texel = cubeFaceCoordinates(forward + right * a + up * b);
				CHECK(texel.face == face);
				CHECK(texel.s == doctest::Approx((a + 1) / 2));
				CHECK(texel.t == doctest::Approx((b + 1) / 2));
			}
		}
	}
	CHECK(cubeFaceCoordinates({0, 0, -5}).face == 5);
	CHECK(cubeFaceCoordinates({0.1, -3, 0.5}).face == 3);
}
