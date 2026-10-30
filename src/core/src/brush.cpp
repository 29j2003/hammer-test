// brush.cpp - Brush geometry and transforms (see brush.h).

#include <sme/brush.h>
#include <sme/displacement.h>

#include <sme/text.h>

#include <cmath>

namespace sme {

namespace {

constexpr double ON_EPSILON = 0.01;
constexpr double WORLD_HALF = 65536.0;

std::vector<Vec3> baseWinding(const Plane& plane) {
	// Pick the world axis most perpendicular to the plane as "up".
	int major = 0;
	double best = -1;
	for (int i = 0; i < 3; ++i) {
		if (std::abs(plane.normal[i]) > best) {
			best = std::abs(plane.normal[i]);
			major = i;
		}
	}
	Vec3 up = major == 2 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
	up = normalize(up - plane.normal * dot(up, plane.normal));
	const Vec3 right = cross(up, plane.normal);
	const Vec3 origin = plane.normal * plane.dist;
	const Vec3 u = up * WORLD_HALF, r = right * WORLD_HALF;
	return {origin - r + u, origin + r + u, origin + r - u, origin - r - u};
}

/// Keeps the part of the polygon behind the plane (inside the brush).
std::vector<Vec3> clipBehind(const std::vector<Vec3>& poly, const Plane& plane) {
	std::vector<Vec3> out;
	if (poly.empty()) {
		return out;
	}
	out.reserve(poly.size() + 1);
	for (std::size_t i = 0; i < poly.size(); ++i) {
		const Vec3& a = poly[i];
		const Vec3& b = poly[(i + 1) % poly.size()];
		const double da = plane.distanceTo(a);
		const double db = plane.distanceTo(b);
		if (da <= ON_EPSILON) {
			out.push_back(a);
		}
		if ((da > ON_EPSILON && db < -ON_EPSILON) || (da < -ON_EPSILON && db > ON_EPSILON)) {
			const double t = da / (da - db);
			out.push_back(a + (b - a) * t);
		}
	}
	return out;
}

Vec3 newellNormal(const std::vector<Vec3>& poly) {
	Vec3 n;
	for (std::size_t i = 0; i < poly.size(); ++i) {
		const Vec3& a = poly[i];
		const Vec3& b = poly[(i + 1) % poly.size()];
		n.x += (a.y - b.y) * (a.z + b.z);
		n.y += (a.z - b.z) * (a.x + b.x);
		n.z += (a.x - b.x) * (a.y + b.y);
	}
	return normalize(n);
}

/// Three points on the polygon's plane in VMF order (normal = cross(p0 - p1, p2 - p1) points outwards).
std::array<Vec3, 3> planePointsFor(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& outward) {
	std::array<Vec3, 3> points{a, b, c};
	if (dot(cross(points[0] - points[1], points[2] - points[1]), outward) < 0) {
		std::swap(points[0], points[2]);
	}
	return points;
}

void lockShift(TextureAxis& axis, const Vec3& delta) {
	if (axis.scale != 0) {
		axis.shift -= dot(delta, axis.axis) / axis.scale;
	}
}

std::vector<Vec3> parseVectorRow(std::string_view text) {
	std::vector<Vec3> out;
	const auto parts = splitTrimmed(text, ' ');
	for (std::size_t i = 0; i + 2 < parts.size(); i += 3) {
		out.push_back({std::atof(parts[i].c_str()), std::atof(parts[i + 1].c_str()), std::atof(parts[i + 2].c_str())});
	}
	return out;
}

std::vector<double> parseNumberRow(std::string_view text) {
	std::vector<double> out;
	for (const auto& part : splitTrimmed(text, ' ')) {
		out.push_back(std::atof(part.c_str()));
	}
	return out;
}

} // namespace

BrushGeometry buildBrushGeometry(const Solid& solid) {
	BrushGeometry geometry;
	std::vector<Plane> planes;
	planes.reserve(solid.sides.size());
	for (const auto& side : solid.sides) {
		planes.push_back(side.plane());
	}
	bool ok = solid.sides.size() >= 4;
	for (std::size_t i = 0; i < planes.size(); ++i) {
		if (length(planes[i].normal) < 0.5) {
			ok = false;
			continue;
		}
		auto poly = baseWinding(planes[i]);
		for (std::size_t j = 0; j < planes.size() && !poly.empty(); ++j) {
			if (i == j || length(planes[j].normal) < 0.5) {
				continue;
			}
			// Coplanar duplicate: keep only the first one.
			if (dot(planes[i].normal, planes[j].normal) > 0.9999 && std::abs(planes[i].dist - planes[j].dist) < ON_EPSILON) {
				if (j < i) {
					poly.clear();
				}
				continue;
			}
			poly = clipBehind(poly, planes[j]);
		}
		if (poly.size() < 3) {
			continue;
		}
		if (dot(newellNormal(poly), planes[i].normal) < 0) {
			std::reverse(poly.begin(), poly.end());
		}
		FaceGeometry face;
		face.sideIndex = static_cast<int>(i);
		face.normal = planes[i].normal;
		for (const auto& v : poly) {
			geometry.bounds.add(v);
		}
		face.vertices = std::move(poly);
		geometry.faces.push_back(std::move(face));
	}
	const auto size = geometry.bounds.size();
	geometry.valid = ok && geometry.faces.size() >= 4 && !geometry.bounds.empty() && size.x < 2 * WORLD_HALF - 1 &&
	                 size.y < 2 * WORLD_HALF - 1 && size.z < 2 * WORLD_HALF - 1;
	return geometry;
}

TexCoord textureCoordinate(const Side& side, const Vec3& point) {
	TexCoord tc;
	tc.u = side.u.scale != 0 ? dot(point, side.u.axis) / side.u.scale + side.u.shift : 0;
	tc.v = side.v.scale != 0 ? dot(point, side.v.axis) / side.v.scale + side.v.shift : 0;
	return tc;
}

std::optional<BrushHit> intersect(const Ray& ray, const Solid& solid) {
	double tEnter = -std::numeric_limits<double>::max();
	double tExit = std::numeric_limits<double>::max();
	int enterSide = -1;
	for (std::size_t i = 0; i < solid.sides.size(); ++i) {
		const auto plane = solid.sides[i].plane();
		const double denom = dot(plane.normal, ray.direction);
		const double dist = plane.distanceTo(ray.origin);
		if (std::abs(denom) < 1e-12) {
			if (dist > 0) {
				return std::nullopt;
			}
			continue;
		}
		const double t = -dist / denom;
		if (denom < 0) {
			if (t > tEnter) {
				tEnter = t;
				enterSide = static_cast<int>(i);
			}
		} else {
			tExit = std::min(tExit, t);
		}
		if (tEnter > tExit) {
			return std::nullopt;
		}
	}
	if (enterSide < 0 || tEnter < 0) {
		return std::nullopt;
	}
	return BrushHit{tEnter, enterSide};
}

void alignToWorld(Side& side, double scale) {
	const auto n = side.plane().normal;
	const double ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
	if (az >= ax && az >= ay) {
		side.u.axis = {1, 0, 0};
		side.v.axis = {0, -1, 0};
	} else if (ax >= ay) {
		side.u.axis = {0, 1, 0};
		side.v.axis = {0, 0, -1};
	} else {
		side.u.axis = {1, 0, 0};
		side.v.axis = {0, 0, -1};
	}
	side.u.shift = side.v.shift = 0;
	side.u.scale = side.v.scale = scale;
	side.rotation = 0;
}

void alignToFace(Side& side, double scale) {
	alignToWorld(side, scale);
	const auto n = side.plane().normal;
	// Project the world axes onto the plane, keeping them perpendicular.
	Vec3 u = normalize(side.u.axis - n * dot(side.u.axis, n));
	Vec3 v = normalize(cross(n, u));
	if (dot(v, side.v.axis) < 0) {
		v = -v;
	}
	side.u.axis = u;
	side.v.axis = v;
}

Solid makeBlock(MapDocument& doc, const Bounds& box, const std::string& material) {
	Solid solid;
	solid.id = doc.newSolidId();
	const Vec3 lo = box.min, hi = box.max;
	struct FaceDef {
		Vec3 a, b, c, outward;
	};
	// Three corners of each face; order is fixed up by planePointsFor().
	const FaceDef faces[6] = {
		{{lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {0, 0, 1}},    // top
		{{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {0, 0, -1}},   // bottom
		{{lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z}, {lo.x, lo.y, lo.z}, {-1, 0, 0}},   // west
		{{hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {1, 0, 0}},    // east
		{{hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z}, {0, 1, 0}},    // north
		{{hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {0, -1, 0}},   // south
	};
	for (const auto& face : faces) {
		Side side;
		side.id = doc.newSideId();
		side.planePoints = planePointsFor(face.a, face.b, face.c, face.outward);
		side.material = material;
		alignToWorld(side);
		solid.sides.push_back(std::move(side));
	}
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "0 175 236");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	solid.extra.push_back(std::move(editor));
	return solid;
}

void translateSolid(Solid& solid, const Vec3& delta, bool textureLock) {
	for (auto& side : solid.sides) {
		for (auto& p : side.planePoints) {
			p += delta;
		}
		if (textureLock) {
			lockShift(side.u, delta);
			lockShift(side.v, delta);
		}
	}
	// Displacement start positions move with the brush.
	for (auto& side : solid.sides) {
		for (auto& extra : side.extra) {
			if (extra.block && iequals(extra.key, "dispinfo")) {
				Vec3 start;
				auto text = extra.get("startposition");
				if (text.size() > 2 && parseVec3(std::string_view(text).substr(1, text.size() - 2), start)) {
					extra.set("startposition", "[" + formatVec3(start + delta) + "]");
				}
			}
		}
	}
}

void rotateSolid(Solid& solid, const Mat3& rotation, const Vec3& center, bool textureLock) {
	for (auto& side : solid.sides) {
		for (auto& p : side.planePoints) {
			p = rotation * (p - center) + center;
		}
		if (textureLock) {
			for (TextureAxis* axis : {&side.u, &side.v}) {
				const Vec3 rotated = rotation * axis->axis;
				if (axis->scale != 0) {
					axis->shift += (dot(center, axis->axis) - dot(center, rotated)) / axis->scale;
				}
				axis->axis = rotated;
			}
		}
	}
}

bool scaleSolid(Solid& solid, const Bounds& from, const Bounds& to) {
	const Vec3 fs = from.size(), ts = to.size();
	for (int i = 0; i < 3; ++i) {
		if (fs[i] <= 1e-6 || ts[i] <= 1e-6) {
			return false;
		}
	}
	for (auto& side : solid.sides) {
		for (auto& p : side.planePoints) {
			for (int i = 0; i < 3; ++i) {
				// The box's own sides land exactly on the new box (a side snapped to 4.5 stays 4.5,
				// no 4.4999999 that opens a gap); points in between scale along.
				if (std::abs(p[i] - from.min[i]) < 1e-9) {
					p[i] = to.min[i];
				} else if (std::abs(p[i] - from.max[i]) < 1e-9) {
					p[i] = to.max[i];
				} else {
					p[i] = to.min[i] + (p[i] - from.min[i]) * ts[i] / fs[i];
				}
			}
		}
	}
	return true;
}

Solid cloneSolid(MapDocument& doc, const Solid& solid) {
	Solid copy = solid;
	copy.id = doc.newSolidId();
	for (auto& side : copy.sides) {
		side.id = doc.newSideId();
	}
	return copy;
}

bool isConvexValid(const Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid || geometry.faces.size() != solid.sides.size()) {
		return false;
	}
	for (const auto& face : geometry.faces) {
		// Degenerate sliver faces make VBSP complain; require some area.
		Vec3 area;
		for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
			area += cross(face.vertices[i] - face.vertices[0], face.vertices[i + 1] - face.vertices[0]);
		}
		if (length(area) < 0.5) {
			return false;
		}
	}
	return true;
}

std::optional<DisplacementMesh> buildDisplacement(const Side& side, const FaceGeometry& face) {
	const auto* info = side.dispInfo();
	if (!info || face.vertices.size() != 4) {
		return std::nullopt;
	}
	const int power = std::atoi(info->get("power", "0").c_str());
	if (power < 2 || power > 4) {
		return std::nullopt;
	}
	const int size = (1 << power) + 1;
	Vec3 start;
	auto startText = info->get("startposition");
	if (startText.size() > 2) {
		startText = startText.substr(1, startText.size() - 2);
	}
	if (!parseVec3(startText, start)) {
		return std::nullopt;
	}

	// Corners in VBSP's order (displacement.h). "elevation" is ignored like VBSP does.
	const auto corners = displacementCorners(face, start);

	const auto rows = [&](std::string_view block) {
		std::vector<std::string> out;
		if (const auto* node = info->find(block)) {
			for (int r = 0; r < size; ++r) {
				out.push_back(node->get("row" + std::to_string(r)));
			}
		}
		return out;
	};
	const auto normals = rows("normals");
	const auto distances = rows("distances");
	const auto offsets = rows("offsets");
	const auto alphas = rows("alphas");

	DisplacementMesh mesh;
	mesh.size = size;
	mesh.positions.resize(static_cast<std::size_t>(size) * size);
	mesh.alpha.resize(mesh.positions.size(), 0.f);
	const double step = 1.0 / (size - 1);
	for (int i = 0; i < size; ++i) {
		const Vec3 a = corners[0] + (corners[1] - corners[0]) * (i * step);
		const Vec3 b = corners[3] + (corners[2] - corners[3]) * (i * step);
		const auto rowNormals = i < static_cast<int>(normals.size()) ? parseVectorRow(normals[i]) : std::vector<Vec3>{};
		const auto rowDistances = i < static_cast<int>(distances.size()) ? parseNumberRow(distances[i]) : std::vector<double>{};
		const auto rowOffsets = i < static_cast<int>(offsets.size()) ? parseVectorRow(offsets[i]) : std::vector<Vec3>{};
		const auto rowAlphas = i < static_cast<int>(alphas.size()) ? parseNumberRow(alphas[i]) : std::vector<double>{};
		for (int j = 0; j < size; ++j) {
			Vec3 p = a + (b - a) * (j * step);
			if (j < static_cast<int>(rowNormals.size()) && j < static_cast<int>(rowDistances.size())) {
				p += rowNormals[j] * rowDistances[j];
			}
			if (j < static_cast<int>(rowOffsets.size())) {
				p += rowOffsets[j];
			}
			const auto index = static_cast<std::size_t>(i) * size + j;
			mesh.positions[index] = p;
			if (j < static_cast<int>(rowAlphas.size())) {
				mesh.alpha[index] = static_cast<float>(rowAlphas[j]);
			}
		}
	}
	return mesh;
}

} // namespace sme
