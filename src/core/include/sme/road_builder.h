// road_builder.h - Road builder: builds a road's brushes along a path and fits them into the map.
//
// The road tool (road_texture.h) only lays a texture onto floors that are already there. The
// builder makes the road itself, the way roads are built by hand in Hammer (block the road in
// with brushes 256 units wide for two lanes, 1024 for four; turns from arch pieces with the
// texture aligned to each piece; a plain piece where roads meet; displacements for a raised
// middle), but in one step and with the texture already continuous:
// - Path: points along the road's centre line (clicked in the views, or taken from selected floor
//   brushes). Every corner becomes a curve: arch pieces round a circle whose inner edge has the
//   chosen radius, each piece's texture runs along it with ONE scale for the whole road, the
//   texture's middle on the centre line and the shift carried on, so the lane lines run through
//   the curve without jumps. Heights follow the points (ramps, sloped curves).
// - Displacement (optional): every road top becomes a displacement (power 2 or 3) with the middle
//   raised (crown) and a little unevenness; neighbouring pieces meet exactly (sewn by
//   construction) and stay editable with the displacement tool. Nodraw brushes under them seal
//   the map (displacements do not).
// - Kerbs (optional): strips beside the road that follow it round the curves.
// - Junctions: where the path meets or crosses a road that is already there (a brush with the
//   road material on top at the same height) the new road stops at its edge, the old road runs
//   through; with a junction material the old road's piece in the crossing gets it (plain
//   asphalt, no lane lines).
// - Fit into surroundings: brushes the road reaches into (the floor it is sunk into, a sidewalk
//   in a curve's way) are carved around it, up to `clearance` above the road; left-over pieces
//   are merged again where they form one brush. Selected floor brushes the road is built from are
//   replaced: what the curves leave of them stays as fill (with the fill material on top).
// - Never refuse: a piece that would be broken is left out and the plan says so; a corner too
//   tight for the radius gets the largest curve that fits, or a mitred joint.
// The plan is computed without changing the map (live preview); applying it is one undo step.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/road_texture.h>

namespace sme {

/// A road's centre line: points in order (their height is the road's top there).
struct RoadPath {
	std::vector<Vec3> points;
};

/// Settings of the road builder.
struct RoadBuildOptions {
	double width = 256;              ///< Kerb to kerb (256: two lanes, 1024: four)
	double thickness = 16;           ///< Brush thickness below the road's top
	double innerRadius = 128;        ///< Radius of a curve's inner edge (0: as tight as it gets)
	int segments = 8;                ///< Pieces per 90 degrees of a curve
	std::string material;            ///< Road top
	std::string sideMaterial;        ///< Road's open sides and ends; empty: the road material
	RoadAlong along = RoadAlong::Auto;
	bool flip = false;               ///< The texture runs from the other end
	int repeatsAcross = 1;           ///< How often the texture fits across the width
	std::string junctionMaterial;    ///< Where the road meets another: the crossing piece gets it; empty: the old road runs through
	bool displacement = false;       ///< Road tops become displacements
	int power = 3;                   ///< Displacement power (2: 5 x 5, 3: 9 x 9 vertices)
	double crown = 4;                ///< Displacement: the middle this much higher than the edges
	double roughness = 0;            ///< Displacement: unevenness in units
	bool fit = true;                 ///< Carve the brushes the road reaches into
	double clearance = 128;          ///< Fit: carved this high above the road
	std::string fillMaterial;        ///< From selection: top of the old floor left beside a curve; empty: as it was
	bool kerbs = false;
	double kerbWidth = 16;
	double kerbHeight = 8;
	std::string kerbMaterial;        ///< Empty: the side material
};

/// What building the road would do.
struct RoadBuildPlan {
	/// A brush that changes: the pieces that stand in for it (empty: it goes away).
	struct Replacement {
		SolidRef ref;
		std::vector<Solid> pieces;
	};
	std::vector<Solid> road;                  ///< Road pieces (and the nodraw seals under displacements)
	std::vector<Solid> kerbs;
	std::vector<Replacement> changed;         ///< Carved neighbours, split old roads, replaced selection
	std::vector<std::vector<Vec3>> centreLines;   ///< The centre line of each path as built (for drawing)
	std::vector<std::string> notes;           ///< What was limited or left out, one line each
	std::string material;                     ///< The road material used
	int curves = 0;                           ///< Corners turned into curves
	int mitred = 0;                           ///< Corners too tight for a curve
	int junctions = 0;                        ///< Old road pieces the road meets
	int carved = 0;                           ///< Neighbours carved
	int displacements = 0;                    ///< Road tops that became displacements
	int seals = 0;                            ///< Nodraw brushes under the displacements (in `road`)
	int skipped = 0;                          ///< Pieces left out (would be broken)
	double radius = 0;                        ///< Inner radius used (smallest of all curves)

	[[nodiscard]] bool ok() const { return !road.empty(); }
	/// Every brush as it would be afterwards (road, kerbs, changed neighbours), for a preview.
	[[nodiscard]] std::vector<Solid> resultSolids() const;
	/// Brushes that change or go away (hidden while the preview stands in for them).
	[[nodiscard]] std::vector<SolidRef> replacedRefs() const;
};

/// Road paths from selected floor brushes: long pieces give the centre line, square pieces
/// between them are corners, a piece with three or more neighbours a junction (the longest
/// road is built first, the others join it). `suggested` gets the width, thickness and material
/// the pieces have (others unchanged). Empty if the brushes have no floor to go on.
[[nodiscard]] std::vector<RoadPath> roadPathsFromSolids(const MapDocument& doc, const std::vector<SolidRef>& solids, RoadBuildOptions* suggested = nullptr,
                                                        std::vector<std::string>* notes = nullptr);

/// Plans the road along `paths` (see the file comment). `replace`: brushes the road is built from
/// (they are carved by the road; what is left stays as fill). Reads `doc` only; new ids come from
/// `ids` (the document itself when applying, a scratch document for a preview).
[[nodiscard]] RoadBuildPlan planRoadBuild(const MapDocument& doc, MapDocument& ids, const std::vector<RoadPath>& paths, const RoadBuildOptions& options,
                                          const TextureSizeFn& textureSize, const std::vector<SolidRef>& replace = {});

/// The road's top at a point of a piece (centre line height plus crown and unevenness): what a
/// displacement vertex of the road is moved to. Exposed for tests.
[[nodiscard]] double roadCrownOffset(double across, double halfWidth, double crown, double roughness, double x, double y);

namespace edit {

/// Applies a road plan (made from the current document) as one undo step "Build road"; returns
/// the change and the new road's brushes (to select).
[[nodiscard]] std::pair<Change, std::vector<SolidRef>> applyRoadBuild(MapDocument& doc, const RoadBuildPlan& plan);

} // namespace edit
} // namespace sme
