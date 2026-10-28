// audio.h - Decodes the sound files of Source games for the sound preview: WAV (PCM 8/16/24/32
// bit, float, Microsoft ADPCM, IMA ADPCM, MP3 inside WAV), MP3 and Ogg Vorbis. The result is
// 16-bit PCM plus what the editor shows about the file (loop point, length, format).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sme {

/// A sound decoded to 16-bit PCM, ready to play or draw as a waveform.
struct DecodedAudio {
	int sampleRate = 0;
	int channels = 0;
	std::vector<std::int16_t> samples;   ///< Interleaved
	std::string format;                  ///< "PCM 16 bit", "MS ADPCM", "MP3", ...
	std::optional<std::size_t> loopStart; ///< Frame where a looping sound starts again (cue/smpl chunk)

	/// Samples per channel.
	[[nodiscard]] std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
	/// Length in seconds.
	[[nodiscard]] double seconds() const { return sampleRate > 0 ? double(frames()) / sampleRate : 0.0; }
};

/// Decodes by content (RIFF/WAVE, Ogg, MP3 frames); `name` only helps to guess. Never throws.
[[nodiscard]] std::optional<DecodedAudio> decodeAudio(std::span<const std::byte> data, std::string_view name = {}, std::string* error = nullptr);

/// Whether the engine plays the file as a loop: a WAV with a cue point or a sampler loop
/// ("cue " / "smpl" chunk) loops, a WAV without them and an MP3 play once; nullopt for Ogg (Source 1
/// cannot play it). Reads only the chunk headers.
[[nodiscard]] std::optional<bool> soundFileLoops(std::span<const std::byte> data);

/// Waveform for drawing: per bucket the lowest and highest sample (-1..1) over all channels.
[[nodiscard]] std::vector<std::pair<float, float>> waveformPeaks(const DecodedAudio& audio, int buckets);

} // namespace sme
