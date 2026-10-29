// test_map_tools.cpp - Map budget against the engine limits and "Make water".

#include <doctest/doctest.h>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/filesystem.h>
#include <sme/history.h>
#include <sme/layout_checks.h>
#include <sme/map_budget.h>
#include <sme/map_tools.h>
#include <sme/recent_projects.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

TEST_CASE("starter room: a closed room with light, player start and cubemap, one undo step") {
	MapDocument doc;
	edit::StarterRoomMaterials materials;
	materials.walls = "BRICK/WALL";
	History history;
	history.push(edit::starterRoom(doc, materials, {512, 384, 256}));
	REQUIRE(doc.world.solids.size() == 6);
	Bounds all;
	for (const auto& solid : doc.world.solids) {
		for (const auto& corner : solidCorners(solid)) all.add(corner);
	}
	CHECK(all.min == Vec3{-272, -208, -16});   // 16 units of wall around 512 x 384 x 256 inside
	CHECK(all.max == Vec3{272, 208, 272});
	int walls = 0;
	for (const auto& solid : doc.world.solids) {
		if (solid.sides.front().material == "BRICK/WALL") ++walls;
	}
	CHECK(walls == 4);
	std::vector<std::string> classes;
	for (const auto& entity : doc.entities) classes.push_back(entity.classname());
	CHECK(classes == std::vector<std::string>{"info_player_start", "light", "env_cubemap"});
	// Everything inside the room.
	for (const auto& entity : doc.entities) {
		const Vec3 o = entity.origin();
		CHECK(o.x > -256);
		CHECK(o.x < 256);
		CHECK(o.z > 0);
		CHECK(o.z < 256);
	}
	history.undo(doc);
	CHECK(doc.world.solids.empty());
	CHECK(doc.entities.empty());
}

namespace {

/// Flood fill from every point entity through 16-unit cells of air: true when it reaches the
/// edge of the map's box (what VBSP reports as a leak). Brushes count as their bounding boxes.
bool leaks(const MapDocument& doc) {
	std::vector<Bounds> solids;
	Bounds all;
	for (const auto& solid : doc.world.solids) {
		Bounds box;
		for (const auto& corner : solidCorners(solid)) box.add(corner);
		solids.push_back(box);
		all.add(box.min);
		all.add(box.max);
	}
	const double cell = 16;
	const Vec3 lo = all.min - Vec3{32, 32, 32};
	const int nx = int((all.max.x - lo.x + 32) / cell) + 1, ny = int((all.max.y - lo.y + 32) / cell) + 1, nz = int((all.max.z - lo.z + 32) / cell) + 1;
	std::vector<std::uint8_t> state(std::size_t(nx) * ny * nz, 0);   // 0 unknown, 1 solid, 2 reached
	const auto index = [&](int x, int y, int z) { return (std::size_t(z) * ny + y) * nx + x; };
	for (const auto& box : solids) {
		const int x0 = int(std::ceil((box.min.x - lo.x) / cell - 0.5)), x1 = int(std::floor((box.max.x - lo.x) / cell - 0.5));
		const int y0 = int(std::ceil((box.min.y - lo.y) / cell - 0.5)), y1 = int(std::floor((box.max.y - lo.y) / cell - 0.5));
		const int z0 = int(std::ceil((box.min.z - lo.z) / cell - 0.5)), z1 = int(std::floor((box.max.z - lo.z) / cell - 0.5));
		for (int z = z0; z <= z1; ++z)
			for (int y = y0; y <= y1; ++y)
				for (int x = x0; x <= x1; ++x) state[index(x, y, z)] = 1;
	}
	std::vector<std::size_t> open;
	for (const auto& entity : doc.entities) {
		const Vec3 o = entity.origin() + Vec3{0, 0, 8};
		const auto i = index(int((o.x - lo.x) / cell), int((o.y - lo.y) / cell), int((o.z - lo.z) / cell));
		if (state[i] == 0) {
			state[i] = 2;
			open.push_back(i);
		}
	}
	while (!open.empty()) {
		const std::size_t i = open.back();
		open.pop_back();
		const int x = int(i % nx), y = int((i / nx) % ny), z = int(i / (std::size_t(nx) * ny));
		if (x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == nz - 1) return true;
		for (const auto& [dx, dy, dz] : std::initializer_list<std::array<int, 3>>{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}) {
			const auto j = index(x + dx, y + dy, z + dz);
			if (state[j] == 0) {
				state[j] = 2;
				open.push_back(j);
			}
		}
	}
	return false;
}

int countClass(const MapDocument& doc, std::string_view classname) {
	return int(std::count_if(doc.entities.begin(), doc.entities.end(), [&](const Entity& e) { return e.classname() == classname; }));
}

const Entity* firstOfClass(const MapDocument& doc, std::string_view classname) {
	for (const auto& entity : doc.entities) {
		if (entity.classname() == classname) return &entity;
	}
	return nullptr;
}

} // namespace

TEST_CASE("tutorial map: inside, tunnel and outside under the sky, sealed, everything to play with") {
	MapDocument doc;
	edit::TutorialMapOptions options;
	options.materials.ground = "NATURE/GRASS";
	History history;
	history.push(edit::tutorialMap(doc, options));
	const edit::TutorialMapLayout layout;

	// Sealed: no way from any entity out of the map, also through the doorways.
	CHECK_FALSE(leaks(doc));
	// The flood fill sees a leak when one is there: a hole in the outside wall's sky part.
	{
		MapDocument holed = doc;
		const auto skyWall = std::find_if(holed.world.solids.begin(), holed.world.solids.end(), [&](const Solid& solid) {
			Bounds box;
			for (const auto& corner : solidCorners(solid)) box.add(corner);
			return solid.sides.front().material == "TOOLS/TOOLSSKYBOX" && box.min.x < layout.outdoor.min.x && box.max.z > 512;
		});
		REQUIRE(skyWall != holed.world.solids.end());
		holed.world.solids.erase(skyWall);
		CHECK(leaks(holed));
	}

	// Sky: the 2D sky, sky brushes above the outside, the light that fits the sky.
	CHECK(doc.world.get("skyname") == "sky_day01_01");
	int skyBrushes = 0, grass = 0;
	for (const auto& solid : doc.world.solids) {
		skyBrushes += solid.sides.front().material == "TOOLS/TOOLSSKYBOX";
		grass += solid.sides.front().material == "NATURE/GRASS";
	}
	CHECK(skyBrushes >= 5);
	CHECK(grass >= 2);
	const auto* sun = firstOfClass(doc, "light_environment");
	REQUIRE(sun);
	CHECK(sun->get("_light") == "237 218 143 800");
	CHECK(firstOfClass(doc, "env_sun"));
	CHECK(firstOfClass(doc, "shadow_control"));
	CHECK_FALSE(firstOfClass(doc, "env_global_light"));   // only for Mapbase
	CHECK_FALSE(firstOfClass(doc, "sky_camera"));         // 3D skybox only when chosen

	// Three areas, each with its soundscape and cubemap; the player with suit, weapons, ammo.
	CHECK(countClass(doc, "env_soundscape") == 3);
	CHECK(countClass(doc, "env_cubemap") == 3);
	CHECK(countClass(doc, "info_player_start") == 1);
	const auto* suit = firstOfClass(doc, "item_suit");
	REQUIRE(suit);
	CHECK(suit->get("spawnflags") == "1");   // short logon
	const Vec3 start = firstOfClass(doc, "info_player_start")->origin();
	for (const char* weapon : {"weapon_crowbar", "weapon_pistol", "weapon_smg1", "weapon_shotgun"}) {
		const auto* entity = firstOfClass(doc, weapon);
		REQUIRE(entity);
		CHECK(std::abs(entity->origin().x - start.x) < 1);   // on the start spot: taken at once
		CHECK(std::abs(entity->origin().y - start.y) < 1);
	}
	CHECK(countClass(doc, "item_ammo_crate") == 3);
	// The citizen is a fixed model, the target has no weapon, no grenades and stays neutral.
	const auto* citizen = firstOfClass(doc, "npc_citizen");
	REQUIRE(citizen);
	CHECK(citizen->get("citizentype") == "4");
	CHECK_FALSE(citizen->get("model").empty());
	const auto* target = firstOfClass(doc, "npc_combine_s");
	REQUIRE(target);
	CHECK(target->get("additionalequipment") == "0");
	CHECK(target->get("NumGrenades") == "0");
	int peace = 0;
	for (const auto& entity : doc.entities) {
		if (entity.classname() == "ai_relationship" && entity.get("subject") == target->get("targetname") && entity.get("disposition") == "4") ++peace;
	}
	CHECK(peace == 2);
	CHECK(countClass(doc, "info_node") >= 10);
	CHECK(countClass(doc, "prop_physics") >= 3);

	// Where things are: the player inside, the target outside, nothing stuck in a brush.
	const auto inside = [](const Bounds& box, const Vec3& p) {
		return p.x > box.min.x && p.x < box.max.x && p.y > box.min.y && p.y < box.max.y && p.z >= box.min.z && p.z < box.max.z;
	};
	CHECK(inside(layout.indoor, start));
	CHECK(inside(layout.outdoor, target->origin()));
	for (const auto& entity : doc.entities) {
		const Vec3 o = entity.origin();
		INFO(entity.classname());
		CHECK((inside(layout.indoor, o) || inside(layout.tunnel, o) || inside(layout.outdoor, o)));
	}
	CHECK_FALSE(boxInSolid(doc, {start + Vec3{-16, -16, 1}, start + Vec3{16, 16, 72}}));
	CHECK_FALSE(boxInSolid(doc, {target->origin() + Vec3{-16, -16, 1}, target->origin() + Vec3{16, 16, 72}}));
	// The layout checks find nothing to complain about (player start, void, lightmaps, nodes).
	CHECK(checkLayout(doc, {}).empty());
	// Small: far below the engine's limits.
	CHECK(doc.world.solids.size() < 80);
	for (const auto& item : mapBudget(doc, BudgetLimits{})) {
		INFO(item.name);
		CHECK_FALSE(item.over());
	}

	history.undo(doc);
	CHECK(doc.world.solids.empty());
	CHECK(doc.entities.empty());
}

TEST_CASE("tutorial map: 3D skybox, Mapbase's global light, soundscapes from the game's list") {
	MapDocument doc;
	edit::TutorialMapOptions options;
	options.skybox3d = true;
	options.globalLight = true;
	(void)edit::tutorialMap(doc, options);
	const auto* camera = firstOfClass(doc, "sky_camera");
	REQUIRE(camera);
	CHECK(camera->get("scale") == "16");
	CHECK(firstOfClass(doc, "env_global_light"));
	CHECK_FALSE(leaks(doc));   // the skybox room is sealed too
	// The camera's room lies below the map, apart from it.
	CHECK(camera->origin().z < -1024);

	const std::vector<std::string> names = {"d1_canals.util_drips", "d1_trainstation.util_city", "d1_town.Indoor_house", "ep2_caves.util_tunnel",
	                                        "coast.wind_canyon"};
	CHECK(edit::pickSoundscape(names, edit::SoundscapeArea::Indoor) == "d1_town.Indoor_house");
	CHECK(edit::pickSoundscape(names, edit::SoundscapeArea::Tunnel) == "ep2_caves.util_tunnel");
	CHECK(edit::pickSoundscape(names, edit::SoundscapeArea::Outdoor) == "d1_trainstation.util_city");
	CHECK(edit::pickSoundscape({"Nothing"}, edit::SoundscapeArea::Tunnel).empty());
}

TEST_CASE("recent projects: newest first, one entry per map, kept as text") {
	std::vector<RecentProject> list;
	list = rememberProject(list, {"sourcemods/a", "C:/maps/one.vmf", 10});
	list = rememberProject(list, {"sourcemods/b", "C:/maps/two.vmf", 20});
	list = rememberProject(list, {"sourcemods/a", "c:\\MAPS\\one.vmf", 30});   // the same map again
	REQUIRE(list.size() == 2);
	CHECK(list[0].time == 30);
	CHECK(list[1].mapPath == "C:/maps/two.vmf");
	CHECK(rememberProject(list, {"x", "", 40}) == list);   // a map never saved is not a project
	for (int i = 0; i < 20; ++i) list = rememberProject(list, {"g", "/m/" + std::to_string(i) + ".vmf", i});
	CHECK(list.size() == 12);
	CHECK(list.front().mapPath == "/m/19.vmf");
	const auto text = writeRecentProjects(list);
	CHECK(parseRecentProjects(text) == list);
	CHECK(parseRecentProjects("broken\nx\ty\n12\tsourcemods/c\t/maps/c.vmf\n").size() == 1);
	CHECK(forgetProject(list, "/M/19.VMF").size() == 11);
	CHECK(writeRecentProjects({{"bad\tkey", "/a.vmf", 1}}).empty());
}
