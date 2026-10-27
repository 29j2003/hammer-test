// fixture.h - Builds a small fake Steam installation on disk for tests and screenshots.
//
// Contains Half-Life 2 (two Steam libraries), the Source SDK Base 2013 Singleplayer,
// a Mapbase-style sourcemod, VPKs, VMT/VTF materials, FGDs and a Hammer++ config.
// Everything is generated, so no Valve files are needed or committed.

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sme::test {

/// A fake Steam installation on disk for tests (no Valve files): Steam root, libraries, games and mods.
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
[[nodiscard]] std::vector<std::byte> makeModelFile(const std::vector<std::string>& sequences, const std::string& include,
                                                   const std::string& texture = "combine_soldier_sheet",
                                                   const std::string& cdmaterials = "models/combine_soldier/");

/// A complete model (.mdl + .vvd + .dx90.vtx) made of boxes {min x y z, max x y z}, one material.
struct StudioModelFiles {
	std::vector<std::byte> mdl, vvd, vtx;
};
/// Builds the model files (see StudioModelFiles).
[[nodiscard]] StudioModelFiles makeStudioModel(const std::vector<std::array<float, 6>>& boxes, const std::vector<std::string>& sequences,
                                               const std::string& include, const std::string& texture, const std::string& cdmaterials);
/// Writes the three files next to each other ("models/x.mdl" -> x.mdl, x.vvd, x.dx90.vtx).
void writeStudioModel(const std::filesystem::path& mdlPath, const StudioModelFiles& files);

/// A particle file (.pcf, binary DMX) holding particle systems with these names.
[[nodiscard]] std::vector<std::byte> makePcf(const std::vector<std::string>& systems);
/// The demo's particle files: a fire with a smoke child, and a burst of sparks (with emitters,
/// initializers, operators and renderers like the game's).
[[nodiscard]] std::vector<std::byte> makeDemoFirePcf();
[[nodiscard]] std::vector<std::byte> makeDemoSparksPcf();

/// Creates an RGBA8888 VTF (solid color with a darker checker) of the given size.
[[nodiscard]] std::vector<std::byte> makeVtf(unsigned size, unsigned char r, unsigned char g, unsigned char b, bool dxt = false);

} // namespace sme::test
