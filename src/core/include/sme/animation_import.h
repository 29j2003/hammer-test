// animation_import.h - New animations for an existing model: an animation .smd (from Blender's
// Source Tools, Crowbar's decompile, ...) is read, matched to the model's skeleton by bone name,
// shown at once on the model (a preview sequence), and compiled by the game's studiomdl.exe.
//
// Why a new model: a model's sequences come from the model itself and its $includemodel models,
// and only a recompile of the model (its .qc, which a game's models do not ship with) can add an
// include. So Mapsmith writes a model of its own, "models/mapsmith/<the model's path>": the
// model's triangles, skeleton, attachments, materials, surface and $keyvalues, written back to a
// reference .smd from the compiled files, $includemodel of the original model (so every one of
// its animations stays) and the new animations. Entities that should play them switch to that
// model. What is not carried over (documented in the manual): facial flexes, LODs, other body
// group options than the first, the physics model ($collisionmodel), hitboxes (studiomdl makes
// them from the bones).
//
// Coordinates: studiomdl turns .smd data 90 degrees about Z (its default $origin rotation); the
// .smd files written here are turned back so that the compile gives the original model again,
// and imported .smd files are turned the same way for the preview.

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/assets.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>

namespace sme {

/// An animation read from an .smd file.
struct SmdAnimation {
	struct Node {
		std::string name;
		int parent = -1;
	};
	std::vector<Node> nodes;
	/// frames[f][node] = position and Euler rotation (radians, x y z as in the file); a node
	/// missing in a frame keeps its value from the frame before.
	std::vector<std::vector<std::pair<AnimVec, AnimVec>>> frames;
};

/// Reads the nodes and skeleton frames of an .smd (triangles are skipped). nullopt + error on
/// broken files.
[[nodiscard]] std::optional<SmdAnimation> parseSmd(std::string_view text, std::string* error = nullptr);

/// How the .smd's bones meet the model's (by name, case does not matter).
struct SkeletonMatch {
	std::vector<int> toModel;               ///< .smd node -> model bone (-1: the model has none)
	std::vector<std::string> notInModel;    ///< .smd bones the model lacks (ignored for the preview)
	std::vector<std::string> notInSmd;      ///< Model bones the .smd does not move (they stay at rest)
	[[nodiscard]] int matched() const;
};
[[nodiscard]] SkeletonMatch matchSkeleton(const SmdAnimation& smd, const std::vector<StudioBone>& bones);

/// Options of an imported animation.
struct ImportedAnimation {
	std::string name;          ///< Sequence name ("wave_hello")
	std::string smdFile;       ///< File name in the source folder ("wave_hello.smd")
	float fps = 30;
	bool loop = false;
	std::string activity;      ///< "ACT_IDLE" or empty
};

/// Adds the .smd's frames to the model as a sequence for the preview (turned like studiomdl does).
/// Returns its index.
int addPreviewSequence(AnimatedModel& model, const SmdAnimation& smd, const ImportedAnimation& options);

/// Euler angles (RadianEuler) of a rotation, as Source's QuaternionAngles/MatrixAngles.
[[nodiscard]] AnimVec quaternionAngles(const AnimQuat& q);

/// The model Mapsmith writes for `original` ("models/mapsmith/humans/group01/male_07.mdl").
[[nodiscard]] std::string animationModelPath(std::string_view originalModel);
/// Where its sources go below the mod: "modelsrc/mapsmith/humans/group01/male_07".
[[nodiscard]] std::string animationSourceFolder(std::string_view originalModel);

/// An attachment of a model (mstudioattachment_t): name, bone and placement on the bone.
struct ModelAttachment {
	std::string name;
	std::string bone;
	AnimVec origin;
	AnimVec angles;   ///< Degrees: pitch, yaw, roll ($attachment's "rotate")
};
/// The attachments of a .mdl (bounds-checked; empty on broken data).
[[nodiscard]] std::vector<ModelAttachment> readAttachments(std::span<const std::byte> mdl);

/// The reference .smd of a compiled model: its skeleton at rest and its triangles (turned back
/// for studiomdl; materials by their file name).
[[nodiscard]] std::string referenceSmd(const ModelGeometry& geometry, const std::vector<StudioBone>& bones);
/// The .qc of the new model (see the file comment).
[[nodiscard]] std::string animationModelQc(std::string_view originalModel, const ModelGeometry& geometry, const ModelInfo& info,
                                           const std::vector<ModelAttachment>& attachments, const std::vector<ImportedAnimation>& animations);

/// The list of imported animations kept next to the sources (animations.txt), read and written.
[[nodiscard]] std::vector<ImportedAnimation> readAnimationList(std::string_view text);
[[nodiscard]] std::string writeAnimationList(const std::vector<ImportedAnimation>& animations);

/// A sequence name from a file name: lower case letters, digits and underscores.
[[nodiscard]] std::string sequenceNameFromFile(std::string_view fileName);

} // namespace sme
