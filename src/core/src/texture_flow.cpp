// texture_flow.cpp - Texture flow: wrap, along a path, around a curve, match neighbour (see texture_flow.h).

#include <sme/texture_flow.h>

#include <sme/brush.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <tuple>

namespace sme {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double ON_LINE = 0.1;         ///< Units: an edge lies on another face's edge
constexpr double MIN_SHARED = 1.0;      ///< Units: shorter common pieces of edges do not link faces
constexpr double UP = 0.5;              ///< A floor's normal is at most 60° from straight up
constexpr double STANDING = 0.3;        ///< A standing face's normal is at most this far from level (z)
constexpr double BACK_TO_BACK = -0.985; ///< Faces this close to opposite (about 170°) are not folded onto each other
constexpr double SLANT = 0.17;          ///< An axis this far out of its face (about 10°) stretches the texture
const double CURVE_MIN = 3.0 * PI / 180.0;   ///< Folds between these two angles are steps of a curve
const double CURVE_MAX = 50.0 * PI / 180.0;

/// One face the tool works on.
struct Face {
	FaceRef ref;
	std::vector<Vec3> poly;
	Vec3 normal;
	Vec3 centroid;
	Bounds box;
	Side side;
	double area = 0;
	bool selected = true;
};

/// Two faces touch along this common piece of their edges.
struct Link {
	int other = 0;
	Vec3 a, b;      ///< The common piece's ends
	double fold = 0;   ///< Angle between the faces' normals (0 = coplanar)
};

double polygonArea(const std::vector<Vec3>& poly) {
	Vec3 sum;
	for (std::size_t i = 0; i < poly.size(); ++i) sum += cross(poly[i], poly[(i + 1) % poly.size()]);
	return length(sum) * 0.5;
}

/// The longest common piece (at least MIN_SHARED long) of an edge of `a` and an edge of `b`.
std::optional<std::pair<Vec3, Vec3>> sharedSegment(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
	std::optional<std::pair<Vec3, Vec3>> best;
	double bestLength = MIN_SHARED;
	for (std::size_t i = 0; i < a.size(); ++i) {
		const Vec3 p0 = a[i];
		const Vec3 p1 = a[(i + 1) % a.size()];
		const double len = length(p1 - p0);
		if (len < 1e-6) continue;
		const Vec3 u = (p1 - p0) / len;
		for (std::size_t j = 0; j < b.size(); ++j) {
			const Vec3 q0 = b[j];
			const Vec3 q1 = b[(j + 1) % b.size()];
			if (length(cross(q0 - p0, u)) > ON_LINE || length(cross(q1 - p0, u)) > ON_LINE) continue;
			const double t0 = dot(q0 - p0, u);
			const double t1 = dot(q1 - p0, u);
			const double lo = std::max(0.0, std::min(t0, t1));
			const double hi = std::min(len, std::max(t0, t1));
			if (hi - lo > bestLength) {
				bestLength = hi - lo;
				best = std::make_pair(p0 + u * lo, p0 + u * hi);
			}
		}
	}
	return best;
}

/// The longest piece (at least MIN_SHARED long) of an edge of `a` that lies on face `b` - in its
/// plane and inside it (a T-junction: a step's tread ending on the next riser, a wall on a floor).
std::optional<std::pair<Vec3, Vec3>> edgeOnFace(const std::vector<Vec3>& a, const std::vector<Vec3>& b, const Vec3& bNormal) {
	std::optional<std::pair<Vec3, Vec3>> best;
	double bestLength = MIN_SHARED;
	const double d = dot(b.front(), bNormal);
	for (std::size_t i = 0; i < a.size(); ++i) {
		const Vec3 p0 = a[i];
		const Vec3 p1 = a[(i + 1) % a.size()];
		if (std::abs(dot(p0, bNormal) - d) > ON_LINE || std::abs(dot(p1, bNormal) - d) > ON_LINE) continue;
		const double len = length(p1 - p0);
		if (len < MIN_SHARED) continue;
		// Clip p0..p1 to the convex polygon b (inside: on the inner side of every edge).
		double lo = 0, hi = 1;
		Vec3 centre;
		for (const auto& v : b) centre += v;
		centre = centre / static_cast<double>(b.size());
		bool outside = false;
		for (std::size_t j = 0; j < b.size() && !outside; ++j) {
			const Vec3 q0 = b[j];
			const Vec3 q1 = b[(j + 1) % b.size()];
			Vec3 inward = cross(bNormal, q1 - q0);
			if (dot(centre - q0, inward) < 0) inward = -inward;
			const double n = length(inward);
			if (n < 1e-9) continue;
			inward = inward / n;
			const double s0 = dot(p0 - q0, inward) + ON_LINE;
			const double s1 = dot(p1 - q0, inward) + ON_LINE;
			if (s0 < 0 && s1 < 0) {
				outside = true;
			} else if (s0 < 0) {
				lo = std::max(lo, s0 / (s0 - s1));
			} else if (s1 < 0) {
				hi = std::min(hi, s0 / (s0 - s1));
			}
		}
		if (outside || hi <= lo) continue;
		const double piece = (hi - lo) * len;
		if (piece > bestLength) {
			bestLength = piece;
			best = std::make_pair(p0 + (p1 - p0) * lo, p0 + (p1 - p0) * hi);
		}
	}
	return best;
}

std::tuple<long long, long long, long long> pointKey(const Vec3& p) {
	return {std::llround(p.x * 100), std::llround(p.y * 100), std::llround(p.z * 100)};
}

/// The faces of `refs` on valid brushes, each once.
std::vector<Face> collectFaces(const MapDocument& doc, const std::vector<FaceRef>& refs) {
	std::vector<Face> faces;
	std::map<std::pair<int, int>, BrushGeometry> geometries;
	for (const auto& ref : refs) {
		if (std::any_of(faces.begin(), faces.end(), [&](const Face& f) { return f.ref == ref; })) continue;
		const Solid* solid = doc.findSolid(ref.solid);
		if (!solid) continue;
		const auto key = std::make_pair(ref.solid.entityId, ref.solid.solidId);
		auto found = geometries.find(key);
		if (found == geometries.end()) found = geometries.emplace(key, buildBrushGeometry(*solid)).first;
		const BrushGeometry& geometry = found->second;
		if (!geometry.valid) continue;
		for (const auto& f : geometry.faces) {
			const Side& side = solid->sides[static_cast<std::size_t>(f.sideIndex)];
			if (side.id != ref.sideId || f.vertices.size() < 3) continue;
			Face face;
			face.ref = ref;
			face.poly = f.vertices;
			face.normal = normalize(f.normal);
			for (const auto& v : face.poly) {
				face.centroid += v;
				face.box.add(v);
			}
			face.centroid = face.centroid / static_cast<double>(face.poly.size());
			face.side = side;
			face.area = polygonArea(face.poly);
			faces.push_back(std::move(face));
		}
	}
	return faces;
}

bool boxesTouch(const Bounds& a, const Bounds& b) {
	return !(a.max.x + ON_LINE < b.min.x || b.max.x + ON_LINE < a.min.x || a.max.y + ON_LINE < b.min.y || b.max.y + ON_LINE < a.min.y ||
	         a.max.z + ON_LINE < b.min.z || b.max.z + ON_LINE < a.min.z);
}

/// Which faces touch which (faces of the same brush too). Pairs of unselected faces are skipped.
std::vector<std::vector<Link>> linkFaces(const std::vector<Face>& faces) {
	std::vector<std::vector<Link>> links(faces.size());
	for (std::size_t i = 0; i < faces.size(); ++i) {
		for (std::size_t j = i + 1; j < faces.size(); ++j) {
			if (!faces[i].selected && !faces[j].selected) continue;
			if (!boxesTouch(faces[i].box, faces[j].box)) continue;
			const double c = dot(faces[i].normal, faces[j].normal);
			if (c < BACK_TO_BACK) continue;
			auto segment = sharedSegment(faces[i].poly, faces[j].poly);
			if (!segment && std::abs(c) < 0.999) {
				// Not edge to edge: an edge of one lying on the other (T-junction).
				segment = edgeOnFace(faces[i].poly, faces[j].poly, faces[j].normal);
				if (!segment) segment = edgeOnFace(faces[j].poly, faces[i].poly, faces[i].normal);
			}
			if (segment) {
				const double fold = std::acos(std::clamp(c, -1.0, 1.0));
				links[i].push_back({static_cast<int>(j), segment->first, segment->second, fold});
				links[j].push_back({static_cast<int>(i), segment->first, segment->second, fold});
			}
		}
	}
	return links;
}

Vec3 rotateAbout(const Vec3& v, const Vec3& axis, double angle) {
	const double c = std::cos(angle), s = std::sin(angle);
	return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}

/// Texels of `p` on `side`.
TexCoord texel(const Side& side, const Vec3& p) {
	return textureCoordinate(side, p);
}

bool sameTexel(double a, double b, double size) {
	const double d = std::fmod(std::abs(a - b), size);
	return d < 0.5 || size - d < 0.5;
}

/// Spreads the texture from the faces that already have one (`sides` set) to the reachable faces
/// (selected ones only), nearest first: few and small folds win over long detours, so a loop is
/// closed opposite its start.
void spread(const std::vector<Face>& faces, const std::vector<std::vector<Link>>& links, std::vector<std::optional<Side>>& sides,
            const std::vector<double>& startCost) {
	using Entry = std::tuple<double, int, int>;   // cost, order, face
	std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
	std::vector<double> cost(faces.size(), std::numeric_limits<double>::max());
	std::vector<bool> done(faces.size(), false);
	int order = 0;
	for (std::size_t i = 0; i < faces.size(); ++i) {
		if (sides[i]) {
			cost[i] = startCost.empty() ? 0.0 : startCost[i];
			queue.emplace(cost[i], order++, static_cast<int>(i));
		}
	}
	// Where each face got its texture from (face, link) - decided when it is taken off the queue.
	std::vector<std::pair<int, const Link*>> from(faces.size(), {-1, nullptr});
	while (!queue.empty()) {
		const auto [c, ord, at] = queue.top();
		queue.pop();
		(void)ord;
		const auto i = static_cast<std::size_t>(at);
		if (done[i]) continue;
		if (!sides[i] && from[i].first >= 0) {
			const Face& src = faces[static_cast<std::size_t>(from[i].first)];
			const Link& link = *from[i].second;
			sides[i] = foldTexture(*sides[static_cast<std::size_t>(from[i].first)], src.normal, faces[i].normal, link.a, normalize(link.b - link.a));
		}
		if (!sides[i]) continue;
		done[i] = true;
		for (const auto& link : links[i]) {
			const auto j = static_cast<std::size_t>(link.other);
			if (done[j] || !faces[j].selected || (sides[j] && from[j].first < 0)) continue;
			const double next = c + 1.0 + link.fold / PI;
			if (next < cost[j]) {
				cost[j] = next;
				from[j] = {at, &link};
				queue.emplace(next, order++, link.other);
			}
		}
	}
}

/// Counts common edges where the planned textures do not meet, and slanted axes.
void judge(const std::vector<Face>& faces, const std::vector<std::vector<Link>>& links, const std::vector<std::optional<Side>>& sides,
           const std::pair<int, int>& texture, FlowPlan& plan) {
	for (std::size_t i = 0; i < faces.size(); ++i) {
		if (!sides[i] || !faces[i].selected) continue;
		for (const TextureAxis* axis : {&sides[i]->u, &sides[i]->v}) {
			if (std::abs(dot(normalize(axis->axis), faces[i].normal)) > SLANT) plan.stretched = true;
		}
		for (const auto& link : links[i]) {
			const auto j = static_cast<std::size_t>(link.other);
			if (j < i || !sides[j]) continue;
			bool meets = true;
			for (const Vec3& p : {link.a, link.b}) {
				const TexCoord ta = texel(*sides[i], p), tb = texel(*sides[j], p);
				if (!sameTexel(ta.u, tb.u, texture.first) || !sameTexel(ta.v, tb.v, texture.second)) meets = false;
			}
			if (!meets) ++plan.seams;
		}
	}
}

/// The planned sides into the plan's face list (selected faces only), with `material` if set.
void collect(const std::vector<Face>& faces, const std::vector<std::optional<Side>>& sides, const std::string& material, FlowPlan& plan) {
	for (std::size_t i = 0; i < faces.size(); ++i) {
		if (!faces[i].selected) continue;
		if (!sides[i]) {
			++plan.unreached;
			continue;
		}
		Side side = *sides[i];
		if (!material.empty()) side.material = material;
		plan.plan.faces.push_back({faces[i].ref, side, 0});
	}
	plan.plan.chains = plan.plan.faces.empty() ? 0 : 1;
	if (!plan.plan.faces.empty()) plan.plan.material = plan.plan.faces.front().side.material;
}

/// Index of the seed: options' seed, else the first face given (faces were sorted, so it is looked up).
int seedIndex(const std::vector<Face>& faces, const std::vector<FaceRef>& given, const std::optional<FaceRef>& seed,
              const std::function<bool(const Face&)>& allowed) {
	const auto find = [&](const FaceRef& ref) {
		for (std::size_t i = 0; i < faces.size(); ++i) {
			if (faces[i].ref == ref && faces[i].selected && allowed(faces[i])) return static_cast<int>(i);
		}
		return -1;
	};
	if (seed) {
		if (const int i = find(*seed); i >= 0) return i;
	}
	for (const auto& ref : given) {
		if (const int i = find(ref); i >= 0) return i;
	}
	return -1;
}

/// The direction most of the faces' folds run along (a curve's axis), or nothing.
std::optional<Vec3> curveAxis(const std::vector<Face>& faces, const std::vector<std::vector<Link>>& links) {
	std::vector<std::pair<Vec3, double>> dirs;   // direction, summed length
	for (std::size_t i = 0; i < faces.size(); ++i) {
		for (const auto& link : links[i]) {
			if (static_cast<std::size_t>(link.other) < i || link.fold < CURVE_MIN || link.fold > PI / 2 + 0.01) continue;
			Vec3 d = link.b - link.a;
			const double len = length(d);
			if (len < 1e-6) continue;
			d = d / len;
			bool merged = false;
			for (auto& [dir, weight] : dirs) {
				if (std::abs(dot(dir, d)) > 0.98) {
					weight += len;
					merged = true;
					break;
				}
			}
			if (!merged) dirs.emplace_back(d, len);
		}
	}
	if (dirs.empty()) return std::nullopt;
	return std::max_element(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
}

double positiveScale(double scale) {
	return std::abs(scale) > 1e-6 ? std::abs(scale) : 0.25;
}

/// Projected extent of a face along `d`.
std::pair<double, double> span(const Face& face, const Vec3& d) {
	double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
	for (const auto& v : face.poly) {
		lo = std::min(lo, dot(v, d));
		hi = std::max(hi, dot(v, d));
	}
	return {lo, hi};
}

bool isFloorPreset(FlowPreset preset) {
	return preset == FlowPreset::Road || preset == FlowPreset::Kerb || preset == FlowPreset::River;
}

/// Auto: the mode (and preset) from the faces.
std::pair<FlowMode, FlowPreset> autoMode(const std::vector<Face>& faces, const std::vector<std::vector<Link>>& links) {
	if (faces.empty()) return {FlowMode::Wrap, FlowPreset::Road};
	const bool allFloors = std::all_of(faces.begin(), faces.end(), [](const Face& f) { return f.normal.z > UP; });
	if (allFloors) return {FlowMode::Path, FlowPreset::Road};
	int curveSteps = 0, folds = 0;
	for (std::size_t i = 0; i < faces.size(); ++i) {
		for (const auto& link : links[i]) {
			if (static_cast<std::size_t>(link.other) < i || link.fold < CURVE_MIN) continue;
			++folds;
			if (link.fold <= CURVE_MAX) ++curveSteps;
		}
	}
	if (curveSteps >= 2 && curveSteps * 2 >= folds) return {FlowMode::Curve, FlowPreset::Road};
	const bool strips = faces.size() >= 2 && std::all_of(faces.begin(), faces.end(), [](const Face& f) {
		if (std::abs(f.normal.z) > STANDING) return false;
		const auto [zlo, zhi] = span(f, {0, 0, 1});
		const Vec3 along = normalize(cross(Vec3{0, 0, 1}, f.normal));
		const auto [alo, ahi] = span(f, along);
		return (ahi - alo) >= 3 * std::max(1e-6, zhi - zlo);
	});
	if (strips) return {FlowMode::Path, FlowPreset::Trim};
	return {FlowMode::Wrap, FlowPreset::Road};
}

} // namespace

Side foldTexture(const Side& from, const Vec3& fromNormal, const Vec3& toNormal, const Vec3& edgePoint, const Vec3& edgeDir) {
	const Vec3 e = normalize(edgeDir);
	const double angle = std::atan2(dot(cross(fromNormal, toNormal), e), dot(fromNormal, toNormal));
	Side side = from;
	for (auto [src, dst] : {std::pair{&from.u, &side.u}, std::pair{&from.v, &side.v}}) {
		const double scale = std::abs(src->scale) > 1e-9 ? src->scale : 0.25;
		dst->axis = rotateAbout(src->axis, e, angle);
		dst->shift = src->shift + (dot(edgePoint, src->axis) - dot(edgePoint, dst->axis)) / scale;
	}
	return side;
}

std::vector<FaceRef> flowFaces(const MapDocument& doc, const Selection& selection) {
	if (!selection.faces.empty()) return selection.faces;
	std::vector<SolidRef> solids = selection.solids;
	for (const int entityId : selection.entities) {
		if (const Entity* entity = doc.findEntity(entityId)) {
			for (const auto& solid : entity->solids) solids.push_back({entityId, solid.id});
		}
	}
	// Every face; the biggest first (the seed when brushes are selected).
	std::vector<std::pair<double, FaceRef>> faces;
	for (const auto& ref : solids) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const BrushGeometry geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) continue;
		for (const auto& face : geometry.faces) faces.emplace_back(polygonArea(face.vertices), FaceRef{ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
	}
	std::stable_sort(faces.begin(), faces.end(), [](const auto& a, const auto& b) { return a.first > b.first + 1e-6; });
	std::vector<FaceRef> refs;
	for (const auto& [area, ref] : faces) refs.push_back(ref);
	return refs;
}

FlowPlan planTextureFlow(const MapDocument& doc, const std::vector<FaceRef>& given, const FlowOptions& options, const TextureSizeFn& textureSize,
                         MapDocument* ids) {
	FlowPlan plan;
	std::vector<Face> faces = collectFaces(doc, given);
	// Order by place, so the result does not depend on the order (the seed is looked up by ref).
	std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
		return std::make_tuple(pointKey(a.centroid), a.ref.solid.entityId, a.ref.solid.solidId, a.ref.sideId) <
		       std::make_tuple(pointKey(b.centroid), b.ref.solid.entityId, b.ref.solid.solidId, b.ref.sideId);
	});
	if (faces.empty()) return plan;

	FlowMode mode = options.mode;
	FlowPreset preset = options.preset;
	if (mode == FlowMode::Auto) {
		const auto links = linkFaces(faces);
		std::tie(mode, preset) = autoMode(faces, links);
	}
	plan.mode = mode;
	plan.preset = preset;

	// Floors along a path: the road tool.
	if (mode == FlowMode::Path && isFloorPreset(preset)) {
		RoadOptions road;
		road.material = options.material;
		road.flip = options.flip;
		road.repeatsAcross = std::max(1, options.repeatsAcross);
		road.curveCorners = options.curveCorners;
		std::vector<FaceRef> refs;
		for (const auto& f : faces) refs.push_back(f.ref);
		plan.plan = planRoad(doc, refs, road, textureSize, ids);
		return plan;
	}

	// Match neighbour: the faces next to the selection that are not selected are the sources.
	if (mode == FlowMode::Match) {
		Bounds around;
		for (const auto& f : faces) {
			around.add(f.box.min);
			around.add(f.box.max);
		}
		std::vector<FaceRef> nearby;
		const auto look = [&](const Entity& entity, int entityId) {
			for (const auto& solid : entity.solids) {
				const BrushGeometry geometry = buildBrushGeometry(solid);
				if (!geometry.valid || !boxesTouch(geometry.bounds, around)) continue;
				for (const auto& face : geometry.faces) {
					const FaceRef ref{{entityId, solid.id}, solid.sides[static_cast<std::size_t>(face.sideIndex)].id};
					if (std::none_of(faces.begin(), faces.end(), [&](const Face& f) { return f.ref == ref; })) nearby.push_back(ref);
				}
			}
		};
		look(doc.world, 0);
		for (const auto& entity : doc.entities) look(entity, entity.id);
		std::vector<Face> sources = collectFaces(doc, nearby);
		for (auto& s : sources) s.selected = false;
		const std::size_t selectedCount = faces.size();
		faces.insert(faces.end(), sources.begin(), sources.end());
		const auto links = linkFaces(faces);
		std::vector<std::optional<Side>> sides(faces.size());
		std::vector<double> startCost(faces.size(), 0.0);
		for (std::size_t i = selectedCount; i < faces.size(); ++i) {
			bool touches = false, sameMaterial = false;
			for (const auto& link : links[i]) {
				const auto& other = faces[static_cast<std::size_t>(link.other)];
				if (!other.selected) continue;
				touches = true;
				if (other.side.material == faces[i].side.material) sameMaterial = true;
			}
			if (!touches) continue;
			sides[i] = faces[i].side;
			startCost[i] = sameMaterial ? 0.0 : 0.5;   // the same material wins
		}
		spread(faces, links, sides, startCost);
		judge(faces, links, sides, textureSize(options.material.empty() ? faces.front().side.material : options.material), plan);
		collect(faces, sides, options.material, plan);
		return plan;
	}

	// Path on standing strips (Trim, Fence): only the standing faces.
	if (mode == FlowMode::Path) {
		std::vector<Face> standing;
		for (auto& f : faces) {
			if (std::abs(f.normal.z) <= STANDING) standing.push_back(f);
		}
		faces = std::move(standing);
		if (faces.empty()) return plan;
	}

	const auto links = linkFaces(faces);
	const auto any = [](const Face&) { return true; };
	int seed = seedIndex(faces, given, options.seed, any);
	if (mode == FlowMode::Path) {
		// The longest strip unless one was picked.
		if (!options.seed || seed < 0) {
			double best = -1;
			for (std::size_t i = 0; i < faces.size(); ++i) {
				const Vec3 along = normalize(cross(Vec3{0, 0, 1}, faces[i].normal));
				const auto [lo, hi] = span(faces[i], along);
				if (hi - lo > best + 1e-6) {
					best = hi - lo;
					seed = static_cast<int>(i);
				}
			}
		}
	}
	if (seed < 0) return plan;
	const Face& seedFace = faces[static_cast<std::size_t>(seed)];
	plan.seed = seedFace.ref;
	const std::string material = options.material.empty() ? seedFace.side.material : options.material;
	const auto texture = textureSize(material);
	Side start = seedFace.side;
	start.material = material;

	if (mode == FlowMode::Curve) {
		// V along the curve's axis (down when it stands), U round the curve in the seed face.
		Vec3 axis = curveAxis(faces, links).value_or(Vec3{0, 0, 1});
		axis = normalize(axis - seedFace.normal * dot(axis, seedFace.normal));
		if (length(axis) < 0.5) axis = normalize(cross(seedFace.normal, Vec3{1, 0, 0}));
		if (std::abs(axis.z) > 0.7 && axis.z > 0) axis = -axis;
		const Vec3 u = normalize(cross(seedFace.normal, axis));
		start.u.axis = u;
		start.v.axis = axis;
		start.u.scale = positiveScale(start.u.scale);
		start.v.scale = positiveScale(start.v.scale);
		start.rotation = 0;
	} else if (mode == FlowMode::Path) {
		// Standing strips: the texture's height fitted to the strip, its long side along it.
		const auto [zlo, zhi] = span(seedFace, {0, 0, 1});
		const double height = std::max(1.0, zhi - zlo);
		plan.stripSize = height;
		for (const auto& f : faces) {
			const auto [lo, hi] = span(f, {0, 0, 1});
			if (std::abs((hi - lo) - height) > 0.1 * height) plan.unevenStrips = true;
		}
		Vec3 along = normalize(cross(Vec3{0, 0, 1}, seedFace.normal));
		if (options.flip) along = -along;
		const Vec3 down{0, 0, -1};
		const bool alongU = texture.first >= texture.second;   // the texture's longer side runs along
		const int repeats = std::max(1, options.repeatsAcross);
		const double across = static_cast<double>(alongU ? texture.second : texture.first);
		const double scale = height / (across * repeats);
		const auto [alo, ahi] = span(seedFace, along);
		(void)ahi;
		if (alongU) {
			start.u.axis = along;
			start.v.axis = down;
		} else {
			start.u.axis = down;
			start.v.axis = along;
		}
		start.u.scale = start.v.scale = scale;
		TextureAxis& alongAxis = alongU ? start.u : start.v;
		TextureAxis& acrossAxis = alongU ? start.v : start.u;
		alongAxis.shift = -alo / scale;
		acrossAxis.shift = zhi / scale;   // the strip's top is the texture's top
		start.rotation = alongU ? 0 : 90;
	}

	std::vector<std::optional<Side>> sides(faces.size());
	sides[static_cast<std::size_t>(seed)] = start;
	spread(faces, links, sides, {});
	judge(faces, links, sides, texture, plan);
	collect(faces, sides, material, plan);
	return plan;
}

namespace edit {

Change applyTextureFlow(MapDocument& doc, const FlowPlan& plan) {
	if (plan.mode == FlowMode::Path && isFloorPreset(plan.preset)) return applyRoad(doc, plan.plan);
	Transaction t(doc, "Texture flow");
	std::vector<SolidRef> touched;
	for (const auto& change : plan.plan.faces) {
		if (std::find(touched.begin(), touched.end(), change.face.solid) == touched.end()) {
			touched.push_back(change.face.solid);
			t.touchSolid(change.face.solid);
		}
		Solid* solid = doc.findSolid(change.face.solid);
		if (!solid) continue;
		for (auto& side : solid->sides) {
			if (side.id != change.face.sideId) continue;
			side.material = change.side.material;
			side.u = change.side.u;
			side.v = change.side.v;
			side.rotation = change.side.rotation;
		}
	}
	return t.finish();
}

} // namespace edit
} // namespace sme
