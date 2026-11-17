// fidelity_effects.cpp - Game enhancements per model or material (see fidelity_effects.h).
// The material rules are those of mapbase-fidelity's deploy.ps1 (New-LiquidMaterial,
// New-WindMaterial, New-WaterMaterial); the output is the same text, line for line.

#include <sme/fidelity_effects.h>

#include <sme/asset_import.h>
#include <sme/assets.h>
#include <sme/container_models.h>
#include <sme/autosave.h>
#include <sme/heightmap.h>
#include <sme/texture.h>
#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/map_tools.h>
#include <sme/material_decisions.h>
#include <sme/material.h>
#include <sme/relief_pattern.h>
#include <sme/text.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>

namespace sme {

namespace {

const std::regex::flag_type ICASE = std::regex::ECMAScript | std::regex::icase;

// deploy.ps1's name rules (PowerShell -match: case-insensitive)
const std::regex BOTTLE_NAMES("(bottle|jug|jar|flask|vial|decanter|carafe|beaker|healthkit|medkit)", ICASE);
const std::regex MEDICAL_NAMES("(vial|medkit|health|syringe|medic)", ICASE);
const std::regex BUSH_NAMES("(bush|shrub|hedge|fern|ivy|plant|flower|grass|weed|cattail|vine)", ICASE);
constexpr float TINTED_BRIGHTNESS = 0.15f;   // texture brightness (linear) below which glass counts as dark

const char* const CRLF = "\r\n";

// deploy.ps1's relief rules (Get-ParallaxSource): $NotParallax, $ReliefNames, $MinimalNames (wood
// stays without relief unless -ParallaxWood) and Get-ParallaxBlocker.
const std::regex NOT_PARALLAX("^materials/(models|skybox|tools|decals|overlays|effects|sprites|console|vgui|dev|editor|particle|water|glass|liquids|nature/water)(/|$)",
                              ICASE);
const std::regex RELIEF_NAMES("(brick|stone|cobble|rock|tile|block|cinder|paver|slate|plank|roof|shingle|wood|board)", ICASE);
const std::regex WOOD_NAMES("(wood|plank|board)", ICASE);

/// Remove-VmtComments: without byte order mark and // comments, line ends \n.
std::string clean(std::string_view text) {
	std::string s(text);
	if (s.starts_with("\xEF\xBB\xBF")) s.erase(0, 3);
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
			while (i < s.size() && s[i] != '\n') ++i;
			if (i < s.size()) out += '\n';
			continue;
		}
		if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
		out += s[i];
	}
	return out;
}

/// The shader name (first word before the first {), or "".
std::string shaderOf(std::string_view vmt) {
	static const std::regex shader(R"re(^\s*"?([A-Za-z0-9_]+)"?\s*\{)re");
	const std::string c = clean(vmt);
	std::smatch m;
	return std::regex_search(c, m, shader) ? m[1].str() : std::string();
}

bool modelShader(const std::string& shader) {
	static const std::regex re("^(SDK_)?(VertexLitGeneric|UnlitGeneric)(_DX\\d+)?$", ICASE);
	return std::regex_match(shader, re);
}

/// Get-BaseTexture
std::string baseTexture(const std::string& cleanText) {
	static const std::regex quoted(R"re("?\$basetexture"?\s+"([^"]+)")re", ICASE);
	static const std::regex plain(R"re("?\$basetexture"?\s+(\S+))re", ICASE);
	std::smatch m;
	if (!std::regex_search(cleanText, m, quoted) && !std::regex_search(cleanText, m, plain)) return {};
	std::string texture = m[1].str();
	std::replace(texture.begin(), texture.end(), '\\', '/');
	return texture;
}

/// Get-LiquidColor: a colour that fits the name (gamma space), or "".
std::string liquidColor(std::string_view path) {
	const std::string p(path);
	const auto has = [&p](const char* pattern) { return std::regex_search(p, std::regex(pattern, ICASE)); };
	if (has("wine")) return "[0.32 0.03 0.07]";
	if (has("beer")) return "[0.62 0.38 0.06]";
	if (has("(whisk|liquor|alcohol|rum)")) return "[0.58 0.32 0.08]";
	if (has("milk")) return "[0.92 0.92 0.88]";
	if (has("oil")) return "[0.45 0.34 0.06]";
	return {};
}

const char* kindName(GameEffect effect) {
	return gameEffectInfo(effect).id;
}

/// The marker's first line: "<marker> (kind) from <path> (...)".
std::string headLine(std::string_view marker, GameEffect effect, std::string_view path) {
	const bool ours = marker == MAPSMITH_MARKER;
	return std::string(marker) + " (" + kindName(effect) + ") from " + std::string(path) +
	       (ours ? " (switch it off in Mapsmith to get the original back)" : " (delete this file to keep the original)");
}

std::string join(const std::vector<std::string>& lines) {
	std::string out;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i) out += CRLF;
		out += lines[i];
	}
	return out;
}

std::string param(const char* key, const std::string& value) {
	return std::string("\t\"") + key + "\" \"" + value + "\"";
}

/// New-LiquidMaterial (the container check is done by the caller: deployWouldApply / the mapper's switch).
std::optional<std::string> liquidMaterial(std::string_view path, std::string_view text, ContainerKind kind, std::string_view marker) {
	const std::string c = clean(text);
	if (!modelShader(shaderOf(text))) return std::nullopt;
	const std::string texture = baseTexture(c);
	std::vector<std::string> lines = {headLine(marker, GameEffect::Liquid, path), "\"Fidelity_Liquid\"", "{"};
	if (!texture.empty()) lines.push_back(param("$basetexture", texture));
	// Only see-through originals have meaningful alpha (glass vs. label).
	static const std::regex translucent(R"re("?\$translucent"?\s+"?1)re", ICASE), alpha(R"re("?\$alpha"?\s+"?0?\.\d)re", ICASE);
	const bool seeThrough = std::regex_search(c, translucent) || std::regex_search(c, alpha);
	if (!seeThrough) lines.push_back(param("$labelthreshold", "2"));
	lines.push_back(param("$liquidplane", "[0 0 1 -100000]"));   // set per prop by the proxy
	if (const auto color = liquidColor(path); !color.empty()) lines.push_back(param("$liquidcolor", color));
	switch (kind) {
	case ContainerKind::Medical:
		for (const auto& l : {param("$liquidkey", "[0.3 0.8 0.15]"), param("$fillmin", "0.82"), param("$fillmax", "0.95"),
		                      param("$bubbles", "[0.5 1.1 0.5 1]"), param("$viscosity", "0.7"), param("$liquidalpha", "0.95")}) {
			lines.push_back(l);
		}
		break;
	case ContainerKind::Tinted:
		for (const auto& l : {param("$tintedglass", "0.8"), param("$bubbles", "[0.3 0.3 4 0.2]"), param("$splash", "1")}) lines.push_back(l);
		break;
	case ContainerKind::Drink:
		for (const auto& l : {param("$bubbles", "[0.35 0.3 4 0.2]"), param("$splash", "1")}) lines.push_back(l);
		break;
	}
	if (kind != ContainerKind::Medical) {
		// the open green bottle: a rest, and it pours out when tipped over
		static const std::regex openBottle("glassbottle01", ICASE);
		if (std::regex_search(std::string(path), openBottle)) {
			for (const auto& l : {param("$fillmin", "0.02"), param("$fillmax", "0.25"), param("$spill", "1")}) lines.push_back(l);
		} else {
			for (const auto& l : {param("$fillmin", "0.45"), param("$fillmax", "0.95")}) lines.push_back(l);
		}
	}
	for (const char* l : {"\t\"Proxies\"", "\t{", "\t\t\"FidelityLiquid\" { }", "\t}", "}"}) lines.emplace_back(l);
	return join(lines) + CRLF;
}

const std::vector<std::string> SWAY_KEYS = {"$treesway", "$treeswayheight", "$treeswaystartheight", "$treeswayradius", "$treeswaystartradius",
                                            "$treeswayspeed", "$treeswaystrength", "$treeswayscrumblestrength", "$treeswayscrumblespeed",
                                            "$treeswayscrumblefrequency"};
const std::vector<std::string> BUSH_SWAY = {"1", "80", "0.1", "60", "0.1", "1.5", "2", "1", "3", "12"};
const std::vector<std::string> TREE_SWAY = {"1", "100000", "0.0005", "250", "0", "1", "2", "0.6", "2", "12"};

/// The see-through leaves, needles and twigs of a tree (not its bark).
bool leafMaterial(std::string_view path, const std::string& cleanText) {
	static const std::regex wood("(bark|trunk|stump|log)", ICASE), seeThrough(R"re("?\$(alphatest|translucent)"?\s+"?1)re", ICASE);
	if (std::regex_search(std::string(path), wood)) return false;
	return std::regex_search(cleanText, seeThrough);
}

/// New-WindMaterial's text for a VertexLitGeneric material with these sway values.
std::optional<std::string> windMaterial(std::string_view path, std::string_view text, const std::vector<std::string>& values, std::string_view marker) {
	std::string t(text);
	if (t.starts_with("\xEF\xBB\xBF")) t.erase(0, 3);
	static const std::regex head(R"re(^((?:\s*//[^\n]*\n)*\s*)"?(?:SDK_)?VertexLitGeneric"?(\s*\{))re", ICASE);
	std::smatch m;
	if (!std::regex_search(t, m, head)) return std::nullopt;
	std::vector<std::string> params;
	for (std::size_t i = 0; i < SWAY_KEYS.size(); ++i) params.push_back("\t\"" + SWAY_KEYS[i] + "\" \"" + values[i] + "\"");
	const std::string body = t.substr(std::size_t(m.position(0) + m.length(0)));
	return headLine(marker, GameEffect::Wind, path) + CRLF + "\"SDK_VertexLitGeneric\"" + m[2].str() + CRLF + join(params) + body;
}

/// New-WaterMaterial: the Water_DX9_HDR block's values replace the ones above it.
std::optional<std::string> waterMaterial(std::string_view path, std::string_view text, std::string_view marker) {
	const std::string c = clean(text);
	static const std::regex head(R"re(^\s*"?(?:SDK_)?Water"?(\s*\{))re", ICASE);
	std::smatch m;
	if (!std::regex_search(c, m, head)) return std::nullopt;
	static const std::regex aboveWater(R"re("?\$abovewater"?\s+"?1)re", ICASE);
	if (!std::regex_search(c, aboveWater)) return std::nullopt;
	std::string body = c.substr(std::size_t(m.position(0) + m.length(0)));
	static const std::regex hdr(R"re([ \t]*"?(?:SDK_)?Water_DX9_HDR"?\s*\{((?:"[^"]*"|[^{}"])*)\}[ \t]*\r?\n?)re", ICASE);
	std::vector<std::string> params;
	std::smatch block;
	if (std::regex_search(body, block, hdr)) {
		const std::string inside = block[1].str();
		body.erase(std::size_t(block.position(0)), std::size_t(block.length(0)));
		static const std::regex entry(R"re("?(\$[A-Za-z0-9_]+)"?[ \t]+("[^"\r\n]*"|[^\s"]+))re");
		for (auto it = std::sregex_iterator(inside.begin(), inside.end(), entry); it != std::sregex_iterator(); ++it) {
			const std::string key = (*it)[1].str();
			// the value above the block goes (wherever it stood)
			std::string kept;
			std::size_t start = 0;
			while (start < body.size()) {
				const std::size_t end = body.find('\n', start);
				const std::string line = body.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
				std::size_t p = line.find_first_not_of(" \t");
				bool drop = false;
				if (p != std::string::npos) {
					if (line[p] == '"') ++p;
					if (iequals(line.substr(p, key.size()), key)) {
						std::size_t q = p + key.size();
						if (q < line.size() && line[q] == '"') ++q;
						drop = q < line.size() && (line[q] == ' ' || line[q] == '\t');
					}
				}
				if (!drop) kept += line;
				if (end == std::string::npos) break;
				start = end + 1;
			}
			body = kept;
			params.push_back("\t\"" + key + "\" " + (*it)[2].str());
		}
	}
	std::string lines = params.empty() ? std::string() : CRLF + join(params);
	return headLine(marker, GameEffect::Water, path) + CRLF + "\"Water_Fidelity\"" + m[1].str() + lines + body;
}

std::string lowerPath(std::string_view path) {
	std::string p = toLower(path);
	std::replace(p.begin(), p.end(), '\\', '/');
	return p;
}

std::optional<std::string> readFile(const std::filesystem::path& file) {
	std::ifstream stream(file, std::ios::binary);
	if (!stream) return std::nullopt;
	std::ostringstream text;
	text << stream.rdbuf();
	return text.str();
}

std::string vmtPath(const std::string& material) {
	return "materials/" + material + ".vmt";
}

} // namespace

const std::vector<GameEffectInfo>& gameEffects() {
	static const std::vector<GameEffectInfo> effects = {
		{GameEffect::Liquid, "liquid", "Liquid inside", "Bottles, jars and vials hold liquid that sloshes, bubbles and pours out when tipped over.", true},
		{GameEffect::Wind, "wind", "Sway in the wind", "Leaves, bushes and plants move in the wind (only the leaves of trees).", true},
		{GameEffect::Water, "water", "Sun glint and foam", "The water reflects the sun and gets foam where it meets the shore.", false},
		{GameEffect::Relief, "relief", "Relief (real depth)", "Bricks, stones and tiles stand out of the wall as you walk past - not windows, signs or metal sheets.", false},
	};
	return effects;
}

const GameEffectInfo& gameEffectInfo(GameEffect effect) {
	for (const auto& info : gameEffects()) {
		if (info.effect == effect) return info;
	}
	return gameEffects().front();
}

bool isGeneratedMaterial(std::string_view vmt) {
	std::string_view first = vmt.substr(0, vmt.find('\n'));
	if (first.starts_with("\xEF\xBB\xBF")) first.remove_prefix(3);
	return first.starts_with(DEPLOY_MARKER) || first.starts_with(MAPSMITH_MARKER);
}

bool effectIsOn(GameEffect effect, std::string_view vmt) {
	const std::string shader = shaderOf(vmt);
	switch (effect) {
	case GameEffect::Liquid: return iequals(shader, "Fidelity_Liquid");
	case GameEffect::Water: return iequals(shader, "Water_Fidelity");
	case GameEffect::Relief: return startsWithI(shader, "Fidelity_Parallax") || startsWithI(shader, "Fidelity_WorldVertexTransition");
	case GameEffect::Wind: {
		static const std::regex sway(R"re("?\$treesway"?\s+"?([0-9]+))re", ICASE);
		std::smatch m;
		const std::string c = clean(vmt);
		return std::regex_search(c, m, sway) && m[1].str() != "0";
	}
	}
	return false;
}

bool effectApplies(GameEffect effect, std::string_view path, std::string_view vmt) {
	const std::string p = lowerPath(path);
	const std::string shader = shaderOf(vmt);
	const bool model = p.starts_with("materials/models/");
	switch (effect) {
	case GameEffect::Liquid: return model && (modelShader(shader) || iequals(shader, "Fidelity_Liquid"));
	case GameEffect::Wind: {
		static const std::regex vlg("^(SDK_)?VertexLitGeneric$", ICASE);
		return model && std::regex_match(shader, vlg);
	}
	// (Called for every material of the game: the patterns are built once.)
	case GameEffect::Water: {
		static const std::regex water("^(SDK_)?Water$", ICASE), aboveWater(R"re("?\$abovewater"?\s+"?1)re", ICASE);
		return iequals(shader, "Water_Fidelity") || (std::regex_match(shader, water) && std::regex_search(clean(vmt), aboveWater));
	}
	case GameEffect::Relief: {
		static const std::regex wall("^(SDK_)?(LightmappedGeneric|WorldVertexTransition)$", ICASE);
		return !model && (std::regex_match(shader, wall) || effectIsOn(GameEffect::Relief, vmt));
	}
	}
	return false;
}

namespace {

/// Get-ParallaxSource for a wall material (not the bullet hole decals): deploy.ps1 gives it relief
/// from its single bump map, or - without one - from its base texture when its path tells of
/// bricks, stones, tiles ...; never to see-through or otherwise blended walls, never to wood.
bool reliefByDeploy(const std::string& path, std::string_view vmt) {
	if (std::regex_search(path, NOT_PARALLAX)) return false;
	static const std::regex wall("^(SDK_)?(LightmappedGeneric|WorldVertexTransition)$", ICASE);
	if (!std::regex_match(shaderOf(vmt), wall)) return false;
	const std::string c = clean(vmt);
	static const std::regex blocker(R"re("?\$(alphatest|translucent|additive|decal|nodiffusebumplighting)"?\s+"?1)re", ICASE);
	static const std::regex blending(R"re(("?\$seamless_scale"?\s)|("?\$maskedblending"?\s+"?1))re", ICASE);
	static const std::regex detail(R"re("?\$detailblendmode"?\s+"?([2-9]|[1-9][0-9]))re", ICASE);
	static const std::regex bump(R"re("?\$bumpmap"?\s+"?([^"\s]+)"?)re", ICASE);
	static const std::regex base(R"re("?\$basetexture"?\s+"?([^"\s]+)"?)re", ICASE);
	const auto count = [&c](const std::regex& re) { return std::distance(std::sregex_iterator(c.begin(), c.end(), re), std::sregex_iterator()); };
	if (std::regex_search(c, blocker) || std::regex_search(c, blending) || std::regex_search(c, detail)) return false;
	std::smatch m;
	const std::string texture = std::regex_search(c, m, base) ? m[1].str() : std::string();
	if (std::regex_search(texture.empty() ? path : texture, WOOD_NAMES)) return false;   // Test-Wood
	const auto bumps = count(bump);
	if (bumps > 0) return bumps == 1;
	return std::regex_search(path, RELIEF_NAMES) && count(base) == 1;
}

} // namespace

bool deployWouldApply(GameEffect effect, std::string_view path, std::string_view vmt) {
	const std::string p = lowerPath(path);
	switch (effect) {
	case GameEffect::Liquid:
		return std::regex_search(p, BOTTLE_NAMES) && !p.starts_with("materials/models/props_foliage") && modelShader(shaderOf(vmt));
	case GameEffect::Wind: {
		if (!p.starts_with("materials/models/props_foliage")) return false;
		if (!effectApplies(GameEffect::Wind, p, vmt) || effectIsOn(GameEffect::Wind, vmt)) return false;
		return std::regex_search(p, BUSH_NAMES) || leafMaterial(p, clean(vmt));
	}
	case GameEffect::Water: return waterMaterial(p, vmt, DEPLOY_MARKER).has_value();
	case GameEffect::Relief: return reliefByDeploy(p, vmt);
	}
	return false;
}

bool modHasShaderDll(const std::filesystem::path& modDir) {
	std::error_code ec;
	return !modDir.empty() && std::filesystem::is_regular_file(modDir / std::filesystem::path(std::string(MOD_SHADER_DLL)), ec);
}

bool shaderNeedsModDll(std::string_view shader) {
	return startsWithI(shader, "Fidelity_") || iequals(shader, "Water_Fidelity");
}

bool effectNeedsShaderDll(GameEffect effect) {
	return effect != GameEffect::Wind;
}

std::vector<std::string> ownMaterialsNeedingShaderDll(const std::filesystem::path& modDir) {
	std::vector<std::string> out;
	const auto root = modDir / "materials";
	std::error_code ec;
	if (!std::filesystem::is_directory(root, ec)) return out;
	for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end;
	     it.increment(ec)) {
		if (!it->is_regular_file(ec) || !iequals(it->path().extension().string(), ".vmt")) continue;
		const auto text = readFile(it->path());
		if (!text || isGeneratedMaterial(*text) || !shaderNeedsModDll(shaderOf(*text))) continue;
		auto name = it->path().lexically_relative(root).generic_string();
		name = toLower(name.substr(0, name.size() - 4));
		out.push_back(std::move(name));
	}
	std::sort(out.begin(), out.end());
	return out;
}

namespace {

/// Bumped when the pattern relief changes (the hash makes the walls new).
/// (pattern2: shallow joints, rounded edges, depth 0.006 - Julius, 29.10.2026; pattern3: flat faces
/// again with a short bevel, windows divided by bars stay flat; pattern4: the depth follows the
/// pattern - fine bricks shallower, too fine ones none, 16.11.2026; pattern5: a bump map generated from
/// the same heights where the material has none, 17.11.2026)
constexpr std::string_view PATTERN_RULES = "pattern5";
/// deploy.ps1: -ParallaxDepth 0.01 x $CourseDepth 0.6 for walls laid in courses.
constexpr const char* PATTERN_DEPTH = "0.006";
/// The shader's default for the darkening of deep parts, written out: a bundle older than this
/// default still darkens as much.
constexpr const char* PATTERN_AO = "0.25";

std::string hexHash(std::uint64_t h) {
	char text[17];
	std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
	return text;
}

void fnv(std::uint64_t& h, const void* data, std::size_t size) {
	const auto* bytes = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i) {
		h ^= bytes[i];
		h *= 1099511628211ull;
	}
}

/// The base texture of a plain wall that can carry a pattern relief, or "" - LightmappedGeneric
/// only (blends keep deploy's two-layer relief), no texture transforms (the height map lies in
/// the base texture's coordinates, the shader reads it in the bump map's), relief that fits.
std::string patternWallTexture(std::string_view material, std::string_view originalVmt, bool force = false) {
	static const std::regex plain("^(SDK_)?LightmappedGeneric$", ICASE);
	if (!std::regex_match(shaderOf(originalVmt), plain)) return {};
	const std::string c = clean(originalVmt);
	static const std::regex moved(R"re("?\$(basetexturetransform|bumptransform|basetexture2|seamless_scale)"?\s)re", ICASE);
	if (std::regex_search(c, moved)) return {};
	const auto parsed = parseMaterial(originalVmt, material);
	if (!parsed) return {};
	const std::string base = baseTexture(c);
	if (force) {
		// The decision file says on: only what cannot carry relief at all stays out.
		if (base.empty()) return {};
		for (const char* flag : {"$alphatest", "$translucent", "$additive", "$decal"}) {
			if (parsed->flag(flag)) return {};
		}
		return base;
	}
	if (!fidelityReliefSource(material, *parsed)) {
		// Also walls whose name tells nothing (a concrete floor laid in slabs): there the pattern
		// decides - with the same blockers as everywhere else (see-through, decals, wood ...).
		if (base.empty() || std::regex_search(vmtPath(toLower(std::string(material))), NOT_PARALLAX)) return {};
		for (const char* flag : {"$alphatest", "$translucent", "$additive", "$decal", "$nodiffusebumplighting", "$maskedblending"}) {
			if (parsed->flag(flag)) return {};
		}
		if (const auto mode = parsed->param("$detailblendmode"); !mode.empty() && std::atoi(mode.c_str()) >= 2) return {};
		if (std::regex_search(base, WOOD_NAMES) || !reliefFitByName(material, base).suits) return {};
	}
	return base;
}

} // namespace

std::string patternReliefHash(const GameFileSystem& fs, std::string_view material, std::string_view originalVmt, const PatternReliefOptions& options) {
	const std::string texture = patternWallTexture(material, originalVmt, options.force);
	if (texture.empty()) return {};
	const auto bytes = fs.read("materials/" + toLower(texture) + ".vtf");
	if (!bytes) return {};
	std::uint64_t h = 1469598103934665603ull;
	fnv(h, PATTERN_RULES.data(), PATTERN_RULES.size());
	if (options.any() || options.faceScale != 0.25 || options.bump != BumpChoice::Auto || options.bumpStrength != 1.0) {
		// What the decision file says changes the relief, so it changes the hash (old files go).
		std::string said = std::string(options.force ? "force;" : "") + (options.noPattern ? "nopattern;" : "") +
		                   (options.depth ? "depth=" + std::to_string(*options.depth) + ";" : "") + "scale=" + std::to_string(options.faceScale);
		if (options.bump != BumpChoice::Auto || options.bumpStrength != 1.0) {
			said += std::string(";bump=") + bumpChoiceKey(options.bump) + ";strength=" + std::to_string(options.bumpStrength);
		}
		fnv(h, said.data(), said.size());
	}
	fnv(h, originalVmt.data(), originalVmt.size());
	fnv(h, bytes->data(), bytes->size());
	return hexHash(h);
}

namespace {

/// The heights and depth of a pattern relief, without the cache of makePatternRelief.
struct PatternHeights {
	std::string textureName;
	HeightMap heights;
	ReliefLayout layout;
	bool patternFound = false;
	std::string depthText = PATTERN_DEPTH;
	std::string depthNote;
	unsigned fullWidth = 0, fullHeight = 0;   ///< The base texture's own size
};

/// Nullopt: no pattern relief (`why`: a pattern too fine for one, "" for none at all).
std::optional<PatternHeights> patternHeights(const GameFileSystem& fs, std::string_view material, std::string_view originalVmt,
                                             const PatternReliefOptions& options, std::string* why, unsigned size = 512) {
	if (why) why->clear();
	PatternHeights out;
	out.textureName = patternWallTexture(material, originalVmt, options.force);
	if (out.textureName.empty()) return std::nullopt;
	const auto bytes = fs.read("materials/" + toLower(out.textureName) + ".vtf");
	if (!bytes) return std::nullopt;
	if (const auto info = readTextureInfo(*bytes)) {
		out.fullWidth = info->width;
		out.fullHeight = info->height;
	}
	const auto image = decodeTexture(*bytes, size);
	if (!image) return std::nullopt;
	std::optional<HeightMap> heights;
	if (options.noPattern) {
		heights = heightFromColor(*image);
	} else {
		heights = heightFromTexture(*image, &out.layout);
	}
	out.patternFound = out.layout.found() && out.layout.confidence >= 0.5;
	if (!heights || (!out.patternFound && !options.force)) return std::nullopt;   // deploy's relief stays
	out.heights = std::move(*heights);
	// The depth: the decision file's, else from the pattern (fine bricks shallower, too fine none).
	if (options.depth) {
		std::ostringstream d;
		d << *options.depth;
		out.depthText = d.str();
		out.depthNote = ", depth " + out.depthText + " from the decision file";
	} else if (out.patternFound) {
		const int fullHeight = int(out.layout.transposed ? out.fullWidth : out.fullHeight);
		const ReliefDepth rule = reliefDepthForLayout(out.layout, std::atof(PATTERN_DEPTH), fullHeight, options.faceScale);
		if (!rule.on) {
			if (!options.force) {
				if (why) *why = rule.reason;
				return std::nullopt;
			}
		} else {
			std::ostringstream d;
			d << std::round(rule.depth * 10000.0) / 10000.0;
			out.depthText = d.str();
			if (rule.depth < std::atof(PATTERN_DEPTH)) out.depthNote = ", " + rule.reason;
		}
	}
	return out;
}

/// The material's own bump keys out of a cleaned material text (whole lines).
std::string withoutBumpKeys(std::string text) {
	static const std::regex keys(R"re(\r?\n[ \t]*"?\$(bumpmap|ssbump|bumpframe)"?[ \t]+[^\r\n]*)re", ICASE);
	return std::regex_replace(text, keys, "");
}

const std::regex OWN_BUMPMAP(R"re("?\$bumpmap"?\s)re", ICASE);

std::string shortNumberText(double v) {
	std::ostringstream d;
	d << std::round(v * 10000.0) / 10000.0;
	return d.str();
}

} // namespace

PatternReliefOptions reliefOptionsFor(const MaterialDecision& decided) {
	PatternReliefOptions options;
	options.force = decided.parallax == ParallaxChoice::On;
	options.depth = decided.depth;
	options.noPattern = decided.pattern && *decided.pattern == "none";
	if (decided.scale && *decided.scale > 0) options.faceScale = *decided.scale;
	options.bump = decided.bump.value_or(BumpChoice::Auto);
	if (decided.bumpStrength) options.bumpStrength = std::clamp(*decided.bumpStrength, BUMP_STRENGTH_MIN, BUMP_STRENGTH_MAX);
	return options;
}

bool generatesBumpMap(const PatternReliefOptions& options, std::string_view originalVmt) {
	if (options.bump == BumpChoice::Generate) return true;
	if (options.bump == BumpChoice::Off) return false;
	return !std::regex_search(clean(originalVmt), OWN_BUMPMAP);
}

std::vector<std::byte> encodeGeneratedNormalVtf(const HeightMap& heights, double strength, unsigned width, unsigned height) {
	if (heights.width == 0 || heights.height == 0) return {};
	unsigned w = width ? width : heights.width, h = height ? height : heights.height;
	while (std::max(w, h) > GENERATED_NORMAL_MAX) {
		w = std::max(1u, w / 2);
		h = std::max(1u, h / 2);
	}
	const ImageRgba normal = normalMapFromHeight(heights, strength, w, h);
	auto vtf = encodeVtf(normal, false, true, GENERATED_NORMAL_MAX);
	return vtf ? std::move(*vtf) : std::vector<std::byte>{};
}

std::string withGeneratedBumpMap(std::string_view vmt, std::string_view normalName) {
	const std::string c = withoutBumpKeys(clean(vmt));
	const auto brace = c.find('{');
	if (brace == std::string::npos) return c;
	return c.substr(0, brace + 1) + CRLF + param("$bumpmap", std::string(normalName)) + c.substr(brace + 1);
}

std::optional<PatternRelief> makePatternRelief(const GameFileSystem& fs, std::string_view material, std::string_view originalVmt,
                                              const PatternReliefOptions& options, std::string* tooFine) {
	if (tooFine) tooFine->clear();
	const std::string hash = patternReliefHash(fs, material, originalVmt, options);
	if (hash.empty()) return std::nullopt;
	// Textures without a clear pattern are remembered by their hash (texture, material text and
	// rules): the open map's walls are checked again after every edit, and decoding and analysing
	// a 512 px texture each time for nothing made those checks slow.
	static std::mutex mutex;
	static std::map<std::string, std::string> without;   // hash -> why too fine ("" = no pattern)
	{
		std::lock_guard lock(mutex);
		if (const auto it = without.find(hash); it != without.end()) {
			if (tooFine) *tooFine = it->second;
			return std::nullopt;
		}
	}
	std::string why;
	const auto found = patternHeights(fs, material, originalVmt, options, &why);
	if (!found) {
		std::lock_guard lock(mutex);
		without[hash] = why;
		if (tooFine) *tooFine = why;
		return std::nullopt;
	}
	PatternRelief out;
	out.hash = hash;
	out.heightName = toLower(material) + std::string(PATTERN_RELIEF_SUFFIX);
	out.heightVtf = encodeHeightVtf(found->heights);
	if (out.heightVtf.empty()) return std::nullopt;
	// The bump map from the same heights, at the base texture's own size (up to 1024).
	if (generatesBumpMap(options, originalVmt)) {
		out.normalVtf = encodeGeneratedNormalVtf(found->heights, options.bumpStrength, found->fullWidth, found->fullHeight);
		if (!out.normalVtf.empty()) out.normalName = toLower(material) + std::string(GENERATED_NORMAL_SUFFIX);
	}
	// deploy.ps1's New-ParallaxMaterial: the shader's name swapped, the relief keys first in the
	// block, the rest of the material as it was (without comments).
	std::string c = clean(originalVmt);
	if (!out.normalName.empty()) c = withoutBumpKeys(std::move(c));
	static const std::regex head(R"re(^\s*"?[A-Za-z0-9_]+"?(\s*\{))re");
	std::smatch m;
	if (!std::regex_search(c, m, head)) return std::nullopt;
	std::vector<std::string> lines{param("$parallaxmap", out.heightName), param("$parallaxdepth", found->depthText), param("$parallaxao", PATTERN_AO)};
	if (!out.normalName.empty()) lines.push_back(param("$bumpmap", out.normalName));
	static const std::regex envmap(R"re("?\$envmap"?\s)re", ICASE);
	if (!std::regex_search(c, envmap)) {
		// As deploy.bat: the map's cubemap, shown only where the wall is wet from rain.
		lines.push_back(param("$envmap", "env_cubemap"));
		lines.push_back(param("$envmapwetonly", "1"));
	}
	// Exactly as there: the brace with what stood before it, CRLF, the keys joined by CRLF, then the
	// rest of the cleaned text (its own line ends).
	std::string body = m[1].str() + CRLF;
	for (std::size_t i = 0; i < lines.size(); ++i) body += lines[i] + (i + 1 < lines.size() ? CRLF : "");
	out.text = std::string(PATTERN_RELIEF_MARKER) + " (relief) from " + vmtPath(toLower(std::string(material))) + " hash " + hash + " " +
	           (found->patternFound ? found->layout.describe() : std::string("from the brightness (decision file: parallax=on)")) + found->depthNote +
	           (out.normalName.empty() ? std::string() : ", bump map generated (strength " + shortNumberText(options.bumpStrength) + ")") +
	           " (Mapsmith > Project > Game enhancements)" + CRLF + "\"Fidelity_Parallax\"" + body +
	           c.substr(std::size_t(m.position(0) + m.length(0)));
	return out;
}

ReliefHeights reliefHeights(const GameFileSystem& fs, std::string_view material, std::string_view originalVmt, const MaterialDecision& decision,
                            bool mapMaterial, unsigned maxSize) {
	ReliefHeights out;
	out.source = "none";
	const PatternReliefOptions options = reliefOptionsFor(decision);
	out.bumpStrength = options.bumpStrength;
	const ParallaxChoice choice = decision.parallax.value_or(ParallaxChoice::Auto);
	const auto parsed = parseMaterial(originalVmt, material);
	if (!parsed) {
		out.note = "the material could not be read";
		return out;
	}
	const std::string base = baseTexture(clean(originalVmt));
	if (!base.empty()) {
		if (const auto bytes = fs.read("materials/" + toLower(base) + ".vtf")) {
			if (const auto info = readTextureInfo(*bytes)) {
				out.textureWidth = info->width;
				out.textureHeight = info->height;
			}
		}
	}
	// As planEnhancements decides (Relief): the decision file, then name, flags and look.
	ReliefFit fit = choice == ParallaxChoice::On    ? ReliefFit{true, {}}
	                : choice == ParallaxChoice::Off ? ReliefFit{false, "material the decision file keeps flat (parallax=off)"}
	                                                : reliefFitInGame(fs, material, *parsed);
	PatternReliefOptions effective = options;
	if (fit.suits && options.bump == BumpChoice::Generate) effective.force = true;   // Mapsmith makes the relief to carry the bump map
	if (fit.suits) {
		const bool ours = mapMaterial || effective.any();
		std::string why;
		if (ours) {
			if (auto found = patternHeights(fs, material, originalVmt, effective, &why, maxSize)) {
				out.heights = std::move(found->heights);
				out.source = found->patternFound && !effective.noPattern ? "pattern" : "brightness";
				out.depth = found->depthText;
				out.note = (found->patternFound ? found->layout.describe() : std::string("from the brightness (decision file)")) + found->depthNote;
				out.bump = generatesBumpMap(effective, originalVmt);
				return out;
			}
		}
		if (!why.empty()) {
			fit = {false, why};
		} else if (const auto source = fidelityReliefSource(material, *parsed)) {
			// deploy.bat makes it: from the material's own height map, its normal map or the brightness.
			if (const auto image = loadTexture(fs, source->texture, maxSize)) {
				std::optional<HeightMap> heights;
				switch (source->kind) {
				case ReliefSource::Kind::HeightMap:
					heights = heightFromImage(*image);
					out.source = "material";
					out.note = "the material's own height map " + toLower(source->texture);
					break;
				case ReliefSource::Kind::NormalMap:
				case ReliefSource::Kind::SSBump:
					heights = heightFromNormalMap(*image, source->kind == ReliefSource::Kind::SSBump);
					out.source = "normal";
					out.note = "deploy.bat makes it from the normal map " + toLower(source->texture);
					break;
				case ReliefSource::Kind::Color:
					heights = heightFromColor(*image);
					out.source = "brightness";
					out.note = "deploy.bat makes it from the brightness of " + toLower(source->texture);
					break;
				}
				if (heights) {
					out.heights = std::move(*heights);
					out.depth = shortNumberText(source->depth);
					return out;
				}
			}
			fit = {false, "no heights from " + toLower(source->texture)};
		} else {
			fit = {false, "neither Mapsmith's relief nor deploy.bat's rules give it one"};
		}
	}
	// No relief: the heights parallax=on would use, to look at.
	out.source = "none";
	out.depth.clear();
	out.note = fit.reason;
	PatternReliefOptions forced = options;
	forced.force = true;
	if (auto found = patternHeights(fs, material, originalVmt, forced, nullptr, maxSize)) {
		out.heights = std::move(found->heights);
	} else if (!base.empty()) {
		if (const auto image = loadTexture(fs, base, maxSize)) {
			if (auto heights = heightFromColor(*image)) out.heights = std::move(*heights);
		}
	}
	// A flat material with bump=generate still gets its bump map (a copy without relief).
	out.bump = options.bump == BumpChoice::Generate && out.heights.width > 0;
	return out;
}

std::vector<std::filesystem::path> mapSourceFiles(const std::filesystem::path& mapDir) {
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	if (mapDir.empty() || !std::filesystem::is_directory(mapDir, ec)) return out;
	auto it = std::filesystem::recursive_directory_iterator(mapDir, std::filesystem::directory_options::skip_permission_denied, ec);
	for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		const auto& path = it->path();
		std::error_code typeError;
		if (it->is_directory(typeError)) {
			if (iequals(path.filename().string(), "autosave")) it.disable_recursion_pending();
			continue;
		}
		const std::string name = toLower(path.filename().string());
		// Timestamped autosave copies ("map.20261028-084357.vmf") are not maps of their own, wherever they lie.
		if (name.ends_with(".vmf") && !isGeneratedMapCopy(name) && !parseAutosaveName(path)) out.push_back(path);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::set<std::string> vmfMaterialNames(const std::vector<std::filesystem::path>& vmfFiles) {
	std::set<std::string> out;
	static const std::regex line(R"re(^\s*"material"\s+"([^"]+)")re", ICASE);
	for (const auto& file : vmfFiles) {
		std::ifstream stream(file, std::ios::binary);
		if (!stream) continue;
		std::string text;
		std::smatch m;
		while (std::getline(stream, text)) {
			if (text.find("aterial") == std::string::npos) continue;   // cheap test before the regex
			if (std::regex_search(text, m, line)) {
				std::string name = toLower(m[1].str());
				std::replace(name.begin(), name.end(), '\\', '/');
				out.insert(std::move(name));
			}
		}
	}
	return out;
}

ContainerKind containerKind(std::string_view path, const float* reflectivity) {
	if (std::regex_search(std::string(path), MEDICAL_NAMES)) return ContainerKind::Medical;
	if (reflectivity) {
		const float brightness = 0.2126f * reflectivity[0] + 0.7152f * reflectivity[1] + 0.0722f * reflectivity[2];
		if (brightness < TINTED_BRIGHTNESS) return ContainerKind::Tinted;
	}
	return ContainerKind::Drink;
}

std::optional<std::array<float, 3>> vtfReflectivity(std::string_view vtf) {
	// VTF 7.x header: "VTF\0", version (2 x u32), header size, width, height (u16), flags, frames,
	// first frame (u16), 4 bytes padding, then the reflectivity (3 x float) at byte 32.
	if (vtf.size() < 44 || vtf.substr(0, 4) != std::string_view("VTF\0", 4)) return std::nullopt;
	std::array<float, 3> out{};
	std::memcpy(out.data(), vtf.data() + 32, 12);
	for (const float v : out) {
		if (!(v >= 0.0f && v <= 16.0f)) return std::nullopt;   // NaN or nonsense: no colour
	}
	return out;
}

std::optional<std::string> makeEffectMaterial(GameEffect effect, std::string_view path, std::string_view vmt, ContainerKind kind, std::string_view marker) {
	const std::string p = lowerPath(path);
	switch (effect) {
	case GameEffect::Liquid:
		if (p.starts_with("materials/models/props_foliage")) return std::nullopt;
		return liquidMaterial(p, vmt, kind, marker);
	case GameEffect::Wind: {
		static const std::regex sway(R"re(\$treesway\b)re", ICASE);
		if (std::regex_search(clean(vmt), sway)) return std::nullopt;   // already swaying
		// Trees (foliage that is no bush): only the see-through leaves; anything else sways whole, like a bush.
		const bool tree = p.starts_with("materials/models/props_foliage") && !std::regex_search(p, BUSH_NAMES);
		if (tree && !leafMaterial(p, clean(vmt))) return std::nullopt;
		return windMaterial(p, vmt, tree ? TREE_SWAY : BUSH_SWAY, marker);
	}
	case GameEffect::Water: return waterMaterial(p, vmt, marker);
	case GameEffect::Relief: return std::nullopt;   // made by the material editor (needs a height map)
	}
	return std::nullopt;
}

std::string withoutEffect(GameEffect effect, std::string_view vmt) {
	std::vector<MaterialEdit> edits;
	std::string shader;
	switch (effect) {
	case GameEffect::Liquid:
		shader = "VertexLitGeneric";
		for (const char* key : {"$liquidcolor", "$liquidalpha", "$surfacecolor", "$foamwidth", "$glasscolor", "$glassalpha", "$fill", "$fillmin",
		                        "$fillmax", "$wobble", "$labelthreshold", "$bubbles", "$tintedglass", "$viscosity", "$liquidkey", "$splash",
		                        "$spill", "$liquidplane"}) {
			edits.push_back({key, std::nullopt});
		}
		break;
	case GameEffect::Wind:
		for (const auto& key : SWAY_KEYS) edits.push_back({key, std::nullopt});
		break;
	case GameEffect::Water:
		shader = "Water";
		for (const char* key : {"$fidelityglint", "$fidelityfoam"}) edits.push_back({key, std::nullopt});
		break;
	case GameEffect::Relief:
		shader = "LightmappedGeneric";
		for (const char* key : {"$parallaxmap", "$parallaxmap2", "$parallaxdepth", "$parallaxao", "$parallaxfade", "$parallaxblend",
		                        "$parallaxmodulate", "$envmapwetonly"}) {
			edits.push_back({key, std::nullopt});
		}
		break;
	}
	try {
		std::string out = editMaterialText(vmt, shader, edits);
		if (effect == GameEffect::Liquid) out = setMaterialProxy(out, "FidelityLiquid", false);
		return out;
	} catch (const std::exception&) {
		return std::string(vmt);   // broken syntax: left as it is
	}
}

// --- Settings ------------------------------------------------------------------------------

bool EnhancementSettings::isExcluded(GameEffect effect, std::string_view item) const {
	const auto it = excluded.find(effect);
	return it != excluded.end() && it->second.count(toLower(item));
}

void EnhancementSettings::setExcluded(GameEffect effect, std::string_view item, bool exclude) {
	auto& list = excluded[effect];
	if (exclude) {
		list.insert(toLower(item));
	} else {
		list.erase(toLower(item));
		if (list.empty()) excluded.erase(effect);
	}
}

std::filesystem::path enhancementSettingsFile(const std::filesystem::path& modDir) {
	return modDir / "cfg" / "mapsmith_enhancements.txt";
}

EnhancementSettings parseEnhancementSettings(std::string_view text) {
	EnhancementSettings settings;
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return settings;   // broken: the defaults
	}
	const KvNode* block = root.find("GameEnhancements");
	if (!block) return settings;
	for (const auto& info : gameEffects()) {
		const KvNode* effect = block->find(info.id);
		if (!effect || !effect->block) continue;
		if (effect->get("enabled") == "0") settings.off.insert(info.effect);
		for (const auto& child : effect->children) {
			if (!child.block && iequals(child.key, "exclude") && !child.value.empty()) settings.setExcluded(info.effect, child.value, true);
		}
	}
	return settings;
}

std::string writeEnhancementSettings(const EnhancementSettings& settings) {
	KvNode root;
	KvNode block = KvNode::makeBlock("GameEnhancements");
	for (const auto& info : gameEffects()) {
		KvNode effect = KvNode::makeBlock(info.id);
		effect.children.push_back({"enabled", settings.enabled(info.effect) ? "1" : "0", {}, false, {}});
		if (const auto it = settings.excluded.find(info.effect); it != settings.excluded.end()) {
			for (const auto& item : it->second) effect.children.push_back({"exclude", item, {}, false, {}});
		}
		block.children.push_back(std::move(effect));
	}
	root.children.push_back(std::move(block));
	return "// Mapsmith: the mod's game enhancements (mapbase-fidelity effects). \"enabled\" \"0\" switches an\n"
	       "// effect off for the whole mod; \"exclude\" leaves out one model (.mdl) or material.\n" +
	       writeKeyValues(root);
}

EnhancementSettings loadEnhancementSettings(const std::filesystem::path& modDir) {
	const auto text = readFile(enhancementSettingsFile(modDir));
	return text ? parseEnhancementSettings(*text) : EnhancementSettings{};
}

bool saveEnhancementSettings(const std::filesystem::path& modDir, const EnhancementSettings& settings) {
	const auto file = enhancementSettingsFile(modDir);
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	const std::string text = writeEnhancementSettings(settings);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	return bool(out);
}

// --- Making the materials --------------------------------------------------------------------

const std::vector<GameEffect>& automaticEffects() {
	static const std::vector<GameEffect> effects = {GameEffect::Liquid, GameEffect::Wind, GameEffect::Water, GameEffect::Relief};
	return effects;
}

std::vector<std::string> effectCandidates(const GameFileSystem& fs, const std::filesystem::path& modDir, GameEffect effect) {
	std::vector<std::string> out;
	std::vector<std::string> paths;
	switch (effect) {
	case GameEffect::Liquid:
		// deploy.ps1 reads the containers by their file name.
		for (auto& path : fs.list("materials/models", ".vmt")) {
			const auto slash = path.rfind('/');
			if (std::regex_search(path.substr(slash == std::string::npos ? 0 : slash + 1), BOTTLE_NAMES)) paths.push_back(std::move(path));
		}
		break;
	case GameEffect::Wind: paths = fs.list("materials/models/props_foliage", ".vmt"); break;
	case GameEffect::Water:
		for (auto& path : fs.list("materials", ".vmt")) {
			if (!path.starts_with("materials/models/") && !path.starts_with("materials/skybox/")) paths.push_back(std::move(path));
		}
		break;
	case GameEffect::Relief:
		for (auto& path : fs.list("materials", ".vmt")) {
			if (!std::regex_search(path, NOT_PARALLAX)) paths.push_back(std::move(path));
		}
		break;
	}
	static const std::regex waterShader(R"re(^\s*(?:\xEF\xBB\xBF)?\s*"?(?:SDK_)?Water"?\s*\{)re", ICASE);
	static const std::regex wallShader(R"re(^\s*(?:\xEF\xBB\xBF)?\s*(//[^\n]*\n\s*)*"?(?:SDK_)?(LightmappedGeneric|WorldVertexTransition)"?\s*\{)re", ICASE);
	for (const auto& path : paths) {
		const auto original = fs.readOriginal(path, modDir);
		if (!original) continue;   // only in the mod: its own material
		if (effect == GameEffect::Water && !std::regex_search(*original, waterShader)) continue;   // quick check first
		if (effect == GameEffect::Relief && !std::regex_search(*original, wallShader)) continue;
		if (deployWouldApply(effect, path, *original)) out.push_back(normalizeMaterialName(path));
	}
	return out;
}

EffectCandidates findEffectCandidates(const GameFileSystem& fs, const std::filesystem::path& modDir, const std::vector<GameEffect>& effects) {
	EffectCandidates out;
	for (const GameEffect effect : effects) out[effect] = effectCandidates(fs, modDir, effect);
	return out;
}

int EnhancementPlan::activeCount() const {
	int total = 0;
	for (const auto& [effect, count] : active) total += count;
	return total;
}

std::map<GameEffect, std::set<std::string>> excludedMaterials(const GameFileSystem& fs, const EnhancementSettings& settings) {
	std::map<GameEffect, std::set<std::string>> out;
	for (const auto& [effect, items] : settings.excluded) {
		for (const auto& item : items) {
			if (item.ends_with(".mdl")) {
				for (auto& material : modelMaterialNames(fs, item)) out[effect].insert(std::move(material));
			} else {
				out[effect].insert(normalizeMaterialName(item));
			}
		}
	}
	return out;
}

EnhancementPlan planEnhancements(const GameFileSystem& fs, const std::filesystem::path& modDir, const EnhancementSettings& settings,
                                 const std::map<GameEffect, std::set<std::string>>& excluded, const EffectCandidates* candidates,
                                 const EnhancementOptions& options) {
	EnhancementPlan plan;
	std::set<std::string> kept;
	std::set<std::string> reliefVisited;   // materials whose relief file this plan decided about
	const EffectCandidates found = candidates ? EffectCandidates{} : findEffectCandidates(fs, modDir);
	const EffectCandidates& all = candidates ? *candidates : found;
	const auto heightFileOf = [&modDir](const std::string& material) {
		return modDir / "materials" / std::filesystem::path(toLower(material) + std::string(PATTERN_RELIEF_SUFFIX) + ".vtf");
	};
	const auto isPattern = [](const std::optional<std::string>& own) { return own && std::string_view(*own).starts_with(PATTERN_RELIEF_MARKER); };
	const auto normalFileOf = [&modDir](const std::string& material) {
		return modDir / "materials" / std::filesystem::path(toLower(material) + std::string(GENERATED_NORMAL_SUFFIX) + ".vtf");
	};
	// A material of Mapsmith's with a generated bump map (pattern relief or a flat copy).
	const auto hasNormal = [](const std::optional<std::string>& own) {
		return own && std::string_view(*own).starts_with(MAPSMITH_MARKER) && own->find(GENERATED_NORMAL_SUFFIX) != std::string::npos;
	};
	// One material of one effect: what its file in the mod should be. `deployMakes`: deploy.bat
	// gives it the effect by itself (a candidate); a wall only of the maps gets nothing but the
	// pattern relief.
	const auto plan1 = [&](GameEffect effect, const std::string& material, bool deployMakes) {
		// Names come from maps, VPKs and the decision file: one that would leave the mod's
		// materials folder ("../../x", "c:/x") never becomes a file name.
		if (!isSafeGamePath(material)) return;
		const std::string path = vmtPath(material);
		const auto file = modDir / "materials" / std::filesystem::path(material + ".vmt");
		std::optional<std::string> own;
		{
			std::error_code ec;
			if (std::filesystem::exists(file, ec)) own = readFile(file);
		}
		if (own && !isGeneratedMaterial(*own)) {
			// The mapper's own version: never touched.
			if (kept.insert(material).second) plan.keptOwn.push_back(material);
			if (effectIsOn(effect, *own)) ++plan.active[effect];
			return;
		}
		const auto original = fs.readOriginal(path, modDir);
		if (!original) return;
		const auto skipped = excluded.find(effect);
		const bool want = settings.enabled(effect) && !settings.isExcluded(effect, material) &&
		                  !(skipped != excluded.end() && skipped->second.count(material));
		const auto removeOwn = [&] {
			EffectChange change;
			change.effect = effect;
			change.material = material;
			change.file = file;
			change.remove = true;
			if (isPattern(own)) change.heightFile = heightFileOf(material);
			if (hasNormal(own)) change.normalFile = normalFileOf(material);
			plan.changes.push_back(std::move(change));
		};
		const auto write = [&](std::string text, std::vector<std::byte> heightVtf = {}, std::vector<std::byte> normalVtf = {}) {
			if (own && *own == text && (heightVtf.empty() || std::filesystem::exists(heightFileOf(material))) &&
			    (normalVtf.empty() || std::filesystem::exists(normalFileOf(material)))) {
				return;   // already so
			}
			EffectChange change;
			change.effect = effect;
			change.material = material;
			change.file = file;
			change.text = std::move(text);
			if (!heightVtf.empty() || isPattern(own)) change.heightFile = heightFileOf(material);
			change.heightVtf = std::move(heightVtf);
			if (!normalVtf.empty() || hasNormal(own)) change.normalFile = normalFileOf(material);
			change.normalVtf = std::move(normalVtf);
			plan.changes.push_back(std::move(change));
		};
		std::string text;
		if (want && effectNeedsShaderDll(effect) && !options.shaderDll) {
			// Without the mod's shader DLL the game draws these as wireframes (see shaderNeedsModDll):
			// nothing gets the effect, and what was generated with it goes (deploy.ps1 does the same).
			if (deployMakes) ++plan.heldBack;
			if (own && effectIsOn(effect, *own)) removeOwn();
			return;
		}
		if (effect == GameEffect::Relief) {
			reliefVisited.insert(toLower(material));
			// The mod's decision file first: parallax=off keeps it off, parallax=on gives it relief
			// (also where name and look would not), depth and pattern shape Mapsmith's relief.
			const MaterialDecision decided = options.decisions ? options.decisions->lookup(material) : MaterialDecision{};
			const ParallaxChoice choice = decided.parallax.value_or(ParallaxChoice::Auto);
			PatternReliefOptions reliefOptions = reliefOptionsFor(decided);
			// By name, flags and the texture's look - as the viewport decides (relief_pattern).
			const auto parsed = parseMaterial(*original, material);
			ReliefFit fit = parsed ? reliefFitInGame(fs, material, *parsed) : reliefFitByName(material, baseTexture(clean(*original)));
			bool wantIt = want;
			if (choice == ParallaxChoice::Off) {
				wantIt = false;
				fit = {false, "material the decision file keeps flat (parallax=off)"};
			} else if (choice == ParallaxChoice::On) {
				wantIt = settings.enabled(effect);
				fit = {true, {}};
			}
			// bump=generate: Mapsmith makes the relief itself (deploy.bat's has no bump map of ours).
			if (fit.suits && reliefOptions.bump == BumpChoice::Generate) reliefOptions.force = true;
			if (wantIt && fit.suits) {
				// Walls of the mod's maps (and every material the decision file shapes): the relief
				// from the texture's pattern (bricks up, joints down), where one is found clearly;
				// else deploy.bat makes it (from the normal map or the brightness).
				const bool ours = (options.mapMaterials && options.mapMaterials->count(toLower(material))) || reliefOptions.any();
				std::string tooFine;
				if (ours) {
					if (isPattern(own)) {
						const std::string hash = patternReliefHash(fs, material, *original, reliefOptions);
						const bool normalThere = !generatesBumpMap(reliefOptions, *original) || std::filesystem::exists(normalFileOf(material));
						if (!hash.empty() && own->find(" hash " + hash + " ") != std::string::npos && std::filesystem::exists(heightFileOf(material)) &&
						    normalThere) {
							++plan.active[effect];
							++plan.patternRelief;
							return;   // made from this texture and these rules already
						}
					}
					if (auto pattern = makePatternRelief(fs, material, *original, reliefOptions, &tooFine)) {
						++plan.active[effect];
						++plan.patternRelief;
						write(std::move(pattern->text), std::move(pattern->heightVtf), std::move(pattern->normalVtf));
						return;
					}
				}
				if (tooFine.empty()) {
					if (deployMakes) ++plan.active[effect];
					// The copy that kept it off (or an old pattern relief) goes: deploy.bat makes the relief again.
					if (own && std::string_view(*own).starts_with(MAPSMITH_MARKER)) removeOwn();
					return;
				}
				// A pattern too fine for relief (small bricks swim): kept off, also against deploy.bat.
				fit = {false, tooFine};
			}
			if (reliefOptions.bump == BumpChoice::Generate && want) {
				// No relief, but bump=generate: a copy of the original with the generated bump map
				// (from the heights parallax=on would use), kept until the line goes.
				PatternReliefOptions forced = reliefOptions;
				forced.force = true;
				const std::string hash = patternReliefHash(fs, material, *original, forced);
				if (!hash.empty()) {
					const std::string tag = " bump " + hash + " ";
					if (own && own->find(tag) != std::string::npos && std::filesystem::exists(normalFileOf(material))) return;   // already so
					std::string why;
					if (const auto found = patternHeights(fs, material, *original, forced, &why)) {
						auto normal = encodeGeneratedNormalVtf(found->heights, reliefOptions.bumpStrength, found->fullWidth, found->fullHeight);
						if (!normal.empty()) {
							write(std::string(MAPSMITH_MARKER) + " off (relief) from " + path + tag + "- flat, with a generated bump map (" + fit.reason +
							          "; Mapsmith > Project > Game enhancements)" + CRLF +
							          withGeneratedBumpMap(*original, toLower(material) + std::string(GENERATED_NORMAL_SUFFIX)),
							      {}, std::move(normal));
							return;
						}
					}
				}
			}
			if (!deployMakes) {
				if (isPattern(own) || hasNormal(own)) removeOwn();   // switched off: the wall is as in the game again
				return;
			}
			text = std::string(MAPSMITH_MARKER) + " off (relief) from " + path +
			       (fit.suits ? std::string(" (Mapsmith > Project > Game enhancements switches it on again)")
			                  : " - no depth for a " + fit.reason + " (Mapsmith decides; Parallax: On in the material's menu gives it relief)") +
			       CRLF + *original;
		} else if (want) {
			ContainerKind kind = containerKind(path);
			if (effect == GameEffect::Liquid && kind == ContainerKind::Drink) {
				// Dark glass from the texture's average colour (deploy.ps1 reads the .vtf header too).
				const std::string texture = baseTexture(clean(*original));
				if (!texture.empty()) {
					if (const auto vtf = fs.read("materials/" + texture + ".vtf")) {
						const std::string_view bytes(reinterpret_cast<const char*>(vtf->data()), std::min<std::size_t>(vtf->size(), 64));
						if (const auto r = vtfReflectivity(bytes)) kind = containerKind(path, r->data());
					}
				}
			}
			auto made = makeEffectMaterial(effect, path, *original, kind, DEPLOY_MARKER);
			if (!made) return;
			text = std::move(*made);
			++plan.active[effect];
		} else {
			// A copy of the original keeps it off, also when deploy.bat runs again.
			text = std::string(MAPSMITH_MARKER) + " off (" + gameEffectInfo(effect).id + ") from " + path +
			       " (Mapsmith > Project > Game enhancements switches it on again)" + CRLF + *original;
		}
		write(std::move(text));
	};
	for (const auto& [effect, materials] : all) {
		for (const auto& material : materials) plan1(effect, material, true);
	}
	// Walls of the maps deploy.bat does not choose by itself (no bump map, a name without bricks
	// or stones) get the pattern relief too, when their texture shows one.
	if (options.mapMaterials) {
		std::set<std::string> done;
		if (const auto it = all.find(GameEffect::Relief); it != all.end()) {
			for (const auto& material : it->second) done.insert(toLower(material));
		}
		const bool reliefOn = std::any_of(all.begin(), all.end(), [](const auto& entry) { return entry.first == GameEffect::Relief; });
		if (reliefOn) {
			for (const auto& material : *options.mapMaterials) {
				if (done.count(material)) continue;
				const auto original = fs.readOriginal(vmtPath(material), modDir);
				if (!original || patternWallTexture(material, *original).empty()) continue;
				plan1(GameEffect::Relief, material, false);
			}
		}
	}
	// Materials the decision file switches on by name (not wildcards: those only shape the ones
	// above) get Mapsmith's relief too, wherever they are used.
	if (options.decisions && std::any_of(all.begin(), all.end(), [](const auto& entry) { return entry.first == GameEffect::Relief; })) {
		std::set<std::string> done;
		if (const auto it = all.find(GameEffect::Relief); it != all.end()) {
			for (const auto& material : it->second) done.insert(toLower(material));
		}
		if (options.mapMaterials) done.insert(options.mapMaterials->begin(), options.mapMaterials->end());
		for (const auto& line : options.decisions->lines()) {
			if (line.pattern.empty() || line.pattern.find_first_of("*?") != std::string::npos || done.count(line.pattern)) continue;
			const MaterialDecision decided = options.decisions->lookup(line.pattern);
			if (decided.parallax != ParallaxChoice::On && decided.bump != BumpChoice::Generate) continue;
			done.insert(line.pattern);
			const auto original = fs.readOriginal(vmtPath(line.pattern), modDir);
			if (!original || patternWallTexture(line.pattern, *original, true).empty()) continue;
			plan1(GameEffect::Relief, line.pattern, false);
		}
	}
	// Mapsmith's relief of a material nothing asks for any more (the decision file switched it on
	// and no longer does, no map uses it): the wall is as in the game again.
	if (options.mapMaterials && options.shaderDll) {
		std::set<std::filesystem::path> planned;
		for (const auto& change : plan.changes) planned.insert(change.file);
		const auto root = modDir / "materials";
		std::error_code ec;
		if (std::filesystem::is_directory(root, ec)) {
			for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
			     !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || !iequals(it->path().extension().string(), ".vmt") || planned.count(it->path())) continue;
				auto name = toLower(it->path().lexically_relative(root).generic_string());
				name = name.substr(0, name.size() - 4);
				if (reliefVisited.count(name)) continue;
				const auto text = readFile(it->path());
				if (!text || !(std::string_view(*text).starts_with(PATTERN_RELIEF_MARKER) || hasNormal(text))) continue;
				EffectChange change;
				change.effect = GameEffect::Relief;
				change.material = name;
				change.file = it->path();
				change.remove = true;
				if (std::string_view(*text).starts_with(PATTERN_RELIEF_MARKER)) change.heightFile = heightFileOf(name);
				if (hasNormal(text)) change.normalFile = normalFileOf(name);
				plan.changes.push_back(std::move(change));
			}
		}
	}
	if (!options.shaderDll) {
		// Every other generated material that needs the DLL goes as well (a pattern relief of a
		// map no longer opened, a material of an effect switched off since): deploy.ps1 does so.
		std::set<std::filesystem::path> planned;
		for (const auto& change : plan.changes) planned.insert(change.file);
		const auto root = modDir / "materials";
		std::error_code ec;
		if (std::filesystem::is_directory(root, ec)) {
			for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
			     !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || !iequals(it->path().extension().string(), ".vmt") || planned.count(it->path())) continue;
				const auto text = readFile(it->path());
				if (!text || !isGeneratedMaterial(*text) || !shaderNeedsModDll(shaderOf(*text))) continue;
				auto name = it->path().lexically_relative(root).generic_string();
				name = name.substr(0, name.size() - 4);
				EffectChange change;
				change.effect = startsWithI(shaderOf(*text), "Fidelity_Liquid") ? GameEffect::Liquid
				                : iequals(shaderOf(*text), "Water_Fidelity")    ? GameEffect::Water
				                                                                : GameEffect::Relief;
				change.material = name;
				change.file = it->path();
				change.remove = true;
				if (std::string_view(*text).starts_with(PATTERN_RELIEF_MARKER)) change.heightFile = heightFileOf(name);
				if (hasNormal(text)) change.normalFile = normalFileOf(name);
				plan.changes.push_back(std::move(change));
			}
		}
	}
	// Containers on shared materials: own model copies (deploy.ps1's Get-ContainerModels).
	if (std::any_of(all.begin(), all.end(), [](const auto& entry) { return entry.first == GameEffect::Liquid; })) {
		planContainerModels(fs, modDir, settings, options.shaderDll, plan);
	}
	return plan;
}

EffectCount countEffect(const GameFileSystem& fs, const std::filesystem::path& modDir, GameEffect effect, const std::vector<std::string>& materials) {
	EffectCount count;
	for (const auto& material : materials) {
		const auto text = fs.readText(vmtPath(material));
		if (!text) continue;
		const bool isOn = effectIsOn(effect, *text);
		const auto original = fs.readOriginal(vmtPath(material), modDir);
		if (isOn || (original && deployWouldApply(effect, vmtPath(material), *original))) ++count.applicable;
		if (isOn) ++count.on;
	}
	return count;
}

std::vector<std::string> modelMaterialNames(const GameFileSystem& fs, std::string_view modelPath) {
	std::vector<std::string> out;
	const auto data = fs.read(modelPath);
	if (!data) return out;
	const auto info = readModelInfo(*data);
	if (!info) return out;
	for (const auto& textureName : info->materials) {
		// Like the engine: the first $cdmaterials folder that holds it.
		std::string found;
		for (const auto& dir : info->materialDirs) {
			const auto candidate = normalizeMaterialName(dir + "/" + textureName);
			if (fs.exists(vmtPath(candidate))) {
				found = candidate;
				break;
			}
		}
		if (found.empty()) continue;
		if (std::find(out.begin(), out.end(), found) == out.end()) out.push_back(found);
	}
	return out;
}

} // namespace sme
