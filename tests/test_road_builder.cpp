// test_road_builder.cpp - Road builder: straight roads, curves (no gaps, texture running on),
// slopes, junctions, displacements with a crown, carving into the floor, paths from selected
// floor brushes (the street they make: centred road, parking lanes, curves round the kerbs, no
// holes), one undo step.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/edge_round.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/road_builder.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
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

// --- From selected floor brushes: the street and the road through it ---------------------------

namespace {

/// Julius' street (editor report of 01.11.2026): an L of floor blocks 24 thick (top at 8), 480
/// wide along x, a jog to 416 wide, then 528 wide along y; sidewalks (top at 16) beside it.
struct Street {
	MapDocument doc;
	std::vector<SolidRef> floor;
	std::vector<SolidRef> sidewalks;
	SolidRef innerCorner;   ///< The sidewalk at the L's inside corner (256, -1296)
};

const std::vector<std::pair<Vec3, Vec3>> STREET_FLOOR{{{-2160, -1168, -16}, {-1472, -688, 8}}, {{-1472, -1168, -16}, {-768, -688, 8}},
                                                     {{-768, -1296, -16}, {-624, -688, 8}},   {{-624, -1296, -16}, {784, -880, 8}},
                                                     {{256, -2352, -16}, {784, -1296, 8}},    {{256, -2880, -16}, {784, -2352, 8}}};

/// `piece`: 0 = the blocks as they are; else every block cut into tiles about that big.
Street julius(double piece = 0) {
	Street s;
	for (const auto& [a, b] : STREET_FLOOR) {
		if (piece <= 0) {
			s.doc.world.solids.push_back(makeBlock(s.doc, box(a, b), "CONCRETE/STREET"));
			s.floor.push_back({0, s.doc.world.solids.back().id});
			continue;
		}
		// Tiles of uneven size (piece, piece / 2 + 16, ...), like a floor blocked in by hand.
		std::vector<double> xs{a.x}, ys{a.y};
		for (int k = 0; xs.back() < b.x; ++k) xs.push_back(std::min(b.x, xs.back() + (k % 2 ? piece : piece / 2 + 16)));
		for (int k = 0; ys.back() < b.y; ++k) ys.push_back(std::min(b.y, ys.back() + (k % 3 ? piece : piece / 2)));
		for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
			for (std::size_t j = 0; j + 1 < ys.size(); ++j) {
				s.doc.world.solids.push_back(makeBlock(s.doc, box({xs[i], ys[j], a.z}, {xs[i + 1], ys[j + 1], b.z}), "CONCRETE/STREET"));
				s.floor.push_back({0, s.doc.world.solids.back().id});
			}
		}
	}
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{-2160, -688, -16}, {-624, -592, 16}},  {{-624, -880, -16}, {-528, -592, 16}},
	                                                            {{-528, -880, -16}, {880, -784, 16}},   {{784, -2880, -16}, {880, -880, 16}},
	                                                            {{-2160, -1264, -16}, {-768, -1168, 16}}, {{-768, -1392, -16}, {256, -1296, 16}},
	                                                            {{160, -2880, -16}, {256, -1392, 16}}}) {
		s.doc.world.solids.push_back(makeBlock(s.doc, box(a, b), "CONCRETE/SIDEWALK"));
		s.sidewalks.push_back({0, s.doc.world.solids.back().id});
	}
	s.innerCorner = s.sidewalks[5];
	return s;
}

Vec3 flat(const Vec3& v) {
	return {v.x, v.y, 0};
}

FaceRef facing(const MapDocument& doc, const SolidRef& ref, Vec3 normal) {
	for (const auto& side : doc.findSolid(ref)->sides) {
		if (dot(side.plane().normal, normal) > 0.999) return {ref, side.id};
	}
	return {};
}

/// The map after the plan (a copy).
MapDocument applied(const MapDocument& doc, const RoadBuildPlan& plan) {
	MapDocument after = doc;
	(void)edit::applyRoadBuild(after, plan);
	return after;
}

/// Every point of the old floor (on a 7.7 unit raster, just below its top) is in exactly one
/// brush afterwards: no hole, nothing doubled.
void checkCovered(const MapDocument& before, const std::vector<SolidRef>& floor, const MapDocument& after) {
	int holes = 0, doubled = 0, samples = 0;
	for (const auto& ref : floor) {
		const Solid* solid = before.findSolid(ref);
		REQUIRE(solid);
		const auto b = buildBrushGeometry(*solid).bounds;
		const Side* t = top(*solid);
		const Plane plane = t ? t->plane() : Plane{{0, 0, 1}, b.max.z};
		for (double x = b.min.x + 1.3; x < b.max.x; x += 7.7) {
			for (double y = b.min.y + 1.7; y < b.max.y; y += 7.7) {
				// Just below the top (a ramp's top slopes).
				const double z = std::abs(plane.normal.z) > 1e-6 ? (plane.dist - plane.normal.x * x - plane.normal.y * y) / plane.normal.z : b.max.z;
				const Vec3 p{x, y, z - 4};
				if (!solidContains(*solid, p, -0.05)) continue;
				++samples;
				int strict = 0, loose = 0;
				for (const auto& s : after.world.solids) {
					strict += solidContains(s, p, -0.05) ? 1 : 0;
					loose += solidContains(s, p, 0.05) ? 1 : 0;
				}
				if (loose == 0) {
					if (holes < 5) MESSAGE("hole at " << x << " " << y);
					++holes;
				}
				if (strict > 1) {
					if (doubled < 5) MESSAGE("doubled at " << x << " " << y);
					++doubled;
				}
			}
		}
	}
	CHECK(samples > 100);
	CHECK(holes == 0);
	CHECK(doubled == 0);
}

/// The road's pieces in the map afterwards (road material on top).
std::vector<const Solid*> roadIn(const MapDocument& doc, const std::string& material) {
	std::vector<const Solid*> out;
	for (const auto& s : doc.world.solids) {
		if (const Side* t = top(s); t && t->material == material) out.push_back(&s);
	}
	return out;
}

bool roadAt(const std::vector<const Solid*>& road, double x, double y, double z) {
	return std::any_of(road.begin(), road.end(), [&](const Solid* s) { return solidContains(*s, {x, y, z}, -0.05); });
}

} // namespace

TEST_CASE("road from selection: Julius' street - centred road, parking lanes both sides, curves round the corners, no holes") {
	Street street = julius();
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	o.fit = true;
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(street.doc, street.doc, street.floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	REQUIRE(plan.ok());
	CHECK(corridor.floor.size() == 6);
	CHECK(corridor.width == doctest::Approx(416));   // the narrowest part (the middle)
	CHECK(corridor.parking == doctest::Approx(96));
	CHECK(corridor.roadWidth == doctest::Approx(224));
	CHECK(corridor.thickness == doctest::Approx(24));
	REQUIRE(corridor.paths.size() == 1);
	const auto& points = corridor.paths.front().points;
	REQUIRE(points.size() >= 4);
	// Starts and ends exactly at the street's ends, in the middle between the kerbs.
	const Vec3 first = points.front(), last = points.back();
	CHECK(((std::abs(first.x + 2160) < 0.5 && std::abs(first.y + 928) < 0.5 && std::abs(last.x - 520) < 0.5 && std::abs(last.y + 2880) < 0.5) ||
	       (std::abs(last.x + 2160) < 0.5 && std::abs(last.y + 928) < 0.5 && std::abs(first.x - 520) < 0.5 && std::abs(first.y + 2880) < 0.5)));
	CHECK(first.z == doctest::Approx(8));
	CHECK(plan.curves >= 2);
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(street.doc, plan);
	checkCovered(street.doc, street.floor, after);
	const auto road = roadIn(after, "ROAD/ASPHALT");
	// Road in the middle, parking lanes free of it, sidewalks untouched.
	for (const double x : {-2000.0, -1600.0, -1100.0}) {
		CHECK(roadAt(road, x, -928, 0));
		CHECK_FALSE(roadAt(road, x, -688 - 56, 0));
		CHECK_FALSE(roadAt(road, x, -1168 + 56, 0));
	}
	for (const double x : {-300.0, 0.0}) {
		CHECK(roadAt(road, x, -1088, 0));
		CHECK_FALSE(roadAt(road, x, -880 - 40, 0));
		CHECK_FALSE(roadAt(road, x, -1296 + 40, 0));
	}
	for (const double y : {-1800.0, -2400.0, -2800.0}) {
		CHECK(roadAt(road, 520, y, 0));
		CHECK_FALSE(roadAt(road, 256 + 60, y, 0));
		CHECK_FALSE(roadAt(road, 784 - 60, y, 0));
	}
	for (const auto& ref : street.sidewalks) {
		const Solid* walk = after.findSolid(ref);
		REQUIRE(walk);
		CHECK(walk->sides.size() == street.doc.findSolid(ref)->sides.size());
		for (const Solid* r : road) CHECK(sharedVolume(*r, *walk) < 1);
	}
	// The parking lanes keep the floor's own material (it is not the road material).
	bool parking = false;
	for (const auto& s : after.world.solids) {
		if (solidContains(s, {-1600, -688 - 56, 0}, -0.05) && top(s)) parking = top(s)->material == "CONCRETE/STREET";
	}
	CHECK(parking);
}

TEST_CASE("road from selection: the same street blocked in from many small blocks gives the same road") {
	Street big = julius();
	Street small = julius(112);
	CHECK(small.floor.size() > 150);
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	RoadCorridor a, b;
	const auto planA = planRoadFromSelection(big.doc, big.doc, big.floor, o, SQUARE, &a);
	const auto planB = planRoadFromSelection(small.doc, small.doc, small.floor, o, SQUARE, &b);
	INFO(describeRoadCorridor(b));
	REQUIRE(planB.ok());
	CHECK(b.width == doctest::Approx(a.width));
	CHECK(b.roadWidth == doctest::Approx(a.roadWidth));
	REQUIRE(a.paths.size() == 1);
	REQUIRE(b.paths.size() == 1);
	REQUIRE(a.paths.front().points.size() == b.paths.front().points.size());
	for (std::size_t i = 0; i < a.paths.front().points.size(); ++i) CHECK(length(a.paths.front().points[i] - b.paths.front().points[i]) < 2);
	CHECK(planB.skipped == 0);
	CHECK(allSound(planB.resultSolids()));
	checkCovered(small.doc, small.floor, applied(small.doc, planB));
}

TEST_CASE("road from selection: only the two blocks of the corner give a curve inside them") {
	Street street = julius();
	const std::vector<SolidRef> corner{street.floor[3], street.floor[4]};   // 1408 x 416 along x, 528 x 1056 along y
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(street.doc, street.doc, corner, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	REQUIRE(plan.ok());
	REQUIRE(corridor.paths.size() == 1);
	CHECK(plan.curves == 1);
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(street.doc, plan);
	checkCovered(street.doc, corner, after);
	// Nothing outside the two blocks changed.
	for (const auto& ref : {street.floor[0], street.floor[1], street.floor[2], street.floor[5]}) {
		CHECK(after.findSolid(ref)->sides.size() == street.doc.findSolid(ref)->sides.size());
	}
	const auto road = roadIn(after, "ROAD/ASPHALT");
	CHECK(roadAt(road, 0, -1088, 0));
	CHECK(roadAt(road, 520, -2000, 0));
	// The curve's middle keeps clear of the inside corner (256, -1296) by the parking lane.
	for (const Solid* r : road) {
		for (const auto& v : solidCorners(*r)) CHECK(length(flat(v - Vec3{256, -1296, 0})) > corridor.parking - 16);
	}
}

TEST_CASE("road from selection: a rounded sidewalk corner - the curve runs round its middle, no gap beside it") {
	Street street = julius();
	// Round the sidewalk's corner at the L's inside (Julius' screenshot: a gap there).
	REQUIRE(edit::roundEdge(street.doc, facing(street.doc, street.innerCorner, {1, 0, 0}), facing(street.doc, street.innerCorner, {0, 1, 0}), 128, 0));
	// The floor is everything with its top at 8 now (the rounding filled the corner's foot).
	std::vector<SolidRef> floor;
	for (const auto& s : street.doc.world.solids) {
		const auto b = buildBrushGeometry(s).bounds;
		if (std::abs(b.max.z - 8) < 0.01) floor.push_back({0, s.id});
	}
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(street.doc, street.doc, floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	REQUIRE(plan.ok());
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(street.doc, plan);
	checkCovered(street.doc, floor, after);
	// The rounded sidewalk stands where it stood; the road keeps the lane clear of its curve.
	const Solid* walk = after.findSolid(street.innerCorner);
	REQUIRE(walk);
	CHECK(brushProblem(*walk).empty());
	const auto road = roadIn(after, "ROAD/ASPHALT");
	for (const Solid* r : road) CHECK(sharedVolume(*r, *walk) < 1);
	// The curve at that corner: inner radius about the rounding's plus the lane.
	const auto& radii = corridor.paths.front().innerRadius;
	const double biggest = *std::max_element(radii.begin(), radii.end());
	CHECK(biggest >= 128 + corridor.parking - 48);
	// Next to the rounding (between it and the road) is floor, not a hole.
	const Vec3 rounding{256 - 128, -1296 + 128, 0};   // the rounding's middle
	for (const double angle : {0.3, 0.8, 1.2}) {
		const Vec3 p = rounding + Vec3{std::cos(angle), -std::sin(angle), 0} * (128 + 24) + Vec3{0, 0, 0};
		const Vec3 q{p.x, p.y, 0};
		CHECK(std::any_of(after.world.solids.begin(), after.world.solids.end(), [&](const Solid& s) { return solidContains(s, q, 0.05); }));
	}
}

TEST_CASE("road from selection: a T street - a main road and a side street that starts on its centre line") {
	MapDocument doc;
	std::vector<SolidRef> floor;
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{0, -240, -16}, {784, 240, 8}}, {{784, -240, -16}, {1264, 240, 8}},
	                                                            {{1264, -240, -16}, {2560, 240, 8}}, {{784, 240, -16}, {1264, 1600, 8}}}) {
		doc.world.solids.push_back(makeBlock(doc, box(a, b), "CONCRETE/STREET"));
		floor.push_back({0, doc.world.solids.back().id});
	}
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(doc, doc, floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	REQUIRE(plan.ok());
	REQUIRE(corridor.paths.size() == 2);
	CHECK(corridor.roadWidth == doctest::Approx(288));   // 480 - 2 x 96
	const auto& main = corridor.paths[0].points;
	const auto& side = corridor.paths[1].points;
	CHECK(std::abs(main.front().y) < 0.5);
	CHECK(std::abs(main.back().y) < 0.5);
	CHECK(std::min(main.front().x, main.back().x) == doctest::Approx(0).epsilon(0.001));
	CHECK(std::max(main.front().x, main.back().x) == doctest::Approx(2560).epsilon(0.001));
	CHECK(length(side.front() - Vec3{1024, 0, 8}) < 1);
	CHECK(length(side.back() - Vec3{1024, 1600, 8}) < 1);
	CHECK(plan.junctions == 1);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(doc, plan);
	checkCovered(doc, floor, after);
	const auto road = roadIn(after, "ROAD/ASPHALT");
	CHECK(roadAt(road, 1024, 0, 0));
	CHECK(roadAt(road, 1024, 800, 0));
	CHECK(roadAt(road, 300, 0, 0));
	CHECK_FALSE(roadAt(road, 300, 200, 0));
}

TEST_CASE("road from selection: whole width, selection with a sidewalk, one undo step") {
	Street street = julius();
	std::vector<SolidRef> picked = street.floor;
	picked.push_back(street.sidewalks[0]);   // selected by mistake: another height, stays
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	o.wholeWidth = true;
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(street.doc, street.doc, picked, o, SQUARE, &corridor);
	REQUIRE(plan.ok());
	CHECK(corridor.ignored.size() == 1);
	CHECK(corridor.parking == 0);
	CHECK(corridor.roadWidth == doctest::Approx(416));
	CHECK(std::none_of(plan.changed.begin(), plan.changed.end(), [&](const RoadBuildPlan::Replacement& r) { return r.ref == street.sidewalks[0]; }));
	// The suggestion for the dialog.
	RoadBuildOptions suggested;
	const auto paths = roadPathsFromSolids(street.doc, street.floor, &suggested);
	CHECK(paths.size() == 1);
	CHECK(suggested.width == doctest::Approx(224));
	CHECK(suggested.thickness == doctest::Approx(24));
	CHECK(suggested.material == "CONCRETE/STREET");
	// One undo step.
	MapDocument doc = street.doc;
	const std::string before = doc.serialize();
	History history;
	auto [change, made] = edit::applyRoadBuild(doc, planRoadFromSelection(doc, doc, street.floor, RoadBuildOptions{}, SQUARE));
	CHECK(change.name == "Build road");
	CHECK_FALSE(made.empty());
	history.push(std::move(change));
	CHECK(doc.serialize() != before);
	history.undo(doc);
	std::sort(doc.world.solids.begin(), doc.world.solids.end(), [](const Solid& a, const Solid& b) { return a.id < b.id; });
	CHECK(keyValueDifferences(before, doc.serialize()).empty());
}

namespace {

/// The kerbs of a map afterwards (kerb material on top).
std::vector<const Solid*> kerbsIn(const MapDocument& doc) {
	return roadIn(doc, "CONCRETE/KERB");
}

/// Common checks for kerbs at the street's edge: no hole, nothing doubled, every brush sound,
/// kerbs never on the road, all of the map's brushes free of overlaps near the street.
void checkEdgeKerbs(const MapDocument& before, const std::vector<SolidRef>& floor, const RoadBuildPlan& plan) {
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(before, plan);
	checkCovered(before, floor, after);
	CHECK(noOverlaps(after.world.solids));
	const auto road = roadIn(after, "ROAD/ASPHALT");
	const auto kerbs = kerbsIn(after);
	CHECK_FALSE(kerbs.empty());
	for (const Solid* k : kerbs) {
		for (const Solid* r : road) CHECK(sharedVolume(*k, *r) < 1);
	}
}

RoadBuildOptions edgeKerbOptions() {
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT";
	o.kerbs = true;
	o.kerbWidth = 16;
	o.kerbHeight = 8;
	o.kerbMaterial = "CONCRETE/KERB";
	o.fillMaterial = "CONCRETE/PARKING";
	return o;
}

bool kerbAt(const MapDocument& doc, double x, double y, double z) {
	const auto kerbs = kerbsIn(doc);
	return std::any_of(kerbs.begin(), kerbs.end(), [&](const Solid* s) { return solidContains(*s, {x, y, z}, -0.05); });
}

} // namespace

TEST_CASE("road from selection: kerbs at the street's edge - between parking lane and sidewalk, flush with it, not beside the road") {
	Street street = julius();
	const RoadBuildOptions o = edgeKerbOptions();
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(street.doc, street.doc, street.floor, o, SQUARE, &corridor);
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	CHECK(corridor.parking == doctest::Approx(96));
	checkEdgeKerbs(street.doc, street.floor, plan);
	const MapDocument after = applied(street.doc, plan);
	// Along the sidewalks: a kerb 16 wide, its top flush with the sidewalk (16, the street is at 8).
	for (const double x : {-2000.0, -1600.0, -1100.0}) {
		CHECK(kerbAt(after, x, -688 - 8, 12));
		CHECK(kerbAt(after, x, -1168 + 8, 12));
		CHECK_FALSE(kerbAt(after, x, -688 - 8, 17));
		CHECK_FALSE(kerbAt(after, x, -688 - 24, 4));   // the parking lane itself stays low
	}
	for (const double y : {-1800.0, -2400.0}) {
		CHECK(kerbAt(after, 256 + 8, y, 12));
		CHECK(kerbAt(after, 784 - 8, y, 12));
	}
	// Not beside the road (the road's edge is 112 from the middle at -928).
	for (const double x : {-2000.0, -1600.0}) {
		CHECK_FALSE(kerbAt(after, x, -928 + 112 + 8, 4));
		CHECK_FALSE(kerbAt(after, x, -928 - 112 - 8, 4));
	}
	// The street's open ends (nothing raised there) get none.
	CHECK_FALSE(kerbAt(after, -2160 + 8, -800, 4));
	// The parking lanes get their own material, the road keeps its own.
	bool parking = false;
	for (const auto& s : after.world.solids) {
		if (solidContains(s, {-1600, -688 - 56, 0}, -0.05) && top(s)) parking = top(s)->material == "CONCRETE/PARKING";
	}
	CHECK(parking);
	// Kerbs beside the road on request (the old way).
	RoadBuildOptions along = o;
	along.kerbsAlongRoad = true;
	const auto alongPlan = planRoadFromSelection(street.doc, street.doc, street.floor, along, SQUARE);
	const MapDocument alongAfter = applied(street.doc, alongPlan);
	CHECK(kerbAt(alongAfter, -1600, -928 + 112 + 8, 12));
	CHECK_FALSE(kerbAt(alongAfter, -1600, -688 - 8, 12));
}

TEST_CASE("road from selection: kerbs at the street's edge follow a rounded sidewalk corner and the street's tiles") {
	Street street = julius(112);
	REQUIRE(edit::roundEdge(street.doc, facing(street.doc, street.innerCorner, {1, 0, 0}), facing(street.doc, street.innerCorner, {0, 1, 0}), 128, 0));
	std::vector<SolidRef> floor;
	for (const auto& s : street.doc.world.solids) {
		const auto b = buildBrushGeometry(s).bounds;
		if (std::abs(b.max.z - 8) < 0.01) floor.push_back({0, s.id});
	}
	const RoadBuildOptions o = edgeKerbOptions();
	const auto plan = planRoadFromSelection(street.doc, street.doc, floor, o, SQUARE);
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	checkEdgeKerbs(street.doc, floor, plan);
	const MapDocument after = applied(street.doc, plan);
	// From the street towards the rounded corner (its middle inside the sidewalk, near (128,
	// -1424)): the first raised thing on the way is a kerb, never the sidewalk itself.
	const Vec3 inside{128, -1424, 12};
	for (const Vec3 from : {Vec3{420, -1180, 12}, Vec3{330, -1150, 12}, Vec3{450, -1260, 12}}) {
		const Vec3 dir = normalize(inside - from);
		std::string hit;
		for (double t = 0; t < length(inside - from) && hit.empty(); t += 1) {
			const Vec3 p = from + dir * t;
			if (kerbAt(after, p.x, p.y, 12)) hit = "kerb";
			else if (std::any_of(after.world.solids.begin(), after.world.solids.end(), [&](const Solid& s) { return solidContains(s, p, -0.05); })) hit = "other";
		}
		CAPTURE(from);
		CHECK(hit == "kerb");
	}
}

TEST_CASE("road from selection: kerbs at a T street's edges, and before a wall a kerb of its own height") {
	MapDocument doc;
	std::vector<SolidRef> floor;
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{0, -240, -16}, {784, 240, 8}}, {{784, -240, -16}, {1264, 240, 8}},
	                                                            {{1264, -240, -16}, {2560, 240, 8}}, {{784, 240, -16}, {1264, 1600, 8}}}) {
		doc.world.solids.push_back(makeBlock(doc, box(a, b), "CONCRETE/STREET"));
		floor.push_back({0, doc.world.solids.back().id});
	}
	// Sidewalks (top 16) along the main street's far side and the side street; a wall (top 200)
	// along the main street's near side, left of the side street.
	for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{0, -336, -16}, {2560, -240, 16}}, {{688, 240, -16}, {784, 1600, 16}},
	                                                            {{1264, 240, -16}, {1360, 1600, 16}}, {{1360, 240, -16}, {2560, 336, 16}}}) {
		doc.world.solids.push_back(makeBlock(doc, box(a, b), "CONCRETE/SIDEWALK"));
	}
	doc.world.solids.push_back(makeBlock(doc, box({0, 240, -16}, {688, 336, 200}), "BRICK/WALL"));
	const RoadBuildOptions o = edgeKerbOptions();
	const auto plan = planRoadFromSelection(doc, doc, floor, o, SQUARE);
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	checkEdgeKerbs(doc, floor, plan);
	const MapDocument after = applied(doc, plan);
	CHECK(kerbAt(after, 300, -240 + 8, 12));        // flush with the sidewalk
	CHECK(kerbAt(after, 2000, 240 - 8, 12));
	CHECK(kerbAt(after, 784 + 8, 800, 12));         // along the side street
	CHECK(kerbAt(after, 1264 - 8, 800, 12));
	CHECK(kerbAt(after, 300, 240 - 8, 14));         // before the wall: 8 high (street at 8 -> 16)
	CHECK_FALSE(kerbAt(after, 300, 240 - 8, 17));
	// Where the side street leaves the main street there is no kerb across it.
	CHECK_FALSE(kerbAt(after, 1024, 240 - 8, 4));
	CHECK_FALSE(kerbAt(after, 1024, 240 + 8, 4));
}

TEST_CASE("road builder: material suggestions by name - lines for the road, plain for parking, kerbs for kerbs") {
	const std::vector<std::string> names{"concrete/concretefloor001a", "nature/asphalt_blend01", "road/street_lines01", "road/asphalt01",
	                                     "tools/toolsnodraw",          "decals/roadline",        "concrete/curb001",    "concrete/sidewalk01",
	                                     "brick/wall01",               "nature/dirt01",          "props/sign_stop"};
	const auto road = roadMaterialSuggestions(names, RoadMaterialRole::Road);
	REQUIRE_FALSE(road.empty());
	CHECK(road.front() == "road/street_lines01");
	CHECK(std::find(road.begin(), road.end(), "decals/roadline") == road.end());
	CHECK(std::find(road.begin(), road.end(), "nature/asphalt_blend01") == road.end());
	const auto parking = roadMaterialSuggestions(names, RoadMaterialRole::Parking);
	REQUIRE_FALSE(parking.empty());
	CHECK(std::find(parking.begin(), parking.end(), "road/street_lines01") == parking.end());
	CHECK(parking.front() == "road/asphalt01");   // plain asphalt, even in a "road" folder
	CHECK(std::find(parking.begin(), parking.end(), "concrete/concretefloor001a") != parking.end());
	const auto kerb = roadMaterialSuggestions(names, RoadMaterialRole::Kerb);
	REQUIRE_FALSE(kerb.empty());
	CHECK(kerb.front() == "concrete/curb001");
	CHECK(std::find(kerb.begin(), kerb.end(), "brick/wall01") == kerb.end());
	CHECK(roadMaterialSuggestions(names, RoadMaterialRole::Road, 1).size() == 1);
}

TEST_CASE("road from selection: parking lanes on one side only, or none - the road moves over to the other kerb") {
	MapDocument doc;
	std::vector<SolidRef> floor;
	// A straight street 480 wide along x (y -240 .. 240), from x 0 to 2048, sidewalks on both sides.
	doc.world.solids.push_back(makeBlock(doc, box({0, -240, -16}, {2048, 240, 8}), "CONCRETE/STREET"));
	floor.push_back({0, doc.world.solids.back().id});
	doc.world.solids.push_back(makeBlock(doc, box({0, -336, -16}, {2048, -240, 16}), "CONCRETE/SIDEWALK"));
	doc.world.solids.push_back(makeBlock(doc, box({0, 240, -16}, {2048, 336, 16}), "CONCRETE/SIDEWALK"));
	const auto plan = [&](RoadParkingSides sides, RoadCorridor* corridor) {
		RoadBuildOptions o = edgeKerbOptions();
		o.parkingSides = sides;
		return planRoadFromSelection(doc, doc, floor, o, SQUARE, corridor);
	};
	for (const auto sides : {RoadParkingSides::Left, RoadParkingSides::Right}) {
		RoadCorridor corridor;
		const auto p = plan(sides, &corridor);
		INFO(describeRoadCorridor(corridor));
		REQUIRE(p.ok());
		CHECK(corridor.roadWidth == doctest::Approx(480 - 96));
		CHECK(std::abs(corridor.offset) == doctest::Approx(48));
		checkEdgeKerbs(doc, floor, p);
		const MapDocument after = applied(doc, p);
		const auto road = roadIn(after, "ROAD/ASPHALT");
		// Which side is "left" follows the road's direction.
		const auto& points = corridor.paths.front().points;
		const bool east = points.back().x > points.front().x;
		const bool laneAtNorth = (sides == RoadParkingSides::Left) == east;
		const double laneY = laneAtNorth ? 240 - 48 : -240 + 48, roadEdgeY = laneAtNorth ? -240 + 8 : 240 - 8;
		CHECK_FALSE(roadAt(road, 1000, laneY, 0));
		CHECK(roadAt(road, 1000, roadEdgeY, 0));   // the road reaches the kerb without a lane
		CHECK(kerbAt(after, 1000, laneAtNorth ? 240 - 8 : -240 + 8, 12));
	}
	RoadCorridor none;
	const auto p = plan(RoadParkingSides::None, &none);
	REQUIRE(p.ok());
	CHECK(none.parking == 0);
	CHECK(none.roadWidth == doctest::Approx(480));
	CHECK(none.offset == doctest::Approx(0));
	// The older "whole width" setting means none.
	RoadBuildOptions old = edgeKerbOptions();
	old.wholeWidth = true;
	RoadCorridor oldCorridor;
	(void)planRoadFromSelection(doc, doc, floor, old, SQUARE, &oldCorridor);
	CHECK(oldCorridor.roadWidth == doctest::Approx(480));
}

TEST_CASE("road from selection: one parking lane round Julius' L street - no holes, curves inside the street") {
	for (const auto sides : {RoadParkingSides::Left, RoadParkingSides::Right}) {
		Street street = julius();
		RoadBuildOptions o = edgeKerbOptions();
		o.parkingSides = sides;
		RoadCorridor corridor;
		const auto plan = planRoadFromSelection(street.doc, street.doc, street.floor, o, SQUARE, &corridor);
		INFO(describeRoadCorridor(corridor));
		REQUIRE(plan.ok());
		CHECK(corridor.roadWidth == doctest::Approx(416 - 96));
		checkEdgeKerbs(street.doc, street.floor, plan);
		// The road stays on the street: never into a sidewalk.
		const MapDocument after = applied(street.doc, plan);
		for (const auto& ref : street.sidewalks) {
			for (const Solid* r : roadIn(after, "ROAD/ASPHALT")) CHECK(sharedVolume(*r, *after.findSolid(ref)) < 1);
		}
	}
}

// --- Big angled streets (Julius' report of 10.11.2026): speed, holes, raised sides, ramps ------

namespace {

/// A street along a polyline (centre points), mitred at the bends: floor 24 thick (top at the
/// points' height), cut into tiles about `tile` long and two strips across, sidewalks `walk` wide
/// and 8 higher on both sides. `halfWidth`: kerb to the middle.
struct AngledStreet {
	MapDocument doc;
	std::vector<SolidRef> floor;
	std::vector<SolidRef> sidewalks;
	std::vector<Vec3> centre;
};

Vec3 mitred(const std::vector<Vec3>& p, std::size_t i, double d) {
	const Vec3 a = i > 0 ? normalize(flat(p[i] - p[i - 1])) : normalize(flat(p[1] - p[0]));
	const Vec3 b = i + 1 < p.size() ? normalize(flat(p[i + 1] - p[i])) : a;
	const Vec3 la{-a.y, a.x, 0}, lb{-b.y, b.x, 0};
	const double denom = 1 + dot(a, b);
	return p[i] + (denom > 1e-3 ? (la + lb) * (d / denom) : la * d);
}

void addStrip(AngledStreet& s, const std::vector<Vec3>& p, double d0, double d1, double lift, double tile, const std::string& material, std::vector<SolidRef>& into) {
	for (std::size_t i = 0; i + 1 < p.size(); ++i) {
		const double len = length(flat(p[i + 1] - p[i]));
		const int count = std::max(1, int(std::lround(len / tile)));
		for (int k = 0; k < count; ++k) {
			const double t0 = double(k) / count, t1 = double(k + 1) / count;
			std::vector<Vec3> points;
			for (const auto& [t, d] : std::vector<std::pair<double, double>>{{t0, d0}, {t1, d0}, {t1, d1}, {t0, d1}}) {
				const Vec3 v = mitred(p, i, d) * (1 - t) + mitred(p, i + 1, d) * t;
				points.push_back({v.x, v.y, v.z + lift});
				points.push_back({v.x, v.y, v.z - 24});
			}
			auto solid = solidFromPoints(s.doc, points, material);
			REQUIRE(solid);
			s.doc.world.solids.push_back(std::move(*solid));
			into.push_back({0, s.doc.world.solids.back().id});
		}
	}
}

AngledStreet angledStreet(std::vector<Vec3> centre, double halfWidth = 208, double tile = 512, double walk = 96) {
	AngledStreet s;
	s.centre = centre;
	addStrip(s, centre, -halfWidth, 0, 0, tile, "CONCRETE/STREET", s.floor);
	addStrip(s, centre, 0, halfWidth, 0, tile * 0.75, "CONCRETE/STREET", s.floor);
	addStrip(s, centre, halfWidth, halfWidth + walk, 8, 1024, "CONCRETE/SIDEWALK", s.sidewalks);
	addStrip(s, centre, -halfWidth - walk, -halfWidth, 8, 1024, "CONCRETE/SIDEWALK", s.sidewalks);
	return s;
}

/// Julius' larger street with angled parts: straight, 30° left, 45° more, 45° back.
std::vector<Vec3> angledCentre() {
	std::vector<Vec3> p{{-3000, 0, 0}, {0, 0, 0}};
	double angle = 0;
	for (const auto& [turn, len] : std::vector<std::pair<double, double>>{{30, 2048}, {45, 1800}, {-45, 1600}}) {
		angle += turn * std::numbers::pi / 180;
		p.push_back(p.back() + Vec3{std::cos(angle) * len, std::sin(angle) * len, 0});
	}
	return p;
}

/// Julius' settings in the report (10.11.2026).
RoadBuildOptions juliusSettings() {
	RoadBuildOptions o;
	o.material = "CONCRETE/BLENDROAD_BROKEN01";
	o.parkingSides = RoadParkingSides::None;
	o.segments = 10;
	o.thickness = 24;
	o.displacement = true;
	o.power = 3;
	o.crown = 4;
	o.fit = true;
	o.kerbs = false;
	return o;
}

} // namespace

TEST_CASE("road from selection: a big angled street (30°, 45°) - no holes, nothing doubled, every brush sound") {
	for (const double tile : {512.0, 100000.0}) {
		for (const auto sides : {RoadParkingSides::None, RoadParkingSides::Both, RoadParkingSides::Left}) {
			for (const bool disp : {false, true}) {
				CAPTURE(tile);
				CAPTURE(int(sides));
				CAPTURE(disp);
				AngledStreet s = angledStreet(angledCentre(), 208, tile);
				RoadBuildOptions o = juliusSettings();
				o.parkingSides = sides;
				o.displacement = disp;
				RoadCorridor corridor;
				const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
				INFO(describeRoadCorridor(corridor));
				INFO(describeRoadBuild(plan));
				REQUIRE(plan.ok());
				CHECK(plan.skipped == 0);
				CHECK(allSound(plan.resultSolids()));
				const MapDocument after = applied(s.doc, plan);
				checkCovered(s.doc, s.floor, after);
			}
		}
	}
}

TEST_CASE("road from selection: big streets and squares plan quickly (the live preview never waits long)") {
	// Release builds (CI, the shipped editor); an unoptimised debug build is several times slower.
#ifdef NDEBUG
	constexpr double bound = 2500;
#else
	constexpr double bound = 15000;
#endif
	// Julius' report of 10.11.2026: a selection that took 54 s (joining hundreds of cut pieces again
	// pair by pair, from the start after every join). Generous bounds: well under a second each here.
	const auto timed = [](const MapDocument& doc, const std::vector<SolidRef>& floor, const RoadBuildOptions& o) {
		MapDocument scratch;
		scratch.copyIdCounters(doc);
		RoadCorridor corridor;
		const auto t0 = std::chrono::steady_clock::now();
		const auto plan = planRoadFromSelection(doc, scratch, floor, o, SQUARE, &corridor);
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		CHECK(plan.ok());
		return ms;
	};
	for (const double tile : {512.0, 100000.0}) {
		AngledStreet s = angledStreet(angledCentre(), 208, tile);
		const double ms = timed(s.doc, s.floor, juliusSettings());
		MESSAGE("angled street, tiles " << tile << ": " << ms << " ms");
		CHECK(ms < bound);
	}
	{
		// A square of floor blocks with pillars standing on it: many ways through, many cuts.
		MapDocument doc;
		std::vector<SolidRef> floor;
		for (double x = -1500; x < 1500; x += 500) {
			for (double y = -1000; y < 1000; y += 400) {
				doc.world.solids.push_back(makeBlock(doc, box({x, y, -24}, {x + 500, y + 400, 0}), "CONCRETE/STREET"));
				floor.push_back({0, doc.world.solids.back().id});
			}
		}
		for (double x = -1200; x < 1200; x += 400) {
			for (double y = -700; y < 700; y += 350) doc.world.solids.push_back(makeBlock(doc, box({x, y, 0}, {x + 32, y + 32, 128}), "CONCRETE/PILLAR"));
		}
		const double ms = timed(doc, floor, juliusSettings());
		MESSAGE("square with pillars: " << ms << " ms");
		CHECK(ms < bound);
	}
	{
		// One big slab the road winds through: every curve piece cuts it (hundreds of pieces to join).
		MapDocument doc;
		doc.world.solids.push_back(makeBlock(doc, box({-2048, -2048, -64}, {2048, 2048, 0}), "CONCRETE/STREET"));
		RoadBuildOptions o = juliusSettings();
		o.width = 256;
		o.innerRadius = 64;
		o.fit = true;
		MapDocument scratch;
		scratch.copyIdCounters(doc);
		const auto t0 = std::chrono::steady_clock::now();
		const auto plan = planRoadBuild(doc, scratch, {path({{-1800, -1800, 0}, {1800, -1800, 0}, {1800, -600, 0}, {-1800, -600, 0}, {-1800, 600, 0}, {1800, 600, 0}, {1800, 1800, 0}})}, o, SQUARE);
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		MESSAGE("winding road through one slab: " << ms << " ms, " << plan.changed.size() << " changed");
		CHECK(plan.ok());
		CHECK(ms < bound);
	}
}

TEST_CASE("road builder: planning can be stopped (a newer preview is wanted)") {
	AngledStreet s = angledStreet(angledCentre());
	RoadBuildOptions o = juliusSettings();
	int asked = 0;
	o.cancelled = [&asked] { return ++asked > 1; };
	MapDocument scratch;
	const auto plan = planRoadFromSelection(s.doc, scratch, s.floor, o, SQUARE);
	CHECK_FALSE(plan.ok());
	CHECK(asked >= 2);
	CHECK(std::find(plan.notes.begin(), plan.notes.end(), "stopped") != plan.notes.end());
}

TEST_CASE("road from selection: a road narrower than the street - the floor beside it rises to the sidewalk, no holes") {
	for (const auto sides : {RoadParkingSides::None, RoadParkingSides::Both}) {
		CAPTURE(int(sides));
		Street street = julius();
		RoadBuildOptions o;
		o.material = "ROAD/ASPHALT";
		o.parkingSides = sides;
		o.parking = 48;
		o.streetRoadWidth = 160;
		o.kerbs = true;
		o.kerbMaterial = "CONCRETE/KERB";
		RoadCorridor corridor;
		const auto plan = planRoadFromSelection(street.doc, street.doc, street.floor, o, SQUARE, &corridor);
		INFO(describeRoadCorridor(corridor));
		INFO(describeRoadBuild(plan));
		REQUIRE(plan.ok());
		CHECK(corridor.roadWidth == doctest::Approx(160));
		CHECK(corridor.raisedSides);
		CHECK(plan.skipped == 0);
		CHECK(allSound(plan.resultSolids()));
		const MapDocument after = applied(street.doc, plan);
		checkCovered(street.doc, street.floor, after);
		CHECK(noOverlaps(after.world.solids));
		const auto road = roadIn(after, "ROAD/ASPHALT");
		const double flatHalf = 80 + (sides == RoadParkingSides::Both ? 48 : 0);
		// Along the first part (centre y = -928): the road, the flat lanes, then raised floor up to the sidewalk (top 16).
		for (const double x : {-2000.0, -1600.0}) {
			CHECK(roadAt(road, x, -928, 0));
			CHECK_FALSE(roadAt(road, x, -928 + 100, 0));
			const auto solidAt = [&](double y, double z) {
				return std::any_of(after.world.solids.begin(), after.world.solids.end(), [&](const Solid& s) { return solidContains(s, {x, y, z}, -0.05); });
			};
			CHECK_FALSE(solidAt(-928 + flatHalf - 8, 12));   // flat street: open above its top (8)
			CHECK(solidAt(-928 + flatHalf + 20, 12));        // beyond it: raised to 16
			CHECK_FALSE(solidAt(-928 + flatHalf + 20, 18));
			CHECK(solidAt(-928 - flatHalf - 20, 12));
		}
		// The raised floor's face towards the road is the kerb.
		bool kerbFace = false;
		for (const auto& s : after.world.solids) {
			for (const auto& side : s.sides) kerbFace = kerbFace || side.material == "CONCRETE/KERB";
		}
		CHECK(kerbFace);
	}
}

TEST_CASE("road from selection: narrower road through a big angled street - raised sides, no holes") {
	AngledStreet s = angledStreet(angledCentre(), 208, 512);
	RoadBuildOptions o = juliusSettings();
	o.streetRoadWidth = 224;
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	CHECK(corridor.raisedSides);
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(s.doc, plan);
	checkCovered(s.doc, s.floor, after);
	// Beside the first part (along x): raised to the sidewalk's top (8).
	const auto solidAt = [&](double x, double y, double z) {
		return std::any_of(after.world.solids.begin(), after.world.solids.end(), [&](const Solid& q) { return solidContains(q, {x, y, z}, -0.05); });
	};
	CHECK(solidAt(-1500, 180, 4));
	CHECK(solidAt(-1500, -180, 4));
	CHECK_FALSE(solidAt(-1500, 80, 4));
}

namespace {

/// A street that ramps up: flat at 0 (x 0..1024), a ramp to 128 (x 1024..2048), flat at 128
/// (x 2048..3072), 416 wide, cut into blocks; sidewalks 96 wide and 8 higher, sloping with it.
/// `leg`: an L - after the top level, a part along y that slopes down again to 64.
struct RampStreet {
	MapDocument doc;
	std::vector<SolidRef> floor, sidewalks;
};

RampStreet rampStreet(bool leg = false) {
	RampStreet s;
	const auto slab = [&](double x0, double x1, double y0, double y1, double z0, double z1, double lift, const char* material, std::vector<SolidRef>& into,
	                      bool alongY = false) {
		// Top rises from z0 to z1 along x (or along y), the brush 24 thick under it.
		std::vector<Vec3> points;
		for (const double x : {x0, x1}) {
			for (const double y : {y0, y1}) {
				const double f = alongY ? (y - y0) / (y1 - y0) : (x - x0) / (x1 - x0);
				const double z = z0 + (z1 - z0) * f + lift;
				points.push_back({x, y, z});
				points.push_back({x, y, z - 24 - lift});
			}
		}
		auto solid = solidFromPoints(s.doc, points, material);
		REQUIRE(solid);
		s.doc.world.solids.push_back(std::move(*solid));
		into.push_back({0, s.doc.world.solids.back().id});
	};
	for (const auto& [x0, x1, z0, z1] : std::vector<std::array<double, 4>>{{0, 512, 0, 0}, {512, 1024, 0, 0}, {1024, 1536, 0, 64}, {1536, 2048, 64, 128},
	                                                                    {2048, 2560, 128, 128}, {2560, 3072, 128, 128}}) {
		slab(x0, x1, -208, 0, z0, z1, 0, "CONCRETE/STREET", s.floor);
		slab(x0, x1, 0, 208, z0, z1, 0, "CONCRETE/STREET", s.floor);
		slab(x0, x1, 208, 304, z0, z1, 8, "CONCRETE/SIDEWALK", s.sidewalks);
		if (!leg || x1 < 3072) slab(x0, x1, -304, -208, z0, z1, 8, "CONCRETE/SIDEWALK", s.sidewalks);
	}
	if (leg) {
		// From the top level's end (x 2656..3072 wide) along -y, sloping down to 64.
		slab(2656, 3072, -1232, -208, 64, 128, 0, "CONCRETE/STREET", s.floor, true);
		slab(2560, 2656, -1232, -304, 72, 136, 0, "CONCRETE/SIDEWALK", s.sidewalks, true);
	}
	return s;
}

} // namespace

TEST_CASE("road from selection: a street that ramps up between two levels - the road follows the ground") {
	for (const bool disp : {false, true}) {
		CAPTURE(disp);
		RampStreet s = rampStreet();
		RoadBuildOptions o = juliusSettings();
		o.displacement = disp;
		o.parkingSides = RoadParkingSides::Both;
		o.parking = 64;
		RoadCorridor corridor;
		const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
		INFO(describeRoadCorridor(corridor));
		INFO(describeRoadBuild(plan));
		REQUIRE(plan.ok());
		CHECK(corridor.floor.size() == s.floor.size());   // the ramp joins both levels: one street
		CHECK(corridor.ignored.empty());
		CHECK(plan.skipped == 0);
		CHECK(allSound(plan.resultSolids()));
		const MapDocument after = applied(s.doc, plan);
		checkCovered(s.doc, s.floor, after);
		CHECK(noOverlaps(after.world.solids));
		// The road's top on the ground: along the middle at several places (1 unit below / above).
		const auto road = roadIn(after, o.material);
		for (const auto& [x, z] : std::vector<std::pair<double, double>>{{300, 0}, {900, 0}, {1280, 32}, {1536, 64}, {1900, 109.5}, {2700, 128}}) {
			CAPTURE(x);
			CHECK(roadAt(road, x, 0, z - 1));
			if (!disp) CHECK_FALSE(roadAt(road, x, 0, z + 1));
		}
	}
}

TEST_CASE("road from selection: an L street with a sloped leg - curve and slope, no holes") {
	RampStreet s = rampStreet(true);
	RoadBuildOptions o = juliusSettings();
	o.displacement = false;
	o.parkingSides = RoadParkingSides::None;
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	CHECK(corridor.ignored.empty());
	CHECK(plan.curves >= 1);
	CHECK(plan.skipped == 0);
	CHECK(allSound(plan.resultSolids()));
	const MapDocument after = applied(s.doc, plan);
	checkCovered(s.doc, s.floor, after);
	CHECK(noOverlaps(after.world.solids));
	const auto road = roadIn(after, o.material);
	// Down the leg: the ground falls from 128 (y -208) to 64 (y -1232).
	const double zLeg = 64 + 64 * (1232.0 - 1100) / 1024;   // 72.25 at y = -1100
	CHECK(roadAt(road, 2864, -1100, zLeg - 1));
	CHECK_FALSE(roadAt(road, 2864, -1100, zLeg + 1));
}

TEST_CASE("road from selection: kerbs at the edge of a ramp street follow the slope, flush with the sidewalk") {
	RampStreet s = rampStreet();
	RoadBuildOptions o = edgeKerbOptions();
	o.parking = 64;
	RoadCorridor corridor;
	const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
	INFO(describeRoadCorridor(corridor));
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	checkEdgeKerbs(s.doc, s.floor, plan);
	const MapDocument after = applied(s.doc, plan);
	// On the flat start, half way up the ramp and on the top level: a kerb against the sidewalk,
	// its top flush with the sidewalk's (8 above the ground).
	for (const auto& [x, z] : std::vector<std::pair<double, double>>{{300, 0}, {1280, 32}, {1800, 97}, {2800, 128}}) {
		CAPTURE(x);
		CHECK(kerbAt(after, x, 208 - 8, z + 4));
		CHECK_FALSE(kerbAt(after, x, 208 - 8, z + 9));
	}
}

TEST_CASE("road from selection: kerbs at the edge round the bends of a big angled street - no holes") {
	for (const double parking : {64.0, 96.0}) {
		CAPTURE(parking);
		AngledStreet s = angledStreet(angledCentre(), 208, 512);
		RoadBuildOptions o = edgeKerbOptions();
		o.parking = parking;
		RoadCorridor corridor;
		const auto plan = planRoadFromSelection(s.doc, s.doc, s.floor, o, SQUARE, &corridor);
		INFO(describeRoadCorridor(corridor));
		INFO(describeRoadBuild(plan));
		REQUIRE(plan.ok());
		checkEdgeKerbs(s.doc, s.floor, plan);
	}
}

TEST_CASE("road from selection: a cut a hair from a corner never drops a big piece of the floor") {
	// The picture test of 10.11.2026: the floor with the road material on top, kerbs at the edge, a
	// 256 x 512 texture: a parking lane piece of 16 549 square units came out with an edge of a
	// hundredth of a unit (two cut planes almost alike) and was left out as a "sliver" - a hole.
	AngledStreet s = angledStreet(angledCentre(), 208, 512);
	for (auto& solid : s.doc.world.solids) {
		for (auto& side : solid.sides) {
			if (side.material == "CONCRETE/STREET") side.material = "ROAD/ASPHALT";
		}
	}
	RoadBuildOptions o = edgeKerbOptions();
	o.thickness = 24;
	o.parking = 64;
	o.fillMaterial.clear();
	const TextureSizeFn tall = [](const std::string&) { return std::pair<int, int>{256, 512}; };
	RoadCorridor corridor;
	MapDocument ids;
	ids.copyIdCounters(s.doc);
	const auto plan = planRoadFromSelection(s.doc, ids, s.floor, o, tall, &corridor);
	INFO(describeRoadBuild(plan));
	REQUIRE(plan.ok());
	CHECK(std::none_of(plan.notes.begin(), plan.notes.end(), [](const std::string& n) { return n.find("sliver") != std::string::npos; }));
	CHECK(allSound(plan.resultSolids()));
	checkCovered(s.doc, s.floor, applied(s.doc, plan));
}
