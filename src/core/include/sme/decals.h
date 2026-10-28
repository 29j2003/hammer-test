// decals.h - Where infodecal and info_overlay entities show up on the brushes, as the game
// shows them: textured polygons clipped to the faces they lie on (pure geometry, no Qt).
//
// infodecal: the engine projects the decal's texture (texture size x $decalscale, centered on
// the entity) onto the surface at the entity. Its axes are world aligned like the engine's
// (map decals have no axis of their own): on floors and ceilings S runs along +X, on walls S is
// horizontal and T points straight down; always upright and not mirrored seen from the front
// (T = S x normal). Here it lands on the nearest face that faces the
// entity and on faces in the same plane next to it; the engine also wraps decals around nearby
// corners, which the editor leaves out.
//
// info_overlay: VBSP's overlay from BasisOrigin/U/V/Normal and the corners uv0..uv3 (in basis
// space), with texture coordinates StartU/EndU/StartV/EndV in VBSP's corner order
// (uv0: StartU StartV, uv1: StartU EndV, uv2: EndU EndV, uv3: EndU StartV), projected along the
// basis normal onto each face in "sides" and clipped to it. Overlays on displacements use the
// flat base face here.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

class GameFileSystem;

/// Size in units of an infodecal with this material: its base texture's size x $decalscale,
/// read from the VMT and the VTF header only. nullopt if the material or texture is missing.
[[nodiscard]] std::optional<std::pair<double, double>> decalSize(const GameFileSystem& fs, std::string_view material);

/// One brush face the patches can land on.
struct SurfaceFace {
	int sideId = 0;
	Vec3 normal;                ///< Outward unit normal
	double dist = 0;            ///< dot(normal, point) for points on the face
	std::vector<Vec3> polygon;  ///< Convex, counter-clockwise seen from the front
	Bounds bounds;
};

/// A textured polygon lying on one brush face.
struct SurfacePatch {
	int sideId = 0;
	Vec3 normal;
	std::vector<Vec3> vertices;                 ///< Convex, counter-clockwise seen from the front
	std::vector<std::array<double, 2>> uv;      ///< Texture coordinates (0..1 across the texture)
};

/// The faces of every brush of the map (world and brush entities), flat (no displacements).
[[nodiscard]] std::vector<SurfaceFace> collectSurfaceFaces(const MapDocument& doc);

/// The engine's world-aligned decal axes on a surface with this normal: `s` to the right of the
/// texture, `t` down the texture.
void decalAxes(const Vec3& normal, Vec3& s, Vec3& t);

/// Clips a convex polygon with texture coordinates against a convex polygon in (about) the same
/// plane; both counter-clockwise seen from `normal`. Texture coordinates follow linearly.
[[nodiscard]] std::pair<std::vector<Vec3>, std::vector<std::array<double, 2>>> clipToPolygon(
    const std::vector<Vec3>& points, const std::vector<std::array<double, 2>>& uv, const std::vector<Vec3>& clip, const Vec3& normal);

/// Where an infodecal of this size (units) at `origin` lands.
[[nodiscard]] std::vector<SurfacePatch> decalPatches(const std::vector<SurfaceFace>& faces, const Vec3& origin, double width, double height);

/// An info_overlay's keys.
struct OverlayInfo {
	Vec3 origin, u{1, 0, 0}, v{0, 1, 0}, normal{0, 0, 1};
	std::array<Vec3, 4> corners;   ///< uv0..uv3: x along U, y along V
	double startU = 0, endU = 1, startV = 0, endV = 1;
	std::vector<int> sides;
};
/// Reads an info_overlay (nullopt without a usable basis or corners).
[[nodiscard]] std::optional<OverlayInfo> readOverlay(const Entity& entity);
/// The overlay's corners in the world (before projecting onto the faces).
[[nodiscard]] std::array<Vec3, 4> overlayCorners(const OverlayInfo& overlay);
/// Where the overlay lands: one or two patches per listed face it covers.
[[nodiscard]] std::vector<SurfacePatch> overlayPatches(const std::vector<SurfaceFace>& faces, const OverlayInfo& overlay);

/// Keys of a new info_overlay of `width` x `height` units centered at `point` on a face: basis
/// from the face (U to the right, V down the texture as seen from the front, so the texture is
/// upright and not mirrored), the face as its only side, the whole texture once.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> overlayKeysOnFace(const Vec3& point, const Vec3& faceNormal, int sideId,
                                                                                const std::string& material, double width, double height);

} // namespace sme
