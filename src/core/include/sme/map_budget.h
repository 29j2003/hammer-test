// map_budget.h - How much of the engine's limits a map uses: brushes, brush sides, planes,
// entities, edicts, overlays, displacements, cubemaps, lights and static props, counted from the
// VMF the way VBSP and the game count them (some are estimates, see BudgetItem::estimate).
//
// Limits come from the engine's public/bspfile.h (MAX_MAP_*) and public/const.h (MAX_EDICT_BITS):
// - Source SDK 2013 (Half-Life 2, Episodes, Portal, TF2, Mapbase mods, ...): MAX_MAP_ENTITIES 8192.
// - Newer branches (Left 4 Dead 2, Alien Swarm, Portal 2, CS:GO): MAX_MAP_ENTITIES 16384
//   (Alien Swarm SDK public/bspfile.h); the other limits are the same.
// - Everywhere: 8192 brushes, 65536 brush sides, 65536 planes, 512 overlays, 2048 displacements,
//   1024 cubemaps, 8192 world lights, 2048 edicts (MAX_EDICT_BITS 11).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <sme/map.h>

namespace sme {

/// The engine limits a map is measured against (0 = no fixed limit, only counted).
struct BudgetLimits {
	std::size_t brushes = 8192;        ///< MAX_MAP_BRUSHES
	std::size_t brushSides = 65536;    ///< MAX_MAP_BRUSHSIDES
	std::size_t planes = 65536;        ///< MAX_MAP_PLANES
	std::size_t entities = 8192;       ///< MAX_MAP_ENTITIES (VBSP counts every entity of the VMF)
	std::size_t edicts = 2048;         ///< 1 << MAX_EDICT_BITS (entities alive in the game at once)
	std::size_t overlays = 512;        ///< MAX_MAP_OVERLAYS
	std::size_t displacements = 2048;  ///< MAX_MAP_DISPINFO
	std::size_t cubemaps = 1024;       ///< MAX_MAP_CUBEMAPSAMPLES
	std::size_t lights = 8192;         ///< MAX_MAP_WORLDLIGHTS
	std::size_t staticProps = 0;       ///< No fixed limit (memory decides)
};

/// The limits of a game by its Steam app id (owner app, or SteamAppId of a mod); unknown apps
/// get the Source SDK 2013 limits.
[[nodiscard]] BudgetLimits budgetLimitsForApp(std::uint32_t appId);

/// One counted thing with its limit.
struct BudgetItem {
	std::string name;          ///< "Brushes", "Brush sides", ...
	std::size_t count = 0;
	std::size_t limit = 0;     ///< 0 = no fixed limit
	bool estimate = false;     ///< VBSP/the game add or remove some (bevel sides, plane pairs, edicts)
	std::string note;          ///< What is counted, for the tooltip

	/// Share of the limit used (0 without limit).
	[[nodiscard]] double fraction() const { return limit == 0 ? 0.0 : double(count) / double(limit); }
	[[nodiscard]] bool over() const { return limit != 0 && count > limit; }
};

/// Counts everything of the budget in one pass over the map (fast enough to run after every change).
[[nodiscard]] std::vector<BudgetItem> mapBudget(const MapDocument& doc, const BudgetLimits& limits);

/// Whether VBSP removes the entity from the entity list or the game deletes it at spawn (func_detail,
/// prop_static, info_overlay, env_cubemap, unnamed lights, ...): it takes no edict.
[[nodiscard]] bool compiledAway(const Entity& entity);

} // namespace sme
