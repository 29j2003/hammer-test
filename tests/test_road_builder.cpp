// test_road_builder.cpp - Road builder: straight roads, curves (no gaps, texture running on),
// slopes, junctions, displacements with a crown, carving into the floor, paths from selected
// floor brushes, one undo step.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/edge_round.h>
#include <sme/history.h>
#include <sme/road_builder.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace sme;

namespace {

const TextureSizeFn SQUARE = [](const std::string&) { return std::pair<int, int>{512, 512}; };

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

RoadPath path(std::initializer_list<Vec3> points) {
	return RoadPath{std::vector<Vec3>(points)};
}

RoadBuildOptions basic() {
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	o.width = 256;
	o.innerRadius = 128;
	o.fit = false;
	return o;
}

const Side* top(const Solid& solid) {
	const Side* best = nullptr;
	for (const auto& side : solid.sides) {
		if (side.plane().normal.z > 0.2 && (!best || side.plane().normal.z > best->plane().normal.z)) best = &side;
	}
	return best;
}

/// The road pieces (road material on top) of a list.
std::vector<const Solid*> roadPieces(const std::vector<Solid>& solids) {
	std::vector<const Solid*> out;
	for (const auto& s : solids) {
		if (const Side* t = top(s); t && t->material == "ROAD/ASPHALT") out.push_back(&s);
	}
	return out;
}

int holders(const std::vector<Solid>& solids, const Vec3& p) {
	return int(std::count_if(solids.begin(), solids.end(), [&](const Solid& s) { return solidContains(s, p, -0.01); }));
}

/// No two brushes overlap (more than rounding: a few cubic units along a long slanted cut is a
/// ten-thousandth of a unit deep).
bool noOverlaps(const std::vector<Solid>& solids) {
	for (std::size_t i = 0; i < solids.size(); ++i) {
		for (std::size_t j = i + 1; j < solids.size(); ++j) {
			const double shared = sharedVolume(solids[i], solids[j]);
			if (shared > 4) {
				MESSAGE("brushes " << i << " and " << j << " share " << shared << " cubic units");
				return false;
			}
		}
	}
	return true;
}

bool allSound(const std::vector<Solid>& solids) {
	return std::all_of(solids.begin(), solids.end(), [](const Solid& s) { return brushProblem(s).empty(); });
}

/// Points along the centre line of a 90° left curve round `centre` with centre radius `r`, from angle a0.
std::vector<Vec3> arcPoints(const Vec3& centre, double r, double a0, double a1, int count, double z) {
	std::vector<Vec3> out;
	for (int i = 0; i <= count; ++i) {
		const double a = a0 + (a1 - a0) * (i + 0.37) / (count + 1);
		out.push_back({centre.x + r * std::cos(a), centre.y + r * std::sin(a), z});
	}
	return out;
}

} // namespace

TEST_CASE("road builder: a straight road is one brush with the texture along it, centred") {
	MapDocument doc;
	auto o = basic();
	const auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}})}, o, SQUARE);
	REQUIRE(plan.road.size() == 1);
	const auto geometry = buildBrushGeometry(plan.road.front());
	CHECK(geometry.bounds.min.x == doctest::Approx(0));
	CHECK(geometry.bounds.max.x == doctest::Approx(1024));
	CHECK(geometry.bounds.min.y == doctest::Approx(-128));
	CHECK(geometry.bounds.max.y == doctest::Approx(128));
	CHECK(geometry.bounds.max.z == doctest::Approx(0));
	CHECK(geometry.bounds.min.z == doctest::Approx(-16));
	const Side* t = top(plan.road.front());
	REQUIRE(t);
	CHECK(t->material == "ROAD/ASPHALT");
	// 256 units span the 512 texels: scale 0.5; the centre line in the texture's middle.
	CHECK(textureCoordinate(*t, {0, 0, 0}).u == doctest::Approx(256));
	CHECK(textureCoordinate(*t, {0, 0, 0}).v == doctest::Approx(0));
	CHECK(textureCoordinate(*t, {512, 0, 0}).v == doctest::Approx(1024));
	CHECK(std::abs(textureCoordinate(*t, {0, 128, 0}).u - textureCoordinate(*t, {0, -128, 0}).u) == doctest::Approx(512));
	// The bottom is nodraw, the open sides get the road material.
	for (const auto& side : plan.road.front().sides) {
		if (side.plane().normal.z < -0.5) CHECK(side.material == "TOOLS/TOOLSNODRAW");
		if (std::abs(side.plane().normal.z) < 0.1) CHECK(side.material == "ROAD/ASPHALT");
	}
}

TEST_CASE("road builder: an L turn becomes arch pieces without gaps, the texture runs on through the curve") {
	MapDocument doc;
	auto o = basic();
	o.segments = 8;
	const auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}, {1024, 1024, 0}})}, o, SQUARE);
	CHECK(plan.curves == 1);
	CHECK(plan.mitred == 0);
	CHECK(plan.radius == doctest::Approx(128));
	REQUIRE(plan.road.size() == 10);   // straight, 8 curve pieces, straight
	CHECK(allSound(plan.road));
	CHECK(noOverlaps(plan.road));
	// Centre radius 256 round (768, 256): every point on the centre line and near both edges is road.
	for (const auto& p : arcPoints({768, 256, 0}, 256, -std::numbers::pi / 2, 0, 40, -8)) CHECK(holders(plan.road, p) >= 1);
	for (const auto& p : arcPoints({768, 256, 0}, 140, -std::numbers::pi / 2, 0, 40, -8)) CHECK(holders(plan.road, p) >= 1);
	for (const auto& p : arcPoints({768, 256, 0}, 370, -std::numbers::pi / 2, 0, 40, -8)) CHECK(holders(plan.road, p) >= 1);
	// Joints: on the line where two pieces meet, both tops give the same texture across (lane
	// lines meet) and the same along at the centre line (dashes run on).
	int joints = 0;
	for (std::size_t i = 0; i + 1 < plan.road.size(); ++i) {
		const auto a = buildBrushGeometry(plan.road[i]), b = buildBrushGeometry(plan.road[i + 1]);
		const Side *ta = top(plan.road[i]), *tb = top(plan.road[i + 1]);
		REQUIRE(ta);
		REQUIRE(tb);
		// Shared top corners.
		std::vector<Vec3> shared;
		for (const auto& fa : a.faces) {
			if (fa.normal.z < 0.5) continue;
			for (const auto& v : fa.vertices) {
				for (const auto& fb : b.faces) {
					if (fb.normal.z < 0.5) continue;
					for (const auto& w : fb.vertices) {
						if (length(v - w) < 0.01) shared.push_back(v);
					}
				}
			}
		}
		REQUIRE(shared.size() == 2);
		++joints;
		const Vec3 mid = (shared[0] + shared[1]) * 0.5;
		for (const Vec3& p : {shared[0], shared[1], mid}) CHECK(textureCoordinate(*ta, p).u == doctest::Approx(textureCoordinate(*tb, p).u).epsilon(0.001));
		CHECK(textureCoordinate(*ta, mid).v == doctest::Approx(textureCoordinate(*tb, mid).v).epsilon(0.001));
	}
	CHECK(joints == 9);
	// The faces where pieces meet are nodraw (both sides of the 9 joints), the open sides not.
	int hidden = 0, open = 0;
	for (const auto& solid : plan.road) {
		for (const auto& side : solid.sides) {
			if (std::abs(side.plane().normal.z) > 0.1) continue;
			(side.material == "TOOLS/TOOLSNODRAW" ? hidden : open) += 1;
		}
	}
	CHECK(hidden == 18);
	CHECK(open == 22);   // two long sides each, and the two ends
}

TEST_CASE("road builder: corners too close together get a smaller curve or a mitre, never a refusal") {
	MapDocument doc;
	auto o = basic();
	o.innerRadius = 512;
	// The second leg is only 300 long: the curve gets tighter.
	auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}, {1024, 300, 0}})}, o, SQUARE);
	CHECK(plan.curves == 1);
	CHECK(plan.radius < 512);
	CHECK(allSound(plan.road));
	CHECK(noOverlaps(plan.road));
	CHECK_FALSE(plan.notes.empty());
	// A zig-zag with legs shorter than the road is wide: mitred joints.
	plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {512, 0, 0}, {512, 100, 0}, {1024, 100, 0}})}, o, SQUARE);
	CHECK(plan.ok());
	CHECK(plan.mitred >= 1);
	CHECK(allSound(plan.road));
}

TEST_CASE("road builder: a ramp and a sloped curve follow the points' heights") {
	MapDocument doc;
	auto o = basic();
	auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 128}})}, o, SQUARE);
	REQUIRE(plan.road.size() == 1);
	const Side* t = top(plan.road.front());
	REQUIRE(t);
	// Measured along the slope: no stretching.
	const double slope = std::sqrt(1024.0 * 1024 + 128 * 128);
	CHECK(textureCoordinate(*t, {1024, 0, 128}).v - textureCoordinate(*t, {0, 0, 0}).v == doctest::Approx(slope / 0.5));
	// A curve going up: split into triangles (a top must be flat), heights continuous.
	plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}, {1024, 1024, 128}})}, o, SQUARE);
	CHECK(plan.curves == 1);
	CHECK(allSound(plan.road));
	CHECK(noOverlaps(plan.road));
	CHECK(plan.road.size() > 10);
	for (const auto& p : arcPoints({768, 256, 0}, 256, -std::numbers::pi / 2, 0, 20, 0)) {
		// The road's top at this point: highest holder's top, between 0 and the curve's end height (32).
		double z = -1;
		for (const auto& s : plan.road) {
			for (double h = 40; h > -20; h -= 0.25) {
				if (solidContains(s, {p.x, p.y, h}, 0.01)) {
					z = std::max(z, h);
					break;
				}
			}
		}
		CHECK(z >= -0.5);
		CHECK(z <= 32.5);
	}
}

TEST_CASE("road builder: a road ending on another one stops at its edge (T), crossing it leaves a gap (X)") {
	MapDocument doc;
	auto o = basic();
	const auto first = planRoadBuild(doc, doc, {path({{0, 0, 0}, {2048, 0, 0}})}, o, SQUARE);
	auto applied = edit::applyRoadBuild(doc, first);
	REQUIRE(applied.second.size() == 1);
	// T: the branch ends at the main road's centre.
	auto plan = planRoadBuild(doc, doc, {path({{1024, 1024, 0}, {1024, 0, 0}})}, o, SQUARE);
	CHECK(plan.junctions == 1);
	REQUIRE(plan.road.size() == 1);
	CHECK(buildBrushGeometry(plan.road.front()).bounds.min.y == doctest::Approx(128));
	CHECK(sharedVolume(plan.road.front(), doc.world.solids.front()) < 0.5);
	CHECK(plan.changed.empty());   // the main road runs through
	// With a junction material the main road's piece in the crossing gets it.
	o.junctionMaterial = "ROAD/PLAIN";
	plan = planRoadBuild(doc, doc, {path({{1024, 1024, 0}, {1024, 0, 0}})}, o, SQUARE);
	REQUIRE(plan.changed.size() == 1);
	const auto& parts = plan.changed.front().pieces;
	CHECK(parts.size() == 3);
	CHECK(parts.front().id == doc.world.solids.front().id);
	CHECK(top(parts.front())->material == "ROAD/PLAIN");
	CHECK(buildBrushGeometry(parts.front()).bounds.min.x == doctest::Approx(896));
	CHECK(buildBrushGeometry(parts.front()).bounds.max.x == doctest::Approx(1152));
	CHECK(allSound(parts));
	// X: a road straight across: two pieces, nothing overlaps.
	o.junctionMaterial.clear();
	plan = planRoadBuild(doc, doc, {path({{1024, 1024, 0}, {1024, -1024, 0}})}, o, SQUARE);
	CHECK(plan.junctions == 1);
	CHECK(plan.road.size() == 2);
	std::vector<Solid> all = plan.road;
	all.push_back(doc.world.solids.front());
	CHECK(noOverlaps(all));
	CHECK(allSound(plan.road));
}

TEST_CASE("road builder: displacements with a raised middle meet exactly and keep the map sealed") {
	MapDocument doc;
	auto o = basic();
	o.displacement = true;
	o.power = 2;
	o.crown = 4;
	o.roughness = 2;
	const auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}, {1024, 1024, 64}})}, o, SQUARE);
	REQUIRE(plan.displacements > 0);
	const auto roads = roadPieces(plan.road);
	CHECK(int(roads.size()) == plan.displacements);
	// Nodraw seals under them, as many as road pieces.
	CHECK(plan.road.size() == 2 * roads.size());
	CHECK(allSound(plan.road));
	// Sewn by construction: sewing moves nothing.
	std::vector<Displacement> disps;
	std::vector<FaceGeometry> faces;
	for (const Solid* s : roads) {
		const auto geometry = buildBrushGeometry(*s);
		for (const auto& face : geometry.faces) {
			const Side& side = s->sides[std::size_t(face.sideIndex)];
			if (auto d = readDisplacement(side)) {
				disps.push_back(*d);
				faces.push_back(face);
			}
		}
	}
	REQUIRE(disps.size() == roads.size());
	std::vector<SewItem> items;
	for (std::size_t i = 0; i < disps.size(); ++i) items.push_back({&disps[i], &faces[i]});
	CHECK(sewDisplacements(items) == 0);
	// The straight piece: the middle 4 higher than the edges (plus the unevenness).
	const auto first = displacementPositions(disps.front(), faces.front());
	double edgeMax = -1e9, middle = -1e9;
	for (const auto& p : first) {
		if (std::abs(std::abs(p.y) - 128) < 0.01) edgeMax = std::max(edgeMax, std::abs(p.z));
		if (std::abs(p.y) < 0.01) middle = std::max(middle, p.z);
	}
	CHECK(edgeMax == doctest::Approx(0).epsilon(0.001));
	CHECK(middle > 2);
	CHECK(middle < 6.5);
	CHECK(roadCrownOffset(0, 128, 4, 0, 10, 20) == doctest::Approx(4));
	CHECK(roadCrownOffset(128, 128, 4, 3, 10, 20) == doctest::Approx(0));
}

TEST_CASE("road builder: the road is carved into the floor and through what stands in its way") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({-512, -512, -64}, {1536, 512, 0}), "NATURE/GRASS"));
	const int floorId = doc.world.solids.back().id;
	doc.world.solids.push_back(makeBlock(doc, box({400, -300, 0}, {500, 300, 48}), "BRICK/WALL"));   // a low wall across
	doc.world.solids.push_back(makeBlock(doc, box({-4096, -4096, 4000}, {4096, 4096, 4016}), "TOOLS/TOOLSSKYBOX"));
	const double floorVolume = solidVolume(doc.world.solids.front());
	auto o = basic();
	o.fit = true;
	o.kerbs = true;
	const auto plan = planRoadBuild(doc, doc, {path({{0, 0, 0}, {1024, 0, 0}})}, o, SQUARE);
	CHECK(plan.carved == 2);   // floor and wall, not the sky
	std::vector<Solid> after = plan.resultSolids();
	CHECK(allSound(after));
	CHECK(noOverlaps(after));
	// The floor keeps its id and loses exactly the road's and kerbs' volume.
	const auto floor = std::find_if(plan.changed.begin(), plan.changed.end(), [&](const auto& r) { return r.ref.solidId == floorId; });
	REQUIRE(floor != plan.changed.end());
	CHECK(floor->pieces.front().id == floorId);
	double left = 0;
	for (const auto& p : floor->pieces) left += solidVolume(p);
	double road = 0;
	for (const auto& s : plan.road) road += solidVolume(s);
	for (const auto& s : plan.kerbs) road += solidVolume(s) * 16.0 / 24.0;   // the kerbs' part below the floor's top
	CHECK(left == doctest::Approx(floorVolume - road).epsilon(0.001));
	// Few pieces: the leftovers are joined again where they form one brush.
	CHECK(floor->pieces.size() <= 5);   // four strips round the road and the slab under it
	// Kerbs follow both sides.
	CHECK(plan.kerbs.size() == 2);
	// The wall is cut where the road and kerbs pass (nothing above the road).
	CHECK(holders(after, {450, 0, 20}) == 0);
	CHECK(holders(after, {450, 250, 20}) == 1);
}

TEST_CASE("road builder: selected floor blocks of an L become one curved road, the rest stays as fill") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, -128, -16}, {768, 128, 0}), "ROAD/ASPHALT"));
	doc.world.solids.push_back(makeBlock(doc, box({768, -128, -16}, {1024, 128, 0}), "ROAD/ASPHALT"));
	doc.world.solids.push_back(makeBlock(doc, box({768, 128, -16}, {1024, 1024, 0}), "ROAD/ASPHALT"));
	std::vector<SolidRef> refs;
	for (const auto& s : doc.world.solids) refs.push_back({0, s.id});
	RoadBuildOptions o;
	std::vector<std::string> notes;
	const auto paths = roadPathsFromSolids(doc, refs, &o, &notes);
	REQUIRE(paths.size() == 1);
	CHECK(o.width == doctest::Approx(256));
	CHECK(o.thickness == doctest::Approx(16));
	CHECK(o.material == "ROAD/ASPHALT");
	REQUIRE(paths.front().points.size() == 3);
	CHECK(length(paths.front().points[0] - Vec3{0, 0, 0}) < 0.01);
	CHECK(length(paths.front().points[1] - Vec3{896, 0, 0}) < 0.01);
	CHECK(length(paths.front().points[2] - Vec3{896, 1024, 0}) < 0.01);
	o.innerRadius = 0;
	o.fillMaterial = "CONCRETE/SIDEWALK";
	o.fit = false;
	const auto plan = planRoadBuild(doc, doc, paths, o, SQUARE, refs);
	CHECK(plan.curves == 1);
	std::vector<Solid> after = plan.resultSolids();
	CHECK(allSound(after));
	CHECK(noOverlaps(after));
	// The whole L is still filled (road or fill), and the outer corner got the fill material.
	for (double x = 8; x < 1024; x += 64) {
		for (double y = -120; y < 1024; y += 64) {
			if (x < 768 && y > 128) continue;
			CHECK(holders(after, {x, y, -8}) == 1);
		}
	}
	bool sidewalk = false;
	for (const auto& r : plan.changed) {
		for (const auto& p : r.pieces) sidewalk = sidewalk || (top(p) && top(p)->material == "CONCRETE/SIDEWALK");
	}
	CHECK(sidewalk);
	// One undo step.
	const std::string before = doc.serialize();
	History history;
	auto [change, made] = edit::applyRoadBuild(doc, planRoadBuild(doc, doc, paths, o, SQUARE, refs));
	CHECK(change.name == "Build road");
	CHECK_FALSE(made.empty());
	history.push(std::move(change));
	CHECK(doc.serialize() != before);
	history.undo(doc);
	// The same brushes again (undo puts them back at the end of the list).
	std::sort(doc.world.solids.begin(), doc.world.solids.end(), [](const Solid& a, const Solid& b) { return a.id < b.id; });
	CHECK(keyValueDifferences(before, doc.serialize()).empty());
}

TEST_CASE("road builder: a T of selected blocks gives a main road and a branch that joins it") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, -128, -16}, {2048, 128, 0}), "ROAD/ASPHALT"));
	doc.world.solids.push_back(makeBlock(doc, box({896, 128, -16}, {1152, 1024, 0}), "ROAD/ASPHALT"));
	std::vector<SolidRef> refs;
	for (const auto& s : doc.world.solids) refs.push_back({0, s.id});
	RoadBuildOptions o;
	const auto paths = roadPathsFromSolids(doc, refs, &o);
	REQUIRE(paths.size() == 2);
	o.fit = false;
	const auto plan = planRoadBuild(doc, doc, paths, o, SQUARE, refs);
	CHECK(plan.junctions == 1);
	std::vector<Solid> all = plan.resultSolids();
	CHECK(allSound(all));
	CHECK(noOverlaps(all));
	CHECK(holders(all, {1024, 600, -8}) == 1);
	CHECK(holders(all, {1024, 0, -8}) == 1);
}

TEST_CASE("road builder: a mapper's blocked-in road (pieces of different widths, a narrow joint, an L) becomes one road") {
	// The road of an editor report (6 floor blocks, 24 units thick, 416 to 528 wide).
	MapDocument doc;
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{-2160, -1168, -16}, {-1472, -688, 8}},
	                                                            {{-1472, -1168, -16}, {-768, -688, 8}},
	                                                            {{-768, -1296, -16}, {-624, -688, 8}},
	                                                            {{-624, -1296, -16}, {784, -880, 8}},
	                                                            {{256, -2352, -16}, {784, -1296, 8}},
	                                                            {{256, -2880, -16}, {784, -2352, 8}}}) {
		doc.world.solids.push_back(makeBlock(doc, box(a, b), "CONCRETE/BLENDROAD_BROKEN01"));
	}
	std::vector<SolidRef> refs;
	for (const auto& s : doc.world.solids) refs.push_back({0, s.id});
	RoadBuildOptions o;
	const auto paths = roadPathsFromSolids(doc, refs, &o);
	REQUIRE(paths.size() == 1);
	CHECK(o.width == doctest::Approx(480));
	CHECK(o.thickness == doctest::Approx(24));
	const auto& points = paths.front().points;
	REQUIRE(points.size() >= 3);
	CHECK(length(points.front() - Vec3{-2160, -928, 8}) < 0.01);
	CHECK(length(points.back() - Vec3{520, -2880, 8}) < 0.01);
	o.fit = false;
	o.innerRadius = 128;
	const auto plan = planRoadBuild(doc, doc, paths, o, SQUARE, refs);
	CHECK(plan.ok());
	CHECK(plan.curves >= 1);
	CHECK(plan.skipped == 0);
	std::vector<Solid> after = plan.resultSolids();
	CHECK(allSound(after));
	CHECK(noOverlaps(after));
	// Nothing of the old road is lost: every point of it is road or fill.
	for (const auto& ref : refs) {
		const auto bounds = buildBrushGeometry(*doc.findSolid(ref)).bounds;
		for (double x = bounds.min.x + 5; x < bounds.max.x; x += 37) {
			for (double y = bounds.min.y + 5; y < bounds.max.y; y += 37) CHECK(holders(after, {x, y, 0}) == 1);
		}
	}
}
