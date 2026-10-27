// map_tools.cpp - Cordons, track chains, drop to floor (see map_tools.h).

#include <sme/map_tools.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <set>

namespace sme {

namespace {

bool readBox(const KvNode& node, Bounds& box) {
	const auto parse = [](std::string text, Vec3& out) {
		std::erase_if(text, [](char c) { return c == '(' || c == ')'; });
		return parseVec3(text, out);
	};
	Vec3 mins, maxs;
	if (!parse(node.get("mins"), mins) || !parse(node.get("maxs"), maxs)) {
		return false;
	}
	box = {};
	box.add(mins);
	box.add(maxs);
	return !box.empty();
}

std::string boxText(const Vec3& v) {
	return "(" + formatVec3(v) + ")";
}

bool overlaps(const Bounds& a, const Bounds& b) {
	for (int i = 0; i < 3; ++i) {
		if (a.max[i] < b.min[i] || b.max[i] < a.min[i]) {
			return false;
		}
	}
	return true;
}

} // namespace

CordonSet readCordons(const MapDocument& doc) {
	CordonSet set;
	for (const auto& block : doc.footerBlocks) {
		if (iequals(block.key, "cordons")) {
			set.active = block.get("active", "0") == "1";
			for (const auto& child : block.children) {
				if (!child.block || !iequals(child.key, "cordon")) {
					continue;
				}
				Cordon cordon;
				cordon.name = child.get("name", "cordon");
				cordon.active = child.get("active", "1") == "1";
				if (const auto* box = child.find("box"); box && readBox(*box, cordon.box)) {
					set.cordons.push_back(std::move(cordon));
				}
			}
		} else if (iequals(block.key, "cordon")) {
			// Older Hammer: one cordon with mins/maxs directly in the block.
			Cordon cordon;
			if (readBox(block, cordon.box)) {
				set.active = block.get("active", "0") == "1";
				set.cordons.push_back(std::move(cordon));
			}
		}
	}
	return set;
}

void writeCordons(MapDocument& doc, const CordonSet& cordons) {
	std::erase_if(doc.footerBlocks, [](const KvNode& block) { return iequals(block.key, "cordons") || iequals(block.key, "cordon"); });
	if (cordons.cordons.empty()) {
		return;
	}
	auto block = KvNode::makeBlock("cordons");
	block.set("active", cordons.active ? "1" : "0");
	for (const auto& cordon : cordons.cordons) {
		auto node = KvNode::makeBlock("cordon");
		node.set("name", cordon.name);
		node.set("active", cordon.active ? "1" : "0");
		auto box = KvNode::makeBlock("box");
		box.set("mins", boxText(cordon.box.min));
		box.set("maxs", boxText(cordon.box.max));
		node.children.push_back(std::move(box));
		block.children.push_back(std::move(node));
	}
	doc.footerBlocks.push_back(std::move(block));
}

MapDocument cordonedCopy(const MapDocument& doc, const CordonSet& cordons, const EntityCatalog* catalog, const std::string& sealMaterial) {
	MapDocument copy = doc;
	std::vector<Bounds> boxes;
	for (const auto& cordon : cordons.cordons) {
		if (cordon.active) {
			boxes.push_back(cordon.box);
		}
	}
	if (!cordons.active || boxes.empty()) {
		return copy;
	}
	const auto inside = [&](const Bounds& b) {
		return std::any_of(boxes.begin(), boxes.end(), [&](const Bounds& box) { return overlaps(b, box); });
	};
	std::erase_if(copy.world.solids, [&](const Solid& solid) { return !inside(buildBrushGeometry(solid).bounds); });
	std::erase_if(copy.entities, [&](const Entity& entity) { return !inside(edit::entityBounds(entity, catalog)); });
	// Seal every cordon with six slabs just outside it, like Hammer does.
	constexpr double THICKNESS = 16;
	for (const auto& box : boxes) {
		const Vec3 lo = box.min, hi = box.max;
		for (int axis = 0; axis < 3; ++axis) {
			for (int side = 0; side < 2; ++side) {
				Bounds slab;
				Vec3 a = lo - Vec3{THICKNESS, THICKNESS, THICKNESS}, b = hi + Vec3{THICKNESS, THICKNESS, THICKNESS};
				if (side == 0) {
					b[axis] = lo[axis];
				} else {
					a[axis] = hi[axis];
				}
				slab.add(a);
				slab.add(b);
				copy.world.solids.push_back(makeBlock(copy, slab, sealMaterial));
			}
		}
	}
	return copy;
}

std::vector<int> trackChain(const MapDocument& doc, int entityId) {
	std::vector<int> chain;
	std::set<int> seen;
	std::vector<int> todo{entityId};
	while (!todo.empty()) {
		const int id = todo.back();
		todo.pop_back();
		if (!seen.insert(id).second) {
			continue;
		}
		const auto* entity = doc.findEntity(id);
		if (!entity || id == 0) {
			continue;
		}
		chain.push_back(id);
		const auto name = entity->get("targetname");
		const std::vector<std::string> next{entity->get("target"), entity->get("altpath")};
		for (const auto& other : doc.entities) {
			const auto otherName = other.get("targetname");
			const bool forward = !otherName.empty() && std::any_of(next.begin(), next.end(), [&](const std::string& n) { return iequals(n, otherName); });
			const bool backward = !name.empty() && (iequals(other.get("target"), name) || iequals(other.get("altpath"), name));
			if (forward || backward) {
				todo.push_back(other.id);
			}
		}
	}
	std::sort(chain.begin(), chain.end());
	return chain;
}

namespace edit {

Change dropToFloor(MapDocument& doc, const Selection& selection, const EntityCatalog* catalog) {
	Transaction t(doc, "Drop to floor");
	// Everything that is not selected can be landed on.
	std::vector<const Solid*> solids;
	std::vector<Bounds> boxes;
	const auto collect = [&](const Entity& entity, int entityId) {
		for (const auto& solid : entity.solids) {
			if (!selection.contains(SolidRef{entityId, solid.id}) && !selection.containsEntity(entityId)) {
				solids.push_back(&solid);
			}
		}
	};
	collect(doc.world, 0);
	for (const auto& entity : doc.entities) {
		collect(entity, entity.id);
		if (entity.solids.empty() && !selection.containsEntity(entity.id)) {
			boxes.push_back(entityBounds(entity, catalog));
		}
	}
	// Five rays down from the bottom of the object (corners and middle): the shortest hit wins.
	const auto fall = [&](const Bounds& b) -> double {
		double best = 1e30;
		const Vec3 lo = b.min, hi = b.max;
		const double inset = 0.5;
		const Vec3 starts[5] = {{lo.x + inset, lo.y + inset, lo.z}, {hi.x - inset, lo.y + inset, lo.z}, {hi.x - inset, hi.y - inset, lo.z},
		                        {lo.x + inset, hi.y - inset, lo.z}, {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z}};
		for (const auto& start : starts) {
			const Ray ray{start + Vec3{0, 0, 0.01}, {0, 0, -1}};
			for (const auto* solid : solids) {
				if (const auto hit = intersect(ray, *solid); hit && hit->distance >= 0) {
					best = std::min(best, hit->distance - 0.01);
				}
			}
			for (const auto& box : boxes) {
				if (const double d = intersect(ray, box); d >= 0) {
					best = std::min(best, d - 0.01);
				}
			}
		}
		return best < 1e29 ? std::max(0.0, best) : 0.0;
	};
	for (const auto& ref : selection.solids) {
		auto* solid = doc.findSolid(ref);
		if (!solid) continue;
		const double d = fall(buildBrushGeometry(*solid).bounds);
		if (d > 0.01) {
			t.touchSolid(ref);
			translateSolid(*solid, {0, 0, -std::round(d * 100) / 100}, true);
		}
	}
	for (const int id : selection.entities) {
		auto* entity = id == 0 ? nullptr : doc.findEntity(id);
		if (!entity) continue;
		const double d = fall(entityBounds(*entity, catalog));
		if (d <= 0.01) continue;
		const Vec3 delta{0, 0, -std::round(d * 100) / 100};
		t.touchEntity(id);
		for (auto& solid : entity->solids) {
			translateSolid(solid, delta, true);
		}
		if (entity->hasOrigin()) {
			entity->setOrigin(entity->origin() + delta);
		}
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
