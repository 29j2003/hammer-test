// prop_placement.cpp - Props along a path or scattered over surfaces; the measure tool's numbers
// (see prop_placement.h).

#include <sme/prop_placement.h>

#include <sme/brush.h>
#include <sme/keyvalues.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace sme {

namespace {

constexpr double DEG = 180.0 / std::numbers::pi;
constexpr double TILE = 512.0 * 512.0;   ///< The area `density` counts props in

/// Small, portable random numbers (the same on every compiler, unlike std:: distributions).
class Random {
public:
	explicit Random(std::uint32_t seed) : m_state(seed * 2654435761u + 0x9e3779b9u) {
		if (m_state == 0) m_state = 1;
	}
	/// 0 <= value < 1
	double next() {
		m_state ^= m_state << 13;
		m_state ^= m_state >> 17;
		m_state ^= m_state << 5;
		return double(m_state) / 4294967296.0;
	}
	double range(double lo, double hi) { return hi <= lo ? lo : lo + (hi - lo) * next(); }

private:
	std::uint32_t m_state;
};

double wrapDegrees(double a) {
	a = std::fmod(a, 360.0);
	if (a < 0) a += 360.0;
	if (a >= 359.9995) a = 0;
	return a;
}

/// Rounded to 1/100 so the VMF stays readable.
double tidy(double v) {
	const double r = std::round(v * 100.0) / 100.0;
	return r == 0 ? 0.0 : r;   // no "-0"
}

PropPlacement tidyPlacement(PropPlacement p) {
	p.origin = {tidy(p.origin.x), tidy(p.origin.y), tidy(p.origin.z)};
	p.pitch = tidy(p.pitch);
	p.yaw = tidy(wrapDegrees(p.yaw));
	p.roll = tidy(p.roll);
	p.scale = tidy(p.scale);
	return p;
}

/// Pitch and roll that turn a model's up axis onto `normal` when it is turned by `yaw`.
std::pair<double, double> tiltOnto(const Vec3& normal, double yaw) {
	const double cy = std::cos(yaw / DEG), sy = std::sin(yaw / DEG);
	const double nx = normal.x * cy + normal.y * sy;
	const double ny = -normal.x * sy + normal.y * cy;
	const double roll = std::asin(std::clamp(-ny, -1.0, 1.0));
	const double pitch = std::atan2(nx, normal.z);
	return {pitch * DEG, roll * DEG};
}

Vec3 triangleNormal(const ScatterTriangle& t) {
	return normalize(cross(t.b - t.a, t.c - t.a));
}

double triangleArea(const ScatterTriangle& t) {
	return length(cross(t.b - t.a, t.c - t.a)) * 0.5;
}

/// Points kept apart by a grid of cells `size` wide.
class SpacingGrid {
public:
	explicit SpacingGrid(double size) : m_size(std::max(size, 1.0)) {}
	bool free(const Vec3& p, double spacing) const {
		const auto [cx, cy] = cell(p);
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				const auto it = m_cells.find(key(cx + dx, cy + dy));
				if (it == m_cells.end()) continue;
				for (const auto& q : it->second) {
					const Vec3 d = q - p;
					if (dot(d, d) < spacing * spacing) return false;
				}
			}
		}
		return true;
	}
	void add(const Vec3& p) {
		const auto [cx, cy] = cell(p);
		m_cells[key(cx, cy)].push_back(p);
	}

private:
	std::pair<long long, long long> cell(const Vec3& p) const {
		return {static_cast<long long>(std::floor(p.x / m_size)), static_cast<long long>(std::floor(p.y / m_size))};
	}
	static long long key(long long x, long long y) { return x * 1000003LL + y; }
	double m_size;
	std::unordered_map<long long, std::vector<Vec3>> m_cells;
};

/// Corners shared by two faces (the edge between them): the two farthest apart.
std::optional<std::pair<Vec3, Vec3>> sharedEdge(const FaceGeometry& a, const FaceGeometry& b) {
	std::vector<Vec3> shared;
	for (const auto& p : a.vertices) {
		for (const auto& q : b.vertices) {
			if (length(p - q) < 0.05) {
				shared.push_back(p);
				break;
			}
		}
	}
	if (shared.size() < 2) return std::nullopt;
	std::pair<Vec3, Vec3> best{shared[0], shared[1]};
	double longest = -1;
	for (std::size_t i = 0; i < shared.size(); ++i) {
		for (std::size_t j = i + 1; j < shared.size(); ++j) {
			if (const double l = length(shared[i] - shared[j]); l > longest) {
				longest = l;
				best = {shared[i], shared[j]};
			}
		}
	}
	if (longest < 0.5) return std::nullopt;
	return best;
}

const FaceGeometry* findFace(const BrushGeometry& geometry, const Solid& solid, int sideId) {
	for (const auto& face : geometry.faces) {
		if (solid.sides[static_cast<std::size_t>(face.sideIndex)].id == sideId) return &face;
	}
	return nullptr;
}

} // namespace

std::vector<PropPlacement> propsAlongPath(const std::vector<Vec3>& points, const PathPropOptions& options) {
	std::vector<PropPlacement> result;
	if (points.size() < 2) return result;
	std::vector<double> at{0.0};   // distance along the path at each point
	for (std::size_t i = 1; i < points.size(); ++i) at.push_back(at.back() + length(points[i] - points[i - 1]));
	const double total = at.back();
	if (total < 1e-6) return result;
	const double spacing = std::max(options.spacing, 1.0);
	Random random(options.seed);

	const auto placeAt = [&](double s) {
		s = std::clamp(s, 0.0, total);
		std::size_t i = 1;
		while (i + 1 < points.size() && at[i] < s) ++i;
		const Vec3 a = points[i - 1], b = points[i];
		const double span = at[i] - at[i - 1];
		const double t = span > 1e-9 ? (s - at[i - 1]) / span : 0.0;
		// Direction on the floor; a vertical piece takes the one before it.
		Vec3 dir = b - a;
		dir.z = 0;
		for (std::size_t k = i; length(dir) < 1e-6 && k > 1; --k) {
			dir = points[k - 1] - points[k - 2];
			dir.z = 0;
		}
		dir = length(dir) < 1e-6 ? Vec3{1, 0, 0} : normalize(dir);
		const Vec3 right{dir.y, -dir.x, 0};
		PropPlacement p;
		p.origin = a + (b - a) * t + right * options.sideOffset;
		const double along = options.alignToPath ? std::atan2(dir.y, dir.x) * DEG : 0.0;
		p.yaw = along + options.yawOffset + (options.randomYaw > 0 ? random.range(-options.randomYaw, options.randomYaw) : 0.0);
		p.scale = random.range(options.scaleMin, std::max(options.scaleMin, options.scaleMax));
		result.push_back(tidyPlacement(p));
	};
	double s = std::max(0.0, options.startOffset);
	double last = -1;
	for (; s <= total + 1e-6 && int(result.size()) < options.maxCount; s += spacing) {
		placeAt(s);
		last = s;
	}
	if (options.includeEnd && last < total - 0.5 && int(result.size()) < options.maxCount) placeAt(total);
	return result;
}

std::vector<PropPlacement> scatterProps(const std::vector<ScatterTriangle>& surface, const std::vector<const Solid*>& avoid,
                                        const ScatterOptions& options, bool* limited) {
	if (limited) *limited = false;
	std::vector<PropPlacement> result;
	const double minUp = std::cos(std::clamp(options.maxSlope, 0.0, 90.0) / DEG) - 1e-9;
	std::vector<const ScatterTriangle*> usable;
	std::vector<double> cumulative;
	double area = 0;
	for (const auto& t : surface) {
		const double a = triangleArea(t);
		if (a < 1e-6 || triangleNormal(t).z < minUp) continue;
		area += a;
		usable.push_back(&t);
		cumulative.push_back(area);
	}
	if (usable.empty()) return result;
	const auto wanted = static_cast<long long>(std::llround(options.density * area / TILE));
	const int target = static_cast<int>(std::min<long long>(wanted, std::max(0, options.maxCount)));
	if (limited && wanted > options.maxCount) *limited = true;
	if (target <= 0) return result;

	// Boxes of the brushes to keep away from (checked first, the brush only when inside).
	std::vector<std::pair<Bounds, const Solid*>> keepOff;
	for (const Solid* solid : avoid) {
		const auto geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) continue;
		Bounds box = geometry.bounds;
		const double m = std::max(0.0, options.keepAway);
		box.min = box.min - Vec3{m, m, m};
		box.max = box.max + Vec3{m, m, m};
		keepOff.emplace_back(box, solid);
	}
	Random random(options.seed);
	SpacingGrid grid(std::max(options.minSpacing, 16.0));
	const long long attempts = static_cast<long long>(target) * 30;
	for (long long n = 0; n < attempts && int(result.size()) < target; ++n) {
		const double pick = random.next() * area;
		const auto it = std::upper_bound(cumulative.begin(), cumulative.end(), pick);
		const auto& t = *usable[static_cast<std::size_t>(std::min<std::ptrdiff_t>(it - cumulative.begin(), std::ptrdiff_t(usable.size()) - 1))];
		double r1 = random.next(), r2 = random.next();
		const double sr = std::sqrt(r1);
		const Vec3 point = t.a * (1 - sr) + t.b * (sr * (1 - r2)) + t.c * (sr * r2);
		if (options.minSpacing > 0 && !grid.free(point, options.minSpacing)) continue;
		bool blocked = false;
		for (const auto& [box, solid] : keepOff) {
			if (point.x < box.min.x || point.x > box.max.x || point.y < box.min.y || point.y > box.max.y || point.z < box.min.z || point.z > box.max.z) continue;
			if (distanceToSolid(*solid, point) < options.keepAway + 1e-6) {
				blocked = true;
				break;
			}
		}
		if (blocked) continue;
		grid.add(point);
		PropPlacement p;
		p.origin = point;
		p.yaw = options.randomYaw ? random.range(0, 360) : 0.0;
		if (options.tiltWithGround) {
			const auto [pitch, roll] = tiltOnto(triangleNormal(t), p.yaw);
			p.pitch = pitch;
			p.roll = roll;
		}
		p.scale = random.range(options.scaleMin, std::max(options.scaleMin, options.scaleMax));
		result.push_back(tidyPlacement(p));
	}
	return result;
}

std::vector<ScatterTriangle> scatterSurface(const MapDocument& doc, const std::vector<FaceRef>& faces) {
	std::vector<ScatterTriangle> triangles;
	const auto add = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
		ScatterTriangle t{a, b, c};
		const Vec3 n = cross(b - a, c - a);
		if (length(n) < 1e-9) return;
		if (n.z < 0) std::swap(t.b, t.c);   // seen from above
		if (std::abs(normalize(n).z) < 1e-3) return;   // a wall
		triangles.push_back(t);
	};
	for (const auto& ref : faces) {
		const Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* face = findFace(geometry, *solid, ref.sideId);
		if (!face || face->vertices.size() < 3) continue;
		const Side& side = solid->sides[static_cast<std::size_t>(face->sideIndex)];
		if (const auto mesh = side.dispInfo() ? buildDisplacement(side, *face) : std::nullopt) {
			const int n = mesh->size;
			for (int row = 0; row + 1 < n; ++row) {
				for (int col = 0; col + 1 < n; ++col) {
					const auto& p00 = mesh->positions[static_cast<std::size_t>(row * n + col)];
					const auto& p01 = mesh->positions[static_cast<std::size_t>(row * n + col + 1)];
					const auto& p10 = mesh->positions[static_cast<std::size_t>((row + 1) * n + col)];
					const auto& p11 = mesh->positions[static_cast<std::size_t>((row + 1) * n + col + 1)];
					add(p00, p01, p11);
					add(p00, p11, p10);
				}
			}
			continue;
		}
		if (face->normal.z <= 1e-3) continue;   // walls and ceilings: nothing stands there
		for (std::size_t i = 1; i + 1 < face->vertices.size(); ++i) add(face->vertices[0], face->vertices[i], face->vertices[i + 1]);
	}
	return triangles;
}

std::vector<Vec3> chainSegments(const std::vector<std::pair<Vec3, Vec3>>& segments) {
	constexpr double JOIN = 0.5;
	std::vector<Vec3> best;
	double bestLength = -1;
	std::vector<bool> usedOverall(segments.size(), false);
	for (std::size_t seed = 0; seed < segments.size(); ++seed) {
		if (usedOverall[seed]) continue;
		std::vector<bool> used(segments.size(), false);
		used[seed] = true;
		usedOverall[seed] = true;
		std::vector<Vec3> chain{segments[seed].first, segments[seed].second};
		for (bool grew = true; grew;) {
			grew = false;
			for (std::size_t i = 0; i < segments.size(); ++i) {
				if (used[i]) continue;
				const auto& [a, b] = segments[i];
				if (length(a - chain.back()) < JOIN) {
					chain.push_back(b);
				} else if (length(b - chain.back()) < JOIN) {
					chain.push_back(a);
				} else if (length(b - chain.front()) < JOIN) {
					chain.insert(chain.begin(), a);
				} else if (length(a - chain.front()) < JOIN) {
					chain.insert(chain.begin(), b);
				} else {
					continue;
				}
				used[i] = true;
				usedOverall[i] = true;
				grew = true;
			}
		}
		double l = 0;
		for (std::size_t i = 1; i < chain.size(); ++i) l += length(chain[i] - chain[i - 1]);
		if (l > bestLength) {
			bestLength = l;
			best = std::move(chain);
		}
	}
	return best;
}

std::vector<Vec3> edgeChainPath(const MapDocument& doc, const std::vector<std::pair<FaceRef, FaceRef>>& edges) {
	std::vector<std::pair<Vec3, Vec3>> segments;
	for (const auto& [a, b] : edges) {
		const Solid* sa = doc.findSolid(a.solid);
		const Solid* sb = doc.findSolid(b.solid);
		if (!sa || !sb) continue;
		const auto ga = buildBrushGeometry(*sa);
		const auto gb = sa == sb ? ga : buildBrushGeometry(*sb);
		const FaceGeometry* fa = findFace(ga, *sa, a.sideId);
		const FaceGeometry* fb = findFace(gb, *sb, b.sideId);
		if (!fa || !fb) continue;
		if (const auto edge = sharedEdge(*fa, *fb)) segments.push_back(*edge);
	}
	return chainSegments(segments);
}

double distanceToSolid(const Solid& solid, const Vec3& point) {
	double outside = -std::numeric_limits<double>::max();
	for (const auto& side : solid.sides) outside = std::max(outside, side.plane().distanceTo(point));
	return outside;
}

std::vector<Entity> makePropEntities(MapDocument& doc, const std::vector<PropPlacement>& placements, const std::string& classname, const std::string& model,
                                     const std::vector<std::pair<std::string, std::string>>& defaults) {
	std::vector<Entity> entities;
	entities.reserve(placements.size());
	for (const auto& p : placements) {
		Entity entity;
		entity.id = doc.newEntityId();
		entity.set("classname", classname);
		for (const auto& [key, value] : defaults) {
			if (!value.empty()) entity.set(key, value);
		}
		entity.set("model", model);
		entity.set("angles", formatNumber(p.pitch) + " " + formatNumber(p.yaw) + " " + formatNumber(p.roll));
		if (std::abs(p.scale - 1.0) > 1e-6) {
			entity.set("modelscale", formatNumber(p.scale));
		} else {
			entity.erase("modelscale");
		}
		entity.setOrigin(p.origin);
		auto editor = KvNode::makeBlock("editor");
		editor.set("color", "255 255 255");
		editor.set("visgroupshown", "1");
		editor.set("visgroupautoshown", "1");
		entity.extra.push_back(std::move(editor));
		entities.push_back(std::move(entity));
	}
	return entities;
}

std::pair<Change, std::vector<int>> applyPropPlacements(MapDocument& doc, std::vector<Entity> entities, const std::string& name) {
	Transaction t(doc, name);
	std::vector<int> ids;
	for (auto& entity : entities) {
		t.touchEntity(entity.id);
		ids.push_back(entity.id);
		doc.entities.push_back(std::move(entity));
	}
	return {t.finish(), ids};
}

Measurement measure(const Vec3& a, const Vec3& b) {
	Measurement m;
	const Vec3 d = b - a;
	m.distance = length(d);
	m.horizontal = std::hypot(d.x, d.y);
	m.height = d.z;
	m.slope = m.distance < 1e-9 ? 0.0 : std::atan2(std::abs(d.z), m.horizontal) * DEG;
	m.walkable = m.slope <= std::acos(0.7) * DEG + 1e-9;
	m.stepUp = d.z <= 18 + 1e-6;
	m.jumpUp = d.z <= 56 + 1e-6;
	m.playerFits = m.distance >= 72 - 1e-6;
	m.crouchFits = m.distance >= 36 - 1e-6;
	m.playerWide = m.distance >= 32 - 1e-6;
	return m;
}

} // namespace sme
