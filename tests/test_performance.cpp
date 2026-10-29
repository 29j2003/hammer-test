// test_performance.cpp - Guards against work that grows out of hand: the game enhancements scan
// of a game with thousands of materials, and a big map read, written and searched. The bounds are
// generous (CI machines are slow and shared); they catch changes that make these many times slower.

#include "fixture.h"

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/map.h>
#include <sme/material.h>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace sme;

namespace {

double secondsSince(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST_CASE("performance: the game enhancements scan of a game with thousands of materials") {
	const auto dir = std::filesystem::temp_directory_path() / "sme_perf_effects";
	std::filesystem::remove_all(dir);
	const auto game = dir / "game";
	const auto mod = dir / "mod";
	std::filesystem::create_directories(mod / "cfg");
	// 4000 brush materials, 1500 model materials, some water and bottles - like a Source game.
	for (int i = 0; i < 4000; ++i) {
		const char* kinds[] = {"brick", "concrete", "metal", "glass"};
		test::writeText(game / "materials" / kinds[i % 4] / (std::string(kinds[i % 4]) + std::to_string(i) + ".vmt"),
		                "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"" + std::string(kinds[i % 4]) + "/" + kinds[i % 4] + std::to_string(i) +
		                    "\"\n\t\"$bumpmap\" \"x/y_normal\"\n\t\"$surfaceprop\" \"brick\"\n}\n");
	}
	for (int i = 0; i < 1500; ++i) {
		const std::string folder = i % 5 ? "props_c17" : "props_junk";
		const std::string name = i % 5 ? "furniture" + std::to_string(i) : "glassbottle" + std::to_string(i);
		test::writeText(game / "materials/models" / folder / (name + ".vmt"),
		                "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/" + folder + "/" + name + "\"\n" +
		                    (i % 5 ? "" : "\t\"$translucent\" \"1\"\n") + "}\n");
	}
	for (int i = 0; i < 40; ++i) {
		test::writeText(game / "materials/nature" / ("water" + std::to_string(i) + ".vmt"),
		                "\"Water\"\n{\n\t\"$abovewater\" \"1\"\n\t\"$normalmap\" \"nature/water_normal\"\n}\n");
	}
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	{
		// What the Asset Browser does in the background when a game is chosen: every material read
		// and sorted into its kinds.
		const auto indexStart = std::chrono::steady_clock::now();
		const auto paths = fs.list("materials", ".vmt");
		std::size_t classified = 0;
		for (const auto& path : paths) {
			if (const auto material = loadMaterial(fs, path)) {
				(void)classifyMaterial(*material);
				++classified;
			}
		}
		const double indexing = secondsSince(indexStart);
		MESSAGE(paths.size() << " materials indexed in " << indexing << " s");
		CHECK(classified == paths.size());
		CHECK(indexing < 20.0);
	}
	const auto start = std::chrono::steady_clock::now();
	const auto candidates = findEffectCandidates(fs, mod);
	const double scan = secondsSince(start);
	const auto planStart = std::chrono::steady_clock::now();
	const auto plan = planEnhancements(fs, mod, EnhancementSettings{}, {}, &candidates);
	const double planning = secondsSince(planStart);
	MESSAGE("5540 materials: candidates " << scan << " s, plan " << planning << " s, " << plan.changes.size() << " changes");
	CHECK_FALSE(candidates.empty());
	CHECK(scan < 20.0);
	CHECK(planning < 20.0);
	std::filesystem::remove_all(dir);
}

TEST_CASE("performance: a big map is read and written quickly") {
	MapDocument doc;
	for (int i = 0; i < 20000; ++i) {
		const double x = (i % 200) * 80.0, y = (i / 200) * 80.0;
		Bounds b;
		b.add({x, y, 0});
		b.add({x + 64, y + 64, 64});
		doc.world.solids.push_back(makeBlock(doc, b, i % 2 ? "brick/brickwall001a" : "concrete/concretefloor001a"));
	}
	for (int i = 0; i < 4000; ++i) {
		Entity e;
		e.id = doc.newEntityId();
		e.keyValues = {{"classname", "prop_static"}, {"origin", std::to_string(i) + " 0 0"}, {"model", "models/props_c17/oildrum001.mdl"}};
		doc.entities.push_back(std::move(e));
	}
	auto start = std::chrono::steady_clock::now();
	const std::string text = doc.serialize();
	const double write = secondsSince(start);
	start = std::chrono::steady_clock::now();
	const MapDocument read = MapDocument::parse(text);
	const double parse = secondsSince(start);
	MESSAGE("20000 brushes, 4000 entities (" << text.size() / 1024 / 1024 << " MB): write " << write << " s, read " << parse << " s");
	CHECK(read.world.solids.size() == 20000);
	CHECK(read.entities.size() == 4000);
	CHECK(write < 20.0);
	CHECK(parse < 30.0);
}
