// relief_pattern.h - Pattern recognition for relief: finds how a wall texture is laid (bricks in a
// running bond, tiles or blocks stacked in a grid, boards or courses without upright joints) and
// builds its height map from that layout - every brick, tile or board a raised face with a crisp
// bevel, the joints down - instead of from the brightness alone, which sank dark bricks and raised
// light mortar. Also decides which materials should get relief at all (no windows, glass, signs,
// grates, smooth plaster, metal sheets ...), by name and by what the texture looks like.
//
// The layout is found from the texture itself: the rows' brightness repeats with the course height
// (the number of courses is whole, the texture tiles), the joint is the narrow extreme of that
// repeat (lighter or darker mortar); the upright joints are found the same way inside each course,
// each course with its own offset (a running bond moves half a brick). Parts that do not look like
// bricks (a window's glass, a plaster panel: very dark, very bright or smooth) stay out of the
// pattern and flat. Textures without such a repeat (rough stone, plaster, rock) have no layout;
// their height comes from the brightness as before (heightFromColor).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/heightmap.h>
#include <sme/texture.h>

namespace sme {

struct Material;

/// How a texture is laid (see the file comment). Coordinates are texels of the analysed grid,
/// which has the size of the height map made from the same texture (a power of two, at most 512).
struct ReliefLayout {
	enum class Kind {
		None,          ///< no regular layout (stone, plaster, rock)
		RunningBond,   ///< courses of units, each course moved by about half a unit (bricks)
		Stacked,       ///< units in a grid, the upright joints in line (tiles, blocks)
		Offset,        ///< courses of units, each course moved its own way
		Courses,       ///< courses without upright joints (boards, siding, long courses)
	};
	Kind kind = Kind::None;
	bool transposed = false;           ///< The courses run upright (vertical boards): x and y swapped
	unsigned width = 0, height = 0;    ///< Size of the analysed grid
	double courseHeight = 0;           ///< Texels from one bed joint to the next (across the courses)
	double unitWidth = 0;              ///< Texels from one upright joint to the next (0: none)
	double bedPhase = 0;               ///< Centre of the first bed joint
	std::vector<double> headPhase;     ///< Per course: centre of its first upright joint
	double bedJoint = 0, headJoint = 0;   ///< Widths of the joints
	bool lightJoints = false;          ///< The mortar is lighter than the units
	double confidence = 0;             ///< 0..1: how clearly the layout repeats
	double coverage = 0;               ///< Share of the texture that shows the pattern
	std::vector<std::int32_t> units;   ///< Per texel: the unit's index, JOINT, or OUTSIDE
	int unitCount = 0;

	static constexpr std::int32_t JOINT = -1;     ///< A joint (mortar, groove)
	static constexpr std::int32_t OUTSIDE = -2;   ///< Not part of the pattern (window, panel): stays flat

	[[nodiscard]] bool found() const { return kind != Kind::None; }
	/// The unit at a texel of the grid (wraps: the texture repeats).
	[[nodiscard]] std::int32_t unitAt(int x, int y) const;
	/// The courses (rows of units) of the texture.
	[[nodiscard]] int courses() const;
	/// "Running bond: 12 courses of 4 bricks, light joints" (for the UI).
	[[nodiscard]] std::string describe() const;
};

/// Finds the layout of a base texture; `found()` is false when there is no clear one.
[[nodiscard]] ReliefLayout detectReliefLayout(const ImageRgba& baseTexture);

/// The height map of a found layout: units up with a rounded edge and a little of their grain,
/// joints a little lower (bricks 0.3, tiles 0.55), parts outside the pattern flat on top.
/// nullopt without a layout.
[[nodiscard]] std::optional<HeightMap> heightFromLayout(const ReliefLayout& layout, const ImageRgba& baseTexture);

/// The automatic height map of a wall without a normal map: from its layout when it has one,
/// else from the brightness (heightFromColor). `layout` (may be null) receives the layout found.
[[nodiscard]] std::optional<HeightMap> heightFromTexture(const ImageRgba& baseTexture, ReliefLayout* layout = nullptr, std::string* error = nullptr);

/// Sets a whole unit (the brick, tile or board at texel x, y of the layout's grid) to a zone in a
/// zone map of any size (`zones.size()` = zonesWidth * zonesHeight). Returns false when the texel
/// is a joint or outside the pattern.
bool setUnitZone(const ReliefLayout& layout, std::vector<std::uint8_t>& zones, unsigned zonesWidth, unsigned zonesHeight, int x, int y,
                 HeightZone zone);

/// Whether a material should get relief (real depth), and why not.
struct ReliefFit {
	bool suits = true;
	std::string reason;   ///< Why not ("window", "sign", "smooth surface" ...), empty when it suits
};

/// By the material's and its texture's names: windows, glass, signs, posters, labels, grates,
/// fences, doors, metal sheets, plaster, paper, cloth, screens, lights ... get none. A name that
/// tells of bricks, stone or tiles wins over a general word (a "brickwall_window" is a window).
[[nodiscard]] ReliefFit reliefFitByName(std::string_view materialName, std::string_view baseTexture = {});

/// By what the texture looks like: large flat panes (dark or bright glass, a big window), panes
/// divided by bars (a window's grid), smooth surfaces without any structure (plaster, paint, sheet
/// metal), or see-through texels.
[[nodiscard]] ReliefFit reliefFitByLook(const ImageRgba& baseTexture);

/// By the material's reflection mask ($envmapmask): bright over a large rectangle is a window's
/// glass (a mixed texture of bricks and a window).
[[nodiscard]] ReliefFit reliefFitByMask(const ImageRgba& envmapMask);

/// Both, for a loaded material (name rules first; `baseTexture` may be null).
[[nodiscard]] ReliefFit reliefFit(std::string_view materialName, const Material& material, const ImageRgba* baseTexture = nullptr);

class GameFileSystem;
/// reliefFit with the base texture read from the game (small: 128 texels). The verdict on a texture's
/// look is remembered per texture file, so the viewport and Game enhancements decide alike and
/// fast. With $envmap, the reflection mask counts too (reliefFitByMask). Used everywhere Mapsmith
/// decides about relief.
[[nodiscard]] ReliefFit reliefFitInGame(const GameFileSystem& fs, std::string_view materialName, const Material& material);

} // namespace sme
