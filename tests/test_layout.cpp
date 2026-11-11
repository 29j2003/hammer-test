// test_layout.cpp - Where things are (player start in a brush, entities in the void, lightmap
// scales, NPCs without nodes), engine limits and func_detail candidates, common compile errors
// explained, ropes and ladders in one step.

#include <doctest/doctest.h>

#include <sme/brush.h>
#include <sme/checks.h>
#include <sme/compile_log.h>
#include <sme/csg.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/layout_checks.h>
#include <sme/map_helpers.h>
#include <sme/optimization_checks.h>
#include <sme/shapes.h>
#include <sme/text.h>

#include <algorithm>

using namespace sme;

namespace {

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

int addEntity(MapDocument& doc, std::vector<std::pair<std::string, std::string>> keys) {
	Entity e;
	e.id = doc.newEntityId();
	e.keyValues = std::move(keys);
	doc.entities.push_back(e);
	return e.id;
}

std::vector<Problem> problemsOf(const MapDocument& doc, Problem::Kind kind) {
	std::vector<Problem> out;
	for (const auto& p : checkMap(doc, {})) {
		if (p.kind == kind) out.push_back(p);
	}
	return out;
}

/// A floor (top at z 0) and a ceiling far above, like a room without walls.
MapDocument floorMap() {
	MapDocument doc;
	doc.world.solids.push_back(makeBlock(doc, box({-512, -512, -16}, {512, 512, 0}), "CONCRETE/CONCRETEFLOOR001A"));
	doc.world.solids.push_back(makeBlock(doc, box({-512, -512, 256}, {512, 512, 272}), "CONCRETE/CONCRETEFLOOR001A"));
	return doc;
}

} // namespace

TEST_CASE("layout: the player start in a brush is found and moved onto the floor") {
	MapDocument doc = floorMap();
	const int ok = addEntity(doc, {{"classname", "info_player_start"}, {"origin", "0 0 0"}});
	CHECK(problemsOf(doc, Problem::Kind::PlayerStartInSolid).empty());   // standing on the floor is fine

	const int stuck = addEntity(doc, {{"classname", "info_player_start"}, {"origin", "128 0 -8"}});
	auto stuckOnes = problemsOf(doc, Problem::Kind::PlayerStartInSolid);
	REQUIRE(stuckOnes.size() == 1);
	CHECK(stuckOnes[0].entityId == stuck);
	CHECK(stuckOnes[0].severity == Problem::Severity::Error);
	auto change = applyFix(doc, stuckOnes[0], nullptr);
	CHECK_FALSE(change.empty());
	CHECK(doc.findEntity(stuck)->origin().z == doctest::Approx(1));
	CHECK(problemsOf(doc, Problem::Kind::PlayerStartInSolid).empty());
	// The head in the ceiling counts too; the box above the start must be free.
	doc.findEntity(ok)->set("origin", "0 0 200");
	CHECK(problemsOf(doc, Problem::Kind::PlayerStartInSolid).size() == 1);
	// Hint and trigger brushes do not block.
	MapDocument hint;
	hint.world.solids.push_back(makeBlock(hint, box({-64, -64, -16}, {64, 64, 0}), "CONCRETE/CONCRETEFLOOR001A"));
	hint.world.solids.push_back(makeBlock(hint, box({-64, -64, 0}, {64, 64, 128}), "TOOLS/TOOLSHINT"));
	addEntity(hint, {{"classname", "info_player_start"}, {"origin", "0 0 0"}});
	CHECK(problemsOf(hint, Problem::Kind::PlayerStartInSolid).empty());
	CHECK(boxInSolid(hint, box({-8, -8, -8}, {8, 8, 8})));   // the floor
}

TEST_CASE("layout: entities in the void, lightmap scales, NPCs without nodes") {
	MapDocument doc = floorMap();
	const int lost = addEntity(doc, {{"classname", "info_target"}, {"origin", "5000 0 0"}});
	addEntity(doc, {{"classname", "info_target"}, {"origin", "0 0 64"}});
	auto outside = problemsOf(doc, Problem::Kind::EntityOutsideWorld);
	REQUIRE(outside.size() == 1);
	CHECK(outside[0].entityId == lost);

	// Lightmap scales: below 4 is costly, the fix sets 16; tool faces do not count.
	doc.world.solids[0].sides[0].lightmapScale = 2;
	doc.world.solids[0].sides[1].lightmapScale = 512;
	doc.world.solids[1].sides[0].material = "TOOLS/TOOLSNODRAW";
	doc.world.solids[1].sides[0].lightmapScale = 1;
	auto scales = problemsOf(doc, Problem::Kind::LightmapScale);
	REQUIRE(scales.size() == 2);
	const auto fine = std::find_if(scales.begin(), scales.end(), [](const Problem& p) { return p.to == "16"; });
	REQUIRE(fine != scales.end());
	CHECK(fine->objects.faces.size() == 1);
	(void)applyFix(doc, *fine, nullptr);
	CHECK(doc.world.solids[0].sides[0].lightmapScale == 16);

	// NPCs without nodes: the fix puts nodes on free floor around each NPC.
	CHECK(problemsOf(doc, Problem::Kind::NpcsWithoutNodes).empty());
	addEntity(doc, {{"classname", "npc_manhack"}, {"origin", "0 0 100"}});   // flies: needs air nodes, not these
	CHECK(problemsOf(doc, Problem::Kind::NpcsWithoutNodes).empty());
	const int citizen = addEntity(doc, {{"classname", "npc_citizen"}, {"origin", "0 0 0"}});
	addEntity(doc, {{"classname", "npc_combine_s"}, {"origin", "32 0 0"}});
	auto nodes = problemsOf(doc, Problem::Kind::NpcsWithoutNodes);
	REQUIRE(nodes.size() == 1);
	CHECK(nodes[0].objects.entities.size() == 2);
	CHECK(nodes[0].objects.entities[0] == citizen);
	const auto before = doc.entities.size();
	CHECK_FALSE(applyFix(doc, nodes[0], nullptr).empty());
	const auto placed = doc.entities.size() - before;
	CHECK(placed >= 9);   // a 3 x 3 grid around the citizen; the second NPC's grid overlaps it
	for (std::size_t i = before; i < doc.entities.size(); ++i) {
		CHECK(doc.entities[i].classname() == "info_node");
		CHECK(doc.entities[i].origin().z == doctest::Approx(0));
	}
	CHECK(problemsOf(doc, Problem::Kind::NpcsWithoutNodes).empty());
	// No free floor (inside a pillar): no spots.
	doc.world.solids.push_back(makeBlock(doc, box({1000, 1000, 0}, {1100, 1100, 200}), "CONCRETE/CONCRETEFLOOR001A"));
	CHECK(nodeSpotsAround(doc, {1050, 1050, 50}).empty());
}

TEST_CASE("compile log: common errors are explained") {
	const auto axis = classifyCompileLine("Texture axis perpendicular to face");
	REQUIRE(axis);
	CHECK(axis->severity == CompileMessage::Severity::Error);
	CHECK(axis->explanation.find("Justify") != std::string::npos);
	const auto portals = classifyCompileLine("LoadPortals: couldn't read c:/maps/test.prt");
	REQUIRE(portals);
	CHECK(portals->explanation.find("leak") != std::string::npos);
	const auto rad = classifyCompileLine("ERROR: Duplication of '//' in file 'fidelity_lights.rad'!");
	REQUIRE(rad);
	CHECK(rad->severity == CompileMessage::Severity::Warning);
	CHECK(classifyCompileLine("Brush 12: areaportal brush doesn't touch two areas")->explanation.find("func_areaportal") != std::string::npos);
	CHECK(classifyCompileLine("Too many light styles on a face")->severity == CompileMessage::Severity::Error);
	CHECK(classifyCompileLine("Displacement found on a(n) 5-sided brush face.")->explanation.find("4 corners") != std::string::npos);
	CHECK(classifyCompileLine("Bad surface extents point (0 0 0)")->severity == CompileMessage::Severity::Error);
}

TEST_CASE("ropes: a chain through the points, its sagging curve") {
	MapDocument doc = floorMap();
	const auto rope = edit::makeRope(doc, {{0, 0, 200}, {256, 0, 200}, {256, 256, 150}}, nullptr, 50);
	REQUIRE(rope.entityIds.size() == 3);
	const auto* first = doc.findEntity(rope.entityIds[0]);
	const auto* second = doc.findEntity(rope.entityIds[1]);
	const auto* last = doc.findEntity(rope.entityIds[2]);
	CHECK(first->classname() == "move_rope");
	CHECK(second->classname() == "keyframe_rope");
	CHECK(first->get("NextKey") == second->get("targetname"));
	CHECK(second->get("NextKey") == last->get("targetname"));
	CHECK(last->get("NextKey").empty());
	CHECK(first->get("Slack") == "50");
	CHECK(first->get("RopeMaterial") == "cable/cable.vmt");
	// A second rope gets other names.
	const std::string firstName = first->get("targetname");
	const auto other = edit::makeRope(doc, {{0, 64, 200}, {64, 64, 200}}, nullptr);   // (moves the entities: pointers above are stale now)
	CHECK(doc.findEntity(other.entityIds[0])->get("targetname") != firstName);
	CHECK(edit::makeRope(doc, {{0, 0, 0}}, nullptr).entityIds.empty());

	const auto curves = ropeCurves(doc);
	REQUIRE(curves.size() == 2);
	const auto& curve = curves[0].startEntityId == rope.entityIds[0] ? curves[0] : curves[1];
	CHECK(curve.points.size() == 1 + 2 * 12);
	// It sags: the middle of the first span is lower than its ends.
	CHECK(curve.points[6].z < 200 - 10);
	CHECK(curve.points[12].z == doctest::Approx(200));
	CHECK(ropeSag({0, 0, 0}, {256, 0, 0}, 0) == doctest::Approx(0));
	// A loop does not hang.
	doc.findEntity(rope.entityIds[2])->set("NextKey", firstName);
	CHECK(ropeCurves(doc).size() == 2);

	// Points of a selection: entities' origins, brushes' top centres.
	Selection selection;
	selection.entities = {rope.entityIds[0]};
	selection.solids = {{0, doc.world.solids[0].id}};
	const auto points = edit::ropePointsOf(doc, selection, nullptr);
	REQUIRE(points.size() == 2);
	CHECK(points[1].z == doctest::Approx(0));
}

TEST_CASE("ladders: climbing line in front, dismounts at the top and the bottom") {
	MapDocument doc = floorMap();
	// A wall at x < 0 up to 128, the ladder against it (8 deep, 32 wide).
	doc.world.solids.push_back(makeBlock(doc, box({-64, -128, 0}, {0, 128, 128}), "CONCRETE/CONCRETEWALL001A"));
	const auto ladder = edit::makeLadder(doc, box({0, -16, 0}, {8, 16, 128}), nullptr);
	REQUIRE(ladder.ladderId != 0);
	CHECK(ladder.front.x == doctest::Approx(1));   // the free side
	const auto* useable = doc.findEntity(ladder.ladderId);
	CHECK(useable->classname() == "func_useableladder");
	CHECK(useable->get("point0") == "26 0 0");
	CHECK(useable->get("point1") == "26 0 128");
	REQUIRE(ladder.dismountIds.size() == 2);
	const auto* top = doc.findEntity(ladder.dismountIds[0]);
	CHECK(top->classname() == "info_ladder_dismount");
	CHECK(top->origin().x < 0);   // on the ledge behind
	CHECK(top->origin().z == doctest::Approx(128));
	CHECK(doc.findEntity(ladder.dismountIds[1])->origin().x > 26);   // a step back at the bottom
	// The other way round: wall at x > 8.
	MapDocument other = floorMap();
	other.world.solids.push_back(makeBlock(other, box({8, -128, 0}, {64, 128, 128}), "CONCRETE/CONCRETEWALL001A"));
	CHECK(edit::makeLadder(other, box({0, -16, 0}, {8, 16, 128}), nullptr).front.x == doctest::Approx(-1));
	CHECK(edit::makeLadder(other, box({0, 0, 0}, {8, 8, 8}), nullptr).ladderId == 0);   // too low for a ladder
}

TEST_CASE("optimization: round shapes and small pieces are func_detail candidates, tool brushes and walls are not") {
	MapDocument doc = floorMap();   // two big slabs: world, never candidates
	ShapeOptions cylinder;
	cylinder.kind = ShapeOptions::Kind::Cylinder;
	cylinder.sides = 12;
	for (auto& s : makeShape(doc, box({0, 0, 0}, {64, 64, 128}), cylinder, "CONCRETE/CONCRETEWALL001A")) doc.world.solids.push_back(std::move(s));
	const int pillar = doc.world.solids.back().id;
	doc.world.solids.push_back(makeBlock(doc, box({200, 0, 0}, {212, 8, 16}), "WOOD/WOODTRIM001A"));   // a small piece
	const int trim = doc.world.solids.back().id;
	doc.world.solids.push_back(makeBlock(doc, box({300, 0, 0}, {308, 8, 8}), "TOOLS/TOOLSHINT"));      // small, but a tool brush
	doc.world.solids.push_back(makeBlock(doc, box({-512, -512, 0}, {-496, 512, 256}), "BRICK/BRICKWALL001A"));   // a wall: 16 thick, long

	const auto refs = detailCandidates(doc);
	REQUIRE(refs.size() == 2);
	CHECK(refs[0].solidId == pillar);
	CHECK(refs[1].solidId == trim);

	auto found = problemsOf(doc, Problem::Kind::DetailCandidates);
	REQUIRE(found.size() == 1);
	CHECK(found[0].severity == Problem::Severity::Info);
	CHECK(found[0].objects.solids.size() == 2);
	CHECK(found[0].fixLabel == "Make func_detail");

	// The fix ties both to one func_detail; undo brings them back to the world.
	const std::size_t worldBefore = doc.world.solids.size();
	auto change = applyFix(doc, found[0], nullptr);
	CHECK(doc.world.solids.size() == worldBefore - 2);
	const auto detail = std::find_if(doc.entities.begin(), doc.entities.end(), [](const Entity& e) { return e.classname() == "func_detail"; });
	REQUIRE(detail != doc.entities.end());
	CHECK(detail->solids.size() == 2);
	CHECK(problemsOf(doc, Problem::Kind::DetailCandidates).empty());
	History history;
	history.push(std::move(change));
	REQUIRE(history.undo(doc));
	CHECK(doc.world.solids.size() == worldBefore);
}

TEST_CASE("optimization: a map close to an engine limit is reported before VBSP stops") {
	MapDocument doc = floorMap();
	BudgetLimits tight;
	tight.brushes = 3;   // two brushes of three: 67 %, no report
	CheckContext context;
	context.limits = &tight;
	const auto near = [&] {
		std::vector<Problem> out;
		for (auto& p : checkMap(doc, context)) {
			if (p.kind == Problem::Kind::NearEngineLimit) out.push_back(p);
		}
		return out;
	};
	CHECK(near().empty());
	doc.world.solids.push_back(makeBlock(doc, box({0, 0, 0}, {64, 64, 64}), "BRICK/BRICKWALL001A"));   // 3 of 3: 100 %
	auto found = near();
	REQUIRE(found.size() == 1);
	CHECK(found[0].severity == Problem::Severity::Warning);
	CHECK(found[0].message.find("Brushes: 3 of 3") != std::string::npos);
	doc.world.solids.push_back(makeBlock(doc, box({100, 0, 0}, {164, 64, 64}), "BRICK/BRICKWALL001A"));   // over
	found = near();
	REQUIRE(found.size() == 1);
	CHECK(found[0].severity == Problem::Severity::Error);
	CHECK(found[0].howToFix.find("func_detail") != std::string::npos);
}

TEST_CASE("compile log: each engine limit, microbrushes and t-junctions are explained") {
	const auto explain = [](std::string_view line) { return classifyCompileLine(line); };
	for (const auto* line : {"ERROR: MAX_MAP_BRUSHSIDES", "ERROR: MAX_MAP_BRUSHES", "ERROR: MAX_MAP_PLANES", "ERROR: MAX_MAP_ENTITIES",
	                         "ERROR: MAX_MAP_OVERLAYS", "ERROR: MAX_MAP_DISPINFO", "ERROR: MAX_MAP_TEXINFO", "Error: MAX_MAP_PORTALS"}) {
		const auto message = explain(line);
		REQUIRE(message);
		CHECK(message->severity == CompileMessage::Severity::Error);
		CHECK(message->explanation.find("engine limit (brushes") == std::string::npos);   // the specific text, not the generic one
	}
	CHECK(explain("ERROR: MAX_MAP_BRUSHSIDES")->explanation.find("65536") != std::string::npos);
	CHECK(explain("ERROR: MAX_MAP_ENTITIES")->explanation.find("prop_static") != std::string::npos);
	const auto micro = explain("Brush 1234: WARNING, microbrush");
	REQUIRE(micro);
	CHECK(micro->severity == CompileMessage::Severity::Warning);
	CHECK(explain("Too many t-junctions to fix up! (65536 max)")->severity == CompileMessage::Severity::Warning);
	CHECK(explain("ERROR: MAX_MAP_LEAFWATERDATA")->explanation.find("engine limit") != std::string::npos);   // others: generic
}

TEST_CASE("layout: a prop with its origin inside a brush is reported (it would be lit black)") {
	MapDocument doc = floorMap();   // floor top at z 0
	addEntity(doc, {{"classname", "prop_static"}, {"model", "models/props_c17/oildrum001.mdl"}, {"origin", "0 0 0"}});      // on the floor
	const int sunk = addEntity(doc, {{"classname", "prop_physics"}, {"model", "models/props_c17/oildrum001.mdl"}, {"origin", "64 0 -8"}});
	addEntity(doc, {{"classname", "prop_static"}, {"model", "models/props_c17/oildrum001.mdl"}, {"origin", "128 0 -8"},
	                {"lightingorigin", "light_here"}});   // lit elsewhere: fine
	addEntity(doc, {{"classname", "info_target"}, {"origin", "192 0 -8"}});   // not a model
	const auto found = problemsOf(doc, Problem::Kind::PropOriginInSolid);
	REQUIRE(found.size() == 1);
	CHECK(found[0].entityId == sunk);
	CHECK(found[0].severity == Problem::Severity::Warning);
	CHECK(found[0].howToFix.find("info_lighting") != std::string::npos);
}

TEST_CASE("optimization: faces flat against func_detail brushes nobody sees become nodraw") {
	MapDocument doc = floorMap();
	const auto detailEntity = [&](const Bounds& b, const std::string& material) {
		const int id = addEntity(doc, {{"classname", "func_detail"}});
		auto* e = doc.findEntity(id);
		e->solids.push_back(makeBlock(doc, b, material));
		return SolidRef{id, e->solids.back().id};
	};
	const auto sideFacing = [&](const SolidRef& ref, const Vec3& normal) {
		for (const auto& side : doc.findSolid(ref)->sides) {
			if (length(side.plane().normal - normal) < 1e-6) return side.id;
		}
		FAIL("no side");
		return 0;
	};
	// A detail pillar on the world floor: its bottom is hidden; the floor's top is only partly covered.
	const auto pillar = detailEntity(box({0, 0, 0}, {64, 64, 128}), "CONCRETE/CONCRETEWALL001A");
	// A detail platform with a world box on top: the box's bottom is hidden, the platform's top only partly.
	const auto platform = detailEntity(box({200, 0, 0}, {328, 128, 32}), "CONCRETE/CONCRETEWALL001A");
	doc.world.solids.push_back(makeBlock(doc, box({232, 32, 32}, {264, 64, 64}), "WOOD/CRATE001A"));
	const SolidRef crate{0, doc.world.solids.back().id};
	// Not listed: a world box on the world floor (VBSP drops that face), a glass detail brush, a box on
	// glass, a door, a box on a clip brush.
	doc.world.solids.push_back(makeBlock(doc, box({-200, -200, 0}, {-136, -136, 64}), "WOOD/CRATE001A"));
	const auto glass = detailEntity(box({-400, 0, 0}, {-300, 16, 128}), "GLASS/GLASSWINDOW001A");
	(void)glass;
	doc.world.solids.push_back(makeBlock(doc, box({-380, 0, 128}, {-340, 16, 160}), "WOOD/CRATE001A"));
	const int door = addEntity(doc, {{"classname", "func_door"}});
	doc.findEntity(door)->solids.push_back(makeBlock(doc, box({400, 0, 0}, {416, 64, 128}), "METAL/METALDOOR001A"));
	const auto clip = detailEntity(box({0, 300, 0}, {64, 364, 64}), "TOOLS/TOOLSPLAYERCLIP");
	(void)clip;
	doc.world.solids.push_back(makeBlock(doc, box({16, 316, 64}, {48, 348, 96}), "WOOD/CRATE001A"));

	const auto faces = hiddenFaces(doc);
	REQUIRE(faces.size() == 3);   // world faces first, then func_detail ones (the platform's bottom lies on the floor)
	CHECK(faces[0] == FaceRef{crate, sideFacing(crate, {0, 0, -1})});
	CHECK(faces[1] == FaceRef{pillar, sideFacing(pillar, {0, 0, -1})});
	CHECK(faces[2] == FaceRef{platform, sideFacing(platform, {0, 0, -1})});

	auto found = problemsOf(doc, Problem::Kind::HiddenFaces);
	REQUIRE(found.size() == 1);
	CHECK(found[0].severity == Problem::Severity::Info);
	CHECK(found[0].fixLabel == "Make nodraw");
	auto change = applyFix(doc, found[0], nullptr);
	const auto materialOf = [&](const FaceRef& f) {
		for (const auto& side : doc.findSolid(f.solid)->sides) {
			if (side.id == f.sideId) return side.material;
		}
		return std::string();
	};
	CHECK(materialOf(faces[0]) == "TOOLS/TOOLSNODRAW");
	CHECK(materialOf(faces[1]) == "TOOLS/TOOLSNODRAW");
	CHECK(problemsOf(doc, Problem::Kind::HiddenFaces).empty());
	History history;
	history.push(std::move(change));
	REQUIRE(history.undo(doc));
	CHECK(materialOf(faces[1]) == "CONCRETE/CONCRETEWALL001A");

	// The game's material decides when it is known: an opaque "glass" stays a cover.
	CHECK(hiddenFaces(doc, [](const std::string&) { return false; }).size() == 5);
	CHECK(seeThroughByName("glass/glasswindow001a"));
	CHECK_FALSE(seeThroughByName("concrete/concretefloor001a"));
}

TEST_CASE("textures edge-on to their face: found, aligned to the world by the fix and every edit, cut faces never get one (Julius' VRAD log of 11.11.2026)") {
	// VRAD printed "face vectors parallel to face normal" hundreds of times: 124 faces of his map
	// had the top's axes (X, -Y) on vertical cut sides, and two brick faces 2 degrees off X had X as U.
	MapDocument doc;
	Solid block = makeBlock(doc, box({0, 0, 0}, {256, 256, 64}), "DEV/DEV_BLENDMEASURE2");
	Side* front = nullptr;
	for (auto& side : block.sides) {
		if (side.plane().normal.y < -0.9) front = &side;
	}
	REQUIRE(front);
	CHECK_FALSE(textureEdgeOn(*front));
	front->u = TextureAxis{{1, 0, 0}, 16, 0.5};
	front->v = TextureAxis{{0, -1, 0}, 8, 0.25};
	CHECK(textureEdgeOn(*front));
	// Tool textures have no lightmap and never count.
	Side tool = *front;
	tool.material = "TOOLS/TOOLSNODRAW";
	CHECK_FALSE(textureEdgeOn(tool));
	// The fix keeps both scales.
	Side fixed = *front;
	CHECK(alignEdgeOnTexture(fixed));
	CHECK_FALSE(textureEdgeOn(fixed));
	CHECK(fixed.u.scale == doctest::Approx(0.5));
	CHECK(fixed.v.scale == doctest::Approx(0.25));
	CHECK_FALSE(alignEdgeOnTexture(fixed));

	// A cut with a slanted vertical plane: the new face takes the top's texture but not its axes.
	{
		MapDocument cutDoc;
		const Solid floor = makeBlock(cutDoc, box({0, 0, 0}, {256, 256, 64}), "DEV/DEV_BLENDMEASURE2");
		const Side* top = nullptr;
		for (const auto& side : floor.sides) {
			if (side.plane().normal.z > 0.9) top = &side;
		}
		REQUIRE(top);
		const Vec3 normal = normalize(Vec3{-0.195, -0.981, 0});
		const auto [back, frontPart] = splitSolid(cutDoc, floor, Plane{normal, dot(normal, Vec3{128, 128, 0})}, *top);
		REQUIRE(back);
		REQUIRE(frontPart);
		for (const auto* part : {&*back, &*frontPart}) {
			for (const auto& side : part->sides) {
				CHECK_FALSE(textureEdgeOn(side));
				CHECK(iequals(side.material, "DEV/DEV_BLENDMEASURE2"));
			}
		}
	}

	// The map check, its fix (one undo step) and the guard of every edit.
	History history;
	Transaction add(doc, "Add");
	doc.world.solids.push_back(block);
	add.touchSolid({0, block.id});
	history.push(add.finish());
	const SolidRef ref{0, block.id};
	REQUIRE(edit::edgeOnTextureFaces(doc).size() == 1);
	CheckContext context;
	const auto problems = checkMap(doc, context);
	const auto it = std::find_if(problems.begin(), problems.end(), [](const Problem& p) { return p.kind == Problem::Kind::EdgeOnTexture; });
	REQUIRE(it != problems.end());
	CHECK(it->severity == Problem::Severity::Warning);
	CHECK(it->message.find("face vectors parallel to face normal") != std::string::npos);
	const std::string before = doc.serialize();
	int count = -1;
	history.push(edit::alignEdgeOnTextures(doc, {}, &count));
	CHECK(count == 1);
	CHECK(edit::edgeOnTextureFaces(doc).empty());
	history.undo(doc);
	CHECK(doc.serialize() == before);
	// An edit that leaves one: the guard aligns it and the change's after state follows.
	Transaction t(doc, "Move");
	t.touchSolid(ref);
	for (auto& side : doc.findSolid(ref)->sides) {
		for (auto& p : side.planePoints) p.z += 16;
	}
	Change change = t.finish();
	CHECK(edit::guardEdgeOnTextures(doc, change) == 1);
	CHECK(edit::edgeOnTextureFaces(doc).empty());
	REQUIRE(change.solids.size() == 1);
	REQUIRE(change.solids.front().after);
	CHECK(std::none_of(change.solids.front().after->sides.begin(), change.solids.front().after->sides.end(), [](const Side& s) { return textureEdgeOn(s); }));
}

TEST_CASE("compile log: VRAD's repeated lines read without the progress dots, edge-on textures explained, fidelity's lights file harmless") {
	CHECK(problemText("BuildFacelights:     0...1...2...3...4. warning - face vectors parallel to face normal. bad lighting will be produced") ==
	      "warning - face vectors parallel to face normal. bad lighting will be produced");
	CHECK(problemText(". warning - face vectors parallel to face normal.") == "warning - face vectors parallel to face normal.");
	CHECK(problemText("5 warning - face vectors parallel") == "warning - face vectors parallel");
	CHECK(problemText(".7... warning - x") == "warning - x");
	CHECK(problemText("warning - x") == "warning - x");
	CHECK(problemText("ERROR: MAX_MAP_BRUSHES") == "ERROR: MAX_MAP_BRUSHES");
	CHECK(problemText("Warning: Overriding 'lights/x' from 'lights.rad'") == "Warning: Overriding 'lights/x' from 'lights.rad'");
	const auto parallel = classifyCompileLine("FinalLightFace:      0...1...2...3...4.. warning - face vectors parallel to face normal. bad lighting will be produced");
	REQUIRE(parallel);
	CHECK(parallel->severity == CompileMessage::Severity::Warning);
	CHECK(parallel->explanation.find("Align to world") != std::string::npos);
	const auto overriding = classifyCompileLine("Warning: Overriding 'lights/physgunlight' from 'lights.rad' with 'fidelity_lights.rad'!");
	REQUIRE(overriding);
	CHECK(overriding->severity == CompileMessage::Severity::Info);
}
