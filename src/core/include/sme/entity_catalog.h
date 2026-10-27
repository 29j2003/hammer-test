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

namespace sme {

struct EntityChoice {
	std::string value;
	std::string displayName;
};

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
	std::vector<EntityChoice> choices;
	std::vector<EntityFlag> flags;
};

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

	[[nodiscard]] bool isBase() const;
	[[nodiscard]] bool isSolid() const;       ///< Brush entity (SolidClass)
	[[nodiscard]] const EntityProperty* property(std::string_view key) const;
	[[nodiscard]] std::string helper(std::string_view helperName) const; ///< arguments, empty if absent
};

class EntityCatalog {
public:
	/// Loads FGDs in order (later files override earlier classes). Returns false if none loaded;
	/// problems with single files are added to `errors`.
	bool load(const std::vector<std::filesystem::path>& fgdFiles, std::vector<std::string>* errors = nullptr);

	[[nodiscard]] const EntityClass* find(std::string_view name) const;

	/// Classes a mapper can place (everything except BaseClass), sorted by name.
	[[nodiscard]] std::vector<const EntityClass*> placeable() const;

	[[nodiscard]] std::size_t size() const { return m_classes.size(); }
	[[nodiscard]] const std::vector<std::filesystem::path>& files() const { return m_files; }

private:
	std::map<std::string, EntityClass> m_classes; ///< key: lowercase class name
	std::vector<std::filesystem::path> m_files;
};

} // namespace sme
