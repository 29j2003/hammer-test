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

/// Sorts a material into the browser's categories (brush, blend, model, tool, see-through, water, decal, sky).
[[nodiscard]] MaterialCategory classifyMaterial(const Material& material);

/// Whether the material is water (Water shader or %compilewater).
[[nodiscard]] bool isWaterMaterial(const GameFileSystem& fs, std::string_view name);
/// The material "Make water" uses: `current` if it is water, else the first water material of
/// `names` (names with "water" in them are tried first). Empty if there is none.
[[nodiscard]] std::string pickWaterMaterial(const GameFileSystem& fs, std::string_view current, const std::vector<std::string>& names);

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
	int skinCount = 1;                      ///< Skin families ($texturegroup), at least 1
	/// A body group ($bodygroup): one of its options is shown at a time ("blank" = nothing).
	struct BodyGroup {
		std::string name;
		std::vector<std::string> options;
	};
	std::vector<BodyGroup> bodyGroups;      ///< In file order; the first option of each is the default
	bool staticProp = false;                ///< Compiled with $staticprop (can be a prop_static)
	float mass = 0;                         ///< Physics mass from $collisionmodel (0 without)
	std::string keyValues;                  ///< $keyvalues text (prop_data, physgun_interactions ...)

	/// The engine's "body" number for one chosen option per body group (missing entries: 0).
	[[nodiscard]] int bodyValue(const std::vector<int>& choices) const;
	/// The option of each body group in a body number (the reverse of bodyValue; one per group).
	[[nodiscard]] std::vector<int> bodyChoices(int body) const;
};

/// Parses a Source 1 .mdl header (versions 44-49). Never reads outside the buffer.
[[nodiscard]] std::optional<ModelInfo> readModelInfo(std::span<const std::byte> data, std::string* error = nullptr);

/// Sequences of a model including all its $includemodel files (as the engine merges them).
[[nodiscard]] std::vector<std::string> modelSequences(const GameFileSystem& fs, std::string_view modelPath, std::string* error = nullptr);

/// What decides the prop class a model can be placed as.
struct PropModelFlags {
	bool staticProp = false;   ///< Compiled with $staticprop (VBSP deletes a prop_static without it)
	bool collision = false;    ///< Has a collision model (.phy); a prop_physics without one is removed at spawn
	bool door = false;         ///< Made for prop_door_rotating (see isDoorPropModel)
	bool operator==(const PropModelFlags&) const = default;
};

/// Whether a model is made for prop_door_rotating: it has the "door_options" the door reads from
/// the model's $keyvalues, or a door name with a handle/hardware body group.
[[nodiscard]] bool isDoorPropModel(std::string_view modelPath, const ModelInfo& info);

/// The flags of a model in the game (nullopt if it is missing or not a readable .mdl).
[[nodiscard]] std::optional<PropModelFlags> readPropModelFlags(const GameFileSystem& fs, std::string_view modelPath);

/// The class a model is made for: prop_door_rotating for door models, prop_static for
/// $staticprop models, prop_physics for models with a collision model, prop_dynamic for the rest.
[[nodiscard]] std::string recommendedPropClass(const PropModelFlags& flags);

/// The class that fits better than `classname` (prop_static / prop_physics), or empty when the
/// class works with the model: a prop_static needs $staticprop, a prop_physics a collision model,
/// and a door model as prop_physics falls over (prop_door_rotating). The other prop classes and
/// non-props are always fine.
[[nodiscard]] std::string betterPropClass(std::string_view classname, const PropModelFlags& flags);

/// Sound names from all sound scripts listed in scripts/game_sounds_manifest.txt (sorted, unique).
[[nodiscard]] std::vector<std::string> soundScriptNames(const GameFileSystem& fs);

/// One entry of a sound script ("Weapon_Pistol.Single" { "wave" "weapons/pistol/pistol_fire2.wav" }).
struct SoundScriptEntry {
	std::string name;
	std::string script;                 ///< File it came from ("scripts/game_sounds_weapons.txt")
	std::string channel;                ///< CHAN_WEAPON, ...
	std::string volume;                 ///< "1", "0.7", "0.5,0.8" (as written; empty = engine default)
	std::string pitch;                  ///< "100", "PITCH_NORM", "95,105"
	std::string soundlevel;             ///< "SNDLVL_75dB", "SNDLVL_NORM"
	std::vector<std::string> waves;     ///< Files below sound/ (sound characters like ")" removed)
};

/// Every sound script entry (later scripts override earlier ones with the same name, as in the engine).
[[nodiscard]] std::vector<SoundScriptEntry> soundScripts(const GameFileSystem& fs);

/// One entry in the engine's format ("wave", or an "rndwave" block for several files).
[[nodiscard]] std::string writeSoundScriptEntry(const SoundScriptEntry& entry);
/// The script text with `entry` replaced (same name) or added at the end. Throws on broken syntax.
[[nodiscard]] std::string upsertSoundScriptEntry(std::string_view scriptText, const SoundScriptEntry& entry);
/// game_sounds_manifest.txt text that also precaches `file` ("scripts/game_sounds_mymod.txt").
[[nodiscard]] std::string addToSoundManifest(std::string_view manifestText, std::string_view file);

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

/// Names for the worldspawn "skyname" key: every materials/skybox/<name>up.vmt that also has its
/// front face (<name>ft.vmt), sorted ("sky_day01_01", "sky_day01_01_hdr", ...).
[[nodiscard]] std::vector<std::string> skyboxNames(const GameFileSystem& fs);

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

/// The file a sound name plays: a wave name ("ambient/hum.wav", sound characters allowed) or the
/// first wave of a sound script entry from `scripts` (may be null). Path below sound/, empty if unknown.
[[nodiscard]] std::string soundFileOf(std::string_view sound, const std::vector<SoundScriptEntry>* scripts);
/// Whether the sound loops in the engine (cue point or sampler loop in the WAV, see
/// soundFileLoops); nullopt if the file is missing or unknown.
[[nodiscard]] std::optional<bool> soundLoops(const GameFileSystem& fs, std::string_view sound, const std::vector<SoundScriptEntry>* scripts);

/// ambient_generic spawnflag "Is NOT Looped": the entity treats its sound as playing once.
constexpr int AMBIENT_NOT_LOOPED = 32;

/// Levenshtein distance (case-insensitive), used for "did you mean" fixes.
[[nodiscard]] std::size_t editDistance(std::string_view a, std::string_view b);

/// Closest candidate to `name`, or empty if none is close enough.
[[nodiscard]] std::string closestMatch(std::string_view name, const std::vector<std::string>& candidates);

} // namespace sme
