// test_model_export.cpp - Taking a model apart for editing: reference, animation and collision
// .smd files that give the model back, the .qc, the materials, and broken files.

#include "fixture.h"

#include <sme/animation_import.h>
#include <sme/filesystem.h>
#include <sme/model_export.h>
#include <sme/model_import.h>

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <numbers>

using namespace sme;

namespace {

constexpr float PI = std::numbers::pi_v<float>;

/// A weapon-like model: a skeleton, two body groups (the second with a blank), two skins, two
/// sequences (one moving the hand), a hit box and a physics model.
test::StudioModelSpec gunModel() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"studio", {{"gun_reference", {{-4, -2, 0, 12, 2, 6}, {8, -1, 6, 10, 1, 12}}}}});
	spec.groups.push_back({"sight", {{"sight_on", {{0, -1, 6, 2, 1, 8}}}, {"blank", {}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"Hand", 0, {8, 0, 6}, {0, 0, 0}}};
	spec.boxBones = {0, 1, 0};
	spec.textures = {"gun_skin", "gun_skin_gold"};
	spec.cdmaterials = "models/weapons/gun/";
	spec.keyValues = "\"prop_data\" { \"base\" \"Metal.Small\" }";
	spec.animations.push_back({"idle", "ACT_VM_IDLE", 30, true, {{{0, 0, 0, 0, 0, 0}, {8, 0, 6, 0, 0, 0}}}});
	spec.animations.push_back({"fire", "ACT_VM_PRIMARYATTACK", 15, false,
	                           {{{0, 0, 0, 0, 0, 0}, {8, 0, 6, 0, 0, 0}}, {{0, 0, 0, 0, 0, 0}, {8, 0, 7, 0.4f, 0, 0}}, {{0, 0, 0, 0, 0, 0}, {8, 0, 6, 0, 0, 0}}}});
	return spec;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& data) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

} // namespace

TEST_CASE("model export: reference, animation and collision .smd files give the model back, with a .qc studiomdl reads") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "export/hl2";
	auto files = test::makeStudioModel(gunModel());
	test::addHitbox(files.mdl, 1, {-1, -1, -1, 1, 2, 3});
	test::writeStudioModel(game / "models/weapons/v_gun.mdl", files);
	writeBytes(game / "models/weapons/v_gun.phy",
	           test::makePhy({{{0, 0, 0}, 10.f}}, "solid {\n\"index\" \"0\"\n\"name\" \"root\"\n\"mass\" \"3.5\"\n}\neditparams {\n\"totalmass\" \"3.5\"\n}\n"));
	test::writeText(game / "materials/models/weapons/gun/gun_skin.vmt",
	                "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/weapons/gun/gun_skin\"\n\t\"$envmap\" \"env_cubemap\"\n}\n");
	writeBytes(game / "materials/models/weapons/gun/gun_skin.vtf", test::makeVtf(16, 200, 120, 40));
	GameFileSystem fs;
	fs.addDirectory(game, {"game"});

	std::string error;
	int steps = 0;
	const auto exported = decompileModel(fs, "models/weapons/v_gun.mdl", {}, &error, [&](int done, int total) {
		CHECK(done <= total);
		++steps;
		return true;
	});
	REQUIRE_MESSAGE(exported, error);
	CHECK(steps > 0);
	CHECK(exported->qcFile == "v_gun.qc");
	CHECK(exported->sequences == 2);
	REQUIRE(exported->texts.count("gun_reference.smd"));
	REQUIRE(exported->texts.count("sight_on.smd"));
	REQUIRE(exported->texts.count("anims/fire.smd"));
	REQUIRE(exported->texts.count("v_gun_physics.smd"));

	// The reference .smd read as the model's rest pose moves no vertex (studiomdl gets the same model).
	auto model = AnimatedModel::fromFiles({{"models/weapons/v_gun.mdl", files.mdl, {}}}, &error);
	REQUIRE(model);
	const auto& referenceText = exported->texts.at("gun_reference.smd");
	const auto reference = parseSmd(referenceText, &error);
	REQUIRE_MESSAGE(reference, error);
	REQUIRE(reference->nodes.size() == 2);
	CHECK(reference->nodes[1].name == "Hand");
	CHECK(reference->nodes[1].parent == 0);
	const auto part = buildBodyPartGeometry({files.mdl, files.vvd, files.vtx}, 0, 0, &error);
	REQUIRE(part);
	auto copy = *model;
	const int rest = addPreviewSequence(copy, *reference, {"rest", "gun_reference.smd", 30, false, ""});
	std::vector<ModelVertex> moved;
	skinVertices(*part, copy.skinning(rest, 0), moved);
	for (std::size_t i = 0; i < moved.size(); ++i) {
		for (int k = 0; k < 3; ++k) CHECK(moved[i].position[k] == doctest::Approx(part->vertices[i].position[k]).epsilon(0.001));
	}
	// Its triangles and bone weights as the importer reads them: the material by its name, the
	// second box on the hand.
	const auto mesh = readSmd(referenceText, &error);
	REQUIRE_MESSAGE(mesh, error);
	CHECK(mesh->triangles.size() == part->indices.size() / 3);
	CHECK(mesh->materials == std::vector<std::string>{"gun_skin"});
	CHECK(std::any_of(mesh->triangles.begin(), mesh->triangles.end(), [](const ImportMesh::Triangle& t) { return t.corners[0].bone == 1; }));
	CHECK(mesh->bones.size() == 2);

	// The animation read back is the sequence: the hand at the middle frame of "fire".
	const auto fire = parseSmd(exported->texts.at("anims/fire.smd"), &error);
	REQUIRE_MESSAGE(fire, error);
	CHECK(fire->frames.size() == 3);
	const int back = addPreviewSequence(copy, *fire, {"fire_back", "fire.smd", 15, false, ""});
	std::vector<AnimVec> wantPos, gotPos;
	std::vector<AnimQuat> wantQuat, gotQuat;
	model->localPose(model->findSequence("fire"), 0.5f, wantPos, wantQuat);
	copy.localPose(back, 0.5f, gotPos, gotQuat);
	for (std::size_t b = 0; b < wantPos.size(); ++b) {
		CHECK(gotPos[b].z == doctest::Approx(wantPos[b].z).epsilon(0.001));
		CHECK(std::abs(gotQuat[b].x * wantQuat[b].x + gotQuat[b].y * wantQuat[b].y + gotQuat[b].z * wantQuat[b].z + gotQuat[b].w * wantQuat[b].w) ==
		      doctest::Approx(1).epsilon(0.001));
	}

	// The collision: the tetrahedron in the .smd's space, turned back like the vertices ((x, y) -> (y, -x)).
	const auto physics = readSmd(exported->texts.at("v_gun_physics.smd"), &error);
	REQUIRE_MESSAGE(physics, error);
	CHECK(physics->triangles.size() == 4);
	const auto bounds = physics->bounds();
	CHECK(bounds.min.x == doctest::Approx(0).epsilon(0.01));
	CHECK(bounds.max.x == doctest::Approx(10).epsilon(0.01));
	CHECK(bounds.min.y == doctest::Approx(-10).epsilon(0.01));
	CHECK(bounds.max.z == doctest::Approx(10).epsilon(0.01));

	// The .qc.
	const auto& qc = exported->texts.at("v_gun.qc");
	CHECK(qc.find("$modelname \"weapons/v_gun.mdl\"") != std::string::npos);
	CHECK(qc.find("$cdmaterials \"models/weapons/gun/\"") != std::string::npos);
	CHECK(qc.find("$bodygroup \"studio\"\n{\n\tstudio \"gun_reference.smd\"\n}") != std::string::npos);
	CHECK(qc.find("$bodygroup \"sight\"\n{\n\tstudio \"sight_on.smd\"\n\tblank\n}") != std::string::npos);
	CHECK(qc.find("$texturegroup \"skinfamilies\"\n{\n\t{ \"gun_skin\" }\n\t{ \"gun_skin_gold\" }\n}") != std::string::npos);
	CHECK(qc.find("$sequence \"idle\" \"anims/idle.smd\" fps 30 loop activity \"ACT_VM_IDLE\" 1") != std::string::npos);
	CHECK(qc.find("$sequence \"fire\" \"anims/fire.smd\" fps 15 activity \"ACT_VM_PRIMARYATTACK\" 1") != std::string::npos);
	CHECK(qc.find("$hboxset \"default\"\n$hbox 3 \"Hand\" -1 -1 -1 1 2 3") != std::string::npos);
	CHECK(qc.find("$collisionmodel \"v_gun_physics.smd\"\n{\n\t$mass 3.5\n}") != std::string::npos);
	CHECK(qc.find("Metal.Small") != std::string::npos);
	// Every file the .qc names is part of the export.
	for (std::size_t pos = 0; (pos = qc.find(".smd\"", pos)) != std::string::npos; ++pos) {
		const std::size_t open = qc.rfind('"', pos - 1);
		const std::string file = qc.substr(open + 1, pos + 4 - open - 1);
		CHECK_MESSAGE(exported->texts.count(file), file);
	}

	// Materials: the .vmt and its texture, also as a picture; the cubemap is not a file.
	CHECK(exported->copies.count("materials/models/weapons/gun/gun_skin.vmt"));
	CHECK(exported->copies.count("materials/models/weapons/gun/gun_skin.vtf"));
	CHECK(exported->copies.size() == 2);
	CHECK(std::any_of(exported->notes.begin(), exported->notes.end(), [](const std::string& n) { return n.find("gun_skin_gold") != std::string::npos; }));

	const auto folder = modelExportFolder(f.root / "export/mymod", "models/weapons/v_gun.mdl");
	CHECK(folder == f.root / "export/mymod/modelsrc/weapons/v_gun");
	const auto written = writeModelExport(fs, *exported, folder, &error);
	REQUIRE_MESSAGE(written, error);
	CHECK(std::filesystem::exists(folder / "v_gun.qc"));
	CHECK(std::filesystem::exists(folder / "anims/idle.smd"));
	CHECK(std::filesystem::exists(folder / "materials/models/weapons/gun/gun_skin.vtf"));
	CHECK(std::filesystem::exists(folder / "materials/models/weapons/gun/gun_skin.png"));
	CHECK(written->size() == exported->texts.size() + exported->copies.size() + 1);

	// One animation alone, and stopping.
	ModelExportOptions only;
	only.sequences = {"FIRE", "missing"};
	only.materials = false;
	only.physics = false;
	const auto single = decompileModel(fs, "models/weapons/v_gun.mdl", only, &error);
	REQUIRE(single);
	CHECK(single->sequences == 1);
	CHECK(single->texts.count("anims/fire.smd"));
	CHECK_FALSE(single->texts.count("anims/idle.smd"));
	CHECK(single->copies.empty());
	CHECK(std::any_of(single->notes.begin(), single->notes.end(), [](const std::string& n) { return n.find("missing") != std::string::npos; }));
	CHECK_FALSE(decompileModel(fs, "models/weapons/v_gun.mdl", {}, &error, [](int, int) { return false; }));
	CHECK(error == "stopped");
	CHECK_FALSE(decompileModel(fs, "models/none.mdl", {}, &error));
	CHECK_FALSE(decompileModel(fs, "materials/models/weapons/gun/gun_skin.vmt", {}, &error));
}

TEST_CASE("model export: physics models of ragdolls, hit boxes and skins from broken files") {
	// Two solids: a ragdoll, each piece on its bone with the joint limits of the text part.
	const auto phy = test::makePhy({{{0, 0, 0}, 4.f}, {{0, 0, 0}, 2.f}},
	                               "solid {\n\"index\" \"0\"\n\"name\" \"root\"\n}\nsolid {\n\"index\" \"1\"\n\"name\" \"Hand\"\n}\n"
	                               "ragdollconstraint {\n\"parent\" \"0\"\n\"child\" \"1\"\n\"xmin\" \"-20\"\n\"xmax\" \"30\"\n\"xfriction\" \"1\"\n}\n"
	                               "editparams {\n\"totalmass\" \"12\"\n\"concave\" \"0\"\n}\n");
	std::string error;
	const auto physics = readPhysicsModel(phy, &error);
	REQUIRE_MESSAGE(physics, error);
	REQUIRE(physics->solids.size() == 2);
	CHECK(physics->solids[1].bone == "Hand");
	CHECK(physics->mass == doctest::Approx(12));
	CHECK_FALSE(physics->concave);
	REQUIRE(physics->constraints.size() == 3);
	CHECK(physics->constraints[0].bone == "Hand");
	CHECK(physics->constraints[0].min == doctest::Approx(-20));
	CHECK(physics->constraints[0].max == doctest::Approx(30));
	// The hand's piece sits where the hand is: 8 0 6 in the model, turned back for the .smd.
	std::vector<StudioBone> bones(2);
	bones[0].name = "root";
	bones[1].name = "Hand";
	bones[1].parent = 0;
	bones[1].pos = {8, 0, 6};
	const auto smd = readSmd(physicsSmd(*physics, bones), &error);
	REQUIRE_MESSAGE(smd, error);
	CHECK(smd->triangles.size() == 8);
	const auto handCorner = smd->triangles[4].corners[0];
	CHECK(handCorner.bone == 1);
	CHECK(handCorner.position.x == doctest::Approx(0).epsilon(0.01));
	CHECK(handCorner.position.y == doctest::Approx(-8).epsilon(0.01));
	CHECK(handCorner.position.z == doctest::Approx(6).epsilon(0.01));

	// Broken files give nothing, never a crash.
	CHECK_FALSE(readPhysicsModel(std::vector<std::byte>(8), &error));
	auto cut = phy;
	cut.resize(60);
	CHECK_FALSE(readPhysicsModel(cut, &error));
	CHECK(readHitboxes(std::vector<std::byte>(16)).empty());
	CHECK(readSkinFamilies(std::vector<std::byte>(500)).empty());
	const auto files = test::makeStudioModel(gunModel());
	CHECK(readSkinFamilies(files.mdl).size() == 2);
	CHECK_FALSE(buildBodyPartGeometry({files.mdl, files.vvd, files.vtx}, 1, 5, &error));
	CHECK_FALSE(buildBodyPartGeometry({files.mdl, files.vvd, files.vtx}, 7, 0, &error));
	const auto blank = buildBodyPartGeometry({files.mdl, files.vvd, files.vtx}, 1, 1, &error);
	REQUIRE(blank);
	CHECK(blank->empty());
	(void)PI;
}
