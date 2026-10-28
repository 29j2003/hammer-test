// test_fidelity_effects.cpp - Game enhancements: the materials match what mapbase-fidelity's
// deploy.ps1 writes (liquid, wind, water), on by default and off for the mod or one model; the
// settings file.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;

namespace {

std::string crlf(std::string_view lines) {
	std::string out;
	for (const char c : lines) {
		if (c == '\n') out += "\r\n";
		else out += c;
	}
	return out;
}

std::string readAll(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

const std::string DEPLOY(DEPLOY_MARKER);

} // namespace

TEST_CASE("game enhancements: materials as deploy.ps1 writes them") {
	// The open green bottle: see-through glass, a rest of liquid that pours out.
	const std::string bottle = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_junk/glassbottle01a\"\n\t\"$translucent\" 1\n}\n";
	const std::string bottlePath = "materials/models/props_junk/glassbottle01a.vmt";
	CHECK(deployWouldApply(GameEffect::Liquid, bottlePath, bottle));
	const auto liquid = makeEffectMaterial(GameEffect::Liquid, bottlePath, bottle, containerKind(bottlePath), DEPLOY_MARKER);
	REQUIRE(liquid);
	CHECK(*liquid == crlf(DEPLOY + " (liquid) from materials/models/props_junk/glassbottle01a.vmt (delete this file to keep the original)\n"
	                               "\"Fidelity_Liquid\"\n{\n"
	                               "\t\"$basetexture\" \"models/props_junk/glassbottle01a\"\n"
	                               "\t\"$liquidplane\" \"[0 0 1 -100000]\"\n"
	                               "\t\"$bubbles\" \"[0.35 0.3 4 0.2]\"\n\t\"$splash\" \"1\"\n"
	                               "\t\"$fillmin\" \"0.02\"\n\t\"$fillmax\" \"0.25\"\n\t\"$spill\" \"1\"\n"
	                               "\t\"Proxies\"\n\t{\n\t\t\"FidelityLiquid\" { }\n\t}\n}\n"));
	CHECK(effectIsOn(GameEffect::Liquid, *liquid));
	CHECK(isGeneratedMaterial(*liquid));

	// A health vial: opaque texture (labels stay solid), only its green parts hold liquid.
	const std::string vial = "// a comment\n\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/items/healthvial\"\n}\n";
	const std::string vialPath = "materials/models/items/healthvial.vmt";
	CHECK(containerKind(vialPath) == ContainerKind::Medical);
	const auto medical = makeEffectMaterial(GameEffect::Liquid, vialPath, vial, containerKind(vialPath), DEPLOY_MARKER);
	REQUIRE(medical);
	CHECK(*medical == crlf(DEPLOY + " (liquid) from materials/models/items/healthvial.vmt (delete this file to keep the original)\n"
	                                "\"Fidelity_Liquid\"\n{\n"
	                                "\t\"$basetexture\" \"models/items/healthvial\"\n"
	                                "\t\"$labelthreshold\" \"2\"\n"
	                                "\t\"$liquidplane\" \"[0 0 1 -100000]\"\n"
	                                "\t\"$liquidkey\" \"[0.3 0.8 0.15]\"\n\t\"$fillmin\" \"0.82\"\n\t\"$fillmax\" \"0.95\"\n"
	                                "\t\"$bubbles\" \"[0.5 1.1 0.5 1]\"\n\t\"$viscosity\" \"0.7\"\n\t\"$liquidalpha\" \"0.95\"\n"
	                                "\t\"Proxies\"\n\t{\n\t\t\"FidelityLiquid\" { }\n\t}\n}\n"));
	// Dark glass (texture brightness below 0.15) and a liquid colour from the name.
	const float dark[3] = {0.05f, 0.04f, 0.03f};
	CHECK(containerKind("materials/models/props_junk/garbage_glassbottle003a.vmt", dark) == ContainerKind::Tinted);
	const auto wine = makeEffectMaterial(GameEffect::Liquid, "materials/models/props/winebottle.vmt", bottle, ContainerKind::Tinted);
	REQUIRE(wine);
	CHECK(wine->find("\"$liquidcolor\" \"[0.32 0.03 0.07]\"") != std::string::npos);
	CHECK(wine->find("\"$tintedglass\" \"0.8\"") != std::string::npos);
	CHECK(wine->starts_with(DEPLOY + " (liquid) from materials/models/props/winebottle.vmt (delete this file"));
	// Not for brushes or other shaders; any model material can get it by the mapper's switch.
	CHECK_FALSE(deployWouldApply(GameEffect::Liquid, "materials/models/props_c17/chair.vmt", bottle));
	CHECK(effectApplies(GameEffect::Liquid, "materials/models/props_c17/chair.vmt", bottle));
	CHECK_FALSE(effectApplies(GameEffect::Liquid, "materials/concrete/wall.vmt", "\"LightmappedGeneric\"\n{\n}\n"));

	// A bush sways whole.
	const std::string bush = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_foliage/bush2\"\n\t\"$alphatest\" \"1\"\n}\n";
	const std::string bushPath = "materials/models/props_foliage/bush2.vmt";
	CHECK(deployWouldApply(GameEffect::Wind, bushPath, bush));
	const auto wind = makeEffectMaterial(GameEffect::Wind, bushPath, bush, ContainerKind::Drink, DEPLOY_MARKER);
	REQUIRE(wind);
	CHECK(*wind == DEPLOY + " (wind) from materials/models/props_foliage/bush2.vmt (delete this file to keep the original)\r\n"
	                        "\"SDK_VertexLitGeneric\"\n{\r\n"
	                        "\t\"$treesway\" \"1\"\r\n\t\"$treeswayheight\" \"80\"\r\n\t\"$treeswaystartheight\" \"0.1\"\r\n"
	                        "\t\"$treeswayradius\" \"60\"\r\n\t\"$treeswaystartradius\" \"0.1\"\r\n\t\"$treeswayspeed\" \"1.5\"\r\n"
	                        "\t\"$treeswaystrength\" \"2\"\r\n\t\"$treeswayscrumblestrength\" \"1\"\r\n\t\"$treeswayscrumblespeed\" \"3\"\r\n"
	                        "\t\"$treeswayscrumblefrequency\" \"12\""
	                        "\n\t\"$basetexture\" \"models/props_foliage/bush2\"\n\t\"$alphatest\" \"1\"\n}\n");
	CHECK(effectIsOn(GameEffect::Wind, *wind));
	CHECK_FALSE(makeEffectMaterial(GameEffect::Wind, bushPath, *wind));   // already swaying

	// A tree: only its see-through leaves, with the tree values; the bark stays still.
	const std::string needles = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_foliage/pine_needles\"\n\t\"$alphatest\" 1\n}\n";
	const auto leaves = makeEffectMaterial(GameEffect::Wind, "materials/models/props_foliage/tree_pine_needles.vmt", needles);
	REQUIRE(leaves);
	CHECK(leaves->find("\"$treeswayheight\" \"100000\"") != std::string::npos);
	CHECK(leaves->find("\"$treeswayscrumblestrength\" \"0.6\"") != std::string::npos);
	const std::string bark = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_foliage/pine_bark\"\n}\n";
	CHECK_FALSE(deployWouldApply(GameEffect::Wind, "materials/models/props_foliage/tree_pine_bark.vmt", bark));
	CHECK_FALSE(makeEffectMaterial(GameEffect::Wind, "materials/models/props_foliage/tree_pine_bark.vmt", bark));
	// Any other model the mapper picks sways whole (bush values).
	const auto barrel = makeEffectMaterial(GameEffect::Wind, "materials/models/props_c17/oildrum.vmt", bark);
	REQUIRE(barrel);
	CHECK(barrel->find("\"$treeswayheight\" \"80\"") != std::string::npos);

	// Water: the HDR block's values replace the ones above it.
	const std::string water = "\"Water\"\n{\n\t\"$abovewater\" 1\n\t\"$fogcolor\" \"{10 20 30}\"\n\t\"$bumpframe\" 0\n"
	                          "\t\"Water_DX9_HDR\"\n\t{\n\t\t\"$fogcolor\" \"{1 2 3}\"\n\t}\n}\n";
	const auto glint = makeEffectMaterial(GameEffect::Water, "materials/nature/water.vmt", water, ContainerKind::Drink, DEPLOY_MARKER);
	REQUIRE(glint);
	CHECK(*glint == DEPLOY + " (water) from materials/nature/water.vmt (delete this file to keep the original)\r\n"
	                         "\"Water_Fidelity\"\n{\r\n\t\"$fogcolor\" \"{1 2 3}\"\n\t\"$abovewater\" 1\n\t\"$bumpframe\" 0\n}\n");
	CHECK_FALSE(makeEffectMaterial(GameEffect::Water, "materials/nature/under.vmt", "\"Water\"\n{\n\t\"$abovewater\" 0\n}\n"));

	// Taking an effect out of a hand-made material keeps the rest.
	const auto plain = withoutEffect(GameEffect::Liquid, *liquid);
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, plain));
	CHECK(plain.find("FidelityLiquid") == std::string::npos);
	CHECK(plain.find("models/props_junk/glassbottle01a") != std::string::npos);
	CHECK_FALSE(effectIsOn(GameEffect::Wind, withoutEffect(GameEffect::Wind, *wind)));

	// The reflectivity from a .vtf header.
	std::string vtf(64, '\0');
	vtf.replace(0, 4, std::string("VTF\0", 4));
	const float colour[3] = {0.25f, 0.5f, 0.75f};
	std::memcpy(vtf.data() + 32, colour, 12);
	const auto r = vtfReflectivity(vtf);
	REQUIRE(r);
	CHECK((*r)[1] == doctest::Approx(0.5f));
	CHECK_FALSE(vtfReflectivity("not a texture"));
}

TEST_CASE("game enhancements: on by default, off for the mod or one model") {
	test::Fixture f;
	test::createFixture(f);   // a folder of its own, removed afterwards
	const auto game = f.root / "effects/hl2";
	const auto mod = f.root / "effects/mymod";
	test::writeText(game / "materials/models/props_junk/glassbottle01a.vmt",
	                "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_junk/glassbottle01a\"\n\t\"$translucent\" 1\n}\n");
	test::writeText(game / "materials/models/props_junk/garbage_milkbottle.vmt",
	                "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_junk/garbage_milkbottle\"\n}\n");
	test::writeText(game / "materials/models/props_c17/chair.vmt", "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_c17/chair\"\n}\n");
	test::writeText(game / "materials/nature/water_canals.vmt", "\"Water\"\n{\n\t\"$abovewater\" 1\n\t\"$normalmap\" \"nature/water\"\n}\n");
	test::writeStudioModel(game / "models/props_junk/glassbottle01a.mdl",
	                       test::makeStudioModel({{-4, -4, 0, 4, 4, 12}}, {"idle"}, "", "glassbottle01a", "models/props_junk/"));
	std::filesystem::create_directories(mod / "materials");
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
		}
	};
	const std::vector<std::string> bottles = {"models/props_junk/glassbottle01a", "models/props_junk/garbage_milkbottle"};
	const auto bottleFile = mod / "materials/models/props_junk/glassbottle01a.vmt";
	CHECK(effectCandidates(fs, mod, GameEffect::Liquid).size() == 2);   // not the chair
	CHECK(effectCandidates(fs, mod, GameEffect::Water) == std::vector<std::string>{"nature/water_canals"});
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).on == 0);
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).applicable == 2);

	// On by default: everything that qualifies gets its material, as deploy.bat writes it.
	EnhancementSettings settings;
	auto plan = planEnhancements(fs, mod, settings);
	CHECK(plan.changes.size() == 3);
	CHECK(plan.activeCount() == 3);
	CHECK(plan.active[GameEffect::Liquid] == 2);
	apply(plan);
	CHECK(readAll(bottleFile).starts_with(DEPLOY + " (liquid) from materials/models/props_junk/glassbottle01a.vmt"));
	CHECK(effectIsOn(GameEffect::Water, *fs.readText("materials/nature/water_canals.vmt")));
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).on == 2);
	CHECK(planEnhancements(fs, mod, settings).changes.empty());   // nothing left to do

	// One model left out: its material goes back to the original (a copy deploy.bat leaves alone).
	CHECK(modelMaterialNames(fs, "models/props_junk/glassbottle01a.mdl") == std::vector<std::string>{"models/props_junk/glassbottle01a"});
	settings.setExcluded(GameEffect::Liquid, "models/props_junk/GlassBottle01a.mdl", true);
	CHECK(settings.isExcluded(GameEffect::Liquid, "models/props_junk/glassbottle01a.mdl"));
	plan = planEnhancements(fs, mod, settings, excludedMaterials(fs, settings));
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].material == "models/props_junk/glassbottle01a");
	apply(plan);
	CHECK(readAll(bottleFile).starts_with(std::string(MAPSMITH_MARKER) + " off (liquid) from materials/models/props_junk/glassbottle01a.vmt"));
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, *fs.readText("materials/models/props_junk/glassbottle01a.vmt")));
	CHECK(effectIsOn(GameEffect::Liquid, *fs.readText("materials/models/props_junk/garbage_milkbottle.vmt")));
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).on == 1);
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).applicable == 2);   // the original still counts

	// Off for the whole mod: every material of the effect, the others stay.
	settings.off.insert(GameEffect::Liquid);
	plan = planEnhancements(fs, mod, settings, excludedMaterials(fs, settings));
	CHECK(plan.changes.size() == 1);   // the milk bottle; the left-out one is off already
	apply(plan);
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).on == 0);
	CHECK(effectIsOn(GameEffect::Water, *fs.readText("materials/nature/water_canals.vmt")));

	// On again (and the model back in the list): from the original, not from the "off" copy.
	settings.off.erase(GameEffect::Liquid);
	settings.setExcluded(GameEffect::Liquid, "models/props_junk/glassbottle01a.mdl", false);
	CHECK(settings.excluded.empty());
	apply(planEnhancements(fs, mod, settings, excludedMaterials(fs, settings)));
	CHECK(countEffect(fs, mod, GameEffect::Liquid, bottles).on == 2);
	CHECK(readAll(bottleFile).starts_with(DEPLOY + " (liquid)"));

	// A material the mapper wrote by hand is never touched, only reported.
	const std::string handmade = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_junk/my_milk\"\n}\n";
	const auto milkFile = mod / "materials/models/props_junk/garbage_milkbottle.vmt";
	test::writeText(milkFile, handmade);
	fs.noteWrittenFile(milkFile);
	settings.off.insert(GameEffect::Liquid);
	plan = planEnhancements(fs, mod, settings);
	CHECK(plan.keptOwn == std::vector<std::string>{"models/props_junk/garbage_milkbottle"});
	for (const auto& change : plan.changes) CHECK(change.material != "models/props_junk/garbage_milkbottle");
	apply(plan);
	CHECK(readAll(milkFile) == handmade);
}

TEST_CASE("game enhancements: relief stays off where it makes no sense, also in the game") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "relief/hl2";
	const auto mod = f.root / "relief/mymod";
	const auto wall = [&](const char* name, const char* params) {
		test::writeText(game / "materials" / (std::string(name) + ".vmt"), std::string("\"LightmappedGeneric\"\n{\n") + params + "}\n");
	};
	wall("brick/brickwall001a", "\t\"$basetexture\" \"brick/brickwall001a\"\n");                                // relief from the texture
	wall("concrete/concretewall011a", "\t\"$basetexture\" \"concrete/concretewall011a\"\n\t\"$bumpmap\" \"concrete/n\"\n");   // from its bump map
	wall("building_template/buildingset040a_window", "\t\"$basetexture\" \"building_template/b40\"\n\t\"$bumpmap\" \"building_template/n\"\n");
	wall("metal/metalwall021a", "\t\"$basetexture\" \"metal/metalwall021a\"\n\t\"$bumpmap\" \"metal/n\"\n");
	wall("wood/woodwall009a", "\t\"$basetexture\" \"wood/woodwall009a\"\n\t\"$bumpmap\" \"wood/n\"\n");                // wood: deploy.bat leaves it
	wall("brick/brickfence", "\t\"$basetexture\" \"brick/brickfence\"\n\t\"$alphatest\" 1\n");                  // see-through: never
	wall("concrete/concretefloor001a", "\t\"$basetexture\" \"concrete/concretefloor001a\"\n");                  // no bump map, no name for it
	std::filesystem::create_directories(mod / "materials");
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	// deploy.ps1's choices (Get-ParallaxSource), from the material text alone.
	const auto deploy = [&](const char* name) { return deployWouldApply(GameEffect::Relief, std::string("materials/") + name + ".vmt", *fs.readText(std::string("materials/") + name + ".vmt")); };
	CHECK(deploy("brick/brickwall001a"));
	CHECK(deploy("concrete/concretewall011a"));
	CHECK(deploy("building_template/buildingset040a_window"));
	CHECK(deploy("metal/metalwall021a"));
	CHECK_FALSE(deploy("wood/woodwall009a"));
	CHECK_FALSE(deploy("brick/brickfence"));
	CHECK_FALSE(deploy("concrete/concretefloor001a"));
	const auto candidates = effectCandidates(fs, mod, GameEffect::Relief);
	CHECK(candidates == std::vector<std::string>{"brick/brickwall001a", "building_template/buildingset040a_window", "concrete/concretewall011a",
	                                             "metal/metalwall021a"});

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
		}
	};
	// On: the window and the metal sheet get copies that keep them off (deploy.bat leaves the mod's
	// own materials alone); the bricks and the concrete are left to deploy.bat.
	EnhancementSettings settings;
	EffectCandidates only{{GameEffect::Relief, candidates}};
	auto plan = planEnhancements(fs, mod, settings, {}, &only);
	CHECK(plan.active[GameEffect::Relief] == 2);
	REQUIRE(plan.changes.size() == 2);
	apply(plan);
	const auto window = readAll(mod / "materials/building_template/buildingset040a_window.vmt");
	CHECK(window.starts_with(std::string(MAPSMITH_MARKER) + " off (relief) from materials/building_template/buildingset040a_window.vmt - no depth for a window"));
	CHECK(isGeneratedMaterial(window));
	CHECK_FALSE(effectIsOn(GameEffect::Relief, window));
	CHECK(readAll(mod / "materials/metal/metalwall021a.vmt").find("metal sheet") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/brickwall001a.vmt"));
	CHECK(planEnhancements(fs, mod, settings, {}, &only).changes.empty());

	// Switched off for the mod: every wall keeps the game's look; on again: the copies go, deploy.bat makes the relief.
	settings.off.insert(GameEffect::Relief);
	plan = planEnhancements(fs, mod, settings, {}, &only);
	CHECK(plan.changes.size() == 2);
	apply(plan);
	CHECK(readAll(mod / "materials/brick/brickwall001a.vmt").starts_with(std::string(MAPSMITH_MARKER) + " off (relief) from materials/brick/brickwall001a.vmt (Mapsmith"));
	settings.off.erase(GameEffect::Relief);
	plan = planEnhancements(fs, mod, settings, {}, &only);
	REQUIRE(plan.changes.size() == 2);
	for (const auto& change : plan.changes) CHECK(change.remove);
	apply(plan);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/brickwall001a.vmt"));
	CHECK(std::filesystem::exists(mod / "materials/building_template/buildingset040a_window.vmt"));   // still no sense

	// deploy.bat's own relief material of a window is replaced by the copy; the mapper's own is kept.
	test::writeText(mod / "materials/metal/metalwall021a.vmt", DEPLOY + " (parallax) from materials/metal/metalwall021a.vmt\r\n\"Fidelity_Parallax\"\n{\n}\n");
	const std::string mine = "\"Fidelity_Parallax\"\n{\n\t\"$basetexture\" \"brick/brickwall001a\"\n}\n";
	test::writeText(mod / "materials/building_template/buildingset040a_window.vmt", mine);
	plan = planEnhancements(fs, mod, settings, {}, &only);
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].material == "metal/metalwall021a");
	CHECK(plan.keptOwn == std::vector<std::string>{"building_template/buildingset040a_window"});
}

TEST_CASE("game enhancements: the settings file") {
	EnhancementSettings settings;
	CHECK(writeEnhancementSettings(settings).find("\"enabled\" \"1\"") != std::string::npos);
	CHECK(parseEnhancementSettings(writeEnhancementSettings(settings)) == settings);
	settings.off.insert(GameEffect::Wind);
	settings.setExcluded(GameEffect::Liquid, "models/props_junk/glassbottle01a.mdl", true);
	settings.setExcluded(GameEffect::Liquid, "models/props_c17/oil_lamp.mdl", true);
	settings.setExcluded(GameEffect::Water, "nature/water_canals", true);
	const std::string text = writeEnhancementSettings(settings);
	CHECK(text.find("wind\n\t{\n\t\t\"enabled\" \"0\"") != std::string::npos);
	CHECK(parseEnhancementSettings(text) == settings);
	CHECK(parseEnhancementSettings("not { valid") == EnhancementSettings{});
	CHECK(parseEnhancementSettings("") == EnhancementSettings{});

	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "settings/mymod";
	CHECK(loadEnhancementSettings(mod) == EnhancementSettings{});   // no file: the defaults
	REQUIRE(saveEnhancementSettings(mod, settings));
	CHECK(std::filesystem::exists(mod / "cfg/mapsmith_enhancements.txt"));
	CHECK(loadEnhancementSettings(mod) == settings);
}
