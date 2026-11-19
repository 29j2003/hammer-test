// npcs.cpp - NPC catalogue, skill values, sounds and placement keys (see npcs.h).

#include <sme/npcs.h>

#include <sme/entity_catalog.h>
#include <sme/filesystem.h>
#include <sme/npc_scripts.h>
#include <sme/text.h>
#include <sme/weapons.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace sme {

namespace {

// Half-Life 2, Episode One/Two and Lost Coast (models and prefixes from game/server/hl2 and
// episodic: SetModel/PrecacheModel, EmitSound names, ConVar "sk_..." names).
constexpr NpcType KNOWN[] = {
	// Resistance and characters.
	{"npc_citizen", "Citizen", "Resistance", "", "weapon_pistol;weapon_smg1;weapon_ar2;weapon_shotgun;weapon_rpg;weapon_crowbar", "NPC_Citizen", "citizen"},
	{"npc_barney", "Barney", "Resistance", "models/barney.mdl", "weapon_pistol;weapon_smg1;weapon_ar2;weapon_shotgun", "NPC_Barney", "barney"},
	{"npc_alyx", "Alyx", "Resistance", "models/alyx.mdl", "weapon_alyxgun;weapon_shotgun;weapon_smg1", "NPC_Alyx;npc_alyx", "alyx"},
	{"npc_vortigaunt", "Vortigaunt", "Resistance", "models/vortigaunt.mdl", "", "NPC_Vortigaunt", "vortigaunt"},
	{"npc_dog", "Dog", "Resistance", "models/dog.mdl", "", "NPC_dog", ""},
	{"npc_monk", "Father Grigori", "Resistance", "models/monk.mdl", "weapon_annabelle", "NPC_Monk", ""},
	{"npc_fisherman", "Fisherman", "Resistance", "models/lostcoast/fisherman/fisherman.mdl", "", "NPC_Fisherman", ""},
	{"npc_kleiner", "Dr. Kleiner", "Characters", "models/kleiner.mdl", "", "", ""},
	{"npc_eli", "Eli Vance", "Characters", "models/eli.mdl", "", "", ""},
	{"npc_mossman", "Dr. Mossman", "Characters", "models/mossman.mdl", "", "", ""},
	{"npc_magnusson", "Dr. Magnusson", "Characters", "models/magnusson.mdl", "", "", ""},
	{"npc_breen", "Dr. Breen", "Characters", "models/breen.mdl", "", "", ""},
	{"npc_gman", "G-Man", "Characters", "models/gman.mdl", "", "", ""},
	// Combine.
	{"npc_combine_s", "Combine Soldier", "Combine", "models/combine_soldier.mdl;models/combine_super_soldier.mdl;models/combine_soldier_prisonguard.mdl",
	 "weapon_smg1;weapon_ar2;weapon_shotgun", "NPC_CombineS;NPC_Combine", "combine_s;combine_guard"},
	{"npc_metropolice", "Metro Police", "Combine", "models/police.mdl", "weapon_pistol;weapon_smg1;weapon_stunstick", "NPC_MetroPolice", "metropolice"},
	{"npc_hunter", "Hunter", "Combine", "models/hunter.mdl", "", "NPC_Hunter", "hunter"},
	{"npc_strider", "Strider", "Combine", "models/combine_strider.mdl", "", "NPC_Strider", "strider"},
	{"npc_combinegunship", "Gunship", "Combine", "models/gunship.mdl", "", "NPC_CombineGunship", "gunship"},
	{"npc_helicopter", "Hunter-Chopper", "Combine", "models/combine_helicopter.mdl", "", "NPC_AttackHelicopter", "helicopter"},
	{"npc_combinedropship", "Dropship", "Combine", "models/combine_dropship.mdl", "", "NPC_CombineDropship", "dropship"},
	{"npc_manhack", "Manhack", "Combine", "models/manhack.mdl", "", "NPC_Manhack", "manhack"},
	{"npc_cscanner", "City Scanner", "Combine", "models/combine_scanner.mdl", "", "NPC_CScanner", "scanner"},
	{"npc_clawscanner", "Shield Scanner", "Combine", "models/shield_scanner.mdl", "", "NPC_SScanner", "scanner"},
	{"npc_rollermine", "Rollermine", "Combine", "models/roller.mdl", "", "NPC_RollerMine", "rollermine"},
	{"npc_turret_floor", "Floor Turret", "Combine", "models/combine_turrets/floor_turret.mdl", "", "NPC_FloorTurret", "turret"},
	{"npc_turret_ceiling", "Ceiling Turret", "Combine", "models/combine_turrets/ceiling_turret.mdl", "", "NPC_CeilingTurret", "turret"},
	{"npc_turret_ground", "Ground Turret", "Combine", "models/combine_turrets/ground_turret.mdl", "", "NPC_FloorTurret", "turret"},
	{"npc_combine_camera", "Combine Camera", "Combine", "models/combine_camera/combine_camera.mdl", "", "NPC_CombineCamera", ""},
	{"npc_stalker", "Stalker", "Combine", "models/stalker.mdl", "", "NPC_Stalker", "stalker"},
	{"npc_advisor", "Advisor", "Combine", "models/advisor.mdl", "", "NPC_Advisor", "advisor"},
	// Creatures.
	{"npc_zombie", "Zombie", "Creatures", "models/zombie/classic.mdl", "", "Zombie;NPC_BaseZombie", "zombie"},
	{"npc_zombie_torso", "Zombie Torso", "Creatures", "models/zombie/classic_torso.mdl", "", "Zombie;NPC_BaseZombie", "zombie"},
	{"npc_fastzombie", "Fast Zombie", "Creatures", "models/zombie/fast.mdl", "", "NPC_FastZombie", "zombie_fast"},
	{"npc_fastzombie_torso", "Fast Zombie Torso", "Creatures", "models/zombie/fast_torso.mdl", "", "NPC_FastZombie", "zombie_fast"},
	{"npc_poisonzombie", "Poison Zombie", "Creatures", "models/zombie/poison.mdl", "", "NPC_PoisonZombie", "zombie_poison"},
	{"npc_zombine", "Zombine", "Creatures", "models/zombie/zombie_soldier.mdl", "", "Zombine;NPC_BaseZombie", "zombie_soldier"},
	{"npc_headcrab", "Headcrab", "Creatures", "models/headcrabclassic.mdl", "", "NPC_HeadCrab", "headcrab"},
	{"npc_headcrab_fast", "Fast Headcrab", "Creatures", "models/headcrab.mdl", "", "NPC_FastHeadcrab", "headcrab_fast"},
	{"npc_headcrab_black", "Poison Headcrab", "Creatures", "models/headcrabblack.mdl", "", "NPC_BlackHeadcrab", "headcrab_poison"},
	{"npc_antlion", "Antlion", "Creatures", "models/antlion.mdl;models/antlion_worker.mdl", "", "NPC_Antlion", "antlion;antlion_worker"},
	{"npc_antlionguard", "Antlion Guard", "Creatures", "models/antlion_guard.mdl", "", "NPC_AntlionGuard", "antlionguard"},
	{"npc_antlion_grub", "Antlion Grub", "Creatures", "models/antlion_grub.mdl", "", "NPC_Antlion_Grub", ""},
	{"npc_barnacle", "Barnacle", "Creatures", "models/barnacle.mdl", "", "NPC_Barnacle", "barnacle"},
	{"npc_ichthyosaur", "Ichthyosaur", "Creatures", "models/ichthyosaur.mdl", "", "NPC_Ichthyosaur", "ichthyosaur"},
	{"npc_crow", "Crow", "Creatures", "models/crow.mdl", "", "NPC_Crow", "crow"},
	{"npc_pigeon", "Pigeon", "Creatures", "models/pigeon.mdl", "", "NPC_Pigeon", "crow"},
	{"npc_seagull", "Seagull", "Creatures", "models/seagull.mdl", "", "NPC_Seagull", "crow"},
	// Code in Mapbase, art only in some mods (shown when the model is there).
	{"npc_bullsquid", "Bullsquid", "Creatures", "models/bullsquid.mdl", "", "NPC_Bullsquid", "bullsquid"},
	{"npc_houndeye", "Houndeye", "Creatures", "models/houndeye.mdl", "", "NPC_Houndeye", "houndeye"},
	{"npc_assassin", "Assassin", "Combine", "models/fassassin.mdl", "", "NPC_Assassin", "assassin"},
	{"npc_hydra", "Hydra", "Creatures", "models/hydra.mdl", "", "NPC_Hydra", ""},
};

// Never listed: not a character the mapper sees (helpers, spawners, drivers, effects) or no model.
constexpr std::string_view HIDDEN[] = {
	"npc_bullseye", "npc_enemyfinder", "npc_enemyfinder_combinecannon", "npc_maker", "npc_template_maker", "npc_antlion_template_maker",
	"npc_heli_avoidsphere", "npc_heli_avoidbox", "npc_heli_nobomb", "npc_helicoptersensor", "npc_launcher", "npc_missiledefense",
	"npc_apcdriver", "npc_cranedriver", "npc_vehicledriver", "npc_puppet", "npc_furniture", "npc_barnacle_tongue_tip",
	"npc_grenade_frag", "npc_grenade_bugbait", "npc_satchel", "npc_tripmine", "npc_concussiongrenade", "npc_contactgrenade",
	"npc_handgrenade", "npc_sniper", "npc_combine_cannon", "npc_spotlight", "npc_hunter_maker", "npc_blob",
};

std::vector<std::string> list(std::string_view text) {
	std::vector<std::string> out;
	for (auto& part : splitTrimmed(text, ';')) {
		if (!part.empty()) out.push_back(std::move(part));
	}
	return out;
}

/// The studio("...") helper or the "model" property's default of a class.
std::string classModel(const EntityClass& cls) {
	std::string model = std::string(trim(cls.helper("studio")));
	if (!model.empty() && model.front() == '"') model.erase(0, 1);
	if (!model.empty() && model.back() == '"') model.pop_back();
	if (model.empty()) {
		if (const auto* p = cls.property("model")) model = p->defaultValue;
	}
	model = normalizePath(model);
	return endsWithI(model, ".mdl") && isSafeGamePath(model) ? model : std::string();
}

/// Weapon classes from the class's "additionalequipment" choices (none, "0" left out).
std::vector<std::string> classWeapons(const EntityClass& cls) {
	std::vector<std::string> out;
	if (const auto* p = cls.property("additionalequipment")) {
		for (const auto& choice : p->choices) {
			const std::string value = toLower(trim(choice.value));
			if (startsWithI(value, "weapon_") && std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
		}
	}
	return out;
}

bool isNpcClass(const EntityClass& cls) {
	if (cls.isBase() || cls.isSolid()) return false;
	if (iequals(cls.classType, "NPCClass")) return true;
	return startsWithI(cls.name, "npc_") || startsWithI(cls.name, "monster_");
}

std::string skillLabel(std::string_view suffix) {
	if (iequals(suffix, "health")) return "Health";
	if (iequals(suffix, "kick")) return "Kick damage";
	std::string words;
	for (const char c : suffix) words += c == '_' ? ' ' : c;
	std::string out;
	for (const auto& word : splitTrimmed(words, ' ')) {
		if (word.empty()) continue;
		std::string w = iequals(word, "dmg") ? std::string("damage") : word;
		if (!out.empty()) out += ' ';
		out += w;
	}
	if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
	return out;
}

} // namespace

std::span<const NpcType> knownNpcTypes() {
	return KNOWN;
}

bool hiddenNpcClass(std::string_view className) {
	return std::any_of(std::begin(HIDDEN), std::end(HIDDEN), [&](std::string_view h) { return iequals(h, className); });
}

std::string prettyClassName(std::string_view className) {
	std::string_view name = className;
	for (const std::string_view prefix : {"npc_", "weapon_", "monster_"}) {
		if (startsWithI(name, prefix)) {
			name.remove_prefix(prefix.size());
			break;
		}
	}
	std::string out;
	bool upper = true;
	for (const char c : name) {
		if (c == '_') {
			out += ' ';
			upper = true;
			continue;
		}
		out += upper ? char(std::toupper(static_cast<unsigned char>(c))) : c;
		upper = false;
	}
	return out.empty() ? std::string(className) : out;
}

std::string weaponLabel(std::string_view weaponClass) {
	static constexpr std::pair<std::string_view, std::string_view> NAMES[] = {
		{"weapon_pistol", "Pistol"}, {"weapon_smg1", "SMG"}, {"weapon_ar2", "Pulse rifle (AR2)"}, {"weapon_shotgun", "Shotgun"},
		{"weapon_357", ".357 Magnum"}, {"weapon_crossbow", "Crossbow"}, {"weapon_rpg", "RPG"}, {"weapon_crowbar", "Crowbar"},
		{"weapon_stunstick", "Stun stick"}, {"weapon_alyxgun", "Alyx's gun"}, {"weapon_annabelle", "Annabelle"}, {"weapon_frag", "Grenade"},
	};
	for (const auto& [cls, name] : NAMES) {
		if (iequals(cls, weaponClass)) return std::string(name);
	}
	return prettyClassName(weaponClass);
}

std::vector<NpcInfo> findNpcs(const EntityCatalog* catalog, const std::function<bool(std::string_view)>& modelExists) {
	const auto exists = [&](const std::string& model) { return !model.empty() && modelExists && modelExists(model); };
	std::vector<NpcInfo> out;
	std::set<std::string> seen;
	for (const auto& type : KNOWN) {
		if (hiddenNpcClass(type.className)) continue;
		NpcInfo npc;
		npc.className = std::string(type.className);
		npc.name = std::string(type.name);
		npc.group = std::string(type.group);
		npc.known = true;
		std::vector<std::string> models = iequals(type.className, "npc_citizen") ? citizenModels() : list(type.models);
		const EntityClass* cls = catalog ? catalog->find(type.className) : nullptr;
		if (cls) {
			// The mod's FGD may show another model (its own art for the class) - it comes first.
			if (const auto own = classModel(*cls); !own.empty() && std::none_of(models.begin(), models.end(), [&](const auto& m) { return iequals(m, own); })) {
				models.insert(models.begin(), own);
			}
			npc.takesModel = cls->property("model") != nullptr;
		}
		std::vector<std::string> present;
		for (auto& model : models) {
			model = normalizePath(model);
			if (exists(model)) present.push_back(model);
		}
		// Only NPCs the game has art for: a class without a model is code nobody can see.
		if (present.empty()) continue;
		npc.models = std::move(present);
		npc.weapons = cls ? classWeapons(*cls) : std::vector<std::string>{};
		if (npc.weapons.empty()) npc.weapons = list(type.weapons);
		npc.soundPrefixes = list(type.soundPrefixes);
		npc.skillPrefixes = list(type.skillPrefixes);
		seen.insert(toLower(npc.className));
		out.push_back(std::move(npc));
	}
	// The mod's own NPC classes (FGD) with a model.
	if (catalog) {
		for (const auto* cls : catalog->placeable()) {
			if (!isNpcClass(*cls) || hiddenNpcClass(cls->name) || seen.count(toLower(cls->name))) continue;
			const std::string model = classModel(*cls);
			if (!exists(model)) continue;
			NpcInfo npc;
			npc.className = toLower(cls->name);
			npc.name = prettyClassName(cls->name);
			npc.group = "Mod";
			npc.models = {model};
			npc.weapons = classWeapons(*cls);
			npc.takesModel = cls->property("model") != nullptr;
			// Mod classes usually follow the game's naming: NPC_<Name>.* sounds, sk_<name>_* values.
			std::string bare = toLower(cls->name);
			for (const std::string_view prefix : {"npc_", "monster_"}) {
				if (startsWithI(bare, prefix)) bare.erase(0, prefix.size());
			}
			std::string sound = "NPC_" + prettyClassName(cls->name);
			sound.erase(std::remove(sound.begin(), sound.end(), ' '), sound.end());
			npc.soundPrefixes = {sound};
			npc.skillPrefixes = {bare};
			seen.insert(npc.className);
			out.push_back(std::move(npc));
		}
	}
	const auto groupOrder = [](const std::string& group) {
		static constexpr std::array<std::string_view, 5> ORDER = {"Resistance", "Characters", "Combine", "Creatures", "Mod"};
		const auto it = std::find(ORDER.begin(), ORDER.end(), group);
		return int(it - ORDER.begin());
	};
	std::stable_sort(out.begin(), out.end(), [&](const NpcInfo& a, const NpcInfo& b) {
		const int ga = groupOrder(a.group), gb = groupOrder(b.group);
		if (ga != gb) return ga < gb;
		return toLower(a.name) < toLower(b.name);
	});
	return out;
}

std::vector<NpcSkillValue> npcSkillValues(const NpcInfo& npc, const std::map<std::string, std::string>& cfgValues) {
	std::vector<NpcSkillValue> out;
	std::set<std::string> seen;
	const auto add = [&](const std::string& name, std::string_view suffix) {
		if (!seen.insert(name).second) return;
		NpcSkillValue value;
		value.name = name;
		value.label = skillLabel(suffix);
		value.help = name + " - a console variable the mod's cfg/skill.cfg sets at every map start";
		out.push_back(std::move(value));
	};
	for (const auto& prefix : npc.skillPrefixes) {
		const std::string base = "sk_" + toLower(prefix) + "_";
		// Several prefixes (combine_s, combine_guard): the second one's health says whose it is.
		add(base + "health", npc.skillPrefixes.size() > 1 && &prefix != &npc.skillPrefixes.front() ? prettyClassName(prefix) + " health" : "health");
		for (const auto& [name, value] : cfgValues) {
			if (!startsWithI(name, base) || name.size() <= base.size()) continue;
			// sk_zombie_ must not take sk_zombie_fast_* (another NPC's prefix).
			const std::string suffix = name.substr(base.size());
			const bool other = std::any_of(knownNpcTypes().begin(), knownNpcTypes().end(), [&](const NpcType& type) {
				for (const auto& p : list(type.skillPrefixes)) {
					if (!iequals(p, prefix) && startsWithI(name, "sk_" + toLower(p) + "_") && toLower(p).size() > toLower(prefix).size()) return true;
				}
				return false;
			});
			if (!other) add(toLower(name), suffix);
		}
	}
	// Health first, then as found.
	std::stable_partition(out.begin(), out.end(), [](const NpcSkillValue& v) { return endsWithI(v.name, "_health"); });
	return out;
}

std::string npcWeaponDamageValue(std::string_view weaponClass) {
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 11> AMMO = {{
		{"weapon_pistol", "sk_npc_dmg_pistol"}, {"weapon_smg1", "sk_npc_dmg_smg1"}, {"weapon_ar2", "sk_npc_dmg_ar2"},
		{"weapon_shotgun", "sk_npc_dmg_buckshot"}, {"weapon_357", "sk_npc_dmg_357"}, {"weapon_crossbow", "sk_npc_dmg_crossbow"},
		{"weapon_rpg", "sk_npc_dmg_rpg_round"}, {"weapon_crowbar", "sk_npc_dmg_crowbar"}, {"weapon_stunstick", "sk_npc_dmg_stunstick"},
		{"weapon_alyxgun", "sk_npc_dmg_alyxgun"}, {"weapon_annabelle", "sk_npc_dmg_357"},
	}};
	for (const auto& [weapon, value] : AMMO) {
		if (iequals(weapon, weaponClass)) return std::string(value);
	}
	return {};
}

std::map<std::string, std::string> skillValuesToRestore(const std::vector<std::string>& names, std::string_view originalCfg) {
	const auto values = readSkillValues(originalCfg);
	std::map<std::string, std::string> out;
	for (const auto& name : names) {
		if (const auto it = values.find(toLower(name)); it != values.end()) out[toLower(name)] = it->second;
	}
	return out;
}

std::vector<const SoundScriptEntry*> npcSounds(const NpcInfo& npc, const std::vector<SoundScriptEntry>& scripts) {
	std::vector<const SoundScriptEntry*> out;
	for (const auto& entry : scripts) {
		for (const auto& prefix : npc.soundPrefixes) {
			if (entry.name.size() > prefix.size() && startsWithI(entry.name, prefix) && entry.name[prefix.size()] == '.') {
				out.push_back(&entry);
				break;
			}
		}
	}
	std::sort(out.begin(), out.end(), [](const SoundScriptEntry* a, const SoundScriptEntry* b) { return toLower(a->name) < toLower(b->name); });
	return out;
}

std::vector<std::pair<std::string, std::string>> npcPlacementKeys(const NpcInfo& npc, std::string_view weapon, std::string_view model) {
	std::vector<std::pair<std::string, std::string>> keys;
	if (!weapon.empty()) keys.emplace_back("additionalequipment", toLower(weapon));
	const std::string chosen = normalizePath(model);
	if (!chosen.empty() && isSafeGamePath(chosen) && endsWithI(chosen, ".mdl")) {
		if (iequals(npc.className, "npc_citizen")) {
			keys.emplace_back("citizentype", "4");   // Unique: the chosen head stays (npc_scripts.h)
			keys.emplace_back("model", chosen);
		} else if (!iequals(chosen, npc.model())) {
			keys.emplace_back("model", chosen);   // Mapbase: every NPC takes a "model" keyvalue
		}
	}
	return keys;
}

} // namespace sme
