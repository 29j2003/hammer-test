// material_replace.cpp - Replace materials across the map (see material_replace.h).

#include <sme/material_replace.h>

#include <sme/brush.h>
#include <sme/entity_search.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>

namespace sme {

namespace {

/// Material names compare without "materials/", ".vmt" and case (Hammer writes upper case).
bool materialMatches(const MaterialReplaceOptions& options, std::string_view material) {
	const std::string name = normalizeMaterialName(std::string(material));
	const std::string pattern = normalizeMaterialName(options.find);
	return options.wildcards ? wildcardMatch(pattern, name) : iequals(pattern, name);
}

/// Calls `visit(entityId, solid)` for every brush in scope (`Doc` is MapDocument or const MapDocument).
template <typename Doc, typename F>
void forEachSolid(Doc& doc, const Selection& selection, bool onlySelection, F&& visit) {
	const auto entityInScope = [&](int id) { return !onlySelection || selection.containsEntity(id); };
	for (auto& solid : doc.world.solids) {
		if (!onlySelection || selection.contains(SolidRef{0, solid.id})) visit(0, solid);
	}
	for (auto& entity : doc.entities) {
		if (!entityInScope(entity.id)) continue;
		for (auto& solid : entity.solids) visit(entity.id, solid);
	}
}

/// Overlay / decal key holding a material, or nullptr for other entities.
const char* materialKeyOf(const Entity& entity) {
	const std::string cls = toLower(entity.classname());
	if (cls == "info_overlay" || cls == "info_overlay_transition") return "material";
	if (cls == "infodecal") return "texture";
	return nullptr;
}

/// Does face `side` of solid `ref` count under the options (the face mode selection counts faces only)?
bool faceInScope(const Selection& selection, const MaterialReplaceOptions& options, const SolidRef& ref, const Side& side) {
	if (!options.onlySelection || selection.faces.empty()) return true;
	return selection.contains(FaceRef{ref, side.id});
}

} // namespace

int countMaterialMatches(const MapDocument& doc, const Selection& selection, const MaterialReplaceOptions& options) {
	if (options.find.empty()) return 0;
	const bool facesOnly = options.onlySelection && !selection.faces.empty();
	int count = 0;
	forEachSolid(doc, selection, options.onlySelection && !facesOnly, [&](int entityId, const Solid& solid) {
		for (const auto& side : solid.sides) {
			if (faceInScope(selection, options, {entityId, solid.id}, side) && materialMatches(options, side.material)) ++count;
		}
	});
	if (options.overlaysAndDecals && !facesOnly) {
		for (const auto& entity : doc.entities) {
			const char* key = materialKeyOf(entity);
			if (key && (!options.onlySelection || selection.containsEntity(entity.id)) && materialMatches(options, entity.get(key))) ++count;
		}
	}
	return count;
}

namespace edit {

Change replaceMaterials(MapDocument& doc, const Selection& selection, const MaterialReplaceOptions& options) {
	Transaction t(doc, "Replace material");
	if (options.find.empty() || options.replaceWith.empty()) {
		return t.finish();
	}
	const bool facesOnly = options.onlySelection && !selection.faces.empty();
	// Hammer stores materials in upper case; keep that style.
	std::string newName = normalizeMaterialName(options.replaceWith);
	std::transform(newName.begin(), newName.end(), newName.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	forEachSolid(doc, selection, options.onlySelection && !facesOnly, [&](int entityId, Solid& solid) {
		const SolidRef ref{entityId, solid.id};
		bool touched = false;
		for (auto& side : solid.sides) {
			if (!faceInScope(selection, options, ref, side) || !materialMatches(options, side.material)) continue;
			if (!touched) {
				// Brushes of an entity are recorded with their entity.
				if (entityId == 0) {
					t.touchSolid(ref);
				} else {
					t.touchEntity(entityId);
				}
				touched = true;
			}
			side.material = newName;
			if (!options.keepAlignment) alignToWorld(side);
		}
	});
	if (options.overlaysAndDecals && !facesOnly) {
		for (auto& entity : doc.entities) {
			const char* key = materialKeyOf(entity);
			if (!key || (options.onlySelection && !selection.containsEntity(entity.id)) || !materialMatches(options, entity.get(key))) continue;
			t.touchEntity(entity.id);
			entity.set(key, normalizeMaterialName(options.replaceWith));
		}
	}
	return t.finish();
}

} // namespace edit

} // namespace sme
