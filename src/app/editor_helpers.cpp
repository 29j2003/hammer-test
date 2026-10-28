// editor_helpers.cpp - Editor: ropes and ladders from the selection (see map_helpers.h).

#include "editor.h"

#include <sme/brush.h>
#include <sme/map_helpers.h>

namespace sme::app {

void Editor::makeRopeFromSelection() {
	const auto points = edit::ropePointsOf(m_doc, m_selection, entities());
	if (points.size() < 2) {
		emit message(tr("Select two or more objects (entities or brushes) for the rope to hang between"));
		return;
	}
	auto rope = edit::makeRope(m_doc, points, entities());
	if (rope.entityIds.empty()) return;
	commit(std::move(rope.change));
	Selection made;
	made.entities = rope.entityIds;
	setSelection(made);
	emit message(tr("Made a rope through %1 points (move_rope + keyframe_rope; Slack sets how far it hangs)").arg(points.size()));
}

void Editor::makeLadderFromSelection() {
	Bounds box;
	for (const auto& ref : m_selection.solids) {
		for (const auto& solid : m_doc.world.solids) {
			if (solid.id != ref.solidId) continue;
			for (const auto& face : buildBrushGeometry(solid).faces) {
				for (const auto& v : face.vertices) box.add(v);
			}
		}
	}
	for (const int id : m_selection.entities) {
		if (const auto* entity = m_doc.findEntity(id); entity && id != 0) box.add(edit::entityBounds(*entity, entities()));
	}
	if (box.empty()) {
		emit message(tr("Select the ladder first (a brush or a ladder model)"));
		return;
	}
	auto ladder = edit::makeLadder(m_doc, box, entities());
	if (ladder.ladderId == 0) {
		emit message(tr("The selection is too low for a ladder (at least 32 units high)"));
		return;
	}
	commit(std::move(ladder.change));
	Selection made;
	made.entities.push_back(ladder.ladderId);
	made.entities.insert(made.entities.end(), ladder.dismountIds.begin(), ladder.dismountIds.end());
	setSelection(made);
	emit message(tr("Made a climbable ladder: func_useableladder in front, info_ladder_dismount at the top and the bottom"));
}

} // namespace sme::app
