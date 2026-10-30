// shape_fit.cpp - Shapes fitted into the map around them (see shape_fit.h).

#include <sme/shape_fit.h>

#include <sme/brush.h>
#include <sme/csg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace sme {

namespace {

constexpr double MIN_CHORD = 2.0;     ///< Shortest side of a round shape (shorter ones make tiny faces)
constexpr double MIN_CHORD_COARSE = 4.0;   ///< The same where makeShape() rounds corners to whole units (32 units and wider)
constexpr double OUTSIDE = 0.5;       ///< How far outside a box side a neighbour is looked for
constexpr double MIN_VOLUME = 1.0;    ///< Fill pieces smaller than this are slivers, left out

std::string units(double v) {
	char text[32];
	std::snprintf(text, sizeof text, "%g", v);
	return text;
}

bool usable(const Solid& solid) {
	return isConvexValid(solid) && solidVolume(solid) > MIN_VOLUME;
}

/// Whether a neighbour holds the point.
bool occupied(const std::vector<NearbySolid>& neighbours, const Vec3& p) {
	return std::any_of(neighbours.begin(), neighbours.end(), [&](const NearbySolid& n) {
		return p.x >= n.bounds.min.x - 0.01 && p.x <= n.bounds.max.x + 0.01 && p.y >= n.bounds.min.y - 0.01 && p.y <= n.bounds.max.y + 0.01 &&
		       p.z >= n.bounds.min.z - 0.01 && p.z <= n.bounds.max.z + 0.01 && solidContains(*n.solid, p);
	});
}

/// `pieces` with every cutter carved out of them (slivers dropped).
std::vector<Solid> carveAll(MapDocument& ids, std::vector<Solid> pieces, const std::vector<const Solid*>& cutters) {
	for (const Solid* cutter : cutters) {
		std::vector<Solid> next;
		for (const auto& piece : pieces) {
			for (auto& part : subtractSolid(ids, piece, *cutter)) next.push_back(std::move(part));
		}
		pieces = std::move(next);
		if (pieces.empty()) break;
	}
	std::erase_if(pieces, [](const Solid& s) { return !usable(s); });
	return pieces;
}

/// Where the line through `p` and `q` crosses x = value (axis 0) or y = value (axis 1).
Vec3 crossing(const Vec3& p, const Vec3& q, int axis, double value) {
	const double span = q[axis] - p[axis];
	if (std::abs(span) < 1e-9) return p;
	const double t = std::clamp((value - p[axis]) / span, 0.0, 1.0);
	return p + (q - p) * t;
}

/// Arch against walls: the space between its outer side and the sides of its box that lie
/// against other brushes, as triangular prisms from each piece's outer face to the box corner of
/// its quarter (one per piece and quarter).
std::vector<Solid> archBackFill(MapDocument& ids, const Bounds& box, const ShapeOptions& options, const std::string& material,
                                const std::vector<NearbySolid>& neighbours) {
	std::vector<Solid> out;
	if (options.addHeight != 0) return out;   // a spiral has no wall behind it
	const Vec3 c = box.center();
	const double zMid = (box.min.z + box.max.z) / 2;
	// Which sides of the box lie against something (a few samples along each half of a side).
	const auto against = [&](int axis, bool maxSide, bool upperHalf) {
		const int other = 1 - axis;
		const double lo = upperHalf ? c[other] : box.min[other], hi = upperHalf ? box.max[other] : c[other];
		for (const double t : {0.25, 0.5, 0.75}) {
			Vec3 p{0, 0, zMid};
			p[axis] = maxSide ? box.max[axis] + OUTSIDE : box.min[axis] - OUTSIDE;
			p[other] = lo + (hi - lo) * t;
			if (occupied(neighbours, p)) return true;
		}
		return false;
	};
	bool quarterAgainst[2][2];   // [x side max][y side max]
	for (int xs = 0; xs < 2; ++xs) {
		for (int ys = 0; ys < 2; ++ys) quarterAgainst[xs][ys] = against(0, xs == 1, ys == 1) || against(1, ys == 1, xs == 1);
	}
	const auto points = archOuterPoints(box, options);
	const double arc = std::clamp(options.arc, 1.0, 360.0);
	const int pieces = int(points.size()) - 1;
	for (int i = 0; i < pieces; ++i) {
		const double a0 = options.startAngle + arc * i / pieces, a1 = options.startAngle + arc * (i + 1) / pieces;
		Vec3 p0 = points[std::size_t(i)], p1 = points[std::size_t(i) + 1];
		// The piece's stretch split where it crosses the box's middle lines (each part has one corner).
		std::vector<std::pair<double, Vec3>> cuts{{a0, p0}};
		for (double b = std::ceil(a0 / 90.0) * 90.0; b < a1 - 1e-9; b += 90.0) {
			if (b <= a0 + 1e-9) continue;
			const long quarter = std::lround(b / 90.0);
			const int axis = (quarter % 2 == 0) ? 1 : 0;   // 0°, 180°: crosses y = c.y; 90°, 270°: x = c.x
			cuts.push_back({b, crossing(p0, p1, axis, c[axis])});
		}
		cuts.push_back({a1, p1});
		for (std::size_t k = 0; k + 1 < cuts.size(); ++k) {
			const double middle = (cuts[k].first + cuts[k + 1].first) / 2 * std::numbers::pi / 180;
			const bool xMax = std::cos(middle) > 0, yMax = std::sin(middle) > 0;
			if (!quarterAgainst[xMax][yMax]) continue;
			const Vec3 corner{xMax ? box.max.x : box.min.x, yMax ? box.max.y : box.min.y, 0};
			std::vector<Vec3> prism;
			for (const double z : {box.min.z, box.max.z}) {
				for (const Vec3& v : {cuts[k].second, cuts[k + 1].second, corner}) prism.push_back({v.x, v.y, z});
			}
			if (auto solid = solidFromPoints(ids, prism, material); solid && usable(*solid)) out.push_back(std::move(*solid));
		}
	}
	return out;
}

/// A shape standing in a hole of a floor: the height of the floor's top lying against at least
/// three sides of the box (between its bottom and top), and the side to take the texture from.
std::optional<std::pair<double, const Side*>> floorAround(const Bounds& box, const std::vector<NearbySolid>& neighbours) {
	std::optional<std::pair<double, const Side*>> best;
	for (const auto& n : neighbours) {
		const double h = n.bounds.max.z;
		if (h < box.min.z + 1 || h > box.max.z - 1 || (best && h <= best->first)) continue;
		const Side* top = nullptr;
		for (const auto& side : n.solid->sides) {
			const Plane plane = side.plane();
			if (plane.normal.z > 0.999 && std::abs(plane.dist - h) < 0.01) top = &side;
		}
		if (!top) continue;
		const Vec3 c = box.center();
		int sides = 0;
		for (const Vec3& p : {Vec3{box.min.x - OUTSIDE, c.y, h - OUTSIDE}, Vec3{box.max.x + OUTSIDE, c.y, h - OUTSIDE},
		                      Vec3{c.x, box.min.y - OUTSIDE, h - OUTSIDE}, Vec3{c.x, box.max.y + OUTSIDE, h - OUTSIDE}}) {
			if (occupied(neighbours, p)) ++sides;
		}
		if (sides >= 3) best = std::pair{h, top};
	}
	return best;
}

/// Whether two boxes overlap with some volume (touching does not count).
bool overlapping(const Bounds& a, const Bounds& b) {
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] <= b.min[i] + 0.01 || b.max[i] <= a.min[i] + 0.01) return false;
	}
	return true;
}

} // namespace

ShapeLimits limitShapeOptions(const Bounds& box, const ShapeOptions& options) {
	ShapeLimits limits;
	limits.used = options;
	if (box.empty()) return limits;
	ShapeOptions& o = limits.used;
	const Vec3 size = box.size();
	const double across = std::min(size.x, size.y);
	const double chord = across >= 32 ? MIN_CHORD_COARSE : MIN_CHORD;
	switch (options.kind) {
		case ShapeOptions::Kind::Arch: {
			const double widest = std::max(1.0, std::floor(across / 2 * 2) / 2);
			if (o.wallWidth > widest) {
				o.wallWidth = widest;
				limits.notes.push_back("wall width limited to " + units(widest) + " units");
			}
			const double arc = std::clamp(o.arc, 1.0, 360.0);
			const int most = std::clamp(int(std::floor(arc * std::numbers::pi / 180 * across / 2 / chord)), 1, 64);
			if (o.sides > most) {
				o.sides = most;
				limits.notes.push_back("pieces limited to " + std::to_string(most));
			}
			break;
		}
		case ShapeOptions::Kind::Cylinder:
		case ShapeOptions::Kind::Spike:
		case ShapeOptions::Kind::Sphere: {
			const int most = std::clamp(int(std::floor(std::numbers::pi * across / chord)), 3, 64);
			if (o.sides > most) {
				o.sides = most;
				limits.notes.push_back("sides limited to " + std::to_string(most));
			}
			break;
		}
		case ShapeOptions::Kind::Stairs: {
			const int most = std::max(1, int(std::floor(std::min(size.y, size.z))));
			if (o.steps > most) {
				o.steps = most;
				limits.notes.push_back("steps limited to " + std::to_string(most));
			}
			break;
		}
		default:
			break;
	}
	return limits;
}

std::vector<Solid> ShapePlan::resultSolids() const {
	std::vector<Solid> all = shape;
	all.insert(all.end(), fill.begin(), fill.end());
	for (const auto& r : carved) all.insert(all.end(), r.pieces.begin(), r.pieces.end());
	return all;
}

std::vector<SolidRef> ShapePlan::replacedRefs() const {
	std::vector<SolidRef> refs;
	for (const auto& r : carved) refs.push_back(r.ref);
	return refs;
}

ShapePlan planShape(const MapDocument& doc, MapDocument& ids, const Bounds& box, const ShapeOptions& options, const std::string& material) {
	ShapePlan plan;
	plan.limits = limitShapeOptions(box, options);
	const ShapeOptions& used = plan.limits.used;
	plan.shape = makeShape(ids, box, used, material);
	if (plan.shape.empty() || !used.fit || used.kind == ShapeOptions::Kind::Block) return plan;
	plan.fitted = true;

	const auto neighbours = solidsNear(doc, box, 2);
	if (neighbours.empty()) return plan;

	// Fill: behind an arch drawn against walls, and the floor around a shape standing in a hole.
	std::vector<Solid> fill;
	if (used.kind == ShapeOptions::Kind::Arch) fill = archBackFill(ids, box, used, material, neighbours);
	if (const auto floor = floorAround(box, neighbours)) {
		Bounds slab = box;
		slab.max.z = floor->first;
		Solid block = makeBlock(ids, slab, material);
		for (auto& side : block.sides) side = makeSide(ids, side.plane(), floor->second->material, floor->second);
		std::vector<const Solid*> cutters;
		for (const auto& s : plan.shape) cutters.push_back(&s);
		for (const auto& s : fill) cutters.push_back(&s);
		for (auto& piece : carveAll(ids, {std::move(block)}, cutters)) fill.push_back(std::move(piece));
	}
	// The fill only goes where nothing is yet.
	std::vector<const Solid*> existing;
	for (const auto& n : neighbours) existing.push_back(n.solid);
	plan.fill = carveAll(ids, std::move(fill), existing);

	// Carve: neighbours the shape reaches into are cut around it.
	std::vector<const Solid*> shapeCutters;
	Bounds shapeBox;
	for (const auto& s : plan.shape) {
		shapeCutters.push_back(&s);
		shapeBox.add(buildBrushGeometry(s).bounds);
	}
	for (const auto& n : neighbours) {
		if (!overlapping(n.bounds, shapeBox)) continue;
		const double before = solidVolume(*n.solid);
		auto pieces = carveAll(ids, {*n.solid}, shapeCutters);
		double after = 0;
		for (const auto& p : pieces) after += solidVolume(p);
		if (before - after < 0.5) continue;
		// The piece keeping most of the original keeps its id (selection and references stay).
		std::stable_partition(pieces.begin(), pieces.end(), [&](const Solid& s) { return s.id == n.ref.solidId; });
		for (std::size_t i = 0; i < pieces.size(); ++i) {
			if (i == 0) {
				pieces[i].id = n.ref.solidId;
			} else if (pieces[i].id == n.ref.solidId) {
				pieces[i].id = ids.newSolidId();
			}
		}
		plan.carved.push_back({n.ref, std::move(pieces)});
	}
	return plan;
}

std::pair<Change, std::vector<SolidRef>> applyShapePlan(MapDocument& doc, const ShapePlan& plan, const std::string& name) {
	Transaction t(doc, name);
	std::vector<SolidRef> refs;
	if (!plan.ok()) return {t.finish(), refs};
	for (const auto& replacement : plan.carved) {
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
	for (const auto* list : {&plan.shape, &plan.fill}) {
		for (const auto& solid : *list) {
			const SolidRef ref{0, solid.id};
			t.touchSolid(ref);
			doc.world.solids.push_back(solid);
			refs.push_back(ref);
		}
	}
	return {t.finish(), refs};
}

} // namespace sme
