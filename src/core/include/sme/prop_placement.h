// prop_placement.h - Many props at once: along a path (kerb stones, fences, lamp posts at a fixed
// spacing, turned with the curve) or scattered over surfaces (trees and rocks on terrain).
//
// Along a path: a prop every `spacing` units measured along the path (curves included), turned to
// face along it (plus an extra yaw), optionally shifted sideways (lamps beside the road, not on
// it). Scatter: random points on the chosen faces / displacements, `density` props per 512x512
// units, never steeper than `maxSlope`, never closer than `minSpacing` to each other, and kept
// `keepAway` units off the brushes to avoid (a road). Random choices come from a seed, so the
// same settings always give the same result (the preview matches what is placed).
// The core only computes placements; entities are made by makePropEntities, applied as one undo
// step by applyPropPlacements.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sme/editing.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/math.h>

namespace sme {

/// One prop: where it stands, how it is turned (degrees, Source order pitch yaw roll), its scale.
struct PropPlacement {
	Vec3 origin;
	double pitch = 0;
	double yaw = 0;
	double roll = 0;
	double scale = 1;
};

struct PathPropOptions {
	double spacing = 128;      ///< Units between two props along the path (at least 1)
	double startOffset = 0;    ///< Distance along the path before the first prop
	double sideOffset = 0;     ///< Units to the right of the path (negative: left)
	double yawOffset = 0;      ///< Extra turn (a model facing sideways gets 90)
	bool alignToPath = true;   ///< Turned along the path; else all keep yawOffset
	bool includeEnd = false;   ///< One more prop at the very end of the path
	double randomYaw = 0;      ///< Up to this many degrees turned either way at random
	double scaleMin = 1, scaleMax = 1;
	std::uint32_t seed = 1;
	int maxCount = 2000;       ///< Never more props than this
};

/// Placements along the polyline `points` (two or more).
[[nodiscard]] std::vector<PropPlacement> propsAlongPath(const std::vector<Vec3>& points, const PathPropOptions& options);

/// A triangle of a surface props can be scattered on (counter-clockwise seen from above it).
struct ScatterTriangle {
	Vec3 a, b, c;
};

struct ScatterOptions {
	double density = 4;        ///< Props per 512x512 units of surface
	double maxSlope = 35;      ///< Degrees; steeper ground gets nothing
	double minSpacing = 48;    ///< Units between two props at least
	double keepAway = 0;       ///< Units off the brushes to avoid
	bool randomYaw = true;
	bool tiltWithGround = false;   ///< Pitch/roll follow the ground (rocks); else upright (trees)
	double scaleMin = 1, scaleMax = 1;
	std::uint32_t seed = 1;
	int maxCount = 1000;       ///< Never more props than this
};

/// Random placements on the triangles, away from `avoid`. `limited` (optional) is set when
/// maxCount cut the result short.
[[nodiscard]] std::vector<PropPlacement> scatterProps(const std::vector<ScatterTriangle>& surface, const std::vector<const Solid*>& avoid,
                                                      const ScatterOptions& options, bool* limited = nullptr);

/// The upward-facing triangles of the faces (brush faces as they are, displacements with their
/// sculpted shape). Faces of brushes that do not exist are skipped.
[[nodiscard]] std::vector<ScatterTriangle> scatterSurface(const MapDocument& doc, const std::vector<FaceRef>& faces);

/// The edges between pairs of faces, ordered into one path (the longest chain of joined edges),
/// for props along a selected kerb edge. Empty if no edge is shared.
[[nodiscard]] std::vector<Vec3> edgeChainPath(const MapDocument& doc, const std::vector<std::pair<FaceRef, FaceRef>>& edges);

/// Orders loose segments into the longest chain of joined segments (ends within 0.5 units).
[[nodiscard]] std::vector<Vec3> chainSegments(const std::vector<std::pair<Vec3, Vec3>>& segments);

/// Units below a brush's top: how far a point lies outside a convex brush (0 or less: inside).
[[nodiscard]] double distanceToSolid(const Solid& solid, const Vec3& point);

/// Point entities of `classname` with `model` at the placements ("angles", "origin", "model",
/// "modelscale" when not 1) and the class's `defaults` (from the FGD). Ids come from `doc`.
[[nodiscard]] std::vector<Entity> makePropEntities(MapDocument& doc, const std::vector<PropPlacement>& placements, const std::string& classname,
                                                   const std::string& model, const std::vector<std::pair<std::string, std::string>>& defaults = {});

/// Adds the entities as one undo step; returns the change and their ids (to select).
[[nodiscard]] std::pair<Change, std::vector<int>> applyPropPlacements(MapDocument& doc, std::vector<Entity> entities, const std::string& name);

/// Measure tool: what two points tell a mapper.
struct Measurement {
	double distance = 0;     ///< Straight line
	double horizontal = 0;   ///< Along the floor
	double height = 0;       ///< b above a (negative: below)
	double slope = 0;        ///< Degrees from horizontal (0..90)
	bool walkable = false;   ///< A slope the player walks up (Source: floor normal z >= 0.7, about 45.6 degrees)
	bool stepUp = false;     ///< The player steps up this height without jumping (18 units)
	bool jumpUp = false;     ///< The player gets up there with a crouch jump (about 56 units)
	bool playerFits = false; ///< A standing player fits through (72 units)
	bool crouchFits = false; ///< A crouching player fits through (36 units)
	bool playerWide = false; ///< Wide enough for the player (32 units)
};

/// Distances between `a` and `b` and what the player can do there.
[[nodiscard]] Measurement measure(const Vec3& a, const Vec3& b);

} // namespace sme
