// road_texture.h - Road tool: lays a road material along a chain of floor faces.
//
// The top faces of the selected brushes (or the selected faces) are put into roads. Two faces
// belong together when they share an edge. One road gets ONE mapping: one scale for all of it
// (from the road's width - the typical piece width, or a width given in units), the texture's
// middle on the road's centre line (carried from piece to piece, so a wider piece just shows
// more of the texture's edge) and the shift along the road carried on across every common
// edge, so the lane line runs on without jumps. Pieces of a curve (round-edge strips, arch
// strips) get their axis along the curve. Slopes are measured along the slope.
// Square corner pieces never run diagonally: a corner continues the longer road, the other
// road starts at its edge. At a junction (a piece with three or more neighbours) the main
// road - the straightest pair of opposite neighbours, the widest of them - runs straight
// through, every branch starts at the junction's edge. Tops beside the road at another height
// (kerbs, sidewalks) are left out, small pieces at road height that hang on one road piece
// (fills of a rounded kerb corner, end caps) take that piece's mapping. Walls and bottoms are
// left alone; a kerb material can go onto the kerb tops and the brushes' sides. Everything is
// one undo step.

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
	std::string sideMaterial;   ///< Optional kerb material: the brushes' sides and the kerb tops beside the road; empty = leave them
	double roadWidth = 0;       ///< The road's width in units (the texture spans it); 0 = Auto (the typical piece width)
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
	int kerbs = 0;                    ///< Tops left out: kerbs and sidewalks beside the road at another height
	int edgePieces = 0;               ///< Small pieces at road height that took their neighbour's mapping
	std::vector<double> widths;       ///< The width (units) each chain's texture spans
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
/// in `ignored`, kerb tops in `kerbs`. The result does not depend on the order of `faces`.
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
