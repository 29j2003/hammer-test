// npc_scripts.cpp - NPCs that play a scene (see npc_scripts.h).

#include <sme/npc_scripts.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <cmath>
#include <numbers>

namespace sme {

namespace {

long long flagsOf(const Entity& entity) {
	try {
		return std::stoll(entity.get("spawnflags", "0"));
	} catch (...) {
		return 0;
	}
}

void setFlags(Entity& entity, long long set, long long clear) {
	entity.set("spawnflags", std::to_string((flagsOf(entity) | set) & ~clear));
}

bool isScript(const Entity* entity) {
	return entity && iequals(entity->classname(), "scripted_sequence");
}

/// The script's name, giving it one ("<npc>_script" or "script_<id>") if it has none.
std::string nameScript(MapDocument& doc, Entity& script) {
	std::string name = script.get("targetname");
	if (!name.empty()) return name;
	const std::string npc = script.get("m_iszEntity");
	name = npc.empty() ? "script_" + std::to_string(script.id) : npc + "_script";
	for (const auto& other : doc.entities) {
		if (other.id != script.id && iequals(other.get("targetname"), name)) {
			name += "_" + std::to_string(script.id);
			break;
		}
	}
	script.set("targetname", name);
	return name;
}

bool firesBegin(const Connection& c, std::string_view scriptName) {
	return !scriptName.empty() && iequals(c.target, scriptName) && iequals(c.input, "BeginSequence");
}

/// The citizen group folder of a citizentype (Default and Downtrodden: Group01, Refugee: Group02, Rebel: Group03).
std::string citizenGroup(std::string_view type) {
	if (type == "2") return "group02";
	if (type == "3") return "group03";
	return "group01";
}

} // namespace

ScriptState scriptState(const MapDocument& doc, int scriptId) {
	ScriptState state;
	const Entity* script = doc.findEntity(scriptId);
	if (!isScript(script) || scriptId == 0) return state;
	const long long flags = flagsOf(*script);
	state.startOnSpawn = (flags & script_flags::START_ON_SPAWN) != 0;
	const std::string name = script->get("targetname");
	const std::string npc = script->get("m_iszEntity");
	bool atMapStart = false;
	for (const auto& other : doc.entities) {
		if (other.id != scriptId && !npc.empty() && iequals(other.get("targetname"), npc) && !isScript(&other)) state.npcId = other.id;
		for (const auto& c : other.connections) {
			if (!firesBegin(c, name)) continue;
			state.starters.push_back(other.id);
			if (iequals(other.classname(), "logic_auto")) atMapStart = true;
			break;
		}
	}
	if (name.empty()) {
		state.start = ScriptState::Start::Immediately;
	} else if (atMapStart) {
		state.start = ScriptState::Start::AtMapStart;
	} else if (!state.starters.empty()) {
		state.start = ScriptState::Start::ByOthers;
	} else if (state.startOnSpawn) {
		state.start = ScriptState::Start::WaitsInIdle;
	}
	if (script->get("m_bLoopActionSequence") == "1") {
		state.loop = ScriptState::Loop::Action;
	} else if ((flags & script_flags::LOOP_POST_IDLE) && !trim(script->get("m_iszPostIdle")).empty()) {
		state.loop = ScriptState::Loop::PostIdle;
	}
	return state;
}

Bounds sequenceTriggerBox(const MapDocument& doc, int scriptId) {
	const Entity* script = doc.findEntity(scriptId);
	Vec3 origin, angles;
	if (script && scriptId != 0) {
		origin = script->origin();
		parseVec3(script->get("angles", "0 0 0"), angles);
	}
	const double yaw = angles.y * std::numbers::pi / 180.0;
	const Vec3 centre = origin + Vec3(std::cos(yaw), std::sin(yaw), 0) * 192.0;
	const auto snap = [](double v) { return std::round(v / 16.0) * 16.0; };
	Bounds box;
	box.add(Vec3(snap(centre.x) - 64, snap(centre.y) - 64, snap(origin.z)));
	box.add(Vec3(snap(centre.x) + 64, snap(centre.y) + 64, snap(origin.z) + 112));
	return box;
}

std::vector<std::string> citizenModels() {
	std::vector<std::string> models;
	for (const char* group : {"group01", "group02", "group03", "group03m"}) {
		for (int i = 1; i <= 9; ++i) models.push_back(std::string("models/humans/") + group + "/male_0" + std::to_string(i) + ".mdl");
		for (int i : {1, 2, 3, 4, 6, 7}) models.push_back(std::string("models/humans/") + group + "/female_0" + std::to_string(i) + ".mdl");
	}
	return models;
}

namespace edit {

Change poseNpc(MapDocument& doc, int npcId, std::string_view sequence, long long setNpcFlags, long long clearNpcFlags) {
	const Entity* found = doc.findEntity(npcId);
	if (!found || npcId == 0 || trim(sequence).empty()) return {};
	const std::string name(trim(sequence));
	constexpr long long POSE_FLAGS = script_flags::START_ON_SPAWN | script_flags::NO_INTERRUPTIONS | script_flags::OVERRIDE_AI;
	Transaction t(doc, "Use animation");
	t.touchEntity(npcId);
	Entity* npc = doc.findEntity(npcId);
	std::string npcName = npc->get("targetname");
	if (npcName.empty()) {
		npcName = npc->classname() + "_" + std::to_string(npc->id);
		npc->set("targetname", npcName);
	}
	if (setNpcFlags || clearNpcFlags) setFlags(*npc, setNpcFlags, clearNpcFlags);
	const Vec3 origin = npc->origin();
	const std::string angles = npc->get("angles", "0 0 0");
	for (auto& other : doc.entities) {
		if (isScript(&other) && iequals(other.get("targetname"), npcName + "_pose")) {
			t.touchEntity(other.id);
			other.set("m_iszIdle", name);
			other.set("m_iszEntity", npcName);
			// Helpers from before kept the NPC idle only after a BeginSequence nothing sent.
			setFlags(other, POSE_FLAGS, script_flags::LOOP_POST_IDLE);
			return t.finish();
		}
	}
	Entity script;
	script.id = doc.newEntityId();
	t.touchEntity(script.id);
	script.set("classname", "scripted_sequence");
	script.set("targetname", npcName + "_pose");
	script.set("m_iszEntity", npcName);
	script.set("m_iszIdle", name);
	script.set("m_fMoveTo", "0");
	script.set("onplayerdeath", "0");
	script.setOrigin(origin);
	script.set("angles", angles);
	script.set("spawnflags", std::to_string(POSE_FLAGS));
	doc.entities.push_back(std::move(script));
	return t.finish();
}

Change startSequenceAtMapStart(MapDocument& doc, int scriptId) {
	if (!isScript(doc.findEntity(scriptId)) || scriptId == 0) return {};
	const ScriptState state = scriptState(doc, scriptId);
	if (state.start == ScriptState::Start::AtMapStart && state.startOnSpawn) return {};
	Transaction t(doc, "Start script at map start");
	t.touchEntity(scriptId);
	Entity* script = doc.findEntity(scriptId);
	const std::string name = nameScript(doc, *script);
	setFlags(*script, script_flags::START_ON_SPAWN, 0);
	const Vec3 origin = script->origin();
	if (state.start != ScriptState::Start::AtMapStart) {
		Connection begin;
		begin.output = "OnMapSpawn";
		begin.target = name;
		begin.input = "BeginSequence";
		begin.timesToFire = 1;
		for (auto& other : doc.entities) {
			if (iequals(other.classname(), "logic_auto")) {
				t.touchEntity(other.id);
				other.connections.push_back(std::move(begin));
				return t.finish();
			}
		}
		Entity automatic;
		automatic.id = doc.newEntityId();
		t.touchEntity(automatic.id);
		automatic.set("classname", "logic_auto");
		automatic.set("spawnflags", "1");   // Remove on fire: once per map, not again after loading a save
		automatic.setOrigin(origin + Vec3(0, 0, 32));
		automatic.connections.push_back(std::move(begin));
		doc.entities.push_back(std::move(automatic));
	}
	return t.finish();
}

Change addSequenceTrigger(MapDocument& doc, int scriptId, const Bounds& box) {
	if (!isScript(doc.findEntity(scriptId)) || scriptId == 0 || box.empty()) return {};
	Transaction t(doc, "Start script with a trigger");
	t.touchEntity(scriptId);
	Entity* script = doc.findEntity(scriptId);
	const std::string name = nameScript(doc, *script);
	setFlags(*script, script_flags::START_ON_SPAWN, 0);
	Entity trigger;
	trigger.id = doc.newEntityId();
	t.touchEntity(trigger.id);
	trigger.set("classname", "trigger_once");
	trigger.set("targetname", name + "_trigger");
	trigger.set("spawnflags", "1");   // Clients (the player)
	trigger.set("StartDisabled", "0");
	Connection begin;
	begin.output = "OnStartTouch";
	begin.target = name;
	begin.input = "BeginSequence";
	begin.timesToFire = 1;
	trigger.connections.push_back(std::move(begin));
	trigger.solids.push_back(makeBlock(doc, box, "tools/toolstrigger"));
	doc.entities.push_back(std::move(trigger));
	return t.finish();
}

Change setSequenceLoop(MapDocument& doc, int scriptId, ScriptState::Loop loop) {
	const Entity* found = doc.findEntity(scriptId);
	if (!isScript(found) || scriptId == 0) return {};
	Entity changed = *found;
	switch (loop) {
		case ScriptState::Loop::Once:
			changed.set("m_bLoopActionSequence", "0");
			setFlags(changed, 0, script_flags::LOOP_POST_IDLE);
			break;
		case ScriptState::Loop::Action:
			changed.set("m_bLoopActionSequence", "1");
			setFlags(changed, 0, script_flags::LOOP_POST_IDLE);
			break;
		case ScriptState::Loop::PostIdle:
			changed.set("m_bLoopActionSequence", "0");
			setFlags(changed, script_flags::LOOP_POST_IDLE, 0);
			if (trim(changed.get("m_iszPostIdle")).empty()) changed.set("m_iszPostIdle", changed.get("m_iszPlay"));
			break;
	}
	if (changed.keyValues == found->keyValues) return {};
	Transaction t(doc, "Script loop");
	t.touchEntity(scriptId);
	doc.findEntity(scriptId)->keyValues = std::move(changed.keyValues);
	return t.finish();
}

Change keepCitizenLook(MapDocument& doc, int npcId, std::string_view model) {
	const Entity* npc = doc.findEntity(npcId);
	if (!npc || npcId == 0 || !iequals(npc->classname(), "npc_citizen")) return {};
	std::string chosen = normalizePath(model);
	if (chosen.empty()) chosen = normalizePath(npc->get("model"));
	if (!startsWithI(chosen, "models/humans/")) chosen = "models/humans/" + citizenGroup(npc->get("citizentype")) + "/male_07.mdl";
	if (npc->get("citizentype") == "4" && iequals(normalizePath(npc->get("model")), chosen)) return {};
	Transaction t(doc, "Fixed look");
	t.touchEntity(npcId);
	Entity* changed = doc.findEntity(npcId);
	changed->set("citizentype", "4");
	changed->set("model", chosen);
	return t.finish();
}

} // namespace edit

} // namespace sme
