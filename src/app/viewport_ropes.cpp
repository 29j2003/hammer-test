// viewport_ropes.cpp - Ropes drawn the way they hang (see viewport.h, map_helpers.h).
//
// Hammer draws a move_rope chain as straight lines; here each span sags by its Slack, so the
// mapper sees the rope before compiling. The curves are rebuilt only after the map changed and
// only while helpers (the kind ropes belong to) are shown.

#include "viewport.h"

#include <sme/map_helpers.h>

namespace sme::app {

namespace {

const QVector4D ROPE_COLOR{0.55f, 0.45f, 0.30f, 1};

} // namespace

void Viewport::drawRopes(std::vector<LineVertex>& lines) {
	if (!m_editor->kindVisible(ObjectKind::Helpers)) return;
	if (m_ropesDirty) {
		m_ropeLines.clear();
		for (const auto& curve : ropeCurves(m_editor->doc())) {
			for (std::size_t i = 0; i + 1 < curve.points.size(); ++i) {
				m_ropeLines.push_back({toQt(curve.points[i]), ROPE_COLOR});
				m_ropeLines.push_back({toQt(curve.points[i + 1]), ROPE_COLOR});
			}
		}
		m_ropesDirty = false;
	}
	lines.insert(lines.end(), m_ropeLines.begin(), m_ropeLines.end());
}

} // namespace sme::app
