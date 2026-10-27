// visibility.h - Kinds of objects that can be hidden while mapping (Source 2 Hammer's view
// filters): world brushes, brush entities, triggers, tool brushes (clip, nodraw, hint ...), the
// sky, models, NPCs, lights, cubemaps and other helper entities. Hiding only affects the editor.

#pragma once

#include <cstdint>
#include <string_view>

#include <sme/editing.h>

namespace sme {

enum class ObjectKind : std::uint32_t {
	World = 1u << 0,           ///< World brushes with normal materials
	BrushEntities = 1u << 1,   ///< func_door, func_detail, ...
	Triggers = 1u << 2,        ///< trigger_* and other brush entities in tool materials
	ToolBrushes = 1u << 3,     ///< Clip, nodraw, hint, skip, areaportal, occluder ... (whole brushes or faces)
	Sky = 1u << 4,             ///< toolsskybox faces
	Models = 1u << 5,          ///< Props and other point entities with a model
	Npcs = 1u << 6,
	Lights = 1u << 7,
	Cubemaps = 1u << 8,        ///< env_cubemap and their room boxes (parallax_obb)
	Helpers = 1u << 9,         ///< Every other point entity (logic, info_*, paths, sounds ...)
};
constexpr std::uint32_t ALL_OBJECT_KINDS = (1u << 10) - 1;

/// Tool material by name ("tools/..."), case-insensitive.
[[nodiscard]] bool isToolMaterialName(std::string_view material);
/// The 2D/3D skybox tool materials.
[[nodiscard]] bool isSkyMaterialName(std::string_view material);
/// What kind of object a point or brush entity is.
[[nodiscard]] ObjectKind entityKind(const Entity& entity, const EntityCatalog* catalog);
/// Everything of the kinds in `hiddenKinds` (a mask of ObjectKind), as a selection: whole brushes
/// and entities, plus single faces (tool or sky faces on otherwise normal brushes).
[[nodiscard]] Selection objectsOfKinds(const MapDocument& doc, std::uint32_t hiddenKinds, const EntityCatalog* catalog);

} // namespace sme
