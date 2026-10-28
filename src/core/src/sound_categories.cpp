// sound_categories.cpp - Sorting sounds into categories (see sound_categories.h).

#include <sme/sound_categories.h>

#include <sme/text.h>

#include <initializer_list>

namespace sme {

namespace {

bool startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes) {
	for (const auto prefix : prefixes) {
		if (text.substr(0, prefix.size()) == prefix) return true;
	}
	return false;
}

bool containsAny(std::string_view text, std::initializer_list<std::string_view> words) {
	for (const auto word : words) {
		if (text.find(word) != std::string_view::npos) return true;
	}
	return false;
}

/// Lower case, forward slashes, without the sound characters of scripts (")^*#@<>!?$&~").
std::string cleanPath(std::string_view path) {
	std::string out = toLower(path);
	for (auto& c : out) {
		if (c == '\\') c = '/';
	}
	const auto start = out.find_first_not_of(")^*#@<>!?$&~");
	return start == std::string::npos ? std::string() : out.substr(start);
}

} // namespace

std::string_view soundCategoryName(SoundCategory category) {
	switch (category) {
	case SoundCategory::Weapons: return "Weapons";
	case SoundCategory::Npcs: return "NPCs and creatures";
	case SoundCategory::Ambient: return "Ambient";
	case SoundCategory::Physics: return "Physics and impacts";
	case SoundCategory::Player: return "Player";
	case SoundCategory::Vehicles: return "Vehicles";
	case SoundCategory::Doors: return "Doors and buttons";
	case SoundCategory::Music: return "Music";
	case SoundCategory::Interface: return "Interface";
	case SoundCategory::Voice: return "Voice and dialogue";
	case SoundCategory::Other: return "Other";
	}
	return "Other";
}

SoundCategory categorizeSoundFile(std::string_view path) {
	const std::string p = cleanPath(path);
	const std::string top = p.substr(0, p.find('/'));
	const bool inFolder = p.find('/') != std::string::npos;
	if (inFolder) {
		if (top == "weapons" || top == "weapon") return SoundCategory::Weapons;
		if (top == "vo" || top == "voice" || top == "dialog" || top == "dialogue" || top == "combined" || top == "sentences") return SoundCategory::Voice;
		if (top == "npc" || top == "npcs" || top == "creatures" || top == "monsters") return SoundCategory::Npcs;
		if (top == "ambient" || top == "ambience" || top == "atmosphere" || top == "nature") return SoundCategory::Ambient;
		if (top == "physics" || top == "debris" || top == "impacts" || top == "impact") return SoundCategory::Physics;
		if (top == "player" || top == "items" || top == "hl1") return SoundCategory::Player;
		if (top == "vehicles" || top == "vehicle") return SoundCategory::Vehicles;
		if (top == "doors" || top == "buttons" || top == "plats" || top == "movers") return SoundCategory::Doors;
		if (top == "music" || top == "songs") return SoundCategory::Music;
		if (top == "ui" || top == "hud" || top == "common" || top == "menu") return SoundCategory::Interface;
	}
	// Words anywhere in the path.
	if (containsAny(p, {"weapon", "pistol", "shotgun", "rifle", "smg", "gunfire", "/gun"})) return SoundCategory::Weapons;
	if (containsAny(p, {"footstep", "pickup"})) return SoundCategory::Player;
	if (containsAny(p, {"door", "button", "lever", "elevator"})) return SoundCategory::Doors;
	if (containsAny(p, {"impact", "debris", "break", "scrape", "bullet"})) return SoundCategory::Physics;
	if (containsAny(p, {"music", "song"})) return SoundCategory::Music;
	if (containsAny(p, {"vehicle", "airboat", "jeep", "helicopter", "train"})) return SoundCategory::Vehicles;
	if (containsAny(p, {"wind", "rain", "water", "hum", "amb"})) return SoundCategory::Ambient;
	return SoundCategory::Other;
}

SoundCategory categorizeSoundScript(std::string_view name, std::string_view firstWave) {
	const std::string n = toLower(name);
	const std::string prefix = n.substr(0, n.find('.'));
	if (startsWithAny(prefix, {"weapon", "basecombatweapon"}) || containsAny(prefix, {"weapon_"})) return SoundCategory::Weapons;
	if (startsWithAny(prefix, {"door", "button", "plat", "elevator", "func_door", "func_button"})) return SoundCategory::Doors;
	if (startsWithAny(prefix, {"music", "song"})) return SoundCategory::Music;
	if (startsWithAny(prefix, {"hud", "ui", "menu", "gamemenu"})) return SoundCategory::Interface;
	if (startsWithAny(prefix, {"vehicle", "airboat", "jeep", "atv", "apc", "prisonpod", "trainhorn"})) return SoundCategory::Vehicles;
	if (startsWithAny(prefix, {"player", "hl2player", "suit", "item", "healthkit", "battery"})) return SoundCategory::Player;
	// Surface sounds ("Wood.ImpactHard", "Metal_Box.Break"): steps belong to the player, the rest to physics.
	const auto rest = n.find('.') == std::string::npos ? std::string_view() : std::string_view(n).substr(n.find('.') + 1);
	if (startsWithAny(rest, {"step"})) return SoundCategory::Player;
	if (startsWithAny(prefix, {"physics", "solid", "debris"}) || startsWithAny(rest, {"impact", "break", "scrape", "bulletimpact", "strain"})) {
		return SoundCategory::Physics;
	}
	if (!firstWave.empty()) {
		const auto byFile = categorizeSoundFile(firstWave);
		if (byFile != SoundCategory::Other) return byFile;
	}
	if (startsWithAny(prefix, {"npc", "zombie", "headcrab", "antlion", "combine", "manhack", "scanner", "strider", "hunter", "citizen",
	                           "vortigaunt", "barnacle", "crow", "pigeon", "seagull"})) {
		return SoundCategory::Npcs;
	}
	if (startsWithAny(prefix, {"ambient", "ambience", "amb_"})) return SoundCategory::Ambient;
	return SoundCategory::Other;
}

std::string soundFolder(std::string_view path) {
	const std::string p = cleanPath(path);
	const auto slash = p.rfind('/');
	return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

} // namespace sme
