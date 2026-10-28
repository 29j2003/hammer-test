// samples.h - The sample files Mapsmith ships, so the importers can be tried without files of
// one's own ("Use a sample ..." in the import dialogs, File > Import into the mod > Samples):
// sounds, pictures, a model, a rigged model with an animation, animations for the Half-Life 2
// people and a particle file. Everything is made here - synthesised sounds, drawn pictures,
// modelled meshes - so no game's files are copied. MP3 and Ogg Vorbis need encoders Mapsmith does
// not have: those two are made once from generated WAVs (tools/make_sample_media.sh) and kept in
// tests/samples_media; writeSamples copies them in.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <sme/audio.h>
#include <sme/texture.h>

namespace sme {

/// One sample: where it goes below the samples folder ("sounds/hum_loop.wav") and its bytes.
struct SampleFile {
	std::string path;
	std::vector<std::byte> data;
};

/// The folders of the samples folder, by what imports them.
inline constexpr const char* SAMPLE_SOUNDS = "sounds";
inline constexpr const char* SAMPLE_PICTURES = "pictures";
inline constexpr const char* SAMPLE_MODELS = "models";
inline constexpr const char* SAMPLE_ANIMATIONS = "animations";
inline constexpr const char* SAMPLE_PARTICLES = "particles";

/// A 4 second hum with a soft wind, a seamless loop (mono, 22050 Hz, loop point at the start).
[[nodiscard]] DecodedAudio sampleHumLoop();
/// A short wind gust loop for the Ogg sample (mono, 22050 Hz).
[[nodiscard]] DecodedAudio sampleWindLoop();
/// A short mechanical click (one shot, mono, 44100 Hz) - the source of the MP3 sample.
[[nodiscard]] DecodedAudio sampleClick();
/// A two-note door chime (stereo, 44100 Hz) - the FLAC sample.
[[nodiscard]] DecodedAudio sampleChime();

/// Square floor tiles with grout, seamless (512 x 512).
[[nodiscard]] ImageRgba sampleTiles();
/// Its normal map (tiles up, grout down; seamless).
[[nodiscard]] ImageRgba sampleTilesNormal();
/// A rusty grate with see-through holes (alpha 0 / 255; 256 x 256).
[[nodiscard]] ImageRgba sampleGrate();

/// A wooden crate as .obj text (metres, Y up like most 3D programs), its .mtl and picture.
[[nodiscard]] std::string sampleCrateObj();
[[nodiscard]] std::string sampleCrateMtl();
[[nodiscard]] ImageRgba sampleCrateTexture();

/// A hanging shop sign with two bones ("bracket", "board" hinged at the bracket), as a rigged
/// reference .smd (Source units and orientation), and its animations on that skeleton.
[[nodiscard]] std::string sampleSignSmd();
[[nodiscard]] std::string sampleSignSwingSmd();
[[nodiscard]] std::string sampleSignSpinSmd();
[[nodiscard]] ImageRgba sampleSignTexture();

/// Animations for Half-Life 2's people (the ValveBiped skeleton): only a few bones, relative to
/// the model's rest pose ("// mapsmith: relative"), so they fit any of the game's humans. Mapsmith
/// completes them for the chosen model (completedAnimationSmd).
[[nodiscard]] std::string sampleHumanNodSmd();
[[nodiscard]] std::string sampleHumanWaveSmd();

/// A .pcf with two systems, "mapsmith_sample_sparks" and "mapsmith_sample_smoke".
[[nodiscard]] std::vector<std::byte> sampleParticleFile();

/// Every sample made here (without the MP3 and Ogg files, see the file comment).
[[nodiscard]] std::vector<SampleFile> makeSamples();
/// The WAVs the MP3/Ogg samples are encoded from (for tools/make_sample_media.sh).
[[nodiscard]] std::vector<SampleFile> sampleMediaSources();
/// Writes the samples below `dir`, the files of `mediaDir` (MP3, Ogg) into its sounds folder and a
/// README.txt that says what each file is for. false (with `error`) if a file cannot be written.
bool writeSamples(const std::filesystem::path& dir, const std::filesystem::path& mediaDir, std::string* error = nullptr);
/// The text of that README.txt.
[[nodiscard]] std::string samplesReadme();

} // namespace sme
