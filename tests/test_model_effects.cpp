// test_model_effects.cpp - Glowing model parts, effects on animations and the particle editor's core.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/asset_import.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/model_effects.h>
#include <sme/model_glow.h>
#include <sme/particle_edit.h>
#include <sme/particles.h>
#include <sme/texture.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;

namespace {

/// A 32x32 texture: dark grey with a bright red 8x8 lamp in the corner.
ImageRgba lampTexture() {
	ImageRgba image;
	image.width = 32;
	image.height = 32;
	image.pixels.resize(32 * 32 * 4);
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) {
			const bool lamp = x < 8 && y < 8;
			const std::size_t i = std::size_t(y * 32 + x) * 4;
			image.pixels[i] = std::byte(lamp ? 250 : 60);
			image.pixels[i + 1] = std::byte(lamp ? 40 : 60);
			image.pixels[i + 2] = std::byte(lamp ? 30 : 64);
			image.pixels[i + 3] = std::byte(255);
		}
	}
	return image;
}

int grey(const ImageRgba& image, int x, int y) {
	return std::to_integer<int>(image.pixels[std::size_t(y * image.width + x) * 4]);
}

std::string readAll(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

std::vector<std::byte> readBytes(const std::filesystem::path& file) {
	const auto s = readAll(file);
	std::vector<std::byte> out(s.size());
	std::memcpy(out.data(), s.data(), s.size());
	return out;
}

} // namespace

TEST_CASE("glow: the auto mask finds the bright colourful part") {
	const auto mask = autoGlowMask(lampTexture(), 0.6);
	REQUIRE(mask.width == 32);
	CHECK(grey(mask, 3, 3) == 255);
	CHECK(grey(mask, 20, 20) == 0);
	CHECK(grey(mask, 8, 3) > 0);    // soft edge next to the lamp
	CHECK(grey(mask, 8, 3) < 255);
	CHECK(glowCoverage(mask) > 0.05);
	CHECK(glowCoverage(mask) < 0.15);
	// A higher threshold than the lamp's brightness: nothing glows.
	CHECK(glowCoverage(autoGlowMask(lampTexture(), 1.0)) == doctest::Approx(0.0));
	// A picture as the mask, scaled to the texture's size.
	const auto picture = pictureGlowMask(lampTexture(), 16, 16);
	CHECK(picture.width == 16);
	CHECK(grey(picture, 1, 1) > grey(picture, 12, 12));
	CHECK(glowCoverage(pictureGlowMask(ImageRgba{}, 8, 8)) == doctest::Approx(0.0));   // broken picture: nothing glows
}

TEST_CASE("glow: material text, pulse, animation script, read back") {
	const std::string vmt = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/weapons/v_gun/gun\"\n\t\"$phong\" \"1\"\n"
	                        "\tProxies\n\t{\n\t\tAnimatedTexture\n\t\t{\n\t\t\tanimatedTextureVar \"$basetexture\"\n\t\t}\n\t}\n}\n";
	GlowSettings steady;
	steady.red = 0.5;
	steady.strength = 2;
	const auto text = glowMaterialText(vmt, "models/weapons/v_gun/gun", steady);
	const auto m = parseMaterial(text, "models/weapons/v_gun/gun");
	REQUIRE(m);
	CHECK(m->param("$selfillum") == "1");
	CHECK(m->param("$selfillummask") == "models/weapons/v_gun/gun_glowmask");
	CHECK(m->param("$selfillumtint") == "[1 2 2]");
	CHECK(m->param("$phong") == "1");
	CHECK(text.find("AnimatedTexture") != std::string::npos);   // other proxies stay
	CHECK(text.find("Sine") == std::string::npos);

	GlowSettings pulse = steady;
	pulse.pulse = true;
	pulse.pulseSeconds = 2;
	const auto pulsed = glowMaterialText(text, "models/weapons/v_gun/gun", pulse);   // replaces the steady one
	CHECK(pulsed.find("Sine") != std::string::npos);
	CHECK(pulsed.find("Multiply") != std::string::npos);
	CHECK(pulsed.find("\"sinePeriod\" \"2\"") != std::string::npos);
	const auto pm = parseMaterial(pulsed, "x");
	REQUIRE(pm);
	CHECK(pm->param("$mapsmith_glowpulse") == "[1 1 1]");

	GlowSettings reload;
	reload.when = GlowSettings::When::DuringAnimation;
	reload.animations = {"ACT_VM_RELOAD", "reload \"bad\""};
	reload.fromCycle = 0.2;
	reload.toCycle = 0.8;
	const auto animated = glowMaterialText(pulsed, "models/weapons/v_gun/gun", reload);
	CHECK(animated.find("Sine") == std::string::npos);   // the earlier pulse proxies went
	CHECK(animated.find("VScriptProxy") != std::string::npos);
	CHECK(animated.find("mapsmith/glow/models_weapons_v_gun_gun.nut") != std::string::npos);
	const auto am = parseMaterial(animated, "x");
	REQUIRE(am);
	CHECK(am->param("$selfillumtint") == "[0 0 0]");   // no script, no glow
	const auto back = readGlowSettings(*am);
	REQUIRE(back);
	CHECK(back->when == GlowSettings::When::DuringAnimation);
	CHECK(back->animations == std::vector<std::string>{"ACT_VM_RELOAD", "reloadbad"});
	CHECK(back->fromCycle == doctest::Approx(0.2));
	CHECK(back->toCycle == doctest::Approx(0.8));
	CHECK(back->red == doctest::Approx(1.0));

	const auto script = glowScriptText(reload);
	CHECK(script.find("kAnimations <- [\"act_vm_reload\", \"reloadbad\"];") != std::string::npos);
	CHECK(script.find("function OnBind()") != std::string::npos);
	CHECK(script.find("self.SetVarVector(0") != std::string::npos);
	CHECK(script.find("GetSequenceActivityName") != std::string::npos);
	CHECK_FALSE(readGlowSettings(*parseMaterial(vmt, "x")));
	CHECK(glowScriptName("models\\Weapons/V_Gun/gun.vmt") == "mapsmith/glow/models_weapons_v_gun_gun.nut");
}

TEST_CASE("glow: plan from the game's files, apply into the mod, take it back") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "glow/hl2";
	const auto mod = f.root / "glow/mymod";
	test::writeText(game / "materials/models/weapons/v_gun/gun.vmt", "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/weapons/v_gun/gun\"\n}\n");
	test::writeText(game / "materials/concrete/wall.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"concrete/wall\"\n}\n");
	const auto vtf = encodeVtf(lampTexture(), false, false, 32);
	REQUIRE(vtf);
	std::filesystem::create_directories(game / "materials/models/weapons/v_gun");
	{
		std::ofstream out(game / "materials/models/weapons/v_gun/gun.vtf", std::ios::binary);
		out.write(reinterpret_cast<const char*>(vtf->data()), std::streamsize(vtf->size()));
	}
	std::filesystem::create_directories(mod);
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	std::string error;
	CHECK_FALSE(planGlow(fs, "concrete/wall", GlowSettings{}, nullptr, &error));
	CHECK(error.find("model materials") != std::string::npos);
	CHECK_FALSE(planGlow(fs, "../../etc/passwd", GlowSettings{}, nullptr, &error));
	GlowSettings noAnims;
	noAnims.when = GlowSettings::When::DuringAnimation;
	CHECK_FALSE(planGlow(fs, "models/weapons/v_gun/gun", noAnims, nullptr, &error));

	GlowSettings reload;
	reload.when = GlowSettings::When::DuringAnimation;
	reload.animations = {"ACT_VM_RELOAD"};
	const auto files = planGlow(fs, "models/weapons/v_gun/gun", reload, nullptr, &error);
	REQUIRE_MESSAGE(files, error);
	CHECK(files->coverage > 0.05);
	CHECK_FALSE(files->scriptText.empty());
	const auto record = applyGlow(mod, *files, &error);
	REQUIRE_MESSAGE(record, error);
	CHECK(record->kind == "glow");
	CHECK(record->created.size() == 3);
	CHECK(std::filesystem::exists(mod / "materials/models/weapons/v_gun/gun_glowmask.vtf"));
	CHECK(std::filesystem::exists(mod / "scripts/vscripts/mapsmith/glow/models_weapons_v_gun_gun.nut"));
	const auto written = decodeTexture(readBytes(mod / "materials/models/weapons/v_gun/gun_glowmask.vtf"));
	REQUIRE(written);
	CHECK(written->width == 32);
	CHECK(readAll(mod / "materials/models/weapons/v_gun/gun.vmt").find("$mapsmith_glow") != std::string::npos);

	// The mod's own copy of the material is kept once, the original comes back.
	CHECK(undoImport(*record));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/models/weapons/v_gun/gun.vmt"));
	CHECK_FALSE(std::filesystem::exists(mod / "scripts/vscripts/mapsmith/glow/models_weapons_v_gun_gun.nut"));
	test::writeText(mod / "materials/models/weapons/v_gun/gun.vmt", "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/weapons/v_gun/gun\"\n\t\"$mine\" 1\n}\n");
	fs.noteWrittenFile(mod / "materials/models/weapons/v_gun/gun.vmt");
	const auto second = applyGlow(mod, *planGlow(fs, "models/weapons/v_gun/gun", GlowSettings{}), &error);
	REQUIRE(second);
	CHECK(second->replaced.size() == 1);
	CHECK(readAll(mod / "materials/models/weapons/v_gun/gun.vmt").find("$mine") != std::string::npos);   // edited from the mod's own copy
	CHECK(undoImport(*second));
	CHECK(readAll(mod / "materials/models/weapons/v_gun/gun.vmt").find("$mapsmith_glow") == std::string::npos);
	CHECK(readAll(mod / "materials/models/weapons/v_gun/gun.vmt").find("$mine") != std::string::npos);
}

TEST_CASE("effects: events for an animation, keyvalues for always, and back") {
	ModelEffect reload;
	reload.particle = "gun_glow";
	reload.attachment = "muzzle";
	reload.sequence = "reload";
	reload.startFrame = 5;
	reload.endFrame = 30;
	const auto events = effectEvents(reload);
	REQUIRE(events.size() == 2);
	CHECK(events[0].event == "AE_CL_CREATE_PARTICLE_EFFECT");
	CHECK(events[0].frame == 5);
	CHECK(events[0].options == "gun_glow follow_attachment muzzle");
	CHECK(events[1].event == "AE_CL_STOP_PARTICLE_EFFECT");
	CHECK(events[1].frame == 30);
	CHECK(events[1].options == "gun_glow");
	CHECK(qcEventLine(events[0]) == "{ event AE_CL_CREATE_PARTICLE_EFFECT 5 \"gun_glow follow_attachment muzzle\" }");

	ModelEffect evil = reload;
	evil.particle = "a\" }\n$sequence x";
	CHECK(effectEvents(evil)[0].options.find('"') == std::string::npos);
	CHECK(qcEventLine({"s", 1, "AE_X", "a\"b\nc"}) == "{ event AE_X 1 \"abc\" }");

	ModelEffect always;
	always.when = ModelEffect::When::Always;
	always.particle = "lamp_glow";
	always.attachment = "light";
	CHECK(effectEvents(always).empty());
	const auto kv = effectKeyValues({always, reload});
	CHECK(kv.find("particles") != std::string::npos);
	CHECK(kv.find("name \"lamp_glow\"") != std::string::npos);
	CHECK(kv.find("attachment_point \"light\"") != std::string::npos);
	CHECK(kv.find("gun_glow") == std::string::npos);
	CHECK(effectKeyValues({reload}).empty());

	const auto back = effectsFromModel(effectEvents({reload}), "mdlkeyvalue\n{\n" + kv + "}\n");
	REQUIRE(back.size() == 2);
	CHECK(back[0] == reload);
	CHECK(back[1].when == ModelEffect::When::Always);
	CHECK(back[1].particle == "lamp_glow");
	CHECK(back[1].attachment == "light");
	CHECK(effectsFromModel({}, "{{{ broken").empty());

	CHECK(effectProblems(reload, {"muzzle"}, {"reload"}, 40).empty());
	CHECK(effectProblems(reload, {"eject"}, {"reload"}, 40).size() == 1);
	CHECK(effectProblems(reload, {"muzzle"}, {"reload"}, 20).size() == 1);   // stops after the end of a 20-frame animation
	CHECK(effectProblems(ModelEffect{}, {}, {}).size() == 3);                 // no particle, no attachment, no animation
	// Kept in the mod between sessions.
	const auto saved = readEffectsText(writeEffectsText({reload, always}));
	REQUIRE(saved.size() == 2);
	CHECK(saved[0] == reload);
	CHECK(saved[1] == always);
	CHECK(readEffectsText("garbage {{").empty());
	CHECK(modelEffectsFile("/mod", "models/weapons/v_smg1.mdl") == std::filesystem::path("/mod/modelsrc/weapons/v_smg1/mapsmith_effects.txt"));
	CHECK(modelEffectsFile("/mod", "models/../../x.mdl").empty());
	CHECK(modelEffectsFile("/mod", "materials/x.vmt").empty());
	CHECK(eventNameForNumber(5004) == "AE_CL_PLAYSOUND");
	CHECK(eventNameForNumber(5001) == "CL_EVENT_MUZZLEFLASH0");
	CHECK(eventNameForNumber(42).empty());
	CHECK(effectAttachFromName("FOLLOW_ROOTBONE") == EffectAttach::FollowRootBone);
	CHECK_FALSE(effectAttachFromName("nonsense"));
}

TEST_CASE("particle editor: values keep their types through write and read") {
	auto glow = particleFromTemplate(ParticleTemplate::Glow, "my glow!");
	CHECK(glow.name == "my_glow");
	auto sparks = particleFromTemplate(ParticleTemplate::Sparks, "my_sparks");
	glow.children.push_back({"my_sparks", 0.25});
	glow.properties.emplace_back("control point to disable rendering if it is the camera", -1);
	glow.properties.emplace_back("bounding_box_min", Vec3{-10, -10, -10});
	glow.operators.push_back({"Test", {{"v2", PcfVector2{1, 2}}, {"v4", PcfVector4{1, 2, 3, 4}}, {"a", PcfAngles{10, 20, 30}},
	                                   {"ints", std::vector<int>{1, 2, 3}}, {"floats", std::vector<float>{0.5f}}, {"flag", true}}});
	const auto read = roundTrip({glow, sparks});
	REQUIRE(read.size() == 2);
	REQUIRE(read[0].children.size() == 1);
	CHECK(read[0].children[0].name == "my_sparks");
	CHECK(read[0].children[0].delay == doctest::Approx(0.25));
	std::vector<std::string> dropped;
	const auto again = toPcfSystem(read[0], &dropped);
	CHECK(dropped.empty());
	CHECK(again.properties == glow.properties);
	CHECK(again.operators.back().values == glow.operators.back().values);
	CHECK(again.emitters.front().values == glow.emitters.front().values);
	CHECK(again.children.size() == 1);
	// Same systems, same bytes (stable ids).
	CHECK(writeParticleFile({again, toPcfSystem(read[1])}) == writeParticleFile({glow, sparks}));
	// A child that is not in the file is left out instead of pointing nowhere.
	auto lonely = glow;
	lonely.children = {{"missing", 0}};
	CHECK(roundTrip({lonely})[0].children.empty());

	CHECK(pcfValueText(PcfValue(PcfColor{255, 128, 0, 255})) == "255 128 0 255");
	CHECK(pcfValueFromText("10 20 30", PcfValue(Vec3{})) == PcfValue(Vec3{10, 20, 30}));
	CHECK_FALSE(pcfValueFromText("10 20", PcfValue(Vec3{})));
	CHECK_FALSE(pcfValueFromText("abc", PcfValue(1.0f)));
	CHECK(pcfValueFromText("300 -5 7", PcfValue(PcfColor{})) == PcfValue(PcfColor{255, 0, 7, 255}));
	CHECK(pcfValueFromText("on", PcfValue(false)) == PcfValue(true));
	CHECK(pcfValueFromText("[1, 2]", PcfValue(std::vector<int>{})) == PcfValue(std::vector<int>{1, 2}));

	// Every template is a system the reader takes (and the preview can simulate).
	for (const auto kind : particleTemplates()) {
		const auto s = roundTrip({particleFromTemplate(kind, "t")});
		REQUIRE(s.size() == 1);
		CHECK_FALSE(s[0].renderers.empty());
		ParticleSimulation sim(s, "t");
		std::size_t most = 0;   // a muzzle flash lives for 0.08 s, smoke starts slowly
		for (int step = 0; step < 30; ++step) {
			sim.step(0.02);
			most = std::max(most, sim.particleCount());
		}
		CHECK(most > 0);
	}
	CHECK_FALSE(particleFunctionCatalog().empty());
}

TEST_CASE("particle editor: children come along, saving merges into the mod's file") {
	auto parent = particleFromTemplate(ParticleTemplate::Glow, "parent");
	parent.children = {{"child", 0}};
	auto child = particleFromTemplate(ParticleTemplate::Sparks, "child");
	child.children = {{"grandchild", 0.1}, {"parent", 0}};   // a cycle ends
	const auto library = roundTrip({child, particleFromTemplate(ParticleTemplate::Smoke, "grandchild")});
	const auto all = withChildren({parent}, library);
	REQUIRE(all.size() == 3);
	CHECK(all[1].name == "child");
	CHECK(all[2].name == "grandchild");

	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "particles_mod";
	std::filesystem::create_directories(mod);
	const std::string gameManifest = "particles_manifest\n{\n\t\"file\" \"particles/blood_impact.pcf\"\n}\n";
	std::string error;
	CHECK_FALSE(saveModParticles(mod, "../evil.pcf", all, gameManifest, &error));
	CHECK_FALSE(saveModParticles(mod, "particles/sub/x.pcf", all, gameManifest, &error));
	const auto file = modParticleFile("My Mod");
	CHECK(file == "particles/my_mod_mapsmith.pcf");
	const auto first = saveModParticles(mod, file, all, gameManifest, &error);
	REQUIRE_MESSAGE(first, error);
	const auto manifest = readAll(mod / "particles/particles_manifest.txt");
	CHECK(manifest.find("blood_impact.pcf") != std::string::npos);   // the game's files stay listed
	CHECK(manifest.find("!particles/my_mod_mapsmith.pcf") != std::string::npos);
	// A second save with one changed system keeps the others.
	auto changed = particleFromTemplate(ParticleTemplate::Embers, "child");
	const auto second = saveModParticles(mod, file, {changed}, gameManifest, &error);
	REQUIRE(second);
	const auto saved = readParticleDefinitions(readBytes(mod / file));
	REQUIRE(saved.size() == 3);
	CHECK(saved[0].name == "parent");
	CHECK(saved[2].name == "child");
	CHECK(saved[2].properties.number("max_particles", 0) == doctest::Approx(50));   // the embers now
	CHECK(saved[0].children.size() == 1);   // still pointing at the (new) child
}
