// checks.h - Finds problems in a map before compiling and fixes the simple ones.
//
// Every problem says what is wrong, which objects cause it (for "select") and how to fix
// it. Problems with an obvious fix (a typo in a material or class name, a broken brush,
// an output to nothing) carry an autofix.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/assets.h>
#include <sme/editing.h>
#include <sme/entity_catalog.h>
#include <sme/history.h>
#include <sme/map.h>

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
		Leak,
		PropClassMismatch,      ///< prop_static without $staticprop, prop_physics without collision model
		SoundLoopMismatch,      ///< ambient_generic "Is NOT Looped" does not match the WAV
		MultipleSkyCameras,
	};

	Severity severity = Severity::Warning;
	Kind kind = Kind::InvalidBrush;
	std::string message;        ///< What is wrong
	std::string howToFix;       ///< What the mapper can do
	Selection objects;          ///< Objects to select/show
	std::string fixLabel;       ///< Button text if an autofix exists, else empty

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
};

/// What the checks may look at besides the map (all optional: without them some checks are skipped).
struct CheckContext {
	const EntityCatalog* entities = nullptr;
	const GameFileSystem* files = nullptr;
	/// All material names of the game ("concrete/wall01"), for "did you mean" suggestions. Optional.
	const std::vector<std::string>* materialNames = nullptr;
	/// Cache for model and sound lookups; without one every run reads the files again.
	CheckCache* cache = nullptr;
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
