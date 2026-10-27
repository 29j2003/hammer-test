// visgroups.cpp - VisGroups (see visgroups.h).

#include <sme/visgroups.h>

#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sme {

namespace {

int toInt(const std::string& text) {
	return std::atoi(text.c_str());
}

bool isGroupNode(const KvNode& node) {
	return node.block && iequals(node.key, "visgroup");
}

VisGroup readGroup(const KvNode& node) {
	VisGroup group;
	group.id = toInt(node.get("visgroupid", "0"));
	group.name = node.get("name");
	group.color = node.get("color", "255 255 255");
	for (const auto& child : node.children) {
		if (isGroupNode(child)) group.children.push_back(readGroup(child));
	}
	return group;
}

/// The `visgroups` block before the world; made when `create` and it is missing.
KvNode* groupsBlock(MapDocument& doc, bool create) {
	for (auto& block : doc.headerBlocks) {
		if (block.block && iequals(block.key, "visgroups")) return &block;
	}
	if (!create) return nullptr;
	// Hammer writes it right after versioninfo.
	auto it = std::find_if(doc.headerBlocks.begin(), doc.headerBlocks.end(), [](const KvNode& b) { return iequals(b.key, "versioninfo"); });
	it = doc.headerBlocks.insert(it == doc.headerBlocks.end() ? doc.headerBlocks.begin() : it + 1, KvNode::makeBlock("visgroups"));
	return &*it;
}

KvNode* findGroupNode(KvNode& parent, int id) {
	for (auto& child : parent.children) {
		if (!isGroupNode(child)) continue;
		if (toInt(child.get("visgroupid", "0")) == id) return &child;
		if (auto* found = findGroupNode(child, id)) return found;
	}
	return nullptr;
}

/// Removes the group below `parent`; its sub-groups take its place. True if found.
bool eraseGroupNode(KvNode& parent, int id) {
	for (std::size_t i = 0; i < parent.children.size(); ++i) {
		auto& child = parent.children[i];
		if (!isGroupNode(child)) continue;
		if (toInt(child.get("visgroupid", "0")) == id) {
			std::vector<KvNode> subGroups;
			for (auto& sub : child.children) {
				if (isGroupNode(sub)) subGroups.push_back(std::move(sub));
			}
			parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(i));
			parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(i), subGroups.begin(), subGroups.end());
			return true;
		}
		if (eraseGroupNode(child, id)) return true;
	}
	return false;
}

void collectIds(const std::vector<VisGroup>& groups, std::vector<int>& out) {
	for (const auto& g : groups) {
		out.push_back(g.id);
		collectIds(g.children, out);
	}
}

const KvNode* editorBlock(const std::vector<KvNode>& extra) {
	for (const auto& node : extra) {
		if (node.block && iequals(node.key, "editor")) return &node;
	}
	return nullptr;
}

/// The object's `editor` block (Hammer writes one for every brush and entity); made when missing.
KvNode& editorBlock(std::vector<KvNode>& extra) {
	for (auto& node : extra) {
		if (node.block && iequals(node.key, "editor")) return node;
	}
	auto editor = KvNode::makeBlock("editor");
	editor.set("color", "220 220 220");
	editor.set("visgroupshown", "1");
	editor.set("visgroupautoshown", "1");
	extra.push_back(std::move(editor));
	return extra.back();
}

bool hasId(const std::vector<KvNode>& extra, int id) {
	const auto ids = visGroupIdsOf(extra);
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void addId(std::vector<KvNode>& extra, int id) {
	if (hasId(extra, id)) return;
	auto& editor = editorBlock(extra);
	// Next to the other group ids, before visgroupshown (Hammer's order).
	auto at = std::find_if(editor.children.begin(), editor.children.end(), [](const KvNode& n) { return iequals(n.key, "visgroupshown"); });
	editor.children.insert(at, KvNode::makeValue("visgroupid", std::to_string(id)));
}

bool removeId(std::vector<KvNode>& extra, int id) {
	for (auto& node : extra) {
		if (!node.block || !iequals(node.key, "editor")) continue;
		const auto before = node.children.size();
		std::erase_if(node.children, [id](const KvNode& n) { return !n.block && iequals(n.key, "visgroupid") && toInt(n.value) == id; });
		return node.children.size() != before;
	}
	return false;
}

/// A clearly different colour for each new group.
std::string groupColor(int id) {
	const double hue = std::fmod(id * 137.508, 360.0) / 60.0;
	const double x = 1 - std::abs(std::fmod(hue, 2.0) - 1);
	double r = 0, g = 0, b = 0;
	switch (static_cast<int>(hue)) {
		case 0: r = 1; g = x; break;
		case 1: r = x; g = 1; break;
		case 2: g = 1; b = x; break;
		case 3: g = x; b = 1; break;
		case 4: r = x; b = 1; break;
		default: r = 1; b = x; break;
	}
	const auto channel = [](double v) { return std::to_string(static_cast<int>(70 + v * 160)); };
	return channel(r) + " " + channel(g) + " " + channel(b);
}

/// Calls `visit(extra, touch)` for every world brush and entity of the selection.
template <typename F>
void forEachMember(MapDocument& doc, Transaction& t, const Selection& selection, F&& visit) {
	const auto candidates = visGroupCandidates(selection);
	for (const auto& ref : candidates.solids) {
		if (doc.findSolid(ref)) {
			t.touchSolid(ref);
			visit(doc.findSolid(ref)->extra);
		}
	}
	for (const int id : candidates.entities) {
		if (id != 0 && doc.findEntity(id)) {
			t.touchEntity(id);
			visit(doc.findEntity(id)->extra);
		}
	}
}

} // namespace

std::vector<VisGroup> visGroups(const MapDocument& doc) {
	std::vector<VisGroup> out;
	for (const auto& block : doc.headerBlocks) {
		if (!block.block || !iequals(block.key, "visgroups")) continue;
		for (const auto& child : block.children) {
			if (isGroupNode(child)) out.push_back(readGroup(child));
		}
	}
	return out;
}

const VisGroup* findVisGroup(const std::vector<VisGroup>& groups, int id) {
	for (const auto& g : groups) {
		if (g.id == id) return &g;
		if (const auto* found = findVisGroup(g.children, id)) return found;
	}
	return nullptr;
}

std::vector<int> visGroupAndChildren(const std::vector<VisGroup>& groups, int id) {
	std::vector<int> out;
	if (const auto* group = findVisGroup(groups, id)) {
		out.push_back(group->id);
		collectIds(group->children, out);
	}
	return out;
}

std::vector<int> visGroupIdsOf(const std::vector<KvNode>& extra) {
	std::vector<int> out;
	if (const auto* editor = editorBlock(extra)) {
		for (const auto& node : editor->children) {
			if (!node.block && iequals(node.key, "visgroupid")) out.push_back(toInt(node.value));
		}
	}
	return out;
}

Selection visGroupMembers(const MapDocument& doc, int id, bool withChildren) {
	const auto ids = withChildren ? visGroupAndChildren(visGroups(doc), id) : std::vector<int>{id};
	const auto inGroup = [&](const std::vector<KvNode>& extra) {
		const auto own = visGroupIdsOf(extra);
		return std::any_of(own.begin(), own.end(), [&](int g) { return std::find(ids.begin(), ids.end(), g) != ids.end(); });
	};
	Selection out;
	for (const auto& solid : doc.world.solids) {
		if (inGroup(solid.extra)) out.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		if (inGroup(entity.extra)) out.entities.push_back(entity.id);
	}
	return out;
}

Selection visGroupCandidates(const Selection& selection) {
	Selection out;
	const auto addSolid = [&](const SolidRef& ref) {
		if (ref.entityId == 0) {
			if (!out.contains(ref)) out.solids.push_back(ref);
		} else if (!out.containsEntity(ref.entityId)) {
			out.entities.push_back(ref.entityId);   // a brush entity is grouped as a whole
		}
	};
	for (const auto& ref : selection.solids) addSolid(ref);
	for (const auto& face : selection.faces) addSolid(face.solid);
	for (const int id : selection.entities) {
		if (id != 0 && !out.containsEntity(id)) out.entities.push_back(id);
	}
	return out;
}

std::pair<Change, int> createVisGroup(MapDocument& doc, const std::string& name, const Selection& members, int parentId) {
	Transaction t(doc, "New visgroup");
	t.touchHeader();
	std::vector<int> ids;
	collectIds(visGroups(doc), ids);
	const int id = ids.empty() ? 1 : *std::max_element(ids.begin(), ids.end()) + 1;
	auto* block = groupsBlock(doc, true);
	KvNode* parent = parentId != 0 ? findGroupNode(*block, parentId) : nullptr;
	auto group = KvNode::makeBlock("visgroup");
	group.set("name", name.empty() ? "Group " + std::to_string(id) : name);
	group.set("visgroupid", std::to_string(id));
	group.set("color", groupColor(id));
	(parent ? parent : block)->children.push_back(std::move(group));
	forEachMember(doc, t, members, [id](std::vector<KvNode>& extra) { addId(extra, id); });
	return {t.finish(), id};
}

Change renameVisGroup(MapDocument& doc, int id, const std::string& name) {
	Transaction t(doc, "Rename visgroup");
	t.touchHeader();
	if (auto* block = groupsBlock(doc, false)) {
		if (auto* node = findGroupNode(*block, id)) node->set("name", name);
	}
	return t.finish();
}

Change setVisGroupColor(MapDocument& doc, int id, const std::string& color) {
	Transaction t(doc, "Visgroup color");
	t.touchHeader();
	if (auto* block = groupsBlock(doc, false)) {
		if (auto* node = findGroupNode(*block, id)) node->set("color", color);
	}
	return t.finish();
}

Change deleteVisGroup(MapDocument& doc, int id) {
	Transaction t(doc, "Delete visgroup");
	t.touchHeader();
	if (auto* block = groupsBlock(doc, false)) eraseGroupNode(*block, id);
	// Members leave only this group (not the moved-up sub-groups).
	for (auto& solid : doc.world.solids) {
		if (hasId(solid.extra, id)) {
			t.touchSolid({0, solid.id});
			removeId(doc.findSolid({0, solid.id})->extra, id);
		}
	}
	for (auto& entity : doc.entities) {
		if (hasId(entity.extra, id)) {
			t.touchEntity(entity.id);
			removeId(doc.findEntity(entity.id)->extra, id);
		}
	}
	return t.finish();
}

Change addToVisGroup(MapDocument& doc, int id, const Selection& selection) {
	Transaction t(doc, "Add to visgroup");
	forEachMember(doc, t, selection, [id](std::vector<KvNode>& extra) { addId(extra, id); });
	return t.finish();
}

Change removeFromVisGroup(MapDocument& doc, int id, const Selection& selection) {
	Transaction t(doc, "Remove from visgroup");
	forEachMember(doc, t, selection, [id](std::vector<KvNode>& extra) { removeId(extra, id); });
	return t.finish();
}

std::set<int> hiddenVisGroupsInFile(const MapDocument& doc) {
	std::set<int> hidden;
	std::vector<int> ids;
	collectIds(visGroups(doc), ids);
	for (const int id : ids) {
		bool any = false, allHidden = true;
		const auto check = [&](const std::vector<KvNode>& extra) {
			if (!hasId(extra, id)) return;
			any = true;
			const auto* editor = editorBlock(extra);
			if (!editor || editor->get("visgroupshown", "1") != "0") allHidden = false;
		};
		for (const auto& solid : doc.world.solids) check(solid.extra);
		for (const auto& entity : doc.entities) check(entity.extra);
		if (any && allHidden) hidden.insert(id);
	}
	return hidden;
}

Selection hiddenByVisGroups(const MapDocument& doc, const std::set<int>& hiddenGroups) {
	Selection out;
	if (hiddenGroups.empty()) return out;
	// A hidden group hides its sub-groups too.
	const auto groups = visGroups(doc);
	std::set<int> all;
	for (const int id : hiddenGroups) {
		for (const int sub : visGroupAndChildren(groups, id)) all.insert(sub);
		all.insert(id);
	}
	const auto hiddenObject = [&](const std::vector<KvNode>& extra) {
		const auto ids = visGroupIdsOf(extra);
		return std::any_of(ids.begin(), ids.end(), [&](int g) { return all.contains(g); });
	};
	for (const auto& solid : doc.world.solids) {
		if (hiddenObject(solid.extra)) out.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		if (hiddenObject(entity.extra)) out.entities.push_back(entity.id);
	}
	return out;
}

void writeVisGroupShown(MapDocument& doc, const std::set<int>& hiddenGroups) {
	const auto hidden = hiddenByVisGroups(doc, hiddenGroups);
	const auto mark = [](std::vector<KvNode>& extra, bool isHidden) {
		if (visGroupIdsOf(extra).empty()) return;
		editorBlock(extra).set("visgroupshown", isHidden ? "0" : "1");
	};
	for (auto& solid : doc.world.solids) mark(solid.extra, hidden.contains(SolidRef{0, solid.id}));
	for (auto& entity : doc.entities) mark(entity.extra, hidden.containsEntity(entity.id));
}

} // namespace sme
