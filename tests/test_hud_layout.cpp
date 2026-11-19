// test_hud_layout.cpp - HudLayout.res / ClientScheme.res reading, vgui positions and in-place edits.

#include <doctest/doctest.h>

#include <sme/asset_import.h>
#include <sme/hud_layout.h>

#include "fixture.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;

namespace {

constexpr std::string_view LAYOUT = R"("Resource/HudLayout.res"
{
	// The health display
	HudHealth [$WIN32]
	{
		"fieldName"		"HudHealth"
		"xpos"	"16"
		"ypos"	"432"
		"wide"	"102"
		"tall"  "36"
		"visible" "1"
		"digit_xpos" "50"
	}
	HudHealth [$X360]
	{
		"fieldName"		"HudHealth"
		"xpos"	"48"
		"ypos"	"416"
	}
	HudAmmo	[$WIN32]
	{
		"xpos"	"r150"
		"ypos"	"432"
		"wide"	"136"
		"tall"  "36"
	}
	HudFlashlight
	{
		"xpos"	"270"		[$WIN32]
		"xpos"	"c-18"		[$X360]		// centred on consoles
		"ypos"	"444"
		"wide"	"36"
		"tall"	"24"
	}
	HudCrosshair
	{
		"visible" "1"
		"wide"	 "640"
		"tall"	 "480"
	}
	HudHintDisplay
	{
		"visible"	"0"
		"xpos"	"c-240"
		"ypos"	"c60"
		"wide"	"480"
		"tall"	"100"
	}
}
)";

constexpr std::string_view SCHEME = R"(Scheme
{
	Colors
	{
		"Orange"	"255 160 0 255"
	}
	BaseSettings
	{
		"FgColor"	"255 220 0 100"
		"BgColor"	"0 0 0 76"
		"Highlight"	"Orange"
		"Main.Title1.X"	"53"
	}
	Fonts
	{
		Default
		{
			"1"	[$WIN32]
			{
				"name"	"Verdana"
				"tall"	"9"
				"weight"	"700"
				"yres"	"1 599"
			}
			"1"	[$X360]
			{
				"name"	"Verdana"
				"tall"	"12"
			}
			"2"
			{
				"name"	"Verdana"
				"tall"	"20"
				"weight"	"900"
				"yres"	"1024 1199"
			}
		}
		HudNumbers
		{
			"1"
			{
				"name"	"HalfLife2" [!$OSX]
				"name"	"Helvetica Bold" [$OSX]
				"tall"	"32"
				"additive"	"1"
				"custom"	"1"
			}
		}
	}
	CustomFontFiles
	{
		"1"	"resource/HALFLIFE2.ttf"
		"2"	"../outside.ttf"
	}
}
)";

std::string readFile(const std::filesystem::path& p) {
	std::ifstream in(p, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

} // namespace

TEST_CASE("hud: platform conditions as the Windows PC game reads them") {
	CHECK(resConditionTrue("[$WIN32]"));
	CHECK(resConditionTrue("[$WINDOWS]"));
	CHECK_FALSE(resConditionTrue("[$X360]"));
	CHECK(resConditionTrue("[!$X360]"));
	CHECK(resConditionTrue("[!$OSX]"));
	CHECK_FALSE(resConditionTrue("[$OSX]"));
	CHECK(resConditionTrue("[$WIN32 && !$X360]"));
	CHECK(resConditionTrue("[$X360 || $WIN32]"));
	CHECK_FALSE(resConditionTrue("[$POSIX || $GAMECONSOLE]"));
	CHECK(resConditionTrue("[!($X360 || $PS3)]"));
	CHECK_FALSE(resConditionTrue("[$WIN32 &&]"));   // broken: not used
	CHECK_FALSE(resConditionTrue("WIN32"));
}

TEST_CASE("hud: elements of HudLayout.res, the PC variant of each") {
	const auto layout = parseResFile(LAYOUT);
	REQUIRE(layout);
	const auto elements = hudElements(*layout);
	REQUIRE(elements.size() == 5);
	CHECK(elements[0].name == "HudHealth");
	CHECK(elements[0].xpos == "16");        // the [$WIN32] block, not the [$X360] one
	CHECK(elements[0].hasPosition);
	CHECK(elements[1].name == "HudAmmo");
	CHECK(elements[2].xpos == "270");       // value condition: the console line is skipped
	CHECK(elements[3].name == "HudCrosshair");
	CHECK(elements[3].fullScreen);
	CHECK_FALSE(elements[3].hasPosition);
	CHECK_FALSE(elements[4].visible);
	CHECK(hudElementInfo("HudAmmo").label == "AMMO");
	CHECK(hudElementInfo("HudSomethingNew").title.empty());
}

TEST_CASE("hud: positions and sizes like vgui's proportional panels") {
	CHECK(hudScaled(16, 480) == 16);
	CHECK(hudScaled(16, 1080) == 36);
	CHECK(hudScaled(432, 1080) == 972);
	// HudHealth at 1920x1080: 16,432 102x36 -> scaled by 1080/480 = 2.25.
	CHECK(resolveHudRect("16", "432", "102", "36", 1920, 1080) == HudRect{36, 972, 229, 81});
	// Right aligned: r150 = 150 units from the right edge.
	CHECK(resolveHudRect("r150", "432", "136", "36", 1920, 1080).x == 1920 - 337);
	// Centred: c-240 = 240 units left of the middle.
	CHECK(resolveHudRect("c-240", "c60", "480", "100", 1920, 1080) == HudRect{960 - 540, 540 + 135, 1080, 225});
	// Fill: f0 = the whole screen; f10 = screen minus 10 units.
	CHECK(resolveHudRect("0", "0", "f0", "f10", 1280, 720) == HudRect{0, 0, 1280, 720 - 15});
	// s: fraction of own size; p: fraction of the screen.
	CHECK(resolveHudRect("rs1", "p0.5", "100", "100", 640, 480) == HudRect{540, 240, 100, 100});
	// o: the other axis.
	CHECK(resolveHudRect("0", "0", "o1", "40", 640, 480).wide == 40);
	// Missing values are 0; garbage reads like atoi.
	CHECK(resolveHudRect("", "abc", "", "", 640, 480) == HudRect{});

	// Back from pixels, keeping the anchor.
	CHECK(hudPositionText("r150", 1920 - 337, 0, 1920, 1080) == "r150");
	CHECK(hudPositionText("r150", 1920 - 450, 0, 1920, 1080) == "r200");
	CHECK(hudPositionText("c-18", 960 - 90, 0, 1920, 1080) == "c-40");
	CHECK(hudPositionText("16", 72, 0, 1920, 1080) == "32");
	CHECK(hudPositionText("s0.5", 72, 0, 1920, 1080) == "32");   // fractions become plain offsets
	CHECK(hudSizeText("102", 450, 1920, 1080) == "200");
	CHECK(hudSizeText("f0", 1900, 1920, 1080) == "f9");
	CHECK(hudAnchor("R10") == HudAnchor::End);
	CHECK(hudPositionNumber("c-18") == doctest::Approx(-18));
	CHECK(hudPositionFrom(HudAnchor::Center, -18.4) == "c-18");
}

TEST_CASE("hud: edits change only the value in the text, comments and other platforms stay") {
	auto layout = parseResFile(LAYOUT);
	REQUIRE(layout);
	const ResNode* health = hudElementBlock(*layout, "HudHealth");
	REQUIRE(health);
	std::string text = withResValue(*layout, *health, "xpos", "40");
	CHECK(text.find("\"xpos\"\t\"40\"") != std::string::npos);   // the value is replaced in place ...
	CHECK(text.find("// The health display") != std::string::npos);
	layout = parseResFile(text);
	REQUIRE(layout);
	CHECK(hudElements(*layout)[0].xpos == "40");
	CHECK(text.find("\"xpos\"\t\"48\"") != std::string::npos);   // ... the console block keeps its own
	// A key the block does not have goes in before its '}', indented like the others.
	health = hudElementBlock(*layout, "HudHealth");
	text = withResValue(*layout, *health, "TextColor", "255 0 0 255");
	CHECK(text.find("\t\t\"TextColor\"\t\"255 0 0 255\"\n\t}") != std::string::npos);
	layout = parseResFile(text);
	REQUIRE(layout);
	CHECK(hudElementBlock(*layout, "HudHealth")->get("TextColor") == "255 0 0 255");
	// The value with a condition: the PC line changes, the console line stays.
	const ResNode* flash = hudElementBlock(*layout, "HudFlashlight");
	text = withResValue(*layout, *flash, "xpos", "300");
	CHECK(text.find("\"300\"\t\t[$WIN32]") != std::string::npos);
	CHECK(text.find("\"c-18\"\t\t[$X360]") != std::string::npos);
	// Quotes in a value cannot break the file.
	layout = parseResFile(text);
	REQUIRE(layout);
	text = withResValue(*layout, *hudElementBlock(*layout, "HudAmmo"), "label", "a\"b\nc");
	CHECK(parseResFile(text));
	CHECK(hudElementBlock(*parseResFile(text), "HudAmmo")->get("label") == "a'b c");
}

TEST_CASE("hud: ClientScheme colours and fonts") {
	const auto scheme = parseResFile(SCHEME);
	REQUIRE(scheme);
	CHECK(parseHudColor("255 220 0 100") == HudColor{255, 220, 0, 100});
	CHECK(parseHudColor("10 20 30") == HudColor{10, 20, 30, 255});
	CHECK(parseHudColor(" 300 0 0 0 ") == HudColor{255, 0, 0, 0});
	CHECK_FALSE(parseHudColor("Orange"));
	CHECK_FALSE(parseHudColor("1 2"));
	CHECK_FALSE(parseHudColor("1 2 3 4 5"));
	CHECK(hudColorText({1, 2, 3, 4}) == "1 2 3 4");
	CHECK(resolveSchemeColor(*scheme, "Highlight") == HudColor{255, 160, 0, 255});   // a name from Colors
	CHECK(resolveSchemeColor(*scheme, "FgColor") == HudColor{255, 220, 0, 100});
	CHECK_FALSE(resolveSchemeColor(*scheme, "Nothing"));
	const auto colors = schemeColors(*scheme);
	REQUIRE(colors.size() == 4);   // FgColor, BgColor, Highlight (BaseSettings), Orange (Colors); not Main.Title1.X
	CHECK(colors[2].name == "Highlight");
	CHECK(colors[2].text == "Orange");
	CHECK(colors[3].section == "Colors");

	const auto fonts = schemeFonts(*scheme);
	REQUIRE(fonts.size() == 2);
	CHECK(fonts[0].entries.size() == 2);   // the [$X360] entry is left out
	const auto* low = pickFontEntry(fonts[0], 480);
	REQUIRE(low);
	CHECK(low->tall == 9);
	CHECK(fontPixelTall(*low, 480) == 9);
	const auto* high = pickFontEntry(fonts[0], 1080);
	REQUIRE(high);
	CHECK(high->tall == 20);
	CHECK_FALSE(pickFontEntry(fonts[0], 800));   // no entry for that range and none without yres
	const auto* numbers = pickFontEntry(fonts[1], 1080);
	REQUIRE(numbers);
	CHECK(numbers->family == "HalfLife2");
	CHECK(numbers->additive);
	CHECK(fontPixelTall(*numbers, 1080) == 72);   // no yres: scaled like the HUD
	const auto files = schemeFontFiles(*scheme);
	REQUIRE(files.size() == 1);   // a path out of the game folder is ignored
	CHECK(files[0] == "resource/HALFLIFE2.ttf");
	// A font size edit inside an entry block.
	const ResNode* entry = findResBlock(*scheme, {"Scheme", "Fonts", "HudNumbers", "1"});
	REQUIRE(entry);
	const auto edited = parseResFile(withResValue(*scheme, *entry, "tall", "40"));
	REQUIRE(edited);
	CHECK(schemeFonts(*edited)[1].entries[0].tall == 40);
}

TEST_CASE("hud: broken or hostile files are refused") {
	std::string error;
	CHECK_FALSE(parseResFile("a { b", &error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(parseResFile("}", &error));
	CHECK_FALSE(parseResFile("\"unterminated", &error));
	CHECK_FALSE(parseResFile("key", &error));
	std::string deep;
	for (int i = 0; i < 100; ++i) deep += "a {";
	CHECK_FALSE(parseResFile(deep, &error));
	CHECK(parseResFile("").has_value());
	const auto includes = parseResFile("#base \"SourceScheme.res\"\nScheme { }");
	REQUIRE(includes);
	CHECK(includes->includes == std::vector<std::string>{"SourceScheme.res"});
	// Nothing in an empty file.
	CHECK(hudElements(*parseResFile("")).empty());
	CHECK(schemeFonts(*parseResFile("")).empty());
}

TEST_CASE("hud: saving into the mod keeps a backup once and restores exactly") {
	test::Fixture f;
	test::createFixture(f);
	const auto layoutFile = f.mod / "scripts/HudLayout.res";
	const auto schemeFile = f.mod / "resource/ClientScheme.res";
	std::filesystem::remove(layoutFile);
	std::filesystem::remove(schemeFile);
	// The mod has its own scheme, not its own layout.
	test::writeText(schemeFile, "Scheme { }\n");
	CHECK_FALSE(hudFilesChanged(f.mod));
	std::string error;
	REQUIRE(saveHudFile(f.mod, HUD_LAYOUT_FILE, "first", &error));
	REQUIRE(saveHudFile(f.mod, HUD_LAYOUT_FILE, "second", &error));
	REQUIRE(saveHudFile(f.mod, HUD_SCHEME_FILE, "Scheme { Colors { } }\n", &error));
	REQUIRE(saveHudFile(f.mod, HUD_SCHEME_FILE, "Scheme { Fonts { } }\n", &error));
	CHECK(readFile(layoutFile) == "second");
	CHECK(readFile(schemeFile.string() + ".mapsmith-backup") == "Scheme { }\n");   // the mod's own file, once
	CHECK(hudFilesChanged(f.mod));
	// One record for both files.
	int records = 0;
	for (const auto& r : loadImports(f.mod)) records += r.kind == "hud";
	CHECK(records == 1);
	REQUIRE(restoreHudFiles(f.mod, &error));
	CHECK_FALSE(std::filesystem::exists(layoutFile));               // Mapsmith made it: gone
	CHECK(readFile(schemeFile) == "Scheme { }\n");                  // the mod's own: back
	CHECK_FALSE(std::filesystem::exists(schemeFile.string() + ".mapsmith-backup"));
	CHECK_FALSE(hudFilesChanged(f.mod));
	CHECK(restoreHudFiles(f.mod, &error));   // nothing to do
	// Paths out of the mod are refused.
	CHECK_FALSE(saveHudFile(f.mod, "../outside.res", "x", &error));
}
