// test_map.cpp - VMF round trip, brush geometry, transforms and undo/redo.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/history.h>
#include <sme/map.h>

using namespace sme;

namespace {

// Shaped like Hammer's output: one 128x128x128 box (with a displacement on top), a light
// with an output, a trigger brush entity, visgroups, cameras and a cordon.
constexpr std::string_view SAMPLE_VMF = R"VMF(versioninfo
{
	"editorversion" "400"
	"editorbuild" "8075"
	"mapversion" "7"
	"formatversion" "100"
	"prefab" "0"
}
visgroups
{
	visgroup
	{
		"name" "Lights"
		"visgroupid" "3"
		"color" "255 255 0"
	}
}
viewsettings
{
	"bSnapToGrid" "1"
	"nGridSpacing" "16"
}
world
{
	"id" "1"
	"mapversion" "7"
	"classname" "worldspawn"
	"skyname" "sky_day01_01"
	solid
	{
		"id" "2"
		side
		{
			"id" "1"
			"plane" "(-64 64 64) (64 64 64) (64 -64 64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
			dispinfo
			{
				"power" "2"
				"startposition" "[-64 -64 64]"
				"flags" "0"
				"elevation" "0"
				"subdiv" "0"
				normals
				{
					"row0" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row1" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row2" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row3" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
					"row4" "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1"
				}
				distances
				{
					"row0" "0 0 0 0 0"
					"row1" "0 0 0 0 0"
					"row2" "0 0 32 0 0"
					"row3" "0 0 0 0 0"
					"row4" "0 0 0 0 0"
				}
				allowed_verts
				{
					"10" "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1"
				}
			}
		}
		side
		{
			"id" "2"
			"plane" "(-64 -64 -64) (64 -64 -64) (64 64 -64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "3"
			"plane" "(-64 64 64) (-64 -64 64) (-64 -64 -64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "4"
			"plane" "(64 64 -64) (64 -64 -64) (64 -64 64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "5"
			"plane" "(64 64 64) (-64 64 64) (-64 64 -64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "6"
			"plane" "(64 -64 -64) (-64 -64 -64) (-64 -64 64)"
			"material" "DEV/DEV_MEASUREGENERIC01"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		editor
		{
			"color" "0 175 236"
			"visgroupshown" "1"
			"visgroupautoshown" "1"
		}
	}
}
entity
{
	"id" "20"
	"classname" "light"
	"_light" "255 255 255 200"
	"targetname" "lamp"
	"origin" "0 0 128"
	connections
	{
		"OnUser1" "lamp,TurnOff,,0.5,-1"
	}
	editor
	{
		"color" "220 30 220"
		"visgroupid" "3"
		"visgroupshown" "1"
		"visgroupautoshown" "1"
		"logicalpos" "[0 500]"
	}
}
entity
{
	"id" "30"
	"classname" "trigger_once"
	"spawnflags" "1"
	solid
	{
		"id" "31"
		side
		{
			"id" "40"
			"plane" "(0 32 32) (32 32 32) (32 0 32)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "41"
			"plane" "(0 0 0) (32 0 0) (32 32 0)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "42"
			"plane" "(0 32 32) (0 0 32) (0 0 0)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "43"
			"plane" "(32 32 0) (32 0 0) (32 0 32)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[0 1 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "44"
			"plane" "(32 32 32) (0 32 32) (0 32 0)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		side
		{
			"id" "45"
			"plane" "(32 0 0) (0 0 0) (0 0 32)"
			"material" "TOOLS/TOOLSTRIGGER"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 0 -1 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
	}
}
cameras
{
	"activecamera" "-1"
}
cordon
{
	"mins" "(-1024 -1024 -1024)"
	"maxs" "(1024 1024 1024)"
	"active" "0"
}
)VMF";

} // namespace

TEST_CASE("VMF parses and round-trips without losing data") {
	const auto doc = MapDocument::parse(SAMPLE_VMF);
	CHECK(doc.world.classname() == "worldspawn");
	REQUIRE(doc.world.solids.size() == 1);
	CHECK(doc.world.solids[0].sides.size() == 6);
	CHECK(doc.world.solids[0].sides[0].dispInfo());
	REQUIRE(doc.entities.size() == 2);
	CHECK(doc.entities[0].get("targetname") == "lamp");
	CHECK(doc.entities[0].origin() == Vec3{0, 0, 128});
	REQUIRE(doc.entities[0].connections.size() == 1);
	const auto& c = doc.entities[0].connections[0];
	CHECK(c.output == "OnUser1");
	CHECK(c.target == "lamp");
	CHECK(c.input == "TurnOff");
	CHECK(c.delay == doctest::Approx(0.5));
	CHECK(c.timesToFire == -1);
	CHECK(doc.entities[1].solids.size() == 1);
	CHECK(doc.headerBlocks.size() == 3);
	CHECK(doc.footerBlocks.size() == 2);
	CHECK(doc.solidCount() == 2);

	const auto written = doc.serialize();
	const auto again = MapDocument::parse(written);
	CHECK(again.serialize() == written);
	CHECK(written.find("allowed_verts") != std::string::npos);
	CHECK(written.find("\"visgroupid\" \"3\"") != std::string::npos);
	CHECK(written.find("\"OnUser1\" \"lamp,TurnOff,,0.5,-1\"") != std::string::npos);
	CHECK(written.find("\"plane\" \"(-64 64 64) (64 64 64) (64 -64 64)\"") != std::string::npos);
	CHECK(written.find("\"uaxis\" \"[1 0 0 0] 0.25\"") != std::string::npos);
	CHECK(written.find("cordon") != std::string::npos);
}

TEST_CASE("broken VMF reports a line number") {
	CHECK_THROWS_WITH_AS(MapDocument::parse("world\n{\n\"id\" \"1\"\n"), "line 4: missing '}'", std::runtime_error);
	CHECK_THROWS(MapDocument::parse("versioninfo { }"));
}

TEST_CASE("brush geometry from Hammer planes") {
	const auto doc = MapDocument::parse(SAMPLE_VMF);
	const auto& solid = doc.world.solids[0];
	CHECK(solid.sides[0].plane().normal == Vec3{0, 0, 1});          // Hammer's top face points up
	CHECK(solid.sides[0].plane().dist == doctest::Approx(64));

	const auto geometry = buildBrushGeometry(solid);
	CHECK(geometry.valid);
	REQUIRE(geometry.faces.size() == 6);
	CHECK(geometry.bounds.min == Vec3{-64, -64, -64});
	CHECK(geometry.bounds.max == Vec3{64, 64, 64});
	for (const auto& face : geometry.faces) {
		CHECK(face.vertices.size() == 4);
		// Counter-clockwise from outside: (v1 - v0) x (v2 - v0) points along the normal.
		CHECK(dot(cross(face.vertices[1] - face.vertices[0], face.vertices[2] - face.vertices[0]), face.normal) > 0);
	}
	CHECK(isConvexValid(solid));

	const auto tc = textureCoordinate(solid.sides[0], {64, -64, 64});
	CHECK(tc.u == doctest::Approx(256));
	CHECK(tc.v == doctest::Approx(256));
}

TEST_CASE("ray picking hits the nearest face") {
	const auto doc = MapDocument::parse(SAMPLE_VMF);
	const auto& solid = doc.world.solids[0];
	const auto hit = intersect(Ray{{0, 0, 500}, {0, 0, -1}}, solid);
	REQUIRE(hit);
	CHECK(hit->distance == doctest::Approx(436));
	CHECK(hit->sideIndex == 0);
	CHECK_FALSE(intersect(Ray{{500, 500, 500}, {0, 0, -1}}, solid));
	CHECK_FALSE(intersect(Ray{{0, 0, 500}, {0, 0, 1}}, solid));
}

TEST_CASE("new blocks, transforms, texture lock") {
	MapDocument doc;
	Bounds box;
	box.add({0, 0, 0});
	box.add({64, 128, 32});
	auto block = makeBlock(doc, box, "DEV/DEV_MEASUREGENERIC01");
	CHECK(block.sides.size() == 6);
	CHECK(isConvexValid(block));
	const auto geometry = buildBrushGeometry(block);
	CHECK(geometry.bounds.min == box.min);
	CHECK(geometry.bounds.max == box.max);
	for (const auto& side : block.sides) {
		const auto n = side.plane().normal;
		const auto center = box.center();
		CHECK(dot(n, side.planePoints[0] - center) > 0);                   // outward
	}

	// Moving with texture lock keeps the texture glued to the face.
	const Vec3 point{16, 16, 32};
	const auto before = textureCoordinate(block.sides[0], point);
	translateSolid(block, {8, 24, 0}, true);
	const auto after = textureCoordinate(block.sides[0], point + Vec3{8, 24, 0});
	CHECK(after.u == doctest::Approx(before.u));
	CHECK(after.v == doctest::Approx(before.v));

	// Rotation by 90 degrees stays on the grid and stays valid.
	auto rotated = block;
	rotateSolid(rotated, Mat3::rotation(2, 90), buildBrushGeometry(block).bounds.center(), true);
	CHECK(isConvexValid(rotated));
	const auto rb = buildBrushGeometry(rotated).bounds;
	CHECK(rb.size().x == doctest::Approx(128));
	CHECK(rb.size().y == doctest::Approx(64));

	// Resize into a new box.
	Bounds target;
	target.add({0, 0, 0});
	target.add({256, 256, 16});
	CHECK(scaleSolid(block, buildBrushGeometry(block).bounds, target));
	CHECK(buildBrushGeometry(block).bounds.max == Vec3{256, 256, 16});
	CHECK_FALSE(scaleSolid(block, target, Bounds{}));

	const auto copy = cloneSolid(doc, block);
	CHECK(copy.id != block.id);
	CHECK(copy.sides[0].id != block.sides[0].id);
}

TEST_CASE("invalid brush is detected") {
	MapDocument doc;
	Bounds box;
	box.add({0, 0, 0});
	box.add({64, 64, 64});
	auto block = makeBlock(doc, box, "X");
	block.sides[0].planePoints = block.sides[1].planePoints;      // two bottoms, no top: open
	CHECK_FALSE(isConvexValid(block));
}

TEST_CASE("displacement mesh") {
	const auto doc = MapDocument::parse(SAMPLE_VMF);
	const auto& solid = doc.world.solids[0];
	const auto geometry = buildBrushGeometry(solid);
	const auto top = std::find_if(geometry.faces.begin(), geometry.faces.end(), [](const FaceGeometry& f) { return f.sideIndex == 0; });
	REQUIRE(top != geometry.faces.end());
	const auto mesh = buildDisplacement(solid.sides[0], *top);
	REQUIRE(mesh);
	CHECK(mesh->size == 5);
	CHECK(mesh->positions.size() == 25);
	CHECK(mesh->positions[0] == Vec3{-64, -64, 64});                // starts at startposition
	CHECK(mesh->positions[12].z == doctest::Approx(96));             // centre raised by 32
	CHECK(mesh->positions[12].x == doctest::Approx(0));
}

TEST_CASE("undo and redo") {
	auto doc = MapDocument::parse(SAMPLE_VMF);
	History history;
	CHECK_FALSE(history.modified());

	// Create a block.
	Bounds box;
	box.add({200, 0, 0});
	box.add({264, 64, 64});
	Transaction create(doc, "Create block");
	auto block = makeBlock(doc, box, "X");
	const SolidRef ref{0, block.id};
	create.touchSolid(ref);
	doc.world.solids.push_back(block);
	history.push(create.finish());
	CHECK(doc.world.solids.size() == 2);
	CHECK(history.modified());

	// Move it, change an entity, change the world.
	Transaction move(doc, "Move");
	move.touchSolid(ref);
	translateSolid(*doc.findSolid(ref), {0, 0, 64}, true);
	move.touchEntity(20);
	doc.findEntity(20)->set("targetname", "lamp2");
	move.touchWorld();
	doc.world.set("skyname", "sky_night");
	history.push(move.finish());

	CHECK(history.undoName() == "Move");
	CHECK(history.undo(doc));
	CHECK(doc.findEntity(20)->get("targetname") == "lamp");
	CHECK(doc.world.get("skyname") == "sky_day01_01");
	CHECK(buildBrushGeometry(*doc.findSolid(ref)).bounds.min.z == doctest::Approx(0));
	CHECK(history.undo(doc));
	CHECK(doc.world.solids.size() == 1);
	CHECK_FALSE(history.modified());
	CHECK_FALSE(history.undo(doc));

	CHECK(history.redo(doc));
	CHECK(history.redo(doc));
	CHECK(doc.world.solids.size() == 2);
	CHECK(buildBrushGeometry(*doc.findSolid(ref)).bounds.min.z == doctest::Approx(64));
	CHECK(doc.findEntity(20)->get("targetname") == "lamp2");

	// Delete an entity and undo it.
	Transaction remove(doc, "Delete");
	remove.touchEntity(30);
	std::erase_if(doc.entities, [](const Entity& e) { return e.id == 30; });
	history.push(remove.finish());
	CHECK(doc.entities.size() == 1);
	history.undo(doc);
	CHECK(doc.entities.size() == 2);
	CHECK(doc.findEntity(30)->solids.size() == 1);

	// New ids never collide with existing ones.
	CHECK(doc.newSolidId() > 31);
}

TEST_CASE("number formatting") {
	CHECK(formatNumber(64) == "64");
	CHECK(formatNumber(0.25) == "0.25");
	CHECK(formatNumber(-0.0) == "0");
	CHECK(formatNumber(-128.5) == "-128.5");
}
