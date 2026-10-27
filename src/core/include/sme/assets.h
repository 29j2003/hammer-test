// assets.h - Information about game assets for the browsers: material categories,
// model sequences (animations), model materials, sound script names.

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/material.h>
#include <sme/math.h>

namespace sme {

class GameFileSystem;

/// Categories used by the material browser filters.
struct MaterialCategory {
	bool blend = false;        ///< Two-texture blend for displacements (WorldVertexTransition, $basetexture2)
	bool model = false;        ///< Meant for models (VertexLitGeneric, materials/models/...)
	bool tool = false;         ///< nodraw, clip, trigger, skybox, ...
	bool translucent = false;  ///< $translucent / $alphatest / $additive
	bool water = false;
	bool decal = false;        ///< $decal or overlays/decals folders
	bool sky = false;          ///< Skybox faces (materials/skybox/)
	bool brush = false;        ///< Usable on world brushes (LightmappedGeneric & co., not model-only)
};

[[nodiscard]] MaterialCategory classifyMaterial(const Material& material);

/// What the editor needs from a .mdl file.
struct ModelInfo {
	std::string name;                       ///< Name stored in the model
	int version = 0;
	std::vector<std::string> sequences;     ///< Animation sequence names (own, without include models)
	std::vector<std::string> activities;    ///< Activity per sequence ("ACT_IDLE", may be empty)
	std::vector<std::string> includeModels; ///< $includemodel files holding more sequences
	std::vector<std::string> materials;     ///< Texture names (combine with materialDirs)
	std::vector<std::string> materialDirs;  ///< $cdmaterials folders
	std::string surfaceProp;
	Vec3 hullMin, hullMax;
};

/// Parses a Source 1 .mdl header (versions 44-49). Never reads outside the buffer.
[[nodiscard]] std::optional<ModelInfo> readModelInfo(std::span<const std::byte> data, std::string* error = nullptr);

/// Sequences of a model including all its $includemodel files (as the engine merges them).
[[nodiscard]] std::vector<std::string> modelSequences(const GameFileSystem& fs, std::string_view modelPath, std::string* error = nullptr);

/// Sound names from all sound scripts listed in scripts/game_sounds_manifest.txt (sorted, unique).
[[nodiscard]] std::vector<std::string> soundScriptNames(const GameFileSystem& fs);

/// One entry of a sound script ("Weapon_Pistol.Single" { "wave" "weapons/pistol/pistol_fire2.wav" }).
struct SoundScriptEntry {
	std::string name;
	std::string script;                 ///< File it came from ("scripts/game_sounds_weapons.txt")
	std::string channel;                ///< CHAN_WEAPON, ...
	std::vector<std::string> waves;     ///< Files below sound/ (sound characters like ")" removed)
};

/// Every sound script entry (later scripts override earlier ones with the same name, as in the engine).
[[nodiscard]] std::vector<SoundScriptEntry> soundScripts(const GameFileSystem& fs);

/// A soundscape (env_soundscape "soundscape" key) from scripts/soundscapes_*.txt.
struct Soundscape {
	std::string name;                   ///< "d1_canals.util_drips"
	std::string file;
	int dsp = -1;
	std::vector<std::string> sounds;    ///< Waves and sound script names it plays
	std::vector<std::string> includes;  ///< Other soundscapes it plays (playsoundscape)
};

/// Soundscapes from scripts/soundscapes_manifest.txt (or every scripts/soundscapes*.txt).
[[nodiscard]] std::vector<Soundscape> soundscapes(const GameFileSystem& fs);

/// A particle system (info_particle_system "effect_name") from a .pcf file.
struct ParticleSystem {
	std::string name;
	std::string file;   ///< "particles/fire_01.pcf"
};

/// Particle system names in one .pcf (DMX) file; empty for broken files.
[[nodiscard]] std::vector<std::string> particleSystemNames(std::span<const std::byte> pcf);

/// Particle systems of the game: files from particles/particles_manifest.txt (or every
/// particles/*.pcf), sorted by name.
[[nodiscard]] std::vector<ParticleSystem> particleSystems(const GameFileSystem& fs);

/// Removes the leading sound characters of a wave name (")weapons/x.wav" -> "weapons/x.wav").
[[nodiscard]] std::string stripSoundChars(std::string_view wave);

/// Levenshtein distance (case-insensitive), used for "did you mean" fixes.
[[nodiscard]] std::size_t editDistance(std::string_view a, std::string_view b);

/// Closest candidate to `name`, or empty if none is close enough.
[[nodiscard]] std::string closestMatch(std::string_view name, const std::vector<std::string>& candidates);

} // namespace sme
