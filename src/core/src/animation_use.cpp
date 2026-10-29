// animation_use.cpp - Which entities play which animation (see animation_use.h).

#include <sme/animation_use.h>

#include <sme/filesystem.h>
#include <sme/npc_scripts.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>

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

std::vector<std::string> npcClassModels(const EntityCatalog& catalog) {
	std::vector<std::string> out;
	for (const auto* cls : catalog.placeable()) {
		if (!isNpcClass(cls->name)) continue;
		if (auto model = studioHelperModel(*cls); !model.empty()) {
			model = toLower(model);
			if (std::find(out.begin(), out.end(), model) == out.end()) out.push_back(std::move(model));
		}
	}
	return out;
}

bool isCharacterModel(std::string_view path, const std::vector<std::string>& npcModels) {
	const std::string lower = toLower(normalizePath(path));
	if (!lower.ends_with(".mdl")) return false;
	const std::string stem = lower.substr(lower.find_last_of('/') + 1, lower.size() - lower.find_last_of('/') - 5);
	for (const std::string_view suffix : {"_shared", "_gestures", "_postures", "_ss", "_anims", "_animations", "_gib", "gibs"}) {
		if (stem.ends_with(suffix)) return false;
	}
	if (std::find(npcModels.begin(), npcModels.end(), lower) != npcModels.end()) return true;
	// Folders of the path (after models/) decide first: props and the like never are characters.
	std::vector<std::string> folders;
	{
		std::string rest = lower.starts_with("models/") ? lower.substr(7) : lower;
		std::size_t at = 0;
		for (std::size_t slash; (slash = rest.find('/', at)) != std::string::npos; at = slash + 1) folders.push_back(rest.substr(at, slash - at));
	}
	for (const auto& folder : folders) {
		if (folder.starts_with("props") || folder == "weapons" || folder == "items" || folder == "gibs" || folder == "effects" ||
		    folder == "editor" || folder == "shadertest" || folder == "vehicles" || folder == "foliage" || folder == "cliffs" ||
		    folder == "nature" || folder == "maps" || folder == "skybox" || folder == "phxtended" || folder == "xqm") {
			return false;
		}
	}
	static constexpr std::string_view characterWords[] = {
		"humans", "human", "characters", "character", "player", "npc", "npcs", "monster", "monsters", "creatures", "zombie",
		"headcrab", "antlion", "combine_s", "combine_super", "police", "metropolice", "vortigaunt", "alyx", "barney", "breen",
		"kleiner", "eli", "mossman", "gman", "odessa", "monk", "citizen", "male_0", "female_0", "dog", "crow", "pigeon",
		"seagull", "stalker", "hunter", "strider", "gunship", "combine_dropship", "manhack", "advisor", "fassassin",
		"zombine", "soldier", "scientist", "survivor"};
	const auto isWord = [](const std::string& text) {
		for (const auto word : characterWords) {
			if (text == word || text.starts_with(word)) return true;
		}
		return false;
	};
	for (const auto& folder : folders) {
		if (isWord(folder)) return true;
	}
	return isWord(stem);
}

bool isAnimationOnlyModel(std::string_view path) {
	const std::string lower = toLower(normalizePath(path));
	if (!lower.ends_with(".mdl")) return false;
	const std::string stem = lower.substr(lower.find_last_of('/') + 1, lower.size() - lower.find_last_of('/') - 5);
	for (const std::string_view suffix : {"_shared", "_gestures", "_postures", "_ss", "_anims", "_animations"}) {
		if (stem.ends_with(suffix)) return true;
	}
	return false;
}

bool isAnimatedModel(const ModelInfo& info) {
	if (info.boneCount < 2) return false;   // a rigid prop: nothing in it can move
	if (!info.includeModels.empty()) return true;
	return std::any_of(info.sequences.begin(), info.sequences.end(), [](const std::string& sequence) {
		std::string name = toLower(sequence);
		while (!name.empty() && (std::isdigit(static_cast<unsigned char>(name.back())) || name.back() == '_')) name.pop_back();
		if (!name.empty() && name.front() == '@') name.erase(0, 1);
		return !(name.empty() || name == "idle" || name == "ref" || name == "reference" || name == "ragdoll" || name == "bindpose" ||
		         name == "bind_pose" || name == "a_ref" || name == "still");
	});
}

bool hasAnimations(const GameFileSystem& fs, std::string_view path) {
	if (isAnimationOnlyModel(path)) return false;
	const auto data = fs.read(normalizePath(path));
	if (!data) return false;
	const auto info = readModelInfo(*data);
	return info && isAnimatedModel(*info);
}

namespace {

/// The words of a name: runs of letters (digits and other characters split them).
std::vector<std::string> nameWords(std::string_view text) {
	std::vector<std::string> words;
	std::string word;
	for (const char c : text) {
		if (std::isalpha(static_cast<unsigned char>(c))) {
			word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		} else if (!word.empty()) {
			words.push_back(std::move(word));
			word.clear();
		}
	}
	if (!word.empty()) words.push_back(std::move(word));
	return words;
}

} // namespace

SequenceSituation classifySequence(std::string_view name, std::string_view activity, bool layer) {
	using namespace sequence_weapon;
	const std::string lowerName = toLower(name), lowerActivity = toLower(activity);
	const std::string both = lowerName + " " + lowerActivity;
	const auto has = [&both](std::string_view word) { return both.find(word) != std::string::npos; };
	SequenceSituation out;
	unsigned weapons = 0;
	if (has("pistol")) weapons |= PISTOL;
	if (has("smg")) weapons |= SMG1;
	if (has("ar2")) weapons |= AR2;
	if (has("rifle")) weapons |= SMG1 | AR2;
	if (has("shotgun")) weapons |= SHOTGUN;
	if (has("rpg")) weapons |= RPG;
	if (has("crossbow")) weapons |= CROSSBOW;
	if (has("melee") || has("stunstick") || has("crowbar")) weapons |= MELEE;
	if (has("grenade") || has("throw")) weapons |= GRENADE;
	if (weapons == 0 || has("unarmed")) weapons |= UNARMED;
	out.weapons = weapons;

	const auto nameWordsList = nameWords(lowerName);
	const auto activityWords = nameWords(lowerActivity);
	const auto anyWord = [](const std::vector<std::string>& words, auto&& test) { return std::any_of(words.begin(), words.end(), test); };
	out.crouch = has("crouch") || anyWord(activityWords, [](const std::string& w) { return w == "low"; });

	const auto startsWithAny = [](const std::string& word, std::initializer_list<std::string_view> prefixes) {
		return std::any_of(prefixes.begin(), prefixes.end(), [&word](std::string_view p) { return word.starts_with(p); });
	};
	// Map-named scenes: d1_t01_..., d3_c17_..., ep1_..., ep2_..., e3_...
	const std::string firstToken = lowerName.substr(0, lowerName.find('_'));
	const bool mapNamed = firstToken.size() < lowerName.size() && firstToken.size() >= 2 && firstToken.size() <= 3 &&
	                      (firstToken[0] == 'd' || firstToken.starts_with("ep") || firstToken == "e3") &&
	                      std::isdigit(static_cast<unsigned char>(firstToken.back())) &&
	                      std::all_of(firstToken.begin() + (firstToken.starts_with("ep") ? 2 : 1), firstToken.end(),
	                                  [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
	const bool noActivity = lowerActivity.empty() || lowerActivity == "act_invalid";
	if (layer || lowerActivity.starts_with("act_gesture") || lowerName.starts_with("g_") || has("gesture")) {
		out.kind = SequenceKind::Gesture;
	} else if (mapNamed) {
		out.kind = SequenceKind::Scene;
	} else if (anyWord(activityWords, [&](const std::string& w) { return startsWithAny(w, {"walk", "run", "sprint", "jog"}); }) ||
	           (noActivity && anyWord(nameWordsList, [&](const std::string& w) { return startsWithAny(w, {"walk", "run", "sprint", "jog"}); }))) {
		out.kind = SequenceKind::Move;
	} else if (anyWord(activityWords, [](const std::string& w) { return w.find("idle") != std::string::npos || w == "busy"; }) ||
	           (noActivity && anyWord(nameWordsList, [](const std::string& w) { return w.find("idle") != std::string::npos; }))) {
		out.kind = SequenceKind::Idle;
	} else if (noActivity) {
		out.kind = SequenceKind::Scene;
	}
	return out;
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

Change useSequence(MapDocument& doc, int entityId, std::string_view sequence, std::string_view key, std::string_view model) {
	const Entity* entity = doc.findEntity(entityId);
	if (!entity || entityId == 0 || trim(sequence).empty()) return {};
	const std::string name(trim(sequence));
	const std::string newModel = normalizePath(model);
	// The entity that moves takes the new model (a script's NPC, found by name).
	const auto switchModel = [&](Transaction& t, int actorId) {
		Entity* actor = doc.findEntity(actorId);
		if (!actor || newModel.empty() || iequals(normalizePath(actor->get("model")), newModel)) return;
		t.touchEntity(actorId);
		doc.findEntity(actorId)->set("model", newModel);
	};
	const auto keys = sequenceKeys(entity->classname());
	if (!keys.empty()) {
		std::string target = keys.front().key;
		for (const auto& k : keys) {
			if (!key.empty() && iequals(k.key, key)) target = k.key;
		}
		Transaction t(doc, "Use animation");
		t.touchEntity(entityId);
		doc.findEntity(entityId)->set(target, name);
		int actor = entityId;
		if (isScriptClass(entity->classname())) {
			actor = 0;
			const std::string npcName = doc.findEntity(entityId)->get("m_iszEntity");
			for (const auto& other : doc.entities) {
				if (!npcName.empty() && !isScriptClass(other.classname()) && iequals(other.get("targetname"), npcName)) actor = other.id;
			}
		}
		if (actor) switchModel(t, actor);
		return t.finish();
	}
	if (!isNpcClass(entity->classname())) return {};
	// An NPC: a scripted_sequence at its feet keeps it in the animation (its pose helper).
	if (newModel.empty()) return poseNpc(doc, entityId, name);
	Transaction t(doc, "Use animation");
	switchModel(t, entityId);
	Change modelChange = t.finish();
	Change pose = poseNpc(doc, entityId, name);
	// One undo step: the model switch keeps the NPC's oldest state, the pose adds the helper.
	for (auto& e : pose.entities) {
		auto it = std::find_if(modelChange.entities.begin(), modelChange.entities.end(), [&e](const Change::EntityState& o) { return o.entityId == e.entityId; });
		if (it != modelChange.entities.end()) {
			it->after = std::move(e.after);
		} else {
			modelChange.entities.push_back(std::move(e));
		}
	}
	return modelChange;
}

} // namespace edit

} // namespace sme
