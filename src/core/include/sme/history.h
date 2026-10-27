// history.h - Undo/redo for map edits.
//
// An edit opens a Transaction, "touches" every solid/entity it is about to change (that
// stores a copy of the old state), changes the document and then commits. Undo puts the
// old copies back, redo the new ones. Only touched objects are copied, so edits on big
// maps stay cheap.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <sme/map.h>

namespace sme {

/// One undo step: the state before and after of everything it touched.
struct Change {
	/// A brush before/after (nullopt: did not exist).
	struct SolidState {
		SolidRef ref;
		std::optional<Solid> before, after;   ///< nullopt = did not exist
	};
	/// An entity (with its brushes) before/after.
	struct EntityState {
		int entityId = 0;
		std::optional<Entity> before, after;
	};
	/// The world's keys, outputs and extra blocks (not its brushes).
	struct WorldState {
		std::vector<std::pair<std::string, std::string>> keyValues;
		std::vector<Connection> connections;
		std::vector<KvNode> extra;
	};

	std::string name;
	std::vector<SolidState> solids;
	std::vector<EntityState> entities;
	std::optional<WorldState> worldBefore, worldAfter;   ///< World key/values (not its solids)
	/// Blocks before the world (visgroups, viewsettings, ...), when the change touched them.
	std::optional<std::vector<KvNode>> headerBefore, headerAfter;

	/// Nothing was touched.
	[[nodiscard]] bool empty() const { return solids.empty() && entities.empty() && !worldBefore && !headerBefore; }
};

/// Records a change while it is made: touch objects before changing them, then finish().
/// Typical use: `Transaction t(doc, "Move"); t.touchSolid(ref); ...change doc...; editor.commit(t.finish());`
class Transaction {
public:
	Transaction(MapDocument& doc, std::string name);

	/// Call before changing, creating (store the ref first) or deleting a solid.
	void touchSolid(const SolidRef& ref);
	/// Call before changing, creating or deleting an entity (includes its brushes). Not for the world (id 0).
	void touchEntity(int entityId);
	/// Call before changing world key/values.
	void touchWorld();
	/// Call before changing the blocks before the world (e.g. the visgroup list).
	void touchHeader();

	/// Captures the new state of everything touched.
	[[nodiscard]] Change finish();

private:
	MapDocument& m_doc;
	Change m_change;
};

/// Undo/redo list of Changes, limited to LIMIT steps.
class History {
public:
	/// Records a change. With `merge`, a change with the same name as the last one is folded into
	/// it (one undo step for a whole paint stroke).
	void push(Change change, bool merge = false);
	bool undo(MapDocument& doc);
	bool redo(MapDocument& doc);
	void clear();

	/// Whether undo / redo has something to do.
	[[nodiscard]] bool canUndo() const { return m_index > 0; }
	[[nodiscard]] bool canRedo() const { return m_index < m_changes.size(); }
	/// Names of the next undo / redo step ("Move"), empty if none.
	[[nodiscard]] std::string undoName() const;
	[[nodiscard]] std::string redoName() const;
	/// Recorded changes, oldest first (undone ones stay until something new is pushed).
	[[nodiscard]] std::size_t size() const { return m_changes.size(); }
	/// How many changes are applied: changes [0, index) are in the document, [index, size) are undone.
	[[nodiscard]] std::size_t index() const { return m_index; }
	/// Name of change `i` ("Move", "Paint", ...); empty when out of range.
	[[nodiscard]] std::string nameAt(std::size_t i) const { return i < m_changes.size() ? m_changes[i].name : std::string{}; }
	/// Increases with every push/undo/redo (for "modified" tracking and redraws).
	[[nodiscard]] unsigned revision() const { return m_revision; }
	/// Revision at which the document matches the file on disk.
	void markSaved() { m_savedIndex = m_index; }
	[[nodiscard]] bool modified() const { return m_savedIndex != m_index; }

	static constexpr std::size_t LIMIT = 500;

private:
	std::vector<Change> m_changes;
	std::size_t m_index = 0;
	std::size_t m_savedIndex = 0;
	unsigned m_revision = 0;
};

} // namespace sme
