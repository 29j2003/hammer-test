// test_edge_round.cpp - Rounding corners in real scenes: curbs on and in floors, raised blocks,
// L walls, walls stacked from bands (also of different heights on the two sides), several corners
// at once, merging when nothing else works, the Problems check for broken brushes, and a fuzz test
// that every result is sound, can be picked, undone exactly and saved stably.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/csg.h>
#include <sme/edge_round.h>
#include <sme/editing.h>

#include <algorithm>
#include <random>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

SolidRef block(MapDocument& doc, Vec3 lo, Vec3 hi, const char* material = "WALL") {
	return edit::createBlock(doc, box(lo, hi), material).second;
}

FaceRef facing(const MapDocument& doc, const SolidRef& ref, Vec3 normal) {
	for (const auto& side : doc.findSolid(ref)->sides) {
		if (dot(side.plane().normal, normal) > 0.999) return {ref, side.id};
	}
	return {};
}

bool filled(const MapDocument& doc, const Vec3& p) {
	return std::any_of(doc.world.solids.begin(), doc.world.solids.end(), [&](const Solid& solid) { return solidContains(solid, p, 0.01); });
}

/// Every brush sound for Source, none overlapping another.
void checkSound(const MapDocument& doc) {
	const auto& all = doc.world.solids;
	for (const auto& solid : all) {
		INFO("brush " << solid.id << ": " << brushProblem(solid));
		CHECK(brushProblem(solid).empty());
	}
	for (std::size_t i = 0; i < all.size(); ++i) {
		for (std::size_t j = i + 1; j < all.size(); ++j) {
			INFO("brushes " << all[i].id << " and " << all[j].id);
			CHECK(sharedVolume(all[i], all[j]) < 0.5);
		}
	}
}

/// Faces that can be seen but not clicked: a face counts when a point just in front of it lies
/// in no other brush, yet a ray onto it picks another face (sampled at its middle and toward
/// each corner; a face fails only if every visible sample fails).
int unpickableFaces(const MapDocument& doc) {
	int bad = 0;
	for (const auto& solid : doc.world.solids) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			const int sideId = solid.sides[std::size_t(face.sideIndex)].id;
			Vec3 c{};
			for (const auto& v : face.vertices) c = c + v;
			c = c / double(face.vertices.size());
			std::vector<Vec3> samples{c};
			for (const auto& v : face.vertices) samples.push_back(c + (v - c) * 0.6);
			int visible = 0, picked = 0;
			int otherBrush = -1, otherSide = -1;
			for (const auto& p : samples) {
				const Vec3 out = p + face.normal * 0.25;
				if (std::any_of(doc.world.solids.begin(), doc.world.solids.end(), [&](const Solid& o) { return o.id != solid.id && solidContains(o, out, 0.01); })) continue;
				++visible;
				const Ray ray{p + face.normal * 64, -face.normal};
				double best = 1e30;
				for (const auto& o : doc.world.solids) {
					if (const auto hit = intersect(ray, o); hit && hit->distance < best) {
						best = hit->distance;
						otherBrush = o.id;
						otherSide = o.sides[std::size_t(hit->sideIndex)].id;
					}
				}
				// Picked if this face is among the nearest hits (ties: brushes touching there).
				const auto hit = intersect(ray, solid);
				if (hit && hit->distance <= best + 1e-4 && solid.sides[std::size_t(hit->sideIndex)].id == sideId) ++picked;
			}
			if (visible > 0 && picked == 0) {
				++bad;
				MESSAGE("face " << sideId << " of brush " << solid.id << " at " << c.x << " " << c.y << " " << c.z << " normal " << face.normal.x << " " << face.normal.y << " " << face.normal.z << " cannot be picked (brush " << otherBrush << " side " << otherSide << ")");
			}
		}
	}
	return bad;
}

std::size_t countOutward(const MapDocument& doc) {
	return doc.world.solids.size();
}

} // namespace

TEST_CASE("round edge: a curb on a floor next to a wall - end rounded, every face sound and clickable") {
	// Julius (31.10.2026): some faces of a rounded curb could not be clicked.
	MapDocument doc;
	block(doc, {-256, -256, -16}, {512, 512, 0}, "FLOOR");
	block(doc, {-256, 512, -16}, {512, 528, 128});
	const SolidRef curb = block(doc, {0, 480, 0}, {256, 512, 16}, "CURB");
	History history;
	const std::string before = doc.serialize();
	auto change = edit::roundEdge(doc, facing(doc, curb, {1, 0, 0}), facing(doc, curb, {0, -1, 0}), 24, 0);
	REQUIRE(change);
	history.push(std::move(*change));
	CHECK(countOutward(doc) == 3);   // the curb stays one brush, nothing added
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
	CHECK_FALSE(filled(doc, {255, 481, 8}));
	CHECK(filled(doc, {240, 496, 8}));
	history.undo(doc);
	CHECK(doc.serialize() == before);
}

TEST_CASE("round edge: a curb sunk into a carved floor - one brush, the gap filled, nothing inside it") {
	MapDocument doc;
	const SolidRef curb = block(doc, {0, 480, -8}, {256, 512, 16}, "CURB");
	for (const auto& [lo, hi] : std::vector<std::pair<Vec3, Vec3>>{{{-256, -256, -16}, {0, 512, 0}}, {{256, -256, -16}, {512, 512, 0}}, {{0, -256, -16}, {256, 480, 0}}}) {
		block(doc, lo, hi, "FLOOR");
	}
	block(doc, {0, 480, -16}, {256, 512, -8}, "FLOOR");
	block(doc, {-256, 512, -16}, {512, 528, 128});
	MapDocument scratch;
	const auto plan = planEdgeRound(doc, scratch, facing(doc, curb, {1, 0, 0}), facing(doc, curb, {0, -1, 0}), 24, 6);
	REQUIRE(plan.ok());
	REQUIRE(plan.replacements.size() == 1);
	CHECK(plan.replacements[0].pieces.size() == 1);   // not split at the floor (a floor would show inside it)
	CHECK(plan.gapFills > 0);
	CHECK(plan.gapFills <= plan.segments);           // strips and one block, not a fan
	REQUIRE(edit::roundEdge(doc, facing(doc, curb, {1, 0, 0}), facing(doc, curb, {0, -1, 0}), 24, 6));
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
	CHECK(filled(doc, {255.5, 480.5, -4}));      // below the floor's top: filled, no hole
	CHECK_FALSE(filled(doc, {255.5, 480.5, 8}));  // above it: rounded
	// No face of any brush lies inside the curb (a floor seen from inside it, Julius' screenshot).
	const Solid* rounded = doc.findSolid(curb);
	REQUIRE(rounded);
	for (const auto& solid : doc.world.solids) {
		if (solid.id == curb.solidId) continue;
		for (const auto& face : buildBrushGeometry(solid).faces) {
			Vec3 c{};
			for (const auto& v : face.vertices) c = c + v;
			c = c / double(face.vertices.size());
			CHECK_FALSE(solidContains(*rounded, c, -0.01));
		}
	}
}

TEST_CASE("round edge: a raised block on a floor and an inner corner of a curb with a wall") {
	MapDocument doc;
	const SolidRef floor = block(doc, {-256, -256, -16}, {512, 512, 0}, "FLOOR");
	const SolidRef raised = block(doc, {0, 0, 0}, {128, 128, 64}, "BLOCK");
	REQUIRE(edit::roundEdge(doc, facing(doc, raised, {-1, 0, 0}), facing(doc, raised, {0, -1, 0}), 32, 0));
	CHECK(doc.world.solids.size() == 2);   // nothing filled: the floor does not cover its sides
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
	// Inner corner between the floor's top and the block's side: a cove of strips.
	MapDocument scratch;
	const auto cove = planEdgeRound(doc, scratch, facing(doc, floor, {0, 0, 1}), facing(doc, raised, {1, 0, 0}), 16, 0);
	REQUIRE(cove.ok());
	CHECK(cove.inner);
	CHECK(cove.innerFills == cove.segments);   // one brush per segment, not more
	REQUIRE(edit::roundEdge(doc, facing(doc, floor, {0, 0, 1}), facing(doc, raised, {1, 0, 0}), 16, 0));
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
}

TEST_CASE("round edge: a wall corner stacked from three bands per side, all faces selected - one curve through all") {
	// Julius (31.10.2026, screenshot): many faces on both sides of a wall corner gave "select the
	// two faces next to the edge first".
	MapDocument doc;
	std::vector<SolidRef> along, across;
	for (int band = 0; band < 3; ++band) {
		const double lo = band * 64, hi = lo + 64;
		along.push_back(block(doc, {0, 0, lo}, {256, 16, hi}));
		across.push_back(block(doc, {0, 16, lo}, {16, 256, hi}));
	}
	std::vector<FaceRef> faces;
	for (const auto& ref : along) faces.push_back(facing(doc, ref, {0, -1, 0}));
	for (const auto& ref : across) faces.push_back(facing(doc, ref, {-1, 0, 0}));
	MapDocument scratch;
	const auto plan = planEdgeRounds(doc, scratch, faces, 32, 6);
	REQUIRE(plan.ok());
	CHECK(plan.corners == 1);
	CHECK(plan.ignoredFaces == 0);
	History history;
	const std::string before = doc.serialize();
	auto change = applyEdgeRound(doc, planEdgeRounds(doc, doc, faces, 32, 6), "Round edge");
	REQUIRE(change);
	history.push(std::move(*change));
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
	// The same curve at every height: corner gone, the arc's middle still wall.
	const double middle = 32 - 32 / std::sqrt(2.0);
	for (const double z : {10.0, 100.0, 180.0}) {
		CHECK_FALSE(filled(doc, {1, 1, z}));
		CHECK(filled(doc, {middle + 1.5, middle + 1.5, z}));
		CHECK(filled(doc, {8, 100, z}));
	}
	history.undo(doc);
	CHECK(doc.serialize() == before);
}

TEST_CASE("round edge: stacks of different heights on the two sides, and faces off the corner left out") {
	// Julius (31.10.2026, screenshot): the lower band is split at another height on one side.
	MapDocument doc;
	std::vector<SolidRef> along, across;
	for (const auto& [lo, hi] : std::vector<std::pair<double, double>>{{0, 64}, {64, 128}, {128, 192}}) along.push_back(block(doc, {0, 0, lo}, {256, 16, hi}));
	for (const auto& [lo, hi] : std::vector<std::pair<double, double>>{{0, 48}, {48, 128}, {128, 192}}) across.push_back(block(doc, {0, 16, lo}, {16, 256, hi}));
	const SolidRef far = block(doc, {1000, 500, 0}, {1100, 600, 500});
	std::vector<FaceRef> faces;
	for (const auto& ref : along) faces.push_back(facing(doc, ref, {0, -1, 0}));
	for (const auto& ref : across) faces.push_back(facing(doc, ref, {-1, 0, 0}));
	faces.push_back(facing(doc, far, {0, 0, 1}));   // touches no corner: left out, not refused
	MapDocument scratch;
	const auto plan = planEdgeRounds(doc, scratch, faces, 32, 6);
	INFO(plan.problem);
	REQUIRE(plan.ok());
	CHECK(plan.corners == 1);
	CHECK(plan.ignoredFaces == 1);
	REQUIRE(applyEdgeRound(doc, planEdgeRounds(doc, doc, faces, 32, 6), "Round edge"));
	checkSound(doc);
	CHECK(unpickableFaces(doc) == 0);
	const double middle = 32 - 32 / std::sqrt(2.0);
	for (const double z : {24.0, 56.0, 100.0, 160.0}) {
		CHECK_FALSE(filled(doc, {1, 1, z}));
		CHECK(filled(doc, {middle + 1.5, middle + 1.5, z}));
	}
}

TEST_CASE("round edge: several corners at once get the same rounding in one undo step") {
	MapDocument doc;
	const SolidRef pillar = block(doc, {0, 0, 0}, {128, 128, 128}, "BLOCK");
	const SolidRef other = block(doc, {512, 0, 0}, {640, 128, 128}, "BLOCK");
	const std::vector<FaceRef> faces{facing(doc, pillar, {0, 0, 1}), facing(doc, pillar, {1, 0, 0}), facing(doc, pillar, {-1, 0, 0}),
	                                 facing(doc, other, {0, -1, 0}), facing(doc, other, {1, 0, 0})};
	MapDocument scratch;
	const auto preview = planEdgeRounds(doc, scratch, faces, 24, 4);
	REQUIRE(preview.ok());
	CHECK(preview.corners == 3);
	// The preview's new ids do not clash with the map's.
	for (const auto& solid : preview.added) CHECK(doc.findSolid({0, solid.id}) == nullptr);
	History history;
	const std::string before = doc.serialize();
	auto change = applyEdgeRound(doc, planEdgeRounds(doc, doc, faces, 24, 4), "Round edge");
	REQUIRE(change);
	history.push(std::move(*change));
	CHECK(doc.world.solids.size() == 2);
	checkSound(doc);
	CHECK_FALSE(filled(doc, {127.5, 64, 127.5}));   // top-east
	CHECK_FALSE(filled(doc, {0.5, 64, 127.5}));     // top-west
	CHECK_FALSE(filled(doc, {639.5, 0.5, 64}));     // the other block's corner
	CHECK(filled(doc, {64, 64, 127.5}));
	history.undo(doc);
	CHECK(doc.serialize() == before);
}

TEST_CASE("round edge: pieces that can only be rounded as one brush - merge offered, done only when asked") {
	// A skin brush thinner than half a unit on the face: rounding it apart would leave slivers.
	MapDocument doc;
	const SolidRef main = block(doc, {0, 0, 0}, {128, 128, 64}, "BLOCK");
	const SolidRef skin = block(doc, {128, 0, 0}, {128.375, 128, 64}, "BLOCK");
	const std::vector<FaceRef> faces{facing(doc, skin, {1, 0, 0}), facing(doc, main, {0, -1, 0}), facing(doc, skin, {0, -1, 0})};
	MapDocument scratch;
	const auto offer = planEdgeRounds(doc, scratch, faces, 24, 4);
	INFO(offer.problem << " radius " << offer.radius);
	CHECK_FALSE(offer.ok());
	CHECK(offer.needsMerge);
	CHECK(offer.problem.find("Merge and round") != std::string::npos);
	const auto merged = planEdgeRounds(doc, doc, faces, 24, 4, EdgeRoundOptions{true});
	REQUIRE(merged.ok());
	CHECK(merged.merged);
	REQUIRE(applyEdgeRound(doc, merged, "Round edge"));
	CHECK(doc.world.solids.size() == 1);
	checkSound(doc);
	CHECK_FALSE(filled(doc, {128, 0.5, 32}));
	CHECK(filled(doc, {100, 64, 32}));
}

TEST_CASE("problems: broken and doubled brushes are found and can be deleted") {
	MapDocument doc;
	block(doc, {0, 0, 0}, {64, 64, 64});
	const SolidRef twin = block(doc, {0, 0, 0}, {64, 64, 64});
	const SolidRef thin = block(doc, {200, 0, 0}, {264, 64, 0.3});
	const auto problems = checkMap(doc, CheckContext{});
	const auto find = [&](Problem::Kind kind) {
		return std::find_if(problems.begin(), problems.end(), [&](const Problem& p) { return p.kind == kind; });
	};
	REQUIRE(find(Problem::Kind::DuplicateBrush) != problems.end());
	REQUIRE(find(Problem::Kind::BrokenBrush) != problems.end());
	CHECK(find(Problem::Kind::BrokenBrush)->message.find("thinner") != std::string::npos);
	(void)applyFix(doc, *find(Problem::Kind::DuplicateBrush), nullptr);
	(void)applyFix(doc, *find(Problem::Kind::BrokenBrush), nullptr);
	CHECK(doc.world.solids.size() == 1);
	CHECK(doc.findSolid(twin) == nullptr);
	CHECK(doc.findSolid(thin) == nullptr);
}

TEST_CASE("round edge fuzz: random boxes, rooms and corners - always sound, clickable, undone exactly, saved stably") {
	std::mt19937 random(20261031);
	const auto pick = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(random); };
	const auto grid = [&](double lo, double hi) { return std::round(pick(lo, hi) / 8) * 8; };
	int planned = 0;
	for (int round = 0; round < 60; ++round) {
		MapDocument doc;
		const Vec3 lo{grid(-64, 64), grid(-64, 64), grid(-32, 32)};
		const Vec3 hi = lo + Vec3{grid(16, 256), grid(16, 256), grid(16, 192)};
		const SolidRef main = block(doc, lo, hi, "BLOCK");
		const int scene = round % 4;
		std::vector<FaceRef> faces;
		const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		const int a = int(pick(0, 6)) % 6;
		int b = int(pick(0, 6)) % 6;
		if (b / 2 == a / 2) b = (b + 2) % 6;
		if (scene == 0) {
			faces = {facing(doc, main, axes[a]), facing(doc, main, axes[b])};
		} else if (scene == 1) {
			// Sunk into a floor carved around it.
			const double top = lo.z + grid(8, std::max(8.0, hi.z - lo.z - 8));
			for (const auto& [flo, fhi] : std::vector<std::pair<Vec3, Vec3>>{{{lo.x - 256, lo.y - 256, lo.z}, {lo.x, hi.y + 256, top}},
			                                                                  {{hi.x, lo.y - 256, lo.z}, {hi.x + 256, hi.y + 256, top}},
			                                                                  {{lo.x, lo.y - 256, lo.z}, {hi.x, lo.y, top}},
			                                                                  {{lo.x, hi.y, lo.z}, {hi.x, hi.y + 256, top}}}) {
				block(doc, flo, fhi, "FLOOR");
			}
			const int side = int(pick(0, 4)) % 4;
			faces = {facing(doc, main, axes[side]), facing(doc, main, axes[side < 2 ? 2 + int(pick(0, 2)) % 2 : int(pick(0, 2)) % 2])};
		} else if (scene == 2) {
			// Inner corner: a wall standing on the block's top.
			const SolidRef wall = block(doc, {hi.x - 16, lo.y, hi.z}, {hi.x, hi.y, hi.z + grid(32, 128)});
			faces = {facing(doc, main, {0, 0, 1}), facing(doc, wall, {-1, 0, 0})};
		} else {
			// A stack: a second band on top, both sides selected.
			const SolidRef top = block(doc, {lo.x, lo.y, hi.z}, {hi.x, hi.y, hi.z + grid(16, 96)}, "BLOCK");
			faces = {facing(doc, main, {1, 0, 0}), facing(doc, main, {0, -1, 0}), facing(doc, top, {1, 0, 0}), facing(doc, top, {0, -1, 0})};
		}
		const double radius = pick(1, 120);
		const int segments = int(pick(0, 13));
		INFO("round " << round << " scene " << scene << " radius " << radius << " segments " << segments);
		MapDocument scratch;
		const auto preview = planEdgeRounds(doc, scratch, faces, radius, segments);
		const std::string before = doc.serialize();
		CHECK(doc.serialize() == before);   // planning changes nothing
		if (!preview.ok()) continue;
		++planned;
		for (const auto& solid : preview.resultSolids()) CHECK(brushProblem(solid).empty());
		auto change = applyEdgeRound(doc, planEdgeRounds(doc, doc, faces, radius, segments), "Round edge");
		REQUIRE(change);
		checkSound(doc);
		CHECK(unpickableFaces(doc) == 0);
		// Saved and read again: the same text.
		const std::string saved = doc.serialize();
		CHECK(MapDocument::parse(saved).serialize() == saved);
		History history;
		history.push(std::move(*change));
		history.undo(doc);
		CHECK(doc.serialize() == before);
	}
	CHECK(planned > 30);
}
