// model_glow.cpp - Glowing parts of models: masks, VMT text, the animation script (see model_glow.h).

#include <sme/model_glow.h>

#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view BACKUP_SUFFIX = ".mapsmith-backup";
constexpr std::string_view KEY_GLOW = "$mapsmith_glow";            ///< "1" marks a Mapsmith glow
constexpr std::string_view KEY_COLOR = "$mapsmith_glowcolor";      ///< colour * strength (the proxies' source)
constexpr std::string_view KEY_PULSE = "$mapsmith_glowpulse";      ///< Sine's 0..1 result
constexpr std::string_view KEY_SETTINGS = "$mapsmith_glowsettings";
constexpr unsigned MAX_MASK_SIZE = 1024;   ///< The mask needs less detail than the colour

std::uint8_t at(const ImageRgba& image, std::size_t index) {
	return std::to_integer<std::uint8_t>(image.pixels[index]);
}

std::string number(double v) {
	std::ostringstream out;
	out.precision(4);
	out << (std::abs(v) < 1e-6 ? 0.0 : v);
	return out.str();
}

std::string vector3(double r, double g, double b) {
	return "[" + number(r) + " " + number(g) + " " + number(b) + "]";
}

/// Animation names as the settings and the script keep them: letters, digits, '_', '.', '-'.
std::string cleanAnimation(std::string_view name) {
	std::string out;
	for (const char c : trim(name)) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-') out += c;
	}
	return out;
}

ImageRgba greyImage(unsigned width, unsigned height) {
	ImageRgba out;
	out.width = static_cast<std::uint16_t>(width);
	out.height = static_cast<std::uint16_t>(height);
	out.pixels.assign(std::size_t(width) * height * 4, std::byte{255});
	return out;
}

void setGrey(ImageRgba& image, std::size_t texel, double value) {
	const auto v = std::byte(static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L)));
	image.pixels[texel * 4] = v;
	image.pixels[texel * 4 + 1] = v;
	image.pixels[texel * 4 + 2] = v;
	image.pixels[texel * 4 + 3] = std::byte{255};
}

std::optional<std::string> readLocalText(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return std::nullopt;
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

/// Writes through a temporary file next to the target (no half-written files).
bool writeFileBytes(const fs::path& file, std::span<const std::byte> data, std::string* error) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = file.string() + ".mapsmith-tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!out) {
			if (error) *error = "cannot write " + displayPath(file);
			return false;
		}
	}
	fs::rename(temp, file, ec);
	if (ec) {
		fs::remove(temp, ec);
		if (error) *error = "cannot write " + displayPath(file);
		return false;
	}
	return true;
}

/// Writes one file of the glow: a file already in the mod is kept once as <file>.mapsmith-backup.
bool writeGlowFile(ImportRecord& record, const fs::path& file, std::span<const std::byte> data, std::string* error) {
	std::error_code ec;
	if (fs::exists(file, ec)) {
		const fs::path backup = file.string() + std::string(BACKUP_SUFFIX);
		if (!fs::exists(backup, ec)) {
			fs::copy_file(file, backup, ec);
			if (ec) {
				if (error) *error = "cannot keep a backup of " + displayPath(file);
				return false;
			}
		}
		if (!writeFileBytes(file, data, error)) return false;
		record.replaced.push_back(file);
		return true;
	}
	if (!writeFileBytes(file, data, error)) return false;
	record.created.push_back(file);
	return true;
}

/// The material block of a parsed VMT (its first block), or nullptr.
KvNode* materialBlock(KvNode& root) {
	for (auto& child : root.children) {
		if (child.block) return &child;
	}
	return nullptr;
}

/// Whether a proxy block is one Mapsmith's glow wrote.
bool ownProxy(const KvNode& proxy) {
	if (iequals(proxy.key, "Sine")) return iequals(proxy.get("resultVar"), KEY_PULSE);
	if (iequals(proxy.key, "Multiply")) return iequals(proxy.get("srcVar1"), KEY_COLOR);
	if (iequals(proxy.key, "VScriptProxy")) return startsWithI(toLower(normalizePath(proxy.get("scriptfile"))), "mapsmith/glow/");
	return false;
}

std::string encodeSettings(const GlowSettings& s) {
	std::string anims;
	for (const auto& a : s.animations) {
		const auto clean = cleanAnimation(a);
		if (clean.empty()) continue;
		if (!anims.empty()) anims += ',';
		anims += clean;
	}
	const char* mask = s.mask == GlowSettings::Mask::Auto ? "auto" : s.mask == GlowSettings::Mask::Whole ? "whole" : "picture";
	return std::string("mask=") + mask + ";threshold=" + number(s.threshold) + ";color=" + number(s.red) + "," + number(s.green) + "," +
	       number(s.blue) + ";strength=" + number(s.strength) + ";pulse=" + (s.pulse ? "1" : "0") + ";period=" + number(s.pulseSeconds) +
	       ";low=" + number(s.pulseLow) + ";when=" + (s.when == GlowSettings::When::Always ? "always" : "animation") + ";anims=" + anims +
	       ";from=" + number(s.fromCycle) + ";to=" + number(s.toCycle) + ";fade=" + number(s.fadeCycle);
}

double toNumber(std::string_view text, double fallback) {
	try {
		std::size_t used = 0;
		const double v = std::stod(std::string(text), &used);
		return used > 0 && std::isfinite(v) ? v : fallback;
	} catch (const std::exception&) {
		return fallback;
	}
}

/// Limits of every value (also for settings read back from a file someone edited).
GlowSettings clamped(GlowSettings s) {
	s.threshold = std::clamp(s.threshold, 0.0, 1.0);
	s.red = std::clamp(s.red, 0.0, 1.0);
	s.green = std::clamp(s.green, 0.0, 1.0);
	s.blue = std::clamp(s.blue, 0.0, 1.0);
	s.strength = std::clamp(s.strength, 0.0, 8.0);
	s.pulseSeconds = std::clamp(s.pulseSeconds, 0.05, 60.0);
	s.pulseLow = std::clamp(s.pulseLow, 0.0, 1.0);
	s.fromCycle = std::clamp(s.fromCycle, 0.0, 1.0);
	s.toCycle = std::clamp(s.toCycle, s.fromCycle, 1.0);
	s.fadeCycle = std::clamp(s.fadeCycle, 0.0, 0.5);
	std::vector<std::string> anims;
	for (const auto& a : s.animations) {
		auto clean = cleanAnimation(a);
		if (!clean.empty() && anims.size() < 32) anims.push_back(std::move(clean));
	}
	s.animations = std::move(anims);
	return s;
}

bool modelShader(std::string_view shader) {
	const std::string s = toLower(shader);
	return s == "vertexlitgeneric" || s == "vertexlitgeneric_dx9" || s == "vertexlitgeneric_dx8" || s == "vertexlitgeneric_dx6" ||
	       s == "vertexlitgeneric_hdr_dx9";
}

} // namespace

ImageRgba autoGlowMask(const ImageRgba& base, double threshold) {
	const unsigned w = base.width, h = base.height;
	ImageRgba out = greyImage(w, h);
	if (w == 0 || h == 0 || base.pixels.size() < std::size_t(w) * h * 4) return out;
	threshold = std::clamp(threshold, 0.0, 1.0);
	std::vector<float> raw(std::size_t(w) * h, 0.f);
	for (std::size_t i = 0; i < raw.size(); ++i) {
		const double r = at(base, i * 4) / 255.0, g = at(base, i * 4 + 1) / 255.0, b = at(base, i * 4 + 2) / 255.0;
		const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
		const double saturation = hi > 0 ? (hi - lo) / hi : 0;
		// Lamps, screens and sights: bright and colourful, or almost white. A soft ramp of 0.08
		// above the threshold so the edge of a lamp fades instead of stepping.
		const bool candidate = saturation > 0.35 || hi > 0.92;
		if (!candidate) continue;
		raw[i] = static_cast<float>(std::clamp((hi - threshold) / 0.08, 0.0, 1.0));
	}
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			double sum = 0;
			int count = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int xx = int(x) + dx, yy = int(y) + dy;
					if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h)) continue;
					sum += raw[std::size_t(yy) * w + std::size_t(xx)];
					++count;
				}
			}
			// The blur widens the mask a little at the edges, the centre keeps its full value.
			const double centre = raw[std::size_t(y) * w + x];
			setGrey(out, std::size_t(y) * w + x, std::max(centre, count ? sum / count : 0.0));
		}
	}
	return out;
}

ImageRgba pictureGlowMask(const ImageRgba& picture, unsigned width, unsigned height) {
	ImageRgba out = greyImage(width, height);
	if (width == 0 || height == 0) return out;
	const unsigned pw = picture.width, ph = picture.height;
	if (pw == 0 || ph == 0 || picture.pixels.size() < std::size_t(pw) * ph * 4) {
		for (std::size_t i = 0; i < std::size_t(width) * height; ++i) setGrey(out, i, 0);
		return out;
	}
	const auto value = [&](unsigned x, unsigned y) {
		const std::size_t i = (std::size_t(y) * pw + x) * 4;
		const double lum = (0.299 * at(picture, i) + 0.587 * at(picture, i + 1) + 0.114 * at(picture, i + 2)) / 255.0;
		return lum * at(picture, i + 3) / 255.0;
	};
	for (unsigned y = 0; y < height; ++y) {
		const double fy = std::clamp((y + 0.5) * ph / height - 0.5, 0.0, double(ph - 1));
		const unsigned y0 = unsigned(fy), y1 = std::min(y0 + 1, ph - 1);
		const double ty = fy - y0;
		for (unsigned x = 0; x < width; ++x) {
			const double fx = std::clamp((x + 0.5) * pw / width - 0.5, 0.0, double(pw - 1));
			const unsigned x0 = unsigned(fx), x1 = std::min(x0 + 1, pw - 1);
			const double tx = fx - x0;
			const double v = (value(x0, y0) * (1 - tx) + value(x1, y0) * tx) * (1 - ty) + (value(x0, y1) * (1 - tx) + value(x1, y1) * tx) * ty;
			setGrey(out, std::size_t(y) * width + x, v);
		}
	}
	return out;
}

double glowCoverage(const ImageRgba& mask) {
	const std::size_t count = std::size_t(mask.width) * mask.height;
	if (count == 0 || mask.pixels.size() < count * 4) return 0;
	double sum = 0;
	for (std::size_t i = 0; i < count; ++i) sum += at(mask, i * 4) / 255.0;
	return sum / double(count);
}

std::string glowMaskName(std::string_view material) {
	return normalizeMaterialName(material) + "_glowmask";
}

std::string glowScriptName(std::string_view material) {
	std::string name = normalizeMaterialName(material);
	for (char& c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
	}
	return "mapsmith/glow/" + name + ".nut";
}

std::string glowMaterialText(std::string_view vmt, std::string_view material, const GlowSettings& input) {
	const GlowSettings s = clamped(input);
	KvNode root = parseKeyValues(vmt);
	KvNode* block = materialBlock(root);
	if (!block) throw std::runtime_error("the material has no shader block");
	// Take out an earlier Mapsmith glow (its keys and proxies) so a new one replaces it.
	std::erase_if(block->children, [](const KvNode& n) { return !n.block && startsWithI(toLower(n.key), std::string(KEY_GLOW)); });
	KvNode* proxies = nullptr;
	for (auto& child : block->children) {
		if (child.block && iequals(child.key, "Proxies")) proxies = &child;
	}
	if (proxies) {
		std::erase_if(proxies->children, [](const KvNode& p) { return p.block && ownProxy(p); });
	}

	const double r = s.red * s.strength, g = s.green * s.strength, b = s.blue * s.strength;
	block->set("$selfillum", "1");
	block->set("$selfillummask", glowMaskName(material));
	const bool animated = s.when == GlowSettings::When::DuringAnimation;
	block->set("$selfillumtint", animated ? "[0 0 0]" : vector3(r, g, b));
	block->set(std::string(KEY_GLOW), "1");
	block->set(std::string(KEY_COLOR), vector3(r, g, b));
	block->set(std::string(KEY_SETTINGS), encodeSettings(s));

	const auto addProxy = [&](KvNode proxy) {
		if (!proxies) {
			block->children.push_back(KvNode::makeBlock("Proxies"));
			proxies = &block->children.back();
		}
		proxies->children.push_back(std::move(proxy));
	};
	if (animated) {
		KvNode script = KvNode::makeBlock("VScriptProxy");
		script.set("scriptfile", glowScriptName(material));
		script.set("var0", "$selfillumtint");
		addProxy(std::move(script));
	} else if (s.pulse) {
		block->set(std::string(KEY_PULSE), "[1 1 1]");
		KvNode sine = KvNode::makeBlock("Sine");
		sine.set("sineMin", number(s.pulseLow));
		sine.set("sineMax", "1");
		sine.set("sinePeriod", number(s.pulseSeconds));
		sine.set("resultVar", std::string(KEY_PULSE));
		addProxy(std::move(sine));
		KvNode multiply = KvNode::makeBlock("Multiply");
		multiply.set("srcVar1", std::string(KEY_COLOR));
		multiply.set("srcVar2", std::string(KEY_PULSE));
		multiply.set("resultVar", "$selfillumtint");
		addProxy(std::move(multiply));
	}
	return writeKeyValues(root);
}

std::string glowScriptText(const GlowSettings& input) {
	const GlowSettings s = clamped(input);
	std::string anims;
	for (const auto& a : s.animations) {
		if (!anims.empty()) anims += ", ";
		anims += "\"" + toLower(a) + "\"";
	}
	const double r = s.red * s.strength, g = s.green * s.strength, b = s.blue * s.strength;
	std::string out;
	out += "// Written by Mapsmith (Glow ...): the material glows while the model plays one of the\n";
	out += "// animations below. Mapsmith writes this file again when the glow changes.\n";
	out += "// Called by the material's VScriptProxy on every draw; `entity` is the drawn entity.\n";
	out += "kAnimations <- [" + anims + "];\n";
	out += "kColor <- Vector(" + number(r) + ", " + number(g) + ", " + number(b) + ");\n";
	out += "kFrom <- " + number(s.fromCycle) + ";\n";
	out += "kTo <- " + number(s.toCycle) + ";\n";
	out += "kFade <- " + number(s.fadeCycle) + ";\n";
	out += std::string("kPulse <- ") + (s.pulse ? "true" : "false") + ";\n";
	out += "kPeriod <- " + number(s.pulseSeconds) + ";\n";
	out += "kLow <- " + number(s.pulseLow) + ";\n";
	out += "\n";
	out += "function GlowPlays(name)\n{\n";
	out += "\tif (name == null) return false;\n";
	out += "\tlocal lower = name.tolower();\n";
	out += "\tforeach (a in kAnimations) if (a == lower) return true;\n";
	out += "\treturn false;\n}\n\n";
	out += "function GlowAmount()\n{\n";
	out += "\tif (entity == null || !(\"GetSequence\" in entity)) return 0.0;\n";
	out += "\tlocal seq = entity.GetSequence();\n";
	out += "\tif (!GlowPlays(entity.GetSequenceActivityName(seq)) && !GlowPlays(entity.GetSequenceName(seq))) return 0.0;\n";
	out += "\tlocal c = entity.GetCycle();\n";
	out += "\tif (c < kFrom || c > kTo) return 0.0;\n";
	out += "\tlocal amount = 1.0;\n";
	out += "\tif (kFade > 0.0)\n\t{\n";
	out += "\t\tif (c - kFrom < kFade && kFrom > 0.0) amount = (c - kFrom) / kFade;\n";
	out += "\t\tif (kTo - c < kFade && kTo < 1.0) amount = amount < (kTo - c) / kFade ? amount : (kTo - c) / kFade;\n";
	out += "\t}\n";
	out += "\tif (kPulse) amount *= kLow + (1.0 - kLow) * (0.5 + 0.5 * sin(Time() * 6.2831853 / kPeriod));\n";
	out += "\treturn amount;\n}\n\n";
	out += "function OnBind()\n{\n";
	out += "\tself.SetVarVector(0, kColor * GlowAmount());\n";
	out += "}\n";
	return out;
}

std::optional<GlowSettings> readGlowSettings(const Material& material) {
	if (material.param(KEY_GLOW) != "1") return std::nullopt;
	GlowSettings s;
	const std::string text = material.param(KEY_SETTINGS);
	std::size_t start = 0;
	while (start <= text.size()) {
		const auto end = std::min(text.find(';', start), text.size());
		const std::string_view part = std::string_view(text).substr(start, end - start);
		const auto eq = part.find('=');
		if (eq != std::string_view::npos) {
			const std::string key = toLower(trim(part.substr(0, eq)));
			const std::string_view value = trim(part.substr(eq + 1));
			if (key == "mask") s.mask = iequals(value, "whole") ? GlowSettings::Mask::Whole : iequals(value, "picture") ? GlowSettings::Mask::Picture : GlowSettings::Mask::Auto;
			else if (key == "threshold") s.threshold = toNumber(value, s.threshold);
			else if (key == "strength") s.strength = toNumber(value, s.strength);
			else if (key == "pulse") s.pulse = value == "1";
			else if (key == "period") s.pulseSeconds = toNumber(value, s.pulseSeconds);
			else if (key == "low") s.pulseLow = toNumber(value, s.pulseLow);
			else if (key == "when") s.when = iequals(value, "animation") ? GlowSettings::When::DuringAnimation : GlowSettings::When::Always;
			else if (key == "from") s.fromCycle = toNumber(value, s.fromCycle);
			else if (key == "to") s.toCycle = toNumber(value, s.toCycle);
			else if (key == "fade") s.fadeCycle = toNumber(value, s.fadeCycle);
			else if (key == "color") {
				double c[3] = {1, 1, 1};
				std::size_t from = 0;
				for (int i = 0; i < 3 && from <= value.size(); ++i) {
					const auto comma = std::min(value.find(',', from), value.size());
					c[i] = toNumber(value.substr(from, comma - from), 1);
					from = comma + 1;
				}
				s.red = c[0];
				s.green = c[1];
				s.blue = c[2];
			} else if (key == "anims") {
				std::size_t from = 0;
				while (from < value.size()) {
					const auto comma = std::min(value.find(',', from), value.size());
					const auto name = cleanAnimation(value.substr(from, comma - from));
					if (!name.empty()) s.animations.push_back(name);
					from = comma + 1;
				}
			}
		}
		if (end >= text.size()) break;
		start = end + 1;
	}
	return clamped(s);
}

std::optional<GlowFiles> planGlow(const GameFileSystem& fs, std::string_view materialName, const GlowSettings& input, const ImageRgba* picture,
                                  std::string* error) {
	const auto fail = [&](std::string message) -> std::optional<GlowFiles> {
		if (error) *error = std::move(message);
		return std::nullopt;
	};
	const GlowSettings settings = clamped(input);
	const std::string name = normalizeMaterialName(materialName);
	if (name.empty() || !isSafeGamePath("materials/" + name + ".vmt")) return fail("not a material name: " + std::string(materialName));
	const auto vmt = fs.readText("materials/" + name + ".vmt");
	if (!vmt) return fail("cannot read materials/" + name + ".vmt");
	std::string parseError;
	const auto parsed = parseMaterial(*vmt, name, &parseError);
	if (!parsed) return fail("cannot read the material: " + parseError);
	if (iequals(parsed->shader, "patch")) return fail("patch materials (VMTs that include another) cannot glow yet");
	if (!modelShader(parsed->shader)) return fail("only model materials (VertexLitGeneric) can glow - this one uses " + parsed->shader);
	if (settings.when == GlowSettings::When::DuringAnimation && settings.animations.empty()) return fail("choose at least one animation");

	const std::string baseTexture = normalizePath(parsed->param("$basetexture"));
	if (baseTexture.empty()) return fail("the material has no $basetexture");
	const auto vtf = fs.read("materials/" + baseTexture + ".vtf");
	if (!vtf) return fail("cannot read materials/" + baseTexture + ".vtf");
	std::string textureError;
	const auto base = decodeTexture(*vtf, MAX_MASK_SIZE, &textureError);
	if (!base || base->width == 0 || base->height == 0) return fail("cannot read the base texture: " + textureError);

	ImageRgba mask;
	switch (settings.mask) {
	case GlowSettings::Mask::Auto: mask = autoGlowMask(*base, settings.threshold); break;
	case GlowSettings::Mask::Whole: mask = greyImage(base->width, base->height); break;
	case GlowSettings::Mask::Picture:
		if (!picture) return fail("choose a picture for the mask");
		mask = pictureGlowMask(*picture, base->width, base->height);
		break;
	}

	GlowFiles files;
	files.material = name;
	files.coverage = glowCoverage(mask);
	try {
		files.vmtText = glowMaterialText(*vmt, name, settings);
	} catch (const std::exception& e) {
		return fail(std::string("cannot change the material: ") + e.what());
	}
	std::string encodeError;
	auto encoded = encodeVtf(mask, true, false, MAX_MASK_SIZE, &encodeError);
	if (!encoded) return fail("cannot write the mask texture: " + encodeError);
	files.maskVtf = std::move(*encoded);
	if (settings.when == GlowSettings::When::DuringAnimation) files.scriptText = glowScriptText(settings);
	return files;
}

std::optional<ImportRecord> applyGlow(const fs::path& modDir, const GlowFiles& files, std::string* error) {
	const std::string name = normalizeMaterialName(files.material);
	if (name.empty() || !isSafeGamePath("materials/" + name + ".vmt")) {
		if (error) *error = "not a material name: " + files.material;
		return std::nullopt;
	}
	ImportRecord record;
	record.kind = "glow";
	record.asset = name;
	const auto bytes = [](std::string_view text) { return std::as_bytes(std::span(text.data(), text.size())); };
	if (!writeGlowFile(record, modDir / "materials" / (name + ".vmt"), bytes(files.vmtText), error) ||
	    !writeGlowFile(record, modDir / "materials" / (glowMaskName(name) + ".vtf"), files.maskVtf, error)) {
		(void)undoImport(record);
		return std::nullopt;
	}
	const fs::path script = modDir / "scripts" / "vscripts" / glowScriptName(name);
	if (!files.scriptText.empty()) {
		if (!writeGlowFile(record, script, bytes(files.scriptText), error)) {
			(void)undoImport(record);
			return std::nullopt;
		}
	} else {
		// A glow that no longer waits for an animation leaves no old script behind (it was ours).
		std::error_code ec;
		if (fs::exists(script, ec) && !fs::exists(script.string() + std::string(BACKUP_SUFFIX), ec)) {
			if (const auto old = readLocalText(script); old && startsWithI(*old, "// Written by Mapsmith")) fs::remove(script, ec);
		}
	}
	return record;
}

} // namespace sme
