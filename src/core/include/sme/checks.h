// checks.h - Finds problems in a map before compiling and fixes the simple ones.
//
// Every problem says what is wrong, which objects cause it (for "select") and how to fix
// it. Problems with an obvious fix (a typo in a material or class name, a broken brush,
// an output to nothing) carry an autofix.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/assets.h>
#include <sme/compile_log.h>
#include <sme/editing.h>
#include <sme/entity_catalog.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/map_budget.h>
#include <sme/mod_install.h>

namespace sme {

class GameFileSystem;

/// One thing the map check found: what is wrong, how to fix it, which objects it is about, and the data for an autofix.
struct Problem {
	enum class Severity { Error, Warning, Info };
	/// What kind of problem it is (decides the autofix).
	enum class Kind {
		InvalidBrush,
		MissingMaterial,
		UnknownClass,
		BrushEntityWithoutBrushes,
		PointClassWithBrushes,
		OutputTargetMissing,
		UnknownOutput,
		UnknownInput,
		NoPlayerStart,
		NoLights,
		BadTextureScale,
		MissingModel,
		DoorAreaportalNotLinked,   ///< objects.entities = {door, areaportal}
		Leak,
		PropClassMismatch,      ///< prop_static without $staticprop, prop_physics without collision model
		SoundLoopMismatch,      ///< ambient_generic "Is NOT Looped" does not match the WAV
		MultipleSkyCameras,
		NoCubemap,              ///< Shiny materials ($envmap env_cubemap) but no env_cubemap: they mirror the sky
		CubemapsNotBuilt,       ///< The compiled map has env_cubemaps whose reflections were never built
		PlayerStartInSolid,     ///< layout_checks: the player spawns stuck
		EntityOutsideWorld,     ///< layout_checks: a point entity in the void
		LightmapScale,          ///< layout_checks: extreme lightmap scales (to = the new scale)
		NpcsWithoutNodes,       ///< layout_checks: NPCs but no info_node (objects = the NPCs)
		PropOriginInSolid,      ///< layout_checks: a prop's origin inside a brush - it is lit black
		ModBinIncomplete,       ///< A Mapbase mod's bin lacks (or has old) client/server/shader DLLs (action installFeatures)
		NearEngineLimit,        ///< optimization_checks: 85 % or more of a MAX_MAP_* limit (map_budget)
		DetailCandidates,       ///< optimization_checks: world brushes that are only details (fix: tie to func_detail)
		BrokenBrush,            ///< A brush VBSP may drop or leave a hole for: a tiny edge, paper thin (fix: delete)
		DuplicateBrush,         ///< Two brushes exactly in one place (faces flicker, clicks pick either; fix: delete one)
	};

	Severity severity = Severity::Warning;
	Kind kind = Kind::InvalidBrush;
	std::string message;        ///< What is wrong
	std::string howToFix;       ///< What the mapper can do
	Selection objects;          ///< Objects to select/show
	std::string fixLabel;       ///< Button text if an autofix exists, else empty
	/// An action of the app instead of a map edit (the button runs it; applyFix does nothing):
	/// "buildCubemaps" = compile with the Cubemaps step on; "installFeatures" = install the
	/// features Mapsmith brings into the mod (Project > Features).
	std::string action;

	// Data for the autofix.
	std::string from, to;
	int entityId = 0;
	Connection connection;
	std::vector<Vec3> line;     ///< Leak line
};

/// What the checks read from game files, kept between runs so a model or sound is read once per
/// game (the Problems panel keeps one and clears it when the game changes).
struct CheckCache {
	std::map<std::string, std::optional<PropModelFlags>> models;   ///< By lower-case model path
	std::map<std::string, std::optional<bool>> soundLoops;         ///< By lower-case sound name
	std::optional<std::vector<SoundScriptEntry>> soundScripts;     ///< Read on first use
	std::map<std::string, bool> reflective;                        ///< By material name: $envmap env_cubemap
	/// The compiled map's cubemaps, read again only when the file changed.
	std::filesystem::path compiledPath;
	std::filesystem::file_time_type compiledTime{};
	std::uintmax_t compiledSize = 0;
	std::optional<BspCubemaps> compiledCubemaps;
};

/// What the checks may look at besides the map (all optional: without them some checks are skipped).
struct CheckContext {
	const EntityCatalog* entities = nullptr;
	const GameFileSystem* files = nullptr;
	/// All material names of the game ("concrete/wall01"), for "did you mean" suggestions. Optional.
	const std::vector<std::string>* materialNames = nullptr;
	/// Cache for model and sound lookups; without one every run reads the files again.
	CheckCache* cache = nullptr;
	/// The map as the game loads it (<game>/maps/<name>.bsp) and the .vmf it was compiled from:
	/// the compiled map is only looked at when it is not older than the .vmf. Empty: skipped.
	std::filesystem::path compiledMap;
	std::filesystem::path mapSource;
	/// The open mod's folder when it is built on Mapbase (its bin folder is checked), else empty;
	/// `bundle`: the features Mapsmith brings (for "older than Mapsmith's"), may be null.
	std::filesystem::path mapbaseModDir;
	const FeatureBundle* bundle = nullptr;
	/// The engine limits of the open game (budgetLimitsForApp); null: Source SDK 2013's.
	const BudgetLimits* limits = nullptr;
};

/// Runs every check on the map (invalid brushes, missing materials/models, unknown classes, broken outputs, ...).
[[nodiscard]] std::vector<Problem> checkMap(const MapDocument& doc, const CheckContext& context);

/// Applies the problem's autofix (empty change if it has none).
[[nodiscard]] Change applyFix(MapDocument& doc, const Problem& problem, const EntityCatalog* entities);

/// Reads a VBSP pointfile (.lin): one "x y z" point per line.
[[nodiscard]] std::vector<Vec3> readPointfile(std::string_view text);

/// Turns a leak line into a problem that names the entity where the leak starts.
[[nodiscard]] Problem leakProblem(const MapDocument& doc, const std::vector<Vec3>& line, const EntityCatalog* entities);

} // namespace sme
