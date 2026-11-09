// overlay_tools.h - Making and shaping info_overlays over several faces (no Qt).
//
// Hammer places an overlay on one face and leaves the rest to "Pick" + Ctrl+click. Roads, kerbs
// and junctions (World of Level Design's roads series, parts 2 and 7) need overlays that cover
// the seams between many brush faces: cracks, oil stains, a dirty kerb line round a curve.
//
// - overlayOnFaces: one overlay over all the given faces (its "sides"), the texture upright on
//   their average normal, tiled at the texture's own proportions or stretched once.
// - planOverlayStrip: a band of overlays along a chain of faces (pieces of a road, a kerb, a
//   path): one overlay per piece, corners mitred at the joints so there is neither a gap nor an
//   overlap, the texture running on along the whole length (StartU/EndU continue) and the same
//   side of the texture on the same side of the band. Each overlay lists the faces it lands on.
// - moveOverlayCorner: a corner of an overlay dragged to a point; faces in the same plane that
//   the overlay now reaches are added to its sides.
//
// VBSP allows 64 faces per overlay (OVERLAY_BSP_FACE_COUNT); more are dropped and reported.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sme/decals.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

struct EntityClass;

/// Faces VBSP accepts in one overlay's "sides".
inline constexpr std::size_t OVERLAY_MAX_SIDES = 64;

/// The keys of an info_overlay as stored in the VMF (BasisOrigin/U/V/Normal, uv0..uv3, Start/End, sides).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> overlayKeys(const OverlayInfo& overlay, const std::string& material);

/// One overlay over all `sideIds` (faces among `faces`). `textureAspect` = texture width /
/// height; `tileSize` > 0: the texture repeats every `tileSize` units across (and at its aspect
/// along) the overlay; 0: the whole texture once. Faces turned away from the average normal and
/// faces beyond 64 are left out (`dropped` counts them). nullopt if no face is left.
[[nodiscard]] std::optional<OverlayInfo> overlayOnFaces(const std::vector<SurfaceFace>& faces, const std::vector<int>& sideIds, double textureAspect,
                                                        double tileSize, int* dropped = nullptr);

/// How a strip lies along its faces.
struct OverlayStripOptions {
	/// Where the band runs: on the chain's middle line, or along its left / right edge (seen along the chain).
	enum class Align { Middle, Left, Right };
	Align align = Align::Middle;
	double width = 32;           ///< Across the band (units)
	double offset = 0;           ///< Middle: sideways (+ = left); Left/Right: inset from that edge
	double textureAspect = 1;    ///< Texture width / height: one repeat along is width * aspect long
	bool reverse = false;        ///< Run the chain the other way round (flips which side is "left")
};

/// The middle line through a chain of faces: the order of the faces (neighbours share an
/// edge), and points from the first face's far edge over the middles of the shared edges to the
/// last face's far edge, with the surface normal at each point. Faces not connected to the
/// longest run are left out. Empty if `sideIds` has no usable face.
struct FaceChainPath {
	std::vector<int> order;            ///< Side ids along the chain
	std::vector<Vec3> points;          ///< order.size() + 1 points
	std::vector<Vec3> normals;         ///< Per point (average of the faces there)
	std::vector<double> halfWidths;    ///< Per point: half the chain's width there (half the edge crossed)
};
[[nodiscard]] FaceChainPath faceChainPath(const std::vector<SurfaceFace>& faces, const std::vector<int>& sideIds);

/// The overlays of a strip along the chain (see the file comment). `allFaces` are every face of
/// the map: an overlay lands on any face under it, not only on the chain's own.
[[nodiscard]] std::vector<OverlayInfo> planOverlayStrip(const std::vector<SurfaceFace>& allFaces, const std::vector<int>& chainSideIds,
                                                         const OverlayStripOptions& options);

/// Corner `index` (0..3, uv0..uv3) moved to `point` (projected into the overlay's basis plane);
/// faces in the same plane as one of its faces that the overlay now covers join its sides.
[[nodiscard]] OverlayInfo moveOverlayCorner(const std::vector<SurfaceFace>& faces, const OverlayInfo& overlay, int index, const Vec3& point);

namespace edit {

/// Places the overlays as info_overlay entities with `material` (one undo step); ids of the new entities in `ids`.
[[nodiscard]] Change placeOverlays(MapDocument& doc, const std::vector<OverlayInfo>& overlays, const std::string& material, const EntityClass* definition,
                                   std::vector<int>* ids = nullptr);

} // namespace edit

} // namespace sme
