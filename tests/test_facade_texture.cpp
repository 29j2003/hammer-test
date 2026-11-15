// test_facade_texture.cpp - What a texture is for in a building, read from its picture (generated
// textures: windows in a grid, plain brick and plaster, strips, roofs, shop fronts, far facades).

#include <doctest/doctest.h>

#include <sme/facade_texture.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

using namespace sme;

namespace {

struct Rgb {
	double r, g, b, a = 1;
};

double noise(int x, int y, int seed) {
	std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(seed) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffff) / 65535.0;
}

ImageRgba paint(int w, int h, const std::function<Rgb(double u, double v, int x, int y)>& shader) {
	ImageRgba image;
	image.width = static_cast<std::uint16_t>(w);
	image.height = static_cast<std::uint16_t>(h);
	image.pixels.resize(static_cast<std::size_t>(w) * h * 4);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const Rgb c = shader((x + 0.5) / w, (y + 0.5) / h, x, y);
			const std::size_t p = (static_cast<std::size_t>(y) * w + x) * 4;
			const auto byte = [](double v) { return std::byte(static_cast<unsigned char>(std::clamp(v, 0.0, 1.0) * 255 + 0.5)); };
			image.pixels[p] = byte(c.r);
			image.pixels[p + 1] = byte(c.g);
			image.pixels[p + 2] = byte(c.b);
			image.pixels[p + 3] = byte(c.a);
		}
	}
	return image;
}

/// A plaster wall with windows: `cols` x `rows` per tile, glass dark with a light frame and mullions.
ImageRgba plasterWithWindows(int cols, int rows, double width = 0.44, double height = 0.55, int size = 512) {
	return paint(size, size, [&](double u, double v, int x, int y) {
		const double n = (noise(x / 4, y / 4, 3) - 0.5) * 0.08;
		const Rgb wall{0.74 + n, 0.68 + n, 0.56 + n};
		const double cu = std::fmod(u * cols, 1.0), cv = std::fmod(v * rows, 1.0);
		const double u0 = 0.5 - width / 2, u1 = 0.5 + width / 2, v0 = 0.5 - height / 2, v1 = 0.5 + height / 2;
		if (cu < u0 - 0.03 || cu > u1 + 0.03 || cv < v0 - 0.03 || cv > v1 + 0.03) return wall;
		if (cu < u0 || cu > u1 || cv < v0 || cv > v1) return Rgb{0.85, 0.84, 0.8};   // frame
		const double mu = (cu - u0) / (u1 - u0), mv = (cv - v0) / (v1 - v0);
		if (std::abs(mu - 0.5) < 0.03 || std::abs(mv - 0.35) < 0.025) return Rgb{0.8, 0.79, 0.75};   // mullions
		return Rgb{0.18 + 0.1 * mv, 0.21 + 0.1 * mv, 0.26 + 0.1 * mv};   // glass
	});
}

ImageRgba plainBrick() {
	return paint(512, 512, [](double u, double v, int x, int y) {
		const int course = static_cast<int>(v * 16);
		const double shift = (course % 2) * 0.5;
		const double bu = std::fmod(u * 8 + shift, 1.0), bv = std::fmod(v * 16, 1.0);
		if (bu < 0.06 || bv < 0.12) return Rgb{0.72, 0.7, 0.66};   // mortar
		const int brick = static_cast<int>(u * 8 + shift) + course * 31;
		const double tint = (noise(brick, course, 9) - 0.5) * 0.18 + (noise(x, y, 4) - 0.5) * 0.05;
		return Rgb{0.55 + tint, 0.27 + tint * 0.6, 0.2 + tint * 0.4};
	});
}

/// Plaster with grain and a few round stains (no windows).
ImageRgba stainedPlaster() {
	return paint(512, 512, [](double u, double v, int x, int y) {
		double n = (noise(x / 3, y / 3, 11) - 0.5) * 0.1;
		for (const auto& [cu, cv, r] : {std::tuple{0.3, 0.3, 0.09}, std::tuple{0.7, 0.62, 0.12}, std::tuple{0.2, 0.8, 0.07}}) {
			const double d = std::hypot(u - cu, v - cv);
			if (d < r) n -= 0.35 * (1 - d / r);
		}
		return Rgb{0.76 + n, 0.72 + n, 0.64 + n};
	});
}

} // namespace

TEST_CASE("facade textures: windows in the picture are found - where, how many per tile, how many floors one tile spans") {
	SUBCASE("two windows across, one floor (HL2's building_template plaster)") {
		const auto a = analyzeTexture(plasterWithWindows(2, 1), "building_template/building_template007a");
		CHECK(a.role == TextureRole::FacadeWindows);
		CHECK(a.hasWindows);
		CHECK(a.columns == 2);
		CHECK(a.rows == 1);
		CHECK(a.floorsPerTile() == 1);
		REQUIRE(a.windows.size() == 2);
		for (const auto& w : a.windows) {
			// About 0.44 of a column (0.22 of the tile) wide, 0.55 high, in the middle of its half.
			CHECK(w.u1 - w.u0 == doctest::Approx(0.25).epsilon(0.25));
			CHECK(w.v1 - w.v0 == doctest::Approx(0.61).epsilon(0.2));
			CHECK((w.v0 + w.v1) / 2 == doctest::Approx(0.5).epsilon(0.1));
		}
		// One tile is one 128-unit floor: 512 texels at 0.25.
		CHECK(a.scaleFor(128) == doctest::Approx(0.25));
		CHECK(a.describe(128) == "facade with windows · 2 windows per tile · 1 floor per tile · scale 0.25");
	}
	SUBCASE("three across, two floors: one tile spans two floors (scale 0.5 at 128-unit floors)") {
		const auto a = analyzeTexture(plasterWithWindows(3, 2, 0.5, 0.55), "");
		CHECK(a.role == TextureRole::FacadeWindows);
		CHECK(a.columns == 3);
		CHECK(a.rows == 2);
		CHECK(a.windows.size() == 6);
		CHECK(a.floorsPerTile() == 2);
		CHECK(a.scaleFor(128) == doctest::Approx(0.5));
	}
	SUBCASE("a window cut by the tile's edge comes out whole") {
		// The same facade moved by a third of a column: the first window crosses the left edge.
		const auto shifted = paint(512, 512, [base = plasterWithWindows(2, 1)](double u, double v, int x, int y) {
			const int sx = (x + 85) % 512;
			const std::size_t p = (static_cast<std::size_t>(y) * 512 + sx) * 4;
			const auto* px = reinterpret_cast<const unsigned char*>(base.pixels.data());
			return Rgb{px[p] / 255.0, px[p + 1] / 255.0, px[p + 2] / 255.0};
		});
		const auto a = analyzeTexture(shifted, "");
		CHECK(a.role == TextureRole::FacadeWindows);
		CHECK(a.columns == 2);
		REQUIRE(a.windows.size() == 2);
		for (const auto& w : a.windows) CHECK(w.u1 - w.u0 == doctest::Approx(0.25).epsilon(0.25));
	}
	SUBCASE("the windows travel with a building's settings") {
		const auto a = analyzeTexture(plasterWithWindows(2, 1), "");
		const auto back = decodeFacadeWindows(encodeFacadeWindows(a.windows));
		REQUIRE(back.size() == a.windows.size());
		for (std::size_t i = 0; i < back.size(); ++i) CHECK(back[i].u0 == doctest::Approx(a.windows[i].u0).epsilon(0.001));
	}
}

TEST_CASE("facade textures: no windows where there are none - brick, stained plaster, strips, roofs") {
	SUBCASE("plain brick in a running bond is a wall") {
		const auto a = analyzeTexture(plainBrick(), "brick/brickwall031d");
		CHECK(a.role == TextureRole::Wall);
		CHECK_FALSE(a.hasWindows);
	}
	SUBCASE("plaster with round stains is a wall") {
		const auto a = analyzeTexture(stainedPlaster(), "plaster/plasterwall016a");
		CHECK(a.role == TextureRole::Wall);
		CHECK_FALSE(a.hasWindows);
	}
	SUBCASE("a wide cornice strip with dentils is trim") {
		const auto strip = paint(512, 128, [](double u, double v, int x, int y) {
			if (v < 0.3) return Rgb{0.7, 0.69, 0.66};
			if (v < 0.7) return std::fmod(u * 32, 1.0) < 0.55 ? Rgb{0.74, 0.73, 0.7} : Rgb{0.35, 0.35, 0.34};
			return Rgb{0.6, 0.6, 0.58};
		});
		const auto a = analyzeTexture(strip, "");
		CHECK(a.role == TextureRole::Trim);
		CHECK(a.describe() == "trim strip · once over a band's height");
	}
	SUBCASE("a square texture banded across and named a cornice is trim") {
		const auto band = paint(256, 256, [](double, double v, int x, int y) {
			const double n = (noise(x, y, 5) - 0.5) * 0.04;
			const double l = v < 0.3 ? 0.72 : (v < 0.4 ? 0.3 : (v < 0.8 ? 0.62 : 0.5));
			return Rgb{l + n, l + n, l + n};
		});
		CHECK(analyzeTexture(band, "concrete/concrete_cornice01").role == TextureRole::Trim);
	}
	SUBCASE("a square cornice with deep dentils, named so, is trim (the dentils make the columns vary)") {
		const auto dentils = paint(256, 256, [](double u, double v, int x, int y) {
			const double base = 0.66 + (noise(x, y, 9) - 0.5) * 0.06;
			if (v < 0.12) return Rgb{base + 0.08, base + 0.08, base + 0.06};
			if (v < 0.3) return Rgb{base - 0.04, base - 0.04, base - 0.05};
			if (v < 0.36) return Rgb{base - 0.25, base - 0.25, base - 0.26};
			if (v < 0.78) return std::fmod(u * 16, 1.0) < 0.55 ? Rgb{base + 0.04, base + 0.04, base + 0.02} : Rgb{base - 0.3, base - 0.3, base - 0.31};
			if (v < 0.86) return Rgb{base - 0.18, base - 0.18, base - 0.19};
			return Rgb{base, base, base - 0.02};
		});
		CHECK(analyzeTexture(dentils, "demo/trim_cornice001").role == TextureRole::Trim);
		CHECK(analyzeTexture(dentils, "").role != TextureRole::FacadeWindows);   // no name: dentils are no windows
	}
	SUBCASE("dark even grey is a roof (tar, gravel)") {
		const auto tar = paint(256, 256, [](double, double, int x, int y) {
			const double n = (noise(x, y, 7) - 0.5) * 0.12 + (noise(x / 8, y / 8, 8) - 0.5) * 0.08;
			return Rgb{0.22 + n, 0.22 + n, 0.23 + n};
		});
		CHECK(analyzeTexture(tar, "").role == TextureRole::Roof);
		CHECK(analyzeTexture(tar, "nature/gravelfloor002a").role == TextureRole::Roof);
	}
}

TEST_CASE("facade textures: shop fronts, and what is unsuitable for a near wall") {
	SUBCASE("one row of big windows is a shop front") {
		const auto a = analyzeTexture(plasterWithWindows(1, 1, 0.8, 0.6), "");
		CHECK(a.role == TextureRole::ShopFront);
		CHECK(a.hasWindows);
	}
	SUBCASE("a dense grid of tiny windows is a far (skybox) facade") {
		const auto a = analyzeTexture(plasterWithWindows(6, 6, 0.4, 0.5, 512), "building_template/building_template013a");
		CHECK(a.role == TextureRole::Unsuitable);
		CHECK_FALSE(a.hasWindows);
		CHECK(a.reason.find("far") != std::string::npos);
	}
	SUBCASE("a garish orange is unsuitable") {
		const auto orange = paint(256, 256, [](double, double, int x, int y) {
			const double n = (noise(x / 2, y / 2, 2) - 0.5) * 0.1;
			return Rgb{0.95 + n, 0.5 + n, 0.05};
		});
		CHECK(analyzeTexture(orange, "").role == TextureRole::Unsuitable);
	}
	SUBCASE("see-through (a decal) is unsuitable") {
		const auto decal = paint(256, 256, [](double u, double v, int, int) { return Rgb{0.3, 0.3, 0.3, std::hypot(u - 0.5, v - 0.5) < 0.3 ? 1.0 : 0.0}; });
		CHECK(analyzeTexture(decal, "").role == TextureRole::Unsuitable);
	}
	SUBCASE("ambiguous: windows win over a roof-like name; a brick named roof is still a roof only when grey") {
		CHECK(analyzeTexture(plasterWithWindows(2, 1), "roof_apartment01").role == TextureRole::FacadeWindows);
		CHECK(analyzeTexture(plainBrick(), "roof_bricks").role == TextureRole::Roof);   // red brick is not garish: the name decides the tie
		const auto red = paint(256, 256, [](double, double, int x, int y) { return Rgb{0.9, 0.15 + (noise(x, y, 1) - 0.5) * 0.05, 0.1}; });
		CHECK(analyzeTexture(red, "roofing_red").role == TextureRole::Unsuitable);
	}
	SUBCASE("roles have names for the dialog and keys for the settings") {
		for (const auto role : {TextureRole::FacadeWindows, TextureRole::Wall, TextureRole::Trim, TextureRole::Roof, TextureRole::ShopFront, TextureRole::Unsuitable}) {
			CHECK(textureRoleFromKey(textureRoleKey(role)) == role);
			CHECK(std::string(textureRoleName(role)).size() > 3);
		}
	}
}
