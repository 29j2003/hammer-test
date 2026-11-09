// overlay_tools.cpp - Overlays over several faces, strips along face chains, corner drags (see overlay_tools.h).

#include <sme/overlay_tools.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>

#include <sme/entity_catalog.h>
#include <sme/text.h>

namespace sme {

namespace {

const SurfaceFace* faceById(const std::vector<SurfaceFace>& faces, int sideId) {
	for (const auto& face : faces) {
		if (face.sideId == sideId) return &face;
	}
	return nullptr;
}

double segmentDistance(const Vec3& p, const Vec3& a, const Vec3& b) {
	const Vec3 ab = b - a;
	const double len2 = dot(ab, ab);
	const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
	return length(p - (a + ab * t));
}

bool onBoundary(const SurfaceFace& face, const Vec3& p) {
	if (std::abs(dot(face.normal, p) - face.dist) > 0.2) return false;
	for (std::size_t i = 0; i < face.polygon.size(); ++i) {
		if (segmentDistance(p, face.polygon[i], face.polygon[(i + 1) % face.polygon.size()]) < 0.1) return true;
	}
	return false;
}

/// The stretch two faces share (its two ends), if they meet along an edge longer than a unit.
std::optional<std::pair<Vec3, Vec3>> sharedEdge(const SurfaceFace& a, const SurfaceFace& b) {
	std::vector<Vec3> points;
	const auto add = [&](const Vec3& p) {
		if (std::none_of(points.begin(), points.end(), [&](const Vec3& o) { return length(o - p) < 0.1; })) points.push_back(p);
	};
	for (const auto& p : a.polygon) {
		if (onBoundary(b, p)) add(p);
	}
	for (const auto& p : b.polygon) {
		if (onBoundary(a, p)) add(p);
	}
	if (points.size() < 2) return std::nullopt;
	double best = 0;
	std::pair<Vec3, Vec3> ends;
	for (std::size_t i = 0; i < points.size(); ++i) {
		for (std::size_t j = i + 1; j < points.size(); ++j) {
			if (const double d = length(points[i] - points[j]); d > best) {
				best = d;
				ends = {points[i], points[j]};
			}
		}
	}
	if (best < 1) return std::nullopt;
	return ends;
}

Vec3 centroid(const std::vector<Vec3>& points) {
	Vec3 sum;
	for (const auto& p : points) sum += p;
	return points.empty() ? sum : sum / double(points.size());
}

/// Where a ray from `origin` (on or in the polygon) along `dir` leaves the face, and the edge it leaves through.
std::pair<Vec3, double> exitPoint(const SurfaceFace& face, const Vec3& origin, const Vec3& dir) {
	double bestT = 0;
	double edgeLength = 0;
	const auto& poly = face.polygon;
	for (std::size_t i = 0; i < poly.size(); ++i) {
		const Vec3 a = poly[i], b = poly[(i + 1) % poly.size()];
		const Vec3 e = b - a;
		// Solve origin + dir*t = a + e*s in the face plane.
		const Vec3 n = face.normal;
		const double denom = dot(cross(dir, e), n);
		if (std::abs(denom) < 1e-9) continue;
		const double t = dot(cross(a - origin, e), n) / denom;
		const double s = dot(cross(a - origin, dir), n) / denom;
		if (t > 1e-6 && s >= -1e-6 && s <= 1 + 1e-6 && t > bestT) {
			bestT = t;
			edgeLength = length(e);
		}
	}
	return {origin + dir * bestT, edgeLength};
}

Vec3 inPlane(const Vec3& v, const Vec3& n) {
	return v - n * dot(v, n);
}

} // namespace

std::vector<std::pair<std::string, std::string>> overlayKeys(const OverlayInfo& o, const std::string& material) {
	std::string sides;
	for (const int id : o.sides) {
		if (!sides.empty()) sides += ' ';
		sides += std::to_string(id);
	}
	return {
	    {"material", material},
	    {"sides", sides},
	    {"RenderOrder", "0"},
	    {"StartU", formatNumber(o.startU)},
	    {"EndU", formatNumber(o.endU)},
	    {"StartV", formatNumber(o.startV)},
	    {"EndV", formatNumber(o.endV)},
	    {"BasisOrigin", formatVec3(o.origin)},
	    {"BasisU", formatVec3(o.u)},
	    {"BasisV", formatVec3(o.v)},
	    {"BasisNormal", formatVec3(o.normal)},
	    {"uv0", formatVec3(o.corners[0])},
	    {"uv1", formatVec3(o.corners[1])},
	    {"uv2", formatVec3(o.corners[2])},
	    {"uv3", formatVec3(o.corners[3])},
	};
}

std::optional<OverlayInfo> overlayOnFaces(const std::vector<SurfaceFace>& faces, const std::vector<int>& sideIds, double textureAspect, double tileSize,
                                          int* dropped) {
	std::vector<const SurfaceFace*> chosen;
	Vec3 normalSum;
	for (const int id : sideIds) {
		const SurfaceFace* face = faceById(faces, id);
		if (!face || face->polygon.size() < 3) continue;
		double area = 0;
		for (std::size_t i = 2; i < face->polygon.size(); ++i) {
			area += 0.5 * length(cross(face->polygon[i - 1] - face->polygon[0], face->polygon[i] - face->polygon[0]));
		}
		normalSum += face->normal * area;
		chosen.push_back(face);
	}
	int drop = 0;
	if (chosen.empty() || length(normalSum) < 1e-9) {
		if (dropped) *dropped = static_cast<int>(sideIds.size());
		return std::nullopt;
	}
	const Vec3 n = normalize(normalSum);
	OverlayInfo o;
	o.normal = n;
	decalAxes(n, o.u, o.v);
	std::vector<Vec3> points;
	for (const auto* face : chosen) {
		if (dot(face->normal, n) < 0.1 || o.sides.size() >= OVERLAY_MAX_SIDES) {
			++drop;   // turned away: VBSP would project nothing onto it; or beyond VBSP's limit
			continue;
		}
		o.sides.push_back(face->sideId);
		points.insert(points.end(), face->polygon.begin(), face->polygon.end());
	}
	drop += static_cast<int>(sideIds.size() - chosen.size());
	if (dropped) *dropped = drop;
	if (o.sides.empty()) return std::nullopt;
	const Vec3 c = centroid(points);
	double minX = std::numeric_limits<double>::max(), maxX = std::numeric_limits<double>::lowest(), minY = minX, maxY = maxX;
	for (const auto& p : points) {
		const double x = dot(p - c, o.u), y = dot(p - c, o.v);
		minX = std::min(minX, x);
		maxX = std::max(maxX, x);
		minY = std::min(minY, y);
		maxY = std::max(maxY, y);
	}
	o.origin = c + o.u * ((minX + maxX) / 2) + o.v * ((minY + maxY) / 2);
	const double w = (maxX - minX) / 2, h = (maxY - minY) / 2;
	o.corners = {Vec3{-w, -h, 0}, Vec3{-w, h, 0}, Vec3{w, h, 0}, Vec3{w, -h, 0}};
	if (tileSize > 0) {
		const double aspect = textureAspect > 0 ? textureAspect : 1;
		o.endU = (2 * w) / (tileSize * aspect);
		o.endV = (2 * h) / tileSize;
	}
	return o;
}

FaceChainPath faceChainPath(const std::vector<SurfaceFace>& faces, const std::vector<int>& sideIds) {
	FaceChainPath out;
	std::vector<const SurfaceFace*> chain;
	for (const int id : sideIds) {
		const SurfaceFace* face = faceById(faces, id);
		if (face && face->polygon.size() >= 3 && std::find(chain.begin(), chain.end(), face) == chain.end()) chain.push_back(face);
	}
	if (chain.empty()) return out;
	if (chain.size() == 1) {
		// One face: along its longest edge, through its middle.
		const auto& f = *chain.front();
		Vec3 dir;
		double longest = 0;
		for (std::size_t i = 0; i < f.polygon.size(); ++i) {
			const Vec3 e = f.polygon[(i + 1) % f.polygon.size()] - f.polygon[i];
			if (length(e) > longest) {
				longest = length(e);
				dir = normalize(e);
			}
		}
		const Vec3 c = centroid(f.polygon);
		double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
		for (const auto& p : f.polygon) {
			lo = std::min(lo, dot(p - c, dir));
			hi = std::max(hi, dot(p - c, dir));
		}
		out.order = {f.sideId};
		out.points = {c + dir * lo, c + dir * hi};
		out.normals = {f.normal, f.normal};
		const Vec3 across = normalize(cross(f.normal, dir));
		double left = std::numeric_limits<double>::lowest(), right = std::numeric_limits<double>::max();
		for (const auto& p : f.polygon) {
			left = std::max(left, dot(p - c, across));
			right = std::min(right, dot(p - c, across));
		}
		// Through the middle across, too.
		const Vec3 shift = across * ((left + right) / 2);
		out.points = {out.points[0] + shift, out.points[1] + shift};
		out.halfWidths = {(left - right) / 2, (left - right) / 2};
		return out;
	}
	// Neighbours: faces sharing an edge.
	const std::size_t count = chain.size();
	std::vector<std::vector<std::size_t>> next(count);
	std::map<std::pair<std::size_t, std::size_t>, std::pair<Vec3, Vec3>> edges;
	for (std::size_t i = 0; i < count; ++i) {
		for (std::size_t j = i + 1; j < count; ++j) {
			if (const auto e = sharedEdge(*chain[i], *chain[j])) {
				next[i].push_back(j);
				next[j].push_back(i);
				edges[{i, j}] = *e;
				edges[{j, i}] = *e;
			}
		}
	}
	// The longest run: farthest face from any face, then the farthest from that one (by steps).
	const auto bfs = [&](std::size_t start, std::vector<std::size_t>& parent) {
		std::vector<int> depth(count, -1);
		parent.assign(count, count);
		std::deque<std::size_t> queue{start};
		depth[start] = 0;
		std::size_t far = start;
		while (!queue.empty()) {
			const std::size_t k = queue.front();
			queue.pop_front();
			if (depth[k] > depth[far]) far = k;
			for (const std::size_t m : next[k]) {
				if (depth[m] >= 0) continue;
				depth[m] = depth[k] + 1;
				parent[m] = k;
				queue.push_back(m);
			}
		}
		return far;
	};
	std::vector<std::size_t> parent;
	// Start from the face with the fewest neighbours (an end of the chain), the first such one.
	std::size_t start = 0;
	for (std::size_t i = 0; i < count; ++i) {
		if (next[i].size() < next[start].size()) start = i;
	}
	const std::size_t a = bfs(start, parent);
	const std::size_t b = bfs(a, parent);
	std::vector<std::size_t> run;
	for (std::size_t k = b; k != count; k = parent[k]) run.push_back(k);
	std::reverse(run.begin(), run.end());   // from a to b
	if (run.size() < 2) {
		return faceChainPath(faces, {chain[run.empty() ? 0 : run.front()]->sideId});
	}
	for (const std::size_t k : run) out.order.push_back(chain[k]->sideId);
	std::vector<Vec3> mids;
	std::vector<double> halves;
	for (std::size_t i = 0; i + 1 < run.size(); ++i) {
		const auto& e = edges[{run[i], run[i + 1]}];
		mids.push_back((e.first + e.second) * 0.5);
		halves.push_back(length(e.second - e.first) / 2);
	}
	// The far ends: from the first shared edge through the first face's middle to its far edge.
	const auto farEnd = [&](const SurfaceFace& face, const Vec3& from, double half) {
		const Vec3 dir = inPlane(centroid(face.polygon) - from, face.normal);
		if (length(dir) < 1e-6) return std::pair{from, half};
		const auto [point, edge] = exitPoint(face, from, normalize(dir));
		return std::pair{point, edge > 0 ? edge / 2 : half};
	};
	const auto first = farEnd(*chain[run.front()], mids.front(), halves.front());
	const auto last = farEnd(*chain[run.back()], mids.back(), halves.back());
	out.points.push_back(first.first);
	out.points.insert(out.points.end(), mids.begin(), mids.end());
	out.points.push_back(last.first);
	out.halfWidths.push_back(first.second);
	out.halfWidths.insert(out.halfWidths.end(), halves.begin(), halves.end());
	out.halfWidths.push_back(last.second);
	for (std::size_t i = 0; i < out.points.size(); ++i) {
		Vec3 n;
		if (i > 0) n += chain[run[i - 1]]->normal;
		if (i < run.size()) n += chain[run[i]]->normal;
		out.normals.push_back(normalize(n));
	}
	return out;
}

std::vector<OverlayInfo> planOverlayStrip(const std::vector<SurfaceFace>& allFaces, const std::vector<int>& chainSideIds, const OverlayStripOptions& options) {
	std::vector<OverlayInfo> out;
	auto path = faceChainPath(allFaces, chainSideIds);
	if (path.points.size() < 2 || options.width <= 0) return out;
	if (options.reverse) {
		std::reverse(path.points.begin(), path.points.end());
		std::reverse(path.normals.begin(), path.normals.end());
		std::reverse(path.halfWidths.begin(), path.halfWidths.end());
	}
	const std::size_t count = path.points.size();
	// Direction of each stretch, in the surface.
	std::vector<Vec3> dirs;
	for (std::size_t i = 0; i + 1 < count; ++i) {
		const Vec3 n = normalize(path.normals[i] + path.normals[i + 1]);
		dirs.push_back(normalize(inPlane(path.points[i + 1] - path.points[i], n)));
	}
	// Left and right corner at every point, mitred: the joint lies on the bisector.
	std::vector<Vec3> left(count), right(count);
	const double half = options.width / 2;
	for (std::size_t i = 0; i < count; ++i) {
		const Vec3 n = path.normals[i];
		Vec3 tangent = i == 0 ? dirs.front() : i + 1 == count ? dirs.back() : normalize(dirs[i - 1] + dirs[i]);
		if (length(tangent) < 1e-9) tangent = dirs[std::min(i, dirs.size() - 1)];   // a hairpin: no bisector
		const Vec3 side = normalize(cross(n, tangent));
		const double cosHalf = (i == 0 || i + 1 == count) ? 1.0 : std::max(0.5, dot(tangent, dirs[i]));
		const double scale = 1.0 / cosHalf;
		const double edge = path.halfWidths[i] - options.offset;   // Left/Right: the band's outer side
		switch (options.align) {
			case OverlayStripOptions::Align::Middle:
				left[i] = path.points[i] + side * ((options.offset + half) * scale);
				right[i] = path.points[i] + side * ((options.offset - half) * scale);
				break;
			case OverlayStripOptions::Align::Left:
				// The outer corner exactly on the chain's edge (the joint), the band inwards from it.
				left[i] = path.points[i] + side * edge;
				right[i] = left[i] - side * (options.width * scale);
				break;
			case OverlayStripOptions::Align::Right:
				right[i] = path.points[i] - side * edge;
				left[i] = right[i] + side * (options.width * scale);
				break;
		}
	}
	const double repeat = options.width * (options.textureAspect > 0 ? options.textureAspect : 1);
	double travelled = 0;
	for (std::size_t i = 0; i + 1 < count; ++i) {
		const Vec3 m0 = (left[i] + right[i]) * 0.5, m1 = (left[i + 1] + right[i + 1]) * 0.5;
		const double stretch = length(m1 - m0);
		if (stretch < 0.01) continue;
		OverlayInfo o;
		o.normal = normalize(path.normals[i] + path.normals[i + 1]);
		o.u = normalize(inPlane(m1 - m0, o.normal));
		o.v = cross(o.u, o.normal);   // the engine's T = S x normal: upright, not mirrored
		o.origin = m0;
		const auto local = [&](const Vec3& p) { return Vec3{dot(p - o.origin, o.u), dot(p - o.origin, o.v), 0}; };
		// uv0/uv3 (StartV, the texture's top) on the left, uv1/uv2 on the right.
		o.corners = {local(left[i]), local(right[i]), local(right[i + 1]), local(left[i + 1])};
		o.startU = travelled / repeat;
		travelled += stretch;
		o.endU = travelled / repeat;
		o.startV = 0;
		o.endV = 1;
		// Faces under this piece: facing the same way, near in height (not the floor under a bridge).
		Bounds area;
		for (const auto& p : {left[i], right[i], left[i + 1], right[i + 1]}) area.add(p);
		const Vec3 margin{32, 32, 32};
		OverlayInfo probe = o;
		for (const auto& face : allFaces) {
			if (dot(face.normal, o.normal) < 0.1) continue;
			if (face.bounds.max.x < area.min.x - margin.x || face.bounds.min.x > area.max.x + margin.x || face.bounds.max.y < area.min.y - margin.y ||
			    face.bounds.min.y > area.max.y + margin.y || face.bounds.max.z < area.min.z - margin.z || face.bounds.min.z > area.max.z + margin.z) {
				continue;
			}
			probe.sides.push_back(face.sideId);
		}
		const Vec3 planePoint = (left[i] + right[i] + left[i + 1] + right[i + 1]) * 0.25;
		for (const auto& patch : overlayPatches(allFaces, probe)) {
			if (std::find(o.sides.begin(), o.sides.end(), patch.sideId) != o.sides.end()) continue;
			const bool near = std::all_of(patch.vertices.begin(), patch.vertices.end(),
			                              [&](const Vec3& p) { return std::abs(dot(p - planePoint, o.normal)) <= 24; });
			if (near && o.sides.size() < OVERLAY_MAX_SIDES) o.sides.push_back(patch.sideId);
		}
		if (!o.sides.empty()) out.push_back(std::move(o));
	}
	return out;
}

OverlayInfo moveOverlayCorner(const std::vector<SurfaceFace>& faces, const OverlayInfo& overlay, int index, const Vec3& point) {
	OverlayInfo o = overlay;
	if (index < 0 || index > 3) return o;
	o.corners[static_cast<std::size_t>(index)] = {dot(point - o.origin, o.u), dot(point - o.origin, o.v), 0};
	// Faces in the plane of one of its faces that it now reaches join it (a corner dragged over a seam).
	std::vector<const SurfaceFace*> own;
	for (const int id : o.sides) {
		if (const auto* f = faceById(faces, id)) own.push_back(f);
	}
	for (const auto& face : faces) {
		if (o.sides.size() >= OVERLAY_MAX_SIDES) break;
		if (std::find(o.sides.begin(), o.sides.end(), face.sideId) != o.sides.end()) continue;
		const bool samePlane = std::any_of(own.begin(), own.end(), [&](const SurfaceFace* f) {
			return dot(f->normal, face.normal) > 0.999 && std::abs(f->dist - face.dist) < 0.5;
		});
		if (!samePlane) continue;
		OverlayInfo probe = o;
		probe.sides = {face.sideId};
		if (!overlayPatches(faces, probe).empty()) o.sides.push_back(face.sideId);
	}
	return o;
}

namespace edit {

Change placeOverlays(MapDocument& doc, const std::vector<OverlayInfo>& overlays, const std::string& material, const EntityClass* definition,
                     std::vector<int>* ids) {
	Transaction t(doc, overlays.size() == 1 ? "Place overlay" : "Place overlays");
	for (const auto& overlay : overlays) {
		Entity entity;
		entity.id = doc.newEntityId();
		t.touchEntity(entity.id);
		entity.set("classname", "info_overlay");
		if (definition) {
			for (const auto& property : definition->properties) {
				if (property.kind == EntityProperty::Kind::Flags) {
					std::uint64_t flags = 0;
					for (const auto& flag : property.flags) flags |= flag.enabledByDefault ? flag.value : 0;
					entity.set(property.name, std::to_string(flags));
				} else if (!property.defaultValue.empty() && !iequals(property.name, "origin")) {
					entity.set(property.name, property.defaultValue);
				}
			}
		}
		for (auto& [key, value] : overlayKeys(overlay, material)) entity.set(key, std::move(value));
		entity.setOrigin(overlay.origin);
		auto editor = KvNode::makeBlock("editor");
		editor.set("color", "220 30 220");
		editor.set("visgroupshown", "1");
		editor.set("visgroupautoshown", "1");
		entity.extra.push_back(std::move(editor));
		if (ids) ids->push_back(entity.id);
		doc.entities.push_back(std::move(entity));
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
