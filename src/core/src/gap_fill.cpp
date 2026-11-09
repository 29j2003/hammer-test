// gap_fill.cpp - "Fill gap" between the selected brushes (see gap_fill.h).

#include <sme/gap_fill.h>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/edge_round.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace sme {

namespace {

constexpr double MIN_VOLUME = 1.0;      ///< Pieces smaller than this are slivers, left out
constexpr double SAME_PLANE = 0.05;     ///< Planes this close (units) are the same plane

bool isTool(const std::string& material) {
	if (material.size() < 6) return false;
	std::string head = material.substr(0, 6);
	for (auto& c : head) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return head == "TOOLS/";
}

double faceArea(const FaceGeometry& face) {
	Vec3 sum;
	for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
		sum = sum + cross(face.vertices[i] - face.vertices[0], face.vertices[i + 1] - face.vertices[0]);
	}
	return length(sum) * 0.5;
}

/// The material the selection uses on most of its surface (tool materials only if nothing else).
std::string commonMaterial(const std::vector<const Solid*>& solids) {
	std::map<std::string, double> area, toolArea;
	for (const Solid* solid : solids) {
		const auto geometry = buildBrushGeometry(*solid);
		for (const auto& face : geometry.faces) {
			const auto& material = solid->sides[static_cast<std::size_t>(face.sideIndex)].material;
			(isTool(material) ? toolArea : area)[material] += faceArea(face);
		}
	}
	const auto& pick = area.empty() ? toolArea : area;
	std::string best;
	double most = -1;
	for (const auto& [material, a] : pick) {
		if (a > most) {
			most = a;
			best = material;
		}
	}
	return best.empty() ? std::string("DEV/DEV_MEASUREGENERIC01") : best;
}

/// The box between two boxes: overlapping axes keep the overlap, separated axes span the gap.
/// Empty if the boxes overlap on every axis (no gap) or the gap is thinner than half a unit.
Bounds boxBetween(const Bounds& a, const Bounds& b) {
	Bounds gap;
	bool separated = false;
	for (int axis = 0; axis < 3; ++axis) {
		double lo, hi;
		if (a.max[axis] <= b.min[axis] + 1e-6) {
			lo = a.max[axis];
			hi = b.min[axis];
			separated = true;
		} else if (b.max[axis] <= a.min[axis] + 1e-6) {
			lo = b.max[axis];
			hi = a.min[axis];
			separated = true;
		} else {
			lo = std::max(a.min[axis], b.min[axis]);
			hi = std::min(a.max[axis], b.max[axis]);
		}
		if (hi - lo < 0.5) return {};
		gap.min[axis] = lo;
		gap.max[axis] = hi;
	}
	return separated ? gap : Bounds{};
}

/// Copies the look of `from` (material, alignment) onto `to`; never a displacement.
void copyLook(Side& to, const Side& from) {
	to.material = from.material;
	to.u = from.u;
	to.v = from.v;
	to.rotation = from.rotation;
	to.lightmapScale = from.lightmapScale;
}

/// Gives each side of the fill the texture of the selected side in the same plane (facing the same
/// way: the texture runs on; facing the other way: the side lies against that brush), else the
/// material of the selected side facing most the same way, else `fallback`; world aligned then.
void textureFill(Solid& fill, const std::vector<const Solid*>& selected, const std::string& fallback) {
	for (auto& side : fill.sides) {
		side.extra.clear();   // never a displacement
		const Plane plane = side.plane();
		const Side* same = nullptr;
		const Side* facing = nullptr;
		const Side* closest = nullptr;
		double closestDot = 0.5;
		for (const Solid* solid : selected) {
			for (const auto& other : solid->sides) {
				const Plane o = other.plane();
				const double d = dot(o.normal, plane.normal);
				if (d > 0.9999 && std::abs(o.dist - plane.dist) < SAME_PLANE) {
					same = &other;
				} else if (d < -0.9999 && std::abs(o.dist + plane.dist) < SAME_PLANE) {
					facing = &other;
				} else if (d > closestDot && !isTool(other.material)) {
					closestDot = d;
					closest = &other;
				}
			}
		}
		if (same) {
			copyLook(side, *same);
		} else {
			side.material = facing ? facing->material : closest ? closest->material : fallback;
			side.rotation = 0;
			alignToWorld(side);
		}
	}
}

/// Whether a point projected along `normal` onto the face's plane lies on the face (a convex
/// polygon, `margin` units of slack).
bool onFace(const FaceGeometry& face, const Vec3& p, double margin) {
	const std::size_t n = face.vertices.size();
	for (std::size_t i = 0; i < n; ++i) {
		const Vec3& a = face.vertices[i];
		const Vec3& b = face.vertices[(i + 1) % n];
		const Vec3 inward = normalize(cross(face.normal, b - a));   // counter-clockwise seen from outside
		if (dot(p - a, inward) < -margin) return false;
	}
	return true;
}

/// The corners of the faces of `a` that face `b`: something of `b` lies in front of the face,
/// over the face (not off to its side). These faces and the ones of `b` facing back bound the gap.
std::vector<Vec3> facingCorners(const BrushGeometry& ga, const Solid& b, const BrushGeometry& gb) {
	std::vector<Vec3> corners;
	std::vector<Vec3> bCorners;
	for (const auto& face : gb.faces) {
		for (const auto& v : face.vertices) bCorners.push_back(v);
	}
	for (const auto& face : ga.faces) {
		if (face.vertices.size() < 3) continue;
		const Plane plane{face.normal, dot(face.normal, face.vertices[0])};
		bool facing = false;
		// A corner of b in front of the face and over it.
		for (const auto& c : bCorners) {
			const double d = plane.distanceTo(c);
			if (d > 0.5 && onFace(face, c - face.normal * d, -0.01)) {
				facing = true;
				break;
			}
		}
		// Or straight out of the face (its middle, its corners pulled in a little) b is hit.
		if (!facing) {
			Vec3 centre;
			for (const auto& v : face.vertices) centre = centre + v;
			centre = centre * (1.0 / double(face.vertices.size()));
			std::vector<Vec3> starts{centre};
			for (const auto& v : face.vertices) starts.push_back(v + (centre - v) * 0.05);
			for (const auto& start : starts) {
				const auto hit = intersect(Ray{start, face.normal}, b);
				if (hit && hit->distance > 0.5) {
					facing = true;
					break;
				}
			}
		}
		if (facing) corners.insert(corners.end(), face.vertices.begin(), face.vertices.end());
	}
	return corners;
}

/// Joins pieces whose union is convex (subtracting splits more than needed).
void joinPieces(MapDocument& ids, std::vector<Solid>& pieces) {
	for (bool joined = true; joined;) {
		joined = false;
		for (std::size_t i = 0; i < pieces.size() && !joined; ++i) {
			for (std::size_t j = i + 1; j < pieces.size() && !joined; ++j) {
				const auto merged = mergeSolids(ids, {pieces[i], pieces[j]});
				if (!merged || !merged->exact || !brushProblem(merged->solid).empty()) continue;
				const double sum = solidVolume(pieces[i]) + solidVolume(pieces[j]);
				if (std::abs(solidVolume(merged->solid) - sum) > 0.5) continue;
				pieces[i] = merged->solid;
				pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(j));
				joined = true;
			}
		}
	}
}

} // namespace

double GapFillPlan::volume() const {
	double v = 0;
	for (const auto& s : fill) v += solidVolume(s);
	return v;
}

GapFillPlan planGapFill(const MapDocument& doc, MapDocument& ids, const std::vector<SolidRef>& selected, const GapFillOptions& options) {
	GapFillPlan plan;
	std::vector<const Solid*> solids;
	std::vector<Bounds> boxes;
	for (const auto& ref : selected) {
		const Solid* solid = doc.findSolid(ref);
		if (!solid) continue;
		const auto geometry = buildBrushGeometry(*solid);
		if (!geometry.valid) continue;
		solids.push_back(solid);
		boxes.push_back(geometry.bounds);
	}
	if (solids.size() < 2) {
		plan.notes.push_back("select two or more brushes around the gap");
		return plan;
	}
	plan.material = options.material.empty() ? commonMaterial(solids) : options.material;

	// The space to fill, before what already stands there is cut out.
	std::vector<Solid> pieces;
	std::vector<BrushGeometry> geometries;
	for (const Solid* solid : solids) geometries.push_back(buildBrushGeometry(*solid));
	if (options.kind == GapFillOptions::Kind::Hull) {
		// Per pair: the hull of the faces turned towards each other (the gap, not the whole hull:
		// an L of two road pieces gets the wedge at the joint, not the inside of the L).
		for (std::size_t i = 0; i < solids.size(); ++i) {
			for (std::size_t j = i + 1; j < solids.size(); ++j) {
				auto points = facingCorners(geometries[i], *solids[j], geometries[j]);
				const auto back = facingCorners(geometries[j], *solids[i], geometries[i]);
				if (points.empty() || back.empty()) continue;
				points.insert(points.end(), back.begin(), back.end());
				if (auto hull = solidFromPoints(ids, points, plan.material)) {
					// Pairs overlap where three brushes meet: what an earlier pair fills is cut out.
					std::vector<Solid> parts{std::move(*hull)};
					for (const auto& earlier : pieces) {
						std::vector<Solid> next;
						for (const auto& part : parts) {
							for (auto& rest : subtractSolid(ids, part, earlier)) next.push_back(std::move(rest));
						}
						parts = std::move(next);
					}
					for (auto& part : parts) pieces.push_back(std::move(part));
				}
			}
		}
	} else {
		for (std::size_t i = 0; i < boxes.size(); ++i) {
			for (std::size_t j = i + 1; j < boxes.size(); ++j) {
				const Bounds gap = boxBetween(boxes[i], boxes[j]);
				if (!gap.empty()) pieces.push_back(makeBlock(ids, gap, plan.material));
			}
		}
	}
	if (pieces.empty()) {
		plan.notes.push_back(options.kind == GapFillOptions::Kind::Box ? "the brushes touch or overlap: no gap between their boxes"
		                                                                : "no faces of the brushes face each other across a gap");
		return plan;
	}
	Bounds around;
	for (const auto& piece : pieces) around.add(buildBrushGeometry(piece).bounds);

	// Everything in the way (the selected brushes too) is cut out of the fill.
	for (const auto& near : solidsNear(doc, around, 0.5)) {
		std::vector<Solid> next;
		for (const auto& piece : pieces) {
			for (auto& part : subtractSolid(ids, piece, *near.solid)) next.push_back(std::move(part));
		}
		pieces = std::move(next);
		if (pieces.empty()) break;
	}
	joinPieces(ids, pieces);
	std::size_t slivers = 0;
	for (auto& piece : pieces) {
		tidyPlanePoints(piece);
		if (solidVolume(piece) < MIN_VOLUME || !brushProblem(piece).empty()) {
			++slivers;
			continue;
		}
		textureFill(piece, solids, plan.material);
		plan.fill.push_back(std::move(piece));
	}
	if (slivers > 0) plan.notes.push_back(std::to_string(slivers) + " sliver piece(s) left out");
	if (plan.fill.empty()) {
		plan.notes.push_back("no empty space between the selected brushes");
	}
	return plan;
}

std::pair<Change, std::vector<SolidRef>> applyGapFill(MapDocument& doc, const GapFillPlan& plan, const std::string& name) {
	Transaction t(doc, name);
	std::vector<SolidRef> refs;
	for (const auto& solid : plan.fill) {
		const SolidRef ref{0, solid.id};
		t.touchSolid(ref);
		doc.world.solids.push_back(solid);
		refs.push_back(ref);
	}
	return {t.finish(), refs};
}

} // namespace sme
