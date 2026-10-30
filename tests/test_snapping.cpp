// test_snapping.cpp - Snapping onto other objects: corners of what is not dragged, the corner
// nearest to the mouse, sides being resized lining up with other brushes (also off the grid),
// and resized brushes landing exactly on the snapped value.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/snapping.h>

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

bool hasCorner(const std::vector<Vec3>& corners, const Vec3& p) {
	return std::any_of(corners.begin(), corners.end(), [&](const Vec3& c) { return length(c - p) < 1e-6; });
}

} // namespace

TEST_CASE("snapping: corners of everything that is neither dragged nor hidden") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01"));
	doc.world.solids.push_back(makeBlock(doc, box({100, 0, 0}, {164.5, 32, 16}), "DEV/DEV_MEASUREGENERIC01"));
	doc.world.solids.push_back(makeBlock(doc, box({-200, 0, 0}, {-100, 10, 10}), "DEV/DEV_MEASUREGENERIC01"));
	Entity light;
	light.id = doc.newEntityId();
	light.set("classname", "light");
	light.set("origin", "500 0 0");
	doc.entities.push_back(light);

	Selection dragged;
	dragged.solids.push_back({0, doc.world.solids[0].id});
	Selection hidden;
	hidden.solids.push_back({0, doc.world.solids[2].id});
	const auto corners = snapCorners(doc, dragged, hidden, nullptr);
	CHECK_FALSE(hasCorner(corners, {0, 0, 0}));            // the dragged brush
	CHECK_FALSE(hasCorner(corners, {-200, 0, 0}));         // the hidden one
	CHECK(hasCorner(corners, {164.5, 32, 16}));            // off the grid, exactly
	CHECK(hasCorner(corners, {100, 0, 0}));
	// The point entity's box (8 corners around its origin).
	CHECK(std::count_if(corners.begin(), corners.end(), [](const Vec3& c) { return c.x > 400; }) == 8);
	// Dragging the entity leaves its corners out.
	dragged.entities.push_back(light.id);
	const auto without = snapCorners(doc, dragged, hidden, nullptr);
	CHECK(std::none_of(without.begin(), without.end(), [](const Vec3& c) { return c.x > 400; }));
}

TEST_CASE("snapping: the nearest corner within the radius wins") {
	const std::vector<Vec3> corners{{0, 0, 0}, {10, 0, 0}, {30, 0, 0}};
	// A stand-in screen distance: how far x lies from 12.
	const auto distance = [](const Vec3& c) -> std::optional<double> { return std::abs(c.x - 12); };
	const auto hit = nearestCorner(corners, distance, 5);
	REQUIRE(hit);
	CHECK(hit->x == 10);
	CHECK_FALSE(nearestCorner(corners, distance, 1));   // nothing close enough
	// Corners that are not on screen do not count.
	const auto hidden = [](const Vec3& c) -> std::optional<double> {
		if (c.x == 10) return std::nullopt;
		return std::abs(c.x - 12);
	};
	CHECK_FALSE(nearestCorner(corners, hidden, 5));
}

TEST_CASE("snapping: a side being resized lines up with a corner of another brush, also off the grid") {
	// The side moves along x; it covers y 0..96, z 0..32.
	const Bounds side = box({0, 0, 0}, {128, 96, 32});
	const std::vector<Vec3> corners{
	    {180.5, 96, 0},     // beside the side's edge (y = 96): an L corner off the 16 grid
	    {176, 500, 0},      // in line but far away beside it
	    {300, 10, 10},      // in front but too far along x
	};
	// Dragged to 178: within 4 units of 180.5 -> exactly 180.5.
	auto hit = snapFacePlane(corners, 0, 178, side, 4, 64);
	REQUIRE(hit);
	CHECK(hit->value == 180.5);
	CHECK(hit->corner == Vec3{180.5, 96, 0});
	// The far brush (y = 500) does not pull, even right in line with it.
	CHECK_FALSE(snapFacePlane(corners, 0, 176, side, 4, 64));
	// Outside the tolerance: no snap (the grid takes over).
	CHECK_FALSE(snapFacePlane(corners, 0, 170, side, 4, 64));
	// Two candidates: the one nearer along the axis wins, then the one closer to the side.
	const std::vector<Vec3> two{{181, 50, 10}, {180, 120, 10}, {180, 40, 10}};
	hit = snapFacePlane(two, 0, 180.2, side, 4, 64);
	REQUIRE(hit);
	CHECK(hit->value == 180);
	CHECK(hit->corner == Vec3{180, 40, 10});   // in front of the side, not 24 units beside it
}

TEST_CASE("snapping: resized boxes keep their minimum and land exactly on an off-grid value") {
	const Bounds start = box({0, 0, 0}, {128, 96, 32});
	CHECK(resizedBounds(start, 0, true, 180.5, 16).max.x == 180.5);
	CHECK(resizedBounds(start, 0, true, -50, 16).max.x == 16);   // never thinner than the minimum
	CHECK(resizedBounds(start, 2, false, -4.5, 16).min.z == -4.5);
	CHECK(resizedBounds(start, 2, false, 40, 16).min.z == 16);

	// The brush scaled to the snapped box has its side exactly there (no 180.49999 gap).
	MapDocument doc;
	Solid solid = makeBlock(doc, start, "DEV/DEV_MEASUREGENERIC01");
	const Bounds to = resizedBounds(start, 0, true, 180.5, 16);
	REQUIRE(scaleSolid(solid, start, to));
	double maxX = -1e9;
	for (const auto& s : solid.sides) {
		for (const auto& p : s.planePoints) maxX = std::max(maxX, p.x);
	}
	CHECK(maxX == 180.5);
	// Odd sizes too: 100 -> 33.3 keeps both ends exact.
	Solid odd = makeBlock(doc, box({7, 0, 0}, {107, 10, 10}), "DEV/DEV_MEASUREGENERIC01");
	REQUIRE(scaleSolid(odd, box({7, 0, 0}, {107, 10, 10}), box({7, 0, 0}, {40.3, 10, 10})));
	bool exact = true;
	for (const auto& s : odd.sides) {
		for (const auto& p : s.planePoints) exact = exact && (p.x == 7 || p.x == 40.3);
	}
	CHECK(exact);
}
