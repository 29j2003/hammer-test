// road_texture.cpp - Road tool: chains of floor faces and their texture axes (see road_texture.h).

#include <sme/road_texture.h>

#include <sme/brush.h>
#include <sme/material.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

namespace sme {

namespace {

constexpr double UP = 0.5;            ///< A road face's normal is at most 60° from straight up
constexpr double ON_LINE = 0.1;       ///< Units: an edge lies on another face's edge
constexpr double MIN_SHARED = 1.0;    ///< Units: shorter common pieces of edges do not link faces
constexpr double ELONGATED = 1.25;    ///< A piece this much longer than wide has a long axis
const double COS30 = std::cos(30.0 * 3.14159265358979323846 / 180.0);

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

/// Middle of the piece's edge across from the common edge `link` (its free end: about as long
/// and running about the same way), or nothing. For a strip of a curve this is the next radial
/// edge, so the road runs along the strip's middle, not across the common edge.
std::optional<Vec3> oppositeEdgeMiddle(const Piece& piece, const Link& link) {
	const Vec3 perp = normalize(cross(piece.normal, link.dir));
	std::optional<Vec3> best;
	double farthest = 1.0;
	for (std::size_t i = 0; i < piece.poly.size(); ++i) {
		const Vec3 a = piece.poly[i];
		const Vec3 b = piece.poly[(i + 1) % piece.poly.size()];
		const double len = length(b - a);
		if (len < 0.5 * link.length || std::abs(dot((b - a) / len, link.dir)) < std::cos(45.0 * 3.14159265358979323846 / 180.0)) continue;
		const Vec3 mid = (a + b) * 0.5;
		const double away = std::abs(dot(mid - link.mid, perp));
		if (away > farthest) {
			farthest = away;
			best = mid;
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

/// Puts the pieces into chains, end to end. A chain starts at a piece with at most one free
/// neighbour; at a fork it goes on where the road runs straightest, the other branch becomes
/// a chain of its own.
std::vector<std::vector<int>> buildChains(const std::vector<Piece>& pieces, const std::vector<std::vector<Link>>& links) {
	std::vector<std::vector<int>> chains;
	std::vector<bool> used(pieces.size(), false);
	const auto freeNeighbours = [&](int index) {
		int count = 0;
		for (const auto& link : links[static_cast<std::size_t>(index)]) count += used[static_cast<std::size_t>(link.other)] ? 0 : 1;
		return count;
	};
	for (;;) {
		int start = -1;
		for (std::size_t i = 0; i < pieces.size() && start < 0; ++i) {
			if (!used[i] && freeNeighbours(static_cast<int>(i)) <= 1) start = static_cast<int>(i);
		}
		for (std::size_t i = 0; i < pieces.size() && start < 0; ++i) {
			if (!used[i]) start = static_cast<int>(i);   // a closed ring
		}
		if (start < 0) break;
		std::vector<int> chain;
		int current = start;
		int previous = -1;
		while (current >= 0) {
			used[static_cast<std::size_t>(current)] = true;
			chain.push_back(current);
			const Piece& piece = pieces[static_cast<std::size_t>(current)];
			// Where the road came from: the common edge with the piece before (else the piece's middle).
			const Link* back = previous >= 0 ? findLink(links, current, previous) : nullptr;
			const Vec3 from = back ? back->mid : piece.centroid;
			int next = -1;
			double straightest = std::numeric_limits<double>::lowest();
			for (const auto& link : links[static_cast<std::size_t>(current)]) {
				if (used[static_cast<std::size_t>(link.other)]) continue;
				const Vec3 heading = normalize(link.mid - from);
				const Vec3 came = back ? normalize(piece.centroid - back->mid) : heading;
				const double score = dot(heading, came);
				if (score > straightest + 1e-9) {
					straightest = score;
					next = link.other;
				}
			}
			previous = current;
			current = next;
		}
		chains.push_back(std::move(chain));
	}
	return chains;
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

RoadPlan planRoad(const MapDocument& doc, const std::vector<FaceRef>& faces, const RoadOptions& options, const TextureSizeFn& textureSize) {
	RoadPlan plan;
	std::vector<Piece> pieces = collectPieces(doc, faces, plan.ignored);
	if (pieces.empty()) return plan;

	// The material: as asked, else the one most of the faces have.
	plan.material = options.material;
	if (plan.material.empty()) {
		std::map<std::string, int> counts;
		for (const auto& piece : pieces) ++counts[piece.material];
		int most = -1;
		for (const auto& [material, count] : counts) {
			if (count > most) {
				most = count;
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

	const auto links = linkPieces(pieces);
	auto chains = buildChains(pieces, links);
	if (options.flip) {
		for (auto& chain : chains) std::reverse(chain.begin(), chain.end());
	}
	plan.chains = static_cast<int>(chains.size());

	for (std::size_t c = 0; c < chains.size(); ++c) {
		const auto& chain = chains[c];
		// Direction of the road on each piece.
		std::vector<Vec3> along(chain.size());
		for (std::size_t k = 0; k < chain.size(); ++k) {
			const Piece& piece = pieces[static_cast<std::size_t>(chain[k])];
			const Link* in = k > 0 ? findLink(links, chain[k], chain[k - 1]) : nullptr;
			const Link* out = k + 1 < chain.size() ? findLink(links, chain[k], chain[k + 1]) : nullptr;
			Vec3 d;
			if (in && out) {
				d = inPlane(out->mid - in->mid, piece.normal);   // through the piece, from edge to edge
			} else if (in || out) {
				// An end piece: across its common edge - unless it is long and the road turns
				// off part of its long side (an L of two straight pieces), then along its length.
				// (A strip of a curve shares its whole long side: across it.)
				const Link* link = in ? in : out;
				const LongAxis shape = longAxis(piece);
				if (shape.aspect > ELONGATED && std::abs(dot(shape.axis, link->dir)) > COS30 && link->length < 0.75 * shape.length) {
					d = shape.axis;
				} else if (const auto far = oppositeEdgeMiddle(piece, *link)) {
					d = inPlane(link->mid - *far, piece.normal);   // from the free end to the common edge
				} else {
					d = inPlane(cross(piece.normal, link->dir), piece.normal);
				}
				// Towards the next piece, or away from the one before.
				const Vec3 towards = out ? out->mid - piece.centroid : piece.centroid - in->mid;
				if (dot(d, towards) < 0) d = -d;
			}
			if (length(d) < 0.5) {
				// A piece on its own (or no clear direction): along its length.
				d = longAxis(piece).axis;
				const Vec3 tieBreak{1, 0.001, 0.000001};
				if (!(in || out) && dot(d, tieBreak) < 0) d = -d;
				if (in && dot(d, piece.centroid - in->mid) < 0) d = -d;
				if (out && dot(d, out->mid - piece.centroid) < 0) d = -d;
			}
			along[k] = d;
		}

		// Width of each piece across the road, and the chain's mean width for the scale along it.
		std::vector<Vec3> across(chain.size());
		std::vector<double> minAcross(chain.size()), width(chain.size());
		double widthSum = 0;
		for (std::size_t k = 0; k < chain.size(); ++k) {
			const Piece& piece = pieces[static_cast<std::size_t>(chain[k])];
			across[k] = normalize(cross(piece.normal, along[k]));
			double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
			for (const auto& v : piece.poly) {
				lo = std::min(lo, dot(v, across[k]));
				hi = std::max(hi, dot(v, across[k]));
			}
			minAcross[k] = lo;
			width[k] = std::max(1.0, hi - lo);
			widthSum += width[k];
		}
		const double texelsAlong = texAcross * repeats / (widthSum / static_cast<double>(chain.size()));   // square pixels

		// Along the road: the distance from the chain's start, carried on across every common edge.
		double offset = 0;
		for (std::size_t k = 0; k < chain.size(); ++k) {
			const Piece& piece = pieces[static_cast<std::size_t>(chain[k])];
			if (k == 0) {
				double lo = std::numeric_limits<double>::max();
				for (const auto& v : piece.poly) lo = std::min(lo, dot(v, along[0]));
				offset = -lo;
			} else {
				const Link* in = findLink(links, chain[k], chain[k - 1]);
				offset = dot(in->mid, along[k - 1]) + offset - dot(in->mid, along[k]);
			}
			const double texelsAcross = texAcross * repeats / width[k];   // exactly the width, n times

			const Solid* solid = doc.findSolid(piece.ref.solid);
			RoadPlan::FaceChange change;
			change.face = piece.ref;
			change.chain = static_cast<int>(c);
			for (const auto& side : solid->sides) {
				if (side.id == piece.ref.sideId) change.side = side;
			}
			change.side.material = plan.material;
			change.side.rotation = 0;
			const TextureAxis alongAxis{along[k], wrapShift(offset * texelsAlong, texAlong), 1.0 / texelsAlong};
			if (plan.alongV) {
				// U across (cross(normal, along): the picture is not mirrored), V along the road.
				change.side.u = {across[k], wrapShift(-minAcross[k] * texelsAcross, texAcross), 1.0 / texelsAcross};
				change.side.v = alongAxis;
			} else {
				// U along, V across the other way round (keeps the picture unmirrored).
				const double maxAcross = minAcross[k] + width[k];
				change.side.u = alongAxis;
				change.side.v = {-across[k], wrapShift(maxAcross * texelsAcross, texAcross), 1.0 / texelsAcross};
			}
			plan.faces.push_back(std::move(change));
		}
	}

	// Kerbs: the standing sides of the road's brushes.
	if (!options.sideMaterial.empty()) {
		std::vector<SolidRef> solids;
		for (const auto& change : plan.faces) {
			if (std::find(solids.begin(), solids.end(), change.face.solid) == solids.end()) solids.push_back(change.face.solid);
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
	return t.finish();
}

} // namespace edit
} // namespace sme
