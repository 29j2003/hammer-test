// history.cpp - Undo/redo (see history.h).

#include <sme/history.h>

#include <algorithm>
#include <limits>

namespace sme {

namespace {

Change::WorldState worldState(const MapDocument& doc) {
	return {doc.world.keyValues, doc.world.connections, doc.world.extra};
}

void applyWorld(MapDocument& doc, const Change::WorldState& state) {
	doc.world.keyValues = state.keyValues;
	doc.world.connections = state.connections;
	doc.world.extra = state.extra;
}

void applyEntity(MapDocument& doc, int entityId, const std::optional<Entity>& state) {
	auto it = std::find_if(doc.entities.begin(), doc.entities.end(), [entityId](const Entity& e) { return e.id == entityId; });
	if (state) {
		if (it != doc.entities.end()) {
			*it = *state;
		} else {
			doc.entities.push_back(*state);
		}
	} else if (it != doc.entities.end()) {
		doc.entities.erase(it);
	}
}

void applySolid(MapDocument& doc, const SolidRef& ref, const std::optional<Solid>& state) {
	auto* entity = doc.findEntity(ref.entityId);
	if (!entity) {
		return;
	}
	auto& solids = entity->solids;
	auto it = std::find_if(solids.begin(), solids.end(), [&](const Solid& s) { return s.id == ref.solidId; });
	if (state) {
		if (it != solids.end()) {
			*it = *state;
		} else {
			solids.push_back(*state);
		}
	} else if (it != solids.end()) {
		solids.erase(it);
	}
}

void apply(MapDocument& doc, const Change& change, bool after) {
	// Entities first (a solid may live in an entity that is being restored), solids second.
	for (const auto& e : change.entities) {
		applyEntity(doc, e.entityId, after ? e.after : e.before);
	}
	for (const auto& s : change.solids) {
		applySolid(doc, s.ref, after ? s.after : s.before);
	}
	if (const auto& world = after ? change.worldAfter : change.worldBefore) {
		applyWorld(doc, *world);
	}
	if (const auto& header = after ? change.headerAfter : change.headerBefore) {
		doc.headerBlocks = *header;
	}
}

} // namespace

Transaction::Transaction(MapDocument& doc, std::string name) : m_doc(doc) {
	m_change.name = std::move(name);
}

void Transaction::touchSolid(const SolidRef& ref) {
	if (std::any_of(m_change.solids.begin(), m_change.solids.end(), [&](const Change::SolidState& s) { return s.ref == ref; })) {
		return;
	}
	Change::SolidState state;
	state.ref = ref;
	if (const auto* solid = m_doc.findSolid(ref)) {
		state.before = *solid;
	}
	m_change.solids.push_back(std::move(state));
}

void Transaction::touchEntity(int entityId) {
	if (entityId == 0) {
		touchWorld();
		return;
	}
	if (std::any_of(m_change.entities.begin(), m_change.entities.end(), [&](const Change::EntityState& e) { return e.entityId == entityId; })) {
		return;
	}
	Change::EntityState state;
	state.entityId = entityId;
	if (const auto* entity = m_doc.findEntity(entityId)) {
		state.before = *entity;
	}
	m_change.entities.push_back(std::move(state));
}

void Transaction::touchWorld() {
	if (!m_change.worldBefore) {
		m_change.worldBefore = worldState(m_doc);
	}
}

void Transaction::touchHeader() {
	if (!m_change.headerBefore) {
		m_change.headerBefore = m_doc.headerBlocks;
	}
}

Change Transaction::finish() {
	for (auto& s : m_change.solids) {
		if (const auto* solid = m_doc.findSolid(s.ref)) {
			s.after = *solid;
		}
	}
	for (auto& e : m_change.entities) {
		if (const auto* entity = m_doc.findEntity(e.entityId)) {
			e.after = *entity;
		}
	}
	if (m_change.worldBefore) {
		m_change.worldAfter = worldState(m_doc);
	}
	if (m_change.headerBefore) {
		m_change.headerAfter = m_doc.headerBlocks;
	}
	m_doc.updateIdCounters();
	return std::move(m_change);
}

void History::push(Change change, bool merge) {
	if (change.empty()) {
		return;
	}
	if (merge && m_index > 0 && m_index == m_changes.size() && m_changes.back().name == change.name && m_savedIndex != m_index) {
		auto& last = m_changes.back();
		for (auto& s : change.solids) {
			auto it = std::find_if(last.solids.begin(), last.solids.end(), [&](const Change::SolidState& o) { return o.ref == s.ref; });
			if (it != last.solids.end()) {
				it->after = std::move(s.after);
			} else {
				last.solids.push_back(std::move(s));
			}
		}
		for (auto& e : change.entities) {
			auto it = std::find_if(last.entities.begin(), last.entities.end(), [&](const Change::EntityState& o) { return o.entityId == e.entityId; });
			if (it != last.entities.end()) {
				it->after = std::move(e.after);
			} else {
				last.entities.push_back(std::move(e));
			}
		}
		if (change.worldBefore) {
			if (!last.worldBefore) {
				last.worldBefore = std::move(change.worldBefore);
			}
			last.worldAfter = std::move(change.worldAfter);
		}
		if (change.headerBefore) {
			if (!last.headerBefore) {
				last.headerBefore = std::move(change.headerBefore);
			}
			last.headerAfter = std::move(change.headerAfter);
		}
		++m_revision;
		return;
	}
	m_changes.resize(m_index);
	if (m_savedIndex > m_index) {
		m_savedIndex = std::numeric_limits<std::size_t>::max(); // the saved state can no longer be reached
	}
	m_changes.push_back(std::move(change));
	if (m_changes.size() > LIMIT) {
		m_changes.erase(m_changes.begin());
		if (m_savedIndex != std::numeric_limits<std::size_t>::max()) {
			m_savedIndex = m_savedIndex == 0 ? std::numeric_limits<std::size_t>::max() : m_savedIndex - 1;
		}
	}
	m_index = m_changes.size();
	++m_revision;
}

bool History::undo(MapDocument& doc) {
	if (!canUndo()) {
		return false;
	}
	apply(doc, m_changes[--m_index], false);
	doc.updateIdCounters();
	++m_revision;
	return true;
}

bool History::redo(MapDocument& doc) {
	if (!canRedo()) {
		return false;
	}
	apply(doc, m_changes[m_index++], true);
	doc.updateIdCounters();
	++m_revision;
	return true;
}

void History::clear() {
	m_changes.clear();
	m_index = 0;
	m_savedIndex = 0;
	++m_revision;
}

std::string History::undoName() const {
	return canUndo() ? m_changes[m_index - 1].name : std::string{};
}

std::string History::redoName() const {
	return canRedo() ? m_changes[m_index].name : std::string{};
}

} // namespace sme
