// material_review.cpp - Export of the mod's materials for review (see material_review.h).

#include <sme/material_review.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <sme/assets.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/relief_pattern.h>
#include <sme/text.h>

namespace sme {

namespace {

std::string fixed(double v, int digits) {
	char text[32];
	std::snprintf(text, sizeof text, "%.*f", digits, v);
	return text;
}

std::string folderOf(const std::string& name) {
	const auto slash = name.rfind('/');
	return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

std::string csvField(std::string_view value) {
	if (value.find_first_of(",\"\n\r") == std::string_view::npos) return std::string(value);
	std::string out = "\"";
	for (const char c : value) {
		if (c == '"') out += "\"\"";
		else if (c != '\r') out += c;
	}
	return out + "\"";
}

std::string jsonString(std::string_view value) {
	std::string out = "\"";
	for (const char c : value) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				char hex[8];
				std::snprintf(hex, sizeof hex, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
				out += hex;
			} else {
				out += c;
			}
		}
	}
	return out + "\"";
}

std::string joined(const std::vector<std::string>& parts, std::string_view separator) {
	std::string out;
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i) out += separator;
		out += parts[i];
	}
	return out;
}

std::string windowsText(const TextureAnalysis& a) {
	if (!a.hasWindows) return {};
	if (a.columns > 0 && a.rows > 0) return std::to_string(a.columns) + "x" + std::to_string(a.rows);
	return std::to_string(a.windows.size());
}

} // namespace

bool reviewSkips(std::string_view material, const ReviewScope& scope) {
	const std::string name = decisionKey(material);
	if (name.starts_with("models/")) return true;
	if (!scope.tools && name.starts_with("tools/")) return true;
	if (!scope.skybox && name.starts_with("skybox/")) return true;
	if (!scope.decals && (name.starts_with("decals/") || name.starts_with("overlays/"))) return true;
	return false;
}

std::vector<std::string> reviewMaterialList(const GameFileSystem& fs, const ReviewScope& scope, const std::set<std::string>& mapMaterials) {
	std::set<std::string> names;
	if (scope.kind == ReviewScope::Kind::MapMaterials) {
		for (const auto& material : mapMaterials) {
			const std::string key = decisionKey(material);
			if (!key.empty() && !reviewSkips(key, scope)) names.insert(key);
		}
	} else {
		const std::string folder = decisionKey(scope.folder);
		for (const auto& path : fs.list("materials", ".vmt")) {
			const std::string key = decisionKey(path);
			if (key.empty() || reviewSkips(key, scope)) continue;
			if (scope.kind == ReviewScope::Kind::Folder && !folder.empty() && !key.starts_with(folder)) continue;
			names.insert(key);
		}
	}
	std::vector<std::string> out;
	for (const auto& name : names) {
		if (scope.kind != ReviewScope::Kind::MapMaterials) {
			// Only brush (world) materials: what can go on a wall, floor or roof.
			const auto material = loadMaterial(fs, name);
			if (!material) continue;
			const MaterialCategory category = classifyMaterial(*material);
			if (category.model) continue;
			const bool asked = (scope.tools && category.tool) || (scope.decals && category.decal) || (scope.skybox && category.sky);
			if (!category.brush && !asked) continue;
			if (!scope.decals && category.decal) continue;
			if (!scope.tools && category.tool && !category.sky) continue;
			if (!scope.skybox && category.sky) continue;
		}
		out.push_back(name);
	}
	std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
		const std::string fa = folderOf(a), fb = folderOf(b);
		return fa != fb ? fa < fb : a < b;
	});
	return out;
}

ReviewEntry reviewEntry(const GameFileSystem& fs, std::string_view materialName, const MaterialDecisions* decisions, unsigned thumb) {
	ReviewEntry e;
	e.name = decisionKey(materialName);
	std::string error;
	const auto material = loadMaterial(fs, e.name, &error);
	if (!material) {
		e.error = error.empty() ? "material not found" : error;
		return e;
	}
	e.shader = material->shader;
	e.baseTexture = normalizeMaterialName(material->param("$basetexture"));
	if (e.baseTexture.empty()) e.baseTexture = normalizeMaterialName(material->previewTexture());
	e.bumpTexture = normalizeMaterialName(material->param("$bumpmap"));
	if (e.bumpTexture.empty()) e.bumpTexture = normalizeMaterialName(material->param("$normalmap"));
	e.surfaceprop = material->param("$surfaceprop");
	const auto has = [&material](const char* key) {
		const std::string v = material->param(key);
		return !v.empty() && v != "0";
	};
	if (has("$bumpmap") || has("$normalmap")) e.flags.push_back(has("$ssbump") ? "ssbump" : "bump");
	if (has("$parallaxmap") || startsWithI(e.shader, "Fidelity_Parallax")) e.flags.push_back("parallax");
	if (has("$envmap")) e.flags.push_back("envmap");
	if (startsWithI(e.shader, "WorldVertexTransition") || startsWithI(e.shader, "SDK_WorldVertexTransition") || has("$basetexture2")) e.flags.push_back("blend");
	if (material->flag("$translucent") || material->flag("$additive")) e.flags.push_back("translucent");
	if (material->flag("$alphatest")) e.flags.push_back("alphatest");
	if (material->flag("$selfillum")) e.flags.push_back("selfillum");
	if (has("$detail")) e.flags.push_back("detail");
	if (material->flag("$phong")) e.flags.push_back("phong");
	if (material->flag("$decal")) e.flags.push_back("decal");

	// The pictures: one for the sheet, one to read the windows from, one for the relief pattern.
	if (!e.baseTexture.empty()) {
		if (const auto data = fs.read("materials/" + toLower(e.baseTexture) + ".vtf")) {
			if (const auto info = readTextureInfo(*data)) {
				e.width = info->width;
				e.height = info->height;
			}
			if (auto image = decodeTexture(*data, thumb)) e.base = std::move(*image);
			if (const auto small = decodeTexture(*data, 256)) e.analysis = analyzeTexture(*small, e.name, e.width, e.height);
			if (const auto large = decodeTexture(*data, 512)) {
				const ReliefLayout layout = detectReliefLayout(*large);
				if (layout.found()) {
					e.reliefPattern = layout.describe();
					e.reliefConfidence = layout.confidence;
					const double across = layout.transposed ? layout.width : layout.height;
					const double along = layout.transposed ? layout.height : layout.width;
					const double fullAcross = layout.transposed ? e.width : e.height;
					const double fullAlong = layout.transposed ? e.height : e.width;
					if (across > 0) e.courseTexels = layout.courseHeight * fullAcross / across;
					if (along > 0 && layout.unitWidth > 0) e.unitTexels = layout.unitWidth * fullAlong / along;
					const ReliefDepth rule = reliefDepthForLayout(layout, 0.006, static_cast<int>(fullAcross));
					e.parallaxAuto = rule.on ? "pattern relief, " + rule.reason : "off: " + rule.reason;
				} else {
					e.reliefPattern = "none";
				}
			}
		} else {
			e.error = "texture " + e.baseTexture + " not found";
		}
	}
	if (!e.bumpTexture.empty()) {
		if (auto image = loadTexture(fs, e.bumpTexture, std::max(32u, thumb / 3))) e.bump = std::move(*image);
	}
	// Relief: whether Mapsmith would give it any (by name and look), then the pattern's depth rule.
	const ReliefFit fit = reliefFitInGame(fs, e.name, *material);
	if (!fit.suits) e.parallaxAuto = "off: " + fit.reason;
	else if (e.parallaxAuto.empty()) e.parallaxAuto = e.flags.empty() || std::find(e.flags.begin(), e.flags.end(), "bump") == e.flags.end()
	                                                      ? "on where deploy.bat makes it (from the brightness)"
	                                                      : "on where deploy.bat makes it (from the normal map)";
	const std::string path = "materials/" + e.name + ".vmt";
	if (const auto text = fs.readText(path)) {
		if (effectIsOn(GameEffect::Relief, *text)) {
			const auto loaded = parseMaterial(*text, e.name);
			const std::string depth = loaded ? loaded->param("$parallaxdepth") : std::string();
			e.parallaxNow = "on (depth " + (depth.empty() ? std::string("0.01") : depth) + ")";
		} else {
			e.parallaxNow = "off";
		}
		for (const auto& info : gameEffects()) {
			if (effectApplies(info.effect, path, *text)) e.effects.push_back(info.id);
		}
	}
	if (decisions) {
		const MaterialDecision decided = decisions->lookup(e.name);
		e.decision = decided.keysText();
		applyMaterialDecision(e.analysis, decided);
	}
	return e;
}

std::string reviewFlagsText(const ReviewEntry& e) {
	return joined(e.flags, " · ");
}

std::string reviewIndexCsv(const std::vector<ReviewEntry>& entries) {
	std::string out =
		"number,name,shader,width,height,surfaceprop,flags,role,windows_per_tile,floors_per_tile,role_reason,relief_pattern,course_texels,"
		"unit_texels,relief_confidence,parallax_now,parallax_auto,effects,decision,base_texture,bump_texture,error\n";
	for (const auto& e : entries) {
		const bool facade = e.analysis.role == TextureRole::FacadeWindows || e.analysis.role == TextureRole::ShopFront;
		const std::vector<std::string> fields = {
			std::to_string(e.number),
			e.name,
			e.shader,
			std::to_string(e.width),
			std::to_string(e.height),
			e.surfaceprop,
			joined(e.flags, " "),
			decisionRoleKey(e.analysis.role),
			windowsText(e.analysis),
			facade ? std::to_string(e.analysis.floorsPerTile()) : std::string(),
			e.analysis.reason,
			e.reliefPattern,
			e.courseTexels > 0 ? fixed(e.courseTexels, 1) : std::string(),
			e.unitTexels > 0 ? fixed(e.unitTexels, 1) : std::string(),
			e.reliefConfidence > 0 ? fixed(e.reliefConfidence, 2) : std::string(),
			e.parallaxNow,
			e.parallaxAuto,
			joined(e.effects, " "),
			e.decision,
			e.baseTexture,
			e.bumpTexture,
			e.error,
		};
		for (std::size_t i = 0; i < fields.size(); ++i) out += (i ? "," : "") + csvField(fields[i]);
		out += "\n";
	}
	return out;
}

std::string reviewIndexJson(const std::vector<ReviewEntry>& entries) {
	std::string out = "[\n";
	for (std::size_t i = 0; i < entries.size(); ++i) {
		const auto& e = entries[i];
		const auto list = [](const std::vector<std::string>& items) {
			std::string s = "[";
			for (std::size_t k = 0; k < items.size(); ++k) s += (k ? ", " : "") + jsonString(items[k]);
			return s + "]";
		};
		std::string windows = "[";
		for (std::size_t k = 0; k < e.analysis.windows.size(); ++k) {
			const auto& w = e.analysis.windows[k];
			windows += std::string(k ? ", " : "") + "[" + fixed(w.u0, 3) + ", " + fixed(w.v0, 3) + ", " + fixed(w.u1, 3) + ", " + fixed(w.v1, 3) + "]";
		}
		windows += "]";
		out += "  {\"number\": " + std::to_string(e.number) + ", \"name\": " + jsonString(e.name) + ", \"shader\": " + jsonString(e.shader) +
		       ", \"width\": " + std::to_string(e.width) + ", \"height\": " + std::to_string(e.height) + ", \"surfaceprop\": " + jsonString(e.surfaceprop) +
		       ", \"flags\": " + list(e.flags) + ", \"role\": " + jsonString(decisionRoleKey(e.analysis.role)) +
		       ", \"role_reason\": " + jsonString(e.analysis.reason) + ", \"windows_per_tile\": " + jsonString(windowsText(e.analysis)) +
		       ", \"windows\": " + windows + ", \"floors_per_tile\": " + std::to_string(e.analysis.floorsPerTile()) +
		       ", \"relief_pattern\": " + jsonString(e.reliefPattern) + ", \"course_texels\": " + fixed(e.courseTexels, 1) +
		       ", \"unit_texels\": " + fixed(e.unitTexels, 1) + ", \"relief_confidence\": " + fixed(e.reliefConfidence, 2) +
		       ", \"parallax_now\": " + jsonString(e.parallaxNow) + ", \"parallax_auto\": " + jsonString(e.parallaxAuto) +
		       ", \"effects\": " + list(e.effects) + ", \"decision\": " + jsonString(e.decision) + ", \"base_texture\": " + jsonString(e.baseTexture) +
		       ", \"bump_texture\": " + jsonString(e.bumpTexture) + ", \"error\": " + jsonString(e.error) + "}";
		out += i + 1 < entries.size() ? ",\n" : "\n";
	}
	return out + "]\n";
}

std::string reviewReadme(int materials, int sheets, int parts, std::string_view game) {
	std::ostringstream out;
	out << "Mapsmith - materials for review\n"
	    << "================================\n\n"
	    << materials << " materials of " << (game.empty() ? std::string("the mod") : std::string(game)) << " on " << sheets << " contact sheet"
	    << (sheets == 1 ? "" : "s") << ", in " << parts << " part" << (parts == 1 ? "" : "s") << ".\n\n"
	    << "What is in it\n"
	    << "- sheet_NNN.jpg: 12 materials per sheet. Under each picture: its number, name, texture size, shader and flags\n"
	    << "  (bump, ssbump, parallax, envmap, blend, translucent, alphatest, selfillum, detail, phong, decal). A small picture\n"
	    << "  in the corner is its normal / height map.\n"
	    << "- index.csv and index.json: one row per material with what Mapsmith decided by itself: its role in a building\n"
	    << "  (facade with windows, wall, trim, roof, shop front, unsuitable), windows per tile, floors per tile, the relief\n"
	    << "  pattern it found (bricks, tiles, boards) with the size of a course, parallax now and what Mapsmith would do,\n"
	    << "  and what the decision file already says.\n\n"
	    << "What to send\n"
	    << "Upload every part (ZIP) into the chat and ask for a decision file. The answer is lines like these - paste them\n"
	    << "into Mapsmith (Project > Load material decisions ...):\n\n"
	    << "    building_template/building_template007a  role=facade windows=2x1 floors=1\n"
	    << "    brick/brickwall031d  role=wall parallax=on depth=0.008 pattern=brick\n"
	    << "    brick/brickwall047a  parallax=off\n"
	    << "    concrete/concretefloor001a  role=ground parallax=off\n"
	    << "    brick/*  parallax=off            // wildcards: the more specific line wins\n\n"
	    << "Keys\n"
	    << "- role: facade | wall | trim | roof | shop | ground | interior | unsuitable (where it goes in a building)\n"
	    << "- windows=CxR: windows across and down one tile of the texture; floors=N: floors one tile spans\n"
	    << "- windowrects=u0,v0,u1,v1;... : exact windows (0..1 across one tile, v from the top)\n"
	    << "- scale: texture scale on buildings (0.25 is the usual)\n"
	    << "- parallax: auto | on | off; depth: 0 .. 0.1 (0.006 shallow, 0.01 the usual); pattern: brick | tile | plank | stone | none\n"
	    << "- note=\"...\": anything to remember\n";
	return out.str();
}

std::vector<std::vector<std::size_t>> reviewSheets(std::size_t count, std::size_t perSheet) {
	std::vector<std::vector<std::size_t>> out;
	if (perSheet == 0) perSheet = 12;
	for (std::size_t i = 0; i < count; i += perSheet) {
		std::vector<std::size_t> sheet;
		for (std::size_t k = i; k < std::min(count, i + perSheet); ++k) sheet.push_back(k);
		out.push_back(std::move(sheet));
	}
	return out;
}

std::vector<std::vector<std::size_t>> reviewParts(const std::vector<std::uint64_t>& sizes, std::uint64_t limit, std::uint64_t fixedBytes) {
	std::vector<std::vector<std::size_t>> out;
	std::uint64_t used = 0;
	for (std::size_t i = 0; i < sizes.size(); ++i) {
		if (out.empty() || (!out.back().empty() && used + sizes[i] > limit)) {
			out.emplace_back();
			used = fixedBytes;
		}
		out.back().push_back(i);
		used += sizes[i];
	}
	return out;
}

std::string reviewSheetName(std::size_t sheet) {
	char name[32];
	std::snprintf(name, sizeof name, "sheet_%03zu.jpg", sheet + 1);
	return name;
}

} // namespace sme
