// npcs.h - The NPCs of a Half-Life 2 / Mapbase mod for the NPCs page: which classes are real
// NPCs, their models, the weapons they can carry, their skill values and their sounds.
//
// How the game sets them up (Source SDK 2013 / Mapbase, game/server/hl2 and episodic):
// - Health and damage are console variables (ConVar "sk_<npc>_health", "sk_<npc>_dmg_*", ...),
//   set at every map start by cfg/skill_manifest.cfg -> cfg/skill.cfg (mod_base_files.h). The
//   mod's cfg/skill.cfg covers the game's (mod folder first); changes need a new map.
// - The model comes from the class (SetModel in Spawn: npc_combine_s -> models/combine_soldier.mdl);
//   many classes take a "model" keyvalue (Mapbase: all of CAI_BaseNPC), citizens pick a random
//   head unless citizentype is Unique (npc_scripts.h).
// - Weapons: the "additionalequipment" keyvalue (FGD choices); NPC damage per bullet is the ammo's
//   sk_npc_dmg_* value (hl2_gamerules.cpp GetAmmoDef), not the NPC's.
// - Sounds: sound script entries named after the class ("NPC_MetroPolice.Die", "NPC_Citizen.*",
//   "Zombie.Pain"), from scripts/npc_sounds_*.txt via scripts/game_sounds_manifest.txt. Speech
//   (citizens, soldiers) goes through sentences and response rules and is not listed here.
//
// Which classes count: the built-in table of Half-Life 2, Episode One/Two and Lost Coast NPCs
// (shown only when one of their models exists, so classes without art - bullsquid, houndeye,
// assassin - stay hidden), plus the mod's own NPC classes from the FGD that have a model.
// Helpers (npc_bullseye, npc_enemyfinder, makers, vehicle drivers ...) are never listed.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/assets.h>

namespace sme {

class EntityCatalog;

/// What Mapsmith knows about a Half-Life 2 NPC class (lists separated by ';').
struct NpcType {
	std::string_view className;      ///< "npc_combine_s"
	std::string_view name;           ///< "Combine Soldier"
	std::string_view group;          ///< "Combine", "Resistance", "Characters", "Creatures"
	std::string_view models;         ///< Default model first
	std::string_view weapons;        ///< Weapon classes it can carry, default first ("" = none)
	std::string_view soundPrefixes;  ///< Sound script name prefixes ("NPC_CombineS;NPC_Combine")
	std::string_view skillPrefixes;  ///< "combine_s" -> sk_combine_s_*
};
/// The built-in table.
[[nodiscard]] std::span<const NpcType> knownNpcTypes();
/// Helper and developer classes that are never shown (npc_bullseye, npc_maker ...).
[[nodiscard]] bool hiddenNpcClass(std::string_view className);

/// One NPC of the page.
struct NpcInfo {
	std::string className;
	std::string name;
	std::string group;
	std::vector<std::string> models;          ///< Existing models first (the shown one first)
	std::vector<std::string> weapons;         ///< Weapon classes it can carry
	std::vector<std::string> soundPrefixes;
	std::vector<std::string> skillPrefixes;
	bool known = false;                       ///< From the built-in table (else from the mod's FGD)
	bool takesModel = false;                  ///< The class has a "model" keyvalue
	[[nodiscard]] std::string model() const { return models.empty() ? std::string() : models.front(); }
};

/// The NPCs to list (see the file comment), grouped then by name. `modelExists("models/x.mdl")`
/// tells which models the game has; `catalog` (may be null) adds the mod's classes and weapons.
[[nodiscard]] std::vector<NpcInfo> findNpcs(const EntityCatalog* catalog, const std::function<bool(std::string_view)>& modelExists);

/// A skill value of an NPC.
struct NpcSkillValue {
	std::string name;    ///< "sk_combine_s_health"
	std::string label;   ///< "Health"
	std::string help;
};
/// The NPC's skill values: <prefix>_health always, then every sk_<prefix>_* of `cfgValues` (the
/// game's skill.cfg), health first.
[[nodiscard]] std::vector<NpcSkillValue> npcSkillValues(const NpcInfo& npc, const std::map<std::string, std::string>& cfgValues);
/// The damage value of a weapon when NPCs use it ("weapon_smg1" -> "sk_npc_dmg_smg1"; empty if
/// the weapon has none, as the grenade launchers of NPCs).
[[nodiscard]] std::string npcWeaponDamageValue(std::string_view weaponClass);
/// The values of `names` as `originalCfg` (the game's skill.cfg) has them, to put back.
[[nodiscard]] std::map<std::string, std::string> skillValuesToRestore(const std::vector<std::string>& names, std::string_view originalCfg);

/// The sound script entries of the NPC (name starts with a prefix and "."), sorted by name.
[[nodiscard]] std::vector<const SoundScriptEntry*> npcSounds(const NpcInfo& npc, const std::vector<SoundScriptEntry>& scripts);

/// The keyvalues of an NPC placed with `weapon` (may be empty) and `model` (empty: its default):
/// additionalequipment, model, and for citizens citizentype Unique so the head stays.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> npcPlacementKeys(const NpcInfo& npc, std::string_view weapon, std::string_view model);

/// A weapon's name for people: Half-Life 2's ("weapon_ar2" -> "Pulse rifle (AR2)"), else prettyClassName.
[[nodiscard]] std::string weaponLabel(std::string_view weaponClass);

/// A class name for people: "npc_combine_s" -> "Combine S", "weapon_smg1" -> "Smg1".
[[nodiscard]] std::string prettyClassName(std::string_view className);

} // namespace sme
