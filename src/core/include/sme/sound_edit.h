// sound_edit.h - Editing decoded sounds for the Sounds page: trim, cut, silence, fades, gain,
// normalize, reverse, speed (pitch), resampling, mono, a loop point - and writing the result as a
// 16-bit PCM WAV the Source engine plays (with a "cue " point and a "smpl" loop when it loops).
//
// Every operation works on a range of frames (one sample per channel); an empty range means the
// whole sound. The functions are pure: they never touch files.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <sme/audio.h>

namespace sme {

/// Frames [begin, end) of a sound. Empty (begin == end) stands for the whole sound.
struct FrameRange {
	std::size_t begin = 0;
	std::size_t end = 0;

	[[nodiscard]] bool empty() const { return end <= begin; }
	[[nodiscard]] std::size_t size() const { return empty() ? 0 : end - begin; }
};

/// `range` clamped to the sound; an empty range becomes the whole sound.
[[nodiscard]] FrameRange effectiveRange(const DecodedAudio& audio, FrameRange range);

/// Only the frames in `range` (the loop point moves along or is dropped when it falls outside).
[[nodiscard]] DecodedAudio trimmed(const DecodedAudio& audio, FrameRange range);
/// Everything except the frames in `range` (an empty range cuts nothing).
[[nodiscard]] DecodedAudio cut(const DecodedAudio& audio, FrameRange range);
/// Sets the frames in `range` to zero.
void silence(DecodedAudio& audio, FrameRange range);
/// Fades from silent to full volume over `range` / from full volume to silent.
void fadeIn(DecodedAudio& audio, FrameRange range);
void fadeOut(DecodedAudio& audio, FrameRange range);
/// Changes the loudness of `range` by `decibels` (clipped at full scale).
void applyGain(DecodedAudio& audio, double decibels, FrameRange range = {});
/// Loudest sample of `range` as 0..1 of full scale.
[[nodiscard]] double peakLevel(const DecodedAudio& audio, FrameRange range = {});
/// Makes the loudest sample of `range` reach `peakDecibels` (e.g. -1 dB). Silence stays silent.
void normalize(DecodedAudio& audio, double peakDecibels = -1.0, FrameRange range = {});
/// Plays `range` backwards.
void reverse(DecodedAudio& audio, FrameRange range = {});
/// Same sound at another sample rate (same length in seconds). Windowed-sinc interpolation that also
/// filters out what the lower rate cannot hold, so going down does not add aliasing hiss.
[[nodiscard]] DecodedAudio resampled(const DecodedAudio& audio, int sampleRate);
/// Faster and higher (factor > 1) or slower and lower (< 1), like a tape; keeps the sample rate.
[[nodiscard]] DecodedAudio speedChanged(const DecodedAudio& audio, double factor);
/// All channels mixed into one.
[[nodiscard]] DecodedAudio toMono(const DecodedAudio& audio);

/// A 16-bit PCM WAV of `audio`. With a loop point the file gets a "cue " chunk (what Source looks
/// for) and a "smpl" loop from there to the end (what sound editors show).
[[nodiscard]] std::vector<std::byte> encodeWav(const DecodedAudio& audio);

/// Sample rates the Source engine plays without problems.
[[nodiscard]] bool isSourceSampleRate(int sampleRate);

} // namespace sme
