// soundscape.h - Soundscape scripts (scripts/soundscapes_*.txt) as the editor edits them:
// every soundscape with its DSP, fade time and rules (looping sound, random sounds, another
// soundscape), read and written back in the engine's format. Keys the editor does not know stay
// as they were. Saving goes into the mod's own file, listed in scripts/soundscapes_manifest.txt.

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/keyvalues.h>

namespace sme {

/// One rule of a soundscape. Numbers stay text as written ("0.5,0.8" is a random range).
struct SoundscapeRule {
	enum class Kind { Looping, Random, Soundscape, Other };

	Kind kind = Kind::Looping;
	std::string key;                    ///< Block name as written ("playlooping"; for Other the unknown name)
	std::string volume, pitch, soundlevel, position;
	std::string time;                   ///< Random: seconds between sounds ("5,10")
	std::string wave;                   ///< Looping: the file below sound/
	std::vector<std::string> waves;     ///< Random: the files it picks from (rndwave)
	std::string name;                   ///< Soundscape: the soundscape it plays
	std::string positionOverride;       ///< Soundscape: all its sounds come from this position
	std::string ambientPositionOverride;
	std::vector<KvNode> extra;          ///< Keys and blocks not known above, kept as they were
};

/// One soundscape of a script file.
struct SoundscapeScript {
	std::string name;                   ///< "d1_canals.util_drips"
	std::string dsp;                    ///< Room effect number ("1"); empty = not set
	std::string fadetime;               ///< Seconds to fade in when it starts; empty = engine default
	std::vector<SoundscapeRule> rules;
	std::vector<KvNode> extra;          ///< Other keys of the soundscape ("dsp_volume", ...)
};

/// Every soundscape in a script file, in order. Throws std::runtime_error on broken syntax.
[[nodiscard]] std::vector<SoundscapeScript> parseSoundscapes(std::string_view text);
/// One soundscape in the engine's format.
[[nodiscard]] std::string writeSoundscape(const SoundscapeScript& scape);
/// The file text with `scape` replacing the one of the same name (case-insensitive) or added at
/// the end; the other soundscapes keep their content. Throws on broken syntax.
[[nodiscard]] std::string upsertSoundscape(std::string_view text, const SoundscapeScript& scape);
/// The file text without the soundscape `name`. Throws on broken syntax.
[[nodiscard]] std::string removeSoundscape(std::string_view text, std::string_view name);
/// soundscapes_manifest.txt text that also loads `file`. `first`: before the others, so that a
/// soundscape in it with the name of one in the game's files is the one that is used.
[[nodiscard]] std::string addToSoundscapeManifest(std::string_view manifestText, std::string_view file, bool first = true);

/// The room effects (dsp numbers) with their names from the engine's dsp_presets.
struct DspPreset {
	int id;
	const char* name;
};
/// All known room effects, by number.
[[nodiscard]] std::span<const DspPreset> dspPresets();

} // namespace sme
