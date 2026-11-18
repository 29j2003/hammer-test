// texture_enhance.cpp - Larger textures, new bump maps and reflection masks for the mod's
// materials (see texture_enhance.h for the rules and why world materials get a transform).

#include <sme/texture_enhance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numbers>
#include <set>
#include <sstream>

#include <vtfpp/vtfpp.h>

#include <sme/asset_import.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/heightmap.h>
#include <sme/keyvalues.h>
#include <sme/material.h>
#include <sme/material_decisions.h>
#include <sme/text.h>

namespace sme {

namespace fs = std::filesystem;

namespace {

// --- Resampling ------------------------------------------------------------------------------

double lanczos3(double x) {
	x = std::abs(x);
	if (x < 1e-8) return 1.0;
	if (x >= 3.0) return 0.0;
	const double px = std::numbers::pi * x;
	return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}

int edgeIndex(int i, int n, EdgeMode mode) {
	if (mode == EdgeMode::Wrap) return ((i % n) + n) % n;
	return std::clamp(i, 0, n - 1);
}

/// For every output position the source positions and weights (normalised to 1).
struct Taps {
	std::vector<int> first;    ///< Start in `index`/`weight` per output position
	std::vector<int> count;
	std::vector<int> index;
	std::vector<float> weight;
};

Taps makeTaps(int in, int out, EdgeMode mode) {
	Taps t;
	t.first.resize(std::size_t(out));
	t.count.resize(std::size_t(out));
	const double scale = double(in) / double(out);
	const double stretch = std::max(1.0, scale);   // downscaling widens the kernel (no aliasing)
	const double support = 3.0 * stretch;
	for (int o = 0; o < out; ++o) {
		const double centre = (o + 0.5) * scale - 0.5;
		const int lo = int(std::ceil(centre - support));
		const int hi = int(std::floor(centre + support));
		t.first[std::size_t(o)] = int(t.index.size());
		double sum = 0;
		const std::size_t start = t.weight.size();
		for (int i = lo; i <= hi; ++i) {
			const double w = lanczos3((i - centre) / stretch);
			if (w == 0.0) continue;
			t.index.push_back(edgeIndex(i, in, mode));
			t.weight.push_back(float(w));
			sum += w;
		}
		if (sum != 0.0) {
			for (std::size_t k = start; k < t.weight.size(); ++k) t.weight[k] = float(t.weight[k] / sum);
		}
		t.count[std::size_t(o)] = int(t.index.size()) - t.first[std::size_t(o)];
	}
	return t;
}

/// Separable resampling of `channels` floats per texel.
std::vector<float> resampleFloat(const std::vector<float>& src, int channels, int inW, int inH, int outW, int outH, EdgeMode u, EdgeMode v) {
	const Taps tx = makeTaps(inW, outW, u);
	const Taps ty = makeTaps(inH, outH, v);
	std::vector<float> mid(std::size_t(outW) * std::size_t(inH) * std::size_t(channels));
	for (int y = 0; y < inH; ++y) {
		const float* row = src.data() + std::size_t(y) * std::size_t(inW) * std::size_t(channels);
		float* dst = mid.data() + std::size_t(y) * std::size_t(outW) * std::size_t(channels);
		for (int x = 0; x < outW; ++x) {
			const int f = tx.first[std::size_t(x)], n = tx.count[std::size_t(x)];
			for (int c = 0; c < channels; ++c) {
				float acc = 0;
				for (int k = 0; k < n; ++k) acc += row[std::size_t(tx.index[std::size_t(f + k)]) * std::size_t(channels) + std::size_t(c)] * tx.weight[std::size_t(f + k)];
				dst[std::size_t(x) * std::size_t(channels) + std::size_t(c)] = acc;
			}
		}
	}
	std::vector<float> out(std::size_t(outW) * std::size_t(outH) * std::size_t(channels));
	const std::size_t stride = std::size_t(outW) * std::size_t(channels);
	for (int y = 0; y < outH; ++y) {
		const int f = ty.first[std::size_t(y)], n = ty.count[std::size_t(y)];
		float* dst = out.data() + std::size_t(y) * stride;
		for (int k = 0; k < n; ++k) {
			const float w = ty.weight[std::size_t(f + k)];
			const float* srcRow = mid.data() + std::size_t(ty.index[std::size_t(f + k)]) * stride;
			for (std::size_t i = 0; i < stride; ++i) dst[i] += srcRow[i] * w;
		}
	}
	return out;
}

std::uint8_t toByte(float v) { return std::uint8_t(std::clamp(int(std::lround(v)), 0, 255)); }

bool validImage(const ImageRgba& image) {
	return image.width > 0 && image.height > 0 && image.pixels.size() == std::size_t(image.width) * image.height * 4;
}

unsigned clampSize(unsigned v) { return std::clamp(v, 1u, ENHANCE_MAX_SIZE); }

bool isPowerOfTwo(unsigned v) { return v > 0 && (v & (v - 1)) == 0; }

ImageAlpha alphaOf(const ImageRgba& image) { return imageAlpha(image); }

// --- Material text helpers --------------------------------------------------------------------

std::string vmtOf(std::string_view material) { return "materials/" + std::string(material) + ".vmt"; }
std::string vtfOf(std::string_view texture) { return "materials/" + std::string(texture) + ".vtf"; }

/// The keys of the coordinate transforms the brush shaders apply (LightmappedGeneric,
/// WorldVertexTransition, Fidelity_Parallax): each one scaled when the base texture is larger.
constexpr std::array<std::string_view, 6> TRANSFORM_KEYS = {"$basetexturetransform", "$bumptransform", "$envmapmasktransform",
                                                            "$basetexturetransform2", "$bumptransform2", "$blendmasktransform"};

bool isIdentityTransform(std::string_view value) {
	// "center .5 .5 scale 1 1 rotate 0 translate 0 0" and shorter forms without scale/rotate/translate.
	std::istringstream in{std::string(value)};
	std::string word;
	double scaleU = 1, scaleV = 1, rotate = 0, tu = 0, tv = 0;
	while (in >> word) {
		const std::string w = toLower(word);
		if (w == "center") {
			double a, b;
			in >> a >> b;
		} else if (w == "scale") {
			in >> scaleU >> scaleV;
		} else if (w == "rotate") {
			in >> rotate;
		} else if (w == "translate") {
			in >> tu >> tv;
		} else {
			return false;   // a matrix "[...]" or something else: not ours to judge
		}
		if (in.fail()) return false;
	}
	return scaleU == 1 && scaleV == 1 && rotate == 0 && tu == 0 && tv == 0;
}

/// K of "center 0 0 scale K L rotate 0 translate 0 0" (Enhance textures' own), or nullopt.
std::optional<std::pair<double, double>> ownTransformScale(std::string_view value) {
	std::istringstream in{std::string(value)};
	std::string a, b, c, d, e;
	double cu = 1, cv = 1, su = 0, sv = 0, r = 1, tu = 1, tv = 1;
	in >> a >> cu >> cv >> b >> su >> sv >> c >> r >> d >> tu >> tv;
	if (in.fail() || (in >> e)) return std::nullopt;
	if (!iequals(a, "center") || !iequals(b, "scale") || !iequals(c, "rotate") || !iequals(d, "translate")) return std::nullopt;
	if (cu != 0 || cv != 0 || r != 0 || tu != 0 || tv != 0 || su <= 0 || sv <= 0) return std::nullopt;
	return std::pair{su, sv};
}

std::string numberText(double v) {
	std::ostringstream out;
	out << std::round(v * 10000.0) / 10000.0;
	return out.str();
}

/// The first line of a generated material (its marker comment) and the rest.
std::pair<std::string, std::string> splitMarker(std::string_view vmt) {
	std::string_view first = vmt.substr(0, vmt.find('\n'));
	std::string_view check = first;
	if (check.starts_with("\xEF\xBB\xBF")) check.remove_prefix(3);
	if (!trim(check).starts_with("//")) return {{}, std::string(vmt)};
	const std::size_t end = vmt.find('\n');
	if (end == std::string_view::npos) return {std::string(first), {}};
	std::string line(first);
	if (!line.empty() && line.back() == '\r') line.pop_back();
	return {line, std::string(vmt.substr(end + 1))};
}

/// The patch resolved: the included material with insert/replace on it (nullopt for plain ones).
std::optional<Material> resolvePatch(const GameFileSystem& fs, std::string_view text, std::string_view name) {
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return std::nullopt;
	}
	const KvNode* block = nullptr;
	for (const auto& child : root.children) {
		if (child.block) {
			block = &child;
			break;
		}
	}
	if (!block || !iequals(block->key, "patch")) return std::nullopt;
	const std::string include = normalizeMaterialName(block->get("include"));
	if (include.empty() || iequals(include, name)) return std::nullopt;   // a patch of itself would loop
	auto base = loadMaterial(fs, include);
	if (!base) return std::nullopt;
	for (const char* section : {"insert", "replace"}) {
		if (const KvNode* part = block->find(section); part && part->block) {
			for (const auto& kv : part->children) {
				if (!kv.block) base->params[toLower(kv.key)] = kv.value;
			}
		}
	}
	base->name = std::string(name);
	base->includeChain.insert(base->includeChain.begin(), include);
	return base;
}

/// The material of a text, patches resolved.
std::optional<Material> materialOfText(const GameFileSystem& fs, std::string_view text, std::string_view name) {
	if (auto patch = resolvePatch(fs, text, name)) return patch;
	auto parsed = parseMaterial(text, name);
	if (parsed && iequals(parsed->shader, "patch")) return std::nullopt;
	return parsed;
}

/// Edits through editMaterialText, keeping a generated material's marker line on top.
std::string editKeepingMarker(std::string_view vmt, const std::vector<MaterialEdit>& edits, const Material* resolved) {
	auto [marker, body] = splitMarker(vmt);
	std::string edited;
	try {
		edited = editMaterialText(body, {}, edits, resolved);
	} catch (const std::exception&) {
		return std::string(vmt);
	}
	if (marker.empty()) return edited;
	return marker + "\r\n" + edited;
}

bool fileExists(const fs::path& p) {
	std::error_code ec;
	return fs::is_regular_file(p, ec);
}

std::optional<std::string> readText(const fs::path& p) { return readFileText(p); }

bool writeAtomic(const fs::path& file, std::span<const std::byte> bytes) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = fs::path(file).concat(".mapsmith-tmp");
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
		if (!out) {
			out.close();
			fs::remove(temp, ec);
			return false;
		}
	}
	fs::rename(temp, file, ec);
	if (ec) {
		fs::remove(temp, ec);
		return false;
	}
	return true;
}

bool writeAtomicText(const fs::path& file, std::string_view text) {
	return writeAtomic(file, std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

/// A file name Enhance textures may write or delete: safe, below materials/, a .vtf or .vmt.
bool ownFilePath(std::string_view path) {
	if (!isSafeGamePath(path) || !startsWithI(path, "materials/")) return false;
	return endsWithI(path, ".vtf") || endsWithI(path, ".vmt");
}

/// The VTF flags kept on a larger copy (sampling and wrap modes; alpha flags are worked out anew).
constexpr std::uint32_t KEPT_FLAGS = 0x2u /*TRILINEAR*/ | 0x4u /*CLAMPS*/ | 0x8u /*CLAMPT*/ | 0x10u /*ANISOTROPIC*/ | 0x40u /*SRGB*/ |
                                     0x80u /*NORMAL*/ | 0x200u /*NOLOD*/ | 0x2000000u /*CLAMPU*/ | 0x8000000u /*SSBUMP*/ | 0x20000000u /*BORDER*/;

std::optional<std::vector<std::byte>> encodeTexture(const ImageRgba& image, std::uint32_t flags, bool normalMap, bool compress) {
	if (!validImage(image)) return std::nullopt;
	try {
		vtfpp::VTF::CreationOptions options;
		options.version = 4;   // 7.4: what Source 2013 reads (public/vtf/vtf.h VTF_MINOR_VERSION)
		options.widthResizeMethod = vtfpp::ImageConversion::ResizeMethod::NONE;
		options.heightResizeMethod = vtfpp::ImageConversion::ResizeMethod::NONE;
		options.flags = flags & KEPT_FLAGS;
		if (normalMap) options.flags |= vtfpp::VTF::FLAG_V0_NORMAL;
		const bool alpha = alphaOf(image) != ImageAlpha::Opaque;
		if (normalMap || !compress) {
			options.outputFormat = vtfpp::ImageFormat::BGRA8888;
		} else {
			options.outputFormat = alpha ? vtfpp::ImageFormat::DXT5 : vtfpp::ImageFormat::DXT1;
		}
		options.compressionLevel = 0;
		const auto vtf = vtfpp::VTF::create(image.pixels, vtfpp::ImageFormat::RGBA8888, image.width, image.height, options);
		if (!vtf) return std::nullopt;
		auto data = vtf.bake();
		if (data.empty()) return std::nullopt;
		return data;
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

/// Materials by shader: brushes (texel coordinates) or models (0..1).
enum class ShaderUse { World, Model, Other };

ShaderUse shaderUse(std::string_view shader, std::string_view material) {
	const std::string s = toLower(shader);
	const bool modelPath = startsWithI(material, "models/");
	const bool skyPath = startsWithI(material, "skybox/");
	for (std::string_view world : {"lightmappedgeneric", "sdk_lightmappedgeneric", "worldvertextransition", "sdk_worldvertextransition", "fidelity_parallax",
	                               "fidelity_worldvertextransition", "lightmappedgeneric_dx9", "worldvertextransition_dx9"}) {
		if (s == world) return modelPath ? ShaderUse::Other : ShaderUse::World;
	}
	for (std::string_view model : {"vertexlitgeneric", "sdk_vertexlitgeneric", "vertexlitgeneric_dx9", "eyerefract", "eyes", "teeth", "fidelity_liquid"}) {
		if (s == model) return ShaderUse::Model;
	}
	if (s == "unlitgeneric" || s == "sdk_unlitgeneric") return (modelPath || skyPath) ? ShaderUse::Model : ShaderUse::World;
	return ShaderUse::Other;
}

} // namespace

// --- Pictures ------------------------------------------------------------------------------

ImageRgba resizeLanczos(const ImageRgba& image, unsigned width, unsigned height, EdgeMode edgesU, EdgeMode edgesV) {
	if (!validImage(image)) return {};
	width = clampSize(width);
	height = clampSize(height);
	const int inW = image.width, inH = image.height;
	const std::size_t n = std::size_t(inW) * std::size_t(inH);
	std::vector<float> src(n * 4);
	for (std::size_t i = 0; i < n * 4; ++i) src[i] = float(std::to_integer<int>(image.pixels[i]));
	const auto out = resampleFloat(src, 4, inW, inH, int(width), int(height), edgesU, edgesV);
	ImageRgba result;
	result.width = std::uint16_t(width);
	result.height = std::uint16_t(height);
	result.pixels.resize(out.size());
	for (std::size_t i = 0; i < out.size(); ++i) result.pixels[i] = std::byte(toByte(out[i]));
	return result;
}

void sharpen(ImageRgba& image, double amount, EdgeMode edgesU, EdgeMode edgesV) {
	if (!validImage(image) || amount <= 0) return;
	const int w = image.width, h = image.height;
	const std::vector<std::byte> src = image.pixels;
	const auto at = [&](int x, int y, int c) {
		return float(std::to_integer<int>(src[(std::size_t(edgeIndex(y, h, edgesV)) * std::size_t(w) + std::size_t(edgeIndex(x, w, edgesU))) * 4 + std::size_t(c)]));
	};
	const float a = float(std::clamp(amount, 0.0, 2.0));
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			for (int c = 0; c < 3; ++c) {
				const float blur = (at(x - 1, y - 1, c) + 2 * at(x, y - 1, c) + at(x + 1, y - 1, c) + 2 * at(x - 1, y, c) + 4 * at(x, y, c) + 2 * at(x + 1, y, c) +
				                    at(x - 1, y + 1, c) + 2 * at(x, y + 1, c) + at(x + 1, y + 1, c)) /
				                   16.f;
				const float v = at(x, y, c);
				image.pixels[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)] = std::byte(toByte(v + a * (v - blur)));
			}
		}
	}
}

ImageRgba resizeNormalMap(const ImageRgba& image, unsigned width, unsigned height, bool ssbump, EdgeMode edgesU, EdgeMode edgesV) {
	if (!validImage(image)) return {};
	width = clampSize(width);
	height = clampSize(height);
	const int inW = image.width, inH = image.height;
	const std::size_t n = std::size_t(inW) * std::size_t(inH);
	std::vector<float> src(n * 4);
	for (std::size_t i = 0; i < n; ++i) {
		for (int c = 0; c < 3; ++c) {
			const float v = float(std::to_integer<int>(image.pixels[i * 4 + std::size_t(c)])) / 255.f;
			src[i * 4 + std::size_t(c)] = ssbump ? v : v * 2.f - 1.f;
		}
		src[i * 4 + 3] = float(std::to_integer<int>(image.pixels[i * 4 + 3]));
	}
	const auto out = resampleFloat(src, 4, inW, inH, int(width), int(height), edgesU, edgesV);
	ImageRgba result;
	result.width = std::uint16_t(width);
	result.height = std::uint16_t(height);
	result.pixels.resize(out.size());
	const std::size_t m = std::size_t(width) * height;
	for (std::size_t i = 0; i < m; ++i) {
		float x = out[i * 4], y = out[i * 4 + 1], z = out[i * 4 + 2];
		if (!ssbump) {
			z = std::max(z, 0.f);
			const float len = std::sqrt(x * x + y * y + z * z);
			if (len > 1e-6f) {
				x /= len;
				y /= len;
				z /= len;
			} else {
				x = y = 0;
				z = 1;
			}
			x = (x + 1.f) * 0.5f;
			y = (y + 1.f) * 0.5f;
			z = (z + 1.f) * 0.5f;
		}
		result.pixels[i * 4] = std::byte(toByte(x * 255.f));
		result.pixels[i * 4 + 1] = std::byte(toByte(y * 255.f));
		result.pixels[i * 4 + 2] = std::byte(toByte(z * 255.f));
		result.pixels[i * 4 + 3] = std::byte(toByte(out[i * 4 + 3]));
	}
	return result;
}

ImageRgba padPicture(const ImageRgba& image, unsigned margin, EdgeMode edgesU, EdgeMode edgesV) {
	if (!validImage(image)) return {};
	margin = std::min(margin, 1024u);
	const int w = image.width, h = image.height, m = int(margin);
	ImageRgba out;
	out.width = std::uint16_t(std::min(65535, w + 2 * m));
	out.height = std::uint16_t(std::min(65535, h + 2 * m));
	out.pixels.resize(std::size_t(out.width) * out.height * 4);
	for (int y = 0; y < out.height; ++y) {
		const int sy = edgeIndex(y - m, h, edgesV);
		for (int x = 0; x < out.width; ++x) {
			const int sx = edgeIndex(x - m, w, edgesU);
			const std::size_t s = (std::size_t(sy) * std::size_t(w) + std::size_t(sx)) * 4;
			const std::size_t d = (std::size_t(y) * out.width + std::size_t(x)) * 4;
			std::copy_n(image.pixels.begin() + std::ptrdiff_t(s), 4, out.pixels.begin() + std::ptrdiff_t(d));
		}
	}
	return out;
}

ImageRgba cropPicture(const ImageRgba& image, unsigned x, unsigned y, unsigned width, unsigned height) {
	if (!validImage(image) || x >= image.width || y >= image.height) return {};
	width = std::min<unsigned>(width, image.width - x);
	height = std::min<unsigned>(height, image.height - y);
	ImageRgba out;
	out.width = std::uint16_t(width);
	out.height = std::uint16_t(height);
	out.pixels.resize(std::size_t(width) * height * 4);
	for (unsigned row = 0; row < height; ++row) {
		const std::size_t s = ((std::size_t(y) + row) * image.width + x) * 4;
		std::copy_n(image.pixels.begin() + std::ptrdiff_t(s), std::size_t(width) * 4, out.pixels.begin() + std::ptrdiff_t(std::size_t(row) * width * 4));
	}
	return out;
}

void thresholdAlpha(ImageRgba& image) {
	for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = std::to_integer<int>(image.pixels[i]) >= 128 ? std::byte(255) : std::byte(0);
}

ImageRgba glossMaskFromColor(const ImageRgba& image) {
	if (!validImage(image)) return {};
	const std::size_t n = std::size_t(image.width) * image.height;
	std::vector<std::uint8_t> luma(n);
	std::array<std::size_t, 256> histogram{};
	for (std::size_t i = 0; i < n; ++i) {
		const int r = std::to_integer<int>(image.pixels[i * 4]), g = std::to_integer<int>(image.pixels[i * 4 + 1]), b = std::to_integer<int>(image.pixels[i * 4 + 2]);
		luma[i] = std::uint8_t((r * 54 + g * 183 + b * 19) >> 8);
		++histogram[luma[i]];
	}
	const auto percentile = [&](double p) {
		const std::size_t target = std::size_t(double(n) * p);
		std::size_t sum = 0;
		for (int v = 0; v < 256; ++v) {
			sum += histogram[std::size_t(v)];
			if (sum > target) return v;
		}
		return 255;
	};
	const int lo = percentile(0.05), hi = std::max(percentile(0.95), lo + 1);
	ImageRgba out;
	out.width = image.width;
	out.height = image.height;
	out.pixels.resize(n * 4);
	for (std::size_t i = 0; i < n; ++i) {
		const auto v = std::byte(toByte(float(std::clamp(int(luma[i]) - lo, 0, hi - lo)) * 255.f / float(hi - lo)));
		out.pixels[i * 4] = out.pixels[i * 4 + 1] = out.pixels[i * 4 + 2] = v;
		out.pixels[i * 4 + 3] = std::byte(255);
	}
	return out;
}

unsigned aiMargin(unsigned width, unsigned height) { return std::clamp(std::min(width, height) / 16u, 4u, 32u); }

UpscaleResult upscalePicture(const ImageRgba& image, unsigned width, unsigned height, UpscaleMethod method, double sharpenAmount, EdgeMode edgesU, EdgeMode edgesV,
                             const AiUpscaler* ai, const std::atomic<bool>* cancel) {
	UpscaleResult result;
	if (!validImage(image)) return result;
	width = clampSize(width);
	height = clampSize(height);
	const ImageAlpha alpha = alphaOf(image);
	const bool larger = width > image.width || height > image.height;
	if (method == UpscaleMethod::Ai && larger) {
		if (!ai || !*ai) {
			result.note = "no AI upscaler next to Mapsmith";
		} else {
			const unsigned margin = aiMargin(image.width, image.height);
			const ImageRgba padded = padPicture(image, margin, edgesU, edgesV);
			std::string error;
			auto big = (*ai)(padded, 4, cancel, &error);
			if (big && big->width == padded.width * 4 && big->height == padded.height * 4 && validImage(*big)) {
				ImageRgba inner = cropPicture(*big, margin * 4, margin * 4, unsigned(image.width) * 4, unsigned(image.height) * 4);
				if (inner.width != width || inner.height != height) inner = resizeLanczos(inner, width, height, edgesU, edgesV);
				if (alpha != ImageAlpha::Opaque) {
					// The alpha on its own (never through the AI, never sharpened).
					const ImageRgba alphaScaled = resizeLanczos(image, width, height, edgesU, edgesV);
					for (std::size_t i = 3; i < inner.pixels.size(); i += 4) inner.pixels[i] = alphaScaled.pixels[i];
				} else {
					for (std::size_t i = 3; i < inner.pixels.size(); i += 4) inner.pixels[i] = std::byte(255);
				}
				if (alpha == ImageAlpha::Binary) thresholdAlpha(inner);
				result.image = std::move(inner);
				result.usedAi = true;
				return result;
			}
			if (cancel && cancel->load()) return result;
			result.note = error.empty() ? "the AI upscaler gave no picture" : error;
		}
	}
	result.image = resizeLanczos(image, width, height, edgesU, edgesV);
	if (larger) sharpen(result.image, sharpenAmount, edgesU, edgesV);
	if (alpha == ImageAlpha::Binary) thresholdAlpha(result.image);
	return result;
}

// --- Material text --------------------------------------------------------------------------

std::string mappingTransformText(double scaleU, double scaleV) {
	return "center 0 0 scale " + numberText(scaleU) + " " + numberText(scaleV) + " rotate 0 translate 0 0";
}

std::pair<double, double> mappingScale(const Material& material) {
	if (const auto scale = ownTransformScale(material.param("$basetexturetransform"))) return *scale;
	return {1.0, 1.0};
}

bool onlyMappingTransforms(const Material& material) {
	std::optional<std::pair<double, double>> seen;
	for (const auto key : TRANSFORM_KEYS) {
		const std::string value = material.param(key);
		if (value.empty()) continue;
		const auto own = ownTransformScale(value);
		if (!own) {
			if (isIdentityTransform(value)) continue;
			return false;
		}
		if (seen && *seen != *own) return false;
		seen = own;
	}
	return true;
}

namespace {

/// The keys a record puts on a material (removals for unlayer: nullopt).
std::vector<MaterialEdit> layerEdits(const EnhancedMaterial& record, const Material& material) {
	std::vector<MaterialEdit> edits;
	for (const auto& [key, name] : record.renamed) edits.push_back({key, name});
	if (record.scaleU != 1.0 || record.scaleV != 1.0) {
		const std::string t = mappingTransformText(record.scaleU, record.scaleV);
		edits.push_back({"$basetexturetransform", t});
		const auto has = [&](std::string_view key) { return !material.param(key).empty(); };
		if (has("$bumpmap") || !record.bumpName.empty()) edits.push_back({"$bumptransform", t});
		if (has("$envmapmask") || !record.maskName.empty()) edits.push_back({"$envmapmasktransform", t});
		if (has("$basetexture2")) edits.push_back({"$basetexturetransform2", t});
		if (has("$bumpmap2")) edits.push_back({"$bumptransform2", t});
		if (has("$blendmodulatetexture")) edits.push_back({"$blendmasktransform", t});
	}
	if (!record.bumpName.empty()) edits.push_back({"$bumpmap", record.bumpName});
	if (!record.maskName.empty()) edits.push_back({"$envmapmask", record.maskName});
	return edits;
}

} // namespace

std::string layerMaterialText(std::string_view vmt, const EnhancedMaterial& record, const Material* resolved) {
	std::optional<Material> parsed;
	if (!resolved) {
		parsed = parseMaterial(splitMarker(vmt).second, record.material);
		resolved = parsed ? &*parsed : nullptr;
	}
	if (!resolved) return std::string(vmt);
	return editKeepingMarker(vmt, layerEdits(record, *resolved), resolved);
}

std::string unlayerMaterialText(std::string_view vmt, const EnhancedMaterial& record, const Material* resolved) {
	std::optional<Material> parsed;
	if (!resolved) {
		parsed = parseMaterial(splitMarker(vmt).second, record.material);
		resolved = parsed ? &*parsed : nullptr;
	}
	if (!resolved) return std::string(vmt);
	std::vector<MaterialEdit> edits;
	for (const auto& [key, name] : record.renamed) {
		const std::string value = normalizeMaterialName(resolved->param(key));
		if (iequals(value, name) && endsWithI(name, HD_TEXTURE_SUFFIX)) {
			edits.push_back({key, name.substr(0, name.size() - HD_TEXTURE_SUFFIX.size())});
		}
	}
	for (const auto key : TRANSFORM_KEYS) {
		if (ownTransformScale(resolved->param(key))) edits.push_back({std::string(key), std::nullopt});
	}
	if (!record.bumpName.empty() && iequals(normalizeMaterialName(resolved->param("$bumpmap")), record.bumpName)) edits.push_back({"$bumpmap", std::nullopt});
	if (!record.maskName.empty() && iequals(normalizeMaterialName(resolved->param("$envmapmask")), record.maskName)) edits.push_back({"$envmapmask", std::nullopt});
	if (edits.empty()) return std::string(vmt);
	return editKeepingMarker(vmt, edits, resolved);
}

bool hasLayer(std::string_view vmt, const EnhancedMaterial& record) {
	const auto parsed = parseMaterial(splitMarker(vmt).second, record.material);
	if (!parsed) return false;
	for (const auto& [key, name] : record.renamed) {
		if (!iequals(normalizeMaterialName(parsed->param(key)), name)) return false;
	}
	if ((record.scaleU != 1.0 || record.scaleV != 1.0) && !ownTransformScale(parsed->param("$basetexturetransform"))) return false;
	if (!record.bumpName.empty() && !iequals(normalizeMaterialName(parsed->param("$bumpmap")), record.bumpName)) return false;
	return true;
}

std::optional<std::string> enhancedOriginal(const GameFileSystem& fs, const fs::path& modDir, const TextureManifest& manifest, std::string_view material) {
	auto original = fs.readOriginal(vmtOf(material), modDir);
	if (!original) return std::nullopt;
	const EnhancedMaterial* record = manifest.find(material);
	if (!record) return original;
	const auto resolved = materialOfText(fs, *original, material);
	if (!resolved) return original;
	return layerMaterialText(*original, *record, &*resolved);
}

// --- The record -----------------------------------------------------------------------------

const EnhancedMaterial* TextureManifest::find(std::string_view material) const {
	for (const auto& m : materials) {
		if (iequals(m.material, material)) return &m;
	}
	return nullptr;
}

void TextureManifest::put(EnhancedMaterial record) {
	for (auto& m : materials) {
		if (iequals(m.material, record.material)) {
			m = std::move(record);
			return;
		}
	}
	materials.push_back(std::move(record));
	std::sort(materials.begin(), materials.end(), [](const auto& a, const auto& b) { return a.material < b.material; });
}

bool TextureManifest::erase(std::string_view material) {
	return std::erase_if(materials, [&](const auto& m) { return iequals(m.material, material); }) > 0;
}

fs::path textureManifestFile(const fs::path& modDir) { return modDir / "cfg" / "mapsmith_textures.txt"; }

TextureManifest parseTextureManifest(std::string_view text) {
	TextureManifest manifest;
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return manifest;
	}
	const KvNode* top = nullptr;
	for (const auto& child : root.children) {
		if (child.block && iequals(child.key, "mapsmith_textures")) {
			top = &child;
			break;
		}
	}
	if (!top) return manifest;
	const auto safeTexture = [](std::string_view name) {
		const std::string n = normalizeMaterialName(name);
		return !n.empty() && isSafeGamePath("materials/" + n + ".vtf") ? n : std::string();
	};
	for (const auto& entry : top->children) {
		if (!entry.block || manifest.materials.size() >= 100000) continue;
		EnhancedMaterial m;
		m.material = normalizeMaterialName(entry.key);
		if (m.material.empty() || !isSafeGamePath(vmtOf(m.material))) continue;
		std::istringstream scale(entry.get("scale", "1 1"));
		scale >> m.scaleU >> m.scaleV;
		if (scale.fail() || !(m.scaleU > 0 && m.scaleU <= 64) || !(m.scaleV > 0 && m.scaleV <= 64)) m.scaleU = m.scaleV = 1;
		m.ownCopy = entry.get("copy") == "1";
		m.bumpName = safeTexture(entry.get("bump"));
		m.maskName = safeTexture(entry.get("mask"));
		for (const auto& kv : entry.children) {
			if (kv.block && iequals(kv.key, "rename")) {
				for (const auto& r : kv.children) {
					const std::string key = toLower(r.key);
					const std::string name = safeTexture(r.value);
					if (r.block || name.empty() || !key.starts_with("$")) continue;
					m.renamed[key] = name;
				}
			} else if (!kv.block && iequals(kv.key, "file")) {
				const std::string path = normalizePath(kv.value);
				if (ownFilePath(path) && endsWithI(path, ".vtf")) m.files.push_back(toLower(path));
			}
		}
		manifest.put(std::move(m));
	}
	return manifest;
}

std::string writeTextureManifest(const TextureManifest& manifest) {
	KvNode root;
	KvNode top = KvNode::makeBlock("mapsmith_textures");
	top.set("version", "1");
	for (const auto& m : manifest.materials) {
		KvNode entry = KvNode::makeBlock(m.material);
		entry.set("scale", numberText(m.scaleU) + " " + numberText(m.scaleV));
		entry.set("copy", m.ownCopy ? "1" : "0");
		if (!m.bumpName.empty()) entry.set("bump", m.bumpName);
		if (!m.maskName.empty()) entry.set("mask", m.maskName);
		if (!m.renamed.empty()) {
			KvNode rename = KvNode::makeBlock("rename");
			for (const auto& [key, name] : m.renamed) rename.set(key, name);
			entry.children.push_back(std::move(rename));
		}
		for (const auto& file : m.files) entry.children.push_back(KvNode::makeValue("file", file));
		top.children.push_back(std::move(entry));
	}
	root.children.push_back(std::move(top));
	return "// Mapsmith: textures made larger, bump maps and reflection masks it added (Materials > Enhance textures).\r\n"
	       "// \"Remove enhancements\" in the same menu deletes these files and takes the keys out of the materials again.\r\n" +
	       writeKeyValues(root);
}

TextureManifest loadTextureManifest(const fs::path& modDir) {
	const auto text = readText(textureManifestFile(modDir));
	return text ? parseTextureManifest(*text) : TextureManifest{};
}

bool saveTextureManifest(const fs::path& modDir, const TextureManifest& manifest) {
	const fs::path file = textureManifestFile(modDir);
	if (manifest.materials.empty()) {
		std::error_code ec;
		fs::remove(file, ec);
		return !ec;
	}
	return writeAtomicText(file, writeTextureManifest(manifest));
}

// --- Plan -----------------------------------------------------------------------------------

bool MaterialEnhancement::needsMaterialText() const {
	if (!skipped.empty()) return false;
	if (scaleU != 1.0 || scaleV != 1.0 || !bumpName.empty() || !maskName.empty()) return true;
	return std::any_of(textures.begin(), textures.end(), [](const EnhanceTexture& t) { return t.target != t.source; });
}

std::uint64_t MaterialEnhancement::bytesEstimate() const {
	std::uint64_t total = 0;
	for (const auto& t : textures) {
		const std::uint64_t texels = std::uint64_t(t.toWidth) * t.toHeight;
		total += t.normalMap ? texels * 4 * 4 / 3 : texels * 4 / 3;   // BGRA8888 vs DXT5 (1 byte per texel), with mipmaps
	}
	if (!bumpName.empty()) {
		const auto& base = textures.empty() ? EnhanceTexture{} : textures.front();
		const std::uint64_t w = std::min(base.toWidth ? base.toWidth : 512u, GENERATED_NORMAL_MAX), h = std::min(base.toHeight ? base.toHeight : 512u, GENERATED_NORMAL_MAX);
		total += w * h * 4 * 4 / 3;
	}
	if (!maskName.empty()) total += std::uint64_t(512) * 512 * 4 / 3;
	return total;
}

namespace {

/// What a texture needs to be made larger (empty reason) or why it cannot.
struct TextureCheck {
	std::string reason;
	TextureInfo info;
};

TextureCheck checkTexture(const GameFileSystem& fs, std::string_view texture) {
	TextureCheck out;
	if (texture.empty() || !isSafeGamePath(vtfOf(texture))) {
		out.reason = "no texture";
		return out;
	}
	const auto data = fs.read(vtfOf(texture));
	if (!data) {
		out.reason = "texture " + std::string(texture) + " not found";
		return out;
	}
	const auto info = readTextureInfo(*data);
	if (!info) {
		out.reason = "texture " + std::string(texture) + " is broken";
		return out;
	}
	out.info = *info;
	if (info->frameCount > 1) out.reason = "an animated texture (" + std::to_string(info->frameCount) + " frames)";
	else if (info->faceCount > 1 || (info->flags & 0x4000u)) out.reason = "a cube map";
	else if (info->flags & (0x800u | 0x8000u | 0x10000u)) out.reason = "a texture the game makes itself";
	else if (info->flags & 0x1u) out.reason = "pixel art (point sampled)";
	else if (info->flags & 0x100u) out.reason = "no mipmaps (an interface texture)";
	else if (!isPowerOfTwo(info->width) || !isPowerOfTwo(info->height)) out.reason = "its size is not a power of two";
	return out;
}

/// The size a texture gets: `factor` times, halved until it fits `maxSize`.
std::pair<unsigned, unsigned> targetSize(unsigned w, unsigned h, unsigned factor, unsigned maxSize) {
	maxSize = std::clamp(maxSize, 64u, ENHANCE_MAX_SIZE);
	unsigned f = std::max(1u, factor);
	while (f > 1 && std::max(w, h) * f > maxSize) f /= 2;
	return {w * f, h * f};
}

} // namespace

MaterialEnhancement planEnhancement(const GameFileSystem& fs, const fs::path& modDir, std::string_view materialName, const EnhanceOptions& options,
                                    const TextureManifest* knownManifest, const MaterialDecisions* knownDecisions) {
	MaterialEnhancement plan;
	plan.material = normalizeMaterialName(materialName);
	const std::string& name = plan.material;
	if (name.empty() || !isSafeGamePath(vmtOf(name))) {
		plan.skipped = "not a material name";
		return plan;
	}
	// Which text the material has now, and which one Mapsmith's copy would start from.
	const fs::path modFile = modDir / "materials" / toNativePath(name + ".vmt");
	std::optional<std::string> own = fileExists(modFile) ? readText(modFile) : std::nullopt;
	std::optional<std::string> baseline;
	bool handWritten = false;
	if (own) {
		const auto [marker, body] = splitMarker(*own);
		(void)body;
		if (marker.starts_with(TEXTURE_ENHANCE_MARKER)) {
			plan.file = MaterialEnhancement::File::Ours;
			baseline = fs.readOriginal(vmtOf(name), modDir);
		} else if (isGeneratedMaterial(*own)) {
			plan.file = MaterialEnhancement::File::Generated;
			baseline = own;
		} else {
			handWritten = true;
			baseline = own;
		}
	} else {
		baseline = fs.readOriginal(vmtOf(name), modDir);
	}
	if (!baseline) {
		plan.skipped = "material not found";
		return plan;
	}
	// The record of an earlier run: plan from the material without it.
	const TextureManifest readManifest = knownManifest ? TextureManifest{} : loadTextureManifest(modDir);
	const TextureManifest& manifest = knownManifest ? *knownManifest : readManifest;
	const EnhancedMaterial* earlier = manifest.find(name);
	std::string text = *baseline;
	if (earlier && plan.file == MaterialEnhancement::File::Generated) text = unlayerMaterialText(text, *earlier);
	auto material = materialOfText(fs, text, name);
	if (!material) {
		plan.skipped = "the material cannot be read";
		return plan;
	}
	plan.shader = material->shader;
	if (material->isTool()) {
		plan.skipped = "a tool material";
		return plan;
	}
	for (std::string_view prefix : {"vgui/", "hud/", "console/", "debug/", "effects/", "sprites/", "particle/", "particles/", "overlays/"}) {
		if (startsWithI(name, prefix)) {
			plan.skipped = "an interface or effect material";
			return plan;
		}
	}
	if (material->flag("$decal") || startsWithI(name, "decals/") || !material->param("$decalscale").empty()) {
		plan.skipped = "a decal (its size in the game comes from the texture's size)";
		return plan;
	}
	const ShaderUse use = shaderUse(material->shader, name);
	if (use == ShaderUse::Other) {
		plan.skipped = "shader " + material->shader + " is left as it is";
		return plan;
	}
	const bool seamless = material->flag("$seamless_scale");
	plan.kind = (use == ShaderUse::World && !seamless) ? EnhanceKind::World : EnhanceKind::Model;
	if (plan.kind == EnhanceKind::World) {
		for (const auto key : TRANSFORM_KEYS) {
			const std::string value = material->param(key);
			if (!value.empty() && !isIdentityTransform(value) && !ownTransformScale(value)) {
				plan.skipped = "it has its own texture transform (" + std::string(key) + ")";
				return plan;
			}
		}
	}
	const std::string base = normalizeMaterialName(material->param("$basetexture"));
	const TextureCheck baseCheck = checkTexture(fs, base);
	if (base.empty()) {
		plan.skipped = "no base texture";
		return plan;
	}
	if (!baseCheck.reason.empty()) {
		plan.skipped = baseCheck.reason;
		return plan;
	}
	// The textures that get larger: base textures always, own bump maps on request.
	const auto modHasOtherFile = [&](std::string_view texture) {
		const std::string path = toLower(vtfOf(texture));
		if (!fileExists(modDir / toNativePath(path))) return false;
		for (const auto& m : manifest.materials) {
			if (std::find(m.files.begin(), m.files.end(), path) != m.files.end()) return false;
		}
		return true;
	};
	const auto addTexture = [&](const std::string& key, const std::string& source, const TextureInfo& info, bool normalMap, bool ssbump) {
		if (options.factor <= 1) return;
		const auto [w, h] = targetSize(info.width, info.height, options.factor, options.maxSize);
		if (w == info.width && h == info.height) return;
		EnhanceTexture t;
		t.key = key;
		t.source = source;
		// Models (0..1 coordinates): the larger texture at its own path, when the mod has no file
		// there of its own - every material using it gets it. World materials: a new name.
		const bool inPlace = plan.kind == EnhanceKind::Model && (startsWithI(source, "models/") || startsWithI(source, "skybox/")) && !modHasOtherFile(source);
		t.target = inPlace ? source : source + std::string(HD_TEXTURE_SUFFIX);
		t.fromWidth = info.width;
		t.fromHeight = info.height;
		t.toWidth = w;
		t.toHeight = h;
		t.flags = info.flags;
		t.normalMap = normalMap;
		t.ssbump = ssbump;
		plan.textures.push_back(std::move(t));
	};
	addTexture("$basetexture", base, baseCheck.info, false, false);
	if (const std::string second = normalizeMaterialName(material->param("$basetexture2")); !second.empty()) {
		if (const auto check = checkTexture(fs, second); check.reason.empty()) addTexture("$basetexture2", second, check.info, false, false);
	}
	const std::string bump = normalizeMaterialName(material->param("$bumpmap"));
	if (options.upscaleBumpMaps) {
		const bool ssbump = material->flag("$ssbump");
		if (!bump.empty()) {
			if (const auto check = checkTexture(fs, bump); check.reason.empty()) addTexture("$bumpmap", bump, check.info, true, ssbump || (check.info.flags & 0x8000000u));
		}
		if (const std::string bump2 = normalizeMaterialName(material->param("$bumpmap2")); !bump2.empty()) {
			if (const auto check = checkTexture(fs, bump2); check.reason.empty()) addTexture("$bumpmap2", bump2, check.info, true, ssbump || (check.info.flags & 0x8000000u));
		}
	}
	// The world coordinate scale follows the base texture.
	if (plan.kind == EnhanceKind::World && !plan.textures.empty() && plan.textures.front().key == "$basetexture") {
		plan.scaleU = double(plan.textures.front().toWidth) / double(plan.textures.front().fromWidth);
		plan.scaleV = double(plan.textures.front().toHeight) / double(plan.textures.front().fromHeight);
	}
	// A bump map where there is none - unless Game enhancements makes one (bump=generate) or the
	// decision file says off; materials that cannot show one are left without.
	const MaterialDecision decision = knownDecisions ? knownDecisions->lookup(name) : loadMaterialDecisions(modDir).lookup(name);
	const std::string shaderLower = toLower(material->shader);
	const bool canBump = shaderLower.find("lightmappedgeneric") != std::string::npos || shaderLower.find("worldvertextransition") != std::string::npos ||
	                     shaderLower.find("vertexlitgeneric") != std::string::npos || shaderLower.starts_with("fidelity_parallax");
	const bool generatedRelief = own && own->find(GENERATED_NORMAL_SUFFIX) != std::string::npos;
	if (options.bumpMaps && canBump && bump.empty() && material->param("$normalmap").empty() && !material->flag("$translucent") && !material->flag("$additive") &&
	    decision.bump.value_or(BumpChoice::Auto) == BumpChoice::Auto && !generatedRelief) {
		plan.bumpName = name + std::string(ENHANCE_BUMP_SUFFIX);
	}
	// A reflection mask where the material reflects without one.
	if (options.glossMasks && material->reflectsWhenDry() && material->param("$envmapmask").empty() && !material->flag("$basealphaenvmapmask") &&
	    !material->flag("$normalmapalphaenvmapmask") && !material->flag("$basemapalphaenvmapmask")) {
		plan.maskName = name + std::string(ENHANCE_MASK_SUFFIX);
	}
	if (plan.textures.empty() && plan.bumpName.empty() && plan.maskName.empty()) {
		plan.skipped = options.factor > 1 ? "already " + std::to_string(std::max(baseCheck.info.width, baseCheck.info.height)) + " px or larger (Max size)" : "nothing to do";
		return plan;
	}
	if (handWritten && plan.needsMaterialText()) {
		plan.skipped = "the mod's own material - Mapsmith never changes it";
		return plan;
	}
	return plan;
}

// --- Making ---------------------------------------------------------------------------------

namespace {

EdgeMode edgesOf(std::uint32_t flags, bool u) {
	if (u) return (flags & 0x4u) ? EdgeMode::Clamp : EdgeMode::Wrap;
	return (flags & 0x8u) ? EdgeMode::Clamp : EdgeMode::Wrap;
}

bool cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }

} // namespace

EnhanceOutput makeEnhancement(const GameFileSystem& fs, const fs::path& modDir, const MaterialEnhancement& plan, const EnhanceOptions& options,
                              const AiUpscaler* ai, const std::atomic<bool>* cancel) {
	EnhanceOutput out;
	out.material = plan.material;
	if (!plan.skipped.empty()) {
		out.error = plan.skipped;
		return out;
	}
	EnhancedMaterial& record = out.record;
	record.material = plan.material;
	record.scaleU = plan.scaleU;
	record.scaleV = plan.scaleV;
	ImageRgba baseSource;   // The original base texture (for the bump map and mask)
	for (const auto& t : plan.textures) {
		if (cancelled(cancel)) {
			out.error = "cancelled";
			return out;
		}
		const auto data = fs.read(vtfOf(t.source));
		std::string error;
		const auto image = data ? decodeTexture(*data, 0, &error) : std::nullopt;
		if (!image) {
			out.error = "texture " + t.source + " cannot be read" + (error.empty() ? "" : ": " + error);
			return out;
		}
		const EdgeMode eu = edgesOf(t.flags, true), ev = edgesOf(t.flags, false);
		ImageRgba larger;
		if (t.normalMap) {
			larger = resizeNormalMap(*image, t.toWidth, t.toHeight, t.ssbump, eu, ev);
		} else {
			auto up = upscalePicture(*image, t.toWidth, t.toHeight, options.method, options.sharpen, eu, ev, ai, cancel);
			if (cancelled(cancel)) {
				out.error = "cancelled";
				return out;
			}
			out.usedAi = out.usedAi || up.usedAi;
			if (!up.note.empty() && options.method == UpscaleMethod::Ai) out.notes.push_back(t.source + ": " + up.note + " - built-in scaling");
			larger = std::move(up.image);
		}
		if (t.key == "$basetexture") baseSource = *image;
		auto vtf = encodeTexture(larger, t.flags, t.normalMap, options.compress && !t.normalMap);
		if (!vtf) {
			out.error = "texture " + t.target + " could not be made";
			return out;
		}
		const std::string path = toLower(vtfOf(t.target));
		out.files.push_back({path, std::move(*vtf)});
		record.files.push_back(path);
		if (t.target != t.source) record.renamed[t.key] = t.target;
	}
	if (!plan.bumpName.empty() || !plan.maskName.empty()) {
		// The heights Game enhancements would take for this material (pattern or brightness), else the brightness.
		const auto vmt = fs.readOriginal(vmtOf(plan.material), modDir);
		const MaterialDecision decision = loadMaterialDecisions(modDir).lookup(plan.material);
		if (baseSource.pixels.empty()) {
			const auto material = vmt ? materialOfText(fs, *vmt, plan.material) : std::nullopt;
			if (material) {
				if (auto image = loadTexture(fs, normalizeMaterialName(material->param("$basetexture")), 1024)) baseSource = std::move(*image);
			}
		}
		const unsigned w = plan.textures.empty() ? baseSource.width : plan.textures.front().toWidth;
		const unsigned h = plan.textures.empty() ? baseSource.height : plan.textures.front().toHeight;
		if (!plan.bumpName.empty() && !cancelled(cancel)) {
			HeightMap heights;
			if (vmt) heights = reliefHeights(fs, plan.material, *vmt, decision, false, 1024).heights;
			if (heights.values.empty() && !baseSource.pixels.empty()) {
				ImageRgba small = baseSource;
				if (std::max(small.width, small.height) > 1024) {
					const double f = 1024.0 / std::max(small.width, small.height);
					small = resizeLanczos(small, unsigned(small.width * f), unsigned(small.height * f));
				}
				if (auto made = heightFromColor(small)) heights = std::move(*made);
			}
			const double strength = decision.bumpStrength ? std::clamp(*decision.bumpStrength, BUMP_STRENGTH_MIN, BUMP_STRENGTH_MAX) : options.bumpStrength;
			auto normal = heights.values.empty() ? std::vector<std::byte>{} : encodeGeneratedNormalVtf(heights, strength, w, h);
			if (!normal.empty()) {
				const std::string path = toLower(vtfOf(plan.bumpName));
				out.files.push_back({path, std::move(normal)});
				record.files.push_back(path);
				record.bumpName = plan.bumpName;
			} else {
				out.notes.push_back(plan.material + ": no bump map (no heights)");
			}
		}
		if (!plan.maskName.empty() && !baseSource.pixels.empty() && !cancelled(cancel)) {
			ImageRgba small = baseSource;
			if (std::max(small.width, small.height) > 512) {
				const double f = 512.0 / std::max(small.width, small.height);
				small = resizeLanczos(small, unsigned(small.width * f), unsigned(small.height * f));
			}
			if (auto vtf = encodeTexture(glossMaskFromColor(small), 0, false, options.compress)) {
				const std::string path = toLower(vtfOf(plan.maskName));
				out.files.push_back({path, std::move(*vtf)});
				record.files.push_back(path);
				record.maskName = plan.maskName;
			}
		}
	}
	if (cancelled(cancel)) {
		out.error = "cancelled";
		return out;
	}
	// The material text: Mapsmith's copy of the original, or the keys on a generated material.
	const bool needsText = record.scaleU != 1.0 || record.scaleV != 1.0 || !record.renamed.empty() || !record.bumpName.empty() || !record.maskName.empty();
	if (needsText) {
		const fs::path modFile = modDir / "materials" / toNativePath(plan.material + ".vmt");
		std::optional<std::string> source;
		if (plan.file == MaterialEnhancement::File::Generated) {
			source = readText(modFile);
			if (source) {
				// An earlier run's keys out first, then this run's on.
				if (const EnhancedMaterial* earlier = loadTextureManifest(modDir).find(plan.material)) *source = unlayerMaterialText(*source, *earlier);
			}
			record.ownCopy = false;
		} else {
			source = fs.readOriginal(vmtOf(plan.material), modDir);
			record.ownCopy = true;
		}
		if (!source) {
			out.error = "material text not found";
			return out;
		}
		const auto resolved = materialOfText(fs, *source, plan.material);
		std::string text = layerMaterialText(*source, record, resolved ? &*resolved : nullptr);
		if (record.ownCopy) {
			text = std::string(TEXTURE_ENHANCE_MARKER) + " from " + vmtOf(plan.material) + " (Mapsmith > Materials > Enhance textures; Remove enhancements takes it out)\r\n" +
			       splitMarker(text).second;
		}
		const std::string path = toLower(vmtOf(plan.material));
		out.files.push_back({path, std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data()) + text.size())});
	}
	return out;
}

std::optional<EnhancePreview> previewEnhancement(const GameFileSystem& fs, const MaterialEnhancement& plan, const EnhanceOptions& options, const AiUpscaler* ai,
                                                 const std::atomic<bool>* cancel, unsigned crop) {
	if (!plan.skipped.empty()) return std::nullopt;
	std::string base;
	EnhanceTexture info;
	for (const auto& t : plan.textures) {
		if (t.key == "$basetexture") {
			info = t;
			base = t.source;
		}
	}
	if (base.empty()) return std::nullopt;
	const auto data = fs.read(vtfOf(base));
	const auto image = data ? decodeTexture(*data) : std::nullopt;
	if (!image) return std::nullopt;
	const EdgeMode eu = edgesOf(info.flags, true), ev = edgesOf(info.flags, false);
	EnhancePreview preview;
	preview.fromWidth = info.fromWidth;
	preview.fromHeight = info.fromHeight;
	preview.toWidth = info.toWidth;
	preview.toHeight = info.toHeight;
	// The part of the texture shown: its middle, `crop` texels of the larger one (the AI needs only that part).
	crop = std::max(64u, crop);
	const unsigned factor = std::max(1u, info.toWidth / std::max(1u, info.fromWidth));
	const unsigned partW = std::min<unsigned>(image->width, std::max(16u, crop / factor));
	const unsigned partH = std::min<unsigned>(image->height, std::max(16u, crop / factor));
	const unsigned x = (image->width - partW) / 2, y = (image->height - partH) / 2;
	// The part with its real neighbours around it (the edges of the part are not the texture's edges).
	const unsigned margin = aiMargin(partW, partH);
	const ImageRgba wide = cropPicture(padPicture(*image, margin, eu, ev), x, y, partW + 2 * margin, partH + 2 * margin);
	auto up = upscalePicture(wide, (partW + 2 * margin) * factor, (partH + 2 * margin) * factor, options.method, options.sharpen, EdgeMode::Clamp, EdgeMode::Clamp, ai, cancel);
	if (cancelled(cancel) || up.image.pixels.empty()) return std::nullopt;
	preview.after = cropPicture(up.image, margin * factor, margin * factor, partW * factor, partH * factor);
	preview.usedAi = up.usedAi;
	preview.note = up.note;
	// Before: the same part as the game shows it now, only larger (smooth, no new detail).
	const ImageRgba part = cropPicture(*image, x, y, partW, partH);
	ImageRgba before;
	before.width = std::uint16_t(partW * factor);
	before.height = std::uint16_t(partH * factor);
	before.pixels.resize(std::size_t(before.width) * before.height * 4);
	for (unsigned row = 0; row < before.height; ++row) {
		for (unsigned col = 0; col < before.width; ++col) {
			// Bilinear, as the game's texture filter shows a small texture close up.
			const double sx = (col + 0.5) / factor - 0.5, sy = (row + 0.5) / factor - 0.5;
			const int x0 = std::clamp(int(std::floor(sx)), 0, int(partW) - 1), y0 = std::clamp(int(std::floor(sy)), 0, int(partH) - 1);
			const int x1 = std::min(x0 + 1, int(partW) - 1), y1 = std::min(y0 + 1, int(partH) - 1);
			const double fx = std::clamp(sx - x0, 0.0, 1.0), fy = std::clamp(sy - y0, 0.0, 1.0);
			for (int c = 0; c < 4; ++c) {
				const auto p = [&](int px, int py) { return double(std::to_integer<int>(part.pixels[(std::size_t(py) * partW + std::size_t(px)) * 4 + std::size_t(c)])); };
				const double v = (p(x0, y0) * (1 - fx) + p(x1, y0) * fx) * (1 - fy) + (p(x0, y1) * (1 - fx) + p(x1, y1) * fx) * fy;
				before.pixels[(std::size_t(row) * before.width + col) * 4 + std::size_t(c)] = std::byte(toByte(float(v)));
			}
		}
	}
	preview.before = std::move(before);
	return preview;
}

// --- Writing ---------------------------------------------------------------------------------

std::vector<std::string> writeEnhancements(const fs::path& modDir, const std::vector<EnhanceOutput>& outputs, std::vector<std::string>* problems, GameFileSystem* noteFs) {
	std::vector<std::string> written;
	TextureManifest manifest = loadTextureManifest(modDir);
	std::set<std::string> ours;
	for (const auto& m : manifest.materials) ours.insert(m.files.begin(), m.files.end());
	const auto problem = [&](std::string text) {
		if (problems) problems->push_back(std::move(text));
	};
	for (const auto& output : outputs) {
		if (!output.error.empty() || output.files.empty()) continue;
		// Every file first: one Mapsmith may not write stops the whole material.
		bool ok = true;
		for (const auto& file : output.files) {
			if (!ownFilePath(file.path)) {
				problem(output.material + ": unsafe file name " + file.path);
				ok = false;
				break;
			}
			const fs::path target = modDir / toNativePath(file.path);
			if (!fileExists(target) || ours.count(file.path)) continue;
			if (endsWithI(file.path, ".vmt")) {
				const auto text = readText(target);
				if (text && (std::string_view(*text).starts_with(TEXTURE_ENHANCE_MARKER) || isGeneratedMaterial(*text))) continue;
			}
			problem(output.material + ": the mod has its own " + file.path + " - left as it is");
			ok = false;
			break;
		}
		if (!ok) continue;
		// Files of an earlier run this one no longer makes.
		if (const EnhancedMaterial* earlier = manifest.find(output.material)) {
			for (const auto& old : earlier->files) {
				if (std::find(output.record.files.begin(), output.record.files.end(), old) != output.record.files.end() || !ownFilePath(old)) continue;
				std::error_code ec;
				const fs::path file = modDir / toNativePath(old);
				if (fs::remove(file, ec) && noteFs) noteFs->noteRemovedFile(file);
			}
		}
		for (const auto& file : output.files) {
			const fs::path target = modDir / toNativePath(file.path);
			if (!writeAtomic(target, file.bytes)) {
				problem(output.material + ": " + file.path + " could not be written");
				ok = false;
				continue;
			}
			if (noteFs) noteFs->noteWrittenFile(target);
		}
		manifest.put(output.record);
		ours.insert(output.record.files.begin(), output.record.files.end());
		if (ok) written.push_back(output.material);
	}
	if (!saveTextureManifest(modDir, manifest)) problem("cfg/mapsmith_textures.txt could not be written");
	return written;
}

int removeEnhancements(const GameFileSystem& fs, const fs::path& modDir, const std::vector<std::string>& materials, std::vector<std::string>* problems, GameFileSystem* noteFs) {
	TextureManifest manifest = loadTextureManifest(modDir);
	std::vector<EnhancedMaterial> chosen;
	for (const auto& m : manifest.materials) {
		if (materials.empty() || std::any_of(materials.begin(), materials.end(), [&](const std::string& n) { return iequals(normalizeMaterialName(n), m.material); })) {
			chosen.push_back(m);
		}
	}
	int removed = 0;
	for (const auto& record : chosen) {
		for (const auto& path : record.files) {
			if (!ownFilePath(path) || !endsWithI(path, ".vtf")) continue;
			std::error_code ec;
			const fs::path file = modDir / toNativePath(path);
			if (fs::remove(file, ec) && noteFs) noteFs->noteRemovedFile(file);
		}
		const fs::path vmt = modDir / "materials" / toNativePath(record.material + ".vmt");
		if (const auto text = fileExists(vmt) ? readText(vmt) : std::nullopt) {
			if (std::string_view(*text).starts_with(TEXTURE_ENHANCE_MARKER)) {
				std::error_code ec;
				if (fs::remove(vmt, ec) && noteFs) noteFs->noteRemovedFile(vmt);
			} else if (isGeneratedMaterial(*text)) {
				// Game enhancements or deploy.bat made it from the enhanced original: the keys go.
				const auto resolved = materialOfText(fs, splitMarker(*text).second, record.material);
				const std::string plain = unlayerMaterialText(*text, record, resolved ? &*resolved : nullptr);
				if (plain != *text && !writeAtomicText(vmt, plain) && problems) problems->push_back(record.material + ": its material could not be written");
			}
		}
		manifest.erase(record.material);
		++removed;
	}
	if (!saveTextureManifest(modDir, manifest) && problems) problems->push_back("cfg/mapsmith_textures.txt could not be written");
	return removed;
}

std::vector<std::string> refreshEnhancementLayers(const GameFileSystem& fs, const fs::path& modDir, GameFileSystem* noteFs) {
	std::vector<std::string> fixed;
	TextureManifest manifest = loadTextureManifest(modDir);
	bool changed = false;
	for (auto& record : manifest.materials) {
		const bool needsText = record.scaleU != 1.0 || record.scaleV != 1.0 || !record.renamed.empty() || !record.bumpName.empty() || !record.maskName.empty();
		if (!needsText) continue;
		const fs::path vmt = modDir / "materials" / toNativePath(record.material + ".vmt");
		const auto text = fileExists(vmt) ? readText(vmt) : std::nullopt;
		std::string out;
		if (text) {
			if (!isGeneratedMaterial(*text) || hasLayer(*text, record)) continue;   // hand-written, or fine
			const auto resolved = materialOfText(fs, splitMarker(*text).second, record.material);
			out = layerMaterialText(*text, record, resolved ? &*resolved : nullptr);
		} else {
			// The generated material went (deploy.bat cleaned up): Mapsmith's own copy instead.
			const auto layered = enhancedOriginal(fs, modDir, manifest, record.material);
			if (!layered) continue;
			out = std::string(TEXTURE_ENHANCE_MARKER) + " from " + vmtOf(record.material) +
			      " (Mapsmith > Materials > Enhance textures; Remove enhancements takes it out)\r\n" + *layered;
			record.ownCopy = true;
			changed = true;
		}
		if (writeAtomicText(vmt, out)) {
			if (noteFs) noteFs->noteWrittenFile(vmt);
			fixed.push_back(record.material);
		}
	}
	if (changed) saveTextureManifest(modDir, manifest);
	return fixed;
}

// --- The AI upscaler program ------------------------------------------------------------------

std::string realEsrganProgramName() {
#ifdef _WIN32
	return "realesrgan-ncnn-vulkan.exe";
#else
	return "realesrgan-ncnn-vulkan";
#endif
}

std::vector<std::string> realEsrganArguments(const fs::path& input, const fs::path& output, const fs::path& modelsDir) {
	return {"-i", input.string(), "-o", output.string(), "-n", std::string(REAL_ESRGAN_MODEL), "-s", "4", "-m", modelsDir.string(), "-f", "png"};
}

} // namespace sme
