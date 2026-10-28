// test_mod_install.cpp - The mod's features installed from Mapsmith's bundle: manifest, branch,
// install with backups, update, removal that puts everything back, VRAD into the SDK.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/mod_install.h>
#include <sme/text.h>

#include <filesystem>
#include <fstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& path) {
	return readFileText(path).value_or("<missing>");
}

void write(const fs::path& path, std::string_view text) {
	test::writeText(path, text);
}

} // namespace

TEST_CASE("mod install: SHA-256") {
	CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
	      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000, 'a');
	CHECK(sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	const auto file = fs::temp_directory_path() / "sme_sha256.txt";
	write(file, million);
	CHECK(fileSha256(file) == sha256Hex(million));
	fs::remove(file);
	CHECK_FALSE(fileSha256(file));
}

TEST_CASE("mod install: the bundle's manifest, checked; the mod's branch from gameinfo.txt") {
	const auto dir = fs::temp_directory_path() / "sme_feature_bundle";
	test::makeFeatureBundle(dir, "0.1.0+abc1234", "one");
	std::string error;
	const auto bundle = readFeatureBundle(dir, &error);
	REQUIRE_MESSAGE(bundle, error);
	CHECK(bundle->version == "0.1.0+abc1234");
	CHECK(bundle->files.size() == 11);
	CHECK(bundle->find("BIN/episodic/client.dll"));   // case does not matter
	CHECK(verifyFeatureBundle(*bundle));
	const auto scripts = bundledScripts(*bundle);
	CHECK(scripts.deploy == dir / "fidelity" / "deploy.ps1");
	CHECK(scripts.report == dir / "fidelity" / "report.ps1");
	CHECK(scripts.update.empty());   // nothing is built

	// A damaged file is found before anything is installed.
	write(dir / "bin" / "game_shader_dx6.dll", "broken");
	CHECK_FALSE(verifyFeatureBundle(*bundle, &error));
	CHECK(error.find("game_shader_dx6.dll") != std::string::npos);

	// Broken manifests: not a bundle, a path out of the folder, a bad checksum.
	const auto bad = [&](std::string_view text) {
		write(dir / "manifest.txt", text);
		return !readFeatureBundle(dir, &error);
	};
	CHECK(bad(""));
	CHECK(bad("something else\nversion 1\n"));
	CHECK(bad("mapsmith-features 1\n"));   // no version
	CHECK(bad("mapsmith-features 1\nversion 1\n" + std::string(64, 'a') + "  ../evil.dll\n"));
	CHECK(bad("mapsmith-features 1\nversion 1\n" + std::string(64, 'a') + "  C:/evil.dll\n"));
	CHECK(bad("mapsmith-features 1\nversion 1\n" + std::string(64, 'x') + "  bin/x.dll\n"));
	CHECK(bad("mapsmith-features 1\nversion 1\nabc  bin/x.dll\n"));
	CHECK_FALSE(readFeatureBundle(dir / "missing", &error));
	fs::remove_all(dir);

	CHECK(modBranch("GameInfo { FileSystem { SearchPaths { game |gameinfo_path|. \n game ep2 \n game hl2 } } }") == ModBranch::Episodic);
	CHECK(modBranch("\"GameInfo\"\n{\n\tgame \"mymod\"\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tgame\t\t\t\t|all_source_engine_paths|episodic\n") ==
	      ModBranch::Episodic);
	CHECK(modBranch("GameInfo {\n // game ep2 (not used)\n game hl2\n}") == ModBranch::Hl2);
	CHECK(modBranch("GameInfo { game |all_source_engine_paths|hl2 }") == ModBranch::Hl2);
	CHECK(modBranchName(ModBranch::Hl2) == "hl2");
}

TEST_CASE("mod install: into a mod with backups, current files untouched, update, removal puts everything back") {
	const auto root = fs::temp_directory_path() / "sme_feature_install";
	fs::remove_all(root);
	const auto bundleDir = root / "features";
	const auto mod = root / "sourcemods" / "mymod";
	const auto sdkBin = root / "Source SDK Base 2013 Singleplayer" / "bin";
	test::makeFeatureBundle(bundleDir, "0.1.0+aaa", "one");
	auto bundle = readFeatureBundle(bundleDir);
	REQUIRE(bundle);

	// Not a mod.
	fs::create_directories(mod);
	auto result = installFeatures(*bundle, mod);
	CHECK_FALSE(result.ok);
	CHECK(result.error.find("gameinfo.txt") != std::string::npos);

	// An Episodic mod with Mapbase's own DLLs, the mapper's autoexec and an old deploy copy of the cfg.
	write(mod / "gameinfo.txt", "GameInfo\n{\n\tFileSystem\n\t{\n\t\tSearchPaths\n\t\t{\n\t\t\tgame |all_source_engine_paths|ep2\n\t\t}\n\t}\n}\n");
	write(mod / "bin" / "client.dll", "mapbase client");
	write(mod / "bin" / "server.dll", "mapbase server");
	write(mod / "cfg" / "autoexec.cfg", "bind f5 jpeg\r\n");
	write(mod / "cfg" / "fidelity.cfg", "// fidelity one\n");   // the same as the bundle's: stays, no backup
	write(sdkBin / "vrad.exe", "mapbase vrad");
	write(sdkBin / "vrad_dll.dll", "mapbase vrad dll");
	result = installFeatures(*bundle, mod, sdkBin);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.changed);
	CHECK(result.vrad);
	CHECK(read(mod / "bin" / "client.dll") == "episodic client one");
	CHECK(read(mod / "bin" / "server.dll") == "episodic server one");
	CHECK(read(mod / "bin" / "game_shader_dx6.dll") == "shaders one");
	CHECK(read(mod / "shaders" / "fxc" / "fidelity_ssao_ps20b.vcs") == "ssao one");
	CHECK(read(mod / "bin" / "client.dll.mapsmith-backup") == "mapbase client");
	CHECK(read(mod / "bin" / "server.dll.mapsmith-backup") == "mapbase server");
	CHECK_FALSE(fs::exists(mod / "cfg" / "fidelity.cfg.mapsmith-backup"));
	CHECK(std::find(result.written.begin(), result.written.end(), "cfg/fidelity.cfg") == result.written.end());
	CHECK(read(mod / "cfg" / "autoexec.cfg") == "bind f5 jpeg\r\nexec fidelity\t// mapbase-fidelity\r\n");
	CHECK_FALSE(fs::exists(mod / "deploy.ps1"));   // the scripts stay in the bundle
	CHECK_FALSE(fs::exists(mod / "fidelity"));
	// VRAD into the SDK, Mapbase's kept in update.ps1's backup folder.
	CHECK(read(sdkBin / "vrad.exe") == "vrad one");
	CHECK(read(sdkBin / "fidelity_backup" / "vrad.exe") == "mapbase vrad");
	CHECK(read(sdkBin / "fidelity_backup" / "fidelity_backup.txt") == "saved vrad.exe\r\nsaved vrad_dll.dll\r\n");
	auto installed = readInstalledFeatures(mod);
	REQUIRE(installed);
	CHECK(installed->version == "0.1.0+aaa");
	CHECK(installed->branch == ModBranch::Episodic);
	CHECK(installed->files.size() == 5);
	CHECK(installed->files.at("bin/client.dll") == sha256Hex("episodic client one"));
	CHECK(installed->backups.size() == 2);
	CHECK(installed->autoexecLine);
	CHECK(installed->vradDir == sdkBin);
	CHECK_FALSE(featuresOutdated(*bundle, *installed));

	// Again: nothing to do.
	result = installFeatures(*bundle, mod, sdkBin);
	REQUIRE(result.ok);
	CHECK_FALSE(result.changed);
	CHECK(result.backedUp.empty());

	// A newer Mapsmith: its files replace the old ones, the first backups stay, a file the new
	// version no longer has goes.
	test::makeFeatureBundle(bundleDir, "0.2.0+bbb", "two");
	fs::remove(bundleDir / "fidelity" / "game" / "shaders" / "fxc" / "fidelity_ssao_ps20b.vcs");
	{
		auto text = read(bundleDir / "manifest.txt");
		const auto at = text.find("fidelity/game/shaders");
		text.erase(text.rfind('\n', at) + 1, text.find('\n', at) - text.rfind('\n', at));
		write(bundleDir / "manifest.txt", text);
	}
	bundle = readFeatureBundle(bundleDir);
	REQUIRE(bundle);
	CHECK(featuresOutdated(*bundle, *installed));
	result = installFeatures(*bundle, mod, sdkBin);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(read(mod / "bin" / "client.dll") == "episodic client two");
	CHECK(read(mod / "bin" / "client.dll.mapsmith-backup") == "mapbase client");   // Mapbase's, not version one
	CHECK_FALSE(fs::exists(mod / "shaders" / "fxc" / "fidelity_ssao_ps20b.vcs"));
	CHECK(read(sdkBin / "vrad.exe") == "vrad two");
	CHECK(read(sdkBin / "fidelity_backup" / "vrad.exe") == "mapbase vrad");
	CHECK(read(mod / "cfg" / "autoexec.cfg") == "bind f5 jpeg\r\nexec fidelity\t// mapbase-fidelity\r\n");   // once
	installed = readInstalledFeatures(mod);
	REQUIRE(installed);
	CHECK(installed->version == "0.2.0+bbb");
	CHECK(installed->files.size() == 4);

	// The mapper changed a file of the install by hand: a removal leaves it.
	write(mod / "cfg" / "fidelity.cfg", "// my own changes\n");

	// Removal: Mapbase's DLLs and VRAD back, the add-on's files gone, the exec line gone.
	result = removeFeatures(mod);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(read(mod / "bin" / "client.dll") == "mapbase client");
	CHECK(read(mod / "bin" / "server.dll") == "mapbase server");
	CHECK_FALSE(fs::exists(mod / "bin" / "client.dll.mapsmith-backup"));
	CHECK_FALSE(fs::exists(mod / "bin" / "game_shader_dx6.dll"));
	CHECK(read(mod / "cfg" / "fidelity.cfg") == "// my own changes\n");
	CHECK(read(mod / "cfg" / "autoexec.cfg") == "bind f5 jpeg\r\n");
	CHECK(read(sdkBin / "vrad.exe") == "mapbase vrad");
	CHECK(read(sdkBin / "vrad_dll.dll") == "mapbase vrad dll");
	CHECK_FALSE(fs::exists(sdkBin / "fidelity_backup"));
	CHECK_FALSE(readInstalledFeatures(mod));
	CHECK(removeFeatures(mod).ok);   // nothing installed: nothing to do
	fs::remove_all(root);
}

TEST_CASE("mod install: an HL2 mod, a deploy.bat install before, VRAD already ours, a locked target") {
	const auto root = fs::temp_directory_path() / "sme_feature_install_hl2";
	fs::remove_all(root);
	const auto bundleDir = root / "features";
	const auto mod = root / "mymod";
	const auto sdkBin = root / "bin";
	test::makeFeatureBundle(bundleDir, "1.0", "one");
	const auto bundle = readFeatureBundle(bundleDir);
	REQUIRE(bundle);
	write(mod / "gameinfo.txt", "GameInfo { FileSystem { SearchPaths { game |all_source_engine_paths|hl2 } } }\n");
	// deploy.bat ran before: its exec line is there, the line stays its own.
	write(mod / "cfg" / "autoexec.cfg", "exec fidelity\t// mapbase-fidelity\n");
	write(sdkBin / "vrad.exe", "vrad one");   // already ours (update.ps1 copied it by hand)
	auto result = installFeatures(*bundle, mod, sdkBin);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(read(mod / "bin" / "client.dll") == "hl2 client one");
	CHECK_FALSE(fs::exists(mod / "bin" / "client.dll.mapsmith-backup"));   // there was none
	CHECK(read(mod / "cfg" / "autoexec.cfg") == "exec fidelity\t// mapbase-fidelity\n");
	CHECK(read(sdkBin / "fidelity_backup" / "fidelity_backup.txt") == "ours vrad.exe\r\n");
	const auto installed = readInstalledFeatures(mod);
	REQUIRE(installed);
	CHECK(installed->branch == ModBranch::Hl2);
	CHECK_FALSE(installed->autoexecLine);
	result = removeFeatures(mod);
	REQUIRE(result.ok);
	CHECK(read(mod / "cfg" / "autoexec.cfg") == "exec fidelity\t// mapbase-fidelity\n");   // deploy.bat's: not ours to remove
	CHECK_FALSE(fs::exists(mod / "bin" / "client.dll"));
	CHECK(read(sdkBin / "vrad.exe") == "vrad one");   // "ours": nothing older to put back
	CHECK_FALSE(fs::exists(sdkBin / "fidelity_backup"));

#ifndef _WIN32
	// A folder that cannot be written (on Windows: the game holding client.dll): the install
	// stops with a plain message and records what it wrote.
	fs::create_directories(mod / "bin");
	fs::permissions(mod / "bin", fs::perms::owner_write, fs::perm_options::remove);
	if (!std::ofstream(mod / "bin" / "probe.txt")) {
		result = installFeatures(*bundle, mod);
		CHECK_FALSE(result.ok);
		CHECK(result.error.find("close the game") != std::string::npos);
	}
	fs::permissions(mod / "bin", fs::perms::owner_write, fs::perm_options::add);
#endif
	fs::remove_all(root);
}
