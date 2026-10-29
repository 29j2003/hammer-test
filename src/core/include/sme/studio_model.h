// studio_model.h - Triangles of a Source 1 model for the viewport and the model previews.
//
// Reads the three files the engine uses: .mdl (body parts, meshes, skins, material folders),
// .vvd (vertex positions, normals, texture coordinates) and .dx90.vtx (triangle lists per
// mesh, LOD 0). The model is shown in its reference pose (no animation), with one chosen model
// per body group (the defaults unless a "body" number says otherwise). All files are
// untrusted: every offset and count is checked, broken files give an error, never a crash.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

#include <sme/math.h>

namespace sme {

class GameFileSystem;

/// One vertex as the renderer uploads it.
struct ModelVertex {
	float position[3];
	float normal[3];
	float uv[2];
};

/// Which bones move a vertex and how much (the .vvd's mstudioboneweight_t; up to three).
struct ModelVertexWeights {
	float weight[3] = {1, 0, 0};
	std::uint8_t bone[3] = {0, 0, 0};
	std::uint8_t count = 0;   ///< 0: the vertex follows no bone
};

/// Triangles of one material.
struct ModelMesh {
	std::string material;          ///< Material name for the cache ("models/props/crate01"), resolved through $cdmaterials
	std::uint32_t firstIndex = 0;  ///< Range in ModelGeometry::indices
	std::uint32_t indexCount = 0;
};

/// A model's triangles (LOD 0) ready for drawing, with its bounds.
struct ModelGeometry {
	std::vector<ModelVertex> vertices;
	std::vector<ModelVertexWeights> weights;   ///< One per vertex (skinning for animation previews)
	std::vector<std::uint32_t> indices;   ///< Triangle list
	std::vector<ModelMesh> meshes;
	Bounds bounds;

	/// Number of triangles.
	[[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
	/// Models that only hold animations for other models have no triangles.
	[[nodiscard]] bool empty() const { return indices.empty(); }
};

/// The raw files of a model (.mdl, .vvd, .vtx), owned by the caller.
struct ModelFiles {
	std::span<const std::byte> mdl, vvd, vtx;
};

/// Builds the geometry from the file contents. `materialExists("models/props/crate01")` picks
/// the $cdmaterials folder that holds a texture (empty function: the first folder). `body` is the
/// engine's body number choosing one model per body group (ModelInfo::bodyValue; 0 = defaults).
[[nodiscard]] std::optional<ModelGeometry> buildModelGeometry(const ModelFiles& files, int skin, std::string* error = nullptr,
                                                              const std::function<bool(const std::string&)>& materialExists = {}, int body = 0);

/// The triangles of one model (`model`, an option of a body group) of one body part alone, with
/// the first skin's materials - for writing a model out again (model_export). nullopt with an
/// error when the part or model does not exist or the files are broken.
[[nodiscard]] std::optional<ModelGeometry> buildBodyPartGeometry(const ModelFiles& files, int part, int model, std::string* error = nullptr,
                                                                 const std::function<bool(const std::string&)>& materialExists = {});

/// Loads "models/x.mdl" (+ .vvd, .dx90.vtx / .vtx) from the game's file system.
[[nodiscard]] std::optional<ModelGeometry> loadModelGeometry(const GameFileSystem& fs, std::string_view modelPath, int skin = 0, std::string* error = nullptr,
                                                             int body = 0);

/// A model drawn with a skin and body group choices, e.g. a door with its handle. The model
/// cache keys it as "models/x.mdl#s=1;b=0;g1=2" (plain path: the defaults).
struct ModelVariant {
	std::string path;                             ///< "models/x.mdl"
	int skin = 0;
	int body = 0;                                 ///< The engine's body number (the "body" key)
	std::vector<std::pair<int, int>> groups;      ///< Body group index -> option, applied on top of `body`
};
/// The cache key of a variant (just the path when it shows the defaults).
[[nodiscard]] std::string modelVariantKey(const ModelVariant& variant);
/// Reads a key made by modelVariantKey (a plain path gives the defaults).
[[nodiscard]] ModelVariant parseModelVariant(std::string_view key);
/// Loads a variant: its skin, and the body number with the group choices set (as the engine's
/// SetBodygroup does - prop_door_rotating sets group 1 to its "hardware").
[[nodiscard]] std::optional<ModelGeometry> loadModelVariant(const GameFileSystem& fs, const ModelVariant& variant, std::string* error = nullptr);

} // namespace sme
