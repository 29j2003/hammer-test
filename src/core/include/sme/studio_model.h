// studio_model.h - Triangles of a Source 1 model for the viewport and the model previews.
//
// Reads the three files the engine uses: .mdl (body parts, meshes, skins, material folders),
// .vvd (vertex positions, normals, texture coordinates) and .dx90.vtx (triangle lists per
// mesh, LOD 0). The model is shown in its reference pose (no animation). All files are
// untrusted: every offset and count is checked, broken files give an error, never a crash.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
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

/// Triangles of one material.
struct ModelMesh {
	std::string material;          ///< Material name for the cache ("models/props/crate01"), resolved through $cdmaterials
	std::uint32_t firstIndex = 0;  ///< Range in ModelGeometry::indices
	std::uint32_t indexCount = 0;
};

/// A model's triangles (LOD 0) ready for drawing, with its bounds.
struct ModelGeometry {
	std::vector<ModelVertex> vertices;
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
/// the $cdmaterials folder that holds a texture (empty function: the first folder).
[[nodiscard]] std::optional<ModelGeometry> buildModelGeometry(const ModelFiles& files, int skin, std::string* error = nullptr,
                                                              const std::function<bool(const std::string&)>& materialExists = {});

/// Loads "models/x.mdl" (+ .vvd, .dx90.vtx / .vtx) from the game's file system.
[[nodiscard]] std::optional<ModelGeometry> loadModelGeometry(const GameFileSystem& fs, std::string_view modelPath, int skin = 0, std::string* error = nullptr);

} // namespace sme
