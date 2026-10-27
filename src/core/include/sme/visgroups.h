// visgroups.h - VisGroups: named, coloured groups of brushes and entities that can be shown and
// hidden together (Hammer's "Visible Groups").
//
// Where Hammer keeps them in a .vmf:
//   - the list of groups is the `visgroups` block before the world; groups nest
//     (`visgroup { "name" "visgroupid" "color" visgroup { ... } }`);
//   - an object belongs to a group through `"visgroupid" "<id>"` keys in its own `editor` block
//     (world brushes and entities; an object may be in several groups);
//   - whether a member was shown when the map was saved is its `"visgroupshown"` key.
//
// The editor keeps the blocks as KeyValues (unknown keys survive). This module reads them and
// changes them through Transactions, so every change is one undo step. Showing and hiding is not
// an edit of the map: the editor hides the members of hidden groups and writes `visgroupshown`
// only into the saved file (see writeVisGroupShown).

#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <sme/editing.h>

namespace sme {

/// One group with its sub-groups.
struct VisGroup {
	int id = 0;
	std::string name;
	std::string color;              ///< "r g b" (0-255), as stored
	std::vector<VisGroup> children;
};

/// The groups of the map (top level, in file order).
[[nodiscard]] std::vector<VisGroup> visGroups(const MapDocument& doc);
/// Group with the id anywhere in the tree, or nullptr.
[[nodiscard]] const VisGroup* findVisGroup(const std::vector<VisGroup>& groups, int id);
/// The id and the ids of all groups below it.
[[nodiscard]] std::vector<int> visGroupAndChildren(const std::vector<VisGroup>& groups, int id);

/// Groups an object is in: the `visgroupid` keys of the `editor` block in `extra`.
[[nodiscard]] std::vector<int> visGroupIdsOf(const std::vector<KvNode>& extra);

/// World brushes and entities in the group (and, with `withChildren`, in its sub-groups).
[[nodiscard]] Selection visGroupMembers(const MapDocument& doc, int id, bool withChildren = true);

/// Objects of a selection that can be in a group: world brushes and entities (faces count as
/// their brush).
[[nodiscard]] Selection visGroupCandidates(const Selection& selection);

// --- Changes (each one undo step) --------------------------------------------------------------

/// New group (inside `parentId`, 0 = top level) with the given members; returns its id.
[[nodiscard]] std::pair<Change, int> createVisGroup(MapDocument& doc, const std::string& name, const Selection& members, int parentId = 0);
[[nodiscard]] Change renameVisGroup(MapDocument& doc, int id, const std::string& name);
/// Colour as "r g b".
[[nodiscard]] Change setVisGroupColor(MapDocument& doc, int id, const std::string& color);
/// Removes the group; its sub-groups move up to its parent, its members simply leave it.
[[nodiscard]] Change deleteVisGroup(MapDocument& doc, int id);
[[nodiscard]] Change addToVisGroup(MapDocument& doc, int id, const Selection& selection);
[[nodiscard]] Change removeFromVisGroup(MapDocument& doc, int id, const Selection& selection);

// --- Showing and hiding ------------------------------------------------------------------------

/// Groups that were hidden when the map was saved: every member has `visgroupshown` "0".
[[nodiscard]] std::set<int> hiddenVisGroupsInFile(const MapDocument& doc);
/// Objects hidden because one of their groups (or a parent group) is hidden.
[[nodiscard]] Selection hiddenByVisGroups(const MapDocument& doc, const std::set<int>& hiddenGroups);
/// Sets `visgroupshown` of every group member ("0" when hidden by a group, else "1"). Used on a
/// copy of the document when saving.
void writeVisGroupShown(MapDocument& doc, const std::set<int>& hiddenGroups);

} // namespace sme
