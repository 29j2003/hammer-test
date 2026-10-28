// sound_categories.h - Sorts the thousands of sounds of a Source game into categories the Sounds
// page shows (weapons, NPCs, ambient, physics, player, vehicles, doors, music, interface, voice):
// sound files by their folders and words in their paths, sound script entries by their names
// ("Weapon_Pistol.Single") and else by their first file.

#pragma once

#include <array>
#include <string>
#include <string_view>

namespace sme {

/// Categories of the Sounds page, in the order it lists them.
enum class SoundCategory {
	Weapons,
	Npcs,        ///< NPCs and creatures (their noises; spoken lines are Voice)
	Ambient,
	Physics,     ///< Impacts, breaking, scraping, debris
	Player,      ///< Footsteps, the suit, pickups
	Vehicles,
	Doors,       ///< Doors, buttons, lifts and other movers
	Music,
	Interface,   ///< HUD and menu sounds
	Voice,       ///< Dialogue and spoken lines
	Other,
};

/// All categories in their order.
inline constexpr std::array<SoundCategory, 11> SOUND_CATEGORIES = {
	SoundCategory::Weapons, SoundCategory::Npcs,    SoundCategory::Ambient, SoundCategory::Physics,
	SoundCategory::Player,  SoundCategory::Vehicles, SoundCategory::Doors,   SoundCategory::Music,
	SoundCategory::Interface, SoundCategory::Voice, SoundCategory::Other};

/// English name ("Weapons", "NPCs and creatures", ...).
[[nodiscard]] std::string_view soundCategoryName(SoundCategory category);
/// Category of a sound file below sound/ ("weapons/pistol/pistol_fire2.wav").
[[nodiscard]] SoundCategory categorizeSoundFile(std::string_view path);
/// Category of a sound script entry by its name, else by its first file (may be empty).
[[nodiscard]] SoundCategory categorizeSoundScript(std::string_view name, std::string_view firstWave);
/// Folder of a sound file below sound/ ("weapons/pistol"), "" for files right in sound/.
[[nodiscard]] std::string soundFolder(std::string_view path);

} // namespace sme
