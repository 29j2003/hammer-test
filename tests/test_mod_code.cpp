// test_mod_code.cpp - The mod's source code: finding the checkout, the script command lines and
// reading update.ps1's output (lines as the real script prints them).

#include <doctest/doctest.h>

#include <sme/mod_code.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& path) {
	fs::create_directories(path.parent_path());
	std::ofstream(path) << "# test\n";
}

/// A fake checkout: fidelity/update.ps1 (+ deploy/report) and sp/src.
void makeCheckout(const fs::path& root, bool withDeploy = true) {
	touch(root / "fidelity" / "update.ps1");
	if (withDeploy) touch(root / "fidelity" / "deploy.ps1");
	touch(root / "fidelity" / "report.ps1");
	fs::create_directories(root / "sp" / "src");
}

std::string text(const fs::path& path) {
	const auto u8 = path.u8string();
	return std::string(u8.begin(), u8.end());
}

} // namespace

TEST_CASE("mod code: a checkout is recognised by its scripts") {
	const auto dir = fs::temp_directory_path() / "sme_mod_code_scripts";
	fs::remove_all(dir);
	CHECK_FALSE(modCodeScripts(dir));
	CHECK_FALSE(modCodeScripts({}));
	touch(dir / "fidelity" / "update.ps1");
	CHECK_FALSE(modCodeScripts(dir));   // a lone fidelity folder is not a checkout
	touch(dir / "update.bat");
	const auto scripts = modCodeScripts(dir);
	REQUIRE(scripts);
	CHECK(scripts->root == dir);
	CHECK(scripts->update == dir / "fidelity" / "update.ps1");
	CHECK(scripts->deploy.empty());
	makeCheckout(dir);
	CHECK(modCodeScripts(dir)->deploy == dir / "fidelity" / "deploy.ps1");
	CHECK(modCodeScripts(dir)->report == dir / "fidelity" / "report.ps1");
	fs::remove_all(dir);
}

TEST_CASE("mod code: found in the search roots, the folder named like the mod first") {
	const auto dir = fs::temp_directory_path() / "sme_mod_code_find";
	fs::remove_all(dir);
	const auto github = dir / "home" / "Documents" / "GitHub";
	const auto drive = dir / "D";
	makeCheckout(github / "aaa-other-mod");
	makeCheckout(drive / "GitHub" / "MyMod");
	fs::create_directories(github / "unrelated");
	const auto roots = modCodeSearchRoots(dir / "home", {drive});
	// The mod's name beats an earlier root with another checkout (case does not matter).
	// (Windows finds "mymod" directly - same folder, other spelling, so compare the folders.)
	const auto named = findModCode("mymod", roots);
	REQUIRE(named);
	CHECK(fs::equivalent(*named, drive / "GitHub" / "MyMod"));
	// Another name: the first checkout of any name.
	CHECK(findModCode("othermod", roots) == github / "aaa-other-mod");
	// A root that is itself the checkout.
	CHECK(findModCode("x", {dir / "missing", drive / "GitHub" / "MyMod"}) == drive / "GitHub" / "MyMod");
	CHECK_FALSE(findModCode("mymod", {dir / "missing"}));
	CHECK_FALSE(findModCode("mymod", {}));
	fs::remove_all(dir);
}

TEST_CASE("mod code: command lines never wait for input") {
	ModCodeScripts scripts;
	scripts.root = fs::path("repo");
	scripts.update = fs::path("repo") / "fidelity" / "update.ps1";
	scripts.report = fs::path("repo") / "fidelity" / "report.ps1";
	const fs::path mod = fs::path("sourcemods") / "my mod";

	const auto windows = modCodeCommand(scripts, ModCodeAction::Update, mod, true);
	REQUIRE(windows);
	CHECK(windows->program == "powershell.exe");
	const std::vector<std::string> expected{"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
	                                        "-File", text(scripts.update), "-GamePath", text(mod)};
	CHECK(windows->args == expected);
	CHECK(windows->workingDir == scripts.root);

	const auto noVrad = modCodeCommand(scripts, ModCodeAction::UpdateNoVrad, mod, true);
	REQUIRE(noVrad);
	CHECK(noVrad->args.back() == "-NoVrad");

	const auto report = modCodeCommand(scripts, ModCodeAction::Report, mod, false);
	REQUIRE(report);
	CHECK(report->program == "pwsh");
	CHECK(std::find(report->args.begin(), report->args.end(), "-ExecutionPolicy") == report->args.end());
	CHECK(report->args[report->args.size() - 3] == text(scripts.report));

	CHECK_FALSE(modCodeCommand(scripts, ModCodeAction::Deploy, mod, true));   // no deploy.ps1
	CHECK_FALSE(modCodeCommand(scripts, ModCodeAction::Update, {}, true));    // no mod folder
	CHECK(modCodeNeedsWindows(ModCodeAction::Update));
	CHECK_FALSE(modCodeNeedsWindows(ModCodeAction::Deploy));
	CHECK_FALSE(modCodeNeedsWindows(ModCodeAction::Report));
	CHECK_FALSE(modCodeNeedsWindows(ModCodeAction::RemoveGameFiles));

	// Fresh install: deploy.ps1 -Remove takes out what it put into the mod.
	CHECK_FALSE(modCodeCommand(scripts, ModCodeAction::RemoveGameFiles, mod, true));   // no deploy.ps1
	scripts.deploy = fs::path("repo") / "fidelity" / "deploy.ps1";
	const auto remove = modCodeCommand(scripts, ModCodeAction::RemoveGameFiles, mod, true);
	REQUIRE(remove);
	CHECK(remove->args.back() == "-Remove");
	CHECK(remove->args[remove->args.size() - 4] == text(scripts.deploy));
	CHECK(classifyScriptLine("Done. The fidelity game files are gone.").kind == ScriptLine::Kind::Success);
}

TEST_CASE("mod code: update.ps1 lines become steps") {
	const auto step = [](std::string_view line) { return classifyScriptLine(line).step; };
	CHECK(step("mapbase-fidelity update -> C:\\Steam\\steamapps\\sourcemods\\mymod") == UpdateStep::Start);
	CHECK(step("  found    C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe") ==
	      UpdateStep::VisualStudio);
	CHECK(step("  fixed    newer_vs_toolsets.vpc: VS2022 back to 1") == UpdateStep::VisualStudio);
	CHECK(step("  ok       games.sln is up to date") == UpdateStep::Projects);
	CHECK(step("  making   games.sln (VPC)") == UpdateStep::Projects);
	CHECK(step("  fixed    games.sln: Release|Win32 build lines added for 12 projects") == UpdateStep::Projects);
	CHECK(step("  building games.sln") == UpdateStep::Build);
	CHECK(step("  copied   bin\\client.dll (episodic)") == UpdateStep::Dlls);
	CHECK(step("  ok       bin\\server.dll is current (episodic)") == UpdateStep::Dlls);
	CHECK(step("  making   maptools.sln (VPC)") == UpdateStep::Vrad);
	CHECK(step("  building maptools.sln (Vrad_dll, Vrad_launcher)") == UpdateStep::Vrad);
	CHECK(step("  copied   vrad.exe -> C:\\Steam\\steamapps\\common\\Source SDK Base 2013 Singleplayer\\bin") == UpdateStep::Vrad);
	CHECK(step("  skipped  VRAD (-NoVrad)") == UpdateStep::Vrad);
	CHECK(step("mapbase-fidelity game files -> C:\\mod") == UpdateStep::GameFiles);
	CHECK(step("All set. Start the mod, or compile a map as usual - nothing else to do.") == UpdateStep::Done);
	CHECK_FALSE(step("  copied   materials\\fidelity\\x.vmt"));

	const auto failed = classifyScriptLine("  FAILED   Visual Studio 2022 with 'Desktop development with C++' not found (Visual Studio Installer)");
	CHECK(failed.kind == ScriptLine::Kind::Failed);
	CHECK(failed.message == "Visual Studio 2022 with 'Desktop development with C++' not found (Visual Studio Installer)");
	CHECK(classifyScriptLine("c:\\src\\game\\client\\x.cpp(12): error C2065: 'M_SQRT1_2': undeclared identifier").kind ==
	      ScriptLine::Kind::Error);
	CHECK(classifyScriptLine("LINK : fatal error LNK1104: cannot open file 'client.dll'").kind == ScriptLine::Kind::Error);
	CHECK(classifyScriptLine("x.cpp(3): warning C4244: conversion").kind == ScriptLine::Kind::Warning);
	CHECK(classifyScriptLine("Done. Start the mod; the console shows ...").kind == ScriptLine::Kind::Success);
	CHECK(classifyScriptLine("  something else").kind == ScriptLine::Kind::Info);
}

TEST_CASE("mod code: a whole run - success, failure with advice, cancelled") {
	ScriptRun ok;
	for (const auto* line : {"mapbase-fidelity update -> C:\\mod", "  found    MSBuild.exe", "  ok       games.sln is up to date",
	                         "  building games.sln", "  copied   bin\\client.dll (episodic)",
	                         "  skipped  VRAD - Source SDK Base 2013 Singleplayer not found (Steam library)",
	                         // deploy.ps1's own skips are not interesting
	                         "mapbase-fidelity game files -> C:\\mod", "  skipped  materials (none)",
	                         "  ok       games.sln is up to date",   // a late line never moves the step back
	                         "Done. Start the mod", "All set. Start the mod"}) {
		ok.feed(line);
	}
	CHECK(ok.step() == UpdateStep::Done);
	ok.finish(0);
	CHECK(ok.succeeded());
	CHECK(ok.advice().empty());
	REQUIRE(ok.skipped().size() == 1);
	CHECK(ok.skipped()[0] == "VRAD - Source SDK Base 2013 Singleplayer not found (Steam library)");

	ScriptRun build;
	for (const auto* line : {"mapbase-fidelity update -> C:\\mod", "  found    MSBuild.exe", "  building games.sln",
	                         "x.cpp(12): error C2065: 'M_SQRT1_2': undeclared identifier",
	                         "x.cpp(12): error C2065: 'M_SQRT1_2': undeclared identifier", "",
	                         "  FAILED   the build of games.sln failed - the errors are above, the whole log is fidelity\\update.log"}) {
		build.feed(line);
	}
	build.finish(1);
	CHECK_FALSE(build.succeeded());
	CHECK(build.step() == UpdateStep::Build);   // where it stopped
	CHECK(build.errors().size() == 1);          // repeated errors once
	CHECK(build.advice().find("did not compile") != std::string::npos);

	ScriptRun vs;
	vs.feed("  FAILED   Visual Studio 2022 with 'Desktop development with C++' not found (Visual Studio Installer)");
	vs.finish(1);
	CHECK(vs.advice().find("Desktop development with C++") != std::string::npos);

	ScriptRun unknown;
	unknown.feed("  FAILED   something new");
	unknown.finish(1);
	CHECK(unknown.advice() == "something new");   // the script's own words

	ScriptRun stopped;
	stopped.feed("mapbase-fidelity update -> C:\\mod");
	stopped.finish(-1);
	CHECK_FALSE(stopped.succeeded());
	CHECK(stopped.advice().find("stopped") != std::string::npos);

	ScriptRun crashed;
	crashed.feedError("Cannot find path 'X:\\nothing' because it does not exist.");
	crashed.feedError("   ");
	crashed.finish(1);
	CHECK(crashed.errors().size() == 1);
	CHECK(crashed.advice().find("log") != std::string::npos);
}

TEST_CASE("mod code: report names the zips to send") {
	ScriptRun report;
	for (const auto* line : {"mapbase-fidelity report -> C:\\mod", "  packed   2 reports, 2 screenshots", "", "Send:",
	                         "  C:\\mod\\fidelity_report.zip  (3.2 MB)", "  C:\\mod\\fidelity_report_2.zip  (1.0 MB)",
	                         "(afterwards: report.bat -Clear empties the folder)"}) {
		report.feed(line);
	}
	report.finish(0);
	CHECK(report.succeeded());
	REQUIRE(report.outputs().size() == 2);
	CHECK(report.outputs()[0] == "C:\\mod\\fidelity_report.zip");
	CHECK(report.outputs()[1] == "C:\\mod\\fidelity_report_2.zip");
}
