// test_asset_import.cpp - Importing the mapper's own sounds, pictures, particle files and models.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/asset_import.h>
#include <sme/audio.h>
#include <sme/model_import.h>
#include <sme/texture.h>
#include <sme/text.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

// FLAC files made with ffmpeg (sine waves, noise, silence; the padding block removed) and what
// ffmpeg decodes from them, as a hash of the 16-bit samples.
const unsigned char FLAC_STEREO16[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x09, 0x00, 0x09, 0x00, 0x00, 0x02, 0x5d, 0x00, 0x02, 0x5d, 0x05, 0x62, 0x22, 0xf0, 0x00, 0x00, 0x03, 0x72, 0xd2, 0x81, 0xea, 0x16, 0x2f, 0xc6, 0x54, 0x27, 0xbb, 0x5f, 0x4a, 0xaf, 0x27, 0x9f, 0xd4, 0xb0, 0xff, 0xf8, 0x76, 0x98, 0x00, 0x03, 0x71, 0x68, 0x46, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x3f, 0x80, 0x2f, 0x0e, 0x62, 0x6c, 0xcd, 0x13, 0x5e, 0x7e, 0x43, 0x4c, 0x00, 0x13, 0xcc, 0xe6, 0x52, 0x59, 0x42, 0xa1, 0x4c, 0x98, 0x53, 0x24, 0x9c, 0x86, 0x61, 0x25, 0x02, 0x86, 0x70, 0xa5, 0x0f, 0x08, 0x86, 0x52, 0x44, 0x94, 0x29, 0x21, 0x10, 0x86, 0x81, 0x99, 0xc2, 0x70, 0xc9, 0x28, 0x58, 0x6c, 0x94, 0xc2, 0x21, 0x49, 0xa1, 0x49, 0x48, 0x70, 0xc9, 0x30, 0xc9, 0x85, 0x38, 0x4a, 0x1a, 0x12, 0x93, 0xf9, 0x4a, 0x4c, 0xf0, 0xc9, 0x40, 0xb2, 0x14, 0x90, 0xb0, 0xc9, 0x30, 0xc9, 0x25, 0x09, 0x42, 0x94, 0xa5, 0x32, 0x68, 0x58, 0x44, 0x0a, 0x61, 0x38, 0x4e, 0x10, 0xe1, 0x81, 0x61, 0xc2, 0x99, 0xff, 0x42, 0x9e, 0x4b, 0x33, 0x99, 0x0a, 0x4c, 0x39, 0x0b, 0x09, 0x92, 0x72, 0x1e, 0x1a, 0x1c, 0xb3, 0x94, 0xd0, 0xcc, 0x39, 0x0c, 0xc9, 0x25, 0x08, 0x58, 0x14, 0xc2, 0x58, 0x16, 0x65, 0x9d, 0x0f, 0x49, 0x49, 0x43, 0x29, 0x0a, 0x61, 0x28, 0x4c, 0xc8, 0x4c, 0xc9, 0x85, 0x42, 0x9e, 0x54, 0x33, 0x4a, 0x72, 0x98, 0x4c, 0xc9, 0x0a, 0x18, 0x61, 0x93, 0x24, 0x93, 0x87, 0x85, 0x25, 0x93, 0xa6, 0x84, 0x43, 0x28, 0x66, 0x1c, 0x28, 0x50, 0x28, 0x49, 0x87, 0x03, 0x92, 0x67, 0x32, 0xc2, 0x9b, 0x0e, 0x53, 0x92, 0x86, 0x4e, 0x13, 0xc2, 0x4a, 0x42, 0x86, 0x14, 0x9f, 0x0b, 0x92, 0x26, 0x79, 0xa4, 0x84, 0x61, 0x33, 0x85, 0x24, 0x99, 0x24, 0xa4, 0x28, 0x1f, 0x29, 0x28, 0x68, 0x79, 0x49, 0x73, 0x2c, 0x38, 0x50, 0xc3, 0x0c, 0x25, 0x02, 0x93, 0x0c, 0x85, 0x0e, 0x42, 0xcc, 0xb3, 0x92, 0x86, 0x85, 0x30, 0xe4, 0x92, 0x49, 0x21, 0x43, 0x30, 0xe4, 0x25, 0x0a, 0x42, 0x94, 0x3d, 0x0b, 0x40, 0xb3, 0x93, 0x42, 0x9c, 0x0a, 0x14, 0x87, 0x09, 0xc2, 0x49, 0x93, 0x86, 0x52, 0x11, 0x0a, 0x4c, 0xd2, 0x50, 0xa6, 0x92, 0xc2, 0x64, 0xa1, 0x99, 0x25, 0x09, 0x90, 0xe6, 0x4f, 0x0f, 0x29, 0x49, 0x66, 0x66, 0x53, 0xc3, 0x24, 0x99, 0x30, 0xe4, 0x32, 0x64, 0x28, 0x50, 0xb3, 0x49, 0xa7, 0x9f, 0x94, 0x80, 0x00, 0x00, 0x10, 0x00, 0x1f, 0xc0, 0x2f, 0x00, 0x3d, 0x8e, 0x62, 0x83, 0x0c, 0x4a, 0x21, 0x33, 0x3f, 0x1d, 0x82, 0x70, 0x00, 0x0f, 0x0b, 0x3c, 0xd9, 0x9a, 0x49, 0x4c, 0xe4, 0x99, 0x0e, 0x12, 0x85, 0x0e, 0x66, 0x78, 0x52, 0x11, 0x0a, 0x19, 0x93, 0x27, 0x39, 0x4e, 0x4c, 0xa1, 0x85, 0x09, 0x99, 0x85, 0x0a, 0x61, 0x49, 0xc2, 0x99, 0x67, 0x3a, 0x14, 0x2c, 0x29, 0x25, 0x0c, 0x93, 0x24, 0x98, 0x78, 0x65, 0x0f, 0x0a, 0x70, 0xcb, 0x0b, 0x08, 0x81, 0x10, 0xca, 0x49, 0xc9, 0x48, 0x52, 0x4c, 0xe6, 0x14, 0x29, 0x33, 0x85, 0x0a, 0x14, 0x29, 0x25, 0x28, 0x52, 0x53, 0x9e, 0x1a, 0x19, 0x93, 0x09, 0xc3, 0xcc, 0x9a, 0x19, 0x49, 0xc9, 0x4c, 0xe4, 0xe6, 0x87, 0x26, 0x4a, 0x18, 0x52, 0x70, 0xa6, 0x18, 0x50, 0xca, 0x1e, 0x4f, 0x42, 0x86, 0x85, 0x0b, 0x26, 0x72, 0x1c, 0xc3, 0x24, 0xe1, 0xe4, 0xcc, 0x94, 0x96, 0x72, 0x79, 0x99, 0xe1, 0xc9, 0x99, 0x86, 0x61, 0xc9, 0x33, 0x26, 0x53, 0x39, 0xe5, 0x27, 0xe1, 0x10, 0xe1, 0x99, 0x26, 0x14, 0x28, 0x70, 0xcc, 0x29, 0x34, 0x39, 0xa6, 0x15, 0x0d, 0x0e, 0x64, 0xe4, 0xd0, 0x94, 0x33, 0x3c, 0x28, 0x4c, 0x99, 0x28, 0x19, 0x10, 0x29, 0x28, 0x59, 0x28, 0x44, 0x9e, 0x65, 0x0a, 0x4e, 0x61, 0x33, 0x0c, 0x85, 0x0a, 0x19, 0x86, 0x53, 0x39, 0xe6, 0x44, 0x39, 0x33, 0x33, 0x92, 0x16, 0x18, 0x53, 0x39, 0x28, 0x4a, 0x14, 0xa1, 0xa5, 0x0e, 0x79, 0x9e, 0x4c, 0x94, 0x28, 0x50, 0xe1, 0x43, 0x24, 0xc3, 0x93, 0x30, 0xa4, 0x29, 0x92, 0x21, 0xe7, 0x49, 0x25, 0x09, 0x49, 0x26, 0x64, 0xa0, 0x59, 0x0c, 0xe6, 0x14, 0x94, 0x3c, 0x34, 0x9c, 0xb0, 0x89, 0x9c, 0x29, 0x30, 0xa1, 0x42, 0x81, 0xc2, 0x66, 0x14, 0x99, 0xe1, 0x92, 0x85, 0x32, 0x92, 0x21, 0x43, 0x42, 0xc9, 0x9c, 0x33, 0x24, 0xcc, 0xa0, 0x52, 0x67, 0x32, 0x73, 0x2c, 0xb2, 0x5c, 0x22, 0x04, 0x49, 0xe1, 0x99, 0x32, 0x4c, 0xcc, 0x22, 0x07, 0x21, 0x42, 0x92, 0x87, 0x28, 0x50, 0xcf, 0x43, 0x08, 0x0c, 0x62};
constexpr std::size_t FLAC_STEREO16_SAMPLES = 1764;
constexpr std::uint32_t FLAC_STEREO16_HASH = 2470429719u;   // FNV-1a of the 16-bit samples as ffmpeg decodes them
const unsigned char FLAC_MONO24[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x04, 0x80, 0x04, 0x80, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x02, 0xb1, 0x11, 0x70, 0x00, 0x00, 0x01, 0xb9, 0x6b, 0x43, 0xb7, 0xc9, 0xf4, 0x3b, 0xaf, 0x5f, 0x24, 0xbf, 0x01, 0xb9, 0x78, 0x69, 0xcf, 0x27, 0xff, 0xf8, 0x7d, 0x0c, 0x00, 0x01, 0xb8, 0x2b, 0x11, 0xd2, 0x4f, 0x01, 0x00, 0x00, 0x02, 0xb8, 0x05, 0x5d, 0x07, 0xd9, 0x0a, 0x1b, 0x0c, 0x12, 0x0d, 0xae, 0x0e, 0xe4, 0xe7, 0x3e, 0x27, 0x36, 0x7e, 0x09, 0xb3, 0x9e, 0xc6, 0xf8, 0x1e, 0x71, 0x3f, 0x6d, 0x46, 0x28, 0x00, 0x22, 0x10, 0x88, 0x95, 0x53, 0x1b, 0x31, 0x99, 0xb3, 0x67, 0xf4, 0xa2, 0x08, 0x41, 0x02, 0x10, 0x81, 0x08, 0x20, 0x42, 0x22, 0x25, 0x5b, 0x6c, 0xd8, 0xcc, 0x6d, 0xb6, 0xed, 0x24, 0x42, 0x10, 0x82, 0x08, 0x10, 0x42, 0x08, 0x42, 0x11, 0x25, 0x56, 0xcd, 0x98, 0xd9, 0x99, 0xb7, 0xd2, 0x24, 0x42, 0x10, 0x41, 0x04, 0x10, 0x42, 0x08, 0x22, 0x22, 0x55, 0xfb, 0x36, 0x66, 0x63, 0x33, 0xff, 0xa1, 0x21, 0x10, 0x40, 0x82, 0x08, 0x20, 0x84, 0x21, 0x08, 0x89, 0x55, 0x31, 0xb3, 0x19, 0x9b, 0x36, 0x7f, 0x4a, 0x20, 0x84, 0x10, 0x21, 0x08, 0x10, 0x82, 0x04, 0x22, 0x22, 0x55, 0xb6, 0xcd, 0x8c, 0xc6, 0xdb, 0x6e, 0xd2, 0x44, 0x21, 0x08, 0x20, 0x81, 0x04, 0x20, 0x84, 0x21, 0x12, 0x55, 0x6c, 0xd9, 0x8d, 0x99, 0x9b, 0x7d, 0x22, 0x44, 0x21, 0x04, 0x10, 0x41, 0x04, 0x20, 0x82, 0x22, 0x25, 0x5f, 0xb3, 0x66, 0x66, 0x33, 0x3f, 0xfa, 0x12, 0x11, 0x04, 0x08, 0x20, 0x82, 0x08, 0x42, 0x10, 0x88, 0x95, 0x53, 0x1b, 0x31, 0x99, 0xb3, 0x67, 0xf4, 0xa2, 0x08, 0x41, 0x02, 0x10, 0x81, 0x08, 0x20, 0x42, 0x22, 0x25, 0x5b, 0x6c, 0xd8, 0xcc, 0x6d, 0xb6, 0xed, 0x24, 0x42, 0x10, 0x82, 0x08, 0x10, 0x42, 0x08, 0x42, 0x11, 0x25, 0x56, 0xcd, 0x98, 0xd9, 0x99, 0xb7, 0xd2, 0x24, 0x42, 0x10, 0x41, 0x04, 0x10, 0x42, 0x08, 0x22, 0x22, 0x55, 0xfb, 0x36, 0x66, 0x63, 0x33, 0xff, 0xa1, 0x21, 0x00, 0x37, 0xd5};
constexpr std::size_t FLAC_MONO24_SAMPLES = 441;
constexpr std::uint32_t FLAC_MONO24_HASH = 251172037u;   // FNV-1a of the 16-bit samples as ffmpeg decodes them
const unsigned char FLAC_NOISE16[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x02, 0x40, 0x02, 0x40, 0x00, 0x01, 0x5d, 0x00, 0x01, 0x5d, 0x01, 0xf4, 0x00, 0xf0, 0x00, 0x00, 0x00, 0xa0, 0x4d, 0xab, 0xe4, 0x60, 0x94, 0x5a, 0x1c, 0x14, 0x8a, 0xcb, 0x8d, 0x18, 0x6e, 0x44, 0xd6, 0x73, 0xff, 0xf8, 0x64, 0x08, 0x00, 0x9f, 0x37, 0x4c, 0xff, 0xe6, 0x1c, 0xa5, 0xda, 0xfe, 0xd0, 0x62, 0x29, 0x23, 0xf1, 0x50, 0xf6, 0x6c, 0xe7, 0xf3, 0x1c, 0x3b, 0xa4, 0x24, 0xcf, 0x66, 0xc7, 0x0e, 0x2d, 0xda, 0x49, 0xe1, 0x80, 0xe2, 0xe1, 0x09, 0x9e, 0x95, 0x94, 0x5d, 0x06, 0x56, 0xab, 0x89, 0xf3, 0x05, 0x82, 0xe4, 0x63, 0xde, 0x17, 0x13, 0xba, 0xd5, 0x8a, 0x96, 0xa5, 0x7b, 0x4c, 0x83, 0xbc, 0x64, 0xc9, 0x93, 0x48, 0x7c, 0x2e, 0xe2, 0xaf, 0xc9, 0xe1, 0x08, 0x6d, 0x9d, 0x61, 0xad, 0xd9, 0x00, 0x34, 0xbd, 0x72, 0x7c, 0xef, 0xfe, 0xd9, 0x3e, 0x78, 0x46, 0xf5, 0x8a, 0x45, 0xe6, 0xc8, 0xde, 0x50, 0x21, 0xd0, 0x49, 0x69, 0x2c, 0x1d, 0x6e, 0xe2, 0x38, 0x11, 0x08, 0x45, 0xc4, 0x7d, 0xc5, 0xd7, 0xa1, 0x08, 0x5d, 0x9f, 0xbf, 0xd7, 0x22, 0x71, 0xab, 0x77, 0xf3, 0x7a, 0xb5, 0x44, 0x2f, 0x58, 0x8a, 0xdf, 0x53, 0xb2, 0x88, 0xb7, 0xe4, 0x8e, 0x69, 0xaf, 0xcc, 0xc4, 0x4c, 0x25, 0xa5, 0x06, 0xdd, 0xff, 0xa3, 0x14, 0xe1, 0xcd, 0x7e, 0x33, 0x11, 0xe8, 0xcf, 0x5b, 0xc8, 0x83, 0x21, 0x6f, 0x35, 0x5f, 0x12, 0x08, 0x92, 0x41, 0x14, 0x33, 0x92, 0x75, 0xa3, 0x1e, 0x22, 0x9a, 0x7a, 0xf7, 0x8a, 0xc8, 0xab, 0x88, 0x12, 0x2b, 0x2b, 0x58, 0xe1, 0x19, 0x12, 0x29, 0x87, 0x31, 0x86, 0x4d, 0x2b, 0x2d, 0x30, 0x4d, 0x91, 0xd3, 0xee, 0xf9, 0x17, 0x3b, 0x08, 0xe0, 0x0b, 0x9a, 0x85, 0xbe, 0xfb, 0xef, 0x12, 0x59, 0x15, 0x8b, 0xd4, 0x2b, 0x6e, 0x2a, 0x75, 0x8c, 0x35, 0xf5, 0xfd, 0x85, 0x4b, 0xc0, 0x40, 0x5d, 0xd9, 0xea, 0xda, 0x3c, 0x9b, 0x8c, 0xa5, 0x6e, 0xa4, 0xde, 0x90, 0xc6, 0x49, 0xb9, 0xbe, 0xfe, 0x96, 0x7e, 0x35, 0x08, 0xef, 0x88, 0x52, 0xf7, 0x3c, 0xa7, 0xa7, 0x48, 0x49, 0x37, 0xcb, 0xca, 0x5f, 0x86, 0xc0, 0x7e, 0x71, 0x22, 0x61, 0xae, 0x71, 0xdf, 0xa9, 0x38, 0x3b, 0x8b, 0x1e, 0x67, 0xd2, 0x6c, 0xa1, 0xba, 0x50, 0x4e, 0xdc, 0x16, 0x24, 0xf7, 0xb5, 0xbd, 0x84, 0xd9, 0x48, 0xe8, 0xc7, 0x97, 0x57, 0x71, 0xe2, 0x32, 0xc9, 0x42, 0xf5, 0x69, 0xd9, 0x87, 0x87, 0x6f, 0xc1, 0xe4, 0x59, 0xbf, 0xdc, 0x8b, 0xc6, 0x2c, 0x72, 0x48, 0x61, 0xfa, 0x1c, 0x60, 0x3f, 0x29, 0xbf, 0xa4, 0xdd, 0xab, 0x41, 0x88, 0xab, 0x6a, 0xd6, 0x7d, 0xe8, 0x7a, 0xd7, 0x08, 0x92, 0x87, 0xaf, 0x37, 0xe4, 0x40, 0xa1, 0x50, 0x1e, 0xf8};
constexpr std::size_t FLAC_NOISE16_SAMPLES = 160;
constexpr std::uint32_t FLAC_NOISE16_HASH = 1717295428u;   // FNV-1a of the 16-bit samples as ffmpeg decodes them
const unsigned char FLAC_SILENT16[] = {0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x02, 0x40, 0x02, 0x40, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x0c, 0x01, 0xf4, 0x00, 0xf0, 0x00, 0x00, 0x00, 0xa0, 0x04, 0xdb, 0xc6, 0x6d, 0x8e, 0x45, 0x59, 0x76, 0x9b, 0xb3, 0x8a, 0x92, 0xcd, 0x6f, 0x7d, 0x9d, 0xff, 0xf8, 0x64, 0x08, 0x00, 0x9f, 0x37, 0x00, 0x00, 0x00, 0x41, 0x2d};
constexpr std::size_t FLAC_SILENT16_SAMPLES = 160;
constexpr std::uint32_t FLAC_SILENT16_HASH = 35159621u;   // FNV-1a of the 16-bit samples as ffmpeg decodes them

std::uint32_t fnv(const std::vector<std::int16_t>& samples) {
	std::uint32_t h = 2166136261u;
	for (const auto s : samples) h = (h ^ std::uint16_t(s)) * 16777619u;
	return h;
}

template <std::size_t N>
std::span<const std::byte> bytes(const unsigned char (&data)[N]) {
	return std::as_bytes(std::span(data, N));
}

std::string readText(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

std::vector<std::byte> readBytes(const fs::path& file) {
	const std::string text = readText(file);
	const auto* data = reinterpret_cast<const std::byte*>(text.data());
	return {data, data + text.size()};
}

/// A test sound: a 440 Hz tone, `channels` channels at `rate`.
DecodedAudio tone(int rate, int channels, double seconds) {
	DecodedAudio audio;
	audio.sampleRate = rate;
	audio.channels = channels;
	const auto frames = std::size_t(rate * seconds);
	for (std::size_t i = 0; i < frames; ++i) {
		const auto s = std::int16_t(8000 * std::sin(2 * 3.14159265 * 440 * double(i) / rate));
		for (int c = 0; c < channels; ++c) audio.samples.push_back(s);
	}
	return audio;
}

ImageRgba picture(std::uint16_t w, std::uint16_t h, bool holes) {
	ImageRgba image;
	image.width = w;
	image.height = h;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const bool hole = holes && (x / 8 + y / 8) % 2 == 0;
			image.pixels.insert(image.pixels.end(), {std::byte(x * 255 / w), std::byte(y * 255 / h), std::byte(128), std::byte(hole ? 0 : 255)});
		}
	}
	return image;
}

} // namespace

TEST_CASE("import: names and folders for the game") {
	CHECK(importName("My Hum (2).WAV") == "my_hum_2_wav");
	CHECK(importName("  crate-01  ") == "crate-01");
	CHECK(importName("äöü").empty());
	CHECK(importFolder("Custom/My Stuff") == "custom/my_stuff");
	CHECK(importFolder("../../etc//x") == "etc/x");
	CHECK(importFolder("\\abs\\..\\path") == "abs/path");
	CHECK(importFolder("").empty());
}

TEST_CASE("import: FLAC decodes exactly like ffmpeg, broken files end cleanly") {
	struct Case {
		std::span<const std::byte> data;
		std::size_t samples;
		std::uint32_t hash;
		int rate, channels;
	};
	const Case cases[] = {
		{bytes(FLAC_STEREO16), FLAC_STEREO16_SAMPLES, FLAC_STEREO16_HASH, 22050, 2},
		{bytes(FLAC_MONO24), FLAC_MONO24_SAMPLES, FLAC_MONO24_HASH, 11025, 1},
		{bytes(FLAC_NOISE16), FLAC_NOISE16_SAMPLES, FLAC_NOISE16_HASH, 8000, 1},
		{bytes(FLAC_SILENT16), FLAC_SILENT16_SAMPLES, FLAC_SILENT16_HASH, 8000, 1},
	};
	for (const auto& c : cases) {
		std::string error;
		const auto audio = decodeAudio(c.data, "x.flac", &error);
		REQUIRE_MESSAGE(audio, error);
		CHECK(audio->sampleRate == c.rate);
		CHECK(audio->channels == c.channels);
		CHECK(audio->samples.size() == c.samples);
		CHECK(fnv(audio->samples) == c.hash);
		CHECK(audio->format.starts_with("FLAC"));
		CHECK_FALSE(soundFileLoops(c.data).has_value());
		// Cut anywhere or with flipped bytes: an error or fewer samples, never a crash.
		for (std::size_t cut = 0; cut < c.data.size(); cut += 7) {
			(void)decodeAudio(c.data.first(cut), "x.flac");
		}
		std::vector<std::byte> flipped(c.data.begin(), c.data.end());
		for (std::size_t i = 42; i < flipped.size(); i += 13) flipped[i] ^= std::byte{0x5a};
		(void)decodeAudio(flipped, "x.flac");
	}
}

TEST_CASE("import: manifests get a line, the game's list stays") {
	const std::string game = "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds.txt\" // the game's { sounds }\n\t\"precache_file\" \"scripts/npc_sounds.txt\"\n}\n";
	const auto added = withManifestEntry(game, "game_sounds_manifest", "precache_file", "scripts/game_sounds_mapsmith.txt");
	CHECK(added.find("scripts/npc_sounds.txt") != std::string::npos);
	CHECK(added.find("\"precache_file\"\t\"scripts/game_sounds_mapsmith.txt\"\n}") != std::string::npos);
	CHECK(withManifestEntry(added, "game_sounds_manifest", "precache_file", "Scripts\\Game_Sounds_Mapsmith.txt") == added);   // once
	CHECK(withoutManifestEntry(added, "scripts/game_sounds_mapsmith.txt") == game);
	CHECK(withManifestEntry("", "particles_manifest", "file", "!particles/fire.pcf") == "particles_manifest\n{\n\t\"file\"\t\"!particles/fire.pcf\"\n}\n");
	const std::string particles = "particles_manifest { \"file\" \"particles/a.pcf\" }";
	const auto withB = withManifestEntry(particles, "particles_manifest", "file", "particles/b.pcf");
	CHECK(withB.find("particles/a.pcf") < withB.find("particles/b.pcf"));
	CHECK(withManifestEntry(withB, "particles_manifest", "file", "!particles/B.pcf") == withB);   // "!" and case do not matter
}

TEST_CASE("import: a sound becomes a game-ready WAV with a sound script entry, and goes again") {
	const auto mod = fs::temp_directory_path() / "sme_import_sound";
	fs::remove_all(mod);
	fs::create_directories(mod);
	const std::string gameManifest = "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds.txt\"\n}\n";

	SoundImportOptions options;
	options.folder = "Ambient/My Stuff";
	options.name = "Hum Loop";
	options.sampleRate = 22050;
	options.channels = SoundImportOptions::Channels::Mono;
	options.loop = true;
	options.loopStart = 100;
	options.script = true;
	options.scriptName = "Custom.Hum";
	options.volume = 0.7;
	options.pitch = 95;
	const auto source = tone(48000, 2, 0.5);
	const auto prepared = prepareImportedSound(source, options);
	CHECK(prepared.sampleRate == 22050);
	CHECK(prepared.channels == 1);
	CHECK(prepared.loopStart == 100u);

	std::string error;
	const auto record = importSound(mod, source, options, gameManifest, false, &error);
	REQUIRE_MESSAGE(record, error);
	CHECK(record->asset == "Custom.Hum");
	const fs::path wav = mod / "sound/ambient/my_stuff/hum_loop.wav";
	CHECK(soundImportTarget(mod, options) == wav);
	REQUIRE(fs::exists(wav));
	const auto data = readBytes(wav);
	const auto decoded = decodeAudio(data, "hum_loop.wav");
	REQUIRE(decoded);
	CHECK(decoded->sampleRate == 22050);
	CHECK(decoded->channels == 1);
	CHECK(soundFileLoops(data) == true);
	const std::string script = readText(importSoundScriptFile(mod));
	CHECK(script.find("Custom.Hum") != std::string::npos);
	CHECK(script.find("ambient/my_stuff/hum_loop.wav") != std::string::npos);
	CHECK(script.find("0.7") != std::string::npos);
	const std::string manifest = readText(mod / "scripts/game_sounds_manifest.txt");
	CHECK(manifest.find("scripts/game_sounds.txt") != std::string::npos);   // the game's sounds stay
	CHECK(manifest.find("scripts/game_sounds_mapsmith.txt") != std::string::npos);

	// The same again: not without asking; a second sound shares the script file.
	CHECK_FALSE(importSound(mod, source, options, gameManifest, false, &error));
	SoundImportOptions second = options;
	second.name = "beep";
	second.scriptName = "Custom.Beep";
	second.loop = false;
	const auto other = importSound(mod, tone(11025, 1, 0.1), second, gameManifest, false, &error);
	REQUIRE_MESSAGE(other, error);
	CHECK(soundFileLoops(readBytes(soundImportTarget(mod, second))) == false);
	CHECK(other->entries.size() == 1);   // the manifest line was already there

	// Undo the first: its WAV and script block go, the manifest line stays for the second.
	REQUIRE(undoImport(*record));
	CHECK_FALSE(fs::exists(wav));
	CHECK(readText(importSoundScriptFile(mod)).find("Custom.Hum") == std::string::npos);
	CHECK(readText(mod / "scripts/game_sounds_manifest.txt").find("game_sounds_mapsmith.txt") != std::string::npos);
	REQUIRE(undoImport(*other));
	CHECK_FALSE(fs::exists(importSoundScriptFile(mod)));
	CHECK(readText(mod / "scripts/game_sounds_manifest.txt").find("game_sounds_mapsmith.txt") == std::string::npos);
	CHECK(readText(mod / "scripts/game_sounds_manifest.txt").find("scripts/game_sounds.txt") != std::string::npos);
	CHECK_FALSE(fs::exists(mod / "sound"));   // empty folders are cleaned up

	// Without a name or sound: a clear error.
	options.name = "!!!";
	CHECK_FALSE(importSound(mod, source, options, gameManifest, false, &error));
	CHECK(error.find("name") != std::string::npos);
	fs::remove_all(mod);
}

TEST_CASE("import: a picture becomes a VTF and a VMT; overwriting keeps a backup for undo") {
	const auto mod = fs::temp_directory_path() / "sme_import_material";
	fs::remove_all(mod);
	fs::create_directories(mod);

	// Pictures decode from PNG; alpha decides between cut-out and see-through.
	const auto holes = picture(100, 60, true);
	std::string error;
	const auto decoded = decodeImageFile(encodePng(holes), &error);
	REQUIRE_MESSAGE(decoded, error);
	CHECK(decoded->width == 100);
	CHECK(decoded->pixels == holes.pixels);
	CHECK_FALSE(decodeImageFile(std::vector<std::byte>(64, std::byte{7})));
	CHECK(imageAlpha(holes) == ImageAlpha::Binary);
	CHECK(imageAlpha(picture(8, 8, false)) == ImageAlpha::Opaque);
	auto soft = picture(8, 8, false);
	soft.pixels[3] = std::byte{128};
	CHECK(imageAlpha(soft) == ImageAlpha::Soft);

	// The VTF: power-of-two size within the limit, mipmaps, DXT5 for alpha.
	const auto vtf = encodeVtf(holes, true, false, 64, &error);
	REQUIRE_MESSAGE(vtf, error);
	const auto info = readTextureInfo(*vtf);
	REQUIRE(info);
	CHECK(info->width == 64);
	CHECK(info->height == 64);
	CHECK(info->format == "DXT5");
	CHECK(info->mipCount > 1);
	CHECK(decodeTexture(*vtf));
	const auto opaque = encodeVtf(picture(32, 16, false), true, false, 2048);
	REQUIRE(opaque);
	CHECK(readTextureInfo(*opaque)->format == "DXT1");
	CHECK(readTextureInfo(*encodeVtf(picture(16, 16, false), true, true, 2048))->format == "BGRA8888");

	MaterialImportOptions options;
	options.folder = "custom";
	options.name = "Fence Wire";
	options.surfaceProp = "metal";
	CHECK(materialImportName(options) == "custom/fence_wire");
	const auto normal = picture(64, 64, false);
	const auto record = importMaterial(mod, holes, &normal, options, false, &error);
	REQUIRE_MESSAGE(record, error);
	CHECK(record->created.size() == 3);
	const std::string vmt = readText(mod / "materials/custom/fence_wire.vmt");
	CHECK(vmt.starts_with("\"LightmappedGeneric\""));
	CHECK(vmt.find("\"$basetexture\" \"custom/fence_wire\"") != std::string::npos);
	CHECK(vmt.find("\"$bumpmap\" \"custom/fence_wire_normal\"") != std::string::npos);
	CHECK(vmt.find("\"$alphatest\" \"1\"") != std::string::npos);
	CHECK(vmt.find("\"$surfaceprop\" \"metal\"") != std::string::npos);
	CHECK(fs::exists(mod / "materials/custom/fence_wire_normal.vtf"));

	// Model materials: VertexLitGeneric below materials/models/.
	MaterialImportOptions model = options;
	model.use = MaterialImportOptions::Use::Model;
	model.alpha = MaterialImportOptions::Alpha::None;
	CHECK(materialImportName(model) == "models/custom/fence_wire");
	CHECK(importedMaterialVmt(model, MaterialImportOptions::Alpha::None, false).starts_with("\"VertexLitGeneric\""));

	// Again: refused without asking; with overwrite the old files come back on undo.
	CHECK_FALSE(importMaterial(mod, holes, nullptr, options, false, &error));
	const std::string before = readText(mod / "materials/custom/fence_wire.vmt");
	options.surfaceProp = "chainlink";
	const auto again = importMaterial(mod, holes, nullptr, options, true, &error);
	REQUIRE_MESSAGE(again, error);
	CHECK(again->created.empty());
	CHECK(again->replaced.size() == 2);
	CHECK(readText(mod / "materials/custom/fence_wire.vmt").find("chainlink") != std::string::npos);
	REQUIRE(undoImport(*again));
	CHECK(readText(mod / "materials/custom/fence_wire.vmt") == before);
	REQUIRE(undoImport(*record));
	CHECK_FALSE(fs::exists(mod / "materials"));
	fs::remove_all(mod);
}

TEST_CASE("import: particle files go into the mod and its particle manifest") {
	const auto mod = fs::temp_directory_path() / "sme_import_particles";
	fs::remove_all(mod);
	fs::create_directories(mod);
	const std::string game = "particles_manifest\n{\n\t\"file\" \"particles/blood_impact.pcf\"\n}\n";
	std::string error;
	CHECK_FALSE(importParticles(mod, std::vector<std::byte>(40, std::byte{1}), "junk", game, false, false, &error));
	CHECK(error.find("particle") != std::string::npos);
	const auto pcf = test::makePcf({"my_fire", "my_smoke"});
	const auto record = importParticles(mod, pcf, "My Fire", game, true, false, &error);
	REQUIRE_MESSAGE(record, error);
	CHECK(record->asset == "particles/my_fire.pcf");
	CHECK(fs::exists(particleImportTarget(mod, "My Fire")));
	const std::string manifest = readText(mod / "particles/particles_manifest.txt");
	CHECK(manifest.find("particles/blood_impact.pcf") != std::string::npos);
	CHECK(manifest.find("\"!particles/my_fire.pcf\"") != std::string::npos);
	CHECK_FALSE(importParticles(mod, pcf, "my_fire", game, true, false, &error));
	REQUIRE(undoImport(*record));
	CHECK_FALSE(fs::exists(particleImportTarget(mod, "my_fire")));
	CHECK(readText(mod / "particles/particles_manifest.txt").find("my_fire") == std::string::npos);
	CHECK(readText(mod / "particles/particles_manifest.txt").find("blood_impact") != std::string::npos);
	fs::remove_all(mod);
}

TEST_CASE("import: the list of imports survives a restart and is safe to read") {
	const fs::path mod = fs::temp_directory_path() / "sme_import_log";
	fs::remove_all(mod);
	fs::create_directories(mod);
	ImportRecord a{"sound", "Custom.Hum", {mod / "sound/custom/hum.wav"}, {mod / "sound/custom/old.wav"},
	               {{ImportEntry::Kind::SoundScript, mod / "scripts/game_sounds_mapsmith.txt", "Custom.Hum"},
	                {ImportEntry::Kind::Manifest, mod / "scripts/game_sounds_manifest.txt", "scripts/game_sounds_mapsmith.txt"}}};
	ImportRecord b{"particles", "particles/fire.pcf", {mod / "particles/fire.pcf"}, {}, {}};
	REQUIRE(rememberImport(mod, a));
	REQUIRE(rememberImport(mod, b));
	CHECK(loadImports(mod) == std::vector<ImportRecord>{a, b});
	REQUIRE(forgetImport(mod, a));
	CHECK(loadImports(mod) == std::vector<ImportRecord>{b});
	// Paths out of the mod are dropped when reading.
	const auto read = readImportLog("import\tsound\tx\ncreated\t../../etc/passwd\ncreated\t/abs/file\ncreated\tC:/win\ncreated\tsound/ok.wav\nend\n", mod);
	REQUIRE(read.size() == 1);
	CHECK(read[0].created == std::vector<fs::path>{mod / "sound/ok.wav"});
	CHECK(readImportLog("created\tx\nend\nimport\tonly", mod).empty());
	fs::remove_all(mod);
}

TEST_CASE("import: models from OBJ and SMD become studiomdl sources") {
	const std::string mtl = "newmtl Wood Planks\nKd 0.5 0.3 0.1\nmap_Kd textures/planks.png\nnewmtl paint\nKd 1 0 0\n";
	// A 2x2x2 cube (Y-up, metres) with quads, one face red, plus junk lines and a bad index.
	const std::string obj = "# cube\nmtllib cube.mtl\n"
	                        "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv -1 2 -1\nv 1 2 -1\nv 1 2 1\nv -1 2 1\n"
	                        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 1 0\n"
	                        "usemtl Wood Planks\n"
	                        "f 1/1 2/2 3/3 4/4\nf 8/1 7/2 6/3 5/4\nf 1/1 5/2 6/3 2/4\nf 2/1 6/2 7/3 3/4\nf 3/1 7/2 8/3 4/4\n"
	                        "usemtl paint\nf -8/1 -4/2 -1/3 -5/4\n"
	                        "f 1 2 99\nf 1//9 2//1 3//1\nnonsense line\nv 1 2\n";
	CHECK(objMaterialLibrary(obj) == "cube.mtl");
	std::string error;
	const auto mesh = readObj(obj, mtl, &error);
	REQUIRE_MESSAGE(mesh, error);
	CHECK(mesh->triangles.size() == 12);
	CHECK(mesh->materials == std::vector<std::string>{"Wood Planks", "paint"});
	CHECK(mesh->textures.at("Wood Planks") == "textures/planks.png");
	CHECK(mesh->colors.at("paint").x == doctest::Approx(1));
	CHECK(modelMaterialNames(*mesh) == std::vector<std::string>{"wood_planks", "paint"});
	CHECK_FALSE(readObj("v 0 0 0\nf 1 2 3\n", ""));

	// Placed: Y-up metres -> Z-up inches, standing on the ground in the middle.
	ModelImportOptions options;
	options.folder = "props/Mine";
	options.name = "Box";
	options.scale = 39.37;
	options.kind = ModelImportOptions::Kind::Physics;
	options.surfaceProp = "wood";
	const auto placed = placedMesh(*mesh, options);
	const Bounds box = placed.bounds();
	CHECK(box.min.z == doctest::Approx(0));
	CHECK(box.max.z == doctest::Approx(2 * 39.37));
	CHECK(box.min.x == doctest::Approx(-39.37));
	CHECK(box.max.y == doctest::Approx(39.37));
	// Collision: the hull of a cube is the cube (12 triangles), its volume 8 m3.
	const auto hull = collisionMesh(placed, ModelImportOptions::Collision::Hull);
	CHECK(hull.triangles.size() == 12);
	CHECK(meshVolume(hull) == doctest::Approx(std::pow(2 * 39.37, 3)).epsilon(0.001));
	CHECK(collisionMesh(placed, ModelImportOptions::Collision::Box).triangles.size() == 12);
	CHECK(collisionMesh(placed, ModelImportOptions::Collision::Parts).triangles.size() == 12);
	CHECK(importMass(placed, options) == doctest::Approx(2000));   // 8 m3 of wood, clamped
	options.mass = 25;
	CHECK(importMass(placed, options) == doctest::Approx(25));
	// A flat mesh still gets a solid box.
	const auto flat = readObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2 3\n", "");
	REQUIRE(flat);
	CHECK(collisionMesh(*flat, ModelImportOptions::Collision::Hull).triangles.size() == 12);

	// SMD: written and read back the same.
	const std::string smd = writeSmd(placed);
	CHECK(smd.starts_with("version 1\nnodes\n0 \"root\" -1\nend\nskeleton\n"));
	CHECK(smd.find("\nwood_planks\n0 ") != std::string::npos);
	const auto back = readSmd(smd, &error);
	REQUIRE_MESSAGE(back, error);
	CHECK(back->triangles.size() == 12);
	CHECK(back->triangles[3].corners[1].position.z == doctest::Approx(placed.triangles[3].corners[1].position.z).epsilon(1e-5));
	CHECK_FALSE(readSmd("version 1\ntriangles\nmat\n0 1 2\nend\n"));

	// QC: model name, materials folder, kind.
	const std::string qc = writeQc(options, "box_ref.smd", "box_phys.smd", 25);
	CHECK(qc.find("$modelname \"props/mine/box.mdl\"") != std::string::npos);
	CHECK(qc.find("$cdmaterials \"models/props/mine/\"") != std::string::npos);
	CHECK(qc.find("$surfaceprop \"wood\"") != std::string::npos);
	CHECK(qc.find("$staticprop") == std::string::npos);
	CHECK(qc.find("$mass 25") != std::string::npos);
	CHECK(qc.find("prop_data") != std::string::npos);
	options.kind = ModelImportOptions::Kind::Static;
	options.collision = ModelImportOptions::Collision::Parts;
	const std::string staticQc = writeQc(options, "box_ref.smd", "box_phys.smd", 25);
	CHECK(staticQc.find("$staticprop") != std::string::npos);
	CHECK(staticQc.find("$concave") != std::string::npos);
	CHECK(staticQc.find("prop_data") == std::string::npos);

	// The sources on disk, then (a stand-in for) studiomdl's files.
	const auto mod = fs::temp_directory_path() / "sme_import_model";
	fs::remove_all(mod);
	fs::create_directories(mod);
	auto plan = prepareModelImport(mod, *mesh, options, false, &error);
	REQUIRE_MESSAGE(plan, error);
	CHECK(plan->qc == mod / "modelsrc/props/mine/box/box.qc");
	CHECK(plan->record.created.size() == 3);
	CHECK(plan->arguments.size() == 5);
	CHECK(plan->arguments[0] == "-game");
	CHECK(plan->record.asset == "models/props/mine/box.mdl");
	CHECK_FALSE(collectModelOutputs(*plan, mod, options, &error));   // nothing compiled yet
	for (const auto& file : modelOutputFiles(mod, options)) {
		fs::create_directories(file.parent_path());
		std::ofstream(file) << "compiled";
	}
	REQUIRE(collectModelOutputs(*plan, mod, options, &error));
	CHECK(plan->record.created.size() == 9);
	CHECK_FALSE(prepareModelImport(mod, *mesh, options, false, &error));   // there already
	REQUIRE(undoImport(plan->record));
	CHECK_FALSE(fs::exists(mod / "models"));
	CHECK_FALSE(fs::exists(mod / "modelsrc"));
	CHECK(studiomdlProblems("Processing x\nERROR: too many vertices\nok\nWARNING: bad uv\n") ==
	      std::vector<std::string>{"ERROR: too many vertices", "WARNING: bad uv"});
	fs::remove_all(mod);
}
