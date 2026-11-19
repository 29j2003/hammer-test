// weapons.h - The weapons of a Half-Life 2 / Mapbase mod for the Weapons page: their scripts,
// sounds and the skill values that set their damage and ammo.
//
// How the game finds them (Source SDK 2013, game/shared/weapon_parse.cpp and
// basecombatweapon_shared.cpp): every weapon reads scripts/<classname>.txt when it is precached
// (ReadWeaponDataFromFileForSlot). Half-Life 2's rules have no encryption key, so the file is read
// from the "GAME" search path as plain KeyValues - the mod's folder first, then the game's VPKs: a
// scripts/weapon_smg1.txt in the mod replaces the game's. Games with a key (HL2:DM, CS:S) read
// scripts/<classname>.ctx (ICE-encrypted) from "MOD" when there is no .txt; those are not read
// here. Mapbase also reads maps/<map>_<classname>.txt first (per-map scripts); the Weapons page
// edits the mod-wide file.
//
// The script's SoundData block names a sound script entry per slot ("single_shot" ->
// "Weapon_SMG1.Single"), resolved through scripts/game_sounds_manifest.txt (assets.h soundScripts)
// to wave files below sound/. Replacing a sound writes a WAV at the same path below the mod's
// sound/ folder, which covers the game's file the same way (mod folder first). Damage and ammo
// limits are console variables of the ammo type (hl2_gamerules.cpp GetAmmoDef: "SMG1" ->
// sk_plr_dmg_smg1, sk_npc_dmg_smg1, sk_max_smg1), set by the mod's cfg/skill.cfg (mod_base_files.h).

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/assets.h>
#include <sme/keyvalues.h>

namespace sme {

class GameFileSystem;

/// The sound slots of a weapon script's SoundData block (weapon_parse.cpp pWeaponSoundCategories).
[[nodiscard]] std::span<const std::string_view> weaponSoundSlots();
/// What a slot is for ("single_shot" -> "Shot (player)").
[[nodiscard]] std::string_view weaponSoundSlotLabel(std::string_view slot);

/// One weapon script.
struct WeaponScript {
	std::string className;      ///< "weapon_smg1"
	std::string file;           ///< "scripts/weapon_smg1.txt"
	bool inMod = false;         ///< The mod's folder has its own copy
	KvNode data;                ///< The WeaponData block (every key, also unknown ones)

	[[nodiscard]] std::string get(std::string_view key, std::string_view fallback = {}) const { return data.get(key, fallback); }
	[[nodiscard]] std::string viewModel() const { return get("viewmodel"); }
	[[nodiscard]] std::string worldModel() const { return get("playermodel"); }
	/// The slots that name a sound (slot, sound script name), in the order of weaponSoundSlots().
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> sounds() const;
};

/// A script from its text (WeaponData block; nullopt for broken text or no block).
[[nodiscard]] std::optional<WeaponScript> parseWeaponScript(std::string_view text, std::string_view className);
/// The file text of a script (writes the WeaponData block back; comments of the original are not kept).
[[nodiscard]] std::string writeWeaponScript(const WeaponScript& script);

/// Every weapon script the game sees (scripts/weapon_*.txt), sorted by class name. `modDir` marks
/// the ones the mod has its own copy of.
[[nodiscard]] std::vector<WeaponScript> findWeapons(const GameFileSystem& fs, const std::filesystem::path& modDir);

/// The name to show: the printname's text through `lookup` ("#HL2_SMG1" -> "SMG"), else the
/// printname, else the class without "weapon_".
[[nodiscard]] std::string weaponDisplayName(const WeaponScript& script, const std::function<std::string(std::string_view token)>& lookup = {});

/// A field of the Stats tab.
struct WeaponField {
	enum class Kind { Integer, Number, Text, Switch, Ammo, Model };
	std::string_view key;
	std::string_view label;
	std::string_view help;
	Kind kind = Kind::Integer;
	std::string_view fallback;   ///< What the game uses when the script has no value
};
/// The fields the Stats tab offers, in order.
[[nodiscard]] std::span<const WeaponField> weaponFields();
/// The ammo types of Half-Life 2 / Episode Two (for the ammo fields).
[[nodiscard]] std::span<const std::string_view> weaponAmmoTypes();

/// A skill value of a weapon.
struct WeaponSkillValue {
	std::string name;     ///< "sk_plr_dmg_smg1"
	std::string label;    ///< "Damage per bullet (player)"
};
/// The skill values that belong to the weapon: those of its ammo types, and weapon-specific ones
/// (crowbar and stun stick damage, shotgun pellets).
[[nodiscard]] std::vector<WeaponSkillValue> weaponSkillValues(const WeaponScript& script);
/// The sk_* values of a skill.cfg text (lower-case name -> value as written).
[[nodiscard]] std::map<std::string, std::string> readSkillValues(std::string_view cfgText);
/// The text with `name` set to `value` (the existing line replaced, else a line added).
[[nodiscard]] std::string withSkillValue(std::string_view cfgText, std::string_view name, std::string_view value);

/// Saves a script as the mod's own copy (<mod>/<script.file>). The first save keeps the mod's
/// earlier copy as a backup (or notes that the file is new), recorded in the mod's import list as
/// kind "weapon script", so restoreWeaponScript puts the game's version back.
bool saveWeaponScript(const std::filesystem::path& modDir, const WeaponScript& script, std::string* error = nullptr);
/// Takes the saved script out again (the game's or the mod's earlier copy counts again).
bool restoreWeaponScript(const std::filesystem::path& modDir, std::string_view scriptFile, std::string* error = nullptr);
/// Whether Mapsmith saved the script (it can be restored).
[[nodiscard]] bool weaponScriptSaved(const std::filesystem::path& modDir, std::string_view scriptFile);

/// Sets skill values in the mod's cfg/skill.cfg; a mod without one starts from `gameSkillCfg` (the
/// game's text). A changed skill.cfg is kept once as cfg/skill.cfg.mapsmith-backup.
bool saveSkillValues(const std::filesystem::path& modDir, const std::map<std::string, std::string>& values, std::string_view gameSkillCfg,
                     std::string* error = nullptr);

/// How a wave of the game is replaced: the import options that write a WAV at the same path below
/// the mod's sound/ folder ("weapons/smg1/smg1_fire1.wav"), keeping the original's sample rate and
/// channels (`original` may be null: 44100 Hz, channels kept). nullopt for anything but a .wav path.
[[nodiscard]] std::optional<SoundImportOptions> soundReplaceOptions(std::string_view wave, const DecodedAudio* original);
/// Replaces a wave of the game (see soundReplaceOptions) with `audio`: an earlier replacement is
/// taken back first (so the backup stays the original), the new WAV is written and recorded in the
/// mod's import list. nullopt with an error if the path cannot be written as it is.
[[nodiscard]] std::optional<ImportRecord> replaceSound(const std::filesystem::path& modDir, std::string_view wave, const DecodedAudio& audio,
                                                       const DecodedAudio* original, std::string* error = nullptr);
/// Takes a replaced wave out again (the game's plays again). True if there was nothing to do.
bool restoreSound(const std::filesystem::path& modDir, std::string_view wave, std::string* error = nullptr);
/// The import list record of a replaced wave (nullopt if Mapsmith did not replace it).
[[nodiscard]] std::optional<ImportRecord> soundReplacement(const std::filesystem::path& modDir, std::string_view wave);

/// Replaces a texture of the game ("models/weapons/v_smg1/smg1_sheet", below materials/, no
/// extension) with a picture: a VTF at the same path in the mod (power of two, at most `maxSize`,
/// DXT1/DXT5), recorded as kind "texture"; an earlier replacement is taken back first and the
/// mod's own file kept as a backup. Model textures are mapped 0..1, so any size fits.
[[nodiscard]] std::optional<ImportRecord> replaceTexture(const std::filesystem::path& modDir, std::string_view texture, const ImageRgba& image,
                                                         unsigned maxSize = 2048, std::string* error = nullptr);
/// Takes a replaced texture out again. True if there was nothing to do.
bool restoreTexture(const std::filesystem::path& modDir, std::string_view texture, std::string* error = nullptr);
/// The import list record of a replaced texture.
[[nodiscard]] std::optional<ImportRecord> textureReplacement(const std::filesystem::path& modDir, std::string_view texture);

} // namespace sme
