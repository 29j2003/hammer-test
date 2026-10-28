// io_links.h - Connecting entities (outputs to inputs) in one step.
//
// Picking a source and a target is all the user should have to do: the likely output of the
// source (a trigger's OnTrigger, a button's OnPressed, a relay's OnTrigger, ...) and the likely
// input of the target (a door's Open, a sound's PlaySound, a light's Toggle, ...) come first,
// ranked from small preference tables and the classes' FGD inputs/outputs. The target gets a
// free name when it has none, in the same undo step.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <sme/entity_catalog.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme {

/// An output of the source paired with an input of the target; lower score = more likely.
struct ConnectionSuggestion {
	std::string output;
	std::string input;
	int score = 0;
};

/// The source's outputs, likely ones first (FGD outputs of `definition`; without a class the
/// common outputs of that kind of entity).
[[nodiscard]] std::vector<std::string> rankedOutputs(const std::string& classname, const EntityClass* definition);
/// The target's inputs, likely ones first (likewise).
[[nodiscard]] std::vector<std::string> rankedInputs(const std::string& classname, const EntityClass* definition);
/// The most likely output -> input pairs, best first, at most `limit`.
[[nodiscard]] std::vector<ConnectionSuggestion> suggestConnections(const std::string& sourceClass, const EntityClass* sourceDefinition,
                                                                   const std::string& targetClass, const EntityClass* targetDefinition,
                                                                   std::size_t limit = 8);

namespace edit {

/// Adds `connection` to the source's outputs, aimed at the target (named with
/// uniqueTargetName() first if it has no name). One undo step "Connect". Returns the change and
/// the target's name (empty if source or target do not exist).
[[nodiscard]] std::pair<Change, std::string> connect(MapDocument& doc, int sourceId, int targetId, Connection connection);

} // namespace edit

} // namespace sme
