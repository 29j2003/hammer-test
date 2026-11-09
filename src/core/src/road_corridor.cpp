// road_corridor.cpp - The street selected floor brushes make and the road through it (see
// road_builder.h, roadCorridorFromSolids).
//
// How the middle of the street is found:
// 1. The selected brushes' tops at the floor's height are drawn into a raster (cells of 4 to 64
//    units, about 512 across the street), minus what stands on them (a sidewalk, a wall).
// 2. A distance field (exact Euclidean, Felzenszwalb & Huttenlocher) gives each cell its distance
//    to the street's edge; thinning (Zhang & Suen) leaves the cells along the middle.
// 3. The main road is the longest way through those cells (between two of their ends); a side
//    street is a branch that reaches clearly beyond the main street's kerb (a short spur into a
//    corner is not). Each becomes a polyline (Douglas-Peucker), a short piece between two long
//    ones (a rounded corner) becomes one corner.
// 4. Exact again: every straight part is moved to the middle between the kerbs, measured with rays
//    against the brushes themselves (not the raster); its ends reach the street's end exactly; a
//    side street starts on the main road's centre line.
// 5. Each bend gets the largest curve whose edges keep the parking lane clear of the kerbs (round
//    a sharp corner: the lane's width; round a rounded sidewalk: the same middle as its rounding).

#include <sme/road_builder.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/edge_round.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <sstream>

namespace sme {

namespace {

constexpr double PI = std::numbers::pi;

Vec3 flat(const Vec3& v) {
	return {v.x, v.y, 0};
}

Vec3 leftOf(const Vec3& d) {
	return normalize(Vec3{-d.y, d.x, 0});
}

Vec3 turned(const Vec3& v, double angle) {
	const double c = std::cos(angle), s = std::sin(angle);
	return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

double cross2(const Vec3& a, const Vec3& b) {
	return a.x * b.y - a.y * b.x;
}

std::string units(double v) {
	std::ostringstream out;
	out << std::lround(v);
	return out.str();
}

bool toolMaterial(const std::string& material) {
	std::string m = material;
	for (auto& c : m) c = char(std::tolower(static_cast<unsigned char>(c)));
	return m.rfind("tools/", 0) == 0;
}

/// A selected brush's top seen from above.
struct FloorTop {
	SolidRef ref;
	std::vector<Vec3> poly;   ///< Corners of the top face, counter-clockwise from above
	Plane plane;
	double z = 0;             ///< Height at its middle
	double bottom = 0;
	double area = 0;
	std::string material;
	Bounds box;
	[[nodiscard]] double zAt(double x, double y) const {
		return std::abs(plane.normal.z) > 1e-6 ? (plane.dist - plane.normal.x * x - plane.normal.y * y) / plane.normal.z : z;
	}
	[[nodiscard]] bool contains(double x, double y, double eps = 0.01) const {
		for (std::size_t i = 0; i < poly.size(); ++i) {
			const Vec3 a = poly[i], b = poly[(i + 1) % poly.size()];
			const Vec3 e = flat(b - a);
			const double len = length(e);
			if (len < 1e-9) continue;
			if (cross2(e, Vec3{x - a.x, y - a.y, 0}) / len < -eps) return false;
		}
		return true;
	}
};

/// The raster of the street.
struct Grid {
	double x0 = 0, y0 = 0, cell = 8;
	int nx = 0, ny = 0;
	std::vector<std::uint8_t> in;   ///< Street cells
	std::vector<float> signedDist;  ///< Units from the street's edge (inside +, outside -)
	[[nodiscard]] int index(int i, int j) const { return j * nx + i; }
	[[nodiscard]] bool valid(int i, int j) const { return i >= 0 && j >= 0 && i < nx && j < ny; }
	[[nodiscard]] Vec3 centre(int i, int j) const { return {x0 + (i + 0.5) * cell, y0 + (j + 0.5) * cell, 0}; }
	[[nodiscard]] bool inside(int i, int j) const { return valid(i, j) && in[std::size_t(index(i, j))] != 0; }
	/// Distance from the edge at a point (bilinear between cell centres; far outside: very negative).
	[[nodiscard]] double distAt(double x, double y) const {
		const double fx = (x - x0) / cell - 0.5, fy = (y - y0) / cell - 0.5;
		const int i = int(std::floor(fx)), j = int(std::floor(fy));
		if (i < 0 || j < 0 || i + 1 >= nx || j + 1 >= ny) return -1e9;
		const double tx = fx - i, ty = fy - j;
		const auto d = [&](int a, int b) { return double(signedDist[std::size_t(index(a, b))]); };
		return (d(i, j) * (1 - tx) + d(i + 1, j) * tx) * (1 - ty) + (d(i, j + 1) * (1 - tx) + d(i + 1, j + 1) * tx) * ty;
	}
};

/// Squared distance transform along one line (Felzenszwalb & Huttenlocher): d[q] = min over p of
/// (q - p)^2 + f[p].
void distance1d(const std::vector<double>& f, std::vector<double>& d) {
	const int n = int(f.size());
	d.assign(std::size_t(n), 0);
	if (n == 0) return;
	std::vector<int> v(std::size_t(n), 0);
	std::vector<double> z(std::size_t(n) + 1, 0);
	const double inf = std::numeric_limits<double>::infinity();
	int k = 0;
	z[0] = -inf;
	z[1] = inf;
	const auto meet = [&](int q, int p) { return ((f[std::size_t(q)] + double(q) * q) - (f[std::size_t(p)] + double(p) * p)) / (2.0 * q - 2.0 * p); };
	for (int q = 1; q < n; ++q) {
		double s = meet(q, v[std::size_t(k)]);
		while (s <= z[std::size_t(k)]) {
			--k;
			s = meet(q, v[std::size_t(k)]);
		}
		++k;
		v[std::size_t(k)] = q;
		z[std::size_t(k)] = s;
		z[std::size_t(k) + 1] = inf;
	}
	k = 0;
	for (int q = 0; q < n; ++q) {
		while (z[std::size_t(k) + 1] < q) ++k;
		const int p = v[std::size_t(k)];
		d[std::size_t(q)] = double(q - p) * (q - p) + f[std::size_t(p)];
	}
}

/// Squared distance (in cells) of every cell to the nearest cell where `source` is set.
std::vector<double> distanceField(const Grid& g, const std::vector<std::uint8_t>& source) {
	const double huge = 1e20;
	std::vector<double> out(source.size(), huge);
	std::vector<double> f, d;
	for (int j = 0; j < g.ny; ++j) {
		f.assign(std::size_t(g.nx), huge);
		for (int i = 0; i < g.nx; ++i) f[std::size_t(i)] = source[std::size_t(g.index(i, j))] ? 0 : huge;
		distance1d(f, d);
		for (int i = 0; i < g.nx; ++i) out[std::size_t(g.index(i, j))] = d[std::size_t(i)];
	}
	for (int i = 0; i < g.nx; ++i) {
		f.assign(std::size_t(g.ny), huge);
		for (int j = 0; j < g.ny; ++j) f[std::size_t(j)] = out[std::size_t(g.index(i, j))];
		distance1d(f, d);
		for (int j = 0; j < g.ny; ++j) out[std::size_t(g.index(i, j))] = d[std::size_t(j)];
	}
	return out;
}

/// Zhang-Suen thinning: the street's cells down to a line one cell wide along its middle.
std::vector<std::uint8_t> thin(const Grid& g) {
	std::vector<std::uint8_t> s = g.in;
	const auto at = [&](int i, int j) -> int { return g.valid(i, j) && s[std::size_t(g.index(i, j))] ? 1 : 0; };
	std::vector<std::size_t> remove;
	for (bool changed = true; changed;) {
		changed = false;
		for (int pass = 0; pass < 2; ++pass) {
			remove.clear();
			for (int j = 1; j + 1 < g.ny; ++j) {
				for (int i = 1; i + 1 < g.nx; ++i) {
					if (!s[std::size_t(g.index(i, j))]) continue;
					// Neighbours p2..p9 clockwise from north.
					const int p[8] = {at(i, j + 1), at(i + 1, j + 1), at(i + 1, j), at(i + 1, j - 1), at(i, j - 1), at(i - 1, j - 1), at(i - 1, j), at(i - 1, j + 1)};
					int b = 0, a = 0;
					for (int k = 0; k < 8; ++k) {
						b += p[k];
						if (p[k] == 0 && p[(k + 1) % 8] == 1) ++a;
					}
					if (b < 2 || b > 6 || a != 1) continue;
					if (pass == 0) {
						if (p[0] * p[2] * p[4] != 0 || p[2] * p[4] * p[6] != 0) continue;
					} else {
						if (p[0] * p[2] * p[6] != 0 || p[0] * p[4] * p[6] != 0) continue;
					}
					remove.push_back(std::size_t(g.index(i, j)));
				}
			}
			for (const auto k : remove) s[k] = 0;
			if (!remove.empty()) changed = true;
		}
	}
	return s;
}

/// Shortest ways through the skeleton's cells (8 neighbours) from a set of start cells.
struct Walk {
	std::vector<double> dist;
	std::vector<int> from;
};
Walk walkFrom(const Grid& g, const std::vector<std::uint8_t>& skel, const std::vector<int>& starts) {
	Walk w;
	w.dist.assign(skel.size(), std::numeric_limits<double>::infinity());
	w.from.assign(skel.size(), -1);
	using Item = std::pair<double, int>;
	std::vector<Item> heap;
	const auto push = [&](double d, int k) {
		heap.push_back({-d, k});
		std::push_heap(heap.begin(), heap.end());
	};
	for (const int k : starts) {
		w.dist[std::size_t(k)] = 0;
		push(0, k);
	}
	while (!heap.empty()) {
		std::pop_heap(heap.begin(), heap.end());
		const auto [negD, k] = heap.back();
		heap.pop_back();
		const double d = -negD;
		if (d > w.dist[std::size_t(k)]) continue;
		const int i = k % g.nx, j = k / g.nx;
		for (int dj = -1; dj <= 1; ++dj) {
			for (int di = -1; di <= 1; ++di) {
				if (di == 0 && dj == 0) continue;
				const int a = i + di, b = j + dj;
				if (!g.valid(a, b)) continue;
				const int n = g.index(a, b);
				if (!skel[std::size_t(n)]) continue;
				const double nd = d + ((di != 0 && dj != 0) ? std::numbers::sqrt2 : 1.0);
				if (nd < w.dist[std::size_t(n)]) {
					w.dist[std::size_t(n)] = nd;
					w.from[std::size_t(n)] = k;
					push(nd, n);
				}
			}
		}
	}
	return w;
}

/// Douglas-Peucker simplification of a polyline (horizontal distances).
std::vector<Vec3> simplify(const std::vector<Vec3>& points, double tolerance) {
	if (points.size() < 3) return points;
	std::vector<bool> keep(points.size(), false);
	keep.front() = keep.back() = true;
	std::vector<std::pair<std::size_t, std::size_t>> stack{{0, points.size() - 1}};
	while (!stack.empty()) {
		const auto [a, b] = stack.back();
		stack.pop_back();
		const Vec3 d = flat(points[b] - points[a]);
		const double len = length(d);
		double worst = -1;
		std::size_t at = a;
		for (std::size_t i = a + 1; i < b; ++i) {
			const Vec3 v = flat(points[i] - points[a]);
			const double off = len > 1e-9 ? std::abs(cross2(d, v)) / len : length(v);
			if (off > worst) {
				worst = off;
				at = i;
			}
		}
		if (worst > tolerance) {
			keep[at] = true;
			stack.push_back({a, at});
			stack.push_back({at, b});
		}
	}
	std::vector<Vec3> out;
	for (std::size_t i = 0; i < points.size(); ++i) {
		if (keep[i]) out.push_back(points[i]);
	}
	return out;
}

/// Where the lines through a (direction da) and b (direction db) cross (horizontally).
std::optional<Vec3> crossing(const Vec3& a, const Vec3& da, const Vec3& b, const Vec3& db) {
	const double det = cross2(da, db);
	if (std::abs(det) < 1e-6) return std::nullopt;
	const double t = cross2(flat(b - a), db) / det;
	return a + da * t;
}

double median(std::vector<double> v) {
	if (v.empty()) return 0;
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

/// Everything the finder works with.
struct Street {
	std::vector<FloorTop> tops;
	std::vector<const Solid*> obstacles;   ///< Brushes standing on the floor
	double top = 0;
	Grid grid;

	/// Whether a point is on the street (exactly: the brushes, not the raster).
	[[nodiscard]] bool on(double x, double y) const {
		bool any = false;
		for (const auto& t : tops) {
			if (x < t.box.min.x - 0.01 || x > t.box.max.x + 0.01 || y < t.box.min.y - 0.01 || y > t.box.max.y + 0.01) continue;
			if (t.contains(x, y, 1e-4)) {
				any = true;
				break;
			}
		}
		if (!any) return false;
		const Vec3 p{x, y, top + 1};
		for (const Solid* o : obstacles) {
			if (solidContains(*o, p, -0.01)) return false;
		}
		return true;
	}
	/// How far from `p` along `d` the street goes on (0 if `p` is not on it).
	[[nodiscard]] double reach(const Vec3& p, const Vec3& d, double limit) const {
		if (!on(p.x, p.y)) return 0;
		const double step = grid.cell / 2;
		double t = 0;
		for (;;) {
			const double next = t + step;
			if (next > limit) return limit;
			const Vec3 q = p + d * next;
			if (!on(q.x, q.y)) {
				double lo = t, hi = next;
				for (int k = 0; k < 24; ++k) {
					const double mid = (lo + hi) / 2;
					const Vec3 m = p + d * mid;
					(on(m.x, m.y) ? lo : hi) = mid;
				}
				return lo;
			}
			t = next;
		}
	}
	/// The floor's height at a point (the top it lies in, else the nearest).
	[[nodiscard]] double zAt(double x, double y) const {
		const FloorTop* best = nullptr;
		double bestD = 1e30;
		for (const auto& t : tops) {
			if (t.contains(x, y, 0.5)) return t.zAt(x, y);
			const double d = std::hypot(std::clamp(x, t.box.min.x, t.box.max.x) - x, std::clamp(y, t.box.min.y, t.box.max.y) - y);
			if (d < bestD) {
				bestD = d;
				best = &t;
			}
		}
		return best ? best->zAt(x, y) : top;
	}
};

/// A path being shaped: points and, per straight part, whether it was centred and how wide the
/// street is there.
struct Line {
	std::vector<Vec3> points;
	std::vector<double> widths;   ///< Per part (between point i and i + 1); 0: not measured
	bool branch = false;
};

/// Removes points where the line hardly turns.
void dropStraightPoints(Line& line) {
	for (bool again = true; again && line.points.size() > 2;) {
		again = false;
		for (std::size_t i = 1; i + 1 < line.points.size(); ++i) {
			const Vec3 d0 = normalize(flat(line.points[i] - line.points[i - 1])), d1 = normalize(flat(line.points[i + 1] - line.points[i]));
			if (std::abs(cross2(d0, d1)) < std::sin(2.0 * PI / 180) && dot(d0, d1) > 0) {
				line.points.erase(line.points.begin() + std::ptrdiff_t(i));
				const double w = std::max(line.widths[i - 1], line.widths[i]);
				line.widths.erase(line.widths.begin() + std::ptrdiff_t(i));
				line.widths[i - 1] = w;
				again = true;
				break;
			}
		}
	}
}

/// A short part between two longer ones that turn the same way (a rounded corner the raster
/// drew as two bends) becomes one corner where the long parts' lines cross.
void mergeShortBends(Line& line, double street) {
	for (bool again = true; again && line.points.size() > 3;) {
		again = false;
		for (std::size_t i = 1; i + 2 < line.points.size(); ++i) {
			const Vec3 a = line.points[i - 1], b = line.points[i], c = line.points[i + 1], d = line.points[i + 2];
			const double shortLen = length(flat(c - b));
			if (shortLen > 0.75 * street || shortLen > length(flat(b - a)) || shortLen > length(flat(d - c))) continue;
			const Vec3 d0 = normalize(flat(b - a)), d1 = normalize(flat(c - b)), d2 = normalize(flat(d - c));
			if (cross2(d0, d1) * cross2(d1, d2) <= 0) continue;   // an S, not one bend
			const auto corner = crossing(b, d0, c, d2);
			if (!corner || length(flat(*corner - (b + c) * 0.5)) > street) continue;
			line.points[i] = Vec3{corner->x, corner->y, b.z};
			line.points.erase(line.points.begin() + std::ptrdiff_t(i + 1));
			line.widths.erase(line.widths.begin() + std::ptrdiff_t(i));
			again = true;
			break;
		}
	}
}

/// Short pieces between two long straight parts (the middle of a wide spot, a jog) become one
/// bend where the long parts' lines cross, or, for parallel parts (a jog), one straight piece from
/// the end of the first to the start of the second (one S), pulled back a little along both so the
/// S is not tighter than it has to be.
void joinShortRuns(Line& line, double street) {
	const auto len = [&](std::size_t k) { return length(flat(line.points[k + 1] - line.points[k])); };
	for (bool again = true; again && line.points.size() > 3;) {
		again = false;
		const std::size_t parts = line.points.size() - 1;
		for (std::size_t i = 0; i + 2 < parts && !again; ++i) {
			const double wi = line.widths[i] > 0 ? line.widths[i] : street;
			if (len(i) < 0.75 * wi) continue;
			std::size_t j = i + 1;
			while (j < parts && len(j) < 0.75 * (line.widths[j] > 0 ? line.widths[j] : street)) ++j;
			if (j >= parts || j == i + 1) continue;
			const Vec3 a0 = line.points[i], a1 = line.points[i + 1], b0 = line.points[j], b1 = line.points[j + 1];
			const Vec3 da = normalize(flat(a1 - a0)), db = normalize(flat(b1 - b0));
			std::vector<Vec3> middle;
			if (std::abs(cross2(da, db)) > std::sin(10 * PI / 180)) {
				const auto corner = crossing(a0, da, b0, db);
				if (!corner || length(flat(*corner - (a1 + b0) * 0.5)) > 1.5 * street) continue;
				middle.push_back(Vec3{corner->x, corner->y, a1.z});
			} else {
				const double pullA = std::min(street / 2, len(i) / 3), pullB = std::min(street / 2, len(j) / 3);
				middle.push_back(a1 - da * pullA);
				middle.push_back(b0 + db * pullB);
			}
			std::vector<Vec3> points(line.points.begin(), line.points.begin() + std::ptrdiff_t(i) + 1);
			std::vector<double> widths(line.widths.begin(), line.widths.begin() + std::ptrdiff_t(i) + 1);
			for (const auto& m : middle) points.push_back(m);
			if (middle.size() == 2) widths.push_back(0);   // the diagonal: not measured
			points.insert(points.end(), line.points.begin() + std::ptrdiff_t(j) + 1, line.points.end());
			widths.insert(widths.end(), line.widths.begin() + std::ptrdiff_t(j), line.widths.end());
			line.points = std::move(points);
			line.widths = std::move(widths);
			again = true;
		}
	}
}

/// Moves every long straight part to the middle between the kerbs (rays against the brushes) and
/// recomputes the corners where the moved parts' lines cross. Returns nothing; fills `widths`.
void centre(Line& line, const Street& street, double streetWidth) {
	const std::size_t n = line.points.size();
	if (n < 2) return;
	std::vector<Vec3> base(n - 1), dir(n - 1);
	std::vector<bool> moved(n - 1, false);
	line.widths.assign(n - 1, 0);
	for (std::size_t k = 0; k + 1 < n; ++k) {
		const Vec3 a = line.points[k], b = line.points[k + 1];
		dir[k] = normalize(flat(b - a));
		base[k] = a;
		const double len = length(flat(b - a));
		const bool open0 = k == 0 && !line.branch, open1 = k + 2 == n;
		// Only parts long enough to have kerbs on both sides (ends of the street count: they run on).
		// A branch's first part starts on the main road: past the main street's kerb only.
		const double inner0 = open0 ? 0 : (k == 0 && line.branch ? streetWidth : streetWidth / 2), inner1 = open1 ? 0 : streetWidth / 2;
		if (len - inner0 - inner1 < std::max(street.grid.cell * 2, 0.25 * streetWidth)) continue;
		const Vec3 left = leftOf(dir[k]);
		std::vector<Vec3> middles;
		std::vector<double> widths;
		for (const double f : {0.1, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0}) {
			const Vec3 p = a + dir[k] * (inner0 + (len - inner0 - inner1) * std::min(f, 0.97));
			const double l = street.reach(p, left, 4 * streetWidth + 512), r = street.reach(p, -left, 4 * streetWidth + 512);
			if (l <= 0 && r <= 0) continue;
			middles.push_back(p + left * ((l - r) / 2));
			widths.push_back(l + r);
		}
		if (middles.empty()) continue;
		// Only the middles where the street has its usual width (not where a side street opens).
		const double usual = median(widths);
		std::vector<Vec3> kept;
		for (std::size_t m = 0; m < middles.size(); ++m) {
			if (std::abs(widths[m] - usual) < std::max(8.0, 0.08 * usual)) kept.push_back(middles[m]);
		}
		if (kept.empty()) kept = middles;
		Vec3 mean{};
		for (const auto& m : kept) mean = mean + m;
		mean = mean / double(kept.size());
		Vec3 d = dir[k];
		if (kept.size() >= 2) {
			// The direction through the middles (principal axis), unless it hardly differs.
			double sxx = 0, sxy = 0, syy = 0;
			for (const auto& m : kept) {
				const Vec3 v = flat(m - mean);
				sxx += v.x * v.x;
				sxy += v.x * v.y;
				syy += v.y * v.y;
			}
			const double angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
			Vec3 fit{std::cos(angle), std::sin(angle), 0};
			if (dot(fit, d) < 0) fit = -fit;
			if (sxx + syy > 1 && dot(fit, d) > std::cos(10 * PI / 180)) d = fit;
		}
		// Streets along the grid stay exactly along it.
		for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, -1, 0}}) {
			if (dot(d, axis) > std::cos(1.5 * PI / 180)) d = axis;
		}
		// Along the grid: the median middle (a rounded kerb near an end does not pull it aside).
		if (std::abs(d.x) == 1 || std::abs(d.y) == 1) {
			std::vector<double> across;
			for (const auto& m : kept) across.push_back(std::abs(d.x) == 1 ? m.y : m.x);
			(std::abs(d.x) == 1 ? mean.y : mean.x) = median(across);
		}
		dir[k] = d;
		base[k] = mean;
		line.widths[k] = usual;
		moved[k] = true;
	}
	std::vector<Vec3> out(n);
	const auto onLine = [&](std::size_t k, const Vec3& p) { return base[k] + dir[k] * dot(flat(p - base[k]), dir[k]); };
	out.front() = onLine(0, line.points.front());
	out.back() = onLine(n - 2, line.points.back());
	for (std::size_t i = 1; i + 1 < n; ++i) {
		const auto corner = crossing(base[i - 1], dir[i - 1], base[i], dir[i]);
		if (corner && length(flat(*corner - line.points[i])) < 1.5 * streetWidth) {
			out[i] = *corner;
		} else if (moved[i - 1] || moved[i]) {
			out[i] = moved[i - 1] ? onLine(i - 1, line.points[i]) : onLine(i, line.points[i]);
		} else {
			out[i] = line.points[i];
		}
	}
	for (std::size_t i = 0; i < n; ++i) out[i].z = line.points[i].z;
	line.points = std::move(out);
}

} // namespace

RoadCorridor roadCorridorFromSolids(const MapDocument& doc, const std::vector<SolidRef>& solids, const RoadBuildOptions& options) {
	RoadCorridor corridor;
	Street street;
	// 1. The tops, grouped by height: the street is the height most of the area has.
	std::vector<FloorTop> tops;
	for (const auto& ref : solids) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		const FaceGeometry* top = nullptr;
		if (geometry.valid) {
			for (const auto& face : geometry.faces) {
				if (face.normal.z > 0.7 && (!top || face.normal.z > top->normal.z)) top = &face;
			}
		}
		const Side* side = top ? &solid->sides[std::size_t(top->sideIndex)] : nullptr;
		if (!top || top->vertices.size() < 3 || toolMaterial(side->material)) {
			corridor.ignored.push_back(ref);
			continue;
		}
		FloorTop t;
		t.ref = ref;
		t.poly = top->vertices;
		// Counter-clockwise from above.
		double area2 = 0;
		for (std::size_t i = 0; i < t.poly.size(); ++i) area2 += cross2(t.poly[i], t.poly[(i + 1) % t.poly.size()]);
		if (area2 < 0) std::reverse(t.poly.begin(), t.poly.end());
		t.area = std::abs(area2) / 2;
		t.plane = side->plane();
		Vec3 c{};
		for (const auto& v : t.poly) {
			c = c + v;
			t.box.add(v);
		}
		c = c / double(t.poly.size());
		t.z = t.zAt(c.x, c.y);
		t.bottom = geometry.bounds.min.z;
		t.material = side->material;
		tops.push_back(std::move(t));
	}
	if (tops.empty()) return corridor;
	{
		std::map<long, double> areaAt;   // 4-unit height bands
		for (const auto& t : tops) areaAt[std::lround(t.z / 4)] += t.area;
		long band = 0;
		double most = -1;
		for (const auto& [b, area] : areaAt) {
			// Neighbouring bands count together (a top at 7.9 and one at 8.1).
			const double around = area + (areaAt.contains(b - 1) ? areaAt[b - 1] : 0) + (areaAt.contains(b + 1) ? areaAt[b + 1] : 0);
			if (around > most) {
				most = around;
				band = b;
			}
		}
		const double z = double(band) * 4;
		int other = 0;
		for (auto& t : tops) {
			if (std::abs(t.z - z) <= 6) {
				street.tops.push_back(t);
			} else {
				corridor.ignored.push_back(t.ref);
				++other;
			}
		}
		if (other > 0) corridor.notes.push_back(std::to_string(other) + " selected brush(es) at another height stay as they are (a sidewalk, a step)");
	}
	{
		std::vector<double> zs, thick;
		std::map<std::string, double> materials;
		for (const auto& t : street.tops) {
			zs.push_back(t.z);
			thick.push_back(t.z - t.bottom);
			materials[t.material] += t.area;
			corridor.floor.push_back(t.ref);
			corridor.area += t.area;
		}
		street.top = median(zs);
		corridor.top = street.top;
		corridor.thickness = std::max(2.0, std::round(median(thick)));
		double most = -1;
		for (const auto& [m, a] : materials) {
			if (a > most) {
				most = a;
				corridor.material = m;
			}
		}
	}
	Bounds box;
	for (const auto& t : street.tops) {
		box.add(t.box.min);
		box.add(t.box.max);
	}
	// What stands on the floor (a sidewalk, a wall): not street.
	{
		Bounds around = box;
		around.min.z = street.top - 1;
		around.max.z = street.top + 2;
		std::vector<SolidRef> skip = corridor.floor;
		for (const auto& n : solidsNear(doc, around, 0, skip)) {
			if (n.bounds.min.z >= street.top + 1 || n.bounds.max.z <= street.top + 1) continue;
			if (n.ref.entityId != 0) {
				const Entity* e = doc.findEntity(n.ref.entityId);
				const std::string cls = e ? e->classname() : std::string();
				if (cls != "func_detail" && cls != "func_brush" && cls != "func_wall") continue;
			}
			if (std::all_of(n.solid->sides.begin(), n.solid->sides.end(), [](const Side& s) { return toolMaterial(s.material); })) continue;
			street.obstacles.push_back(n.solid);
		}
	}

	// 2. The raster.
	Grid& g = street.grid;
	const double extent = std::max(box.max.x - box.min.x, box.max.y - box.min.y);
	g.cell = 4;
	while (g.cell < 64 && extent / g.cell > 600) g.cell *= 2;
	g.x0 = box.min.x - 3 * g.cell;
	g.y0 = box.min.y - 3 * g.cell;
	g.nx = int(std::ceil((box.max.x - g.x0) / g.cell)) + 3;
	g.ny = int(std::ceil((box.max.y - g.y0) / g.cell)) + 3;
	corridor.cell = g.cell;
	g.in.assign(std::size_t(g.nx) * std::size_t(g.ny), 0);
	const auto cellRange = [&](const Bounds& b, int& i0, int& i1, int& j0, int& j1) {
		i0 = std::max(0, int(std::floor((b.min.x - g.x0) / g.cell)));
		i1 = std::min(g.nx - 1, int(std::ceil((b.max.x - g.x0) / g.cell)));
		j0 = std::max(0, int(std::floor((b.min.y - g.y0) / g.cell)));
		j1 = std::min(g.ny - 1, int(std::ceil((b.max.y - g.y0) / g.cell)));
	};
	for (const auto& t : street.tops) {
		int i0, i1, j0, j1;
		cellRange(t.box, i0, i1, j0, j1);
		for (int j = j0; j <= j1; ++j) {
			for (int i = i0; i <= i1; ++i) {
				const Vec3 c = g.centre(i, j);
				if (t.contains(c.x, c.y, 1e-6)) g.in[std::size_t(g.index(i, j))] = 1;
			}
		}
	}
	for (const Solid* o : street.obstacles) {
		const Bounds b = buildBrushGeometry(*o).bounds;
		int i0, i1, j0, j1;
		cellRange(b, i0, i1, j0, j1);
		for (int j = j0; j <= j1; ++j) {
			for (int i = i0; i <= i1; ++i) {
				const Vec3 c = g.centre(i, j);
				if (g.in[std::size_t(g.index(i, j))] && solidContains(*o, {c.x, c.y, street.top + 1}, -0.01)) g.in[std::size_t(g.index(i, j))] = 0;
			}
		}
	}
	{
		std::vector<std::uint8_t> outside(g.in.size()), inside(g.in.size());
		for (std::size_t k = 0; k < g.in.size(); ++k) {
			outside[k] = g.in[k] ? 0 : 1;
			inside[k] = g.in[k];
		}
		const auto toOut = distanceField(g, outside), toIn = distanceField(g, inside);
		g.signedDist.resize(g.in.size());
		for (std::size_t k = 0; k < g.in.size(); ++k) {
			g.signedDist[k] = g.in[k] ? float((std::sqrt(toOut[k]) - 0.5) * g.cell) : float(-(std::sqrt(toIn[k]) - 0.5) * g.cell);
		}
	}

	// 3. The middle: thinned cells; the main road the longest way through them, branches after.
	std::vector<std::uint8_t> skel = thin(g);
	std::vector<int> cells;
	for (std::size_t k = 0; k < skel.size(); ++k) {
		if (skel[k]) cells.push_back(int(k));
	}
	if (cells.empty()) {
		corridor.notes.push_back("the selected floor is too small for a road");
		return corridor;
	}
	// The biggest connected piece only.
	{
		std::vector<int> label(skel.size(), -1);
		int best = -1;
		std::size_t bestSize = 0;
		int next = 0;
		for (const int start : cells) {
			if (label[std::size_t(start)] >= 0) continue;
			std::size_t size = 0;
			std::deque<int> queue{start};
			label[std::size_t(start)] = next;
			while (!queue.empty()) {
				const int k = queue.front();
				queue.pop_front();
				++size;
				const int i = k % g.nx, j = k / g.nx;
				for (int dj = -1; dj <= 1; ++dj) {
					for (int di = -1; di <= 1; ++di) {
						if (!g.valid(i + di, j + dj)) continue;
						const int nk = g.index(i + di, j + dj);
						if (skel[std::size_t(nk)] && label[std::size_t(nk)] < 0) {
							label[std::size_t(nk)] = next;
							queue.push_back(nk);
						}
					}
				}
			}
			if (size > bestSize) {
				bestSize = size;
				best = next;
			}
			++next;
		}
		if (next > 1) corridor.notes.push_back("the selected floor has parts that do not touch: the road goes through the biggest one");
		for (std::size_t k = 0; k < skel.size(); ++k) {
			if (skel[k] && label[k] != best) skel[k] = 0;
		}
		std::erase_if(cells, [&](int k) { return !skel[std::size_t(k)]; });
	}
	const auto pathTo = [&](const Walk& w, int end) {
		std::vector<int> out;
		for (int k = end; k >= 0; k = w.from[std::size_t(k)]) out.push_back(k);
		std::reverse(out.begin(), out.end());
		return out;
	};
	const auto farthest = [&](const Walk& w) {
		int best = cells.front();
		for (const int k : cells) {
			if (std::isfinite(w.dist[std::size_t(k)]) && w.dist[std::size_t(k)] > w.dist[std::size_t(best)]) best = k;
		}
		return best;
	};
	const int a = farthest(walkFrom(g, skel, {cells.front()}));
	const Walk fromA = walkFrom(g, skel, {a});
	const int b = farthest(fromA);
	std::vector<std::vector<int>> chains{pathTo(fromA, b)};
	std::vector<std::uint8_t> taken(skel.size(), 0);
	for (const int k : chains.front()) taken[std::size_t(k)] = 1;
	const auto distCells = [&](int k) { return double(g.signedDist[std::size_t(k)]) / g.cell + 0.5; };
	for (int round = 0; round < 12; ++round) {
		std::vector<int> starts;
		for (const int k : cells) {
			if (taken[std::size_t(k)]) starts.push_back(k);
		}
		const Walk w = walkFrom(g, skel, starts);
		// The end farthest from what is taken: a branch if it reaches clearly beyond the kerb.
		int best = -1;
		double bestLen = 0;
		for (const int k : cells) {
			if (taken[std::size_t(k)] || !std::isfinite(w.dist[std::size_t(k)])) continue;
			const auto chain = pathTo(w, k);
			const int join = chain.front();
			const double len = w.dist[std::size_t(k)];
			if (len > 2.2 * distCells(join) + 3 && len > bestLen) {
				bestLen = len;
				best = k;
			}
		}
		if (best < 0) break;
		const auto chain = pathTo(w, best);
		for (const int k : chain) taken[std::size_t(k)] = 1;
		chains.push_back(chain);
	}

	// At a junction the main road is the straightest way through (a T's top, not round the corner
	// into the side street because that way is longer).
	{
		const double reachCells = std::max(4.0, 2 * distCells(chains.front()[chains.front().size() / 2]));
		const auto armDir = [&](const std::vector<int>& arm) {
			// From the arm's first cell (the junction) to the cell about a street's width along it.
			const Vec3 j = g.centre(arm.front() % g.nx, arm.front() / g.nx);
			Vec3 tip = j;
			double walked = 0;
			for (std::size_t i = 1; i < arm.size() && walked < reachCells; ++i) {
				tip = g.centre(arm[i] % g.nx, arm[i] / g.nx);
				walked = length(flat(tip - j)) / g.cell;
			}
			const Vec3 d = flat(tip - j);
			return length(d) > 1e-9 ? normalize(d) : Vec3{};
		};
		for (std::size_t c = 1; c < chains.size(); ++c) {
			auto& main = chains.front();
			auto& branch = chains[c];
			const auto at = std::find(main.begin(), main.end(), branch.front());
			if (at == main.end() || at == main.begin() || at + 1 == main.end()) continue;
			const std::size_t j = std::size_t(at - main.begin());
			std::vector<int> armA(main.begin(), main.begin() + std::ptrdiff_t(j) + 1);
			std::reverse(armA.begin(), armA.end());
			std::vector<int> armB(main.begin() + std::ptrdiff_t(j), main.end());
			const Vec3 dA = armDir(armA), dB = armDir(armB), dC = armDir(branch);
			const double ab = -dot(dA, dB), ac = -dot(dA, dC), bc = -dot(dB, dC);
			if (ab >= std::max(ac, bc) - 0.2) continue;
			std::vector<int> newMain, newBranch;
			if (ac >= bc) {
				// A and C: main from A's far end through the junction into C; B the branch.
				newMain.assign(armA.rbegin(), armA.rend());
				newMain.insert(newMain.end(), branch.begin() + 1, branch.end());
				newBranch = armB;
			} else {
				newMain.assign(branch.rbegin(), branch.rend());
				newMain.insert(newMain.end(), armB.begin() + 1, armB.end());
				newBranch = armA;
			}
			main = std::move(newMain);
			branch = std::move(newBranch);
		}
	}

	// The street's width: twice the distance to the edge along the main road's middle part.
	double streetWidth = 0;
	{
		std::vector<double> ds;
		const auto& main = chains.front();
		for (std::size_t i = main.size() / 8; i < main.size() - main.size() / 8; ++i) ds.push_back(2 * (double(g.signedDist[std::size_t(main[i])]) + g.cell / 2));
		streetWidth = std::max(2 * g.cell, median(ds));
	}

	// 4. Polylines, made exact.
	std::vector<Line> lines;
	for (std::size_t c = 0; c < chains.size(); ++c) {
		std::vector<Vec3> raw;
		for (const int k : chains[c]) raw.push_back(g.centre(k % g.nx, k / g.nx));
		Line line;
		line.branch = c > 0;
		line.points = simplify(raw, std::max(1.5 * g.cell, 0.08 * streetWidth));
		if (line.points.size() < 2) continue;
		line.widths.assign(line.points.size() - 1, 0);
		mergeShortBends(line, streetWidth);
		dropStraightPoints(line);
		centre(line, street, streetWidth);
		joinShortRuns(line, streetWidth);
		mergeShortBends(line, streetWidth);
		dropStraightPoints(line);
		lines.push_back(std::move(line));
	}
	if (lines.empty()) {
		corridor.notes.push_back("the selected floor is too small for a road");
		return corridor;
	}
	// The ends reach the street's end; a branch starts on the main road's centre line.
	const auto extend = [&](Line& line, bool atStart) {
		auto& pts = line.points;
		const Vec3 end = atStart ? pts.front() : pts.back();
		const Vec3 inward = atStart ? normalize(flat(pts[1] - pts[0])) : normalize(flat(pts[pts.size() - 2] - pts.back()));
		const double out = street.reach(end, -inward, 8 * streetWidth + 4096);
		Vec3 p = end - inward * out;
		(atStart ? pts.front() : pts.back()) = p;
	};
	for (auto& line : lines) {
		if (!line.branch) extend(line, true);
		extend(line, false);
	}
	const Line& main = lines.front();
	for (std::size_t li = 1; li < lines.size(); ++li) {
		Line& line = lines[li];
		const Vec3 start = line.points.front();
		const Vec3 d = normalize(flat(line.points[1] - line.points[0]));
		// The main road's part nearest to the start; the branch starts where its line meets it.
		double bestD = 1e30;
		std::optional<Vec3> join;
		for (std::size_t k = 0; k + 1 < main.points.size(); ++k) {
			const Vec3 p = main.points[k], q = main.points[k + 1];
			const Vec3 e = flat(q - p);
			const double t = std::clamp(dot(flat(start - p), e) / std::max(1e-9, dot(e, e)), 0.0, 1.0);
			const double dist = length(flat(p + e * t - start));
			if (dist < bestD) {
				bestD = dist;
				const auto cross = crossing(line.points[1], d, p, normalize(e));
				join = cross ? *cross : p + e * t;
			}
		}
		if (join) line.points.front() = *join;
	}
	// Heights from the floor; coordinates a hair off a whole unit (the edge searches) made whole.
	for (auto& line : lines) {
		for (auto& p : line.points) {
			for (double* c : {&p.x, &p.y}) {
				if (std::abs(*c - std::round(*c)) < 0.02) *c = std::round(*c);
			}
			p.z = street.zAt(p.x, p.y);
		}
	}

	// Widths: the road is sized from the narrowest long straight part of the street, so the parking
	// lanes are at least `parking` wide everywhere (wider where the street is wider).
	{
		double narrowest = 0;
		for (const auto& line : lines) {
			for (std::size_t k = 0; k < line.widths.size(); ++k) {
				const double len = length(flat(line.points[k + 1] - line.points[k]));
				if (line.widths[k] > 0 && len >= 0.5 * line.widths[k] && (narrowest == 0 || line.widths[k] < narrowest)) narrowest = line.widths[k];
			}
		}
		if (narrowest > 0) streetWidth = narrowest;
	}
	corridor.width = std::round(streetWidth);
	const double minRoad = std::min(corridor.width, 128.0);
	double parking = options.wholeWidth ? 0 : std::max(0.0, options.parking);
	if (corridor.width - 2 * parking < minRoad) {
		const double fits = std::max(0.0, std::floor((corridor.width - minRoad) / 2));
		if (parking > 0) corridor.notes.push_back("the street is " + units(corridor.width) + " wide: the parking lanes are " + units(fits) + " (not " + units(parking) + ") so the road keeps " + units(minRoad));
		parking = fits;
	}
	corridor.parking = parking;
	corridor.roadWidth = corridor.width - 2 * parking;
	const double hw = corridor.roadWidth / 2;

	// 5. Curves: per corner the largest one whose edges keep the lane clear of the kerbs.
	const double tolerance = 0.75 * g.cell + 1;
	int tight = 0;
	for (auto& line : lines) {
		RoadPath path;
		path.points = line.points;
		path.innerRadius.assign(path.points.size(), -1.0);
		const std::size_t n = path.points.size() - 1;
		for (std::size_t i = 1; i < n; ++i) {
			const Vec3 P = path.points[i];
			const Vec3 d0 = normalize(flat(P - path.points[i - 1])), d1 = normalize(flat(path.points[i + 1] - P));
			const double theta = std::atan2(cross2(d0, d1), dot(d0, d1));
			const double turn = std::abs(theta);
			if (turn < PI / 180) continue;
			const double len0 = length(flat(P - path.points[i - 1])), len1 = length(flat(path.points[i + 1] - P));
			const double room = std::min(len0 * (i - 1 == 0 ? 1.0 : 0.5), len1 * (i + 1 == n ? 1.0 : 0.5));
			const double maxR = room / std::tan(turn / 2);
			// The lane each side keeps: what the narrower of the two parts leaves beside the road.
			const double w0 = line.widths[i - 1] > 0 ? line.widths[i - 1] : corridor.width, w1 = line.widths[i] > 0 ? line.widths[i] : corridor.width;
			const double clear = std::max(0.0, std::min(w0, w1) / 2 - hw);
			const double side = theta > 0 ? 1.0 : -1.0;
			const auto clearance = [&](double inner) {
				const double R = std::min(inner + hw, maxR);
				if (R < hw - 1e-6) return -1e9;
				const double t = R * std::tan(turn / 2);
				const Vec3 t0 = flat(P) - d0 * t;
				const Vec3 centre = t0 + leftOf(d0) * (side * R);
				const Vec3 r0 = normalize(t0 - centre);
				double worst = 1e9;
				for (int k = 0; k <= 16; ++k) {
					const Vec3 u = turned(r0, theta * k / 16);
					for (const double r : {R - hw, R + hw}) {
						const Vec3 s = centre + u * r;
						worst = std::min(worst, g.distAt(s.x, s.y));
					}
				}
				return worst;
			};
			const double need = clear - tolerance;
			double best = -1;
			const double hi = std::max(0.0, std::min(4 * corridor.width, maxR - hw));
			if (clearance(0) >= need) {
				double lo = 0, top = hi;
				if (clearance(top) >= need) {
					lo = top;
				} else {
					for (int k = 0; k < 16; ++k) {
						const double mid = (lo + top) / 2;
						(clearance(mid) >= need ? lo : top) = mid;
					}
				}
				best = std::floor(lo / 4) * 4;
			} else {
				// No curve keeps the lane fully clear (a jog, a tight spot): the largest one that
				// stays on the street, else the one that keeps clearest of its edge.
				const double onStreet = -tolerance;
				double found = -1;
				for (int k = 32; k >= 0 && found < 0; --k) {
					const double r = hi * k / 32;
					if (clearance(r) >= onStreet) found = r;
				}
				if (found < 0) {
					double bestClear = -1e30;
					for (int k = 0; k <= 16; ++k) {
						const double r = hi * k / 16;
						const double c = clearance(r);
						if (c > bestClear) {
							bestClear = c;
							found = r;
						}
					}
				}
				best = std::floor(found / 4) * 4;
				++tight;
			}
			// A curve with no inner radius fans out from one point (slivers beside it): at least 16.
			path.innerRadius[i] = std::max(best, std::min(16.0, std::max(0.0, maxR - hw)));
		}
		corridor.paths.push_back(std::move(path));
	}
	if (tight > 0) corridor.notes.push_back(std::to_string(tight) + " curve(s) reach into the parking lane: the street is too tight there for the lane");
	if (corridor.paths.size() > 1) corridor.notes.push_back(std::to_string(corridor.paths.size() - 1) + " side street(s) join the main road");
	return corridor;
}

std::string describeRoadCorridor(const RoadCorridor& c) {
	std::ostringstream out;
	out << "street: " << c.floor.size() << " floor brush(es)";
	if (!c.ignored.empty()) out << ", " << c.ignored.size() << " left alone";
	out << ", top " << c.top << ", thickness " << c.thickness << ", area " << std::lround(c.area) << ", raster " << c.cell << "\n";
	out << "width " << c.width << ": road " << c.roadWidth << ", parking " << c.parking << " each side, material " << c.material << "\n";
	out << "floor:";
	for (const auto& ref : c.floor) out << " " << ref.entityId << "/" << ref.solidId;
	out << "\n";
	if (!c.ignored.empty()) {
		out << "left alone:";
		for (const auto& ref : c.ignored) out << " " << ref.entityId << "/" << ref.solidId;
		out << "\n";
	}
	for (std::size_t i = 0; i < c.paths.size(); ++i) {
		out << (i == 0 ? "main road:" : "side street:");
		const auto& p = c.paths[i];
		for (std::size_t k = 0; k < p.points.size(); ++k) {
			out << " (" << p.points[k].x << " " << p.points[k].y << " " << p.points[k].z << ")";
			if (k < p.innerRadius.size() && p.innerRadius[k] >= 0) out << " r" << p.innerRadius[k];
		}
		out << "\n";
	}
	for (const auto& note : c.notes) out << "note: " << note << "\n";
	return out.str();
}

std::vector<RoadPath> roadPathsFromSolids(const MapDocument& doc, const std::vector<SolidRef>& solids, RoadBuildOptions* suggested, std::vector<std::string>* notes) {
	RoadBuildOptions options = suggested ? *suggested : RoadBuildOptions{};
	const RoadCorridor corridor = roadCorridorFromSolids(doc, solids, options);
	if (suggested && corridor.ok()) {
		suggested->width = corridor.roadWidth;
		suggested->thickness = corridor.thickness;
		if (suggested->material.empty()) suggested->material = corridor.material;
	}
	if (notes) notes->insert(notes->end(), corridor.notes.begin(), corridor.notes.end());
	return corridor.paths;
}

RoadBuildPlan planRoadFromSelection(const MapDocument& doc, MapDocument& ids, const std::vector<SolidRef>& solids, const RoadBuildOptions& options,
                                    const TextureSizeFn& textureSize, RoadCorridor* corridorOut) {
	RoadCorridor corridor = roadCorridorFromSolids(doc, solids, options);
	RoadBuildPlan plan;
	if (corridor.ok()) {
		RoadBuildOptions o = options;
		o.width = corridor.roadWidth;
		if (o.material.empty()) o.material = corridor.material;
		plan = planRoadBuild(doc, ids, corridor.paths, o, textureSize, corridor.floor);
		plan.notes.insert(plan.notes.begin(), corridor.notes.begin(), corridor.notes.end());
	} else {
		plan.notes = corridor.notes;
	}
	if (corridorOut) *corridorOut = std::move(corridor);
	return plan;
}

std::string describeRoadBuild(const RoadBuildPlan& plan) {
	std::ostringstream out;
	out << "road builder: " << (plan.road.size() - std::size_t(plan.seals)) << " road brush(es), " << plan.seals << " seal(s), " << plan.kerbs.size() << " kerb(s), "
	    << plan.curves << " curve(s), " << plan.mitred << " mitred, " << plan.junctions << " junction(s), " << plan.carved << " carved, " << plan.skipped
	    << " left out, material " << plan.material << "\n";
	for (const auto& line : plan.centreLines) {
		out << "line:";
		for (const auto& p : line) out << " (" << p.x << " " << p.y << " " << p.z << ")";
		out << "\n";
	}
	for (const auto& r : plan.changed) out << "replaced " << r.ref.entityId << "/" << r.ref.solidId << " by " << r.pieces.size() << " piece(s)\n";
	for (const auto& note : plan.notes) out << "note: " << note << "\n";
	return out.str();
}

} // namespace sme
