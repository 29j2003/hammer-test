// road_builder.cpp - Building roads along a path and fitting them into the map (see road_builder.h).

#include <sme/road_builder.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/edge_round.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <optional>

namespace sme {

namespace {

constexpr double PI = std::numbers::pi;
const std::string NODRAW = "TOOLS/TOOLSNODRAW";

std::string units(double v) {
	char text[32];
	std::snprintf(text, sizeof text, "%g", std::round(v * 10) / 10);
	return text;
}

Vec3 flat(const Vec3& v) {
	return {v.x, v.y, 0};
}

/// The horizontal direction to the left of `d` (seen from above, d forward).
Vec3 leftOf(const Vec3& d) {
	return normalize(Vec3{-d.y, d.x, 0});
}

/// `v` turned by `angle` (radians) about the vertical axis.
Vec3 turned(const Vec3& v, double angle) {
	const double c = std::cos(angle), s = std::sin(angle);
	return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

double cross2(const Vec3& a, const Vec3& b) {
	return a.x * b.y - a.y * b.x;
}

bool sameMaterial(const std::string& a, const std::string& b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}

bool isToolMaterial(const std::string& material) {
	return material.size() >= 6 && sameMaterial(material.substr(0, 6), "tools/");
}

/// A cross-section of the road: the centre point (the road's top) and the horizontal direction to
/// its left edge (corners at centre +- left * half width; longer than 1 at a mitred corner).
struct Station {
	Vec3 c;
	Vec3 left;
	double s = 0;   ///< Length along the centre line's chords up to here
};

/// The stretch between two consecutive stations.
struct Span {
	std::size_t a = 0;   ///< First station (the other is a + 1)
	bool arc = false;
	Vec3 centre;         ///< Arc: the circle's middle
	double radius = 0;   ///< Arc: centre line radius
};

/// A path laid out: stations and spans.
struct PathLayout {
	std::vector<Station> stations;
	std::vector<Span> spans;
};

/// The centre line through the points with every corner turned into an arc (or a mitre).
PathLayout layoutPath(const RoadPath& path, const RoadBuildOptions& o, RoadBuildPlan& plan) {
	PathLayout out;
	std::vector<Vec3> p;
	for (const auto& point : path.points) {
		if (p.empty() || length(flat(point - p.back())) >= 1) p.push_back(point);
	}
	// A turn back on itself has no curve: the point is left out.
	for (bool again = true; again && p.size() > 2;) {
		again = false;
		for (std::size_t i = 1; i + 1 < p.size(); ++i) {
			const Vec3 d0 = normalize(flat(p[i] - p[i - 1])), d1 = normalize(flat(p[i + 1] - p[i]));
			if (dot(d0, d1) < std::cos(170.0 * PI / 180)) {
				plan.notes.push_back("a point where the path turns back on itself was left out");
				p.erase(p.begin() + std::ptrdiff_t(i));
				again = true;
				break;
			}
		}
	}
	if (p.size() < 2) return out;
	const double hw = o.width / 2;
	const std::size_t n = p.size() - 1;
	std::vector<Vec3> dir(n);
	std::vector<double> len(n);
	for (std::size_t i = 0; i < n; ++i) {
		dir[i] = normalize(flat(p[i + 1] - p[i]));
		len[i] = length(flat(p[i + 1] - p[i]));
	}
	const auto zAt = [&](std::size_t segment, double along) {
		return p[segment].z + (p[segment + 1].z - p[segment].z) * std::clamp(along / len[segment], 0.0, 1.0);
	};

	const auto addStation = [&](const Vec3& c, const Vec3& left, bool arc, const Vec3& centre, double radius) {
		if (!out.stations.empty()) {
			const Station& last = out.stations.back();
			if (length(c - last.c) < 0.5) return;   // the same cross-section
			Station s{c, left, last.s + length(c - last.c)};
			out.spans.push_back({out.stations.size() - 1, arc, centre, radius});
			out.stations.push_back(s);
		} else {
			out.stations.push_back({c, left, 0});
		}
	};

	addStation(p[0], leftOf(dir[0]), false, {}, 0);
	const double wanted = std::max(0.0, o.innerRadius) + hw;
	for (std::size_t i = 1; i < n; ++i) {
		const Vec3 d0 = dir[i - 1], d1 = dir[i];
		const double theta = std::atan2(cross2(d0, d1), dot(d0, d1));   // + left turn
		const double turn = std::abs(theta);
		bool arc = turn > 1.0 * PI / 180;
		double radius = wanted;
		if (arc) {
			const double tanHalf = std::tan(turn / 2);
			const double room = std::min(len[i - 1] * (i - 1 == 0 ? 1.0 : 0.5), len[i] * (i + 1 == n ? 1.0 : 0.5));
			radius = std::min(wanted, room / tanHalf);
			if (radius < hw - 1e-6) {
				arc = false;
				++plan.mitred;
				plan.notes.push_back("a corner is too close to the next one for a curve: its pieces meet at an angle (move the points apart)");
			} else if (radius < wanted - 0.5) {
				plan.notes.push_back("a curve was made tighter (inner radius " + units(radius - hw) + ") to fit between its points");
			}
		}
		if (!arc) {
			// Mitre: one cross-section along the bisector, as wide as the road on both sides.
			const Vec3 bisector = normalize(leftOf(d0) + leftOf(d1));
			const double c = std::max(0.2, std::cos(theta / 2));
			addStation(p[i], bisector / c, false, {}, 0);
			continue;
		}
		++plan.curves;
		plan.radius = plan.radius == 0 ? radius - hw : std::min(plan.radius, radius - hw);
		const double t = radius * std::tan(turn / 2);
		const Vec3 t0 = p[i] - d0 * t;
		const double z0 = zAt(i - 1, len[i - 1] - t), z1 = zAt(i, t);
		const double side = theta > 0 ? 1.0 : -1.0;
		const Vec3 centre = flat(t0) + leftOf(d0) * (side * radius);
		const Vec3 r0 = flat(t0) - centre;
		const int pieces = std::clamp(int(std::lround(std::max(1, o.segments) * turn / (PI / 2))), 1, 64);
		for (int k = 0; k <= pieces; ++k) {
			const double phi = theta * k / pieces;
			Vec3 c = centre + turned(r0, phi);
			c.z = z0 + (z1 - z0) * k / pieces;
			addStation(c, leftOf(turned(d0, phi)), k > 0, centre, radius);
		}
	}
	addStation(p[n], leftOf(dir[n - 1]), false, {}, 0);
	return out;
}

/// One brush the builder makes, with what it needs to know about it later.
struct Piece {
	enum class Kind { Road, Kerb, Seal };
	Kind kind = Kind::Road;
	Solid solid;
	std::vector<Vec3> top;          ///< The true top corners (counter-clockwise from above)
	std::vector<double> across;     ///< Offset from the centre line of each corner
	bool arc = false;
	Vec3 leftAxis;                  ///< Horizontal, to the road's left
	double topZ = 0;                ///< Average height of the top
};

/// Whether four points lie in one plane.
bool planar(const std::vector<Vec3>& q) {
	if (q.size() < 4) return true;
	const Vec3 n = cross(q[1] - q[0], q[2] - q[0]);
	const double len = length(n);
	if (len < 1e-9) return true;
	return std::abs(dot(n / len, q[3] - q[0])) < 0.01;
}

/// Height and across offset at (x, y) inside a piece's top (fan of triangles from corner 0).
std::pair<double, double> topAt(const Piece& piece, double x, double y) {
	const auto& q = piece.top;
	double bestZ = q.front().z, bestA = piece.across.front();
	double bestWorst = -1e30;
	for (std::size_t k = 1; k + 1 < q.size(); ++k) {
		const Vec3 &a = q[0], &b = q[k], &c = q[k + 1];
		const double det = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		if (std::abs(det) < 1e-9) continue;
		const double wb = ((x - a.x) * (c.y - a.y) - (c.x - a.x) * (y - a.y)) / det;
		const double wc = ((b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y)) / det;
		const double wa = 1 - wb - wc;
		const double worst = std::min({wa, wb, wc});
		if (worst > bestWorst) {
			bestWorst = worst;
			bestZ = wa * a.z + wb * b.z + wc * c.z;
			bestA = wa * piece.across[0] + wb * piece.across[k] + wc * piece.across[k + 1];
		}
	}
	return {bestZ, bestA};
}

/// The texture of a road (or kerb) top: one axis along the piece's chord, the other across, the
/// shift so the centre line sits in the texture's middle and the length carries on.
struct Mapping {
	bool alongV = true;
	double scale = 0.25;
	double texAcross = 512;   ///< Texels across (for the middle)
	bool flip = false;
};

void mapTop(Side& side, const Mapping& m, const Vec3& c0, const Vec3& c1, double s0, bool centred, double acrossFactor) {
	Vec3 along = normalize(c1 - c0);
	Vec3 across = leftOf(flat(c1 - c0));
	double s = s0;
	if (m.flip) {
		along = -along;
		across = -across;
		s = -s0;
	}
	const double k = m.scale;
	const double middle = centred ? m.texAcross / 2 : 0;
	// Across, the texture is measured along the joint lines (radial on a curve, slightly longer
	// than straight across the piece), so a lane line meets the next piece's at the joint.
	const double ka = k * std::clamp(acrossFactor, 0.5, 1.0);
	TextureAxis alongAxis{along, (s - dot(c0, along)) / k, k};
	if (m.alongV) {
		side.v = alongAxis;
		side.u = TextureAxis{across, middle - dot(c0, across) / ka, ka};
	} else {
		side.u = alongAxis;
		side.v = TextureAxis{-across, middle - dot(c0, -across) / ka, ka};
	}
	side.rotation = 0;
}

/// Brushes for a strip between two stations: `lo`/`hi` are the offsets across (from the centre
/// line, + to the left) at the first and the second station, `lift` raises the top, `depth` is
/// how far the brush reaches below it. A top that is not flat is split into two triangles, or
/// (for displacements) gets one plane through its middle that the displacement then bends.
std::vector<Piece> stripPieces(MapDocument& ids, const Station& s0, const Station& s1, double lo0, double hi0, double lo1, double hi1, double lift,
                               double depth, bool oneTop) {
	std::vector<Vec3> q{s0.c + s0.left * lo0, s1.c + s1.left * lo1, s1.c + s1.left * hi1, s0.c + s0.left * hi0};
	std::vector<double> across{lo0, lo1, hi1, hi0};
	for (auto& v : q) v.z += lift;
	// Collapsed corners (a curve with no inner radius): a triangle.
	for (std::size_t i = 0; i < q.size() && q.size() > 3;) {
		const std::size_t j = (i + 1) % q.size();
		if (length(flat(q[i] - q[j])) < 0.1) {
			q.erase(q.begin() + std::ptrdiff_t(j));
			across.erase(across.begin() + std::ptrdiff_t(j));
		} else {
			++i;
		}
	}
	std::vector<Piece> out;
	const auto prism = [&](const std::vector<Vec3>& top) -> std::optional<Solid> {
		std::vector<Vec3> points = top;
		for (const auto& v : top) points.push_back(v - Vec3{0, 0, depth});
		return solidFromPoints(ids, points, NODRAW);
	};
	const auto add = [&](std::optional<Solid> solid, std::vector<Vec3> top, std::vector<double> off) {
		if (!solid) return;
		Piece piece;
		piece.solid = std::move(*solid);
		double z = 0;
		for (const auto& v : top) z += v.z;
		piece.topZ = z / double(top.size());
		piece.top = std::move(top);
		piece.across = std::move(off);
		out.push_back(std::move(piece));
	};
	if (q.size() < 3) return out;
	if (planar(q)) {
		add(prism(q), q, across);
	} else if (oneTop) {
		// One plane through the middle along the chord and level across: the corners are moved
		// onto it (straight up or down); the displacement puts them back where they belong.
		const Vec3 m0 = (q[0] + q[3]) * 0.5, m1 = (q[1] + q[2]) * 0.5;
		const Vec3 mid = (m0 + m1) * 0.5;
		const Vec3 normal = normalize(cross(normalize(m1 - m0), leftOf(flat(m1 - m0))));
		std::vector<Vec3> onPlane = q;
		for (auto& v : onPlane) {
			if (std::abs(normal.z) > 1e-6) v.z = mid.z - (normal.x * (v.x - mid.x) + normal.y * (v.y - mid.y)) / normal.z;
		}
		add(prism(onPlane), q, across);
	} else {
		add(prism({q[0], q[1], q[2]}), {q[0], q[1], q[2]}, {across[0], across[1], across[2]});
		add(prism({q[0], q[2], q[3]}), {q[0], q[2], q[3]}, {across[0], across[2], across[3]});
	}
	return out;
}

/// Whether both points lie in the plane.
bool onPlane(const Plane& plane, const Vec3& a, const Vec3& b) {
	return std::abs(plane.distanceTo(a)) < 0.05 && std::abs(plane.distanceTo(b)) < 0.05;
}

/// A tall prism over a brush's footprint: from its bottom up to `top` (what carves its neighbours).
std::optional<Solid> columnOver(MapDocument& ids, const Solid& solid, double top) {
	std::vector<Vec3> points = solidCorners(solid);
	const std::size_t count = points.size();
	for (std::size_t i = 0; i < count; ++i) points.push_back({points[i].x, points[i].y, top});
	return solidFromPoints(ids, points, NODRAW);
}

/// Whether two boxes overlap with some volume.
bool boxesOverlap(const Bounds& a, const Bounds& b, double margin = 0.01) {
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] <= b.min[i] + margin || b.max[i] <= a.min[i] + margin) return false;
	}
	return true;
}

/// The upward face with the highest normal (the top), or nullptr.
const Side* topSide(const Solid& solid, double minNormal = 0.7) {
	const Side* best = nullptr;
	double bestZ = minNormal;
	for (const auto& side : solid.sides) {
		const double z = side.plane().normal.z;
		if (z > bestZ) {
			bestZ = z;
			best = &side;
		}
	}
	return best;
}

/// Whether a side lies in the plane of one of the brush's sides (it was there before a cut; the cut
/// gives the parts fresh side ids, so the plane tells).
bool fromOriginal(const Solid& original, const Side& side) {
	const Plane p = side.plane();
	return std::any_of(original.sides.begin(), original.sides.end(), [&](const Side& o) {
		const Plane q = o.plane();
		return dot(p.normal, q.normal) > 0.99999 && std::abs(p.dist - q.dist) < 0.01;
	});
}

/// Pieces of a carved brush: the sides the carve added get `material` (the brush's own look,
/// world-aligned), joined where two pieces form one brush again.
void tidyCarved(MapDocument& ids, const Solid& original, std::vector<Solid>& pieces, const std::string& material) {
	for (auto& piece : pieces) {
		for (auto& side : piece.sides) {
			if (fromOriginal(original, side)) continue;
			if (!side.dispInfo()) {
				side.material = material;
				alignToWorld(side);
			}
		}
	}
	bool joined = true;
	while (joined && pieces.size() > 1) {
		joined = false;
		for (std::size_t i = 0; i < pieces.size() && !joined; ++i) {
			for (std::size_t j = i + 1; j < pieces.size() && !joined; ++j) {
				if (!boxesOverlap(buildBrushGeometry(pieces[i]).bounds, buildBrushGeometry(pieces[j]).bounds, -0.01)) continue;
				auto merged = mergeSolids(ids, {pieces[i], pieces[j]});
				// The pieces never overlap: the hull is their union exactly when the volumes add up.
				if (!merged || std::abs(solidVolume(merged->solid) - solidVolume(pieces[i]) - solidVolume(pieces[j])) > 0.5 ||
				    !brushProblem(merged->solid).empty()) {
					continue;
				}
				pieces[i] = std::move(merged->solid);
				pieces.erase(pieces.begin() + std::ptrdiff_t(j));
				joined = true;
			}
		}
	}
}

/// The material most of a brush's visible sides have (for faces a carve adds).
std::string mainMaterial(const Solid& solid) {
	std::map<std::string, int> count;
	for (const auto& side : solid.sides) {
		if (!isToolMaterial(side.material)) ++count[side.material];
	}
	std::string best = solid.sides.empty() ? NODRAW : solid.sides.front().material;
	int most = 0;
	for (const auto& [material, n] : count) {
		if (n > most) {
			most = n;
			best = material;
		}
	}
	return best;
}

/// A piece with every cutter taken out of it (the cut faces nodraw, as they lie against the cutter).
std::vector<Piece> carvePiece(MapDocument& ids, const Piece& piece, const Solid& cutter) {
	std::vector<Piece> out;
	auto parts = subtractSolid(ids, piece.solid, cutter);
	if (parts.size() == 1 && parts.front().id == piece.solid.id && parts.front().sides.size() == piece.solid.sides.size()) {
		out.push_back(piece);
		return out;
	}
	for (auto& part : parts) {
		if (solidVolume(part) < 1.0) continue;
		Piece p = piece;
		for (auto& side : part.sides) {
			if (!fromOriginal(piece.solid, side)) side.material = NODRAW;
		}
		p.solid = std::move(part);
		out.push_back(std::move(p));
	}
	return out;
}

} // namespace

double roadCrownOffset(double across, double halfWidth, double crown, double roughness, double x, double y) {
	if (halfWidth <= 0) return 0;
	const double f = std::max(0.0, 1.0 - (across / halfWidth) * (across / halfWidth));
	// Smooth, repeatable unevenness from the world position (the same on both sides of a seam).
	const double noise = (std::sin(x * 0.031 + 1.7) * std::sin(y * 0.027 + 0.3) + 0.5 * std::sin((x + y) * 0.071 + 0.9)) / 1.5;
	return f * (crown + roughness * noise);
}

std::vector<Solid> RoadBuildPlan::resultSolids() const {
	std::vector<Solid> all = road;
	all.insert(all.end(), kerbs.begin(), kerbs.end());
	for (const auto& r : changed) all.insert(all.end(), r.pieces.begin(), r.pieces.end());
	return all;
}

std::vector<SolidRef> RoadBuildPlan::replacedRefs() const {
	std::vector<SolidRef> refs;
	for (const auto& r : changed) refs.push_back(r.ref);
	return refs;
}

// --- Paths from selected floor brushes ---------------------------------------------------------

namespace {

/// A selected floor brush seen from above.
struct FloorPiece {
	SolidRef ref;
	std::vector<Vec3> outline;   ///< Top face corners
	Plane top;
	double bottom = 0;
	Vec3 centre;
	Vec3 dir;                    ///< Along its longest top edge
	double along = 0, across = 0;
	bool longPiece = false;
	std::string material;
	[[nodiscard]] double zAt(double x, double y) const {
		return std::abs(top.normal.z) > 1e-6 ? (top.dist - top.normal.x * x - top.normal.y * y) / top.normal.z : centre.z;
	}
};

/// Where two outlines touch: the shared stretch of edge (more than a unit long, same height).
struct Contact {
	Vec3 mid;
	Vec3 dir;
	double length = 0;
};
std::optional<Contact> touching(const FloorPiece& a, const FloorPiece& b) {
	for (std::size_t i = 0; i < a.outline.size(); ++i) {
		const Vec3 p = a.outline[i], q = a.outline[(i + 1) % a.outline.size()];
		const Vec3 d = flat(q - p);
		const double len = length(d);
		if (len < 1) continue;
		const Vec3 u = d / len;
		for (std::size_t j = 0; j < b.outline.size(); ++j) {
			const Vec3 r = b.outline[j], s = b.outline[(j + 1) % b.outline.size()];
			if (std::abs(cross2(u, flat(r - p))) > 0.5 || std::abs(cross2(u, flat(s - p))) > 0.5) continue;
			const double t0 = dot(flat(r - p), u), t1 = dot(flat(s - p), u);
			const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(len, std::max(t0, t1));
			if (hi - lo <= 1) continue;
			const Vec3 m = p + u * ((lo + hi) / 2);
			if (std::abs(a.zAt(m.x, m.y) - b.zAt(m.x, m.y)) < 2) return Contact{m, u, hi - lo};
		}
	}
	return std::nullopt;
}

/// Whether the contact lies at an end of the piece (the road goes on there), not along its side
/// (a road joining it there: a T). Square pieces go on in every direction.
bool atEnd(const FloorPiece& piece, const Contact& contact) {
	if (!piece.longPiece || std::abs(dot(contact.dir, piece.dir)) < 0.5) return true;
	// Touching along most of its long side: the road crosses it (a short piece between two roads).
	if (contact.length >= 0.75 * piece.along) return true;
	const double fromEnd = piece.along / 2 - std::abs(dot(flat(contact.mid - piece.centre), piece.dir));
	return fromEnd <= contact.length / 2 + 1;
}

/// Where the lines through a (direction da) and b (direction db) cross (horizontally).
std::optional<Vec3> crossing(const Vec3& a, const Vec3& da, const Vec3& b, const Vec3& db) {
	const double det = cross2(da, db);
	if (std::abs(det) < 1e-6) return std::nullopt;
	const double t = cross2(flat(b - a), db) / det;
	return a + da * t;
}

/// Centre line points through a chain of pieces (in order).
RoadPath chainPath(const std::vector<FloorPiece>& pieces, const std::vector<std::size_t>& chain, std::optional<Vec3> startAt) {
	RoadPath path;
	if (chain.empty()) return path;
	// Each piece's direction of travel.
	std::vector<Vec3> travel(chain.size());
	std::vector<bool> crossed(chain.size(), false);
	for (std::size_t i = 0; i < chain.size(); ++i) {
		const Vec3 from = i > 0 ? pieces[chain[i - 1]].centre : (startAt ? *startAt : pieces[chain[i]].centre);
		const Vec3 to = i + 1 < chain.size() ? pieces[chain[i + 1]].centre : pieces[chain[i]].centre;
		Vec3 t = flat(to - from);
		if (length(t) < 1e-6) t = pieces[chain[i]].dir;
		const FloorPiece& p = pieces[chain[i]];
		// A long piece the road crosses (its neighbours lie beside its long sides) counts as a joint.
		if (p.longPiece && chain.size() > 1 && std::abs(dot(p.dir, normalize(t))) < 0.5) crossed[i] = true;
		const bool runs = p.longPiece && !crossed[i];
		Vec3 d = runs ? p.dir : normalize(t);
		if (runs && dot(d, t) < 0) d = -d;
		travel[i] = normalize(d);
	}
	const auto runs = [&](std::size_t i) { return pieces[chain[i]].longPiece && !crossed[i]; };
	const auto add = [&](Vec3 point, const FloorPiece& on) {
		point.z = on.zAt(point.x, point.y);
		if (path.points.empty() || length(flat(point - path.points.back())) > 0.5) path.points.push_back(point);
	};
	const auto half = [&](std::size_t i) {
		const FloorPiece& p = pieces[chain[i]];
		return dot(travel[i], p.dir) * dot(travel[i], p.dir) > 0.5 ? p.along / 2 : p.across / 2;
	};
	// The start: the far end of the first piece (or the junction it starts from).
	if (startAt) {
		add(*startAt, pieces[chain[0]]);
		path.points.back().z = startAt->z;
	} else {
		add(pieces[chain[0]].centre - travel[0] * half(0), pieces[chain[0]]);
	}
	// Corners: where the centre lines of consecutive long pieces cross.
	std::optional<std::size_t> last;
	for (std::size_t i = 0; i < chain.size(); ++i) {
		if (!runs(i)) continue;
		if (last) {
			const FloorPiece &a = pieces[chain[*last]], &b = pieces[chain[i]];
			const Vec3 da = travel[*last], db = travel[i];
			if (std::abs(cross2(da, db)) < 0.02) {
				if (std::abs(cross2(da, flat(b.centre - a.centre))) > 1) {
					// A jog sideways: from a little before the first piece's end to a little after
					// the second's start, so the S between them is not tighter than it has to be.
					const double pullA = std::min(a.across / 2, half(*last) / 2), pullB = std::min(b.across / 2, half(i) / 2);
					add(a.centre + da * (half(*last) - pullA), a);
					add(b.centre - db * (half(i) - pullB), b);
				}
			} else if (const auto corner = crossing(a.centre, da, b.centre, db)) {
				// The corner's height: the piece between them if there is one, else the first.
				const FloorPiece& on = (i > *last + 1) ? pieces[chain[*last + 1]] : a;
				add(*corner, on);
			}
		}
		last = i;
	}
	const std::size_t end = chain.size() - 1;
	add(pieces[chain[end]].centre + travel[end] * half(end), pieces[chain[end]]);
	return path;
}

} // namespace

std::vector<RoadPath> roadPathsFromSolids(const MapDocument& doc, const std::vector<SolidRef>& solids, RoadBuildOptions* suggested,
                                          std::vector<std::string>* notes) {
	std::vector<FloorPiece> pieces;
	std::map<std::string, int> materials;
	for (const auto& ref : solids) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) continue;
		const FaceGeometry* top = nullptr;
		for (const auto& face : geometry.faces) {
			if (face.normal.z > 0.7 && (!top || face.normal.z > top->normal.z)) top = &face;
		}
		if (!top || top->vertices.size() < 3) continue;
		FloorPiece p;
		p.ref = ref;
		p.outline = top->vertices;
		p.top = solid->sides[std::size_t(top->sideIndex)].plane();
		p.bottom = geometry.bounds.min.z;
		p.material = solid->sides[std::size_t(top->sideIndex)].material;
		++materials[p.material];
		for (const auto& v : p.outline) p.centre += v;
		p.centre = p.centre / double(p.outline.size());
		double longest = 0;
		for (std::size_t i = 0; i < p.outline.size(); ++i) {
			const Vec3 e = flat(p.outline[(i + 1) % p.outline.size()] - p.outline[i]);
			if (length(e) > longest) {
				longest = length(e);
				p.dir = normalize(e);
			}
		}
		double a0 = 1e30, a1 = -1e30, c0 = 1e30, c1 = -1e30;
		const Vec3 left = leftOf(p.dir);
		for (const auto& v : p.outline) {
			a0 = std::min(a0, dot(v, p.dir));
			a1 = std::max(a1, dot(v, p.dir));
			c0 = std::min(c0, dot(v, left));
			c1 = std::max(c1, dot(v, left));
		}
		p.along = a1 - a0;
		p.across = c1 - c0;
		// The middle of its extent (not the corners' average, which leans towards a cut corner).
		const double mid = (a0 + a1) / 2, side = (c0 + c1) / 2;
		const Vec3 c = p.dir * mid + left * side;
		p.centre = {c.x, c.y, p.zAt(c.x, c.y)};
		p.longPiece = p.along >= 1.25 * p.across;
		pieces.push_back(std::move(p));
	}
	std::vector<RoadPath> paths;
	if (pieces.empty()) return paths;
	if (suggested) {
		std::vector<double> widths, thick;
		for (const auto& p : pieces) {
			if (p.longPiece) widths.push_back(p.across);
			thick.push_back(p.centre.z - p.bottom);
		}
		if (widths.empty()) {
			for (const auto& p : pieces) widths.push_back(std::min(p.along, p.across));
		}
		std::sort(widths.begin(), widths.end());
		std::sort(thick.begin(), thick.end());
		suggested->width = std::max(16.0, std::round(widths[widths.size() / 2]));
		suggested->thickness = std::max(2.0, std::round(thick[thick.size() / 2]));
		if (suggested->material.empty()) {
			int most = 0;
			for (const auto& [material, n] : materials) {
				if (n > most) {
					most = n;
					suggested->material = material;
				}
			}
		}
	}
	// Neighbours: pieces the road goes on into, and pieces joining another one's side (T).
	const std::size_t count = pieces.size();
	std::vector<std::vector<std::size_t>> next(count);
	struct SideJoin {
		std::size_t trunk, branch;
	};
	std::vector<SideJoin> sideJoins;
	for (std::size_t i = 0; i < count; ++i) {
		for (std::size_t j = i + 1; j < count; ++j) {
			const auto contact = touching(pieces[i], pieces[j]);
			if (!contact) continue;
			const bool endI = atEnd(pieces[i], *contact), endJ = atEnd(pieces[j], *contact);
			if (endI && endJ) {
				next[i].push_back(j);
				next[j].push_back(i);
			} else if (!endI) {
				sideJoins.push_back({i, j});
			} else {
				sideJoins.push_back({j, i});
			}
		}
	}
	std::vector<bool> used(count, false);
	const auto weight = [&](std::size_t i) { return std::max(pieces[i].along, pieces[i].across); };
	// The longest chain from `from` through unused pieces (depth-first, small graphs).
	std::function<std::vector<std::size_t>(std::size_t, std::vector<bool>&, int&)> longest = [&](std::size_t from, std::vector<bool>& seen,
	                                                                                             int& budget) -> std::vector<std::size_t> {
		seen[from] = true;
		std::vector<std::size_t> best;
		double bestLength = -1;
		for (const std::size_t n : next[from]) {
			if (seen[n] || used[n] || --budget < 0) continue;
			auto chain = longest(n, seen, budget);
			double total = 0;
			for (const auto c : chain) total += weight(c);
			if (total > bestLength) {
				bestLength = total;
				best = std::move(chain);
			}
		}
		seen[from] = false;
		best.insert(best.begin(), from);
		return best;
	};
	const auto longestFrom = [&](std::size_t from) {
		std::vector<bool> seen(count, false);
		int budget = 20000;
		return longest(from, seen, budget);
	};
	const auto branchOut = [&] {
		for (bool again = true; again;) {
			again = false;
			// Through a junction piece: the branch starts at its middle.
			for (std::size_t i = 0; i < count && !again; ++i) {
				if (!used[i]) continue;
				for (const std::size_t n : next[i]) {
					if (used[n]) continue;
					auto branch = longestFrom(n);
					for (const auto c : branch) used[c] = true;
					paths.push_back(chainPath(pieces, branch, pieces[i].centre));
					if (notes) notes->push_back("a junction: one road joins another");
					again = true;
					break;
				}
			}
			// Along a road's side (T): the branch starts on that road's centre line.
			for (std::size_t k = 0; k < sideJoins.size() && !again; ++k) {
				const auto [trunk, b] = sideJoins[k];
				if (!used[trunk] || used[b]) continue;
				auto branch = longestFrom(b);
				for (const auto c : branch) used[c] = true;
				const FloorPiece& t = pieces[trunk];
				Vec3 start = t.centre + t.dir * dot(flat(pieces[b].centre - t.centre), t.dir);
				start.z = t.zAt(start.x, start.y);
				paths.push_back(chainPath(pieces, branch, start));
				if (notes) notes->push_back("a junction: one road joins another");
				again = true;
			}
		}
	};
	for (;;) {
		// The next main road: the longest chain through the biggest piece left.
		std::optional<std::size_t> start;
		for (std::size_t i = 0; i < count; ++i) {
			if (!used[i] && (!start || weight(i) > weight(*start))) start = i;
		}
		if (!start) break;
		auto chain = longestFrom(*start);
		chain = longestFrom(chain.back());
		for (const auto c : chain) used[c] = true;
		paths.push_back(chainPath(pieces, chain, std::nullopt));
		branchOut();
	}
	std::erase_if(paths, [](const RoadPath& p) { return p.points.size() < 2; });
	return paths;
}

// --- Planning ------------------------------------------------------------------------------

RoadBuildPlan planRoadBuild(const MapDocument& doc, MapDocument& ids, const std::vector<RoadPath>& paths, const RoadBuildOptions& options,
                            const TextureSizeFn& textureSize, const std::vector<SolidRef>& replace) {
	RoadBuildPlan plan;
	RoadBuildOptions o = options;
	o.width = std::max(16.0, o.width);
	o.thickness = std::max(1.0, o.thickness);
	o.repeatsAcross = std::max(1, o.repeatsAcross);
	o.power = std::clamp(o.power, 2, 3);
	o.kerbWidth = std::max(1.0, o.kerbWidth);
	o.kerbHeight = std::max(1.0, o.kerbHeight);
	if (o.displacement && o.innerRadius < 8) {
		o.innerRadius = 8;
		plan.notes.push_back("curves keep an inner radius of 8 units: a displacement needs four corners");
	}
	plan.material = o.material.empty() ? std::string("CONCRETE/CONCRETEFLOOR001A") : o.material;
	const std::string sideMaterial = o.sideMaterial.empty() ? plan.material : o.sideMaterial;
	const std::string kerbMaterial = o.kerbMaterial.empty() ? sideMaterial : o.kerbMaterial;
	const double hw = o.width / 2;

	// One mapping for the whole road: the texture spans its width.
	Mapping road;
	{
		const auto [w, h] = textureSize ? textureSize(plan.material) : std::pair<int, int>{512, 512};
		const int tw = std::max(1, w), th = std::max(1, h);
		road.alongV = o.along == RoadAlong::V || (o.along == RoadAlong::Auto && th >= tw);
		road.texAcross = road.alongV ? tw : th;
		road.scale = o.width / (road.texAcross * o.repeatsAcross);
		road.flip = o.flip;
	}
	Mapping kerb;
	kerb.flip = o.flip;

	// Neighbours: everything near the paths.
	Bounds around;
	for (const auto& path : paths) {
		for (const auto& p : path.points) around.add(p);
	}
	if (around.empty()) return plan;
	const double reach = hw + (o.kerbs ? o.kerbWidth : 0) + std::max(0.0, o.innerRadius) + 64;
	around.min = around.min - Vec3{reach, reach, 2 * o.thickness + 32};
	around.max = around.max + Vec3{reach, reach, o.clearance + o.kerbHeight + 32};
	std::vector<SolidRef> skip = replace;
	const auto near = solidsNear(doc, around, 1, skip);

	// Old roads: neighbours with the road material on top.
	struct OldRoad {
		SolidRef ref;
		std::vector<Piece> pieces;
		bool modified = false;
	};
	std::vector<OldRoad> oldRoads;
	std::vector<const NearbySolid*> others;
	for (const auto& n : near) {
		const Side* top = topSide(*n.solid);
		if (top && sameMaterial(top->material, plan.material)) {
			Piece piece;
			piece.solid = *n.solid;
			piece.topZ = n.bounds.max.z;
			oldRoads.push_back({n.ref, {std::move(piece)}});
		} else {
			others.push_back(&n);
		}
	}

	std::vector<Piece> roads, kerbs, seals;
	for (const auto& path : paths) {
		const PathLayout layout = layoutPath(path, o, plan);
		if (layout.stations.size() < 2) continue;
		std::vector<Vec3> line;
		for (const auto& s : layout.stations) line.push_back(s.c);
		plan.centreLines.push_back(std::move(line));

		std::vector<Piece> newRoads, newKerbs, newSeals;
		const std::size_t lastStation = layout.stations.size() - 1;
		for (const auto& span : layout.spans) {
			const Station &s0 = layout.stations[span.a], &s1 = layout.stations[span.a + 1];
			const Vec3 leftAxis = leftOf(flat(s1.c - s0.c));
			// Internal lines (faces on them are hidden): the stations inside the path, and a diagonal.
			const auto classify = [&](Piece& piece, const std::string& topMaterial, const Mapping* mapping, bool centred, const std::string& sides) {
				const auto geometry = buildBrushGeometry(piece.solid);
				for (const auto& face : geometry.faces) {
					Side& side = piece.solid.sides[std::size_t(face.sideIndex)];
					const Plane plane = side.plane();
					if (plane.normal.z > 0.2) {
						side.material = topMaterial;
						if (mapping) {
							mapTop(side, *mapping, s0.c, s1.c, s0.s, centred, (dot(s0.left, leftAxis) + dot(s1.left, leftAxis)) / 2);
						} else {
							alignToWorld(side);
						}
						continue;
					}
					if (plane.normal.z < -0.2) {
						side.material = NODRAW;
						alignToWorld(side);
						continue;
					}
					bool inside = false;
					// A station's line, unless it is an end of the path.
					const auto stationLine = [&](const Station& s) {
						return onPlane(plane, s.c - s.left * hw, s.c + s.left * hw);
					};
					if (span.a > 0 && stationLine(s0)) inside = true;
					if (span.a + 1 < lastStation && stationLine(s1)) inside = true;
					if (inside) {
						side.material = NODRAW;
						alignToWorld(side);
					} else {
						side.material = sides;
						alignToFace(side);
					}
				}
			};
			const bool oneTop = o.displacement;
			// The road.
			const double lo0 = -hw, hi0 = hw;
			auto made = stripPieces(ids, s0, s1, lo0, hi0, lo0, hi0, 0, o.thickness, oneTop);
			const bool split = made.size() > 1;
			const Vec3 diagA = split ? made.front().top[0] : Vec3{}, diagB = split ? made.front().top[2] : Vec3{};
			for (auto& piece : made) {
				piece.kind = Piece::Kind::Road;
				piece.arc = span.arc;
				piece.leftAxis = leftAxis;
				classify(piece, plan.material, &road, true, sideMaterial);
				if (split) {
					// The face along the diagonal between the two halves is inside.
					for (auto& side : piece.solid.sides) {
						const Plane plane = side.plane();
						if (std::abs(plane.normal.z) < 0.2 && onPlane(plane, diagA, diagB)) {
							side.material = NODRAW;
						}
					}
				}
				newRoads.push_back(std::move(piece));
			}
			// Seals under displacements (displacements do not seal the map).
			if (o.displacement) {
				for (auto& piece : stripPieces(ids, s0, s1, lo0, hi0, lo0, hi0, -o.thickness, o.thickness, oneTop)) {
					piece.kind = Piece::Kind::Seal;
					for (auto& side : piece.solid.sides) side.material = NODRAW;
					newSeals.push_back(std::move(piece));
				}
			}
			// Kerbs on both sides (on a curve's inside never past its middle).
			if (o.kerbs) {
				for (const double sideSign : {1.0, -1.0}) {
					double inner = hw, outer = hw + o.kerbWidth;
					if (span.arc) {
						const Vec3 mid = (s0.c + s1.c) * 0.5;
						const bool insideOfCurve = dot(leftAxis * sideSign, flat(span.centre - mid)) > 0;
						if (insideOfCurve) {
							const double room = span.radius - hw;
							if (room < std::min(o.kerbWidth, 4.0)) continue;   // no room on a tight curve's inside
							outer = hw + std::min(o.kerbWidth, room);
						}
					}
					const double a = sideSign * inner, b = sideSign * outer;
					const double lo = std::min(a, b), hi = std::max(a, b);
					auto kerbsMade = stripPieces(ids, s0, s1, lo, hi, lo, hi, o.kerbHeight, o.thickness + o.kerbHeight, false);
					const bool kerbSplit = kerbsMade.size() > 1;
					const Vec3 kerbA = kerbSplit ? kerbsMade.front().top[0] : Vec3{}, kerbB = kerbSplit ? kerbsMade.front().top[2] : Vec3{};
					for (auto& piece : kerbsMade) {
						piece.kind = Piece::Kind::Kerb;
						piece.arc = span.arc;
						piece.leftAxis = leftAxis;
						classify(piece, kerbMaterial, &kerb, false, kerbMaterial);
						if (kerbSplit) {
							for (auto& side : piece.solid.sides) {
								const Plane plane = side.plane();
								if (std::abs(plane.normal.z) < 0.2 && onPlane(plane, kerbA, kerbB)) side.material = NODRAW;
							}
						}
						newKerbs.push_back(std::move(piece));
					}
				}
			}
		}

		// Junctions: an old road (or a road built just before) at the same height that the new
		// one reaches: the new road stops at its edge.
		const auto meets = [&](const Piece& piece, const Solid& old) {
			const Bounds a = buildBrushGeometry(piece.solid).bounds, b = buildBrushGeometry(old).bounds;
			if (a.max.x <= b.min.x + 0.01 || b.max.x <= a.min.x + 0.01 || a.max.y <= b.min.y + 0.01 || b.max.y <= a.min.y + 0.01) return false;
			if (std::abs(b.max.z - piece.topZ) > 8) return false;
			const auto column = columnOver(ids, piece.solid, piece.topZ + 4);
			return column && sharedVolume(*column, old) > 0.5;
		};
		const auto meet = [&](std::vector<Piece>& list, bool* modified) {
			for (std::size_t i = 0; i < list.size(); ++i) {
				const Piece* straight = nullptr;
				bool met = false;
				for (const auto& piece : newRoads) {
					if (!meets(piece, list[i].solid)) continue;
					met = true;
					if (!piece.arc && piece.top.size() == 4 && !straight) straight = &piece;
				}
				if (!met) continue;
				++plan.junctions;
				const Piece oldPiece = list[i];
				const Solid& old = oldPiece.solid;
				const Piece* cutAlong = straight;
				std::optional<Piece> along;
				if (cutAlong) along = *cutAlong;   // the pieces below are replaced
				// Nothing of the new road over the old one's footprint, at any height (its seal
				// below, its kerbs above).
				std::optional<Solid> footprint;
				{
					const Bounds b = buildBrushGeometry(old).bounds;
					std::vector<Vec3> points;
					for (const auto& v : solidCorners(old)) {
						points.push_back({v.x, v.y, b.min.z - 2 * o.thickness - 64});
						points.push_back({v.x, v.y, b.max.z + o.kerbHeight + 64});
					}
					footprint = solidFromPoints(ids, points, NODRAW);
				}
				const Solid& cutter = footprint ? *footprint : old;
				for (auto* pieces : {&newRoads, &newKerbs, &newSeals}) {
					std::vector<Piece> next;
					for (const auto& piece : *pieces) {
						for (auto& part : carvePiece(ids, piece, cutter)) next.push_back(std::move(part));
					}
					*pieces = std::move(next);
				}
				// A junction material: the old road's piece in the crossing gets it (not a
				// displacement: cutting it would break its grid).
				const bool displaced = std::any_of(old.sides.begin(), old.sides.end(), [](const Side& side) { return side.dispInfo(); });
				if (!o.junctionMaterial.empty() && along && displaced) {
					plan.notes.push_back("a displacement road piece at a junction keeps its material (cutting it would break the displacement)");
				}
				if (!o.junctionMaterial.empty() && along && !displaced) {
					const Vec3 left = along->leftAxis;
					const Vec3 lp = along->top[3], rp = along->top[0];
					Side cap;
					cap.material = NODRAW;
					alignToWorld(cap);
					std::vector<Solid> parts;
					auto [outLeft, rest] = splitSolid(ids, old, Plane{left, dot(left, lp)}, cap);
					std::optional<Solid> middle;
					if (rest) {
						auto [outRight, mid] = splitSolid(ids, *rest, Plane{-left, dot(-left, rp)}, cap);
						if (outRight) parts.push_back(std::move(*outRight));
						middle = std::move(mid);
					}
					if (outLeft) parts.push_back(std::move(*outLeft));
					if (middle && (outLeft || parts.size() > 0)) {
						for (auto& side : middle->sides) {
							if (side.plane().normal.z > 0.2) {
								side.material = o.junctionMaterial;
								alignToWorld(side);
							}
						}
						// The piece keeping the old id first.
						parts.insert(parts.begin(), std::move(*middle));
						bool fine = std::all_of(parts.begin(), parts.end(), [](const Solid& s) { return brushProblem(s).empty(); });
						if (fine) {
							for (std::size_t k = 0; k < parts.size(); ++k) {
								if (k == 0) {
									parts[k].id = old.id;
								} else if (parts[k].id == old.id) {
									parts[k].id = ids.newSolidId();
								}
							}
							std::vector<Piece> replaced;
							for (auto& part : parts) {
								Piece p = oldPiece;
								p.solid = std::move(part);
								replaced.push_back(std::move(p));
							}
							list.erase(list.begin() + std::ptrdiff_t(i));
							list.insert(list.begin() + std::ptrdiff_t(i), replaced.begin(), replaced.end());
							i += parts.size() - 1;
							if (modified) *modified = true;
						}
					}
				}
			}
		};
		for (auto& old : oldRoads) meet(old.pieces, &old.modified);
		meet(roads, nullptr);
		// Kerbs built before give way to this road.
		for (const auto& piece : newRoads) {
			std::vector<Piece> next;
			for (const auto& k : kerbs) {
				if (!boxesOverlap(buildBrushGeometry(k.solid).bounds, buildBrushGeometry(piece.solid).bounds)) {
					next.push_back(k);
					continue;
				}
				const auto column = columnOver(ids, piece.solid, piece.topZ + o.clearance);
				if (!column) {
					next.push_back(k);
					continue;
				}
				for (auto& part : carvePiece(ids, k, *column)) next.push_back(std::move(part));
			}
			kerbs = std::move(next);
		}
		for (auto& p : newRoads) roads.push_back(std::move(p));
		for (auto& p : newKerbs) kerbs.push_back(std::move(p));
		for (auto& p : newSeals) seals.push_back(std::move(p));
	}

	// Columns over everything built: what neighbours (and the replaced floor) give way to.
	std::vector<Solid> columns;
	for (const auto* list : {&roads, &kerbs, &seals}) {
		for (const auto& piece : *list) {
			const double top = (piece.kind == Piece::Kind::Kerb ? piece.topZ : piece.topZ + (piece.kind == Piece::Kind::Seal ? o.thickness : 0)) + o.clearance;
			if (auto column = columnOver(ids, piece.solid, top)) columns.push_back(std::move(*column));
		}
	}
	const auto carveByColumns = [&](const Solid& solid, std::vector<Solid>& pieces) {
		pieces = {solid};
		const Bounds box = buildBrushGeometry(solid).bounds;
		bool changed = false;
		for (const auto& column : columns) {
			if (!boxesOverlap(box, buildBrushGeometry(column).bounds)) continue;
			std::vector<Solid> next;
			for (const auto& piece : pieces) {
				auto parts = subtractSolid(ids, piece, column);
				if (!(parts.size() == 1 && parts.front().id == piece.id && parts.front().sides.size() == piece.sides.size())) changed = true;
				for (auto& part : parts) {
					if (solidVolume(part) >= 1.0) next.push_back(std::move(part));
				}
			}
			pieces = std::move(next);
			if (pieces.empty()) break;
		}
		return changed;
	};
	const auto keepId = [&](const SolidRef& ref, std::vector<Solid>& pieces) {
		if (pieces.empty()) return;
		std::stable_partition(pieces.begin(), pieces.end(), [&](const Solid& s) { return s.id == ref.solidId; });
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			if (i == 0) {
				pieces[i].id = ref.solidId;
			} else if (pieces[i].id == ref.solidId) {
				pieces[i].id = ids.newSolidId();
			}
		}
	};

	// Old roads that were split.
	for (auto& old : oldRoads) {
		if (!old.modified) continue;
		std::vector<Solid> pieces;
		for (auto& piece : old.pieces) pieces.push_back(std::move(piece.solid));
		plan.changed.push_back({old.ref, std::move(pieces)});
	}
	// Fit: neighbours the road reaches into.
	int unfit = 0;
	if (o.fit) {
		for (const NearbySolid* n : others) {
			if (n->ref.entityId != 0) {
				const Entity* entity = doc.findEntity(n->ref.entityId);
				if (!entity || entity->classname() != "func_detail") continue;
			}
			if (std::all_of(n->solid->sides.begin(), n->solid->sides.end(), [](const Side& s) { return isToolMaterial(s.material); })) continue;
			if (n->solid->sides.end() != std::find_if(n->solid->sides.begin(), n->solid->sides.end(), [](const Side& s) { return s.dispInfo(); })) continue;
			std::vector<Solid> pieces;
			if (!carveByColumns(*n->solid, pieces)) continue;
			tidyCarved(ids, *n->solid, pieces, mainMaterial(*n->solid));
			if (!std::all_of(pieces.begin(), pieces.end(), [](const Solid& s) { return brushProblem(s).empty(); })) {
				++unfit;
				continue;
			}
			keepId(n->ref, pieces);
			plan.changed.push_back({n->ref, std::move(pieces)});
			++plan.carved;
		}
	}
	if (unfit > 0) plan.notes.push_back(std::to_string(unfit) + " brush(es) beside the road were not carved (it would leave a sliver): check them");
	// The ground beside the road: the top material most floors near the road's height have.
	std::string groundMaterial;
	{
		std::map<std::string, int> count;
		double roadTop = 0;
		int tops = 0;
		for (const auto& piece : roads) {
			roadTop += piece.topZ;
			++tops;
		}
		if (tops > 0) roadTop /= tops;
		for (const NearbySolid* n : others) {
			const Side* top = topSide(*n->solid);
			if (top && !isToolMaterial(top->material) && std::abs(n->bounds.max.z - roadTop) < 32) ++count[top->material];
		}
		int most = 0;
		for (const auto& [material, n] : count) {
			if (n > most) {
				most = n;
				groundMaterial = material;
			}
		}
		if (groundMaterial.empty() && o.kerbs) groundMaterial = kerbMaterial;
	}
	// The selected floor the road is built from: what is left of it stays (fill).
	for (const auto& ref : replace) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		std::vector<Solid> pieces;
		carveByColumns(*solid, pieces);
		tidyCarved(ids, *solid, pieces, mainMaterial(*solid));
		std::erase_if(pieces, [](const Solid& s) { return !brushProblem(s).empty(); });
		// What is left beside a curve was road: it gets the fill material, else the ground's.
		const std::string fill = !o.fillMaterial.empty() ? o.fillMaterial : groundMaterial;
		for (auto& piece : pieces) {
			for (auto& side : piece.sides) {
				if (side.plane().normal.z > 0.7 && (!o.fillMaterial.empty() || sameMaterial(side.material, plan.material)) && !fill.empty()) {
					side.material = fill;
					alignToWorld(side);
				}
			}
		}
		keepId(ref, pieces);
		plan.changed.push_back({ref, std::move(pieces)});
	}

	// Displacements: every four-cornered road top, bent onto the road's true surface.
	int flatTops = 0;
	if (o.displacement) {
		for (auto& piece : roads) {
			const auto geometry = buildBrushGeometry(piece.solid);
			const FaceGeometry* top = nullptr;
			for (const auto& face : geometry.faces) {
				if (face.normal.z > 0.2 && (!top || face.normal.z > top->normal.z)) top = &face;
			}
			if (!top) continue;
			auto disp = top->vertices.size() == 4 ? makeDisplacement(*top, o.power) : std::nullopt;
			if (!disp) {
				++flatTops;
				continue;
			}
			const auto base = displacementBase(*disp, *top);
			for (std::size_t i = 0; i < base.size(); ++i) {
				const auto [z, across] = topAt(piece, base[i].x, base[i].y);
				const Vec3 target{base[i].x, base[i].y, z + roadCrownOffset(across, hw, o.crown, o.roughness, base[i].x, base[i].y)};
				disp->setVector(i, target - base[i]);
			}
			updateTriangleTags(*disp, *top);
			storeDisplacement(piece.solid.sides[std::size_t(top->sideIndex)], *disp);
			++plan.displacements;
		}
		if (flatTops > 0) plan.notes.push_back(std::to_string(flatTops) + " road piece(s) stay flat: cut at a junction or a curve's tip, they do not have four corners");
	}

	// Never anything broken: pieces with a problem are left out.
	for (auto* list : {&roads, &kerbs, &seals}) {
		for (const auto& piece : *list) {
			if (!brushProblem(piece.solid).empty()) ++plan.skipped;
		}
		std::erase_if(*list, [](const Piece& p) { return !brushProblem(p.solid).empty(); });
	}
	if (plan.skipped > 0) plan.notes.push_back(std::to_string(plan.skipped) + " piece(s) left out: too thin or broken");
	for (auto& piece : roads) plan.road.push_back(std::move(piece.solid));
	plan.seals = int(seals.size());
	for (auto& piece : seals) plan.road.push_back(std::move(piece.solid));
	for (auto& piece : kerbs) plan.kerbs.push_back(std::move(piece.solid));
	return plan;
}

namespace edit {

std::pair<Change, std::vector<SolidRef>> applyRoadBuild(MapDocument& doc, const RoadBuildPlan& plan) {
	Transaction t(doc, "Build road");
	std::vector<SolidRef> refs;
	if (!plan.ok()) return {t.finish(), refs};
	for (const auto& replacement : plan.changed) {
		auto* entity = doc.findEntity(replacement.ref.entityId);
		if (!entity) continue;
		t.touchSolid(replacement.ref);
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == replacement.ref.solidId; });
		for (const auto& piece : replacement.pieces) {
			const SolidRef ref{replacement.ref.entityId, piece.id};
			if (!(ref == replacement.ref)) t.touchSolid(ref);
			entity->solids.push_back(piece);
		}
	}
	for (const auto* list : {&plan.road, &plan.kerbs}) {
		for (const auto& solid : *list) {
			const SolidRef ref{0, solid.id};
			t.touchSolid(ref);
			doc.world.solids.push_back(solid);
			refs.push_back(ref);
		}
	}
	return {t.finish(), refs};
}

} // namespace edit
} // namespace sme
