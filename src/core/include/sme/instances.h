// instances.h - func_instance: a map placed inside another map (Portal 2, CS:GO and Mapbase).
//
// VBSP merges each func_instance when compiling: it loads the instance's .vmf, replaces the
// `$variables` named in the replaceNN keys, gives the entity names a prefix or postfix (so two
// copies do not talk to each other's entities), and moves and turns everything to the
// func_instance's origin and angles. This module does the same in the editor:
//   - resolveInstanceFile finds the .vmf (next to the map, in the game's map folders);
//   - InstanceCache loads it once per file and modification time;
//   - prepareInstance returns a copy with fixups applied and transformed (the preview and
//     "collapse" both use it);
//   - collapseInstance copies the result into the map with new ids, as one undo step.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sme/editing.h>

namespace sme {

/// How entity names inside the instance are made unique (the func_instance's fixup_style key).
enum class FixupStyle { Prefix = 0, Postfix = 1, None = 2 };

/// One replacement: "$variable" is replaced by "value" (keys replace01, replace02, ...).
struct InstanceReplacement {
	std::string variable;   ///< with the leading '$'
	std::string value;
};

/// What a func_instance entity says about its instance.
struct InstanceSettings {
	std::string file;                                ///< as typed, e.g. "instances/door.vmf"
	std::string name;                                ///< the func_instance's targetname (fixup name)
	FixupStyle style = FixupStyle::Prefix;
	std::vector<InstanceReplacement> replacements;
	Vec3 origin;
	Vec3 angles;                                     ///< pitch yaw roll
};

/// func_instance (the only class that instances other maps).
[[nodiscard]] bool isInstanceEntity(const Entity& entity);
/// Reads the keys of a func_instance (file, name, fixup style, $variables, origin, angles).
[[nodiscard]] InstanceSettings instanceSettings(const Entity& funcInstance);
/// Writes the replacements as replace01..NN (the old ones are removed first).
void setInstanceReplacements(Entity& funcInstance, const std::vector<InstanceReplacement>& replacements);

/// Folders searched for an instance file, most specific first: the map's folder, then the
/// given map source folders (the game's mapsrc, sdk_content/maps ...).
[[nodiscard]] std::vector<std::filesystem::path> instanceSearchFolders(const std::filesystem::path& mapFile,
                                                                       const std::vector<std::filesystem::path>& mapSourceFolders);
/// The instance's .vmf ("file" may lack the extension, may use '\'). Paths leaving the search
/// folders ("..") are allowed only when the file exists; nothing is created.
[[nodiscard]] std::optional<std::filesystem::path> resolveInstanceFile(const std::string& file, const std::vector<std::filesystem::path>& folders);

/// Loaded instance maps, by path and modification time (an edited instance file is read again).
class InstanceCache {
public:
	/// The parsed map, or nullptr if it cannot be read (the reason is in `error`).
	[[nodiscard]] std::shared_ptr<const MapDocument> load(const std::filesystem::path& file, std::string* error = nullptr);
	void clear() { m_entries.clear(); }

private:
	// Cached map and the file time it was read at.
	struct Entry {
		std::filesystem::file_time_type time;
		std::shared_ptr<const MapDocument> doc;
		std::string error;
	};
	std::map<std::filesystem::path, Entry> m_entries;
};

/// Name after fixup: "name-entity" (prefix) or "entity-name" (postfix). Names starting with
/// '@' or '!' stay global, like in VBSP.
[[nodiscard]] std::string fixupName(const std::string& entityName, const InstanceSettings& settings);

/// Copy of the instance with variables replaced, names fixed up and everything moved and turned
/// to the func_instance (ids stay those of the instance file). The instance's
/// func_instance_parms entity supplies default values and is left out.
[[nodiscard]] MapDocument prepareInstance(const MapDocument& instance, const InstanceSettings& settings, const EntityCatalog* catalog);

/// What the views draw for an instance: its brushes and the boxes of its point entities.
struct InstancePreview {
	std::vector<Solid> solids;                                ///< world brushes and brush-entity brushes
	std::vector<std::pair<Bounds, std::string>> pointBoxes;   ///< box and classname of each point entity
	Bounds bounds;
};
/// Brushes and point-entity boxes of a prepared instance for the views.
[[nodiscard]] InstancePreview buildInstancePreview(const MapDocument& prepared, const EntityCatalog* catalog);

/// Replaces the func_instance by the instance's content (new ids, one undo step). `added` gets
/// what was put into the map.
[[nodiscard]] Change collapseInstance(MapDocument& doc, int funcInstanceId, const MapDocument& instance, const EntityCatalog* catalog,
                                      Selection* added = nullptr);

} // namespace sme
