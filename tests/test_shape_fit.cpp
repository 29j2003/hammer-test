// test_shape_fit.cpp - Block tool shapes fitted into the map around them: arches against walls,
// shapes in holes of floors and sunk into floors, settings reduced to what the box takes.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/shape_fit.h>
#include <sme/shapes.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

/// How many world brushes hold the point.
int holders(const MapDocument& doc, const Vec3& p) {
	return int(std::count_if(doc.world.solids.begin(), doc.world.solids.end(), [&](const Solid& s) { return solidContains(s, p, -0.01); }));
}

bool filled(const MapDocument& doc, const Vec3& p) {
	return holders(doc, p) > 0;
}

bool allValid(const std::vector<Solid>& solids) {
	return std::all_of(solids.begin(), solids.end(), [](const Solid& s) { return isConvexValid(s) && solidVolume(s) > 1; });
}

double volume(const std::vector<Solid>& solids) {
	double sum = 0;
	for (const auto& s : solids) sum += solidVolume(s);
	return sum;
}

ShapeOptions archOptions(double arc, int pieces, double width) {
	ShapeOptions o;
	o.kind = ShapeOptions::Kind::Arch;
	o.arc = arc;
	o.sides = pieces;
	o.wallWidth = width;
	return o;
}

/// A floor 32 units thick with a square hole (top at z = 0).
void floorWithHole(MapDocument& doc, double lo, double hi) {
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{-512, -512, -32}, {lo, 512, 0}},
	                                                               {{hi, -512, -32}, {512, 512, 0}},
	                                                               {{lo, -512, -32}, {hi, lo, 0}},
	                                                               {{lo, hi, -32}, {hi, 512, 0}}}) {
		auto created = edit::createBlock(doc, box(a, b), "FLOOR");
	}
}

} // namespace

TEST_CASE("shape fit: an arch standing on a floor is left alone, closed and convex") {
	MapDocument doc;
	auto created = edit::createBlock(doc, box({-512, -512, -16}, {512, 512, 0}), "FLOOR");
	const auto before = doc.serialize();
	MapDocument scratch;
	const auto plan = planShape(doc, doc, box({-128, -128, 0}, {128, 128, 128}), archOptions(180, 8, 16), "ARCH");
	REQUIRE(plan.ok());
	CHECK(plan.fitted);
	CHECK(plan.shape.size() == 8);
	CHECK(plan.fill.empty());
	CHECK(plan.carved.empty());
	CHECK_FALSE(plan.limits.limited());
	CHECK(allValid(plan.shape));
	// Closed: the pieces together hold the half ring (polygon of 8 chords, a little less than the
	// round one), no gaps between them.
	const double ring = std::numbers::pi * (128.0 * 128 - 112.0 * 112) / 2 * 128;
	CHECK(volume(plan.shape) == doctest::Approx(ring).epsilon(0.03));
	for (int i = 0; i < 8; ++i) {
		const double a = (i + 0.5) / 8 * std::numbers::pi;   // middle of each piece, inside the ring
		const Vec3 p{120 * std::cos(a), 120 * std::sin(a), 64};
		CHECK(std::count_if(plan.shape.begin(), plan.shape.end(), [&](const Solid& s) { return solidContains(s, p, -0.01); }) == 1);
	}
	auto [change, refs] = applyShapePlan(doc, plan, "Create arch");
	CHECK(refs.size() == 8);
	History history;
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.serialize() == before);
}

TEST_CASE("shape fit: an arch drawn into a corner fills the space behind it") {
	// A rounded room corner: a quarter arch in the corner of two walls. Without the fill the void
	// between the curve and the corner stays open (and leaks in the game).
	MapDocument doc;
	auto east = edit::createBlock(doc, box({128, -64, 0}, {144, 144, 128}), "WALL");
	auto north = edit::createBlock(doc, box({-64, 128, 0}, {128, 144, 128}), "WALL");
	const auto bounds = box({0, 0, 0}, {128, 128, 128});
	auto options = archOptions(90, 4, 16);
	MapDocument scratch;
	const auto plan = planShape(doc, scratch, bounds, options, "ARCH");
	REQUIRE(plan.ok());
	CHECK(plan.shape.size() == 4);
	CHECK(plan.fill.size() >= 4);
	CHECK(allValid(plan.fill));
	CHECK(plan.carved.empty());
	auto [change, refs] = applyShapePlan(doc, plan, "Create arch");
	CHECK(refs.size() == plan.shape.size() + plan.fill.size());
	CHECK(filled(doc, {126, 125, 64}));     // the corner behind the curve
	CHECK(filled(doc, {127, 100, 64}));     // behind the curve, near the east wall
	CHECK_FALSE(filled(doc, {70, 70, 64}));  // in front of the curve: open
	// Nothing overlaps: every point is held by one brush at most.
	for (const Vec3& p : {Vec3{126, 126, 64}, Vec3{120, 70, 64}, Vec3{70, 120, 64}, Vec3{100, 100, 64}}) CHECK(holders(doc, p) <= 1);

	// The same arch in the open: nothing to fill.
	MapDocument open;
	const auto alone = planShape(open, scratch, bounds, options, "ARCH");
	CHECK(alone.fill.empty());
	// Switched off: the arch alone, also against the walls.
	options.fit = false;
	const auto off = planShape(doc, scratch, box({0, 0, 256}, {128, 128, 384}), options, "ARCH");
	CHECK_FALSE(off.fitted);
	CHECK(off.fill.empty());
	CHECK(off.carved.empty());
}

TEST_CASE("shape fit: a shape standing in a hole of a floor gets the floor filled around it") {
	// Like the platform of the edge rounding (Julius, 30.10.2026): the shape reaches down into a
	// floor carved around its box; around a round shape the hole would stay open.
	MapDocument doc;
	floorWithHole(doc, 0, 128);
	ShapeOptions cylinder;
	cylinder.kind = ShapeOptions::Kind::Cylinder;
	cylinder.sides = 12;
	MapDocument scratch;
	const auto plan = planShape(doc, scratch, box({0, 0, -32}, {128, 128, 96}), cylinder, "PILLAR");
	REQUIRE(plan.ok());
	CHECK_FALSE(plan.fill.empty());
	CHECK(allValid(plan.fill));
	auto [change, refs] = applyShapePlan(doc, plan, "Create cylinder");
	CHECK(filled(doc, {1, 1, -16}));         // the hole's corner beside the pillar: filled
	CHECK(filled(doc, {127, 1, -1}));
	CHECK(filled(doc, {10, 10, -16}));
	CHECK_FALSE(filled(doc, {1, 1, 8}));     // above the floor: open
	CHECK(holders(doc, {64, 64, -16}) == 1); // the pillar itself, nothing on top of it
	// The fill's top is floor: its texture comes from the floor.
	bool floorTop = false;
	for (const auto& solid : plan.fill) {
		for (const auto& side : solid.sides) {
			if (side.plane().normal.z > 0.99 && side.material == "FLOOR") floorTop = true;
		}
	}
	CHECK(floorTop);
}

TEST_CASE("shape fit: a shape sunk into a floor cuts the floor around it") {
	MapDocument doc;
	auto [cf, floor] = edit::createBlock(doc, box({-256, -256, -32}, {256, 256, 0}), "FLOOR");
	const auto before = doc.serialize();
	ShapeOptions cylinder;
	cylinder.kind = ShapeOptions::Kind::Cylinder;
	cylinder.sides = 8;
	MapDocument scratch;
	const auto preview = planShape(doc, scratch, box({0, 0, -32}, {128, 128, 96}), cylinder, "PILLAR");
	REQUIRE(preview.carved.size() == 1);
	CHECK(preview.replacedRefs() == std::vector<SolidRef>{floor});
	CHECK(doc.serialize() == before);   // planning changes nothing
	const auto plan = planShape(doc, doc, box({0, 0, -32}, {128, 128, 96}), cylinder, "PILLAR");
	auto [change, refs] = applyShapePlan(doc, plan, "Create cylinder");
	CHECK(allValid(doc.world.solids));
	CHECK(doc.findSolid(floor));               // a piece of the floor keeps its id
	CHECK(holders(doc, {64, 64, -16}) == 1);   // the pillar only, the floor is cut around it
	CHECK(filled(doc, {200, 200, -16}));       // the rest of the floor stays
	CHECK(filled(doc, {2, 2, -16}));           // beside the pillar inside its box: still floor
	History history;
	history.push(std::move(change));
	history.undo(doc);
	CHECK(doc.serialize() == before);
	// A plain block never cuts or fills (the block tool's everyday brush).
	const auto block = planShape(doc, scratch, box({0, 0, -32}, {128, 128, 96}), ShapeOptions{}, "BLOCK");
	CHECK_FALSE(block.fitted);
	CHECK(block.carved.empty());
}

TEST_CASE("shape fit: settings the box cannot take are reduced, not refused") {
	auto arch = archOptions(180, 64, 100);
	auto limits = limitShapeOptions(box({0, 0, 0}, {64, 64, 64}), arch);
	CHECK(limits.used.wallWidth == 32);
	CHECK(limits.used.sides < 64);
	CHECK(limits.used.sides >= 1);
	REQUIRE(limits.notes.size() == 2);
	CHECK(limits.notes[0] == "wall width limited to 32 units");
	MapDocument scratch;
	const auto plan = planShape(scratch, scratch, box({0, 0, 0}, {64, 64, 64}), arch, "ARCH");
	REQUIRE(plan.ok());
	CHECK(plan.shape.size() == std::size_t(limits.used.sides));   // every piece made, none dropped
	CHECK(allValid(plan.shape));

	ShapeOptions stairs;
	stairs.kind = ShapeOptions::Kind::Stairs;
	stairs.steps = 300;
	CHECK(limitShapeOptions(box({0, 0, 0}, {64, 128, 48}), stairs).used.steps == 48);
	ShapeOptions cylinder;
	cylinder.kind = ShapeOptions::Kind::Cylinder;
	cylinder.sides = 64;
	limits = limitShapeOptions(box({0, 0, 0}, {16, 16, 16}), cylinder);
	CHECK(limits.used.sides == 25);   // no side shorter than 2 units
	CHECK(limits.notes == std::vector<std::string>{"sides limited to 25"});
	// What fits stays as it is.
	CHECK_FALSE(limitShapeOptions(box({0, 0, 0}, {256, 256, 128}), archOptions(180, 8, 16)).limited());
}
