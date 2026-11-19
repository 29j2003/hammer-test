// test_npcs.cpp - The NPCs page's core: which classes are listed (only NPCs with art, no helpers,
// no duplicates, the mod's own FGD classes), skill values per NPC, sounds by prefix, placement keys.

#include "fixture.h"

#include <sme/entity_catalog.h>
#include <sme/npcs.h>
#include <sme/text.h>
#include <sme/weapons.h>

#include <doctest/doctest.h>

#include <fstream>
#include <set>

using namespace sme;
namespace fs = std::filesystem;

namespace {

EntityCatalog catalogFrom(const fs::path& dir, const std::string& text) {
	fs::create_directories(dir);
	const auto file = dir / "test.fgd";
	std::ofstream(file) << text;
	EntityCatalog catalog;
	REQUIRE(catalog.load({file}));
	return catalog;
}

} // namespace

TEST_CASE("npcs: only NPCs the game has a model for, no helpers, no duplicates, the mod's own classes") {
	test::Fixture f;
	test::createFixture(f);
	const auto catalog = catalogFrom(f.root / "npcs", R"(
@BaseClass = BaseNPC [ model(studio) : "World model" : "" ]
@NPCClass base(BaseNPC) studio("models/humans/group01/male_07.mdl") = npc_citizen : "Citizen"
[
	additionalequipment(choices) : "Weapons" : "0" = [ "weapon_pistol" : "Pistol" "weapon_smg1" : "SMG" "0" : "Nothing" ]
]
@NPCClass base(BaseNPC) = npc_combine_s : "Soldier" []
@NPCClass base(BaseNPC) = npc_bullseye : "Bullseye" []
@NPCClass base(BaseNPC) studio("models/bullsquid.mdl") = npc_bullsquid : "Bullsquid" []
@NPCClass base(BaseNPC) studio("models/mymod/robot.mdl") = npc_robot : "A robot of the mod" []
@NPCClass base(BaseNPC) studio("models/missing.mdl") = npc_ghost : "No art" []
@PointClass = npc_maker : "Maker" []
)");
	const std::set<std::string> models = {"models/humans/group01/male_07.mdl", "models/humans/group02/female_01.mdl", "models/combine_soldier.mdl",
	                                      "models/mymod/robot.mdl", "models/police.mdl"};
	const auto npcs = findNpcs(&catalog, [&](std::string_view m) { return models.count(toLower(m)) > 0; });
	std::vector<std::string> classes;
	for (const auto& npc : npcs) classes.push_back(npc.className);
	// Citizen (head present), soldier and police (model present, police even without an FGD class),
	// the mod's robot. Not: bullseye/maker (helpers), bullsquid and ghost (no model), zombies (no art).
	CHECK(classes == std::vector<std::string>{"npc_citizen", "npc_combine_s", "npc_metropolice", "npc_robot"});
	const auto& citizen = npcs[0];
	CHECK(citizen.name == "Citizen");
	CHECK(citizen.model() == "models/humans/group01/male_07.mdl");
	CHECK(citizen.models.size() == 2);   // only the heads the game has
	CHECK(citizen.weapons == std::vector<std::string>{"weapon_pistol", "weapon_smg1"});   // the FGD's choices win
	CHECK(npcs[1].weapons.front() == "weapon_smg1");   // no FGD choices: the table
	CHECK(npcs[3].group == "Mod");
	CHECK(npcs[3].name == "Robot");
	CHECK(npcs[3].soundPrefixes == std::vector<std::string>{"NPC_Robot"});
	CHECK(hiddenNpcClass("npc_bullseye"));
	CHECK_FALSE(hiddenNpcClass("npc_zombie"));

	// Without a catalog the table alone counts.
	const auto plain = findNpcs(nullptr, [&](std::string_view m) { return models.count(toLower(m)) > 0; });
	CHECK(plain.size() == 3);
	// Every known class once.
	std::set<std::string> seen;
	for (const auto& type : knownNpcTypes()) CHECK(seen.insert(std::string(type.className)).second);
}

TEST_CASE("npcs: skill values per NPC, another NPC's longer prefix left out, restore values") {
	NpcInfo zombie;
	zombie.className = "npc_zombie";
	zombie.skillPrefixes = {"zombie"};
	const std::map<std::string, std::string> cfg = {{"sk_zombie_health", "50"}, {"sk_zombie_dmg_one_slash", "10"}, {"sk_zombie_dmg_both_slash", "25"},
	                                                {"sk_zombie_fast_health", "50"}, {"sk_zombie_poison_health", "175"}, {"sk_citizen_health", "40"}};
	const auto values = npcSkillValues(zombie, cfg);
	REQUIRE(values.size() == 3);
	CHECK(values[0].name == "sk_zombie_health");
	CHECK(values[0].label == "Health");
	std::set<std::string> names;
	for (const auto& v : values) names.insert(v.name);
	CHECK(names.count("sk_zombie_dmg_one_slash"));
	CHECK_FALSE(names.count("sk_zombie_fast_health"));   // the fast zombie's
	for (const auto& v : values) {
		if (v.name == "sk_zombie_dmg_both_slash") CHECK(v.label == "Damage both slash");
	}
	// Health is listed even when the cfg has no line for it.
	NpcInfo turret;
	turret.skillPrefixes = {"turret"};
	CHECK(npcSkillValues(turret, {}).front().name == "sk_turret_health");

	CHECK(npcWeaponDamageValue("weapon_smg1") == "sk_npc_dmg_smg1");
	CHECK(npcWeaponDamageValue("WEAPON_SHOTGUN") == "sk_npc_dmg_buckshot");
	CHECK(npcWeaponDamageValue("weapon_frag").empty());
	const auto back = skillValuesToRestore({"sk_zombie_health", "sk_unknown"}, "sk_zombie_health \"50\"\nsk_other \"1\"\n");
	CHECK(back == std::map<std::string, std::string>{{"sk_zombie_health", "50"}});
}

TEST_CASE("npcs: sounds by prefix and placement keys") {
	NpcInfo police;
	police.className = "npc_metropolice";
	police.models = {"models/police.mdl"};
	police.soundPrefixes = {"NPC_MetroPolice"};
	std::vector<SoundScriptEntry> scripts(4);
	scripts[0].name = "NPC_MetroPolice.Die";
	scripts[1].name = "npc_metropolice.FootstepLeft";
	scripts[2].name = "NPC_MetroPoliceX.Other";   // another prefix
	scripts[3].name = "Weapon_Pistol.Single";
	const auto sounds = npcSounds(police, scripts);
	REQUIRE(sounds.size() == 2);
	CHECK(sounds[0]->name == "NPC_MetroPolice.Die");

	using Keys = std::vector<std::pair<std::string, std::string>>;
	CHECK(npcPlacementKeys(police, "weapon_stunstick", "") == Keys{{"additionalequipment", "weapon_stunstick"}});
	CHECK(npcPlacementKeys(police, "", "models/police.mdl").empty());   // its own model: nothing to set
	CHECK(npcPlacementKeys(police, "", "models/mymod/police2.mdl") == Keys{{"model", "models/mymod/police2.mdl"}});
	CHECK(npcPlacementKeys(police, "", "../outside.mdl").empty());
	NpcInfo citizen;
	citizen.className = "npc_citizen";
	citizen.models = {"models/humans/group01/male_07.mdl"};
	CHECK(npcPlacementKeys(citizen, "weapon_smg1", "models/humans/group01/male_07.mdl") ==
	      Keys{{"additionalequipment", "weapon_smg1"}, {"citizentype", "4"}, {"model", "models/humans/group01/male_07.mdl"}});
	CHECK(prettyClassName("npc_combine_s") == "Combine S");
	CHECK(prettyClassName("weapon_smg1") == "Smg1");
}
