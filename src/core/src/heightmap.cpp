// heightmap.cpp - Height maps for relief shaders (see heightmap.h).

#include <sme/heightmap.h>

#include <sme/material.h>
#include <sme/relief_pattern.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include <cstring>

namespace sme {

namespace {

constexpr unsigned MAX_SIZE = 512;

bool powerOfTwo(unsigned n) {
	return n > 0 && (n & (n - 1)) == 0;
}

unsigned floorPowerOfTwo(unsigned n) {
	unsigned p = 1;
	while (p * 2 <= n) p *= 2;
	return p;
}

/// Channels 0..1 of an image, scaled down (box filter) to a power of two of at most MAX_SIZE.
struct Channels {
	unsigned w = 0, h = 0;
	std::vector<double> r, g, b;
};

Channels channels(const ImageRgba& image) {
	Channels out;
	const unsigned sw = image.width, sh = image.height;
	out.w = std::min(MAX_SIZE, floorPowerOfTwo(sw));
	out.h = std::min(MAX_SIZE, floorPowerOfTwo(sh));
	const std::size_t n = std::size_t(out.w) * out.h;
	out.r.assign(n, 0);
	out.g.assign(n, 0);
	out.b.assign(n, 0);
	for (unsigned y = 0; y < out.h; ++y) {
		const unsigned y0 = y * sh / out.h, y1 = std::max(y0 + 1, (y + 1) * sh / out.h);
		for (unsigned x = 0; x < out.w; ++x) {
			const unsigned x0 = x * sw / out.w, x1 = std::max(x0 + 1, (x + 1) * sw / out.w);
			double r = 0, g = 0, b = 0;
			for (unsigned sy = y0; sy < y1; ++sy) {
				for (unsigned sx = x0; sx < x1; ++sx) {
					const auto* p = &image.pixels[(std::size_t(sy) * sw + sx) * 4];
					r += std::to_integer<int>(p[0]);
					g += std::to_integer<int>(p[1]);
					b += std::to_integer<int>(p[2]);
				}
			}
			const double count = double(x1 - x0) * (y1 - y0) * 255.0;
			const std::size_t i = std::size_t(y) * out.w + x;
			out.r[i] = r / count;
			out.g[i] = g / count;
			out.b[i] = b / count;
		}
	}
	return out;
}

using Complex = std::complex<double>;

/// In-place radix-2 FFT over `n` values with `stride` (inverse scales by 1/n).
void fft(Complex* data, std::size_t n, std::size_t stride, bool inverse) {
	for (std::size_t i = 1, j = 0; i < n; ++i) {
		std::size_t bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(data[i * stride], data[j * stride]);
	}
	for (std::size_t length = 2; length <= n; length <<= 1) {
		const double angle = 2 * std::numbers::pi / double(length) * (inverse ? 1 : -1);
		const Complex step(std::cos(angle), std::sin(angle));
		for (std::size_t i = 0; i < n; i += length) {
			Complex w(1);
			for (std::size_t k = 0; k < length / 2; ++k) {
				const Complex a = data[(i + k) * stride], b = data[(i + k + length / 2) * stride] * w;
				data[(i + k) * stride] = a + b;
				data[(i + k + length / 2) * stride] = a - b;
				w *= step;
			}
		}
	}
	if (inverse) {
		for (std::size_t i = 0; i < n; ++i) data[i * stride] /= double(n);
	}
}

void fft2(std::vector<Complex>& data, unsigned w, unsigned h, bool inverse) {
	for (unsigned y = 0; y < h; ++y) fft(&data[std::size_t(y) * w], w, 1, inverse);
	for (unsigned x = 0; x < w; ++x) fft(&data[x], h, w, inverse);
}

/// Waves across the texture (along the longer side) for a frequency index.
double cycles(unsigned x, unsigned y, unsigned w, unsigned h) {
	const double fx = x <= w / 2 ? double(x) : double(x) - w;
	const double fy = (y <= h / 2 ? double(y) : double(y) - h) * double(w) / h;
	return std::sqrt(fx * fx + fy * fy);
}

/// Heights to 0..1 by the 1 % and 99 % heights; `plateau`: flatter tops and bottoms.
std::optional<HeightMap> normalize(const std::vector<double>& heights, unsigned w, unsigned h, bool plateau, std::string* error) {
	std::vector<double> sorted = heights;
	std::sort(sorted.begin(), sorted.end());
	const double bottom = sorted[std::size_t(double(sorted.size()) * 0.01)], top = sorted[std::size_t(double(sorted.size()) * 0.99)];
	if (top - bottom < 1e-9) {
		if (error) *error = "the texture is flat";
		return std::nullopt;
	}
	HeightMap map;
	map.width = w;
	map.height = h;
	map.values.resize(heights.size());
	for (std::size_t i = 0; i < heights.size(); ++i) {
		double t = std::clamp((heights[i] - bottom) / (top - bottom), 0.0, 1.0);
		if (plateau) {
			t = std::clamp((t - 0.15) / 0.7, 0.0, 1.0);
			t = t * t * (3 - 2 * t);
		}
		map.values[i] = float(t);
	}
	return map;
}

} // namespace

std::optional<HeightMap> heightFromNormalMap(const ImageRgba& normalMap, bool ssbump, std::string* error) {
	if (normalMap.width < 4 || normalMap.height < 4 || normalMap.pixels.size() < std::size_t(normalMap.width) * normalMap.height * 4) {
		if (error) *error = "the normal map is too small";
		return std::nullopt;
	}
	auto c = channels(normalMap);
	const unsigned w = c.w, h = c.h;
	const std::size_t n = std::size_t(w) * h;
	if (ssbump) {
		// The weights of the three lightmap directions: their sum is the normal.
		for (std::size_t i = 0; i < n; ++i) {
			double x = 0.81649658 * c.r[i] - 0.40824829 * (c.g[i] + c.b[i]);
			double y = 0.70710678 * (c.g[i] - c.b[i]);
			double z = 0.57735027 * (c.r[i] + c.g[i] + c.b[i]);
			double length = std::sqrt(x * x + y * y + z * z);
			if (length < 1e-6) {
				x = 0, y = 0, z = 1, length = 1;
			}
			c.r[i] = (x / length + 1) * 0.5;
			c.g[i] = (y / length + 1) * 0.5;
			c.b[i] = (z / length + 1) * 0.5;
		}
	}
	// Slopes (x right, y down in the texture).
	std::vector<double> p(n), q(n);
	for (std::size_t i = 0; i < n; ++i) {
		const double nx = c.r[i] * 2 - 1, ny = c.g[i] * 2 - 1, nz = std::max(c.b[i] * 2 - 1, 0.2);
		p[i] = -nx / nz;
		q[i] = -ny / nz;
	}
	// Green up or down: the one whose slopes fit together (least curl).
	double same = 0, flipped = 0;
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			const std::size_t i = std::size_t(y) * w + x, right = std::size_t(y) * w + (x + 1) % w, down = std::size_t((y + 1) % h) * w + x;
			const double dpdy = p[down] - p[i], dqdx = q[right] - q[i];
			same += (dpdy - dqdx) * (dpdy - dqdx);
			flipped += (dpdy + dqdx) * (dpdy + dqdx);
		}
	}
	if (flipped < same * 0.7) {
		for (auto& v : q) v = -v;
	}
	// Frankot-Chellappa: H = -i (u P + v Q) / (u^2 + v^2), slow waves taken out.
	std::vector<Complex> P(n), Q(n), H(n);
	for (std::size_t i = 0; i < n; ++i) {
		P[i] = p[i];
		Q[i] = q[i];
	}
	fft2(P, w, h, false);
	fft2(Q, w, h, false);
	for (unsigned y = 0; y < h; ++y) {
		const double v = 2 * std::numbers::pi * (y <= h / 2 ? double(y) : double(y) - h) / h;
		for (unsigned x = 0; x < w; ++x) {
			const double u = 2 * std::numbers::pi * (x <= w / 2 ? double(x) : double(x) - w) / w;
			const double d = u * u + v * v;
			const std::size_t i = std::size_t(y) * w + x;
			if (d < 1e-12) continue;
			const double cyc = cycles(x, y, w, h);
			const double keep = 1.0 - std::exp(-(cyc * cyc) / 9.0);
			const Complex a = u * P[i] + v * Q[i];
			H[i] = Complex(a.imag() / d * keep, -a.real() / d * keep);
		}
	}
	fft2(H, w, h, true);
	std::vector<double> heights(n);
	for (std::size_t i = 0; i < n; ++i) heights[i] = H[i].real();
	return normalize(heights, w, h, false, error);
}

std::optional<HeightMap> heightFromColor(const ImageRgba& baseTexture, std::string* error) {
	if (baseTexture.width < 4 || baseTexture.height < 4 || baseTexture.pixels.size() < std::size_t(baseTexture.width) * baseTexture.height * 4) {
		if (error) *error = "the texture is too small";
		return std::nullopt;
	}
	const auto c = channels(baseTexture);
	const unsigned w = c.w, h = c.h;
	const std::size_t n = std::size_t(w) * h;
	// Brightness, band-passed: without the slow changes (light and stains across the texture)
	// and without the finest grain; joints are dark, so dark stays low.
	std::vector<Complex> L(n);
	for (std::size_t i = 0; i < n; ++i) L[i] = 0.299 * c.r[i] + 0.587 * c.g[i] + 0.114 * c.b[i];
	fft2(L, w, h, false);
	const double fine = std::max(w, h) / 6.0;
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			const double cyc = cycles(x, y, w, h);
			const double keepSlow = 1.0 - std::exp(-(cyc * cyc) / 9.0);
			const double keepFine = std::exp(-(cyc * cyc) / (2 * fine * fine));
			L[std::size_t(y) * w + x] *= keepSlow * keepFine;
		}
	}
	fft2(L, w, h, true);
	std::vector<double> heights(n);
	for (std::size_t i = 0; i < n; ++i) heights[i] = L[i].real();
	return normalize(heights, w, h, true, error);
}

HeightMap heightFromImage(const ImageRgba& image) {
	HeightMap map;
	map.width = image.width;
	map.height = image.height;
	map.values.resize(std::size_t(image.width) * image.height);
	for (std::size_t i = 0; i < map.values.size() && i * 4 < image.pixels.size(); ++i) {
		map.values[i] = std::to_integer<int>(image.pixels[i * 4]) / 255.f;
	}
	return map;
}

void invertHeights(HeightMap& map) {
	for (auto& v : map.values) v = 1.f - v;
}

void applyHeightZones(HeightMap& map, const std::vector<std::uint8_t>& zones, unsigned zonesWidth, unsigned zonesHeight) {
	if (zonesWidth == 0 || zonesHeight == 0 || zones.size() < std::size_t(zonesWidth) * zonesHeight) {
		return;
	}
	for (unsigned y = 0; y < map.height; ++y) {
		const unsigned zy = std::min(zonesHeight - 1, y * zonesHeight / map.height);
		for (unsigned x = 0; x < map.width; ++x) {
			const unsigned zx = std::min(zonesWidth - 1, x * zonesWidth / map.width);
			auto& v = map.values[std::size_t(y) * map.width + x];
			switch (static_cast<HeightZone>(zones[std::size_t(zy) * zonesWidth + zx])) {
				case HeightZone::Flat: v = 1.f; break;          // on top: no depth there
				case HeightZone::Inverted: v = 1.f - v; break;
				case HeightZone::Normal: break;
			}
		}
	}
}

ImageRgba heightImage(const HeightMap& map) {
	ImageRgba image;
	image.width = static_cast<std::uint16_t>(map.width);
	image.height = static_cast<std::uint16_t>(map.height);
	image.pixels.resize(map.values.size() * 4);
	for (std::size_t i = 0; i < map.values.size(); ++i) {
		const auto v = std::byte(static_cast<unsigned char>(std::clamp(map.values[i], 0.f, 1.f) * 255.f + 0.5f));
		image.pixels[i * 4] = image.pixels[i * 4 + 1] = image.pixels[i * 4 + 2] = v;
		image.pixels[i * 4 + 3] = std::byte(255);
	}
	return image;
}

std::vector<std::byte> encodeHeightVtf(const HeightMap& map) {
	if (map.width == 0 || map.height == 0 || map.width > 65535 || map.height > 65535 || !powerOfTwo(map.width) || !powerOfTwo(map.height) ||
	    map.values.size() != std::size_t(map.width) * map.height) {
		return {};
	}
	// Byte for byte what deploy.ps1's height_map.cs writes (WriteI8): VTF 7.2, 80-byte header,
	// no thumbnail, no flags, I8 with every mip level box-filtered, smallest level first. The game
	// loads both alike; a different layout (7.4 with resources, another reflectivity) gave the
	// editor's relief no advantage and made it harder to compare with deploy's in a report.
	const unsigned w = map.width, h = map.height;
	std::vector<std::vector<std::uint8_t>> levels(1);
	levels[0].resize(std::size_t(w) * h);
	for (std::size_t i = 0; i < levels[0].size(); ++i) {
		levels[0][i] = static_cast<std::uint8_t>(std::clamp(map.values[i], 0.f, 1.f) * 255.f + 0.5f);
	}
	unsigned mips = 1;
	while ((w >> mips) > 0 || (h >> mips) > 0) ++mips;
	for (unsigned l = 1; l < mips; ++l) {
		const unsigned pw = std::max(w >> (l - 1), 1u), ph = std::max(h >> (l - 1), 1u);
		const unsigned lw = std::max(w >> l, 1u), lh = std::max(h >> l, 1u);
		const auto& prev = levels[l - 1];
		std::vector<std::uint8_t> cur(std::size_t(lw) * lh);
		for (unsigned y = 0; y < lh; ++y) {
			for (unsigned x = 0; x < lw; ++x) {
				const unsigned x0 = std::min(x * 2, pw - 1), x1 = std::min(x * 2 + 1, pw - 1);
				const unsigned y0 = std::min(y * 2, ph - 1), y1 = std::min(y * 2 + 1, ph - 1);
				cur[std::size_t(y) * lw + x] = static_cast<std::uint8_t>(
					(prev[std::size_t(y0) * pw + x0] + prev[std::size_t(y0) * pw + x1] + prev[std::size_t(y1) * pw + x0] + prev[std::size_t(y1) * pw + x1] + 2) / 4);
			}
		}
		levels.push_back(std::move(cur));
	}
	std::size_t total = 80;
	for (const auto& level : levels) total += level.size();
	std::vector<std::byte> out(total, std::byte(0));
	const auto put = [&out](std::size_t at, const void* data, std::size_t size) { std::memcpy(out.data() + at, data, size); };
	const auto put32 = [&put](std::size_t at, std::int32_t v) { put(at, &v, 4); };   // little endian like the game
	const auto put16 = [&put](std::size_t at, std::uint16_t v) { put(at, &v, 2); };
	const auto putF = [&put](std::size_t at, float v) { put(at, &v, 4); };
	put(0, "VTF", 4);
	put32(4, 7);
	put32(8, 2);
	put32(12, 80);
	put16(16, std::uint16_t(w));
	put16(18, std::uint16_t(h));
	put32(20, 0);              // flags
	put16(24, 1);              // frames
	putF(32, 0.5f);            // reflectivity
	putF(36, 0.5f);
	putF(40, 0.5f);
	putF(48, 1.0f);            // bump scale
	put32(52, 5);              // IMAGE_FORMAT_I8
	out[56] = std::byte(mips);
	put32(57, -1);             // no thumbnail
	put16(63, 1);              // depth
	std::size_t at = 80;
	for (unsigned l = mips; l-- > 0;) {
		put(at, levels[l].data(), levels[l].size());
		at += levels[l].size();
	}
	return out;
}

} // namespace sme

namespace sme {

namespace {

bool containsAnyI(std::string_view text, std::initializer_list<std::string_view> words) {
	const std::string lower = toLower(text);
	return std::any_of(words.begin(), words.end(), [&](std::string_view w) { return lower.find(w) != std::string::npos; });
}

} // namespace

std::optional<ReliefSource> fidelityReliefSource(std::string_view materialName, const Material& material) {
	// deploy.ps1's numbers: -ParallaxDepth 0.01 (stones, rock); bump maps and bricks and tiles in
	// courses at 0.6 of it (Julius: a tiled floor looked right at about 0.006).
	constexpr double DEPTH = 0.01, SHALLOW_SHARE = 0.6;
	const std::string shader = toLower(material.shader);
	if ((shader == "fidelity_parallax" || shader == "fidelity_worldvertextransition") && !material.param("$parallaxmap").empty()) {
		ReliefSource source;
		source.kind = ReliefSource::Kind::HeightMap;
		source.texture = material.param("$parallaxmap");
		if (const auto depth = material.param("$parallaxdepth"); !depth.empty()) {
			try {
				source.depth = std::stod(depth);
			} catch (const std::exception&) {
			}
		}
		return source;
	}
	const std::string path = "materials/" + toLower(materialName);
	for (const char* folder : {"models", "skybox", "tools", "decals", "overlays", "effects", "sprites", "console", "vgui", "dev", "editor",
	                           "particle", "water", "glass", "liquids", "nature/water"}) {
		const std::string prefix = std::string("materials/") + folder;
		if (path.starts_with(prefix) && (path.size() == prefix.size() || path[prefix.size()] == '/')) return std::nullopt;
	}
	const bool wall = shader == "lightmappedgeneric" || shader == "sdk_lightmappedgeneric" || shader == "worldvertextransition" ||
	                  shader == "sdk_worldvertextransition";
	if (!wall) return std::nullopt;
	// Drawn otherwise than the parallax shader can: see-through, decals, three-way mapping, detail modes 2+.
	for (const char* flag : {"$alphatest", "$translucent", "$additive", "$decal", "$nodiffusebumplighting", "$maskedblending"}) {
		if (material.flag(flag)) return std::nullopt;
	}
	if (!material.param("$seamless_scale").empty()) return std::nullopt;
	if (const auto mode = material.param("$detailblendmode"); !mode.empty() && std::atoi(mode.c_str()) >= 2) return std::nullopt;
	// Wood keeps its own look (deploy.bat without -ParallaxWood).
	const std::string base = material.param("$basetexture");
	if (containsAnyI(base.empty() ? path : base, {"wood", "plank", "board"})) return std::nullopt;
	// Windows, signs, grates, metal sheets, plaster ...: depth there makes no sense (relief_pattern).
	if (!reliefFitByName(materialName, base).suits) return std::nullopt;
	if (const auto bump = material.param("$bumpmap"); !bump.empty()) {
		ReliefSource source;
		source.kind = material.flag("$ssbump") ? ReliefSource::Kind::SSBump : ReliefSource::Kind::NormalMap;
		source.texture = bump;
		source.depth = DEPTH * SHALLOW_SHARE;
		return source;
	}
	if (base.empty() || !containsAnyI(path, {"brick", "stone", "cobble", "rock", "tile", "block", "cinder", "paver", "slate", "roof", "shingle"})) {
		return std::nullopt;
	}
	ReliefSource source;
	source.kind = ReliefSource::Kind::Color;
	source.texture = base;
	source.depth = containsAnyI(path, {"brick", "tile", "block", "cinder", "paver"}) ? DEPTH * SHALLOW_SHARE : DEPTH;
	return source;
}

} // namespace sme
