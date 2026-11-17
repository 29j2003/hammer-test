// make_seeds.cpp - sme-fuzz-seeds: writes the starting inputs (corpora) for the libFuzzer targets in
// tests/fuzz. Everything comes from the test fixture and the demo (a fake Steam installation, no
// Valve files) plus a few made here (a picture, a compiled map, a ZIP) and the sample sounds.
//
//   sme-fuzz-seeds <out dir> [--media <tests/samples_media>]
//
// writes <out>/<target>/<n> for every target (vtf, vpk, studio, phy, pcf, audio, image, bsp, vmf,
// text, fgd, zip). Text seeds carry the reader they are for in their first byte (see fuzz_text.cpp).

#include "fixture.h"
#include "fuzz_common.h"

#include <sme/asset_import.h>
#include <sme/shipping.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using namespace sme;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

fuzz::Bytes fileBytes(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	fuzz::Bytes out(text.size());
	std::memcpy(out.data(), text.data(), text.size());
	return out;
}

fuzz::Bytes withSelector(std::uint8_t selector, const fuzz::Bytes& data) {
	fuzz::Bytes out{std::byte(selector)};
	out.insert(out.end(), data.begin(), data.end());
	return out;
}

fuzz::Bytes textBytes(std::string_view text) {
	fuzz::Bytes out(text.size());
	std::memcpy(out.data(), text.data(), text.size());
	return out;
}

class Corpus {
public:
	explicit Corpus(fs::path root) : m_root(std::move(root)) {}
	void add(const std::string& target, const fuzz::Bytes& data) {
		if (data.size() > 1024 * 1024) return;   // libFuzzer's default -max_len is far below; big seeds only slow it
		const auto dir = m_root / target;
		fs::create_directories(dir);
		const auto path = dir / std::to_string(m_count[target]++);
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
	}
	void report() const {
		for (const auto& [target, count] : m_count) std::cout << target << ": " << count << "\n";
	}

private:
	fs::path m_root;
	std::map<std::string, int> m_count;
};

/// Text reader (fuzz_text.cpp's first byte) for a file of the fixture, or -1.
int textSelector(const fs::path& path) {
	const auto name = toLower(path.filename().string());
	const auto ext = toLower(path.extension().string());
	if (ext == ".vmt") return 1;
	if (name == "gameinfo.txt") return 8;
	if (name.find("gameconfig") != std::string::npos || name.find("hammerplusplus") != std::string::npos) return 2;
	if (name.find("soundscape") != std::string::npos) return 3;
	if (name.find("closecaption") != std::string::npos || name.find("_english") != std::string::npos) return 4;
	if (name == "gamemenu.res") return 6;
	if (name == "chapterbackgrounds.txt") return 7;
	if (ext == ".vcd") return 9;
	if (name == "mapsmith_materials.txt") return 10;
	if (ext == ".cfg") return 11;
	if (ext == ".smd") return 15;
	if (ext == ".obj") return 16;
	if (ext == ".txt" || ext == ".res" || ext == ".rad") return 0;
	return -1;
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: sme-fuzz-seeds <out dir> [--media <dir>]\n";
		return 2;
	}
	Corpus corpus(argv[1]);
	fs::path media;
	for (int i = 2; i + 1 < argc; ++i) {
		if (std::string(argv[i]) == "--media") media = argv[i + 1];
	}

	test::Fixture fixture;
	test::createFixture(fixture);
	test::createDemo(fixture);
	std::map<std::string, fuzz::Bytes> models;   // "<path without extension>" -> joined mdl/vvd/vtx/ani
	std::map<std::string, std::vector<fuzz::Bytes>> modelParts;
	for (const auto& entry : fs::recursive_directory_iterator(fixture.root)) {
		if (!entry.is_regular_file()) continue;
		const auto& path = entry.path();
		const auto ext = toLower(path.extension().string());
		const auto name = toLower(path.filename().string());
		const auto data = fileBytes(path);
		if (ext == ".vtf") corpus.add("vtf", data);
		else if (ext == ".vpk" && name.ends_with("_dir.vpk")) corpus.add("vpk", data);
		else if (ext == ".fgd") corpus.add("fgd", data);
		else if (ext == ".vmf") corpus.add("vmf", data);
		else if (ext == ".phy") corpus.add("phy", data);
		else if (ext == ".pcf") corpus.add("pcf", data);
		else if (ext == ".wav") corpus.add("audio", withSelector(0, data));
		else if (ext == ".mdl" || ext == ".vvd" || ext == ".vtx" || ext == ".ani") {
			auto stem = (path.parent_path() / path.stem()).string();
			if (stem.ends_with(".dx90")) stem.resize(stem.size() - 5);
			auto& parts = modelParts[stem];
			parts.resize(4);
			const int at = ext == ".mdl" ? 0 : ext == ".vvd" ? 1 : ext == ".vtx" ? 2 : 3;
			if (parts[std::size_t(at)].empty() || name.find(".dx90.") != std::string::npos) parts[std::size_t(at)] = data;
		} else if (const int selector = textSelector(path); selector >= 0) {
			corpus.add("text", withSelector(std::uint8_t(selector), data));
		}
	}
	for (const auto& [stem, parts] : modelParts) {
		if (!parts[0].empty()) corpus.add("studio", fuzz::joinParts(parts));
	}
	// Models made directly: a skeleton with animations, one stored in the .ani.
	{
		test::StudioModelSpec spec;
		spec.groups.push_back({"body", {{"arm", {{-4, -4, 0, 4, 4, 8}, {-2, -2, 32, 2, 2, 40}}}}});
		spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"hand", 0, {0, 0, 32}, {0, 0, 0}}};
		spec.boxBones = {0, 1};
		test::StudioModelSpec::Animation wave;
		wave.name = "wave";
		for (int f = 0; f < 4; ++f) wave.frames.push_back({{0, 0, 0, 0.2f * float(f), 0, 0}, {0, 0, 32, 0, 0.1f * float(f), 0}});
		spec.animations.push_back(wave);
		auto stored = wave;
		stored.name = "stored";
		stored.inAni = true;
		stored.sectionFrames = 2;
		spec.animations.push_back(stored);
		auto files = test::makeStudioModel(spec);
		test::addHitbox(files.mdl, 1, {-1, -1, -1, 1, 1, 1});
		corpus.add("studio", fuzz::joinParts({files.mdl, files.vvd, files.vtx, files.ani}));
	}
	corpus.add("phy", test::makePhy({{{0, 0, 0}, 8.f}, {{1, 2, 3}, 4.f}}, "solid {\n\"index\" \"1\"\n\"name\" \"root\"\n}\n"));
	corpus.add("pcf", test::makeDemoFirePcf());
	corpus.add("pcf", test::makeDemoSparksPcf());

	// Pictures and textures made here.
	ImageRgba image;
	image.width = 16;
	image.height = 8;
	image.pixels.resize(16 * 8 * 4);
	for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = std::byte((i * 37 + i / 7) & 0xff);
	corpus.add("image", encodePng(image));
	if (const auto vtf = encodeVtf(image, true, false, 16)) corpus.add("vtf", *vtf);
	if (const auto vtf = encodeVtf(image, false, true, 16)) corpus.add("vtf", *vtf);

	// Sounds: the sample media (MP3, OGG) with their reader selected.
	if (!media.empty()) {
		for (const auto& entry : fs::directory_iterator(media)) {
			const auto ext = toLower(entry.path().extension().string());
			if (ext == ".mp3") corpus.add("audio", withSelector(1, fileBytes(entry.path())));
			if (ext == ".ogg") corpus.add("audio", withSelector(2, fileBytes(entry.path())));
		}
	}

	// A compiled map with a cubemap lump and a pakfile (header + lumps only).
	{
		fuzz::Bytes bsp(8 + 64 * 16 + 32);
		const auto put32 = [&bsp](std::size_t at, std::uint32_t v) { std::memcpy(bsp.data() + at, &v, 4); };
		std::memcpy(bsp.data(), "VBSP", 4);
		put32(4, 20);
		put32(8 + 42 * 16, 8 + 64 * 16);
		put32(8 + 42 * 16 + 4, 32);
		corpus.add("bsp", bsp);
	}

	// ZIPs as Mapsmith writes them (an editor report).
	{
		fuzz::ScratchFile zip("seed.zip");
		std::vector<PackageEntry> entries{{"scene.vmf", {}, textBytes("world\n{\n\t\"id\" \"1\"\n}\n")},
		                                  {"replay.txt", {}, textBytes("tool=select\n")},
		                                  {"picks.log", {}, textBytes("pick 1 2 3\n")}};
		if (writeZip(zip.path(), entries)) corpus.add("zip", fileBytes(zip.path()));
	}

	// Text formats that are not in the fixture as files.
	const std::pair<int, std::string_view> texts[] = {
		{5, "<sfx><clr:255,255,255><len:2.5><delay:1>Who's there?"},
		{10, "brick/*           role=wall pattern=brick parallax=on depth=0.006\nmetal/floor01    parallax=off bump=generate bumpstrength=0.5\n"},
		{12, "pick 12 face 3 at 1 2 3\n"},
		{13, "# branch.oid 0123456789abcdef\n# branch.head main\n1 .M N... 100644 100644 100644 0123 4567 maps/a.vmf"},
		{14, "ERROR: Leaked at (0 0 64), see maps/demo.lin\n0 0 64\n128 0 64\n"},
		{16, "mtllib crate.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0 0\nvn 0 0 1\nusemtl wood\nf 1/1/1 2/1/1 3/1/1\n\0newmtl wood\nKd 0.5 0.4 0.3\n"sv},
		{17, "style=city17 floors=4 windows=3x2 condition=damaged"},
		{18, "walk target=point1 speed=run"},
		{19, "models/a.mdl#s=1;b=2;g1=3"},
	};
	for (const auto& [selector, text] : texts) corpus.add("text", withSelector(std::uint8_t(selector), textBytes(text)));
	corpus.report();
	return 0;
}
