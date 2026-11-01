// test_texture_flow.cpp - Texture flow: wrap (pillar, stairs, arch), around a curve (bevel strips,
// cylinder), along a path (trims round a corner, a river), match neighbour, Auto, apply and undo.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/texture_flow.h>

#include <cmath>
#include <functional>
#include <numbers>

using namespace sme;

namespace {

SolidRef addSolid(MapDocument& doc, const std::vector<Vec3>& points, const std::string& material = "BRICK/BRICKWALL001A") {
	auto solid = solidFromPoints(doc, points, material);
	REQUIRE(solid.has_value());
	doc.world.solids.push_back(std::move(*solid));
	return {0, doc.world.solids.back().id};
}

SolidRef addBox(MapDocument& doc, Vec3 lo, Vec3 hi, const std::string& material = "BRICK/BRICKWALL001A") {
	return addSolid(doc, {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
	                      {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}}, material);
}

/// The faces of `ref` whose normal passes `test`.
std::vector<FaceRef> facesWhere(const MapDocument& doc, const SolidRef& ref, const std::function<bool(const Vec3&, const Vec3&)>& test) {
	std::vector<FaceRef> faces;
	const Solid* solid = doc.findSolid(ref);
	REQUIRE(solid);
	const auto geometry = buildBrushGeometry(*solid);
	REQUIRE(geometry.valid);
	for (const auto& face : geometry.faces) {
		Vec3 c;
		for (const auto& v : face.vertices) c += v;
		c = c / static_cast<double>(face.vertices.size());
		if (test(normalize(face.normal), c)) faces.push_back({ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
	}
	return faces;
}

FaceRef faceWhere(const MapDocument& doc, const SolidRef& ref, const std::function<bool(const Vec3&, const Vec3&)>& test) {
	const auto faces = facesWhere(doc, ref, test);
	REQUIRE(faces.size() == 1);
	return faces.front();
}

const Side* planned(const FlowPlan& plan, const FaceRef& face) {
	for (const auto& change : plan.plan.faces) {
		if (change.face == face) return &change.side;
	}
	return nullptr;
}

TextureSizeFn size(int w, int h) {
	return [w, h](const std::string&) { return std::pair<int, int>{w, h}; };
}

/// The texels of `p` on the two planned faces are the same (exactly, not up to repeats).
void checkMeets(const FlowPlan& plan, const FaceRef& a, const FaceRef& b, const Vec3& p) {
	const Side* sa = planned(plan, a);
	const Side* sb = planned(plan, b);
	REQUIRE(sa);
	REQUIRE(sb);
	const TexCoord ta = textureCoordinate(*sa, p), tb = textureCoordinate(*sb, p);
	CHECK(std::abs(ta.u - tb.u) < 0.5);
	CHECK(std::abs(ta.v - tb.v) < 0.5);
}

/// The axes lie in the face (no stretching).
void checkInPlane(const FlowPlan& plan, const MapDocument& doc, const FaceRef& face) {
	const Side* side = planned(plan, face);
	REQUIRE(side);
	const Solid* solid = doc.findSolid(face.solid);
	for (const auto& s : solid->sides) {
		if (s.id != face.sideId) continue;
		const Vec3 n = s.plane().normal;
		CHECK(std::abs(dot(normalize(side->u.axis), n)) < 1e-6);
		CHECK(std::abs(dot(normalize(side->v.axis), n)) < 1e-6);
	}
}

/// A ring segment brush of a curve about the axis through `centre` along `axis` (unit, z or y),
/// radii r1..r2, angles a0..a1 in the plane spanned by `e1`, `e2`, `depth` deep along the axis.
std::vector<SolidRef> addRing(MapDocument& doc, Vec3 centre, Vec3 e1, Vec3 e2, Vec3 axis, double r1, double r2, double a0, double a1, int segments,
                              double depth) {
	std::vector<SolidRef> refs;
	const auto at = [&](double r, double degrees, double d) {
		const double a = degrees * std::numbers::pi / 180.0;
		return centre + e1 * (r * std::cos(a)) + e2 * (r * std::sin(a)) + axis * d;
	};
	for (int k = 0; k < segments; ++k) {
		const double s = a0 + (a1 - a0) * k / segments;
		const double e = a0 + (a1 - a0) * (k + 1) / segments;
		refs.push_back(addSolid(doc, {at(r1, s, 0), at(r2, s, 0), at(r1, e, 0), at(r2, e, 0), at(r1, s, depth), at(r2, s, depth), at(r1, e, depth), at(r2, e, depth)}));
	}
	return refs;
}

} // namespace

TEST_CASE("texture flow: folding over an edge turns the axes about it and keeps the texel on the edge") {
	Side wall;
	wall.u = {{1, 0, 0}, 13, 0.25};
	wall.v = {{0, 0, -1}, 7, 0.25};
	// Front wall (normal -y) to the right wall (normal +x), edge along z through (64, 0, 0).
	const Side right = foldTexture(wall, {0, -1, 0}, {1, 0, 0}, {64, 0, 0}, {0, 0, 1});
	CHECK(right.u.axis.y == doctest::Approx(1));
	CHECK(right.v.axis.z == doctest::Approx(-1));
	for (const Vec3 p : {Vec3{64, 0, 0}, Vec3{64, 0, 100}}) {
		CHECK(textureCoordinate(right, p).u == doctest::Approx(textureCoordinate(wall, p).u));
		CHECK(textureCoordinate(right, p).v == doctest::Approx(textureCoordinate(wall, p).v));
	}
	// Further along the right wall the texture keeps going (no jump, no stretch).
	CHECK(textureCoordinate(right, {64, 32, 0}).u == doctest::Approx(textureCoordinate(wall, {64, 0, 0}).u + 32 / 0.25));
}

TEST_CASE("texture flow: wrap round a pillar - every vertical edge meets, one seam where the loop closes") {
	MapDocument doc;
	const auto pillar = addBox(doc, {0, 0, 0}, {64, 64, 256});
	const auto sides = facesWhere(doc, pillar, [](const Vec3& n, const Vec3&) { return std::abs(n.z) < 0.5; });
	REQUIRE(sides.size() == 4);
	const FaceRef front = faceWhere(doc, pillar, [](const Vec3& n, const Vec3&) { return n.y < -0.5; });
	const FaceRef right = faceWhere(doc, pillar, [](const Vec3& n, const Vec3&) { return n.x > 0.5; });
	const FaceRef left = faceWhere(doc, pillar, [](const Vec3& n, const Vec3&) { return n.x < -0.5; });
	const FaceRef back = faceWhere(doc, pillar, [](const Vec3& n, const Vec3&) { return n.y > 0.5; });
	FlowOptions options;
	options.mode = FlowMode::Wrap;
	options.seed = front;
	// Perimeter 256 units = 1024 texels; on a 300 texel texture the loop cannot close without a seam.
	const auto plan = planTextureFlow(doc, sides, options, size(300, 300));
	REQUIRE(plan.faceCount() == 4);
	CHECK(plan.mode == FlowMode::Wrap);
	CHECK(plan.unreached == 0);
	CHECK_FALSE(plan.stretched);
	checkMeets(plan, front, right, {64, 0, 0});
	checkMeets(plan, front, right, {64, 0, 256});
	checkMeets(plan, front, left, {0, 0, 128});
	for (const auto& f : sides) checkInPlane(plan, doc, f);
	// The loop closes behind the pillar (right->back or left->back), with exactly one seam.
	CHECK(plan.seams == 1);
	// On a 512 texture the 1024 texels go round exactly twice: no seam at all.
	const auto exact = planTextureFlow(doc, sides, options, size(512, 512));
	CHECK(exact.seams == 0);
	(void)back;
}

TEST_CASE("texture flow: around a bevelled corner of four strips - U runs on round the curve by arc length") {
	MapDocument doc;
	// A rounded outer corner: four standing strips of a quarter circle, radius 64..96, 128 high.
	const auto strips = addRing(doc, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 64, 96, 0, 90, 4, 128);
	std::vector<FaceRef> outer;
	for (const auto& s : strips) {
		outer.push_back(faceWhere(doc, s, [](const Vec3& n, const Vec3& c) {
			const Vec3 radial = normalize(Vec3{c.x, c.y, 0});
			return std::abs(n.z) < 0.1 && dot(n, radial) > 0.9;
		}));
	}
	FlowOptions options;   // Auto: folds of 22.5° are a curve
	const auto plan = planTextureFlow(doc, outer, options, size(512, 512));
	CHECK(plan.mode == FlowMode::Curve);
	REQUIRE(plan.faceCount() == 4);
	CHECK(plan.seams == 0);
	CHECK_FALSE(plan.stretched);
	// V runs along the curve's axis (down), U round it.
	for (const auto& f : outer) {
		CHECK(std::abs(planned(plan, f)->v.axis.z) == doctest::Approx(1));
		checkInPlane(plan, doc, f);
	}
	// Seams between neighbours meet; U over the whole curve = the outer polyline's length / scale.
	const auto corner = [](double degrees, double z) {
		const double a = degrees * std::numbers::pi / 180.0;
		return Vec3{96 * std::cos(a), 96 * std::sin(a), z};
	};
	for (int k = 1; k < 4; ++k) checkMeets(plan, outer[static_cast<std::size_t>(k - 1)], outer[static_cast<std::size_t>(k)], corner(22.5 * k, 64));
	const Side& first = *planned(plan, outer.front());
	const Side& last = *planned(plan, outer.back());
	const double chord = 2 * 96 * std::sin(22.5 / 2 * std::numbers::pi / 180.0);
	const double total = std::abs(textureCoordinate(last, corner(90, 0)).u - textureCoordinate(first, corner(0, 0)).u);
	CHECK(total == doctest::Approx(4 * chord / first.u.scale).epsilon(1e-6));
}

TEST_CASE("texture flow: a 12-sided cylinder - U continuous all the way round, the total is the circumference") {
	MapDocument doc;
	std::vector<Vec3> points;
	for (int k = 0; k < 12; ++k) {
		const double a = k * std::numbers::pi / 6;
		points.push_back({100 * std::cos(a), 100 * std::sin(a), 0});
		points.push_back({100 * std::cos(a), 100 * std::sin(a), 200});
	}
	const auto cylinder = addSolid(doc, points);
	const auto sides = facesWhere(doc, cylinder, [](const Vec3& n, const Vec3&) { return std::abs(n.z) < 0.5; });
	REQUIRE(sides.size() == 12);
	FlowOptions options;
	options.mode = FlowMode::Curve;
	options.seed = sides.front();
	const auto plan = planTextureFlow(doc, sides, options, size(333, 333));
	REQUIRE(plan.faceCount() == 12);
	CHECK(plan.seams == 1);   // where the loop closes (12 edges, 11 meet)
	// Walk round: the U span of each face adds up to the circumference / scale.
	double total = 0;
	for (const auto& f : sides) {
		const Side& side = *planned(plan, f);
		const auto geometry = buildBrushGeometry(*doc.findSolid(cylinder));
		for (const auto& face : geometry.faces) {
			if (doc.findSolid(cylinder)->sides[static_cast<std::size_t>(face.sideIndex)].id != f.sideId) continue;
			double lo = 1e18, hi = -1e18;
			for (const auto& v : face.vertices) {
				lo = std::min(lo, textureCoordinate(side, v).u);
				hi = std::max(hi, textureCoordinate(side, v).u);
			}
			total += hi - lo;
		}
		checkInPlane(plan, doc, f);
	}
	const double edge = 2 * 100 * std::sin(std::numbers::pi / 12);
	CHECK(total == doctest::Approx(12 * edge / planned(plan, sides.front())->u.scale).epsilon(1e-6));
}

TEST_CASE("texture flow: an arch's inner curve wraps without seams") {
	MapDocument doc;
	// Half ring in the x/z plane (an arch over the y axis), 16 deep.
	const auto pieces = addRing(doc, {0, 0, 128}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 96, 128, 0, 180, 8, 16);
	std::vector<FaceRef> inner;
	for (const auto& p : pieces) {
		inner.push_back(faceWhere(doc, p, [](const Vec3& n, const Vec3& c) {
			const Vec3 radial = normalize(Vec3{c.x, 0, c.z - 128});
			return std::abs(n.y) < 0.1 && dot(n, radial) < -0.9;
		}));
	}
	const auto plan = planTextureFlow(doc, inner, FlowOptions{}, size(512, 512));
	CHECK(plan.mode == FlowMode::Curve);
	REQUIRE(plan.faceCount() == 8);
	CHECK(plan.seams == 0);
	CHECK(plan.unreached == 0);
	for (const auto& f : inner) checkInPlane(plan, doc, f);
}

TEST_CASE("texture flow: stairs - treads and risers run on into each other") {
	MapDocument doc;
	std::vector<FaceRef> faces;
	std::vector<std::pair<FaceRef, FaceRef>> steps;   // riser, tread
	for (int k = 0; k < 4; ++k) {
		const double x0 = k * 32.0, x1 = x0 + 32, top = (k + 1) * 16.0;
		const auto step = addBox(doc, {x0, 0, 0}, {x1, 128, top}, "CONCRETE/CONCRETEFLOOR001A");
		const FaceRef riser = faceWhere(doc, step, [](const Vec3& n, const Vec3&) { return n.x < -0.5; });
		const FaceRef tread = faceWhere(doc, step, [](const Vec3& n, const Vec3&) { return n.z > 0.5; });
		faces.push_back(riser);
		faces.push_back(tread);
		steps.emplace_back(riser, tread);
	}
	FlowOptions options;
	options.mode = FlowMode::Wrap;
	options.seed = steps.front().first;
	const auto plan = planTextureFlow(doc, faces, options, size(512, 512));
	REQUIRE(plan.faceCount() == 8);
	CHECK(plan.unreached == 0);
	for (int k = 0; k < 4; ++k) {
		const double x0 = k * 32.0, top = (k + 1) * 16.0;
		// The riser's top edge is the tread's front edge.
		checkMeets(plan, steps[static_cast<std::size_t>(k)].first, steps[static_cast<std::size_t>(k)].second, {x0, 64, top});
		// The tread's back edge runs into the next riser (a T-junction: it ends on that face).
		if (k < 3) checkMeets(plan, steps[static_cast<std::size_t>(k)].second, steps[static_cast<std::size_t>(k + 1)].first, {x0 + 32, 64, top});
	}
	for (const auto& f : faces) checkInPlane(plan, doc, f);
}

TEST_CASE("texture flow: a trim along an L-shaped wall base - fitted to its height, round the corner") {
	MapDocument doc;
	// Two thin baseboards, 24 high, meeting in a corner: A along x, B along y from A's back.
	const auto a = addBox(doc, {0, 0, 0}, {256, 8, 24}, "TRIM/BASEBOARD");
	const auto b = addBox(doc, {248, 8, 0}, {256, 264, 24}, "TRIM/BASEBOARD");
	const FaceRef aFront = faceWhere(doc, a, [](const Vec3& n, const Vec3&) { return n.y > 0.5; });
	const FaceRef bFront = faceWhere(doc, b, [](const Vec3& n, const Vec3&) { return n.x < -0.5; });
	const auto plan = planTextureFlow(doc, {aFront, bFront}, FlowOptions{}, size(512, 128));
	CHECK(plan.mode == FlowMode::Path);
	CHECK(plan.preset == FlowPreset::Trim);
	REQUIRE(plan.faceCount() == 2);
	CHECK(plan.stripSize == doctest::Approx(24));
	CHECK_FALSE(plan.unevenStrips);
	// The texture's 128 texels of height span the 24 units; its top sits on the trim's top.
	const Side& sa = *planned(plan, aFront);
	CHECK(sa.v.scale == doctest::Approx(24.0 / 128));
	CHECK(textureCoordinate(sa, {100, 8, 24}).v == doctest::Approx(0).epsilon(1e-9));
	CHECK(textureCoordinate(sa, {100, 8, 0}).v == doctest::Approx(128));
	// Round the corner (B's front ends on A's front): the texture meets.
	checkMeets(plan, aFront, bFront, {248, 8, 12});
	CHECK(plan.seams == 0);
	// Two repeats across: half the scale.
	FlowOptions twice;
	twice.repeatsAcross = 2;
	CHECK(planned(planTextureFlow(doc, {aFront, bFront}, twice, size(512, 128)), aFront)->v.scale == doctest::Approx(12.0 / 128));
}

TEST_CASE("texture flow: a river strip with a bend runs along it (floors use the road tool)") {
	MapDocument doc;
	const auto r1 = addBox(doc, {0, 0, 0}, {512, 128, 8}, "NATURE/WATER_RIVER");
	const auto r2 = addBox(doc, {384, 128, 0}, {512, 512, 8}, "NATURE/WATER_RIVER");
	const std::vector<FaceRef> tops{faceWhere(doc, r1, [](const Vec3& n, const Vec3&) { return n.z > 0.5; }),
	                                faceWhere(doc, r2, [](const Vec3& n, const Vec3&) { return n.z > 0.5; })};
	FlowOptions options;
	options.mode = FlowMode::Path;
	options.preset = FlowPreset::River;
	options.curveCorners = false;
	const auto plan = planTextureFlow(doc, tops, options, size(512, 1024));
	CHECK(plan.mode == FlowMode::Path);
	CHECK(plan.preset == FlowPreset::River);
	CHECK(plan.faceCount() == 2);
	CHECK(plan.plan.chains >= 1);
	for (const auto& change : plan.plan.faces) CHECK(change.side.material == "NATURE/WATER_RIVER");
	// Auto sees only floors: a road-like path.
	const auto automatic = planTextureFlow(doc, tops, FlowOptions{}, size(512, 1024));
	CHECK(automatic.mode == FlowMode::Path);
	CHECK(automatic.preset == FlowPreset::Road);
}

TEST_CASE("texture flow: match neighbour - the selected face continues the face next to it") {
	MapDocument doc;
	const auto left = addBox(doc, {0, 0, 0}, {128, 16, 128});
	const auto right = addBox(doc, {128, 0, 0}, {256, 16, 128});
	const FaceRef leftFront = faceWhere(doc, left, [](const Vec3& n, const Vec3&) { return n.y < -0.5; });
	const FaceRef rightFront = faceWhere(doc, right, [](const Vec3& n, const Vec3&) { return n.y < -0.5; });
	// The left wall is aligned with an odd shift and scale; the right one is not.
	for (auto& side : doc.findSolid(left)->sides) {
		if (side.id != leftFront.sideId) continue;
		side.u = {{1, 0, 0}, 37, 0.5};
		side.v = {{0, 0, -1}, 11, 0.5};
	}
	FlowOptions options;
	options.mode = FlowMode::Match;
	const auto plan = planTextureFlow(doc, {rightFront}, options, size(512, 512));
	REQUIRE(plan.faceCount() == 1);
	const Side& side = *planned(plan, rightFront);
	CHECK(side.u.scale == doctest::Approx(0.5));
	const Side& leftSide = *std::find_if(doc.findSolid(left)->sides.begin(), doc.findSolid(left)->sides.end(), [&](const Side& s) { return s.id == leftFront.sideId; });
	for (const Vec3 p : {Vec3{128, 0, 0}, Vec3{128, 0, 128}}) {
		CHECK(textureCoordinate(side, p).u == doctest::Approx(textureCoordinate(leftSide, p).u));
		CHECK(textureCoordinate(side, p).v == doctest::Approx(textureCoordinate(leftSide, p).v));
	}
	// No unselected face next to them: nothing to match.
	MapDocument lonely;
	const auto alone = addBox(lonely, {0, 0, 0}, {64, 64, 64});
	Selection all;
	all.solids = {alone};
	const auto none = planTextureFlow(lonely, flowFaces(lonely, all), options, size(512, 512));
	CHECK(none.empty());
	CHECK(none.unreached == 6);
}

TEST_CASE("texture flow: Auto picks wrap for a box, apply is one undo step with the seed's material") {
	MapDocument doc;
	const auto box = addBox(doc, {0, 0, 0}, {64, 64, 64});
	for (auto& side : doc.findSolid(box)->sides) side.material = "TOOLS/TOOLSNODRAW";
	const FaceRef top = faceWhere(doc, box, [](const Vec3& n, const Vec3&) { return n.z > 0.5; });
	for (auto& side : doc.findSolid(box)->sides) {
		if (side.id == top.sideId) side.material = "METAL/METALWALL001A";
	}
	Selection selection;
	selection.solids = {box};
	auto faces = flowFaces(doc, selection);
	REQUIRE(faces.size() == 6);
	FlowOptions options;
	options.seed = top;
	const auto plan = planTextureFlow(doc, faces, options, size(256, 256));
	CHECK(plan.mode == FlowMode::Wrap);
	CHECK(plan.faceCount() == 6);
	const std::string before = doc.serialize();
	History history;
	history.push(edit::applyTextureFlow(doc, plan));
	for (const auto& side : doc.findSolid(box)->sides) CHECK(side.material == "METAL/METALWALL001A");
	REQUIRE(history.canUndo());
	history.undo(doc);
	CHECK(doc.serialize() == before);
}
