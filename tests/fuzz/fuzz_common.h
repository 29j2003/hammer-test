// fuzz_common.h - Helpers shared by the libFuzzer targets in tests/fuzz (built with -DSME_FUZZ=ON).
//
// A target gets one blob of bytes per run. Formats made of several files (a model: .mdl, .vvd,
// .vtx) take them from one blob: the lengths of all parts but the last as 32-bit little-endian
// numbers, then the parts (splitParts; joinParts writes it for the seeds). Readers that only take
// a file path get the bytes through a scratch file (ScratchFile), one per process.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define SME_FUZZ_PID _getpid()
#else
#include <unistd.h>
#define SME_FUZZ_PID getpid()
#endif

namespace sme::fuzz {

using Bytes = std::vector<std::byte>;

inline std::span<const std::byte> bytes(const std::uint8_t* data, std::size_t size) {
	return {reinterpret_cast<const std::byte*>(data), size};
}

inline std::string_view text(const std::uint8_t* data, std::size_t size) {
	return {reinterpret_cast<const char*>(data), size};
}

/// Splits "len0 .. len(n-2) part0 .. part(n-1)" (lengths clamped to what is there).
inline std::vector<Bytes> splitParts(const std::uint8_t* data, std::size_t size, int parts) {
	std::vector<Bytes> out;
	std::size_t at = 4 * std::size_t(parts - 1);
	if (size < at) {
		out.resize(std::size_t(parts));
		return out;
	}
	for (int i = 0; i < parts; ++i) {
		std::size_t length = size - at;
		if (i + 1 < parts) {
			std::uint32_t claimed = 0;
			std::memcpy(&claimed, data + 4 * i, 4);
			length = std::min<std::size_t>(claimed, size - at);
		}
		out.emplace_back(reinterpret_cast<const std::byte*>(data + at), reinterpret_cast<const std::byte*>(data + at + length));
		at += length;
	}
	return out;
}

/// Joins parts in the format splitParts reads (used by the seed writer).
inline Bytes joinParts(const std::vector<Bytes>& parts) {
	Bytes out;
	for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
		const auto length = std::uint32_t(parts[i].size());
		const auto* p = reinterpret_cast<const std::byte*>(&length);
		out.insert(out.end(), p, p + 4);
	}
	for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
	return out;
}

/// A file in the temp folder that holds the current input (same name for the whole process).
class ScratchFile {
public:
	explicit ScratchFile(std::string_view name) {
		m_dir = std::filesystem::temp_directory_path() / ("sme-fuzz-" + std::to_string(SME_FUZZ_PID));
		std::filesystem::create_directories(m_dir);
		m_path = m_dir / std::string(name);
	}
	~ScratchFile() {
		std::error_code ec;
		std::filesystem::remove_all(m_dir, ec);
	}
	ScratchFile(const ScratchFile&) = delete;
	ScratchFile& operator=(const ScratchFile&) = delete;

	const std::filesystem::path& write(const std::uint8_t* data, std::size_t size) {
		std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(data), std::streamsize(size));
		return m_path;
	}
	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }
	[[nodiscard]] const std::filesystem::path& dir() const { return m_dir; }

private:
	std::filesystem::path m_dir, m_path;
};

} // namespace sme::fuzz
