// test_decals.cpp - Infodecal and info_overlay geometry: decal axes, clipping, where decals
// land, overlays from their keys and the keys of a new overlay on a face.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/brush.h>
#include <sme/decals.h>
#include <sme/game.h>
#include <sme/game_discovery.h>

#include <cmath>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

double area(const std::vector<Vec3>& polygon) {
	double sum = 0;
	for (std::size_t i = 2; i < polygon.size(); ++i) sum += 0.5 * length(cross(polygon[i - 1] - polygon[0], polygon[i] - polygon[0]));
	return sum;
}

const SurfaceFace* faceFacing(const std::vector<SurfaceFace>& faces, const Vec3& normal, double dist) {
	for (const auto& face : faces) {
		if (dot(face.normal, normal) > 0.99 && std::abs(face.dist - dist) < 0.01) return &face;
	}
	return nullptr;
}

} // namespace

TEST_CASE("decal axes: upright and not mirrored seen from the front") {
	Vec3 s, t;
	decalAxes({0, 0, 1}, s, t);        // floor: S along +X, T toward -Y (down seen from above)
	CHECK(s.x == doctest::Approx(1));
	CHECK(t.y == doctest::Approx(-1));
	decalAxes({0, -1, 0}, s, t);       // wall facing -Y: right is +X, down is -Z
	CHECK(s.x == doctest::Approx(1));
	CHECK(t.z == doctest::Approx(-1));
	decalAxes({1, 0, 0}, s, t);        // wall facing +X: right is +Y
	CHECK(s.y == doctest::Approx(1));
	CHECK(t.z == doctest::Approx(-1));
	for (const Vec3 n : {Vec3{0, 0, 1}, Vec3{0, 0, -1}, Vec3{0.6, 0.8, 0}, Vec3{0.3, 0.2, 0.9}}) {
		decalAxes(n, s, t);
		CHECK(dot(s, n) == doctest::Approx(0).epsilon(1e-9));
		CHECK(dot(t, n) == doctest::Approx(0).epsilon(1e-9));
		CHECK(dot(cross(s, t), normalize(n)) == doctest::Approx(-1));   // T = S x N
	}
}

TEST_CASE("clipping a textured polygon to a face") {
	const std::vector<Vec3> face = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};   // counter-clockwise from +Z
	const std::vector<Vec3> square = {{5, 5, 0}, {15, 5, 0}, {15, 15, 0}, {5, 15, 0}};
	const std::vector<std::array<double, 2>> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	const auto [points, coords] = clipToPolygon(square, uv, face, {0, 0, 1});
	REQUIRE(points.size() == 4);
	CHECK(area(points) == doctest::Approx(25));
	for (std::size_t i = 0; i < points.size(); ++i) {
		// Texture coordinates follow the cut: u = (x - 5) / 10, v = (y - 5) / 10.
		CHECK(coords[i][0] == doctest::Approx((points[i].x - 5) / 10));
		CHECK(coords[i][1] == doctest::Approx((points[i].y - 5) / 10));
	}
	const std::vector<Vec3> outside = {{20, 20, 0}, {30, 20, 0}, {30, 30, 0}};
	CHECK(clipToPolygon(outside, {{0, 0}, {1, 0}, {1, 1}}, face, {0, 0, 1}).first.empty());
}

TEST_CASE("infodecal lands on the face under it, split over coplanar brushes") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {128, 128, 64}), "DEV/FLOOR"));
	doc.world.solids.push_back(makeBlock(doc, box({128, 0, 0}, {256, 128, 64}), "DEV/FLOOR"));
	const auto faces = collectSurfaceFaces(doc);
	CHECK(faces.size() == 12);
	// On the top of the first block, away from edges: one patch of 32 x 16.
	auto patches = decalPatches(faces, {64, 64, 64}, 32, 16);
	REQUIRE(patches.size() == 1);
	CHECK(patches[0].normal.z == doctest::Approx(1));
	CHECK(area(patches[0].vertices) == doctest::Approx(32 * 16));
	for (const auto& v : patches[0].vertices) CHECK(v.z == doctest::Approx(64));
	// The texture's u runs along +X on floors.
	for (std::size_t i = 0; i < patches[0].vertices.size(); ++i) {
		CHECK(patches[0].uv[i][0] == doctest::Approx((patches[0].vertices[i].x - 48) / 32));
	}
	// Counter-clockwise seen from above (front).
	const auto& v = patches[0].vertices;
	CHECK(dot(cross(v[1] - v[0], v[2] - v[0]), Vec3{0, 0, 1}) > 0);
	// Over the seam between both blocks: two patches, together the whole decal.
	patches = decalPatches(faces, {128, 64, 66}, 64, 32);
	REQUIRE(patches.size() == 2);
	CHECK(area(patches[0].vertices) + area(patches[1].vertices) == doctest::Approx(64 * 32));
	// On a wall: the side facing -Y, upright.
	patches = decalPatches(faces, {64, -1, 32}, 16, 16);
	REQUIRE(patches.size() == 1);
	CHECK(patches[0].normal.y == doctest::Approx(-1));
	for (std::size_t i = 0; i < patches[0].vertices.size(); ++i) {
		CHECK(patches[0].uv[i][1] == doctest::Approx((40 - patches[0].vertices[i].z) / 16));   // v grows downward
	}
	// Far from every surface: nothing.
	CHECK(decalPatches(faces, {64, 64, 200}, 16, 16).empty());
}

TEST_CASE("info_overlay from its keys, clipped to its faces") {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {128, 128, 64}), "DEV/FLOOR"));
	const auto faces = collectSurfaceFaces(doc);
	const SurfaceFace* top = faceFacing(faces, {0, 0, 1}, 64);
	REQUIRE(top);
	// Keys as placed on the top face: 64 x 32 at (100, 64, 64) - it hangs over the edge at x = 128.
	Entity entity;
	entity.set("classname", "info_overlay");
	for (const auto& [key, value] : overlayKeysOnFace({100, 64, 64}, top->normal, top->sideId, "decals/arrow", 64, 32)) entity.set(key, value);
	entity.setOrigin({100, 64, 64});
	CHECK(entity.get("sides") == std::to_string(top->sideId));
	CHECK(entity.get("BasisNormal") == "0 0 1");
	CHECK(entity.get("BasisU") == "1 0 0");
	CHECK(entity.get("BasisV") == "0 -1 0");
	CHECK(entity.get("uv0") == "-32 -16 0");
	CHECK(entity.get("uv2") == "32 16 0");
	const auto overlay = readOverlay(entity);
	REQUIRE(overlay);
	const auto corners = overlayCorners(*overlay);
	CHECK(corners[0].x == doctest::Approx(68));   // uv0: left (-U) and top (-V = +Y)
	CHECK(corners[0].y == doctest::Approx(80));
	const auto patches = overlayPatches(faces, *overlay);
	REQUIRE(patches.size() == 2);   // two triangles
	double covered = 0;
	for (const auto& patch : patches) {
		covered += area(patch.vertices);
		for (std::size_t i = 0; i < patch.vertices.size(); ++i) {
			CHECK(patch.vertices[i].x <= 128 + 1e-6);   // cut at the face's edge
			// u from StartU at x = 68 to EndU at x = 132; v from StartV at y = 80 to EndV at y = 48.
			CHECK(patch.uv[i][0] == doctest::Approx((patch.vertices[i].x - 68) / 64));
			CHECK(patch.uv[i][1] == doctest::Approx((80 - patch.vertices[i].y) / 32));
		}
	}
	CHECK(covered == doctest::Approx(60 * 32));
	// A side that is not listed gets nothing; a missing basis is no overlay.
	Entity other = entity;
	other.set("sides", "999999");
	CHECK(overlayPatches(faces, *readOverlay(other)).empty());
	other.set("BasisNormal", "");
	CHECK_FALSE(readOverlay(other));
}

TEST_CASE("decal size: base texture size x $decalscale from the game's files") {
	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "sourcemods/mymod") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);
	const auto arrow = decalSize(game->files, "decals/demo_arrow");   // 128 x 128 at $decalscale 0.5
	REQUIRE(arrow);
	CHECK(arrow->first == doctest::Approx(64));
	CHECK(arrow->second == doctest::Approx(64));
	const auto wall = decalSize(game->files, "DEMO/BrickWall001");   // no $decalscale: texture size
	REQUIRE(wall);
	CHECK(wall->first == doctest::Approx(256));
	CHECK_FALSE(decalSize(game->files, "decals/missing"));
}
