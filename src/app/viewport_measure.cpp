// viewport_measure.cpp - The measure tool in a view (two points, distances, slope, the player's
// size for scale) and the ghost props of "Place props" before they are placed.
//
// Part of Viewport (viewport.h); split off to keep viewport.cpp readable.

#include "viewport.h"

#include <QFontMetrics>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

#include <sme/prop_placement.h>

namespace sme::app {

namespace {

const QVector4D MEASURE_COLOR{1.0f, 0.85f, 0.25f, 1};
const QVector4D LEG_COLOR{1.0f, 0.85f, 0.25f, 0.45f};
const QVector4D PLAYER_COLOR{0.45f, 0.9f, 1.0f, 0.7f};
const QVector4D PROP_COLOR{0.4f, 1.0f, 0.55f, 0.9f};
constexpr int MAX_GHOSTS = 200;   ///< Props drawn as models in the preview (the rest as markers)

void addLine(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

/// A standing player (32 x 32 x 72) on the point, for scale.
void playerBox(std::vector<LineVertex>& lines, const Vec3& at) {
	const Vec3 a{at.x - 16, at.y - 16, at.z}, b{at.x + 16, at.y + 16, at.z + 72};
	const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
	for (int k = 0; k < 4; ++k) {
		addLine(lines, c[k], c[(k + 1) % 4], PLAYER_COLOR);
		addLine(lines, c[k + 4], c[(k + 1) % 4 + 4], PLAYER_COLOR);
		addLine(lines, c[k], c[k + 4], PLAYER_COLOR);
	}
	// Eye height (64) and step height (18) as ticks.
	addLine(lines, {a.x, a.y, at.z + 64}, {b.x, a.y, at.z + 64}, PLAYER_COLOR);
	addLine(lines, {a.x, a.y, at.z + 18}, {b.x, a.y, at.z + 18}, PLAYER_COLOR);
}

QString units(double v) {
	return QString::number(std::round(v * 10) / 10, 'g', 7);
}

QString mark(bool yes) {
	return yes ? QStringLiteral("✓") : QStringLiteral("✗");
}

QString measureText(const Vec3& a, const Vec3& b) {
	const auto m = measure(a, b);
	QString text = QObject::tr("%1 units").arg(units(m.distance));
	QString detail = QObject::tr("across %1 · height %2%3 · slope %4°")
	                     .arg(units(m.horizontal), m.height >= 0 ? QStringLiteral("+") : QString(), units(m.height))
	                     .arg(std::round(m.slope * 10) / 10);
	// What it means for the player: a gap (mostly vertical: does he fit?) or a way (can he get up?).
	QString player;
	if (std::abs(m.height) > m.horizontal) {
		player = QObject::tr("%1 player fits (72)   %2 crouching (36)   %3 step up (18)   %4 jump up (56)")
		             .arg(mark(m.playerFits), mark(m.crouchFits), mark(std::abs(m.height) <= 18), mark(std::abs(m.height) <= 56));
	} else {
		player = QObject::tr("%1 walkable slope (45.6°)   %2 step up (18)   %3 jump up (56)   %4 wide enough (32)")
		             .arg(mark(m.walkable), mark(std::abs(m.height) <= 18), mark(std::abs(m.height) <= 56), mark(m.horizontal >= 32));
	}
	return text + "\n" + detail + "\n" + player;
}

} // namespace

std::optional<Vec3> Viewport::measurePointAt(const QPointF& pos, bool snapToGrid) const {
	const double grid = m_editor->snapGrid();
	if (m_camera.is2D()) {
		Vec3 p = viewPlanePoint(pos);
		if (snapToGrid) p = snap(p, grid);
		// The depth of the view stays that of the first point: a 2D view measures in its plane.
		if (const auto& first = m_editor->measureLine().a; first && !m_editor->measureLine().b) p[viewAxis()] = (*first)[viewAxis()];
		return p;
	}
	const Ray ray = rayAt(pos);
	if (const auto hit = m_renderer.pick(ray, true)) {
		Vec3 p = hit->point;
		if (snapToGrid) {
			// Along the surface onto the grid; across it the point stays on the surface.
			int across = 0;
			for (int axis = 1; axis < 3; ++axis) {
				if (std::abs(hit->normal[axis]) > std::abs(hit->normal[across])) across = axis;
			}
			const Vec3 snapped = snap(p, grid);
			for (int axis = 0; axis < 3; ++axis) {
				if (axis != across) p[axis] = snapped[axis];
			}
			// Back onto the surface (a slanted one moves across when moved along).
			double rest = 0;
			for (int axis = 0; axis < 3; ++axis) {
				if (axis != across) rest += hit->normal[axis] * (p[axis] - hit->point[axis]);
			}
			p[across] = hit->point[across] - rest / hit->normal[across];
		}
		return p;
	}
	if (const auto p = rayPlane(ray, {0, 0, 0}, {0, 0, 1})) return snapToGrid ? snap(*p, grid) : *p;
	return std::nullopt;
}

bool Viewport::measureMousePress(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) return false;
	const bool snapToGrid = m_editor->snapping() && !(event->modifiers() & Qt::ControlModifier);
	if (const auto point = measurePointAt(event->position(), snapToGrid)) {
		m_editor->measureClick(*point);
		const auto& line = m_editor->measureLine();
		if (line.a && line.b) emit m_editor->message(measureText(*line.a, *line.b).replace('\n', QStringLiteral("  ·  ")));
	}
	return true;
}

void Viewport::measureMouseMove(const QPointF& pos, Qt::KeyboardModifiers modifiers) {
	const auto& line = m_editor->measureLine();
	if (!line.a || line.b) return;
	m_editor->setMeasureHover(measurePointAt(pos, m_editor->snapping() && !(modifiers & Qt::ControlModifier)));
}

void Viewport::drawMeasureLines(std::vector<LineVertex>& lines) const {
	const auto& line = m_editor->measureLine();
	if (!line.a) return;
	const Vec3 a = *line.a;
	playerBox(lines, a);
	const auto end = line.end();
	if (!end) return;
	const Vec3 b = *end;
	addLine(lines, a, b, MEASURE_COLOR);
	// The legs: across the floor, then up.
	const Vec3 corner{b.x, b.y, a.z};
	if (std::abs(b.z - a.z) > 0.5 && std::hypot(b.x - a.x, b.y - a.y) > 0.5) {
		addLine(lines, a, corner, LEG_COLOR);
		addLine(lines, corner, b, LEG_COLOR);
	}
	// Small crosses on both ends.
	for (const Vec3& p : {a, b}) {
		addLine(lines, p - Vec3{6, 0, 0}, p + Vec3{6, 0, 0}, MEASURE_COLOR);
		addLine(lines, p - Vec3{0, 6, 0}, p + Vec3{0, 6, 0}, MEASURE_COLOR);
		addLine(lines, p - Vec3{0, 0, 6}, p + Vec3{0, 0, 6}, MEASURE_COLOR);
	}
	if (line.b) playerBox(lines, b);
}

void Viewport::drawMeasureLabels(OverlayRecorder& p) const {
	if (m_editor->tool() == Editor::Tool::Measure && !m_editor->measureLine().a) {
		p.drawText(QRectF(8, height() - 26, width() - 16, 20),
		           tr("Measure: click the first point (Ctrl: no grid snapping) - the box shows a standing player"), QColor(255, 220, 110));
	}
	const auto& line = m_editor->measureLine();
	const auto end = line.end();
	if (!line.a || !end) return;
	QPointF at;
	if (!m_camera.project((*line.a + *end) * 0.5, width(), height(), at)) return;
	const QString text = measureText(*line.a, *end);
	const QStringList rows = text.split('\n');
	// Beside the line's middle, kept inside the view (the player line is the longest).
	const QFontMetrics metrics(font());
	int wide = 0;
	for (const auto& r : rows) wide = std::max(wide, metrics.horizontalAdvance(r) + 8);
	const double x = std::clamp(at.x() + 10, 8.0, std::max(8.0, width() - wide - 8.0));
	const double y = std::clamp(at.y() - 10, 8.0, std::max(8.0, height() - 17.0 * rows.size() - 30));
	for (int i = 0; i < rows.size(); ++i) {
		p.drawText(QRectF(x, y + i * 17, wide, 17), rows[i], i == 0 ? QColor(255, 220, 110) : QColor(235, 235, 235));
	}
}

void Viewport::drawPropPreview(const SceneRenderer::Params& params, std::vector<LineVertex>& lines) {
	const auto& placements = m_editor->propPreview();
	const auto& items = m_editor->propPreviewItems();
	if (placements.empty() && items.empty()) return;
	const QString single = m_editor->propPreviewModel();
	int drawn = 0;
	// One model for all (Place props), or each its own (the building builder).
	const std::size_t total = placements.size() + items.size();
	for (std::size_t i = 0; i < total; ++i) {
		const bool own = i >= placements.size();
		const PropPlacement& placement = own ? items[i - placements.size()].second : placements[i];
		const QString& model = own ? items[i - placements.size()].first : single;
		const Vec3& o = placement.origin;
		// A post with a tick showing which way the prop faces.
		const double yaw = placement.yaw * 3.14159265358979323846 / 180.0;
		addLine(lines, o, o + Vec3{0, 0, 24}, PROP_COLOR);
		addLine(lines, o + Vec3{0, 0, 2}, o + Vec3{std::cos(yaw) * 16, std::sin(yaw) * 16, 2}, PROP_COLOR);
		if (model.isEmpty() || drawn >= MAX_GHOSTS) continue;
		QMatrix4x4 transform;
		transform.translate(toQt(o));
		transform.rotate(float(placement.yaw), 0, 0, 1);
		transform.rotate(float(placement.pitch), 0, 1, 0);
		transform.rotate(float(placement.roll), 1, 0, 0);
		transform.scale(float(placement.scale));
		m_renderer.drawGhostModel(params, model, transform, 0.8f);
		++drawn;
	}
}

} // namespace sme::app
