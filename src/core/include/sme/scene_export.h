// scene_export.h - Exports the map for other programs and engines (Blender, Unity, Unreal):
// Wavefront OBJ (+ .mtl) or glTF 2.0 (.gltf with the data embedded), with the textures as PNG
// files next to it and the models of props. Converted to meters with Y pointing up, as these
// formats expect (1 Source unit = 1 inch).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sme/map.h>

namespace sme {

class EntityCatalog;
class GameFileSystem;

enum class ExportFormat { Obj, Gltf };

/// What goes into an export.
struct ExportOptions {
	ExportFormat format = ExportFormat::Gltf;
	bool tools = false;        ///< Include tool textures (nodraw, skybox, triggers, clips)
	bool models = true;        ///< Include models of props and NPCs (needs the game's files)
	bool textures = true;      ///< Write the textures as PNG files (needs the game's files)
};

/// What the export wrote.
struct ExportResult {
	std::size_t triangles = 0;
	std::size_t materials = 0;
	std::size_t textures = 0;
	std::size_t models = 0;
	std::vector<std::string> warnings;
};

/// Writes `file` (and, for OBJ, the .mtl next to it; textures into "<name>_textures/").
/// `fs` may be null: then no textures and no models.
[[nodiscard]] std::optional<ExportResult> exportMap(const MapDocument& doc, const EntityCatalog* catalog, const GameFileSystem* fs,
                                                    const std::filesystem::path& file, const ExportOptions& options, std::string* error = nullptr);

} // namespace sme
