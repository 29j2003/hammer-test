// scene_export.cpp - OBJ and glTF export (see scene_export.h).

#include <sme/scene_export.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/studio_model.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace sme {

namespace {

constexpr double METERS_PER_UNIT = 0.0254;

struct Mesh {
	std::vector<float> positions, normals, uvs;   // already converted (meters, Y up, 0..1 UVs)
	std::vector<std::uint32_t> indices;
};

/// Source (Z up, inches) to meters with Y up: (x, y, z) -> (x, z, -y).
void pushPoint(std::vector<float>& out, const Vec3& p, double scale) {
	out.insert(out.end(), {float(p.x * scale), float(p.z * scale), float(-p.y * scale)});
}

std::string sanitize(std::string name) {
	for (auto& c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
	}
	return name;
}

std::string base64(const std::vector<unsigned char>& data) {
	static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < data.size(); i += 3) {
		const unsigned v = (unsigned(data[i]) << 16) | (i + 1 < data.size() ? unsigned(data[i + 1]) << 8 : 0) | (i + 2 < data.size() ? unsigned(data[i + 2]) : 0);
		out += TABLE[(v >> 18) & 63];
		out += TABLE[(v >> 12) & 63];
		out += i + 1 < data.size() ? TABLE[(v >> 6) & 63] : '=';
		out += i + 2 < data.size() ? TABLE[v & 63] : '=';
	}
	return out;
}

std::string jsonString(const std::string& s) {
	std::string out = "\"";
	for (const char c : s) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

} // namespace

std::optional<ExportResult> exportMap(const MapDocument& doc, const EntityCatalog* catalog, const GameFileSystem* fs, const std::filesystem::path& file,
                                      const ExportOptions& options, std::string* error) {
	ExportResult result;
	std::map<std::string, Mesh> meshes;    // by material name ("concrete/wall01")
	std::map<std::string, std::pair<int, int>> sizes;
	std::map<std::string, bool> tools;
	const auto materialInfo = [&](const std::string& raw) {
		const auto name = normalizeMaterialName(raw);
		if (!sizes.contains(name)) {
			std::pair<int, int> size{512, 512};
			bool tool = name.starts_with("tools/");
			if (fs) {
				if (const auto material = loadMaterial(*fs, name)) {
					tool = tool || material->isTool();
					if (const auto data = fs->read("materials/" + normalizeMaterialName(material->previewTexture()) + ".vtf")) {
						if (const auto info = readTextureInfo(*data)) {
							size = {std::max(1, int(info->width)), std::max(1, int(info->height))};
						}
					}
				}
			}
			sizes[name] = size;
			tools[name] = tool;
		}
		return name;
	};

	// Brushes (and displacements) of the world and of brush entities.
	const auto addSolid = [&](const Solid& solid) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			const auto name = materialInfo(side.material);
			if (!options.tools && tools[name]) continue;
			auto& mesh = meshes[name];
			const auto [w, h] = sizes[name];
			const auto addVertex = [&](const Vec3& p, const Vec3& n, const Vec3& base) {
				pushPoint(mesh.positions, p, METERS_PER_UNIT);
				pushPoint(mesh.normals, n, 1.0);
				const auto tc = textureCoordinate(side, base);
				mesh.uvs.insert(mesh.uvs.end(), {float(tc.u / w), float(tc.v / h)});
				return static_cast<std::uint32_t>(mesh.positions.size() / 3 - 1);
			};
			if (auto disp = buildDisplacement(side, face)) {
				const int n = disp->size;
				const auto first = static_cast<std::uint32_t>(mesh.positions.size() / 3);
				for (const auto& p : disp->positions) {
					const Vec3 base = p - face.normal * dot(p - face.vertices[0], face.normal);
					(void)addVertex(p, face.normal, base);
				}
				for (int i = 0; i + 1 < n; ++i) {
					for (int j = 0; j + 1 < n; ++j) {
						const auto a = first + std::uint32_t(i * n + j), b = a + 1, c = a + std::uint32_t(n), d = c + 1;
						mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
						result.triangles += 2;
					}
				}
				continue;
			}
			const auto first = static_cast<std::uint32_t>(mesh.positions.size() / 3);
			for (const auto& v : face.vertices) {
				(void)addVertex(v, face.normal, v);
			}
			for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
				mesh.indices.insert(mesh.indices.end(), {first, first + std::uint32_t(i), first + std::uint32_t(i + 1)});
				++result.triangles;
			}
		}
	};
	for (const auto& solid : doc.world.solids) addSolid(solid);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) addSolid(solid);
	}

	// Models of props and NPCs (reference pose), placed and turned like in the map.
	if (options.models && fs) {
		std::map<std::string, std::optional<ModelGeometry>> cache;
		for (const auto& entity : doc.entities) {
			const auto path = edit::entityModel(entity, catalog);
			if (path.empty()) continue;
			auto [it, inserted] = cache.try_emplace(path);
			if (inserted) {
				std::string modelError;
				it->second = loadModelGeometry(*fs, path, 0, &modelError);
				if (!it->second) result.warnings.push_back(path + ": " + modelError);
			}
			if (!it->second || it->second->empty()) continue;
			const auto& model = *it->second;
			Vec3 angles;
			(void)parseVec3(entity.get("angles", "0 0 0"), angles);
			const Mat3 rotation = edit::anglesToMatrix(angles);
			const Vec3 origin = entity.origin();
			for (const auto& part : model.meshes) {
				const auto name = materialInfo(part.material);
				auto& mesh = meshes[name];
				std::map<std::uint32_t, std::uint32_t> remap;
				for (std::uint32_t k = part.firstIndex; k < part.firstIndex + part.indexCount && k < model.indices.size(); ++k) {
					const auto source = model.indices[k];
					auto [slot, fresh] = remap.try_emplace(source, 0);
					if (fresh) {
						const auto& v = model.vertices[source];
						pushPoint(mesh.positions, origin + rotation * Vec3{v.position[0], v.position[1], v.position[2]}, METERS_PER_UNIT);
						pushPoint(mesh.normals, rotation * Vec3{v.normal[0], v.normal[1], v.normal[2]}, 1.0);
						mesh.uvs.insert(mesh.uvs.end(), {v.uv[0], v.uv[1]});
						slot->second = static_cast<std::uint32_t>(mesh.positions.size() / 3 - 1);
					}
					mesh.indices.push_back(slot->second);
				}
				result.triangles += part.indexCount / 3;
			}
			++result.models;
		}
	}
	std::erase_if(meshes, [](const auto& entry) { return entry.second.indices.empty(); });
	result.materials = meshes.size();

	// Textures as PNG next to the file.
	const auto stem = file.stem().string();
	const auto textureDir = file.parent_path() / (stem + "_textures");
	std::map<std::string, std::string> textureFiles;   // material -> relative path
	if (options.textures && fs) {
		std::error_code ec;
		std::filesystem::create_directories(textureDir, ec);
		for (const auto& [name, mesh] : meshes) {
			const auto material = loadMaterial(*fs, name);
			if (!material) continue;
			const auto image = loadTexture(*fs, material->previewTexture(), 1024);
			if (!image) continue;
			const auto png = encodePng(*image);
			const auto pngName = sanitize(name) + ".png";
			std::ofstream out(textureDir / pngName, std::ios::binary);
			out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
			if (out) {
				textureFiles[name] = stem + "_textures/" + pngName;
				++result.textures;
			}
		}
	}

	std::ofstream out(file, std::ios::binary);
	if (!out) {
		if (error) *error = "cannot write " + file.string();
		return std::nullopt;
	}
	if (options.format == ExportFormat::Obj) {
		const auto mtlName = stem + ".mtl";
		out << "# Exported by Source Map Editor (meters, Y up)\nmtllib " << mtlName << "\n";
		std::size_t base = 1;
		std::ostringstream mtl;
		for (const auto& [name, mesh] : meshes) {
			const auto id = sanitize(name);
			out << "o " << id << "\nusemtl " << id << "\n";
			for (std::size_t i = 0; i < mesh.positions.size(); i += 3) out << "v " << mesh.positions[i] << ' ' << mesh.positions[i + 1] << ' ' << mesh.positions[i + 2] << "\n";
			for (std::size_t i = 0; i < mesh.uvs.size(); i += 2) out << "vt " << mesh.uvs[i] << ' ' << 1.0f - mesh.uvs[i + 1] << "\n";   // OBJ: v up
			for (std::size_t i = 0; i < mesh.normals.size(); i += 3) out << "vn " << mesh.normals[i] << ' ' << mesh.normals[i + 1] << ' ' << mesh.normals[i + 2] << "\n";
			for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
				out << 'f';
				for (int k = 0; k < 3; ++k) {
					const auto n = base + mesh.indices[i + std::size_t(k)];
					out << ' ' << n << '/' << n << '/' << n;
				}
				out << "\n";
			}
			base += mesh.positions.size() / 3;
			mtl << "newmtl " << id << "\nKd 1 1 1\n";
			if (const auto it = textureFiles.find(name); it != textureFiles.end()) mtl << "map_Kd " << it->second << "\n";
			mtl << "\n";
		}
		std::ofstream(file.parent_path() / mtlName, std::ios::binary) << mtl.str();
		return result;
	}

	// glTF 2.0: one mesh with a primitive per material, data embedded as base64.
	std::vector<unsigned char> buffer;
	const auto append = [&](const void* data, std::size_t size) {
		const auto offset = buffer.size();
		buffer.insert(buffer.end(), static_cast<const unsigned char*>(data), static_cast<const unsigned char*>(data) + size);
		while (buffer.size() % 4) buffer.push_back(0);
		return offset;
	};
	std::ostringstream views, accessors, primitives, materials, textures, images;
	int view = 0, accessor = 0, material = 0, texture = 0;
	const auto addView = [&](std::size_t offset, std::size_t length, int target) {
		views << (view ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << offset << ",\"byteLength\":" << length << ",\"target\":" << target << "}";
		return view++;
	};
	for (const auto& [name, mesh] : meshes) {
		const auto vertexCount = mesh.positions.size() / 3;
		float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
		for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
			lo[i % 3] = std::min(lo[i % 3], mesh.positions[i]);
			hi[i % 3] = std::max(hi[i % 3], mesh.positions[i]);
		}
		const int positions = addView(append(mesh.positions.data(), mesh.positions.size() * 4), mesh.positions.size() * 4, 34962);
		const int normals = addView(append(mesh.normals.data(), mesh.normals.size() * 4), mesh.normals.size() * 4, 34962);
		const int uvs = addView(append(mesh.uvs.data(), mesh.uvs.size() * 4), mesh.uvs.size() * 4, 34962);
		const int indices = addView(append(mesh.indices.data(), mesh.indices.size() * 4), mesh.indices.size() * 4, 34963);
		const auto acc = [&](int bufferView, std::size_t count, const char* type, int component, const std::string& extra = {}) {
			accessors << (accessor ? "," : "") << "{\"bufferView\":" << bufferView << ",\"componentType\":" << component << ",\"count\":" << count
			          << ",\"type\":\"" << type << "\"" << extra << "}";
			return accessor++;
		};
		std::ostringstream bounds;
		bounds << ",\"min\":[" << lo[0] << "," << lo[1] << "," << lo[2] << "],\"max\":[" << hi[0] << "," << hi[1] << "," << hi[2] << "]";
		const int p = acc(positions, vertexCount, "VEC3", 5126, bounds.str());
		const int n = acc(normals, vertexCount, "VEC3", 5126);
		const int t = acc(uvs, vertexCount, "VEC2", 5126);
		const int i = acc(indices, mesh.indices.size(), "SCALAR", 5125);
		materials << (material ? "," : "") << "{\"name\":" << jsonString(name) << ",\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1";
		if (const auto it = textureFiles.find(name); it != textureFiles.end()) {
			images << (texture ? "," : "") << "{\"uri\":" << jsonString(it->second) << "}";
			textures << (texture ? "," : "") << "{\"source\":" << texture << "}";
			materials << ",\"baseColorTexture\":{\"index\":" << texture << "}";
			++texture;
		}
		materials << "}}";
		primitives << (material ? "," : "") << "{\"attributes\":{\"POSITION\":" << p << ",\"NORMAL\":" << n << ",\"TEXCOORD_0\":" << t << "},\"indices\":" << i
		           << ",\"material\":" << material << "}";
		++material;
	}
	out << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Source Map Editor\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
	    << "\"nodes\":[{\"name\":" << jsonString(stem) << (meshes.empty() ? "" : ",\"mesh\":0") << "}],";
	if (!meshes.empty()) {
		out << "\"meshes\":[{\"name\":" << jsonString(stem) << ",\"primitives\":[" << primitives.str() << "]}],"
		    << "\"materials\":[" << materials.str() << "],"
		    << "\"accessors\":[" << accessors.str() << "],\"bufferViews\":[" << views.str() << "],";
		if (texture > 0) {
			out << "\"textures\":[" << textures.str() << "],\"images\":[" << images.str() << "],";
		}
	}
	out << "\"buffers\":[{\"byteLength\":" << buffer.size() << ",\"uri\":\"data:application/octet-stream;base64," << base64(buffer) << "\"}]}";
	return result;
}

} // namespace sme
