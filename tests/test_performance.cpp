// test_performance.cpp - Guards against work that grows out of hand: the game enhancements scan
// of a game with thousands of materials, and a big map read, written and searched. The bounds are
// generous (CI machines are slow and shared); they catch changes that make these many times slower.

#include "fixture.h"

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/edge_round.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/map.h>
#include <sme/material.h>
#include <sme/snapping.h>

#include <doctest/doctest.h>

#include <chrono>
#include <algorithm>
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

namespace {

/// The big map of the read/write test: 20 000 blocks in a grid.
MapDocument bigBlockMap() {
	MapDocument doc;
	for (int i = 0; i < 20000; ++i) {
		const double x = (i % 200) * 80.0, y = (i / 200) * 80.0;
		Bounds b;
		b.add({x, y, 0});
		b.add({x + 64, y + 64, 64});
		doc.world.solids.push_back(makeBlock(doc, b, i % 2 ? "brick/brickwall001a" : "concrete/concretefloor001a"));
	}
	return doc;
}

/// A side's face pointing along `normal` (the face picked for the edge tools).
int sideFacing(const Solid& solid, const Vec3& normal) {
	for (const auto& side : solid.sides) {
		if (dot(side.plane().normal, normal) > 0.99) return side.id;
	}
	return 0;
}

} // namespace

TEST_CASE("performance: a big map written the way Hammer++ writes it is read, kept and written quickly") {
	// Hammer++ writes plane points with many digits: every side differs from Mapsmith's own
	// writing and keeps its layout (the round trip's worst case for time and memory).
	const MapDocument doc = bigBlockMap();
	std::string text = doc.serialize();
	// "(0 0 64)" -> "(0.000000 0.000000 64.000000)", lines ending in CR LF (Hammer on Windows).
	std::string hammer;
	hammer.reserve(text.size() * 2);
	std::size_t at = 0;
	while (at < text.size()) {
		const auto end = std::min(text.find('\n', at), text.size());
		const std::string_view row(text.data() + at, end - at);
		const bool plane = row.find("\"plane\"") != std::string_view::npos;
		std::string number;
		const auto flush = [&] {
			if (plane && !number.empty() && number.find('.') == std::string::npos) number += ".000000";
			hammer += number;
			number.clear();
		};
		for (const char c : row) {
			if (plane && ((c >= '0' && c <= '9') || c == '-' || c == '.')) {
				number += c;
				continue;
			}
			flush();
			hammer += c;
		}
		flush();
		hammer += "\r\n";
		at = end + 1;
	}
	auto start = std::chrono::steady_clock::now();
	const MapDocument read = MapDocument::parse(hammer);
	const double parse = secondsSince(start);
	start = std::chrono::steady_clock::now();
	const std::string again = read.serialize();
	const double write = secondsSince(start);
	MESSAGE("20000 brushes as Hammer++ writes them (" << hammer.size() / 1024 / 1024 << " MB): read " << parse << " s, write " << write << " s");
	REQUIRE(read.world.solids.size() == 20000);
	CHECK(read.world.solids.front().sides.front().source != nullptr);   // the layout was kept
	CHECK(again == hammer);
	CHECK(parse < 30.0);
	CHECK(write < 20.0);
}

TEST_CASE("performance: snapping targets and edge rounding in a big map") {
	MapDocument doc = bigBlockMap();
	auto start = std::chrono::steady_clock::now();
	const auto corners = snapCorners(doc, Selection{}, Selection{}, nullptr);
	const double collect = secondsSince(start);
	CHECK(corners.size() == 20000u * 8);
	// Rounding an upright edge of a block in the middle of the map (the preview plans it again
	// after every change of radius or segments).
	const Solid& block = doc.world.solids[10100];
	const FaceRef a{{0, block.id}, sideFacing(block, {1, 0, 0})};
	const FaceRef b{{0, block.id}, sideFacing(block, {0, 1, 0})};
	MapDocument scratch;
	start = std::chrono::steady_clock::now();
	const auto plan = planEdgeRound(doc, scratch, a, b, 8, 4);
	const double rounding = secondsSince(start);
	MESSAGE("20000 brushes: snapping corners collected in " << collect << " s, an edge rounding planned in " << rounding << " s");
	CHECK(plan.ok());
	CHECK(collect < 10.0);
	CHECK(rounding < 10.0);
}
