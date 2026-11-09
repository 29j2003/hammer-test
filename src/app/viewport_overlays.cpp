// viewport_overlays.cpp - Dragging the corners of an info_overlay in the map view (see viewport.h).
//
// With one info_overlay selected (and a selection tool active) its four corners show as small
// squares in the overlay's plane. Dragging one moves that corner (on the grid while snapping is
// on); letting go writes uv0..uv3 and adds faces in the same plane that the overlay now reaches
// to its "sides" (sme::moveOverlayCorner) - one undo step. Hammer only offers this through the
// overlay's handles in its own tool; here it works with the normal selection.

#include "viewport.h"

#include <QMouseEvent>

#include <cmath>

#include <sme/overlay_tools.h>
#include <sme/text.h>

namespace sme::app {

namespace {

const QVector4D CORNER_COLOR{0.95f, 0.35f, 0.95f, 1};
const QVector4D EDGE_COLOR{0.95f, 0.35f, 0.95f, 0.6f};

bool selectionTool(Editor::Tool tool) {
	return tool == Editor::Tool::Select || tool == Editor::Tool::Move || tool == Editor::Tool::Rotate || tool == Editor::Tool::Scale;
}

void addLine(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

} // namespace

const Entity* Viewport::selectedOverlay() const {
	const auto& selection = m_editor->selection();
	if (!selectionTool(m_editor->tool()) || selection.entities.size() != 1 || !selection.solids.empty() || !selection.faces.empty()) return nullptr;
	const Entity* entity = m_editor->doc().findEntity(selection.entities.front());
	return entity && iequals(entity->classname(), "info_overlay") ? entity : nullptr;
}

bool Viewport::overlayCornerPress(QMouseEvent* event) {
	const Entity* entity = selectedOverlay();
	if (!entity || event->modifiers() != Qt::NoModifier) return false;
	const auto overlay = readOverlay(*entity);
	if (!overlay) return false;
	const auto corners = overlayCorners(*overlay);
	for (int i = 0; i < 4; ++i) {
		QPointF q;
		if (m_camera.project(corners[static_cast<std::size_t>(i)], width(), height(), q) && QLineF(q, event->position()).length() < 8) {
			m_drag = Drag::OverlayCorner;
			m_overlayEntity = entity->id;
			m_overlayCorner = i;
			m_overlayStart = m_overlayDrag = *overlay;
			m_overlayPoint = corners[static_cast<std::size_t>(i)];
			emit m_editor->message(tr("Drag the overlay's corner - it may reach over onto the next face"));
			return true;
		}
	}
	return false;
}

void Viewport::overlayCornerMove(const QPointF& pos) {
	const Vec3 n = normalize(m_overlayStart.normal);
	std::optional<Vec3> p;
	if (m_camera.is2D()) {
		p = viewPlanePoint(pos);
	} else {
		p = rayPlane(rayAt(pos), m_overlayStart.origin, n);
	}
	if (!p) return;
	Vec3 point = *p;
	if (m_editor->snapGrid() > 0) point = snap(point, m_editor->snapGrid());
	point -= n * dot(point - m_overlayStart.origin, n);   // back into the overlay's plane
	m_overlayPoint = point;
	m_overlayDrag.corners[static_cast<std::size_t>(m_overlayCorner)] = {dot(point - m_overlayStart.origin, m_overlayStart.u),
	                                                                    dot(point - m_overlayStart.origin, m_overlayStart.v), 0};
	requestFrame();
}

void Viewport::finishOverlayCorner() {
	m_drag = Drag::None;
	const int corner = m_overlayCorner;
	m_overlayCorner = -1;
	auto& doc = m_editor->doc();
	Entity* entity = doc.findEntity(m_overlayEntity);
	if (!entity || corner < 0) return;
	const auto start = overlayCorners(m_overlayStart)[static_cast<std::size_t>(corner)];
	if (length(start - m_overlayPoint) < 0.01) {
		requestFrame();
		return;
	}
	const auto moved = moveOverlayCorner(collectSurfaceFaces(doc), m_overlayStart, corner, m_overlayPoint);
	Transaction t(doc, "Move overlay corner");
	t.touchEntity(m_overlayEntity);
	entity = doc.findEntity(m_overlayEntity);
	const auto keys = overlayKeys(moved, entity->get("material"));
	for (const auto& [key, value] : keys) {
		if (key == "sides" || key.rfind("uv", 0) == 0) entity->set(key, value);
	}
	m_editor->commit(t.finish());
	if (moved.sides.size() > m_overlayStart.sides.size()) {
		emit m_editor->message(tr("The overlay now covers %n face(s)", nullptr, int(moved.sides.size())));
	}
	requestFrame();
}

void Viewport::drawOverlayCorners(std::vector<LineVertex>& lines) const {
	OverlayInfo overlay;
	if (m_drag == Drag::OverlayCorner) {
		overlay = m_overlayDrag;
	} else {
		const Entity* entity = selectedOverlay();
		if (!entity) return;
		const auto read = readOverlay(*entity);
		if (!read) return;
		overlay = *read;
	}
	const Vec3 n = normalize(overlay.normal);
	const auto corners = overlayCorners(overlay);
	const Vec3 lift = n * 0.5;   // just above the surface
	for (int i = 0; i < 4; ++i) addLine(lines, corners[static_cast<std::size_t>(i)] + lift, corners[static_cast<std::size_t>((i + 1) % 4)] + lift, EDGE_COLOR);
	// Squares of about 6 pixels: their size follows the distance to the camera.
	for (const auto& c : corners) {
		const double distance = m_camera.is2D() ? 0 : length(c - fromQt(m_camera.position));
		const double half = m_camera.is2D() ? 3.0 / std::max(1e-6, double(m_camera.zoom)) : std::max(1.0, distance * 0.006);
		const Vec3 a = normalize(overlay.u) * half, b = normalize(overlay.v) * half;
		const Vec3 p = c + lift;
		addLine(lines, p - a - b, p + a - b, CORNER_COLOR);
		addLine(lines, p + a - b, p + a + b, CORNER_COLOR);
		addLine(lines, p + a + b, p - a + b, CORNER_COLOR);
		addLine(lines, p - a + b, p - a - b, CORNER_COLOR);
	}
}

} // namespace sme::app
