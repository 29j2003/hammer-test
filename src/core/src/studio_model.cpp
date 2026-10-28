// studio_model.cpp - MDL/VVD/VTX triangles (see studio_model.h).
//
// Layouts from the Source SDK 2013 headers studio.h (studiohdr_t, mstudiobodyparts_t,
// mstudiomodel_t, mstudiomesh_t), studio.h/vertexFileHeader_t and optimize.h (pack(1)).

#include <sme/studio_model.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <cstring>

namespace sme {

namespace {

// studiohdr_t
constexpr std::size_t HDR_VERSION = 4;
constexpr std::size_t HDR_NUM_TEXTURES = 204, HDR_TEXTURE_INDEX = 208;
constexpr std::size_t HDR_NUM_CDTEXTURES = 212, HDR_CDTEXTURE_INDEX = 216;
constexpr std::size_t HDR_NUM_SKINREF = 220, HDR_NUM_SKINFAMILIES = 224, HDR_SKIN_INDEX = 228;
constexpr std::size_t HDR_NUM_BODYPARTS = 232, HDR_BODYPART_INDEX = 236;
constexpr std::size_t HDR_SIZE = 408;
constexpr std::size_t TEXTURE_SIZE = 64;
constexpr std::size_t BODYPART_SIZE = 16;     // sznameindex, nummodels, base, modelindex
constexpr std::size_t MODEL_SIZE = 148;       // name[64], type, radius, nummeshes 72, meshindex 76, numvertices 80, vertexindex 84, ...
constexpr std::size_t MESH_SIZE = 116;        // material 0, modelindex 4, numvertices 8, vertexoffset 12, ...

// vertexFileHeader_t
constexpr std::size_t VVD_NUM_LOD_VERTS = 16, VVD_NUM_FIXUPS = 48, VVD_FIXUP_START = 52, VVD_VERTEX_START = 56, VVD_HEADER = 64;
constexpr std::size_t VVD_VERTEX_SIZE = 48;   // bone weights 16, position 16, normal 28, uv 40
constexpr std::size_t VVD_FIXUP_SIZE = 12;    // lod, sourceVertexID, numVertexes

// optimize.h (packed)
constexpr std::size_t VTX_NUM_BODYPARTS = 28, VTX_BODYPART_OFFSET = 32, VTX_HEADER = 36;
constexpr std::size_t VTX_VERTEX_SIZE = 9;    // boneWeightIndex[3], numBones, origMeshVertID (u16) 4, boneID[3]
constexpr std::size_t VTX_MESH_SIZE = 9;      // numStripGroups, stripGroupHeaderOffset, flags
constexpr std::uint8_t STRIP_IS_TRISTRIP = 0x02;

constexpr int MAX_COUNT = 1 << 20;
constexpr std::size_t MAX_VERTICES = 1 << 21;
constexpr std::size_t MAX_INDICES = 1 << 23;

class Reader {
public:
	explicit Reader(std::span<const std::byte> data) : m_data(data) {}
	[[nodiscard]] bool fits(long long offset, long long size) const {
		return offset >= 0 && size >= 0 && static_cast<unsigned long long>(offset) <= m_data.size() &&
		       static_cast<unsigned long long>(size) <= m_data.size() - static_cast<std::size_t>(offset);
	}
	template <typename T>
	[[nodiscard]] T get(long long offset) const {
		T v{};
		if (fits(offset, sizeof(T))) {
			std::memcpy(&v, m_data.data() + offset, sizeof(T));
		}
		return v;
	}
	[[nodiscard]] int i32(long long offset) const { return get<std::int32_t>(offset); }
	[[nodiscard]] std::string str(long long offset) const {
		std::string out;
		if (!fits(offset, 1)) {
			return out;
		}
		for (auto i = static_cast<std::size_t>(offset); i < m_data.size() && out.size() < 260; ++i) {
			const char c = static_cast<char>(m_data[i]);
			if (c == '\0') break;
			out.push_back(c);
		}
		return out;
	}

private:
	std::span<const std::byte> m_data;
};

/// A count read from a file: in range or -1.
int count(int value, int maximum = MAX_COUNT) {
	return value >= 0 && value <= maximum ? value : -1;
}

struct StripLayout {
	std::size_t groupSize;   // 25 (Source 2013) or 33 (later branches with topology data)
	std::size_t stripSize;   // 27 or 35
};

/// Triangles of one VTX mesh as mesh-relative vertex ids. False if the layout does not fit.
bool readVtxMesh(const Reader& vtx, long long mesh, const StripLayout& layout, std::vector<std::uint32_t>& out) {
	const int groups = count(vtx.i32(mesh), 64);
	const long long groupStart = mesh + vtx.i32(mesh + 4);
	if (groups < 0 || !vtx.fits(groupStart, static_cast<long long>(groups) * static_cast<long long>(layout.groupSize))) {
		return false;
	}
	for (int g = 0; g < groups; ++g) {
		const long long group = groupStart + static_cast<long long>(g) * static_cast<long long>(layout.groupSize);
		const int numVerts = count(vtx.i32(group));
		const long long verts = group + vtx.i32(group + 4);
		const int numIndices = count(vtx.i32(group + 8));
		const long long indices = group + vtx.i32(group + 12);
		const int numStrips = count(vtx.i32(group + 16), 4096);
		const long long strips = group + vtx.i32(group + 20);
		if (numVerts < 0 || numIndices < 0 || numStrips < 0 || !vtx.fits(verts, static_cast<long long>(numVerts) * VTX_VERTEX_SIZE) ||
		    !vtx.fits(indices, static_cast<long long>(numIndices) * 2) ||
		    !vtx.fits(strips, static_cast<long long>(numStrips) * static_cast<long long>(layout.stripSize))) {
			return false;
		}
		const auto vertexId = [&](int index) -> std::optional<std::uint32_t> {
			const auto local = vtx.get<std::uint16_t>(indices + static_cast<long long>(index) * 2);
			if (local >= numVerts) {
				return std::nullopt;
			}
			return vtx.get<std::uint16_t>(verts + static_cast<long long>(local) * VTX_VERTEX_SIZE + 4);
		};
		const auto addTriangles = [&](int first, int n, bool strip) {
			if (first < 0 || n < 0 || first + n > numIndices) {
				return false;
			}
			if (!strip) {
				if (n % 3 != 0) return false;
				for (int i = first; i < first + n; ++i) {
					const auto v = vertexId(i);
					if (!v) return false;
					out.push_back(*v);
				}
				return true;
			}
			for (int i = first; i + 2 < first + n; ++i) {
				const auto a = vertexId(i), b = vertexId(i + 1), c = vertexId(i + 2);
				if (!a || !b || !c) return false;
				if (*a == *b || *b == *c || *a == *c) continue;   // degenerate joins
				const bool odd = (i - first) % 2 == 1;
				out.insert(out.end(), {*a, odd ? *c : *b, odd ? *b : *c});
			}
			return true;
		};
		if (numStrips == 0) {
			if (!addTriangles(0, numIndices, false)) return false;
			continue;
		}
		for (int s = 0; s < numStrips; ++s) {
			const long long strip = strips + static_cast<long long>(s) * static_cast<long long>(layout.stripSize);
			const int n = vtx.i32(strip);
			const int first = vtx.i32(strip + 4);
			const auto flags = vtx.get<std::uint8_t>(strip + 18);
			if (!addTriangles(first, n, flags & STRIP_IS_TRISTRIP)) return false;
		}
		if (out.size() > MAX_INDICES) {
			return false;
		}
	}
	return true;
}

} // namespace

std::optional<ModelGeometry> buildModelGeometry(const ModelFiles& files, int skin, std::string* error,
                                                const std::function<bool(const std::string&)>& materialExists, int body) {
	const auto fail = [&](const std::string& message) -> std::optional<ModelGeometry> {
		if (error) *error = message;
		return std::nullopt;
	};
	const Reader mdl(files.mdl), vvd(files.vvd), vtx(files.vtx);
	if (!mdl.fits(0, HDR_SIZE) || std::memcmp(files.mdl.data(), "IDST", 4) != 0) return fail("not a Source model (.mdl)");
	const int version = mdl.i32(HDR_VERSION);
	if (version < 44 || version > 49) return fail("unsupported model version " + std::to_string(version));
	if (mdl.i32(HDR_NUM_BODYPARTS) == 0) {
		return ModelGeometry{};   // animations only ($includemodel targets like male_shared.mdl)
	}
	if (!vvd.fits(0, VVD_HEADER) || std::memcmp(files.vvd.data(), "IDSV", 4) != 0) return fail("vertex file (.vvd) missing or broken");
	if (!vtx.fits(0, VTX_HEADER) || vtx.i32(0) != 7) return fail("triangle file (.vtx) missing or broken");

	// Vertices of LOD 0 (with the fixup table when there is one).
	const int lodVerts = count(vvd.i32(VVD_NUM_LOD_VERTS), static_cast<int>(MAX_VERTICES));
	const int numFixups = count(vvd.i32(VVD_NUM_FIXUPS));
	const long long fixups = vvd.i32(VVD_FIXUP_START);
	const long long vertexStart = vvd.i32(VVD_VERTEX_START);
	if (lodVerts < 0 || numFixups < 0 || !vvd.fits(vertexStart, static_cast<long long>(lodVerts) * VVD_VERTEX_SIZE) ||
	    !vvd.fits(fixups, static_cast<long long>(numFixups) * VVD_FIXUP_SIZE)) {
		return fail("vertex file (.vvd) out of range");
	}
	ModelGeometry geometry;
	const auto readVertex = [&](long long index) {
		const long long base = vertexStart + index * static_cast<long long>(VVD_VERTEX_SIZE);
		ModelVertex v{};
		for (int k = 0; k < 3; ++k) {
			v.position[k] = vvd.get<float>(base + 16 + k * 4);
			v.normal[k] = vvd.get<float>(base + 28 + k * 4);
		}
		v.uv[0] = vvd.get<float>(base + 40);
		v.uv[1] = vvd.get<float>(base + 44);
		return v;
	};
	if (numFixups == 0) {
		geometry.vertices.reserve(static_cast<std::size_t>(lodVerts));
		for (int i = 0; i < lodVerts; ++i) {
			geometry.vertices.push_back(readVertex(i));
		}
	} else {
		for (int f = 0; f < numFixups; ++f) {
			const long long fixup = fixups + static_cast<long long>(f) * VVD_FIXUP_SIZE;
			const int source = vvd.i32(fixup + 4), n = vvd.i32(fixup + 8);
			if (vvd.i32(fixup) < 0 || source < 0 || n < 0 || !vvd.fits(vertexStart, (static_cast<long long>(source) + n) * VVD_VERTEX_SIZE) ||
			    geometry.vertices.size() + static_cast<std::size_t>(n) > MAX_VERTICES) {
				return fail("vertex fixups out of range");
			}
			for (int i = 0; i < n; ++i) {
				geometry.vertices.push_back(readVertex(source + i));
			}
		}
	}

	// Material names per texture: the first $cdmaterials folder that has it.
	const int numTextures = count(mdl.i32(HDR_NUM_TEXTURES), 4096);
	const long long textureIndex = mdl.i32(HDR_TEXTURE_INDEX);
	const int numCd = count(mdl.i32(HDR_NUM_CDTEXTURES), 256);
	const long long cdIndex = mdl.i32(HDR_CDTEXTURE_INDEX);
	std::vector<std::string> folders;
	if (numCd > 0 && mdl.fits(cdIndex, static_cast<long long>(numCd) * 4)) {
		for (int i = 0; i < numCd; ++i) {
			auto folder = normalizePath(mdl.str(mdl.i32(cdIndex + i * 4)));
			if (!folder.empty() && folder.back() != '/') folder += '/';
			folders.push_back(std::move(folder));
		}
	}
	if (folders.empty()) folders.emplace_back();
	std::vector<std::string> textures;
	if (numTextures > 0 && mdl.fits(textureIndex, static_cast<long long>(numTextures) * TEXTURE_SIZE)) {
		for (int i = 0; i < numTextures; ++i) {
			const long long base = textureIndex + static_cast<long long>(i) * TEXTURE_SIZE;
			const auto name = normalizePath(mdl.str(base + mdl.i32(base)));
			std::string resolved = folders.front() + name;
			if (materialExists) {
				for (const auto& folder : folders) {
					if (materialExists(folder + name)) {
						resolved = folder + name;
						break;
					}
				}
			}
			textures.push_back(std::move(resolved));
		}
	}
	// Skin table: skins[family][skinref] -> texture.
	const int numSkinRef = count(mdl.i32(HDR_NUM_SKINREF), 4096);
	const int numFamilies = count(mdl.i32(HDR_NUM_SKINFAMILIES), 4096);
	const long long skinIndex = mdl.i32(HDR_SKIN_INDEX);
	const bool haveSkins = numSkinRef > 0 && numFamilies > 0 && mdl.fits(skinIndex, static_cast<long long>(numSkinRef) * numFamilies * 2);
	const int family = std::clamp(skin, 0, std::max(0, numFamilies - 1));
	const auto materialOf = [&](int skinRef) -> std::string {
		int texture = skinRef;
		if (haveSkins && skinRef >= 0 && skinRef < numSkinRef) {
			texture = mdl.get<std::int16_t>(skinIndex + (static_cast<long long>(family) * numSkinRef + skinRef) * 2);
		}
		return texture >= 0 && texture < static_cast<int>(textures.size()) ? textures[static_cast<std::size_t>(texture)] : std::string("tools/toolsnodraw");
	};

	// Body parts (one model of each, chosen by `body`) with their meshes, matched with the VTX tree.
	const int numBodyParts = count(mdl.i32(HDR_NUM_BODYPARTS), 256);
	const long long bodyPartIndex = mdl.i32(HDR_BODYPART_INDEX);
	if (numBodyParts < 0 || !mdl.fits(bodyPartIndex, static_cast<long long>(numBodyParts) * BODYPART_SIZE)) return fail("body parts out of range");
	if (vtx.i32(VTX_NUM_BODYPARTS) != numBodyParts) return fail("model files do not belong together (.vtx body parts)");
	const long long vtxBodyParts = vtx.i32(VTX_BODYPART_OFFSET);

	for (const StripLayout layout : {StripLayout{25, 27}, StripLayout{33, 35}}) {
		geometry.indices.clear();
		geometry.meshes.clear();
		bool ok = vtx.fits(vtxBodyParts, static_cast<long long>(numBodyParts) * 8);
		for (int b = 0; ok && b < numBodyParts; ++b) {
			const long long part = bodyPartIndex + static_cast<long long>(b) * BODYPART_SIZE;
			const int numModels = count(mdl.i32(part + 4), 256);
			const int base = std::max(1, mdl.i32(part + 8));
			const int choice = numModels > 0 ? (std::max(0, body) / base) % numModels : 0;
			const long long modelIndex = part + mdl.i32(part + 12) + static_cast<long long>(choice) * MODEL_SIZE;
			const long long vtxPart = vtxBodyParts + static_cast<long long>(b) * 8;
			if (numModels <= 0) continue;
			if (!mdl.fits(modelIndex, MODEL_SIZE) || vtx.i32(vtxPart) <= choice) { ok = false; break; }
			const long long vtxModel = vtxPart + vtx.i32(vtxPart + 4) + static_cast<long long>(choice) * 8;
			const int lods = vtx.i32(vtxModel);
			const long long vtxLod = vtxModel + vtx.i32(vtxModel + 4);
			if (lods < 1 || !vtx.fits(vtxLod, 12)) { ok = false; break; }
			const int numMeshes = count(mdl.i32(modelIndex + 72), 4096);
			const long long meshIndex = modelIndex + mdl.i32(modelIndex + 76);
			const long long modelVertexBase = mdl.i32(modelIndex + 84) / static_cast<long long>(VVD_VERTEX_SIZE);
			const long long vtxMeshes = vtxLod + vtx.i32(vtxLod + 4);
			if (numMeshes < 0 || vtx.i32(vtxLod) != numMeshes || !mdl.fits(meshIndex, static_cast<long long>(numMeshes) * MESH_SIZE) ||
			    !vtx.fits(vtxMeshes, static_cast<long long>(numMeshes) * VTX_MESH_SIZE)) {
				ok = false;
				break;
			}
			for (int m = 0; ok && m < numMeshes; ++m) {
				const long long mesh = meshIndex + static_cast<long long>(m) * MESH_SIZE;
				const long long meshVertexBase = modelVertexBase + mdl.i32(mesh + 12);
				const int meshVertexCount = mdl.i32(mesh + 8);
				std::vector<std::uint32_t> local;
				if (!readVtxMesh(vtx, vtxMeshes + static_cast<long long>(m) * VTX_MESH_SIZE, layout, local)) {
					ok = false;
					break;
				}
				ModelMesh out;
				out.material = materialOf(mdl.i32(mesh));
				out.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
				for (const auto id : local) {
					const long long global = meshVertexBase + id;
					if (static_cast<int>(id) >= meshVertexCount || global < 0 || global >= static_cast<long long>(geometry.vertices.size())) {
						ok = false;
						break;
					}
					geometry.indices.push_back(static_cast<std::uint32_t>(global));
				}
				out.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - out.firstIndex;
				if (out.indexCount > 0) {
					geometry.meshes.push_back(std::move(out));
				}
			}
		}
		if (ok) {
			for (const auto& v : geometry.vertices) {
				geometry.bounds.add({v.position[0], v.position[1], v.position[2]});
			}
			// Merge meshes that share a material: fewer draw calls.
			std::stable_sort(geometry.meshes.begin(), geometry.meshes.end(), [](const auto& a, const auto& b) { return a.material < b.material; });
			return geometry;
		}
	}
	return fail("triangle file (.vtx) does not match the model");
}

std::optional<ModelGeometry> loadModelGeometry(const GameFileSystem& fs, std::string_view modelPath, int skin, std::string* error, int body) {
	const auto path = normalizePath(modelPath);
	if (!endsWithI(path, ".mdl")) {
		if (error) *error = "not a .mdl file";
		return std::nullopt;
	}
	const auto stem = path.substr(0, path.size() - 4);
	const auto mdl = fs.read(path);
	if (!mdl) {
		if (error) *error = path + " not found";
		return std::nullopt;
	}
	const auto vvd = fs.read(stem + ".vvd");
	auto vtx = fs.read(stem + ".dx90.vtx");
	if (!vtx) vtx = fs.read(stem + ".vtx");
	if (!vtx) vtx = fs.read(stem + ".dx80.vtx");
	const std::vector<std::byte> empty;
	const std::function<bool(const std::string&)> exists = [&](const std::string& material) { return fs.exists("materials/" + material + ".vmt"); };
	return buildModelGeometry({*mdl, vvd ? *vvd : empty, vtx ? *vtx : empty}, skin, error, exists, body);
}

} // namespace sme
