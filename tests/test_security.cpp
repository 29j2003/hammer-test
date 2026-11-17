// test_security.cpp - Rules against hostile files (docs/ENTWICKLUNG.md, "Sicherheit"): game paths
// from maps, VPKs, models and the decision file never leave their folder, and the picture
// importer hands only the formats it offers to the decoder.

#include <doctest/doctest.h>

#include <sme/asset_import.h>
#include <sme/filesystem.h>
#include <sme/git_status.h>
#include <sme/texture.h>

#include <cstring>
#include <string_view>
#include <vector>

using namespace sme;

TEST_CASE("security: game paths that would leave their folder are refused") {
	CHECK(isSafeGamePath("materials/brick/wall01.vmt"));
	CHECK(isSafeGamePath("brick/wall01"));
	CHECK(isSafeGamePath("models/props_c17/oildrum001.mdl"));
	CHECK(isSafeGamePath("a..b/c"));   // dots inside a name are fine
	for (const std::string_view bad : {"", "../cfg/autoexec", "materials/../../bin/client", "brick/..", "/etc/passwd", "\\\\server\\share\\x",
	                                   "c:/windows/win.ini", "C:\\Users\\x\\a.vmt", "materials/a.vmt:stream", "materials\\..\\..\\x"}) {
		CAPTURE(bad);
		CHECK_FALSE(isSafeGamePath(bad));
	}
	// The file system never resolves them either (no mounts needed: refused before any lookup).
	GameFileSystem fs;
	CHECK_FALSE(fs.read("c:/windows/win.ini"));
	CHECK_FALSE(fs.read("../../etc/passwd"));
	CHECK_FALSE(fs.exists("materials/../../x.vmt"));
}

TEST_CASE("security: repository settings that would start programs on git status or pull") {
	// What git writes into a fresh clone, Git for Windows' builtin fsmonitor, Git LFS: harmless.
	CHECK(gitConfigRisks("[core]\n\trepositoryformatversion = 0\n\tfilemode = false\n\tfsmonitor = true\n[remote \"origin\"]\n"
	                     "\turl = https://github.com/someone/mymod.git\n\tfetch = +refs/heads/*:refs/remotes/origin/*\n"
	                     "[branch \"master\"]\n\tremote = origin\n[filter \"lfs\"]\n\tclean = git-lfs clean -- %f\n\tprocess = git-lfs filter-process\n"
	                     "[credential]\n\thelper = manager\n").empty());
	// A downloaded folder with its own .git could carry these: each one runs a program.
	const auto risks = gitConfigRisks("[core]\n\tFSMonitor = \"C:/Users/Public/evil.exe\"\n\thooksPath = .githooks\n"
	                                  "[filter \"x\"]\n\tclean = evil.bat\n[include]\n\tpath = ../other.cfg\n"
	                                  "[credential]\n\thelper = !evil\n[remote \"origin\"]\n\turl = ext::sh -c evil\n"
	                                  "[core] sshCommand = evil.exe\n; [core] fsmonitor = commented.exe\n");
	CHECK(risks.size() == 7);
	REQUIRE_FALSE(risks.empty());
	CHECK(risks.front() == "core.fsmonitor = C:/Users/Public/evil.exe");
	CHECK(gitRepositoryRisks("/nonexistent/folder").empty());
}

TEST_CASE("security: the picture importer only decodes PNG, JPEG, TGA and BMP") {
	const auto bytes = [](std::string_view text) {
		std::vector<std::byte> out(text.size());
		std::memcpy(out.data(), text.data(), text.size());
		return out;
	};
	// Formats whose decoders had the most memory bugs never reach them (whatever follows the magic).
	for (const std::string_view magic : {"GIF89a", "8BPS", "#?RADIANCE\n", "#?RGBE\n", "\x53\x80\xF6\x34", "P6\n1 1\n255\n", "P5\n"}) {
		CAPTURE(magic);
		std::string error;
		CHECK_FALSE(decodeImageFile(bytes(std::string(magic) + std::string(64, '\x7f')), &error));
		CHECK(error.find("PNG, JPG, TGA, BMP") != std::string::npos);
	}
	// PNG: plain ones are read; interlaced ones never reach vtfpp's de-interlacer (it frees a moved
	// pointer - heap corruption), and neither do PNGs whose first chunk is not IHDR.
	ImageRgba picture;
	picture.width = 5;
	picture.height = 3;
	picture.pixels.assign(5 * 3 * 4, std::byte(200));
	auto png = encodePng(picture);
	REQUIRE(png.size() > 29);
	CHECK(decodeImageFile(png));
	auto interlaced = png;
	interlaced[28] = std::byte(1);
	std::string error;
	CHECK_FALSE(decodeImageFile(interlaced, &error));
	CHECK(error.find("interlaced") != std::string::npos);
	auto hidden = png;
	std::memcpy(hidden.data() + 12, "CgBI", 4);
	CHECK_FALSE(decodeImageFile(hidden));
	// Sizes from the header are checked before the decoder allocates them: a PNG claiming
	// 20000 x 20000, a short run-length TGA claiming 65535 x 65535.
	auto huge = png;
	for (const int at : {16, 20}) {
		huge[std::size_t(at)] = std::byte(0);
		huge[std::size_t(at) + 1] = std::byte(0);
		huge[std::size_t(at) + 2] = std::byte(0x4e);
		huge[std::size_t(at) + 3] = std::byte(0x20);
	}
	error.clear();
	CHECK_FALSE(decodeImageFile(huge, &error));
	CHECK(error.find("too large") != std::string::npos);
	std::vector<std::byte> tga(40, std::byte(0));
	tga[2] = std::byte(10);   // run-length true colour
	tga[12] = tga[13] = tga[14] = tga[15] = std::byte(0xff);
	tga[16] = std::byte(32);
	CHECK_FALSE(decodeImageFile(tga));
}
