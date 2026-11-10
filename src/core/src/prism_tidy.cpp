// prism_tidy.cpp - Clean pieces after cuts (see prism_tidy.h).

#include <sme/prism_tidy.h>

#include <sme/brush.h>
#include <sme/csg.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>

namespace sme {

namespace {

constexpr double WELD = 0.02;   // corners closer than this are one corner
const char* const NODRAW_MATERIAL = "TOOLS/TOOLSNODRAW";

using Poly = std::vector<Vec3>;   // seen from above: x, y (z = 0), counter-clockwise

double cross2(const Vec3& a, const Vec3& b) {
	return a.x * b.y - a.y * b.x;
}

double area(const Poly& p) {
	double a = 0;
	for (std::size_t i = 0; i < p.size(); ++i) a += cross2(p[i], p[(i + 1) % p.size()]);
	return a / 2;
}

double len2(const Vec3& v) {
	return std::sqrt(v.x * v.x + v.y * v.y);
}

Vec3 unit2(const Vec3& v) {
	const double l = len2(v);
	return l > 1e-12 ? Vec3{v.x / l, v.y / l, 0} : Vec3{};
}

/// Counter-clockwise angle from direction a to direction b, in [0, 2 pi).
double ccw(const Vec3& a, const Vec3& b) {
	double t = std::atan2(cross2(a, b), a.x * b.x + a.y * b.y);
	if (t < 0) t += 2 * std::numbers::pi;
	return t;
}

constexpr double DEG = std::numbers::pi / 180;

/// Corners that bulge out of the line between their neighbours by less than three hundredths of a unit
/// are dropped (cuts leave such specks; the edges are one). A curve of radius 16 cut into 10 pieces per
/// quarter bulges by 0.05: real corners stay.
Poly withoutStraightCorners(Poly p) {
	for (bool changed = true; changed && p.size() > 3;) {
		changed = false;
		for (std::size_t i = 0; i < p.size() && p.size() > 3;) {
			const Vec3& a = p[(i + p.size() - 1) % p.size()];
			const Vec3& b = p[i];
			const Vec3& c = p[(i + 1) % p.size()];
			const Vec3 ac = c - a;
			const double l1 = len2(b - a), l2 = len2(c - b), lac = len2(ac);
			bool drop = l1 < 1e-6 || l2 < 1e-6;
			if (!drop && lac > 1e-9) {
				const double bulge = std::abs(cross2(ac, b - a)) / lac;
				const bool between = dot(b - a, ac) > 0 && dot(c - b, ac) > 0;
				drop = between && bulge < 0.03;
			}
			if (drop) {
				p.erase(p.begin() + std::ptrdiff_t(i));
				changed = true;
			} else {
				++i;
			}
		}
	}
	return p;
}

bool isReflex(const Poly& p, std::size_t i) {
	const Vec3& a = p[(i + p.size() - 1) % p.size()];
	const Vec3& b = p[i];
	const Vec3& c = p[(i + 1) % p.size()];
	const Vec3 e1 = b - a, e2 = c - b;
	return cross2(e1, e2) < -1e-6 * len2(e1) * len2(e2);
}

struct Hit {
	double t = 0;
	std::size_t edge = 0;   // from p[edge] to p[edge + 1]
	Vec3 point;
};

/// Where a ray from corner `from` leaves the polygon first (edges next to the corner do not count).
std::optional<Hit> castRay(const Poly& p, std::size_t from, const Vec3& d) {
	const std::size_t n = p.size();
	const Vec3 o = p[from];
	std::optional<Hit> best;
	for (std::size_t e = 0; e < n; ++e) {
		if (e == from || (e + 1) % n == from) continue;
		const Vec3 a = p[e], b = p[(e + 1) % n];
		const Vec3 s = b - a;
		const double denom = cross2(d, s);
		if (std::abs(denom) < 1e-12) continue;
		const Vec3 ao = a - o;
		const double t = cross2(ao, s) / denom;
		const double u = cross2(ao, d) / denom;
		if (t <= 1e-6 || u < -1e-9 || u > 1 + 1e-9) continue;
		if (!best || t < best->t) best = Hit{t, e, o + d * t};
	}
	return best;
}

/// Whether segment a-b crosses an edge of the polygon (touching at the ends does not count).
bool crossesEdges(const Poly& p, const Vec3& a, const Vec3& b, std::size_t skipA, std::size_t skipB) {
	const std::size_t n = p.size();
	for (std::size_t e = 0; e < n; ++e) {
		const std::size_t f = (e + 1) % n;
		if (e == skipA || f == skipA || e == skipB || f == skipB) continue;
		const Vec3 c = p[e], d = p[f];
		const double d1 = cross2(b - a, c - a), d2 = cross2(b - a, d - a), d3 = cross2(d - c, a - c), d4 = cross2(d - c, b - c);
		if (((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) && ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9))) return true;
	}
	return false;
}

/// One way to cut the polygon at corner `r`: to corner `k`, or to a new point on edge `edge`.
struct Cut {
	bool toCorner = false;
	std::size_t k = 0;
	std::size_t edge = 0;
	Vec3 point;
	double worst = 0;    // the sharpest corner it makes (radians)
	bool hint = false;
	bool resolves = false;   // both parts are convex at r
};

std::pair<Poly, Poly> split(const Poly& p, std::size_t r, const Cut& cut) {
	const std::size_t n = p.size();
	Poly a, b;
	if (cut.toCorner) {
		for (std::size_t i = r;; i = (i + 1) % n) {
			a.push_back(p[i]);
			if (i == cut.k) break;
		}
		for (std::size_t i = cut.k;; i = (i + 1) % n) {
			b.push_back(p[i]);
			if (i == r) break;
		}
	} else {
		for (std::size_t i = r;; i = (i + 1) % n) {
			a.push_back(p[i]);
			if (i == cut.edge) break;
		}
		a.push_back(cut.point);
		b.push_back(cut.point);
		for (std::size_t i = (cut.edge + 1) % n;; i = (i + 1) % n) {
			b.push_back(p[i]);
			if (i == r) break;
		}
	}
	return {withoutStraightCorners(std::move(a)), withoutStraightCorners(std::move(b))};
}

/// The sharpest corner of a polygon (radians).
double sharpest(const Poly& p) {
	double worst = std::numbers::pi;
	for (std::size_t i = 0; i < p.size(); ++i) {
		const Vec3& a = p[(i + p.size() - 1) % p.size()];
		const Vec3& b = p[i];
		const Vec3& c = p[(i + 1) % p.size()];
		const double angle = ccw(c - b, a - b);   // inside angle (counter-clockwise polygon)
		worst = std::min(worst, angle);
	}
	return worst;
}

std::optional<Cut> evaluate(const Poly& p, std::size_t r, const Vec3& d, bool hint, const PrismTidyOptions& o) {
	const std::size_t n = p.size();
	const Vec3 v = p[r];
	const Vec3 toNext = p[(r + 1) % n] - v, toPrev = p[(r + n - 1) % n] - v;
	const double inside = ccw(toNext, toPrev);   // the reflex inside angle
	const double first = ccw(toNext, d);
	if (first <= 1e-6 || first >= inside - 1e-6) return std::nullopt;   // not into the polygon
	const auto hit = castRay(p, r, d);
	if (!hit) return std::nullopt;
	Cut cut;
	cut.hint = hint;
	const Vec3 a = p[hit->edge], b = p[(hit->edge + 1) % n];
	const double snap = std::max(o.minEdge, 4.0);
	const double toA = len2(hit->point - a), toB = len2(hit->point - b);
	if (toA < snap || toB < snap) {
		cut.toCorner = true;
		cut.k = toA <= toB ? hit->edge : (hit->edge + 1) % n;
		if (cut.k == r || cut.k == (r + 1) % n || cut.k == (r + n - 1) % n) return std::nullopt;
		if (crossesEdges(p, v, p[cut.k], r, cut.k)) return std::nullopt;
	} else {
		cut.edge = hit->edge;
		cut.point = hit->point;
	}
	const auto [pa, pb] = split(p, r, cut);
	if (pa.size() < 3 || pb.size() < 3 || area(pa) <= 1e-6 || area(pb) <= 1e-6) return std::nullopt;
	cut.worst = std::min(sharpest(pa), sharpest(pb));
	// Convex at r in both parts.
	const Vec3 dir = cut.toCorner ? p[cut.k] - v : d;
	const double f = ccw(toNext, dir);
	cut.resolves = f < std::numbers::pi - 1e-6 && inside - f < std::numbers::pi - 1e-6 && f > 1e-6;
	// Edges shorter than allowed.
	for (const Poly* part : {&pa, &pb}) {
		for (std::size_t i = 0; i < part->size(); ++i) {
			if (len2((*part)[(i + 1) % part->size()] - (*part)[i]) < o.minEdge * 0.999) cut.worst = std::min(cut.worst, 0.0);
		}
	}
	return cut;
}

} // namespace

std::vector<std::vector<Vec3>> convexParts(const std::vector<Vec3>& polygon, const PrismTidyOptions& o) {
	std::vector<Poly> out;
	Poly start;
	for (const auto& v : polygon) start.push_back({v.x, v.y, 0});
	if (area(start) < 0) std::reverse(start.begin(), start.end());
	std::vector<Poly> todo{withoutStraightCorners(std::move(start))};
	int guard = int(polygon.size()) * 8 + 64;
	while (!todo.empty()) {
		Poly p = std::move(todo.back());
		todo.pop_back();
		if (p.size() < 3) continue;
		std::vector<std::size_t> reflex;
		for (std::size_t i = 0; i < p.size(); ++i) {
			if (isReflex(p, i)) reflex.push_back(i);
		}
		if (reflex.empty() || --guard < 0) {
			out.push_back(std::move(p));
			continue;
		}
		// The best cut over all inside corners: along a hint first, then the widest angles.
		std::optional<Cut> best;
		std::size_t bestAt = 0;
		const double minAngle = o.minAngle * DEG;
		const auto better = [&](const Cut& a, const Cut& b) {
			const auto rank = [&](const Cut& c) {
				return std::tuple{c.resolves ? 1 : 0, c.worst >= minAngle ? 1 : 0, c.hint ? 1 : 0, c.worst};
			};
			return rank(a) > rank(b);
		};
		for (const std::size_t r : reflex) {
			const Vec3 v = p[r];
			const Vec3 toNext = unit2(p[(r + 1) % p.size()] - v), toPrev = unit2(p[(r + p.size() - 1) % p.size()] - v);
			std::vector<std::pair<Vec3, bool>> directions;
			for (const auto& h : o.hints) {
				const Vec3 hd = unit2(h.direction);
				if (len2(hd) < 0.5) continue;
				const Vec3 off = v - Vec3{h.point.x, h.point.y, 0};
				if (std::abs(cross2(hd, off)) < 0.5) {
					directions.push_back({hd, true});
					directions.push_back({-hd, true});
				}
			}
			const Vec3 bisector = unit2(-(toNext + toPrev));
			if (len2(bisector) > 0.5) directions.push_back({bisector, false});
			directions.push_back({-toPrev, false});   // the edge coming in, on straight
			directions.push_back({-toNext, false});   // the edge going out, backwards
			for (const auto& [d, hint] : directions) {
				auto cut = evaluate(p, r, d, hint, o);
				if (!cut) continue;
				if (!best || better(*cut, *best)) {
					best = cut;
					bestAt = r;
				}
			}
		}
		if (!best) {
			out.push_back(std::move(p));   // nothing to cut with (cannot happen for a simple polygon)
			continue;
		}
		auto [a, b] = split(p, bestAt, *best);
		todo.push_back(std::move(a));
		todo.push_back(std::move(b));
	}
	return out;
}

namespace {

/// A brush that is an upright prism: its outline from above, its top and bottom.
struct Prism {
	std::size_t index = 0;
	Poly outline;
	const Side* top = nullptr;
	const Side* bottom = nullptr;
	Plane topPlane, bottomPlane;
	std::vector<const Side*> walls;
};

std::optional<Prism> asPrism(const Solid& solid) {
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid) return std::nullopt;
	Prism p;
	const FaceGeometry* topFace = nullptr;
	for (const auto& face : geometry.faces) {
		const Side& side = solid.sides[std::size_t(face.sideIndex)];
		if (side.dispInfo()) return std::nullopt;
		if (face.normal.z > 0.05) {
			if (p.top) return std::nullopt;
			p.top = &side;
			topFace = &face;
		} else if (face.normal.z < -0.05) {
			if (p.bottom) return std::nullopt;
			p.bottom = &side;
		} else if (std::abs(face.normal.z) > 2e-3) {
			return std::nullopt;
		} else {
			p.walls.push_back(&side);
		}
	}
	if (!p.top || !p.bottom || !topFace) return std::nullopt;
	p.topPlane = p.top->plane();
	p.bottomPlane = p.bottom->plane();
	for (const auto& v : topFace->vertices) p.outline.push_back({v.x, v.y, 0});
	if (area(p.outline) < 0) std::reverse(p.outline.begin(), p.outline.end());
	return p;
}

bool samePlane(const Plane& a, const Plane& b) {
	return dot(a.normal, b.normal) > 0.999999 && std::abs(a.dist - b.dist) < 0.02;
}

bool sameAxis(const TextureAxis& a, const TextureAxis& b) {
	return dot(a.axis, b.axis) > 0.99999 && std::abs(a.shift - b.shift) < 0.01 && std::abs(a.scale - b.scale) < 1e-6;
}

bool sameKind(const Prism& a, const Prism& b) {
	return samePlane(a.topPlane, b.topPlane) && samePlane(a.bottomPlane, b.bottomPlane) && a.top->material == b.top->material &&
	       sameAxis(a.top->u, b.top->u) && sameAxis(a.top->v, b.top->v) && a.bottom->material == b.bottom->material;
}

/// The outline of pieces lying side by side: their edges, with edges two pieces share removed.
/// Empty if it is not one or more simple counter-clockwise loops (a hole, corners touching).
std::vector<Poly> outlineOf(const std::vector<Prism>& group) {
	std::vector<Vec3> points;
	const auto weld = [&](const Vec3& v) {
		for (std::size_t i = 0; i < points.size(); ++i) {
			if (std::abs(points[i].x - v.x) < WELD && std::abs(points[i].y - v.y) < WELD) return i;
		}
		points.push_back(v);
		return points.size() - 1;
	};
	std::vector<std::pair<std::size_t, std::size_t>> edges;
	for (const auto& prism : group) {
		std::vector<std::size_t> ids;
		for (const auto& v : prism.outline) {
			const std::size_t id = weld(v);
			if (ids.empty() || ids.back() != id) ids.push_back(id);
		}
		while (ids.size() > 1 && ids.front() == ids.back()) ids.pop_back();
		for (std::size_t i = 0; i < ids.size(); ++i) edges.push_back({ids[i], ids[(i + 1) % ids.size()]});
	}
	// Edges cut where another piece's corner lies on them (a T where pieces meet).
	std::vector<std::pair<std::size_t, std::size_t>> fine;
	for (const auto& [a, b] : edges) {
		const Vec3 pa = points[a], pb = points[b];
		const Vec3 d = pb - pa;
		const double l = len2(d);
		if (l < 1e-9) continue;
		std::vector<std::pair<double, std::size_t>> on;
		for (std::size_t k = 0; k < points.size(); ++k) {
			if (k == a || k == b) continue;
			const Vec3 w = points[k] - pa;
			const double t = (w.x * d.x + w.y * d.y) / (l * l);
			if (t <= 1e-9 || t >= 1 - 1e-9) continue;
			if (std::abs(cross2(d, w)) / l < WELD) on.push_back({t, k});
		}
		std::sort(on.begin(), on.end());
		std::size_t last = a;
		for (const auto& [t, k] : on) {
			fine.push_back({last, k});
			last = k;
		}
		fine.push_back({last, b});
	}
	std::map<std::pair<std::size_t, std::size_t>, int> count;
	for (const auto& e : fine) ++count[e];
	for (auto& [e, c] : count) {
		const auto back = count.find({e.second, e.first});
		if (back == count.end()) continue;
		const int both = std::min(c, back->second);
		c -= both;
		back->second -= both;
	}
	std::map<std::size_t, std::vector<std::size_t>> next;
	std::size_t left = 0;
	for (const auto& [e, c] : count) {
		for (int i = 0; i < c; ++i) {
			next[e.first].push_back(e.second);
			++left;
		}
	}
	for (const auto& [v, out] : next) {
		if (out.size() != 1) return {};   // corners touching: leave it as it is
	}
	std::vector<Poly> loops;
	std::map<std::size_t, bool> used;
	for (const auto& [start, out] : next) {
		if (used[start]) continue;
		Poly loop;
		std::size_t at = start;
		for (std::size_t steps = 0; steps <= left; ++steps) {
			if (used[at]) break;
			used[at] = true;
			loop.push_back(points[at]);
			const auto it = next.find(at);
			if (it == next.end()) return {};
			at = it->second.front();
		}
		if (at != start) return {};
		loop = withoutStraightCorners(std::move(loop));
		// Edges shorter than a quarter unit (two cuts passing a hair apart; no brush may have
		// them): the corner whose removal changes the outline least goes.
		for (bool again = true; again && loop.size() > 3;) {
			again = false;
			for (std::size_t i = 0; i < loop.size() && loop.size() > 3; ++i) {
				const std::size_t j = (i + 1) % loop.size();
				if (len2(loop[j] - loop[i]) >= 0.25) continue;
				const auto change = [&](std::size_t k) {
					const Vec3 a = loop[(k + loop.size() - 1) % loop.size()], b = loop[k], c = loop[(k + 1) % loop.size()];
					return std::abs(cross2(b - a, c - b)) / 2;
				};
				loop.erase(loop.begin() + std::ptrdiff_t(change(i) <= change(j) ? i : j));
				again = true;
				break;
			}
		}
		if (loop.size() < 3 || area(loop) <= 0) return {};   // a hole
		loops.push_back(std::move(loop));
	}
	return loops;
}

/// The convex outline of two convex polygons if together they are convex (no gap, no overlap).
std::optional<Poly> joined(const Poly& a, const Poly& b) {
	std::vector<Vec3> pts = a;
	pts.insert(pts.end(), b.begin(), b.end());
	std::sort(pts.begin(), pts.end(), [](const Vec3& p, const Vec3& q) { return p.x < q.x || (p.x == q.x && p.y < q.y); });
	Poly hull;
	for (int pass = 0; pass < 2; ++pass) {
		const std::size_t start = hull.size();
		for (const auto& p : pts) {
			while (hull.size() >= start + 2 && cross2(hull[hull.size() - 1] - hull[hull.size() - 2], p - hull[hull.size() - 2]) <= 1e-9) hull.pop_back();
			hull.push_back(p);
		}
		hull.pop_back();
		std::reverse(pts.begin(), pts.end());
	}
	const double together = area(a) + area(b);
	if (std::abs(area(hull) - together) > 0.05 + 1e-7 * together) return std::nullopt;
	return withoutStraightCorners(std::move(hull));
}

/// Length two convex polygons share along their edges.
double shared(const Poly& a, const Poly& b) {
	double total = 0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		const Vec3 p = a[i], q = a[(i + 1) % a.size()];
		const Vec3 d = unit2(q - p);
		const double l = len2(q - p);
		for (std::size_t j = 0; j < b.size(); ++j) {
			const Vec3 r = b[j], s = b[(j + 1) % b.size()];
			if (std::abs(cross2(d, r - p)) > WELD || std::abs(cross2(d, s - p)) > WELD) continue;
			if (dot(d, s - r) >= 0) continue;   // facing edges run opposite ways
			const double t0 = std::clamp(dot(d, s - p), 0.0, l), t1 = std::clamp(dot(d, r - p), 0.0, l);
			total += std::max(0.0, t1 - t0);
		}
	}
	return total;
}

void joinParts(std::vector<Poly>& parts, const PrismTidyOptions& o) {
	for (bool any = true; any && parts.size() > 1;) {
		any = false;
		double bestShared = 0;
		std::size_t bi = 0, bj = 0;
		std::optional<Poly> bestPoly;
		for (std::size_t i = 0; i < parts.size(); ++i) {
			for (std::size_t j = i + 1; j < parts.size(); ++j) {
				const double s = shared(parts[i], parts[j]);
				if (s < 0.5 || s <= bestShared) continue;
				auto u = joined(parts[i], parts[j]);
				if (!u || sharpest(*u) < std::min(o.minAngle * DEG, std::min(sharpest(parts[i]), sharpest(parts[j])))) continue;
				bestShared = s;
				bi = i;
				bj = j;
				bestPoly = std::move(u);
			}
		}
		if (bestPoly) {
			parts[bi] = std::move(*bestPoly);
			parts.erase(parts.begin() + std::ptrdiff_t(bj));
			any = true;
		}
	}
}

double heightOn(const Plane& plane, double x, double y) {
	return (plane.dist - plane.normal.x * x - plane.normal.y * y) / plane.normal.z;
}

/// Whether edge a-b lies on the outline (else it is a cut between two new pieces).
bool onOutline(const Vec3& a, const Vec3& b, const std::vector<Poly>& loops) {
	const Vec3 m = (a + b) * 0.5;
	const Vec3 d = unit2(b - a);
	for (const auto& loop : loops) {
		for (std::size_t i = 0; i < loop.size(); ++i) {
			const Vec3 p = loop[i], q = loop[(i + 1) % loop.size()];
			const Vec3 e = unit2(q - p);
			if (std::abs(cross2(e, d)) > 1e-3) continue;
			if (std::abs(cross2(e, m - p)) > 0.05) continue;
			const double t = dot(e, m - p);
			if (t > -0.05 && t < len2(q - p) + 0.05) return true;
		}
	}
	return false;
}

std::optional<Solid> rebuild(MapDocument& ids, const Poly& part, const Prism& model, const std::vector<Prism>& group, const std::vector<Poly>& loops) {
	Solid solid;
	solid.id = ids.newSolidId();
	solid.sides.push_back(makeSide(ids, model.topPlane, model.top->material, model.top));
	solid.sides.push_back(makeSide(ids, model.bottomPlane, model.bottom->material, model.bottom));
	for (std::size_t i = 0; i < part.size(); ++i) {
		const Vec3 a = part[i], b = part[(i + 1) % part.size()];
		const Vec3 n = unit2(Vec3{b.y - a.y, a.x - b.x, 0});
		const Plane plane{n, n.x * a.x + n.y * a.y};
		// On the outline: the look of the wall it lies on (the closest one); a cut between two
		// new pieces: nodraw (nothing sees it).
		const Side* like = nullptr;
		if (onOutline(a, b, loops)) {
			double best = 1e30;
			const Vec3 m = (a + b) * 0.5;
			for (const auto& prism : group) {
				for (const Side* wall : prism.walls) {
					const Plane wp = wall->plane();
					if (dot(wp.normal, plane.normal) < 0.99) continue;
					const double off = std::abs(wp.distanceTo(Vec3{m.x, m.y, heightOn(model.topPlane, m.x, m.y)})) + (1 - dot(wp.normal, plane.normal)) * 1000;
					if (off < best) {
						best = off;
						like = wall;
					}
				}
			}
			if (!like || best > 0.5) like = nullptr;
			solid.sides.push_back(like ? makeSide(ids, plane, like->material, like) : makeSide(ids, plane, model.top->material));
		} else {
			solid.sides.push_back(makeSide(ids, plane, NODRAW_MATERIAL));
		}
	}
	tidyPlanePoints(solid);
	const auto geometry = buildBrushGeometry(solid);
	if (!geometry.valid || geometry.faces.size() != solid.sides.size() || !isConvexValid(solid)) return std::nullopt;
	// The brush is the part: no wider than it, and every corner of the part is a corner of the brush.
	Bounds partBox;
	for (const auto& v : part) partBox.add(v);
	if (geometry.bounds.min.x < partBox.min.x - 0.1 || geometry.bounds.min.y < partBox.min.y - 0.1 || geometry.bounds.max.x > partBox.max.x + 0.1 ||
	    geometry.bounds.max.y > partBox.max.y + 0.1) {
		return std::nullopt;
	}
	for (const auto& v : part) {
		const Vec3 p{v.x, v.y, heightOn(model.topPlane, v.x, v.y)};
		bool found = false;
		for (const auto& face : geometry.faces) {
			for (const auto& w : face.vertices) found = found || length(w - p) < 0.1;
		}
		if (!found) return std::nullopt;
	}
	return solid;
}

} // namespace

std::vector<Solid> tidyPrisms(MapDocument& ids, const std::vector<Solid>& pieces, const PrismTidyOptions& options) {
	std::vector<Solid> out;
	std::vector<Prism> prisms;
	for (std::size_t i = 0; i < pieces.size(); ++i) {
		auto p = asPrism(pieces[i]);
		if (!p) {
			out.push_back(pieces[i]);
			continue;
		}
		p->index = i;
		prisms.push_back(std::move(*p));
	}
	std::vector<bool> taken(prisms.size(), false);
	for (std::size_t i = 0; i < prisms.size(); ++i) {
		if (taken[i]) continue;
		std::vector<Prism> group{prisms[i]};
		taken[i] = true;
		for (std::size_t j = i + 1; j < prisms.size(); ++j) {
			if (!taken[j] && sameKind(prisms[i], prisms[j])) {
				group.push_back(prisms[j]);
				taken[j] = true;
			}
		}
		const auto keep = [&] {
			for (const auto& prism : group) out.push_back(pieces[prism.index]);
		};
		const auto loops = outlineOf(group);
		if (loops.empty()) {
			keep();
			continue;
		}
		std::vector<Poly> parts;
		for (const auto& loop : loops) {
			for (auto& part : convexParts(loop, options)) parts.push_back(std::move(part));
		}
		if (options.join) joinParts(parts, options);
		std::vector<Solid> made;
		bool fine = true;
		for (const auto& part : parts) {
			auto solid = rebuild(ids, part, group.front(), group, loops);
			if (!solid) {
				fine = false;
				break;
			}
			solid->extra = pieces[group.front().index].extra;
			made.push_back(std::move(*solid));
		}
		if (!fine) {
			keep();
			continue;
		}
		for (auto& m : made) out.push_back(std::move(m));
	}
	return out;
}

} // namespace sme
