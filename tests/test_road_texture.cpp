// test_road_texture.cpp - Road tool: chains of floor faces, the texture along the road and
// fitted across it, continuous over straight pieces, curves (round-edge strips), S-bends and ramps;
// one scale per road (pieces of other widths), square corners, junctions, kerbs and edge pieces.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/road_texture.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

using namespace sme;

namespace {

/// A brush from its corner points, in the world.
SolidRef addSolid(MapDocument& doc, const std::vector<Vec3>& points, const std::string& material = "DEV/DEV_MEASUREGENERIC01") {
	auto solid = solidFromPoints(doc, points, material);
	REQUIRE(solid.has_value());
	doc.world.solids.push_back(std::move(*solid));
	return {0, doc.world.solids.back().id};
}

/// A box brush from (x0, y0, 0) to (x1, y1, top).
SolidRef addBox(MapDocument& doc, double x0, double y0, double x1, double y1, double top = 16) {
	return addSolid(doc, {{x0, y0, 0}, {x1, y0, 0}, {x0, y1, 0}, {x1, y1, 0}, {x0, y0, top}, {x1, y0, top}, {x0, y1, top}, {x1, y1, top}});
}

/// Pieces of a curve around `centre` between radius r1 and r2, from angle a0 to a1 (degrees), as
/// round-edge or arch strips are: one convex brush per segment.
std::vector<SolidRef> addArc(MapDocument& doc, Vec3 centre, double r1, double r2, double a0, double a1, int segments) {
	std::vector<SolidRef> refs;
	const auto at = [&](double r, double degrees, double z) {
		const double a = degrees * std::numbers::pi / 180.0;
		return Vec3{centre.x + r * std::cos(a), centre.y + r * std::sin(a), z};
	};
	for (int k = 0; k < segments; ++k) {
		const double s = a0 + (a1 - a0) * k / segments;
		const double e = a0 + (a1 - a0) * (k + 1) / segments;
		refs.push_back(addSolid(doc, {at(r1, s, 0), at(r2, s, 0), at(r1, e, 0), at(r2, e, 0), at(r1, s, 16), at(r2, s, 16), at(r1, e, 16), at(r2, e, 16)}));
	}
	return refs;
}

/// The planned side for a face of brush `ref` (its top face).
const RoadPlan::FaceChange* planned(const RoadPlan& plan, const SolidRef& ref) {
	for (const auto& change : plan.faces) {
		if (change.face.solid == ref) return &change;
	}
	return nullptr;
}

/// Two texel values are the same up to whole texture repeats.
bool sameTexel(double a, double b, double size, double tolerance = 0.5) {
	double d = std::fmod(std::abs(a - b), size);
	return d < tolerance || size - d < tolerance;
}

/// All faces of the selected brushes that face up (the road tool's input in object mode).
std::vector<FaceRef> tops(const MapDocument& doc, const std::vector<SolidRef>& solids) {
	Selection selection;
	selection.solids = solids;
	return roadFaces(doc, selection);
}

TextureSizeFn size(int w, int h) {
	return [w, h](const std::string&) { return std::pair<int, int>{w, h}; };
}

} // namespace

TEST_CASE("road: a straight road of three pieces runs on without jumps and spans the width exactly") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);
	const auto b = addBox(doc, 512, 0, 1024, 256);
	const auto c = addBox(doc, 1024, 0, 1536, 256);
	RoadOptions options;
	options.material = "ROAD/DIRT01";
	const auto plan = planRoad(doc, tops(doc, {c, a, b}), options, size(512, 512));
	REQUIRE(plan.faces.size() == 3);
	CHECK(plan.chains == 1);
	CHECK(plan.ignored == 0);
	CHECK(plan.alongV);
	for (const auto& ref : {a, b, c}) {
		const auto* change = planned(plan, ref);
		REQUIRE(change);
		CHECK(change->side.material == "ROAD/DIRT01");
		CHECK(std::abs(std::abs(change->side.v.axis.x) - 1) < 1e-9);   // along the road
		// Across: 0 at one kerb, the whole texture (512) at the other, the middle at 256.
		const double edge0 = textureCoordinate(change->side, {700, 0, 16}).u;
		const double edge1 = textureCoordinate(change->side, {700, 256, 16}).u;
		CHECK(std::abs(std::abs(edge1 - edge0) - 512) < 1e-6);
		CHECK(sameTexel(textureCoordinate(change->side, {700, 128, 16}).u, 256, 512));
	}
	// The same texel on both sides of every common edge.
	for (const double x : {512.0, 1024.0}) {
		const auto* left = planned(plan, x == 512 ? a : b);
		const auto* right = planned(plan, x == 512 ? b : c);
		for (const double y : {0.0, 77.0, 256.0}) {
			const Vec3 p{x, y, 16};
			CHECK(sameTexel(textureCoordinate(left->side, p).v, textureCoordinate(right->side, p).v, 512));
			CHECK(sameTexel(textureCoordinate(left->side, p).u, textureCoordinate(right->side, p).u, 512));
		}
	}
	// Square pixels along the road: 512 texels over the 256 wide road -> 2 texels per unit along.
	const auto* first = planned(plan, a);
	CHECK(std::abs(1.0 / first->side.v.scale - 2.0) < 1e-9);
}

TEST_CASE("road: an L with a rounded corner - every strip runs along the curve, the lane stays in the middle") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);                                 // along +x
	const auto arc = addArc(doc, {512, 512, 0}, 256, 512, -90, 0, 6);           // the turn
	const auto b = addBox(doc, 768, 512, 1024, 1024);                           // along +y
	std::vector<SolidRef> all{a, b};
	all.insert(all.end(), arc.begin(), arc.end());
	RoadOptions options;
	options.material = "ROAD/DIRT01";
	const auto plan = planRoad(doc, tops(doc, all), options, size(512, 1024));
	REQUIRE(plan.faces.size() == 8);
	CHECK(plan.chains == 1);
	CHECK(plan.alongV);   // tall texture: V along the road
	for (int k = 0; k < 6; ++k) {
		const auto* change = planned(plan, arc[static_cast<std::size_t>(k)]);
		REQUIRE(change);
		const double mid = (-90.0 + 90.0 * (k + 0.5) / 6) * std::numbers::pi / 180.0;
		const Vec3 tangent{-std::sin(mid), std::cos(mid), 0};
		CHECK(std::abs(std::abs(dot(change->side.v.axis, tangent)) - 1) < 1e-6);
		// The centre line (radius 384) is the middle of the texture across: exactly where the strips
		// meet, within the flat face's chord error (r (1 - cos(7.5°)), 2 % of the width) between.
		const Vec3 centre{512 + 384 * std::cos(mid), 512 + 384 * std::sin(mid), 16};
		CHECK(sameTexel(textureCoordinate(change->side, centre).u, 256, 512, 10.0));
		const double start = (-90.0 + 90.0 * k / 6) * std::numbers::pi / 180.0;
		CHECK(sameTexel(textureCoordinate(change->side, {512 + 384 * std::cos(start), 512 + 384 * std::sin(start), 16}).u, 256, 512, 0.5));
	}
	// Along the chain, the texture runs on across every common edge (at the centre line).
	std::vector<SolidRef> order{a};
	order.insert(order.end(), arc.begin(), arc.end());
	order.push_back(b);
	for (std::size_t k = 0; k + 1 < order.size(); ++k) {
		const double angle = (-90.0 + 90.0 * static_cast<double>(k) / 6) * std::numbers::pi / 180.0;
		const Vec3 p{512 + 384 * std::cos(angle), 512 + 384 * std::sin(angle), 16};
		const auto* left = planned(plan, order[k]);
		const auto* right = planned(plan, order[k + 1]);
		CHECK(sameTexel(textureCoordinate(left->side, p).v, textureCoordinate(right->side, p).v, 1024, 1.0));
		CHECK(sameTexel(textureCoordinate(left->side, p).u, textureCoordinate(right->side, p).u, 512, 1.0));
	}
}

TEST_CASE("road: an S-bend keeps one chain, the lane in the middle and its side through the change of turn") {
	MapDocument doc;
	const auto first = addArc(doc, {0, 0, 0}, 256, 512, 270, 360, 5);     // turns left
	const auto second = addArc(doc, {768, 0, 0}, 256, 512, 180, 90, 5);   // then right
	std::vector<SolidRef> all = first;
	all.insert(all.end(), second.begin(), second.end());
	const auto plan = planRoad(doc, tops(doc, all), RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	REQUIRE(plan.faces.size() == 10);
	CHECK(plan.chains == 1);
	for (int k = 0; k < 10; ++k) {
		const bool left = k < 5;
		const Vec3 centre = left ? Vec3{0, 0, 0} : Vec3{768, 0, 0};
		const double degrees = left ? 270 + 90.0 * (k + 0.5) / 5 : 180 - 90.0 * (k - 5 + 0.5) / 5;
		const double a = degrees * std::numbers::pi / 180.0;
		const auto* change = planned(plan, all[static_cast<std::size_t>(k)]);
		REQUIRE(change);
		CHECK(std::abs(std::abs(dot(change->side.v.axis, Vec3{-std::sin(a), std::cos(a), 0})) - 1) < 1e-6);
		CHECK(sameTexel(textureCoordinate(change->side, {centre.x + 384 * std::cos(a), centre.y + 384 * std::sin(a), 16}).u, 256, 512, 12.0));
	}
	// Where the turn changes, both kerbs keep their texel (the lane does not jump to the other side).
	const auto* last = planned(plan, first.back());
	const auto* next = planned(plan, second.front());
	for (const Vec3 p : {Vec3{256, 0, 16}, Vec3{512, 0, 16}}) {
		CHECK(sameTexel(textureCoordinate(last->side, p).u, textureCoordinate(next->side, p).u, 512, 1.0));
	}
}

TEST_CASE("road: a ramp is measured along its slope, not stretched") {
	MapDocument doc;
	const auto flat = addBox(doc, 0, 0, 512, 256);
	const auto ramp = addSolid(doc, {{512, 0, 0}, {512, 256, 0}, {1024, 0, 0}, {1024, 256, 0}, {512, 0, 16}, {512, 256, 16}, {1024, 0, 272}, {1024, 256, 272}});
	const auto plan = planRoad(doc, tops(doc, {flat, ramp}), RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	REQUIRE(plan.faces.size() == 2);
	CHECK(plan.chains == 1);
	const auto* change = planned(plan, ramp);
	REQUIRE(change);
	const Vec3 bottom{512, 128, 16}, top{1024, 128, 272};
	const double texels = std::abs(dot(top - bottom, change->side.v.axis) / change->side.v.scale);
	CHECK(std::abs(texels - length(top - bottom) * 2.0) < 1e-6);   // 2 texels per unit along the slope
	// The axes lie in the slope (no stretching across either).
	const Vec3 normal = normalize(cross(Vec3{0, 256, 0}, top - bottom));
	CHECK(std::abs(dot(change->side.u.axis, normal)) < 1e-9);
	CHECK(std::abs(dot(change->side.v.axis, normal)) < 1e-9);
	// Continuous where the ramp starts.
	const auto* before = planned(plan, flat);
	CHECK(sameTexel(textureCoordinate(before->side, bottom).v, textureCoordinate(change->side, bottom).v, 512));
}

TEST_CASE("road: the texture's shape decides which axis runs along; U and V can be chosen") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);
	const auto faces = tops(doc, {a});
	auto plan = planRoad(doc, faces, RoadOptions{"ROAD/WIDE"}, size(1024, 512));
	REQUIRE(plan.faces.size() == 1);
	CHECK_FALSE(plan.alongV);   // wide texture: U along
	CHECK(std::abs(std::abs(plan.faces[0].side.u.axis.x) - 1) < 1e-9);
	CHECK(std::abs(std::abs(plan.faces[0].side.v.axis.y) - 1) < 1e-9);
	// Across: the texture's height (512) over the width.
	const double v0 = textureCoordinate(plan.faces[0].side, {100, 0, 16}).v;
	const double v1 = textureCoordinate(plan.faces[0].side, {100, 256, 16}).v;
	CHECK(std::abs(std::abs(v1 - v0) - 512) < 1e-6);
	RoadOptions forced{"ROAD/WIDE"};
	forced.along = RoadAlong::V;
	plan = planRoad(doc, faces, forced, size(1024, 512));
	CHECK(plan.alongV);
	CHECK(std::abs(std::abs(plan.faces[0].side.v.axis.x) - 1) < 1e-9);
	// Two repeats across: the width holds the texture twice.
	RoadOptions twice{"ROAD/DIRT01"};
	twice.repeatsAcross = 2;
	plan = planRoad(doc, faces, twice, size(512, 512));
	const double u0 = textureCoordinate(plan.faces[0].side, {100, 0, 16}).u;
	const double u1 = textureCoordinate(plan.faces[0].side, {100, 256, 16}).u;
	CHECK(std::abs(std::abs(u1 - u0) - 1024) < 1e-6);
}

TEST_CASE("road: the result does not depend on the order of the faces; flip turns it around") {
	MapDocument doc;
	std::vector<SolidRef> all{addBox(doc, 0, 0, 512, 256)};
	const auto arc = addArc(doc, {512, 512, 0}, 256, 512, -90, 0, 4);
	all.insert(all.end(), arc.begin(), arc.end());
	all.push_back(addBox(doc, 768, 512, 1024, 1024));
	auto faces = tops(doc, all);
	const auto reference = planRoad(doc, faces, RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	std::mt19937 random(7);
	for (int round = 0; round < 5; ++round) {
		std::shuffle(faces.begin(), faces.end(), random);
		const auto plan = planRoad(doc, faces, RoadOptions{"ROAD/DIRT01"}, size(512, 512));
		REQUIRE(plan.faces.size() == reference.faces.size());
		for (const auto& change : reference.faces) {
			const auto* same = planned(plan, change.face.solid);
			REQUIRE(same);
			CHECK((same->side.u == change.side.u));
			CHECK((same->side.v == change.side.v));
		}
	}
	RoadOptions flipped{"ROAD/DIRT01"};
	flipped.flip = true;
	const auto plan = planRoad(doc, faces, flipped, size(512, 512));
	const auto* normal = planned(reference, all.front());
	const auto* turned = planned(plan, all.front());
	CHECK(dot(normal->side.v.axis, turned->side.v.axis) < -0.999);
}

TEST_CASE("road: a gap starts a new chain; walls and bottoms are left out; kerbs get the side material") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);
	const auto b = addBox(doc, 576, 0, 1088, 256);   // 64 units away
	auto faces = tops(doc, {a, b});
	// In face mode a wall and a bottom are selected too.
	const Solid* solid = doc.findSolid(a);
	const auto geometry = buildBrushGeometry(*solid);
	for (const auto& face : geometry.faces) {
		if (normalize(face.normal).z < 0.5) faces.push_back({a, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
	}
	RoadOptions options{"ROAD/DIRT01"};
	options.sideMaterial = "CONCRETE/KERB01";
	const auto plan = planRoad(doc, faces, options, size(512, 512));
	CHECK(plan.faces.size() == 2);
	CHECK(plan.chains == 2);
	CHECK(plan.ignored == 5);          // four walls and the bottom of a
	CHECK(plan.sideFaces.size() == 8);   // four standing sides of each brush

	// One undo step; undo gives the map back exactly.
	const std::string before = doc.serialize();
	History history;
	history.push(edit::applyRoad(doc, plan));
	const Solid* changed = doc.findSolid(a);
	int kerbs = 0, roads = 0;
	for (const auto& side : changed->sides) {
		kerbs += side.material == "CONCRETE/KERB01";
		roads += side.material == "ROAD/DIRT01";
	}
	CHECK(kerbs == 4);
	CHECK(roads == 1);
	REQUIRE(history.undo(doc));
	CHECK(doc.serialize() == before);

	// The preview shows the same brushes changed, without touching the map.
	std::vector<SolidRef> replaced;
	const auto preview = roadPreview(doc, plan, &replaced);
	CHECK(preview.size() == 2);
	CHECK(replaced.size() == 2);
	CHECK(doc.serialize() == before);
}

TEST_CASE("road: without a material the one most faces have is used; nothing to do without road faces") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);
	const auto b = addBox(doc, 512, 0, 1024, 256);
	for (auto& side : doc.findSolid(a)->sides) side.material = "ROAD/OLD";
	for (auto& side : doc.findSolid(b)->sides) side.material = "ROAD/OLD";
	const auto plan = planRoad(doc, tops(doc, {a, b}), RoadOptions{}, size(512, 512));
	CHECK(plan.material == "ROAD/OLD");
	CHECK(plan.faces.size() == 2);
	CHECK(planRoad(doc, {}, RoadOptions{}, size(512, 512)).empty());
}

namespace {

/// The texture coordinate of `p` on the planned face of brush `ref`.
TexCoord at(const RoadPlan& plan, const SolidRef& ref, const Vec3& p) {
	const auto* change = planned(plan, ref);
	REQUIRE(change);
	return textureCoordinate(change->side, p);
}

/// The planned face of `ref` runs along `axis` (or against it).
bool runsAlong(const RoadPlan& plan, const SolidRef& ref, const Vec3& axis) {
	const auto* change = planned(plan, ref);
	return change && std::abs(std::abs(dot(change->side.v.axis, axis)) - 1) < 1e-9;
}

} // namespace

TEST_CASE("road: pieces of different widths share one scale; the lane runs straight on (Julius' 55)") {
	// Julius (31.10.2026): two pieces, the near one wider - each got its own scale, the lane
	// lines jumped at the seam. Now: one scale, the texture's middle on the road's centre line.
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);        // 256 wide
	const auto b = addBox(doc, 512, -64, 1024, 256);   // 320 wide, wider to one side
	const auto plan = planRoad(doc, tops(doc, {b, a}), RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	REQUIRE(plan.faces.size() == 2);
	CHECK(plan.chains == 1);
	REQUIRE(plan.widths.size() == 1);
	CHECK(std::abs(plan.widths[0] - 256) < 1e-9);   // the narrow piece's width (lower median)
	const auto* first = planned(plan, a);
	const auto* second = planned(plan, b);
	CHECK(std::abs(first->side.u.scale - second->side.u.scale) < 1e-12);   // one scale
	CHECK(std::abs(first->side.v.scale - second->side.v.scale) < 1e-12);
	// The texture's middle stays on the narrow piece's middle (y = 128) in both pieces; the
	// wider piece just shows 64 units more of the texture's edge.
	CHECK(sameTexel(at(plan, a, {300, 128, 16}).u, 256, 512));
	CHECK(sameTexel(at(plan, b, {800, 128, 16}).u, 256, 512));
	// Across the seam: the same texel at the centre line and at every lane position.
	for (const double y : {0.0, 32.0, 64.0, 128.0, 192.0, 256.0}) {
		const Vec3 p{512, y, 16};
		CHECK(sameTexel(at(plan, a, p).u, at(plan, b, p).u, 512));
		CHECK(sameTexel(at(plan, a, p).v, at(plan, b, p).v, 512));
	}
	// A width given in units: the texture spans it.
	RoadOptions wide{"ROAD/DIRT01"};
	wide.roadWidth = 320;
	const auto plan2 = planRoad(doc, tops(doc, {a, b}), wide, size(512, 512));
	REQUIRE(plan2.widths.size() == 1);
	CHECK(std::abs(plan2.widths[0] - 320) < 1e-9);
	CHECK(std::abs(1.0 / planned(plan2, a)->side.u.scale - 512.0 / 320.0) < 1e-9);
}

TEST_CASE("road: an L of straight pieces with a square corner - never diagonal; the other road starts at the corner (Julius' 54)") {
	MapDocument doc;
	const auto a = addBox(doc, 0, 0, 512, 256);           // along x, 512 long
	const auto corner = addBox(doc, 512, 0, 768, 256);    // the square corner
	const auto b = addBox(doc, 512, 256, 768, 1024);      // along y, 768 long (the longer road)
	const auto plan = planRoad(doc, tops(doc, {a, corner, b}), RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	REQUIRE(plan.faces.size() == 3);
	CHECK(plan.chains == 2);
	// Nothing runs diagonally: the corner continues the longer road (along y).
	CHECK(runsAlong(plan, corner, {0, 1, 0}));
	CHECK(runsAlong(plan, b, {0, 1, 0}));
	CHECK(runsAlong(plan, a, {1, 0, 0}));
	for (const auto& change : plan.faces) {
		const Vec3 v = change.side.v.axis;
		CHECK((std::abs(std::abs(v.x) - 1) < 1e-9 || std::abs(std::abs(v.y) - 1) < 1e-9));
	}
	// The corner and the long road are one road: seamless where they meet.
	for (const double x : {512.0, 600.0, 640.0, 768.0}) {
		const Vec3 p{x, 256, 16};
		CHECK(sameTexel(at(plan, corner, p).u, at(plan, b, p).u, 512));
		CHECK(sameTexel(at(plan, corner, p).v, at(plan, b, p).v, 512));
	}
	// The short road starts at the corner's edge (0 there), its lane on its own middle (y = 128).
	CHECK(sameTexel(at(plan, a, {512, 128, 16}).v, 0, 512));
	CHECK(sameTexel(at(plan, a, {256, 128, 16}).u, 256, 512));
	CHECK(planned(plan, a)->side.v.axis.x < 0);   // runs away from the corner
}

TEST_CASE("road: a T junction - the main road runs straight through, the branch starts at its edge; kerbs and edge pieces (Julius' 53)") {
	MapDocument doc;
	const auto w = addBox(doc, 0, 0, 512, 256);
	const auto j = addBox(doc, 512, 0, 768, 256);          // the junction
	const auto e = addBox(doc, 768, 0, 1280, 256);
	const auto branch = addBox(doc, 512, 256, 768, 768);   // the branch, north
	const auto kerb = addBox(doc, 0, 256, 512, 512, 24);   // a sidewalk beside the road, 8 higher
	const auto strip = addBox(doc, 768, 256, 1280, 264);   // a thin strip at road height along e
	std::vector<SolidRef> all{w, j, e, branch, kerb, strip};
	RoadOptions options{"ROAD/DIRT01"};
	options.sideMaterial = "CONCRETE/KERB01";
	const auto plan = planRoad(doc, tops(doc, all), options, size(512, 512));
	CHECK(plan.kerbs == 1);
	CHECK(plan.edgePieces == 1);
	CHECK(plan.chains == 2);
	CHECK(planned(plan, kerb) == nullptr);   // the sidewalk keeps its top
	// The kerb's top goes onto the kerb material with the sides.
	const Solid* kerbSolid = doc.findSolid(kerb);
	int kerbTop = 0;
	for (const auto& face : plan.sideFaces) {
		if (face.solid == kerb) {
			for (const auto& side : kerbSolid->sides) kerbTop += side.id == face.sideId && side.plane().normal.z > 0.5;
		}
	}
	CHECK(kerbTop == 1);
	// Main road: west - junction - east, all along x, seamless.
	for (const auto& ref : {w, j, e}) CHECK(runsAlong(plan, ref, {1, 0, 0}));
	CHECK(planned(plan, w)->chain == planned(plan, j)->chain);
	CHECK(planned(plan, j)->chain == planned(plan, e)->chain);
	for (const double x : {512.0, 768.0}) {
		for (const double y : {0.0, 64.0, 128.0, 256.0}) {
			const Vec3 p{x, y, 16};
			const SolidRef left = x == 512 ? w : j;
			const SolidRef right = x == 512 ? j : e;
			CHECK(sameTexel(at(plan, left, p).u, at(plan, right, p).u, 512));
			CHECK(sameTexel(at(plan, left, p).v, at(plan, right, p).v, 512));
		}
	}
	// The branch: its own road, along y, starting (0) at the junction's edge, lane in its middle.
	CHECK(runsAlong(plan, branch, {0, 1, 0}));
	CHECK(planned(plan, branch)->chain != planned(plan, j)->chain);
	CHECK(sameTexel(at(plan, branch, {640, 256, 16}).v, 0, 512));
	CHECK(sameTexel(at(plan, branch, {640, 500, 16}).u, 256, 512));
	CHECK(planned(plan, branch)->side.v.axis.y > 0);   // away from the junction
	// The thin strip takes the east piece's mapping: seamless along their common edge.
	for (const double x : {800.0, 1000.0}) {
		const Vec3 p{x, 256, 16};
		CHECK(sameTexel(at(plan, strip, p).u, at(plan, e, p).u, 512));
		CHECK(sameTexel(at(plan, strip, p).v, at(plan, e, p).v, 512));
	}

	// The same plan for any order of the faces.
	auto faces = tops(doc, all);
	std::mt19937 random(11);
	for (int round = 0; round < 5; ++round) {
		std::shuffle(faces.begin(), faces.end(), random);
		const auto again = planRoad(doc, faces, options, size(512, 512));
		REQUIRE(again.faces.size() == plan.faces.size());
		for (const auto& change : plan.faces) {
			const auto* same = planned(again, change.face.solid);
			REQUIRE(same);
			CHECK((same->side.u == change.side.u));
			CHECK((same->side.v == change.side.v));
		}
	}

	// One undo step gives the map back exactly.
	const std::string before = doc.serialize();
	History history;
	history.push(edit::applyRoad(doc, plan));
	REQUIRE(history.undo(doc));
	CHECK(doc.serialize() == before);
}

TEST_CASE("road: a crossing - the wider road runs through, both halves of the other start at the crossing") {
	MapDocument doc;
	const auto w = addBox(doc, 0, 0, 512, 320);             // the wide road (320) along x
	const auto x = addBox(doc, 512, 0, 768, 320);           // the crossing (256 x 320)
	const auto e = addBox(doc, 768, 0, 1280, 320);
	const auto north = addBox(doc, 512, 320, 768, 832);     // the narrow road (256) along y
	const auto south = addBox(doc, 512, -512, 768, 0);
	const auto plan = planRoad(doc, tops(doc, {w, x, e, north, south}), RoadOptions{"ROAD/DIRT01"}, size(512, 512));
	REQUIRE(plan.faces.size() == 5);
	CHECK(plan.chains == 3);
	for (const auto& ref : {w, x, e}) CHECK(runsAlong(plan, ref, {1, 0, 0}));
	CHECK(planned(plan, w)->chain == planned(plan, e)->chain);
	CHECK(runsAlong(plan, north, {0, 1, 0}));
	CHECK(runsAlong(plan, south, {0, 1, 0}));
	CHECK(planned(plan, north)->side.v.axis.y > 0);   // both run away from the crossing
	CHECK(planned(plan, south)->side.v.axis.y < 0);
	CHECK(sameTexel(at(plan, north, {640, 320, 16}).v, 0, 512));
	CHECK(sameTexel(at(plan, south, {640, 0, 16}).v, 0, 512));
}
