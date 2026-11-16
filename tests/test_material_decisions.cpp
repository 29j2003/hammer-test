// test_material_decisions.cpp - The mod's decision file (mapsmith_materials.txt): reading, writing
// back without losing anything, wildcards and which line wins, merging an answer from the chat,
// the decisions reaching the building builder and Game enhancements' relief, the depth rule for
// fine bricks, and the export of the materials for review.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/building_builder.h>
#include <sme/facade_texture.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/material_decisions.h>
#include <sme/material_review.h>
#include <sme/relief_pattern.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;

namespace {

std::string readAll(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

template <typename T>
void put(std::vector<std::byte>& out, std::size_t at, T value) {
	std::memcpy(out.data() + at, &value, sizeof value);
}

/// An uncompressed RGB888 .vtf of a brick wall: `courses` courses of 4 bricks in a running bond.
std::vector<std::byte> brickVtf(unsigned size, int courses = 8, bool bricks = true) {
	std::vector<std::byte> out(80 + std::size_t(size) * size * 3, std::byte(0));
	std::memcpy(out.data(), "VTF", 4);
	put<std::int32_t>(out, 4, 7);
	put<std::int32_t>(out, 8, 2);
	put<std::int32_t>(out, 12, 80);
	put<std::uint16_t>(out, 16, std::uint16_t(size));
	put<std::uint16_t>(out, 18, std::uint16_t(size));
	put<std::uint16_t>(out, 24, 1);
	put<float>(out, 48, 1.f);
	put<std::int32_t>(out, 52, 2);   // IMAGE_FORMAT_RGB888
	out[56] = std::byte(1);
	put<std::int32_t>(out, 57, -1);
	put<std::uint16_t>(out, 63, 1);
	std::uint32_t seed = 11;
	const auto random = [&seed] {
		seed = seed * 1664525u + 1013904223u;
		return double(seed >> 8) / double(1u << 24);
	};
	std::vector<double> tones(1024);
	for (auto& tone : tones) tone = 0.25 + 0.45 * random();
	const double course = double(size) / courses, unit = course * 2;
	const int perCourse = int(std::lround(size / unit));
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			double tone = 0.5 + (random() - 0.5) * 0.5;
			if (bricks) {
				const int row = int(y / course);
				const double offset = row % 2 ? unit * 0.5 : 0.0;
				const double u = std::fmod(x + 0.5 + offset, unit), v = std::fmod(y + 0.5, course);
				const double joint = std::max(1.0, course * 0.12);
				const bool isJoint = v < joint || v > course - joint || u < joint || u > unit - joint;
				const int brick = row * perCourse + int(std::fmod(x + offset, double(size)) / unit);
				tone = (isJoint ? 0.82 : tones[std::size_t(brick) % tones.size()]) + (random() - 0.5) * 0.12;
			}
			const auto byte = [](double c) { return std::byte(static_cast<unsigned char>(std::clamp(c, 0.0, 1.0) * 255)); };
			auto* p = out.data() + 80 + (std::size_t(y) * size + x) * 3;
			p[0] = byte(tone * 1.15);
			p[1] = byte(tone * 0.8);
			p[2] = byte(tone * 0.7);
		}
	}
	return out;
}

void writeBytes(const std::filesystem::path& file, const std::vector<std::byte>& bytes) {
	std::filesystem::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

ImageRgba decoded(const std::vector<std::byte>& vtf) {
	auto image = decodeTexture(vtf);
	REQUIRE(image);
	return *image;
}

} // namespace

TEST_CASE("material decisions: lines are read, written back unchanged, and bad values are reported with their line") {
	const std::string text =
		"// Julius' mod: decided in the chat\n"
		"building_template/building_template007a  role=facade windows=2x1 floors=1\n"
		"BRICK/BrickWall031d.vmt  role=wall parallax=on depth=0.008 pattern=brick   // the red one\n"
		"\n"
		"materials/brick/brickwall047a  parallax=off  shine=low\n"
		"concrete/concretefloor001a  role=ground parallax=off note=\"street, keep flat\"\n"
		"brick/*  parallax=off\n"
		"plaster/wall01  role=castle depth=0.5 windows=2 floors=x scale=-1 parallax=maybe pattern=marble\n"
		"role=wall\n"
		"broken/quote  note=\"no end\n";
	const auto d = MaterialDecisions::parse(text);
	// Unchanged it is the same text again (comments, spacing, case and all).
	CHECK(d.serialize() == text);
	CHECK(d.entryCount() == 7);
	// Every bad value with its line: unknown role, depth, windows, floors, scale, parallax, pattern; a line
	// without a name; an open quote.
	const auto& problems = d.problems();
	CHECK(problems.size() == 9);
	CHECK(std::count_if(problems.begin(), problems.end(), [](const DecisionProblem& p) { return p.line == 8; }) == 7);
	CHECK(std::any_of(problems.begin(), problems.end(), [](const DecisionProblem& p) { return p.line == 9 && p.message.find("material name") != std::string::npos; }));
	CHECK(std::any_of(problems.begin(), problems.end(), [](const DecisionProblem& p) { return p.line == 10 && p.message.find("quote") != std::string::npos; }));

	const auto facade = d.lookup("BUILDING_TEMPLATE/building_template007a");
	REQUIRE(facade.role);
	CHECK((*facade.role == "facade"));
	CHECK((facade.windowColumns == 2));
	CHECK((facade.windowRows == 1));
	CHECK((facade.floors == 1));
	CHECK_FALSE(facade.parallax);
	// The exact line beats the wildcard per key: brick/* says off, the red brick says on.
	const auto red = d.lookup("brick/brickwall031d");
	CHECK((red.parallax == ParallaxChoice::On));
	CHECK((red.depth == doctest::Approx(0.008)));
	CHECK((*red.pattern == "brick"));
	CHECK((d.lookup("brick/brickwall099z").parallax == ParallaxChoice::Off));   // only the wildcard
	CHECK(d.lookup("concrete/concretefloor001a").note == std::string("street, keep flat"));
	const auto old = d.lookup("brick/brickwall047a");
	CHECK((old.parallax == ParallaxChoice::Off));
	REQUIRE(old.extra.size() == 1);
	CHECK(old.extra[0] == std::pair<std::string, std::string>{"shine", "low"});
	CHECK(d.lookup("nature/grass").empty());
	CHECK(d.lookup("plaster/wall01").empty());   // every value of its line was bad

	// A more specific wildcard wins over a general one; a later line over an earlier one of the same weight.
	const auto w = MaterialDecisions::parse("*  parallax=off depth=0.002\nbrick/*  parallax=on\nbrick/*  depth=0.004\n");
	CHECK((w.lookup("brick/a").parallax == ParallaxChoice::On));
	CHECK((w.lookup("brick/a").depth == doctest::Approx(0.004)));
	CHECK((w.lookup("stone/a").parallax == ParallaxChoice::Off));
	CHECK(decisionPatternMatches("brick/wall??a", "brick/wall01a"));
	CHECK_FALSE(decisionPatternMatches("brick/wall??a", "brick/wall1a"));
	CHECK(decisionPatternMatches("*/wall*", "brick/old/wall9"));
}

TEST_CASE("material decisions: editing a line keeps everything else, unknown keys and comments; an emptied line goes") {
	auto d = MaterialDecisions::parse("// keep me\nbrick/a  parallax=on custom=\"x y\"  // red\nbrick/b  role=wall\n");
	d.edit("BRICK/A", [](MaterialDecision& m) { m.depth = 0.005; });
	d.edit("stone/new", [](MaterialDecision& m) { m.parallax = ParallaxChoice::Off; });
	d.edit("brick/b", [](MaterialDecision& m) { m.role.reset(); });
	CHECK(d.serialize() == "// keep me\nbrick/a  parallax=on depth=0.005 custom=\"x y\"  // red\nstone/new  parallax=off\n");
	// Read back: the same.
	CHECK(MaterialDecisions::parse(d.serialize()).serialize() == d.serialize());
	REQUIRE(d.exact("stone/new"));
	CHECK(d.exact("stone/new")->parallax == ParallaxChoice::Off);
}

TEST_CASE("material decisions: an answer from the chat is merged in and the preview lists what changes") {
	const auto before = MaterialDecisions::parse("brick/a  parallax=on depth=0.005\nbrick/b  role=wall\n");
	auto after = before;
	after.merge(MaterialDecisions::parse("// from the chat\nbrick/a  parallax=off\nconcrete/c  role=ground parallax=off\n"));
	CHECK((after.lookup("brick/a").parallax == ParallaxChoice::Off));
	CHECK((after.lookup("brick/a").depth == doctest::Approx(0.005)));   // what the answer did not say stays
	CHECK((after.lookup("brick/b").role == std::string("wall")));
	CHECK(after.serialize().find("from the chat") == std::string::npos);
	const auto changes = compareDecisions(before, after);
	REQUIRE(changes.size() == 2);
	CHECK((changes[0].pattern == "brick/a"));
	CHECK(changes[0].before == "parallax=on depth=0.005");
	CHECK(changes[0].after == "parallax=off depth=0.005");
	CHECK((changes[1].pattern == "concrete/c"));
	CHECK(changes[1].before.empty());

	// The file in a mod: saved, read back; empty again: it goes.
	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "decisions/mymod";
	std::filesystem::create_directories(mod);
	CHECK(loadMaterialDecisions(mod).entryCount() == 0);
	REQUIRE(saveMaterialDecisions(mod, after));
	CHECK(readAll(materialDecisionsFile(mod)) == after.serialize());
	CHECK(loadMaterialDecisions(mod).serialize() == after.serialize());
	auto emptied = loadMaterialDecisions(mod);
	for (const char* name : {"brick/a", "brick/b", "concrete/c"}) emptied.edit(name, [](MaterialDecision& m) { m = {}; });
	REQUIRE(saveMaterialDecisions(mod, emptied));
	CHECK_FALSE(std::filesystem::exists(materialDecisionsFile(mod)));
}

TEST_CASE("material decisions: role, windows, floors and scale override what the picture says (building builder)") {
	TextureAnalysis plain;
	plain.role = TextureRole::Wall;
	plain.width = plain.height = 512;
	MaterialDecision d;
	d.role = "facade";
	d.windowColumns = 2;
	d.windowRows = 1;
	d.floors = 2;
	d.scale = 0.5;
	TextureAnalysis a = plain;
	applyMaterialDecision(a, d);
	CHECK((a.role == TextureRole::FacadeWindows));
	CHECK(a.decided);
	CHECK(a.hasWindows);
	CHECK(a.columns == 2);
	CHECK(a.floorsPerTile() == 2);
	CHECK(a.windows.size() == 2);   // a regular grid when no exact windows are given
	CHECK(a.scaleFor(128) == doctest::Approx(0.5));
	CHECK(a.describe().find("set by the decision file") != std::string::npos);
	// The building builder takes it: windows from the texture, the decided tile.
	BuildingOptions options = defaultBuildingOptions();
	applyFacadeFacts(options, a);
	CHECK(options.windowsFromTexture);
	// Exact windows win over the grid; a wall decision takes found windows away; nothing set changes nothing.
	MaterialDecision exactWindows;
	exactWindows.role = "facade";
	exactWindows.windowRects = std::vector<FacadeWindowRect>{{0.1, 0.2, 0.4, 0.7}};
	TextureAnalysis b = plain;
	applyMaterialDecision(b, exactWindows);
	REQUIRE(b.windows.size() == 1);
	CHECK(b.windows[0].u1 == doctest::Approx(0.4));
	TextureAnalysis c = a;
	MaterialDecision wall;
	wall.role = "wall";
	applyMaterialDecision(c, wall);
	CHECK((c.role == TextureRole::Wall));
	CHECK_FALSE(c.hasWindows);
	TextureAnalysis untouched = plain;
	applyMaterialDecision(untouched, MaterialDecision{});
	CHECK_FALSE(untouched.decided);
	CHECK(decisionTextureRole("ground") == TextureRole::Unsuitable);
	CHECK(decisionTextureRole("interior") == TextureRole::Wall);
}

TEST_CASE("relief depth: small bricks get a shallower relief, too fine ones none, big ones the usual depth") {
	const auto layoutOf = [](unsigned size, int courses) {
		const ReliefLayout layout = detectReliefLayout(decoded(brickVtf(size, courses)));
		REQUIRE(layout.found());
		return layout;
	};
	// 8 courses on 256 texels: big bricks, the usual depth.
	const auto big = reliefDepthForLayout(layoutOf(256, 8), 0.006, 256);
	CHECK(big.on);
	CHECK((big.depth == doctest::Approx(0.006)));
	// 32 courses: 8 texels a course - shallower (0.08 x 1/32 = 0.0025).
	const auto small = reliefDepthForLayout(layoutOf(256, 32), 0.006, 256);
	CHECK(small.on);
	CHECK((small.depth == doctest::Approx(0.0025).epsilon(0.1)));
	CHECK(small.reason.find("shallower") != std::string::npos);
	// The same pattern on a 1024 texture (the analysis sees 256): still shallower by the part of the texture.
	const auto smallBig = reliefDepthForLayout(layoutOf(256, 32), 0.006, 1024);
	CHECK(smallBig.on);
	CHECK((smallBig.depth == doctest::Approx(small.depth)));
	// 64 courses: under the minimum depth - off.
	ReliefLayout fine = layoutOf(256, 32);
	fine.courseHeight /= 2;   // as if twice as many courses (the generator gets too fine to detect at 256)
	const auto tooFine = reliefDepthForLayout(fine, 0.006, 1024);
	CHECK_FALSE(tooFine.on);
	CHECK(tooFine.reason.find("too fine") != std::string::npos);
	// Courses only 5 texels high in the full texture: off too.
	const auto tiny = reliefDepthForLayout(layoutOf(256, 8), 0.006, 40);
	CHECK_FALSE(tiny.on);
	// No layout: the base depth stays.
	CHECK((reliefDepthForLayout(ReliefLayout{}, 0.006).depth == doctest::Approx(0.006)));
}

TEST_CASE("game enhancements: the decision file switches relief off, on (outside the maps too) and sets its depth; fine bricks get less") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "decided/hl2";
	const auto mod = f.root / "decided/mymod";
	const auto wall = [&](const std::string& name, const std::string& texture) {
		test::writeText(game / "materials" / (name + ".vmt"), "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"" + texture + "\"\n}\n");
	};
	wall("brick/brickwall001a", "brick/brickwall001a");    // a deploy candidate (brick)
	wall("brick/smallbrick01", "brick/smallbrick01");      // a deploy candidate with small bricks
	wall("concrete/plain", "concrete/plain");              // no pattern, not used in a map
	wall("plaster/wall01", "plaster/wall01");              // a name deploy never chooses
	writeBytes(game / "materials/brick/brickwall001a.vtf", brickVtf(128, 8));
	writeBytes(game / "materials/brick/smallbrick01.vtf", brickVtf(256, 32));
	writeBytes(game / "materials/concrete/plain.vtf", brickVtf(128, 8, false));
	writeBytes(game / "materials/plaster/wall01.vtf", brickVtf(128, 8));
	std::filesystem::create_directories(mod / "materials");
	test::writeText(mod / "bin/game_shader_dx6.dll", "dll");
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});
	const auto apply = [&](const EnhancementPlan& plan) {
		for (const auto& change : plan.changes) {
			if (change.remove) {
				std::filesystem::remove(change.file);
				fs.noteRemovedFile(change.file);
			} else {
				std::filesystem::create_directories(change.file.parent_path());
				test::writeText(change.file, change.text);
				fs.noteWrittenFile(change.file);
			}
			if (!change.heightFile.empty()) {
				if (!change.heightVtf.empty() && !change.remove) {
					writeBytes(change.heightFile, change.heightVtf);
					fs.noteWrittenFile(change.heightFile);
				} else {
					std::filesystem::remove(change.heightFile);
					fs.noteRemovedFile(change.heightFile);
				}
			}
		}
	};
	const auto fileOf = [&](const std::string& material) { return readAll(mod / "materials" / (material + ".vmt")); };
	EffectCandidates only{{GameEffect::Relief, {"brick/brickwall001a", "brick/smallbrick01"}}};
	const std::set<std::string> mapMaterials = {"brick/brickwall001a", "brick/smallbrick01"};
	EnhancementOptions options;
	options.mapMaterials = &mapMaterials;

	// Without decisions: the big bricks get the usual 0.006, the small ones a shallower relief.
	apply(planEnhancements(fs, mod, {}, {}, &only, options));
	CHECK(fileOf("brick/brickwall001a").find("\"$parallaxdepth\" \"0.006\"") != std::string::npos);
	const std::string small = fileOf("brick/smallbrick01");
	CHECK(small.starts_with(std::string(PATTERN_RELIEF_MARKER)));
	CHECK(small.find("\"$parallaxdepth\" \"0.0025\"") != std::string::npos);
	CHECK(small.find("small bricks or tiles swim") != std::string::npos);

	// The decision file: big bricks off, the small ones deeper, plaster (never a candidate, not in a map) on.
	auto decisions = MaterialDecisions::parse("brick/brickwall001a  parallax=off\nbrick/smallbrick01  depth=0.004\nplaster/wall01  parallax=on\n");
	options.decisions = &decisions;
	auto plan = planEnhancements(fs, mod, {}, {}, &only, options);
	apply(plan);
	CHECK(fileOf("brick/brickwall001a").starts_with(std::string(MAPSMITH_MARKER) + " off (relief)"));
	CHECK(fileOf("brick/brickwall001a").find("parallax=off") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/brickwall001a_mapsmith_relief.vtf"));
	CHECK(fileOf("brick/smallbrick01").find("\"$parallaxdepth\" \"0.004\"") != std::string::npos);
	const std::string plaster = fileOf("plaster/wall01");
	CHECK(plaster.starts_with(std::string(PATTERN_RELIEF_MARKER)));
	CHECK(std::filesystem::exists(mod / "materials/plaster/wall01_mapsmith_relief.vtf"));
	// Again: nothing to do.
	CHECK(planEnhancements(fs, mod, {}, {}, &only, options).changes.empty());
	// parallax=on for a texture without any pattern: from the brightness.
	decisions = MaterialDecisions::parse("concrete/plain  parallax=on depth=0.003\n");
	apply(planEnhancements(fs, mod, {}, {}, &only, options));
	const std::string concrete = fileOf("concrete/plain");
	CHECK(concrete.find("from the brightness") != std::string::npos);
	CHECK(concrete.find("\"$parallaxdepth\" \"0.003\"") != std::string::npos);
	// The decision gone: the plaster's relief goes again, the bricks get their automatic relief back.
	decisions = MaterialDecisions{};
	apply(planEnhancements(fs, mod, {}, {}, &only, options));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/plaster/wall01.vmt"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/plain.vmt"));
	CHECK(fileOf("brick/brickwall001a").find("\"$parallaxdepth\" \"0.006\"") != std::string::npos);
	// A wildcard off wins for every brick (no exact line says otherwise).
	decisions = MaterialDecisions::parse("brick/*  parallax=off\n");
	apply(planEnhancements(fs, mod, {}, {}, &only, options));
	CHECK(fileOf("brick/brickwall001a").starts_with(std::string(MAPSMITH_MARKER) + " off (relief)"));
	CHECK(fileOf("brick/smallbrick01").starts_with(std::string(MAPSMITH_MARKER) + " off (relief)"));
}

TEST_CASE("material review: the list, the facts per material, the index, the sheets and the parts") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "review/hl2";
	const auto mod = f.root / "review/mymod";
	const auto material = [&](const std::string& name, const std::string& body) { test::writeText(game / "materials" / (name + ".vmt"), body); };
	material("brick/brickwall001a", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/brickwall001a\"\n\t\"$bumpmap\" \"brick/brickwall001a_normal\"\n\t\"$surfaceprop\" \"brick\"\n}\n");
	material("concrete/floor01", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"concrete/floor01\"\n\t\"$envmap\" \"env_cubemap\"\n}\n");
	material("nature/blend01", "\"WorldVertexTransition\"\n{\n\t\"$basetexture\" \"concrete/floor01\"\n\t\"$basetexture2\" \"brick/brickwall001a\"\n}\n");
	material("tools/toolsnodraw", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsnodraw\"\n\t\"%compilenodraw\" \"1\"\n}\n");
	material("decals/scorch1", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"decals/scorch1\"\n\t\"$decal\" \"1\"\n}\n");
	material("models/props/crate", "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props/crate\"\n}\n");
	writeBytes(game / "materials/brick/brickwall001a.vtf", brickVtf(256, 8));
	writeBytes(game / "materials/brick/brickwall001a_normal.vtf", brickVtf(64, 8, false));
	writeBytes(game / "materials/concrete/floor01.vtf", brickVtf(128, 8, false));
	std::filesystem::create_directories(mod);
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	// The maps' materials (tools left out unless asked), every world material, one folder.
	const std::set<std::string> maps = {"CONCRETE/FLOOR01", "brick/brickwall001a", "tools/toolsnodraw"};
	CHECK(reviewMaterialList(fs, {}, maps) == std::vector<std::string>{"brick/brickwall001a", "concrete/floor01"});
	ReviewScope all;
	all.kind = ReviewScope::Kind::AllWorld;
	CHECK(reviewMaterialList(fs, all, {}) == std::vector<std::string>{"brick/brickwall001a", "concrete/floor01", "nature/blend01"});
	all.tools = all.decals = true;
	CHECK(reviewMaterialList(fs, all, {}).size() == 5);   // still no model material
	ReviewScope folder;
	folder.kind = ReviewScope::Kind::Folder;
	folder.folder = "Brick/";
	CHECK(reviewMaterialList(fs, folder, {}) == std::vector<std::string>{"brick/brickwall001a"});

	// The facts of one material, with the decision file applied.
	const auto decisions = MaterialDecisions::parse("brick/brickwall001a  role=wall parallax=off\n");
	auto brick = reviewEntry(fs, "BRICK/BRICKWALL001A", &decisions, 128);
	CHECK(brick.error.empty());
	CHECK(brick.shader == "LightmappedGeneric");
	CHECK(brick.width == 256);
	CHECK(brick.surfaceprop == "brick");
	CHECK(std::find(brick.flags.begin(), brick.flags.end(), "bump") != brick.flags.end());
	CHECK(brick.base.width >= 128);
	CHECK(brick.bump.width > 0);
	CHECK(brick.reliefPattern.find("running bond") != std::string::npos);
	CHECK(brick.courseTexels == doctest::Approx(32).epsilon(0.1));
	CHECK(brick.decision == "role=wall parallax=off");
	CHECK((brick.analysis.role == TextureRole::Wall));
	CHECK(brick.parallaxNow == "off");
	CHECK(std::find(brick.effects.begin(), brick.effects.end(), "relief") != brick.effects.end());
	auto floor = reviewEntry(fs, "concrete/floor01", nullptr, 128);
	CHECK(reviewFlagsText(floor) == "envmap");
	CHECK(floor.reliefPattern == "none");
	auto missing = reviewEntry(fs, "nothing/here", nullptr);
	CHECK_FALSE(missing.error.empty());
	brick.number = 1;
	floor.number = 2;
	missing.number = 3;
	const std::vector<ReviewEntry> entries = {brick, floor, missing};

	const std::string csv = reviewIndexCsv(entries);
	CHECK(std::count(csv.begin(), csv.end(), '\n') == 4);
	CHECK(csv.starts_with("number,name,shader,width,height,surfaceprop,flags,role,"));
	CHECK(csv.find("1,brick/brickwall001a,LightmappedGeneric,256,256,brick,bump,wall,") != std::string::npos);
	CHECK(csv.find("role=wall parallax=off") != std::string::npos);
	const std::string json = reviewIndexJson(entries);
	CHECK(json.find("\"name\": \"brick/brickwall001a\"") != std::string::npos);
	CHECK(json.find("\"decision\": \"role=wall parallax=off\"") != std::string::npos);
	CHECK(std::count(json.begin(), json.end(), '{') == 3);
	const std::string readme = reviewReadme(3, 1, 1, "My Mod");
	CHECK(readme.find("3 materials of My Mod on 1 contact sheet, in 1 part.") != std::string::npos);
	CHECK(readme.find("parallax=on depth=0.008") != std::string::npos);

	// Sheets of 12, parts under the limit (the index rides in every part).
	const auto sheets = reviewSheets(30);
	REQUIRE(sheets.size() == 3);
	CHECK(sheets[2].size() == 6);
	CHECK(sheets[1].front() == 12);
	CHECK(reviewSheetName(0) == "sheet_001.jpg");
	const auto parts = reviewParts({600, 600, 600, 2000, 100}, 1500, 200);
	REQUIRE(parts.size() == 4);
	CHECK(parts[0] == std::vector<std::size_t>{0, 1});
	CHECK(parts[1] == std::vector<std::size_t>{2});
	CHECK(parts[2] == std::vector<std::size_t>{3});   // too big for a part: on its own
	CHECK(parts[3] == std::vector<std::size_t>{4});
	CHECK(reviewParts({}, 100).empty());
}
