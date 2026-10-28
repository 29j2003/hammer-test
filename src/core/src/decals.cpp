// decals.cpp - Infodecal and info_overlay geometry (see decals.h).

#include <sme/decals.h>

#include <sme/brush.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sme {

namespace {

constexpr double EPS = 1e-6;
constexpr double SIN_45 = 0.70710678;

const SurfaceFace* findFace(const std::vector<SurfaceFace>& faces, int sideId) {
	for (const auto& face : faces) {
		if (face.sideId == sideId) return &face;
	}
	return nullptr;
}

/// Moves `p` along `direction` onto the face's plane (nullopt if the direction runs along it).
std::optional<Vec3> projectOnto(const SurfaceFace& face, const Vec3& p, const Vec3& direction) {
	const double along = dot(face.normal, direction);
	if (std::abs(along) < 0.05) return std::nullopt;
	return p - direction * ((dot(face.normal, p) - face.dist) / along);
}

/// The point (in the face's plane) lies on the face.
bool insidePolygon(const SurfaceFace& face, const Vec3& p) {
	for (std::size_t e = 0; e < face.polygon.size(); ++e) {
		const Vec3 a = face.polygon[e], b = face.polygon[(e + 1) % face.polygon.size()];
		if (dot(p - a, cross(face.normal, b - a)) < -0.01) return false;
	}
	return true;
}

/// The patch of one convex polygon on one face, if anything is left after clipping.
void addPatch(std::vector<SurfacePatch>& out, const SurfaceFace& face, const std::vector<Vec3>& points, const std::vector<std::array<double, 2>>& uv) {
	auto [vertices, coords] = clipToPolygon(points, uv, face.polygon, face.normal);
	if (vertices.size() < 3) return;
	// Keep only patches with an area (clipping can leave a sliver along an edge).
	double area = 0;
	for (std::size_t i = 2; i < vertices.size(); ++i) {
		area += 0.5 * length(cross(vertices[i - 1] - vertices[0], vertices[i] - vertices[0]));
	}
	if (area < 0.01) return;
	out.push_back({face.sideId, face.normal, std::move(vertices), std::move(coords)});
}

/// Parses "x y z" (missing parts are 0).
Vec3 vec3Of(const Entity& entity, std::string_view key, const Vec3& fallback) {
	Vec3 v;
	const auto text = entity.get(key);
	if (text.empty() || !parseVec3(text, v)) return fallback;
	return v;
}

double numberOf(const Entity& entity, std::string_view key, double fallback) {
	const auto text = entity.get(key);
	if (text.empty()) return fallback;
	try {
		return std::stod(text);
	} catch (...) {
		return fallback;
	}
}

} // namespace

std::optional<std::pair<double, double>> decalSize(const GameFileSystem& fs, std::string_view material) {
	const auto vmt = loadMaterial(fs, material);
	if (!vmt) return std::nullopt;
	const auto base = vmt->param("$basetexture");
	if (base.empty()) return std::nullopt;
	const auto data = fs.read("materials/" + normalizeMaterialName(base) + ".vtf");
	if (!data) return std::nullopt;
	const auto info = readTextureInfo(*data);
	if (!info || info->width == 0 || info->height == 0) return std::nullopt;
	double scale = 1;
	if (const auto text = vmt->param("$decalscale"); !text.empty()) {
		try {
			scale = std::stod(text);
		} catch (...) {
			scale = 1;
		}
		if (scale <= 0) scale = 1;
	}
	return std::pair{info->width * scale, info->height * scale};
}

std::vector<SurfaceFace> collectSurfaceFaces(const MapDocument& doc) {
	std::vector<SurfaceFace> faces;
	const auto addSolid = [&](const Solid& solid) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			if (face.vertices.size() < 3) continue;
			SurfaceFace out;
			out.sideId = solid.sides[static_cast<std::size_t>(face.sideIndex)].id;
			out.normal = normalize(face.normal);
			out.dist = dot(out.normal, face.vertices.front());
			out.polygon = face.vertices;
			for (const auto& v : face.vertices) out.bounds.add(v);
			faces.push_back(std::move(out));
		}
	};
	for (const auto& solid : doc.world.solids) addSolid(solid);
	for (const auto& entity : doc.entities) {
		for (const auto& solid : entity.solids) addSolid(solid);
	}
	return faces;
}

void decalAxes(const Vec3& normal, Vec3& s, Vec3& t) {
	const Vec3 n = normalize(normal);
	if (std::abs(n.z) > SIN_45) {
		// Floors and ceilings: S along +X (in the plane).
		s = normalize(Vec3{1, 0, 0} - n * n.x);
	} else {
		// Walls: S horizontal, to the right seen from the front.
		s = normalize(cross(Vec3{0, 0, 1}, n));
	}
	t = normalize(cross(s, n));   // down the texture
}

std::pair<std::vector<Vec3>, std::vector<std::array<double, 2>>> clipToPolygon(const std::vector<Vec3>& points, const std::vector<std::array<double, 2>>& uv,
                                                                                 const std::vector<Vec3>& clip, const Vec3& normal) {
	std::vector<Vec3> current = points;
	std::vector<std::array<double, 2>> coords = uv;
	coords.resize(current.size());
	for (std::size_t e = 0; e < clip.size() && current.size() >= 3; ++e) {
		const Vec3 a = clip[e], b = clip[(e + 1) % clip.size()];
		// Inside: to the left of the edge seen from the front.
		const Vec3 inward = normalize(cross(normal, b - a));
		const auto side = [&](const Vec3& p) { return dot(p - a, inward); };
		std::vector<Vec3> nextPoints;
		std::vector<std::array<double, 2>> nextCoords;
		for (std::size_t i = 0; i < current.size(); ++i) {
			const std::size_t j = (i + 1) % current.size();
			const double di = side(current[i]), dj = side(current[j]);
			if (di >= -EPS) {
				nextPoints.push_back(current[i]);
				nextCoords.push_back(coords[i]);
			}
			if ((di >= -EPS) != (dj >= -EPS)) {
				const double f = di / (di - dj);
				nextPoints.push_back(current[i] + (current[j] - current[i]) * f);
				nextCoords.push_back({coords[i][0] + (coords[j][0] - coords[i][0]) * f, coords[i][1] + (coords[j][1] - coords[i][1]) * f});
			}
		}
		current = std::move(nextPoints);
		coords = std::move(nextCoords);
	}
	if (current.size() < 3) return {};
	return {std::move(current), std::move(coords)};
}

std::vector<SurfacePatch> decalPatches(const std::vector<SurfaceFace>& faces, const Vec3& origin, double width, double height) {
	std::vector<SurfacePatch> out;
	if (width <= 0 || height <= 0) return out;
	const double reach = 8;   // how far from the surface the entity may sit
	const double radius = 0.5 * std::sqrt(width * width + height * height);
	// The nearest face the decal faces (origin in front of it or on it, within reach).
	const SurfaceFace* nearest = nullptr;
	double best = 3 * reach;
	for (const auto& face : faces) {
		const double distance = dot(face.normal, origin) - face.dist;
		if (distance < -1 || distance > reach) continue;
		bool near = true;
		for (int k = 0; k < 3; ++k) {
			near = near && origin[k] >= face.bounds.min[k] - radius && origin[k] <= face.bounds.max[k] + radius;
		}
		if (!near) continue;
		// Right under the entity counts by height alone; beside it (a face meeting another one at
		// an edge) only if nothing is right under it.
		const Vec3 foot = origin - face.normal * distance;
		double score = std::abs(distance);
		if (!insidePolygon(face, foot)) {
			double edge = 1e30;
			for (std::size_t e = 0; e < face.polygon.size(); ++e) {
				const Vec3 a = face.polygon[e], b = face.polygon[(e + 1) % face.polygon.size()];
				const double f = std::clamp(dot(foot - a, b - a) / std::max(EPS, dot(b - a, b - a)), 0.0, 1.0);
				edge = std::min(edge, length(foot - (a + (b - a) * f)));
			}
			score = length(Vec3{edge, distance, 0}) + reach;
		}
		if (score < best) {
			best = score;
			nearest = &face;
		}
	}
	if (!nearest) return out;
	Vec3 s, t;
	decalAxes(nearest->normal, s, t);
	const Vec3 center = origin - nearest->normal * (dot(nearest->normal, origin) - nearest->dist);
	const Vec3 hs = s * (width / 2), ht = t * (height / 2);
	// Counter-clockwise seen from the front: top left, bottom left, bottom right, top right
	// (T points down, S right).
	const std::vector<Vec3> square = {center - hs - ht, center - hs + ht, center + hs + ht, center + hs - ht};
	const std::vector<std::array<double, 2>> uv = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
	for (const auto& face : faces) {
		// Faces in the same plane (a floor of several brushes) get their part too.
		if (dot(face.normal, nearest->normal) < 0.999 || std::abs(face.dist - nearest->dist) > 0.5) continue;
		addPatch(out, face, square, uv);
	}
	return out;
}

std::optional<OverlayInfo> readOverlay(const Entity& entity) {
	OverlayInfo overlay;
	overlay.origin = vec3Of(entity, "BasisOrigin", entity.origin());
	overlay.u = normalize(vec3Of(entity, "BasisU", {}));
	overlay.v = normalize(vec3Of(entity, "BasisV", {}));
	overlay.normal = normalize(vec3Of(entity, "BasisNormal", {}));
	if (length(overlay.normal) < 0.5 || length(overlay.u) < 0.5) return std::nullopt;
	if (length(overlay.v) < 0.5) overlay.v = normalize(cross(overlay.u, overlay.normal));
	for (int i = 0; i < 4; ++i) {
		overlay.corners[static_cast<std::size_t>(i)] = vec3Of(entity, "uv" + std::to_string(i), {});
	}
	const Vec3 d = overlay.corners[2] - overlay.corners[0];
	if (std::abs(d.x) < EPS && std::abs(d.y) < EPS) return std::nullopt;
	overlay.startU = numberOf(entity, "StartU", 0);
	overlay.endU = numberOf(entity, "EndU", 1);
	overlay.startV = numberOf(entity, "StartV", 0);
	overlay.endV = numberOf(entity, "EndV", 1);
	std::istringstream sides(entity.get("sides"));
	for (int id = 0; sides >> id;) overlay.sides.push_back(id);
	return overlay;
}

std::array<Vec3, 4> overlayCorners(const OverlayInfo& overlay) {
	std::array<Vec3, 4> out;
	for (std::size_t i = 0; i < 4; ++i) {
		out[i] = overlay.origin + overlay.u * overlay.corners[i].x + overlay.v * overlay.corners[i].y;
	}
	return out;
}

std::vector<SurfacePatch> overlayPatches(const std::vector<SurfaceFace>& faces, const OverlayInfo& overlay) {
	std::vector<SurfacePatch> out;
	const auto corners = overlayCorners(overlay);
	const std::array<std::array<double, 2>, 4> tex = {{{overlay.startU, overlay.startV},
	                                                    {overlay.startU, overlay.endV},
	                                                    {overlay.endU, overlay.endV},
	                                                    {overlay.endU, overlay.startV}}};
	for (const int sideId : overlay.sides) {
		const SurfaceFace* face = findFace(faces, sideId);
		if (!face || dot(face->normal, overlay.normal) < 0.1) continue;   // faces turned away get nothing
		std::array<Vec3, 4> on;
		bool ok = true;
		for (std::size_t i = 0; i < 4 && ok; ++i) {
			const auto p = projectOnto(*face, corners[i], overlay.normal);
			ok = p.has_value();
			if (p) on[i] = *p;
		}
		if (!ok) continue;
		// Two triangles: texture coordinates are exact per triangle even for a skewed quad.
		for (const auto& tri : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
			std::vector<Vec3> points;
			std::vector<std::array<double, 2>> uv;
			for (const int k : tri) {
				points.push_back(on[static_cast<std::size_t>(k)]);
				uv.push_back(tex[static_cast<std::size_t>(k)]);
			}
			// The corners may wind either way: make them counter-clockwise seen from the front.
			if (dot(cross(points[1] - points[0], points[2] - points[0]), face->normal) < 0) {
				std::swap(points[1], points[2]);
				std::swap(uv[1], uv[2]);
			}
			addPatch(out, *face, points, uv);
		}
	}
	return out;
}

std::vector<std::pair<std::string, std::string>> overlayKeysOnFace(const Vec3& point, const Vec3& faceNormal, int sideId, const std::string& material,
                                                                  double width, double height) {
	Vec3 u, v;
	const Vec3 n = normalize(faceNormal);
	decalAxes(n, u, v);
	const double w = width / 2, h = height / 2;
	const auto corner = [](double x, double y) { return formatVec3({x, y, 0}); };
	return {
	    {"material", material},
	    {"sides", std::to_string(sideId)},
	    {"RenderOrder", "0"},
	    {"StartU", "0"},
	    {"EndU", "1"},
	    {"StartV", "0"},
	    {"EndV", "1"},
	    {"BasisOrigin", formatVec3(point)},
	    {"BasisU", formatVec3(u)},
	    {"BasisV", formatVec3(v)},
	    {"BasisNormal", formatVec3(n)},
	    {"uv0", corner(-w, -h)},
	    {"uv1", corner(-w, h)},
	    {"uv2", corner(w, h)},
	    {"uv3", corner(w, -h)},
	};
}

} // namespace sme
