// asset_refs.cpp - Assets named by faces and entity keys (see asset_refs.h).

#include <sme/asset_refs.h>

#include <algorithm>

#include <sme/entity_catalog.h>
#include <sme/entity_search.h>
#include <sme/text.h>

namespace sme {

namespace {

std::string slashes(std::string_view text) {
	std::string out(text);
	std::replace(out.begin(), out.end(), '\\', '/');
	return out;
}

bool endsWith(std::string_view text, std::string_view end) {
	return text.size() >= end.size() && iequals(text.substr(text.size() - end.size()), end);
}

void addUnique(std::vector<AssetRef>& list, AssetRef ref) {
	if (ref.name.empty()) return;
	const auto same = [&](const AssetRef& other) { return other.kind == ref.kind && iequals(other.name, ref.name); };
	if (std::none_of(list.begin(), list.end(), same)) list.push_back(std::move(ref));
}

/// FGD type of a key ("sound", "particlesystem", "material" ...), empty when unknown.
std::string keyType(const Entity& entity, std::string_view key, const EntityCatalog* catalog) {
	if (!catalog) return {};
	const auto* cls = catalog->find(entity.classname());
	const auto* property = cls ? cls->property(key) : nullptr;
	return property ? toLower(property->type) : std::string();
}

bool sameAsset(const AssetRef& ref, const AssetRef& wanted) {
	if (ref.kind != wanted.kind) return false;
	return iequals(ref.name, wanted.kind == AssetKind::Material ? normalizedMaterial(wanted.name) : slashes(wanted.name));
}

} // namespace

std::string normalizedMaterial(std::string_view material) {
	std::string name = toLower(slashes(material));
	if (name.starts_with("materials/")) name.erase(0, 10);
	if (name.ends_with(".vmt")) name.resize(name.size() - 4);
	return name;
}

std::vector<AssetRef> entityAssets(const Entity& entity, const EntityCatalog* catalog) {
	std::vector<AssetRef> out;
	const std::string cls = toLower(entity.classname());
	for (const auto& [key, value] : entity.keyValues) {
		if (value.empty() || key == "classname") continue;
		const std::string lower = toLower(key);
		const std::string type = keyType(entity, key, catalog);
		if ((lower == "model" || type == "studio") && endsWith(value, ".mdl")) {
			addUnique(out, {AssetKind::Model, toLower(slashes(value))});
		} else if (type == "particlesystem" || lower == "effect_name") {
			addUnique(out, {AssetKind::Particle, value});
		} else if (lower == "soundscape" && cls.starts_with("env_soundscape")) {
			addUnique(out, {AssetKind::Soundscape, value});
		} else if ((cls == "infodecal" && lower == "texture") || (cls == "info_overlay" && lower == "material") || type == "material" ||
		           type == "decal") {
			addUnique(out, {AssetKind::Material, normalizedMaterial(value)});
		} else if (type == "sound" || ((type.empty() || type == "string") && isSoundKey(key, value))) {
			addUnique(out, {AssetKind::Sound, slashes(value)});
		}
	}
	return out;
}

std::vector<AssetRef> selectionAssets(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog, std::size_t limit) {
	std::vector<AssetRef> out;
	const auto addSolid = [&](const Solid& solid) {
		for (const auto& side : solid.sides) addUnique(out, {AssetKind::Material, normalizedMaterial(side.material)});
	};
	for (const auto& face : selection.faces) {
		if (const auto* solid = doc.findSolid(face.solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == face.sideId) addUnique(out, {AssetKind::Material, normalizedMaterial(side.material)});
			}
		}
	}
	for (const auto& ref : selection.solids) {
		if (const auto* solid = doc.findSolid(ref)) addSolid(*solid);
	}
	std::vector<AssetRef> named;   // what the entities name comes after the materials
	for (const int id : selection.entities) {
		const auto* entity = doc.findEntity(id);
		if (!entity) continue;
		for (const auto& solid : entity->solids) addSolid(solid);
		for (auto& ref : entityAssets(*entity, catalog)) addUnique(named, std::move(ref));
	}
	for (auto& ref : named) addUnique(out, std::move(ref));
	if (out.size() > limit) out.resize(limit);
	return out;
}

std::map<std::string, int> modelUsage(const MapDocument& doc) {
	std::map<std::string, int> usage;
	for (const auto& entity : doc.entities) {
		const auto model = entity.get("model");
		if (endsWithI(model, ".mdl")) ++usage[normalizePath(model)];
	}
	return usage;
}

Selection objectsUsing(const MapDocument& doc, const AssetRef& asset, const EntityCatalog* catalog) {
	Selection out;
	if (asset.kind == AssetKind::Material) {
		const std::string wanted = normalizedMaterial(asset.name);
		const auto scan = [&](int entityId, const Solid& solid) {
			for (const auto& side : solid.sides) {
				if (normalizedMaterial(side.material) == wanted) out.faces.push_back({{entityId, solid.id}, side.id});
			}
		};
		for (const auto& solid : doc.world.solids) scan(0, solid);
		for (const auto& entity : doc.entities) {
			for (const auto& solid : entity.solids) scan(entity.id, solid);
		}
	}
	for (const auto& entity : doc.entities) {
		const auto refs = entityAssets(entity, catalog);
		if (std::any_of(refs.begin(), refs.end(), [&](const AssetRef& ref) { return sameAsset(ref, asset); })) {
			out.entities.push_back(entity.id);
		}
	}
	return out;
}

} // namespace sme
