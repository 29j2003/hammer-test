// facade_texture.h - Reads a texture and decides what it is for in a building, and how it is laid:
// a facade with its windows in the picture (where they are, how many per tile, how many floors one
// tile spans), a plain wall, a trim / cornice strip, a roof (tar, gravel, tiles), a shop front, or
// unsuitable (a far / skybox facade with tiny windows, a decal, a garish colour). The building
// builder takes its decisions from this (which part a material goes on, its scale in whole floors
// and whole windows, whether window geometry is built); names only break ties.
//
// Windows are found on a small luminance / saturation grid (at most 128 cells wide) of the texture:
// cells that differ clearly from the wall (robust: median and median deviation) are closed over thin
// mullions, the grid is rolled so a column and a row of plain wall lie at its edges (windows cut by
// the tile's edge come out whole), and rectangular blobs of similar size that sit in a regular grid
// are the windows. Bricks and tiles are too small (or too thin) to pass, mortar networks too big,
// stains too round.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <sme/texture.h>

namespace sme {

/// A window of a facade texture: 0..1 across one tile, v from the top. u1 may be above 1 (a window
/// cut by the tile's right edge continues in the next tile).
struct FacadeWindowRect {
	double u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	bool operator==(const FacadeWindowRect&) const = default;
};

/// What a texture is for in a building.
enum class TextureRole {
	Unknown,         ///< Not analysed (no picture)
	FacadeWindows,   ///< A wall with its windows in the picture: fitted in whole floors and windows, no window geometry
	Wall,            ///< A plain wall (brick, plaster, concrete): window geometry is built in it
	Trim,            ///< A strip (cornice, moulding, ledge): once over a band's height
	Roof,            ///< Tar, gravel, roof tiles
	ShopFront,       ///< A ground floor with big shop windows in the picture
	Unsuitable       ///< Far / skybox facade (tiny windows), decal or overlay, garish colour, too small
};

/// What the picture of a texture says.
struct TextureAnalysis {
	TextureRole role = TextureRole::Unknown;
	bool hasWindows = false;
	int columns = 0;                        ///< Windows across one tile
	int rows = 0;                           ///< Window rows down one tile (one per floor)
	std::vector<FacadeWindowRect> windows;  ///< Each window of one tile
	double windowShare = 0;                 ///< Part of the tile the windows cover
	double confidence = 0;                  ///< 0..1
	double brightness = 0;                  ///< Mean luminance 0..1
	double saturation = 0;                  ///< Mean colour saturation 0..1
	double alpha = 1;                       ///< Mean opacity
	double aspect = 1;                      ///< Width / height of the picture
	double anisotropy = 1;                  ///< Variance of the row means / of the column means (strips: high)
	double banded = 0;                      ///< Share of rows that are even across (strips: most of them)
	int width = 0, height = 0;              ///< Of the full texture (for the scale)
	std::string reason;                     ///< Why this role (one short sentence, English)
	double scaleOverride = 0;               ///< > 0: the scale the mod's decision file sets (scaleFor returns it)
	bool decided = false;                   ///< A line of the mod's decision file set something
	/// Floors one tile spans (window rows; at least 1).
	[[nodiscard]] int floorsPerTile() const { return rows > 0 ? rows : 1; }
	/// The scale (units per texel) at which one tile is a whole number of floors of `floorHeight`
	/// (facades), the usual 0.25 otherwise.
	[[nodiscard]] double scaleFor(double floorHeight) const;
	/// "facade · 2 windows per tile · 1 floor per tile · scale 0.5" (for the dialog).
	[[nodiscard]] std::string describe(double floorHeight = 128) const;
};

/// Analyses a texture's picture (any size; `fullWidth` / `fullHeight`: the texture's real size
/// when `image` is a smaller mip, 0: the image's). `name` (the material) only breaks ties.
[[nodiscard]] TextureAnalysis analyzeTexture(const ImageRgba& image, std::string_view name = {}, int fullWidth = 0, int fullHeight = 0);

/// The role's name for the UI ("facade with windows", "plain wall" ...).
[[nodiscard]] const char* textureRoleName(TextureRole role);
/// Back from a stored name ("FacadeWindows" ...); Unknown if it is none.
[[nodiscard]] TextureRole textureRoleFromKey(std::string_view key);
[[nodiscard]] const char* textureRoleKey(TextureRole role);

/// The windows as text ("u0 v0 u1 v1;...") and back (kept with a building's settings).
[[nodiscard]] std::string encodeFacadeWindows(const std::vector<FacadeWindowRect>& windows);
[[nodiscard]] std::vector<FacadeWindowRect> decodeFacadeWindows(std::string_view text);

} // namespace sme
