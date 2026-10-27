// entity_catalog.h - Entity definitions from one or more FGD files, with base classes resolved.
//
// Each game/mod brings its own FGDs (Mapbase extends halflife2.fgd, Black Mesa has bms.fgd, ...).
// Files are loaded in order; a later class with the same name replaces the earlier one.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <sme/math.h>

namespace sme {

/// One choice of a "choices" property: the value written to the map and the name shown.
struct EntityChoice {
	std::string value;
	std::string displayName;
};

/// One bit of a "flags" property (spawnflags).
struct EntityFlag {
	std::uint64_t value = 0;
	std::string displayName;
	bool enabledByDefault = false;
};

/// One keyvalue of an entity ("targetname", "spawnflags", ...).
struct EntityProperty {
	enum class Kind { Plain, Choices, Flags };

	Kind kind = Kind::Plain;
	std::string name;
	std::string type;            ///< FGD type: "string", "integer", "studio", "color255", ... ("choices"/"flags")
	std::string displayName;
	std::string defaultValue;
	std::string description;
	bool readOnly = false;
	std::string origin;          ///< Class that defines it (a base class like "Targetname", or the class itself)
	std::vector<EntityChoice> choices;
	std::vector<EntityFlag> flags;
};

/// One input or output of a class.
struct EntityIo {
	std::string name;
	std::string type;            ///< "void", "string", "integer", ...
	std::string description;
};

/// A class helper such as studio("models/x.mdl"), iconsprite(...), size(-8 -8 -8, 8 8 8), color(...).
struct EntityHelper {
	std::string name;            ///< lowercase
	std::string arguments;
};

/// An entity class from the FGD, with everything inherited from its base classes already merged in.
struct EntityClass {
	std::string name;            ///< e.g. "prop_physics"
	std::string classType;       ///< "PointClass", "SolidClass", "BaseClass", "NPCClass", ...
	std::string description;
	std::vector<std::string> bases;           ///< Direct base classes as written
	std::vector<EntityHelper> helpers;        ///< Own and inherited helpers (own first)
	std::vector<EntityProperty> properties;   ///< Inherited first, then own (own replaces same name)
	std::vector<EntityIo> inputs;
	std::vector<EntityIo> outputs;
	std::filesystem::path sourceFile;         ///< FGD the class was last defined in

	/// BaseClass: only for inheriting, never placed.
	[[nodiscard]] bool isBase() const;
	[[nodiscard]] bool isSolid() const;       ///< Brush entity (SolidClass)
	/// The property with this key (case-insensitive) or nullptr.
	[[nodiscard]] const EntityProperty* property(std::string_view key) const;
	[[nodiscard]] std::string helper(std::string_view helperName) const; ///< arguments, empty if absent
};

/// All entity classes of the loaded FGDs.
class EntityCatalog {
public:
	/// Loads FGDs in order (later files override earlier classes). Returns false if none loaded;
	/// problems with single files are added to `errors`.
	bool load(const std::vector<std::filesystem::path>& fgdFiles, std::vector<std::string>* errors = nullptr);

	/// The class with this name (case-insensitive) or nullptr.
	[[nodiscard]] const EntityClass* find(std::string_view name) const;

	/// Classes a mapper can place (everything except BaseClass), sorted by name.
	[[nodiscard]] std::vector<const EntityClass*> placeable() const;

	/// Number of classes, base classes included.
	[[nodiscard]] std::size_t size() const { return m_classes.size(); }

	/// Model sizes the editor learned (filled once a model is loaded; makes prop boxes fit).
	void setModelBounds(const std::string& model, const Bounds& bounds) const { m_modelBounds[model] = bounds; }
	[[nodiscard]] const Bounds* modelBounds(const std::string& model) const {
		const auto it = m_modelBounds.find(model);
		return it == m_modelBounds.end() ? nullptr : &it->second;
	}
	/// The FGD files that were loaded, in order.
	[[nodiscard]] const std::vector<std::filesystem::path>& files() const { return m_files; }

private:
	std::map<std::string, EntityClass> m_classes; ///< key: lowercase class name
	std::vector<std::filesystem::path> m_files;
	mutable std::map<std::string, Bounds> m_modelBounds;   ///< key: normalized model path
};

} // namespace sme
