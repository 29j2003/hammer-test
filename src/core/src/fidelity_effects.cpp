// fidelity_effects.cpp - Game enhancements per model or material (see fidelity_effects.h).
// The material rules are those of mapbase-fidelity's deploy.ps1 (New-LiquidMaterial,
// New-WindMaterial, New-WaterMaterial); the output is the same text, line for line.

#include <sme/fidelity_effects.h>

#include <sme/assets.h>
#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/material.h>
#include <sme/relief_pattern.h>
#include <sme/text.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
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
	const bool seeThrough = std::regex_search(c, std::regex(R"re("?\$translucent"?\s+"?1)re", ICASE)) ||
	                        std::regex_search(c, std::regex(R"re("?\$alpha"?\s+"?0?\.\d)re", ICASE));
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
		if (std::regex_search(std::string(path), std::regex("glassbottle01", ICASE))) {
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
	if (std::regex_search(std::string(path), std::regex("(bark|trunk|stump|log)", ICASE))) return false;
	return std::regex_search(cleanText, std::regex(R"re("?\$(alphatest|translucent)"?\s+"?1)re", ICASE));
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
	if (!std::regex_search(c, std::regex(R"re("?\$abovewater"?\s+"?1)re", ICASE))) return std::nullopt;
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
	case GameEffect::Water:
		return iequals(shader, "Water_Fidelity") ||
		       (std::regex_match(shader, std::regex("^(SDK_)?Water$", ICASE)) && std::regex_search(clean(vmt), std::regex(R"re("?\$abovewater"?\s+"?1)re", ICASE)));
	case GameEffect::Relief:
		return !model && (std::regex_match(shader, std::regex("^(SDK_)?(LightmappedGeneric|WorldVertexTransition)$", ICASE)) || effectIsOn(GameEffect::Relief, vmt));
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
		if (std::regex_search(clean(vmt), std::regex(R"re(\$treesway\b)re", ICASE))) return std::nullopt;   // already swaying
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
                                 const std::map<GameEffect, std::set<std::string>>& excluded, const EffectCandidates* candidates) {
	EnhancementPlan plan;
	std::set<std::string> kept;
	const EffectCandidates found = candidates ? EffectCandidates{} : findEffectCandidates(fs, modDir);
	for (const auto& [effect, materials] : candidates ? *candidates : found) {
		const auto skipped = excluded.find(effect);
		for (const auto& material : materials) {
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
				continue;
			}
			const auto original = fs.readOriginal(path, modDir);
			if (!original) continue;
			const bool want = settings.enabled(effect) && !settings.isExcluded(effect, material) &&
			                  !(skipped != excluded.end() && skipped->second.count(material));
			std::string text;
			if (effect == GameEffect::Relief) {
				// deploy.bat makes the relief (it needs the height maps); Mapsmith only keeps it off where it
				// is switched off or makes no sense (windows, signs, metal sheets ... - relief_pattern).
				// By name, flags and the texture's look - as the viewport decides (relief_pattern).
				const auto parsed = parseMaterial(*original, material);
				const ReliefFit fit = parsed ? reliefFitInGame(fs, material, *parsed) : reliefFitByName(material, baseTexture(clean(*original)));
				if (want && fit.suits) {
					++plan.active[effect];
					if (own && std::string_view(*own).starts_with(MAPSMITH_MARKER)) {
						EffectChange change;   // the copy that kept it off goes: deploy.bat makes the relief again
						change.effect = effect;
						change.material = material;
						change.file = file;
						change.remove = true;
						plan.changes.push_back(std::move(change));
					}
					continue;
				}
				text = std::string(MAPSMITH_MARKER) + " off (relief) from " + path +
				       (fit.suits ? std::string(" (Mapsmith > Project > Game enhancements switches it on again)")
				                  : " - no depth for a " + fit.reason + " (Mapsmith decides; the material editor can still give it relief)") +
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
				if (!made) continue;
				text = std::move(*made);
				++plan.active[effect];
			} else {
				// A copy of the original keeps it off, also when deploy.bat runs again.
				text = std::string(MAPSMITH_MARKER) + " off (" + gameEffectInfo(effect).id + ") from " + path +
				       " (Mapsmith > Project > Game enhancements switches it on again)" + CRLF + *original;
			}
			if (own && *own == text) continue;   // already so
			EffectChange change;
			change.effect = effect;
			change.material = material;
			change.file = file;
			change.text = std::move(text);
			plan.changes.push_back(std::move(change));
		}
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
