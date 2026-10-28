// animation_use.cpp - Which entities play which animation (see animation_use.h).

#include <sme/animation_use.h>

#include <sme/text.h>

namespace sme {

namespace {

/// The model path of an FGD studio("...") helper.
std::string studioHelperModel(const EntityClass& cls) {
	for (const char* helper : {"studio", "studioprop"}) {
		std::string argument = cls.helper(helper);
		if (argument.size() >= 2 && argument.front() == '"' && argument.back() == '"') argument = argument.substr(1, argument.size() - 2);
		if (endsWithI(argument, ".mdl")) return normalizePath(argument);
	}
	return {};
}

bool isScriptClass(std::string_view classname) {
	return startsWithI(classname, "scripted_") || startsWithI(classname, "aiscripted_");
}

// scripted_sequence spawnflags (HL2 FGD): No Interruptions, Override AI, Loop in Post Idle.
constexpr long long POSE_FLAGS = 32 | 64 | 256;

} // namespace

std::vector<SequenceKey> sequenceKeys(std::string_view classname) {
	if (iequals(classname, "scripted_sequence")) {
		return {{"m_iszIdle", "Pre Action Idle"}, {"m_iszEntry", "Entry"}, {"m_iszPlay", "Action"},
		        {"m_iszPostIdle", "Post Action Idle"}, {"m_iszCustomMove", "Custom Move"}};
	}
	if (startsWithI(classname, "prop_dynamic") || startsWithI(classname, "prop_physics") || iequals(classname, "prop_ragdoll") ||
	    startsWithI(classname, "prop_door") || iequals(classname, "cycler") || startsWithI(classname, "generic_actor")) {
		return {{"DefaultAnim", "Default animation"}};
	}
	return {};
}

bool isNpcClass(std::string_view classname) {
	return startsWithI(classname, "npc_") || startsWithI(classname, "monster_");
}

std::string animatedModel(const MapDocument& doc, const Entity& entity, const EntityCatalog* catalog) {
	if (const auto model = normalizePath(entity.get("model")); endsWithI(model, ".mdl")) return model;
	if (isScriptClass(entity.classname())) {
		for (const char* key : {"m_iszEntity", "target"}) {
			const auto target = entity.get(key);
			if (target.empty()) continue;
			for (const auto& other : doc.entities) {
				if (other.id != entity.id && !isScriptClass(other.classname()) && iequals(other.get("targetname"), target)) {
					return animatedModel(doc, other, catalog);
				}
			}
		}
		return {};
	}
	if (catalog) {
		if (const auto* cls = catalog->find(entity.classname())) return studioHelperModel(*cls);
	}
	return {};
}

std::vector<SequenceUse> sequenceUses(const MapDocument& doc, const EntityCatalog* catalog, std::string_view model) {
	std::vector<SequenceUse> out;
	const std::string wanted = normalizePath(model);
	for (const auto& entity : doc.entities) {
		const auto keys = sequenceKeys(entity.classname());
		if (keys.empty()) continue;
		if (!iequals(animatedModel(doc, entity, catalog), wanted)) continue;
		int actor = entity.id;
		if (isScriptClass(entity.classname())) {
			const auto target = entity.get("m_iszEntity");
			for (const auto& other : doc.entities) {
				if (!target.empty() && iequals(other.get("targetname"), target)) {
					actor = other.id;
					break;
				}
			}
		}
		for (const auto& key : keys) {
			const std::string raw = entity.get(key.key);
			const std::string value(trim(raw));
			if (!value.empty()) out.push_back({entity.id, actor, key.key, key.label, value});
		}
	}
	return out;
}

namespace edit {

Change useSequence(MapDocument& doc, int entityId, std::string_view sequence, std::string_view key) {
	const Entity* entity = doc.findEntity(entityId);
	if (!entity || entityId == 0 || trim(sequence).empty()) return {};
	const std::string name(trim(sequence));
	const auto keys = sequenceKeys(entity->classname());
	if (!keys.empty()) {
		std::string target = keys.front().key;
		for (const auto& k : keys) {
			if (!key.empty() && iequals(k.key, key)) target = k.key;
		}
		Transaction t(doc, "Use animation");
		t.touchEntity(entityId);
		doc.findEntity(entityId)->set(target, name);
		return t.finish();
	}
	if (!isNpcClass(entity->classname())) return {};
	// An NPC: a scripted_sequence at its feet keeps it in the animation (its pose helper).
	Transaction t(doc, "Use animation");
	t.touchEntity(entityId);
	Entity* npc = doc.findEntity(entityId);
	std::string npcName = npc->get("targetname");
	if (npcName.empty()) {
		npcName = npc->classname() + "_" + std::to_string(npc->id);
		npc->set("targetname", npcName);
	}
	const Vec3 origin = npc->origin();
	const std::string angles = npc->get("angles", "0 0 0");
	for (auto& other : doc.entities) {
		if (iequals(other.classname(), "scripted_sequence") && iequals(other.get("targetname"), npcName + "_pose")) {
			t.touchEntity(other.id);
			other.set("m_iszIdle", name);
			other.set("m_iszEntity", npcName);
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

} // namespace edit

} // namespace sme
