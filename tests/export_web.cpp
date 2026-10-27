// export_web.cpp - Exports the demo map with the editor core into one JSON file for the browser
// preview: per-face triangles (positions, normals, texel UVs), textures as PNG data URIs,
// material categories, entities with their FGD definitions, model sequences and the problems
// found by checkMap. Used only to build the interactive preview page.
//   sme-export-web FIXTURE_DIR OUT.json

#include "fixture.h"

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/editing.h>
#include <sme/game.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace sme;

namespace {

std::string base64(const std::vector<std::byte>& data) {
	static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	std::size_t i = 0;
	for (; i + 2 < data.size(); i += 3) {
		const unsigned n = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8) | unsigned(data[i + 2]);
		out += table[(n >> 18) & 63];
		out += table[(n >> 12) & 63];
		out += table[(n >> 6) & 63];
		out += table[n & 63];
	}
	if (i + 1 == data.size()) {
		const unsigned n = unsigned(data[i]) << 16;
		out += table[(n >> 18) & 63];
		out += table[(n >> 12) & 63];
		out += "==";
	} else if (i + 2 == data.size()) {
		const unsigned n = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8);
		out += table[(n >> 18) & 63];
		out += table[(n >> 12) & 63];
		out += table[(n >> 6) & 63];
		out += '=';
	}
	return out;
}

std::string quote(std::string_view s) {
	std::string out = "\"";
	for (const char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\n') {
			out += "\\n";
		} else if (static_cast<unsigned char>(c) < 0x20) {
			out += ' ';
		} else {
			out += c;
		}
	}
	return out + "\"";
}

std::string num(double v) {
	return formatNumber(std::round(v * 1000) / 1000);
}

std::string vec(const Vec3& v) {
	return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]";
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "usage: sme-export-web FIXTURE_DIR OUT.json\n";
		return 1;
	}
	test::Fixture f;
	test::createFixture(f, argv[1]);
	test::createDemo(f);
	const auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "sourcemods/mymod") entry = &g;
	}
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	const auto doc = MapDocument::parse(*readFileText(f.mod / "mapsrc/demo.vmf"));

	std::ostringstream json;
	json << "{";

	// Materials: everything in the game (for the browser) with categories and textures.
	std::set<std::string> materialNames;
	for (const auto& path : game->files.list("materials", ".vmt")) {
		materialNames.insert(normalizeMaterialName(path));
	}
	json << "\"materials\":{";
	bool first = true;
	for (const auto& name : materialNames) {
		const auto material = loadMaterial(game->files, name);
		if (!material) continue;
		const auto category = classifyMaterial(*material);
		const auto image = loadTexture(game->files, material->previewTexture(), 256);
		std::string second;
		if (const auto t2 = material->param("$basetexture2"); !t2.empty()) {
			if (const auto image2 = loadTexture(game->files, t2, 256)) {
				second = "data:image/png;base64," + base64(encodePng(*image2));
			}
		}
		const char* kind = material->flag("$translucent") || material->flag("%compiletrigger") ? "translucent" : "opaque";
		json << (first ? "" : ",") << quote(name) << ":{\"shader\":" << quote(material->shader) << ",\"kind\":\"" << kind << "\""
		     << ",\"tool\":" << (category.tool ? "true" : "false") << ",\"blend\":" << (category.blend ? "true" : "false")
		     << ",\"model\":" << (category.model ? "true" : "false") << ",\"translucent\":" << (category.translucent ? "true" : "false")
		     << ",\"brush\":" << (category.brush ? "true" : "false") << ",\"sky\":" << (material->flag("%compilesky") ? "true" : "false");
		if (image) {
			json << ",\"w\":" << image->width << ",\"h\":" << image->height << ",\"png\":\"data:image/png;base64," << base64(encodePng(*image)) << "\"";
		}
		if (!second.empty()) json << ",\"png2\":\"" << second << "\"";
		json << "}";
		first = false;
	}
	json << "},";

	// Faces with triangles; UVs in texels (divided by the texture size in the page).
	json << "\"faces\":[";
	first = true;
	const auto writeSolid = [&](const Solid& solid, int entityId) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			std::vector<Vec3> pos, nor;
			std::vector<double> uv, alpha;
			if (const auto disp = buildDisplacement(side, face)) {
				const int n = disp->size;
				std::vector<Vec3> normals(disp->positions.size());
				for (int i = 0; i + 1 < n; ++i) for (int j = 0; j + 1 < n; ++j) {
					const int a = i * n + j, b = a + 1, c = a + n, d = c + 1;
					for (auto tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
						const Vec3 tn = cross(disp->positions[tri[1]] - disp->positions[tri[0]], disp->positions[tri[2]] - disp->positions[tri[0]]);
						for (int k : tri) normals[k] += tn;
					}
				}
				Vec3 sum;
				for (const auto& nn : normals) sum += nn;
				const bool flip = dot(sum, face.normal) < 0;
				for (int i = 0; i + 1 < n; ++i) for (int j = 0; j + 1 < n; ++j) {
					const int a = i * n + j, b = a + 1, c = a + n, d = c + 1;
					for (auto tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
						if (flip) std::swap(tri[1], tri[2]);
						for (int k : tri) {
							const Vec3& p = disp->positions[k];
							const Vec3 base = p - face.normal * dot(p - face.vertices[0], face.normal);
							const auto tc = textureCoordinate(side, base);
							pos.push_back(p);
							nor.push_back(normalize(flip ? -normals[k] : normals[k]));
							uv.push_back(tc.u);
							uv.push_back(tc.v);
							alpha.push_back(disp->alpha[k]);
						}
					}
				}
			} else {
				for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
					for (const auto* v : {&face.vertices[0], &face.vertices[i], &face.vertices[i + 1]}) {
						const auto tc = textureCoordinate(side, *v);
						pos.push_back(*v);
						nor.push_back(face.normal);
						uv.push_back(tc.u);
						uv.push_back(tc.v);
						alpha.push_back(0);
					}
				}
			}
			json << (first ? "" : ",") << "{\"solid\":" << solid.id << ",\"entity\":" << entityId << ",\"side\":" << side.id
			     << ",\"material\":" << quote(normalizeMaterialName(side.material)) << ",\"normal\":" << vec(face.normal) << ",\"p\":[";
			for (std::size_t k = 0; k < pos.size(); ++k) json << (k ? "," : "") << num(pos[k].x) << "," << num(pos[k].y) << "," << num(pos[k].z);
			json << "],\"n\":[";
			for (std::size_t k = 0; k < nor.size(); ++k) json << (k ? "," : "") << num(nor[k].x) << "," << num(nor[k].y) << "," << num(nor[k].z);
			json << "],\"uv\":[";
			for (std::size_t k = 0; k < uv.size(); ++k) json << (k ? "," : "") << num(uv[k]);
			json << "],\"a\":[";
			for (std::size_t k = 0; k < alpha.size(); ++k) json << (k ? "," : "") << num(alpha[k]);
			json << "],\"u\":" << vec(side.u.axis) << ",\"us\":" << num(side.u.scale) << ",\"ush\":" << num(side.u.shift)
			     << ",\"v\":" << vec(side.v.axis) << ",\"vs\":" << num(side.v.scale) << ",\"vsh\":" << num(side.v.shift) << "}";
			first = false;
		}
	};
	for (const auto& solid : doc.world.solids) writeSolid(solid, 0);
	for (const auto& entity : doc.entities) for (const auto& solid : entity.solids) writeSolid(solid, entity.id);
	json << "],";

	// Entities with keys, outputs and box.
	json << "\"entities\":[";
	first = true;
	for (const auto& entity : doc.entities) {
		const auto box = edit::entityBounds(entity, &game->entities);
		json << (first ? "" : ",") << "{\"id\":" << entity.id << ",\"brush\":" << (entity.solids.empty() ? "false" : "true")
		     << ",\"min\":" << vec(box.min) << ",\"max\":" << vec(box.max) << ",\"keys\":[";
		for (std::size_t k = 0; k < entity.keyValues.size(); ++k) {
			json << (k ? "," : "") << "[" << quote(entity.keyValues[k].first) << "," << quote(entity.keyValues[k].second) << "]";
		}
		json << "],\"outputs\":[";
		for (std::size_t k = 0; k < entity.connections.size(); ++k) {
			const auto& c = entity.connections[k];
			json << (k ? "," : "") << "[" << quote(c.output) << "," << quote(c.target) << "," << quote(c.input) << "," << quote(c.parameter) << ","
			     << num(c.delay) << "," << c.timesToFire << "]";
		}
		json << "]}";
		first = false;
	}
	json << "],";

	// Entity classes (flattened) for the smart editor.
	json << "\"classes\":{";
	first = true;
	for (const auto* cls : game->entities.placeable()) {
		json << (first ? "" : ",") << quote(cls->name) << ":{\"type\":" << quote(cls->classType) << ",\"desc\":" << quote(cls->description)
		     << ",\"color\":" << quote(cls->helper("color")) << ",\"studio\":" << quote(cls->helper("studio")) << ",\"props\":[";
		for (std::size_t k = 0; k < cls->properties.size(); ++k) {
			const auto& p = cls->properties[k];
			json << (k ? "," : "") << "{\"name\":" << quote(p.name) << ",\"type\":" << quote(p.type) << ",\"label\":" << quote(p.displayName)
			     << ",\"def\":" << quote(p.defaultValue) << ",\"desc\":" << quote(p.description) << ",\"choices\":[";
			for (std::size_t c = 0; c < p.choices.size(); ++c) json << (c ? "," : "") << "[" << quote(p.choices[c].value) << "," << quote(p.choices[c].displayName) << "]";
			json << "],\"flags\":[";
			for (std::size_t c = 0; c < p.flags.size(); ++c) json << (c ? "," : "") << "[" << p.flags[c].value << "," << quote(p.flags[c].displayName) << "]";
			json << "]}";
		}
		json << "],\"inputs\":[";
		for (std::size_t k = 0; k < cls->inputs.size(); ++k) json << (k ? "," : "") << quote(cls->inputs[k].name);
		json << "],\"outputs\":[";
		for (std::size_t k = 0; k < cls->outputs.size(); ++k) json << (k ? "," : "") << quote(cls->outputs[k].name);
		json << "]}";
		first = false;
	}
	json << "},";

	// Sequences of the demo model, sounds, models.
	json << "\"sequences\":{";
	json << "\"models/humans/group01/male_07.mdl\":[";
	const auto sequences = modelSequences(game->files, "models/humans/group01/male_07.mdl");
	for (std::size_t k = 0; k < sequences.size(); ++k) json << (k ? "," : "") << quote(sequences[k]);
	json << "]},\"sounds\":[";
	const auto sounds = soundScriptNames(game->files);
	for (std::size_t k = 0; k < sounds.size(); ++k) json << (k ? "," : "") << quote(sounds[k]);
	json << "],";

	// Problems as the editor finds them.
	std::vector<std::string> names(materialNames.begin(), materialNames.end());
	const CheckContext context{&game->entities, &game->files, &names};
	json << "\"problems\":[";
	first = true;
	for (const auto& p : checkMap(doc, context)) {
		const char* kinds[] = {"InvalidBrush", "MissingMaterial", "UnknownClass", "BrushEntityWithoutBrushes", "PointClassWithBrushes", "OutputTargetMissing",
		                       "UnknownOutput", "UnknownInput", "NoPlayerStart", "NoLights", "BadTextureScale", "MissingModel", "Leak"};
		const char* severities[] = {"error", "warning", "info"};
		json << (first ? "" : ",") << "{\"kind\":\"" << kinds[static_cast<int>(p.kind)] << "\",\"severity\":\"" << severities[static_cast<int>(p.severity)]
		     << "\",\"message\":" << quote(p.message) << ",\"fix\":" << quote(p.howToFix) << ",\"fixLabel\":" << quote(p.fixLabel)
		     << ",\"from\":" << quote(p.from) << ",\"to\":" << quote(p.to) << ",\"entity\":" << p.entityId << ",\"connection\":["
		     << quote(p.connection.output) << "," << quote(p.connection.target) << "," << quote(p.connection.input) << "],\"solids\":[";
		std::set<int> solids;
		for (const auto& face : p.objects.faces) solids.insert(face.solid.solidId);
		for (const auto& s : p.objects.solids) solids.insert(s.solidId);
		bool f2 = true;
		for (const int s : solids) {
			json << (f2 ? "" : ",") << s;
			f2 = false;
		}
		json << "]}";
		first = false;
	}
	json << "]}";

	std::ofstream(argv[2], std::ios::binary) << json.str();
	std::cout << "exported " << json.str().size() / 1024 << " KiB\n";
	return 0;
}
