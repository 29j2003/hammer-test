// entity_catalog.cpp - FGD loading and base class flattening (see entity_catalog.h).

#include <sme/entity_catalog.h>

#include <sme/text.h>

#include <algorithm>
#include <functional>
#include <set>

#include <toolpp/FGD.h>

namespace sme {

namespace {

constexpr int MAX_BASE_DEPTH = 32;

EntityClass convert(std::string_view name, const toolpp::FGD::Entity& entity, const std::filesystem::path& file) {
	EntityClass out;
	out.name = std::string(name);
	out.classType = std::string(entity.classType);
	out.description = std::string(entity.description);
	out.sourceFile = file;

	for (const auto& property : entity.classProperties) {
		if (iequals(property.name, "base")) {
			for (auto& base : splitTrimmed(property.arguments, ',')) {
				out.bases.push_back(std::move(base));
			}
		} else {
			out.helpers.push_back({toLower(property.name), std::string(trim(property.arguments))});
		}
	}
	for (const auto& field : entity.fields) {
		EntityProperty p;
		p.name = std::string(field.name);
		p.type = toLower(field.valueType);
		p.displayName = std::string(field.displayName);
		p.defaultValue = std::string(field.valueDefault);
		p.description = std::string(field.description);
		p.readOnly = field.readonly;
		p.origin = out.name;
		out.properties.push_back(std::move(p));
	}
	for (const auto& field : entity.fieldsWithChoices) {
		EntityProperty p;
		p.kind = EntityProperty::Kind::Choices;
		p.name = std::string(field.name);
		p.type = "choices";
		p.displayName = std::string(field.displayName);
		p.defaultValue = std::string(field.valueDefault);
		p.description = std::string(field.description);
		p.readOnly = field.readonly;
		p.origin = out.name;
		for (const auto& choice : field.choices) {
			p.choices.push_back({std::string(choice.value), std::string(choice.displayName)});
		}
		out.properties.push_back(std::move(p));
	}
	for (const auto& field : entity.fieldsWithFlags) {
		EntityProperty p;
		p.kind = EntityProperty::Kind::Flags;
		p.name = std::string(field.name);
		p.type = "flags";
		p.displayName = std::string(field.displayName);
		p.description = std::string(field.description);
		p.readOnly = field.readonly;
		p.origin = out.name;
		for (const auto& flag : field.flags) {
			p.flags.push_back({flag.value, std::string(flag.displayName), flag.enabledByDefault});
		}
		out.properties.push_back(std::move(p));
	}
	for (const auto& io : entity.inputs) {
		out.inputs.push_back({std::string(io.name), toLower(io.valueType), std::string(io.description)});
	}
	for (const auto& io : entity.outputs) {
		out.outputs.push_back({std::string(io.name), toLower(io.valueType), std::string(io.description)});
	}
	return out;
}

template<typename T>
void mergeByName(std::vector<T>& inherited, const std::vector<T>& own) {
	for (const auto& item : own) {
		const auto it = std::find_if(inherited.begin(), inherited.end(), [&](const T& existing) {
			return iequals(existing.name, item.name);
		});
		if (it != inherited.end()) {
			*it = item;
		} else {
			inherited.push_back(item);
		}
	}
}

} // namespace

bool EntityClass::isBase() const {
	return iequals(classType, "BaseClass");
}

bool EntityClass::isSolid() const {
	return iequals(classType, "SolidClass");
}

const EntityProperty* EntityClass::property(std::string_view key) const {
	const auto it = std::find_if(properties.begin(), properties.end(), [key](const EntityProperty& p) {
		return iequals(p.name, key);
	});
	return it == properties.end() ? nullptr : &*it;
}

std::string EntityClass::helper(std::string_view helperName) const {
	for (const auto& h : helpers) {
		if (iequals(h.name, helperName)) {
			return h.arguments;
		}
	}
	return {};
}

bool EntityCatalog::load(const std::vector<std::filesystem::path>& fgdFiles, std::vector<std::string>* errors) {
	std::map<std::string, EntityClass> raw;
	m_files.clear();
	for (const auto& file : fgdFiles) {
		try {
			const toolpp::FGD fgd(file);
			if (fgd.getEntities().empty()) {
				if (errors) {
					errors->push_back(displayPath(file) + ": no entities found");
				}
				continue;
			}
			for (const auto& [name, entity] : fgd.getEntities()) {
				raw[toLower(name)] = convert(name, entity, file);
			}
			m_files.push_back(file);
		} catch (const std::exception& e) {
			if (errors) {
				errors->push_back(displayPath(file) + ": " + e.what());
			}
		}
	}

	// Flatten base classes: properties/IO from bases first (in order), then the class's own.
	m_classes.clear();
	std::map<std::string, EntityClass> done; // memo: shared bases (Targetname, ...) are flattened once
	std::function<EntityClass(const std::string&, int, std::set<std::string>&)> flatten;
	flatten = [&](const std::string& key, int depth, std::set<std::string>& visiting) -> EntityClass {
		if (const auto it = done.find(key); it != done.end()) {
			return it->second;
		}
		const auto& own = raw.at(key);
		EntityClass result = own;
		result.properties.clear();
		result.inputs.clear();
		result.outputs.clear();
		std::vector<EntityHelper> inheritedHelpers;
		visiting.insert(key);
		for (const auto& baseName : own.bases) {
			const auto baseKey = toLower(baseName);
			if (!raw.contains(baseKey) || visiting.contains(baseKey) || depth >= MAX_BASE_DEPTH) {
				if (errors && !raw.contains(baseKey)) {
					errors->push_back(own.name + ": unknown base class " + baseName);
				}
				continue;
			}
			const auto base = flatten(baseKey, depth + 1, visiting);
			mergeByName(result.properties, base.properties);
			mergeByName(result.inputs, base.inputs);
			mergeByName(result.outputs, base.outputs);
			for (const auto& helper : base.helpers) {
				inheritedHelpers.push_back(helper);
			}
		}
		visiting.erase(key);
		mergeByName(result.properties, own.properties);
		mergeByName(result.inputs, own.inputs);
		mergeByName(result.outputs, own.outputs);
		for (const auto& helper : inheritedHelpers) {
			const bool shadowed = std::any_of(result.helpers.begin(), result.helpers.end(), [&](const EntityHelper& h) {
				return h.name == helper.name;
			});
			if (!shadowed) {
				result.helpers.push_back(helper);
			}
		}
		done.emplace(key, result);
		return result;
	};
	for (const auto& [key, entity] : raw) {
		std::set<std::string> visiting;
		m_classes.emplace(key, flatten(key, 0, visiting));
		(void)entity;
	}
	return !m_files.empty();
}

const EntityClass* EntityCatalog::find(std::string_view name) const {
	const auto it = m_classes.find(toLower(name));
	return it == m_classes.end() ? nullptr : &it->second;
}

std::vector<const EntityClass*> EntityCatalog::placeable() const {
	std::vector<const EntityClass*> out;
	for (const auto& [key, entity] : m_classes) {
		if (!entity.isBase()) {
			out.push_back(&entity);
		}
	}
	return out;
}

} // namespace sme
