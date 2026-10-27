// instances.cpp - func_instance loading, fixups, preview and collapse (see instances.h).

#include <sme/instances.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sme {

namespace {

/// Keys that name other entities, for maps without an FGD (the usual Source 2013 ones).
constexpr std::string_view NAME_KEYS[] = {"targetname", "parentname", "target", "filtername", "damagefilter", "lightingorigin",
                                          "lightingoriginhack", "landmark", "attach1", "attach2", "measuretarget", "SourceEntityName",
                                          "m_iszEntity", "m_iszEntry", "LookAtName", "spawntarget", "goal", "template01", "template02",
                                          "template03", "template04", "template05", "template06", "template07", "template08"};

bool namesAnEntity(const Entity& entity, const std::string& key, const EntityCatalog* catalog) {
	if (catalog) {
		if (const auto* definition = catalog->find(entity.classname())) {
			if (const auto* property = definition->property(key)) {
				const auto& type = property->type;
				return iequals(type, "target_source") || iequals(type, "target_destination") || iequals(type, "target_name_or_class");
			}
		}
	}
	return std::any_of(std::begin(NAME_KEYS), std::end(NAME_KEYS), [&](std::string_view k) { return iequals(k, key); });
}

/// A name that is a class ("target_name_or_class" may hold one) or a special name stays as it is.
bool isGlobalName(const std::string& value, const EntityCatalog* catalog) {
	return value.empty() || value[0] == '!' || value[0] == '@' || (catalog && catalog->find(value));
}

std::string replaceAll(std::string text, const std::vector<InstanceReplacement>& replacements) {
	for (const auto& r : replacements) {
		if (r.variable.size() < 2) continue;
		std::size_t pos = 0;
		while ((pos = text.find(r.variable, pos)) != std::string::npos) {
			text.replace(pos, r.variable.size(), r.value);
			pos += r.value.size();
		}
	}
	return text;
}

/// "$var value" -> {"$var", "value"}.
std::optional<InstanceReplacement> splitReplacement(const std::string& text) {
	const auto trimmed = std::string(trim(text));
	if (trimmed.empty() || trimmed[0] != '$') return std::nullopt;
	const auto space = trimmed.find_first_of(" \t");
	if (space == std::string::npos) return InstanceReplacement{trimmed, {}};
	return InstanceReplacement{trimmed.substr(0, space), std::string(trim(std::string_view(trimmed).substr(space + 1)))};
}

Vec3 anglesOf(const Entity& entity) {
	Vec3 angles;
	if (!parseVec3(entity.get("angles", "0 0 0"), angles)) return {};
	return angles;
}

void transformEntity(Entity& entity, const Mat3& rotation, const Vec3& origin) {
	for (auto& solid : entity.solids) {
		rotateSolid(solid, rotation, {}, true);
		translateSolid(solid, origin, true);
	}
	if (entity.hasOrigin()) {
		entity.setOrigin(rotation * entity.origin() + origin);
	}
	if (entity.solids.empty()) {
		entity.set("angles", formatVec3(edit::matrixToAngles(rotation * edit::anglesToMatrix(anglesOf(entity)))));
	}
}

/// Removes the instance's own visgroup ids (they mean nothing in the map it is copied into).
void dropVisGroupIds(std::vector<KvNode>& extra) {
	for (auto& node : extra) {
		if (node.block && iequals(node.key, "editor")) {
			std::erase_if(node.children, [](const KvNode& n) { return !n.block && iequals(n.key, "visgroupid"); });
		}
	}
}

} // namespace

bool isInstanceEntity(const Entity& entity) {
	return iequals(entity.classname(), "func_instance");
}

InstanceSettings instanceSettings(const Entity& funcInstance) {
	InstanceSettings s;
	s.file = funcInstance.get("file");
	s.name = funcInstance.get("targetname");
	if (s.name.empty()) {
		s.name = "AutoInstance" + std::to_string(funcInstance.id);   // VBSP names unnamed instances itself
	}
	const auto style = funcInstance.get("fixup_style", "0");
	s.style = style == "1" ? FixupStyle::Postfix : style == "2" ? FixupStyle::None : FixupStyle::Prefix;
	s.origin = funcInstance.origin();
	s.angles = anglesOf(funcInstance);
	std::vector<std::pair<std::string, std::string>> keys = funcInstance.keyValues;
	std::sort(keys.begin(), keys.end());   // replace01, replace02, ... in order
	for (const auto& [key, value] : keys) {
		if (toLower(key).starts_with("replace")) {
			if (auto r = splitReplacement(value)) s.replacements.push_back(std::move(*r));
		}
	}
	return s;
}

void setInstanceReplacements(Entity& funcInstance, const std::vector<InstanceReplacement>& replacements) {
	std::erase_if(funcInstance.keyValues, [](const auto& kv) { return toLower(kv.first).starts_with("replace"); });
	int n = 1;
	for (const auto& r : replacements) {
		if (r.variable.empty()) continue;
		char key[16];
		std::snprintf(key, sizeof key, "replace%02d", n++);
		const std::string variable = r.variable[0] == '$' ? r.variable : "$" + r.variable;
		funcInstance.set(key, r.value.empty() ? variable : variable + " " + r.value);
	}
}

std::vector<std::filesystem::path> instanceSearchFolders(const std::filesystem::path& mapFile, const std::vector<std::filesystem::path>& mapSourceFolders) {
	std::vector<std::filesystem::path> out;
	const auto add = [&](const std::filesystem::path& p) {
		if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
	};
	add(mapFile.parent_path());
	for (const auto& folder : mapSourceFolders) add(folder);
	return out;
}

std::optional<std::filesystem::path> resolveInstanceFile(const std::string& file, const std::vector<std::filesystem::path>& folders) {
	std::string name = file;
	std::replace(name.begin(), name.end(), '\\', '/');
	if (trim(name).empty()) return std::nullopt;
	std::filesystem::path relative(name);
	if (!relative.has_extension()) relative += ".vmf";
	std::error_code ec;
	if (relative.is_absolute()) {
		if (std::filesystem::is_regular_file(relative, ec)) return relative;
		return std::nullopt;
	}
	for (const auto& folder : folders) {
		const auto candidate = folder / relative;
		if (std::filesystem::is_regular_file(candidate, ec)) return candidate.lexically_normal();
	}
	return std::nullopt;
}

std::shared_ptr<const MapDocument> InstanceCache::load(const std::filesystem::path& file, std::string* error) {
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(file, ec);
	if (ec) {
		if (error) *error = "cannot read " + file.string();
		return nullptr;
	}
	if (const auto it = m_entries.find(file); it != m_entries.end() && it->second.time == time) {
		if (error) *error = it->second.error;
		return it->second.doc;
	}
	Entry entry;
	entry.time = time;
	std::ifstream in(file, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	try {
		entry.doc = std::make_shared<const MapDocument>(MapDocument::parse(text.str()));
	} catch (const std::exception& e) {
		entry.error = e.what();
	}
	if (!in && !entry.doc) entry.error = "cannot read " + file.string();
	if (error) *error = entry.error;
	auto doc = entry.doc;
	m_entries[file] = std::move(entry);
	return doc;
}

std::string fixupName(const std::string& entityName, const InstanceSettings& settings) {
	if (entityName.empty() || entityName[0] == '@' || entityName[0] == '!' || settings.style == FixupStyle::None) {
		return entityName;
	}
	return settings.style == FixupStyle::Prefix ? settings.name + "-" + entityName : entityName + "-" + settings.name;
}

MapDocument prepareInstance(const MapDocument& instance, const InstanceSettings& settings, const EntityCatalog* catalog) {
	MapDocument out = instance;
	// Defaults of the instance's parameters (func_instance_parms: "parm1" "$var type default").
	std::vector<InstanceReplacement> replacements = settings.replacements;
	for (const auto& entity : instance.entities) {
		if (!iequals(entity.classname(), "func_instance_parms")) continue;
		for (const auto& [key, value] : entity.keyValues) {
			if (!toLower(key).starts_with("parm")) continue;
			const auto parts = splitTrimmed(value, ' ');
			if (parts.size() >= 3 && !parts[0].empty() && parts[0][0] == '$' &&
			    std::none_of(replacements.begin(), replacements.end(), [&](const InstanceReplacement& r) { return r.variable == parts[0]; })) {
				std::string fallback;
				for (std::size_t i = 2; i < parts.size(); ++i) fallback += (i > 2 ? " " : "") + std::string(parts[i]);
				replacements.push_back({std::string(parts[0]), fallback});
			}
		}
	}
	// Longest variable first, so "$color2" is not eaten by "$color".
	std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) { return a.variable.size() > b.variable.size(); });
	std::erase_if(out.entities, [](const Entity& e) { return iequals(e.classname(), "func_instance_parms"); });

	const Mat3 rotation = edit::anglesToMatrix(settings.angles);
	for (auto& entity : out.entities) {
		for (auto& [key, value] : entity.keyValues) {
			value = replaceAll(value, replacements);
			if (namesAnEntity(entity, key, catalog) && !isGlobalName(value, catalog)) {
				value = fixupName(value, settings);
			}
		}
		for (auto& connection : entity.connections) {
			connection.target = replaceAll(connection.target, replacements);
			connection.parameter = replaceAll(connection.parameter, replacements);
			if (!isGlobalName(connection.target, catalog)) connection.target = fixupName(connection.target, settings);
		}
		transformEntity(entity, rotation, settings.origin);
	}
	for (auto& solid : out.world.solids) {
		rotateSolid(solid, rotation, {}, true);
		translateSolid(solid, settings.origin, true);
	}
	return out;
}

InstancePreview buildInstancePreview(const MapDocument& prepared, const EntityCatalog* catalog) {
	InstancePreview preview;
	const auto addSolid = [&](const Solid& solid) {
		preview.bounds.add(buildBrushGeometry(solid).bounds);
		preview.solids.push_back(solid);
	};
	for (const auto& solid : prepared.world.solids) addSolid(solid);
	for (const auto& entity : prepared.entities) {
		if (!entity.solids.empty()) {
			for (const auto& solid : entity.solids) addSolid(solid);
			continue;
		}
		const auto box = edit::entityBounds(entity, catalog);
		preview.bounds.add(box);
		preview.pointBoxes.emplace_back(box, entity.classname());
	}
	return preview;
}

Change collapseInstance(MapDocument& doc, int funcInstanceId, const MapDocument& instance, const EntityCatalog* catalog, Selection* added) {
	Transaction t(doc, "Collapse instance");
	const Entity* funcInstance = doc.findEntity(funcInstanceId);
	if (!funcInstance || funcInstanceId == 0 || !isInstanceEntity(*funcInstance)) {
		return t.finish();
	}
	const auto prepared = prepareInstance(instance, instanceSettings(*funcInstance), catalog);
	Selection result;
	for (const auto& solid : prepared.world.solids) {
		Solid copy = cloneSolid(doc, solid);
		dropVisGroupIds(copy.extra);
		t.touchSolid({0, copy.id});
		result.solids.push_back({0, copy.id});
		doc.world.solids.push_back(std::move(copy));
	}
	for (const auto& entity : prepared.entities) {
		Entity copy = entity;
		copy.id = doc.newEntityId();
		for (auto& solid : copy.solids) {
			solid = cloneSolid(doc, solid);
			dropVisGroupIds(solid.extra);
		}
		dropVisGroupIds(copy.extra);
		t.touchEntity(copy.id);
		result.entities.push_back(copy.id);
		doc.entities.push_back(std::move(copy));
	}
	t.touchEntity(funcInstanceId);
	std::erase_if(doc.entities, [funcInstanceId](const Entity& e) { return e.id == funcInstanceId; });
	if (added) *added = std::move(result);
	return t.finish();
}

} // namespace sme
