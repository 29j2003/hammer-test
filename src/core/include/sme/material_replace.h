// material_replace.h - Replace one material with another across the selection or the whole map
// (Hammer's "Replace Textures", with wildcards and overlays/decals). One undo step.

#pragma once

#include <string>

#include <sme/editing.h>

namespace sme {

/// What Replace materials looks for and where.
struct MaterialReplaceOptions {
	std::string find;                 ///< Material to look for, e.g. "dev/dev_measuregeneric01" or "concrete/*"
	std::string replaceWith;          ///< New material
	bool wildcards = false;           ///< `find` may use * and ? (else the whole name must match)
	bool keepAlignment = true;        ///< Keep the faces' texture axes; off: align the new texture to the world
	bool overlaysAndDecals = true;    ///< Also info_overlay "material" and infodecal "texture" keys
	bool onlySelection = false;       ///< Only the selected brushes / entities / faces
};

/// How many faces (and overlay/decal keys) the options would change, without changing anything.
[[nodiscard]] int countMaterialMatches(const MapDocument& doc, const Selection& selection, const MaterialReplaceOptions& options);

namespace edit {
/// Replaces the material; returns an empty Change when nothing matched.
[[nodiscard]] Change replaceMaterials(MapDocument& doc, const Selection& selection, const MaterialReplaceOptions& options);
} // namespace edit

} // namespace sme
