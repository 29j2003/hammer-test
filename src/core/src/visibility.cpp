// visibility.cpp - Kinds of objects for the view filters (see visibility.h).

#include <sme/visibility.h>

#include <sme/text.h>

namespace sme {

namespace {

/// Kind of a world brush from its faces: all sky, all tool, or normal (World).
ObjectKind brushKind(const Solid& solid) {
	bool allTool = !solid.sides.empty(), allSky = !solid.sides.empty();
	for (const auto& side : solid.sides) {
		const bool sky = isSkyMaterialName(side.material);
		allSky = allSky && sky;
		allTool = allTool && (sky || isToolMaterialName(side.material));
	}
	if (allSky) return ObjectKind::Sky;
	if (allTool) return ObjectKind::ToolBrushes;
	return ObjectKind::World;
}

} // namespace

bool isToolMaterialName(std::string_view material) {
	return startsWithI(material, "tools/") || startsWithI(material, "tools\\");
}

bool isSkyMaterialName(std::string_view material) {
	return startsWithI(material, "tools/toolsskybox") || startsWithI(material, "tools\\toolsskybox");
}

ObjectKind entityKind(const Entity& entity, const EntityCatalog* catalog) {
	const std::string classname = entity.classname();
	if (iequals(classname, "env_cubemap") || iequals(classname, "parallax_obb")) return ObjectKind::Cubemaps;
	if (!entity.solids.empty()) {
		if (startsWithI(classname, "trigger_")) return ObjectKind::Triggers;
		// Brush entities made only of tool faces (func_areaportal, func_clip_vphysics, ...).
		for (const auto& solid : entity.solids) {
			if (brushKind(solid) == ObjectKind::World) return ObjectKind::BrushEntities;
		}
		return ObjectKind::Triggers;
	}
	if (startsWithI(classname, "npc_") || startsWithI(classname, "monster_")) return ObjectKind::Npcs;
	if (startsWithI(classname, "light") || iequals(classname, "env_projectedtexture") || iequals(classname, "env_sun") ||
	    iequals(classname, "point_spotlight")) {
		return ObjectKind::Lights;
	}
	if (!edit::entityModel(entity, catalog).empty()) return ObjectKind::Models;
	return ObjectKind::Helpers;
}

Selection objectsOfKinds(const MapDocument& doc, std::uint32_t hiddenKinds, const EntityCatalog* catalog) {
	Selection out;
	if (hiddenKinds == 0) return out;
	const auto hides = [&](ObjectKind kind) { return (hiddenKinds & static_cast<std::uint32_t>(kind)) != 0; };
	for (const auto& solid : doc.world.solids) {
		const ObjectKind kind = brushKind(solid);
		if (hides(kind)) {
			out.solids.push_back({0, solid.id});
			continue;
		}
		if (kind != ObjectKind::World) continue;
		// Normal brushes: their sky and tool faces alone.
		for (const auto& side : solid.sides) {
			const bool sky = isSkyMaterialName(side.material);
			if ((sky && hides(ObjectKind::Sky)) || (!sky && isToolMaterialName(side.material) && hides(ObjectKind::ToolBrushes))) {
				out.faces.push_back({{0, solid.id}, side.id});
			}
		}
	}
	for (const auto& entity : doc.entities) {
		if (hides(entityKind(entity, catalog))) {
			out.entities.push_back(entity.id);
		}
	}
	return out;
}

} // namespace sme
