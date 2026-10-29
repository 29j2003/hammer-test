// test_mod_install.cpp - The mod's features installed from Mapsmith's bundle: manifest, branch,
// install with backups, update, removal that puts everything back, VRAD into the SDK.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/checks.h>
#include <sme/map.h>
#include <sme/mod_install.h>
#include <sme/text.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

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
	CHECK(bundle->files.size() == 13);
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
	CHECK(read(mod / "bin" / "game_shader_dx9.dll") == "episodic mapbase shaders one");   // Mapbase's own shaders
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
	CHECK(installed->files.size() == 6);
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
	CHECK(installed->files.size() == 5);

	// The mapper changed a file of the install by hand: a removal leaves it.
	write(mod / "cfg" / "fidelity.cfg", "// my own changes\n");

	// Removal: Mapbase's DLLs and VRAD back, the add-on's files gone, the exec line gone.
	result = removeFeatures(mod);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(read(mod / "bin" / "client.dll") == "mapbase client");
	CHECK(read(mod / "bin" / "server.dll") == "mapbase server");
	CHECK_FALSE(fs::exists(mod / "bin" / "client.dll.mapsmith-backup"));
	CHECK_FALSE(fs::exists(mod / "bin" / "game_shader_dx6.dll"));
	CHECK_FALSE(fs::exists(mod / "bin" / "game_shader_dx9.dll"));   // there was none before
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
	CHECK(read(mod / "bin" / "game_shader_dx9.dll") == "hl2 mapbase shaders one");
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

namespace {

/// A minimal Windows DLL header: "MZ", e_lfanew, "PE\0\0" and the COFF link time.
std::string peFile(std::uint32_t linkTime, std::string_view tail) {
	std::string data(0x98, '\0');
	data[0] = 'M';
	data[1] = 'Z';
	data[0x3C] = char(0x80);
	data.replace(0x80, 4, std::string("PE\0\0", 4));
	data[0x84] = char(0x4C);   // i386
	data[0x85] = char(0x01);
	for (int i = 0; i < 4; ++i) data[0x88 + i] = char((linkTime >> (8 * i)) & 0xFF);
	return data + std::string(tail);
}

} // namespace

TEST_CASE("mod install: PE link time") {
	const auto bytes = [](const std::string& text) {
		return std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data()) + text.size());
	};
	CHECK(peLinkTime(bytes(peFile(0x5F3E2A10, "code"))) == 0x5F3E2A10u);
	CHECK_FALSE(peLinkTime(bytes("")));
	CHECK_FALSE(peLinkTime(bytes("not a dll at all, just some text that is long enough for the header check")));
	auto noPe = peFile(1, "");
	noPe[0x80] = 'X';
	CHECK_FALSE(peLinkTime(bytes(noPe)));
	auto pastEnd = peFile(1, "");
	pastEnd[0x3C] = char(0xF0);   // e_lfanew beyond the data
	CHECK_FALSE(peLinkTime(bytes(pastEnd)));
	const auto file = fs::temp_directory_path() / "sme_pe_link_time.dll";
	write(file, peFile(1234567, "rest"));
	CHECK(peLinkTimeOfFile(file) == 1234567u);
	fs::remove(file);
	CHECK_FALSE(peLinkTimeOfFile(file));
}

TEST_CASE("mod install: a Mapbase mod's bin - missing DLLs, an old game_shader_dx9.dll, filled from Mapbase, incomplete installs") {
	const auto root = fs::temp_directory_path() / "sme_mod_bin";
	fs::remove_all(root);
	const auto sourcemods = root / "sourcemods";
	const auto mod = sourcemods / "test2";
	const auto bundleDir = root / "features";
	test::makeFeatureBundle(bundleDir, "1.0", "one");
	// The bundle's Mapbase shaders as real headers: newer than the mod's old copy.
	write(bundleDir / "bin" / "episodic" / "game_shader_dx9.dll", peFile(2000, "new shaders"));
	{
		auto text = read(bundleDir / "manifest.txt");
		const auto at = text.find("  bin/episodic/game_shader_dx9.dll");
		text.replace(text.rfind('\n', at) + 1, 64, sha256Hex(peFile(2000, "new shaders")));
		write(bundleDir / "manifest.txt", text);
	}
	const auto bundle = readFeatureBundle(bundleDir);
	REQUIRE(bundle);
	REQUIRE(verifyFeatureBundle(*bundle));

	// Not a mod: nothing to say.
	CHECK(checkModBin(mod, &*bundle).ok());
	CHECK(copyMapbaseBin(sourcemods, mod).empty());

	// A new mod with only client.dll and server.dll (what test2 had): the shaders are missing.
	write(mod / "gameinfo.txt", "GameInfo { FileSystem { SearchPaths { game |all_source_engine_paths|ep2 } } }\n");
	write(mod / "bin" / "client.dll", "mapbase client");
	write(mod / "bin" / "server.dll", "mapbase server");
	auto state = checkModBin(mod, nullptr);
	CHECK_FALSE(state.ok());
	CHECK(state.missing == std::vector<std::string>{"bin/game_shader_dx9.dll"});
	CHECK(state.outdated.empty());

	// Filled from Mapbase's own game folder next to it (the branch's), never overwriting.
	write(sourcemods / "mapbase_episodic" / "bin" / "client.dll", "mapbase_episodic client");
	write(sourcemods / "mapbase_episodic" / "bin" / "game_shader_dx9.dll", peFile(1000, "old shaders"));
	write(sourcemods / "mapbase_hl2" / "bin" / "game_shader_dx9.dll", "wrong branch");
	CHECK(copyMapbaseBin(sourcemods, mod) == std::vector<std::string>{"bin/game_shader_dx9.dll"});
	CHECK(read(mod / "bin" / "game_shader_dx9.dll") == peFile(1000, "old shaders"));
	CHECK(read(mod / "bin" / "client.dll") == "mapbase client");
	CHECK(checkModBin(mod, nullptr).ok());

	// With the bundle: Mapbase's game_shader_dx9.dll is older than Mapsmith's. The game code is not
	// judged by its age (that is the features' version).
	state = checkModBin(mod, &*bundle);
	CHECK(state.missing.empty());
	CHECK(state.outdated == std::vector<std::string>{"bin/game_shader_dx9.dll"});

	// A newer one than the bundle's is fine, so is the bundle's own.
	write(mod / "bin" / "game_shader_dx9.dll", peFile(3000, "even newer"));
	CHECK(checkModBin(mod, &*bundle).ok());

	// Installed features: the missing game_shader_dx6.dll is found, and the install counts as
	// incomplete until all its files are there again.
	REQUIRE(installFeatures(*bundle, mod).ok);
	CHECK(checkModBin(mod, &*bundle).ok());
	CHECK(missingFeatureFiles(*bundle, mod).empty());
	fs::remove(mod / "bin" / "game_shader_dx6.dll");
	fs::remove(mod / "bin" / "game_shader_dx9.dll");
	state = checkModBin(mod, &*bundle);
	CHECK(state.missing == std::vector<std::string>{"bin/game_shader_dx9.dll", "bin/game_shader_dx6.dll"});
	CHECK(missingFeatureFiles(*bundle, mod) == std::vector<std::string>{"bin/game_shader_dx9.dll", "bin/game_shader_dx6.dll"});
	REQUIRE(installFeatures(*bundle, mod).ok);
	CHECK(missingFeatureFiles(*bundle, mod).empty());
	CHECK(read(mod / "bin" / "game_shader_dx9.dll") == peFile(2000, "new shaders"));
	fs::remove_all(root);
}

TEST_CASE("mod install: the Problems list warns about a Mapbase mod without its shaders") {
	const auto root = fs::temp_directory_path() / "sme_mod_bin_check";
	fs::remove_all(root);
	const auto mod = root / "test2";
	write(mod / "gameinfo.txt", "GameInfo { FileSystem { SearchPaths { game |all_source_engine_paths|ep2 } } }\n");
	write(mod / "bin" / "client.dll", "mapbase client");
	write(mod / "bin" / "server.dll", "mapbase server");
	const MapDocument doc;
	const auto find = [](const std::vector<Problem>& problems) {
		const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::ModBinIncomplete; });
		return it == problems.end() ? std::optional<Problem>() : std::optional<Problem>(*it);
	};
	CHECK_FALSE(find(checkMap(doc, CheckContext{})));   // not a Mapbase mod (or none known)

	CheckContext context;
	context.mapbaseModDir = mod;
	auto problem = find(checkMap(doc, context));
	REQUIRE(problem);
	CHECK(problem->severity == Problem::Severity::Warning);
	CHECK(problem->action == "installFeatures");
	CHECK(problem->message.find("bin/game_shader_dx9.dll") != std::string::npos);
	CHECK(problem->message.find("wireframe") != std::string::npos);
	CHECK(problem->fixLabel.empty());   // no bundle: nothing to install from

	test::makeFeatureBundle(root / "features", "1.0", "one");
	const auto bundle = readFeatureBundle(root / "features");
	REQUIRE(bundle);
	context.bundle = &*bundle;
	problem = find(checkMap(doc, context));
	REQUIRE(problem);
	CHECK(problem->fixLabel == "Install features");

	REQUIRE(installFeatures(*bundle, mod).ok);
	CHECK_FALSE(find(checkMap(doc, context)));
	fs::remove_all(root);
}
