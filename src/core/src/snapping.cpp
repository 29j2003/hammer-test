// snapping.cpp - Snapping onto other objects (see snapping.h).

#include <sme/snapping.h>

#include <sme/csg.h>

#include <algorithm>
#include <cmath>

namespace sme {

std::vector<Vec3> snapCorners(const MapDocument& doc, const Selection& skip, const Selection& hidden, const EntityCatalog* catalog) {
	std::vector<Vec3> corners;
	const auto addSolid = [&](const Solid& solid, int entityId) {
		const SolidRef ref{entityId, solid.id};
		if (skip.contains(ref) || hidden.contains(ref)) return;
		const auto points = solidCorners(solid);
		corners.insert(corners.end(), points.begin(), points.end());
	};
	for (const auto& solid : doc.world.solids) addSolid(solid, 0);
	for (const auto& entity : doc.entities) {
		if (skip.containsEntity(entity.id) || hidden.containsEntity(entity.id)) continue;
		for (const auto& solid : entity.solids) addSolid(solid, entity.id);
		if (entity.solids.empty()) {
			const Bounds box = edit::entityBounds(entity, catalog);   // props: their model's box
			if (box.empty()) continue;
			for (int i = 0; i < 8; ++i) {
				corners.push_back({(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z});
			}
		}
	}
	return corners;
}

std::optional<Vec3> nearestCorner(const std::vector<Vec3>& corners, const std::function<std::optional<double>(const Vec3&)>& distance, double radius) {
	std::optional<Vec3> best;
	double bestDistance = radius;
	for (const Vec3& corner : corners) {
		if (const auto d = distance(corner); d && *d < bestDistance) {
			bestDistance = *d;
			best = corner;
		}
	}
	return best;
}

std::optional<PlaneSnap> snapFacePlane(const std::vector<Vec3>& corners, int axis, double raw, const Bounds& face, double tolerance, double reach) {
	std::optional<PlaneSnap> best;
	double bestAlong = 0, bestBeside = 0;
	for (const Vec3& corner : corners) {
		const double along = std::abs(corner[axis] - raw);
		if (along > tolerance) continue;
		// How far the corner lies beside the side (0 when it is in front of or behind it).
		double beside = 0;
		for (int a = 0; a < 3; ++a) {
			if (a == axis) continue;
			const double outside = std::max({face.min[a] - corner[a], corner[a] - face.max[a], 0.0});
			beside = std::max(beside, outside);
		}
		if (beside > reach) continue;
		constexpr double SAME = 1e-9;
		if (!best || along < bestAlong - SAME || (along <= bestAlong + SAME && beside < bestBeside)) {
			best = PlaneSnap{corner[axis], corner};
			bestAlong = along;
			bestBeside = beside;
		}
	}
	return best;
}

Bounds resizedBounds(const Bounds& start, int axis, bool isMax, double value, double minimum) {
	Bounds out = start;
	if (isMax) {
		out.max[axis] = std::max(start.min[axis] + minimum, value);
	} else {
		out.min[axis] = std::min(start.max[axis] - minimum, value);
	}
	return out;
}

} // namespace sme
