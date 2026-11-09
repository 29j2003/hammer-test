// displacement.h - Creating, sculpting and sewing displacements (Hammer's terrain surfaces).
//
// A displacement turns a four-sided brush face into a grid of (2^power + 1)^2 vertices. The
// VMF stores per vertex a direction ("normals") and a length ("distances"), an extra offset,
// an alpha for blended materials, plus tags per triangle; buildDisplacement() in brush.h turns
// that into positions. Here the data is read into a Displacement, changed, and written back as
// a dispinfo block exactly the way Hammer writes it, so VBSP compiles it.
//
// Sculpting works on the vertex grid around a brush (circle) position: raise or lower along
// the face normal, smooth, flatten to a height, add noise, paint the blend alpha. Sewing moves
// the vertices on edges that neighbouring displacements share onto the same spot.

#pragma once

#include <optional>
#include <vector>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// The editable content of a dispinfo block.
struct Displacement {
	int power = 3;                       ///< 2, 3 or 4: 5, 9 or 17 vertices per row
	Vec3 startPosition;                  ///< The face corner the grid starts at
	int flags = 0;
	double elevation = 0;
	bool subdiv = false;
	std::vector<Vec3> normals;           ///< Direction per vertex (row-major, size * size)
	std::vector<double> distances;       ///< Length along the direction
	std::vector<Vec3> offsets;           ///< Extra offset per vertex
	std::vector<Vec3> offsetNormals;
	std::vector<double> alphas;          ///< Blend between the two materials, 0..255
	std::vector<int> triangleTags;       ///< Two per grid cell, row by row (walkable, buildable)
	std::vector<int> allowedVerts;       ///< Ten bit fields; -1 = every vertex allowed

	/// Vertices per row.
	[[nodiscard]] int size() const { return (1 << power) + 1; }
	/// The displacement of a vertex (normal * distance).
	[[nodiscard]] Vec3 vector(std::size_t index) const { return normals[index] * distances[index]; }
	/// Sets the displacement of a vertex; a zero vector keeps the old direction.
	void setVector(std::size_t index, const Vec3& v);
};

/// The displacement of a side, or nullopt if the side has none (or its data is broken).
[[nodiscard]] std::optional<Displacement> readDisplacement(const Side& side);
/// Writes the displacement into the side's dispinfo (created if needed; keys the editor does not
/// know are kept).
void storeDisplacement(Side& side, const Displacement& displacement);
/// Removes the dispinfo of a side. False if it had none.
bool removeDisplacement(Side& side);

/// The face corners in the grid's order: clockwise seen from outside, starting at the corner
/// nearest to `start` (as VBSP orders them). Empty if the face does not have four corners.
[[nodiscard]] std::vector<Vec3> displacementCorners(const FaceGeometry& face, const Vec3& start);
/// The flat grid positions (no displacement) of a displacement on this face, row-major.
[[nodiscard]] std::vector<Vec3> displacementBase(const Displacement& displacement, const FaceGeometry& face);
/// Final vertex positions (base + vector + offset), row-major.
[[nodiscard]] std::vector<Vec3> displacementPositions(const Displacement& displacement, const FaceGeometry& face);

/// A flat displacement of `power` (2..4) on the face, raised by `elevation` along the face
/// normal. Nullopt if the face does not have four corners.
[[nodiscard]] std::optional<Displacement> makeDisplacement(const FaceGeometry& face, int power, double elevation = 0);
/// Recomputes the triangle tags from the current shape (walkable below 45°-ish, buildable flatter).
void updateTriangleTags(Displacement& displacement, const FaceGeometry& face);

/// Sculpt operations of the displacement tool.
enum class SculptMode { Raise, Smooth, Flatten, Noise, PaintAlpha };

/// The brush of one dab: a circle around `center` (measured in the face plane) with a smooth falloff.
struct SculptBrush {
	Vec3 center;
	double radius = 64;
	double strength = 8;       ///< Units per dab (Raise, Noise), 0..1 (Smooth, Flatten), alpha per dab (PaintAlpha)
	bool invert = false;       ///< Lower instead of raise, erase alpha instead of paint
	double flattenHeight = 0;  ///< Flatten: height above the face plane to move towards
	unsigned seed = 1;         ///< Noise
	/// Direction Raise, Noise and Flatten move the vertices in (unit length). Zero: the face
	/// normal (Hammer's default). Another axis (world X/Y/Z, the camera) pushes a slope or an
	/// embankment sideways, which the normal cannot.
	Vec3 axis;
	/// Flatten along `axis`: the point whose height along the axis everything moves towards.
	Vec3 flattenPoint;
};

/// How much a vertex at `distance` from the brush center is affected (1 at the center, 0 at the rim).
[[nodiscard]] double sculptFalloff(double distance, double radius);
/// Applies one dab to a displacement. Returns true if a vertex changed.
bool sculpt(Displacement& displacement, const FaceGeometry& face, SculptMode mode, const SculptBrush& brush);

/// How "Blend along selection" paints: full blend up to `width` units from the target brushes,
/// fading out over `softness` units; `noise` (units) frays the border so it does not look drawn
/// with a ruler.
struct BlendNearOptions {
	double width = 32;
	double softness = 64;
	double noise = 0;
	unsigned seed = 1;
	bool secondMaterialNear = true;   ///< Near the targets: the second material (alpha 255), else the first (alpha 0)
};

/// Distance from `point` to a convex brush (0 inside). Exact: nearest face, edge or corner.
[[nodiscard]] double distanceToBrush(const BrushGeometry& brush, const Vec3& point);
/// The blend alpha at `distance` from the targets (0..255; see BlendNearOptions).
[[nodiscard]] double blendNearAlpha(double distance, const BlendNearOptions& options);
/// Paints the blend alpha of a displacement by distance to the target brushes. Existing paint
/// toward the near material is kept (the stronger one wins), so painting twice does not undo
/// hand work. Returns true if a vertex changed.
bool blendNear(Displacement& displacement, const FaceGeometry& face, const std::vector<BrushGeometry>& targets, const BlendNearOptions& options);

/// One displacement with its face, for sewing.
struct SewItem {
	Displacement* displacement = nullptr;
	const FaceGeometry* face = nullptr;
};
/// Moves vertices that lie on the same spot of shared edges (same flat position) to their
/// average, so neighbouring displacements have no cracks. Returns how many vertices moved.
int sewDisplacements(const std::vector<SewItem>& items);

namespace edit {

/// Turns the faces into displacements of `power` (faces without four corners and faces that
/// already are displacements are skipped and counted in `skipped`). One undo step.
[[nodiscard]] Change createDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces, int power, double elevation = 0, int* skipped = nullptr);
/// Turns displacements back into flat faces. One undo step.
[[nodiscard]] Change destroyDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces);
/// Sews the displacements among `faces` together (see sewDisplacements). One undo step;
/// `moved` gets the number of vertices that moved.
[[nodiscard]] Change sewDisplacements(MapDocument& doc, const std::vector<FaceRef>& faces, int* moved = nullptr);
/// "Blend along selection": the selected brushes that are not displacements (a road, a path) are
/// the targets; the selected displacements, or, if none is selected, every displacement within
/// reach of the targets, get their blend alpha painted by distance (see blendNear). One undo
/// step; `count` gets the number of displacements changed.
[[nodiscard]] Change blendAlongSelection(MapDocument& doc, const Selection& selection, const BlendNearOptions& options, int* count = nullptr);
/// Every displacement face of the brushes (and brush entities) in the selection, plus selected faces that are displacements.
[[nodiscard]] std::vector<FaceRef> displacementFaces(const MapDocument& doc, const Selection& selection);

} // namespace edit

} // namespace sme
