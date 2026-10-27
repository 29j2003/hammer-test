// fixture.h - Builds a small fake Steam installation on disk for tests and screenshots.
//
// Contains Half-Life 2 (two Steam libraries), the Source SDK Base 2013 Singleplayer,
// a Mapbase-style sourcemod, VPKs, VMT/VTF materials, FGDs and a Hammer++ config.
// Everything is generated, so no Valve files are needed or committed.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sme::test {

struct Fixture {
	std::filesystem::path root;        ///< Folder that holds everything (deleted by the destructor if owned)
	std::filesystem::path steam;       ///< Steam root
	std::filesystem::path library2;    ///< Second Steam library
	std::filesystem::path hl2;         ///< .../Half-Life 2/hl2
	std::filesystem::path sdkBase;     ///< .../Source SDK Base 2013 Singleplayer
	std::filesystem::path mod;         ///< .../sourcemods/mymod
	bool owned = true;

	Fixture() = default;
	Fixture(const Fixture&) = delete;
	Fixture& operator=(const Fixture&) = delete;
	~Fixture();
};

/// Creates the fixture below `dir` (a fresh temp folder if empty).
void createFixture(Fixture& fixture, const std::filesystem::path& dir = {});

/// Writes a text file, creating parent folders.
void writeText(const std::filesystem::path& path, std::string_view text);

/// Adds demo content (textures, a model with animations, sounds, entity classes) to the
/// fixture's mod and writes the demo map to <mod>/mapsrc/demo.vmf.
void createDemo(Fixture& fixture);

/// Minimal .mdl (studiohdr_t, version 48) with the given sequences and an optional $includemodel.
[[nodiscard]] std::vector<std::byte> makeModelFile(const std::vector<std::string>& sequences, const std::string& include);

/// Creates an RGBA8888 VTF (solid color with a darker checker) of the given size.
[[nodiscard]] std::vector<std::byte> makeVtf(unsigned size, unsigned char r, unsigned char g, unsigned char b, bool dxt = false);

} // namespace sme::test
