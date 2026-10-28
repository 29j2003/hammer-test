// asset_refs.h - Which game assets (materials, models, sounds, particle systems, soundscapes) the
// selected objects use, and which objects use a given asset. Behind the "Show in ..." and
// "Find in map" entries of the context menus.

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <sme/editing.h>

namespace sme {

class EntityCatalog;

/// Kind of a game asset named by a face or an entity key.
enum class AssetKind { Material, Model, Sound, Particle, Soundscape };

/// One asset, with its name as the browsers list it: materials without "materials/" and ".vmt"
/// ("concrete/wall01", lower case), models as "models/....mdl", sounds and the rest as written.
struct AssetRef {
	AssetKind kind = AssetKind::Material;
	std::string name;
	bool operator==(const AssetRef&) const = default;
};

/// The name a material is listed under ("CONCRETE\\Wall01.vmt" -> "concrete/wall01").
[[nodiscard]] std::string normalizedMaterial(std::string_view material);
/// The assets an entity names in its keys (model, sounds, particle system, soundscape, decal or
/// overlay material). `catalog` (may be null) knows which keys are sounds and particles by type.
[[nodiscard]] std::vector<AssetRef> entityAssets(const Entity& entity, const EntityCatalog* catalog);
/// The assets of the selection, each once: materials of the selected faces (or of all faces of
/// the selected brushes and brush entities), then what the selected entities name. At most
/// `limit` entries.
[[nodiscard]] std::vector<AssetRef> selectionAssets(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog,
                                                    std::size_t limit = 12);
/// How many entities use each model ("model" key ending in .mdl), by lower-case path
/// ("models/props/crate01.mdl"): the "Used in this map" filter and counts of the model lists.
[[nodiscard]] std::map<std::string, int> modelUsage(const MapDocument& doc);
/// Everything that uses the asset: faces with the material (and decals/overlays naming it), or
/// entities naming the model, sound, particle system or soundscape. Case-insensitive.
[[nodiscard]] Selection objectsUsing(const MapDocument& doc, const AssetRef& asset, const EntityCatalog* catalog);

} // namespace sme
