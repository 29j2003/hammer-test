// texture_flow.h - Texture flow: lays a texture so it runs on correctly from face to face.
//
// One tool for "the texture should continue over to the neighbour, at the right angle", with modes:
// - Wrap: from a seed face the texture is folded over every common edge onto the next face, as a
//   sheet of paper would be (pillars, boxes, wall corners, stairs, bevelled or rounded edges,
//   cylinders, arches). The texture axes turn about the edge; the texel along the edge is the same
//   on both faces. Closed loops (a pillar's four sides) meet with one seam, opposite the seed.
// - Along a path: strips (roads, kerbs, rivers, trims, fences) get the texture along their chain,
//   one scale, centred, on through corners. Floors (Road, Kerb, River presets) use the road tool
//   (road_texture.h, corners cut into curves); standing strips (Trim, Fence presets) get the
//   texture's height fitted to the strip and wrapped round the corners.
// - Around a curve: faces of a rounded edge, arch, cylinder or sphere - V runs along the curve's
//   axis, U round it by arc length (a brick wall continues round a rounded corner).
// - Match neighbour: the selected faces continue the texture of the faces next to them that are
//   not selected (the same material first).
// Auto picks the mode from the faces. The plan is applied as one undo step.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/road_texture.h>

namespace sme {

/// How the texture flows (see the file comment).
enum class FlowMode { Auto, Wrap, Path, Curve, Match };

/// What runs along a path: floors (Road, Kerb, River) or standing strips (Trim, Fence).
enum class FlowPreset { Road, Kerb, River, Trim, Fence };

/// Settings of the texture flow tool.
struct FlowOptions {
	FlowMode mode = FlowMode::Auto;
	FlowPreset preset = FlowPreset::Road;   ///< For Path (Auto picks Road for floors, Trim for standing strips)
	std::string material;                   ///< Empty: the seed's material (Match: the neighbour's)
	bool flip = false;                      ///< Path: the texture runs from the other end
	int repeatsAcross = 1;                  ///< Path: how often the texture fits across the strip
	bool curveCorners = true;               ///< Path on floors: cut square corners into curves
	std::optional<FaceRef> seed;            ///< Wrap / Curve: the face the texture starts from (else the first face given)
};

/// What the tool would do (planTextureFlow).
struct FlowPlan {
	FlowMode mode = FlowMode::Wrap;         ///< The mode used (never Auto)
	FlowPreset preset = FlowPreset::Road;
	RoadPlan plan;                          ///< Faces with their new sides (and, for floors, corner pieces)
	std::optional<FaceRef> seed;            ///< Where Wrap / Curve / Trim started
	int seams = 0;                          ///< Common edges where the texture does not meet (closed loops)
	int unreached = 0;                      ///< Faces not connected to the seed (or to any neighbour, Match)
	bool stretched = false;                 ///< Some face's texture is projected at a slant (the seed's axes do not lie in it)
	bool unevenStrips = false;              ///< Path: strips of different heights / widths share one scale
	double stripSize = 0;                   ///< Path on standing strips: the height the texture is fitted to

	[[nodiscard]] bool empty() const { return plan.empty(); }
	[[nodiscard]] int faceCount() const { return plan.faceCount(); }
};

/// The faces the tool works on: the selected faces (in the order they were picked; the first is
/// the seed), or every face of the selected brushes and brush entities.
[[nodiscard]] std::vector<FaceRef> flowFaces(const MapDocument& doc, const Selection& selection);

/// Plans the texture flow over `faces` (see the file comment). Curve pieces of road corners take
/// new ids from `ids` (as planRoad).
[[nodiscard]] FlowPlan planTextureFlow(const MapDocument& doc, const std::vector<FaceRef>& faces, const FlowOptions& options,
                                       const TextureSizeFn& textureSize, MapDocument* ids = nullptr);

/// The texture continued from `from` (a face with normal `fromNormal`) over the common edge
/// (a point on it and its direction) onto a face with normal `toNormal`: the axes turned about the
/// edge, the shifts so the texel along the edge is the same on both faces.
[[nodiscard]] Side foldTexture(const Side& from, const Vec3& fromNormal, const Vec3& toNormal, const Vec3& edgePoint, const Vec3& edgeDir);

namespace edit {

/// Applies a texture flow plan as one undo step ("Texture flow", or "Road" for floors).
[[nodiscard]] Change applyTextureFlow(MapDocument& doc, const FlowPlan& plan);

} // namespace edit
} // namespace sme
