// test_fidelity_effects.cpp - Game enhancements: the materials match what mapbase-fidelity's
// deploy.ps1 writes (liquid, wind, water), on by default and off for the mod or one model; the
// settings file.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/heightmap.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

template <typename T>
void put(std::vector<std::byte>& out, std::size_t at, T value) {
	std::memcpy(out.data() + at, &value, sizeof value);
}

/// An uncompressed RGB888 .vtf (7.2, one mip level) of a brick wall: courses of bricks in a running
/// bond, each brick its own shade (some darker than the light mortar), with grain.
std::vector<std::byte> brickVtf(unsigned size, bool bricks = true) {
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
	std::vector<double> tones(64);
	for (auto& tone : tones) tone = 0.25 + 0.45 * random();
	const double course = size / 8.0, unit = size / 4.0;
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			double tone = 0.5 + (random() - 0.5) * 0.5;   // without bricks: only grain
			if (bricks) {
				const int row = int(y / course);
				const double offset = row % 2 ? unit * 0.5 : 0.0;
				const double u = std::fmod(x + 0.5 + offset, unit), v = std::fmod(y + 0.5, course);
				const bool joint = v < 1.5 || v > course - 1.5 || u < 1.5 || u > unit - 1.5;
				const int brick = row * 4 + int(std::fmod(x + offset, double(size)) / unit);
				tone = (joint ? 0.82 : tones[std::size_t(brick) % tones.size()]) + (random() - 0.5) * 0.12;
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

TEST_CASE("relief height maps: byte for byte as deploy.ps1's height_map.cs writes them (WriteI8)") {
	HeightMap map;
	map.width = 4;
	map.height = 2;
	map.values = {0.f, 1.f, 0.5f, 0.25f, 1.f, 1.f, 0.f, 0.75f};
	const auto vtf = encodeHeightVtf(map);
	// 7.2, 80-byte header, levels 4x2, 2x1, 1x1 (smallest first), I8.
	REQUIRE(vtf.size() == 80 + 1 + 2 + 8);
	const auto i32 = [&vtf](std::size_t at) {
		std::int32_t v;
		std::memcpy(&v, vtf.data() + at, 4);
		return v;
	};
	const auto f32 = [&vtf](std::size_t at) {
		float v;
		std::memcpy(&v, vtf.data() + at, 4);
		return v;
	};
	CHECK(std::memcmp(vtf.data(), "VTF\0", 4) == 0);
	CHECK(i32(4) == 7);
	CHECK(i32(8) == 2);
	CHECK(i32(12) == 80);
	CHECK(i32(20) == 0);              // no flags (no sRGB, no clamping: it tiles like the wall)
	CHECK(f32(32) == doctest::Approx(0.5));
	CHECK(f32(48) == doctest::Approx(1.0));
	CHECK(i32(52) == 5);              // IMAGE_FORMAT_I8: the shader reads .r
	CHECK(int(vtf[56]) == 3);         // every mip level
	CHECK(i32(57) == -1);             // no thumbnail
	const auto byte = [&vtf](std::size_t at) { return int(vtf[at]); };
	// Level 0 at the end, rounded like height_map.cs (t * 255 + 0.5).
	CHECK(byte(83) == 0);
	CHECK(byte(84) == 255);
	CHECK(byte(85) == 128);
	CHECK(byte(86) == 64);
	// Level 1: box filter of 2 x 2 with +2 rounding, as WriteI8.
	CHECK(byte(81) == (0 + 255 + 255 + 255 + 2) / 4);
	CHECK(byte(82) == (128 + 64 + 0 + 191 + 2) / 4);
	CHECK(byte(80) == (byte(81) + byte(82) + byte(81) + byte(82) + 2) / 4);
	map.width = 3;
	CHECK(encodeHeightVtf(map).empty());   // not a power of two: refused, as the game cannot tile it
}

TEST_CASE("game enhancements: only with the mod's shader DLL - without it the game draws them as wireframes") {
	CHECK(shaderNeedsModDll("Fidelity_Parallax"));
	CHECK(shaderNeedsModDll("fidelity_liquid"));
	CHECK(shaderNeedsModDll("Water_Fidelity"));
	CHECK_FALSE(shaderNeedsModDll("LightmappedGeneric"));
	CHECK_FALSE(shaderNeedsModDll("Water"));
	CHECK(effectNeedsShaderDll(GameEffect::Relief));
	CHECK(effectNeedsShaderDll(GameEffect::Liquid));
	CHECK(effectNeedsShaderDll(GameEffect::Water));
	CHECK_FALSE(effectNeedsShaderDll(GameEffect::Wind));   // Mapbase's own $treesway

	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "dll/hl2";
	const auto mod = f.root / "dll/mymod";
	test::writeText(game / "materials/models/props_junk/glassbottle01a.vmt",
	                "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props_junk/glassbottle01a\"\n\t\"$translucent\" 1\n}\n");
	test::writeText(game / "materials/brick/brickwall001a.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/brickwall001a\"\n}\n");
	// What deploy.bat made while the DLL was there, and the mapper's own relief floor (the
	// material editor's) - Julius' see-through floor.
	test::writeText(mod / "materials/models/props_junk/glassbottle01a.vmt", DEPLOY + " (liquid) from x\r\n\"Fidelity_Liquid\"\n{\n}\n");
	test::writeText(mod / "materials/concrete/floor01.vmt", "\"Fidelity_Parallax\"\n{\n\t\"$basetexture\" \"concrete/floor01\"\n\t\"$parallaxmap\" \"concrete/floor01_relief\"\n}\n");
	test::writeText(mod / "materials/concrete/floor02.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"concrete/floor02\"\n}\n");
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});
	CHECK_FALSE(modHasShaderDll(mod));
	CHECK(ownMaterialsNeedingShaderDll(mod) == std::vector<std::string>{"concrete/floor01"});

	EffectCandidates only{{GameEffect::Liquid, {"models/props_junk/glassbottle01a"}}, {GameEffect::Relief, {"brick/brickwall001a"}}};
	EnhancementOptions options;
	options.shaderDll = false;
	auto plan = planEnhancements(fs, mod, {}, {}, &only, options);
	// Nothing gets a shader the game does not know; deploy's bottle goes; the mapper's floor stays (reported above).
	CHECK(plan.heldBack == 2);
	CHECK(plan.activeCount() == 0);
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].remove);
	CHECK(plan.changes[0].material == "models/props_junk/glassbottle01a");
	// With the DLL the bottle gets its liquid again.
	test::writeText(mod / "bin/game_shader_dx6.dll", "dll");
	CHECK(modHasShaderDll(mod));
	CHECK(ownMaterialsNeedingShaderDll(mod).size() == 1);   // (the list is only shown while the DLL is missing)
	std::filesystem::remove(mod / "materials/models/props_junk/glassbottle01a.vmt");
	fs.noteRemovedFile(mod / "materials/models/props_junk/glassbottle01a.vmt");
	plan = planEnhancements(fs, mod, {}, {}, &only, {});
	CHECK(plan.heldBack == 0);
	CHECK(plan.active[GameEffect::Liquid] == 1);
}

TEST_CASE("game enhancements: walls of the mod's maps get their relief from the texture's pattern") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "pattern/hl2";
	const auto mod = f.root / "pattern/mymod";
	const auto wall = [&](const std::string& name, const std::string& texture, const std::string& extra = {}) {
		test::writeText(game / "materials" / (name + ".vmt"), "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"" + texture + "\"\n" + extra + "}\n");
	};
	wall("brick/brickwall001a", "brick/brickwall001a");                                    // a deploy candidate (brick)
	wall("concrete/concretefloor001a", "concrete/concretefloor001a");                      // not deploy's, but laid in courses
	wall("concrete/plain", "concrete/plain");                                               // no pattern
	wall("brick/shiny", "brick/brickwall001a", "\t\"$envmap\" \"env_cubemap\"\n");           // has its own reflection
	writeBytes(game / "materials/brick/brickwall001a.vtf", brickVtf(128));
	writeBytes(game / "materials/concrete/concretefloor001a.vtf", brickVtf(128));
	writeBytes(game / "materials/concrete/plain.vtf", brickVtf(128, false));
	std::filesystem::create_directories(mod / "materials");
	test::writeText(mod / "bin/game_shader_dx6.dll", "dll");
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	// The maps: one .vmf in the map folder uses three of the walls.
	test::writeText(f.root / "pattern/mapsrc/a.vmf",
	                "world\n{\n\tsolid\n\t{\n\t\tside\n\t\t{\n\t\t\t\"material\" \"BRICK/BRICKWALL001A\"\n\t\t}\n\t\tside\n\t\t{\n"
	                "\t\t\t\"material\" \"concrete/concretefloor001a\"\n\t\t}\n\t\tside\n\t\t{\n\t\t\t\"material\" \"concrete/plain\"\n"
	                "\t\t}\n\t\tside\n\t\t{\n\t\t\t\"material\" \"brick/shiny\"\n\t\t}\n\t}\n}\n");
	const auto mapMaterials = vmfMaterialNames({f.root / "pattern/mapsrc/a.vmf", f.root / "pattern/mapsrc/missing.vmf"});
	{
		// The map sources of a mod: not the autosaves (in their folder or as timestamped copies anywhere) nor the cordon copies.
		const auto dir = f.root / "pattern_sources";
		std::filesystem::create_directories(dir / "sub" / "autosave");
		std::filesystem::create_directories(dir / "Autosave");
		for (const char* file : {"a.vmf", "sub/b.VMF", "sub/autosave/b.20261028.vmf", "Autosave/a.1.vmf", "a_cordon.vmf", "notes.txt", "test.20261028-084357.vmf"}) {
			std::ofstream(dir / file) << "versioninfo\n{\n}\n";
		}
		const auto sources = mapSourceFiles(dir);
		REQUIRE(sources.size() == 2);
		CHECK(sources[0].filename() == "a.vmf");
		CHECK(sources[1].filename() == "b.VMF");
		CHECK(mapSourceFiles(dir / "missing").empty());
		CHECK(mapSourceFiles({}).empty());
	}
	CHECK(mapMaterials == std::set<std::string>{"brick/brickwall001a", "brick/shiny", "concrete/concretefloor001a", "concrete/plain"});

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
	EffectCandidates only{{GameEffect::Relief, effectCandidates(fs, mod, GameEffect::Relief)}};
	CHECK(only[GameEffect::Relief] == std::vector<std::string>{"brick/brickwall001a", "brick/shiny"});
	EnhancementOptions options;
	options.mapMaterials = &mapMaterials;
	EnhancementSettings settings;
	auto plan = planEnhancements(fs, mod, settings, {}, &only, options);
	CHECK(plan.patternRelief == 3);   // both brick walls and the floor in courses; the plain one keeps deploy's (none)
	apply(plan);
	const auto text = readAll(mod / "materials/brick/brickwall001a.vmt");
	CHECK(text.starts_with(std::string(PATTERN_RELIEF_MARKER) + " (relief) from materials/brick/brickwall001a.vmt hash "));
	CHECK(isGeneratedMaterial(text));   // and so not deploy.bat's to overwrite: it leaves the mod's own alone
	CHECK(effectIsOn(GameEffect::Relief, text));
	// deploy's form (New-ParallaxMaterial): the shader, the relief keys first, the course depth and darkening,
	// the wet-only cubemap of a wall without its own reflection, the rest as it was.
	CHECK(text.find("\r\n\"Fidelity_Parallax\"\n{\r\n\t\"$parallaxmap\" \"brick/brickwall001a_mapsmith_relief\"\r\n\t\"$parallaxdepth\" \"0.006\"\r\n\t\"$parallaxao\" \"0.25\"\r\n"
	                "\t\"$envmap\" \"env_cubemap\"\r\n\t\"$envmapwetonly\" \"1\"\n\t\"$basetexture\" \"brick/brickwall001a\"") != std::string::npos);
	const auto shiny = readAll(mod / "materials/brick/shiny.vmt");
	CHECK(shiny.find("$envmapwetonly") == std::string::npos);   // its own reflection stays as it is
	CHECK(std::count(shiny.begin(), shiny.end(), '$') == 5);   // parallaxmap, parallaxdepth, parallaxao, basetexture, envmap
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/plain.vmt"));
	// The height map: bricks up, joints down, readable by the game (I8) and by Mapsmith.
	const auto height = loadTexture(fs, "brick/brickwall001a_mapsmith_relief");
	REQUIRE(height);
	CHECK(height->width == 128);
	const auto at = [&](unsigned x, unsigned y) { return int(height->pixels[(std::size_t(y) * height->width + x) * 4]); };
	CHECK(at(16, 8) > at(16, 0) + 60);   // middle of a brick above the bed joint
	// Again: nothing to do (the hash in the first line says it is made from this texture).
	plan = planEnhancements(fs, mod, settings, {}, &only, options);
	CHECK(plan.changes.empty());
	CHECK(plan.patternRelief == 3);
	// Another texture: made again.
	writeBytes(game / "materials/concrete/concretefloor001a.vtf", brickVtf(64));
	plan = planEnhancements(fs, mod, settings, {}, &only, options);
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].material == "concrete/concretefloor001a");
	apply(plan);
	// Relief off for one wall and for the mod: Mapsmith's copies and their height maps go
	// (the deploy candidate gets the copy that keeps it off).
	settings.setExcluded(GameEffect::Relief, "concrete/concretefloor001a", true);
	plan = planEnhancements(fs, mod, settings, {}, &only, options);
	apply(plan);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/concretefloor001a.vmt"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/concretefloor001a_mapsmith_relief.vtf"));
	settings.off.insert(GameEffect::Relief);
	plan = planEnhancements(fs, mod, settings, {}, &only, options);
	apply(plan);
	CHECK(readAll(mod / "materials/brick/brickwall001a.vmt").starts_with(std::string(MAPSMITH_MARKER) + " off (relief)"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/brickwall001a_mapsmith_relief.vtf"));
	CHECK(readAll(mod / "materials/brick/shiny.vmt").starts_with(std::string(MAPSMITH_MARKER) + " off (relief)"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/shiny_mapsmith_relief.vtf"));
	// Without the shader DLL nothing of it stays (the game would draw wireframes).
	settings.off.clear();
	settings.setExcluded(GameEffect::Relief, "concrete/concretefloor001a", false);
	apply(planEnhancements(fs, mod, settings, {}, &only, options));
	CHECK(std::filesystem::exists(mod / "materials/concrete/concretefloor001a_mapsmith_relief.vtf"));
	options.shaderDll = false;
	apply(planEnhancements(fs, mod, settings, {}, &only, options));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/concretefloor001a.vmt"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/concrete/concretefloor001a_mapsmith_relief.vtf"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/brickwall001a_mapsmith_relief.vtf"));
}
