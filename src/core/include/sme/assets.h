// assets.h - Information about game assets for the browsers: material categories,
// model sequences (animations), model materials, sound script names.

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/material.h>
#include <sme/math.h>

namespace sme {

class GameFileSystem;

/// Categories used by the material browser filters.
struct MaterialCategory {
	bool blend = false;        ///< Two-texture blend for displacements (WorldVertexTransition, $basetexture2)
	bool model = false;        ///< Meant for models (VertexLitGeneric, materials/models/...)
	bool tool = false;         ///< nodraw, clip, trigger, skybox, ...
	bool translucent = false;  ///< $translucent / $alphatest / $additive
	bool water = false;
	bool decal = false;        ///< $decal or overlays/decals folders
	bool sky = false;          ///< Skybox faces (materials/skybox/)
	bool brush = false;        ///< Usable on world brushes (LightmappedGeneric & co., not model-only)
};

[[nodiscard]] MaterialCategory classifyMaterial(const Material& material);

/// What the editor needs from a .mdl file.
struct ModelInfo {
	std::string name;                       ///< Name stored in the model
	int version = 0;
	std::vector<std::string> sequences;     ///< Animation sequence names (own, without include models)
	std::vector<std::string> activities;    ///< Activity per sequence ("ACT_IDLE", may be empty)
	std::vector<std::string> includeModels; ///< $includemodel files holding more sequences
	std::vector<std::string> materials;     ///< Texture names (combine with materialDirs)
	std::vector<std::string> materialDirs;  ///< $cdmaterials folders
	std::string surfaceProp;
	Vec3 hullMin, hullMax;
};

/// Parses a Source 1 .mdl header (versions 44-49). Never reads outside the buffer.
[[nodiscard]] std::optional<ModelInfo> readModelInfo(std::span<const std::byte> data, std::string* error = nullptr);

/// Sequences of a model including all its $includemodel files (as the engine merges them).
[[nodiscard]] std::vector<std::string> modelSequences(const GameFileSystem& fs, std::string_view modelPath, std::string* error = nullptr);

/// Sound names from all sound scripts listed in scripts/game_sounds_manifest.txt (sorted, unique).
[[nodiscard]] std::vector<std::string> soundScriptNames(const GameFileSystem& fs);

/// Levenshtein distance (case-insensitive), used for "did you mean" fixes.
[[nodiscard]] std::size_t editDistance(std::string_view a, std::string_view b);

/// Closest candidate to `name`, or empty if none is close enough.
[[nodiscard]] std::string closestMatch(std::string_view name, const std::vector<std::string>& candidates);

} // namespace sme
