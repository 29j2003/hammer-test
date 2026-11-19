// weapons.cpp - Weapon scripts, their sounds and skill values (see weapons.h).

#include <sme/weapons.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view BACKUP = ".mapsmith-backup";
constexpr std::string_view SCRIPT_KIND = "weapon script";

// weapon_parse.cpp pWeaponSoundCategories, in its order.
constexpr std::array<std::string_view, 16> SLOTS = {"empty",       "single_shot", "single_shot_npc", "double_shot", "double_shot_npc", "burst",
                                                    "reload",      "reload_npc",  "melee_miss",      "melee_hit",   "melee_hit_world", "special1",
                                                    "special2",    "special3",    "taunt",           "deploy"};

constexpr std::array FIELDS = {
	WeaponField{"printname", "Name", "What the HUD shows (a #token from the mod's text file, or plain text).", WeaponField::Kind::Text, ""},
	WeaponField{"clip_size", "Magazine size", "Rounds in one magazine; -1: no magazine (fires from the reserve).", WeaponField::Kind::Integer, "-1"},
	WeaponField{"default_clip", "Rounds when picked up", "Rounds in the magazine when the weapon is picked up.", WeaponField::Kind::Integer, "clip size"},
	WeaponField{"primary_ammo", "Ammo type", "Which ammo it uses; the ammo type sets damage and the most the player can carry (skill values below).",
	            WeaponField::Kind::Ammo, "None"},
	WeaponField{"clip2_size", "Second magazine", "Secondary fire's magazine (-1: none).", WeaponField::Kind::Integer, "-1"},
	WeaponField{"default_clip2", "Second rounds when picked up", "Secondary rounds when picked up.", WeaponField::Kind::Integer, "second magazine"},
	WeaponField{"secondary_ammo", "Second ammo type", "Ammo of the secondary fire.", WeaponField::Kind::Ammo, "None"},
	WeaponField{"bucket", "HUD slot", "Column in the weapon selection (0 = first).", WeaponField::Kind::Integer, "0"},
	WeaponField{"bucket_position", "Position in slot", "Row inside the column.", WeaponField::Kind::Integer, "0"},
	WeaponField{"weight", "Weight", "Higher is preferred when the game switches weapons for you.", WeaponField::Kind::Integer, "0"},
	WeaponField{"autoswitchto", "Switch to when picked up", "The game switches to it when the player picks it up.", WeaponField::Kind::Switch, "1"},
	WeaponField{"autoswitchfrom", "Switch away when empty", "The game may switch away from it.", WeaponField::Kind::Switch, "1"},
	WeaponField{"viewmodel_fov", "View model FOV (Mapbase)", "Field of view of the first-person model; 0 uses the player's setting (54).",
	            WeaponField::Kind::Number, "0"},
	WeaponField{"bob_scale", "Bob (Mapbase)", "How much the weapon bobs when walking (1 = normal).", WeaponField::Kind::Number, "1"},
	WeaponField{"sway_scale", "Sway (Mapbase)", "How far the weapon lags behind when looking around (1 = normal).", WeaponField::Kind::Number, "1"},
	WeaponField{"sway_speed_scale", "Sway speed (Mapbase)", "How fast it catches up (1 = normal).", WeaponField::Kind::Number, "1"},
	WeaponField{"viewmodel", "First-person model", "The model the player sees in his hands.", WeaponField::Kind::Model, ""},
	WeaponField{"playermodel", "World model", "The model on the ground and in NPCs' hands.", WeaponField::Kind::Model, ""},
};

// hl2_gamerules.cpp GetAmmoDef (Half-Life 2 and the episodes).
constexpr std::array<std::string_view, 14> AMMO = {"None",         "AR2",       "AR2AltFire", "Pistol",  "SMG1",    "357",  "XBowBolt",
                                                   "Buckshot",     "RPG_Round", "SMG1_Grenade", "Grenade", "slam", "SniperRound", "Hopwire"};

struct AmmoSkill {
	std::string_view ammo, player, npc, max;
};
constexpr std::array AMMO_SKILLS = {
	AmmoSkill{"AR2", "sk_plr_dmg_ar2", "sk_npc_dmg_ar2", "sk_max_ar2"},
	AmmoSkill{"AlyxGun", "sk_plr_dmg_alyxgun", "sk_npc_dmg_alyxgun", "sk_max_alyxgun"},
	AmmoSkill{"Pistol", "sk_plr_dmg_pistol", "sk_npc_dmg_pistol", "sk_max_pistol"},
	AmmoSkill{"SMG1", "sk_plr_dmg_smg1", "sk_npc_dmg_smg1", "sk_max_smg1"},
	AmmoSkill{"357", "sk_plr_dmg_357", "sk_npc_dmg_357", "sk_max_357"},
	AmmoSkill{"XBowBolt", "sk_plr_dmg_crossbow", "sk_npc_dmg_crossbow", "sk_max_crossbow"},
	AmmoSkill{"Buckshot", "sk_plr_dmg_buckshot", "sk_npc_dmg_buckshot", "sk_max_buckshot"},
	AmmoSkill{"RPG_Round", "sk_plr_dmg_rpg_round", "sk_npc_dmg_rpg_round", "sk_max_rpg_round"},
	AmmoSkill{"SMG1_Grenade", "sk_plr_dmg_smg1_grenade", "sk_npc_dmg_smg1_grenade", "sk_max_smg1_grenade"},
	AmmoSkill{"SniperRound", "sk_plr_dmg_sniper_round", "sk_npc_dmg_sniper_round", "sk_max_sniper_round"},
	AmmoSkill{"Grenade", "sk_plr_dmg_grenade", "sk_npc_dmg_grenade", "sk_max_grenade"},
	AmmoSkill{"AR2AltFire", "", "", "sk_max_ar2_altfire"},
	AmmoSkill{"Hopwire", "sk_plr_dmg_grenade", "sk_npc_dmg_grenade", "sk_max_hopwire"},
};

std::optional<std::string> readText(const fs::path& path) {
	return readFileText(path);
}

bool writeFile(const fs::path& path, std::string_view text, std::string* error) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (out) out.write(text.data(), std::streamsize(text.size()));
	if (!out) {
		if (error) *error = "cannot write " + displayPath(path);
		return false;
	}
	return true;
}

std::optional<ImportRecord> recordFor(const fs::path& modDir, std::string_view kind, std::string_view asset) {
	for (const auto& record : loadImports(modDir)) {
		if (record.kind == kind && iequals(record.asset, asset)) return record;
	}
	return std::nullopt;
}

/// The sk_ name of a cfg line, or empty ("sk_plr_dmg_smg1 \"4\" // comment" -> "sk_plr_dmg_smg1").
std::string skillName(std::string_view line) {
	std::size_t at = 0;
	while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
	if (at < line.size() && line[at] == '"') ++at;
	std::size_t end = at;
	while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
	const std::string name = toLower(line.substr(at, end - at));
	return startsWithI(name, "sk_") ? name : std::string();
}

/// The value after the name on a cfg line ("sk_x \"4\"" -> "4", "sk_x 4" -> "4").
std::string skillLineValue(std::string_view line, std::string_view name) {
	const auto at = toLower(line).find(toLower(name));
	if (at == std::string::npos) return {};
	std::size_t i = at + name.size();
	if (i < line.size() && line[i] == '"') ++i;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
	if (i < line.size() && line[i] == '"') {
		const auto close = line.find('"', i + 1);
		return std::string(line.substr(i + 1, close == std::string_view::npos ? std::string_view::npos : close - i - 1));
	}
	std::size_t end = i;
	while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '/' && line[end] != '\r') ++end;
	return std::string(line.substr(i, end - i));
}

/// A value safe for a cfg line: digits, sign, point.
std::string cleanSkillValue(std::string_view value) {
	std::string out;
	for (const char c : value) {
		if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-') out.push_back(c);
	}
	return out.empty() ? "0" : out;
}

} // namespace

std::span<const std::string_view> weaponSoundSlots() {
	return SLOTS;
}

std::string_view weaponSoundSlotLabel(std::string_view slot) {
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 16> labels = {{
		{"empty", "Dry fire (empty)"},
		{"single_shot", "Shot (player)"},
		{"single_shot_npc", "Shot (NPC)"},
		{"double_shot", "Double shot (player)"},
		{"double_shot_npc", "Double shot (NPC)"},
		{"burst", "Burst"},
		{"reload", "Reload (player)"},
		{"reload_npc", "Reload (NPC)"},
		{"melee_miss", "Swing (miss)"},
		{"melee_hit", "Hit"},
		{"melee_hit_world", "Hit the world"},
		{"special1", "Special 1"},
		{"special2", "Special 2"},
		{"special3", "Special 3"},
		{"taunt", "Taunt"},
		{"deploy", "Draw"},
	}};
	for (const auto& [key, label] : labels) {
		if (iequals(key, slot)) return label;
	}
	return slot;
}

std::vector<std::pair<std::string, std::string>> WeaponScript::sounds() const {
	std::vector<std::pair<std::string, std::string>> out;
	const KvNode* block = data.find("SoundData");
	if (!block || !block->block) return out;
	for (const auto slot : SLOTS) {
		const std::string sound = block->get(slot);
		if (!sound.empty()) out.emplace_back(std::string(slot), sound);
	}
	return out;
}

std::optional<WeaponScript> parseWeaponScript(std::string_view text, std::string_view className) {
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return std::nullopt;
	}
	const KvNode* block = root.find("WeaponData");
	if (!block || !block->block) {
		// Some files name the block differently; the engine takes the file's first block.
		block = nullptr;
		for (const auto& child : root.children) {
			if (child.block) {
				block = &child;
				break;
			}
		}
	}
	if (!block) return std::nullopt;
	WeaponScript out;
	out.className = toLower(className);
	out.file = "scripts/" + out.className + ".txt";
	out.data = *block;
	return out;
}

std::string writeWeaponScript(const WeaponScript& script) {
	KvNode root;
	KvNode block = script.data;
	if (block.key.empty()) block.key = "WeaponData";
	block.block = true;
	root.children.push_back(std::move(block));
	return "// " + script.className + " - saved by Mapsmith (Weapons page)\n" + writeKeyValues(root);
}

std::vector<WeaponScript> findWeapons(const GameFileSystem& files, const fs::path& modDir) {
	std::vector<WeaponScript> out;
	for (const auto& path : files.list("scripts", ".txt")) {
		const std::string name = path.substr(path.find_last_of('/') + 1);
		if (!startsWithI(name, "weapon_") || name == "weapon_manifest.txt" || path.find('/') != path.rfind('/')) continue;
		const std::string className = name.substr(0, name.size() - 4);
		const auto text = files.readText(path);
		if (!text) continue;
		auto script = parseWeaponScript(*text, className);
		if (!script) continue;
		script->file = path;
		std::error_code ec;
		script->inMod = !modDir.empty() && resolveI(modDir, path).has_value();
		out.push_back(std::move(*script));
	}
	// One entry per class: file names differing only in case ("Weapon_SMG1.txt") are the same
	// class to the game (its file system ignores case); the mod's copy wins, as in the game.
	for (auto& w : out) w.className = toLower(w.className);
	std::stable_sort(out.begin(), out.end(), [](const WeaponScript& a, const WeaponScript& b) {
		return a.className != b.className ? a.className < b.className : a.inMod > b.inMod;
	});
	out.erase(std::unique(out.begin(), out.end(), [](const WeaponScript& a, const WeaponScript& b) { return a.className == b.className; }), out.end());
	return out;
}

namespace {

// Player weapons of HL2/EP1/EP2 (player.cpp CheatImpulseCommands gives these, plus the stunstick,
// which only Metrocops carry in HL2 but has a full view model and works for the player).
constexpr std::array<std::string_view, 12> PLAYER_WEAPONS = {"weapon_357",     "weapon_ar2",     "weapon_bugbait",    "weapon_crossbow",
                                                             "weapon_crowbar", "weapon_frag",    "weapon_physcannon", "weapon_pistol",
                                                             "weapon_rpg",     "weapon_shotgun", "weapon_smg1",       "weapon_stunstick"};
// Weapons only NPCs use (they borrow a player weapon's print name or have none).
constexpr std::array<std::string_view, 6> NPC_WEAPONS = {"weapon_alyxgun",         "weapon_annabelle",     "weapon_citizenpackage",
                                                         "weapon_citizensuitcase", "weapon_oldmanharpoon", "weapon_smg2"};
// Debug tools, unfinished or cut weapons, multiplayer/SDK bases and Mapbase's scripted-weapon template.
constexpr std::array<std::string_view, 25> DEVELOPER_WEAPONS = {
	"weapon_cubemap",      "weapon_physgun",        "weapon_hopwire",     "weapon_custom_scripted1", "weapon_brickbat",
	"weapon_extinguisher", "weapon_flaregun",       "weapon_immolator",   "weapon_irifle",           "weapon_manhack",
	"weapon_mattpipe",     "weapon_molotov",        "weapon_sniperrifle", "weapon_striderbuster",    "weapon_thumper",
	"weapon_tripwire",     "weapon_cguard",         "weapon_ar1",         "weapon_slam",             "weapon_hl2mp_base",
	"weapon_sdk_base",     "weapon_basesdkgrenade", "weapon_ifm_base",    "weapon_ifm_base_camera",  "weapon_ifm_steadycam"};

bool inList(std::span<const std::string_view> list, std::string_view name) {
	return std::any_of(list.begin(), list.end(), [&](std::string_view n) { return iequals(n, name); });
}

} // namespace

WeaponRole weaponRole(std::string_view className) {
	if (inList(PLAYER_WEAPONS, className)) return WeaponRole::Player;
	if (inList(NPC_WEAPONS, className)) return WeaponRole::Npc;
	if (inList(DEVELOPER_WEAPONS, className)) return WeaponRole::Developer;
	return WeaponRole::Unknown;
}

bool isViewModelPath(std::string_view model) {
	const std::string path = toLower(normalizePath(model));
	const auto slash = path.find_last_of('/');
	const std::string_view file = slash == std::string::npos ? std::string_view(path) : std::string_view(path).substr(slash + 1);
	return path.rfind("models/", 0) == 0 && file.size() > 6 && file.substr(0, 2) == "v_" && file.substr(file.size() - 4) == ".mdl";
}

bool listedByDefault(const WeaponScript& script, const std::function<bool(std::string_view)>& modelExists) {
	const WeaponRole role = weaponRole(script.className);
	if (role != WeaponRole::Player && role != WeaponRole::Unknown) return false;
	const std::string model = normalizePath(script.viewModel());
	return isViewModelPath(model) && (!modelExists || modelExists(model));
}

std::string weaponDisplayName(const WeaponScript& script, const std::function<std::string(std::string_view)>& lookup) {
	const std::string print = script.get("printname");
	if (!print.empty() && print.front() == '#' && lookup) {
		const std::string text = lookup(print.substr(1));
		if (!text.empty()) return text;
	}
	if (!print.empty() && print.front() != '#' && print != "!! Missing printname on ") return print;
	std::string name = script.className;
	if (startsWithI(name, "weapon_")) name = name.substr(7);
	return name;
}

std::span<const WeaponField> weaponFields() {
	return FIELDS;
}

std::span<const std::string_view> weaponAmmoTypes() {
	return AMMO;
}

std::vector<WeaponSkillValue> weaponSkillValues(const WeaponScript& script) {
	std::vector<WeaponSkillValue> out;
	const auto add = [&out](std::string_view name, std::string label) {
		if (name.empty()) return;
		if (std::any_of(out.begin(), out.end(), [&](const WeaponSkillValue& v) { return v.name == name; })) return;
		out.push_back({std::string(name), std::move(label)});
	};
	for (const auto& [key, which] : {std::pair{"primary_ammo", ""}, std::pair{"secondary_ammo", " (secondary)"}}) {
		const std::string ammo = script.get(key);
		for (const auto& skill : AMMO_SKILLS) {
			if (!iequals(skill.ammo, ammo)) continue;
			add(skill.player, std::string("Damage per hit (player)") + which);
			add(skill.npc, std::string("Damage per hit (NPCs)") + which);
			add(skill.max, std::string("Most ammo carried") + which);
		}
	}
	const std::string& c = script.className;
	if (c == "weapon_crowbar") add("sk_plr_dmg_crowbar", "Damage per hit (player)");
	if (c == "weapon_stunstick") add("sk_plr_dmg_stunstick", "Damage per hit (player)");
	if (c == "weapon_shotgun") {
		add("sk_plr_num_shotgun_pellets", "Pellets per shot");
		add("sk_plr_num_shotgun_pellets_double", "Pellets per double shot");
	}
	return out;
}

std::map<std::string, std::string> readSkillValues(std::string_view cfgText) {
	std::map<std::string, std::string> out;
	std::istringstream lines{std::string(cfgText)};
	for (std::string line; std::getline(lines, line);) {
		const std::string name = skillName(line);
		if (name.empty()) continue;
		out[name] = skillLineValue(line, name);
	}
	return out;
}

std::string withSkillValue(std::string_view cfgText, std::string_view name, std::string_view value) {
	const std::string wanted = toLower(name);
	const std::string line = std::string(name) + "\t\"" + cleanSkillValue(value) + "\"";
	std::string out;
	bool done = false;
	std::istringstream lines{std::string(cfgText)};
	for (std::string each; std::getline(lines, each);) {
		const bool cr = !each.empty() && each.back() == '\r';
		if (!done && skillName(each) == wanted) {
			out += line + (cr ? "\r\n" : "\n");
			done = true;
		} else {
			out += each + "\n";
		}
	}
	if (!done) {
		if (!out.empty() && out.back() != '\n') out += "\n";
		out += line + "\n";
	}
	return out;
}

bool saveWeaponScript(const fs::path& modDir, const WeaponScript& script, std::string* error) {
	const std::string file = normalizePath(script.file);
	if (!isSafeGamePath(file) || !startsWithI(file, "scripts/") || !endsWithI(file, ".txt")) {
		if (error) *error = "not a weapon script path: " + script.file;
		return false;
	}
	const fs::path target = modDir / toNativePath(file);
	std::error_code ec;
	if (!recordFor(modDir, SCRIPT_KIND, file)) {
		ImportRecord record{std::string(SCRIPT_KIND), file, {}, {}, {}};
		if (fs::exists(target, ec)) {
			fs::copy_file(target, target.string() + std::string(BACKUP), fs::copy_options::overwrite_existing, ec);
			if (ec) {
				if (error) *error = "cannot back up " + displayPath(target);
				return false;
			}
			record.replaced.push_back(target);
		} else {
			record.created.push_back(target);
		}
		if (!rememberImport(modDir, record)) {
			if (error) *error = "cannot write " + displayPath(importLogFile(modDir));
			return false;
		}
	}
	return writeFile(target, writeWeaponScript(script), error);
}

bool restoreWeaponScript(const fs::path& modDir, std::string_view scriptFile, std::string* error) {
	const auto record = recordFor(modDir, SCRIPT_KIND, normalizePath(scriptFile));
	if (!record) return true;
	if (!undoImport(*record, error)) return false;
	return forgetImport(modDir, *record);
}

bool weaponScriptSaved(const fs::path& modDir, std::string_view scriptFile) {
	return recordFor(modDir, SCRIPT_KIND, normalizePath(scriptFile)).has_value();
}

bool saveSkillValues(const fs::path& modDir, const std::map<std::string, std::string>& values, std::string_view gameSkillCfg, std::string* error) {
	const fs::path cfg = modDir / "cfg" / "skill.cfg";
	std::error_code ec;
	const auto own = readText(cfg);
	std::string text = own ? *own : std::string(gameSkillCfg);
	for (const auto& [name, value] : values) {
		if (skillName(name) != toLower(name)) continue;   // Only sk_ names
		text = withSkillValue(text, name, value);
	}
	if (own) {
		const fs::path backup = cfg.string() + std::string(BACKUP);
		if (!fs::exists(backup, ec)) fs::copy_file(cfg, backup, ec);
	}
	if (!writeFile(cfg, text, error)) return false;
	// The game runs "exec skill_manifest.cfg" at map start, which runs skill.cfg (mod_base_files.h).
	const fs::path manifest = modDir / "cfg" / "skill_manifest.cfg";
	if (!fs::exists(manifest, ec)) {
		std::string lines = "exec skill.cfg\n";
		if (fs::exists(modDir / "cfg" / "skill_episodic.cfg", ec)) lines += "exec skill_episodic.cfg\n";
		if (!writeFile(manifest, lines, error)) return false;
	}
	return true;
}

std::optional<SoundImportOptions> soundReplaceOptions(std::string_view wave, const DecodedAudio* original) {
	const std::string path = normalizePath(stripSoundChars(wave));
	if (!isSafeGamePath(path) || !endsWithI(path, ".wav")) return std::nullopt;
	const auto slash = path.find_last_of('/');
	SoundImportOptions options;
	options.folder = slash == std::string::npos ? std::string() : path.substr(0, slash);
	options.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
	options.name.resize(options.name.size() - 4);
	// The importer cleans names; a path it would change cannot be covered at the same place.
	if (importFolder(options.folder) != toLower(options.folder) || importName(options.name) != toLower(options.name)) return std::nullopt;
	options.sampleRate = 44100;
	if (original) {
		if (original->sampleRate == 44100 || original->sampleRate == 22050 || original->sampleRate == 11025) options.sampleRate = original->sampleRate;
		options.channels = original->channels == 1 ? SoundImportOptions::Channels::Mono : SoundImportOptions::Channels::Stereo;
		options.loop = original->loopStart.has_value();
		options.loopStart = 0;
	}
	options.script = false;
	return options;
}

std::optional<ImportRecord> soundReplacement(const fs::path& modDir, std::string_view wave) {
	return recordFor(modDir, "sound", normalizePath(stripSoundChars(wave)));
}

std::optional<ImportRecord> replaceSound(const fs::path& modDir, std::string_view wave, const DecodedAudio& audio, const DecodedAudio* original,
                                         std::string* error) {
	const auto options = soundReplaceOptions(wave, original);
	if (!options) {
		if (error) *error = "only .wav files with plain names can be replaced in place: " + std::string(wave);
		return std::nullopt;
	}
	if (!restoreSound(modDir, wave, error)) return std::nullopt;
	auto record = importSound(modDir, audio, *options, {}, true, error);
	if (!record) return std::nullopt;
	if (!rememberImport(modDir, *record)) {
		undoImport(*record);
		if (error) *error = "cannot write " + displayPath(importLogFile(modDir));
		return std::nullopt;
	}
	return record;
}

bool restoreSound(const fs::path& modDir, std::string_view wave, std::string* error) {
	const auto record = soundReplacement(modDir, wave);
	if (!record) return true;
	if (!undoImport(*record, error)) return false;
	return forgetImport(modDir, *record);
}

std::optional<ImportRecord> textureReplacement(const fs::path& modDir, std::string_view texture) {
	return recordFor(modDir, "texture", normalizePath(texture));
}

bool restoreTexture(const fs::path& modDir, std::string_view texture, std::string* error) {
	const auto record = textureReplacement(modDir, texture);
	if (!record) return true;
	if (!undoImport(*record, error)) return false;
	return forgetImport(modDir, *record);
}

std::optional<ImportRecord> replaceTexture(const fs::path& modDir, std::string_view texture, const ImageRgba& image, unsigned maxSize, std::string* error) {
	const std::string name = normalizePath(texture);
	const std::string path = "materials/" + name + ".vtf";
	if (name.empty() || !isSafeGamePath(path)) {
		if (error) *error = "not a texture path: " + std::string(texture);
		return std::nullopt;
	}
	auto vtf = encodeVtf(image, true, false, maxSize, error);
	if (!vtf) return std::nullopt;
	if (!restoreTexture(modDir, name, error)) return std::nullopt;
	const fs::path target = modDir / toNativePath(path);
	ImportRecord record{"texture", name, {}, {}, {}};
	std::error_code ec;
	if (fs::exists(target, ec)) {
		fs::copy_file(target, target.string() + std::string(BACKUP), fs::copy_options::overwrite_existing, ec);
		if (ec) {
			if (error) *error = "cannot back up " + displayPath(target);
			return std::nullopt;
		}
		record.replaced.push_back(target);
	} else {
		record.created.push_back(target);
	}
	const std::string bytes(reinterpret_cast<const char*>(vtf->data()), vtf->size());
	if (!writeFile(target, bytes, error)) return std::nullopt;
	if (!rememberImport(modDir, record)) {
		undoImport(record);
		if (error) *error = "cannot write " + displayPath(importLogFile(modDir));
		return std::nullopt;
	}
	return record;
}

} // namespace sme
