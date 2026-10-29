// test_container_models.cpp - Containers on shared materials get their own model copy with a
// liquid material of their own, as mapbase-fidelity's deploy.ps1 makes it (Get-ContainerModels):
// the material table read from the .mdl, the patched names, the list fidelity_models.txt, and
// removal when switched off.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/container_models.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/mod_install.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace sme;

namespace {

std::string readAll(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// The game's dark glass material (VertexLitGeneric, see-through), named like no container.
std::string glassVmt(const std::string& texture) {
	return "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"" + texture + "\"\n\t\"$translucent\" 1\n\t\"$envmap\" \"env_cubemap\"\n}\n";
}

/// A game (hl2) and a mod with its shader DLL, read like the editor reads them.
struct Setup {
	test::Fixture f;
	std::filesystem::path game, mod;
	GameFileSystem fs;

	explicit Setup(const char* name) {
		test::createFixture(f);
		game = f.root / name / "hl2";
		mod = f.root / name / "mymod";
		test::writeText(mod / "bin/game_shader_dx6.dll", "dll");
	}
	void model(const std::string& path, const std::vector<std::string>& textures, const std::string& cdmaterials = "models\\props_junk\\") {
		test::StudioModelSpec spec;
		spec.textures = textures;
		spec.cdmaterials = cdmaterials;
		test::writeStudioModel(game / path, test::makeStudioModel(spec));
	}
	void mount() {
		std::filesystem::create_directories(mod / "materials");
		fs.addDirectory(mod, {"game", "mod"});
		fs.addDirectory(game, {"game"});
	}
	/// Writes the plan's files the way the editor does (materials, then the list, then the models).
	void apply(const EnhancementPlan& plan) {
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
		const auto list = mod / std::string(CONTAINER_MODELS_MANIFEST);
		if (plan.modelsManifest && !plan.modelsManifest->empty()) test::writeText(list, *plan.modelsManifest);
		for (const auto& change : plan.modelChanges) {
			if (change.remove) {
				std::filesystem::remove(change.file);
				fs.noteRemovedFile(change.file);
			} else {
				std::filesystem::create_directories(change.file.parent_path());
				std::ofstream(change.file, std::ios::binary).write(reinterpret_cast<const char*>(change.bytes.data()), std::streamsize(change.bytes.size()));
				fs.noteWrittenFile(change.file);
			}
		}
		if (plan.modelsManifest && plan.modelsManifest->empty()) std::filesystem::remove(list);
	}
};

} // namespace

TEST_CASE("container models: the material table of an .mdl, with every count and offset checked") {
	test::StudioModelSpec spec;
	spec.textures = {"GlassBrown01a", "Label01"};
	spec.cdmaterials = "Models\\Props_Junk";
	const auto files = test::makeStudioModel(spec);
	const auto table = readMdlMaterialTable(files.mdl);
	REQUIRE(table);
	REQUIRE(table->textures.size() == 2);
	CHECK(table->textures[0].name == "GlassBrown01a");
	CHECK(table->textures[1].name == "Label01");
	for (const auto& texture : table->textures) {
		CHECK(std::memcmp(files.mdl.data() + texture.offset, texture.name.data(), texture.name.size()) == 0);
	}
	CHECK(table->folders == std::vector<std::string>{"models/props_junk/"});   // lower case, '/', ending in '/'

	// Damaged files: nothing, never a read past the end.
	CHECK_FALSE(readMdlMaterialTable({}));
	CHECK_FALSE(readMdlMaterialTable(std::span(files.mdl).first(219)));
	auto broken = files.mdl;
	const std::int32_t far = 0x7fffff00;
	std::memcpy(broken.data() + 208, &far, 4);   // texture table beyond the file
	CHECK_FALSE(readMdlMaterialTable(broken));
	broken = files.mdl;
	std::memcpy(broken.data() + 204, &far, 4);   // absurd count
	CHECK_FALSE(readMdlMaterialTable(broken));
	broken = files.mdl;
	const std::int32_t index = [&] { std::int32_t v; std::memcpy(&v, files.mdl.data() + 208, 4); return v; }();
	std::memcpy(broken.data() + index, &far, 4);   // a name beyond the file
	CHECK_FALSE(readMdlMaterialTable(broken));
	broken = files.mdl;
	std::fill(broken.begin() + std::ptrdiff_t(table->textures[0].offset), broken.end(), std::byte{'x'});   // no end of the name
	CHECK_FALSE(readMdlMaterialTable(broken));
	// Random bytes behind a valid magic: never a crash.
	std::vector<std::byte> noise(4096);
	std::uint32_t seed = 12345;
	for (int round = 0; round < 200; ++round) {
		for (auto& b : noise) {
			seed = seed * 1664525u + 1013904223u;
			b = std::byte(seed >> 24);
		}
		std::memcpy(noise.data(), "IDST", 4);
		(void)readMdlMaterialTable(noise);
	}
}

TEST_CASE("container models: the glass jug on a material named like no container gets its own copy with liquid") {
	// Julius (29.10.2026): glassjug01.mdl looked like the original in the game. Its material is not
	// named after a container, so the liquid comes only through a copy of the model (deploy.ps1's
	// Get-ContainerModels), which Mapsmith did not make.
	Setup s("jug");
	test::writeText(s.game / "materials/models/props_junk/glassbrown01a.vmt", glassVmt("models/props_junk/glassbrown01a"));
	s.model("models/props_junk/glassjug01.mdl", {"GlassBrown01a"});
	s.mount();

	const auto scan = findContainerModels(s.fs, s.mod);
	REQUIRE(scan.models.size() == 1);
	CHECK(scan.models[0].model == "models/props_junk/glassjug01.mdl");
	CHECK(scan.models[0].own == std::vector<std::string>{"materials/models/props_junk/glassbrow_flq.vmt"});
	CHECK(scan.ownSources.at("materials/models/props_junk/glassbrow_flq.vmt") == "materials/models/props_junk/glassbrown01a.vmt");
	// The copy: the same file, only the name in the texture table changed (same length).
	const auto original = s.fs.read("models/props_junk/glassjug01.mdl");
	REQUIRE(original);
	REQUIRE(scan.models[0].bytes.size() == original->size());
	const auto table = readMdlMaterialTable(scan.models[0].bytes);
	REQUIRE(table);
	CHECK(table->textures[0].name == "GlassBrow_flq");
	std::size_t differ = 0;
	for (std::size_t i = 0; i < original->size(); ++i) differ += (*original)[i] != scan.models[0].bytes[i];
	CHECK(differ == 4);

	auto plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	// The shared material itself stays as it is (other things may use it); its own copy gets liquid.
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].material == "models/props_junk/glassbrow_flq");
	const std::string& text = plan.changes[0].text;
	CHECK(text.starts_with(std::string(DEPLOY_MARKER) + " (liquid) from materials/models/props_junk/glassbrow_flq.vmt"));
	CHECK(text.find("\"Fidelity_Liquid\"") != std::string::npos);
	CHECK(text.find("\"$basetexture\" \"models/props_junk/glassbrown01a\"") != std::string::npos);
	CHECK(text.find("\"$tintedglass\" \"0.8\"") != std::string::npos);   // dark glass (deploy.ps1: kind tinted)
	REQUIRE(plan.modelChanges.size() == 1);
	CHECK_FALSE(plan.modelChanges[0].remove);
	CHECK(plan.modelChanges[0].file == s.mod / "models/props_junk/glassjug01.mdl");
	CHECK(plan.liquidModels == std::vector<std::string>{"models/props_junk/glassjug01.mdl"});
	CHECK(plan.containerModels == std::vector<std::string>{"models/props_junk/glassjug01.mdl"});
	CHECK(plan.active[GameEffect::Liquid] == 1);
	// The list, in deploy.ps1's form (its Remove-GeneratedModels reads it): path, SHA-256 in capitals.
	REQUIRE(plan.modelsManifest);
	const std::string& bytes = std::string(reinterpret_cast<const char*>(scan.models[0].bytes.data()), scan.models[0].bytes.size());
	std::string hash = sha256Hex(bytes);
	std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	CHECK(*plan.modelsManifest == std::string(CONTAINER_MODELS_HEADER) + "\r\nmodels/props_junk/glassjug01.mdl\t" + hash + "\r\n");
	s.apply(plan);
	CHECK(s.fs.readText("models/props_junk/glassjug01.mdl") == bytes);   // the game now loads the copy
	CHECK(effectIsOn(GameEffect::Liquid, *s.fs.readText("materials/models/props_junk/glassbrow_flq.vmt")));

	// Twice: nothing changes.
	plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	CHECK(plan.changes.empty());
	CHECK(plan.modelChanges.empty());
	CHECK_FALSE(plan.modelsManifest);
	CHECK(plan.liquidModels.size() == 1);

	// Left out by its model: the copy goes, the list too, and a copy of the original material at
	// the own material's place keeps deploy.bat from making it again.
	EnhancementSettings settings;
	settings.setExcluded(GameEffect::Liquid, "models/props_junk/glassjug01.mdl", true);
	plan = planEnhancements(s.fs, s.mod, settings, excludedMaterials(s.fs, settings));
	REQUIRE(plan.modelChanges.size() == 1);
	CHECK(plan.modelChanges[0].remove);
	REQUIRE(plan.changes.size() == 1);
	CHECK(plan.changes[0].text.starts_with(std::string(MAPSMITH_MARKER) + " off (liquid)"));
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, plan.changes[0].text));
	CHECK(plan.modelsManifest == std::string());
	CHECK(plan.liquidModels.empty());
	s.apply(plan);
	CHECK_FALSE(std::filesystem::exists(s.mod / "models/props_junk/glassjug01.mdl"));
	CHECK_FALSE(std::filesystem::exists(s.mod / std::string(CONTAINER_MODELS_MANIFEST)));
	CHECK(s.fs.read("models/props_junk/glassjug01.mdl") == original);   // the game's again

	// Back on: made again.
	plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	CHECK(plan.modelChanges.size() == 1);
	CHECK(plan.changes.size() == 1);
	s.apply(plan);
	CHECK(std::filesystem::exists(s.mod / "models/props_junk/glassjug01.mdl"));

	// Without the mod's shader DLL nothing is made: the copy goes with its liquid material.
	EnhancementOptions noDll;
	noDll.shaderDll = false;
	plan = planEnhancements(s.fs, s.mod, EnhancementSettings{}, {}, nullptr, noDll);
	REQUIRE(plan.modelChanges.size() == 1);
	CHECK(plan.modelChanges[0].remove);
	CHECK(std::any_of(plan.changes.begin(), plan.changes.end(),
	                  [](const EffectChange& c) { return c.remove && c.material == "models/props_junk/glassbrow_flq"; }));
	CHECK(plan.modelsManifest == std::string());
}

TEST_CASE("container models: name clashes, short names, container materials, the mod's own files") {
	Setup s("clash");
	// Two jugs whose materials both end up as ..._flq: the second takes the next free suffix.
	test::writeText(s.game / "materials/models/props_junk/glassbrown01a.vmt", glassVmt("models/props_junk/glassbrown01a"));
	test::writeText(s.game / "materials/models/props_junk/glassbrown01b.vmt", glassVmt("models/props_junk/glassbrown01b"));
	s.model("models/props_junk/glassjug01.mdl", {"GlassBrown01a"});
	s.model("models/props_junk/glassjug02.mdl", {"GlassBrown01b", "GlassBrown01a"});   // a second skin, sharing the first's
	// Too short for its own name: left alone.
	test::writeText(s.game / "materials/models/props_junk/ab12.vmt", glassVmt("models/props_junk/ab12"));
	s.model("models/props_junk/jar_short.mdl", {"ab12"});
	// A material named like a container gets the liquid itself, no copy.
	test::writeText(s.game / "materials/models/props_junk/glassbottle01a.vmt", glassVmt("models/props_junk/glassbottle01a"));
	s.model("models/props_junk/glassbottle01a.mdl", {"GlassBottle01a"});
	// No container by its file name: never copied, even on the same material.
	s.model("models/props_junk/window_pane.mdl", {"GlassBrown01a"});
	// Foliage is never a container.
	s.model("models/props_foliage/jug_plant.mdl", {"GlassBrown01a"});
	s.mount();

	const auto scan = findContainerModels(s.fs, s.mod);
	REQUIRE(scan.models.size() == 2);
	CHECK(scan.models[0].model == "models/props_junk/glassjug01.mdl");
	CHECK(scan.models[0].own == std::vector<std::string>{"materials/models/props_junk/glassbrow_flq.vmt"});
	CHECK(scan.models[1].model == "models/props_junk/glassjug02.mdl");
	CHECK(scan.models[1].own ==
	      std::vector<std::string>{"materials/models/props_junk/glassbrow_fq1.vmt", "materials/models/props_junk/glassbrow_flq.vmt"});
	const auto table = readMdlMaterialTable(scan.models[1].bytes);
	REQUIRE(table);
	CHECK(table->textures[0].name == "GlassBrow_fq1");
	CHECK(table->textures[1].name == "GlassBrow_flq");

	// The mod's own model at that place and the mod's own material by that name stay untouched.
	test::writeText(s.mod / "models/props_junk/glassjug01.mdl", "the mod's own model");
	test::writeText(s.mod / "materials/models/props_junk/glassbrow_fq1.vmt", "\"VertexLitGeneric\"\n{\n}\n");
	s.fs.noteWrittenFile(s.mod / "models/props_junk/glassjug01.mdl");
	s.fs.noteWrittenFile(s.mod / "materials/models/props_junk/glassbrow_fq1.vmt");
	const auto plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	CHECK(plan.modelChanges.empty());   // jug01: the mod's own; jug02: one of its materials is the mod's own
	CHECK(std::find(plan.keptOwn.begin(), plan.keptOwn.end(), "models/props_junk/glassbrow_fq1") != plan.keptOwn.end());
	CHECK(std::find(plan.modelNotes.begin(), plan.modelNotes.end(), "kept models/props_junk/glassjug01.mdl (the mod has its own)") !=
	      plan.modelNotes.end());
	CHECK(readAll(s.mod / "models/props_junk/glassjug01.mdl") == "the mod's own model");
}

TEST_CASE("container models: copies from an older run, and ones changed since, as deploy.ps1 treats them") {
	Setup s("list");
	test::writeText(s.game / "materials/models/props_junk/glassbrown01a.vmt", glassVmt("models/props_junk/glassbrown01a"));
	s.model("models/props_junk/glassjug01.mdl", {"GlassBrown01a"});
	s.mount();
	s.apply(planEnhancements(s.fs, s.mod, EnhancementSettings{}));
	REQUIRE(std::filesystem::exists(s.mod / "models/props_junk/glassjug01.mdl"));

	// A copy listed by an older run whose model is no container any more: removed while unchanged.
	test::writeText(s.mod / "models/props_junk/old_jug.mdl", "IDST old copy _flq");
	test::writeText(s.mod / "models/props_junk/old_vial.mdl", "changed by the mapper");
	auto list = readAll(s.mod / std::string(CONTAINER_MODELS_MANIFEST));
	list += "models/props_junk/old_jug.mdl\r\n";   // an old line without a checksum: ours when it holds _flq
	list += "models/props_junk/old_vial.mdl\t" + std::string(64, 'A') + "\r\n";
	list += "../../escape.mdl\t" + std::string(64, 'B') + "\r\n";   // never followed out of the mod
	test::writeText(s.mod / std::string(CONTAINER_MODELS_MANIFEST), list);
	auto plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	REQUIRE(plan.modelChanges.size() == 1);
	CHECK(plan.modelChanges[0].remove);
	CHECK(plan.modelChanges[0].model == "models/props_junk/old_jug.mdl");
	CHECK(std::find(plan.modelNotes.begin(), plan.modelNotes.end(), "kept models/props_junk/old_vial.mdl (changed since it was made)") !=
	      plan.modelNotes.end());
	REQUIRE(plan.modelsManifest);   // the old lines are gone from the list
	CHECK(plan.modelsManifest->find("old_") == std::string::npos);
	s.apply(plan);
	CHECK_FALSE(std::filesystem::exists(s.mod / "models/props_junk/old_jug.mdl"));
	CHECK(std::filesystem::exists(s.mod / "models/props_junk/old_vial.mdl"));

	// The jug's copy changed by hand since: kept, and no longer listed.
	test::writeText(s.mod / "models/props_junk/glassjug01.mdl", "edited");
	s.fs.noteWrittenFile(s.mod / "models/props_junk/glassjug01.mdl");
	plan = planEnhancements(s.fs, s.mod, EnhancementSettings{});
	CHECK(plan.modelChanges.empty());
	CHECK(plan.modelsManifest == std::string());
	CHECK(std::find(plan.modelNotes.begin(), plan.modelNotes.end(), "kept models/props_junk/glassjug01.mdl (changed since it was made)") !=
	      plan.modelNotes.end());

	// Liquid off for the whole mod: no copies, the own material kept off.
	s.apply(plan);
	std::filesystem::remove(s.mod / "models/props_junk/glassjug01.mdl");
	s.fs.noteRemovedFile(s.mod / "models/props_junk/glassjug01.mdl");
	EnhancementSettings off;
	off.off.insert(GameEffect::Liquid);
	plan = planEnhancements(s.fs, s.mod, off);
	CHECK(plan.modelChanges.empty());
	CHECK(plan.liquidModels.empty());
	REQUIRE(plan.changes.size() == 1);
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, plan.changes[0].text));
}
