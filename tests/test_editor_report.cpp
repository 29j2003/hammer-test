// test_editor_report.cpp - The editor report (sme/editor_report.h): picks.log written and read
// back, replay.txt settings, masked paths, the map around the selection with its ids, rays
// through the map by plain geometry (hidden brushes and faces left out), the road planner's notes
// and the replay of a whole report ZIP.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/editor_report.h>
#include <sme/history.h>
#include <sme/road_texture.h>
#include <sme/shipping.h>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace sme;

namespace {

SolidRef addBlock(MapDocument& doc, Vec3 a, Vec3 b, const std::string& material = "DEV/DEV_MEASUREGENERIC01") {
	Bounds box;
	box.add(a);
	box.add(b);
	doc.world.solids.push_back(makeBlock(doc, box, material));
	return {0, doc.world.solids.back().id};
}

int sideFacing(const MapDocument& doc, const SolidRef& ref, const Vec3& normal) {
	for (const auto& side : doc.findSolid(ref)->sides) {
		if (dot(side.plane().normal, normal) > 0.99) return side.id;
	}
	return 0;
}

std::vector<std::byte> bytes(const std::string& text) {
	std::vector<std::byte> out;
	for (const char c : text) out.push_back(static_cast<std::byte>(c));
	return out;
}

struct TempDir {
	std::filesystem::path path;
	TempDir() {
		path = std::filesystem::temp_directory_path() / ("sme_editor_report_" + std::to_string(std::rand()));
		std::filesystem::create_directories(path);
	}
	~TempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

} // namespace

TEST_CASE("editor report: a ZIP entry is read back; missing entries are none") {
	TempDir dir;
	const auto zip = dir.path / "r.zip";
	REQUIRE(writeZip(zip, {{"a.txt", {}, bytes("hello")}, {"sub/b.log", {}, bytes("line 1\nline 2\n")}}));
	CHECK(readZipEntry(zip, "a.txt") == std::optional<std::string>("hello"));
	CHECK(readZipEntry(zip, "sub/b.log") == std::optional<std::string>("line 1\nline 2\n"));
	CHECK_FALSE(readZipEntry(zip, "missing.txt"));
	CHECK_FALSE(readZipEntry(dir.path / "nothing.zip", "a.txt"));
}

TEST_CASE("editor report: a click's trace is written to picks.log and read back") {
	PickTrace trace;
	trace.view = "3D";
	trace.viewWidth = 1200;
	trace.viewHeight = 800;
	trace.screenX = 600.5;
	trace.screenY = 400;
	trace.tool = "Select";
	trace.modifiers = "ctrl";
	trace.ray = Ray{{12.5, -700.25, 64.000015}, {0, 1, 0}};
	trace.candidates.push_back({"brush", 0, 12, 40, 956.25, ""});
	trace.candidates.push_back({"brush", 0, 13, 50, 956.25, "from inside"});
	trace.skipped.push_back("brush 0/7: hidden");
	trace.ties = 2;
	trace.order = "0/13 edited 4 volume 100, 0/12 edited 0 volume 50";
	trace.result = "brush 0/13 side 50 at 956.25";
	trace.selection = "brushes 0/13 entities - faces -";
	const std::string text = formatPickTrace(trace, 7) + formatPickTrace(PickTrace{}, 8);
	CHECK(text.find("pick #7 view=3D size=1200x800 at=600.5,400 tool=Select mode=objects modifiers=ctrl cycle=0") != std::string::npos);
	CHECK(text.find("  hit brush 0/13 side 50 at 956.25 (from inside)") != std::string::npos);
	CHECK(text.find("  skipped brush 0/7: hidden") != std::string::npos);
	CHECK(text.find("  ties 2 (0/13 edited 4") != std::string::npos);
	const auto picks = parsePickLog(text);
	REQUIRE(picks.size() == 2);
	CHECK(picks[0].number == 7);
	CHECK(picks[0].view == "3D");
	CHECK_FALSE(picks[0].faceMode);
	CHECK(picks[0].ray.origin.x == doctest::Approx(12.5));
	CHECK(picks[0].ray.origin.y == doctest::Approx(-700.25));
	CHECK(picks[0].ray.origin.z == doctest::Approx(64.000015).epsilon(1e-9));   // Hammer++ decimals survive
	CHECK(picks[0].ray.direction.y == doctest::Approx(1));
	CHECK(picks[0].result == "brush 0/13 side 50 at 956.25");
	CHECK(picks[1].result == "nothing");
}

TEST_CASE("editor report: replay.txt settings, selections and road / flow options go there and back") {
	const std::vector<SolidRef> solids{{0, 12}, {5, 13}};
	const std::vector<FaceRef> faces{{{0, 12}, 40}, {{5, 13}, 51}};
	CHECK(formatSolids(solids) == "0/12,5/13");
	CHECK(formatFaces(faces) == "0/12/40,5/13/51");
	CHECK(parseSolids("0/12, 5/13") == solids);
	CHECK(parseFaces(formatFaces(faces)) == faces);
	CHECK(parseSolids("").empty());

	RoadOptions road;
	road.material = "ROAD/ASPHALT01";
	road.along = RoadAlong::V;
	road.flip = true;
	road.repeatsAcross = 2;
	road.roadWidth = 192;
	road.curveCorners = false;
	road.sideMaterial = "CONCRETE/KERB";
	FlowOptions flow;
	flow.mode = FlowMode::Curve;
	flow.preset = FlowPreset::Trim;
	flow.material = "METAL/TRIM";
	flow.seed = FaceRef{{0, 3}, 9};
	std::vector<std::pair<std::string, std::string>> settings{{"grid", "16"}, {"multi", "a\nb"}};
	addRoadSettings(settings, road, {1024, 512});
	addFlowSettings(settings, flow, {256, 64});
	const auto parsed = parseSettings(formatSettings(settings));
	CHECK(parsed.at("grid") == "16");
	CHECK(parsed.at("multi") == "a b");   // one line per value
	std::pair<int, int> size;
	const RoadOptions roadBack = roadSettings(parsed, &size);
	CHECK(roadBack.material == road.material);
	CHECK((roadBack.along == RoadAlong::V));
	CHECK(roadBack.flip);
	CHECK(roadBack.repeatsAcross == 2);
	CHECK(roadBack.roadWidth == doctest::Approx(192));
	CHECK_FALSE(roadBack.curveCorners);
	CHECK(roadBack.sideMaterial == road.sideMaterial);
	CHECK(size == std::pair<int, int>{1024, 512});
	const FlowOptions flowBack = flowSettings(parsed, &size);
	CHECK((flowBack.mode == FlowMode::Curve));
	CHECK((flowBack.preset == FlowPreset::Trim));
	CHECK(flowBack.material == "METAL/TRIM");
	REQUIRE(flowBack.seed);
	CHECK(*flowBack.seed == FaceRef{{0, 3}, 9});
	CHECK(size == std::pair<int, int>{256, 64});
}

TEST_CASE("editor report: home folders and user names in paths are masked") {
	CHECK(maskPersonalText("map C:\\Users\\Julius\\Documents\\a.vmf", "C:\\Users\\Julius") == "map ~\\Documents\\a.vmf");
	CHECK(maskPersonalText("map C:/Users/Julius/Documents/a.vmf", "C:\\Users\\Julius") == "map ~/Documents/a.vmf");
	CHECK(maskPersonalText("/home/julius/maps/x.vmf and /home/julius", "/home/julius") == "~/maps/x.vmf and ~");
	// Other users' folders (another drive, a copied log): the name hidden too.
	CHECK(maskPersonalText("D:\\Users\\someone\\x.txt", "/home/julius") == "D:\\Users\\<user>\\x.txt");
	CHECK(maskPersonalText("/home/other/y", "") == "/home/<user>/y");
	CHECK(maskPersonalText("D:\\GitHub\\Half-Life 2\\mapsrc", "C:\\Users\\Julius") == "D:\\GitHub\\Half-Life 2\\mapsrc");
}

TEST_CASE("editor report: the scene keeps the objects around the selection with their ids, or the whole small map") {
	MapDocument doc;
	const auto near1 = addBlock(doc, {0, 0, 0}, {128, 128, 128});
	const auto near2 = addBlock(doc, {900, 0, 0}, {1000, 128, 128});
	const auto far = addBlock(doc, {5000, 5000, 0}, {5128, 5128, 128});
	Entity light;
	light.id = doc.newEntityId();
	light.set("classname", "light");
	light.set("origin", "64 64 300");
	doc.entities.push_back(light);
	Entity farLight = light;
	farLight.id = doc.newEntityId();
	farLight.set("origin", "6000 6000 300");
	doc.entities.push_back(farLight);
	Bounds area;
	area.add({0, 0, 0});
	area.add({128, 128, 128});

	std::size_t solids = 0, entities = 0;
	const MapDocument part = reportScene(doc, area, 1024, 100, &solids, &entities);
	CHECK(solids == 2);
	CHECK(entities == 1);
	CHECK(part.findSolid(near1));
	CHECK(part.findSolid(near2));
	CHECK_FALSE(part.findSolid(far));
	REQUIRE(part.entities.size() == 1);
	CHECK(part.entities.front().id == light.id);
	// Written and read back, the ids stay (the replay talks about the same brushes as the log).
	const MapDocument again = MapDocument::parse(part.serialize());
	CHECK(again.findSolid(near2));
	CHECK(again.findSolid(near2)->sides.front().id == doc.findSolid(near2)->sides.front().id);

	const MapDocument whole = reportScene(doc, area, 1024, 10'000'000, &solids, &entities);
	CHECK(solids == 3);
	CHECK(entities == 2);
}

TEST_CASE("editor report: brushes along a ray - nearest first, hidden brushes and hidden faces left out") {
	MapDocument doc;
	const auto front = addBlock(doc, {0, 0, 0}, {128, 128, 128});
	const auto back = addBlock(doc, {0, 256, 0}, {128, 384, 128});
	const Ray ray{{64, -500, 64}, {0, 1, 0}};
	auto hits = brushesAlongRay(doc, ray);
	REQUIRE(hits.size() == 2);
	CHECK(hits[0].solid == front);
	CHECK(hits[0].distance == doctest::Approx(500));
	CHECK(hits[0].sideId == sideFacing(doc, front, {0, -1, 0}));
	CHECK(hits[1].solid == back);
	Selection hidden;
	hidden.solids = {front};
	hits = brushesAlongRay(doc, ray, &hidden);
	REQUIRE(hits.size() == 1);
	CHECK(hits[0].solid == back);
	// Its front face hidden (tool textures hidden): the ray goes on to the brush behind.
	hidden.solids.clear();
	hidden.faces = {FaceRef{front, sideFacing(doc, front, {0, -1, 0})}};
	hits = brushesAlongRay(doc, ray, &hidden);
	REQUIRE(hits.size() == 1);
	CHECK(hits[0].solid == back);
	// From inside: the face it leaves through.
	hits = brushesAlongRay(doc, Ray{{64, 64, 64}, {0, 1, 0}});
	REQUIRE_FALSE(hits.empty());
	CHECK(hits[0].solid == front);
	CHECK(hits[0].inside);
	CHECK(hits[0].distance == doctest::Approx(64));
}

TEST_CASE("editor report: an edit is described with its planes before and after") {
	MapDocument doc;
	const auto ref = addBlock(doc, {0, 0, 0}, {128, 128, 128});
	Transaction t(doc, "Scale");
	t.touchSolid(ref);
	Solid* solid = doc.findSolid(ref);
	for (auto& side : solid->sides) {
		for (auto& p : side.planePoints) {
			if (p.x > 64) p.x = 640;
		}
	}
	const Change change = t.finish();
	const std::string text = describeChange(change);
	CHECK(text.find("edit \"Scale\": 1 brush(es)") != std::string::npos);
	CHECK(text.find("before:") != std::string::npos);
	CHECK(text.find("after:") != std::string::npos);
	CHECK(text.find("box (0 0 0) - (128 128 128)") != std::string::npos);
	CHECK(text.find("box (0 0 0) - (640 128 128)") != std::string::npos);
	CHECK(text.find("INVALID") == std::string::npos);
	CHECK(text.find("POINTS INWARD") == std::string::npos);
}

TEST_CASE("editor report: the road planner says what it saw and decided") {
	MapDocument doc;
	const auto a = addBlock(doc, {0, 0, 0}, {512, 256, 16});
	const auto b = addBlock(doc, {512, 0, 0}, {1024, 256, 16});
	const auto c = addBlock(doc, {1024, 0, 0}, {1536, 256, 16});
	Selection selection;
	selection.solids = {a, b, c};
	RoadOptions options;
	options.material = "ROAD/DIRT01";
	const auto plan = planRoad(doc, roadFaces(doc, selection), options, [](const std::string&) { return std::pair<int, int>{512, 512}; });
	REQUIRE(plan.faces.size() == 3);
	REQUIRE_FALSE(plan.notes.empty());
	const std::string text = describeRoadPlan(plan, options);
	CHECK(text.find("road plan: material \"ROAD/DIRT01\"") != std::string::npos);
	CHECK(text.find("note: pieces 3") != std::string::npos);
	CHECK(text.find("note: road 0:") != std::string::npos);
	CHECK(text.find("width 256") != std::string::npos);
	CHECK(text.find("face 0/" + std::to_string(b.solidId)) != std::string::npos);
}

TEST_CASE("editor report: a whole report is replayed - its clicks against its map, its road planned again") {
	MapDocument doc;
	const auto front = addBlock(doc, {0, 0, 0}, {128, 128, 128});
	const auto back = addBlock(doc, {0, 256, 0}, {128, 384, 128});
	const auto road1 = addBlock(doc, {0, -1024, 0}, {512, -768, 16});
	const auto road2 = addBlock(doc, {512, -1024, 0}, {1024, -768, 16});
	const int hiddenSide = sideFacing(doc, front, {0, -1, 0});

	PickTrace click;
	click.view = "3D";
	click.tool = "Select";
	click.ray = Ray{{64, -500, 64}, {0, 1, 0}};
	click.result = "brush 0/" + std::to_string(front.solidId) + " side " + std::to_string(hiddenSide) + " at 500";
	std::vector<std::pair<std::string, std::string>> settings{{"hidden.faces", formatFaces({FaceRef{front, hiddenSide}})}, {"grid", "16"}};
	RoadOptions options;
	options.material = "ROAD/DIRT01";
	addRoadSettings(settings, options, {512, 512});
	settings.emplace_back("road.solids", formatSolids({road1, road2}));

	TempDir dir;
	const auto zip = dir.path / "report.zip";
	REQUIRE(writeZip(zip, {{"scene.vmf", {}, bytes(doc.serialize())},
	                       {"replay.txt", {}, bytes(formatSettings(settings))},
	                       {"picks.log", {}, bytes(formatPickTrace(click, 1))}}));
	std::string error;
	const auto replay = replayEditorReport(zip, &error);
	REQUIRE_MESSAGE(replay, error);
	INFO(*replay);
	CHECK(replay->find("scene: 4 brush(es)") != std::string::npos);
	CHECK(replay->find("grid: 16") != std::string::npos);
	CHECK(replay->find("pick #1 (3D): recorded brush 0/" + std::to_string(front.solidId)) != std::string::npos);
	// The recorded click took the block through its hidden face; by geometry it is the one behind.
	CHECK(replay->find("  now: brush 0/" + std::to_string(back.solidId) + " side") != std::string::npos);
	CHECK(replay->find("road replayed with the recorded settings and selection:") != std::string::npos);
	CHECK(replay->find("2 face(s)") != std::string::npos);

	CHECK_FALSE(replayEditorReport(dir.path / "missing.zip", &error));
	CHECK(error.find("no scene.vmf") != std::string::npos);
}

TEST_CASE("editor report: the road builder's settings go there and back; a street is planned again from its selection") {
	RoadBuildOptions o;
	o.material = "ROAD/ASPHALT01";
	o.parking = 80;
	o.wholeWidth = false;
	o.kerbs = true;
	o.kerbWidth = 24;
	o.displacement = true;
	o.power = 2;
	o.fillMaterial = "CONCRETE/PLAIN";
	std::vector<std::pair<std::string, std::string>> settings;
	addRoadBuildSettings(settings, o, false, {{0, 0, 8}, {512, 0, 8}}, {256, 512});
	std::map<std::string, std::string> map(settings.begin(), settings.end());
	bool fromSelection = true;
	std::vector<Vec3> points;
	std::pair<int, int> size;
	const auto back = roadBuildSettings(map, &fromSelection, &points, &size);
	CHECK_FALSE(fromSelection);
	REQUIRE(points.size() == 2);
	CHECK(points[1].x == doctest::Approx(512));
	CHECK(size == std::pair<int, int>{256, 512});
	CHECK(back.material == o.material);
	CHECK(back.parking == doctest::Approx(80));
	CHECK(back.kerbs);
	CHECK(back.kerbWidth == doctest::Approx(24));
	CHECK(back.displacement);
	CHECK(back.power == 2);
	CHECK(back.fillMaterial == o.fillMaterial);

	// A street of two blocks (an L), recorded as built from its selection: replayed.
	MapDocument doc;
	const auto a = addBlock(doc, {0, -240, -16}, {1536, 240, 8});
	const auto b = addBlock(doc, {1056, 240, -16}, {1536, 1536, 8});
	std::vector<std::pair<std::string, std::string>> recorded;
	addRoadBuildSettings(recorded, RoadBuildOptions{}, true, {}, {512, 512});
	recorded.emplace_back("build.solids", formatSolids({a, b}));
	TempDir dir;
	const auto zip = dir.path / "report.zip";
	REQUIRE(writeZip(zip, {{"scene.vmf", {}, bytes(doc.serialize())}, {"replay.txt", {}, bytes(formatSettings(recorded))}}));
	std::string error;
	const auto replay = replayEditorReport(zip, &error);
	REQUIRE_MESSAGE(replay, error);
	INFO(*replay);
	CHECK(replay->find("road builder replayed with the recorded settings and selection:") != std::string::npos);
	CHECK(replay->find("street: 2 floor brush(es)") != std::string::npos);
	CHECK(replay->find("width 480: road 288, parking 96") != std::string::npos);
	CHECK(replay->find("main road:") != std::string::npos);
	CHECK(replay->find("1 curve(s)") != std::string::npos);
}
