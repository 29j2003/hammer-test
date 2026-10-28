// test_map_tools.cpp - Map budget against the engine limits and "Make water".

#include <doctest/doctest.h>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/filesystem.h>
#include <sme/history.h>
#include <sme/map_budget.h>
#include <sme/map_tools.h>

#include <filesystem>
#include <fstream>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

int addEntity(MapDocument& doc, std::vector<std::pair<std::string, std::string>> keys, Vec3 origin = {}) {
	Entity e;
	e.id = doc.newEntityId();
	e.keyValues = std::move(keys);
	e.setOrigin(origin);
	doc.entities.push_back(std::move(e));
	return doc.entities.back().id;
}

const BudgetItem& item(const std::vector<BudgetItem>& items, const std::string& name) {
	for (const auto& i : items) {
		if (i.name == name) return i;
	}
	FAIL("no budget item " << name);
	return items.front();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("map budget counts against the engine limits") {
	CHECK(budgetLimitsForApp(220).entities == 8192);    // Half-Life 2: Source SDK 2013
	CHECK(budgetLimitsForApp(243750).entities == 8192); // Source SDK Base 2013 Multiplayer / unknown
	CHECK(budgetLimitsForApp(620).entities == 16384);   // Portal 2
	CHECK(budgetLimitsForApp(730).brushes == 8192);

	MapDocument doc;
	// Two blocks side by side share three plane pairs (top, bottom and the two sides along X).
	auto [c1, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	auto [c2, b] = edit::createBlock(doc, box({64, 0, 0}, {128, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	doc.findSolid(b)->sides[0].extra.push_back(KvNode::makeBlock("dispinfo"));
	addEntity(doc, {{"classname", "prop_static"}, {"model", "models/a.mdl"}});
	addEntity(doc, {{"classname", "prop_static"}, {"model", "models/b.mdl"}});
	addEntity(doc, {{"classname", "light"}});                                    // baked, deleted at spawn
	addEntity(doc, {{"classname", "light"}, {"targetname", "lamp"}});            // switchable: stays
	addEntity(doc, {{"classname", "env_cubemap"}});
	addEntity(doc, {{"classname", "info_overlay"}});
	addEntity(doc, {{"classname", "info_player_start"}});
	const int detail = addEntity(doc, {{"classname", "func_detail"}});
	auto [c3, ref] = edit::createBlock(doc, box({0, 0, 64}, {32, 32, 96}), "DEV/DEV_MEASUREGENERIC01");
	// Move the third block into the func_detail.
	auto* detailEntity = doc.findEntity(detail);
	detailEntity->solids.push_back(*doc.findSolid(ref));
	doc.world.solids.pop_back();

	const auto budget = mapBudget(doc, BudgetLimits{});
	CHECK(item(budget, "Brushes").count == 3);
	CHECK(item(budget, "Brushes").limit == 8192);
	CHECK(item(budget, "Brush sides").count == 18);
	CHECK(item(budget, "Brush sides").estimate);
	// Planes: x=0, x=64, x=128, y=0, y=64, z=0, z=64 (blocks 1+2), x=32, y=32, z=96 (block 3) -> 10 pairs.
	CHECK(item(budget, "Planes").count == 20);
	CHECK(item(budget, "Map entities").count == 9);   // 8 + worldspawn
	CHECK(item(budget, "Map entities").limit == 8192);
	// Edicts: world + player + named light + info_player_start (the rest is compiled away).
	CHECK(item(budget, "Edicts").count == 4);
	CHECK(item(budget, "Edicts").limit == 2048);
	CHECK(item(budget, "Overlays").count == 1);
	CHECK(item(budget, "Displacements").count == 1);
	CHECK(item(budget, "Cubemaps").count == 1);
	CHECK(item(budget, "Lights").count == 2);
	CHECK(item(budget, "Static props").count == 2);
	CHECK(item(budget, "Static props").limit == 0);
	CHECK_FALSE(item(budget, "Brushes").over());

	BudgetLimits tight;
	tight.brushes = 2;
	const auto over = mapBudget(doc, tight);
	CHECK(item(over, "Brushes").over());
	CHECK(item(over, "Brushes").fraction() == doctest::Approx(1.5));
	(void)c1; (void)c2; (void)c3; (void)a;
}

TEST_CASE("make water: material, nodraw, lod control and cubemap in one step") {
	const auto dir = std::filesystem::temp_directory_path() / "sme_make_water";
	std::filesystem::remove_all(dir);
	writeFile(dir / "materials/concrete/floor.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"concrete/floor\"\n}\n");
	writeFile(dir / "materials/nature/canal_surface.vmt", "\"Water\"\n{\n\t\"$normalmap\" \"nature/water_normal\"\n\t\"%compilewater\" \"1\"\n}\n");
	writeFile(dir / "materials/nature/water_murky.vmt", "\"Water\"\n{\n\t\"$normalmap\" \"nature/water_normal\"\n}\n");
	GameFileSystem fs;
	fs.addDirectory(dir, {"game"});
	CHECK(isWaterMaterial(fs, "nature/canal_surface"));
	CHECK_FALSE(isWaterMaterial(fs, "concrete/floor"));
	const std::vector<std::string> names{"concrete/floor", "nature/canal_surface", "nature/water_murky"};
	CHECK(pickWaterMaterial(fs, "NATURE/CANAL_SURFACE", names) == "NATURE/CANAL_SURFACE");   // the current one is water
	CHECK(pickWaterMaterial(fs, "concrete/floor", names) == "nature/water_murky");          // "water" in the name first
	CHECK(pickWaterMaterial(fs, "concrete/floor", {"concrete/floor", "nature/canal_surface"}) == "nature/canal_surface");
	CHECK(pickWaterMaterial(fs, "concrete/floor", {"concrete/floor"}).empty());
	std::filesystem::remove_all(dir);

	MapDocument doc;
	auto [c1, pool] = edit::createBlock(doc, box({0, 0, 0}, {256, 128, 48}), "CONCRETE/FLOOR");
	const std::string before = doc.serialize();
	Selection selection;
	selection.solids.push_back(pool);
	const auto result = edit::makeWater(doc, selection, "nature/water_murky", nullptr);
	CHECK(result.brushes == 1);
	REQUIRE(result.lodControlId != 0);
	REQUIRE(result.cubemapId != 0);
	const Solid* solid = doc.findSolid(pool);
	REQUIRE(solid);
	int water = 0, nodraw = 0, topId = 0;
	for (const auto& side : solid->sides) {
		if (side.material == "nature/water_murky") {
			++water;
			topId = side.id;
			CHECK(side.plane().normal.z == doctest::Approx(1));
		} else if (side.material == "TOOLS/TOOLSNODRAW") {
			++nodraw;
		}
	}
	CHECK(water == 1);
	CHECK(nodraw == 5);
	const auto* cubemap = doc.findEntity(result.cubemapId);
	REQUIRE(cubemap);
	CHECK(cubemap->classname() == "env_cubemap");
	CHECK(cubemap->get("sides") == std::to_string(topId));
	CHECK(cubemap->origin() == Vec3{128, 64, 112});   // 64 above the middle of the surface
	const auto* lod = doc.findEntity(result.lodControlId);
	REQUIRE(lod);
	CHECK(lod->classname() == "water_lod_control");
	CHECK(lod->get("cheapwaterstartdistance") == "1000");

	// One undo step brings back everything.
	History history;
	history.push(result.change);
	REQUIRE(history.undo(doc));
	CHECK(doc.serialize() == before);
	REQUIRE(history.redo(doc));
	CHECK(doc.findEntity(result.cubemapId));

	// A second pool next to the first: the lod control and the cubemap are already there.
	auto [c2, second] = edit::createBlock(doc, box({256, 0, 0}, {384, 128, 48}), "CONCRETE/FLOOR");
	Selection next;
	next.solids.push_back(second);
	const auto again = edit::makeWater(doc, next, "nature/water_murky", nullptr);
	CHECK(again.brushes == 1);
	CHECK(again.lodControlId == 0);
	CHECK(again.cubemapId == 0);

	// Nothing usable selected: nothing changes.
	const auto none = edit::makeWater(doc, Selection{}, "nature/water_murky", nullptr);
	CHECK(none.brushes == 0);
	CHECK(none.change.empty());
	(void)c1; (void)c2;
}
