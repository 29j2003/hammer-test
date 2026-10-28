// sound_edit.cpp - Editing decoded sounds and writing WAV files (see sound_edit.h).

#include <sme/sound_edit.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sme {

namespace {

std::int16_t clip(double value) {
	return static_cast<std::int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
}

/// Calls `apply(sampleIndex, framePositionInRange)` for every sample of the range.
template <typename Fn>
void forEachSample(DecodedAudio& audio, FrameRange range, Fn&& apply) {
	const auto r = effectiveRange(audio, range);
	const auto channels = static_cast<std::size_t>(std::max(1, audio.channels));
	for (std::size_t frame = r.begin; frame < r.end; ++frame) {
		for (std::size_t c = 0; c < channels; ++c) {
			apply(frame * channels + c, frame - r.begin, r.size());
		}
	}
}

/// Resamples by reading the input at `step` frames per output frame (band-limited when step > 1).
DecodedAudio resampleBy(const DecodedAudio& audio, double step, int outRate) {
	DecodedAudio out;
	out.sampleRate = outRate;
	out.channels = audio.channels;
	out.format = "PCM 16 bit";
	const std::size_t inFrames = audio.frames();
	if (inFrames == 0 || audio.channels <= 0 || step <= 0) {
		return out;
	}
	const auto channels = static_cast<std::size_t>(audio.channels);
	const auto outFrames = static_cast<std::size_t>(std::floor(double(inFrames) / step));
	out.samples.resize(outFrames * channels);
	// Cut-off relative to the input's Nyquist frequency: lower when reading faster than 1:1.
	const double cutoff = step > 1 ? 1.0 / step : 1.0;
	constexpr int TAPS = 16;   // per side
	const int reach = static_cast<int>(std::ceil(TAPS / cutoff));
	const auto sinc = [](double x) { return std::abs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x); };
	std::vector<double> weights;
	for (std::size_t o = 0; o < outFrames; ++o) {
		const double t = double(o) * step;
		const auto center = static_cast<long long>(std::floor(t));
		weights.clear();
		double sum = 0;
		for (long long k = center - reach + 1; k <= center + reach; ++k) {
			const double x = (t - double(k)) * cutoff;
			if (std::abs(x) >= TAPS) {
				weights.push_back(0);
				continue;
			}
			// Blackman window over the filter's length.
			const double w = 0.42 + 0.5 * std::cos(std::numbers::pi * x / TAPS) + 0.08 * std::cos(2 * std::numbers::pi * x / TAPS);
			const double weight = sinc(x) * w;
			weights.push_back(weight);
			sum += weight;
		}
		if (std::abs(sum) < 1e-12) sum = 1;
		for (std::size_t c = 0; c < channels; ++c) {
			double value = 0;
			std::size_t i = 0;
			for (long long k = center - reach + 1; k <= center + reach; ++k, ++i) {
				if (weights[i] == 0) continue;
				// Mirror at the ends instead of reading silence (no clicks at the edges).
				long long at = k < 0 ? -k : k;
				if (at >= static_cast<long long>(inFrames)) at = 2 * static_cast<long long>(inFrames) - 2 - at;
				at = std::clamp<long long>(at, 0, static_cast<long long>(inFrames) - 1);
				value += weights[i] * audio.samples[static_cast<std::size_t>(at) * channels + c];
			}
			out.samples[o * channels + c] = clip(value / sum);
		}
	}
	if (audio.loopStart) {
		out.loopStart = std::min<std::size_t>(outFrames > 0 ? outFrames - 1 : 0, static_cast<std::size_t>(std::llround(double(*audio.loopStart) / step)));
	}
	return out;
}

void put(std::vector<std::byte>& out, std::uint32_t value, int bytes) {
	for (int i = 0; i < bytes; ++i) {
		out.push_back(std::byte((value >> (8 * i)) & 0xff));
	}
}

void tag(std::vector<std::byte>& out, const char* name) {
	for (int i = 0; i < 4; ++i) {
		out.push_back(std::byte(name[i]));
	}
}

} // namespace

FrameRange effectiveRange(const DecodedAudio& audio, FrameRange range) {
	const std::size_t frames = audio.frames();
	if (range.empty()) {
		return {0, frames};
	}
	return {std::min(range.begin, frames), std::min(range.end, frames)};
}

DecodedAudio trimmed(const DecodedAudio& audio, FrameRange range) {
	const auto r = effectiveRange(audio, range);
	const auto channels = static_cast<std::size_t>(std::max(1, audio.channels));
	DecodedAudio out = audio;
	out.samples.assign(audio.samples.begin() + std::ptrdiff_t(r.begin * channels), audio.samples.begin() + std::ptrdiff_t(r.end * channels));
	out.loopStart.reset();
	if (audio.loopStart && *audio.loopStart >= r.begin && *audio.loopStart < r.end) {
		out.loopStart = *audio.loopStart - r.begin;
	}
	return out;
}

DecodedAudio cut(const DecodedAudio& audio, FrameRange range) {
	if (range.empty()) {
		return audio;
	}
	const auto r = effectiveRange(audio, range);
	const auto channels = static_cast<std::size_t>(std::max(1, audio.channels));
	DecodedAudio out = audio;
	out.samples.erase(out.samples.begin() + std::ptrdiff_t(r.begin * channels), out.samples.begin() + std::ptrdiff_t(r.end * channels));
	if (audio.loopStart) {
		const std::size_t loop = *audio.loopStart;
		if (loop >= r.end) {
			out.loopStart = loop - r.size();
		} else if (loop >= r.begin) {
			out.loopStart = r.begin < out.frames() ? std::optional<std::size_t>(r.begin) : std::nullopt;
		}
	}
	return out;
}

void silence(DecodedAudio& audio, FrameRange range) {
	forEachSample(audio, range, [&](std::size_t i, std::size_t, std::size_t) { audio.samples[i] = 0; });
}

void fadeIn(DecodedAudio& audio, FrameRange range) {
	forEachSample(audio, range, [&](std::size_t i, std::size_t at, std::size_t size) {
		const double t = size > 1 ? double(at) / double(size - 1) : 1.0;
		audio.samples[i] = clip(audio.samples[i] * t);
	});
}

void fadeOut(DecodedAudio& audio, FrameRange range) {
	forEachSample(audio, range, [&](std::size_t i, std::size_t at, std::size_t size) {
		const double t = size > 1 ? 1.0 - double(at) / double(size - 1) : 0.0;
		audio.samples[i] = clip(audio.samples[i] * t);
	});
}

void applyGain(DecodedAudio& audio, double decibels, FrameRange range) {
	const double factor = std::pow(10.0, decibels / 20.0);
	forEachSample(audio, range, [&](std::size_t i, std::size_t, std::size_t) { audio.samples[i] = clip(audio.samples[i] * factor); });
}

double peakLevel(const DecodedAudio& audio, FrameRange range) {
	const auto r = effectiveRange(audio, range);
	const auto channels = static_cast<std::size_t>(std::max(1, audio.channels));
	int peak = 0;
	for (std::size_t i = r.begin * channels; i < r.end * channels; ++i) {
		peak = std::max(peak, std::abs(int(audio.samples[i])));
	}
	return std::min(1.0, peak / 32767.0);
}

void normalize(DecodedAudio& audio, double peakDecibels, FrameRange range) {
	const double peak = peakLevel(audio, range);
	if (peak <= 0) {
		return;
	}
	const double target = std::pow(10.0, std::min(0.0, peakDecibels) / 20.0);
	applyGain(audio, 20.0 * std::log10(target / peak), range);
}

void reverse(DecodedAudio& audio, FrameRange range) {
	const auto r = effectiveRange(audio, range);
	const auto channels = static_cast<std::size_t>(std::max(1, audio.channels));
	if (r.size() < 2) {
		return;
	}
	for (std::size_t a = r.begin, b = r.end - 1; a < b; ++a, --b) {
		for (std::size_t c = 0; c < channels; ++c) {
			std::swap(audio.samples[a * channels + c], audio.samples[b * channels + c]);
		}
	}
}

DecodedAudio resampled(const DecodedAudio& audio, int sampleRate) {
	if (sampleRate <= 0 || audio.sampleRate <= 0) {
		return audio;
	}
	if (sampleRate == audio.sampleRate) {
		DecodedAudio out = audio;
		out.format = "PCM 16 bit";
		return out;
	}
	return resampleBy(audio, double(audio.sampleRate) / sampleRate, sampleRate);
}

DecodedAudio speedChanged(const DecodedAudio& audio, double factor) {
	if (factor <= 0 || std::abs(factor - 1.0) < 1e-9) {
		return audio;
	}
	return resampleBy(audio, factor, audio.sampleRate);
}

DecodedAudio toMono(const DecodedAudio& audio) {
	if (audio.channels <= 1) {
		return audio;
	}
	DecodedAudio out = audio;
	out.channels = 1;
	const auto channels = static_cast<std::size_t>(audio.channels);
	const std::size_t frames = audio.frames();
	out.samples.resize(frames);
	for (std::size_t f = 0; f < frames; ++f) {
		long sum = 0;
		for (std::size_t c = 0; c < channels; ++c) sum += audio.samples[f * channels + c];
		out.samples[f] = clip(double(sum) / double(channels));
	}
	return out;
}

std::vector<std::byte> encodeWav(const DecodedAudio& audio) {
	const auto channels = static_cast<std::uint32_t>(std::max(1, audio.channels));
	const auto rate = static_cast<std::uint32_t>(std::max(1, audio.sampleRate));
	const auto frames = static_cast<std::uint32_t>(audio.frames());
	std::vector<std::byte> out;
	out.reserve(audio.samples.size() * 2 + 128);
	tag(out, "RIFF");
	put(out, 0, 4);   // size, filled in at the end
	tag(out, "WAVE");
	tag(out, "fmt ");
	put(out, 16, 4);
	put(out, 1, 2);   // PCM
	put(out, channels, 2);
	put(out, rate, 4);
	put(out, rate * channels * 2, 4);
	put(out, channels * 2, 2);
	put(out, 16, 2);
	tag(out, "data");
	put(out, static_cast<std::uint32_t>(audio.samples.size() * 2), 4);
	for (const auto sample : audio.samples) {
		put(out, static_cast<std::uint16_t>(sample), 2);
	}
	if (audio.loopStart && *audio.loopStart < frames) {
		const auto loop = static_cast<std::uint32_t>(*audio.loopStart);
		// One cue point: Source loops from here to the end.
		tag(out, "cue ");
		put(out, 4 + 24, 4);
		put(out, 1, 4);
		put(out, 1, 4);              // id
		put(out, loop, 4);           // position in the playlist
		tag(out, "data");
		put(out, 0, 4);              // chunk start
		put(out, 0, 4);              // block start
		put(out, loop, 4);           // sample offset
		// Sampler loop (forward) from the cue point to the last frame.
		tag(out, "smpl");
		put(out, 36 + 24, 4);
		put(out, 0, 4);              // manufacturer
		put(out, 0, 4);              // product
		put(out, static_cast<std::uint32_t>(1'000'000'000.0 / rate), 4);   // sample period in ns
		put(out, 60, 4);             // MIDI unity note
		put(out, 0, 4);              // pitch fraction
		put(out, 0, 4);              // SMPTE format
		put(out, 0, 4);              // SMPTE offset
		put(out, 1, 4);              // loops
		put(out, 0, 4);              // sampler data
		put(out, 1, 4);              // cue point id
		put(out, 0, 4);              // type: forward
		put(out, loop, 4);
		put(out, frames > 0 ? frames - 1 : 0, 4);
		put(out, 0, 4);              // fraction
		put(out, 0, 4);              // play count: endless
	}
	if (out.size() & 1) {
		out.push_back(std::byte{0});
	}
	const auto size = static_cast<std::uint32_t>(out.size() - 8);
	for (int i = 0; i < 4; ++i) {
		out[std::size_t(4 + i)] = std::byte((size >> (8 * i)) & 0xff);
	}
	return out;
}

namespace {

/// Writes bits most significant first (FLAC's order).
class BitWriter {
public:
	void bits(std::uint64_t value, int count) {
		for (int i = count - 1; i >= 0; --i) {
			m_byte = std::uint8_t((m_byte << 1) | ((value >> i) & 1));
			if (++m_used == 8) {
				m_out.push_back(std::byte(m_byte));
				m_byte = 0;
				m_used = 0;
			}
		}
	}
	void align() {
		if (m_used > 0) bits(0, 8 - m_used);
	}
	std::vector<std::byte>& data() { return m_out; }

private:
	std::vector<std::byte> m_out;
	std::uint8_t m_byte = 0;
	int m_used = 0;
};

std::uint8_t crc8(const std::byte* data, std::size_t size) {
	std::uint8_t crc = 0;
	for (std::size_t i = 0; i < size; ++i) {
		crc ^= std::uint8_t(data[i]);
		for (int k = 0; k < 8; ++k) crc = std::uint8_t((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
	}
	return crc;
}

std::uint16_t crc16(const std::byte* data, std::size_t size) {
	std::uint16_t crc = 0;
	for (std::size_t i = 0; i < size; ++i) {
		crc ^= std::uint16_t(std::uint16_t(data[i]) << 8);
		for (int k = 0; k < 8; ++k) crc = std::uint16_t((crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1);
	}
	return crc;
}

} // namespace

std::vector<std::byte> encodeFlac(const DecodedAudio& audio) {
	constexpr std::uint32_t BLOCK = 4096;
	const int channels = std::clamp(audio.channels, 1, 8);
	const auto rate = static_cast<std::uint32_t>(std::clamp(audio.sampleRate, 1, 655350));
	const std::size_t frames = audio.samples.size() / std::size_t(channels);
	BitWriter w;
	w.bits(0x664C6143, 32);   // "fLaC"
	// STREAMINFO, the only (last) metadata block.
	w.bits(1, 1);
	w.bits(0, 7);
	w.bits(34, 24);
	w.bits(BLOCK, 16);
	w.bits(BLOCK, 16);
	w.bits(0, 24);   // frame sizes unknown
	w.bits(0, 24);
	w.bits(rate, 20);
	w.bits(std::uint64_t(channels - 1), 3);
	w.bits(15, 5);   // 16 bits per sample
	w.bits(frames, 36);
	for (int i = 0; i < 16; ++i) w.bits(0, 8);   // MD5 unknown
	std::uint64_t frameNumber = 0;
	for (std::size_t start = 0; start < frames; start += BLOCK, ++frameNumber) {
		const auto count = std::uint32_t(std::min<std::size_t>(BLOCK, frames - start));
		const std::size_t frameStart = w.data().size();
		w.bits(0x3FFE, 14);   // sync
		w.bits(0, 1);
		w.bits(0, 1);         // fixed block size
		w.bits(7, 4);         // block size: 16 bits at the end of the header
		w.bits(0, 4);         // sample rate from STREAMINFO
		w.bits(std::uint64_t(channels - 1), 4);   // independent channels
		w.bits(4, 3);         // 16 bits per sample
		w.bits(0, 1);
		// Frame number, UTF-8 coded.
		if (frameNumber < 0x80) {
			w.bits(frameNumber, 8);
		} else {
			int extra = 1;
			while (extra < 6 && frameNumber >= (1ull << (6 + 5 * extra))) ++extra;
			w.bits(std::uint64_t((0xFFu << (7 - extra)) & 0xFFu) | (frameNumber >> (6 * extra)), 8);
			for (int k = extra - 1; k >= 0; --k) w.bits(0x80 | ((frameNumber >> (6 * k)) & 0x3F), 8);
		}
		w.bits(count - 1, 16);
		w.bits(crc8(w.data().data() + frameStart, w.data().size() - frameStart), 8);
		for (int c = 0; c < channels; ++c) {
			w.bits(0, 1);
			w.bits(1, 6);   // VERBATIM
			w.bits(0, 1);
			for (std::uint32_t i = 0; i < count; ++i) w.bits(std::uint16_t(audio.samples[(start + i) * std::size_t(channels) + std::size_t(c)]), 16);
		}
		w.align();
		w.bits(crc16(w.data().data() + frameStart, w.data().size() - frameStart), 16);
	}
	return std::move(w.data());
}

bool isSourceSampleRate(int sampleRate) {
	return sampleRate == 44100 || sampleRate == 22050 || sampleRate == 11025;
}

} // namespace sme
