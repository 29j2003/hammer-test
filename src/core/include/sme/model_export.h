// model_export.h - Taking a compiled model of the game apart for editing (like Crowbar's
// decompile): its triangles back into reference .smd files (every model of every body group,
// with the bone weights and the skeleton), its sequences into animation .smd files, its physics
// model (.phy) into a collision .smd, and a .qc that studiomdl compiles into the same model again
// ($modelname, $cdmaterials, $bodygroup, $texturegroup, $surfaceprop, $keyvalues, $includemodel,
// $attachment, $hboxset/$hbox, $sequence, $collisionmodel / $collisionjoints). Its materials (.vmt,
// .vtf) are copied out of the game's file system (VPKs too), the textures also as .png pictures.
// The files go to <mod>/modelsrc/<model path>/ by default, where Mapsmith's own model and
// animation import read them again.
//
// Coordinates: studiomdl turns .smd data 90 degrees about Z; the files are turned back (as
// animation_import's referenceSmd), so a recompile gives the model in the same place. Not written:
// LODs, flexes (faces), sequence events, the other blend samples of a pose-parameter blend (only
// the centre one), IK rules. Files are untrusted: every offset and count is checked.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/studio_anim.h>

namespace sme {

class GameFileSystem;

/// One hit box ($hbox): a box on a bone, in the bone's space.
struct ModelHitbox {
	std::string bone;
	int group = 0;
	AnimVec min, max;
	std::string name;
};
/// A set of hit boxes ($hboxset).
struct ModelHitboxSet {
	std::string name;
	std::vector<ModelHitbox> boxes;
};
/// The hit box sets of a .mdl (bounds-checked; empty on broken data).
[[nodiscard]] std::vector<ModelHitboxSet> readHitboxes(std::span<const std::byte> mdl);

/// The skin table of a .mdl: families[family][skinref] = texture index (empty on broken data).
[[nodiscard]] std::vector<std::vector<int>> readSkinFamilies(std::span<const std::byte> mdl);

/// A model's physics (.phy): convex pieces per solid, in Source units, and its text part.
struct PhysicsModel {
	struct Solid {
		int index = 0;
		std::string bone;                          ///< From the text part ("name"); empty if it gives none
		std::vector<std::vector<AnimVec>> pieces;  ///< Convex pieces, each as triangle corners (3 per triangle)
	};
	std::vector<Solid> solids;
	double mass = 0;                               ///< "totalmass" of the text part (0 if none)
	bool concave = false;                          ///< More than one convex piece in one solid
	/// Joint limits of ragdolls: child bone, axis ("x"/"y"/"z"), min, max, friction.
	struct Constraint {
		std::string bone;
		char axis = 'x';
		float min = 0, max = 0, friction = 0;
	};
	std::vector<Constraint> constraints;
	std::string text;                              ///< The text part as in the file
};
/// Reads a .phy file (header, compact surfaces of the solids, text). nullopt with an error when
/// it is not one or has no triangles.
[[nodiscard]] std::optional<PhysicsModel> readPhysicsModel(std::span<const std::byte> phy, std::string* error = nullptr);
/// The collision .smd of a physics model for the skeleton (one solid: in model space on bone 0;
/// a ragdoll: each solid on its bone), turned back for studiomdl.
[[nodiscard]] std::string physicsSmd(const PhysicsModel& physics, const std::vector<StudioBone>& bones);

/// One sequence as an animation .smd: every bone of the model in every frame (a delta sequence
/// on the rest pose), root bones turned back for studiomdl.
[[nodiscard]] std::string sequenceSmd(const AnimatedModel& model, int sequence);

/// What to export.
struct ModelExportOptions {
	bool animations = true;               ///< Animation .smd files at all
	std::vector<std::string> sequences;   ///< Only these (by name, also from include models); empty: all of the model's own
	bool reference = true;                ///< The reference .smd files (every body group model)
	bool physics = true;                  ///< The collision .smd from the .phy
	bool materials = true;                ///< Copy the .vmt and .vtf files
	bool pictures = true;                 ///< The textures also as .png next to their .vtf
};

/// The export as file contents, before anything is written (paths relative to the export folder).
struct ModelExport {
	std::string qcFile;                                  ///< "v_pistol.qc"
	std::map<std::string, std::string> texts;            ///< .qc and .smd files
	std::map<std::string, std::string> copies;           ///< Game file ("materials/x.vmt") -> path in the folder
	std::vector<std::string> pictures;                   ///< .vtf paths in the folder that get a .png next to them
	std::vector<std::string> notes;                      ///< What was left out or could not be read
	int sequences = 0;                                   ///< Animation .smd files
};

/// Takes "models/x.mdl" apart. `progress(done, total)` is called per step and may return false to
/// stop (then nullopt with error "stopped").
[[nodiscard]] std::optional<ModelExport> decompileModel(const GameFileSystem& fs, std::string_view modelPath, const ModelExportOptions& options,
                                                        std::string* error = nullptr, const std::function<bool(int, int)>& progress = {});

/// Writes an export into `folder` (created if needed; existing files are replaced). Returns the
/// written files, or nullopt with an error.
[[nodiscard]] std::optional<std::vector<std::filesystem::path>> writeModelExport(const GameFileSystem& fs, const ModelExport& exported,
                                                                                 const std::filesystem::path& folder, std::string* error = nullptr,
                                                                                 const std::function<bool(int, int)>& progress = {});

/// The default folder of an export: <mod>/modelsrc/<model path without models/ and .mdl>.
[[nodiscard]] std::filesystem::path modelExportFolder(const std::filesystem::path& modDir, std::string_view modelPath);

} // namespace sme
