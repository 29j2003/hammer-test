// test_animation_import.cpp - Animations brought onto a model: .smd reading, skeleton matching,
// the preview, the written reference .smd and .qc, and which entities use which animation.

#include "fixture.h"

#include <sme/animation_import.h>
#include <sme/animation_use.h>
#include <sme/entity_catalog.h>
#include <sme/map.h>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace sme;

namespace {

constexpr float PI = std::numbers::pi_v<float>;

test::StudioModelSpec armModel() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"arm", {{-4, -4, 0, 4, 4, 8}, {-2, -2, 32, 2, 2, 40}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"Hand", 0, {0, 0, 32}, {0, 0, 0}}};
	spec.boxBones = {0, 1};
	spec.animations.push_back({"idle", "ACT_IDLE", 30, true, {{{0, 0, 0, 0, 0, 0}, {0, 0, 32, 0, 0, 0}}}});
	return spec;
}

const char* WAVE_SMD = R"(version 1
// exported by hand
nodes
0 "root" -1
1 "hand" 0
2 "tail" 0
end
skeleton
time 0
0 0 0 0 0 0 -1.570796
1 0 0 32 0 0 0
2 0 0 -10 0 0 0
time 1
1 0 0 32 0.5 0 0
time 2
0 0 0 0 0.5 0 -1.570796
end
)";

} // namespace

TEST_CASE("animation import: .smd files read, with clear errors for broken ones") {
	std::string error;
	const auto smd = parseSmd(WAVE_SMD, &error);
	REQUIRE_MESSAGE(smd, error);
	REQUIRE(smd->nodes.size() == 3);
	CHECK(smd->nodes[1].name == "hand");
	CHECK(smd->nodes[1].parent == 0);
	REQUIRE(smd->frames.size() == 3);
	// Bones a frame does not list keep their values from the frame before.
	CHECK(smd->frames[1][0].second.z == doctest::Approx(-1.570796));
	CHECK(smd->frames[1][1].second.x == doctest::Approx(0.5));
	CHECK(smd->frames[2][1].second.x == doctest::Approx(0.5));
	CHECK(smd->frames[2][0].second.x == doctest::Approx(0.5));

	for (const char* broken : {"", "hello", "version 1\nnodes\nend\nskeleton\ntime 0\nend\n", "version 1\nnodes\n0 \"a\" -1\nend\n",
	                           "version 1\nnodes\n0 \"a\" -1\nend\nskeleton\ntime 0\n0 x 0 0 0 0 0\nend\n",
	                           "version 1\nnodes\n0 \"a\" -1\nend\nskeleton\ntime 0\n3 0 0 0 0 0 0\nend\n",
	                           "version 1\nnodes\n0 \"a\" 1\n1 \"b\" -1\nend\n", "version 1\nnodes\n1 \"a\" -1\nend\n",
	                           "version 1\nnodes\n0 \"a\" -1\nend\nskeleton\n0 0 0 0 0 0 0\nend\n"}) {
		CHECK_FALSE(parseSmd(broken, &error));
		CHECK_FALSE(error.empty());
	}
	CHECK(error.find("line") != std::string::npos);
}

TEST_CASE("animation import: matched to the model's skeleton and shown at once") {
	const auto files = test::makeStudioModel(armModel());
	std::string error;
	auto model = AnimatedModel::fromFiles({{"models/arm.mdl", files.mdl, {}}}, &error);
	REQUIRE_MESSAGE(model, error);
	const auto geometry = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error);
	REQUIRE(geometry);
	const auto smd = parseSmd(WAVE_SMD);
	REQUIRE(smd);
	const auto match = matchSkeleton(*smd, model->bones());
	CHECK(match.matched() == 2);
	CHECK(match.toModel == std::vector<int>{0, 1, -1});   // names match whatever their case
	CHECK(match.notInModel == std::vector<std::string>{"tail"});
	CHECK(match.notInSmd.empty());

	// The preview: the file's root at -90 degrees about Z is the model's root at rest
	// (studiomdl turns .smd data by +90 degrees), the hand then rolls by 0.5.
	const int index = addPreviewSequence(*model, *smd, {"wave", "wave.smd", 10, true, "ACT_SIGNAL1"});
	REQUIRE(index == 1);
	CHECK(model->sequences()[1].model == -1);
	CHECK(model->sequences()[1].frames == 3);
	CHECK(model->sequences()[1].looping);
	CHECK(model->sequences()[1].activity == "ACT_SIGNAL1");
	std::vector<ModelVertex> moved;
	skinVertices(*geometry, model->skinning(index, 0), moved);
	for (std::size_t i = 0; i < moved.size(); ++i) {
		for (int k = 0; k < 3; ++k) CHECK(moved[i].position[k] == doctest::Approx(geometry->vertices[i].position[k]).epsilon(0.001));
	}
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	model->localPose(index, 0.5f, pos, quat);
	CHECK(quat[1].x == doctest::Approx(std::sin(0.25f)).epsilon(0.001));
	// Importing it again under the same name replaces the preview.
	CHECK(addPreviewSequence(*model, *smd, {"wave", "wave.smd", 10, true, ""}) == 1);
	CHECK(model->sequences().size() == 2);
}

TEST_CASE("animation import: Euler angles, the reference .smd and the .qc of the new model") {
	// Angles -> rotation -> angles gives the same rotation back.
	for (const AnimVec angles : {AnimVec{0.3f, -0.2f, 1.1f}, AnimVec{0, 0, PI / 2}, AnimVec{-1.2f, 0.4f, -2.5f}, AnimVec{0, 1.2f, 0}}) {
		const AnimQuat q = angleQuaternion(angles);
		const AnimQuat back = angleQuaternion(quaternionAngles(q));
		const float dot = std::abs(q.x * back.x + q.y * back.y + q.z * back.z + q.w * back.w);
		CHECK(dot == doctest::Approx(1).epsilon(0.0001));
	}

	auto spec = armModel();
	spec.cdmaterials = "models/props/";
	spec.textures = {"armskin"};
	const auto files = test::makeStudioModel(spec);
	std::string error;
	const auto model = AnimatedModel::fromFiles({{"models/arm.mdl", files.mdl, {}}}, &error);
	const auto geometry = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error);
	REQUIRE(model);
	REQUIRE(geometry);
	const std::string smdText = referenceSmd(*geometry, model->bones());
	CHECK(smdText.find("triangles\narmskin\n") != std::string::npos);
	// What studiomdl reads back is the model again: the written skeleton, read as an animation,
	// is the rest pose.
	const auto reference = parseSmd(smdText, &error);
	REQUIRE_MESSAGE(reference, error);
	auto copy = *model;
	const int rest = addPreviewSequence(copy, *reference, {"rest", "reference.smd", 30, false, ""});
	std::vector<ModelVertex> moved;
	skinVertices(*geometry, copy.skinning(rest, 0), moved);
	for (std::size_t i = 0; i < moved.size(); ++i) {
		for (int k = 0; k < 3; ++k) CHECK(moved[i].position[k] == doctest::Approx(geometry->vertices[i].position[k]).epsilon(0.001));
	}
	// Every triangle, three vertices each, turned back by 90 degrees about Z ((x, y) -> (y, -x)).
	std::size_t triangles = 0, pos = 0;
	while ((pos = smdText.find("\narmskin\n", pos)) != std::string::npos) {
		++triangles;
		++pos;
	}
	CHECK(triangles == geometry->indices.size() / 3);
	const auto first = smdText.substr(smdText.find("\narmskin\n") + 9);
	const auto& v = geometry->vertices[geometry->indices[geometry->meshes.front().firstIndex]];
	float bone = 0, x = 0, y = 0;
	REQUIRE(std::sscanf(first.c_str(), "%f %f %f", &bone, &x, &y) == 3);
	CHECK(x == doctest::Approx(v.position[1]).epsilon(0.001));
	CHECK(y == doctest::Approx(-v.position[0]).epsilon(0.001));

	ModelInfo info;
	info.surfaceProp = "flesh";
	info.materialDirs = {"models/props/"};
	info.keyValues = "\"prop_data\" { \"base\" \"Flesh.Small\" }";
	const std::string qc = animationModelQc("models/creatures/arm.mdl", *geometry, info, {{"hand_tip", "Hand", {0, 0, 8}, {0, 90, 0}}},
	                                        {{"wave", "wave.smd", 10, true, "ACT_SIGNAL1"}, {"bow", "bow.smd", 30, false, ""}});
	CHECK(qc.find("$modelname \"mapsmith/creatures/arm.mdl\"") != std::string::npos);
	CHECK(qc.find("$includemodel \"creatures/arm.mdl\"") != std::string::npos);
	CHECK(qc.find("$cdmaterials \"models/props/\"") != std::string::npos);
	CHECK(qc.find("$surfaceprop \"flesh\"") != std::string::npos);
	CHECK(qc.find("$body \"body\" \"reference.smd\"") != std::string::npos);
	CHECK(qc.find("$attachment \"hand_tip\" \"Hand\" 0 0 8 rotate 0 90 0") != std::string::npos);
	CHECK(qc.find("$sequence \"wave\" \"wave.smd\" fps 10 loop activity \"ACT_SIGNAL1\" 1") != std::string::npos);
	CHECK(qc.find("$sequence \"bow\" \"bow.smd\" fps 30\n") != std::string::npos);
	CHECK(qc.find("Flesh.Small") != std::string::npos);
	CHECK(animationModelPath("models/creatures/arm.mdl") == "models/mapsmith/creatures/arm.mdl");
	CHECK(animationModelPath("models/mapsmith/creatures/arm.mdl") == "models/mapsmith/creatures/arm.mdl");   // ours again: the same file
	CHECK(animationSourceFolder("models\\Creatures\\arm.mdl") == "modelsrc/mapsmith/creatures/arm");

	// The list of added animations survives a round trip.
	const std::vector<ImportedAnimation> list{{"wave", "wave.smd", 12.5f, true, "ACT_SIGNAL1"}, {"bow", "bow.smd", 30, false, ""}};
	const auto back = readAnimationList(writeAnimationList(list));
	REQUIRE(back.size() == 2);
	CHECK(back[0].name == "wave");
	CHECK(back[0].fps == doctest::Approx(12.5));
	CHECK(back[0].loop);
	CHECK(back[0].activity == "ACT_SIGNAL1");
	CHECK_FALSE(back[1].loop);
	CHECK(readAnimationList("broken {").empty());
	CHECK(sequenceNameFromFile("C:/anims/Wave Hello!.smd") == "wave_hello");
	CHECK(sequenceNameFromFile("...") == "new_animation");
	CHECK(readAttachments(std::vector<std::byte>(16)).empty());
}

TEST_CASE("animation use: which entities play which animation, and putting one on an entity") {
	MapDocument doc;
	const auto add = [&](std::vector<std::pair<std::string, std::string>> keys) {
		Entity e;
		e.id = doc.newEntityId();
		e.keyValues = std::move(keys);
		e.set("origin", "16 32 0");
		doc.entities.push_back(e);
		return e.id;
	};
	const int npc = add({{"classname", "npc_citizen"}, {"targetname", "bob"}, {"model", "models/Humans/male.mdl"}});
	const int script = add({{"classname", "scripted_sequence"}, {"m_iszEntity", "bob"}, {"m_iszIdle", "sit"}, {"m_iszPlay", "wave"}});
	const int prop = add({{"classname", "prop_dynamic"}, {"model", "models/humans/male.mdl"}, {"DefaultAnim", " idle "}});
	add({{"classname", "prop_dynamic"}, {"model", "models/other.mdl"}, {"DefaultAnim", "spin"}});
	const int unnamed = add({{"classname", "npc_zombie"}});

	CHECK(animatedModel(doc, *doc.findEntity(script), nullptr) == "models/humans/male.mdl");
	const auto uses = sequenceUses(doc, nullptr, "models\\humans\\MALE.mdl");
	REQUIRE(uses.size() == 3);
	CHECK(uses[0].entityId == script);
	CHECK(uses[0].actorId == npc);
	CHECK(uses[0].label == "Pre Action Idle");
	CHECK(uses[0].sequence == "sit");
	CHECK(uses[1].sequence == "wave");
	CHECK(uses[2].entityId == prop);
	CHECK(uses[2].sequence == "idle");
	CHECK(sequenceKeys("scripted_sequence").size() == 5);
	CHECK(sequenceKeys("prop_dynamic_override").front().key == "DefaultAnim");
	CHECK(sequenceKeys("info_target").empty());
	CHECK(isNpcClass("npc_alyx"));
	CHECK_FALSE(isNpcClass("prop_dynamic"));

	// Props: DefaultAnim; scripts: the chosen field (else the Pre Action Idle); NPCs: a pose helper.
	auto change = edit::useSequence(doc, prop, "walk");
	CHECK_FALSE(change.empty());
	CHECK(doc.findEntity(prop)->get("DefaultAnim") == "walk");
	change = edit::useSequence(doc, script, "jump", "m_iszplay");
	CHECK(doc.findEntity(script)->get("m_iszPlay") == "jump");
	change = edit::useSequence(doc, script, "rest");
	CHECK(doc.findEntity(script)->get("m_iszIdle") == "rest");
	const std::size_t before = doc.entities.size();
	change = edit::useSequence(doc, npc, "cower");
	REQUIRE(doc.entities.size() == before + 1);
	const Entity& helper = doc.entities.back();
	CHECK(helper.classname() == "scripted_sequence");
	CHECK(helper.get("targetname") == "bob_pose");
	CHECK(helper.get("m_iszEntity") == "bob");
	CHECK(helper.get("m_iszIdle") == "cower");
	CHECK(helper.get("spawnflags") == "112");   // Start on Spawn, No Interruptions, Override AI: it idles from map start
	CHECK(helper.origin().x == doctest::Approx(16));
	// Again: the same helper is updated, no second one.
	change = edit::useSequence(doc, npc, "sit");
	CHECK(doc.entities.size() == before + 1);
	CHECK(doc.entities.back().get("m_iszIdle") == "sit");
	// A helper from before (no Start on Spawn, it never ran) is mended.
	doc.entities.back().set("spawnflags", "352");
	change = edit::useSequence(doc, npc, "cower");
	CHECK(doc.entities.back().get("spawnflags") == "112");
	// An NPC with a new model: model and helper in one step.
	change = edit::useSequence(doc, npc, "cower_new", {}, "models/mapsmith/humans/bob.mdl");
	CHECK(doc.findEntity(npc)->get("model") == "models/mapsmith/humans/bob.mdl");
	CHECK(doc.entities.back().get("m_iszIdle") == "cower_new");
	CHECK(change.entities.size() == 2);
	REQUIRE(change.entities.front().before);
	CHECK(change.entities.front().before->get("model") == "models/Humans/male.mdl");
	// An NPC without a name gets one.
	change = edit::useSequence(doc, unnamed, "idle");
	CHECK(doc.findEntity(unnamed)->get("targetname") == "npc_zombie_" + std::to_string(unnamed));
	// With a new model (animations only it has): the moving entity switches to it in the same step.
	change = edit::useSequence(doc, script, "wave_new", "m_iszPlay", "models/mapsmith/humans/male.mdl");
	CHECK(doc.findEntity(script)->get("m_iszPlay") == "wave_new");
	CHECK(doc.findEntity(npc)->get("model") == "models/mapsmith/humans/male.mdl");
	CHECK(change.entities.size() == 2);
	CHECK(change.entities.back().before->get("model") == "models/mapsmith/humans/bob.mdl");
	change = edit::useSequence(doc, prop, "wave_new", {}, "models/mapsmith/humans/male.mdl");
	CHECK(doc.findEntity(prop)->get("model") == "models/mapsmith/humans/male.mdl");
	// Nothing for classes without animations, the world, empty names.
	const int light = add({{"classname", "light"}});
	CHECK(edit::useSequence(doc, light, "x").empty());
	CHECK(edit::useSequence(doc, 0, "x").empty());
	CHECK(edit::useSequence(doc, prop, "  ").empty());
}
