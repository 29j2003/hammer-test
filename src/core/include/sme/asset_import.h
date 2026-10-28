// asset_import.h - Bringing the mapper's own files into the mod: sounds (WAV/MP3/Ogg/FLAC become
// a game-ready 16-bit WAV, optionally with a sound script entry), images (PNG/JPG/TGA/BMP become a
// VTF and a VMT), particle files (.pcf plus the particle manifest). Models are in model_import.h.
//
// Every import first names its targets (so the UI can ask before overwriting anything), then
// writes and returns an ImportRecord: the files it created and the lines it added to shared files
// (sound script, manifests). undoImport() takes exactly that back again. The records are kept in
// the mod (cfg/mapsmith_imports.txt), so an import can still be removed after a restart.
//
// Shared files the game already has (scripts/game_sounds_manifest.txt,
// particles/particles_manifest.txt) are never replaced by a short one: when the mod has none yet,
// the game's text (what the file system reads now) is copied first and the new line added to it,
// otherwise every sound or particle of the game would be gone.

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/audio.h>
#include <sme/texture.h>

namespace sme {

/// A file name part for the game: lower case, letters, digits, '_' and '-' ("My Hum (2).wav" ->
/// "my_hum_2"). Empty if nothing usable is left.
[[nodiscard]] std::string importName(std::string_view text);
/// A folder below sound/, materials/ ...: each part cleaned like importName, joined by '/'
/// ("Custom/My Stuff" -> "custom/my_stuff"); never "..", never absolute.
[[nodiscard]] std::string importFolder(std::string_view text);

/// A line an import added to a shared text file, so it can be taken out again.
struct ImportEntry {
	enum class Kind {
		SoundScript,   ///< A block named `value` in a sound script file
		Manifest,      ///< A ""file"" / ""precache_file"" line with the value `value` in a manifest
	};
	Kind kind = Kind::Manifest;
	std::filesystem::path file;
	std::string value;
	bool operator==(const ImportEntry&) const = default;
};

/// What one import changed in the mod.
struct ImportRecord {
	std::string kind;                              ///< "sound", "material", "model", "particles"
	std::string asset;                             ///< What to use in the editor ("custom/hum.wav", "custom/brick", "models/custom/crate.mdl", "particles/fire.pcf")
	std::vector<std::filesystem::path> created;    ///< Files that did not exist before
	std::vector<std::filesystem::path> replaced;   ///< Files overwritten on request; the old one waits as <file>.mapsmith-backup
	std::vector<ImportEntry> entries;              ///< Lines added to shared files
	bool operator==(const ImportRecord&) const = default;
};

/// Removes the created files and the added lines (a sound script file left without entries goes
/// too) and puts replaced files back from their backups. Missing files are fine; false (with
/// `error`) only if a file could not be changed.
bool undoImport(const ImportRecord& record, std::string* error = nullptr);

/// The list of imports kept in the mod, as text and back (paths relative to the mod folder).
[[nodiscard]] std::string writeImportLog(const std::vector<ImportRecord>& records, const std::filesystem::path& modDir);
[[nodiscard]] std::vector<ImportRecord> readImportLog(std::string_view text, const std::filesystem::path& modDir);
/// <mod>/cfg/mapsmith_imports.txt
[[nodiscard]] std::filesystem::path importLogFile(const std::filesystem::path& modDir);
/// Adds a record to the mod's list / removes one (the newest equal one). False if the file could not be written.
bool rememberImport(const std::filesystem::path& modDir, const ImportRecord& record);
bool forgetImport(const std::filesystem::path& modDir, const ImportRecord& record);
[[nodiscard]] std::vector<ImportRecord> loadImports(const std::filesystem::path& modDir);

/// A manifest text with the line `"<key>" "<value>"` added before the closing brace of its first
/// block (`key` "file" for particles, "precache_file" for sounds). An empty text becomes a new
/// manifest called `root`. Unchanged if the value is already listed (any case, either key).
[[nodiscard]] std::string withManifestEntry(std::string_view text, std::string_view root, std::string_view key, std::string_view value);
/// The text without lines whose value is `value` (case-insensitive).
[[nodiscard]] std::string withoutManifestEntry(std::string_view text, std::string_view value);

// --- Sounds ----------------------------------------------------------------------------

/// How a sound comes into the mod.
struct SoundImportOptions {
	std::string folder = "custom";     ///< Below sound/
	std::string name;                  ///< File name without .wav
	int sampleRate = 44100;            ///< 44100, 22050 or 11025; 0 keeps the file's rate when Source plays it (else 44100)
	enum class Channels { Keep, Mono, Stereo };
	Channels channels = Channels::Keep;
	bool normalize = false;            ///< Loudest peak to -1 dB
	bool loop = false;                 ///< Loops in the game (cue point), for ambient sounds
	std::size_t loopStart = 0;         ///< Frame (of the result) the loop starts again at
	// Sound script entry (optional): a name like "Custom.Hum" maps and code can play.
	bool script = false;
	std::string scriptName;
	double volume = 1.0;               ///< 0..1
	int pitch = 100;                   ///< 100 = as recorded
	std::string channel = "CHAN_AUTO";
	std::string soundLevel = "SNDLVL_NORM";
};

/// The sound as the game gets it: sample rate, channels, peak level and loop point as chosen.
[[nodiscard]] DecodedAudio prepareImportedSound(const DecodedAudio& audio, const SoundImportOptions& options);
/// One sound script block for the options, playing `wave` (path below sound/).
[[nodiscard]] std::string soundScriptEntry(const SoundImportOptions& options, std::string_view wave);
/// The sound script file the importer writes (<mod>/scripts/game_sounds_mapsmith.txt).
[[nodiscard]] std::filesystem::path importSoundScriptFile(const std::filesystem::path& modDir);
/// The WAV the import writes.
[[nodiscard]] std::filesystem::path soundImportTarget(const std::filesystem::path& modDir, const SoundImportOptions& options);
/// Writes the WAV and (with options.script) the script entry; the script file is added to the
/// mod's scripts/game_sounds_manifest.txt, which starts from `gameManifest` (the game's text) when
/// the mod has none. With `overwrite` false an existing WAV or script name stops the import.
[[nodiscard]] std::optional<ImportRecord> importSound(const std::filesystem::path& modDir, const DecodedAudio& audio, const SoundImportOptions& options,
                                                      std::string_view gameManifest, bool overwrite, std::string* error = nullptr);

// --- Materials -------------------------------------------------------------------------

/// How a picture becomes a material.
struct MaterialImportOptions {
	std::string folder = "custom";     ///< Below materials/ (model materials: below materials/models/)
	std::string name;
	enum class Use { Brush, Model };
	Use use = Use::Brush;              ///< LightmappedGeneric for brushes, VertexLitGeneric for models
	std::string surfaceProp = "default";
	enum class Alpha { Auto, None, Translucent, AlphaTest };
	Alpha alpha = Alpha::Auto;         ///< Auto: cut-out for 0/255 alpha, see-through for soft alpha
	unsigned maxSize = 2048;           ///< Longer side at most (power of two, the engine needs it)
	bool compress = true;              ///< DXT1/DXT5; off: BGRA8888 (sharper, 4-8x larger)
};

/// The surface properties offered for imported materials (the game's common ones).
[[nodiscard]] const std::vector<std::string>& commonSurfaceProps();

/// Decodes a picture file (PNG, JPG, TGA, BMP, PSD, GIF) to RGBA. nullopt for anything else.
[[nodiscard]] std::optional<ImageRgba> decodeImageFile(std::span<const std::byte> data, std::string* error = nullptr);
/// What the alpha channel holds: nothing (all opaque), only 0/255 (cut-out) or soft values.
enum class ImageAlpha { Opaque, Binary, Soft };
[[nodiscard]] ImageAlpha imageAlpha(const ImageRgba& image);
/// The alpha handling the import uses for the options and the picture.
[[nodiscard]] MaterialImportOptions::Alpha effectiveAlpha(const MaterialImportOptions& options, const ImageRgba& image);
/// A VTF of the picture: power-of-two size (at most maxSize), mipmaps, DXT1/DXT5 or BGRA8888;
/// `normalMap` marks it as a bump map (no compression artefacts: BGRA8888, NORMAL flag).
[[nodiscard]] std::optional<std::vector<std::byte>> encodeVtf(const ImageRgba& image, bool compress, bool normalMap, unsigned maxSize,
                                                              std::string* error = nullptr);
/// The material name the import makes ("custom/brick", "models/custom/crate").
[[nodiscard]] std::string materialImportName(const MaterialImportOptions& options);
/// The VMT text for an imported material (`hasNormalMap`: a <name>_normal texture next to it).
[[nodiscard]] std::string importedMaterialVmt(const MaterialImportOptions& options, MaterialImportOptions::Alpha alpha, bool hasNormalMap);
/// The files the import writes (VMT, VTF, normal-map VTF if there is one).
[[nodiscard]] std::vector<std::filesystem::path> materialImportTargets(const std::filesystem::path& modDir, const MaterialImportOptions& options, bool hasNormalMap);
/// Writes the VTF(s) and the VMT. With `overwrite` false an existing file stops the import.
[[nodiscard]] std::optional<ImportRecord> importMaterial(const std::filesystem::path& modDir, const ImageRgba& color, const ImageRgba* normalMap,
                                                         const MaterialImportOptions& options, bool overwrite, std::string* error = nullptr);

// --- Particles -------------------------------------------------------------------------

/// The .pcf the import writes (<mod>/particles/<name>.pcf).
[[nodiscard]] std::filesystem::path particleImportTarget(const std::filesystem::path& modDir, std::string_view name);
/// Copies a particle file into the mod and lists it in particles/particles_manifest.txt (made from
/// `gameManifest` when the mod has none); `precache` loads it with every map ("!" in front).
/// Rejects files that are not particle files (no particle systems in them).
[[nodiscard]] std::optional<ImportRecord> importParticles(const std::filesystem::path& modDir, std::span<const std::byte> pcf, std::string_view name,
                                                          std::string_view gameManifest, bool precache, bool overwrite, std::string* error = nullptr);

} // namespace sme
