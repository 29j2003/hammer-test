// brush.h - Brush geometry: face polygons from planes, texture coordinates, picking,
// creating and transforming brushes, displacement surfaces.
//
// Source 1 brushes are convex: every face plane cuts away everything outside the brush.
// All functions here keep that rule; isConvexValid() checks it after edits.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/map.h>
#include <sme/math.h>

namespace sme {

struct FaceGeometry {
	int sideIndex = 0;
	std::vector<Vec3> vertices;     ///< Counter-clockwise seen from outside (OpenGL front face)
	Vec3 normal;
};

struct BrushGeometry {
	std::vector<FaceGeometry> faces;
	Bounds bounds;
	bool valid = false;             ///< false if the planes do not form a closed convex solid
};

[[nodiscard]] BrushGeometry buildBrushGeometry(const Solid& solid);

/// Texture coordinates in texels (divide by texture size for 0..1), like the engine computes them.
struct TexCoord {
	double u = 0, v = 0;
};
[[nodiscard]] TexCoord textureCoordinate(const Side& side, const Vec3& point);

/// Ray against a convex brush: distance (>= 0) and side index, or nullopt.
struct BrushHit {
	double distance = 0;
	int sideIndex = 0;
};
[[nodiscard]] std::optional<BrushHit> intersect(const Ray& ray, const Solid& solid);

/// Hammer's default world-aligned texture axes for a face with this normal.
void alignToWorld(Side& side, double scale = 0.25);
/// Texture axes lying in the face plane (Hammer's "face" alignment).
void alignToFace(Side& side, double scale = 0.25);

/// Axis-aligned block with six sides (new ids from the document).
[[nodiscard]] Solid makeBlock(MapDocument& doc, const Bounds& box, const std::string& material);

/// Moves a brush; with texture lock the textures stay where they are on the faces.
void translateSolid(Solid& solid, const Vec3& delta, bool textureLock);
/// Rotates around `center`; texture lock keeps textures glued to the faces.
void rotateSolid(Solid& solid, const Mat3& rotation, const Vec3& center, bool textureLock);
/// Maps the brush from box `from` into box `to` (resize). Fails (returns false) for degenerate boxes.
bool scaleSolid(Solid& solid, const Bounds& from, const Bounds& to);

/// Copy with fresh solid and side ids.
[[nodiscard]] Solid cloneSolid(MapDocument& doc, const Solid& solid);

/// Closed, convex, every plane contributes a face, no degenerate faces.
[[nodiscard]] bool isConvexValid(const Solid& solid);

/// Displacement surface built from a side's dispinfo.
struct DisplacementMesh {
	int size = 0;                       ///< Vertices per row (2^power + 1)
	std::vector<Vec3> positions;        ///< size * size, row-major
	std::vector<float> alpha;           ///< Blend alpha 0..255 per vertex
};
[[nodiscard]] std::optional<DisplacementMesh> buildDisplacement(const Side& side, const FaceGeometry& face);

} // namespace sme
