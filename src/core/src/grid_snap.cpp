// grid_snap.cpp - Snapping brush corners to the grid (see grid_snap.h).

#include <sme/grid_snap.h>

#include <algorithm>
#include <cmath>

#include <sme/brush.h>
#include <sme/edge_round.h>

namespace sme {

namespace {

double snapValue(double v, double grid) {
	return std::round(v / grid) * grid;
}

Vec3 snapPoint(const Vec3& p, double grid) {
	return {snapValue(p.x, grid), snapValue(p.y, grid), snapValue(p.z, grid)};
}

bool onGrid(const Vec3& p, double grid, double tolerance) {
	return length(p - snapPoint(p, grid)) <= tolerance;
}

bool hasDisplacement(const Solid& solid) {
	return std::any_of(solid.sides.begin(), solid.sides.end(), [](const Side& s) { return s.dispInfo() != nullptr; });
}

/// Unique corners of the geometry (within a hundredth).
std::vector<Vec3> cornersOf(const BrushGeometry& geometry) {
	std::vector<Vec3> out;
	for (const auto& face : geometry.faces) {
		for (const auto& v : face.vertices) {
			if (std::none_of(out.begin(), out.end(), [&](const Vec3& o) { return length(o - v) < 0.01; })) out.push_back(v);
		}
	}
	return out;
}

} // namespace

int offGridCorners(const Solid& solid, double grid, double tolerance) {
	if (grid <= 0) return 0;
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid) return 0;
	int count = 0;
	for (const auto& corner : cornersOf(geometry)) count += onGrid(corner, grid, tolerance) ? 0 : 1;
	return count;
}

std::optional<Solid> snapSolidToGrid(const Solid& solid, double grid) {
	if (grid <= 0 || hasDisplacement(solid)) return std::nullopt;
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid) return std::nullopt;
	const auto before = cornersOf(geometry);
	if (std::all_of(before.begin(), before.end(), [&](const Vec3& c) { return onGrid(c, grid, 0.01); })) return std::nullopt;
	Solid out = solid;
	for (const auto& face : geometry.faces) {
		std::vector<Vec3> moved;
		for (const auto& v : face.vertices) {
			const Vec3 s = snapPoint(v, grid);
			if (std::none_of(moved.begin(), moved.end(), [&](const Vec3& o) { return length(o - s) < 0.01; })) moved.push_back(s);
		}
		if (moved.size() < 3) return std::nullopt;   // the face would vanish
		// The plane through the three moved corners spanning the largest triangle.
		double bestArea = 0;
		std::array<Vec3, 3> best{};
		for (std::size_t a = 0; a < moved.size(); ++a) {
			for (std::size_t b = a + 1; b < moved.size(); ++b) {
				for (std::size_t c = b + 1; c < moved.size(); ++c) {
					const double area = length(cross(moved[b] - moved[a], moved[c] - moved[a]));
					if (area > bestArea) {
						bestArea = area;
						best = {moved[a], moved[b], moved[c]};
					}
				}
			}
		}
		if (bestArea < 1e-6) return std::nullopt;
		const Vec3 n = normalize(cross(best[1] - best[0], best[2] - best[0]));
		// Every moved corner must stay in that plane, else the face would bend.
		for (const auto& p : moved) {
			if (std::abs(dot(p - best[0], n)) > 0.01) return std::nullopt;
		}
		// Plane points as the VMF wants them: normal = cross(p0 - p1, p2 - p1) points out.
		Side& side = out.sides[static_cast<std::size_t>(face.sideIndex)];
		side.planePoints = {best[2], best[1], best[0]};
		if (dot(side.plane().normal, face.normal) < 0) side.planePoints = {best[0], best[1], best[2]};
	}
	// The result must be the same brush with its corners on the grid: same faces, same number of corners.
	const auto after = buildBrushGeometry(out);
	if (!after.valid || after.faces.size() != geometry.faces.size() || !brushProblem(out).empty()) return std::nullopt;
	const auto corners = cornersOf(after);
	if (corners.size() != before.size()) return std::nullopt;
	for (const auto& c : corners) {
		if (!onGrid(c, grid, 0.01)) return std::nullopt;
	}
	return out;
}

namespace edit {

Change snapToGrid(MapDocument& doc, const Selection& selection, double grid, int* snapped, int* refused) {
	Transaction t(doc, "Snap vertices to grid");
	int done = 0, left = 0;
	std::vector<SolidRef> refs = selection.solids;
	for (const int id : selection.entities) {
		if (const Entity* entity = doc.findEntity(id)) {
			for (const auto& solid : entity->solids) refs.push_back({id, solid.id});
		}
	}
	for (const auto& ref : refs) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid || offGridCorners(*solid, grid) == 0) continue;
		auto result = snapSolidToGrid(*solid, grid);
		if (!result) {
			++left;
			continue;
		}
		t.touchSolid(ref);
		*doc.findSolid(ref) = std::move(*result);
		++done;
	}
	if (snapped) *snapped = done;
	if (refused) *refused = left;
	return t.finish();
}

} // namespace edit

} // namespace sme
