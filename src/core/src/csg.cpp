// csg.cpp - Convex brush construction and cutting (see csg.h).

#include <sme/csg.h>

#include <sme/brush.h>

#include <algorithm>
#include <cmath>

namespace sme {

namespace {

constexpr double EPS = 0.01;          // distance tolerance in units
constexpr std::size_t MAX_HULL_POINTS = 160;

bool samePlane(const Plane& a, const Plane& b) {
	return dot(a.normal, b.normal) > 0.99999 && std::abs(a.dist - b.dist) < EPS;
}

double snapCoordinate(double v) {
	const double r = std::round(v);
	return std::abs(v - r) < 1e-3 ? r : std::round(v * 1000) / 1000;
}

void addEditorBlock(Solid& solid) {
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "0 175 236");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	solid.extra.push_back(std::move(editor));
}

std::array<Vec3, 3> planePointsOf(const Plane& plane) {
	// Two directions in the plane with U x V = normal, so (p0 - p1) x (p2 - p1) points outwards.
	const Vec3 n = plane.normal;
	const Vec3 helper = std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
	const Vec3 u = normalize(cross(helper, n));
	const Vec3 v = cross(n, u);
	const Vec3 p1 = n * plane.dist;
	return {p1 + u * 64.0, p1, p1 + v * 64.0};
}

void copyTexture(Side& side, const Side& from) {
	side.material = from.material;
	side.u = from.u;
	side.v = from.v;
	side.rotation = from.rotation;
	side.lightmapScale = from.lightmapScale;
	side.smoothingGroups = from.smoothingGroups;
	// Axes along the new face normal would smear the texture: fall back to world alignment.
	const Vec3 n = side.plane().normal;
	if (std::abs(dot(normalize(side.u.axis), n)) > 0.99 || std::abs(dot(normalize(side.v.axis), n)) > 0.99) {
		alignToWorld(side, side.u.scale != 0 ? std::abs(side.u.scale) : 0.25);
	}
}

/// Copy with fresh solid and side ids.
Solid freshIds(MapDocument& doc, Solid solid) {
	solid.id = doc.newSolidId();
	for (auto& side : solid.sides) {
		side.id = doc.newSideId();
	}
	return solid;
}

bool usable(const Solid& solid) {
	return isConvexValid(solid) && solidVolume(solid) > 1.0;
}

} // namespace

Side makeSide(MapDocument& doc, const Plane& plane, const std::string& material, const Side* textureFrom) {
	Side side;
	side.id = doc.newSideId();
	side.planePoints = planePointsOf(plane);
	if (textureFrom) {
		copyTexture(side, *textureFrom);
	} else {
		side.material = material;
		alignToWorld(side);
	}
	return side;
}

void removeUnusedSides(Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	std::vector<bool> used(solid.sides.size(), false);
	for (const auto& face : geometry.faces) {
		used[static_cast<std::size_t>(face.sideIndex)] = true;
	}
	std::vector<Side> kept;
	for (std::size_t i = 0; i < solid.sides.size(); ++i) {
		if (used[i]) {
			kept.push_back(std::move(solid.sides[i]));
		}
	}
	solid.sides = std::move(kept);
}

void tidyPlanePoints(Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	for (const auto& face : geometry.faces) {
		auto& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
		const Plane original = side.plane();
		std::vector<Vec3> corners;
		for (const auto& v : face.vertices) {
			corners.push_back({snapCoordinate(v.x), snapCoordinate(v.y), snapCoordinate(v.z)});
		}
		// The three corners spanning the largest triangle give the most exact plane.
		double best = 0;
		std::array<Vec3, 3> points = side.planePoints;
		for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
			for (std::size_t j = i + 1; j < corners.size(); ++j) {
				const double area = length(cross(corners[i] - corners[0], corners[j] - corners[0]));
				if (area > best) {
					best = area;
					points = {corners[i], corners[0], corners[j]};   // counter-clockwise from outside
				}
			}
		}
		Side test = side;
		test.planePoints = points;
		if (best > 0 && samePlane(test.plane(), original)) {
			side.planePoints = points;
		}
	}
}

std::vector<HullPlane> convexHull(const std::vector<Vec3>& input) {
	// Unique points (merging near duplicates).
	std::vector<Vec3> points;
	for (const auto& p : input) {
		if (std::none_of(points.begin(), points.end(), [&](const Vec3& q) { return length(p - q) < EPS; })) {
			points.push_back(p);
		}
	}
	std::vector<HullPlane> planes;
	std::vector<double> areas;
	if (points.size() < 4 || points.size() > MAX_HULL_POINTS) {
		return planes;
	}
	const std::size_t n = points.size();
	for (std::size_t i = 0; i < n; ++i) {
		for (std::size_t j = i + 1; j < n; ++j) {
			for (std::size_t k = j + 1; k < n; ++k) {
				Vec3 normal = cross(points[j] - points[i], points[k] - points[i]);
				const double area = length(normal);
				if (area < 1e-6) {
					continue;
				}
				normal = normal * (1.0 / area);
				double dist = dot(normal, points[i]);
				bool front = false, back = false;
				for (const auto& p : points) {
					const double d = dot(normal, p) - dist;
					front = front || d > EPS;
					back = back || d < -EPS;
					if (front && back) {
						break;
					}
				}
				if (front && back) {
					continue;
				}
				std::array<Vec3, 3> vmf{points[j], points[i], points[k]};   // (p0 - p1) x (p2 - p1) = normal
				if (front) {
					// All points in front: the outward normal is the other way round.
					normal = -normal;
					dist = -dist;
					vmf = {points[k], points[i], points[j]};
				}
				const Plane plane{normal, dist};
				const auto existing = std::find_if(planes.begin(), planes.end(), [&](const HullPlane& h) { return samePlane(h.plane, plane); });
				if (existing == planes.end()) {
					planes.push_back({plane, vmf});
					areas.push_back(area);
				} else if (area > areas[static_cast<std::size_t>(existing - planes.begin())]) {
					*existing = {plane, vmf};
					areas[static_cast<std::size_t>(existing - planes.begin())] = area;
				}
			}
		}
	}
	return planes.size() >= 4 ? planes : std::vector<HullPlane>{};
}

std::optional<Solid> solidFromPoints(MapDocument& doc, const std::vector<Vec3>& points, const std::string& material) {
	const auto hull = convexHull(points);
	if (hull.empty()) {
		return std::nullopt;
	}
	Solid solid;
	solid.id = doc.newSolidId();
	for (const auto& plane : hull) {
		Side side;
		side.id = doc.newSideId();
		side.planePoints = plane.points;
		side.material = material;
		alignToWorld(side);
		solid.sides.push_back(std::move(side));
	}
	addEditorBlock(solid);
	if (!usable(solid)) {
		return std::nullopt;
	}
	return solid;
}

std::pair<std::optional<Solid>, std::optional<Solid>> splitSolid(MapDocument& doc, const Solid& solid, const Plane& plane, const Side& capTexture) {
	const auto geometry = buildBrushGeometry(solid);
	double lowest = 1e30, highest = -1e30;
	for (const auto& face : geometry.faces) {
		for (const auto& v : face.vertices) {
			const double d = dot(plane.normal, v) - plane.dist;
			lowest = std::min(lowest, d);
			highest = std::max(highest, d);
		}
	}
	if (highest <= EPS) {
		return {std::nullopt, solid};   // all behind
	}
	if (lowest >= -EPS) {
		return {solid, std::nullopt};   // all in front
	}
	Solid back = solid;
	back.sides.push_back(makeSide(doc, plane, capTexture.material, &capTexture));
	removeUnusedSides(back);
	tidyPlanePoints(back);
	Solid front = freshIds(doc, solid);
	front.sides.push_back(makeSide(doc, Plane{-plane.normal, -plane.dist}, capTexture.material, &capTexture));
	removeUnusedSides(front);
	tidyPlanePoints(front);
	std::optional<Solid> frontPart, backPart;
	if (usable(front)) frontPart = std::move(front);
	if (usable(back)) backPart = std::move(back);
	return {std::move(frontPart), std::move(backPart)};
}

std::vector<Solid> subtractSolid(MapDocument& doc, const Solid& solid, const Solid& cutter) {
	const auto a = buildBrushGeometry(solid).bounds, b = buildBrushGeometry(cutter).bounds;
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] <= b.min[i] + EPS || b.max[i] <= a.min[i] + EPS) {
			return {solid};   // boxes do not overlap
		}
	}
	std::vector<Solid> out;
	std::optional<Solid> remaining = solid;
	bool cut = false;
	for (const auto& side : cutter.sides) {
		auto [front, back] = splitSolid(doc, *remaining, side.plane(), side);
		if (front && back) {
			cut = true;
		}
		if (front && !back) {
			// The rest lies outside the cutter: nothing to take away.
			if (!cut) {
				return {solid};
			}
			out.push_back(std::move(*front));
			return out;
		}
		if (front) {
			out.push_back(std::move(*front));
		}
		if (!back) {
			return out;
		}
		remaining = std::move(back);
	}
	return out;   // what remains is inside the cutter
}

std::vector<Solid> hollowSolid(MapDocument& doc, const Solid& solid, double thickness) {
	if (std::abs(thickness) < 0.5) {
		return {};
	}
	Solid shifted;
	shifted.id = doc.newSolidId();
	for (const auto& side : solid.sides) {
		Plane plane = side.plane();
		plane.dist -= thickness;   // inwards for walls inside, outwards (negative thickness) for walls outside
		shifted.sides.push_back(makeSide(doc, plane, side.material, &side));
	}
	removeUnusedSides(shifted);
	if (!usable(shifted)) {
		return {};
	}
	if (thickness > 0) {
		return subtractSolid(doc, solid, shifted);
	}
	// Walls outside: the grown brush minus the original.
	tidyPlanePoints(shifted);
	return subtractSolid(doc, shifted, solid);
}

std::optional<MergeResult> mergeSolids(MapDocument& doc, const std::vector<Solid>& solids) {
	if (solids.size() < 2) {
		return std::nullopt;
	}
	std::vector<Vec3> points;
	for (const auto& solid : solids) {
		for (const auto& face : buildBrushGeometry(solid).faces) {
			points.insert(points.end(), face.vertices.begin(), face.vertices.end());
		}
	}
	const auto hull = convexHull(points);
	if (hull.empty()) {
		return std::nullopt;
	}
	MergeResult result;
	result.solid.id = solids.front().id;
	result.solid.extra = solids.front().extra;
	for (const auto& plane : hull) {
		// Texture from a side in the same plane, else from the side facing the most similar way.
		const Side* best = nullptr;
		double bestScore = -2;
		for (const auto& solid : solids) {
			for (const auto& side : solid.sides) {
				const Plane p = side.plane();
				const double score = dot(p.normal, plane.plane.normal) + (samePlane(p, plane.plane) ? 10 : 0);
				if (score > bestScore) {
					bestScore = score;
					best = &side;
				}
			}
		}
		Side side;
		side.id = doc.newSideId();
		side.planePoints = plane.points;
		if (best) {
			copyTexture(side, *best);
		} else {
			side.material = "TOOLS/TOOLSNODRAW";
			alignToWorld(side);
		}
		result.solid.sides.push_back(std::move(side));
	}
	if (!usable(result.solid)) {
		return std::nullopt;
	}
	// Exact when the brushes fill the hull: nothing is left after taking them all away.
	std::vector<Solid> rest{result.solid};
	for (const auto& solid : solids) {
		std::vector<Solid> next;
		for (const auto& piece : rest) {
			auto parts = subtractSolid(doc, piece, solid);
			next.insert(next.end(), parts.begin(), parts.end());
		}
		rest = std::move(next);
	}
	double left = 0;
	for (const auto& piece : rest) {
		left += solidVolume(piece);
	}
	result.exact = left < 1.0;
	return result;
}

std::optional<Solid> roundEdge(MapDocument& doc, const Solid& solid, int sideA, int sideB, double radius, int segments) {
	if (radius <= 0 || segments < 1 || sideA == sideB) {
		return std::nullopt;
	}
	const Side* a = nullptr;
	const Side* b = nullptr;
	int indexA = -1, indexB = -1;
	for (std::size_t i = 0; i < solid.sides.size(); ++i) {
		if (solid.sides[i].id == sideA) {
			a = &solid.sides[i];
			indexA = static_cast<int>(i);
		}
		if (solid.sides[i].id == sideB) {
			b = &solid.sides[i];
			indexB = static_cast<int>(i);
		}
	}
	if (!a || !b) {
		return std::nullopt;
	}
	// The faces must share an edge (two corners).
	const auto geometry = buildBrushGeometry(solid);
	const FaceGeometry* faceA = nullptr;
	const FaceGeometry* faceB = nullptr;
	for (const auto& face : geometry.faces) {
		if (face.sideIndex == indexA) faceA = &face;
		if (face.sideIndex == indexB) faceB = &face;
	}
	if (!faceA || !faceB) {
		return std::nullopt;
	}
	std::vector<Vec3> shared;
	for (const auto& p : faceA->vertices) {
		for (const auto& q : faceB->vertices) {
			if (length(p - q) < EPS) {
				shared.push_back(p);
			}
		}
	}
	const Plane pa = a->plane(), pb = b->plane();
	const double c = dot(pa.normal, pb.normal);
	if (shared.size() < 2 || c > 0.999) {
		return std::nullopt;
	}
	// Circle of the rounding: touches both faces; its center lies inside the corner.
	const Vec3 center = shared.front() - (pa.normal + pb.normal) * (radius / (1 + c));
	const double angle = std::acos(std::clamp(c, -1.0, 1.0));
	Solid out = solid;
	for (int i = 0; i < segments; ++i) {
		const double t = (i + 0.5) / segments;
		const Vec3 normal = normalize(pa.normal * std::sin((1 - t) * angle) + pb.normal * std::sin(t * angle));
		out.sides.push_back(makeSide(doc, Plane{normal, dot(center, normal) + radius}, a->material, t < 0.5 ? a : b));
	}
	removeUnusedSides(out);
	tidyPlanePoints(out);
	if (!usable(out)) {
		return std::nullopt;
	}
	return out;
}

double solidVolume(const Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	if (geometry.faces.empty()) {
		return 0;
	}
	const Vec3 reference = geometry.bounds.center();
	double volume = 0;
	for (const auto& face : geometry.faces) {
		for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
			const Vec3 a = face.vertices[0] - reference, b = face.vertices[i] - reference, cc = face.vertices[i + 1] - reference;
			volume += dot(a, cross(b, cc)) / 6.0;
		}
	}
	return std::abs(volume);
}

} // namespace sme
