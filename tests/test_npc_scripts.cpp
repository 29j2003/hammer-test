// test_npc_scripts.cpp - NPCs that play a scene: how a scripted_sequence starts and loops, the
// trigger and map-start helpers, the citizen's fixed look, animated models and sequence filters.

#include "fixture.h"

#include <sme/animation_use.h>
#include <sme/assets.h>
#include <sme/filesystem.h>
#include <sme/history.h>
#include <sme/npc_scripts.h>

#include <doctest/doctest.h>

using namespace sme;

namespace {

int addEntity(MapDocument& doc, std::vector<std::pair<std::string, std::string>> keys) {
	Entity e;
	e.id = doc.newEntityId();
	e.keyValues = std::move(keys);
	doc.entities.push_back(e);
	return e.id;
}

} // namespace

TEST_CASE("npc scripts: how a scripted_sequence starts") {
	MapDocument doc;
	const int npc = addEntity(doc, {{"classname", "npc_citizen"}, {"targetname", "guard"}, {"origin", "0 0 0"}});
	const int script = addEntity(doc, {{"classname", "scripted_sequence"}, {"targetname", "watch"}, {"m_iszEntity", "guard"},
	                                   {"m_iszIdle", "idle_angry"}, {"m_iszPlay", "d1_t01_BreakRoom_WatchBreen"},
	                                   {"origin", "64 0 0"}, {"angles", "0 90 0"}, {"spawnflags", "16"}});
	// Julius' case: Start on Spawn on a named script only parks the NPC in the Pre Action Idle.
	ScriptState state = scriptState(doc, script);
	CHECK(state.start == ScriptState::Start::WaitsInIdle);
	CHECK(state.startOnSpawn);
	CHECK(state.npcId == npc);
	CHECK(state.loop == ScriptState::Loop::Once);
	doc.findEntity(script)->set("spawnflags", "0");
	CHECK(scriptState(doc, script).start == ScriptState::Start::Never);
	doc.findEntity(script)->set("targetname", "");
	CHECK(scriptState(doc, script).start == ScriptState::Start::Immediately);
	CHECK(scriptState(doc, npc).start == ScriptState::Start::Never);   // not a script: default state

	// Start at map start: names the script, Start on Spawn, a new logic_auto fires BeginSequence.
	History history;
	const std::size_t count = doc.entities.size();
	Change change = edit::startSequenceAtMapStart(doc, script);
	REQUIRE_FALSE(change.empty());
	CHECK(doc.findEntity(script)->get("targetname") == "guard_script");
	CHECK(doc.findEntity(script)->get("spawnflags") == "16");
	REQUIRE(doc.entities.size() == count + 1);
	const Entity& automatic = doc.entities.back();
	CHECK(automatic.classname() == "logic_auto");
	REQUIRE(automatic.connections.size() == 1);
	CHECK(automatic.connections[0].output == "OnMapSpawn");
	CHECK(automatic.connections[0].target == "guard_script");
	CHECK(automatic.connections[0].input == "BeginSequence");
	state = scriptState(doc, script);
	CHECK(state.start == ScriptState::Start::AtMapStart);
	CHECK(state.starters == std::vector<int>{automatic.id});
	CHECK(edit::startSequenceAtMapStart(doc, script).empty());   // already
	history.push(std::move(change));
	CHECK(history.undo(doc));
	CHECK(doc.entities.size() == count);
	CHECK(doc.findEntity(script)->get("targetname").empty());

	// An existing logic_auto takes the output instead of a new one.
	const int existing = addEntity(doc, {{"classname", "logic_auto"}, {"origin", "0 0 0"}});
	change = edit::startSequenceAtMapStart(doc, script);
	CHECK(doc.entities.size() == count + 1);
	CHECK(doc.findEntity(existing)->connections.size() == 1);
}

TEST_CASE("npc scripts: start with a trigger") {
	MapDocument doc;
	addEntity(doc, {{"classname", "npc_citizen"}, {"targetname", "guard"}, {"origin", "0 0 0"}});
	const int script = addEntity(doc, {{"classname", "scripted_sequence"}, {"targetname", "watch"}, {"m_iszEntity", "guard"},
	                                   {"origin", "64 0 8"}, {"angles", "0 90 0"}, {"spawnflags", "4"}});
	// The box: on the script's floor, 192 units in front of it (yaw 90: +Y).
	const Bounds box = sequenceTriggerBox(doc, script);
	CHECK(box.min.z == doctest::Approx(16));   // snapped to 16
	CHECK(box.max.z - box.min.z == doctest::Approx(112));
	CHECK((box.min.x + box.max.x) / 2 == doctest::Approx(64));
	CHECK((box.min.y + box.max.y) / 2 == doctest::Approx(192));

	const std::size_t count = doc.entities.size();
	Change change = edit::addSequenceTrigger(doc, script, box);
	REQUIRE_FALSE(change.empty());
	REQUIRE(doc.entities.size() == count + 1);
	const Entity& trigger = doc.entities.back();
	CHECK(trigger.classname() == "trigger_once");
	CHECK(trigger.get("spawnflags") == "1");
	REQUIRE(trigger.solids.size() == 1);
	CHECK(trigger.solids[0].sides.size() == 6);
	CHECK(trigger.solids[0].sides[0].material == "tools/toolstrigger");
	REQUIRE(trigger.connections.size() == 1);
	CHECK(trigger.connections[0].output == "OnStartTouch");
	CHECK(trigger.connections[0].target == "watch");
	CHECK(trigger.connections[0].input == "BeginSequence");
	// Start on Spawn: the NPC waits at the spot until the player walks in. Other flags stay.
	CHECK(doc.findEntity(script)->get("spawnflags") == "20");
	const ScriptState state = scriptState(doc, script);
	CHECK(state.start == ScriptState::Start::ByOthers);
	CHECK(state.starters == std::vector<int>{trigger.id});
	History history;
	history.push(std::move(change));
	CHECK(history.undo(doc));
	CHECK(doc.entities.size() == count);
	CHECK(doc.findEntity(script)->get("spawnflags") == "4");
	CHECK(edit::addSequenceTrigger(doc, 0, box).empty());
	CHECK(edit::addSequenceTrigger(doc, script, Bounds{}).empty());
}

TEST_CASE("npc scripts: looping the action or the post idle") {
	MapDocument doc;
	const int script = addEntity(doc, {{"classname", "scripted_sequence"}, {"m_iszPlay", "wave"}, {"spawnflags", "256"}});
	// Loop in Post Idle without a Post Action Idle loops nothing.
	CHECK(scriptState(doc, script).loop == ScriptState::Loop::Once);
	Change change = edit::setSequenceLoop(doc, script, ScriptState::Loop::Action);
	REQUIRE_FALSE(change.empty());
	CHECK(doc.findEntity(script)->get("m_bLoopActionSequence") == "1");
	CHECK(doc.findEntity(script)->get("spawnflags") == "0");
	CHECK(scriptState(doc, script).loop == ScriptState::Loop::Action);
	CHECK(edit::setSequenceLoop(doc, script, ScriptState::Loop::Action).empty());
	change = edit::setSequenceLoop(doc, script, ScriptState::Loop::PostIdle);
	CHECK(doc.findEntity(script)->get("m_bLoopActionSequence") == "0");
	CHECK(doc.findEntity(script)->get("m_iszPostIdle") == "wave");   // the action, as nothing else was set
	CHECK(doc.findEntity(script)->get("spawnflags") == "256");
	CHECK(scriptState(doc, script).loop == ScriptState::Loop::PostIdle);
	change = edit::setSequenceLoop(doc, script, ScriptState::Loop::Once);
	CHECK(doc.findEntity(script)->get("spawnflags") == "0");
	CHECK(scriptState(doc, script).loop == ScriptState::Loop::Once);
}

TEST_CASE("npc scripts: a citizen keeps its head") {
	MapDocument doc;
	const int citizen = addEntity(doc, {{"classname", "npc_citizen"}, {"citizentype", "3"}});
	Change change = edit::keepCitizenLook(doc, citizen);
	REQUIRE_FALSE(change.empty());
	CHECK(doc.findEntity(citizen)->get("citizentype") == "4");
	CHECK(doc.findEntity(citizen)->get("model") == "models/humans/group03/male_07.mdl");
	CHECK(edit::keepCitizenLook(doc, citizen).empty());
	change = edit::keepCitizenLook(doc, citizen, "models/Humans/Group01/female_02.mdl");
	CHECK(doc.findEntity(citizen)->get("model") == "models/humans/group01/female_02.mdl");
	const int zombie = addEntity(doc, {{"classname", "npc_zombie"}});
	CHECK(edit::keepCitizenLook(doc, zombie).empty());
	const auto heads = citizenModels();
	CHECK(heads.size() == 60);
	CHECK(heads.front() == "models/humans/group01/male_01.mdl");
}

TEST_CASE("npc scripts: models with real animations") {
	ModelInfo bottle;
	bottle.boneCount = 1;
	bottle.sequences = {"idle"};
	CHECK_FALSE(isAnimatedModel(bottle));
	bottle.sequences = {"idle", "spin"};
	CHECK_FALSE(isAnimatedModel(bottle));   // one bone: nothing in it moves
	ModelInfo corpse;
	corpse.boneCount = 20;
	corpse.sequences = {"ragdoll", "Idle01", "@ref"};
	CHECK_FALSE(isAnimatedModel(corpse));
	corpse.sequences.push_back("twitch");
	CHECK(isAnimatedModel(corpse));
	ModelInfo citizen;
	citizen.boneCount = 50;
	citizen.sequences = {"idle"};
	citizen.includeModels = {"models/humans/male_shared.mdl"};
	CHECK(isAnimatedModel(citizen));
	CHECK(isAnimationOnlyModel("models/humans/male_shared.mdl"));
	CHECK(isAnimationOnlyModel("models\\Police_SS.mdl"));
	CHECK_FALSE(isAnimationOnlyModel("models/humans/group01/male_07.mdl"));

	// The demo game: the citizen animates, the bottle and the crate do not.
	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	GameFileSystem files;
	files.addDirectory(f.mod, {"game"});
	files.addDirectory(f.sdkBase / "hl2", {"game"});
	CHECK(hasAnimations(files, "models/humans/group01/male_07.mdl"));
	CHECK_FALSE(hasAnimations(files, "models/props_junk/glassbottle01a.mdl"));
	CHECK_FALSE(hasAnimations(files, "models/props_demo/crate01.mdl"));
	CHECK_FALSE(hasAnimations(files, "models/humans/male_shared.mdl"));
	CHECK_FALSE(hasAnimations(files, "models/missing.mdl"));
}

TEST_CASE("npc scripts: sorting sequences by weapon, stance and kind") {
	using namespace sequence_weapon;
	auto s = classifySequence("Idle_Relaxed_SMG1_1", "ACT_IDLE_RELAXED");
	CHECK(s.weapons == SMG1);
	CHECK_FALSE(s.crouch);
	CHECK(s.kind == SequenceKind::Idle);
	s = classifySequence("walk_aiming_rifle", "ACT_WALK_AIM_RIFLE");
	CHECK(s.weapons == (SMG1 | AR2));
	CHECK(s.kind == SequenceKind::Move);
	s = classifySequence("crouch_idle", "ACT_CROUCHIDLE");
	CHECK(s.weapons == UNARMED);
	CHECK(s.crouch);
	CHECK(s.kind == SequenceKind::Idle);
	s = classifySequence("shootAR2c", "ACT_RANGE_ATTACK_AR2_LOW");
	CHECK(s.weapons == AR2);
	CHECK(s.crouch);
	CHECK(s.kind == SequenceKind::Action);
	s = classifySequence("d1_t01_BreakRoom_WatchBreen", "");
	CHECK(s.kind == SequenceKind::Scene);
	CHECK(s.weapons == UNARMED);
	s = classifySequence("ep1_c17_trainstation_idle", "");
	CHECK(s.kind == SequenceKind::Scene);
	s = classifySequence("lean_wall", "");
	CHECK(s.kind == SequenceKind::Scene);
	s = classifySequence("idle_angry", "");
	CHECK(s.kind == SequenceKind::Idle);
	s = classifySequence("run_all", "ACT_RUN");
	CHECK(s.kind == SequenceKind::Move);
	s = classifySequence("g_wave", "ACT_GESTURE_WAVE");
	CHECK(s.kind == SequenceKind::Gesture);
	s = classifySequence("flinch_head", "ACT_FLINCH_HEAD", true);
	CHECK(s.kind == SequenceKind::Gesture);
	s = classifySequence("pistol_draw", "ACT_ARM");
	CHECK(s.weapons == PISTOL);
	s = classifySequence("swing", "ACT_MELEE_ATTACK_SWING");
	CHECK(s.weapons == MELEE);
	s = classifySequence("throw1", "ACT_RANGE_ATTACK_THROW");
	CHECK(s.weapons == GRENADE);
	s = classifySequence("Idle_Unarmed", "ACT_IDLE");
	CHECK(s.weapons == UNARMED);
	s = classifySequence("ar2_idle", "ACT_IDLE_AR2");
	CHECK(s.kind == SequenceKind::Idle);   // a weapon name is no scene
	s = classifySequence("sitchair1", "ACT_BUSY_SIT_CHAIR");
	CHECK(s.kind == SequenceKind::Idle);
}
