// edge_round.cpp - Rounding or bevelling the corner between two faces (see edge_round.h).

#include <sme/edge_round.h>

#include <sme/brush.h>
#include <sme/csg.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sme {

namespace {

constexpr double ON_PLANE = 0.1;      ///< Units a vertex may lie off a plane and still count as on it
constexpr double MIN_CHORD = 2.0;     ///< Shortest segment of an arc (shorter ones make tiny faces)
constexpr double MIN_PIECE = 1.0;     ///< A piece of a brush must reach at least this far along the corner

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

const FaceGeometry* faceOf(const BrushGeometry& geometry, const Solid& solid, int sideId) {
	for (const auto& face : geometry.faces) {
		if (solid.sides[static_cast<std::size_t>(face.sideIndex)].id == sideId) return &face;
	}
	return nullptr;
}

/// Everything the planning works on.
struct Corner {
	const MapDocument* doc = nullptr;
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

/// A piece of a brush and whether neighbours cover the corner along it.
struct Piece {
	Solid solid;
	bool covered = false;
	std::size_t target = 0;   ///< Index of the brush it came from
};

/// The planes of the rounding for radius `r` with `n` segments (outer corner: keep behind them).
std::vector<Plane> arcPlanes(const Corner& corner, double r, int n) {
	const Vec3 center = corner.origin - (corner.planeA.normal + corner.planeB.normal) * (r / (1 + corner.c));
	const double step = corner.angle / n;
	std::vector<Plane> planes;
	for (int i = 0; i < n; ++i) {
		const Vec3 normal = slerp(corner.planeA.normal, corner.planeB.normal, corner.angle, (i + 0.5) / n);
		// Through the arc points at both ends of the segment (the arc's chords).
		planes.push_back(Plane{normal, dot(normal, center) + r * std::cos(step / 2)});
	}
	return planes;
}

/// The piece cut by the arc planes; nullopt if nothing usable is left of it.
std::optional<Solid> cutPiece(const Corner& corner, const Solid& piece, const std::vector<Plane>& planes) {
	Solid out = piece;
	for (std::size_t i = 0; i < planes.size(); ++i) {
		const Side* from = (i + 0.5) / planes.size() < 0.5 ? corner.sideA : corner.sideB;
		out.sides.push_back(makeSide(*corner.ids, planes[i], from->material, from));
	}
	const auto geometry = buildBrushGeometry(out);
	if (!geometry.valid || geometry.faces.empty()) return std::nullopt;
	removeUnusedSides(out);
	tidyPlanePoints(out);
	if (!usable(out)) return std::nullopt;
	return out;
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

/// The part of the polygon on the side of `plane` where most of it lies: sign of that side
/// (+1 in front, -1 behind) and that part.
std::pair<int, std::vector<Vec3>> mainSide(const std::vector<Vec3>& polygon, const Plane& plane) {
	auto behind = clipBehind(polygon, plane);
	auto front = clipBehind(polygon, Plane{-plane.normal, -plane.dist});
	const double back = behind.size() >= 3 ? polygonArea(behind) : 0, fore = front.size() >= 3 ? polygonArea(front) : 0;
	if (back < 1e-3 && fore < 1e-3) return {0, {}};
	return back >= fore ? std::pair{-1, std::move(behind)} : std::pair{1, std::move(front)};
}

} // namespace

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

EdgeRoundPlan planEdgeRound(const MapDocument& doc, MapDocument& ids, const FaceRef& a, const FaceRef& b, double radius, int segments) {
	EdgeRoundPlan plan;
	plan.requested = radius;
	plan.segments = std::clamp(segments, 1, 64);
	if (!(radius > 0)) {
		plan.problem = "The radius must be more than 0";
		return plan;
	}
	const Solid* solidA = doc.findSolid(a.solid);
	const Solid* solidB = doc.findSolid(b.solid);
	if (!solidA || !solidB || (a.solid == b.solid && a.sideId == b.sideId)) {
		plan.problem = "Select two different faces";
		return plan;
	}
	Corner corner;
	corner.doc = &doc;
	corner.ids = &ids;
	for (const auto& side : solidA->sides) if (side.id == a.sideId) corner.sideA = &side;
	for (const auto& side : solidB->sides) if (side.id == b.sideId) corner.sideB = &side;
	const auto geometryA = buildBrushGeometry(*solidA), geometryB = buildBrushGeometry(*solidB);
	const FaceGeometry* faceA = corner.sideA ? faceOf(geometryA, *solidA, a.sideId) : nullptr;
	const FaceGeometry* faceB = corner.sideB ? faceOf(geometryB, *solidB, b.sideId) : nullptr;
	if (!faceA || !faceB) {
		plan.problem = "Select two different faces";
		return plan;
	}
	corner.planeA = corner.sideA->plane();
	corner.planeB = corner.sideB->plane();
	corner.c = std::clamp(dot(corner.planeA.normal, corner.planeB.normal), -1.0, 1.0);
	if (corner.c > 0.995) {
		plan.problem = "Both faces point the same way - pick two faces that meet at a corner";
		return plan;
	}
	if (corner.c < -0.995) {
		plan.problem = "The faces lie opposite each other - pick two faces that meet at a corner";
		return plan;
	}
	corner.angle = std::acos(corner.c);
	const Vec3 axis = cross(corner.planeA.normal, corner.planeB.normal);
	corner.d = normalize(axis);
	// A point on the line where the two planes meet, level with the middle of the faces.
	Vec3 middle{};
	for (const auto& v : faceA->vertices) middle = middle + v;
	for (const auto& v : faceB->vertices) middle = middle + v;
	middle = middle / double(faceA->vertices.size() + faceB->vertices.size());
	const double k = dot(corner.d, middle);
	corner.origin = (cross(corner.planeB.normal, corner.d) * corner.planeA.dist + cross(corner.d, corner.planeA.normal) * corner.planeB.dist +
	                 axis * k) /
	                dot(corner.planeA.normal, cross(corner.planeB.normal, corner.d));

	// Outer corner (each face lies behind the other's plane) or inner corner (in front).
	const auto [sideOfB, partB] = mainSide(faceB->vertices, corner.planeA);
	const auto [sideOfA, partA] = mainSide(faceA->vertices, corner.planeB);
	if (sideOfA == 0 || sideOfB == 0 || sideOfA != sideOfB) {
		plan.problem = "These faces do not form a corner (one lies across the other)";
		return plan;
	}
	plan.inner = sideOfA > 0;
	const double tangent = std::tan(corner.angle / 2);   // distance of the arc's ends from the corner per unit radius
	plan.largest = std::min(reach(corner, partA), reach(corner, partB)) / tangent;
	const auto segmentsFor = [&](double r) { return std::clamp(int(std::floor(corner.angle * r / MIN_CHORD)), 1, plan.segments); };

	if (plan.inner) {
		// Inner corner: a fan of brushes from the corner line up to the arc, along the stretch
		// where both faces run.
		double fromA = 1e30, toA = -1e30, fromB = 1e30, toB = -1e30;
		for (const auto& v : partA) fromA = std::min(fromA, corner.along(v)), toA = std::max(toA, corner.along(v));
		for (const auto& v : partB) fromB = std::min(fromB, corner.along(v)), toB = std::max(toB, corner.along(v));
		const double from = std::max(fromA, fromB), to = std::min(toA, toB);
		if (to - from < MIN_PIECE) {
			plan.problem = "The faces do not run alongside each other";
			return plan;
		}
		const auto fillet = [&](double r) -> std::optional<std::vector<Solid>> {
			const int n = segmentsFor(r);
			const Vec3 center = corner.origin + (corner.planeA.normal + corner.planeB.normal) * (r / (1 + corner.c));
			std::vector<Solid> pieces;
			for (int i = 0; i < n; ++i) {
				const Vec3 p = center - slerp(corner.planeA.normal, corner.planeB.normal, corner.angle, double(i) / n) * r;
				const Vec3 q = center - slerp(corner.planeA.normal, corner.planeB.normal, corner.angle, double(i + 1) / n) * r;
				const Vec3 o = corner.origin;
				std::vector<Vec3> points;
				for (const double s : {from, to}) {
					for (const Vec3& v : {o, p, q}) points.push_back(v + corner.d * s);
				}
				auto piece = solidFromPoints(ids, points, corner.sideA->material);
				if (!piece) return std::nullopt;
				// Textures: the chord and the ends from the face it lies nearest to.
				const Side* from = (i + 0.5) / n < 0.5 ? corner.sideA : corner.sideB;
				Solid textured;
				textured.id = piece->id;
				textured.extra = piece->extra;
				for (const auto& side : piece->sides) textured.sides.push_back(makeSide(ids, side.plane(), from->material, from));
				tidyPlanePoints(textured);
				if (!usable(textured)) return std::nullopt;
				pieces.push_back(std::move(textured));
			}
			return pieces;
		};
		double r = std::min(radius, plan.largest);
		auto pieces = r >= 0.5 ? fillet(r) : std::nullopt;
		if (!pieces && r >= 0.5) {
			double lo = 0, hi = r;
			for (int i = 0; i < 24; ++i) {
				const double mid = (lo + hi) / 2;
				(fillet(mid) ? lo : hi) = mid;
			}
			r = std::floor(lo * 2) / 2;
			pieces = r >= 0.5 ? fillet(r) : std::nullopt;
		}
		if (!pieces) {
			plan.problem = "There is no room for a rounding at this corner";
			return plan;
		}
		plan.clamped = r < radius - 1e-6;
		plan.radius = r;
		plan.segments = segmentsFor(r);
		plan.added = std::move(*pieces);
		plan.addedEntity = a.solid.entityId;
		return plan;
	}

	// Outer corner: the brushes of both faces are cut by the arc's planes.
	std::vector<SolidRef> targets{a.solid};
	if (!(b.solid == a.solid)) targets.push_back(b.solid);
	std::vector<const Solid*> originals;
	Bounds around;
	for (const auto& ref : targets) {
		originals.push_back(doc.findSolid(ref));
		around.add(buildBrushGeometry(*originals.back()).bounds);
	}
	// Neighbours that may cover the corner (every other brush near the two).
	corner.neighbours = solidsNear(doc, around, 2, targets);

	// Pieces: parts of the brushes in front of a face's plane stay as they are; the rest is
	// split where the neighbours' cover begins or ends.
	std::vector<Piece> pieces;
	for (std::size_t t = 0; t < originals.size(); ++t) pieces.push_back({*originals[t], false, t});
	const auto splitAll = [&](const Plane& plane, const Side& capTexture, bool markFrontCovered) {
		std::vector<Piece> next;
		for (auto& piece : pieces) {
			auto [front, back] = splitSolid(ids, piece.solid, plane, capTexture);
			if (front && back) {
				next.push_back({std::move(*back), piece.covered, piece.target});
				next.push_back({std::move(*front), piece.covered || markFrontCovered, piece.target});
			} else {
				next.push_back(std::move(piece));
			}
		}
		pieces = std::move(next);
	};
	// Outside the corner's wedge the arc must not cut anything.
	splitAll(corner.planeA, *corner.sideA, true);
	splitAll(corner.planeB, *corner.sideB, true);
	double from = 1e30, to = -1e30;
	for (const auto* solid : originals) {
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			for (const auto& v : face.vertices) from = std::min(from, corner.along(v)), to = std::max(to, corner.along(v));
		}
	}
	for (const double s : coverChanges(corner, from, to)) {
		const Side* surface = corner.surfaceAt(s);
		Plane plane{corner.d, s + dot(corner.d, corner.origin)};
		if (surface) {
			plane = surface->plane();
		}
		splitAll(plane, surface ? *surface : *corner.sideA, false);
	}
	for (auto& piece : pieces) {
		if (piece.covered) continue;
		double lo = 1e30, hi = -1e30;
		for (const auto& face : buildBrushGeometry(piece.solid).faces) {
			for (const auto& v : face.vertices) lo = std::min(lo, corner.along(v)), hi = std::max(hi, corner.along(v));
		}
		piece.covered = corner.covered((lo + hi) / 2);
		if (piece.covered) ++plan.keptSquare;
	}

	// Brushes that touched before must still touch afterwards (no hole between the two walls),
	// measured where the rounding happens (parts left square do not count).
	const auto touching = [](const std::vector<Piece>& all) {
		double area = 0;
		for (const auto& p : all) {
			for (const auto& q : all) {
				if (p.target == 0 && q.target == 1 && !p.covered && !q.covered) area += touchArea(p.solid, q.solid);
			}
		}
		return area;
	};
	const double touchedBefore = originals.size() == 2 ? touching(pieces) : 0;
	const auto cut = [&](double r) -> std::optional<std::vector<Piece>> {
		const auto planes = arcPlanes(corner, r, segmentsFor(r));
		std::vector<Piece> out;
		for (const auto& piece : pieces) {
			if (piece.covered) {
				out.push_back(piece);
				continue;
			}
			auto result = cutPiece(corner, piece.solid, planes);
			if (!result) return std::nullopt;
			out.push_back({std::move(*result), false, piece.target});
		}
		// A quarter of the contact must stay, else the corner gets paper thin between them.
		if (touchedBefore > 1 && touching(out) < std::max(0.5, touchedBefore / 4)) return std::nullopt;
		return out;
	};
	double r = std::min(radius, plan.largest);
	auto result = r >= 0.5 ? cut(r) : std::nullopt;
	if (!result && r >= 0.5) {
		double lo = 0, hi = r;
		for (int i = 0; i < 24; ++i) {
			const double mid = (lo + hi) / 2;
			(cut(mid) ? lo : hi) = mid;
		}
		r = std::floor(lo * 2) / 2;
		result = r >= 0.5 ? cut(r) : std::nullopt;
	}
	if (!result) {
		plan.problem = "There is no room for a rounding at this corner";
		return plan;
	}
	plan.clamped = r < radius - 1e-6;
	plan.radius = r;
	plan.segments = segmentsFor(r);
	plan.largest = std::max(plan.largest, r);
	for (std::size_t t = 0; t < targets.size(); ++t) {
		EdgeRoundPlan::Replacement replacement{targets[t], {}};
		for (auto& piece : *result) {
			if (piece.target == t) replacement.pieces.push_back(std::move(piece.solid));
		}
		// The piece with the original's sides keeps its id (selection and face references stay).
		std::stable_partition(replacement.pieces.begin(), replacement.pieces.end(),
		                      [&](const Solid& s) { return s.id == targets[t].solidId; });
		for (std::size_t i = 0; i < replacement.pieces.size(); ++i) {
			if (i == 0) {
				replacement.pieces[i].id = targets[t].solidId;
			} else if (replacement.pieces[i].id == targets[t].solidId) {
				replacement.pieces[i].id = ids.newSolidId();
			}
		}
		plan.replacements.push_back(std::move(replacement));
	}
	return plan;
}

void applyEdgeRound(MapDocument& doc, Transaction& transaction, const EdgeRoundPlan& plan) {
	if (!plan.ok()) return;
	for (const auto& replacement : plan.replacements) {
		auto* entity = doc.findEntity(replacement.ref.entityId);
		if (!entity) continue;
		transaction.touchSolid(replacement.ref);
		std::erase_if(entity->solids, [&](const Solid& s) { return s.id == replacement.ref.solidId; });
		for (const auto& piece : replacement.pieces) {
			const SolidRef ref{replacement.ref.entityId, piece.id};
			if (!(ref == replacement.ref)) transaction.touchSolid(ref);
			entity->solids.push_back(piece);
		}
	}
	if (auto* entity = doc.findEntity(plan.addedEntity)) {
		for (const auto& solid : plan.added) {
			transaction.touchSolid({plan.addedEntity, solid.id});
			entity->solids.push_back(solid);
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
