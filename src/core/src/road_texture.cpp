// road_texture.cpp - Road tool: chains of floor faces and their texture axes (see road_texture.h).

#include <sme/road_texture.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/edge_round.h>
#include <sme/material.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace sme {

namespace {

constexpr double UP = 0.5;            ///< A road face's normal is at most 60° from straight up
constexpr double ON_LINE = 0.1;       ///< Units: an edge lies on another face's edge
constexpr double MIN_SHARED = 1.0;    ///< Units: shorter common pieces of edges do not link faces
constexpr double ELONGATED = 1.25;    ///< A piece this much longer than wide has a long axis
constexpr double KERB_HEIGHT = 2.0;   ///< Units: a top this much above or below the road beside it is a kerb
constexpr double BESIDE = 2.0;        ///< Units: pieces this close seen from above lie beside each other
constexpr double SMALL_NARROW = 0.25; ///< A piece this much narrower than the one it hangs on is an edge piece
constexpr double SMALL_AREA = 0.15;   ///< ... or this much smaller
const double COS30 = std::cos(30.0 * 3.14159265358979323846 / 180.0);
const double COS45 = std::cos(45.0 * 3.14159265358979323846 / 180.0);
const double COS35 = std::cos(35.0 * 3.14159265358979323846 / 180.0);   ///< From this turn on a corner can curve

/// One road face: its corners (in its plane) and where it is.
struct Piece {
	FaceRef ref;
	std::vector<Vec3> poly;
	Vec3 normal;
	Vec3 centroid;
	Bounds box;
	std::string material;
};

/// Two pieces touch along this common piece of their edges.
struct Link {
	int other = 0;
	Vec3 mid;          ///< Middle of the common piece
	Vec3 dir;          ///< Its direction (unit)
	double length = 0; ///< Its length
};

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

/// The face's long direction (smallest box around it in its plane), how much longer than wide
/// it is, and its length along that direction.
struct LongAxis {
	Vec3 axis{1, 0, 0};
	double aspect = 1;
	double length = 0;
};
LongAxis longAxis(const Piece& piece) {
	LongAxis best;
	double bestArea = std::numeric_limits<double>::max();
	for (std::size_t i = 0; i < piece.poly.size(); ++i) {
		Vec3 e = piece.poly[(i + 1) % piece.poly.size()] - piece.poly[i];
		e = normalize(e - piece.normal * dot(e, piece.normal));
		if (length(e) < 0.5) continue;
		const Vec3 perp = normalize(cross(piece.normal, e));
		double minE = std::numeric_limits<double>::max(), maxE = std::numeric_limits<double>::lowest();
		double minP = minE, maxP = maxE;
		for (const auto& v : piece.poly) {
			minE = std::min(minE, dot(v, e));
			maxE = std::max(maxE, dot(v, e));
			minP = std::min(minP, dot(v, perp));
			maxP = std::max(maxP, dot(v, perp));
		}
		const double a = maxE - minE;
		const double b = maxP - minP;
		if (a * b < bestArea - 1e-6) {
			bestArea = a * b;
			best.axis = a >= b ? e : perp;
			best.aspect = std::max(a, b) / std::max(1e-6, std::min(a, b));
			best.length = std::max(a, b);
		}
	}
	return best;
}

/// Sort key of a point: rounded coordinates (stable against tiny differences).
std::tuple<long long, long long, long long> pointKey(const Vec3& p) {
	return {std::llround(p.x * 100), std::llround(p.y * 100), std::llround(p.z * 100)};
}

/// Collects the road pieces (faces facing up on valid brushes) of `faces`; counts the rest.
std::vector<Piece> collectPieces(const MapDocument& doc, const std::vector<FaceRef>& faces, int& ignored) {
	std::vector<Piece> pieces;
	std::map<std::pair<int, int>, BrushGeometry> geometries;   // by entity / solid id
	for (const auto& ref : faces) {
		const Solid* solid = doc.findSolid(ref.solid);
		if (!solid) {
			++ignored;
			continue;
		}
		auto key = std::make_pair(ref.solid.entityId, ref.solid.solidId);
		auto found = geometries.find(key);
		if (found == geometries.end()) found = geometries.emplace(key, buildBrushGeometry(*solid)).first;
		const BrushGeometry& geometry = found->second;
		const FaceGeometry* face = nullptr;
		if (geometry.valid) {
			for (const auto& f : geometry.faces) {
				if (solid->sides[static_cast<std::size_t>(f.sideIndex)].id == ref.sideId) face = &f;
			}
		}
		const Vec3 normal = face ? normalize(face->normal) : Vec3{};
		if (!face || face->vertices.size() < 3 || normal.z <= UP) {
			++ignored;
			continue;
		}
		Piece piece;
		piece.ref = ref;
		piece.poly = face->vertices;
		piece.normal = normal;
		for (const auto& v : piece.poly) {
			piece.centroid += v;
			piece.box.add(v);
		}
		piece.centroid = piece.centroid / static_cast<double>(piece.poly.size());
		piece.material = solid->sides[static_cast<std::size_t>(face->sideIndex)].material;
		pieces.push_back(std::move(piece));
	}
	// The same face given twice counts once.
	std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
		return std::tie(a.ref.solid.entityId, a.ref.solid.solidId, a.ref.sideId) < std::tie(b.ref.solid.entityId, b.ref.solid.solidId, b.ref.sideId);
	});
	pieces.erase(std::unique(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.ref == b.ref; }), pieces.end());
	// Order by place, so the result does not depend on the order the faces were given in.
	std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
		return std::make_tuple(pointKey(a.centroid), a.ref.sideId) < std::make_tuple(pointKey(b.centroid), b.ref.sideId);
	});
	return pieces;
}

/// Which pieces touch which (along a common piece of edge).
std::vector<std::vector<Link>> linkPieces(const std::vector<Piece>& pieces) {
	std::vector<std::vector<Link>> links(pieces.size());
	for (std::size_t i = 0; i < pieces.size(); ++i) {
		for (std::size_t j = i + 1; j < pieces.size(); ++j) {
			const Bounds& a = pieces[i].box;
			const Bounds& b = pieces[j].box;
			if (a.max.x + ON_LINE < b.min.x || b.max.x + ON_LINE < a.min.x || a.max.y + ON_LINE < b.min.y || b.max.y + ON_LINE < a.min.y ||
			    a.max.z + ON_LINE < b.min.z || b.max.z + ON_LINE < a.min.z) {
				continue;
			}
			if (const auto segment = sharedSegment(pieces[i].poly, pieces[j].poly)) {
				const Vec3 mid = (segment->first + segment->second) * 0.5;
				const Vec3 dir = normalize(segment->second - segment->first);
				const double shared = length(segment->second - segment->first);
				links[i].push_back({static_cast<int>(j), mid, dir, shared});
				links[j].push_back({static_cast<int>(i), mid, dir, shared});
			}
		}
	}
	return links;
}

/// The link from `from` to `to`.
const Link* findLink(const std::vector<std::vector<Link>>& links, int from, int to) {
	for (const auto& link : links[static_cast<std::size_t>(from)]) {
		if (link.other == to) return &link;
	}
	return nullptr;
}

/// `v` in the plane with normal `n`, unit length (zero if it stands on the plane).
Vec3 inPlane(const Vec3& v, const Vec3& n) {
	return normalize(v - n * dot(v, n));
}

/// Texture shift kept small (the texture repeats, so whole texture sizes do not matter).
double wrapShift(double shift, double size) {
	if (size <= 0) return shift;
	double wrapped = std::fmod(shift, size);
	if (wrapped < 0) wrapped += size;
	return std::abs(wrapped - size) < 1e-6 ? 0 : wrapped;
}


/// Area of a flat polygon.
double polygonArea(const std::vector<Vec3>& poly) {
	Vec3 sum;
	for (std::size_t i = 0; i < poly.size(); ++i) sum += cross(poly[i], poly[(i + 1) % poly.size()]);
	return 0.5 * length(sum);
}

/// The height of the piece's plane straight above or below `p` (its own centroid's height when
/// the piece stands upright).
double heightAt(const Piece& piece, const Vec3& p) {
	if (std::abs(piece.normal.z) < 1e-6) return piece.centroid.z;
	const Vec3& o = piece.poly.front();
	return o.z - (piece.normal.x * (p.x - o.x) + piece.normal.y * (p.y - o.y)) / piece.normal.z;
}

/// The two pieces lie side by side seen from above (their boxes touch within `gap` units).
bool besideFromAbove(const Piece& a, const Piece& b, double gap) {
	return a.box.min.x <= b.box.max.x + gap && b.box.min.x <= a.box.max.x + gap && a.box.min.y <= b.box.max.y + gap &&
	       b.box.min.y <= a.box.max.y + gap;
}

/// Two pieces lie in one plane.
bool coplanar(const Piece& a, const Piece& b) {
	return dot(a.normal, b.normal) > 0.9999 && std::abs(dot(b.centroid - a.centroid, a.normal)) < ON_LINE;
}

/// Across a common edge, into the piece: perpendicular to the edge, in the piece's plane.
Vec3 acrossEdge(const Piece& piece, const Link& link) {
	Vec3 d = normalize(cross(piece.normal, link.dir));
	if (dot(d, piece.centroid - link.mid) < 0) d = -d;
	return d;
}

/// Across the piece from the common edge `link` to the edge on its other end (about as long,
/// running about the same way, farthest away): the bisector of both edges' perpendiculars - a
/// strip of a curve runs along its middle, a rectangle straight across, even when the common
/// edge covers only part of its side.
Vec3 throughFrom(const Piece& piece, const Link& link) {
	const Vec3 in = acrossEdge(piece, link);
	const Vec3 perp = normalize(cross(piece.normal, link.dir));
	std::optional<Vec3> other;
	double farthest = 1.0;
	for (std::size_t i = 0; i < piece.poly.size(); ++i) {
		const Vec3 a = piece.poly[i];
		const Vec3 b = piece.poly[(i + 1) % piece.poly.size()];
		const double len = length(b - a);
		if (len < 0.5 * link.length || std::abs(dot((b - a) / len, link.dir)) < COS45) continue;
		const double away = std::abs(dot((a + b) * 0.5 - link.mid, perp));
		if (away > farthest) {
			farthest = away;
			other = (b - a) / len;
		}
	}
	if (!other) return in;
	Vec3 out = normalize(cross(piece.normal, *other));
	if (dot(out, in) < 0) out = -out;
	return inPlane(in + out, piece.normal);
}

/// The piece's extent along `d` (its length when `d` runs along it).
double extent(const Piece& piece, const Vec3& d) {
	double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
	for (const auto& v : piece.poly) {
		lo = std::min(lo, dot(v, d));
		hi = std::max(hi, dot(v, d));
	}
	return hi - lo;
}

/// Where the line through `q` along `dir` (in the plane with normal `normal`) meets the line of
/// the common edge `link`; the edge's middle when the line runs along the edge.
Vec3 meetEdge(const Vec3& q, const Vec3& dir, const Link& link, const Vec3& normal) {
	const Vec3 n = normalize(cross(normal, link.dir));
	const double den = dot(dir, n);
	if (std::abs(den) < 0.2) return link.mid;
	return q + dir * (dot(link.mid - q, n) / den);
}

/// The lower median (of an even count the smaller middle value: a road with one bulge keeps
/// the narrow width).
double lowerMedian(std::vector<double> values) {
	if (values.empty()) return 0;
	std::sort(values.begin(), values.end());
	return values[(values.size() - 1) / 2];
}


/// `v` turned about the unit axis `n` by `angle` (radians, counter-clockwise seen from `n`).
Vec3 rotateAbout(const Vec3& v, const Vec3& n, double angle) {
	const double c = std::cos(angle), s = std::sin(angle);
	return v * c + cross(n, v) * s + n * (dot(n, v) * (1 - c));
}

/// The circular arc the road's centre line takes through a corner: it leaves `start` along
/// `inDir`, turns and crosses the common edge `out` running `outDir` - tangent to both, so the
/// lane runs on smoothly from one road into the other.
struct CurveArc {
	bool ok = false;
	Vec3 normal;      ///< The corner's plane normal (the axis the arc turns about)
	Vec3 centre;      ///< Middle of the circle (the turn's inside)
	Vec3 start, end;  ///< Where it enters and leaves the corner
	double radius = 0;
	double angle = 0; ///< How far it turns (radians)
	double turn = 1;  ///< +1 counter-clockwise seen from `normal`, -1 clockwise

	/// The point `a` radians along the arc from `start`.
	[[nodiscard]] Vec3 at(double a) const { return centre + rotateAbout(start - centre, normal, turn * a); }
};

constexpr double PI = 3.14159265358979323846;

CurveArc curveArc(const Vec3& start, const Vec3& inDir, const Link& out, const Vec3& outDir, const Vec3& normal) {
	CurveArc arc;
	const Vec3 t1 = inPlane(inDir, normal), t2 = inPlane(outDir, normal);
	if (length(t1) < 0.5 || length(t2) < 0.5) return arc;
	const double turn = dot(cross(t1, t2), normal);
	const double angle = std::acos(std::clamp(dot(t1, t2), -1.0, 1.0));
	// Only real turns: 20° .. 150° (flatter bends need no curve, sharper ones have no room).
	if (std::abs(turn) < 1e-3 || angle < 20.0 * PI / 180.0 || angle > 150.0 * PI / 180.0) return arc;
	arc.turn = turn > 0 ? 1 : -1;
	const Vec3 n1 = cross(normal, t1) * arc.turn;   // from the entry towards the circle's middle
	const Vec3 n2 = cross(normal, t2) * arc.turn;   // from the exit towards it
	const Vec3 m2 = normalize(cross(normal, out.dir));
	const double den = dot(n1 - n2, m2);
	if (std::abs(den) < 1e-6) return arc;
	const double r = dot(out.mid - start, m2) / den;
	if (r < 4.0) return arc;
	arc.normal = normal;
	arc.radius = r;
	arc.angle = angle;
	arc.start = start;
	arc.centre = start + n1 * r;
	arc.end = arc.centre - n2 * r;
	// It must leave through the common edge, not beside it.
	arc.ok = std::abs(dot(arc.end - out.mid, out.dir)) <= out.length * 0.5 + ON_LINE;
	return arc;
}

/// A piece of a cut corner: the brush and the arc segment its road top holds (-1: the small
/// block at the turn's inside the lane never crosses).
struct CurveCut {
	Solid solid;
	int segment = -1;
};

/// The corner brush cut into `segments` (even) pieces along `arc` by planes through the circle's
/// middle, optionally with a block cut off at the turn's inside (so no piece runs out to a point
/// there): the first half of the pieces lies beyond a line along the entry road, the second half
/// beyond one along the exit road, the block inside both. Every cut keeps the pieces convex and
/// tiling the brush; nullopt when a piece would be no clean Source brush (brushProblem), a
/// segment would miss its piece of the arc, or the pieces would not fill the brush.
std::optional<std::vector<CurveCut>> cutCorner(MapDocument& ids, const Solid& solid, const Piece& top, const CurveArc& arc, int segments, bool block) {
	if (segments < 2 || segments % 2 != 0) return std::nullopt;
	std::set<int> ownSides;
	for (const auto& side : solid.sides) ownSides.insert(side.id);
	Side cap;
	cap.material = "TOOLS/TOOLSNODRAW";
	cap.u = {{1, 0, 0}, 0, 0.25};
	cap.v = {{0, -1, 0}, 0, 0.25};
	const auto radial = [&](double a) {
		const Vec3 d = normalize(arc.at(a) - arc.centre);
		const Vec3 n = normalize(cross(arc.normal, d)) * arc.turn;   // in front: farther along the arc
		return Plane{n, dot(n, arc.centre)};
	};
	const auto split = [&](const Solid& s, const Plane& plane) { return splitSolid(ids, s, plane, cap); };
	std::vector<CurveCut> cuts;
	const auto wedge = [&](std::optional<Solid> part, int first, int last, const Vec3& towardsCentre) -> bool {
		if (!part) return false;
		Solid rest = std::move(*part);
		if (block) {
			// Beyond the line half the radius from the circle's middle: the block stays inside.
			const double reach = arc.radius * 0.5;
			auto [inside, beyond] = split(rest, Plane{towardsCentre, dot(towardsCentre, arc.centre) - reach});
			if (!beyond) return false;
			if (inside) cuts.push_back({std::move(*inside), -1});
			rest = std::move(*beyond);
		}
		for (int k = first; k < last; ++k) {
			auto [later, piece] = split(rest, radial(arc.angle * (k + 1) / segments));
			if (!piece || !later) return false;
			cuts.push_back({std::move(*piece), k});
			rest = std::move(*later);
		}
		cuts.push_back({std::move(rest), last});
		return true;
	};
	auto [second, first] = split(solid, radial(arc.angle * 0.5));
	const int half = segments / 2;
	const Vec3 n1 = normalize(arc.centre - arc.start), n2 = normalize(arc.centre - arc.end);
	if (!wedge(std::move(first), 0, half - 1, n1) || !wedge(std::move(second), half, segments - 1, n2)) return std::nullopt;
	// The block's two halves as one brush when they make one.
	std::vector<std::size_t> blocks;
	for (std::size_t i = 0; i < cuts.size(); ++i) {
		if (cuts[i].segment < 0) blocks.push_back(i);
	}
	if (blocks.size() == 2) {
		if (auto merged = mergeSolids(ids, {cuts[blocks[0]].solid, cuts[blocks[1]].solid}); merged && merged->exact) {
			merged->solid.id = cuts[blocks[0]].solid.id;
			cuts[blocks[0]].solid = std::move(merged->solid);
			cuts.erase(cuts.begin() + static_cast<std::ptrdiff_t>(blocks[1]));
		}
	}
	// Clean brushes, the whole corner filled, every segment holding its piece of the arc.
	double volume = 0;
	for (auto& cut : cuts) {
		for (auto& side : cut.solid.sides) {
			if (!ownSides.contains(side.id)) {
				side.material = "TOOLS/TOOLSNODRAW";
				alignToWorld(side);
			}
		}
		if (!brushProblem(cut.solid).empty()) return std::nullopt;
		volume += solidVolume(cut.solid);
		if (cut.segment >= 0) {
			const Vec3 mid = arc.at(arc.angle * (cut.segment + 0.5) / segments) - top.normal * 0.25;
			if (!solidContains(cut.solid, mid, 0.01)) return std::nullopt;
		}
	}
	const double whole = solidVolume(solid);
	if (std::abs(volume - whole) > std::max(1.0, whole * 1e-4)) return std::nullopt;
	// The piece that kept the brush's id first.
	std::stable_partition(cuts.begin(), cuts.end(), [&](const CurveCut& c) { return c.solid.id == solid.id; });
	return cuts;
}

/// How many pieces a curve gets: about one per 22.5° of turn, at least 4 (even, 4 .. 8), fewer
/// while a piece's stretch of the centre line would be shorter than 16 units.
int curveSegments(const CurveArc& arc) {
	int segments = 2 * static_cast<int>(std::ceil(arc.angle / (45.0 * PI / 180.0) - 1e-9));
	segments = std::clamp(segments, 4, 8);
	while (segments > 2 && arc.radius * arc.angle / segments < 16.0) segments -= 2;
	return segments;
}

/// The corner cut along `arc`: first with a block at the turn's inside, then without, then
/// with fewer pieces; nullopt when none of them is clean.
std::optional<std::vector<CurveCut>> cutCurve(MapDocument& ids, const Solid& solid, const Piece& top, const CurveArc& arc) {
	if (!arc.ok) return std::nullopt;
	for (int segments = curveSegments(arc); segments >= 2; segments -= 2) {
		for (const bool block : {true, false}) {
			if (auto cuts = cutCorner(ids, solid, top, arc, segments, block)) return cuts;
		}
	}
	return std::nullopt;
}

} // namespace

std::pair<int, int> materialTextureSize(const GameFileSystem& fs, std::string_view material) {
	const auto vmt = loadMaterial(fs, material);
	const std::string texture = vmt ? vmt->previewTexture() : std::string();
	if (!texture.empty()) {
		if (const auto data = fs.read("materials/" + normalizeMaterialName(texture) + ".vtf")) {
			if (const auto info = readTextureInfo(*data); info && info->width > 0 && info->height > 0) return {info->width, info->height};
		}
	}
	return {512, 512};
}

std::vector<FaceRef> roadFaces(const MapDocument& doc, const Selection& selection) {
	if (!selection.faces.empty()) return selection.faces;
	std::vector<SolidRef> solids = selection.solids;
	for (const int entityId : selection.entities) {
		if (const Entity* entity = doc.findEntity(entityId)) {
			for (const auto& solid : entity->solids) solids.push_back({entityId, solid.id});
		}
	}
	std::vector<FaceRef> faces;
	for (const auto& ref : solids) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const BrushGeometry geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) continue;
		for (const auto& face : geometry.faces) {
			if (normalize(face.normal).z > UP) faces.push_back({ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
		}
	}
	return faces;
}

RoadPlan planRoad(const MapDocument& doc, const std::vector<FaceRef>& faces, const RoadOptions& options, const TextureSizeFn& textureSize,
                  MapDocument* ids) {
	RoadPlan plan;
	MapDocument scratch;   // ids for a preview
	MapDocument& idSource = ids ? *ids : scratch;
	std::vector<Piece> pieces = collectPieces(doc, faces, plan.ignored);
	if (pieces.empty()) return plan;
	const int count = static_cast<int>(pieces.size());
	const auto at = [&](int i) -> const Piece& { return pieces[static_cast<std::size_t>(i)]; };
	const auto links = linkPieces(pieces);

	// --- Which tops are road: kerbs and sidewalks beside it at another height are not. ---
	// Pieces that touch form a surface; the largest surface is road, and so is every other
	// surface unless it lies beside a road piece more than KERB_HEIGHT above or below it.
	std::vector<int> surface(static_cast<std::size_t>(count), -1);
	std::vector<double> surfaceArea;
	for (int i = 0; i < count; ++i) {
		if (surface[static_cast<std::size_t>(i)] >= 0) continue;
		const int id = static_cast<int>(surfaceArea.size());
		surfaceArea.push_back(0);
		std::vector<int> stack{i};
		surface[static_cast<std::size_t>(i)] = id;
		while (!stack.empty()) {
			const int p = stack.back();
			stack.pop_back();
			surfaceArea.back() += polygonArea(at(p).poly);
			for (const auto& link : links[static_cast<std::size_t>(p)]) {
				if (surface[static_cast<std::size_t>(link.other)] < 0) {
					surface[static_cast<std::size_t>(link.other)] = id;
					stack.push_back(link.other);
				}
			}
		}
	}
	std::vector<int> bySize(surfaceArea.size());
	for (std::size_t k = 0; k < bySize.size(); ++k) bySize[k] = static_cast<int>(k);
	std::stable_sort(bySize.begin(), bySize.end(), [&](int a, int b) { return surfaceArea[static_cast<std::size_t>(a)] > surfaceArea[static_cast<std::size_t>(b)] + 1e-6; });
	std::vector<char> roadSurface(surfaceArea.size(), 0);
	std::vector<bool> kerb(static_cast<std::size_t>(count), false);
	for (std::size_t k = 0; k < bySize.size(); ++k) {
		const int s = bySize[k];
		bool isKerb = false;
		for (int i = 0; i < count && !isKerb && k > 0; ++i) {
			if (surface[static_cast<std::size_t>(i)] != s) continue;
			for (int j = 0; j < count && !isKerb; ++j) {
				if (!roadSurface[static_cast<std::size_t>(surface[static_cast<std::size_t>(j)])] || !besideFromAbove(at(i), at(j), BESIDE)) continue;
				isKerb = std::abs(at(i).centroid.z - heightAt(at(j), at(i).centroid)) > KERB_HEIGHT;
			}
		}
		roadSurface[static_cast<std::size_t>(s)] = isKerb ? 0 : 1;
		for (int i = 0; i < count; ++i) {
			if (surface[static_cast<std::size_t>(i)] == s && isKerb) kerb[static_cast<std::size_t>(i)] = true;
		}
	}

	// --- Small pieces at road height hanging on one road piece take its mapping. ---
	std::vector<int> parent(static_cast<std::size_t>(count), -1);
	const auto active = [&](int i) { return !kerb[static_cast<std::size_t>(i)] && parent[static_cast<std::size_t>(i)] < 0; };
	const auto activeLinks = [&](int i) {
		std::vector<const Link*> result;
		for (const auto& link : links[static_cast<std::size_t>(i)]) {
			if (active(link.other)) result.push_back(&link);
		}
		return result;
	};
	const auto narrow = [&](int i) {
		const LongAxis shape = longAxis(at(i));
		return shape.length / std::max(1.0, shape.aspect);
	};
	for (int pass = 0; pass < 8; ++pass) {
		bool changed = false;
		for (int i = 0; i < count; ++i) {
			if (!active(i)) continue;
			// It hangs on the neighbour it shares the longest edge with; any other neighbour may
			// only touch its short ends (a strip along the road's edge, not a piece across it).
			const auto near = activeLinks(i);
			if (near.empty()) continue;
			const Link* hang = near.front();
			for (const auto* link : near) {
				if (link->length > hang->length + 1e-6 || (std::abs(link->length - hang->length) <= 1e-6 && link->other < hang->other)) hang = link;
			}
			const int j = hang->other;
			if (!coplanar(at(i), at(j))) continue;
			bool endsOnly = true;
			for (const auto* link : near) endsOnly = endsOnly && (link == hang || link->length <= 1.5 * narrow(i));
			if (!endsOnly) continue;
			if (narrow(i) < SMALL_NARROW * narrow(j) || polygonArea(at(i).poly) < SMALL_AREA * polygonArea(at(j).poly)) {
				parent[static_cast<std::size_t>(i)] = j;
				changed = true;
			}
		}
		if (!changed) break;
	}

	// --- How the road runs on each piece, from the pieces around it. ---
	// A piece between two neighbours on opposite edges runs from one to the other (a strip of a
	// curve along its middle); a piece with neighbours on two edges at an angle is a corner, one
	// with three or more a junction. Every piece "uses" the common edges the road crosses on it.
	std::vector<Vec3> dir(static_cast<std::size_t>(count));
	std::vector<std::vector<int>> uses(static_cast<std::size_t>(count));   // the neighbours the road crosses to
	std::vector<char> node(static_cast<std::size_t>(count), 0);           // corner or junction
	std::vector<char> curve(static_cast<std::size_t>(count), 0);          // a corner the road curves through
	for (int i = 0; i < count; ++i) {
		if (!active(i)) continue;
		const auto near = activeLinks(i);
		const Piece& piece = at(i);
		if (near.size() >= 3) {
			node[static_cast<std::size_t>(i)] = 1;
		} else if (near.size() == 2) {
			if (std::abs(dot(near[0]->dir, near[1]->dir)) < (options.curveCorners ? COS35 : COS45)) {
				node[static_cast<std::size_t>(i)] = 1;
			} else {
				const Vec3 in = acrossEdge(piece, *near[0]);
				const Vec3 out = -acrossEdge(piece, *near[1]);
				dir[static_cast<std::size_t>(i)] = inPlane(in + out, piece.normal);
				uses[static_cast<std::size_t>(i)] = {near[0]->other, near[1]->other};
			}
		} else if (near.size() == 1) {
			// An end piece: across its common edge - unless it is long and the road turns off
			// part of its long side (an L of two straight pieces), then along its length.
			const Link& link = *near.front();
			const LongAxis shape = longAxis(piece);
			if (shape.aspect > ELONGATED && std::abs(dot(shape.axis, link.dir)) > COS30 && link.length < 0.75 * shape.length) {
				dir[static_cast<std::size_t>(i)] = shape.axis;
			} else {
				dir[static_cast<std::size_t>(i)] = throughFrom(piece, link);
				uses[static_cast<std::size_t>(i)] = {link.other};
			}
		} else {
			dir[static_cast<std::size_t>(i)] = longAxis(piece).axis;
		}
	}
	// Stretches of road between corners and junctions, and how long each is.
	std::vector<int> stretch(static_cast<std::size_t>(count), -1);
	std::vector<double> stretchLength;
	for (int i = 0; i < count; ++i) {
		if (!active(i) || node[static_cast<std::size_t>(i)] || stretch[static_cast<std::size_t>(i)] >= 0) continue;
		const int id = static_cast<int>(stretchLength.size());
		stretchLength.push_back(0);
		std::vector<int> stack{i};
		stretch[static_cast<std::size_t>(i)] = id;
		while (!stack.empty()) {
			const int p = stack.back();
			stack.pop_back();
			stretchLength.back() += extent(at(p), dir[static_cast<std::size_t>(p)]);
			for (const int q : uses[static_cast<std::size_t>(p)]) {
				if (!node[static_cast<std::size_t>(q)] && stretch[static_cast<std::size_t>(q)] < 0) {
					stretch[static_cast<std::size_t>(q)] = id;
					stack.push_back(q);
				}
			}
		}
	}
	const auto lengthBehind = [&](int neighbour) {
		const int s = stretch[static_cast<std::size_t>(neighbour)];
		return s >= 0 ? stretchLength[static_cast<std::size_t>(s)] : 0.0;
	};
	for (int i = 0; i < count; ++i) {
		if (!active(i) || !node[static_cast<std::size_t>(i)]) continue;
		const auto near = activeLinks(i);
		const Piece& piece = at(i);
		if (near.size() == 2 && options.curveCorners) {
			// A corner the road curves through (when it can be cut cleanly; tried here with the
			// centre line through the edges' middles, cut for real once the centre line is known).
			const Vec3 in = acrossEdge(piece, *near[0]);
			const Vec3 out = -acrossEdge(piece, *near[1]);
			const CurveArc arc = curveArc(near[0]->mid, in, *near[1], out, piece.normal);
			MapDocument trial;
			const Solid* solid = doc.findSolid(piece.ref.solid);
			if (solid && cutCurve(trial, *solid, piece, arc)) {
				curve[static_cast<std::size_t>(i)] = 1;
				dir[static_cast<std::size_t>(i)] = in;
				uses[static_cast<std::size_t>(i)] = {near[0]->other, near[1]->other};
				continue;
			}
			if (std::abs(dot(near[0]->dir, near[1]->dir)) >= COS45) {
				// A gentle bend (35°..45°) that cannot be cut: one strip, from one edge to the other.
				node[static_cast<std::size_t>(i)] = 0;
				dir[static_cast<std::size_t>(i)] = inPlane(in + out, piece.normal);
				uses[static_cast<std::size_t>(i)] = {near[0]->other, near[1]->other};
				continue;
			}
			++plan.squareCorners;
		}
		if (near.size() == 2) {
			// A corner continues the longer road (then the wider common edge); the other road
			// starts at its edge. Never diagonal.
			const auto better = [&](const Link* a, const Link* b) {
				const double la = lengthBehind(a->other), lb = lengthBehind(b->other);
				if (std::abs(la - lb) > 1e-6) return la > lb;
				if (std::abs(a->length - b->length) > 1e-6) return a->length > b->length;
				return a->other < b->other;
			};
			const Link* keep = better(near[0], near[1]) ? near[0] : near[1];
			dir[static_cast<std::size_t>(i)] = -acrossEdge(piece, *keep);
			uses[static_cast<std::size_t>(i)] = {keep->other};
			continue;
		}
		// A junction: the main road is the straightest pair of opposite neighbours, of those the
		// widest, then the longest; it runs straight through.
		std::size_t bestA = 0, bestB = 1;
		std::tuple<int, double, double, double> best{-1, 0, 0, 0};
		for (std::size_t a = 0; a < near.size(); ++a) {
			for (std::size_t b = a + 1; b < near.size(); ++b) {
				const double straight = -dot(normalize(near[a]->mid - piece.centroid), normalize(near[b]->mid - piece.centroid));
				const bool opposite = straight > 0.7 && std::abs(dot(near[a]->dir, near[b]->dir)) > COS45;
				const std::tuple<int, double, double, double> score{opposite ? 1 : 0, std::min(near[a]->length, near[b]->length),
				                                                    lengthBehind(near[a]->other) + lengthBehind(near[b]->other), straight};
				if (score > best) {
					best = score;
					bestA = a;
					bestB = b;
				}
			}
		}
		const Vec3 in = acrossEdge(piece, *near[bestA]);
		const Vec3 out = -acrossEdge(piece, *near[bestB]);
		dir[static_cast<std::size_t>(i)] = inPlane(in + out, piece.normal);
		uses[static_cast<std::size_t>(i)] = {near[bestA]->other, near[bestB]->other};
	}

	// --- Roads: pieces linked where both use the common edge and run the same way. ---
	const auto usesEdge = [&](int i, int j) {
		const auto& u = uses[static_cast<std::size_t>(i)];
		return std::find(u.begin(), u.end(), j) != u.end();
	};
	const auto joined = [&](int i, int j) {
		return usesEdge(i, j) && usesEdge(j, i) &&
		       (curve[static_cast<std::size_t>(i)] || curve[static_cast<std::size_t>(j)] ||
		        std::abs(dot(dir[static_cast<std::size_t>(i)], dir[static_cast<std::size_t>(j)])) > COS45);
	};
	const auto joinedNeighbours = [&](int i) {
		std::vector<int> result;
		for (const auto* link : activeLinks(i)) {
			if (joined(i, link->other)) result.push_back(link->other);
		}
		return result;
	};
	// A road starting at a junction or a corner: the common edge it starts at (the widest).
	const auto startEdge = [&](int i) -> const Link* {
		const Link* found = nullptr;
		for (const auto* link : activeLinks(i)) {
			if (joined(i, link->other)) continue;
			if (std::abs(dot(dir[static_cast<std::size_t>(i)], normalize(cross(at(i).normal, link->dir)))) < 0.5) continue;   // runs along that edge
			if (!found || link->length > found->length) found = link;
		}
		return found;
	};
	std::vector<std::vector<int>> chains;
	std::vector<bool> placed(static_cast<std::size_t>(count), false);
	const auto walk = [&](int start) {
		std::vector<int> chain;
		int current = start, previous = -1;
		while (current >= 0) {
			placed[static_cast<std::size_t>(current)] = true;
			chain.push_back(current);
			int next = -1;
			for (const int j : joinedNeighbours(current)) {
				if (j != previous && !placed[static_cast<std::size_t>(j)]) {
					next = j;
					break;
				}
			}
			previous = current;
			current = next;
		}
		return chain;
	};
	for (int round = 0; round < 2; ++round) {
		for (int i = 0; i < count; ++i) {
			if (!active(i) || placed[static_cast<std::size_t>(i)]) continue;
			if (round == 0 && joinedNeighbours(i).size() > 1) continue;   // first the roads with ends, then rings
			auto chain = walk(i);
			// Start where the road leaves a junction or a corner, if one end does.
			if (!startEdge(chain.front()) && startEdge(chain.back())) std::reverse(chain.begin(), chain.end());
			if (options.flip) std::reverse(chain.begin(), chain.end());
			chains.push_back(std::move(chain));
		}
	}
	plan.chains = static_cast<int>(chains.size());

	// --- The material and the texture's size. ---
	plan.material = options.material;
	if (plan.material.empty()) {
		std::map<std::string, int> counts;
		for (int i = 0; i < count; ++i) {
			if (!kerb[static_cast<std::size_t>(i)]) ++counts[at(i).material];
		}
		int most = -1;
		for (const auto& [material, n] : counts) {
			if (n > most) {
				most = n;
				plan.material = material;
			}
		}
	}
	plan.sideMaterial = options.sideMaterial;
	auto [texWidth, texHeight] = textureSize ? textureSize(plan.material) : std::pair<int, int>{512, 512};
	if (texWidth <= 0) texWidth = 512;
	if (texHeight <= 0) texHeight = 512;
	plan.alongV = options.along == RoadAlong::V || (options.along == RoadAlong::Auto && texHeight >= texWidth);
	const double texAcross = plan.alongV ? texWidth : texHeight;
	const double texAlong = plan.alongV ? texHeight : texWidth;
	const double repeats = std::max(1, options.repeatsAcross);

	std::map<int, RoadPlan::FaceChange> mapped;   // by piece
	for (std::size_t c = 0; c < chains.size(); ++c) {
		const auto& chain = chains[c];
		const std::size_t n = chain.size();
		const auto piece = [&](std::size_t k) -> const Piece& { return at(chain[k]); };
		const auto linkTo = [&](std::size_t k, std::size_t other) { return findLink(links, chain[k], chain[other]); };
		const Link* start = startEdge(chain.front());

		// Directions, pointing on along the chain.
		std::vector<Vec3> along(n);
		for (std::size_t k = 0; k < n; ++k) {
			Vec3 d = dir[static_cast<std::size_t>(chain[k])];
			if (k > 0) {
				if (dot(d, piece(k).centroid - linkTo(k, k - 1)->mid) < 0) d = -d;
			} else if (n > 1) {
				if (dot(d, linkTo(0, 1)->mid - piece(0).centroid) < 0) d = -d;
			} else {
				const Vec3 away = start ? piece(0).centroid - start->mid : Vec3{1, 0.001, 0.000001};
				if (dot(d, away) < 0) d = -d;
				if (options.flip) d = -d;
			}
			along[k] = d;
		}

		// Corners the road curves through (not at the road's ends).
		const auto isCurve = [&](std::size_t k) { return k > 0 && k + 1 < n && curve[static_cast<std::size_t>(chain[k])]; };

		// One scale for the whole road: from its width (the lower median of the pieces' widths;
		// curve corners left out, their width across is no road's width).
		std::vector<Vec3> across(n);
		std::vector<double> width(n);
		std::vector<double> straightWidths;
		for (std::size_t k = 0; k < n; ++k) {
			across[k] = normalize(cross(piece(k).normal, along[k]));
			width[k] = std::max(1.0, extent(piece(k), across[k]));
			if (!isCurve(k)) straightWidths.push_back(width[k]);
		}
		const double typical = lowerMedian(straightWidths.empty() ? width : straightWidths);
		const double roadWidth = options.roadWidth > 0 ? options.roadWidth : typical;
		plan.widths.push_back(roadWidth);
		const double texels = texAcross * repeats / roadWidth;   // per unit, across and along (square pixels)

		// The centre line: through the middle of the piece of typical width, carried on from piece
		// to piece where it meets the common edges - through a curve corner along its arc.
		std::size_t reference = 0;
		bool referenceSet = false;
		for (std::size_t k = 0; k < n; ++k) {
			if (isCurve(k)) continue;
			if (!referenceSet || std::abs(width[k] - typical) < std::abs(width[reference] - typical) - 1e-6) reference = k;
			referenceSet = true;
		}
		std::vector<Vec3> centre(n);   // a point of the centre line on each piece
		std::vector<Vec3> crossing(n); // where it crosses the common edge to the piece before (k >= 1)
		{
			const Piece& p = piece(reference);
			double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
			for (const auto& v : p.poly) {
				lo = std::min(lo, dot(v, across[reference]));
				hi = std::max(hi, dot(v, across[reference]));
			}
			centre[reference] = p.centroid + across[reference] * ((lo + hi) * 0.5 - dot(p.centroid, across[reference]));
		}
		for (std::size_t k = reference + 1; k < n; ++k) {
			crossing[k] = meetEdge(centre[k - 1], along[k - 1], *linkTo(k - 1, k), piece(k - 1).normal);
			if (isCurve(k - 1)) {
				// Out of a curve: where its arc (from where it came in) leaves.
				const CurveArc arc = curveArc(crossing[k - 1], along[k - 2], *linkTo(k - 1, k), along[k], piece(k - 1).normal);
				if (arc.ok) crossing[k] = arc.end;
			}
			centre[k] = crossing[k];
		}
		for (std::size_t k = reference; k-- > 0;) {
			crossing[k + 1] = meetEdge(centre[k + 1], along[k + 1], *linkTo(k + 1, k), piece(k + 1).normal);
			if (isCurve(k + 1)) {
				// Back through a curve: the same arc, run the other way round.
				const CurveArc arc = curveArc(crossing[k + 2], -along[k + 2], *linkTo(k + 1, k), -along[k], piece(k + 1).normal);
				if (arc.ok) crossing[k + 1] = arc.end;
			}
			centre[k] = crossing[k + 1];
		}

		// Cut the curve corners along their arcs (now that the centre line is known); a corner
		// that cannot be cut stays one piece and runs on like the road before it.
		std::map<std::size_t, std::pair<CurveArc, std::vector<CurveCut>>> curves;
		for (std::size_t k = 1; k + 1 < n; ++k) {
			if (!isCurve(k)) continue;
			const Piece& p = piece(k);
			const CurveArc arc = curveArc(crossing[k], along[k - 1], *linkTo(k, k + 1), along[k + 1], p.normal);
			const Solid* solid = doc.findSolid(p.ref.solid);
			auto cuts = solid ? cutCurve(idSource, *solid, p, arc) : std::nullopt;
			if (!cuts) {
				++plan.squareCorners;
				along[k] = along[k - 1];
				across[k] = across[k - 1];
				continue;
			}
			curves.emplace(k, std::make_pair(arc, std::move(*cuts)));
		}

		// Along: 0 where the road starts (the junction's or corner's edge, else the free end),
		// carried on across every common edge at the centre line; through a curve along its
		// chords (the pieces' straight stretches of the arc).
		std::vector<double> shift(n);
		std::vector<double> entry(n);   // a curve's value along the road where it starts
		if (start) {
			shift[0] = -dot(meetEdge(centre[0], along[0], *start, piece(0).normal), along[0]) * texels;
		} else {
			double lo = std::numeric_limits<double>::max();
			for (const auto& v : piece(0).poly) lo = std::min(lo, dot(v, along[0]));
			shift[0] = -lo * texels;
		}
		const auto chordLength = [&](std::size_t k) {
			const auto& [arc, cuts] = curves.at(k);
			int segments = 0;
			for (const auto& cut : cuts) segments = std::max(segments, cut.segment + 1);
			return 2.0 * arc.radius * std::sin(arc.angle / segments * 0.5) * segments;
		};
		for (std::size_t k = 1; k < n; ++k) {
			if (curves.contains(k)) {
				entry[k] = curves.contains(k - 1) ? entry[k - 1] + chordLength(k - 1) * texels : dot(crossing[k], along[k - 1]) * texels + shift[k - 1];
				continue;
			}
			const double before = curves.contains(k - 1) ? entry[k - 1] + chordLength(k - 1) * texels
			                                              : dot(crossing[k], along[k - 1]) * texels + shift[k - 1];
			shift[k] = before - dot(crossing[k], along[k]) * texels;
		}

		const double middle = texAcross * repeats * 0.5;   // the texture's middle sits on the centre line
		// A face's texture: running `alongDir`, the centre line through `point` where the value
		// along the road is `value` (texels).
		const auto mapSide = [&](Side& side, const Vec3& alongDir, const Vec3& normal, const Vec3& point, double value) {
			const Vec3 acrossDir = normalize(cross(normal, alongDir));
			side.material = plan.material;
			side.rotation = 0;
			const TextureAxis alongAxis{alongDir, wrapShift(value - dot(point, alongDir) * texels, texAlong), 1.0 / texels};
			if (plan.alongV) {
				// U across (cross(normal, along): the picture is not mirrored), V along the road.
				side.u = {acrossDir, wrapShift(middle - dot(point, acrossDir) * texels, texAcross), 1.0 / texels};
				side.v = alongAxis;
			} else {
				// U along, V across the other way round (keeps the picture unmirrored).
				side.u = alongAxis;
				side.v = {-acrossDir, wrapShift(middle + dot(point, acrossDir) * texels, texAcross), 1.0 / texels};
			}
		};

		for (std::size_t k = 0; k < n; ++k) {
			const Piece& p = piece(k);
			const Solid* solid = doc.findSolid(p.ref.solid);
			if (const auto found = curves.find(k); found != curves.end()) {
				// The curve's pieces: each road top runs along its chord of the arc.
				const auto& [arc, cuts] = found->second;
				int segments = 0;
				for (const auto& cut : cuts) segments = std::max(segments, cut.segment + 1);
				const Plane topPlane{p.normal, dot(p.normal, p.centroid)};
				RoadPlan::CornerSplit split;
				split.original = p.ref.solid;
				for (const auto& cut : cuts) {
					Solid brush = cut.solid;
					const int segment = cut.segment >= 0 ? cut.segment : segments / 2 - 1;   // the block: like the piece beside it
					const Vec3 a0 = arc.at(arc.angle * segment / segments);
					const Vec3 a1 = arc.at(arc.angle * (segment + 1) / segments);
					const double value = entry[k] + 2.0 * arc.radius * std::sin(arc.angle / segments * 0.5) * segment * texels;
					for (auto& side : brush.sides) {
						const Plane plane = side.plane();
						if (dot(plane.normal, topPlane.normal) > 0.9999 && std::abs(plane.dist - topPlane.dist) < ON_LINE) {
							mapSide(side, normalize(a1 - a0), p.normal, a0, value);
						} else if (!options.sideMaterial.empty() && std::abs(plane.normal.z) < UP && side.material != "TOOLS/TOOLSNODRAW") {
							side.material = options.sideMaterial;
						}
					}
					split.pieces.push_back(std::move(brush));
				}
				plan.corners.push_back(std::move(split));
				continue;
			}
			RoadPlan::FaceChange change;
			change.face = p.ref;
			change.chain = static_cast<int>(c);
			for (const auto& side : solid->sides) {
				if (side.id == p.ref.sideId) change.side = side;
			}
			const double value = dot(centre[k], along[k]) * texels + shift[k];
			mapSide(change.side, along[k], p.normal, centre[k], value);
			mapped.emplace(chain[k], std::move(change));
		}
	}

	// Small pieces take the mapping of the piece they hang on (in one plane: seamless).
	for (int i = 0; i < count; ++i) {
		if (kerb[static_cast<std::size_t>(i)] || parent[static_cast<std::size_t>(i)] < 0) continue;
		int root = parent[static_cast<std::size_t>(i)];
		for (int guard = 0; guard < count && parent[static_cast<std::size_t>(root)] >= 0; ++guard) root = parent[static_cast<std::size_t>(root)];
		const auto found = mapped.find(root);
		if (found == mapped.end()) continue;
		const Solid* solid = doc.findSolid(at(i).ref.solid);
		RoadPlan::FaceChange change;
		change.face = at(i).ref;
		change.chain = found->second.chain;
		for (const auto& side : solid->sides) {
			if (side.id == at(i).ref.sideId) change.side = side;
		}
		change.side.material = found->second.side.material;
		change.side.u = found->second.side.u;
		change.side.v = found->second.side.v;
		change.side.rotation = 0;
		mapped.emplace(i, std::move(change));
		++plan.edgePieces;
	}
	for (auto& [index, change] : mapped) plan.faces.push_back(std::move(change));

	// Kerbs: the tops left out beside the road, and the standing sides of all these brushes.
	std::vector<SolidRef> kerbSolids;
	for (int i = 0; i < count; ++i) {
		if (!kerb[static_cast<std::size_t>(i)]) continue;
		++plan.kerbs;
		if (!options.sideMaterial.empty()) plan.sideFaces.push_back(at(i).ref);
		if (std::find(kerbSolids.begin(), kerbSolids.end(), at(i).ref.solid) == kerbSolids.end()) kerbSolids.push_back(at(i).ref.solid);
	}
	if (!options.sideMaterial.empty()) {
		std::vector<SolidRef> solids;
		for (const auto& change : plan.faces) {
			if (std::find(solids.begin(), solids.end(), change.face.solid) == solids.end()) solids.push_back(change.face.solid);
		}
		for (const auto& ref : kerbSolids) {
			if (std::find(solids.begin(), solids.end(), ref) == solids.end()) solids.push_back(ref);
		}
		for (const auto& ref : solids) {
			const Solid* solid = doc.findSolid(ref);
			const BrushGeometry geometry = buildBrushGeometry(*solid);
			for (const auto& face : geometry.faces) {
				if (std::abs(normalize(face.normal).z) < UP) plan.sideFaces.push_back({ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
			}
		}
	}
	return plan;
}

namespace {

/// Writes the plan's values into the brush (the faces it plans for that brush).
void applyToSolid(Solid& solid, const SolidRef& ref, const RoadPlan& plan) {
	for (auto& side : solid.sides) {
		for (const auto& change : plan.faces) {
			if (change.face.solid == ref && change.face.sideId == side.id) {
				side.material = change.side.material;
				side.u = change.side.u;
				side.v = change.side.v;
				side.rotation = change.side.rotation;
			}
		}
		for (const auto& face : plan.sideFaces) {
			if (face.solid == ref && face.sideId == side.id) side.material = plan.sideMaterial;
		}
	}
}

/// Every brush the plan changes, each once.
std::vector<SolidRef> plannedSolids(const RoadPlan& plan) {
	std::vector<SolidRef> solids;
	const auto add = [&](const SolidRef& ref) {
		if (std::find(solids.begin(), solids.end(), ref) == solids.end()) solids.push_back(ref);
	};
	for (const auto& change : plan.faces) add(change.face.solid);
	for (const auto& face : plan.sideFaces) add(face.solid);
	return solids;
}

} // namespace

std::vector<Solid> roadPreview(const MapDocument& doc, const RoadPlan& plan, std::vector<SolidRef>* replaced) {
	std::vector<Solid> copies;
	for (const auto& corner : plan.corners) {
		copies.insert(copies.end(), corner.pieces.begin(), corner.pieces.end());
		if (replaced) replaced->push_back(corner.original);
	}
	for (const auto& ref : plannedSolids(plan)) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		Solid copy = *solid;
		applyToSolid(copy, ref, plan);
		copies.push_back(std::move(copy));
		if (replaced) replaced->push_back(ref);
	}
	return copies;
}

namespace edit {

Change applyRoad(MapDocument& doc, const RoadPlan& plan) {
	Transaction t(doc, "Road");
	for (const auto& ref : plannedSolids(plan)) {
		t.touchSolid(ref);
		if (Solid* solid = doc.findSolid(ref)) applyToSolid(*solid, ref, plan);
	}
	// Curve corners: the corner brush replaced by its pieces (in its place where the first keeps
	// its id - undo puts it back exactly there - the others after it).
	for (const auto& corner : plan.corners) {
		Entity* entity = doc.findEntity(corner.original.entityId);
		if (!entity || corner.pieces.empty()) continue;
		t.touchSolid(corner.original);
		const auto at = std::find_if(entity->solids.begin(), entity->solids.end(), [&](const Solid& s) { return s.id == corner.original.solidId; });
		const bool keeps = corner.pieces.front().id == corner.original.solidId;
		if (at != entity->solids.end()) {
			if (keeps) {
				*at = corner.pieces.front();
			} else {
				entity->solids.erase(at);
			}
		}
		for (std::size_t i = keeps ? 1 : 0; i < corner.pieces.size(); ++i) {
			t.touchSolid({corner.original.entityId, corner.pieces[i].id});
			entity->solids.push_back(corner.pieces[i]);
		}
	}
	return t.finish();
}

} // namespace edit
} // namespace sme
