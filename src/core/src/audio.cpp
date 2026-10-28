// audio.cpp - Sound decoding for the preview (see audio.h).

#include <sme/audio.h>

#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#include "minimp3.h"

namespace sme {

namespace {

constexpr std::size_t MAX_FRAMES = 48000 * 60 * 20;   // 20 minutes: anything longer is not a game sound

/// Little-endian reader with bounds checks (the files come from mods and VPKs).
class Bytes {
public:
	explicit Bytes(std::span<const std::byte> data) : m_data(data) {}
	[[nodiscard]] std::size_t size() const { return m_data.size(); }
	[[nodiscard]] bool has(std::size_t at, std::size_t n) const { return at <= m_data.size() && n <= m_data.size() - at; }
	[[nodiscard]] std::uint8_t u8(std::size_t at) const { return has(at, 1) ? std::to_integer<std::uint8_t>(m_data[at]) : 0; }
	[[nodiscard]] std::uint16_t u16(std::size_t at) const { return static_cast<std::uint16_t>(u8(at) | (u8(at + 1) << 8)); }
	[[nodiscard]] std::int16_t s16(std::size_t at) const { return static_cast<std::int16_t>(u16(at)); }
	[[nodiscard]] std::uint32_t u32(std::size_t at) const { return u16(at) | (std::uint32_t(u16(at + 2)) << 16); }
	[[nodiscard]] bool tag(std::size_t at, const char* four) const {
		return has(at, 4) && std::memcmp(m_data.data() + at, four, 4) == 0;
	}
	[[nodiscard]] std::span<const std::byte> sub(std::size_t at, std::size_t n) const {
		if (!has(at, 0)) return {};
		return m_data.subspan(at, std::min(n, m_data.size() - at));
	}

private:
	std::span<const std::byte> m_data;
};

std::int16_t clamp16(int v) {
	return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
}

bool fail(std::string* error, const char* text) {
	if (error) *error = text;
	return false;
}

// --- MP3 (minimp3) --------------------------------------------------------------------

bool decodeMp3(std::span<const std::byte> data, DecodedAudio& out, std::string* error) {
	mp3dec_t decoder;
	mp3dec_init(&decoder);
	std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm{};
	const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
	std::size_t at = 0;
	while (at < data.size()) {
		mp3dec_frame_info_t info{};
		const int frames = mp3dec_decode_frame(&decoder, bytes + at, static_cast<int>(std::min<std::size_t>(data.size() - at, 1 << 20)), pcm.data(), &info);
		if (info.frame_bytes <= 0) {
			break;   // nothing more that looks like MP3
		}
		at += static_cast<std::size_t>(info.frame_bytes);
		if (frames <= 0) {
			continue;   // ID3 tag or a frame needing more data
		}
		if (out.channels == 0) {
			out.channels = info.channels;
			out.sampleRate = info.hz;
		}
		if (info.channels != out.channels) {
			continue;
		}
		out.samples.insert(out.samples.end(), pcm.begin(), pcm.begin() + frames * info.channels);
		if (out.frames() > MAX_FRAMES) {
			break;
		}
	}
	if (out.samples.empty()) {
		return fail(error, "no MP3 frames found");
	}
	out.format = "MP3";
	return true;
}

// --- Ogg Vorbis (stb_vorbis) -------------------------------------------------------------

bool decodeOgg(std::span<const std::byte> data, DecodedAudio& out, std::string* error) {
	int channels = 0, rate = 0;
	short* pcm = nullptr;
	const int frames = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()), &channels, &rate, &pcm);
	if (frames <= 0 || !pcm || channels <= 0) {
		std::free(pcm);
		return fail(error, "not a readable Ogg Vorbis file");
	}
	out.channels = channels;
	out.sampleRate = rate;
	out.samples.assign(pcm, pcm + static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
	std::free(pcm);
	out.format = "Ogg Vorbis";
	return true;
}

// --- WAV ------------------------------------------------------------------------------

constexpr int MS_ADAPTATION[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
constexpr int MS_COEFS[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};

constexpr int IMA_STEPS[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,    31,
                               34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,
                               157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,   544,   598,   658,
                               724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
                               3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
                               15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int IMA_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct WavFormat {
	int tag = 0;
	int channels = 0;
	int rate = 0;
	int blockAlign = 0;
	int bits = 0;
	int samplesPerBlock = 0;
	std::vector<std::array<int, 2>> coefs;   ///< MS ADPCM predictors
};

bool decodeMsAdpcm(const WavFormat& f, const Bytes& in, std::size_t at, std::size_t end, DecodedAudio& out) {
	const int ch = f.channels;
	if (ch < 1 || ch > 2 || f.blockAlign < 7 * ch) return false;
	const auto& coefs = f.coefs.empty() ? std::vector<std::array<int, 2>>{} : f.coefs;
	const auto coef = [&](int index, int which) {
		if (!coefs.empty()) return index < static_cast<int>(coefs.size()) ? coefs[static_cast<std::size_t>(index)][static_cast<std::size_t>(which)] : 0;
		return index < 7 ? MS_COEFS[index][which] : 0;
	};
	const int perBlock = f.samplesPerBlock > 0 ? f.samplesPerBlock : (f.blockAlign - 7 * ch) * 2 / ch + 2;
	for (std::size_t block = at; block + static_cast<std::size_t>(7 * ch) <= end; block += static_cast<std::size_t>(f.blockAlign)) {
		const std::size_t blockEnd = std::min(end, block + static_cast<std::size_t>(f.blockAlign));
		int predictor[2]{}, delta[2]{}, s1[2]{}, s2[2]{};
		std::size_t p = block;
		for (int c = 0; c < ch; ++c) predictor[c] = in.u8(p++);
		for (int c = 0; c < ch; ++c, p += 2) delta[c] = in.s16(p);
		for (int c = 0; c < ch; ++c, p += 2) s1[c] = in.s16(p);
		for (int c = 0; c < ch; ++c, p += 2) s2[c] = in.s16(p);
		for (int c = 0; c < ch; ++c) out.samples.push_back(clamp16(s2[c]));
		for (int c = 0; c < ch; ++c) out.samples.push_back(clamp16(s1[c]));
		int produced = 2;
		int c = 0;
		const auto nibble = [&](int n) {
			const int value = n >= 8 ? n - 16 : n;
			const int predicted = (s1[c] * coef(predictor[c], 0) + s2[c] * coef(predictor[c], 1)) / 256;
			const int sample = std::clamp(predicted + value * delta[c], -32768, 32767);
			s2[c] = s1[c];
			s1[c] = sample;
			delta[c] = std::max(16, MS_ADAPTATION[n] * delta[c] / 256);
			out.samples.push_back(static_cast<std::int16_t>(sample));
			c = (c + 1) % ch;
		};
		for (; p < blockEnd && produced < perBlock; ++p) {
			const int byte = in.u8(p);
			nibble(byte >> 4);
			nibble(byte & 15);
			produced += ch == 1 ? 2 : 1;
		}
		if (out.frames() > MAX_FRAMES) break;
	}
	return true;
}

bool decodeImaAdpcm(const WavFormat& f, const Bytes& in, std::size_t at, std::size_t end, DecodedAudio& out) {
	const int ch = f.channels;
	if (ch < 1 || ch > 2 || f.blockAlign < 4 * ch) return false;
	for (std::size_t block = at; block + static_cast<std::size_t>(4 * ch) <= end; block += static_cast<std::size_t>(f.blockAlign)) {
		const std::size_t blockEnd = std::min(end, block + static_cast<std::size_t>(f.blockAlign));
		int predictor[2]{}, index[2]{};
		for (int c = 0; c < ch; ++c) {
			predictor[c] = in.s16(block + static_cast<std::size_t>(4 * c));
			index[c] = std::clamp<int>(in.u8(block + static_cast<std::size_t>(4 * c) + 2), 0, 88);
		}
		const std::size_t first = out.samples.size();
		for (int c = 0; c < ch; ++c) out.samples.push_back(clamp16(predictor[c]));
		const auto decode = [&](int c, int n) {
			const int step = IMA_STEPS[index[c]];
			int diff = step >> 3;
			if (n & 1) diff += step >> 2;
			if (n & 2) diff += step >> 1;
			if (n & 4) diff += step;
			predictor[c] = std::clamp(predictor[c] + ((n & 8) ? -diff : diff), -32768, 32767);
			index[c] = std::clamp(index[c] + IMA_INDEX[n], 0, 88);
			return static_cast<std::int16_t>(predictor[c]);
		};
		// After the header: per channel 4 bytes (8 samples) in turn, low nibble first.
		std::size_t p = block + static_cast<std::size_t>(4 * ch);
		std::vector<std::int16_t> group[2];
		while (p + static_cast<std::size_t>(4 * ch) <= blockEnd) {
			for (int c = 0; c < ch; ++c) {
				group[c].clear();
				for (int i = 0; i < 4; ++i, ++p) {
					const int byte = in.u8(p);
					group[c].push_back(decode(c, byte & 15));
					group[c].push_back(decode(c, byte >> 4));
				}
			}
			for (std::size_t i = 0; i < 8; ++i) {
				for (int c = 0; c < ch; ++c) out.samples.push_back(group[c][i]);
			}
		}
		(void)first;
		if (out.frames() > MAX_FRAMES) break;
	}
	return true;
}

bool decodeWav(const Bytes& in, DecodedAudio& out, std::string* error) {
	WavFormat f;
	std::size_t dataAt = 0, dataSize = 0;
	bool haveFormat = false, haveData = false;
	std::optional<std::size_t> loop;
	for (std::size_t at = 12; in.has(at, 8);) {
		const std::uint32_t size = in.u32(at + 4);
		const std::size_t body = at + 8;
		if (in.tag(at, "fmt ") && in.has(body, 16)) {
			f.tag = in.u16(body);
			f.channels = in.u16(body + 2);
			f.rate = static_cast<int>(in.u32(body + 4));
			f.blockAlign = in.u16(body + 12);
			f.bits = in.u16(body + 14);
			const int extra = size >= 18 ? in.u16(body + 16) : 0;
			if (f.tag == 0xFFFE && size >= 40) {
				f.tag = in.u16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub format GUID starts with the tag
			}
			if ((f.tag == 2 || f.tag == 0x11) && extra >= 2) {
				f.samplesPerBlock = in.u16(body + 18);
			}
			if (f.tag == 2 && extra >= 4) {
				const int count = std::min<int>(in.u16(body + 20), 64);
				for (int i = 0; i < count && in.has(body + 22 + std::size_t(i) * 4, 4); ++i) {
					f.coefs.push_back({in.s16(body + 22 + std::size_t(i) * 4), in.s16(body + 24 + std::size_t(i) * 4)});
				}
			}
			haveFormat = true;
		} else if (in.tag(at, "data")) {
			dataAt = body;
			dataSize = std::min<std::size_t>(size, in.size() - std::min(in.size(), body));
			haveData = true;
		} else if (in.tag(at, "cue ") && in.has(body, 4) && in.u32(body) > 0 && in.has(body + 4, 24)) {
			loop = in.u32(body + 4 + 20);   // first cue point: its sample offset (Source loops from here)
		} else if (in.tag(at, "smpl") && in.has(body, 36) && in.u32(body + 28) > 0 && in.has(body + 36, 24)) {
			loop = in.u32(body + 36 + 8);   // first sampler loop: its start
		}
		at = body + size + (size & 1);
		if (size > in.size()) break;
	}
	if (!haveFormat || !haveData) {
		return fail(error, "WAV without format or data");
	}
	if (f.channels < 1 || f.channels > 8 || f.rate < 1000 || f.rate > 192000) {
		return fail(error, "WAV with an unusual format");
	}
	out.channels = f.channels;
	out.sampleRate = f.rate;
	const std::size_t end = dataAt + dataSize;
	switch (f.tag) {
		case 1:   // PCM
		case 3: { // IEEE float
			const int bytes = f.bits / 8;
			if (bytes < 1 || bytes > 4 || (f.tag == 3 && bytes != 4)) {
				return fail(error, "WAV with an unsupported sample size");
			}
			const std::size_t count = std::min(dataSize / static_cast<std::size_t>(bytes), MAX_FRAMES * static_cast<std::size_t>(f.channels));
			out.samples.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				const std::size_t p = dataAt + i * static_cast<std::size_t>(bytes);
				int v = 0;
				if (f.tag == 3) {
					float value;
					const std::uint32_t raw = in.u32(p);
					std::memcpy(&value, &raw, 4);
					v = static_cast<int>(std::clamp(value, -1.f, 1.f) * 32767.f);
				} else if (bytes == 1) {
					v = (in.u8(p) - 128) << 8;
				} else if (bytes == 2) {
					v = in.s16(p);
				} else {
					v = in.s16(p + static_cast<std::size_t>(bytes) - 2);   // the upper 16 bits
				}
				out.samples.push_back(clamp16(v));
			}
			out.format = f.tag == 3 ? "PCM float" : "PCM " + std::to_string(f.bits) + " bit";
			break;
		}
		case 2:
			if (!decodeMsAdpcm(f, in, dataAt, end, out)) return fail(error, "broken MS ADPCM WAV");
			out.format = "MS ADPCM";
			break;
		case 0x11:
			if (!decodeImaAdpcm(f, in, dataAt, end, out)) return fail(error, "broken IMA ADPCM WAV");
			out.format = "IMA ADPCM";
			break;
		case 0x55: {
			DecodedAudio mp3;
			if (!decodeMp3(in.sub(dataAt, dataSize), mp3, error)) return false;
			out = std::move(mp3);
			out.format = "MP3 in WAV";
			break;
		}
		default:
			return fail(error, ("WAV format " + std::to_string(f.tag) + " is not supported").c_str());
	}
	if (loop && *loop < out.frames()) {
		out.loopStart = loop;
	}
	return !out.samples.empty() || fail(error, "WAV without samples");
}

} // namespace

std::optional<DecodedAudio> decodeAudio(std::span<const std::byte> data, std::string_view name, std::string* error) {
	DecodedAudio out;
	const Bytes in(data);
	bool ok = false;
	if (in.tag(0, "RIFF") && in.tag(8, "WAVE")) {
		ok = decodeWav(in, out, error);
	} else if (in.tag(0, "OggS")) {
		ok = decodeOgg(data, out, error);
	} else if (endsWithI(name, ".wav")) {
		ok = fail(error, "not a RIFF WAVE file");
	} else {
		ok = decodeMp3(data, out, error);   // MP3 has no fixed header (ID3 tag or frame sync)
	}
	if (!ok) {
		return std::nullopt;
	}
	return out;
}

std::optional<bool> soundFileLoops(std::span<const std::byte> data) {
	const Bytes in(data);
	if (in.tag(0, "OggS")) {
		return std::nullopt;   // Source 1 does not play Ogg
	}
	if (!(in.tag(0, "RIFF") && in.tag(8, "WAVE"))) {
		return false;          // MP3: the engine never loops it
	}
	// Only the chunk headers are read (like decodeWav, without decoding the samples).
	for (std::size_t at = 12; in.has(at, 8);) {
		const std::uint32_t size = in.u32(at + 4);
		const std::size_t body = at + 8;
		if (in.tag(at, "cue ") && in.has(body, 4) && in.u32(body) > 0) {
			return true;
		}
		if (in.tag(at, "smpl") && in.has(body, 36) && in.u32(body + 28) > 0) {
			return true;
		}
		if (size > in.size()) break;
		at = body + size + (size & 1);
	}
	return false;
}

std::vector<std::pair<float, float>> waveformPeaks(const DecodedAudio& audio, int buckets) {
	std::vector<std::pair<float, float>> out;
	const std::size_t frames = audio.frames();
	if (buckets <= 0 || frames == 0) {
		return out;
	}
	out.resize(static_cast<std::size_t>(buckets), {0.f, 0.f});
	const auto channels = static_cast<std::size_t>(audio.channels);
	for (int b = 0; b < buckets; ++b) {
		const std::size_t from = frames * static_cast<std::size_t>(b) / static_cast<std::size_t>(buckets);
		const std::size_t to = std::max(from + 1, frames * static_cast<std::size_t>(b + 1) / static_cast<std::size_t>(buckets));
		float low = 0, high = 0;
		for (std::size_t i = from * channels; i < std::min(to, frames) * channels; ++i) {
			const float v = audio.samples[i] / 32768.f;
			low = std::min(low, v);
			high = std::max(high, v);
		}
		out[static_cast<std::size_t>(b)] = {low, high};
	}
	return out;
}

} // namespace sme
