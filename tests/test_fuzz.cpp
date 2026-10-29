// test_fuzz.cpp - Every reader of foreign files survives damaged input: valid files with flipped,
// overwritten and missing bytes, cut at many places, and plain noise. "Survives" means no crash
// and no hang (and, in a sanitizer build, no read outside the data); the results do not matter.
// The inputs are made by the test fixture (no game files); the mutations come from a fixed seed,
// so a failure repeats.

#include "fixture.h"

#include <sme/animation_import.h>
#include <sme/asset_import.h>
#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/compile_log.h>
#include <sme/heightmap.h>
#include <sme/keyvalues.h>
#include <sme/map.h>
#include <sme/material.h>
#include <sme/model_import.h>
#include <sme/particles.h>
#include <sme/relief_pattern.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>
#include <sme/texture.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace sme;

namespace {

using Bytes = std::vector<std::byte>;

/// Damaged copies of `valid`: cut at many places, single and multiple flipped bytes, 32-bit
/// fields overwritten with values that break offsets and counts (0, -1, huge, just past the end),
/// and noise of a few sizes. `read` is called on each.
void fuzzBytes(const Bytes& valid, const std::function<void(const Bytes&)>& read, unsigned seed, int rounds = 400) {
	std::mt19937 random(seed);
	read({});
	read(valid);
	// Cuts: every byte for small files, spread out for big ones.
	const std::size_t step = std::max<std::size_t>(1, valid.size() / 300);
	for (std::size_t cut = 0; cut < valid.size(); cut += step) read(Bytes(valid.begin(), valid.begin() + std::ptrdiff_t(cut)));
	if (valid.empty()) return;
	const auto pick = [&](std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(random); };
	const std::uint32_t nasty[] = {0, 0xffffffffu, 0x7fffffffu, 0x80000000u, std::uint32_t(valid.size()), std::uint32_t(valid.size() + 1),
	                               std::uint32_t(valid.size() - 1), 0x10000u, 255};
	for (int round = 0; round < rounds; ++round) {
		Bytes copy = valid;
		const int kind = round % 3;
		if (kind == 0) {
			for (int i = 0, n = 1 + int(pick(8)); i < n; ++i) copy[pick(copy.size())] = std::byte(pick(256));
		} else if (kind == 1 && copy.size() >= 4) {
			// Overwrite an aligned 32-bit field (headers are full of offsets and counts).
			const std::size_t at = pick(copy.size() / 4) * 4;
			const std::uint32_t value = nasty[pick(std::size(nasty))];
			std::memcpy(copy.data() + at, &value, 4);
		} else {
			// A run of bytes zeroed or set to 0xff.
			const std::size_t at = pick(copy.size()), length = std::min<std::size_t>(1 + pick(32), copy.size() - at);
			std::memset(copy.data() + at, round % 2 ? 0xff : 0, length);
		}
		read(copy);
	}
	for (const std::size_t size : {1u, 3u, 16u, 64u, 409u, 4096u}) {
		Bytes noise(size);
		for (auto& b : noise) b = std::byte(pick(256));
		read(noise);
	}
}

/// The same for text formats: cut, characters replaced or doubled, lines dropped.
void fuzzText(const std::string& valid, const std::function<void(const std::string&)>& read, unsigned seed, int rounds = 300) {
	Bytes bytes(valid.size());
	std::memcpy(bytes.data(), valid.data(), valid.size());
	fuzzBytes(bytes, [&](const Bytes& b) { read(std::string(reinterpret_cast<const char*>(b.data()), b.size())); }, seed, rounds);
	std::mt19937 random(seed + 1);
	const char specials[] = {'"', '{', '}', '\n', '\0', '/', '-', '9', 'e', ' ', '\\', ':'};
	for (int round = 0; round < rounds; ++round) {
		std::string copy = valid;
		for (int i = 0; i < 3 && !copy.empty(); ++i) {
			const std::size_t at = std::uniform_int_distribution<std::size_t>(0, copy.size() - 1)(random);
			copy[at] = specials[random() % std::size(specials)];
		}
		read(copy);
	}
}

/// A small model with a skeleton and an animation (the reader's richest input).
test::StudioModelFiles animatedModel() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"arm", {{-4, -4, 0, 4, 4, 8}, {-2, -2, 32, 2, 2, 40}}}}});
	spec.groups.push_back({"handle", {{"none", {}}, {"lever", {{4, -1, 10, 6, 1, 14}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"hand", 0, {0, 0, 32}, {0, 0, 0}}};
	spec.boxBones = {0, 1, 1};
	test::StudioModelSpec::Animation wave;
	wave.name = "wave";
	for (int f = 0; f < 6; ++f) wave.frames.push_back({{0, 0, 0, 0.2f * float(f), 0, 0}, {0, 0, 32, 0, 0.1f * float(f), 0}});
	spec.animations.push_back(wave);
	test::StudioModelSpec::Animation stored = wave;
	stored.name = "stored";
	stored.inAni = true;
	stored.sectionFrames = 2;
	stored.rawFirstBone = true;
	spec.animations.push_back(stored);
	return test::makeStudioModel(spec);
}

/// A .bsp with two cubemap samples and a pakfile directory with built cubemaps.
Bytes cubemapBsp() {
	Bytes out(8 + 64 * 16 + 4);
	const auto put32 = [](Bytes& v, std::size_t at, std::uint32_t value) { std::memcpy(v.data() + at, &value, 4); };
	std::memcpy(out.data(), "VBSP", 4);
	put32(out, 4, 20);
	const std::size_t samplesAt = out.size();
	out.resize(out.size() + 32);
	put32(out, 8 + 42 * 16, std::uint32_t(samplesAt));
	put32(out, 8 + 42 * 16 + 4, 32);
	Bytes zip(10);
	const auto add32 = [&zip](std::uint32_t v) { for (int i = 0; i < 4; ++i) zip.push_back(std::byte((v >> (8 * i)) & 0xff)); };
	const auto add16 = [&zip](std::uint16_t v) { zip.push_back(std::byte(v & 0xff)); zip.push_back(std::byte(v >> 8)); };
	const std::size_t central = zip.size();
	const std::vector<std::string> names{"materials/maps/test/c1_2_3.vtf", "materials/maps/test/c1_2_3.hdr.vtf"};
	for (const auto& name : names) {
		add32(0x02014b50u);
		for (int i = 0; i < 6; ++i) add16(0);
		add32(0); add32(0); add32(0);
		add16(std::uint16_t(name.size()));
		for (int i = 0; i < 4; ++i) add16(0);
		add32(0); add32(0);
		for (const char c : name) zip.push_back(std::byte(c));
	}
	const std::size_t centralSize = zip.size() - central;
	add32(0x06054b50u); add16(0); add16(0); add16(std::uint16_t(names.size())); add16(std::uint16_t(names.size()));
	add32(std::uint32_t(centralSize)); add32(std::uint32_t(central)); add16(0);
	const std::size_t pak = out.size();
	out.insert(out.end(), zip.begin(), zip.end());
	put32(out, 8 + 40 * 16, std::uint32_t(pak));
	put32(out, 8 + 40 * 16 + 4, std::uint32_t(zip.size()));
	return out;
}

ImageRgba testImage(std::uint16_t w, std::uint16_t h, bool alpha) {
	ImageRgba image;
	image.width = w;
	image.height = h;
	image.pixels.resize(std::size_t(w) * h * 4);
	for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = std::byte((i * 37 + i / 7) & 0xff);
	if (!alpha) {
		for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = std::byte(255);
	}
	return image;
}

Bytes wavFile() {
	Bytes out;
	const auto add = [&out](const void* p, std::size_t n) {
		const auto* b = static_cast<const std::byte*>(p);
		out.insert(out.end(), b, b + n);
	};
	const auto add32 = [&](std::uint32_t v) { add(&v, 4); };
	const auto add16 = [&](std::uint16_t v) { add(&v, 2); };
	add("RIFF", 4); add32(36 + 400); add("WAVE", 4);
	add("fmt ", 4); add32(16); add16(1); add16(2); add32(22050); add32(22050 * 4); add16(4); add16(16);
	add("data", 4); add32(400);
	for (int i = 0; i < 200; ++i) add16(std::uint16_t(i * 300));
	return out;
}

} // namespace

TEST_CASE("fuzz: model files (.mdl, .vvd, .vtx, .ani) - geometry, info, skeleton and animation") {
	const auto files = animatedModel();
	const auto start = std::chrono::steady_clock::now();
	fuzzBytes(files.mdl, [&](const Bytes& mdl) {
		(void)readModelInfo(mdl);
		(void)buildModelGeometry({mdl, files.vvd, files.vtx}, 0, nullptr, {}, 3);
		(void)readAttachments(mdl);
		if (auto model = AnimatedModel::fromFiles({{"models/test_anim.mdl", mdl, files.ani}})) {
			for (int s = 0; s < int(model->sequences().size()); ++s) {
				(void)model->boneToModel(s, 0.37f);
				(void)model->skinning(s, 0.99f);
			}
		}
	}, 1);
	fuzzBytes(files.vvd, [&](const Bytes& vvd) { (void)buildModelGeometry({files.mdl, vvd, files.vtx}, 1, nullptr, {}, 1); }, 2);
	fuzzBytes(files.vtx, [&](const Bytes& vtx) { (void)buildModelGeometry({files.mdl, files.vvd, vtx}, 0, nullptr, {}, 2); }, 3);
	fuzzBytes(files.ani, [&](const Bytes& ani) {
		if (auto model = AnimatedModel::fromFiles({{"models/test_anim.mdl", files.mdl, ani}})) {
			for (int s = 0; s < int(model->sequences().size()); ++s) (void)model->boneToModel(s, 0.5f);
		}
	}, 4);
	// Generous: a few thousand reads take well under a second; minutes would mean a hang-like loop.
	CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(60));
}

TEST_CASE("fuzz: textures (.vtf), pictures and height maps") {
	const auto colour = encodeVtf(testImage(64, 32, true), true, false, 64);
	const auto plain = encodeVtf(testImage(16, 16, false), false, true, 16);
	REQUIRE(colour);
	REQUIRE(plain);
	HeightMap heights;
	heights.width = 16;
	heights.height = 8;
	heights.values.assign(128, 0.5f);
	for (const auto& vtf : {*colour, *plain, encodeHeightVtf(heights)}) {
		fuzzBytes(vtf, [](const Bytes& data) {
			(void)readTextureInfo(data);
			(void)decodeTexture(data, 32);
		}, 5, 300);
	}
	fuzzBytes(encodePng(testImage(12, 7, true)), [](const Bytes& data) { (void)decodeImageFile(data); }, 6, 300);
	// Odd pictures for the relief analysis: 1 x N, N x 1, flat, tiny.
	for (const auto& [w, h] : std::vector<std::pair<int, int>>{{1, 1}, {1, 64}, {64, 1}, {2, 3}, {3, 512}}) {
		const auto image = testImage(std::uint16_t(w), std::uint16_t(h), false);
		ReliefLayout layout;
		(void)heightFromTexture(image, &layout);
		(void)reliefFitByLook(image);
	}
}

TEST_CASE("fuzz: sounds, particles and compiled maps") {
	const auto wav = wavFile();
	REQUIRE(decodeAudio(wav, "x.wav"));
	for (const char* name : {"x.wav", "x.mp3", "x.ogg", "x.flac"}) {
		fuzzBytes(wav, [name](const Bytes& data) {
			(void)decodeAudio(data, name);
			(void)soundFileLoops(data);
		}, 7, 200);
	}
	// sourcepp's DMX reader reserves as many entries as the file claims: a broken count asks for
	// gigabytes, which throws std::bad_alloc (caught by the readers) - but AddressSanitizer aborts
	// on such a request instead of throwing, so a sanitizer build leaves the particle files out.
#ifndef __SANITIZE_ADDRESS__
	for (const auto& pcf : {test::makeDemoFirePcf(), test::makeDemoSparksPcf()}) {
		fuzzBytes(pcf, [](const Bytes& data) {
			(void)readParticleDefinitions(data);
			(void)particleSystemNames(data);
		}, 8);
	}
#endif
	const auto bsp = cubemapBsp();
	REQUIRE(readBspCubemaps(bsp, "test"));
	fuzzBytes(bsp, [](const Bytes& data) { (void)readBspCubemaps(data, "test"); }, 9);
}

TEST_CASE("fuzz: text formats - .smd, .obj, .vmt, .vmf, KeyValues") {
	const std::string smd =
		"version 1\nnodes\n0 \"root\" -1\n1 \"hand\" 0\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\n1 0 0 32 0 0 0\ntime 1\n0 0 0 0 0.1 0 0\n"
		"1 0 0 32 0 0.2 0\nend\ntriangles\ncrate\n0 0 0 0 0 0 1 0 0\n0 8 0 0 0 0 1 1 0\n0 8 8 0 0 0 1 1 1\nend\n";
	fuzzText(smd, [](const std::string& text) {
		(void)parseSmd(text);
		(void)readSmd(text);
	}, 10);
	const std::string obj = "mtllib crate.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 1\nvn 0 0 1\nusemtl wood\n"
	                        "f 1/1/1 2/2/1 3/2/1 4/1/1\nf -1 -2 -3\n";
	const std::string mtl = "newmtl wood\nKd 0.5 0.4 0.3\nmap_Kd wood.png\n";
	fuzzText(obj, [&](const std::string& text) {
		if (auto mesh = readObj(text, mtl)) {
			ModelImportOptions options;
			const auto placed = placedMesh(*mesh, options);
			(void)collisionMesh(placed, ModelImportOptions::Collision::Hull);
			(void)meshVolume(placed);
		}
	}, 11);
	fuzzText(mtl, [&](const std::string& text) { (void)readObj(obj, text); }, 12, 100);
	const std::string vmt = "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/wall\"\n\t\"$envmap\" \"env_cubemap\" [$WIN32]\n"
	                        "\t\"Proxies\"\n\t{\n\t\t\"AnimatedTexture\"\n\t\t{\n\t\t\t\"animatedtexturevar\" \"$basetexture\"\n\t\t}\n\t}\n}\n";
	fuzzText(vmt, [](const std::string& text) {
		(void)parseMaterial(text, "brick/wall");
		try {
			(void)parseKeyValues(text);
		} catch (const std::exception&) {
			// A broken file is reported as an exception; that is fine.
		}
	}, 13);
	const std::string vmf = "versioninfo\n{\n\t\"editorversion\" \"400\"\n}\nworld\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n\tsolid\n\t{\n"
	                        "\t\t\"id\" \"2\"\n\t\tside\n\t\t{\n\t\t\t\"id\" \"1\"\n\t\t\t\"plane\" \"(0 0 0) (0 64 0) (64 64 0)\"\n"
	                        "\t\t\t\"material\" \"BRICK/WALL\"\n\t\t\t\"uaxis\" \"[1 0 0 0] 0.25\"\n\t\t\t\"vaxis\" \"[0 -1 0 0] 0.25\"\n\t\t}\n\t}\n}\n"
	                        "entity\n{\n\t\"id\" \"3\"\n\t\"classname\" \"light\"\n\t\"origin\" \"0 0 64\"\n\tconnections\n\t{\n"
	                        "\t\t\"OnTrigger\" \"lamp,TurnOn,,0,-1\"\n\t}\n}\n";
	fuzzText(vmf, [](const std::string& text) {
		try {
			(void)MapDocument::parse(text);
		} catch (const std::exception&) {
		}
	}, 14);
	fuzzText("models/a.mdl#s=1;b=2;g1=3", [](const std::string& text) { (void)parseModelVariant(text); }, 15, 100);
}
