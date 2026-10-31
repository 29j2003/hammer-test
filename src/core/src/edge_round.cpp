// edge_round.cpp - Rounding or bevelling corners between faces (see edge_round.h).

#include <sme/edge_round.h>

#include <sme/brush.h>
#include <sme/csg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <type_traits>

namespace sme {

namespace {

constexpr double ON_PLANE = 0.1;       ///< Units a vertex may lie off a plane and still count as on it
constexpr double MIN_CHORD = 2.0;      ///< Shortest segment of an arc (shorter ones make tiny faces)
constexpr double MIN_PIECE = 1.0;      ///< A stretch of a corner must reach at least this far along it
constexpr double MIN_FILL = 2.0;       ///< The strip of a fill next to a face is at least this thick
constexpr double MIN_FILL_STEP = 0.17; ///< Each segment of a fill turns at least this far (about 10 degrees)
constexpr double OVERLAP = 0.5;        ///< Cubic units two brushes may share (rounding of plane points)

bool usable(const Solid& solid) {
	return isConvexValid(solid) && solidVolume(solid) > 1.0;
}

/// Direction between unit vectors `a` and `b` at `t` (0 = a, 1 = b) along the great circle.
Vec3 slerp(const Vec3& a, const Vec3& b, double angle, double t) {
	if (angle < 1e-6) return a;
	const double s = std::sin(angle);
	return normalize(a * (std::sin((1 - t) * angle) / s) + b * (std::sin(t * angle) / s));
}

/// The part of a convex polygon behind a plane.
std::vector<Vec3> clipBehind(const std::vector<Vec3>& polygon, const Plane& plane) {
	std::vector<Vec3> out;
	for (std::size_t i = 0; i < polygon.size(); ++i) {
		const Vec3& p = polygon[i];
		const Vec3& q = polygon[(i + 1) % polygon.size()];
		const double dp = dot(plane.normal, p) - plane.dist, dq = dot(plane.normal, q) - plane.dist;
		if (dp <= 0) out.push_back(p);
		if ((dp < 0 && dq > 0) || (dp > 0 && dq < 0)) out.push_back(p + (q - p) * (dp / (dp - dq)));
	}
	return out;
}

double polygonArea(const std::vector<Vec3>& polygon) {
	Vec3 sum{};
	for (std::size_t i = 1; i + 1 < polygon.size(); ++i) sum = sum + cross(polygon[i] - polygon[0], polygon[i + 1] - polygon[0]);
	return length(sum) * 0.5;
}

/// Area where two faces lie flat against each other (opposite normals, same plane).
double touchArea(const Solid& a, const Solid& b) {
	const auto ga = buildBrushGeometry(a), gb = buildBrushGeometry(b);
	double area = 0;
	for (const auto& fa : ga.faces) {
		for (const auto& fb : gb.faces) {
			if (dot(fa.normal, fb.normal) > -0.999 || fa.vertices.size() < 3 || fb.vertices.size() < 3) continue;
			if (std::abs(dot(fa.normal, fb.vertices[0] - fa.vertices[0])) > ON_PLANE) continue;
			// fb clipped by the edges of fa (each edge plane faces out of fa's polygon).
			std::vector<Vec3> overlap = fb.vertices;
			Vec3 middle{};
			for (const auto& v : fa.vertices) middle = middle + v;
			middle = middle / double(fa.vertices.size());
			for (std::size_t i = 0; i < fa.vertices.size() && overlap.size() >= 3; ++i) {
				const Vec3 p = fa.vertices[i], q = fa.vertices[(i + 1) % fa.vertices.size()];
				Vec3 n = normalize(cross(q - p, fa.normal));
				if (dot(n, middle - p) > 0) n = -n;
				overlap = clipBehind(overlap, Plane{n, dot(n, p)});
			}
			if (overlap.size() >= 3) area += polygonArea(overlap);
		}
	}
	return area;
}

bool samePlaneAs(const Plane& a, const Plane& b, double tolerance = 0.05) {
	return dot(a.normal, b.normal) > 0.9999 && std::abs(a.dist - b.dist) < tolerance;
}

/// A selected face with what the planning needs of it.
struct FaceInfo {
	FaceRef ref;
	const Solid* solid = nullptr;
	const Side* side = nullptr;
	Plane plane;
	std::vector<Vec3> vertices;
};

/// The faces of the two sides of one corner line.
struct CornerFaces {
	std::vector<FaceInfo> a, b;
};

/// The face `ref` of the document with its corners, if it exists.
std::optional<FaceInfo> faceInfo(const MapDocument& doc, const FaceRef& ref) {
	const Solid* solid = doc.findSolid(ref.solid);
	if (!solid) return std::nullopt;
	const auto geometry = buildBrushGeometry(*solid);
	for (const auto& face : geometry.faces) {
		const Side& side = solid->sides[static_cast<std::size_t>(face.sideIndex)];
		if (side.id == ref.sideId && face.vertices.size() >= 3) return FaceInfo{ref, solid, &side, side.plane(), face.vertices};
	}
	return std::nullopt;
}

/// Groups faces into corners (see the file comment of edge_round.h); `ignored` counts faces
/// that touch none.
std::vector<CornerFaces> findCorners(const MapDocument& doc, const std::vector<FaceRef>& faces, int& ignored) {
	std::vector<FaceInfo> infos;
	for (const auto& ref : faces) {
		if (std::any_of(infos.begin(), infos.end(), [&](const FaceInfo& f) { return f.ref == ref; })) continue;
		if (auto info = faceInfo(doc, ref)) infos.push_back(std::move(*info));
	}
	// Faces in one plane form a side.
	std::vector<std::vector<std::size_t>> groups;
	for (std::size_t i = 0; i < infos.size(); ++i) {
		auto group = std::find_if(groups.begin(), groups.end(), [&](const std::vector<std::size_t>& g) { return samePlaneAs(infos[g.front()].plane, infos[i].plane); });
		if (group == groups.end()) {
			groups.push_back({i});
		} else {
			group->push_back(i);
		}
	}
	std::vector<bool> used(infos.size(), false);
	std::vector<CornerFaces> corners;
	for (std::size_t gi = 0; gi < groups.size(); ++gi) {
		for (std::size_t gj = gi + 1; gj < groups.size(); ++gj) {
			const Plane& pi = infos[groups[gi].front()].plane;
			const Plane& pj = infos[groups[gj].front()].plane;
			if (std::abs(dot(pi.normal, pj.normal)) > 0.995) continue;
			const Vec3 d = normalize(cross(pi.normal, pj.normal));
			// A point on the corner line (where both planes meet).
			const Vec3 axis = cross(pi.normal, pj.normal);
			const Vec3 onLine = (cross(pj.normal, axis) * pi.dist + cross(axis, pi.normal) * pj.dist) / dot(axis, axis);
			// Faces the corner line runs along or through (it lies in their plane), how far along
			// it, and how far they reach from it; `beside`: faces of the side that stop short of
			// the line (wholly on one side of it), with their distance from it.
			struct Touch {
				std::vector<std::size_t> on;
				std::vector<std::pair<std::size_t, double>> beside;
				double lo = 1e30, hi = -1e30, reach = 0;
			};
			const auto touching = [&](const std::vector<std::size_t>& group, const Plane& own) {
				const Vec3 across = normalize(cross(d, own.normal));   // in the face's plane, across the line
				Touch out;
				for (const std::size_t k : group) {
					double a = 1e30, b = -1e30, near = 1e30, far = -1e30;
					for (const auto& v : infos[k].vertices) {
						a = std::min(a, dot(d, v));
						b = std::max(b, dot(d, v));
						near = std::min(near, dot(across, v - onLine));
						far = std::max(far, dot(across, v - onLine));
					}
					if (b - a < MIN_PIECE) continue;
					if (near <= ON_PLANE && far >= -ON_PLANE) {
						out.on.push_back(k);
						out.lo = std::min(out.lo, a);
						out.hi = std::max(out.hi, b);
						out.reach = std::max({out.reach, far, -near});
					} else {
						out.beside.push_back({k, near > 0 ? near : -far});
					}
				}
				return out;
			};
			const Touch ta = touching(groups[gi], pi), tb = touching(groups[gj], pj);
			std::vector<std::size_t> a = ta.on, b = tb.on;
			double loA = ta.lo, hiA = ta.hi, loB = tb.lo, hiB = tb.hi;
			// A face of a side may stop short of the line where the side continues in another brush
			// (the outer corner of an L of two walls: one wall's face starts behind the other).
			// Such faces belong to a corner found by the other side, as long as that side reaches
			// that far from the line; with just two sides selected, they make the corner on their own.
			const auto addBeside = [](std::vector<std::size_t>& list, const Touch& own, double otherReach) {
				for (const auto& [k, distance] : own.beside) {
					if (distance <= otherReach + ON_PLANE) list.push_back(k);
				}
			};
			if (!a.empty() || !b.empty()) {
				const bool hadA = !a.empty(), hadB = !b.empty();
				addBeside(a, ta, hadB ? tb.reach : 0);
				addBeside(b, tb, hadA ? ta.reach : 0);
				if (!hadA) loA = loB, hiA = hiB;
				if (!hadB) loB = loA, hiB = hiA;
			}
			if ((a.empty() || b.empty()) && groups.size() == 2) {
				a = groups[gi];
				b = groups[gj];
				loA = loB = -1e30;
				hiA = hiB = 1e30;
			}
			if (a.empty() || b.empty() || std::min(hiA, hiB) - std::max(loA, loB) < MIN_PIECE) continue;
			CornerFaces corner;
			for (const std::size_t k : a) {
				corner.a.push_back(infos[k]);
				used[k] = true;
			}
			for (const std::size_t k : b) {
				corner.b.push_back(infos[k]);
				used[k] = true;
			}
			corners.push_back(std::move(corner));
		}
	}
	ignored = int(std::count(used.begin(), used.end(), false));
	return corners;
}

/// Everything the planning of one corner works on.
struct Corner {
	MapDocument* ids = nullptr;
	const Side* sideA = nullptr;
	const Side* sideB = nullptr;
	Plane planeA, planeB;
	Vec3 d;               ///< Along the corner line
	Vec3 origin;          ///< A point on the corner line
	double angle = 0;     ///< Between the face normals
	double c = 0;         ///< cos(angle)
	std::vector<NearbySolid> neighbours;

	[[nodiscard]] double along(const Vec3& p) const { return dot(d, p - origin); }

	/// Whether other brushes lie against both faces at `s` along the corner line.
	[[nodiscard]] bool covered(double s) const {
		const Vec3 q = origin + d * s;
		const Vec3 nearA = q + planeA.normal * 0.5 - planeB.normal * 0.5;
		const Vec3 nearB = q + planeB.normal * 0.5 - planeA.normal * 0.5;
		const auto inside = [&](const Vec3& p) {
			return std::any_of(neighbours.begin(), neighbours.end(), [&](const NearbySolid& n) {
				return p.x >= n.bounds.min.x - 0.01 && p.x <= n.bounds.max.x + 0.01 && p.y >= n.bounds.min.y - 0.01 && p.y <= n.bounds.max.y + 0.01 &&
				       p.z >= n.bounds.min.z - 0.01 && p.z <= n.bounds.max.z + 0.01 && solidContains(*n.solid, p);
			});
		};
		return inside(nearA) && inside(nearB);
	}

	/// The neighbour's side where the cover starts or ends at `s` (the floor's top), if any.
	[[nodiscard]] const Side* surfaceAt(double s) const {
		const Vec3 q = origin + d * s;
		const Side* best = nullptr;
		double bestAlong = 0.2;
		for (const auto& n : neighbours) {
			for (const auto& side : n.solid->sides) {
				const Plane plane = side.plane();
				const double towards = std::abs(dot(plane.normal, d));
				if (std::abs(dot(plane.normal, q) - plane.dist) < 0.5 && towards > bestAlong) {
					best = &side;
					bestAlong = towards;
				}
			}
		}
		return best;
	}

	/// The arc's corner points for radius `r` and `n` segments, in the plane through the origin
	/// across the corner line: from on face A's plane (first) to on face B's (last).
	[[nodiscard]] std::vector<Vec3> arc(double r, int n, bool inner) const {
		const Vec3 bisector = (planeA.normal + planeB.normal) * (r / (1 + c));
		const Vec3 center = inner ? origin + bisector : origin - bisector;
		std::vector<Vec3> points;
		for (int i = 0; i <= n; ++i) {
			const Vec3 dir = slerp(planeA.normal, planeB.normal, angle, double(i) / n);
			points.push_back(inner ? center - dir * r : center + dir * r);
		}
		return points;
	}

	[[nodiscard]] Vec3 footA(const Vec3& p) const { return p - planeA.normal * (dot(planeA.normal, p) - planeA.dist); }
	[[nodiscard]] Vec3 footB(const Vec3& p) const { return p - planeB.normal * (dot(planeB.normal, p) - planeB.dist); }
};

/// Points along the corner line where the cover by neighbours starts or ends.
std::vector<double> coverChanges(const Corner& corner, double from, double to) {
	std::vector<double> changes;
	if (corner.neighbours.empty() || to - from < 2 * MIN_PIECE) return changes;
	constexpr int SAMPLES = 96;
	const auto at = [&](int k) { return from + (to - from) * (k + 0.5) / SAMPLES; };
	bool previous = corner.covered(at(0));
	for (int k = 1; k < SAMPLES; ++k) {
		const bool now = corner.covered(at(k));
		if (now == previous) continue;
		double lo = at(k - 1), hi = at(k);
		for (int i = 0; i < 30; ++i) {
			const double mid = (lo + hi) / 2;
			(corner.covered(mid) == previous ? lo : hi) = mid;
		}
		const double s = (lo + hi) / 2;
		if (s - from >= MIN_PIECE && to - s >= MIN_PIECE) changes.push_back(s);
		previous = now;
	}
	return changes;
}

/// A plane of the rounding with the plane points every brush gets for it: the same points for
/// all brushes along the corner, so where brushes overlap at the corner (a curb laid into the
/// next one) their curved faces lie in exactly one plane (no fighting faces, nothing hidden by a
/// hair's breadth when picking).
struct ArcPlane {
	Plane plane;
	std::optional<std::array<Vec3, 3>> points;   ///< nullopt: the side's own points from the plane
};

/// The planes of the rounding for radius `r` with `n` segments (outer corner: keep behind them),
/// each through the arc's corner points at both ends of its segment (the arc's chords), taken
/// from points rounded the way the map writes them.
std::vector<ArcPlane> arcPlanes(const Corner& corner, double r, int n) {
	const auto arc = corner.arc(r, n, false);
	// Rounded as tidyPlanePoints rounds corners (whole units where close, else thousandths).
	const auto written = [](double v) {
		const double whole = std::round(v);
		return std::abs(v - whole) < 1e-3 ? whole : std::round(v * 1000) / 1000;
	};
	const auto snapped = [&](const Vec3& v) { return Vec3{written(v.x), written(v.y), written(v.z)}; };
	std::vector<ArcPlane> planes;
	for (int i = 0; i < n; ++i) {
		const Vec3 normal = slerp(corner.planeA.normal, corner.planeB.normal, corner.angle, (i + 0.5) / n);
		Side side;
		side.planePoints = {snapped(arc[std::size_t(i)]), snapped(arc[std::size_t(i) + 1]), snapped(arc[std::size_t(i)] + corner.d * 256)};
		if (dot(side.plane().normal, normal) < 0) std::swap(side.planePoints[0], side.planePoints[1]);
		const Plane plane = side.plane();
		// Points too close for a plane (a tiny radius): the exact plane, points from it.
		if (!(dot(plane.normal, normal) > 0.999)) {
			const Plane exact{normal, dot(normal, arc[std::size_t(i)])};
			planes.push_back({exact, std::nullopt});
			continue;
		}
		planes.push_back({plane, side.planePoints});
	}
	return planes;
}

/// What cutting a piece by the arc planes left.
enum class Cut { Kept, Removed, Broken };

/// The piece cut by the arc planes: kept (into `out`), removed entirely (it lay in the corner
/// the rounding takes away), or broken (a sliver would be left).
Cut cutPiece(const Corner& corner, const Solid& piece, const std::vector<ArcPlane>& planes, Solid& out) {
	out = piece;
	// Beyond the curve's reach (behind every arc plane): kept just as it is.
	const auto before = buildBrushGeometry(piece);
	const bool untouched = std::all_of(before.faces.begin(), before.faces.end(), [&](const FaceGeometry& face) {
		return std::all_of(face.vertices.begin(), face.vertices.end(), [&](const Vec3& v) {
			return std::all_of(planes.begin(), planes.end(), [&](const ArcPlane& p) { return dot(p.plane.normal, v) - p.plane.dist <= 0.01; });
		});
	});
	if (untouched) return Cut::Kept;
	for (std::size_t i = 0; i < planes.size(); ++i) {
		const Side* from = (i + 0.5) / planes.size() < 0.5 ? corner.sideA : corner.sideB;
		Side side = makeSide(*corner.ids, planes[i].plane, from->material, from);
		if (planes[i].points) side.planePoints = *planes[i].points;
		out.sides.push_back(std::move(side));
	}
	const auto geometry = buildBrushGeometry(out);
	if (!geometry.valid || geometry.faces.empty()) return Cut::Removed;
	removeUnusedSides(out);
	tidyPlanePoints(out);
	// Back to the shared points (tidying chose points of this brush's own corners). Matched by
	// plane: side ids may repeat between the map and a preview's scratch ids.
	for (auto& side : out.sides) {
		const Plane plane = side.plane();
		for (const auto& arcPlane : planes) {
			if (arcPlane.points && samePlaneAs(plane, arcPlane.plane)) side.planePoints = *arcPlane.points;
		}
	}
	removeUnusedSides(out);   // a side that only touched at an edge with the tidied points
	if (!usable(out) || !brushProblem(out).empty()) return solidVolume(out) < 1.0 ? Cut::Removed : Cut::Broken;
	return Cut::Kept;
}

/// Largest distance of the polygon's corners from the corner line, measured in its plane.
double reach(const Corner& corner, const std::vector<Vec3>& polygon) {
	double most = 0;
	for (const auto& v : polygon) {
		const Vec3 off = v - corner.origin;
		most = std::max(most, length(off - corner.d * dot(off, corner.d)));
	}
	return most;
}

/// Cross sections of the brushes that fill the space between the corner and the arc (`arc` from
/// face A to face B): strips against face A up to the middle of the arc, strips against face B
/// beyond it, and one block in the corner that takes the segment past the middle along (still
/// convex: the arc turns away from the corner there). n segments give n brushes, each convex,
/// none a sliver, and they do not all meet in one point (no fan). The block is the last one.
std::vector<std::vector<Vec3>> fillSections(const Corner& corner, const std::vector<Vec3>& arc) {
	const int n = int(arc.size()) - 1;
	const auto tidy = [](const std::vector<Vec3>& polygon) {
		std::vector<Vec3> out;
		for (const auto& p : polygon) {
			if (out.empty() || length(p - out.back()) > 0.01) out.push_back(p);
		}
		while (out.size() > 1 && length(out.front() - out.back()) <= 0.01) out.pop_back();
		return out;
	};
	std::vector<std::vector<Vec3>> sections;
	if (n == 1) {
		sections.push_back({corner.origin, arc[0], arc[1]});
		return sections;
	}
	const int k = n / 2;
	for (int i = 0; i < k; ++i) sections.push_back(tidy({arc[i], arc[i + 1], corner.footA(arc[i + 1]), corner.footA(arc[i])}));
	for (int i = k + 1; i < n; ++i) sections.push_back(tidy({arc[i], arc[i + 1], corner.footB(arc[i + 1]), corner.footB(arc[i])}));
	sections.push_back(tidy({corner.origin, corner.footA(arc[k]), arc[k], arc[k + 1], corner.footB(arc[k + 1])}));
	return sections;
}

/// The brush of one cross section from `from` to `to` along the corner line, textured from
/// `texture`, kept inside the planes of `within` (a brush, a cover surface). nullopt if nothing
/// sound is left; `empty` then says whether nothing was there at all (else it would be broken).
std::optional<Solid> sectionBrush(const Corner& corner, const std::vector<Vec3>& section, double from, double to, const Side& texture,
                                  const std::vector<Side>& within, bool& empty) {
	empty = false;
	if (section.size() < 3 || to - from < MIN_PIECE) return std::nullopt;
	std::vector<Vec3> points;
	for (const double s : {from, to}) {
		for (const auto& v : section) points.push_back(v + corner.d * s);
	}
	auto hull = solidFromPoints(*corner.ids, points, texture.material);
	if (!hull) return std::nullopt;
	Solid solid;
	solid.id = hull->id;
	solid.extra = hull->extra;
	for (const auto& side : hull->sides) solid.sides.push_back(makeSide(*corner.ids, side.plane(), texture.material, &texture));
	for (const auto& side : within) {
		Side copy = side;
		copy.id = corner.ids->newSideId();
		solid.sides.push_back(std::move(copy));
	}
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid || geometry.faces.empty()) {
		empty = true;
		return std::nullopt;
	}
	removeUnusedSides(solid);
	tidyPlanePoints(solid);
	if (!usable(solid) || !brushProblem(solid).empty()) {
		empty = solidVolume(solid) < 0.5;   // a crumb left by plane points rounding: nothing there
		return std::nullopt;
	}
	return solid;
}

/// What planning one corner gives.
struct CornerResult {
	std::vector<EdgeRoundPlan::Replacement> replacements;
	std::vector<Solid> added;
	int addedEntity = 0;
	double radius = 0, largest = 0;
	int segments = 0;
	bool clamped = false, raised = false, inner = false, merged = false, needsMerge = false;
	int gapFills = 0, innerFills = 0;
	std::string problem;

	[[nodiscard]] bool ok() const { return problem.empty(); }
};

/// Segments for a fill: fewer while a segment would turn less than about 10 degrees or the
/// strip next to a face would stay thinner than MIN_FILL units.
int fillSegments(const Corner& corner, double r, int n) {
	while (n > 1 && (corner.angle / n < MIN_FILL_STEP || r * (1 - std::cos(corner.angle / n)) < MIN_FILL)) --n;
	return n;
}

/// The largest radius from `start` down for which `attempt` works (`start` itself, else found by
/// stepping down and narrowing in, to half units), with its result; nullopt below half a unit.
/// What works is not one stretch of radii (the number of segments changes with the radius, a
/// cut may touch a brush's corner at one radius and not the next), so plain halving could miss
/// every radius that works: it steps down until one does, then narrows in toward the last miss.
template <typename Attempt>
auto searchRadius(double start, Attempt&& attempt) -> std::optional<std::pair<double, typename std::invoke_result_t<Attempt&, double>::value_type>> {
	if (start < 0.5) return std::nullopt;
	if (auto result = attempt(start)) return std::pair{start, std::move(*result)};
	double missed = start;
	for (double r = start * 0.9; r >= 0.5; r *= 0.9) {
		auto result = attempt(r);
		if (!result) {
			missed = r;
			continue;
		}
		// Narrow in between this radius and the last one that missed, keeping the best that works.
		double best = r, lo = r, hi = missed;
		auto bestResult = std::move(*result);
		for (int i = 0; i < 12; ++i) {
			const double mid = (lo + hi) / 2;
			if (auto found = attempt(mid)) {
				best = lo = mid;
				bestResult = std::move(*found);
			} else {
				hi = mid;
			}
		}
		// Half units where that still works (tidier numbers in the dialog and the map).
		const double tidy = std::floor(best * 2) / 2;
		if (tidy >= 0.5 && tidy < best) {
			if (auto found = attempt(tidy)) return std::pair{tidy, std::move(*found)};
		}
		return std::pair{best, std::move(bestResult)};
	}
	return std::nullopt;
}

/// The smallest radius above `start` up to `most` for which `attempt` works (to half units), with
/// its result: at a flat inner bend a small rounding is thinner than half a unit everywhere, a
/// larger one fits; nullopt if none up to `most` does.
template <typename Attempt>
auto searchRadiusUp(double start, double most, Attempt&& attempt) -> std::optional<std::pair<double, typename std::invoke_result_t<Attempt&, double>::value_type>> {
	double failed = std::max(start, 0.5);
	for (double r = failed * 1.25; r <= most * 1.25; r *= 1.25) {
		const double hi = std::min(r, most);
		if (!attempt(hi)) {
			failed = hi;
			if (hi >= most) break;
			continue;
		}
		// The smallest that works between the last failure and `hi`.
		double lo = failed, top = hi;
		for (int i = 0; i < 20; ++i) {
			const double mid = (lo + top) / 2;
			(attempt(mid) ? top : lo) = mid;
		}
		for (double up = std::ceil(top * 2) / 2; up <= hi + 1e-9; up += 0.5) {
			if (auto result = attempt(up)) return std::pair{up, std::move(*result)};
		}
		if (auto result = attempt(hi)) return std::pair{hi, std::move(*result)};
		return std::nullopt;
	}
	return std::nullopt;
}

/// A brush to round.
struct Target {
	SolidRef ref;
	Solid solid;
};

/// Plans one corner (see the file comment of edge_round.h).
CornerResult planCorner(const MapDocument& doc, MapDocument& ids, const CornerFaces& faces, double radius, int segments, bool allowMerge) {
	CornerResult result;
	Corner corner;
	corner.ids = &ids;
	corner.sideA = faces.a.front().side;
	corner.sideB = faces.b.front().side;
	corner.planeA = faces.a.front().plane;
	corner.planeB = faces.b.front().plane;
	corner.c = std::clamp(dot(corner.planeA.normal, corner.planeB.normal), -1.0, 1.0);
	corner.angle = std::acos(corner.c);
	const Vec3 axis = cross(corner.planeA.normal, corner.planeB.normal);
	corner.d = normalize(axis);
	// A point on the line where the two planes meet, level with the middle of the faces.
	Vec3 middle{};
	std::size_t count = 0;
	for (const auto* side : {&faces.a, &faces.b}) {
		for (const auto& f : *side) {
			for (const auto& v : f.vertices) {
				middle = middle + v;
				++count;
			}
		}
	}
	middle = middle / double(count);
	const double k = dot(corner.d, middle);
	corner.origin = (cross(corner.planeB.normal, corner.d) * corner.planeA.dist + cross(corner.d, corner.planeA.normal) * corner.planeB.dist + axis * k) /
	                dot(corner.planeA.normal, cross(corner.planeB.normal, corner.d));

	// Outer corner (each face lies behind the other's plane) or inner corner (in front); how far
	// the faces reach from the corner limits the radius; where each side runs along the line.
	const double tangent = std::tan(corner.angle / 2);
	int sign = 0;
	double reachMost = 1e30;
	std::vector<std::pair<double, double>> spanA, spanB;
	// Each face lies on one side of the other side's plane: in front (inner corner) or behind
	// (outer). A face that runs on across the line (a floor under the block whose side makes the
	// corner) takes the side the others decide, and only its part on that side counts.
	bool mixed = false;
	const auto vote = [&](const std::vector<FaceInfo>& list, const Plane& other) {
		for (const auto& f : list) {
			const auto behind = clipBehind(f.vertices, other), front = clipBehind(f.vertices, Plane{-other.normal, -other.dist});
			const double back = behind.size() >= 3 ? polygonArea(behind) : 0, fore = front.size() >= 3 ? polygonArea(front) : 0;
			if (back + fore < 1e-3 || (back > 1 && fore > 1)) continue;   // across the line: no say
			const int s = fore > back ? 1 : -1;
			if (sign != 0 && s != sign) mixed = true;
			sign = s;
		}
	};
	vote(faces.a, corner.planeB);
	vote(faces.b, corner.planeA);
	if (sign == 0) {
		// Every face runs across: the larger parts decide.
		double fore = 0, back = 0;
		for (const auto* list : {&faces.a, &faces.b}) {
			for (const auto& f : *list) {
				const Plane& other = list == &faces.a ? corner.planeB : corner.planeA;
				const auto behind = clipBehind(f.vertices, other), front = clipBehind(f.vertices, Plane{-other.normal, -other.dist});
				back += behind.size() >= 3 ? polygonArea(behind) : 0;
				fore += front.size() >= 3 ? polygonArea(front) : 0;
			}
		}
		sign = fore > back ? 1 : -1;
	}
	// How far a side reaches from the line: its farthest face (a side may be several faces side
	// by side, a thin one at the corner and a wide one behind it).
	const auto look = [&](const std::vector<FaceInfo>& list, const Plane& other, std::vector<std::pair<double, double>>& spans) {
		double side = 0;
		for (const auto& f : list) {
			const auto part = clipBehind(f.vertices, sign > 0 ? Plane{-other.normal, -other.dist} : other);
			if (part.size() < 3 || polygonArea(part) < 1e-3) continue;
			side = std::max(side, reach(corner, part));
			double lo = 1e30, hi = -1e30;
			for (const auto& v : part) {
				lo = std::min(lo, corner.along(v));
				hi = std::max(hi, corner.along(v));
			}
			spans.push_back({lo, hi});
		}
		if (side > 0) reachMost = std::min(reachMost, side);
	};
	look(faces.a, corner.planeB, spanA);
	look(faces.b, corner.planeA, spanB);
	if (mixed || spanA.empty() || spanB.empty()) {
		result.problem = "These faces do not form a corner (one lies across the other)";
		return result;
	}
	result.inner = sign > 0;
	result.largest = reachMost / tangent;
	const auto segmentsFor = [&](double r, int most) { return std::clamp(int(std::floor(corner.angle * r / MIN_CHORD)), 1, most); };
	const double start = std::min(radius, result.largest);

	if (result.inner) {
		// Stretches where both sides run: the fill goes there.
		const auto join = [](std::vector<std::pair<double, double>> spans) {
			std::sort(spans.begin(), spans.end());
			std::vector<std::pair<double, double>> out;
			for (const auto& s : spans) {
				if (!out.empty() && s.first <= out.back().second + 0.01) {
					out.back().second = std::max(out.back().second, s.second);
				} else {
					out.push_back(s);
				}
			}
			return out;
		};
		std::vector<std::pair<double, double>> stretches;
		for (const auto& a : join(spanA)) {
			for (const auto& b : join(spanB)) {
				const double lo = std::max(a.first, b.first), hi = std::min(a.second, b.second);
				if (hi - lo >= MIN_PIECE) stretches.push_back({lo, hi});
			}
		}
		if (stretches.empty()) {
			result.problem = "The faces do not run alongside each other";
			return result;
		}
		Bounds around;
		for (const auto* side : {&faces.a, &faces.b}) {
			for (const auto& f : *side) {
				for (const auto& v : f.vertices) around.add(v);
			}
		}
		corner.neighbours = solidsNear(doc, around, 2);
		int used = 0;
		const auto fill = [&](double r) -> std::optional<std::vector<Solid>> {
			used = fillSegments(corner, r, segmentsFor(r, segments));
			const auto sections = fillSections(corner, corner.arc(r, used, true));
			std::vector<Solid> pieces;
			for (const auto& [lo, hi] : stretches) {
				for (std::size_t i = 0; i < sections.size(); ++i) {
					const bool nearB = i + 1 < sections.size() && i >= std::size_t(used / 2);
					bool empty = false;
					auto piece = sectionBrush(corner, sections[i], lo, hi, nearB ? *corner.sideB : *corner.sideA, {}, empty);
					if (!piece) return std::nullopt;
					// The fill must not reach into any brush (it only touches the faces).
					for (const auto& n : corner.neighbours) {
						if (sharedVolume(*piece, *n.solid) > OVERLAP) return std::nullopt;
					}
					pieces.push_back(std::move(*piece));
				}
			}
			return pieces;
		};
		auto found = searchRadius(start, fill);
		// A flat bend: the fill of a small radius is thinner than half a unit everywhere (nothing
		// Source could build); the smallest larger radius that gives a sound fill is taken instead.
		if (!found) found = searchRadiusUp(start, result.largest, fill);
		if (!found) {
			result.problem = "There is no room for a rounding at this corner";
			return result;
		}
		result.radius = found->first;
		result.clamped = result.radius < radius - 1e-6;
		result.raised = result.radius > radius + 1e-6;
		result.segments = fillSegments(corner, result.radius, segmentsFor(result.radius, segments));
		result.innerFills = int(found->second.size());
		result.added = std::move(found->second);
		result.addedEntity = faces.a.front().ref.solid.entityId;
		return result;
	}

	// Outer corner: the brushes of both sides are cut by the arc's planes.
	std::vector<Target> targets;
	for (const auto* side : {&faces.a, &faces.b}) {
		for (const auto& f : *side) {
			if (std::none_of(targets.begin(), targets.end(), [&](const Target& t) { return t.ref == f.ref.solid; })) targets.push_back({f.ref.solid, *f.solid});
		}
	}
	std::vector<SolidRef> skip;
	Bounds around;
	for (const auto& t : targets) {
		skip.push_back(t.ref);
		around.add(buildBrushGeometry(t.solid).bounds);
	}
	corner.neighbours = solidsNear(doc, around, 2, skip);

	const auto planOuter = [&](const std::vector<Target>& brushes, CornerResult& out) -> bool {
		// Pieces: parts of the brushes in front of a face's plane (outside the corner's wedge) stay
		// as they are; the rest is rounded whole.
		struct Piece {
			Solid solid;
			bool keep = false;
			std::size_t target = 0;
		};
		std::vector<Piece> pieces;
		for (std::size_t t = 0; t < brushes.size(); ++t) pieces.push_back({brushes[t].solid, false, t});
		for (const auto& [plane, side] : {std::pair{corner.planeA, corner.sideA}, std::pair{corner.planeB, corner.sideB}}) {
			std::vector<Piece> next;
			for (auto& piece : pieces) {
				auto [front, back] = splitSolid(ids, piece.solid, plane, *side);
				if (front && back) {
					next.push_back({std::move(*back), piece.keep, piece.target});
					next.push_back({std::move(*front), true, piece.target});
				} else {
					next.push_back(std::move(piece));
				}
			}
			pieces = std::move(next);
		}
		// Stretches of the corner line where neighbours cover both faces: there the space the
		// rounding opens gets filled (the brushes keep one piece each; no split at the surface).
		double from = 1e30, to = -1e30;
		for (const auto& t : brushes) {
			for (const auto& face : buildBrushGeometry(t.solid).faces) {
				for (const auto& v : face.vertices) {
					from = std::min(from, corner.along(v));
					to = std::max(to, corner.along(v));
				}
			}
		}
		std::vector<double> marks{from};
		for (const double s : coverChanges(corner, from, to)) marks.push_back(s);
		marks.push_back(to);
		struct Covered {
			double lo, hi;
			const Side* loSurface;
			const Side* hiSurface;
		};
		std::vector<Covered> covered;
		for (std::size_t i = 0; i + 1 < marks.size(); ++i) {
			if (!corner.covered((marks[i] + marks[i + 1]) / 2)) continue;
			covered.push_back({marks[i], marks[i + 1], i > 0 ? corner.surfaceAt(marks[i]) : nullptr,
			                   i + 2 < marks.size() ? corner.surfaceAt(marks[i + 1]) : nullptr});
		}

		// Brushes that touched before must still touch afterwards (no hole between two walls).
		const auto touching = [](const std::vector<Piece>& all) {
			double area = 0;
			for (std::size_t i = 0; i < all.size(); ++i) {
				for (std::size_t j = i + 1; j < all.size(); ++j) {
					if (all[i].target != all[j].target && !all[i].keep && !all[j].keep) area += touchArea(all[i].solid, all[j].solid);
				}
			}
			return area;
		};
		const double touchedBefore = brushes.size() > 1 ? touching(pieces) : 0;
		const auto segmentsAt = [&](double r) {
			const int n = segmentsFor(r, segments);
			return covered.empty() ? n : fillSegments(corner, r, n);
		};
		const auto cut = [&](double r) -> std::optional<std::pair<std::vector<Piece>, std::vector<Solid>>> {
			const int used = segmentsAt(r);
			const auto planes = arcPlanes(corner, r, used);
			std::vector<Piece> rounded;
			for (const auto& piece : pieces) {
				if (piece.keep) {
					rounded.push_back(piece);
					continue;
				}
				Solid solid;
				const Cut how = cutPiece(corner, piece.solid, planes, solid);
				if (how == Cut::Broken) return std::nullopt;
				if (how == Cut::Kept) rounded.push_back({std::move(solid), false, piece.target});
			}
			if (std::none_of(rounded.begin(), rounded.end(), [](const Piece& p) { return !p.keep; })) return std::nullopt;   // nothing left to round
			// A quarter of the contact must stay, else the corner gets paper thin between them.
			if (touchedBefore > 1 && touching(rounded) < std::max(0.5, touchedBefore / 4)) return std::nullopt;
			// Fills next to the neighbours: what the rounding took away of each brush there.
			std::vector<Solid> fills;
			if (!covered.empty()) {
				const auto sections = fillSections(corner, corner.arc(r, used, false));
				for (const auto& stretch : covered) {
					const Side* surface = stretch.hiSurface ? stretch.hiSurface : stretch.loSurface;
					const double mid = (stretch.lo + stretch.hi) / 2;
					std::vector<Side> caps;
					for (const auto* cap : {stretch.loSurface, stretch.hiSurface}) {
						if (!cap) continue;
						// The cover surface bounds the fill (keep the side the stretch lies on).
						const Plane plane = cap->plane();
						caps.push_back(dot(plane.normal, corner.origin + corner.d * mid) - plane.dist > 0 ? makeSide(ids, Plane{-plane.normal, -plane.dist}, cap->material, cap)
						                                                                                    : *cap);
					}
					// Without a surface the stretch ends at a brush's end (its own side bounds the fill).
					const double lo = stretch.loSurface || stretch.lo <= from + 0.01 ? stretch.lo - 1 : stretch.lo;
					const double hi = stretch.hiSurface || stretch.hi >= to - 0.01 ? stretch.hi + 1 : stretch.hi;
					for (const auto& t : brushes) {
						std::vector<Side> within = t.solid.sides;
						within.insert(within.end(), caps.begin(), caps.end());
						for (const auto& section : sections) {
							bool empty = false;
							auto piece = sectionBrush(corner, section, lo, hi, surface ? *surface : *corner.sideA, within, empty);
							if (!piece && empty) continue;   // the brush has nothing in this part of the corner
							if (!piece) return std::nullopt;  // a sliver would be left: try another radius
							for (const auto& n : corner.neighbours) {
								if (sharedVolume(*piece, *n.solid) > OVERLAP) return std::nullopt;
							}
							fills.push_back(std::move(*piece));
						}
					}
				}
			}
			return std::pair{std::move(rounded), std::move(fills)};
		};
		auto found = searchRadius(start, cut);
		if (!found) return false;
		out.radius = found->first;
		out.segments = segmentsAt(out.radius);
		auto& [rounded, fills] = found->second;
		out.gapFills = int(fills.size());
		out.added = std::move(fills);
		out.addedEntity = brushes.front().ref.entityId;
		for (std::size_t t = 0; t < brushes.size(); ++t) {
			EdgeRoundPlan::Replacement replacement{brushes[t].ref, {}};
			for (auto& piece : rounded) {
				if (piece.target == t) replacement.pieces.push_back(std::move(piece.solid));
			}
			// The piece with the original's sides keeps its id (selection and face references stay).
			std::stable_partition(replacement.pieces.begin(), replacement.pieces.end(), [&](const Solid& s) { return s.id == brushes[t].ref.solidId; });
			for (std::size_t i = 0; i < replacement.pieces.size(); ++i) {
				if (i == 0) {
					replacement.pieces[i].id = brushes[t].ref.solidId;
				} else if (replacement.pieces[i].id == brushes[t].ref.solidId) {
					replacement.pieces[i].id = ids.newSolidId();
				}
			}
			out.replacements.push_back(std::move(replacement));
		}
		return true;
	};

	if (planOuter(targets, result)) {
		result.clamped = result.radius < radius - 1e-6;
		return result;
	}
	// The brushes cannot be rounded apart: as one brush, if together they make one exactly.
	if (targets.size() > 1) {
		std::vector<Solid> solids;
		for (const auto& t : targets) solids.push_back(t.solid);
		if (const auto merged = mergeSolids(ids, solids); merged && merged->exact) {
			Solid one = merged->solid;
			one.id = targets.front().ref.solidId;
			CornerResult attempt = result;
			if (planOuter({Target{targets.front().ref, one}}, attempt)) {
				if (!allowMerge) {
					result.needsMerge = true;
					result.problem = "These pieces can only be rounded together after merging them into one brush - Merge and round?";
					return result;
				}
				attempt.clamped = attempt.radius < radius - 1e-6;
				attempt.merged = true;
				for (std::size_t t = 1; t < targets.size(); ++t) attempt.replacements.push_back({targets[t].ref, {}});
				return attempt;
			}
		}
	}
	result.problem = "There is no room for a rounding at this corner";
	return result;
}

/// Puts a corner's result into a working copy of the map (for the next corner).
void applyCorner(MapDocument& doc, const CornerResult& result) {
	for (const auto& replacement : result.replacements) {
		auto* entity = doc.findEntity(replacement.ref.entityId);
		if (!entity) continue;
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == replacement.ref.solidId; });
		entity->solids.insert(entity->solids.end(), replacement.pieces.begin(), replacement.pieces.end());
	}
	if (auto* entity = doc.findEntity(result.addedEntity)) entity->solids.insert(entity->solids.end(), result.added.begin(), result.added.end());
}

using Key = std::pair<int, int>;   ///< Entity id and solid id

Key keyOf(const SolidRef& ref) {
	return {ref.entityId, ref.solidId};
}

} // namespace

std::string brushProblem(const Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid || geometry.faces.size() != solid.sides.size()) return "not a closed convex brush";
	if (!isConvexValid(solid)) return "a face without area";
	for (const auto& face : geometry.faces) {
		for (std::size_t i = 0; i < face.vertices.size(); ++i) {
			if (length(face.vertices[i] - face.vertices[(i + 1) % face.vertices.size()]) < 0.25) return "an edge shorter than a quarter unit";
		}
	}
	// Thickness: how far the brush reaches behind each of its faces.
	for (const auto& face : geometry.faces) {
		const Plane plane = solid.sides[static_cast<std::size_t>(face.sideIndex)].plane();
		double deepest = 0;
		for (const auto& other : geometry.faces) {
			for (const auto& v : other.vertices) deepest = std::max(deepest, plane.dist - dot(plane.normal, v));
		}
		if (deepest < 0.5) return "thinner than half a unit";
	}
	return {};
}

double sharedVolume(const Solid& a, const Solid& b) {
	const auto ba = buildBrushGeometry(a).bounds, bb = buildBrushGeometry(b).bounds;
	for (int i = 0; i < 3; ++i) {
		if (ba.min[i] >= bb.max[i] - 0.01 || bb.min[i] >= ba.max[i] - 0.01) return 0;
	}
	Solid both;
	both.sides = a.sides;
	for (const auto& side : b.sides) {
		const Plane plane = side.plane();
		if (std::none_of(both.sides.begin(), both.sides.end(), [&](const Side& s) { return samePlaneAs(s.plane(), plane, 0.01); })) both.sides.push_back(side);
	}
	return solidVolume(both);
}

int defaultRoundSegments(double radius) {
	const double arc = radius * std::numbers::pi / 2;
	return std::clamp(int(std::lround(arc / 12)), 2, 16);
}

std::vector<Solid> EdgeRoundPlan::resultSolids() const {
	std::vector<Solid> all;
	for (const auto& r : replacements) all.insert(all.end(), r.pieces.begin(), r.pieces.end());
	all.insert(all.end(), added.begin(), added.end());
	return all;
}

std::vector<SolidRef> EdgeRoundPlan::replacedRefs() const {
	std::vector<SolidRef> refs;
	for (const auto& r : replacements) refs.push_back(r.ref);
	return refs;
}

EdgeRoundPlan planEdgeRounds(const MapDocument& doc, MapDocument& ids, const std::vector<FaceRef>& faces, double radius, int segments, const EdgeRoundOptions& options) {
	EdgeRoundPlan plan;
	plan.requested = radius;
	plan.requestedSegments = std::clamp(segments > 0 ? segments : defaultRoundSegments(radius), 1, 64);
	plan.segments = plan.requestedSegments;
	if (!(radius > 0)) {
		plan.problem = "The radius must be more than 0";
		return plan;
	}
	const auto corners = findCorners(doc, faces, plan.ignoredFaces);
	if (corners.empty()) {
		plan.problem = faces.size() < 2 ? "Select the faces on both sides of the edge (Faces mode, Ctrl+click)"
		                                : "These faces do not meet at a corner - select the faces on both sides of an edge";
		return plan;
	}
	// One corner plans on the map itself; several one after the other on a working copy (a later
	// corner may round pieces of an earlier one). New ids then come from the copy for a preview
	// (they must not clash with the map's ids), from `ids` when applying.
	std::optional<MapDocument> work;
	if (corners.size() > 1) work.emplace(doc);
	MapDocument& idSource = work && &ids != &doc ? *work : ids;
	std::map<Key, Key> originOf;                 // a piece -> the original brush (0,0 = an added brush)
	std::vector<Key> origins;                    // original brushes replaced, in order
	std::map<Key, std::vector<Key>> pieces;      // original brush -> its current pieces
	std::vector<Key> addedKeys;
	double radiusUsed = 0, largest = 1e30;
	int segmentsUsed = plan.requestedSegments;
	for (const auto& found : corners) {
		const MapDocument& current = work ? *work : doc;
		// The faces may have gone with an earlier corner: look each up again.
		CornerFaces cornerFaces;
		for (const auto& f : found.a) {
			if (auto info = faceInfo(current, f.ref)) cornerFaces.a.push_back(std::move(*info));
		}
		for (const auto& f : found.b) {
			if (auto info = faceInfo(current, f.ref)) cornerFaces.b.push_back(std::move(*info));
		}
		if (cornerFaces.a.empty() || cornerFaces.b.empty()) continue;
		CornerResult result = planCorner(current, idSource, cornerFaces, radius, plan.requestedSegments, options.allowMerge);
		if (!result.ok()) {
			plan.needsMerge = result.needsMerge;
			plan.problem = result.problem;
			plan.largest = result.largest;
			return plan;
		}
		++plan.corners;
		plan.inner = plan.inner || result.inner;
		plan.clamped = plan.clamped || result.clamped;
		plan.raised = plan.raised || result.raised;
		plan.merged = plan.merged || result.merged;
		plan.gapFills += result.gapFills;
		plan.innerFills += result.innerFills;
		radiusUsed = plan.corners == 1 ? result.radius : std::min(radiusUsed, result.radius);
		largest = std::min(largest, std::max(result.largest, result.radius));
		segmentsUsed = plan.corners == 1 ? result.segments : std::min(segmentsUsed, result.segments);
		if (!work) {
			plan.replacements = std::move(result.replacements);
			plan.added = std::move(result.added);
			plan.addedEntities.assign(plan.added.size(), result.addedEntity);
			break;
		}
		// Track where every piece came from.
		for (const auto& replacement : result.replacements) {
			const Key from = keyOf(replacement.ref);
			const auto known = originOf.find(from);
			const Key origin = known != originOf.end() ? known->second : from;
			if (known == originOf.end()) {
				origins.push_back(origin);
				pieces[origin] = {origin};
			}
			auto& list = origin == Key{0, 0} ? addedKeys : pieces[origin];
			std::erase(list, from);
			for (const auto& piece : replacement.pieces) {
				const Key ref{replacement.ref.entityId, piece.id};
				list.push_back(ref);
				originOf[ref] = origin;
			}
		}
		for (const auto& solid : result.added) {
			const Key ref{result.addedEntity, solid.id};
			addedKeys.push_back(ref);
			originOf[ref] = {0, 0};
		}
		applyCorner(*work, result);
	}
	if (plan.corners == 0) {
		plan.problem = "These faces do not meet at a corner - select the faces on both sides of an edge";
		return plan;
	}
	plan.radius = radiusUsed;
	plan.largest = largest;
	plan.segments = segmentsUsed;
	if (work) {
		for (const auto& origin : origins) {
			EdgeRoundPlan::Replacement replacement{{origin.first, origin.second}, {}};
			for (const auto& ref : pieces[origin]) {
				if (const Solid* solid = work->findSolid({ref.first, ref.second})) replacement.pieces.push_back(*solid);
			}
			std::stable_partition(replacement.pieces.begin(), replacement.pieces.end(), [&](const Solid& s) { return s.id == origin.second; });
			plan.replacements.push_back(std::move(replacement));
		}
		for (const auto& ref : addedKeys) {
			if (const Solid* solid = work->findSolid({ref.first, ref.second})) {
				plan.added.push_back(*solid);
				plan.addedEntities.push_back(ref.first);
			}
		}
	}
	if (plan.segments < plan.requestedSegments) plan.note = "Fewer segments, so every brush stays sound";
	return plan;
}

EdgeRoundPlan planEdgeRound(const MapDocument& doc, MapDocument& ids, const FaceRef& a, const FaceRef& b, double radius, int segments) {
	if (a.solid == b.solid && a.sideId == b.sideId) {
		EdgeRoundPlan plan;
		plan.requested = radius;
		plan.problem = "Select two different faces";
		return plan;
	}
	return planEdgeRounds(doc, ids, {a, b}, radius, segments);
}

void applyEdgeRound(MapDocument& doc, Transaction& transaction, const EdgeRoundPlan& plan) {
	if (!plan.ok()) return;
	for (const auto& replacement : plan.replacements) {
		auto* entity = doc.findEntity(replacement.ref.entityId);
		if (!entity) continue;
		transaction.touchSolid(replacement.ref);
		// In place where the brush keeps its id (undo puts it back exactly there), new pieces after.
		auto at = std::find_if(entity->solids.begin(), entity->solids.end(), [&](const Solid& s) { return s.id == replacement.ref.solidId; });
		const bool keeps = !replacement.pieces.empty() && replacement.pieces.front().id == replacement.ref.solidId;
		if (at != entity->solids.end()) {
			if (keeps) {
				*at = replacement.pieces.front();
			} else {
				entity->solids.erase(at);
			}
		}
		for (std::size_t i = keeps ? 1 : 0; i < replacement.pieces.size(); ++i) {
			const auto& piece = replacement.pieces[i];
			transaction.touchSolid({replacement.ref.entityId, piece.id});
			entity->solids.push_back(piece);
		}
	}
	for (std::size_t i = 0; i < plan.added.size(); ++i) {
		const int entityId = i < plan.addedEntities.size() ? plan.addedEntities[i] : 0;
		if (auto* entity = doc.findEntity(entityId)) {
			transaction.touchSolid({entityId, plan.added[i].id});
			entity->solids.push_back(plan.added[i]);
		}
	}
}

std::optional<Change> applyEdgeRound(MapDocument& doc, const EdgeRoundPlan& plan, const std::string& name) {
	if (!plan.ok()) return std::nullopt;
	Transaction transaction(doc, name);
	applyEdgeRound(doc, transaction, plan);
	return transaction.finish();
}

} // namespace sme
