// photo_material.cpp - A photo becomes a material (see photo_material.h).

#include <sme/photo_material.h>

#include <sme/heightmap.h>
#include <sme/text.h>
#include <sme/texture_enhance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr unsigned MIN_SIZE = 64;
constexpr unsigned MAX_SIZE = 2048;

bool valid(const ImageRgba& image) {
	return image.width > 0 && image.height > 0 && image.pixels.size() == std::size_t(image.width) * image.height * 4;
}

int channel(const ImageRgba& image, unsigned x, unsigned y, int c) {
	return std::to_integer<int>(image.pixels[(std::size_t(y) * image.width + x) * 4 + std::size_t(c)]);
}

std::byte toByte(double v) {
	return std::byte(std::uint8_t(std::clamp(std::lround(v), 0L, 255L)));
}

std::string num(double v) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%.2f", v);
	std::string out = buffer;
	while (out.find('.') != std::string::npos && (out.back() == '0' || out.back() == '.')) {
		const bool dot = out.back() == '.';
		out.pop_back();
		if (dot) break;
	}
	return out;
}

/// Box blur of a w x h float field along both axes, wrapping (prefix sums, radius r).
void boxBlurWrap(std::vector<float>& field, unsigned w, unsigned h, unsigned r) {
	std::vector<double> prefix;
	std::vector<float> line;
	const auto pass = [&](unsigned count, unsigned length, auto index) {
		prefix.assign(std::size_t(length) * 3 + 1, 0.0);
		line.resize(length);
		const unsigned radius = std::min(r, length / 2);
		for (unsigned a = 0; a < count; ++a) {
			// Three copies side by side so the window can wrap without branches.
			for (unsigned i = 0; i < length * 3; ++i) prefix[i + 1] = prefix[i] + field[index(a, i % length)];
			for (unsigned i = 0; i < length; ++i) {
				const std::size_t lo = std::size_t(length) + i - radius, hi = std::size_t(length) + i + radius + 1;
				line[i] = float((prefix[hi] - prefix[lo]) / double(hi - lo));
			}
			for (unsigned i = 0; i < length; ++i) field[index(a, i)] = line[i];
		}
	};
	pass(h, w, [w](unsigned y, unsigned x) { return std::size_t(y) * w + x; });
	pass(w, h, [w](unsigned x, unsigned y) { return std::size_t(y) * w + x; });
}

/// One pass of the tile blend along x (`alongX`) or y.
ImageRgba blendPass(const ImageRgba& image, double blend, bool alongX) {
	const unsigned w = image.width, h = image.height;
	const unsigned length = alongX ? w : h;
	const double band = std::clamp(blend, 0.0, 0.5) * double(length);
	if (band < 1.0 || length < 4) return image;
	std::vector<double> weight(length);
	for (unsigned i = 0; i < length; ++i) {
		const double d = double(std::min(i, length - 1 - i));
		const double t = std::clamp(d / band, 0.0, 1.0);
		weight[i] = t * t * (3 - 2 * t);   // smoothstep: 0 at the edge, 1 inside
	}
	ImageRgba out = image;
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			const double k = weight[alongX ? x : y];
			if (k >= 1.0) continue;
			const unsigned sx = alongX ? (x + w / 2) % w : x, sy = alongX ? y : (y + h / 2) % h;
			for (int c = 0; c < 4; ++c) {
				const double v = k * channel(image, x, y, c) + (1 - k) * channel(image, sx, sy, c);
				out.pixels[(std::size_t(y) * w + x) * 4 + std::size_t(c)] = toByte(v);
			}
		}
	}
	return out;
}

bool containsWord(std::string_view text, std::string_view word) {
	return text.find(word) != std::string_view::npos;
}

} // namespace

ImageRgba cropPhotoSquare(const ImageRgba& photo, double x, double y, double size) {
	if (!valid(photo)) return {};
	const unsigned shorter = std::min(photo.width, photo.height);
	const unsigned side = std::clamp(unsigned(std::lround(double(shorter) * std::clamp(size, 0.1, 1.0))), 1u, unsigned(shorter));
	const unsigned x0 = unsigned(std::lround(std::clamp(x, 0.0, 1.0) * double(photo.width - side)));
	const unsigned y0 = unsigned(std::lround(std::clamp(y, 0.0, 1.0) * double(photo.height - side)));
	return cropPicture(photo, x0, y0, side, side);
}

void flattenPhotoLighting(ImageRgba& image, double amount) {
	amount = std::clamp(amount, 0.0, 1.0);
	if (!valid(image) || amount <= 0) return;
	const unsigned w = image.width, h = image.height;
	const std::size_t n = std::size_t(w) * h;
	std::vector<float> light(n);
	double sum = 0;
	for (std::size_t i = 0; i < n; ++i) {
		const int r = std::to_integer<int>(image.pixels[i * 4]), g = std::to_integer<int>(image.pixels[i * 4 + 1]),
		          b = std::to_integer<int>(image.pixels[i * 4 + 2]);
		light[i] = float(r * 0.2126 + g * 0.7152 + b * 0.0722) + 1.0f;
		sum += light[i];
	}
	const double mean = sum / double(n);
	// A wide blur (three box passes ~ Gaussian) keeps only the slow change: the lighting, not the
	// texture's own pattern.
	const unsigned radius = std::max(1u, std::min(w, h) / 8);
	for (int i = 0; i < 3; ++i) boxBlurWrap(light, w, h, radius);
	for (std::size_t i = 0; i < n; ++i) {
		const double factor = std::clamp(std::pow(mean / double(light[i]), amount), 0.25, 4.0);
		for (int c = 0; c < 3; ++c) image.pixels[i * 4 + std::size_t(c)] = toByte(std::to_integer<int>(image.pixels[i * 4 + std::size_t(c)]) * factor);
	}
}

ImageRgba makePhotoTileable(const ImageRgba& image, double blend) {
	if (!valid(image)) return {};
	return blendPass(blendPass(image, blend, true), blend, false);
}

int photoSeamError(const ImageRgba& image) {
	if (!valid(image) || image.width < 2 || image.height < 2) return 0;
	double total = 0;
	std::size_t count = 0;
	for (unsigned y = 0; y < image.height; ++y) {
		for (int c = 0; c < 3; ++c) total += std::abs(channel(image, 0, y, c) - channel(image, image.width - 1, y, c));
		count += 3;
	}
	for (unsigned x = 0; x < image.width; ++x) {
		for (int c = 0; c < 3; ++c) total += std::abs(channel(image, x, 0, c) - channel(image, x, image.height - 1, c));
		count += 3;
	}
	return int(std::lround(total / double(count)));
}

unsigned photoTextureSize(unsigned side) {
	unsigned size = MIN_SIZE;
	while (size * 2 <= side && size * 2 <= 1024) size *= 2;
	return size;
}

std::string suggestPhotoSurfaceProp(std::string_view nameIn, const ImageRgba& color) {
	const std::string name = toLower(nameIn);
	// Name first: the mapper knows what the photo shows.
	static const std::array<std::pair<std::string_view, std::string_view>, 26> words{{
		{"brick", "brick"}, {"concrete", "concrete"}, {"cement", "concrete"}, {"asphalt", "concrete"}, {"plaster", "plaster"},
		{"tile", "tile"}, {"rock", "rock"}, {"stone", "rock"}, {"cliff", "rock"}, {"gravel", "gravel"}, {"dirt", "dirt"},
		{"soil", "dirt"}, {"mud", "mud"}, {"grass", "grass"}, {"sand", "sand"}, {"snow", "snow"}, {"ice", "ice"},
		{"plank", "wood_plank"}, {"wood", "wood"}, {"crate", "wood_crate"}, {"grate", "metalgrate"}, {"metal", "metal"},
		{"rust", "metal"}, {"glass", "glass"}, {"carpet", "carpet"}, {"rubber", "rubber"},
	}};
	for (const auto& [word, prop] : words) {
		if (containsWord(name, word)) return std::string(prop);
	}
	if (!valid(color)) return "default";
	// Colour: average hue and saturation.
	double r = 0, g = 0, b = 0;
	const std::size_t n = std::size_t(color.width) * color.height;
	for (std::size_t i = 0; i < n; ++i) {
		r += std::to_integer<int>(color.pixels[i * 4]);
		g += std::to_integer<int>(color.pixels[i * 4 + 1]);
		b += std::to_integer<int>(color.pixels[i * 4 + 2]);
	}
	r /= double(n), g /= double(n), b /= double(n);
	const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
	const double saturation = hi > 0 ? (hi - lo) / hi : 0;
	if (saturation < 0.12) return hi > 200 ? "plaster" : "concrete";
	if (g >= r && g >= b) return "grass";
	if (r >= g && g >= b && saturation > 0.2) return r - b > 60 && hi < 170 ? "dirt" : "brick";
	return "default";
}

std::optional<PhotoMaterial> makePhotoMaterial(const ImageRgba& photo, const PhotoMaterialOptions& options, std::string* error) {
	if (!valid(photo)) {
		if (error) *error = "the photo is empty or could not be read";
		return std::nullopt;
	}
	if (std::min(photo.width, photo.height) < 32) {
		if (error) *error = "the photo is too small (at least 32 x 32 pixels)";
		return std::nullopt;
	}
	ImageRgba square = cropPhotoSquare(photo, options.cropX, options.cropY, options.cropSize);
	unsigned size = options.size ? options.size : photoTextureSize(square.width);
	size = std::clamp(size, MIN_SIZE, MAX_SIZE);
	unsigned pow2 = MIN_SIZE;
	while (pow2 * 2 <= size) pow2 *= 2;
	size = pow2;
	PhotoMaterial out;
	out.color = square.width == size ? std::move(square) : resizeLanczos(square, size, size, EdgeMode::Clamp, EdgeMode::Clamp);
	// Photos have no alpha that means anything: opaque.
	for (std::size_t i = 3; i < out.color.pixels.size(); i += 4) out.color.pixels[i] = std::byte{255};
	flattenPhotoLighting(out.color, options.flattenLight);
	out.color = makePhotoTileable(out.color, options.seamBlend);

	const double strength = std::clamp(options.bumpStrength, 0.0, 4.0);
	const double gloss = std::clamp(options.gloss, 0.0, 1.0);
	ImageRgba glossMask = gloss > 0 ? glossMaskFromColor(out.color) : ImageRgba{};
	const bool needNormal = strength > 0 || (options.model && gloss > 0);
	if (needNormal) {
		if (strength > 0) {
			std::string why;
			const auto heights = heightFromColor(out.color, &why);
			if (heights) out.normal = normalMapFromHeight(*heights, strength, size, size);
		}
		if (!valid(out.normal)) {
			// Flat: straight up (phong needs a bump map for its mask).
			out.normal.width = out.normal.height = std::uint16_t(size);
			out.normal.pixels.assign(std::size_t(size) * size * 4, std::byte{0});
			for (std::size_t i = 0; i < out.normal.pixels.size(); i += 4) {
				out.normal.pixels[i] = out.normal.pixels[i + 1] = std::byte{128};
				out.normal.pixels[i + 2] = out.normal.pixels[i + 3] = std::byte{255};
			}
		}
		// The gloss goes into the bump map's alpha (phong mask / $normalmapalphaenvmapmask).
		for (std::size_t i = 0; i < out.normal.pixels.size(); i += 4) {
			out.normal.pixels[i + 3] = valid(glossMask) ? glossMask.pixels[i] : std::byte{255};
		}
	} else if (valid(glossMask)) {
		out.mask = std::move(glossMask);
	}
	out.surfaceProp = options.surfaceProp.empty() ? suggestPhotoSurfaceProp(options.name, out.color) : options.surfaceProp;
	return out;
}

std::string photoMaterialName(const PhotoMaterialOptions& options) {
	MaterialImportOptions o;
	o.folder = options.folder;
	o.name = options.name;
	o.use = options.model ? MaterialImportOptions::Use::Model : MaterialImportOptions::Use::Brush;
	return materialImportName(o);
}

std::string photoMaterialVmt(const PhotoMaterialOptions& options, const PhotoMaterial& material) {
	const std::string name = photoMaterialName(options);
	const double gloss = std::clamp(options.gloss, 0.0, 1.0);
	const bool normal = valid(material.normal);
	std::string out = options.model ? "\"VertexLitGeneric\"\n{\n" : "\"LightmappedGeneric\"\n{\n";
	out += "\t\"$basetexture\" \"" + name + "\"\n";
	if (normal) out += "\t\"$bumpmap\" \"" + name + "_normal\"\n";
	out += "\t\"$surfaceprop\" \"" + (material.surfaceProp.empty() ? std::string("default") : material.surfaceProp) + "\"\n";
	if (gloss > 0) {
		if (options.model) {
			// Phong; its mask is the bump map's alpha (the default when $bumpmap is set).
			out += "\t\"$phong\" \"1\"\n";
			out += "\t\"$phongexponent\" \"" + num(8 + 32 * gloss) + "\"\n";
			out += "\t\"$phongboost\" \"" + num(0.5 + 3 * gloss) + "\"\n";
			out += "\t\"$phongfresnelranges\" \"[0.2 0.6 1]\"\n";
		} else {
			out += "\t\"$envmap\" \"env_cubemap\"\n";
			if (normal) {
				out += "\t\"$normalmapalphaenvmapmask\" \"1\"\n";
			} else if (valid(material.mask)) {
				out += "\t\"$envmapmask\" \"" + name + "_mask\"\n";
			}
			const std::string tint = num(0.1 + 0.6 * gloss);
			out += "\t\"$envmaptint\" \"[" + tint + " " + tint + " " + tint + "]\"\n";
		}
	}
	out += "}\n";
	return out;
}

std::vector<fs::path> photoMaterialTargets(const fs::path& modDir, const PhotoMaterialOptions& options, const PhotoMaterial& material) {
	const fs::path base = modDir / "materials" / fs::path(photoMaterialName(options));
	std::vector<fs::path> out{base.string() + ".vmt", base.string() + ".vtf"};
	if (valid(material.normal)) out.emplace_back(base.string() + "_normal.vtf");
	if (valid(material.mask)) out.emplace_back(base.string() + "_mask.vtf");
	return out;
}

std::optional<ImportRecord> writePhotoMaterial(const fs::path& modDir, const PhotoMaterialOptions& options, const PhotoMaterial& material, bool overwrite,
                                               std::string* error) {
	if (importName(options.name).empty()) {
		if (error) *error = "the material needs a name";
		return std::nullopt;
	}
	if (!valid(material.color)) {
		if (error) *error = "nothing to write";
		return std::nullopt;
	}
	const auto targets = photoMaterialTargets(modDir, options, material);
	if (!overwrite) {
		std::error_code ec;
		for (const auto& file : targets) {
			if (fs::exists(file, ec)) {
				if (error) *error = displayPath(file) + " already exists";
				return std::nullopt;
			}
		}
	}
	const unsigned size = material.color.width;
	std::vector<std::pair<fs::path, std::vector<std::byte>>> files;
	const auto vtf = encodeVtf(material.color, true, false, size, error);
	if (!vtf) return std::nullopt;
	files.emplace_back(targets[1], *vtf);
	std::size_t next = 2;
	if (valid(material.normal)) {
		const auto normal = encodeVtf(material.normal, false, true, size, error);
		if (!normal) return std::nullopt;
		files.emplace_back(targets[next++], *normal);
	}
	if (valid(material.mask)) {
		const auto mask = encodeVtf(material.mask, true, false, size, error);
		if (!mask) return std::nullopt;
		files.emplace_back(targets[next++], *mask);
	}
	const std::string vmt = photoMaterialVmt(options, material);
	files.emplace_back(targets[0], std::vector<std::byte>(reinterpret_cast<const std::byte*>(vmt.data()), reinterpret_cast<const std::byte*>(vmt.data()) + vmt.size()));
	ImportRecord record{"material", photoMaterialName(options), {}, {}, {}};
	for (const auto& [file, data] : files) {
		if (!writeImportFile(record, file, data, overwrite, error)) {
			undoImport(record);
			return std::nullopt;
		}
	}
	return record;
}

} // namespace sme
