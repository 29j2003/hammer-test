// road_texture.h - Road tool: lays a road material along a chain of floor faces.
//
// The top faces of the selected brushes (or the selected faces) are put into chains: two faces
// belong together when they share an edge. Along each chain the texture runs with the road -
// across exactly the road's width (n repeats), along it with square pixels and the shift
// carried on from piece to piece, so the lane line runs on without jumps. Pieces of a curve
// (round-edge strips, arch strips) get their axis along the curve's centre line; slopes are
// measured along the slope, so ramps are not stretched. Walls and bottoms are left alone, the
// brushes' sides optionally get a kerb material. Everything is one undo step.

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <sme/editing.h>
#include <sme/filesystem.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// Which texture axis runs along the road: from the texture's shape (Auto: the longer side,
/// V for square textures), or U / V.
enum class RoadAlong { Auto, U, V };

/// Settings of the road tool.
struct RoadOptions {
	std::string material;       ///< Road material; empty = the one most of the faces already have
	RoadAlong along = RoadAlong::Auto;
	bool flip = false;          ///< Turn the texture around (runs from the other end)
	int repeatsAcross = 1;      ///< How often the texture repeats across the road's width
	std::string sideMaterial;   ///< Optional material for the brushes' sides (kerbs); empty = leave them
};

/// What the road tool would do (planRoad): new texture values per face.
struct RoadPlan {
	struct FaceChange {
		FaceRef face;
		Side side;              ///< The side with its new material and texture axes
		int chain = 0;          ///< Which chain it belongs to (0 based)
	};
	std::vector<FaceChange> faces;
	std::vector<FaceRef> sideFaces;   ///< Brush sides that get RoadOptions::sideMaterial
	std::string material;             ///< The road material used
	std::string sideMaterial;
	int chains = 0;
	int ignored = 0;                  ///< Faces left out (walls, bottoms, broken brushes)
	bool alongV = true;               ///< The texture's V axis runs along the road

	[[nodiscard]] bool empty() const { return faces.empty(); }
};

/// Texture size (width, height in texels) of a material.
using TextureSizeFn = std::function<std::pair<int, int>(const std::string& material)>;

/// Size of a material's base texture in texels, read from the VTF header (the full size, not a
/// preview's); 512 x 512 when the material or its texture cannot be read.
[[nodiscard]] std::pair<int, int> materialTextureSize(const GameFileSystem& fs, std::string_view material);

/// The upward faces (normal at most 60° from straight up) of the selected brushes and brush
/// entities, or the selected faces in face mode (all of them; planRoad leaves out the rest).
[[nodiscard]] std::vector<FaceRef> roadFaces(const MapDocument& doc, const Selection& selection);

/// Plans the road over `faces` (see the file comment). Faces that do not face up are counted
/// in `ignored`. The result does not depend on the order of `faces`.
[[nodiscard]] RoadPlan planRoad(const MapDocument& doc, const std::vector<FaceRef>& faces, const RoadOptions& options,
                                const TextureSizeFn& textureSize);

/// Copies of the brushes the plan changes, already changed (for a live preview); `replaced`
/// gets the brushes they stand in for.
[[nodiscard]] std::vector<Solid> roadPreview(const MapDocument& doc, const RoadPlan& plan, std::vector<SolidRef>* replaced);

namespace edit {

/// Applies a road plan as one undo step ("Road").
[[nodiscard]] Change applyRoad(MapDocument& doc, const RoadPlan& plan);

} // namespace edit
} // namespace sme
