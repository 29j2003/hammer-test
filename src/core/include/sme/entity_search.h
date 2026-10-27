// entity_search.h - Finding entities in a map: by class, name, any key=value (with wildcards),
// the targets of their outputs, their model or their sound. Used by the Find dialog.
//
// Query text ("Anything" field), terms separated by spaces, all must match:
//   word            class, name, model or any value contains it (wildcards * and ? allowed)
//   key=value       that key has that value (value may use * and ?)
//   class:x name:x  only the class / targetname
//   target:x        an output fires at x
//   model:x sound:x only the model / sound keys

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <sme/map.h>

namespace sme {

/// Case-insensitive match with '*' (any run of characters) and '?' (one character).
[[nodiscard]] bool wildcardMatch(std::string_view pattern, std::string_view text);
/// Like wildcardMatch, but a pattern without wildcards only has to appear somewhere in the text.
[[nodiscard]] bool looseMatch(std::string_view pattern, std::string_view text);

/// What to look at; Anything understands the query syntax described above.
enum class EntityField { Anything, Class, Name, KeyValue, OutputTarget, Model, Sound };

/// Ids of the entities (not the world) that match `query` in `field`, in map order.
[[nodiscard]] std::vector<int> findEntities(const MapDocument& doc, EntityField field, std::string_view query);

/// True for keys that name a sound (ambient_generic's "message", "*sound*", values ending in .wav/.mp3).
[[nodiscard]] bool isSoundKey(std::string_view key, std::string_view value);

} // namespace sme
