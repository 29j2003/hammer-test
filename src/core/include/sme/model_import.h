// model_import.h - Bringing the mapper's own 3D models into the mod: an .obj (with its .mtl) or a
// reference .smd becomes the sources studiomdl compiles - a reference SMD, a collision SMD and a
// .qc - written to <mod>/modelsrc/<folder>/<name>/, compiled with the game's studiomdl into
// <mod>/models/<folder>/<name>.mdl (+ .vvd, .vtx, .phy).
//
// OBJ files are usually Y-up in metres or centimetres; Source is Z-up in inches. The options turn
// and scale the mesh and stand it on the ground at its middle (the origin of a prop is where it
// stands). The collision is the convex hull (one solid piece, cheap and stable), the bounding box,
// or the mesh itself split into its separate parts ($concave). Files are untrusted: sizes and
// indices are checked, broken lines are skipped.

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/math.h>

namespace sme {

/// Triangles with a material each, as they go into a reference SMD.
struct ImportMesh {
	struct Vertex {
		Vec3 position;
		Vec3 normal;
		double u = 0, v = 0;
	};
	struct Triangle {
		std::array<Vertex, 3> corners;
		std::string material;
	};
	std::vector<Triangle> triangles;
	std::vector<std::string> materials;              ///< In first-use order
	std::map<std::string, std::string> textures;     ///< Material -> picture file named by the .mtl (map_Kd), as written there
	std::map<std::string, Vec3> colors;              ///< Material -> diffuse colour 0..1 (Kd) for materials without a picture

	[[nodiscard]] Bounds bounds() const;
};

/// The .mtl file an .obj names (mtllib), empty if none.
[[nodiscard]] std::string objMaterialLibrary(std::string_view obj);
/// Reads an .obj (polygons become triangle fans; missing normals are computed per face) and its
/// .mtl text (may be empty). nullopt if there are no triangles.
[[nodiscard]] std::optional<ImportMesh> readObj(std::string_view obj, std::string_view mtl, std::string* error = nullptr);
/// Reads the triangles of a reference .smd. nullopt if there are none.
[[nodiscard]] std::optional<ImportMesh> readSmd(std::string_view smd, std::string* error = nullptr);

/// How a model comes into the mod.
struct ModelImportOptions {
	std::string folder = "custom";      ///< Below models/ (its materials go below materials/models/)
	std::string name;
	enum class Kind { Static, Physics, Dynamic };
	Kind kind = Kind::Static;           ///< prop_static, prop_physics or prop_dynamic
	double scale = 1.0;                 ///< File units to inches (metres: 39.37, centimetres: 0.3937)
	bool yUp = true;                    ///< The file is Y-up (most programs): turned to Source's Z-up
	bool onGround = true;               ///< Origin at the middle of the bottom
	std::string surfaceProp = "default";
	double mass = 0;                    ///< Physics props, kg; 0: from the size
	enum class Collision { Hull, Box, Parts };
	Collision collision = Collision::Hull;
};

/// The mesh in Source space: turned, scaled and placed as the options say.
[[nodiscard]] ImportMesh placedMesh(const ImportMesh& mesh, const ModelImportOptions& options);
/// The collision triangles for the options (hull, box or the mesh itself).
[[nodiscard]] ImportMesh collisionMesh(const ImportMesh& placed, ModelImportOptions::Collision collision);
/// Volume enclosed by closed triangles (cubic inches); 0 for open meshes.
[[nodiscard]] double meshVolume(const ImportMesh& mesh);
/// Mass for a physics prop: the option, else from the hull's volume (wood-like), 1..2000 kg.
[[nodiscard]] double importMass(const ImportMesh& placed, const ModelImportOptions& options);
/// A reference SMD of the triangles (one bone, material names as the file names of the materials).
[[nodiscard]] std::string writeSmd(const ImportMesh& mesh);
/// The .qc for the options and the source file names next to it.
[[nodiscard]] std::string writeQc(const ModelImportOptions& options, std::string_view referenceSmd, std::string_view collisionSmd, double mass);

/// "models/custom/crate.mdl"
[[nodiscard]] std::string modelImportName(const ModelImportOptions& options);
/// Folder of the sources (<mod>/modelsrc/<folder>/<name>).
[[nodiscard]] std::filesystem::path modelSourceDir(const std::filesystem::path& modDir, const ModelImportOptions& options);
/// Every file studiomdl writes for the model (.mdl, .vvd, .dx90.vtx, .dx80.vtx, .sw.vtx, .phy).
[[nodiscard]] std::vector<std::filesystem::path> modelOutputFiles(const std::filesystem::path& modDir, const ModelImportOptions& options);
/// The material file names the model needs (as written into the SMD).
[[nodiscard]] std::vector<std::string> modelMaterialNames(const ImportMesh& mesh);

/// What the import wrote before compiling, and how to call studiomdl.
struct ModelImportPlan {
	ImportRecord record;                 ///< The sources (and later the compiled files)
	std::filesystem::path qc;
	std::vector<std::string> arguments;  ///< For studiomdl: -game <mod> -nop4 -verbose <qc>
};
/// Writes the sources. With `overwrite` false, existing sources or compiled files stop it.
[[nodiscard]] std::optional<ModelImportPlan> prepareModelImport(const std::filesystem::path& modDir, const ImportMesh& mesh, const ModelImportOptions& options,
                                                                bool overwrite, std::string* error = nullptr);
/// After studiomdl: adds the compiled files to the record. False (with `error`) if the .mdl is
/// missing, i.e. the compile failed.
bool collectModelOutputs(ModelImportPlan& plan, const std::filesystem::path& modDir, const ModelImportOptions& options, std::string* error = nullptr);
/// The useful lines of studiomdl's output: errors and warnings (empty if it printed none).
[[nodiscard]] std::vector<std::string> studiomdlProblems(std::string_view output);

} // namespace sme
