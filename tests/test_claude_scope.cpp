// test_claude_scope.cpp - What Claude may touch through the Claude integration (sme/claude_scope.h):
// names in the exchange folder (no way out: "..", absolute and drive paths, UNC, ":" streams,
// reserved Windows names, links), materials of the mod (only its own .vmt files), VMT parameters
// and values, decision lines, the rate limit and the token comparison.

#include <doctest/doctest.h>

#include <sme/claude_scope.h>
#include <sme/material_decisions.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

using namespace sme;
namespace fs = std::filesystem;

namespace {

/// A fresh folder below the temp directory, removed at the end.
struct TempFolder {
	fs::path path;
	TempFolder() {
		std::random_device random;
		path = fs::temp_directory_path() / ("sme_claude_" + std::to_string(random()));
		fs::create_directories(path);
	}
	~TempFolder() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeFile(const fs::path& path, const std::string& text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("claude scope: exchange names stay inside the folder") {
	std::string why;
	CHECK(checkExchangeName("notes.txt") == std::optional<std::string>("notes.txt"));
	CHECK(checkExchangeName("a\\b/c.md") == std::optional<std::string>("a/b/c.md"));
	for (const char* bad : {"", "../x.txt", "a/../../x.txt", "./x.txt", "/etc/passwd", "\\\\server\\share\\x.txt", "\\x.txt", "C:\\x.txt",
	                        "c:x.txt", "x.txt:stream", "x.txt::$DATA", "CON", "con.txt", "nul.json", "COM1.log", "lpt9", "aux .txt", "x.txt.",
	                        "x.txt ", "a//b.txt", "a/b/c/d/e.txt", "x<y>.txt", "x|y.txt", "x?.txt", "x*.txt", "a\x01.txt", "conin$"}) {
		CAPTURE(bad);
		CHECK_FALSE(checkExchangeName(bad, &why));
		CHECK_FALSE(why.empty());
	}
	CHECK(reservedWindowsName("COM\xC2\xB9.txt"));
	CHECK_FALSE(reservedWindowsName("console.txt"));
	CHECK_FALSE(reservedWindowsName("com10"));
	CHECK_FALSE(checkExchangeName(std::string(300, 'a') + ".txt"));
}

TEST_CASE("claude scope: only plain data files may be written") {
	for (const char* ok : {"a.txt", "b.MD", "c.csv", "d.json", "e.log", "brick/wall.vmt"}) CHECK(exchangeExtensionAllowed(ok));
	for (const char* bad : {"a.exe", "b.bat", "c.ps1", "d.cmd", "e.lnk", "f.url", "g.js", "h.html", "i.dll", "noext", "j.txt.exe", "k.cfg", "dir.txt/x"}) {
		CAPTURE(bad);
		CHECK_FALSE(exchangeExtensionAllowed(bad));
	}
}

TEST_CASE("claude scope: resolveInside never leaves the folder, not even through links") {
	TempFolder temp;
	const fs::path base = temp.path / "exchange";
	const fs::path outside = temp.path / "outside";
	fs::create_directories(base / "sub");
	fs::create_directories(outside);
	writeFile(outside / "secret.txt", "secret");
	std::string why;
	const auto inside = resolveInside(base, "sub/new.txt", &why);
	REQUIRE(inside);
	CHECK(pathInside(base, *inside));
	CHECK_FALSE(resolveInside(base, "../outside/secret.txt", &why));
	CHECK_FALSE(resolveInside(temp.path / "missing", "a.txt", &why));
	// A link inside the folder that points out (a junction on Windows is the same case).
	std::error_code ec;
	fs::create_directory_symlink(outside, base / "link", ec);
	if (!ec) {
		CHECK_FALSE(resolveInside(base, "link/secret.txt", &why));
		CHECK_FALSE(resolveInside(base, "link", &why));
	}
	fs::create_symlink(outside / "secret.txt", base / "file.txt", ec);
	if (!ec) CHECK_FALSE(resolveInside(base, "file.txt", &why));
	// The base folder itself reached through a link is fine (its canonical path counts).
	fs::create_directory_symlink(base, temp.path / "baselink", ec);
	if (!ec) CHECK(resolveInside(temp.path / "baselink", "sub/new.txt", &why));
	CHECK_FALSE(pathInside(base, outside / "secret.txt"));
	CHECK_FALSE(pathInside(base, temp.path / "exchange2" / "x.txt"));
}

TEST_CASE("claude scope: materials are the mod's own .vmt files only") {
	TempFolder temp;
	const fs::path mod = temp.path / "mymod";
	writeFile(mod / "materials" / "brick" / "wall01.vmt", "\"LightmappedGeneric\" { \"$basetexture\" \"brick/wall01\" }");
	writeFile(temp.path / "other" / "materials" / "x.vmt", "x");
	std::string why;
	CHECK(checkMaterialName("Materials\\Brick\\Wall01.VMT") == std::optional<std::string>("brick/wall01"));
	CHECK_FALSE(checkMaterialName("brick/*", &why));
	CHECK_FALSE(checkMaterialName("../other/materials/x", &why));
	CHECK_FALSE(checkMaterialName("c:/x", &why));
	const auto file = modMaterialFile(mod, "brick/wall01", &why);
	REQUIRE(file);
	CHECK(file->filename() == "wall01.vmt");
	CHECK_FALSE(modMaterialFile(mod, "brick/missing", &why));   // not in the mod (a VPK's or the base game's)
	CHECK(why.find("cannot be changed") != std::string::npos);
	CHECK_FALSE(modMaterialFile(mod, "../../other/materials/x", &why));
	std::error_code ec;
	fs::create_directory_symlink(temp.path / "other" / "materials", mod / "materials" / "linked", ec);
	if (!ec) CHECK_FALSE(modMaterialFile(mod, "linked/x", &why));
}

TEST_CASE("claude scope: VMT parameters and values") {
	std::string why;
	CHECK(checkVmtParameter("$ParallaxDepth") == std::optional<std::string>("$parallaxdepth"));
	for (const char* bad : {"parallaxdepth", "$", "$a b", "$a\"", "${", "%keywords", "$a/b", "$\xC3\xA4"}) {
		CAPTURE(bad);
		CHECK_FALSE(checkVmtParameter(bad, &why));
	}
	CHECK(checkVmtValue("brick/brickwall01_normal"));
	CHECK(checkVmtValue("[1 0.5 0.25]"));
	CHECK(checkVmtValue(""));
	for (const char* bad : {"a\"b", "a}b", "{", "a\nb", "a\rb", "x // y", "a\tb"}) {
		CAPTURE(bad);
		CHECK_FALSE(checkVmtValue(bad, &why));
	}
	CHECK_FALSE(checkVmtValue(std::string(CLAUDE_VALUE_MAX + 1, 'a'), &why));
}

TEST_CASE("claude scope: decision lines are read back exactly") {
	std::string why;
	const auto line = decisionLineFromClaude("brick/*", {{"parallax", "off"}, {"note", "fine bricks, too small"}}, &why);
	REQUIRE(line);
	CHECK(*line == "brick/* parallax=off note=\"fine bricks, too small\"");
	const auto parsed = MaterialDecisions::parse(*line);
	CHECK(parsed.problems().empty());
	CHECK(parsed.lookup("brick/brickwall01").parallax == ParallaxChoice::Off);
	CHECK(decisionLineFromClaude("concrete/wall01", {{"depth", "0.006"}, {"role", "wall"}}));
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"parallax", "maybe"}}, &why));      // bad value
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"evil", "1"}}, &why));              // unknown key
	CHECK_FALSE(decisionLineFromClaude("brick/x", {}, &why));                           // nothing to set
	CHECK_FALSE(decisionLineFromClaude("brick x", {{"parallax", "on"}}, &why));         // two names
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"note", "a\"b"}}, &why));           // quote breaks out
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"note", "a\nother/* parallax=on"}}, &why));   // a second line
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"role", "wall parallax=on"}}, &why));
	CHECK_FALSE(decisionLineFromClaude("brick/x", {{"role", "wall"}, {"ROLE", "roof"}}, &why));
	CHECK_FALSE(decisionLineFromClaude("brick//x", {{"role", "wall"}}, &why));
	CHECK_FALSE(decisionLineFromClaude("", {{"role", "wall"}}, &why));
}

TEST_CASE("claude scope: rate limit and token comparison") {
	using namespace std::chrono_literals;
	ClaudeRateLimit limit(3, 60s);
	const auto t0 = ClaudeRateLimit::Clock::time_point{} + 1000s;
	CHECK(limit.allow(t0));
	CHECK(limit.allow(t0 + 1s));
	CHECK(limit.allow(t0 + 2s));
	CHECK_FALSE(limit.allow(t0 + 3s));
	CHECK(limit.waitSeconds(t0 + 3s) > 0);
	CHECK(limit.allow(t0 + 61s));
	CHECK(constantTimeEquals("abc", "abc"));
	CHECK_FALSE(constantTimeEquals("abc", "abd"));
	CHECK_FALSE(constantTimeEquals("abc", "abcd"));
	CHECK_FALSE(constantTimeEquals("", "a"));
}
