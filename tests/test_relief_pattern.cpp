// test_relief_pattern.cpp - Pattern recognition for relief: bricks, tiles and boards found in
// generated textures (every brick its own colour, dark and light, noise, stains), the height map
// with the units up and the joints down, the per-unit zones, and which materials get relief.

#include <doctest/doctest.h>

#include <sme/heightmap.h>
#include <sme/material.h>
#include <sme/relief_pattern.h>

#include <cmath>
#include <cstdint>

using namespace sme;

namespace {

/// A small deterministic random generator (tests must not depend on the platform's).
struct Random {
	std::uint32_t state;
	double next() {
		state = state * 1664525u + 1013904223u;
		return double(state >> 8) / double(1u << 24);
	}
};

struct Wall {
	unsigned size = 256;
	int courses = 8;          ///< Rows of units (across the texture)
	int perCourse = 4;        ///< Units per row
	bool runningBond = true;  ///< Every other row moved by half a unit
	bool lightMortar = true;
	int joint = 3;            ///< Joint width in texels
	double noise = 0.06;      ///< Grain on the units
	bool upright = false;     ///< Courses standing (boards upright): x and y swapped
	bool headJoints = true;   ///< Units in the rows (else boards: rows only)
	std::uint32_t seed = 7;
	double stains = 0;        ///< Light falling across and a dark stain over several bricks (0..1)
};

/// Is this texel of the wall a joint? Also the unit's index.
bool wallJoint(const Wall& wall, unsigned x, unsigned y, int* unit = nullptr) {
	if (wall.upright) std::swap(x, y);
	const double courseHeight = double(wall.size) / wall.courses, unitWidth = double(wall.size) / wall.perCourse;
	const double v = std::fmod(y + 0.5, courseHeight);
	const int course = int(y / courseHeight);
	const double offset = wall.runningBond && course % 2 ? unitWidth * 0.5 : 0.0;
	const double u = std::fmod(x + 0.5 + offset, unitWidth);
	if (unit) *unit = wall.headJoints ? course * wall.perCourse + int(std::fmod(x + offset, double(wall.size)) / unitWidth) : course;   // a board spans the row
	const bool bed = v < wall.joint * 0.5 || v > courseHeight - wall.joint * 0.5;
	const bool head = wall.headJoints && (u < wall.joint * 0.5 || u > unitWidth - wall.joint * 0.5);
	return bed || head;
}

/// Bricks with their own colours - some darker than the mortar, some lighter - and grain.
ImageRgba makeWall(const Wall& wall) {
	ImageRgba image;
	image.width = image.height = static_cast<std::uint16_t>(wall.size);
	image.pixels.resize(std::size_t(wall.size) * wall.size * 4);
	Random random{wall.seed};
	std::vector<double> unitTone(std::size_t(wall.courses * wall.perCourse + wall.perCourse));
	for (auto& tone : unitTone) tone = 0.25 + 0.45 * random.next();   // dark and light bricks
	const double mortar = wall.lightMortar ? 0.82 : 0.08;
	for (unsigned y = 0; y < wall.size; ++y) {
		for (unsigned x = 0; x < wall.size; ++x) {
			int unit = 0;
			const bool joint = wallJoint(wall, x, y, &unit);
			double tone = joint ? mortar : unitTone[std::size_t(unit) % unitTone.size()];
			tone += (random.next() - 0.5) * 2 * wall.noise;
			if (wall.stains > 0) {
				const double dx = double(x) / wall.size - 0.3, dy = double(y) / wall.size - 0.6;
				tone *= 1.0 + wall.stains * (0.4 * (double(x) / wall.size - 0.5) - 0.5 * std::exp(-(dx * dx + dy * dy) / 0.02));
			}
			const auto byte = [](double v) { return std::byte(static_cast<unsigned char>(std::clamp(v, 0.0, 1.0) * 255)); };
			auto* p = &image.pixels[(std::size_t(y) * wall.size + x) * 4];
			p[0] = byte(tone * 1.15);   // reddish
			p[1] = byte(tone * 0.8);
			p[2] = byte(tone * 0.7);
			p[3] = std::byte(255);
		}
	}
	return image;
}

/// Mean height over the unit faces (away from the joints) and over the joints.
std::pair<double, double> faceAndJoint(const Wall& wall, const HeightMap& map) {
	double face = 0, joint = 0;
	int faces = 0, joints = 0;
	for (unsigned y = 0; y < map.height; ++y) {
		for (unsigned x = 0; x < map.width; ++x) {
			const unsigned sx = x * wall.size / map.width, sy = y * wall.size / map.height;
			const bool isJoint = wallJoint(wall, sx, sy);
			// Inner face: no joint within 4 texels.
			bool inner = !isJoint;
			for (int d = -4; d <= 4 && inner; d += 2) {
				inner = !wallJoint(wall, (sx + wall.size + d) % wall.size, sy) && !wallJoint(wall, sx, (sy + wall.size + d) % wall.size);
			}
			if (inner) {
				face += map.at(x, y);
				++faces;
			} else if (isJoint && wallJoint(wall, (sx + 1) % wall.size, sy) && wallJoint(wall, sx, (sy + 1) % wall.size)) {
				joint += map.at(x, y);
				++joints;
			}
		}
	}
	return {faces ? face / faces : 0.0, joints ? joint / joints : 1.0};
}

/// Every unit's own mean height: the lowest one (a dark brick must not sink).
double lowestFace(const Wall& wall, const HeightMap& map) {
	std::vector<double> sum(std::size_t(wall.courses * wall.perCourse + wall.perCourse), 0.0), count(sum.size(), 0.0);
	for (unsigned y = 0; y < map.height; ++y) {
		for (unsigned x = 0; x < map.width; ++x) {
			int unit = 0;
			if (wallJoint(wall, x, y, &unit)) continue;
			bool inner = true;
			for (int d = -4; d <= 4 && inner; d += 2) {
				inner = !wallJoint(wall, (x + wall.size + d) % wall.size, y) && !wallJoint(wall, x, (y + wall.size + d) % wall.size);
			}
			if (!inner) continue;
			sum[std::size_t(unit) % sum.size()] += map.at(x, y);
			count[std::size_t(unit) % sum.size()] += 1;
		}
	}
	double lowest = 1.0;
	for (std::size_t i = 0; i < sum.size(); ++i) {
		if (count[i] > 0) lowest = std::min(lowest, sum[i] / count[i]);
	}
	return lowest;
}

/// The largest height difference between neighbouring texels (wrapping).
double steepestStep(const HeightMap& map) {
	double steepest = 0;
	for (unsigned y = 0; y < map.height; ++y) {
		for (unsigned x = 0; x < map.width; ++x) {
			const double here = map.at(x, y);
			steepest = std::max({steepest, std::abs(here - map.at((x + 1) % map.width, y)), std::abs(here - map.at(x, (y + 1) % map.height))});
		}
	}
	return steepest;
}

ImageRgba noiseTexture(unsigned size, std::uint32_t seed, double amount) {
	ImageRgba image;
	image.width = image.height = static_cast<std::uint16_t>(size);
	image.pixels.resize(std::size_t(size) * size * 4);
	Random random{seed};
	// Blotches: noise smoothed over a few texels (rough plaster, stone).
	std::vector<double> v(std::size_t(size) * size);
	for (auto& x : v) x = random.next();
	for (int pass = 0; pass < 3; ++pass) {
		std::vector<double> s(v.size());
		for (unsigned y = 0; y < size; ++y) {
			for (unsigned x = 0; x < size; ++x) {
				double sum = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) sum += v[std::size_t((y + size + dy) % size) * size + (x + size + dx) % size];
				}
				s[std::size_t(y) * size + x] = sum / 9;
			}
		}
		v.swap(s);
	}
	for (std::size_t i = 0; i < v.size(); ++i) {
		const auto b = std::byte(static_cast<unsigned char>(std::clamp(0.5 + (v[i] - 0.5) * amount * 4, 0.0, 1.0) * 255));
		image.pixels[i * 4] = image.pixels[i * 4 + 1] = image.pixels[i * 4 + 2] = b;
		image.pixels[i * 4 + 3] = std::byte(255);
	}
	return image;
}

Material material(const char* text) {
	auto m = parseMaterial(text, "x");
	REQUIRE(m);
	return *m;
}

} // namespace

TEST_CASE("relief pattern: bricks in a running bond, every brick up and every joint down") {
	struct Case {
		Wall wall;
		ReliefLayout::Kind kind;
	};
	const Case cases[] = {
		{Wall{}, ReliefLayout::Kind::RunningBond},
		{Wall{.lightMortar = false}, ReliefLayout::Kind::RunningBond},                                         // dark mortar
		{Wall{.size = 512, .courses = 12, .perCourse = 3, .joint = 5, .seed = 3}, ReliefLayout::Kind::RunningBond},   // 42.7 texels a course
		{Wall{.size = 256, .courses = 16, .perCourse = 6, .joint = 2, .noise = 0.12, .seed = 11}, ReliefLayout::Kind::RunningBond},
		{Wall{.size = 512, .courses = 14, .perCourse = 4, .joint = 6, .noise = 0.1, .seed = 13, .stains = 1.0}, ReliefLayout::Kind::RunningBond},   // light across, a stain
		{Wall{.courses = 4, .perCourse = 4, .runningBond = false, .seed = 5}, ReliefLayout::Kind::Stacked},             // tiles
		{Wall{.courses = 8, .perCourse = 8, .runningBond = false, .lightMortar = false, .seed = 9}, ReliefLayout::Kind::Stacked},
	};
	for (const auto& c : cases) {
		CAPTURE(c.wall.courses);
		CAPTURE(c.wall.perCourse);
		CAPTURE(c.wall.lightMortar);
		const ImageRgba image = makeWall(c.wall);
		const ReliefLayout layout = detectReliefLayout(image);
		REQUIRE(layout.found());
		CHECK(layout.kind == c.kind);
		CHECK_FALSE(layout.transposed);
		CHECK(layout.courses() == c.wall.courses);
		CHECK(layout.courseHeight == doctest::Approx(double(c.wall.size) / c.wall.courses).epsilon(0.02));
		CHECK(layout.unitWidth == doctest::Approx(double(c.wall.size) / c.wall.perCourse).epsilon(0.02));
		CHECK(layout.lightJoints == c.wall.lightMortar);
		CHECK(layout.coverage > 0.95);
		CHECK(layout.confidence > 0.5);
		const auto map = heightFromLayout(layout, image);
		REQUIRE(map);
		const auto [face, joint] = faceAndJoint(c.wall, *map);
		CHECK(face > 0.8);
		// Shallow joints (a black strip along every tile in the game, bricks smeared): bricks' mortar
		// at 0.3, tiles' grout at 0.55 of the height.
		if (c.kind == ReliefLayout::Kind::Stacked) {
			CHECK(joint == doctest::Approx(0.55).epsilon(0.15));
		} else {
			CHECK(joint == doctest::Approx(0.3).epsilon(0.25));
		}
		CHECK(steepestStep(*map) < 0.35);   // eased edges: no cliff from one texel to the next
		// The darkest brick stands as high as the others (brightness alone sank it).
		CHECK(lowestFace(c.wall, *map) > 0.75);
		// The automatic way takes the layout too.
		ReliefLayout found;
		const auto automatic = heightFromTexture(image, &found);
		REQUIRE(automatic);
		CHECK(found.found());
		CHECK(automatic->values == map->values);
	}
	// What brightness alone did: dark bricks sunk under light mortar (the problem this solves).
	const Wall dark{.seed = 7};
	const auto byBrightness = heightFromColor(makeWall(dark));
	REQUIRE(byBrightness);
	CHECK(lowestFace(dark, *byBrightness) < 0.6);
}

TEST_CASE("relief pattern: boards, upright courses and textures without a layout") {
	// Boards across: rows without upright joints.
	const Wall boards{.courses = 8, .runningBond = false, .lightMortar = false, .headJoints = false, .seed = 4};
	ReliefLayout layout = detectReliefLayout(makeWall(boards));
	REQUIRE(layout.found());
	CHECK(layout.kind == ReliefLayout::Kind::Courses);
	CHECK(layout.unitWidth == 0);
	CHECK(layout.courses() == 8);
	// The same standing up.
	const Wall upright{.courses = 8, .runningBond = false, .lightMortar = false, .upright = true, .headJoints = false, .seed = 4};
	const ImageRgba standing = makeWall(upright);
	layout = detectReliefLayout(standing);
	REQUIRE(layout.found());
	CHECK(layout.transposed);
	CHECK(layout.courses() == 8);
	const auto map = heightFromLayout(layout, standing);
	REQUIRE(map);
	const auto [face, joint] = faceAndJoint(upright, *map);
	CHECK(face > 0.8);
	CHECK(joint < 0.4);
	// Rough plaster or stone: no layout, the brightness as before.
	for (std::uint32_t seed : {1u, 2u, 3u}) {
		const ImageRgba rough = noiseTexture(256, seed, 0.6);
		CHECK_FALSE(detectReliefLayout(rough).found());
		ReliefLayout none;
		CHECK(heightFromTexture(rough, &none));
		CHECK_FALSE(none.found());
	}
	// Too small or empty: nothing, never a crash.
	CHECK_FALSE(detectReliefLayout(ImageRgba{}).found());
	CHECK_FALSE(detectReliefLayout(noiseTexture(8, 1, 0.5)).found());
}

TEST_CASE("relief pattern: a façade's window stays flat, a whole brick at a time") {
	Wall wall{.size = 256, .courses = 16, .perCourse = 4, .seed = 21};
	ImageRgba image = makeWall(wall);
	// A dark window pane over the middle: no bricks there.
	for (unsigned y = 96; y < 160; ++y) {
		for (unsigned x = 64; x < 192; ++x) {
			auto* p = &image.pixels[(std::size_t(y) * wall.size + x) * 4];
			p[0] = p[1] = p[2] = std::byte(8);
		}
	}
	const ReliefLayout layout = detectReliefLayout(image);
	REQUIRE(layout.found());
	CHECK(layout.kind == ReliefLayout::Kind::RunningBond);
	CHECK(layout.unitAt(128, 128) == ReliefLayout::OUTSIDE);
	CHECK(layout.coverage < 0.95);
	CHECK(layout.coverage > 0.6);
	const auto map = heightFromLayout(layout, image);
	REQUIRE(map);
	// Flat in the window (no grooves), the bricks around it still up.
	double minimum = 1, maximum = 0;
	for (unsigned y = 104; y < 152; ++y) {
		for (unsigned x = 72; x < 184; ++x) {
			minimum = std::min<double>(minimum, map->at(x, y));
			maximum = std::max<double>(maximum, map->at(x, y));
		}
	}
	CHECK(maximum - minimum < 0.05);
	CHECK(layout.describe().find("running bond") != std::string::npos);

	// One click: the whole brick at (20, 4) flat, nothing else.
	const int unit = layout.unitAt(20, 4);
	REQUIRE(unit >= 0);
	std::vector<std::uint8_t> zones(std::size_t(map->width) * map->height, 0);
	CHECK(setUnitZone(layout, zones, map->width, map->height, 20, 4, HeightZone::Flat));
	int marked = 0, wrong = 0;
	for (unsigned y = 0; y < map->height; ++y) {
		for (unsigned x = 0; x < map->width; ++x) {
			if (zones[std::size_t(y) * map->width + x] == std::uint8_t(HeightZone::Flat)) {
				++marked;
				wrong += layout.unitAt(int(x), int(y)) != unit ? 1 : 0;
			}
		}
	}
	CHECK(marked > 200);   // a brick of 64 x 16 less its joints
	CHECK(wrong == 0);
	// A joint is no brick; a zone map of another size works too.
	int jx = 0, jy = 0;
	for (; jy < 256 && layout.unitAt(jx, jy) != ReliefLayout::JOINT; ++jy) {}
	CHECK_FALSE(setUnitZone(layout, zones, map->width, map->height, jx, jy, HeightZone::Flat));
	std::vector<std::uint8_t> half(128 * 128, 0);
	CHECK(setUnitZone(layout, half, 128, 128, 20, 4, HeightZone::Inverted));
	CHECK(std::count(half.begin(), half.end(), std::uint8_t(HeightZone::Inverted)) > 50);
}

TEST_CASE("relief: which materials get it - by name and by look") {
	// Windows, glass, signs, grates, doors, metal sheets, plaster: no.
	for (const char* name : {"building_template/buildingset040a_window", "glass/glasswindow001a", "metal/citysign01", "decals/poster_01",
	                         "metal/metalgrate014a", "props/chainlinkfence01", "metal/metaldoor001a", "metal/metalwall021a",
	                         "plaster/plasterwall030a", "concrete/concretewall_painted", "lights/white001", "nature/cloth_curtain"}) {
		CAPTURE(name);
		CHECK_FALSE(reliefFitByName(name).suits);
		CHECK_FALSE(reliefFitByName(name).reason.empty());
	}
	// Bricks, stone, tiles, blocks, rock, concrete: yes - also bricks around something.
	for (const char* name : {"brick/brickwall001a", "stone/stonewall024a", "tile/tilefloor013a", "concrete/concreteblock001a", "nature/rockwall004a",
	                         "concrete/concretewall011a", "brick/brickwall_painted01", "building_template/indoorwall01", "design/tilewall"}) {
		CAPTURE(name);
		CHECK(reliefFitByName(name).suits);
	}
	// A window named with stone words is still a window.
	CHECK_FALSE(reliefFitByName("stone/stonewindow01").suits);
	// The base texture's name counts too.
	CHECK_FALSE(reliefFitByName("custom/wall01", "glass/window_dirty").suits);

	// By look: a big flat pane, a smooth surface, see-through texels; bricks pass.
	CHECK(reliefFitByLook(makeWall(Wall{})).suits);
	CHECK(reliefFitByLook(noiseTexture(128, 4, 0.6)).suits);
	ImageRgba pane = makeWall(Wall{.size = 128, .courses = 8, .perCourse = 4});
	for (unsigned y = 16; y < 112; ++y) {
		for (unsigned x = 16; x < 112; ++x) {
			auto* p = &pane.pixels[(std::size_t(y) * 128 + x) * 4];
			p[0] = std::byte(30), p[1] = std::byte(50), p[2] = std::byte(70);
		}
	}
	CHECK_FALSE(reliefFitByLook(pane).suits);
	ImageRgba smooth = noiseTexture(128, 4, 0.0);
	CHECK(reliefFitByLook(smooth).reason == "smooth surface");
	ImageRgba clear = makeWall(Wall{.size = 64, .courses = 4, .perCourse = 2});
	for (std::size_t i = 0; i < clear.pixels.size(); i += 4) clear.pixels[i + 3] = std::byte(0);
	CHECK(reliefFitByLook(clear).reason == "see-through");

	// For a material: the name first, then see-through flags, then the look.
	CHECK_FALSE(reliefFit("brick/brickwall001a", material("LightmappedGeneric { $basetexture brick/a $translucent 1 }")).suits);
	CHECK(reliefFit("brick/brickwall001a", material("LightmappedGeneric { $basetexture brick/a }")).suits);
	CHECK_FALSE(reliefFit("brick/brickwall001a", material("LightmappedGeneric { $basetexture brick/a }"), &pane).suits);

	// Mapsmith's relief rules (viewport, Game enhancements) leave the windows out too.
	CHECK_FALSE(fidelityReliefSource("building_template/buildingset040a_window",
	                                 material("LightmappedGeneric { $basetexture building_template/buildingset040a_window $bumpmap b }")));
	CHECK_FALSE(fidelityReliefSource("metal/metalwall021a", material("LightmappedGeneric { $basetexture metal/metalwall021a $bumpmap b }")));
	CHECK(fidelityReliefSource("concrete/wall01", material("LightmappedGeneric { $basetexture concrete/wall01 $bumpmap b }")));
}

namespace {

/// A wall texture with a window: brick pillars left and right, in between dark glass divided into
/// panes by light bars (Julius' screenshot, 29.10.2026).
ImageRgba windowWall(unsigned size, unsigned barEvery, double glass) {
	ImageRgba image = makeWall(Wall{.size = size, .courses = 16, .perCourse = 4});
	Random random{3};
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = size / 5; x < size - size / 5; ++x) {
			const bool bar = x % barEvery < 3 || y % barEvery < 3;
			const double tone = bar ? 0.65 : glass + (random.next() - 0.5) * 0.02;
			auto* p = &image.pixels[(std::size_t(y) * size + x) * 4];
			p[0] = p[1] = p[2] = std::byte(static_cast<unsigned char>(std::clamp(tone, 0.0, 1.0) * 255));
		}
	}
	return image;
}

/// A grey image (the reflection mask): `inside` within the rectangle, `outside` elsewhere, with bars.
ImageRgba maskImage(unsigned size, double inside, double outside, unsigned barEvery) {
	ImageRgba image;
	image.width = image.height = static_cast<std::uint16_t>(size);
	image.pixels.resize(std::size_t(size) * size * 4);
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			const bool pane = x >= size / 5 && x < size - size / 5;
			const bool bar = barEvery && (x % barEvery < 2 || y % barEvery < 2);
			const double v = pane && !bar ? inside : outside;
			auto* p = &image.pixels[(std::size_t(y) * size + x) * 4];
			p[0] = p[1] = p[2] = std::byte(static_cast<unsigned char>(v * 255));
			p[3] = std::byte(255);
		}
	}
	return image;
}

} // namespace

TEST_CASE("relief: windows stay flat - panes between bars, reflecting glass, every kind of material by name") {
	// The window of Julius' screenshot: brick pillars, a dark pane cut into cells by light bars.
	for (const unsigned bars : {24u, 32u, 64u, 128u}) {
		CAPTURE(bars);
		const ImageRgba window = windowWall(512, bars, 0.06);
		const ReliefFit fit = reliefFitByLook(window);
		CHECK_FALSE(fit.suits);
		CHECK(fit.reason.find("window") != std::string::npos);
	}
	// Walls stay walls: small bricks with dark joints, dark rough stone, light bricks, stains.
	CHECK(reliefFitByLook(makeWall(Wall{.size = 512, .courses = 30, .perCourse = 12, .lightMortar = false})).suits);
	CHECK(reliefFitByLook(makeWall(Wall{.size = 512, .courses = 16, .perCourse = 4, .lightMortar = false, .joint = 6})).suits);
	CHECK(reliefFitByLook(makeWall(Wall{.size = 256, .courses = 8, .perCourse = 4, .stains = 0.8})).suits);
	ImageRgba darkStone = noiseTexture(256, 9, 0.6);
	for (std::size_t i = 0; i < darkStone.pixels.size(); i += 4) {
		for (int c = 0; c < 3; ++c) darkStone.pixels[i + c] = std::byte(static_cast<unsigned char>(std::to_integer<int>(darkStone.pixels[i + c]) / 4));
	}
	CHECK(reliefFitByLook(darkStone).suits);

	// The reflection mask: bright over the pane (with or without bars) is glass; spots, a dark mask,
	// or an even sheen over the whole wall are not a window.
	CHECK_FALSE(reliefFitByMask(maskImage(256, 0.9, 0.05, 0)).suits);
	CHECK_FALSE(reliefFitByMask(maskImage(256, 0.9, 0.05, 32)).suits);
	CHECK(reliefFitByMask(maskImage(256, 0.2, 0.05, 0)).suits);
	CHECK(reliefFitByMask(noiseTexture(256, 5, 0.3)).suits);

	// By name, the kinds of material a Half-Life 2 map has: depth only for stone, bricks, tiles,
	// blocks, concrete, cobbles, rock - never for windows (also when bricks are in the name), glass,
	// doors, signs, grates, fences, metal, plaster, wood (deploy's -ParallaxWood), cloth, lights.
	struct Case {
		const char* material;
		bool relief;
	};
	const Case cases[] = {
		{"brick/brickwall003a", true}, {"brick/brickfloor001a", true}, {"stone/stonewall007a", true}, {"stone/stonefloor009a", true},
		{"tile/tilewall006a", true}, {"tile/tilefloor007b", true}, {"concrete/concretewall064a", true}, {"concrete/concretefloor028a", true},
		{"concrete/concreteblock001a", true}, {"nature/rockwall011a", true}, {"nature/cobblestone001a", true}, {"stone/cobble01", true},
		{"building_template/buildingset031a", true},
		{"brick/brickwindow01", false}, {"brick/brickwall_window", false}, {"building_template/buildingset040a_window", false},
		{"props/windows_glass01", false}, {"glass/glasswindow001a", false}, {"glass/combineglass001a", false}, {"plaster/plasterwindow01", false},
		{"stone/stainedglass01", false}, {"building_template/storefront01", false}, {"metal/skylight01", false},
		{"metal/metaldoor001a", false}, {"wood/wooddoor002a", false}, {"props/gate01", false}, {"metal/citysign01", false},
		{"decals/poster_01", false}, {"metal/metalgrate014a", false}, {"metal/metalfence001a", false}, {"props/chainlinkfence01", false},
		{"metal/metalwall021a", false}, {"metal/metalsheet01", false}, {"plaster/plasterwall030a", false}, {"wood/woodwall003a", false},
		{"wood/woodfloor007a", false}, {"nature/cloth_curtain", false}, {"lights/white001", false}, {"metal/metalvent001a", false},
		{"props/wallpaper01", false}, {"carpet/carpet01", false}, {"concrete/concretewall_painted", false},
	};
	for (const auto& c : cases) {
		CAPTURE(c.material);
		const Material bumped = material((std::string("LightmappedGeneric { $basetexture ") + c.material + " $bumpmap b }").c_str());
		CHECK(fidelityReliefSource(c.material, bumped).has_value() == c.relief);
	}
	// Folders that never get relief, whatever the name.
	for (const char* name : {"water/water01", "nature/water/canal01", "tools/toolsnodraw", "decals/brickwall_crack", "overlays/stonewall01",
	                         "models/props/brickwall", "skybox/sky_brick", "effects/brickflash", "glass/brickglass"}) {
		CAPTURE(name);
		CHECK_FALSE(fidelityReliefSource(name, material((std::string("LightmappedGeneric { $basetexture ") + name + " $bumpmap b }").c_str())));
	}
	// See-through, decals, three-way mapping: drawn otherwise than the parallax shader can.
	for (const char* flags : {"$translucent 1", "$alphatest 1", "$additive 1", "$decal 1", "$seamless_scale 0.5"}) {
		CAPTURE(flags);
		CHECK_FALSE(fidelityReliefSource("brick/brickwall003a",
		                                 material((std::string("LightmappedGeneric { $basetexture brick/brickwall003a $bumpmap b ") + flags + " }").c_str())));
	}
}

TEST_CASE("relief: pattern height maps - flat faces, a short bevel, shallow joints") {
	// Julius (29.10.2026): no cushions - most of a brick's face is flat on top; the joint lies only a
	// little lower (bricks 0.3, tiles 0.55); the bevel takes at most a third of the way in.
	const Wall wall{.size = 512, .courses = 16, .perCourse = 4};
	const ImageRgba texture = makeWall(wall);
	const ReliefLayout layout = detectReliefLayout(texture);
	REQUIRE(layout.found());
	const auto map = heightFromLayout(layout, texture);
	REQUIRE(map);
	const auto [face, joint] = faceAndJoint(wall, *map);
	CHECK(face > 0.9);
	CHECK(joint == doctest::Approx(0.3).epsilon(0.15));
	// Flat: along the middle of a course, away from head joints, the height hardly changes.
	std::size_t flatTexels = 0, faceTexels = 0;
	for (unsigned y = 0; y < map->height; ++y) {
		for (unsigned x = 0; x + 1 < map->width; ++x) {
			if (wallJoint(wall, x, y)) continue;
			++faceTexels;
			flatTexels += map->at(x, y) > 0.85 ? 1 : 0;
		}
	}
	CHECK(double(flatTexels) / double(faceTexels) > 0.6);
}

TEST_CASE("relief: the walk's reach as in the game - the preview and the 3D view use the same units") {
	// A 512 texture at Hammer's 0.25 scale covers 128 units: 1/128 texture units per unit.
	constexpr double perUnit = 1.0 / 128;
	// Head-on: no shift.
	CHECK(parallaxReach(0.01, 0, 0, 1, perUnit).u == doctest::Approx(0));
	// 45 degrees: the depth itself.
	const double s = std::sqrt(0.5);
	CHECK(parallaxReach(0.01, s, 0, s, perUnit).u == doctest::Approx(0.01));
	// Flat (about 20 degrees): at most 2.5 x the depth (the old preview went to 5 x, unlimited).
	const double z = std::sin(20 * 3.14159265 / 180), x = std::cos(20 * 3.14159265 / 180);
	const auto flat = parallaxReach(0.006, x, 0, z, perUnit);
	CHECK(flat.u <= 2.5 * 0.006 + 1e-9);
	// Deep relief: never more than 4 world units (0.031 of this texture).
	CHECK(parallaxReach(0.03, x, 0, z, perUnit).u <= 4 * perUnit + 1e-9);
	// Very flat: the relief fades out.
	CHECK(parallaxReach(0.01, 0.997, 0, 0.07, perUnit).strength == doctest::Approx(0));
	CHECK(parallaxReach(0.01, 0.99, 0, 0.14, perUnit).strength < 0.5);
}
