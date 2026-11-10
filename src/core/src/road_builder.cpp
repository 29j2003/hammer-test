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

/// The part of `solid` inside the convex `cutter` (none if they do not overlap); defined below.
std::optional<Solid> insideOf(MapDocument& ids, const Solid& solid, const Solid& cutter);

/// Whether a live preview's planning was called off (a newer one is wanted).
bool stopped(const RoadBuildOptions& o) {
	return o.cancelled && o.cancelled();
}

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
	std::vector<double> radii;   // each point's own inner radius (below 0: the options')
	for (std::size_t i = 0; i < path.points.size(); ++i) {
		const Vec3& point = path.points[i];
		if (p.empty() || length(flat(point - p.back())) >= 1) {
			p.push_back(point);
			radii.push_back(i < path.innerRadius.size() ? path.innerRadius[i] : -1.0);
		}
	}
	// A turn back on itself has no curve: the point is left out.
	for (bool again = true; again && p.size() > 2;) {
		again = false;
		for (std::size_t i = 1; i + 1 < p.size(); ++i) {
			const Vec3 d0 = normalize(flat(p[i] - p[i - 1])), d1 = normalize(flat(p[i + 1] - p[i]));
			if (dot(d0, d1) < std::cos(170.0 * PI / 180)) {
				plan.notes.push_back("a point where the path turns back on itself was left out");
				p.erase(p.begin() + std::ptrdiff_t(i));
				radii.erase(radii.begin() + std::ptrdiff_t(i));
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
	// The ground's profile (from a street): heights where the slope changes.
	const std::vector<Vec3>& ground = path.profile;
	const auto heightAt = [&](const Vec3& q) {
		double bestD = 1e30, z = q.z;
		for (std::size_t k = 0; k + 1 < ground.size(); ++k) {
			const Vec3 a = ground[k], b = ground[k + 1];
			const Vec3 e = flat(b - a);
			const double ee = dot(e, e);
			const double t = ee > 1e-9 ? std::clamp(dot(flat(q - a), e) / ee, 0.0, 1.0) : 0.0;
			const double d = length(flat(a + e * t - q));
			if (d < bestD) {
				bestD = d;
				z = a.z + (b.z - a.z) * t;
			}
		}
		return z;
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

	// A straight stretch to `c`: a joint wherever the ground's slope changes on the way.
	const auto straightTo = [&](Vec3 c, const Vec3& left) {
		if (!ground.empty() && !out.stations.empty()) {
			const Vec3 a = out.stations.back().c;
			const Vec3 d = flat(c - a);
			const double length2 = length(d);
			if (length2 > 2) {
				const Vec3 along = d / length2;
				std::vector<double> at;
				for (const auto& q : ground) {
					const double t = dot(flat(q - a), along);
					if (t > 1 && t < length2 - 1 && std::abs(cross2(along, flat(q - a))) < 2) at.push_back(t);
				}
				std::sort(at.begin(), at.end());
				for (const double t : at) {
					Vec3 m = a + along * t;
					m.z = heightAt(m);
					addStation(m, leftOf(along), false, {}, 0);
				}
			}
			c.z = heightAt(c);
		}
		addStation(c, left, false, {}, 0);
	};

	{
		Vec3 first = p[0];
		if (!ground.empty()) first.z = heightAt(first);
		addStation(first, leftOf(dir[0]), false, {}, 0);
	}
	for (std::size_t i = 1; i < n; ++i) {
		const double wanted = std::max(0.0, radii[i] >= 0 ? radii[i] : o.innerRadius) + hw;
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
			straightTo(p[i], bisector / c);
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
			c.z = ground.empty() ? z0 + (z1 - z0) * k / pieces : heightAt(c);
			if (k == 0) {
				straightTo(c, leftOf(turned(d0, phi)));
			} else {
				addStation(c, leftOf(turned(d0, phi)), true, centre, radius);
			}
		}
	}
	straightTo(p[n], leftOf(dir[n - 1]));
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

/// Joins touching pieces whose union is one convex brush again (what a carve split needlessly), in
/// passes over all pairs with their boxes kept (no restart after each join: hundreds of pieces stay
/// quick). Above `limit` pieces nothing is joined (valid, only more brushes).
void joinPieces(MapDocument& ids, std::vector<Solid>& pieces, std::size_t limit = 2000) {
	if (pieces.size() < 2 || pieces.size() > limit) return;
	std::vector<Bounds> box;
	box.reserve(pieces.size());
	for (const auto& piece : pieces) box.push_back(buildBrushGeometry(piece).bounds);
	for (bool joined = true; joined;) {
		joined = false;
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			for (std::size_t j = i + 1; j < pieces.size();) {
				if (!boxesOverlap(box[i], box[j], -0.01)) {
					++j;
					continue;
				}
				auto merged = joinTouching(ids, pieces[i], pieces[j]);
				if (!merged || !brushProblem(*merged).empty()) {
					++j;
					continue;
				}
				merged->id = pieces[i].id;
				box[i] = buildBrushGeometry(*merged).bounds;
				pieces[i] = std::move(*merged);
				pieces.erase(pieces.begin() + std::ptrdiff_t(j));
				box.erase(box.begin() + std::ptrdiff_t(j));
				joined = true;
			}
		}
	}
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
	joinPieces(ids, pieces);
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
	// A road narrower than the street (raiseBeyond...): the flat street along the road (road and
	// parking lanes), as tall prisms per span; the floor outside them rises to its neighbour.
	const bool raise = o.raiseBeyondLeft > 0 || o.raiseBeyondRight > 0;
	std::vector<Solid> flatColumns;
	const auto giveUp = [&] {
		RoadBuildPlan none;
		none.notes.push_back("stopped");
		return none;
	};
	for (const auto& path : paths) {
		if (stopped(o)) return giveUp();
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
			if (raise) {
				const double left = o.raiseBeyondLeft > 0 ? o.raiseBeyondLeft : hw, right = o.raiseBeyondRight > 0 ? o.raiseBeyondRight : hw;
				std::vector<Vec3> points;
				for (const auto& c : {s0.c - s0.left * right, s1.c - s1.left * right, s1.c + s1.left * left, s0.c + s0.left * left}) {
					points.push_back({c.x, c.y, std::min(s0.c.z, s1.c.z) - 4 * o.thickness - 256});
					points.push_back({c.x, c.y, std::max(s0.c.z, s1.c.z) + o.clearance + 256});
				}
				if (auto column = solidFromPoints(ids, points, NODRAW)) flatColumns.push_back(std::move(*column));
			}
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
	std::vector<Bounds> columnBox;
	columnBox.reserve(columns.size());
	for (const auto& column : columns) columnBox.push_back(buildBrushGeometry(column).bounds);
	const auto carveByColumns = [&](const Solid& solid, std::vector<Solid>& pieces) {
		pieces = {solid};
		const Bounds box = buildBrushGeometry(solid).bounds;
		bool changed = false;
		for (std::size_t c = 0; c < columns.size(); ++c) {
			const Solid& column = columns[c];
			if (!boxesOverlap(box, columnBox[c])) continue;
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
			if (stopped(o)) return giveUp();
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
	// A road narrower than the street: each piece of the floor left beside it is split into what lies
	// in the flat street (road and lanes: stays) and what lies beyond; that rises to the height of
	// what borders it - a sidewalk's top (the sidewalk reaches the road), before something taller a
	// kerb's height, beside nothing higher it stays flush. A piece that borders only other pieces takes
	// the height of a raised one beside it.
	int raisedPieces = 0;
	std::vector<Bounds> flatBox;
	for (const auto& column : flatColumns) flatBox.push_back(buildBrushGeometry(column).bounds);
	const auto inFlat = [&](const Vec3& p) {
		for (std::size_t c = 0; c < flatColumns.size(); ++c) {
			const Bounds& b = flatBox[c];
			if (p.x < b.min.x || p.x > b.max.x || p.y < b.min.y || p.y > b.max.y) continue;
			if (solidContains(flatColumns[c], p, 0.01)) return true;
		}
		return false;
	};
	const auto raiseBeside = [&](std::vector<Solid>& pieces, const Solid& original) {
		std::vector<Solid> level, beyond;
		// Only the floor's surface rises: a piece whose top lies below the old floor's top (the slab
		// left under the road) stays.
		const Side* originalTop = topSide(original);
		const auto belowSurface = [&](const Solid& piece) {
			if (!originalTop) return false;
			const Side* t = topSide(piece);
			if (!t) return true;
			const auto geometry = buildBrushGeometry(piece);
			double z = 0, n = 0;
			for (const auto& face : geometry.faces) {
				if (&piece.sides[std::size_t(face.sideIndex)] != t) continue;
				for (const auto& v : face.vertices) {
					z += originalTop->plane().distanceTo(v);
					++n;
				}
			}
			return n > 0 && z / n < -0.5;
		};
		for (auto& piece : pieces) {
			if (belowSurface(piece)) {
				level.push_back(std::move(piece));
				continue;
			}
			std::vector<Solid> rest{piece};
			for (std::size_t c = 0; c < flatColumns.size() && !rest.empty(); ++c) {
				std::vector<Solid> next;
				for (auto& r : rest) {
					if (!boxesOverlap(buildBrushGeometry(r).bounds, flatBox[c])) {
						next.push_back(std::move(r));
						continue;
					}
					if (auto in = insideOf(ids, r, flatColumns[c])) level.push_back(std::move(*in));
					for (auto& part : subtractSolid(ids, r, flatColumns[c])) {
						if (solidVolume(part) >= 1.0) next.push_back(std::move(part));
					}
				}
				rest = std::move(next);
			}
			for (auto& r : rest) beyond.push_back(std::move(r));
		}
		joinPieces(ids, level);
		joinPieces(ids, beyond);
		// The rise of each piece beyond: from what borders it outside the street.
		struct Rise {
			double by = 0;
			std::string top, side;
			bool known = false;
		};
		std::vector<Rise> rises(beyond.size());
		std::vector<std::vector<Vec3>> outlines(beyond.size());
		std::vector<double> tops(beyond.size(), 0);
		for (std::size_t i = 0; i < beyond.size(); ++i) {
			const auto geometry = buildBrushGeometry(beyond[i]);
			const FaceGeometry* top = nullptr;
			for (const auto& face : geometry.faces) {
				if (face.normal.z > 0.7 && (!top || face.normal.z > top->normal.z)) top = &face;
			}
			if (!top) continue;
			outlines[i] = top->vertices;
			double z = 0;
			for (const auto& v : top->vertices) z += v.z;
			tops[i] = z / double(top->vertices.size());
			Vec3 middle{};
			for (const auto& v : top->vertices) middle = middle + v;
			middle = middle / double(top->vertices.size());
			const auto around = solidsNear(doc, geometry.bounds, 4, replace);
			double best = 1e30;
			const Solid* bestSolid = nullptr;
			bool taller = false;
			const Solid* tallSolid = nullptr;
			for (std::size_t k = 0; k < top->vertices.size(); ++k) {
				const Vec3 a = top->vertices[k], b = top->vertices[(k + 1) % top->vertices.size()];
				const Vec3 edge = flat(b - a);
				if (length(edge) < 1) continue;
				Vec3 out = normalize(Vec3{edge.y, -edge.x, 0});
				const Vec3 mid = (a + b) * 0.5;
				if (dot(out, flat(mid - middle)) < 0) out = -out;
				for (const double t : {0.25, 0.5, 0.75}) {
					const Vec3 at = a + (b - a) * t + out * 2.0;
					const Vec3 probe{at.x, at.y, at.z + 1};
					if (inFlat(probe)) continue;   // towards the road: not a neighbour
					for (const auto& n : around) {
						if (std::all_of(n.solid->sides.begin(), n.solid->sides.end(), [](const Side& side) { return isToolMaterial(side.material); })) continue;
						if (!solidContains(*n.solid, probe, 0.05)) continue;
						// Its top right there (a sidewalk beside a ramp slopes too).
						double neighbourTop = n.bounds.max.z;
						if (const Side* t = topSide(*n.solid)) {
							const Plane tp = t->plane();
							if (tp.normal.z > 0.7) neighbourTop = (tp.dist - tp.normal.x * probe.x - tp.normal.y * probe.y) / tp.normal.z;
						}
						const double rise = neighbourTop - at.z;
						if (rise < 1) continue;
						if (rise <= 64) {
							if (rise < best) {
								best = rise;
								bestSolid = n.solid;
							}
						} else {
							taller = true;
							tallSolid = n.solid;
						}
					}
				}
			}
			if (bestSolid) {
				rises[i] = {best, mainMaterial(*bestSolid), mainMaterial(*bestSolid), true};
				if (const Side* t = topSide(*bestSolid)) rises[i].top = t->material;
			} else if (taller) {
				rises[i] = {o.kerbHeight, mainMaterial(beyond[i]), mainMaterial(*tallSolid), true};
				if (const Side* t = topSide(beyond[i])) rises[i].top = t->material;
			}
		}
		// Pieces that border only other pieces: the rise of a raised piece they touch.
		for (int round = 0; round < 8; ++round) {
			bool changed = false;
			for (std::size_t i = 0; i < beyond.size(); ++i) {
				if (rises[i].known || outlines[i].empty()) continue;
				for (std::size_t j = 0; j < beyond.size() && !rises[i].known; ++j) {
					if (i == j || !rises[j].known || !boxesOverlap(buildBrushGeometry(beyond[i]).bounds, buildBrushGeometry(beyond[j]).bounds, -0.5)) continue;
					rises[i] = rises[j];
					changed = true;
				}
			}
			if (!changed) break;
		}
		for (std::size_t i = 0; i < beyond.size(); ++i) {
			if (!rises[i].known || rises[i].by < 0.5) {
				level.push_back(std::move(beyond[i]));
				continue;
			}
			// The top corners go up by the rise; the faces get the neighbour's look.
			std::vector<Vec3> corners = solidCorners(beyond[i]);
			for (auto& c : corners) {
				if (std::any_of(outlines[i].begin(), outlines[i].end(), [&](const Vec3& v) { return length(v - c) < 0.05; })) c.z += rises[i].by;
			}
			auto raised = solidFromPoints(ids, corners, rises[i].side.empty() ? NODRAW : rises[i].side);
			if (!raised || !brushProblem(*raised).empty()) {
				level.push_back(std::move(beyond[i]));
				continue;
			}
			for (auto& side : raised->sides) {
				const Plane plane = side.plane();
				if (plane.normal.z > 0.7) {
					side.material = rises[i].top.empty() ? side.material : rises[i].top;
				} else if (plane.normal.z < -0.7) {
					side.material = NODRAW;
				} else if (o.raisedEdgeKerb) {
					// Facing the road: the kerb.
					const auto faces = buildBrushGeometry(*raised).faces;
					for (const auto& face : faces) {
						if (&raised->sides[std::size_t(face.sideIndex)] != &side || face.vertices.empty()) continue;
						Vec3 centre{};
						for (const auto& v : face.vertices) centre = centre + v;
						centre = centre / double(face.vertices.size());
						if (inFlat(centre + plane.normal * 2.0)) side.material = o.kerbMaterial.empty() ? side.material : o.kerbMaterial;
					}
				}
				alignToWorld(side);
			}
			raised->id = beyond[i].id;
			level.push_back(std::move(*raised));
			++raisedPieces;
		}
		pieces = std::move(level);
	};

	// The selected floor the road is built from: what is left of it stays (fill).
	double lostFloor = 0;   // square units of floor in slivers that joined nothing
	int lostSlivers = 0;
	// A sliver the cuts left (too thin for a brush) joins a neighbouring piece instead of leaving a
	// hole; only what joins nothing is left out (and said).
	const auto absorbSlivers = [&](std::vector<Solid>& pieces) {
		std::vector<Solid> good, bad;
		for (auto& piece : pieces) (brushProblem(piece).empty() ? good : bad).push_back(std::move(piece));
		for (auto& sliver : bad) {
			bool joined = false;
			const Bounds sb = buildBrushGeometry(sliver).bounds;
			for (auto& piece : good) {
				if (!boxesOverlap(sb, buildBrushGeometry(piece).bounds, -0.05)) continue;
				auto merged = joinTouching(ids, piece, sliver);
				if (!merged || !brushProblem(*merged).empty()) continue;
				merged->id = piece.id;
				piece = std::move(*merged);
				joined = true;
				break;
			}
			if (!joined) {
				lostFloor += solidVolume(sliver) / std::max(1.0, o.thickness);
				++lostSlivers;
			}
		}
		pieces = std::move(good);
	};
	for (const auto& ref : replace) {
		if (stopped(o)) return giveUp();
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		std::vector<Solid> pieces;
		carveByColumns(*solid, pieces);
		tidyCarved(ids, *solid, pieces, mainMaterial(*solid));
		absorbSlivers(pieces);
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
		if (raise && !flatColumns.empty()) {
			raiseBeside(pieces, *solid);
			absorbSlivers(pieces);
		}
		keepId(ref, pieces);
		plan.changed.push_back({ref, std::move(pieces)});
	}
	if (raisedPieces > 0) {
		plan.notes.push_back(std::to_string(raisedPieces) + " piece(s) of the floor beside the road rise to the sidewalk (the road is narrower than the street)");
	}

	if (lostSlivers > 0) {
		plan.notes.push_back(std::to_string(lostSlivers) + " sliver(s) of the floor (about " + units(lostFloor) +
		                     " square units) were too thin for a brush and are left out: check beside the road");
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

namespace {

/// A brush's footprint seen from above: its corners' convex outline, counter-clockwise.
std::vector<Vec3> footprint(const Solid& solid) {
	std::vector<Vec3> points;
	for (const auto& c : solidCorners(solid)) {
		const Vec3 p{c.x, c.y, 0};
		if (std::none_of(points.begin(), points.end(), [&](const Vec3& q) { return length(q - p) < 0.05; })) points.push_back(p);
	}
	if (points.size() < 3) return {};
	// Monotone chain.
	std::sort(points.begin(), points.end(), [](const Vec3& a, const Vec3& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
	std::vector<Vec3> hull(points.size() * 2);
	std::size_t k = 0;
	for (std::size_t i = 0; i < points.size(); ++i) {
		while (k >= 2 && cross2(hull[k - 1] - hull[k - 2], points[i] - hull[k - 2]) <= 1e-9) --k;
		hull[k++] = points[i];
	}
	for (std::size_t i = points.size() - 1, t = k + 1; i > 0; --i) {
		while (k >= t && cross2(hull[k - 1] - hull[k - 2], points[i - 1] - hull[k - 2]) <= 1e-9) --k;
		hull[k++] = points[i - 1];
	}
	hull.resize(k > 0 ? k - 1 : 0);
	return hull;
}

/// The outline moved outward by `by` (mitred corners, a sharp corner bevelled so it never reaches
/// further than three times `by`).
std::vector<Vec3> grown(const std::vector<Vec3>& outline, double by) {
	std::vector<Vec3> out;
	const std::size_t n = outline.size();
	for (std::size_t i = 0; i < n; ++i) {
		const Vec3 a = outline[(i + n - 1) % n], b = outline[i], c = outline[(i + 1) % n];
		const Vec3 d0 = normalize(b - a), d1 = normalize(c - b);
		const Vec3 n0{d0.y, -d0.x, 0}, n1{d1.y, -d1.x, 0};   // outward (counter-clockwise outline)
		const Vec3 mid = normalize(n0 + n1);
		const double cosHalf = dot(mid, n0);
		if (cosHalf > 0.34) {
			out.push_back(b + mid * (by / cosHalf));
		} else {
			out.push_back(b + n0 * by);
			out.push_back(b + n1 * by);
		}
	}
	return out;
}

/// An upright prism over an outline from `low` to `high`.
std::optional<Solid> prism(MapDocument& ids, const std::vector<Vec3>& outline, double low, double high, const std::string& material) {
	std::vector<Vec3> points;
	for (const auto& p : outline) {
		points.push_back({p.x, p.y, low});
		points.push_back({p.x, p.y, high});
	}
	return solidFromPoints(ids, points, material);
}

/// The part of `solid` inside the convex `cutter` (none if they do not overlap).
std::optional<Solid> insideOf(MapDocument& ids, const Solid& solid, const Solid& cutter) {
	std::optional<Solid> part = solid;
	for (const auto& side : cutter.sides) {
		if (!part) break;
		auto [front, back] = splitSolid(ids, *part, side.plane(), side);
		part = std::move(back);
	}
	if (part && solidVolume(*part) < 1.0) return std::nullopt;
	return part;
}

} // namespace

std::vector<std::string> roadMaterialSuggestions(const std::vector<std::string>& names, RoadMaterialRole role, std::size_t count) {
	const auto has = [](const std::string& name, std::initializer_list<const char*> words) {
		return std::any_of(words.begin(), words.end(), [&](const char* w) { return name.find(w) != std::string::npos; });
	};
	std::vector<std::pair<int, std::string>> scored;
	for (const auto& original : names) {
		std::string name = original;
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		std::replace(name.begin(), name.end(), '\\', '/');
		if (name.rfind("tools/", 0) == 0 || name.rfind("decals/", 0) == 0 || name.rfind("overlays/", 0) == 0 || name.rfind("dev/", 0) == 0) continue;
		if (has(name, {"blend", "sign", "cone", "barrier", "decal", "overlay", "puddle"})) continue;
		// The file's own name tells lines and roads (a folder "road/" also holds plain asphalt).
		const std::string base = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
		const bool lined = has(base, {"line", "stripe", "lane", "marking", "crosswalk"});
		const bool roadish = has(base, {"road", "street", "highway"});
		const bool roadFolder = has(name.substr(0, name.size() - base.size()), {"road", "street", "highway"});
		const bool plain = has(name, {"asphalt", "tarmac", "concrete", "pavement", "parking"});
		int score = 0;
		switch (role) {
			case RoadMaterialRole::Road:
				score = (roadish ? 3 : 0) + (lined ? 2 : 0) + (roadFolder ? 1 : 0) + (has(name, {"asphalt", "tarmac"}) ? 1 : 0);
				if (!roadish && !lined && !roadFolder && !has(name, {"asphalt", "tarmac"})) score = 0;
				break;
			case RoadMaterialRole::Parking:
			case RoadMaterialRole::Junction:
				if (lined || roadish) break;
				score = (has(name, {"asphalt", "tarmac"}) ? 3 : 0) + (has(name, {"parking"}) ? 2 : 0) + (has(name, {"concrete", "pavement"}) ? 1 : 0);
				break;
			case RoadMaterialRole::Kerb:
				if (lined) break;
				score = (has(name, {"curb", "kerb"}) ? 4 : 0) + (has(name, {"sidewalk", "pavement"}) ? 2 : 0) + (has(name, {"concrete"}) ? 1 : 0);
				break;
			case RoadMaterialRole::Side:
				if (lined) break;
				score = (has(name, {"curb", "kerb", "concrete"}) ? 2 : 0) + (plain ? 1 : 0);
				break;
		}
		if (score > 0) scored.emplace_back(score, original);
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
	std::vector<std::string> out;
	for (const auto& [score, name] : scored) {
		if (out.size() >= count) break;
		out.push_back(name);
	}
	return out;
}

void addStreetKerbs(const MapDocument& doc, MapDocument& ids, RoadBuildPlan& plan, const RoadCorridor& corridor, const RoadBuildOptions& options) {
	if (!plan.ok() || corridor.floor.empty()) return;
	const double top = corridor.top;
	const double kerbWidth = std::max(1.0, options.kerbWidth);
	const double kerbHeight = std::max(1.0, options.kerbHeight);
	const std::string material = !options.kerbMaterial.empty() ? options.kerbMaterial : (!options.sideMaterial.empty() ? options.sideMaterial : plan.material);
	const auto isFloor = [&](const SolidRef& ref) { return std::find(corridor.floor.begin(), corridor.floor.end(), ref) != corridor.floor.end(); };

	// The floor left beside the road (the parking lanes): the replaced selection's pieces.
	std::vector<RoadBuildPlan::Replacement*> floor;
	Bounds street;
	for (auto& r : plan.changed) {
		if (!isFloor(r.ref)) continue;
		floor.push_back(&r);
		for (const auto& piece : r.pieces) street.add(buildBrushGeometry(piece).bounds);
	}
	if (floor.empty() || street.empty()) return;
	// The street's surface may slope (a ramp): heights from the old floor brushes' tops.
	double lowest = top, highest = top;
	for (auto* r : floor) {
		if (const Solid* original = doc.findSolid(r->ref)) {
			const auto b = buildBrushGeometry(*original).bounds;
			lowest = std::min(lowest, b.max.z);
			highest = std::max(highest, b.max.z);
			if (const Side* t = topSide(*original)) {
				for (const auto& v : solidCorners(*original)) {
					if (std::abs(t->plane().distanceTo(v)) < 0.05) lowest = std::min(lowest, v.z);
				}
			}
		}
	}
	const auto heightOf = [](const Solid& solid, double x, double y, double fallback) {
		if (const Side* t = topSide(solid)) {
			const Plane p = t->plane();
			if (p.normal.z > 0.7) return (p.dist - p.normal.x * x - p.normal.y * y) / p.normal.z;
		}
		return fallback;
	};

	// Everything raised that borders the street: brushes of the world and func_detail that stand on
	// or beside it (their bottom at most the street's top) and rise above it. Carved neighbours count
	// as the plan leaves them.
	struct Raised {
		Solid solid;
		double rise;
	};
	std::vector<Raised> raised;
	const auto consider = [&](const Solid& solid) {
		if (std::all_of(solid.sides.begin(), solid.sides.end(), [](const Side& s) { return isToolMaterial(s.material); })) return;
		if (std::any_of(solid.sides.begin(), solid.sides.end(), [](const Side& s) { return s.dispInfo(); })) return;
		const Bounds b = buildBrushGeometry(solid).bounds;
		if (b.empty() || b.min.z > highest + 1 || b.max.z < lowest + 2) return;
		Bounds reach = street;
		reach.min = reach.min - Vec3{kerbWidth + 1, kerbWidth + 1, 0};
		reach.max = reach.max + Vec3{kerbWidth + 1, kerbWidth + 1, 0};
		if (b.max.x < reach.min.x || b.min.x > reach.max.x || b.max.y < reach.min.y || b.min.y > reach.max.y) return;
		raised.push_back({solid, b.max.z - top});
	};
	const auto visit = [&](const Entity& entity, bool world) {
		if (!world && entity.classname() != "func_detail") return;
		for (const auto& solid : entity.solids) {
			const SolidRef ref{world ? 0 : entity.id, solid.id};
			if (isFloor(ref)) continue;
			const auto changed = std::find_if(plan.changed.begin(), plan.changed.end(), [&](const RoadBuildPlan::Replacement& r) { return r.ref == ref; });
			if (changed != plan.changed.end()) {
				for (const auto& piece : changed->pieces) consider(piece);
			} else {
				consider(solid);
			}
		}
	};
	visit(doc.world, true);
	for (const auto& entity : doc.entities) visit(entity, false);
	// Sidewalks first (a corner where a sidewalk meets a wall keeps the sidewalk's kerb).
	std::stable_sort(raised.begin(), raised.end(), [](const Raised& a, const Raised& b) { return a.rise < b.rise; });

	int made = 0;
	double low = street.min.z - 1;
	for (const auto& r : raised) {
		const auto outline = footprint(r.solid);
		if (outline.size() < 3) continue;
		const auto zone = prism(ids, grown(outline, kerbWidth), low, highest + 1, NODRAW);
		const auto under = prism(ids, outline, low, highest + 1, NODRAW);
		if (!zone || !under) continue;
		const Bounds zoneBox = buildBrushGeometry(*zone).bounds;
		for (auto* replacement : floor) {
			std::vector<Solid> rest;
			bool cut = false;
			const Solid* original = doc.findSolid(replacement->ref);
			const Side* originalTop = original ? topSide(*original) : nullptr;
			for (auto& piece : replacement->pieces) {
				const Bounds pb = buildBrushGeometry(piece).bounds;
				// Only the floor's surface (not the slab left under the road), next to the neighbour.
				const Side* pieceTop = topSide(piece);
				bool surface = pieceTop && originalTop;
				if (surface) {
					for (const auto& v : solidCorners(piece)) {
						if (std::abs(pieceTop->plane().distanceTo(v)) < 0.05 && std::abs(originalTop->plane().distanceTo(v)) > 0.5) surface = false;
					}
				}
				if (!surface || !boxesOverlap(pb, zoneBox)) {
					rest.push_back(std::move(piece));
					continue;
				}
				// How much higher the neighbour is right here: flush with a sidewalk; before a wall (or
				// anything taller) a kerb of its own height. Not higher here: no kerb.
				const Vec3 middle = (pb.min + pb.max) * 0.5;
				const double here = heightOf(piece, middle.x, middle.y, pb.max.z);
				const double rise = heightOf(r.solid, middle.x, middle.y, here + r.rise) - here;
				if (rise < 1) {
					rest.push_back(std::move(piece));
					continue;
				}
				const double lift = rise <= 32 ? rise : kerbHeight;
				const Plane topPlane = pieceTop->plane();
				auto inside = insideOf(ids, piece, *zone);
				if (!inside) {
					rest.push_back(std::move(piece));
					continue;
				}
				// What lies under the neighbour (a wall standing on the floor) stays floor.
				std::vector<Solid> kerbParts = subtractSolid(ids, *inside, *under);
				std::vector<Solid> keep = subtractSolid(ids, piece, *zone);
				if (auto below = insideOf(ids, *inside, *under)) keep.push_back(std::move(*below));
				std::vector<Solid> kerbs;
				bool sound = true;
				for (const auto& part : kerbParts) {
					if (solidVolume(part) < 1.0) continue;
					std::vector<Vec3> corners = solidCorners(part);
					for (auto& c : corners) {
						if (std::abs(topPlane.distanceTo(c)) < 0.05) c.z += lift;
					}
					auto kerb = solidFromPoints(ids, corners, material);
					if (!kerb || !brushProblem(*kerb).empty()) {
						sound = false;
						break;
					}
					for (auto& side : kerb->sides) {
						if (side.plane().normal.z < -0.7) side.material = NODRAW;
					}
					kerbs.push_back(std::move(*kerb));
				}
				std::erase_if(keep, [](const Solid& s) { return solidVolume(s) < 1.0; });
				if (sound) sound = std::all_of(keep.begin(), keep.end(), [](const Solid& s) { return brushProblem(s).empty(); });
				if (!sound || kerbs.empty()) {
					// A strip too thin for a brush (a sliver at a corner): the floor stays as it was there.
					rest.push_back(std::move(piece));
					continue;
				}
				tidyCarved(ids, piece, keep, mainMaterial(piece));
				for (auto& k : keep) rest.push_back(std::move(k));
				for (auto& k : kerbs) plan.kerbs.push_back(std::move(k));
				made += int(kerbs.size());
				cut = true;
			}
			replacement->pieces = std::move(rest);
			if (cut) {
				// The first piece keeps the brush's id.
				auto& pieces = replacement->pieces;
				std::stable_partition(pieces.begin(), pieces.end(), [&](const Solid& s) { return s.id == replacement->ref.solidId; });
				for (std::size_t i = 0; i < pieces.size(); ++i) {
					if (i == 0) pieces[i].id = replacement->ref.solidId;
					else if (pieces[i].id == replacement->ref.solidId) pieces[i].id = ids.newSolidId();
				}
			}
		}
	}
	if (made == 0) plan.notes.push_back("no kerbs: nothing raised (sidewalk or wall) borders the street beside the parking lanes");
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
