// test_groups.cpp - VisGroups (list, members, edits with undo, show/hide, file round trip) and
// func_instance (finding the file, fixups, transform, preview, collapse).

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/instances.h>
#include <sme/visgroups.h>

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

const char* GROUPED_VMF = R"VMF(versioninfo
{
	"editorversion" "400"
	"mapversion" "3"
}
visgroups
{
	visgroup
	{
		"name" "Walls"
		"visgroupid" "4"
		"color" "100 200 50"
		visgroup
		{
			"name" "Windows"
			"visgroupid" "7"
			"color" "1 2 3"
		}
	}
}
world
{
	"id" "1"
	"classname" "worldspawn"
	solid
	{
		"id" "2"
		side
		{
			"id" "1"
			"plane" "(0 0 64) (64 0 64) (64 -64 64)"
			"material" "DEV/X"
			"uaxis" "[1 0 0 0] 0.25"
			"vaxis" "[0 -1 0 0] 0.25"
			"rotation" "0"
			"lightmapscale" "16"
			"smoothing_groups" "0"
		}
		editor
		{
			"color" "0 180 0"
			"visgroupid" "4"
			"visgroupshown" "0"
			"visgroupautoshown" "1"
		}
	}
}
entity
{
	"id" "3"
	"classname" "info_target"
	"origin" "0 0 0"
	editor
	{
		"color" "220 30 220"
		"visgroupid" "7"
		"visgroupid" "4"
		"visgroupshown" "0"
		"visgroupautoshown" "1"
	}
}
)VMF";

std::filesystem::path tempFolder(const char* name) {
	const auto dir = std::filesystem::temp_directory_path() / name;
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	return dir;
}

} // namespace

TEST_CASE("visgroups: read the list and the members of a saved map") {
	const auto doc = MapDocument::parse(GROUPED_VMF);
	const auto groups = visGroups(doc);
	REQUIRE(groups.size() == 1);
	CHECK(groups[0].name == "Walls");
	CHECK(groups[0].color == "100 200 50");
	REQUIRE(groups[0].children.size() == 1);
	CHECK(groups[0].children[0].id == 7);
	CHECK(findVisGroup(groups, 7)->name == "Windows");
	CHECK(visGroupAndChildren(groups, 4) == std::vector<int>{4, 7});

	const auto walls = visGroupMembers(doc, 4, false);
	CHECK(walls.solids.size() == 1);
	CHECK(walls.entities == std::vector<int>{3});
	const auto windows = visGroupMembers(doc, 7, false);
	CHECK(windows.solids.empty());
	CHECK(windows.entities.size() == 1);
	CHECK(visGroupIdsOf(doc.entities[0].extra) == std::vector<int>{7, 4});

	// Saved hidden: every member says visgroupshown 0.
	CHECK(hiddenVisGroupsInFile(doc) == std::set<int>{4, 7});
	// The file comes back as it was.
	CHECK(MapDocument::parse(doc.serialize()).serialize() == doc.serialize());
}

TEST_CASE("visgroups: create, add, remove, rename, colour, delete - each one undo step") {
	MapDocument doc;
	History history;
	auto [c1, a] = edit::createBlock(doc, box({0, 0, 0}, {64, 64, 64}), "X");
	history.push(std::move(c1));
	auto [c2, b] = edit::createBlock(doc, box({128, 0, 0}, {192, 64, 64}), "X");
	history.push(std::move(c2));
	Entity target;
	target.id = doc.newEntityId();
	target.set("classname", "info_target");
	target.setOrigin({0, 0, 128});
	doc.entities.push_back(target);

	Selection first;
	first.solids.push_back(a);
	first.entities.push_back(target.id);
	auto [create, id] = createVisGroup(doc, "Room", first);
	history.push(std::move(create));
	REQUIRE(visGroups(doc).size() == 1);
	CHECK(visGroups(doc)[0].name == "Room");
	CHECK(!visGroups(doc)[0].color.empty());
	CHECK(visGroupMembers(doc, id).solids == std::vector<SolidRef>{a});
	CHECK(visGroupMembers(doc, id).entities == std::vector<int>{target.id});

	// A sub-group; members of sub-groups count for the parent.
	Selection second;
	second.faces.push_back({b, doc.findSolid(b)->sides.front().id});   // a face stands for its brush
	auto [sub, subId] = createVisGroup(doc, "Detail", second, id);
	history.push(std::move(sub));
	CHECK(visGroupAndChildren(visGroups(doc), id) == std::vector<int>{id, subId});
	CHECK(visGroupMembers(doc, id).solids.size() == 2);
	CHECK(visGroupMembers(doc, id, false).solids.size() == 1);

	Selection onlyB;
	onlyB.solids.push_back(b);
	history.push(addToVisGroup(doc, id, onlyB));
	CHECK(visGroupMembers(doc, id, false).solids.size() == 2);
	history.push(addToVisGroup(doc, id, onlyB));   // already in: no change
	CHECK(visGroupIdsOf(doc.findSolid(b)->extra).size() == 2);
	history.push(removeFromVisGroup(doc, id, onlyB));
	CHECK(visGroupMembers(doc, id, false).solids.size() == 1);

	history.push(renameVisGroup(doc, id, "Big room"));
	history.push(setVisGroupColor(doc, id, "10 20 30"));
	CHECK(visGroups(doc)[0].name == "Big room");
	CHECK(visGroups(doc)[0].color == "10 20 30");

	// Hiding: the members of the group and of its sub-groups.
	const auto hidden = hiddenByVisGroups(doc, {id});
	CHECK(hidden.solids.size() == 2);
	CHECK(hidden.entities.size() == 1);
	MapDocument saved = doc;
	writeVisGroupShown(saved, {id});
	CHECK(hiddenVisGroupsInFile(MapDocument::parse(saved.serialize())) == std::set<int>{id, subId});

	// Delete: the sub-group moves up, the members only leave this group.
	history.push(deleteVisGroup(doc, id));
	REQUIRE(visGroups(doc).size() == 1);
	CHECK(visGroups(doc)[0].id == subId);
	CHECK(visGroupIdsOf(doc.findSolid(a)->extra).empty());
	CHECK(visGroupIdsOf(doc.findSolid(b)->extra) == std::vector<int>{subId});

	// Undo goes back through all of it.
	history.undo(doc);
	CHECK(visGroups(doc)[0].id == id);
	CHECK(visGroups(doc)[0].children.size() == 1);
	while (history.undo(doc)) {
	}
	CHECK(visGroups(doc).empty());
}

TEST_CASE("instances: file, settings, fixups, transform, preview, collapse") {
	const auto folder = tempFolder("sme_instances_test");
	std::filesystem::create_directories(folder / "instances");
	// The instance: a block, a named light with an output to a named relay, and a parameter.
	MapDocument instance;
	auto [c, block] = edit::createBlock(instance, box({0, 0, 0}, {64, 32, 16}), "DEV/$floor");
	(void)c;
	Entity light;
	light.id = instance.newEntityId();
	light.set("classname", "light");
	light.set("targetname", "lamp");
	light.set("_light", "$color 200");
	light.setOrigin({32, 0, 8});
	Connection output;
	output.output = "OnUser1";
	output.target = "relay";
	output.input = "Trigger";
	light.connections.push_back(output);
	Connection global;
	global.output = "OnUser2";
	global.target = "!player";
	global.input = "Kill";
	light.connections.push_back(global);
	instance.entities.push_back(light);
	Entity parms;
	parms.id = instance.newEntityId();
	parms.set("classname", "func_instance_parms");
	parms.set("parm1", "$color color255 255 0 0");
	parms.set("parm2", "$speed integer 100");
	parms.setOrigin({0, 0, 0});
	instance.entities.push_back(parms);
	{
		std::ofstream out(folder / "instances" / "lamp.vmf", std::ios::binary);
		out << instance.serialize();
	}

	// The map that uses it.
	MapDocument doc;
	Entity func;
	func.id = doc.newEntityId();
	func.set("classname", "func_instance");
	func.set("targetname", "hall1");
	func.set("file", "instances\\lamp");
	func.set("angles", "0 90 0");
	func.setOrigin({1000, 0, 0});
	func.set("replace01", "$color 0 255 0");
	doc.entities.push_back(func);

	const auto settings = instanceSettings(doc.entities.front());
	CHECK(settings.name == "hall1");
	CHECK(settings.style == FixupStyle::Prefix);
	REQUIRE(settings.replacements.size() == 1);
	CHECK(settings.replacements[0].variable == "$color");
	CHECK(settings.replacements[0].value == "0 255 0");
	CHECK(fixupName("lamp", settings) == "hall1-lamp");
	CHECK(fixupName("@global", settings) == "@global");

	const auto folders = instanceSearchFolders(folder / "my_map.vmf", {});
	const auto file = resolveInstanceFile(settings.file, folders);
	REQUIRE(file);
	CHECK(file->filename() == "lamp.vmf");
	CHECK_FALSE(resolveInstanceFile("instances/missing.vmf", folders));
	CHECK_FALSE(resolveInstanceFile("", folders));

	InstanceCache cache;
	std::string error;
	const auto loaded = cache.load(*file, &error);
	REQUIRE(loaded);
	CHECK(cache.load(*file).get() == loaded.get());   // same file and time: no second read

	const auto prepared = prepareInstance(*loaded, settings, nullptr);
	REQUIRE(prepared.entities.size() == 1);   // func_instance_parms is left out
	const auto& lamp = prepared.entities.front();
	CHECK(lamp.get("targetname") == "hall1-lamp");
	CHECK(lamp.get("_light") == "0 255 0 200");       // replaced from the func_instance
	CHECK(lamp.connections[0].target == "hall1-relay");
	CHECK(lamp.connections[1].target == "!player");   // special names stay
	// Turned 90° around Z and moved: (32, 0, 8) -> (1000, 32, 8).
	CHECK(lamp.origin().x == doctest::Approx(1000));
	CHECK(lamp.origin().y == doctest::Approx(32));
	CHECK(lamp.origin().z == doctest::Approx(8));
	const auto blockBounds = buildBrushGeometry(prepared.world.solids.front()).bounds;
	CHECK(blockBounds.min.x == doctest::Approx(1000 - 32));
	CHECK(blockBounds.max.y == doctest::Approx(64));
	CHECK(prepared.world.solids.front().sides.front().material == "DEV/$floor");   // materials are not variables

	// A default from func_instance_parms when the func_instance does not set the variable.
	auto defaults = settings;
	defaults.replacements.clear();
	CHECK(prepareInstance(*loaded, defaults, nullptr).entities.front().get("_light") == "255 0 0 200");

	const auto preview = buildInstancePreview(prepared, nullptr);
	CHECK(preview.solids.size() == 1);
	CHECK(preview.pointBoxes.size() == 1);
	CHECK(preview.bounds.max.x > 1000);

	// Collapse: the content becomes part of the map, one undo step.
	History history;
	Selection added;
	history.push(collapseInstance(doc, func.id, *loaded, nullptr, &added));
	CHECK(doc.world.solids.size() == 1);
	REQUIRE(doc.entities.size() == 1);
	CHECK(doc.entities.front().classname() == "light");
	CHECK(doc.entities.front().get("targetname") == "hall1-lamp");
	CHECK(doc.entities.front().id != light.id);
	CHECK(added.solids.size() == 1);
	CHECK(added.entities.size() == 1);
	history.undo(doc);
	CHECK(doc.world.solids.empty());
	REQUIRE(doc.entities.size() == 1);
	CHECK(doc.entities.front().classname() == "func_instance");

	// Replacements written back as replace01..NN.
	setInstanceReplacements(doc.entities.front(), {{"$a", "1"}, {"b", "two words"}});
	CHECK(doc.entities.front().get("replace01") == "$a 1");
	CHECK(doc.entities.front().get("replace02") == "$b two words");

	std::filesystem::remove_all(folder);
}
