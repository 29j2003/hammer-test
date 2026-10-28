// test_samples.cpp - Mapsmith's sample files: each one goes cleanly through the importer it is
// for (sounds decode, pictures become VTFs, the models become studiomdl sources, the animations
// fit their skeletons, the particle file reads back), plus the FLAC and particle-file writers.

#include <doctest/doctest.h>

#include <sme/animation_import.h>
#include <sme/asset_import.h>
#include <sme/model_import.h>
#include <sme/particles.h>
#include <sme/samples.h>
#include <sme/sound_edit.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

using namespace sme;
namespace fs = std::filesystem;

namespace {

const SampleFile* findSample(const std::vector<SampleFile>& samples, std::string_view path) {
	for (const auto& s : samples) {
		if (s.path == path) return &s;
	}
	return nullptr;
}

std::string textOf(const SampleFile& file) {
	return {reinterpret_cast<const char*>(file.data.data()), file.data.size()};
}

std::vector<std::byte> readFile(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	const std::string text{std::istreambuf_iterator<char>(in), {}};
	std::vector<std::byte> out(text.size());
	std::memcpy(out.data(), text.data(), text.size());
	return out;
}

StudioBone bone(std::string name, int parent, AnimVec pos, AnimVec rot = {}) {
	StudioBone b;
	b.name = std::move(name);
	b.parent = parent;
	b.pos = pos;
	b.rot = rot;
	b.quat = angleQuaternion(rot);
	return b;
}

} // namespace

TEST_CASE("samples: every file is there, the README names each one") {
	const auto samples = makeSamples();
	const std::set<std::string> expected = {"sounds/hum_loop.wav",
	                                        "sounds/door_chime.flac",
	                                        "pictures/terracotta_tiles.png",
	                                        "pictures/terracotta_tiles_normal.png",
	                                        "pictures/rusty_grate.png",
	                                        "models/crate/crate.obj",
	                                        "models/crate/crate.mtl",
	                                        "models/crate/crate.png",
	                                        "models/hanging_sign/hanging_sign.smd",
	                                        "models/hanging_sign/hanging_sign.png",
	                                        "animations/hanging_sign_swing.smd",
	                                        "animations/hanging_sign_spin.smd",
	                                        "animations/hl2_human_nod.smd",
	                                        "animations/hl2_human_wave.smd",
	                                        "particles/mapsmith_samples.pcf",
	                                        "README.txt"};
	std::set<std::string> paths;
	for (const auto& s : samples) {
		paths.insert(s.path);
		CHECK_MESSAGE(!s.data.empty(), s.path);
	}
	CHECK(paths == expected);
	const std::string readme = samplesReadme();
	for (const auto& path : expected) {
		if (path == "README.txt") continue;
		CHECK_MESSAGE(readme.find(fs::path(path).filename().string()) != std::string::npos, path);
	}
	CHECK(readme.find("click.mp3") != std::string::npos);
	CHECK(readme.find("wind_loop.ogg") != std::string::npos);
	// The same bytes every time (no clock, no random seed from outside).
	const auto again = makeSamples();
	REQUIRE(again.size() == samples.size());
	for (std::size_t i = 0; i < samples.size(); ++i) CHECK(again[i].data == samples[i].data);
}

TEST_CASE("samples: the sounds decode as the sound import reads them") {
	const auto samples = makeSamples();
	const auto* hum = findSample(samples, "sounds/hum_loop.wav");
	REQUIRE(hum);
	std::string error;
	const auto loop = decodeAudio(hum->data, "hum_loop.wav", &error);
	REQUIRE_MESSAGE(loop, error);
	CHECK(loop->sampleRate == 22050);
	CHECK(loop->channels == 1);
	CHECK(loop->seconds() == doctest::Approx(4.0));
	CHECK(soundFileLoops(hum->data) == true);   // cue point: Source loops it
	// Seamless: the end runs into the start without a click.
	CHECK(std::abs(int(loop->samples.front()) - int(loop->samples.back())) < 3000);
	CHECK(peakLevel(*loop) > 0.3);

	const auto* chime = findSample(samples, "sounds/door_chime.flac");
	REQUIRE(chime);
	const auto flac = decodeAudio(chime->data, "door_chime.flac", &error);
	REQUIRE_MESSAGE(flac, error);
	CHECK(flac->format.starts_with("FLAC"));
	CHECK(flac->channels == 2);
	CHECK(flac->samples == sampleChime().samples);   // lossless

	// The pre-made MP3 and Ogg (encoded once from Mapsmith's own WAVs) decode too.
	for (const char* name : {"click.mp3", "wind_loop.ogg"}) {
		const auto data = readFile(fs::path(SME_SAMPLE_MEDIA_DIR) / name);
		REQUIRE_MESSAGE(!data.empty(), name);
		const auto audio = decodeAudio(data, name, &error);
		REQUIRE_MESSAGE(audio, std::string(name) + ": " + error);
		CHECK(audio->seconds() > 0.2);
		CHECK(peakLevel(*audio) > 0.1);
	}
	// And they go through the import's preparation like any sound.
	SoundImportOptions options;
	options.loop = true;
	const auto prepared = prepareImportedSound(*loop, options);
	CHECK(prepared.loopStart.has_value());
}

TEST_CASE("FLAC writer: several frames, stereo and mono, read back exactly") {
	for (const int channels : {1, 2}) {
		DecodedAudio audio;
		audio.sampleRate = 44100;
		audio.channels = channels;
		for (int i = 0; i < 9000 * channels; ++i) audio.samples.push_back(std::int16_t((i * 7919) % 65536 - 32768));   // full range, odd values
		const auto flac = encodeFlac(audio);
		std::string error;
		const auto back = decodeAudio(flac, "x.flac", &error);
		REQUIRE_MESSAGE(back, error);
		CHECK(back->channels == channels);
		CHECK(back->sampleRate == 44100);
		CHECK(back->samples == audio.samples);
	}
	// Empty sound: a valid file without frames.
	DecodedAudio silent;
	silent.sampleRate = 22050;
	silent.channels = 1;
	const auto empty = encodeFlac(silent);
	CHECK(empty.size() == 4 + 4 + 34);
}

TEST_CASE("samples: the pictures become materials") {
	const auto samples = makeSamples();
	std::string error;
	const auto tiles = decodeImageFile(findSample(samples, "pictures/terracotta_tiles.png")->data, &error);
	REQUIRE_MESSAGE(tiles, error);
	CHECK(tiles->width == 512);
	CHECK(imageAlpha(*tiles) == ImageAlpha::Opaque);
	CHECK(encodeVtf(*tiles, true, false, 1024).has_value());
	// Seamless: the first and the last column are both grout.
	const auto pixel = [&](int x, int y) { return std::to_integer<int>(tiles->pixels[(std::size_t(y) * tiles->width + std::size_t(x)) * 4]); };
	for (int y = 10; y < 500; y += 37) CHECK(std::abs(pixel(0, y) - pixel(511, y)) < 40);
	const auto normal = decodeImageFile(findSample(samples, "pictures/terracotta_tiles_normal.png")->data, &error);
	REQUIRE(normal);
	CHECK(encodeVtf(*normal, false, true, 1024).has_value());
	const auto grate = decodeImageFile(findSample(samples, "pictures/rusty_grate.png")->data, &error);
	REQUIRE(grate);
	CHECK(imageAlpha(*grate) == ImageAlpha::Binary);   // holes: $alphatest
	MaterialImportOptions options;
	options.name = "rusty_grate";
	CHECK(effectiveAlpha(options, *grate) == MaterialImportOptions::Alpha::AlphaTest);
}

TEST_CASE("samples: the crate and the rigged sign become studiomdl sources") {
	const auto samples = makeSamples();
	std::string error;
	const auto crate = readObj(sampleCrateObj(), sampleCrateMtl(), &error);
	REQUIRE_MESSAGE(crate, error);
	CHECK(crate->triangles.size() == 12);
	CHECK(crate->textures.at("crate_wood") == "crate.png");
	CHECK(objMaterialLibrary(sampleCrateObj()) == "crate.mtl");
	ModelImportOptions options;
	options.name = "crate";
	options.scale = 39.37;   // metres
	options.yUp = true;
	const auto placed = placedMesh(*crate, options);
	const Bounds box = placed.bounds();
	CHECK(box.max.z - box.min.z == doctest::Approx(0.8 * 39.37).epsilon(0.01));   // stands up (Y up turned to Z up)
	CHECK(box.min.z == doctest::Approx(0));
	CHECK(meshVolume(placed) > 0);   // closed and facing out

	const auto sign = readSmd(sampleSignSmd(), &error);
	REQUIRE_MESSAGE(sign, error);
	REQUIRE(sign->bones.size() == 2);
	CHECK(sign->bones[1].name == "board");
	CHECK(sign->bones[1].parent == 0);
	CHECK(sign->triangles.size() == 36);
	int onBoard = 0;
	for (const auto& t : sign->triangles) onBoard += t.corners[0].bone == 1 ? 1 : 0;
	CHECK(onBoard == 24);   // the hinge pin and the board
	options = {};
	options.name = "hanging_sign";
	options.kind = ModelImportOptions::Kind::Dynamic;
	options.yUp = true;   // ignored: a rigged .smd keeps Source's orientation
	const auto signPlaced = placedMesh(*sign, options);
	CHECK(signPlaced.bones[1].position.z == doctest::Approx(sign->bones[1].position.z));
	CHECK(signPlaced.bounds().min.z == doctest::Approx(0));
	const std::string smd = writeSmd(signPlaced);
	CHECK(smd.find("1 \"board\" 0") != std::string::npos);
	CHECK(smd.find(" 1 1 1\n") != std::string::npos);   // a weight link to the board
	const auto again = readSmd(smd, &error);
	REQUIRE(again);
	CHECK(again->bones.size() == 2);
	const auto collision = collisionMesh(signPlaced, ModelImportOptions::Collision::Hull);
	CHECK(collision.bones.size() == 2);   // studiomdl needs the collision on the model's bones
	CHECK(writeSmd(collision).find("\"bracket\"") != std::string::npos);

	// Through the importer: the sources are written, the material picture is named like it.
	const auto mod = fs::temp_directory_path() / "sme_samples_mod";
	fs::remove_all(mod);
	fs::create_directories(mod);
	const auto plan = prepareModelImport(mod, *sign, options, false, &error);
	REQUIRE_MESSAGE(plan, error);
	CHECK(modelMaterialNames(*sign) == std::vector<std::string>{"hanging_sign"});
	CHECK(findSample(samples, "models/hanging_sign/hanging_sign.png"));
	fs::remove_all(mod);
}

TEST_CASE("samples: the animations fit their skeletons; partial, relative files are completed") {
	std::string error;
	// The sign's animations on the sign's skeleton.
	const auto signMesh = readSmd(sampleSignSmd());
	REQUIRE(signMesh);
	std::vector<StudioBone> signBones;
	for (const auto& b : signMesh->bones) signBones.push_back(bone(b.name, b.parent, {float(b.position.x), float(b.position.y), float(b.position.z)}));
	for (const auto& text : {sampleSignSwingSmd(), sampleSignSpinSmd()}) {
		const auto smd = parseSmd(text, &error);
		REQUIRE_MESSAGE(smd, error);
		CHECK_FALSE(smd->relative);
		const auto match = matchSkeleton(*smd, signBones);
		CHECK(match.matched() == 2);
		CHECK(match.notInSmd.empty());
		CHECK_FALSE(animationNeedsCompleting(*smd, signBones));
	}

	// HL2 people: a stand-in ValveBiped skeleton (names as the game's, values made up).
	const std::vector<StudioBone> biped = {
		bone("ValveBiped.Bip01_Pelvis", -1, {0, 0, 38}, {0, 0, 1.5f}),
		bone("ValveBiped.Bip01_Spine", 0, {0, 0, 4}),
		bone("ValveBiped.Bip01_Neck1", 1, {0, 0, 18}, {0, 0.1f, 0}),
		bone("ValveBiped.Bip01_Head1", 2, {0, 0, 3}, {0.05f, 0, 0}),
		bone("ValveBiped.Bip01_R_UpperArm", 1, {0, -6, 15}),
		bone("ValveBiped.Bip01_R_Forearm", 4, {11, 0, 0}),
		bone("ValveBiped.Bip01_R_Hand", 5, {11, 0, 0}),
		bone("ValveBiped.Bip01_L_UpperArm", 1, {0, 6, 15}, {0.2f, 0.3f, 0.4f}),
	};
	for (const auto& text : {sampleHumanNodSmd(), sampleHumanWaveSmd()}) {
		const auto smd = parseSmd(text, &error);
		REQUIRE_MESSAGE(smd, error);
		CHECK(smd->relative);
		CHECK(matchSkeleton(*smd, biped).matched() >= 2);
		REQUIRE(animationNeedsCompleting(*smd, biped));
		const std::string completed = completedAnimationSmd(*smd, biped);
		const auto whole = parseSmd(completed, &error);
		REQUIRE_MESSAGE(whole, error);
		CHECK_FALSE(whole->relative);
		CHECK(whole->nodes.size() == biped.size());
		CHECK(whole->frames.size() == smd->frames.size());
		CHECK(matchSkeleton(*whole, biped).notInSmd.empty());
		CHECK_FALSE(animationNeedsCompleting(*whole, biped));
		// A bone the file does not move stays at its rest pose in every frame.
		for (const auto& frame : whole->frames) {
			const auto& [pos, rot] = frame[7];   // L_UpperArm
			CHECK(pos.y == doctest::Approx(6));
			CHECK(rot.x == doctest::Approx(0.2).epsilon(0.001));
			CHECK(rot.y == doctest::Approx(0.3).epsilon(0.001));
			CHECK(rot.z == doctest::Approx(0.4).epsilon(0.001));
		}
		// The root keeps its rest pose too, written back in the .smd's space (turned back 90 degrees
		// about Z, as studiomdl turns it again).
		const auto& [rootPos, rootRot] = whole->frames.front()[0];
		CHECK(rootPos.z == doctest::Approx(38));
		CHECK(rootRot.z == doctest::Approx(1.5 - 3.14159265 / 2).epsilon(0.001));
	}
	// The nod moves the head away from its rest pose somewhere in the loop.
	const auto nod = parseSmd(completedAnimationSmd(*parseSmd(sampleHumanNodSmd()), biped));
	REQUIRE(nod);
	double moved = 0;
	for (const auto& frame : nod->frames) moved = std::max(moved, double(std::abs(frame[3].second.z)));
	CHECK(moved > 0.2);
	// An absolute file that lacks bones is completed as well (with its own values kept).
	const std::string partial = "version 1\nnodes\n0 \"ValveBiped.Bip01_Head1\" -1\nend\nskeleton\ntime 0\n0 1 2 3 0.1 0.2 0.3\nend\n";
	const auto head = parseSmd(partial);
	REQUIRE(head);
	CHECK(animationNeedsCompleting(*head, biped));
	const auto completedHead = parseSmd(completedAnimationSmd(*head, biped));
	REQUIRE(completedHead);
	CHECK(completedHead->frames[0][3].first.x == doctest::Approx(1));
	CHECK(completedHead->frames[0][3].second.z == doctest::Approx(0.3));
}

TEST_CASE("particle files: written in the game's binary DMX, read back by Mapsmith and simulated") {
	const auto pcf = sampleParticleFile();
	const std::string header = "<!-- dmx encoding binary 2 format pcf 1 -->\n";
	REQUIRE(pcf.size() > header.size());
	CHECK(std::string(reinterpret_cast<const char*>(pcf.data()), header.size()) == header);
	CHECK(pcf[header.size()] == std::byte(0));
	const auto systems = readParticleDefinitions(pcf);
	REQUIRE(systems.size() == 2);
	CHECK(systems[0].name == "mapsmith_sample_sparks");
	CHECK(systems[1].name == "mapsmith_sample_smoke");
	CHECK(systems[0].material() == "effects/spark.vmt");
	CHECK(systems[0].properties.number("max_particles", 0) == 200);
	REQUIRE(systems[0].emitters.size() == 1);
	CHECK(systems[0].emitters[0].name == "emit_continuously");
	CHECK(systems[0].emitters[0].number("emission_rate", 0) == doctest::Approx(40));
	REQUIRE(systems[0].initializers.size() == 4);
	CHECK(systems[0].initializers[0].vector("speed_in_local_coordinate_system_max", {}).z == doctest::Approx(160));
	const auto* color = systems[0].initializers[2].find("color1");
	REQUIRE(color);
	CHECK(color->numbers == std::vector<double>{255, 230, 160, 255});
	CHECK(systems[1].operators.size() == 5);
	CHECK(systems[1].renderers[0].name == "render_animated_sprites");
	// The preview runs them.
	ParticleSimulation sparks(systems, "mapsmith_sample_sparks");
	REQUIRE(sparks.valid());
	sparks.step(1.0);
	CHECK(sparks.particleCount() > 10);
	ParticleSimulation smoke(systems, "mapsmith_sample_smoke");
	smoke.step(2.0);
	CHECK(smoke.particleCount() > 3);
	// Every value type comes back.
	PcfSystem all;
	all.name = "types";
	all.properties = {{"i", 7}, {"f", 1.5f}, {"b", true}, {"s", std::string("text")}, {"c", PcfColor{1, 2, 3, 4}}, {"v", Vec3{1, 2, 3}}};
	const auto back = readParticleDefinitions(writeParticleFile({all}));
	REQUIRE(back.size() == 1);
	CHECK(back[0].properties.number("i", 0) == 7);
	CHECK(back[0].properties.number("f", 0) == doctest::Approx(1.5));
	CHECK(back[0].properties.number("b", 0) == 1);
	CHECK(back[0].properties.find("s")->text == "text");
	CHECK(back[0].properties.find("c")->numbers == std::vector<double>{1, 2, 3, 4});
	CHECK(back[0].properties.vector("v", {}).y == doctest::Approx(2));
}

TEST_CASE("samples: written to a folder with the pre-made media") {
	const auto dir = fs::temp_directory_path() / "sme_samples_out";
	fs::remove_all(dir);
	std::string error;
	REQUIRE_MESSAGE(writeSamples(dir, SME_SAMPLE_MEDIA_DIR, &error), error);
	for (const auto& s : makeSamples()) CHECK_MESSAGE(fs::file_size(dir / fs::path(s.path)) == s.data.size(), s.path);
	CHECK(fs::exists(dir / "sounds" / "click.mp3"));
	CHECK(fs::exists(dir / "sounds" / "wind_loop.ogg"));
	// Particle import accepts the sample (it refuses files without systems).
	const auto pcf = readFile(dir / "particles" / "mapsmith_samples.pcf");
	CHECK(readParticleDefinitions(pcf).size() == 2);
	fs::remove_all(dir);
	// Media sources: the WAVs the MP3 and Ogg are made from.
	const auto sources = sampleMediaSources();
	REQUIRE(sources.size() == 2);
	CHECK(decodeAudio(sources[0].data, sources[0].path).has_value());
}
