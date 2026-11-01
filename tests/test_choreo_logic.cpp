// test_choreo_logic.cpp - What starts NPC scenes and under which conditions: the entities made
// for every start and condition, the chain, variables and "Set variable" steps, regeneration
// without duplicates, older maps, renames, copies and the checks.

#include <sme/brush.h>
#include <sme/choreography.h>
#include <sme/history.h>
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

const Entity* named(const MapDocument& doc, std::string_view name) {
	return findNamedEntity(doc, name);
}

bool hasLink(const Entity* e, std::string_view output, std::string_view target, std::string_view input, std::string_view parameter = {}) {
	if (!e) return false;
	return std::any_of(e->connections.begin(), e->connections.end(), [&](const Connection& c) {
		return iequals(c.output, output) && iequals(c.target, target) && iequals(c.input, input) && c.parameter == parameter;
	});
}

int countLinks(const Entity* e, std::string_view target) {
	if (!e) return 0;
	return static_cast<int>(std::count_if(e->connections.begin(), e->connections.end(), [&](const Connection& c) { return iequals(c.target, target); }));
}

ChoreoStep say(std::string actor, std::string sound) {
	ChoreoStep s;
	s.kind = ChoreoStep::Kind::Say;
	s.actor = std::move(actor);
	s.sound = std::move(sound);
	s.seconds = 1;
	return s;
}

ChoreoStep setVariable(std::string variable, ChoreoStep::VariableOp op, double value) {
	ChoreoStep s;
	s.kind = ChoreoStep::Kind::SetVariable;
	s.variable = std::move(variable);
	s.op = op;
	s.value = value;
	return s;
}

ChoreoStart start(ChoreoStart::Kind kind, std::string target = {}, bool once = true) {
	ChoreoStart s;
	s.kind = kind;
	s.target = std::move(target);
	s.once = once;
	return s;
}

ChoreoCondition condition(ChoreoCondition::Kind kind, std::string name = {}, double value = 0, ChoreoCondition::Compare compare = ChoreoCondition::Compare::Equal) {
	ChoreoCondition c;
	c.kind = kind;
	c.name = std::move(name);
	c.value = value;
	c.compare = compare;
	return c;
}

/// A small map: a floor (so "inside the map" means something), Alyx, a button, a scene for her.
struct World {
	MapDocument doc;
	History history;
	int alyx = 0, button = 0;

	World() {
		Bounds floor;
		floor.add(Vec3(-1024, -1024, -16));
		floor.add(Vec3(1024, 1024, 256));
		doc.world.solids.push_back(makeBlock(doc, floor, "dev/dev_measuregeneric01"));
		alyx = addEntity(doc, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}, {"angles", "0 0 0"}});
		button = addEntity(doc, {{"classname", "func_button"}, {"targetname", "btn"}});
	}
	/// A new scene of Alyx named `name` with one line, returns its id.
	int scene(std::string name) {
		history.push(edit::createChoreoScene(doc, alyx, "m"));
		const int id = choreoScenes(doc).back();
		ChoreoScene plan = readChoreoScene(*doc.findEntity(id));
		plan.name = std::move(name);
		plan.steps = {say("alyx", "s." + plan.name)};
		history.push(edit::setChoreoScene(doc, id, plan));
		return id;
	}
	void set(int id, const std::function<void(ChoreoScene&)>& change) {
		ChoreoScene plan = readChoreoScene(doc, *doc.findEntity(id));
		change(plan);
		history.push(edit::setChoreoScene(doc, id, plan));
	}
	std::size_t helperCount(std::string_view scene) const {
		return static_cast<std::size_t>(std::count_if(doc.entities.begin(), doc.entities.end(), [&](const Entity& e) { return iequals(e.get(CHOREO_SCENE_KEY), scene); }));
	}
};

} // namespace

TEST_CASE("choreo logic: starts and conditions survive the trip through a keyvalue") {
	ChoreoStart s = start(ChoreoStart::Kind::Output, "door|1", false);
	s.output = "OnFullyOpen";
	s.delay = 1.5;
	s.a = 3;
	s.b = 7;
	const auto back = decodeChoreoStart(encodeChoreoStart(s));
	REQUIRE(back);
	CHECK(*back == s);
	CHECK_FALSE(decodeChoreoStart("teleport|1"));
	const ChoreoCondition c = condition(ChoreoCondition::Kind::Variable, "alyx_mood", 2, ChoreoCondition::Compare::Greater);
	const auto cBack = decodeChoreoCondition(encodeChoreoCondition(c));
	REQUIRE(cBack);
	CHECK(*cBack == c);
	CHECK(describeChoreoStart(s) == "door|1 OnFullyOpen, 1.5 s later");
	CHECK(describeChoreoCondition(c) == "alyx_mood > 2");
	CHECK(describeChoreoCondition(condition(ChoreoCondition::Kind::ScenePlayed, "intro")) == "scene intro has played");
	const auto step = decodeChoreoStep(encodeChoreoStep(setVariable("count", ChoreoStep::VariableOp::Add, 2)));
	REQUIRE(step);
	CHECK(step->kind == ChoreoStep::Kind::SetVariable);
	CHECK(step->variable == "count");
	CHECK(step->op == ChoreoStep::VariableOp::Add);
	CHECK(step->value == doctest::Approx(2));
	CHECK(defaultUseOutput("func_button") == "OnPressed");
	CHECK(defaultUseOutput("npc_alyx") == "OnPlayerUse");
}

TEST_CASE("choreo logic: every kind of start makes its entity, aimed at the scene's relay") {
	World w;
	const int id = w.scene("greet");
	CHECK(named(w.doc, "greet_auto1"));   // the first scene of an NPC starts at map start
	ChoreoStart timer = start(ChoreoStart::Kind::Timer);
	timer.a = 5;
	timer.b = 9;
	ChoreoStart output = start(ChoreoStart::Kind::Output, "btn", false);
	output.output = "OnDamaged";
	ChoreoStart near = start(ChoreoStart::Kind::Near);
	near.a = 96;
	ChoreoStart area = start(ChoreoStart::Kind::Area);
	area.delay = 2;
	w.set(id, [&](ChoreoScene& s) {
		s.starts = {start(ChoreoStart::Kind::MapStart), area, start(ChoreoStart::Kind::Use, "btn"), start(ChoreoStart::Kind::LookAt, "btn"), near,
		            start(ChoreoStart::Kind::AfterScene, "other"), output, timer, start(ChoreoStart::Kind::Input)};
	});
	const Entity* relay = named(w.doc, "greet_start");
	REQUIRE(relay);
	CHECK(relay->classname() == "logic_relay");
	CHECK(hasLink(relay, "OnTrigger", "greet", "Start"));
	const Entity* automatic = named(w.doc, "greet_auto1");
	REQUIRE(automatic);
	CHECK(automatic->classname() == "logic_auto");
	CHECK(hasLink(automatic, "OnMapSpawn", "greet_start", "Trigger"));
	const Entity* box = named(w.doc, "greet_area2");
	REQUIRE(box);
	CHECK(box->classname() == "trigger_once");   // once, no condition
	CHECK(box->solids.size() == 1);
	REQUIRE(box->connections.size() == 1);
	CHECK(box->connections[0].delay == doctest::Approx(2));
	// Several starts: "once" through a gate the scene removes when it starts (a timesToFire 1
	// output would fire again after another start played the scene).
	CHECK(named(w.doc, "greet_gate3"));
	CHECK(hasLink(named(w.doc, "btn"), "OnPressed", "greet_gate3", "Trigger"));
	CHECK(hasLink(named(w.doc, "greet"), "OnStart", "greet_gate3", "Kill"));
	const Entity* look = named(w.doc, "greet_look4");
	REQUIRE(look);
	CHECK(look->classname() == "trigger_look");
	CHECK(look->get("target") == "btn");
	const Entity* nearBox = named(w.doc, "greet_near5");
	REQUIRE(nearBox);
	CHECK(nearBox->get("parentname") == "alyx");   // follows the NPC
	REQUIRE(nearBox->solids.size() == 1);
	CHECK(hasLink(named(w.doc, "btn"), "OnDamaged", "greet_start", "Trigger"));   // every time: straight to the relay
	const Entity* clock = named(w.doc, "greet_timer8");
	REQUIRE(clock);
	CHECK(clock->get("UseRandomTime") == "1");
	CHECK(clock->get("UpperRandomBound") == "9");
	CHECK(hasLink(clock, "OnTimer", "greet_start", "Trigger"));
	// A start that fires every time: the relay is off while the scene plays.
	CHECK(hasLink(named(w.doc, "greet"), "OnStart", "greet_start", "Disable"));
	CHECK(hasLink(named(w.doc, "greet"), "OnCompletion", "greet_start", "Enable"));
	// Nothing for "input"; the relay has the one Start output.
	CHECK(relay->connections.size() == 1);
	// Regenerating changes nothing (no duplicates).
	const std::string before = w.doc.serialize();
	Transaction t(w.doc, "sync");
	CHECK_FALSE(edit::syncChoreoLogic(w.doc, t));
	CHECK(t.finish().empty());
	CHECK(w.doc.serialize() == before);
	// Back to map start only: the other helpers and the outputs on the button go.
	w.set(id, [](ChoreoScene& s) { s.starts = {start(ChoreoStart::Kind::MapStart)}; });
	CHECK(w.helperCount("greet") == 2);   // the logic_auto and the relay
	CHECK(named(w.doc, "btn")->connections.empty());
	CHECK(countLinks(named(w.doc, "greet"), "greet_start") == 0);
}

TEST_CASE("choreo logic: two starts, three conditions - one chain, all must hold") {
	World w;
	const int intro = w.scene("intro");
	const int greet = w.scene("greet");
	REQUIRE(edit::createChoreoVariable(w.doc, "mood", false, 0, {0, 0, 0}).empty() == false);
	REQUIRE(edit::createChoreoVariable(w.doc, "met", true, 0, {0, 0, 0}).empty() == false);
	w.set(greet, [](ChoreoScene& s) {
		s.starts = {start(ChoreoStart::Kind::Area), start(ChoreoStart::Kind::Use, "")};
		s.conditions = {condition(ChoreoCondition::Kind::ScenePlayed, "intro"), condition(ChoreoCondition::Kind::Variable, "mood", 2, ChoreoCondition::Compare::Greater),
		                condition(ChoreoCondition::Kind::Variable, "met", 0)};
	});
	// Area with conditions: trigger_multiple, removed when the scene starts.
	const Entity* box = named(w.doc, "greet_area1");
	REQUIRE(box);
	CHECK(box->classname() == "trigger_multiple");
	CHECK(hasLink(named(w.doc, "greet"), "OnStart", "greet_area1", "Kill"));
	// Use: NPCs fire OnPlayerUse (through a gate: several starts).
	CHECK(hasLink(named(w.doc, "alyx"), "OnPlayerUse", "greet_gate2", "Trigger"));
	// The chain: relay -> played? -> mood > 2? -> met false? -> Start.
	CHECK(hasLink(named(w.doc, "greet_start"), "OnTrigger", "greet_if1", "Test"));
	const Entity* played = named(w.doc, "greet_if1");
	REQUIRE(played);
	CHECK(played->classname() == "logic_branch");
	CHECK(hasLink(named(w.doc, "intro"), "OnCompletion", "greet_if1", "SetValue", "1"));
	CHECK(hasLink(played, "OnTrue", "greet_if2", "Compare"));
	const Entity* mood = named(w.doc, "greet_if2");
	REQUIRE(mood);
	CHECK(mood->classname() == "logic_compare");
	CHECK(mood->get("CompareValue") == "2");
	CHECK(hasLink(named(w.doc, "mood"), "OutValue", "greet_if2", "SetValue"));
	CHECK(hasLink(mood, "OnGreaterThan", "greet_if3", "Test"));
	const Entity* met = named(w.doc, "greet_if3");
	REQUIRE(met);
	CHECK(met->classname() == "logic_branch");
	const Entity* watcher = named(w.doc, "greet_if3w");
	REQUIRE(watcher);
	CHECK(watcher->classname() == "logic_branch_listener");
	CHECK(watcher->get("Branch01") == "met");
	CHECK(hasLink(watcher, "OnAllTrue", "greet_if3", "SetValue", "1"));
	CHECK(hasLink(met, "OnFalse", "greet", "Start"));
	CHECK_FALSE(hasLink(met, "OnTrue", "greet", "Start"));
	CHECK_FALSE(hasLink(named(w.doc, "greet_start"), "OnTrigger", "greet", "Start"));
	(void)intro;
	// Global state and chance.
	w.set(greet, [](ChoreoScene& s) {
		s.conditions = {condition(ChoreoCondition::Kind::Global, "gordon_precriminal", 1), condition(ChoreoCondition::Kind::Random, {}, 30)};
	});
	const Entity* global = named(w.doc, "greet_if1g");
	REQUIRE(global);
	CHECK(global->classname() == "env_global");
	CHECK(global->get("globalstate") == "gordon_precriminal");
	CHECK(global->get("spawnflags") == "0");   // never changes the state itself
	CHECK(hasLink(global, "OutCounter", "greet_if1", "SetValueCompare"));
	CHECK(hasLink(named(w.doc, "greet_start"), "OnTrigger", "greet_if1g", "GetCounter"));
	CHECK(hasLink(named(w.doc, "greet_if1"), "OnEqualTo", "greet_if2", "PickRandom"));
	const Entity* chance = named(w.doc, "greet_if2");
	REQUIRE(chance);
	CHECK(chance->classname() == "logic_case");
	CHECK(countLinks(chance, "greet") == 3);   // 3 of 10 cases go on
	CHECK(countLinks(chance, "greet_if2") == 7);   // the others count as connected but do nothing
	// The old condition entities went (no listener, no mirror left behind).
	CHECK_FALSE(named(w.doc, "greet_if3"));
	CHECK_FALSE(named(w.doc, "greet_if3w"));
	CHECK(countLinks(named(w.doc, "intro"), "greet_if1") == 0);
	CHECK(named(w.doc, "mood")->connections.empty());
}

TEST_CASE("choreo logic: Set variable steps fire the scene's trigger outputs; variables renamed and deleted") {
	World w;
	const int id = w.scene("chat");
	(void)edit::createChoreoVariable(w.doc, "talks", false, 1, {0, 0, 0});
	(void)edit::createChoreoVariable(w.doc, "talked", true, 0, {0, 0, 0});
	w.set(id, [](ChoreoScene& s) {
		s.steps.push_back(setVariable("talks", ChoreoStep::VariableOp::Add, 1));
		s.steps.push_back(setVariable("talked", ChoreoStep::VariableOp::Toggle, 0));
		s.steps.push_back(setVariable("talks", ChoreoStep::VariableOp::Set, 5));
	});
	const Entity* scene = named(w.doc, "chat");
	CHECK(hasLink(scene, "OnTrigger1", "talks", "Add", "1"));
	CHECK(hasLink(scene, "OnTrigger2", "talked", "Toggle"));
	CHECK(hasLink(scene, "OnTrigger3", "talks", "SetValue", "5"));
	const auto plan = readChoreoScene(w.doc, *scene);
	const auto events = parseVcd(writeVcd(w.doc, plan));
	const auto fires = std::count_if(events.begin(), events.end(), [](const VcdEvent& e) { return e.type == "firetrigger"; });
	CHECK(fires == 3);
	const auto third = std::find_if(events.begin(), events.end(), [](const VcdEvent& e) { return e.type == "firetrigger" && e.param == "3"; });
	REQUIRE(third != events.end());
	CHECK(third->start == doctest::Approx(1.0));   // after the 1 s line
	const auto vars = choreoVariables(w.doc);
	REQUIRE(vars.size() == 2);
	CHECK(vars[0].name == "talks");
	CHECK(vars[0].initial == doctest::Approx(1));
	CHECK(vars[1].flag);
	// A condition on it, then a rename: steps, condition, outputs follow.
	const int other = w.scene("later");
	w.set(other, [](ChoreoScene& s) { s.conditions = {condition(ChoreoCondition::Kind::Variable, "talks", 3, ChoreoCondition::Compare::Greater)}; });
	w.history.push(edit::renameChoreoVariable(w.doc, "talks", "chats"));
	CHECK(hasLink(named(w.doc, "chat"), "OnTrigger1", "chats", "Add", "1"));
	CHECK(countLinks(named(w.doc, "chat"), "talks") == 0);
	CHECK(readChoreoScene(*named(w.doc, "later")).conditions[0].name == "chats");
	CHECK(hasLink(named(w.doc, "chats"), "OutValue", "later_if1", "SetValue"));
	CHECK(edit::renameChoreoVariable(w.doc, "chats", "alyx").empty());   // taken
	// Starting value.
	w.history.push(edit::setChoreoVariableInitial(w.doc, "chats", 4));
	CHECK(named(w.doc, "later_if1")->get("InitialValue") == "4");
	// Deleted: the outputs go, the steps stay and the checks name them.
	w.history.push(edit::deleteChoreoVariable(w.doc, "talked"));
	CHECK(countLinks(named(w.doc, "chat"), "talked") == 0);
	const auto issues = checkChoreoLogic(w.doc);
	CHECK(std::any_of(issues.begin(), issues.end(), [](const ChoreoLogicIssue& i) { return i.scene == "chat" && i.message.find("no such variable") != std::string::npos; }));
	// Undo everything: the map as it was before the first scene.
	while (w.history.undo(w.doc)) {
	}
	CHECK(choreoScenes(w.doc).empty());
	CHECK(choreoVariables(w.doc).size() == 2);   // made outside the history
}

TEST_CASE("choreo logic: hand-made entities and outputs stay; save and load; older maps are taken over") {
	World w;
	const int id = w.scene("greet");
	w.set(id, [](ChoreoScene& s) { s.starts = {start(ChoreoStart::Kind::Use, "btn")}; });
	// The mapper wires the button to the relay too, and adds an unrelated output.
	Entity* button = w.doc.findEntity(w.button);
	CHECK(hasLink(button, "OnPressed", "greet_start", "Trigger"));
	const int lamp = addEntity(w.doc, {{"classname", "light"}, {"targetname", "lamp"}});
	addEntity(w.doc, {{"classname", "func_button"}, {"targetname", "btn2"}});
	w.doc.findEntity(lamp)->connections.push_back(Connection{"OnUser1", "greet_start", "Trigger", "", 0, -1});
	button->connections.push_back(Connection{"OnPressed", "lamp", "Toggle", "", 0, -1});
	// Switching the start to the other button: only Mapsmith's output moves.
	w.set(id, [](ChoreoScene& s) { s.starts = {start(ChoreoStart::Kind::Use, "btn2")}; });
	CHECK_FALSE(hasLink(w.doc.findEntity(w.button), "OnPressed", "greet_start", "Trigger"));
	CHECK(hasLink(w.doc.findEntity(w.button), "OnPressed", "lamp", "Toggle"));
	CHECK(hasLink(w.doc.findEntity(lamp), "OnUser1", "greet_start", "Trigger"));   // hand-made, kept
	CHECK(hasLink(named(w.doc, "btn2"), "OnPressed", "greet_start", "Trigger"));
	CHECK(named(w.doc, "greet")->get("mapsmith_links") == "btn2");
	// Save and load: the same plan, and nothing to regenerate.
	MapDocument again = MapDocument::parse(w.doc.serialize());
	const int reread = choreoScenes(again).front();
	CHECK(readChoreoScene(again, *again.findEntity(reread)) == readChoreoScene(w.doc, *w.doc.findEntity(id)));
	Transaction t(again, "sync");
	CHECK_FALSE(edit::syncChoreoLogic(again, t));

	// An older map: one "mapsmith_trigger" and a trigger_once the mapper resized.
	MapDocument old;
	const int npc = addEntity(old, {{"classname", "npc_alyx"}, {"targetname", "alyx"}, {"origin", "0 0 0"}});
	(void)npc;
	const int scene = addEntity(old, {{"classname", "logic_choreographed_scene"}, {"targetname", "talk"}, {"SceneFile", "scenes/m/talk.vcd"},
	                                  {"mapsmith_scene", "1"}, {"mapsmith_trigger", "trigger"}, {"mapsmith_step01", encodeChoreoStep(say("alyx", "s.a"))}});
	Entity trigger;
	trigger.id = old.newEntityId();
	trigger.keyValues = {{"classname", "trigger_once"}, {"targetname", "talk_trigger"}, {"spawnflags", "1"}, {"mapsmith_scene", "talk"}};
	Bounds big;
	big.add(Vec3(100, 100, 0));
	big.add(Vec3(400, 300, 128));
	trigger.solids.push_back(makeBlock(old, big, "tools/toolstrigger"));
	trigger.connections.push_back(Connection{"OnStartTouch", "talk", "Start", "", 0, 1});
	const int triggerId = trigger.id;
	old.entities.push_back(trigger);
	const auto legacy = readChoreoScene(*old.findEntity(scene));
	REQUIRE(legacy.starts.size() == 1);
	CHECK(legacy.starts[0].kind == ChoreoStart::Kind::Area);
	CHECK_FALSE(edit::setChoreoScene(old, scene, legacy).empty());
	const Entity* kept = old.findEntity(triggerId);
	REQUIRE(kept);   // the same entity, its box as the mapper made it
	CHECK(kept->get("targetname") == "talk_area1");
	CHECK(kept->get(CHOREO_ROLE_KEY) == "start1");
	REQUIRE(kept->connections.size() == 1);
	CHECK(kept->connections[0].target == "talk_start");
	CHECK(kept->solids.front().id == trigger.solids.front().id);
	CHECK(old.findEntity(scene)->get("mapsmith_trigger").empty());
	CHECK(old.findEntity(scene)->get("mapsmith_start01") == encodeChoreoStart(legacy.starts[0]));
}

TEST_CASE("choreo logic: renaming a scene takes the others' starts and conditions along; copies; names") {
	World w;
	const int intro = w.scene("intro");
	const int greet = w.scene("greet");
	w.set(greet, [](ChoreoScene& s) {
		s.starts = {start(ChoreoStart::Kind::AfterScene, "intro")};
		s.conditions = {condition(ChoreoCondition::Kind::SceneNotPlayed, "intro")};
	});
	w.set(intro, [](ChoreoScene& s) { s.name = "opening"; });
	const auto plan = readChoreoScene(*w.doc.findEntity(greet));
	CHECK(plan.starts[0].target == "opening");
	CHECK(plan.conditions[0].name == "opening");
	CHECK(w.doc.findEntity(intro)->get("SceneFile") == "scenes/m/opening.vcd");
	CHECK(hasLink(named(w.doc, "opening"), "OnCompletion", "greet_gate1", "Trigger"));
	CHECK(hasLink(named(w.doc, "opening"), "OnCompletion", "greet_if1", "SetValue", "1"));
	CHECK(named(w.doc, "opening_start"));
	CHECK_FALSE(named(w.doc, "intro_start"));
	// A name in use stays with its owner.
	CHECK(edit::setChoreoScene(w.doc, greet, [&] {
		      auto p = plan;
		      p.name = "alyx";
		      return p;
	      }())
	          .empty());
	// Copies: alyx_greet -> greet_2, with the same starts and conditions.
	w.history.push(edit::duplicateChoreoScene(w.doc, greet, "m"));
	const int copy = choreoScenes(w.doc).back();
	const auto copied = readChoreoScene(*w.doc.findEntity(copy));
	CHECK(copied.name == "greet_2");
	CHECK(copied.starts == plan.starts);
	CHECK(copied.conditions == plan.conditions);
	CHECK(named(w.doc, "greet_2_start"));
	CHECK(uniqueChoreoName(w.doc, "greet") == "greet_3");
	CHECK(uniqueChoreoName(w.doc, "new scene") == "new_scene");
	CHECK(choreoScenesOfActor(w.doc, "alyx").size() == 3);
	// Deleting: its outputs on other entities go with it.
	w.history.push(edit::deleteChoreoScene(w.doc, greet));
	CHECK_FALSE(hasLink(named(w.doc, "opening"), "OnCompletion", "greet_gate1", "Trigger"));
	CHECK(hasLink(named(w.doc, "opening"), "OnCompletion", "greet_2_gate1", "Trigger"));
	CHECK_FALSE(named(w.doc, "greet_start"));
}

TEST_CASE("choreo logic: checks - outside the map, deleted scenes and variables, two scenes at once, no NPC") {
	World w;
	const int a = w.scene("a");
	const int b = w.scene("b");
	// b was made second: it starts with an area, so it does not clash with a yet.
	CHECK(readChoreoScene(*w.doc.findEntity(b)).starts[0].kind == ChoreoStart::Kind::Area);
	const auto has = [&w](std::string_view scene, std::string_view text) {
		const auto issues = checkChoreoLogic(w.doc);
		return std::any_of(issues.begin(), issues.end(), [&](const ChoreoLogicIssue& i) { return i.scene == scene && i.message.find(text) != std::string::npos; });
	};
	CHECK_FALSE(has("b", "starts together"));
	w.set(b, [](ChoreoScene& s) { s.starts = {start(ChoreoStart::Kind::MapStart)}; });
	CHECK(has("b", "starts together with \"a\""));
	w.set(b, [](ChoreoScene& s) {
		s.conditions = {condition(ChoreoCondition::Kind::ScenePlayed, "gone"), condition(ChoreoCondition::Kind::Variable, "nothing", 1)};
		s.starts = {start(ChoreoStart::Kind::MapStart), start(ChoreoStart::Kind::Use, "nobody"), start(ChoreoStart::Kind::AfterScene, "missing")};
	});
	CHECK_FALSE(has("b", "starts together"));   // it has conditions now
	CHECK(has("b", "no scene \"gone\""));
	CHECK(has("b", "no variable \"nothing\""));
	CHECK(has("b", "no entity is named \"nobody\""));
	CHECK(has("b", "there is no scene \"missing\""));
	// A trigger box dragged into the void.
	w.set(a, [](ChoreoScene& s) { s.starts = {start(ChoreoStart::Kind::Area)}; });
	Entity* box = const_cast<Entity*>(named(w.doc, "a_area1"));
	REQUIRE(box);
	Bounds far;
	far.add(Vec3(5000, 5000, 0));
	far.add(Vec3(5100, 5100, 100));
	box->solids = {makeBlock(w.doc, far, "tools/toolstrigger")};
	CHECK(has("a", "outside the map"));
	// A start aimed at "the NPC" of a scene without any.
	w.set(a, [](ChoreoScene& s) {
		s.steps.clear();
		s.starts = {start(ChoreoStart::Kind::Use)};
	});
	CHECK(has("a", "has no NPC yet"));
	// More than 16 Set variable steps.
	(void)edit::createChoreoVariable(w.doc, "n", false, 0, {0, 0, 0});
	w.set(a, [](ChoreoScene& s) {
		for (int i = 0; i < 17; ++i) s.steps.push_back(setVariable("n", ChoreoStep::VariableOp::Add, 1));
	});
	CHECK(has("a", "more than 16"));
	CHECK(named(w.doc, "a")->connections.size() >= 16);
	CHECK_FALSE(hasLink(named(w.doc, "a"), "OnTrigger17", "n", "Add", "1"));
}
