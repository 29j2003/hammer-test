// test_choreography.cpp - NPC scenes: the plan in the map, its helpers, the timeline, the .vcd
// it writes (read back), undo, the VMF round trip, checks and the voice-line caption.

#include "fixture.h"

#include <sme/choreography.h>
#include <sme/game.h>
#include <sme/shipping.h>
#include <sme/history.h>
#include <sme/localization.h>
#include <sme/text.h>

#include <doctest/doctest.h>

#include <algorithm>

using namespace sme;

namespace {

int addEntity(MapDocument& doc, std::vector<std::pair<std::string, std::string>> keys) {
	Entity e;
	e.id = doc.newEntityId();
	e.keyValues = std::move(keys);
	doc.entities.push_back(e);
	return e.id;
}

int sceneEntity(const MapDocument& doc) {
	const auto ids = choreoScenes(doc);
	return ids.empty() ? 0 : ids.front();
}

std::vector<const Entity*> helpers(const MapDocument& doc, std::string_view scene, std::string_view classname) {
	std::vector<const Entity*> out;
	for (const auto& e : doc.entities) {
		if (iequals(e.get(CHOREO_SCENE_KEY), scene) && iequals(e.classname(), classname)) out.push_back(&e);
	}
	return out;
}

ChoreoStep move(std::string actor, Vec3 to, bool run = false) {
	ChoreoStep s;
	s.kind = ChoreoStep::Kind::Move;
	s.actor = std::move(actor);
	s.point = to;
	s.run = run;
	return s;
}

ChoreoStep anim(std::string actor, std::string sequence, double seconds = 0) {
	ChoreoStep s;
	s.kind = ChoreoStep::Kind::Animation;
	s.actor = std::move(actor);
	s.sequence = std::move(sequence);
	s.seconds = seconds;
	return s;
}

ChoreoStep say(std::string actor, std::string sound, double seconds, std::string caption) {
	ChoreoStep s;
	s.kind = ChoreoStep::Kind::Say;
	s.actor = std::move(actor);
	s.sound = std::move(sound);
	s.seconds = seconds;
	s.caption = std::move(caption);
	return s;
}

} // namespace

TEST_CASE("choreography: a step survives the trip through a keyvalue, odd characters included") {
	ChoreoStep s = say("alyx", "mod.scene.line1", 2.5, "Hello | \"Gordon\" - 100% sure");
	s.withPrevious = true;
	const std::string text = encodeChoreoStep(s);
	CHECK(text.find('"') == std::string::npos);   // a VMF value cannot hold a quote
	const auto back = decodeChoreoStep(text);
	REQUIRE(back);
	CHECK(back->kind == ChoreoStep::Kind::Say);
	CHECK(back->actor == "alyx");
	CHECK(back->withPrevious);
	CHECK(back->seconds == doctest::Approx(2.5));
	CHECK(back->caption == "Hello | 'Gordon' - 100% sure");
	const auto m = decodeChoreoStep(encodeChoreoStep(move("bob", {64, -32.5, 8}, true)));
	REQUIRE(m);
	CHECK(m->kind == ChoreoStep::Kind::Move);
	CHECK(m->run);
	CHECK(m->point.y == doctest::Approx(-32.5));
	CHECK_FALSE(decodeChoreoStep("dance|alyx"));
	CHECK_FALSE(decodeChoreoStep(""));
}

TEST_CASE("choreography: a new scene, its steps, points and trigger in the map; undo") {
	MapDocument doc;
	const int npc = addEntity(doc, {{"classname", "npc_citizen"}, {"origin", "0 0 0"}, {"angles", "0 0 0"}});
	History history;
	Change created = edit::createChoreoScene(doc, npc, "Test Map");
	REQUIRE_FALSE(created.empty());
	history.push(std::move(created));
	const int id = sceneEntity(doc);
	REQUIRE(id != 0);
	const Entity* entity = doc.findEntity(id);
	CHECK(entity->get("targetname") == "npc_citizen_" + std::to_string(npc) + "_scene");
	CHECK(entity->get("SceneFile") == "scenes/test_map/" + toLower(entity->get("targetname")) + ".vcd");
	CHECK(doc.findEntity(npc)->get("targetname") == "npc_citizen_" + std::to_string(npc));   // named so the scene can find it
	CHECK(helpers(doc, entity->get("targetname"), "logic_auto").size() == 1);

	ChoreoScene scene = readChoreoScene(*entity);
	const std::string actor = doc.findEntity(npc)->get("targetname");
	scene.name = "intro";
	scene.steps = {move(actor, {160, 0, 0}), anim(actor, "wave", 1.5), say(actor, "mymod.intro.hello", 2.0, "Hello there"), move(actor, {160, 160, 0}, true)};
	const std::string before = doc.serialize();
	Change set = edit::setChoreoScene(doc, id, scene);
	REQUIRE_FALSE(set.empty());
	history.push(std::move(set));
	entity = doc.findEntity(id);
	CHECK(entity->get("targetname") == "intro");
	CHECK(readChoreoScene(*entity) == scene);
	// One point per move, named as the .vcd names them; the map-start helper follows the new name.
	const auto points = helpers(doc, "intro", "info_target");
	REQUIRE(points.size() == 2);
	CHECK(points[0]->get("targetname") == "intro_p1");
	CHECK(points[0]->get("origin") == "160 0 0");
	CHECK(points[1]->get("targetname") == "intro_p4");
	const auto autos = helpers(doc, "intro", "logic_auto");
	REQUIRE(autos.size() == 1);
	REQUIRE(autos[0]->connections.size() == 1);
	CHECK(autos[0]->connections[0].target == "intro");
	CHECK(autos[0]->connections[0].input == "Start");
	// Nothing to do the second time.
	CHECK(edit::setChoreoScene(doc, id, scene).empty());
	// A point moved with the gizmo (its info_target) is where the NPC walks.
	{
		Entity* p1 = doc.findEntity(points[0]->id);
		p1->set("origin", "200 40 0");
		const auto resolved = readChoreoScene(doc, *doc.findEntity(id));
		CHECK(resolved.steps[0].point.x == doctest::Approx(200));
		CHECK(resolved.steps[0].point.y == doctest::Approx(40));
		CHECK(readChoreoScene(*doc.findEntity(id)).steps[0].point.x == doctest::Approx(160));   // the plan alone still says 160
		p1->set("origin", "160 0 0");
	}

	// Started by a trigger: the logic_auto goes, a trigger_once comes (in front of the NPC).
	scene.trigger = ChoreoTrigger::Trigger;
	history.push(edit::setChoreoScene(doc, id, scene));
	CHECK(helpers(doc, "intro", "logic_auto").empty());
	const auto triggers = helpers(doc, "intro", "trigger_once");
	REQUIRE(triggers.size() == 1);
	CHECK(triggers[0]->connections[0].output == "OnStartTouch");
	REQUIRE(triggers[0]->solids.size() == 1);
	// One move fewer: its point goes.
	scene.steps.pop_back();
	history.push(edit::setChoreoScene(doc, id, scene));
	CHECK(helpers(doc, "intro", "info_target").size() == 1);
	// Undo all the way back to the plain map.
	while (history.undo(doc)) {
	}
	CHECK(doc.serialize() != before);   // the scene itself was made before `before`
	CHECK(choreoScenes(doc).empty());
	CHECK(doc.findEntity(npc)->get("targetname").empty());
	// Redo brings it back exactly.
	while (history.redo(doc)) {
	}
	CHECK(readChoreoScene(*doc.findEntity(id)).steps.size() == 3);
}

TEST_CASE("choreography: after another scene, by input; renaming moves outputs along; deleting") {
	MapDocument doc;
	addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}});
	const int npcB = addEntity(doc, {{"classname", "npc_barney"}, {"targetname", "barney"}, {"origin", "64 0 0"}});
	(void)edit::createChoreoScene(doc, npcB, "m");
	const int first = sceneEntity(doc);
	(void)edit::createChoreoScene(doc, npcB, "m");
	const auto ids = choreoScenes(doc);
	REQUIRE(ids.size() == 2);
	const int second = ids[1];
	ChoreoScene a = readChoreoScene(*doc.findEntity(first));
	a.steps = {say("alyx", "s.one", 1, "One")};
	(void)edit::setChoreoScene(doc, first, a);
	ChoreoScene b = readChoreoScene(*doc.findEntity(second));
	b.steps = {say("barney", "s.two", 1, "Two")};
	b.trigger = ChoreoTrigger::AfterScene;
	b.afterScene = a.name;
	(void)edit::setChoreoScene(doc, second, b);
	const auto link = [&doc, first](std::string_view target) {
		const auto& c = doc.findEntity(first)->connections;
		return std::count_if(c.begin(), c.end(), [target](const Connection& x) { return x.output == "OnCompletion" && iequals(x.target, target) && x.input == "Start"; });
	};
	CHECK(link(b.name) == 1);
	CHECK(helpers(doc, b.name, "logic_auto").empty());
	// Renamed: the other scene's output follows.
	b.name = "second_part";
	(void)edit::setChoreoScene(doc, second, b);
	CHECK(link("second_part") == 1);
	// By input only: the link goes, nothing else is made.
	b.trigger = ChoreoTrigger::Input;
	(void)edit::setChoreoScene(doc, second, b);
	CHECK(link("second_part") == 0);
	CHECK(helpers(doc, "second_part", "logic_auto").empty());
	CHECK(helpers(doc, "second_part", "trigger_once").empty());
	CHECK(choreoScenesOfActor(doc, "BARNEY") == std::vector<int>{second});
	// Deleting takes its helpers along.
	const auto helpersBefore = helpers(doc, a.name, "logic_auto").size();
	CHECK(helpersBefore == 1);
	(void)edit::deleteChoreoScene(doc, first);
	CHECK(choreoScenes(doc) == std::vector<int>{second});
	CHECK(helpers(doc, a.name, "logic_auto").empty());
}

TEST_CASE("choreography: the timeline - one after another, together with the previous, where the NPC is") {
	MapDocument doc;
	addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}, {"angles", "0 90 0"}});
	addEntity(doc, {{"classname", "npc_barney"}, {"targetname", "barney"}, {"origin", "0 100 0"}});
	ChoreoScene scene;
	scene.name = "talk";
	scene.steps = {move("alyx", {160, 0, 0}), say("barney", "s.hi", 3, "Hi"), anim("alyx", "wave", 2)};
	scene.steps[2].withPrevious = true;   // Alyx waves while Barney talks
	ChoreoStep wait;
	wait.kind = ChoreoStep::Kind::Wait;
	wait.actor = "alyx";
	wait.seconds = 1;
	scene.steps.push_back(wait);
	const auto t = choreoTimeline(doc, scene);
	REQUIRE(t.size() == 4);
	CHECK(t[0].start == doctest::Approx(0));
	CHECK(t[0].end == doctest::Approx(160.0 / CHOREO_WALK_SPEED));
	CHECK(t[0].yaw == doctest::Approx(0));   // turned towards where she walked
	CHECK(t[1].start == doctest::Approx(t[0].end));
	CHECK(t[2].start == doctest::Approx(t[1].start));
	CHECK(t[3].start == doctest::Approx(t[1].end));   // after the longer of the two
	CHECK(choreoLength(t) == doctest::Approx(t[3].end));
	// Halfway along the walk, then waving at the end point.
	const auto mid = choreoActorPose(doc, scene, t, "alyx", t[0].end / 2);
	CHECK(mid.step == 0);
	CHECK(mid.position.x == doctest::Approx(80));
	const auto waving = choreoActorPose(doc, scene, t, "alyx", t[2].start + 0.5);
	CHECK(waving.step == 2);
	CHECK(waving.stepTime == doctest::Approx(0.5));
	CHECK(waving.position.x == doctest::Approx(160));
	const auto before = choreoActorPose(doc, scene, t, "alyx", -1);
	CHECK(before.step == -1);
	CHECK(before.yaw == doctest::Approx(90));
	// Running is faster; a move on the spot still takes a moment.
	CHECK(choreoStepLength(move("x", {360, 0, 0}, true), {0, 0, 0}) == doctest::Approx(2.0));
	CHECK(choreoStepLength(move("x", {0, 0, 0}), {0, 0, 0}) == doctest::Approx(0.5));
}

TEST_CASE("choreography: the .vcd - actors, channels, events, times, pauses for late NPCs") {
	MapDocument doc;
	addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}, {"model", "models/alyx.mdl"}});
	ChoreoScene scene;
	scene.name = "intro";
	scene.steps = {move("alyx", {160, 0, 0}), anim("alyx", "wave", 1.5), say("alyx", "mymod.intro.hello", 2, "Hello")};
	ChoreoStep face;
	face.kind = ChoreoStep::Kind::Face;
	face.actor = "alyx";
	face.point = {160, 200, 0};
	scene.steps.push_back(face);
	ChoreoStep gesture = anim("alyx", "g_wave", 1);
	gesture.gesture = true;
	scene.steps.push_back(gesture);
	const std::string vcd = writeVcd(doc, scene);
	CHECK(vcd.rfind("// Choreo version 1", 0) == 0);
	CHECK(vcd.find("faceposermodel \"models/alyx.mdl\"") != std::string::npos);
	CHECK(vcd.find("fps 60") != std::string::npos);
	const auto events = parseVcd(vcd);
	REQUIRE(events.size() == 6);
	const auto byType = [&events](std::string_view type) {
		return std::find_if(events.begin(), events.end(), [type](const VcdEvent& e) { return e.type == type; });
	};
	const auto pause = byType("section");
	REQUIRE(pause != events.end());
	CHECK(pause->actor.empty());
	CHECK(pause->param == "noaction");
	CHECK(pause->start == doctest::Approx(2.0));
	CHECK(pause->end == doctest::Approx(-1));
	const auto walk = byType("moveto");
	REQUIRE(walk != events.end());
	CHECK(walk->actor == "alyx");
	CHECK(walk->channel == "move");
	CHECK(walk->param == "intro_p1");
	CHECK(walk->param2 == "Walk");
	CHECK(std::find(walk->flags.begin(), walk->flags.end(), "resumecondition") != walk->flags.end());
	CHECK(walk->end == doctest::Approx(2.0));
	const auto wave = byType("sequence");
	REQUIRE(wave != events.end());
	CHECK(wave->param == "wave");
	CHECK(wave->start == doctest::Approx(2.0));
	CHECK(wave->end == doctest::Approx(3.5));
	const auto speak = byType("speak");
	REQUIRE(speak != events.end());
	CHECK(speak->channel == "audio");
	CHECK(speak->param == "mymod.intro.hello");
	CHECK(speak->cctype == "cc_master");
	CHECK(speak->start == doctest::Approx(3.5));
	CHECK(speak->end == doctest::Approx(5.5));
	const auto facing = byType("face");
	REQUIRE(facing != events.end());
	CHECK(facing->param == "intro_p4");
	CHECK(byType("gesture")->param == "g_wave");
}

TEST_CASE("choreography: the plan rides through save and load, the rest of the map stays byte for byte") {
	const std::string vmf =
		"versioninfo\n{\n\t\"editorversion\" \"400\"\n}\nworld\n{\n\t\"id\" \"1\"\n\t\"classname\" \"worldspawn\"\n}\n"
		"entity\n{\n\t\"id\" \"2\"\n\t\"classname\" \"npc_alyx\"\n\t\"targetname\" \"alyx\"\n\t\"origin\" \"0 0 0\"\n}\n";
	MapDocument doc = MapDocument::parse(vmf);
	REQUIRE(doc.serialize() == vmf);
	(void)edit::createChoreoScene(doc, 2, "m");
	const int id = sceneEntity(doc);
	ChoreoScene scene = readChoreoScene(*doc.findEntity(id));
	scene.steps = {move("alyx", {32, 0, 0}), say("alyx", "s.a", 1, "A \"quoted\" | line")};
	(void)edit::setChoreoScene(doc, id, scene);
	const std::string saved = doc.serialize();
	CHECK(saved.rfind(vmf.substr(0, vmf.find("entity")), 0) == 0);   // header and world untouched
	MapDocument again = MapDocument::parse(saved);
	CHECK(again.serialize() == saved);
	const int reread = sceneEntity(again);
	REQUIRE(reread != 0);
	const auto back = readChoreoScene(*again.findEntity(reread));
	REQUIRE(back.steps.size() == 2);
	CHECK(back.steps[1].caption == "A 'quoted' | line");
}

TEST_CASE("choreography: checks - no NPC, unknown name, nothing to say, no nodes, unknown sound and sequence") {
	MapDocument doc;
	addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}});
	ChoreoScene scene;
	scene.name = "s";
	scene.steps = {move("alyx", {500, 0, 0}), say("", "x", 1, ""), say("nobody", "", 1, ""), anim("alyx", "fly"), say("alyx", "missing.sound", 1, "")};
	const auto issues = checkChoreoScene(doc, scene, [](std::string_view s) { return s != "missing.sound"; },
	                                     [](std::string_view, std::string_view seq) { return std::optional<bool>(seq != "fly"); });
	const auto has = [&issues](int step, std::string_view text) {
		return std::any_of(issues.begin(), issues.end(), [&](const ChoreoIssue& i) { return i.step == step && i.message.find(text) != std::string::npos; });
	};
	CHECK(has(-1, "no info_node"));
	CHECK(has(1, "has no NPC"));
	CHECK(has(2, "no entity is named"));
	CHECK(has(2, "says nothing"));
	CHECK(has(3, "no sequence \"fly\""));
	CHECK(has(4, "not in the game's sound scripts"));
	// A node near the point: the move is fine; far away: a warning for that step.
	addEntity(doc, {{"classname", "info_node"}, {"origin", "480 0 0"}});
	CHECK_FALSE(std::any_of(checkChoreoScene(doc, scene).begin(), checkChoreoScene(doc, scene).end(), [](const ChoreoIssue& i) { return i.step == 0; }));
	scene.steps[0].point = {3000, 0, 0};
	const auto far = checkChoreoScene(doc, scene);
	CHECK(std::any_of(far.begin(), far.end(), [](const ChoreoIssue& i) { return i.step == 0 && i.message.find("384") != std::string::npos; }));
}

TEST_CASE("choreography: files - the .vcd in the mod, the voice line's caption compiled") {
	test::Fixture f;
	test::createFixture(f);
	MapDocument doc;
	const int npc = addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}});
	(void)edit::createChoreoScene(doc, npc, "c17_test");
	const int id = sceneEntity(doc);
	ChoreoScene scene = readChoreoScene(*doc.findEntity(id));
	scene.steps = {say("alyx", voiceLineName(scene.name, "Line 1!"), 1.25, "Hello")};
	(void)edit::setChoreoScene(doc, id, scene);
	CHECK(scene.steps[0].sound == "alyx_scene.line_1");
	std::string error;
	const auto written = writeChoreoFiles(doc, f.mod, &error);
	CHECK(error.empty());
	REQUIRE(written == std::vector<std::string>{"scenes/c17_test/alyx_scene.vcd"});
	const auto text = readFileText(f.mod / "scenes/c17_test/alyx_scene.vcd");
	REQUIRE(text);
	CHECK(parseVcd(*text).size() == 1);
	// The caption: token = the sound script name, in closecaption_english.txt and its .dat.
	REQUIRE(setVoiceCaption(f.mod, scene.steps[0].sound, "Hello", &error));
	const auto captions = loadLocalizationFile(f.mod / "resource/closecaption_english.txt");
	REQUIRE(captions);
	CHECK(captions->get("alyx_scene.line_1") == "Hello");
	CHECK(std::filesystem::exists(f.mod / "resource/closecaption_english.dat"));
	// Kept tokens stay; a second line goes next to the first.
	REQUIRE(setVoiceCaption(f.mod, "alyx_scene.line_2", "Bye", &error));
	const auto both = loadLocalizationFile(f.mod / "resource/closecaption_english.txt");
	CHECK(both->get("alyx_scene.line_1") == "Hello");
	CHECK(both->get("alyx_scene.line_2") == "Bye");
	// Scene files never leave the scenes folder.
	doc.findEntity(id)->set("SceneFile", "../../evil.vcd");
	CHECK(writeChoreoFiles(doc, f.mod).empty());
}

TEST_CASE("choreography: Ship packs the scene file and the sounds its steps say") {
	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	auto result = discoverGames(SteamInstallation::load(f.steam));
	const GameEntry* entry = nullptr;
	for (const auto& g : result.games) {
		if (g.key() == "sourcemods/mymod") entry = &g;
	}
	REQUIRE(entry);
	const auto game = loadGame(*entry, result.steam, entry->fgds);
	REQUIRE(game);
	MapDocument doc;
	const int npc = addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}});
	(void)edit::createChoreoScene(doc, npc, "demo");
	const int id = sceneEntity(doc);
	ChoreoScene scene = readChoreoScene(*doc.findEntity(id));
	scene.steps = {say("alyx", "Demo.Wind", 1, "Windy")};
	(void)edit::setChoreoScene(doc, id, scene);
	REQUIRE(writeChoreoFiles(doc, f.mod).size() == 1);
	const auto deps = mapDependencies(doc, game->files, f.mod);
	const auto has = [&deps](std::string_view path) {
		return std::any_of(deps.files.begin(), deps.files.end(), [path](const ShipFile& file) { return iequals(file.path, path); });
	};
	CHECK(has("scenes/demo/alyx_scene.vcd"));
	CHECK(has("sound/ambient/wind1.wav"));
	// The plan keys themselves are no file names.
	CHECK_FALSE(std::any_of(deps.files.begin(), deps.files.end(), [](const ShipFile& file) { return file.path.find('|') != std::string::npos; }));
}
