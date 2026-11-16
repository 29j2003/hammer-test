// material_review.h - "Export materials for review": everything Mapsmith knows about the mod's
// materials, in a form someone else (a person, or Claude in a chat) can read and answer with a
// decision file (sme/material_decisions.h): contact sheets of the textures (drawn by the app), an
// index (CSV and JSON) with the shader, size, flags and Mapsmith's own automatic decisions per
// material (role in a building, windows per tile, relief pattern, parallax now and what Mapsmith
// would do), and a README that says what to send back. This module chooses the materials, reads
// their facts and writes the text files; the app draws the sheets and packs the ZIP parts.

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <sme/facade_texture.h>
#include <sme/material_decisions.h>
#include <sme/texture.h>

namespace sme {

class GameFileSystem;

/// Which materials to review.
struct ReviewScope {
	enum class Kind {
		MapMaterials,   ///< The brush materials of the mod's maps (and the open map)
		Folder,         ///< Every brush material whose name starts with `folder` ("brick/", "building_template")
		AllWorld,       ///< Every brush (world) material of the game and the mod
	};
	Kind kind = Kind::MapMaterials;
	std::string folder;
	bool tools = false;    ///< tools/ (nodraw, clip ...)
	bool skybox = false;   ///< skybox/
	bool decals = false;   ///< decals/, overlays/ and $decal materials
};

/// Whether a material name is left out by the scope's switches (tools, sky, decals).
[[nodiscard]] bool reviewSkips(std::string_view material, const ReviewScope& scope);
/// The materials to review, sorted by folder, then name ("brick/brickwall01"). `mapMaterials`:
/// the materials of the mod's maps (for Kind::MapMaterials). Model materials are never listed.
[[nodiscard]] std::vector<std::string> reviewMaterialList(const GameFileSystem& fs, const ReviewScope& scope, const std::set<std::string>& mapMaterials);

/// What Mapsmith knows about one material.
struct ReviewEntry {
	int number = 0;                    ///< 1-based, as on the sheets
	std::string name;                  ///< "brick/brickwall031d"
	std::string shader;
	std::string baseTexture, bumpTexture;
	std::string surfaceprop;
	int width = 0, height = 0;         ///< Of the base texture
	std::vector<std::string> flags;    ///< bump, ssbump, parallax, envmap, blend, translucent, alphatest, selfillum, detail, phong, decal
	TextureAnalysis analysis;          ///< What the picture is for in a building (with the decision file applied)
	std::string reliefPattern;         ///< "bricks in a running bond: 12 courses of 4 ..." or "none"
	double courseTexels = 0;           ///< Height of one course in the full texture
	double unitTexels = 0;             ///< Width of one unit (brick, tile) in the full texture
	double reliefConfidence = 0;
	std::string parallaxNow;           ///< "on (depth 0.006)" / "off" - the material as the game loads it now
	std::string parallaxAuto;          ///< What Mapsmith would do by itself: "on: ..." / "off: ..."
	std::vector<std::string> effects;  ///< Game enhancements the material could get ("relief", "water" ...)
	std::string decision;              ///< What the decision file says now (keysText), "" = nothing
	ImageRgba base;                    ///< Picture for the sheet (at least `thumb` on its longer side when the texture is that big)
	ImageRgba bump;                    ///< Normal / height map picture (small), empty when none
	std::string error;                 ///< Why something could not be read
};

/// Reads one material's facts (and its pictures for the sheet: `thumb` texels, the bump map a
/// third of that). `decisions` may be null.
[[nodiscard]] ReviewEntry reviewEntry(const GameFileSystem& fs, std::string_view material, const MaterialDecisions* decisions, unsigned thumb = 384);

/// The index as CSV (one row per material, a header row first) and as JSON (an array of objects).
[[nodiscard]] std::string reviewIndexCsv(const std::vector<ReviewEntry>& entries);
[[nodiscard]] std::string reviewIndexJson(const std::vector<ReviewEntry>& entries);
/// The README of an export: what it is and what to answer (the decision file format).
[[nodiscard]] std::string reviewReadme(int materials, int sheets, int parts, std::string_view game);

/// The materials of each sheet (`perSheet` per sheet, in order).
[[nodiscard]] std::vector<std::vector<std::size_t>> reviewSheets(std::size_t count, std::size_t perSheet = 12);
/// Files of these sizes packed into parts of at most `limit` bytes, in order (`fixed`: bytes every
/// part carries anyway - index and README). A file bigger than a part gets a part of its own.
[[nodiscard]] std::vector<std::vector<std::size_t>> reviewParts(const std::vector<std::uint64_t>& sizes, std::uint64_t limit, std::uint64_t fixed = 0);
/// "sheet_001.jpg"
[[nodiscard]] std::string reviewSheetName(std::size_t sheet);
/// The flags as one short line for a sheet label ("bump · parallax · envmap").
[[nodiscard]] std::string reviewFlagsText(const ReviewEntry& entry);

} // namespace sme
