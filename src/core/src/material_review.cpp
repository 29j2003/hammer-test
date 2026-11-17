// material_review.cpp - Export of the mod's materials for review (see material_review.h).

#include <sme/material_review.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <sme/assets.h>
#include <sme/fidelity_effects.h>
#include <sme/heightmap.h>
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

/// The picture made smaller (box filter, aspect kept) when its longer side is over `maxSize` -
/// a texture without mipmaps comes at its full size.
ImageRgba fitPicture(ImageRgba image, unsigned maxSize) {
	const unsigned w = image.width, h = image.height;
	if (w == 0 || h == 0 || std::max(w, h) <= maxSize || image.pixels.size() < std::size_t(w) * h * 4) return image;
	const double scale = double(maxSize) / std::max(w, h);
	const unsigned nw = std::max(1u, unsigned(std::lround(w * scale))), nh = std::max(1u, unsigned(std::lround(h * scale)));
	ImageRgba out;
	out.width = static_cast<std::uint16_t>(nw);
	out.height = static_cast<std::uint16_t>(nh);
	out.pixels.resize(std::size_t(nw) * nh * 4);
	for (unsigned y = 0; y < nh; ++y) {
		const unsigned y0 = unsigned(std::size_t(y) * h / nh), y1 = std::max(y0 + 1, unsigned(std::size_t(y + 1) * h / nh));
		for (unsigned x = 0; x < nw; ++x) {
			const unsigned x0 = unsigned(std::size_t(x) * w / nw), x1 = std::max(x0 + 1, unsigned(std::size_t(x + 1) * w / nw));
			unsigned sum[4] = {0, 0, 0, 0};
			for (unsigned sy = y0; sy < y1; ++sy) {
				for (unsigned sx = x0; sx < x1; ++sx) {
					for (int c = 0; c < 4; ++c) sum[c] += std::to_integer<unsigned>(image.pixels[(std::size_t(sy) * w + sx) * 4 + std::size_t(c)]);
				}
			}
			const unsigned count = (x1 - x0) * (y1 - y0);
			for (int c = 0; c < 4; ++c) out.pixels[(std::size_t(y) * nw + x) * 4 + std::size_t(c)] = std::byte(static_cast<unsigned char>(sum[c] / count));
		}
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

std::vector<std::string> reviewMaterialList(const GameFileSystem& fs, const ReviewScope& scope, const std::set<std::string>& mapMaterials,
                                            const MaterialDecisions* decisions) {
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
		if (scope.reliefOnly && !reviewCanGetRelief(fs, name, decisions)) continue;
		out.push_back(name);
	}
	std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
		const std::string fa = folderOf(a), fb = folderOf(b);
		return fa != fb ? fa < fb : a < b;
	});
	return out;
}

bool reviewCanGetRelief(const GameFileSystem& fs, std::string_view materialName, const MaterialDecisions* decisions) {
	const std::string name = decisionKey(materialName);
	const auto material = loadMaterial(fs, name);
	if (!material) return false;
	for (const char* flag : {"$translucent", "$additive", "$alphatest", "$decal"}) {
		if (material->flag(flag)) return false;
	}
	if (decisions) {
		if (const auto role = decisions->lookup(name).role) {
			for (const char* fits : {"facade", "wall", "roof", "ground", "shop", "interior"}) {
				if (*role == fits) return true;
			}
			return false;
		}
	}
	// What the picture says (as the building builder reads it).
	std::string base = normalizeMaterialName(material->param("$basetexture"));
	if (base.empty()) return false;
	const auto data = fs.read("materials/" + toLower(base) + ".vtf");
	if (!data) return false;
	int width = 0, height = 0;
	if (const auto info = readTextureInfo(*data)) {
		width = info->width;
		height = info->height;
	}
	const auto small = decodeTexture(*data, 256);
	if (!small) return false;
	const TextureRole role = analyzeTexture(*small, name, width, height).role;
	return role == TextureRole::FacadeWindows || role == TextureRole::Wall || role == TextureRole::Roof || role == TextureRole::ShopFront;
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

void reviewRelief(const GameFileSystem& fs, const std::filesystem::path& modDir, ReviewEntry& e, const MaterialDecisions* decisions, bool mapMaterial,
                  ReviewPictures* pictures) {
	const std::string path = "materials/" + e.name + ".vmt";
	// The material as Game enhancements read it: the game's original, or the mod's own hand-written one.
	std::optional<std::string> vmt;
	bool handWritten = false;
	if (!modDir.empty()) {
		const auto own = modDir / "materials" / std::filesystem::path(e.name + ".vmt");
		std::error_code ec;
		if (std::filesystem::is_regular_file(own, ec)) {
			std::ifstream in(own, std::ios::binary);
			std::ostringstream text;
			text << in.rdbuf();
			if (!isGeneratedMaterial(text.str())) {
				vmt = text.str();
				handWritten = true;
			}
		}
		if (!vmt) vmt = fs.readOriginal(path, modDir);
	}
	if (!vmt) vmt = fs.readText(path);
	ReliefHeights relief;
	relief.source = "none";
	if (!vmt) {
		relief.note = "material not found";
	} else if (handWritten) {
		// Game enhancements never touch it: relief only from its own height map.
		relief.note = "the mod's own material (Game enhancements leave it as written)";
		const auto parsed = parseMaterial(*vmt, e.name);
		const auto source = parsed ? fidelityReliefSource(e.name, *parsed) : std::nullopt;
		if (source && source->kind == ReliefSource::Kind::HeightMap) {
			if (const auto image = loadTexture(fs, source->texture, REVIEW_HEIGHT_MAX)) {
				relief.heights = heightFromImage(*image);
				relief.source = "material";
				relief.depth = fixed(source->depth, 4);
				while (relief.depth.size() > 1 && (relief.depth.back() == '0' || relief.depth.back() == '.')) relief.depth.pop_back();
				relief.note = "the mod's own material with its height map " + toLower(source->texture);
			}
		}
		if (relief.source == "none") {
			// To look at: the heights Mapsmith would make for it (were it the game's).
			MaterialDecision on;
			on.parallax = ParallaxChoice::On;
			relief.heights = reliefHeights(fs, e.name, *vmt, on, mapMaterial, REVIEW_HEIGHT_MAX).heights;
		}
	} else {
		const MaterialDecision decided = decisions ? decisions->lookup(e.name) : MaterialDecision{};
		relief = reliefHeights(fs, e.name, *vmt, decided, mapMaterial, REVIEW_HEIGHT_MAX);
	}
	e.heightSource = relief.source;
	e.reliefDepth = relief.depth;
	e.reliefNote = relief.note;
	e.bumpGenerated = relief.bump;
	e.bumpStrength = relief.bumpStrength;
	if (!pictures) return;
	if (!e.baseTexture.empty()) {
		if (auto image = loadTexture(fs, e.baseTexture, REVIEW_BASE_MAX)) pictures->base = fitPicture(std::move(*image), REVIEW_BASE_MAX);
	}
	if (relief.heights.width > 0) pictures->height = fitPicture(heightImage(relief.heights), REVIEW_HEIGHT_MAX);
	if (!e.bumpTexture.empty()) {
		if (auto image = loadTexture(fs, e.bumpTexture, REVIEW_HEIGHT_MAX)) pictures->normal = fitPicture(std::move(*image), REVIEW_HEIGHT_MAX);
	}
}

std::string reviewPictureName(int number, std::string_view kind, std::string_view extension) {
	char name[32];
	std::snprintf(name, sizeof name, "%03d", number);
	return "pictures/" + std::string(name) + "_" + std::string(kind) + std::string(extension);
}

std::string reviewFlagsText(const ReviewEntry& e) {
	return joined(e.flags, " · ");
}

std::string reviewIndexCsv(const std::vector<ReviewEntry>& entries) {
	std::string out =
		"number,name,shader,width,height,surfaceprop,flags,role,windows_per_tile,floors_per_tile,role_reason,relief_pattern,course_texels,"
		"unit_texels,relief_confidence,parallax_now,parallax_auto,height_source,relief_depth,relief_note,bump_generated,effects,decision,base_texture,"
		"bump_texture,pictures,error\n";
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
			e.heightSource,
			e.reliefDepth,
			e.reliefNote,
			e.bumpGenerated ? "strength " + fixed(e.bumpStrength, 2) : std::string(),
			joined(e.effects, " "),
			e.decision,
			e.baseTexture,
			e.bumpTexture,
			joined(e.pictureFiles, " "),
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
		       ", \"height_source\": " + jsonString(e.heightSource) + ", \"relief_depth\": " + jsonString(e.reliefDepth) +
		       ", \"relief_note\": " + jsonString(e.reliefNote) + ", \"bump_generated\": " + (e.bumpGenerated ? "true" : "false") +
		       ", \"bump_strength\": " + fixed(e.bumpStrength, 2) + ", \"pictures\": " + list(e.pictureFiles) +
		       ", \"effects\": " + list(e.effects) + ", \"decision\": " + jsonString(e.decision) + ", \"base_texture\": " + jsonString(e.baseTexture) +
		       ", \"bump_texture\": " + jsonString(e.bumpTexture) + ", \"error\": " + jsonString(e.error) + "}";
		out += i + 1 < entries.size() ? ",\n" : "\n";
	}
	return out + "]\n";
}

std::string reviewReadme(int materials, int sheets, int parts, std::string_view game, bool pictures) {
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
	    << "  and what the decision file already says. height_source says where the relief's heights come from now:\n"
	    << "  pattern (Mapsmith's relief from the bricks/tiles/boards it found), material (the material's own height map),\n"
	    << "  normal (deploy.bat's relief from the normal map), brightness (from the texture's brightness) or none (no\n"
	    << "  relief; the height picture then shows what parallax=on would use). relief_depth is the $parallaxdepth used,\n"
	    << "  bump_generated whether Mapsmith generates a bump map from the same heights (and how steep).\n";
	if (pictures) {
		out << "- pictures/NNN_base.jpg: the base texture at its own resolution (up to 1024 on the longer side). Look at it\n"
		    << "  big: is it really bricks, tiles or boards, and where are the joints?\n"
		    << "- pictures/NNN_height.jpg: exactly the heights the game's relief uses (up to 512): white is out, black is in.\n"
		    << "  The joints should be dark and the faces light and even; a height that follows dirt or paint instead of\n"
		    << "  the shape makes a wrong relief (then pattern=none, a different depth or parallax=off).\n"
		    << "- pictures/NNN_normal.jpg: the material's own normal map (up to 512), when it has one.\n"
		    << "  NNN is the material's number in the index and on the sheets. Each part has the pictures of its sheets.\n";
	}
	out << "\n"
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
	    << "- parallax: auto | on | off; depth: 0 .. 0.1 (0.006 shallow, 0.01 the usual); pattern: brick | tile | plank | stone | none\n"	    << "- bump: auto | generate | off - a bump map generated from the relief's heights (auto: where the material has\n"
	    << "  none and Mapsmith makes the relief); bumpstrength: 0.1 .. 4 (1 is as steep as the relief)\n"
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
