// clipboard.h - Copying and pasting map objects (Hammer and Source 2 Hammer: Ctrl+C, Ctrl+X,
// Ctrl+V and Paste Special with several copies).
//
// The copied objects travel as VMF text: a world with the copied world brushes plus the copied
// entities. So they can be pasted into another map, another editor window, or read as text.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <sme/editing.h>

namespace sme {

struct PasteOptions {
	int copies = 1;
	Vec3 offset;                    ///< Added per copy: copy i (from 1) is moved by offset * i
	Vec3 rotation;                  ///< Degrees around x, y, z per copy, around the copied objects' center
	std::optional<Vec3> center;     ///< The first copy's center goes here (else the objects keep their place)
	bool uniqueNames = false;       ///< Names that are taken get a number ("door" -> "door_2"), also in the copies' outputs
	std::string namePrefix;         ///< Put before every name of the pasted entities (and where the copies refer to them)
	bool textureLock = true;
};

namespace edit {

/// The selected brushes and entities as VMF text. Brushes of a brush entity that is not selected
/// as a whole are copied as world brushes. Empty when nothing is selected.
[[nodiscard]] std::string copyObjects(const MapDocument& doc, const Selection& selection);
/// Text that `pasteObjects` understands (copied map objects or a whole VMF).
[[nodiscard]] bool looksLikeMapObjects(std::string_view text);
/// Box around the objects in copied text (to place them under the cursor); none if there are none.
[[nodiscard]] std::optional<Bounds> copiedBounds(std::string_view text, const EntityCatalog* catalog);
/// Inserts the copied objects `options.copies` times as one undo step ("Paste"), with new ids.
/// Returns the change and the selection of everything pasted; nullopt (with `error`) for text
/// that is no map.
[[nodiscard]] std::optional<std::pair<Change, Selection>> pasteObjects(MapDocument& doc, std::string_view text, const PasteOptions& options,
                                                                       const EntityCatalog* catalog, std::string* error = nullptr);

} // namespace edit
} // namespace sme
