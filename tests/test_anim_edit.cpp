// test_anim_edit.cpp - The animation editor's core: keys as offsets on a base animation,
// interpolation, the gizmo's turn/move, mirror, undo, length changes, the project file, the
// baked .smd, replacing a sequence in a decompiled .qc and the checksums of compiled files.

#include "fixture.h"

#include <sme/anim_edit.h>
#include <sme/animation_import.h>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <numbers>

using namespace sme;

namespace {

constexpr float PI = std::numbers::pi_v<float>;

/// A little body: root, spine, an arm and a hand on each side (left at +Y), and an animation
/// "wave" that lifts the spine over 11 frames.
test::StudioModelSpec bodyModel() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"body", {{-4, -4, 0, 4, 4, 8}, {-2, 6, 30, 2, 10, 34}, {-2, -10, 30, 2, -6, 34}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}},
	              {"ValveBiped.Bip01_Spine", 0, {0, 0, 20}, {0, 0, 0}},
	              {"ValveBiped.Bip01_L_UpperArm", 1, {0, 8, 10}, {0, 0, PI / 2}},
	              {"ValveBiped.Bip01_L_Hand", 2, {10, 0, 0}, {0, 0, 0}},
	              {"ValveBiped.Bip01_R_UpperArm", 1, {0, -8, 10}, {0, 0, -PI / 2}},
	              {"ValveBiped.Bip01_R_Hand", 4, {10, 0, 0}, {0, 0, 0}}};
	spec.boxBones = {0, 3, 5};
	test::StudioModelSpec::Animation wave{"wave", "ACT_GESTURE", 30, false, {}};
	for (int f = 0; f < 11; ++f) {
		wave.frames.push_back({{0, 0, 0, 0, 0, 0}, {0, 0, 20.f + float(f), 0, 0, 0}});
	}
	spec.animations.push_back(wave);
	return spec;
}

AnimatedModel loadBody() {
	const auto files = test::makeStudioModel(bodyModel());
	std::string error;
	auto model = AnimatedModel::fromFiles({{"models/body.mdl", files.mdl, {}}}, &error);
	REQUIRE_MESSAGE(model, error);
	return std::move(*model);
}

AnimClip tweakClip(const AnimatedModel& model) {
	AnimClip clip;
	clip.model = "models/body.mdl";
	clip.name = "wave_better";
	clip.baseSequence = "wave";
	clip.frames = model.sequences()[std::size_t(model.findSequence("wave"))].frames;
	return clip;
}

AnimVec position(const BoneMatrix& m) {
	return {m.m[0][3], m.m[1][3], m.m[2][3]};
}

/// Angle in radians between two rotation matrices.
float angleBetween(const BoneMatrix& a, const BoneMatrix& b) {
	float trace = 0;
	for (int r = 0; r < 3; ++r) {
		for (int k = 0; k < 3; ++k) trace += a.m[k][r] * b.m[k][r];
	}
	return std::acos(std::clamp((trace - 1.f) * 0.5f, -1.f, 1.f));
}

AnimQuat axisAngle(float x, float y, float z, float angle) {
	const float s = std::sin(angle * 0.5f);
	return {x * s, y * s, z * s, std::cos(angle * 0.5f)};
}

} // namespace

TEST_CASE("anim edit: a tweak without keys is the base animation") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	REQUIRE(editor.baseSequence() == model.findSequence("wave"));
	const auto baked = editor.bake();
	REQUIRE(baked.size() == 11);
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	for (int f : {0, 5, 10}) {
		model.localPose(editor.baseSequence(), float(f) / 10.f, pos, quat, false);
		CHECK(baked[std::size_t(f)][1].first.z == doctest::Approx(pos[1].z));
	}
	CHECK(baked[10][1].first.z == doctest::Approx(30));
	CHECK(editor.keyCount() == 0);
}

TEST_CASE("anim edit: the rotate gizmo turns a bone in model space and its children follow") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int arm = editor.boneIndex("valvebiped.bip01_l_upperarm");
	const int hand = editor.boneIndex("ValveBiped.Bip01_L_Hand");
	REQUIRE(arm > 0);
	REQUIRE(hand > arm);
	const auto before = editor.boneToModel(5);
	const AnimQuat turn = axisAngle(0, 0, 1, PI / 2);
	editor.rotateBone(arm, 5, turn);
	CHECK(editor.hasKey(arm, 5));
	const auto after = editor.boneToModel(5);
	// The arm stays where it is, its rotation is turn * before.
	CHECK(position(after[std::size_t(arm)]).y == doctest::Approx(position(before[std::size_t(arm)]).y));
	const BoneMatrix expected = concat(quaternionMatrix(turn, {}), before[std::size_t(arm)]);
	CHECK(angleBetween(after[std::size_t(arm)], expected) == doctest::Approx(0).epsilon(1e-3));
	// The hand (10 units along the arm) swings round the arm's joint.
	const AnimVec joint = position(before[std::size_t(arm)]);
	const AnimVec handBefore = position(before[std::size_t(hand)]), handAfter = position(after[std::size_t(hand)]);
	const float dx = handBefore.x - joint.x, dy = handBefore.y - joint.y;
	CHECK(handAfter.x - joint.x == doctest::Approx(-dy).epsilon(1e-3));
	CHECK(handAfter.y - joint.y == doctest::Approx(dx).epsilon(1e-3));
	// One key: it holds before and after (the offset rides on the base's motion).
	const auto first = editor.boneToModel(0);
	CHECK(angleBetween(first[std::size_t(arm)], concat(quaternionMatrix(turn, {}), editor.boneToModel(0)[std::size_t(arm)])) > 0.1f);
	CHECK(position(editor.boneToModel(10)[1]).z == doctest::Approx(30));   // the spine still rises
}

TEST_CASE("anim edit: keys interpolate linear or smooth") {
	const auto model = loadBody();
	AnimClip clip = tweakClip(model);
	clip.frames = 21;
	AnimEditor editor(model, clip);
	const int hand = editor.boneIndex("ValveBiped.Bip01_R_Hand");
	editor.setKey(hand, 0, {}, {});
	editor.setKey(hand, 20, axisAngle(1, 0, 0, PI / 2), {0, 0, 8});
	const auto angleAt = [&](float frame) {
		const auto [rot, pos] = editor.offset(hand, frame);
		return 2.f * std::acos(std::clamp(std::fabs(rot.w), 0.f, 1.f));
	};
	CHECK(angleAt(10) == doctest::Approx(PI / 4).epsilon(1e-3));
	CHECK(angleAt(5) == doctest::Approx(PI / 2 * 0.15625f).epsilon(1e-3));   // smoothstep(0.25)
	CHECK(editor.offset(hand, 10).second.z == doctest::Approx(4));
	editor.setInterp({hand}, 0, KeyInterp::Linear);
	CHECK(angleAt(5) == doctest::Approx(PI / 8).epsilon(1e-3));
	CHECK(angleAt(30) == doctest::Approx(PI / 2).epsilon(1e-3));   // after the last key it holds
	CHECK(editor.keyFrames({hand}) == std::vector<int>{0, 20});
}

TEST_CASE("anim edit: the move gizmo moves a bone in model space") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int hand = editor.boneIndex("ValveBiped.Bip01_L_Hand");
	const AnimVec before = position(editor.boneToModel(3)[std::size_t(hand)]);
	editor.moveBone(hand, 3, {1, 2, 3});
	const AnimVec after = position(editor.boneToModel(3)[std::size_t(hand)]);
	CHECK(after.x - before.x == doctest::Approx(1).epsilon(1e-3));
	CHECK(after.y - before.y == doctest::Approx(2).epsilon(1e-3));
	CHECK(after.z - before.z == doctest::Approx(3).epsilon(1e-3));
}

TEST_CASE("anim edit: undo, redo and one step for a whole drag") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int hand = editor.boneIndex("ValveBiped.Bip01_L_Hand");
	CHECK_FALSE(editor.canUndo());
	editor.beginChange();
	for (int step = 1; step <= 5; ++step) {
		editor.revertChange();
		editor.rotateBone(hand, 2, axisAngle(0, 1, 0, 0.1f * float(step)));
	}
	editor.endChange();
	const auto [rot, pos] = editor.offset(hand, 2);
	CHECK(2.f * std::acos(std::clamp(std::fabs(rot.w), 0.f, 1.f)) == doctest::Approx(0.5f).epsilon(1e-3));   // no drift
	CHECK(editor.undo());
	CHECK(editor.keyCount() == 0);
	CHECK_FALSE(editor.canUndo());
	CHECK(editor.redo());
	CHECK(editor.keyCount() == 1);
	// A drag that changed nothing leaves no undo step.
	editor.beginChange();
	editor.endChange();
	CHECK(editor.undo());
	CHECK_FALSE(editor.canUndo());
}

TEST_CASE("anim edit: delete, move and copy keys, copy and paste a pose, reset") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int l = editor.boneIndex("ValveBiped.Bip01_L_Hand"), r = editor.boneIndex("ValveBiped.Bip01_R_Hand");
	editor.setKey(l, 2, axisAngle(0, 0, 1, 0.3f), {});
	editor.setKey(r, 2, axisAngle(0, 0, 1, 0.6f), {});
	CHECK(editor.moveKeys({l}, 2, 7));
	CHECK_FALSE(editor.hasKey(l, 2));
	CHECK(editor.hasKey(l, 7));
	CHECK(editor.moveKeys({}, 7, 9, true));
	CHECK(editor.hasKey(l, 7));
	CHECK(editor.hasKey(l, 9));
	CHECK_FALSE(editor.deleteKeys({l}, 3));
	CHECK(editor.deleteKeys({}, 2));
	CHECK_FALSE(editor.hasKey(r, 2));
	const ClipPose copied = editor.copyPose({l}, 9);
	REQUIRE(copied.bones.size() == 1);
	editor.pastePose(copied, 1);
	CHECK(editor.hasKey(l, 1));
	editor.resetBones({l}, 9);
	CHECK(editor.offset(l, 9).first.w == doctest::Approx(1));
	editor.clearBones({l});
	CHECK(editor.keyCount() == 0);
}

TEST_CASE("anim edit: a new length moves the keys and stretches the base") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int hand = editor.boneIndex("ValveBiped.Bip01_L_Hand");
	editor.setKey(hand, 5, axisAngle(0, 0, 1, 0.2f), {});
	editor.setKey(hand, 10, axisAngle(0, 0, 1, 0.4f), {});
	editor.setFrameCount(21);
	CHECK(editor.clip().frames == 21);
	CHECK(editor.keyFrames() == std::vector<int>{10, 20});
	CHECK(position(editor.boneToModel(20)[1]).z == doctest::Approx(30));   // the base's end is the clip's end
	CHECK(position(editor.boneToModel(10)[1]).z == doctest::Approx(25));
	editor.setFrameCount(2);
	CHECK(editor.keyFrames() == std::vector<int>{1});   // both met at the end: the later one stays
	editor.setFps(0);   // ignored
	CHECK(editor.clip().fps == 30);
	editor.setFps(24);
	CHECK(editor.sequenceInfo().fps == 24);
	editor.setName("Better Wave!");
	CHECK(editor.clip().name == "better_wave");
}

TEST_CASE("anim edit: mirror puts a pose on the other side") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	const int lArm = editor.boneIndex("ValveBiped.Bip01_L_UpperArm"), rArm = editor.boneIndex("ValveBiped.Bip01_R_UpperArm");
	const int lHand = editor.boneIndex("ValveBiped.Bip01_L_Hand"), rHand = editor.boneIndex("ValveBiped.Bip01_R_Hand");
	// At rest the two sides are mirror images: mirroring changes nothing.
	CHECK(editor.mirrorPose({}, 0) == 0);
	editor.rotateBone(lArm, 4, axisAngle(1, 0, 0, 0.7f));   // lifts the left arm
	CHECK(editor.mirrorPose({rArm}, 4) == 1);                // the right one does the same
	const auto m = editor.boneToModel(4);
	const AnimVec left = position(m[std::size_t(lHand)]), right = position(m[std::size_t(rHand)]);
	CHECK(right.x == doctest::Approx(left.x).epsilon(1e-3));
	CHECK(right.y == doctest::Approx(-left.y).epsilon(1e-3));
	CHECK(right.z == doctest::Approx(left.z).epsilon(1e-3));
	CHECK(left.z > 31.f);
}

TEST_CASE("anim edit: bone names by side and group") {
	CHECK(mirroredBoneName("ValveBiped.Bip01_L_Hand") == "ValveBiped.Bip01_R_Hand");
	CHECK(mirroredBoneName("ValveBiped.Bip01_R_Finger12") == "ValveBiped.Bip01_L_Finger12");
	CHECK(mirroredBoneName("v_weapon.Left_Arm") == "v_weapon.Right_Arm");
	CHECK(mirroredBoneName("mixamorig:LeftHand") == "mixamorig:RightHand");
	CHECK(mirroredBoneName("leftover") == "");
	CHECK(mirroredBoneName("ValveBiped.Bip01_Spine") == "");
	CHECK(mirroredBoneName("hand.l") == "hand.r");
	CHECK(boneGroup("ValveBiped.Bip01_R_Forearm") == "Right arm");
	CHECK(boneGroup("ValveBiped.Bip01_L_Finger0") == "Left fingers");
	CHECK(boneGroup("ValveBiped.Bip01_Head1") == "Spine and head");
	CHECK(boneGroup("v_weapon.slide") == "Weapon");
	CHECK(boneGroup("root") == "");
}

TEST_CASE("anim edit: the project file keeps the keys and checks what it reads") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	editor.setKey(editor.boneIndex("ValveBiped.Bip01_L_Hand"), 3, axisAngle(0, 0, 1, 0.5f), {1, 2, 3});
	editor.setInterp({}, 3, KeyInterp::Linear);
	editor.setActivity("ACT_VM_RELOAD");
	editor.setLoop(true);
	const std::string text = writeAnimClip(editor.clip());
	std::string error;
	const auto back = readAnimClip(text, &error);
	REQUIRE_MESSAGE(back, error);
	CHECK(back->name == "wave_better");
	CHECK(back->baseSequence == "wave");
	CHECK(back->activity == "ACT_VM_RELOAD");
	CHECK(back->loop);
	REQUIRE(back->tracks.size() == 1);
	const auto& key = back->tracks.begin()->second.at(0);
	CHECK(key.frame == 3);
	CHECK(key.interp == KeyInterp::Linear);
	CHECK(key.pos.y == doctest::Approx(2));
	CHECK(key.rot.z == doctest::Approx(editor.clip().tracks.begin()->second[0].rot.z));

	CHECK_FALSE(readAnimClip("not { a project", &error));
	CHECK_FALSE(readAnimClip("\"other\" { }", &error));
	CHECK_FALSE(readAnimClip("\"smanim\" { \"frames\" \"0\" }", &error));
	// Keys outside the clip, broken numbers and quotes in names are dropped; huge lengths limited.
	const auto odd = readAnimClip("\"smanim\" { \"frames\" \"999999999\" \"name\" \"../x\" \"bone\" { \"name\" \"a\" "
	                              "\"key\" \"-1 smooth 0 0 0 1 0 0 0\" \"key\" \"2 smooth nan 0 0 1 0 0 0\" \"key\" \"4 smooth 0 0 0 0 1e30 0 0\" "
	                              "\"key\" \"5 linear 0 0 0 0 0 0 0\" } }",
	                              &error);
	REQUIRE(odd);
	CHECK(odd->frames == ANIM_CLIP_MAX_FRAMES);
	CHECK(odd->name == "x");
	REQUIRE(odd->tracks.count("a") == 1);
	REQUIRE(odd->tracks.at("a").size() == 1);
	CHECK(odd->tracks.at("a")[0].rot.w == doctest::Approx(1));   // a zero rotation becomes none
	CHECK(animClipPath("models/weapons/v_smg1.mdl", "Reload Fast") == "modelsrc/weapons/v_smg1/animations/reload_fast.smanim");
	CHECK(animClipPath("models/../../evil.mdl", "x") == "modelsrc/evil/animations/x.smanim");
}

TEST_CASE("anim edit: the baked .smd has every bone in every frame") {
	const auto model = loadBody();
	AnimEditor editor(model, tweakClip(model));
	editor.moveBone(editor.boneIndex("ValveBiped.Bip01_L_Hand"), 0, {0, 0, 5});
	std::string error;
	const auto smd = parseSmd(editor.smd(), &error);
	REQUIRE_MESSAGE(smd, error);
	CHECK(smd->nodes.size() == model.bones().size());
	REQUIRE(smd->frames.size() == 11);
	CHECK_FALSE(animationNeedsCompleting(*smd, model.bones()));
	// The preview of the .smd shows what the editor shows (turned like studiomdl turns it).
	AnimatedModel copy = model;
	ImportedAnimation options;
	options.name = "check";
	const int seq = addPreviewSequence(copy, *smd, options);
	const auto want = editor.boneToModel(10);
	const auto got = copy.boneToModel(seq, 1.f);
	for (std::size_t b = 0; b < want.size(); ++b) {
		CHECK(position(got[b]).x == doctest::Approx(position(want[b]).x).epsilon(1e-3));
		CHECK(position(got[b]).y == doctest::Approx(position(want[b]).y).epsilon(1e-3));
		CHECK(position(got[b]).z == doctest::Approx(position(want[b]).z).epsilon(1e-3));
	}
	const auto info = editor.sequenceInfo();
	CHECK(info.name == "wave_better");
	CHECK(info.frames == 11);
}

TEST_CASE("anim edit: replacing a sequence in a decompiled .qc keeps everything else") {
	const std::string qc = "$modelname \"weapons/v_smg1.mdl\"\n"
	                       "$sequence \"idle01\" \"idle01.smd\" fps 30 loop activity \"ACT_VM_IDLE\" 1\r\n"
	                       "\t$sequence \"reload\" \"reload.smd\" fps 30 activity \"ACT_VM_RELOAD\" 1 { event 5004 10 \"Weapon_SMG1.Reload\" }\n"
	                       "$sequence \"fire01\" \"fire01.smd\" fps 30\n";
	const auto replaced = replaceSequenceInQc(qc, "RELOAD", 40, true);
	REQUIRE(replaced);
	CHECK(replaced->smdFile == "reload.smd");
	CHECK(replaced->qc.find("\t$sequence \"reload\" \"reload.smd\" fps 40 loop activity \"ACT_VM_RELOAD\" 1 { event 5004 10 \"Weapon_SMG1.Reload\" }\n") !=
	      std::string::npos);
	CHECK(replaced->qc.find("$sequence \"idle01\" \"idle01.smd\" fps 30 loop activity \"ACT_VM_IDLE\" 1\r\n") != std::string::npos);
	const auto unlooped = replaceSequenceInQc(qc, "idle01", 30, false);
	REQUIRE(unlooped);
	CHECK(unlooped->qc.find("$sequence \"idle01\" \"idle01.smd\" fps 30 activity \"ACT_VM_IDLE\" 1\r\n") != std::string::npos);
	CHECK_FALSE(replaceSequenceInQc(qc, "nothere", 30, false));
}

TEST_CASE("anim edit: checksums of a compiled model's files") {
	const auto files = test::makeStudioModel(bodyModel());
	const auto mdl = mdlChecksum(files.mdl);
	REQUIRE(mdl);
	std::vector<std::byte> vvd(64), vtx(64), phy(64);
	std::memcpy(vvd.data(), "IDSV", 4);
	std::memcpy(vvd.data() + 8, &*mdl, 4);
	const int version = 7, sixteen = 16;
	std::memcpy(vtx.data(), &version, 4);
	std::memcpy(vtx.data() + 16, &*mdl, 4);
	std::memcpy(phy.data(), &sixteen, 4);
	std::memcpy(phy.data() + 12, &*mdl, 4);
	ModelChecksums sums;
	sums.mdl = mdl;
	sums.vvd = vvdChecksum(vvd);
	sums.dx90 = vtxChecksum(vtx);
	sums.phy = phyChecksum(phy);
	CHECK(sums.consistent());
	sums.phy = *mdl + 1;
	CHECK_FALSE(sums.consistent());
	sums.phy.reset();
	sums.dx90.reset();
	CHECK_FALSE(sums.consistent());   // the .dx90.vtx must be there
	CHECK_FALSE(vvdChecksum(std::vector<std::byte>(3)));
	CHECK_FALSE(mdlChecksum(vvd));
}
