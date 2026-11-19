// anim_edit.h - Keyframe editing of a model's animations, the way Source Filmmaker does it:
// pick a bone, turn or move it, set a key on the timeline; between keys the pose is
// interpolated (linear or smooth, rotations with slerp).
//
// A clip always has a base underneath, so improving an animation stays easy:
// - Tweak: the base is an existing sequence of the model (a weapon's reload, an NPC's gesture),
//   stretched over the clip's length. Keys are offsets on top of it - a key that turns the hand
//   a little keeps everything else of the original motion.
// - New: the base is one still pose (the rest pose, or one frame of a sequence); keys are offsets
//   on that pose.
// Offsets: rotation after the base's (final = base * offset, in the bone's own axes), position
// added to the base's (in the parent's space). A bone without keys follows the base; before its
// first key and after its last one the nearest key holds.
//
// The clip is baked into plain frames for the preview (AnimatedModel::addKeyframedSequence) and
// written as an animation .smd with model_export's writer; a project file (.smanim, KeyValues
// text) keeps the keys to continue later. Project files are untrusted: everything is checked and
// limited when read. Compiling the result (as a new animation or replacing the sequence in the
// model) is in anim_compile below.

#pragma once

#include <map>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/studio_anim.h>

namespace sme {

/// How the curve runs from a key to the next one.
enum class KeyInterp { Linear, Smooth };

/// One key of a bone: offsets on the base pose at a frame.
struct BoneKey {
	int frame = 0;
	AnimQuat rot;   ///< Rotation after the base pose's rotation
	AnimVec pos;    ///< Added to the base pose's position (parent space)
	KeyInterp interp = KeyInterp::Smooth;
	bool operator==(const BoneKey& o) const {
		return frame == o.frame && interp == o.interp && rot.x == o.rot.x && rot.y == o.rot.y && rot.z == o.rot.z && rot.w == o.rot.w && pos.x == o.pos.x &&
		       pos.y == o.pos.y && pos.z == o.pos.z;
	}
};

/// An animation being edited (see the file comment).
struct AnimClip {
	enum class Mode { Tweak, New };
	std::string model;          ///< "models/weapons/v_smg1.mdl"
	std::string name;           ///< The sequence it becomes ("reload_fast")
	Mode mode = Mode::Tweak;
	std::string baseSequence;   ///< Tweak: what plays underneath; New: the sequence of the start pose (empty: rest pose)
	int baseFrame = 0;          ///< New: the frame of baseSequence used as the start pose
	int frames = 30;            ///< Length in frames (at least 1)
	float fps = 30;
	bool loop = false;
	std::string activity;       ///< "ACT_VM_RELOAD" or empty
	/// Keys per bone (the model's bone name), sorted by frame, at most one per frame.
	std::map<std::string, std::vector<BoneKey>> tracks;
	bool operator==(const AnimClip&) const = default;
};

/// Limits of a clip (also what reading a project file accepts).
inline constexpr int ANIM_CLIP_MAX_FRAMES = 10000;
inline constexpr int ANIM_CLIP_MAX_KEYS = 200000;

/// A pose of some bones copied from a frame (Ctrl+C / Ctrl+V): offsets by bone name.
struct ClipPose {
	std::map<std::string, std::pair<AnimQuat, AnimVec>> bones;
};

/// Editing a clip on a model (see the file comment). Keeps an undo history of its own.
class AnimEditor {
public:
	/// `model` must outlive the editor. The clip's base sequence is looked up by name.
	AnimEditor(const AnimatedModel& model, AnimClip clip);

	[[nodiscard]] const AnimClip& clip() const { return m_clip; }
	[[nodiscard]] const AnimatedModel& model() const { return *m_model; }
	[[nodiscard]] const std::vector<StudioBone>& bones() const { return m_model->bones(); }
	/// Index of a bone by name (case does not matter), -1 if the model has none.
	[[nodiscard]] int boneIndex(std::string_view name) const;
	/// The base sequence's index in the model (-1: rest pose).
	[[nodiscard]] int baseSequence() const { return m_base; }

	// --- Poses ------------------------------------------------------------------------------
	/// Every bone's base pose at a frame (relative to its parent).
	void basePose(float frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const;
	/// A bone's offset at a frame, interpolated between its keys (identity without keys).
	[[nodiscard]] std::pair<AnimQuat, AnimVec> offset(int bone, float frame) const;
	/// The final pose at a frame (fractional frames for smooth playback).
	void pose(float frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const;
	/// Bone to model space for every bone at that frame.
	[[nodiscard]] std::vector<BoneMatrix> boneToModel(float frame) const;
	/// The skinning matrices at that frame (as AnimatedModel::skinning).
	[[nodiscard]] std::vector<BoneMatrix> skinning(float frame) const;

	// --- Keys -------------------------------------------------------------------------------
	[[nodiscard]] bool hasKey(int bone, int frame) const;
	/// Frames with a key of any of these bones (all bones if empty), sorted.
	[[nodiscard]] std::vector<int> keyFrames(const std::vector<int>& bones = {}) const;
	/// Number of keys in the clip.
	[[nodiscard]] int keyCount() const;
	/// Sets the key of these bones at `frame` to what they show there now ("K").
	void keyBones(const std::vector<int>& bones, int frame);
	/// Sets a bone's key at `frame` to these offsets (a key there keeps its interpolation).
	void setKey(int bone, int frame, const AnimQuat& rot, const AnimVec& pos);
	/// Turns a bone at `frame` by a rotation in model space (about the bone's own position) and
	/// keys it - what the rotate gizmo does.
	void rotateBone(int bone, int frame, const AnimQuat& modelRotation);
	/// Moves a bone at `frame` by a model-space offset and keys it - the move gizmo.
	void moveBone(int bone, int frame, const AnimVec& modelOffset);
	/// Deletes the keys of these bones (all bones if empty) at `frame`. False if there was none.
	bool deleteKeys(const std::vector<int>& bones, int frame);
	/// Moves (or copies) the keys of these bones (all if empty) at `from` to `to` (a key there is replaced).
	bool moveKeys(const std::vector<int>& bones, int from, int to, bool copy = false);
	/// Sets how the curve leaves the keys of these bones (all if empty) at `frame`.
	void setInterp(const std::vector<int>& bones, int frame, KeyInterp interp);
	/// Keys these bones back to the base pose at `frame` (offset identity).
	void resetBones(const std::vector<int>& bones, int frame);
	/// Removes every key of these bones.
	void clearBones(const std::vector<int>& bones);

	/// The offsets of these bones (all if empty) at a frame / keys them at another frame.
	[[nodiscard]] ClipPose copyPose(const std::vector<int>& bones, int frame) const;
	void pastePose(const ClipPose& pose, int frame);
	/// Mirrors the pose of these bones (all if empty) at `frame` left to right (bones paired by
	/// their names: L/R, Left/Right; the model's Y axis is the side axis) and keys them. Returns
	/// how many bones changed.
	int mirrorPose(const std::vector<int>& bones, int frame);

	// --- Clip settings ----------------------------------------------------------------------
	/// New length: the keys move with it (a key at the last frame stays at the last frame), the
	/// base sequence is stretched to it - so this also changes the speed.
	void setFrameCount(int frames);
	void setFps(float fps);
	void setLoop(bool loop);
	void setName(std::string name);
	void setActivity(std::string activity);

	// --- Undo ---------------------------------------------------------------------------------
	/// Starts a change that ends with endChange() (a gizmo drag): one undo step for all of it.
	void beginChange();
	void endChange();
	/// Back to the clip as it was at beginChange() (each drag step starts from there, so the
	/// result does not drift).
	void revertChange();
	[[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
	[[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
	bool undo();
	bool redo();
	/// Counts every change (the UI redraws when it moves).
	[[nodiscard]] int revision() const { return m_revision; }

	// --- Results ------------------------------------------------------------------------------
	/// Every frame's pose of every bone (relative to its parent).
	[[nodiscard]] std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> bake() const;
	/// The clip as a complete animation .smd for the model (every bone in every frame).
	[[nodiscard]] std::string smd() const;
	/// The sequence for AnimatedModel::addKeyframedSequence (name, fps, loop, activity).
	[[nodiscard]] StudioSequence sequenceInfo() const;

private:
	void changed();   ///< Before a change: undo step (outside a change group), clears redo
	[[nodiscard]] std::vector<BoneKey>* track(int bone, bool create);
	[[nodiscard]] const std::vector<BoneKey>* track(int bone) const;
	void putKey(int bone, BoneKey key);
	[[nodiscard]] std::vector<int> orAll(const std::vector<int>& bones) const;

	const AnimatedModel* m_model;
	AnimClip m_clip;
	int m_base = -1;
	std::vector<AnimClip> m_undo, m_redo;
	std::optional<AnimClip> m_changeStart;
	int m_changeDepth = 0;
	int m_revision = 0;
};

/// Bones grouped by their names for the bone list ("Right arm", "Left hand", "Spine", ...):
/// the group's name for a bone name (empty: "Other").
[[nodiscard]] std::string boneGroup(std::string_view boneName);
/// The other side's bone name ("ValveBiped.Bip01_L_Hand" -> "...R_Hand"), empty if it has no side.
[[nodiscard]] std::string mirroredBoneName(std::string_view boneName);

/// The clip as a project file (.smanim, KeyValues text) and back. Reading checks and limits
/// everything (untrusted); nullopt with an error on a broken file.
[[nodiscard]] std::string writeAnimClip(const AnimClip& clip);
[[nodiscard]] std::optional<AnimClip> readAnimClip(std::string_view text, std::string* error = nullptr);
/// Where a clip's project file goes below the mod: "modelsrc/<model without models/ and .mdl>/animations/<name>.smanim".
[[nodiscard]] std::string animClipPath(std::string_view model, std::string_view name);

// --- Compiling (see anim_compile) ---------------------------------------------------------------

/// The .qc of a decompiled model (model_export) with one sequence's animation replaced: the
/// sequence's .smd file name in the export, and its "$sequence" line with the clip's fps and
/// loop (everything else of the line and what follows it - activity, events - stays). Returns the
/// changed .qc text and the .smd file name, or nullopt if the .qc has no such sequence.
struct ReplacedSequence {
	std::string qc;
	std::string smdFile;
};
[[nodiscard]] std::optional<ReplacedSequence> replaceSequenceInQc(std::string_view qc, std::string_view sequence, float fps, bool loop);

/// The header checksums of a compiled model's files: the .mdl's and those of the .vvd, .vtx and
/// .phy next to it must be the same, or the engine refuses the model ("bad checksum"). Each entry:
/// file extension and its checksum (nullopt: file missing or not readable).
struct ModelChecksums {
	std::optional<int> mdl, vvd, dx90, dx80, sw, phy;
	/// Every file that is there has the .mdl's checksum (and the .mdl, .vvd and .dx90.vtx are there).
	[[nodiscard]] bool consistent() const;
};
/// Checksum fields of the headers (studiohdr_t, vertexFileHeader_t, OptimizedModel::FileHeader_t,
/// phyheader_t; Source SDK 2013 public/studio.h, optimize.h, phyfile.h). nullopt if too short or no such file.
[[nodiscard]] std::optional<int> mdlChecksum(std::span<const std::byte> mdl);
[[nodiscard]] std::optional<int> vvdChecksum(std::span<const std::byte> vvd);
[[nodiscard]] std::optional<int> vtxChecksum(std::span<const std::byte> vtx);
[[nodiscard]] std::optional<int> phyChecksum(std::span<const std::byte> phy);

} // namespace sme
