// test_grid_snap.cpp - Snapping brush corners to the grid and the "off the grid" check.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/csg.h>
#include <sme/edge_round.h>
#include <sme/editing.h>
#include <sme/grid_snap.h>

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

bool allOnGrid(const Solid& solid) {
	return offGridCorners(solid) == 0;
}

} // namespace

TEST_CASE("grid snap: fractional corners move to whole units, the faces keep their materials") {
	MapDocument doc;
	auto [made, ref] = edit::createBlock(doc, box({0.3, -0.4, 0}, {64.45, 64, 32.2}), "BRICK/WALL01");
	(void)made;
	const Solid before = *doc.findSolid(ref);
	CHECK(offGridCorners(before) == 8);
	const auto snapped = snapSolidToGrid(before);
	REQUIRE(snapped);
	CHECK(allOnGrid(*snapped));
	CHECK(brushProblem(*snapped).empty());
	const auto g = buildBrushGeometry(*snapped);
	CHECK(g.bounds.min.x == doctest::Approx(0));
	CHECK(g.bounds.min.y == doctest::Approx(0));
	CHECK(g.bounds.max.x == doctest::Approx(64));
	CHECK(g.bounds.max.z == doctest::Approx(32));
	for (std::size_t i = 0; i < snapped->sides.size(); ++i) {
		CHECK(snapped->sides[i].id == before.sides[i].id);
		CHECK(snapped->sides[i].material == "BRICK/WALL01");
	}
	// Already on the grid: nothing to do.
	CHECK_FALSE(snapSolidToGrid(*snapped));
}

TEST_CASE("grid snap: a slanted brush keeps its faces; neighbours that shared a corner still share it") {
	MapDocument doc;
	// Two wedges side by side sharing a slanted top (corners at fractions, like an arch piece).
	const std::vector<Vec3> a = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}, {0, 0, 10.3}, {0, 64, 10.3}, {64, 0, 20.6}, {64, 64, 20.6}};
	const std::vector<Vec3> b = {{64, 0, 0}, {128, 0, 0}, {128, 64, 0}, {64, 64, 0}, {64, 0, 20.6}, {64, 64, 20.6}, {128, 0, 30.9}, {128, 64, 30.9}};
	auto sa = solidFromPoints(doc, a, "DEV/DEV_MEASUREGENERIC01");
	auto sb = solidFromPoints(doc, b, "DEV/DEV_MEASUREGENERIC01");
	REQUIRE(sa);
	REQUIRE(sb);
	const auto na = snapSolidToGrid(*sa), nb = snapSolidToGrid(*sb);
	REQUIRE(na);
	REQUIRE(nb);
	CHECK(na->sides.size() == sa->sides.size());
	const auto ca = solidCorners(*na), cb = solidCorners(*nb);
	int shared = 0;
	for (const auto& p : ca) shared += std::any_of(cb.begin(), cb.end(), [&](const Vec3& q) { return length(p - q) < 0.01; }) ? 1 : 0;
	CHECK(shared == 4);
	CHECK(sharedVolume(*na, *nb) < 0.01);
}

TEST_CASE("grid snap: a brush that would lose a face stays as it is and is counted as refused") {
	MapDocument doc;
	// Half a unit thick between whole units: both faces would land on the same grid line.
	auto [made, thin] = edit::createBlock(doc, box({0, 0, 0.6}, {64, 64, 0.9}), "DEV/DEV_MEASUREGENERIC01");
	(void)made;
	auto [made2, fine] = edit::createBlock(doc, box({0.2, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	(void)made2;
	CHECK_FALSE(snapSolidToGrid(*doc.findSolid(thin)));
	Selection selection;
	selection.solids = {thin, fine};
	int snapped = 0, refused = 0;
	History history;
	history.push(edit::snapToGrid(doc, selection, 1, &snapped, &refused));
	CHECK(snapped == 1);
	CHECK(refused == 1);
	CHECK(allOnGrid(*doc.findSolid(fine)));
	CHECK(history.undo(doc));
	CHECK(offGridCorners(*doc.findSolid(fine)) == 4);
}

TEST_CASE("grid snap: displacements are left alone") {
	MapDocument doc;
	auto [made, ref] = edit::createBlock(doc, box({0.5, 0, 0}, {64, 64, 16}), "NATURE/GRASS");
	(void)made;
	Solid* solid = doc.findSolid(ref);
	KvNode info = KvNode::makeBlock("dispinfo");
	info.set("power", "2");
	solid->sides[0].extra.push_back(info);
	CHECK_FALSE(snapSolidToGrid(*doc.findSolid(ref)));
}

TEST_CASE("map check: brushes off the grid are listed once, the autofix snaps them") {
	MapDocument doc;
	auto [m1, a] = edit::createBlock(doc, box({0.25, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	auto [m2, b] = edit::createBlock(doc, box({128, 0, 0}, {192, 64, 64.7}), "DEV/DEV_MEASUREGENERIC01");
	auto [m3, c] = edit::createBlock(doc, box({256, 0, 0}, {320, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	(void)m1;
	(void)m2;
	(void)m3;
	(void)c;
	const auto problems = checkMap(doc, {});
	const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::OffGridCorners; });
	REQUIRE(it != problems.end());
	CHECK(it->severity == Problem::Severity::Info);
	CHECK(it->objects.solids.size() == 2);
	CHECK(it->message.find("2 brushes") != std::string::npos);
	(void)applyFix(doc, *it, nullptr);
	CHECK(allOnGrid(*doc.findSolid(a)));
	CHECK(allOnGrid(*doc.findSolid(b)));
	const auto again = checkMap(doc, {});
	CHECK(std::none_of(again.begin(), again.end(), [](const Problem& p) { return p.kind == Problem::Kind::OffGridCorners; }));
}
