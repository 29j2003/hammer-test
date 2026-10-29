// studio_anim.h - Skeletal animation of Source 1 models (.mdl versions 44-49) for previews:
// the skeleton, the sequences of the model and of its $includemodel models (merged like the
// engine's virtual model: the model's own first, then each include's that are new), and their
// compressed animation data - in the .mdl or in the .ani file next to it, in sections for long
// animations. A sequence at a point of its cycle gives a pose; the pose moves the model's
// vertices on the CPU with up to three bone weights per vertex (from the .vvd).
//
// Layouts and math follow the Source SDK 2013 (studio.h: mstudiobone_t, mstudioanimdesc_t,
// mstudioseqdesc_t, mstudioanim_t; bone_setup.cpp: CalcAnimation, CalcVirtualAnimation,
// ExtractAnimValue, CalcBoneQuaternion/Position; mathlib: AngleQuaternion, QuaternionMatrix).
// Files are untrusted: every offset is bounds-checked, broken data gives the bone's rest pose.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/studio_model.h>

namespace sme {

class GameFileSystem;

/// A rotation (x, y, z, w).
struct AnimQuat {
	float x = 0, y = 0, z = 0, w = 1;
};
/// A position or Euler angles in radians (x = roll, y = pitch, z = yaw as RadianEuler).
struct AnimVec {
	float x = 0, y = 0, z = 0;
};
/// Rotation and translation as Source's matrix3x4_t (row major, translation in column 3).
struct BoneMatrix {
	float m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};

/// One bone of a skeleton (mstudiobone_t): its rest pose relative to the parent, the scales of
/// its compressed animation values and the matrix from model space at rest into the bone.
struct StudioBone {
	std::string name;
	int parent = -1;
	AnimVec pos;
	AnimQuat quat;
	AnimVec rot;
	AnimVec posScale, rotScale;
	BoneMatrix poseToBone;
};

/// A sequence as the Animations page lists it.
struct StudioSequence {
	std::string name;
	std::string activity;   ///< "ACT_IDLE" or empty
	bool looping = false;
	bool delta = false;     ///< Adds onto another pose (gestures, layers); previewed on top of the idle
	/// A gesture or layer (delta, autoplay, or the "...default" base pose of
	/// a gesture's inDelta/outDelta pair): played over other animations in the game, not alone.
	bool layer = false;
	int blends = 1;         ///< Blend samples (pose-parameter blends: 9-way aims, move_yaw ...); the centre one is shown
	float fps = 30;
	int frames = 1;
	int model = 0;          ///< Which file of the AnimatedModel holds it (0 = the model itself, -1 = added frames)
	int animation = 0;      ///< Its animation in that file (for blends the centre sample)
	/// Other files that have a sequence of the same name (the engine plays the first one, `model`).
	std::vector<std::string> alsoIn;
	/// Seconds one pass takes (at least one frame).
	[[nodiscard]] float duration() const { return frames > 1 && fps > 0 ? float(frames - 1) / fps : 0.f; }
};

/// A model with the files its sequences come from (see the file comment).
class AnimatedModel {
public:
	/// The bytes of one model file and of its .ani (empty if it has none).
	struct FileData {
		std::string path;             ///< "models/x.mdl" (for messages and include matching)
		std::vector<std::byte> mdl;
		std::vector<std::byte> ani;
	};

	/// Reads "models/x.mdl" with its .ani and its include models (and theirs, 4 levels deep).
	[[nodiscard]] static std::optional<AnimatedModel> load(const GameFileSystem& fs, std::string_view modelPath, std::string* error = nullptr);
	/// From file contents: the model first, then its include models in order.
	[[nodiscard]] static std::optional<AnimatedModel> fromFiles(std::vector<FileData> files, std::string* error = nullptr);

	/// The model's own skeleton (the bones the vertices are weighted to).
	[[nodiscard]] const std::vector<StudioBone>& bones() const { return m_files.front().bones; }
	/// Every sequence once: the model's own first, then the include models' new ones.
	[[nodiscard]] const std::vector<StudioSequence>& sequences() const { return m_sequences; }
	/// Index of a sequence by name (case does not matter), -1 if there is none.
	[[nodiscard]] int findSequence(std::string_view name) const;
	/// The idle a delta sequence is shown on: the ACT_IDLE sequence (not a layer), else the first
	/// looping sequence with "idle" in its name, else -1 (the rest pose).
	[[nodiscard]] int idleSequence() const;
	/// Paths of the files the sequences come from (index = StudioSequence::model).
	[[nodiscard]] std::vector<std::string> files() const;

	/// Adds a sequence made of plain frames (an animation being imported, for its preview):
	/// frames[f][bone] is every bone's position and rotation relative to its parent. Its
	/// StudioSequence::model is -1. Returns the sequence's index.
	int addKeyframedSequence(StudioSequence sequence, std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> frames);

	/// The pose of every bone of the model (relative to its parent) at `cycle` (0 = first frame,
	/// 1 = last frame) of the sequence; the rest pose for an unknown sequence.
	void localPose(int sequence, float cycle, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const;
	/// Bone to model space for every bone at that point.
	[[nodiscard]] std::vector<BoneMatrix> boneToModel(int sequence, float cycle) const;
	/// The matrices that move a vertex from the rest pose to that pose (bone to model times pose to bone).
	[[nodiscard]] std::vector<BoneMatrix> skinning(int sequence, float cycle) const;

private:
	struct AnimDesc {
		long long offset = 0;       ///< Of the mstudioanimdesc_t in the .mdl
		float fps = 30;
		int flags = 0;
		int frames = 1;
		int block = 0;
		long long index = 0;
		long long sectionIndex = 0;
		int sectionFrames = 0;
	};
	struct File {
		std::string path;
		std::vector<std::byte> mdl, ani;
		std::vector<StudioBone> bones;
		std::vector<AnimDesc> anims;
		std::vector<std::pair<long long, long long>> aniBlocks;   ///< datastart, dataend
		std::vector<int> toModel;                                 ///< Bone of this file -> bone of the model (-1: none)
	};
	/// Where the data of an animation's frame starts (buffer + offset), with the frame inside it.
	struct AnimData {
		const std::vector<std::byte>* data = nullptr;
		long long offset = 0;
		int frame = 0;
	};
	[[nodiscard]] static std::optional<File> parse(FileData data, std::string* error);
	[[nodiscard]] AnimData animData(const File& file, const AnimDesc& anim, int frame) const;
	/// Applies one frame of an animation onto the model's pose (bones of `file` mapped by name).
	void applyFrame(const File& file, const AnimDesc& anim, int frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat,
	                std::vector<bool>* touched) const;

	std::vector<File> m_files;
	std::vector<StudioSequence> m_sequences;
	std::vector<std::vector<std::vector<std::pair<AnimVec, AnimQuat>>>> m_keyframed;   ///< Frames of addKeyframedSequence's
};

/// The vertices moved by the skinning matrices (positions and normals, weights from
/// geometry.weights). Vertices without weights, or bones out of range, stay where they are.
void skinVertices(const ModelGeometry& geometry, const std::vector<BoneMatrix>& skinning, std::vector<ModelVertex>& out);

/// Rotation of Euler angles (RadianEuler: x roll, y pitch, z yaw), as AngleQuaternion.
[[nodiscard]] AnimQuat angleQuaternion(const AnimVec& angles);
/// The 3x4 matrix of a rotation and a position, as QuaternionMatrix.
[[nodiscard]] BoneMatrix quaternionMatrix(const AnimQuat& q, const AnimVec& pos);
/// a * b (apply b first), as ConcatTransforms.
[[nodiscard]] BoneMatrix concat(const BoneMatrix& a, const BoneMatrix& b);
/// The inverse of a rotation + translation matrix.
[[nodiscard]] BoneMatrix invertRigid(const BoneMatrix& m);
/// A point moved by the matrix.
[[nodiscard]] AnimVec transformPoint(const BoneMatrix& m, const AnimVec& p);

} // namespace sme
