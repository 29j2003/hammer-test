// test_vmf_roundtrip.cpp - VMFs of every Hammer flavour survive a load and save unchanged.
//
// The maps here are written by hand in the layouts the different Hammers save (Source SDK 2013
// Hammer with CRLF, Hammer++, the Portal 2 / CS:GO Hammer with ESC-separated outputs, and files
// with blocks nobody here knows, as newer branches like Strata's may write). Opening and saving
// must give the same bytes back; editing one brush must change that brush and nothing else.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/map.h>

#include <random>
#include <string>

using namespace sme;

namespace {

/// A box brush in Hammer's layout. `between` goes after the plane of each side (Hammer++ writes
/// its vertices_plus there), `numbers` replaces the plain plane/axis text of side 1.
std::string boxSolid(int id, int firstSide, int x, int y, int z, int size, const std::string& indent,
                     const std::string& between = {}, const std::string& sideExtra = {}) {
	const int a = x, b = x + size, c = y, d = y + size, e = z, f = z + size;
	const auto n = [](int v) { return std::to_string(v); };
	const std::string planes[6] = {
		"(" + n(a) + " " + n(d) + " " + n(f) + ") (" + n(b) + " " + n(d) + " " + n(f) + ") (" + n(b) + " " + n(c) + " " + n(f) + ")",
		"(" + n(a) + " " + n(c) + " " + n(e) + ") (" + n(b) + " " + n(c) + " " + n(e) + ") (" + n(b) + " " + n(d) + " " + n(e) + ")",
		"(" + n(a) + " " + n(d) + " " + n(f) + ") (" + n(a) + " " + n(c) + " " + n(f) + ") (" + n(a) + " " + n(c) + " " + n(e) + ")",
		"(" + n(b) + " " + n(d) + " " + n(e) + ") (" + n(b) + " " + n(c) + " " + n(e) + ") (" + n(b) + " " + n(c) + " " + n(f) + ")",
		"(" + n(b) + " " + n(d) + " " + n(f) + ") (" + n(a) + " " + n(d) + " " + n(f) + ") (" + n(a) + " " + n(d) + " " + n(e) + ")",
		"(" + n(b) + " " + n(c) + " " + n(e) + ") (" + n(a) + " " + n(c) + " " + n(e) + ") (" + n(a) + " " + n(c) + " " + n(f) + ")",
	};
	const std::string axes[6][2] = {{"[1 0 0 0] 0.25", "[0 -1 0 0] 0.25"}, {"[1 0 0 0] 0.25", "[0 -1 0 0] 0.25"},
	                                {"[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"}, {"[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"},
	                                {"[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"}, {"[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"}};
	const std::string i1 = indent + "\t", i2 = indent + "\t\t";
	std::string out = indent + "solid\n" + indent + "{\n" + i1 + "\"id\" \"" + n(id) + "\"\n";
	for (int s = 0; s < 6; ++s) {
		out += i1 + "side\n" + i1 + "{\n";
		out += i2 + "\"id\" \"" + n(firstSide + s) + "\"\n";
		out += i2 + "\"plane\" \"" + planes[s] + "\"\n";
		out += between;
		out += i2 + "\"material\" \"DEV/DEV_MEASUREGENERIC01B\"\n";
		out += i2 + "\"uaxis\" \"" + axes[s][0] + "\"\n";
		out += i2 + "\"vaxis\" \"" + axes[s][1] + "\"\n";
		out += i2 + "\"rotation\" \"0\"\n";
		out += i2 + "\"lightmapscale\" \"16\"\n";
		out += i2 + "\"smoothing_groups\" \"0\"\n";
		if (s == 0) out += sideExtra;
		out += i1 + "}\n";
	}
	out += i1 + "editor\n" + i1 + "{\n" + i2 + "\"color\" \"0 180 225\"\n" + i2 + "\"visgroupshown\" \"1\"\n" + i2 +
	       "\"visgroupautoshown\" \"1\"\n" + i1 + "}\n";
	out += indent + "}\n";
	return out;
}

/// A power 2 displacement block (5 x 5 vertices) as Hammer writes it.
std::string dispInfo(const std::string& indent, bool triangleTags) {
	const std::string i1 = indent + "\t", i2 = indent + "\t\t";
	std::string out = indent + "dispinfo\n" + indent + "{\n";
	out += i1 + "\"power\" \"2\"\n" + i1 + "\"startposition\" \"[-64 -64 64]\"\n" + i1 + "\"flags\" \"0\"\n" + i1 +
	       "\"elevation\" \"0\"\n" + i1 + "\"subdiv\" \"0\"\n";
	const auto rows = [&](const std::string& name, const std::string& value, int perRow) {
		out += i1 + name + "\n" + i1 + "{\n";
		for (int r = 0; r < 5; ++r) {
			std::string row;
			for (int v = 0; v < perRow; ++v) row += (v ? " " : "") + value;
			out += i2 + "\"row" + std::to_string(r) + "\" \"" + row + "\"\n";
		}
		out += i1 + "}\n";
	};
	rows("normals", "0 0 1", 5);
	rows("distances", "12.5", 5);
	rows("offsets", "0 0 0", 5);
	rows("offset_normals", "0 0 1", 5);
	rows("alphas", "0", 5);
	if (triangleTags) {
		out += i1 + "triangle_tags\n" + i1 + "{\n";
		for (int r = 0; r < 4; ++r) out += i2 + "\"row" + std::to_string(r) + "\" \"9 9 9 9 9 9 9 9\"\n";
		out += i1 + "}\n";
	}
	out += i1 + "allowed_verts\n" + i1 + "{\n" + i2 + "\"10\" \"-1 -1 -1 -1 -1 -1 -1 -1 -1 -1\"\n" + i1 + "}\n";
	out += indent + "}\n";
	return out;
}

std::string toCrlf(const std::string& text) {
	std::string out;
	for (const char c : text) {
		if (c == '\n') out += '\r';
		out += c;
	}
	return out;
}

/// Source SDK 2013 Hammer: CRLF, hidden brushes between visible ones, a group, a hidden entity
/// among the entities, visgroups, cameras and a cordon.
std::string sdk2013Map() {
	std::string m = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"8864\"\n\t\"mapversion\" \"12\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"0\"\n}\n";
	m += "visgroups\n{\n\tvisgroup\n\t{\n\t\t\"name\" \"Lights\"\n\t\t\"visgroupid\" \"5\"\n\t\t\"color\" \"231 156 129\"\n\t\tvisgroup\n\t\t{\n\t\t\t\"name\" \"Lamps\"\n\t\t\t\"visgroupid\" \"6\"\n\t\t\t\"color\" \"98 117 236\"\n\t\t}\n\t}\n}\n";
	m += "viewsettings\n{\n\t\"bSnapToGrid\" \"1\"\n\t\"bShowGrid\" \"1\"\n\t\"bShowLogicalGrid\" \"0\"\n\t\"nGridSpacing\" \"16\"\n\t\"bShow3DGrid\" \"0\"\n}\n";
	m += "world\n{\n\t\"id\" \"1\"\n\t\"mapversion\" \"12\"\n\t\"classname\" \"worldspawn\"\n\t\"detailmaterial\" \"detail/detailsprites\"\n\t\"detailvbsp\" \"detail.vbsp\"\n\t\"maxpropscreenwidth\" \"-1\"\n\t\"skyname\" \"sky_day01_01\"\n";
	m += boxSolid(2, 1, -64, -64, 0, 64, "\t", {}, dispInfo("\t\t\t", false));
	m += "\thidden\n\t{\n" + boxSolid(3, 7, 64, -64, 0, 64, "\t\t") + "\t}\n";
	m += boxSolid(4, 13, -64, 64, 0, 64, "\t");
	m += "\tgroup\n\t{\n\t\t\"id\" \"20\"\n\t\teditor\n\t\t{\n\t\t\t\"color\" \"0 100 0\"\n\t\t\t\"visgroupshown\" \"1\"\n\t\t\t\"visgroupautoshown\" \"1\"\n\t\t}\n\t}\n";
	m += "}\n";
	m += "entity\n{\n\t\"id\" \"30\"\n\t\"classname\" \"light\"\n\t\"_light\" \"255 255 255 200\"\n\t\"_quadratic_attn\" \"1\"\n\t\"targetname\" \"lamp\"\n\t\"origin\" \"0 0 96\"\n";
	m += "\tconnections\n\t{\n\t\t\"OnUser1\" \"lamp,TurnOff,,0.5,-1\"\n\t\t\"OnUser2\" \"lamp,TurnOn,,0.00,1\"\n\t}\n";
	m += "\teditor\n\t{\n\t\t\"color\" \"220 30 220\"\n\t\t\"visgroupid\" \"6\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 1000]\"\n\t}\n}\n";
	m += "hidden\n{\n\tentity\n\t{\n\t\t\"id\" \"31\"\n\t\t\"classname\" \"info_target\"\n\t\t\"origin\" \"16 16 16\"\n\t\teditor\n\t\t{\n\t\t\t\"color\" \"220 30 220\"\n\t\t\t\"visgroupshown\" \"1\"\n\t\t\t\"visgroupautoshown\" \"1\"\n\t\t\t\"logicalpos\" \"[0 1500]\"\n\t\t}\n\t}\n}\n";
	m += "entity\n{\n\t\"id\" \"32\"\n\t\"classname\" \"func_detail\"\n" + boxSolid(33, 19, 128, 0, 0, 32, "\t") +
	     "\teditor\n\t{\n\t\t\"color\" \"0 180 0\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 2000]\"\n\t}\n}\n";
	m += "cameras\n{\n\t\"activecamera\" \"0\"\n\tcamera\n\t{\n\t\t\"position\" \"[-247.5 -301.25 172]\"\n\t\t\"look\" \"[-187.162 -244.8 140.3]\"\n\t}\n}\n";
	m += "cordon\n{\n\t\"mins\" \"(-1024 -1024 -1024)\"\n\t\"maxs\" \"(1024 1024 1024)\"\n\t\"active\" \"0\"\n}\n";
	return toCrlf(m);
}

/// Hammer++: a block inside every side after its plane, numbers with many digits, the cordons block.
std::string hammerPlusPlusMap() {
	const std::string verts = "\t\t\tvertices_plus\n\t\t\t{\n\t\t\t\t\"v\" \"-64 -64 64\"\n\t\t\t\t\"v\" \"-64 64 64\"\n\t\t\t\t\"v\" \"64 64 64\"\n\t\t\t}\n";
	std::string m = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"9540\"\n\t\"mapversion\" \"3\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"0\"\n}\n";
	m += "visgroups\n{\n}\nviewsettings\n{\n\t\"bSnapToGrid\" \"1\"\n\t\"bShowGrid\" \"1\"\n\t\"bShowLogicalGrid\" \"0\"\n\t\"nGridSpacing\" \"8\"\n\t\"bShow3DGrid\" \"0\"\n}\n";
	m += "world\n{\n\t\"id\" \"1\"\n\t\"mapversion\" \"3\"\n\t\"classname\" \"worldspawn\"\n\t\"skyname\" \"sky_day01_01\"\n";
	m += boxSolid(2, 1, -64, -64, 0, 128, "\t", verts);
	// A rotated brush face: Hammer++ keeps more digits than Hammer.
	m += "\tsolid\n\t{\n\t\t\"id\" \"9\"\n";
	const char* planes[] = {"(-45.2548339959 45.2548339959 64) (45.2548339959 45.2548339959 64) (45.2548339959 -45.2548339959 64)",
	                        "(-45.2548339959 -45.2548339959 0) (45.2548339959 -45.2548339959 0) (45.2548339959 45.2548339959 0)",
	                        "(-45.2548339959 45.2548339959 64) (-45.2548339959 -45.2548339959 64) (-45.2548339959 -45.2548339959 0)",
	                        "(45.2548339959 45.2548339959 0) (45.2548339959 -45.2548339959 0) (45.2548339959 -45.2548339959 64)",
	                        "(45.2548339959 45.2548339959 64) (-45.2548339959 45.2548339959 64) (-45.2548339959 45.2548339959 0)",
	                        "(45.2548339959 -45.2548339959 0) (-45.2548339959 -45.2548339959 0) (-45.2548339959 -45.2548339959 64)"};
	for (int s = 0; s < 6; ++s) {
		m += "\t\tside\n\t\t{\n\t\t\t\"id\" \"" + std::to_string(40 + s) + "\"\n\t\t\t\"plane\" \"" + planes[s] + "\"\n" + verts;
		m += "\t\t\t\"material\" \"BRICK/BRICKFLOOR001A\"\n\t\t\t\"uaxis\" \"[0.707106781187 0.707106781187 0 -12.2843] 0.25\"\n";
		m += "\t\t\t\"vaxis\" \"[0.707106781187 -0.707106781187 0 3.00001] 0.25\"\n\t\t\t\"rotation\" \"45\"\n\t\t\t\"lightmapscale\" \"8\"\n\t\t\t\"smoothing_groups\" \"0\"\n\t\t}\n";
	}
	m += "\t\teditor\n\t\t{\n\t\t\t\"color\" \"0 154 231\"\n\t\t\t\"visgroupshown\" \"1\"\n\t\t\t\"visgroupautoshown\" \"1\"\n\t\t}\n\t}\n";
	m += "}\n";
	m += "entity\n{\n\t\"id\" \"50\"\n\t\"classname\" \"prop_static\"\n\t\"angles\" \"0 37.5 0\"\n\t\"model\" \"models/props_c17/oildrum001.mdl\"\n\t\"skin\" \"0\"\n\t\"solid\" \"6\"\n\t\"origin\" \"-12.25 40.000001 0\"\n";
	m += "\teditor\n\t{\n\t\t\"color\" \"255 255 0\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 0]\"\n\t}\n}\n";
	m += "cameras\n{\n\t\"activecamera\" \"-1\"\n}\n";
	m += "cordons\n{\n\t\"active\" \"0\"\n\tcordon\n\t{\n\t\t\"name\" \"cordon\"\n\t\t\"active\" \"1\"\n\t\tbox\n\t\t{\n\t\t\t\"mins\" \"(-512 -512 -128)\"\n\t\t\t\"maxs\" \"(512 512 512)\"\n\t\t}\n\t}\n}\n";
	return m;
}

/// Portal 2 / CS:GO Hammer: outputs separated by ESC, instances with replacements, a displacement
/// with triangle tags, entity comments, a negative zero in a texture shift.
std::string portal2Map() {
	std::string m = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"6157\"\n\t\"mapversion\" \"1\"\n\t\"formatversion\" \"100\"\n\t\"prefab\" \"0\"\n}\n";
	m += "visgroups\n{\n}\nviewsettings\n{\n\t\"bSnapToGrid\" \"1\"\n\t\"bShowGrid\" \"1\"\n\t\"bShowLogicalGrid\" \"0\"\n\t\"nGridSpacing\" \"64\"\n\t\"bShow3DGrid\" \"0\"\n}\n";
	m += "world\n{\n\t\"id\" \"1\"\n\t\"mapversion\" \"1\"\n\t\"classname\" \"worldspawn\"\n\t\"detailmaterial\" \"detail/detailsprites\"\n\t\"detailvbsp\" \"detail.vbsp\"\n\t\"maxblobcount\" \"250\"\n\t\"paintinmap\" \"0\"\n\t\"skyname\" \"sky_black_nofog\"\n";
	std::string solid = boxSolid(2, 1, -128, -128, -64, 128, "\t", {}, dispInfo("\t\t\t", true));
	// Portal 2's Hammer writes "-0" in a shift now and then.
	solid.replace(solid.find("[1 0 0 0] 0.25"), 14, "[1 0 0 -0] 0.25");
	m += solid + "}\n";
	m += "entity\n{\n\t\"id\" \"10\"\n\t\"classname\" \"logic_relay\"\n\t\"targetname\" \"@relay_open\"\n\t\"spawnflags\" \"0\"\n\t\"StartDisabled\" \"0\"\n\t\"origin\" \"0 0 32\"\n";
	m += "\tconnections\n\t{\n\t\t\"OnTrigger\" \"door\x1BOpen\x1B\x1B" "0\x1B-1\"\n\t\t\"OnTrigger\" \"@ding\x1BPlaySound\x1Bhello, world\x1B" "0.25\x1B" "1\"\n\t}\n";
	m += "\teditor\n\t{\n\t\t\"color\" \"0 100 250\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"comments\" \"Opens the exit door\"\n\t\t\"logicalpos\" \"[0 500]\"\n\t}\n}\n";
	m += "entity\n{\n\t\"id\" \"11\"\n\t\"classname\" \"func_instance\"\n\t\"angles\" \"0 90 0\"\n\t\"file\" \"instances/p2editor/door_exit.vmf\"\n\t\"fixup_style\" \"0\"\n\t\"targetname\" \"exit_door\"\n\t\"replace01\" \"$skin 1\"\n\t\"replace02\" \"$connections@relay_open\"\n\t\"origin\" \"256 0 0\"\n";
	m += "\teditor\n\t{\n\t\t\"color\" \"0 0 255\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 1000]\"\n\t}\n}\n";
	m += "cameras\n{\n\t\"activecamera\" \"-1\"\n}\ncordons\n{\n\t\"active\" \"0\"\n}\n";
	return m;
}

/// Parts this editor does not know (as a newer Hammer, Strata's for example, may add): an unknown
/// top-level block between entities, unknown keys and blocks in sides, brushes and entities, keys
/// after blocks, a key twice, empty blocks, values with braces, slashes and line breaks.
std::string unknownPartsMap() {
	std::string m = "versioninfo\n{\n\t\"editorversion\" \"400\"\n\t\"editorbuild\" \"10001\"\n\t\"mapversion\" \"5\"\n\t\"formatversion\" \"101\"\n\t\"prefab\" \"0\"\n\t\"futurekey\" \"yes\"\n}\n";
	m += "visgroups\n{\n}\nviewsettings\n{\n}\n";
	m += "world\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n\t\"skyname\" \"sky_day01_01\"\n\t\"comment\" \"{braces} and \\\\backslashes\\\\ and // no comment\"\n";
	std::string solid = boxSolid(2, 1, 0, 0, 0, 64, "\t", {}, "\t\t\tfuture_side_data\n\t\t\t{\n\t\t\t\t\"layer\" \"2\"\n\t\t\t\tdeeper\n\t\t\t\t{\n\t\t\t\t}\n\t\t\t}\n\t\t\t\"future_side_key\" \"0.300\"\n\t\t\t\"material\" \"DEV/SECOND_COPY\"\n");
	solid.insert(solid.find("\t\teditor"), "\t\t\"future_solid_key\" \"1\"\n");
	m += solid;
	m += "}\n";
	m += "entity\n{\n\t\"id\" \"7\"\n\t\"classname\" \"info_target\"\n\t\"origin\" \"1 2 3\"\n\teditor\n\t{\n\t\t\"color\" \"220 30 220\"\n\t}\n\t\"key_after_block\" \"x\"\n\t\"targetname\" \"twice\"\n\t\"targetname\" \"twice\"\n\t\"message\" \"two\nlines\"\n\tfuture_entity_block\n\t{\n\t\t\"a\" \"b\"\n\t}\n}\n";
	m += "future_top_level\n{\n\t\"data\" \"1\"\n}\n";
	m += "entity\n{\n\t\"id\" \"8\"\n\t\"classname\" \"logic_auto\"\n\t\"origin\" \"0 0 0\"\n\tconnections\n\t{\n\t}\n}\n";
	m += "cameras\n{\n\t\"activecamera\" \"-1\"\n}\n";
	return m;
}

/// Every line of `differences` is inside `path` (a brush that was edited).
bool allInside(const std::vector<std::string>& differences, const std::string& path) {
	for (const auto& line : differences) {
		if (!line.starts_with(path)) {
			MESSAGE("outside the edited brush: " << line);
			return false;
		}
	}
	return true;
}

} // namespace

TEST_CASE("VMF round trip: Source SDK 2013 Hammer (CRLF, hidden brushes, group, hidden entity, displacement)") {
	const auto text = sdk2013Map();
	VmfLoadReport report;
	auto doc = MapDocument::parse(text, &report);
	CHECK(report.clean());
	CHECK(doc.lineEnding == "\r\n");
	CHECK(doc.world.solids.size() == 2);   // the hidden one stays in its block
	CHECK(doc.entities.size() == 2);
	CHECK(doc.serialize() == text);
	if (doc.serialize() != text) {
		const auto out = doc.serialize();
		std::size_t i = 0;
		while (i < out.size() && i < text.size() && out[i] == text[i]) ++i;
		MESSAGE("first difference at " << i << ": [" << text.substr(i > 80 ? i - 80 : 0, 160) << "] vs [" << out.substr(i > 80 ? i - 80 : 0, 160) << "]");
	}
	CHECK(keyValueDifferences(text, doc.serialize()).empty());

	// Moving one brush changes that brush only; the hidden brush, the group, the hidden entity
	// and everything after them stay where and as they were.
	translateSolid(doc.world.solids[1], {16, 0, 0}, true);
	const auto edited = doc.serialize();
	const auto differences = keyValueDifferences(text, edited);
	CHECK_FALSE(differences.empty());
	CHECK(allInside(differences, "world #1 (id 1) > solid #2 (id 4)"));
	CHECK(edited.find("\r\n") != std::string::npos);
	CHECK(edited.find("\n") == edited.find("\r\n") + 1);
}

TEST_CASE("VMF round trip: Hammer++ (vertices_plus, many digits, cordons)") {
	const auto text = hammerPlusPlusMap();
	auto doc = MapDocument::parse(text);
	CHECK(doc.serialize() == text);
	// The numbers keep their digits: nothing is rounded or reformatted on load.
	CHECK(doc.serialize().find("-45.2548339959 45.2548339959 64") != std::string::npos);
	CHECK(doc.serialize().find("\"origin\" \"-12.25 40.000001 0\"") != std::string::npos);
	// Retexturing one face rewrites that face's material, nothing else.
	doc.world.solids[1].sides[2].material = "BRICK/BRICKWALL001A";
	const auto differences = keyValueDifferences(text, doc.serialize());
	REQUIRE(differences.size() == 1);
	CHECK(differences[0].starts_with("world #1 (id 1) > solid #2 (id 9) > side #3 (id 42) > material"));
	// Moving it rewrites its planes. Hammer++'s exact corners of the moved faces would now be wrong,
	// so they are left out (Hammer++ takes the corners from the planes then); the other brush keeps them.
	translateSolid(doc.world.solids[1], {0, 0, 8}, true);
	const auto moved = doc.serialize();
	CHECK(allInside(keyValueDifferences(text, moved), "world #1 (id 1) > solid #2 (id 9)"));
	const auto again = MapDocument::parse(moved);
	CHECK(again.world.solids[1].sides[0].extra.empty());
	REQUIRE(again.world.solids[0].sides[0].extra.size() == 1);
	CHECK(again.world.solids[0].sides[0].extra[0].key == "vertices_plus");
}

TEST_CASE("VMF round trip: Portal 2 / CS:GO Hammer (ESC outputs, instances, triangle tags, -0)") {
	const auto text = portal2Map();
	auto doc = MapDocument::parse(text);
	CHECK(doc.serialize() == text);
	for (const auto& line : keyValueDifferences(text, doc.serialize())) MESSAGE(line);
	REQUIRE(doc.entities[0].connections.size() == 2);
	CHECK(doc.entities[0].connections[1].parameter == "hello, world");   // a comma inside, thanks to ESC
	CHECK(doc.entities[0].connections[1].separator == '\x1B');
	// Changing one output rewrites that output only, still with ESC.
	doc.entities[0].connections[0].delay = 2;
	const auto differences = keyValueDifferences(text, doc.serialize());
	REQUIRE(differences.size() == 1);
	CHECK(differences[0].starts_with("entity #1 (id 10) > connections #1 > OnTrigger"));
	CHECK(doc.serialize().find("door\x1BOpen\x1B\x1B" "2\x1B-1") != std::string::npos);
	// Moving the instance changes its origin only.
	auto moved = MapDocument::parse(text);
	moved.entities[1].setOrigin({320, 0, 0});
	const auto movedDifferences = keyValueDifferences(text, moved.serialize());
	REQUIRE(movedDifferences.size() == 1);
	CHECK(movedDifferences[0] == "entity #2 (id 11) > origin: \"256 0 0\" -> \"320 0 0\"");
}

TEST_CASE("VMF round trip: unknown keys and blocks anywhere stay where they were") {
	const auto text = unknownPartsMap();
	auto doc = MapDocument::parse(text);
	CHECK(doc.serialize() == text);
	// The second "material" of side 1 is kept as it is, beside the first.
	CHECK(doc.world.solids[0].sides[0].material == "DEV/DEV_MEASUREGENERIC01B");
	// Edits elsewhere leave every unknown part alone.
	doc.entities[0].set("targetname", "renamed");
	doc.entities[1].connections.push_back({"OnMapSpawn", "renamed", "Kill", "", 0, -1});
	const auto edited = doc.serialize();
	const auto differences = keyValueDifferences(text, edited);
	REQUIRE(differences.size() == 2);
	CHECK(differences[0] == "entity #1 (id 7) > targetname: \"twice\" -> \"renamed\"");
	CHECK(differences[1] == "entity #2 (id 8) > connections #1: 1 entries added, first: OnMapSpawn");
	// A key added to an entity follows its other keys (as Hammer writes it).
	doc.entities[0].set("spawnflags", "1");
	const auto withKey = doc.serialize();
	CHECK(withKey.find("\"message\" \"two\nlines\"\n\t\"spawnflags\" \"1\"") != std::string::npos);
	CHECK(withKey.find("future_top_level") < withKey.find("\"logic_auto\""));
}

TEST_CASE("VMF round trip: what writing a value again would change is kept as read") {
	// Hand-edited and tool-written files: other spellings of the same numbers, outputs with
	// fewer or more fields, a side without the newer keys, a UTF-8 byte order mark, no final newline.
	const std::string text = "\xEF\xBB\xBF" "world\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n\tsolid\n\t{\n\t\t\"id\" \"2\"\n"
	                         "\t\tside\n\t\t{\n\t\t\t\"id\" \"1\"\n\t\t\t\"plane\" \"(0 0 64.000) (64 0 64.0) (64 -64 64)\"\n"
	                         "\t\t\t\"material\" \"A\"\n\t\t\t\"uaxis\" \"[1.0 0 0 0] 0.250\"\n\t\t\t\"vaxis\" \"[0 -1 0 0] .25\"\n\t\t}\n\t}\n}\n"
	                         "entity\n{\n\t\"id\" \"3\"\n\t\"classname\" \"logic_relay\"\n\tconnections\n\t{\n"
	                         "\t\t\"OnTrigger\" \"a,B\"\n\t\t\"OnTrigger\" \"a,B,c,1e-005,-1,extra\"\n\t\t\"OnTrigger\" \"a,B,,+0.50,1\"\n\t}\n}";
	const auto doc = MapDocument::parse(text);
	CHECK(doc.byteOrderMark);
	CHECK_FALSE(doc.finalNewline);
	CHECK(doc.serialize() == text);
	CHECK(doc.world.solids[0].sides[0].u.scale == 0.25);
	CHECK(doc.entities[0].connections[1].delay == doctest::Approx(1e-5));
}

TEST_CASE("VMF round trip: an unreadable texture axis is kept and never written twice") {
	const std::string text = "world\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n\tsolid\n\t{\n\t\t\"id\" \"2\"\n"
	                         "\t\tside\n\t\t{\n\t\t\t\"id\" \"1\"\n\t\t\t\"plane\" \"(0 0 64) (64 0 64) (64 -64 64)\"\n"
	                         "\t\t\t\"material\" \"A\"\n\t\t\t\"uaxis\" \"broken\"\n\t\t\t\"vaxis\" \"[0 -1 0 0] 0.25\"\n\t\t}\n\t}\n}\n";
	auto doc = MapDocument::parse(text);
	CHECK(doc.serialize() == text);
	// Aligning the texture writes a real axis in place of the unreadable one.
	doc.world.solids[0].sides[0].u = {{1, 0, 0}, 8, 0.5};
	const auto written = doc.serialize();
	CHECK(written.find("broken") == std::string::npos);
	CHECK(written.find("\"uaxis\" \"[1 0 0 8] 0.5\"") != std::string::npos);
	const auto first = written.find("uaxis");
	CHECK(written.find("uaxis", first + 1) == std::string::npos);
}

TEST_CASE("VMF round trip: missing or doubled ids get new ones, all else stays") {
	const std::string text = "world\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n"
	                         "\tsolid\n\t{\n\t\t\"id\" \"5\"\n\t\tside\n\t\t{\n\t\t\t\"plane\" \"(0 0 64) (64 0 64) (64 -64 64)\"\n\t\t}\n\t}\n"
	                         "\tsolid\n\t{\n\t\t\"id\" \"5\"\n\t\tside\n\t\t{\n\t\t\t\"plane\" \"(0 0 64) (64 0 64) (64 -64 64)\"\n\t\t}\n\t}\n}\n"
	                         "entity\n{\n\t\"classname\" \"info_target\"\n}\nentity\n{\n\t\"id\" \"9\"\n\t\"classname\" \"info_target\"\n}\n"
	                         "entity\n{\n\t\"id\" \"9\"\n\t\"classname\" \"info_target\"\n}\n";
	VmfLoadReport report;
	const auto doc = MapDocument::parse(text, &report);
	CHECK(report.renumbered == 3);
	CHECK(report.notes.size() == 3);
	CHECK(doc.world.solids[0].id == 5);
	CHECK(doc.world.solids[1].id > 9);
	CHECK(doc.entities[0].id > 9);
	CHECK(doc.entities[1].id == 9);
	CHECK(doc.entities[2].id > 9);
	CHECK(doc.entities[0].id != doc.entities[2].id);
	// The new ids are all that changes.
	for (const auto& line : keyValueDifferences(text, doc.serialize())) {
		CHECK((line.find("> id: ") != std::string::npos || line.ends_with("became key id")));
	}
	VmfLoadReport again;
	(void)MapDocument::parse(doc.serialize(), &again);
	CHECK(again.clean());
}

TEST_CASE("VMF round trip: random maps in Hammer's layout come back byte for byte") {
	// Random trees shaped like VMFs: brushes with valid planes, entities with keys and outputs,
	// unknown keys and blocks at random places, numbers in many spellings.
	std::mt19937 rng(20261030);
	const auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
	const char* numbers[] = {"0", "-0", "1", "64", "-128", "0.25", "0.250", "1e-005", "1.5e+2", "+3", "12.000001", "-45.2548339959", ".5"};
	const auto number = [&] { return std::string(numbers[pick(13)]); };
	const auto word = [&] {
		static const char* words[] = {"alpha", "Beta", "gamma_1", "x", "{b}", "a/b", "c\\d", "two words", "", "\x1B"};
		return std::string(words[pick(10)]);
	};
	const auto unknownBlock = [&](auto&& self, int depth) -> KvNode {
		auto node = KvNode::makeBlock("future_" + std::to_string(pick(5)));
		const int count = pick(4);
		for (int i = 0; i < count; ++i) {
			if (depth < 3 && pick(3) == 0) {
				node.children.push_back(self(self, depth + 1));
			} else {
				node.children.push_back(KvNode::makeValue(word() + std::to_string(i), pick(2) ? word() : number()));
			}
		}
		return node;
	};
	for (int round = 0; round < 200; ++round) {
		KvNode root = KvNode::makeBlock({});
		int nextId = 2, nextSide = 1;
		if (pick(2)) root.children.push_back(unknownBlock(unknownBlock, 0));
		auto world = KvNode::makeBlock("world");
		world.children.push_back(KvNode::makeValue("id", "1"));
		world.children.push_back(KvNode::makeValue("classname", "worldspawn"));
		const auto addSolid = [&](KvNode& parent) {
			auto solid = KvNode::makeBlock("solid");
			solid.children.push_back(KvNode::makeValue("id", std::to_string(nextId++)));
			const int sides = 1 + pick(6);
			for (int s = 0; s < sides; ++s) {
				auto side = KvNode::makeBlock("side");
				side.children.push_back(KvNode::makeValue("id", std::to_string(nextSide++)));
				side.children.push_back(KvNode::makeValue("plane", "(" + number() + " 0 " + number() + ") (64 " + number() + " 0) (0 64 " +
				                                                       std::to_string(1 + pick(100)) + ")"));
				if (pick(4) == 0) side.children.push_back(unknownBlock(unknownBlock, 0));
				side.children.push_back(KvNode::makeValue("material", "TOOLS/TOOLSNODRAW"));
				if (pick(5)) side.children.push_back(KvNode::makeValue("uaxis", "[1 " + number() + " 0 " + number() + "] " + number()));
				if (pick(5)) side.children.push_back(KvNode::makeValue("vaxis", "[0 -1 " + number() + " 0] 0.25"));
				if (pick(3)) side.children.push_back(KvNode::makeValue("rotation", number()));
				if (pick(3)) side.children.push_back(KvNode::makeValue("lightmapscale", std::to_string(1 << pick(6))));
				if (pick(3)) side.children.push_back(KvNode::makeValue("smoothing_groups", std::to_string(pick(3))));
				if (pick(4) == 0) side.children.push_back(KvNode::makeValue("unknown_side_key", word()));
				if (pick(6) == 0) std::swap(side.children[1 + pick(int(side.children.size()) - 1)], side.children.back());
				solid.children.push_back(std::move(side));
			}
			if (pick(2)) solid.children.push_back(unknownBlock(unknownBlock, 0));
			if (pick(3) == 0) {
				auto hidden = KvNode::makeBlock("hidden");
				hidden.children.push_back(std::move(solid));
				parent.children.push_back(std::move(hidden));
			} else {
				parent.children.push_back(std::move(solid));
			}
		};
		const int solids = pick(5);
		for (int i = 0; i < solids; ++i) addSolid(world);
		root.children.push_back(std::move(world));
		const int entities = pick(5);
		for (int i = 0; i < entities; ++i) {
			auto entity = KvNode::makeBlock("entity");
			entity.children.push_back(KvNode::makeValue("id", std::to_string(nextId++)));
			entity.children.push_back(KvNode::makeValue("classname", pick(2) ? "info_target" : "func_detail"));
			const int keys = pick(5);
			for (int k = 0; k < keys; ++k) entity.children.push_back(KvNode::makeValue(word() + "_key", pick(2) ? word() : number()));
			if (pick(2)) {
				auto connections = KvNode::makeBlock("connections");
				const char sep = pick(2) ? ',' : '\x1B';
				const int outputs = pick(4);
				for (int o = 0; o < outputs; ++o) {
					std::string value = "target" + std::string(1, sep) + "Input";
					const int fields = pick(5);
					for (int f = 0; f < fields; ++f) value += std::string(1, sep) + (f == 0 ? word() : number());
					connections.children.push_back(KvNode::makeValue("OnTrigger", value));
				}
				entity.children.push_back(std::move(connections));
			}
			if (pick(2)) addSolid(entity);
			if (pick(2)) entity.children.push_back(unknownBlock(unknownBlock, 0));
			if (pick(4) == 0) entity.children.push_back(KvNode::makeValue("late_key", word()));
			root.children.push_back(std::move(entity));
			if (pick(4) == 0) root.children.push_back(unknownBlock(unknownBlock, 0));
		}
		if (pick(2)) root.children.push_back(unknownBlock(unknownBlock, 0));
		std::string text = writeKeyValues(root);
		if (pick(2)) text = toCrlf(text);
		VmfLoadReport report;
		const auto doc = MapDocument::parse(text, &report);
		REQUIRE(report.clean());
		CHECK(doc.serialize() == text);
		if (doc.serialize() != text) {
			for (const auto& line : keyValueDifferences(text, doc.serialize())) MESSAGE(line);
			break;
		}
	}
}
