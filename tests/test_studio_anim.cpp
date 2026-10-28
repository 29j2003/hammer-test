// test_studio_anim.cpp - Skeletal animation: skeleton, sequences (own, .ani, sections, include
// models), poses and skinning, with generated models.

#include "fixture.h"

#include <sme/filesystem.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <numbers>

using namespace sme;

namespace {

constexpr float PI = std::numbers::pi_v<float>;

/// A post (root bone) with a block on top (bone "hand", 32 units up).
test::StudioModelSpec armSpec() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"arm", {{-4, -4, 0, 4, 4, 8}, {-2, -2, 32, 2, 2, 40}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"hand", 0, {0, 0, 32}, {0, 0, 0}}};
	spec.boxBones = {0, 1};
	return spec;
}

/// Frames that roll the root from `from` to `to` (radians), the hand staying on it.
std::vector<std::vector<std::array<float, 6>>> roll(float from, float to, int frames) {
	std::vector<std::vector<std::array<float, 6>>> out;
	for (int f = 0; f < frames; ++f) {
		const float t = frames > 1 ? float(f) / float(frames - 1) : 0.f;
		out.push_back({{0, 0, 0, from + (to - from) * t, 0, 0}, {0, 0, 32, 0, 0, 0}});
	}
	return out;
}

/// Centre of the vertices weighted to `bone`.
AnimVec centre(const ModelGeometry& geometry, const std::vector<ModelVertex>& vertices, int bone) {
	AnimVec sum;
	int n = 0;
	for (std::size_t i = 0; i < vertices.size(); ++i) {
		if (geometry.weights[i].bone[0] != bone) continue;
		sum.x += vertices[i].position[0];
		sum.y += vertices[i].position[1];
		sum.z += vertices[i].position[2];
		++n;
	}
	return {sum.x / float(n), sum.y / float(n), sum.z / float(n)};
}

std::optional<AnimatedModel> loadFiles(const test::StudioModelFiles& files, std::string* error = nullptr) {
	std::vector<AnimatedModel::FileData> data{{"models/test_anim.mdl", files.mdl, files.ani}};
	return AnimatedModel::fromFiles(std::move(data), error);
}

} // namespace

TEST_CASE("animation: skeleton, sequences, poses between frames and the vertices that follow") {
	auto spec = armSpec();
	spec.animations.push_back({"idle", "ACT_IDLE", 30, true, roll(0, 0, 2)});
	spec.animations.push_back({"fall_over", "ACT_DIESIMPLE", 10, false, roll(0, PI / 2, 5)});
	const auto files = test::makeStudioModel(spec);
	std::string error;
	const auto geometry = buildModelGeometry({files.mdl, files.vvd, files.vtx}, 0, &error);
	REQUIRE_MESSAGE(geometry, error);
	REQUIRE(geometry->weights.size() == geometry->vertices.size());
	CHECK(geometry->weights.front().count == 1);

	const auto model = loadFiles(files, &error);
	REQUIRE_MESSAGE(model, error);
	REQUIRE(model->bones().size() == 2);
	CHECK(model->bones()[1].name == "hand");
	CHECK(model->bones()[1].parent == 0);
	REQUIRE(model->sequences().size() == 2);
	const auto& fall = model->sequences()[1];
	CHECK(fall.name == "fall_over");
	CHECK(fall.activity == "ACT_DIESIMPLE");
	CHECK_FALSE(fall.looping);
	CHECK(model->sequences()[0].looping);
	CHECK(fall.frames == 5);
	CHECK(fall.fps == doctest::Approx(10));
	CHECK(fall.duration() == doctest::Approx(0.4));
	CHECK(model->findSequence("FALL_OVER") == 1);
	CHECK(model->findSequence("nope") == -1);

	// The rest pose (and the first frame, which is the rest pose) leaves every vertex in place.
	std::vector<ModelVertex> moved;
	for (const int sequence : {-1, 0, 1}) {
		skinVertices(*geometry, model->skinning(sequence, 0), moved);
		REQUIRE(moved.size() == geometry->vertices.size());
		for (std::size_t i = 0; i < moved.size(); ++i) {
			for (int k = 0; k < 3; ++k) CHECK(moved[i].position[k] == doctest::Approx(geometry->vertices[i].position[k]).epsilon(0.001));
		}
	}
	// The last frame: the root rolled by 90 degrees about x - the hand (36 up) lies at y = -36.
	skinVertices(*geometry, model->skinning(1, 1), moved);
	const AnimVec hand = centre(*geometry, moved, 1);
	CHECK(hand.x == doctest::Approx(0).epsilon(0.01));
	CHECK(hand.y == doctest::Approx(-36).epsilon(0.01));
	CHECK(std::abs(hand.z) < 0.1f);
	// Half way: 45 degrees, between the stored frames (frame 2 of 0..4).
	skinVertices(*geometry, model->skinning(1, 0.5f), moved);
	const AnimVec half = centre(*geometry, moved, 1);
	CHECK(half.y == doctest::Approx(-36 * std::sin(PI / 4)).epsilon(0.01));
	CHECK(half.z == doctest::Approx(36 * std::cos(PI / 4)).epsilon(0.01));
	// Between two stored frames (cycle 0.125 = frame 0.5): the roll in between.
	skinVertices(*geometry, model->skinning(1, 0.125f), moved);
	const AnimVec between = centre(*geometry, moved, 1);
	CHECK(between.y == doctest::Approx(-36 * std::sin(PI / 16)).epsilon(0.02));
	// The root's own block turns about its centre line only.
	const AnimVec post = centre(*geometry, moved, 0);
	CHECK(post.x == doctest::Approx(0).epsilon(0.01));
}

TEST_CASE("animation: long ones in sections, data in the .ani, raw rotations, include models") {
	std::string error;
	SUBCASE("sections") {
		auto spec = armSpec();
		test::StudioModelSpec::Animation slide{"slide", "", 30, true, {}};
		for (int f = 0; f < 11; ++f) slide.frames.push_back({{float(f) * 2, 0, 0, 0, 0, 0}, {0, 0, 32, 0, 0, 0}});
		slide.sectionFrames = 4;
		spec.animations.push_back(slide);
		const auto model = loadFiles(test::makeStudioModel(spec), &error);
		REQUIRE_MESSAGE(model, error);
		REQUIRE(model->sequences().size() == 1);
		CHECK(model->sequences()[0].frames == 11);
		// Every frame comes from its section, the last one from the extra section.
		for (int f = 0; f < 11; ++f) {
			std::vector<AnimVec> pos;
			std::vector<AnimQuat> quat;
			model->localPose(0, float(f) / 10.f, pos, quat);
			CHECK(pos[0].x == doctest::Approx(float(f) * 2).epsilon(0.01));
		}
	}
	SUBCASE("in the .ani file, read through the game's file system") {
		auto spec = armSpec();
		test::StudioModelSpec::Animation fall{"fall_over", "", 10, false, roll(0, PI / 2, 3)};
		fall.inAni = true;
		spec.animations.push_back(fall);
		const auto files = test::makeStudioModel(spec);
		REQUIRE_FALSE(files.ani.empty());
		const auto dir = std::filesystem::temp_directory_path() / "sme_studio_anim";
		std::filesystem::remove_all(dir);
		test::writeStudioModel(dir / "models/test_anim.mdl", files);
		GameFileSystem fs;
		fs.addDirectory(dir, {"game"});
		const auto model = AnimatedModel::load(fs, "models/test_anim.mdl", &error);
		REQUIRE_MESSAGE(model, error);
		const auto geometry = loadModelGeometry(fs, "models/test_anim.mdl", 0, &error);
		REQUIRE(geometry);
		std::vector<ModelVertex> moved;
		skinVertices(*geometry, model->skinning(0, 1), moved);
		CHECK(centre(*geometry, moved, 1).y == doctest::Approx(-36).epsilon(0.01));
		// Without the .ani the sequence is still listed, the pose stays at rest.
		std::filesystem::remove(dir / "models/test_anim.ani");
		const auto lost = AnimatedModel::load(fs, "models/test_anim.mdl", &error);
		REQUIRE(lost);
		skinVertices(*geometry, lost->skinning(0, 1), moved);
		CHECK(centre(*geometry, moved, 1).z == doctest::Approx(36).epsilon(0.01));
		std::filesystem::remove_all(dir);
	}
	SUBCASE("raw rotation and position (Quaternion48, Vector48)") {
		auto spec = armSpec();
		test::StudioModelSpec::Animation tilted{"tilted", "", 30, true, {{{0, 0, 8, PI / 2, 0, 0}, {0, 0, 32, 0, 0, 0}}}};
		tilted.rawFirstBone = true;
		spec.animations.push_back(tilted);
		const auto model = loadFiles(test::makeStudioModel(spec), &error);
		REQUIRE_MESSAGE(model, error);
		std::vector<AnimVec> pos;
		std::vector<AnimQuat> quat;
		model->localPose(0, 0, pos, quat);
		CHECK(pos[0].z == doctest::Approx(8));
		CHECK(quat[0].x == doctest::Approx(std::sin(PI / 4)).epsilon(0.001));
		CHECK(quat[0].w == doctest::Approx(std::cos(PI / 4)).epsilon(0.001));
	}
	SUBCASE("sequences of an include model, its bones matched by name") {
		auto base = armSpec();
		base.sequences = {"ragdoll"};
		base.include = "models/test_anim_shared.mdl";
		const auto baseFiles = test::makeStudioModel(base);
		// The shared model lists the bones the other way round and holds the animation.
		test::StudioModelSpec shared;
		shared.groups.push_back({"body", {{"none", {}}}});
		shared.bones = {{"hand", -1, {0, 0, 32}, {0, 0, 0}}, {"root", -1, {0, 0, 0}, {0, 0, 0}}};
		shared.animations.push_back({"ragdoll", "", 30, true, {}});   // same name as the base's: the base one wins
		shared.animations.push_back({"wave", "ACT_SIGNAL1", 30, true, {}});
		for (int f = 0; f < 3; ++f) {
			shared.animations.back().frames.push_back({{0, 0, 32, 0, 0, 0}, {0, 0, 0, PI / 2 * float(f) / 2.f, 0, 0}});
		}
		const auto sharedFiles = test::makeStudioModel(shared);
		const auto model = AnimatedModel::fromFiles({{"models/test_anim.mdl", baseFiles.mdl, {}}, {"models/test_anim_shared.mdl", sharedFiles.mdl, {}}}, &error);
		REQUIRE_MESSAGE(model, error);
		REQUIRE(model->sequences().size() == 2);
		CHECK(model->sequences()[0].name == "ragdoll");
		CHECK(model->sequences()[0].model == 0);
		CHECK(model->sequences()[1].name == "wave");
		CHECK(model->sequences()[1].model == 1);
		CHECK(model->sequences()[1].activity == "ACT_SIGNAL1");
		CHECK(model->files() == std::vector<std::string>{"models/test_anim.mdl", "models/test_anim_shared.mdl"});
		const auto geometry = buildModelGeometry({baseFiles.mdl, baseFiles.vvd, baseFiles.vtx}, 0, &error);
		REQUIRE(geometry);
		std::vector<ModelVertex> moved;
		skinVertices(*geometry, model->skinning(1, 1), moved);
		CHECK(centre(*geometry, moved, 1).y == doctest::Approx(-36).epsilon(0.01));
	}
}

TEST_CASE("animation: broken files never crash and keep the rest pose") {
	auto spec = armSpec();
	spec.animations.push_back({"fall_over", "", 10, false, roll(0, PI / 2, 4)});
	spec.animations.back().sectionFrames = 2;
	const auto files = test::makeStudioModel(spec);
	CHECK_FALSE(AnimatedModel::fromFiles({}));
	CHECK_FALSE(AnimatedModel::fromFiles({{"x.mdl", std::vector<std::byte>(16), {}}}));
	// Cut off at every length, and with bytes flipped: whatever loads gives finite poses.
	for (std::size_t length = 0; length < files.mdl.size(); length += 7) {
		std::vector<std::byte> cut(files.mdl.begin(), files.mdl.begin() + static_cast<std::ptrdiff_t>(length));
		if (const auto model = AnimatedModel::fromFiles({{"x.mdl", cut, {}}})) {
			for (std::size_t s = 0; s < model->sequences().size(); ++s) (void)model->skinning(int(s), 0.7f);
		}
	}
	for (std::size_t at = 408; at < files.mdl.size(); at += 13) {
		auto broken = files.mdl;
		broken[at] = std::byte(0x7f);
		broken[at - 1] = std::byte(0xff);
		if (const auto model = AnimatedModel::fromFiles({{"x.mdl", broken, {}}})) {
			for (std::size_t s = 0; s < model->sequences().size(); ++s) {
				for (const auto& m : model->skinning(int(s), 1.f)) {
					for (const auto& row : m.m) {
						for (const float v : row) CHECK(std::isfinite(v));
					}
				}
			}
		}
	}
	// Cycles outside 0..1 and unknown sequences: the ends and the rest pose.
	const auto model = loadFiles(files);
	REQUIRE(model);
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	model->localPose(0, 5.f, pos, quat);
	CHECK(quat[0].x == doctest::Approx(std::sin(PI / 4)).epsilon(0.001));
	model->localPose(0, std::nanf(""), pos, quat);
	CHECK(quat[0].w == doctest::Approx(1));
	model->localPose(42, 0.5f, pos, quat);
	CHECK(quat[0].w == doctest::Approx(1));
}
