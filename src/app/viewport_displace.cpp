// viewport_displace.cpp - The displacement tool of the map view (see viewport.h).
//
// Create mode: a click on a face makes it a displacement. The other modes sculpt or paint with a
// round brush: pressing the left mouse button starts a stroke, every bit of mouse movement
// applies a dab to the displacements under the brush (Shift inverts), letting go writes the
// changed displacements into the map as one undo step. While the stroke runs only the touched
// displacement meshes are uploaded again (SceneRenderer::updateDisplacement), not the scene.

#include "viewport.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <sme/brush.h>

#include <cmath>
#include <numbers>

namespace sme::app {

namespace {

const QVector4D BRUSH_COLOR{1.0f, 0.62f, 0.15f, 1};
const QVector4D BRUSH_INNER{1.0f, 0.62f, 0.15f, 0.45f};

/// The side with this id in the brush, or nullptr.
const Side* findSide(const Solid& solid, int sideId) {
	for (const auto& side : solid.sides) {
		if (side.id == sideId) return &side;
	}
	return nullptr;
}

/// How much one dab does at strength 1..100 in this mode (see SculptBrush::strength).
double dabStrength(Editor::DisplaceMode mode, int strength) {
	const double s = strength / 100.0;
	switch (mode) {
		case Editor::DisplaceMode::Raise:
		case Editor::DisplaceMode::Noise: return 16.0 * s;   // units
		case Editor::DisplaceMode::Smooth:
		case Editor::DisplaceMode::Flatten: return s;        // share of the way
		case Editor::DisplaceMode::Alpha: return 255.0 * s;  // blend
		case Editor::DisplaceMode::Create: break;
	}
	return 0;
}

SculptMode sculptMode(Editor::DisplaceMode mode) {
	switch (mode) {
		case Editor::DisplaceMode::Smooth: return SculptMode::Smooth;
		case Editor::DisplaceMode::Flatten: return SculptMode::Flatten;
		case Editor::DisplaceMode::Noise: return SculptMode::Noise;
		case Editor::DisplaceMode::Alpha: return SculptMode::PaintAlpha;
		default: return SculptMode::Raise;
	}
}

} // namespace

bool Viewport::displaceMousePress(QMouseEvent* event) {
	if (m_camera.is2D() || event->button() != Qt::LeftButton) {
		return false;   // 2D views select and move as usual
	}
	const auto& settings = m_editor->displaceSettings();
	const auto hit = m_renderer.pick(rayAt(event->position()), true);
	if (!hit || hit->sideId == 0) {
		return true;
	}
	const Solid* solid = m_editor->doc().findSolid(hit->solid);
	const Side* side = solid ? findSide(*solid, hit->sideId) : nullptr;
	if (!side) {
		return true;
	}
	if (settings.mode == Editor::DisplaceMode::Create) {
		m_editor->createDisplacements({FaceRef{hit->solid, hit->sideId}});
		return true;
	}
	if (!side->dispInfo()) {
		emit m_editor->message(tr("Sculpt on a displacement - the Create mode turns a face into one"));
		return true;
	}
	// The stroke works on every displacement that faces about the same way as the one clicked
	// (a terrain of many faces), so walls next to the ground stay untouched.
	const auto clickedGeometry = buildBrushGeometry(*solid);
	Vec3 clickedNormal = hit->normal;
	for (const auto& face : clickedGeometry.faces) {
		if (solid->sides[static_cast<std::size_t>(face.sideIndex)].id == side->id) clickedNormal = face.normal;
	}
	m_sculptFaces.clear();
	const auto& hidden = m_editor->hidden();
	const auto collect = [&](const Solid& brush, const SolidRef& ref) {
		if (hidden.contains(ref)) return;
		bool any = false;
		for (const auto& s : brush.sides) any = any || s.dispInfo();
		if (!any) return;
		for (const auto& face : buildBrushGeometry(brush).faces) {
			const Side& s = brush.sides[static_cast<std::size_t>(face.sideIndex)];
			if (!s.dispInfo() || dot(face.normal, clickedNormal) < 0.5) continue;
			if (auto d = readDisplacement(s)) m_sculptFaces.push_back({{ref, s.id}, s, face, std::move(*d), false});
		}
	};
	for (const auto& brush : m_editor->doc().world.solids) collect(brush, {0, brush.id});
	for (const auto& entity : m_editor->doc().entities) {
		if (hidden.containsEntity(entity.id)) continue;
		for (const auto& brush : entity.solids) collect(brush, {entity.id, brush.id});
	}
	m_drag = Drag::Sculpt;
	m_flattenPoint = hit->point;
	++m_sculptSeed;
	m_brushCursor = std::pair{hit->point, hit->normal};
	sculptDab(hit->point);
	return true;
}

void Viewport::displaceMouseMove(const QPointF& pos) {
	updateBrushCursor(pos);
	if (!m_brushCursor) return;
	// A dab every fifth of the radius: the result does not depend on how fast the mouse moves.
	const Vec3 point = m_brushCursor->first;
	const double spacing = std::max(1.0, m_editor->displaceSettings().radius * 0.2);
	const double travelled = length(point - m_lastDab);
	if (travelled < spacing) return;
	const int steps = std::min(16, static_cast<int>(travelled / spacing));
	const Vec3 from = m_lastDab;
	for (int i = 1; i <= steps; ++i) sculptDab(from + (point - from) * (double(i) / steps));
}

void Viewport::sculptDab(const Vec3& point) {
	m_lastDab = point;
	const auto& settings = m_editor->displaceSettings();
	SculptBrush brush;
	brush.center = point;
	brush.radius = settings.radius;
	brush.strength = dabStrength(settings.mode, settings.strength);
	brush.invert = (m_pressModifiers & Qt::ShiftModifier) || (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
	brush.seed = m_sculptSeed * 7919u + static_cast<unsigned>(std::llround(point.x) * 31 + std::llround(point.y));
	const SculptMode mode = sculptMode(settings.mode);
	bool any = false;
	makeCurrent();
	for (auto& item : m_sculptFaces) {
		const Vec3 n = normalize(item.face.normal);
		// Quick check: the brush center, laid onto the face plane, near the face at all?
		Bounds reach;
		for (const auto& v : item.face.vertices) reach.add(v);
		const Vec3 onPlane = point - n * dot(point - item.face.vertices.front(), n);
		const Vec3 margin{settings.radius, settings.radius, settings.radius};
		if (onPlane.x < reach.min.x - margin.x || onPlane.y < reach.min.y - margin.y || onPlane.z < reach.min.z - margin.z ||
		    onPlane.x > reach.max.x + margin.x || onPlane.y > reach.max.y + margin.y || onPlane.z > reach.max.z + margin.z) {
			continue;
		}
		brush.flattenHeight = dot(m_flattenPoint - item.face.vertices.front(), n);
		if (!sculpt(item.displacement, item.face, mode, brush)) continue;
		item.changed = true;
		any = true;
		DisplacementMesh mesh;
		mesh.size = item.displacement.size();
		mesh.positions = displacementPositions(item.displacement, item.face);
		mesh.alpha.assign(item.displacement.alphas.begin(), item.displacement.alphas.end());
		if (!m_renderer.updateDisplacement(item.ref.sideId, item.side, item.face, mesh)) {
			m_needsRebuild = true;   // drawn differently than expected: the stroke's end rebuilds anyway
		}
	}
	doneCurrent();
	if (any) requestFrame();
}

void Viewport::finishSculpt() {
	m_drag = Drag::None;
	std::vector<SculptFace> faces;
	faces.swap(m_sculptFaces);
	const bool alpha = m_editor->displaceSettings().mode == Editor::DisplaceMode::Alpha;
	Transaction t(m_editor->doc(), alpha ? "Paint blend" : "Sculpt");
	for (const auto& item : faces) {
		if (!item.changed) continue;
		Solid* solid = m_editor->doc().findSolid(item.ref.solid);
		if (!solid) continue;
		t.touchSolid(item.ref.solid);
		for (auto& side : solid->sides) {
			if (side.id == item.ref.sideId) storeDisplacement(side, item.displacement);
		}
	}
	auto change = t.finish();
	if (!change.empty()) m_editor->commit(std::move(change));
	requestFrame();
}

void Viewport::updateBrushCursor(const QPointF& pos) {
	const auto old = m_brushCursor;
	m_brushCursor.reset();
	if (!m_camera.is2D() && m_editor->displaceSettings().mode != Editor::DisplaceMode::Create) {
		if (const auto hit = m_renderer.pick(rayAt(pos), true); hit && hit->sideId != 0) {
			const Solid* solid = m_editor->doc().findSolid(hit->solid);
			const Side* side = solid ? findSide(*solid, hit->sideId) : nullptr;
			if (side && (side->dispInfo() || m_drag == Drag::Sculpt)) m_brushCursor = std::pair{hit->point, hit->normal};
		}
	}
	if (old.has_value() != m_brushCursor.has_value() || (old && length(old->first - m_brushCursor->first) > 0.01)) requestFrame();
}

void Viewport::drawBrushCursor(std::vector<LineVertex>& lines) const {
	if (!m_brushCursor) return;
	const Vec3 n = normalize(m_brushCursor->second);
	const Vec3 c = m_brushCursor->first + n * 0.5;   // just above the surface
	const Vec3 helper = std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
	const Vec3 u = normalize(cross(helper, n)), v = cross(n, u);
	const double r = m_editor->displaceSettings().radius;
	constexpr int SEGMENTS = 48;
	const auto ring = [&](double radius, const QVector4D& color) {
		for (int i = 0; i < SEGMENTS; ++i) {
			const double a = 2 * std::numbers::pi * i / SEGMENTS, b = 2 * std::numbers::pi * (i + 1) / SEGMENTS;
			lines.push_back({toQt(c + (u * std::cos(a) + v * std::sin(a)) * radius), color});
			lines.push_back({toQt(c + (u * std::cos(b) + v * std::sin(b)) * radius), color});
		}
	};
	ring(r, BRUSH_COLOR);
	ring(r * 0.5, BRUSH_INNER);   // where the falloff is half
	lines.push_back({toQt(c), BRUSH_COLOR});
	lines.push_back({toQt(c + n * std::max(8.0, r * 0.25)), BRUSH_COLOR});
}

bool Viewport::displaceWheel(QWheelEvent* event) {
	if (m_editor->tool() != Editor::Tool::Displace || !(event->modifiers() & Qt::ControlModifier)) return false;
	auto settings = m_editor->displaceSettings();
	settings.radius = std::round(settings.radius * std::pow(1.15, event->angleDelta().y() / 120.0));
	m_editor->setDisplaceSettings(settings);
	emit m_editor->message(tr("Brush radius %1").arg(m_editor->displaceSettings().radius));
	requestFrame();
	return true;
}

bool Viewport::displaceKey(QKeyEvent* event) {
	if (event->modifiers() != Qt::NoModifier || (event->key() != Qt::Key_BracketLeft && event->key() != Qt::Key_BracketRight)) return false;
	auto settings = m_editor->displaceSettings();
	settings.radius = std::round(settings.radius * (event->key() == Qt::Key_BracketRight ? 1.25 : 0.8));
	m_editor->setDisplaceSettings(settings);
	emit m_editor->message(tr("Brush radius %1").arg(m_editor->displaceSettings().radius));
	requestFrame();
	return true;
}

} // namespace sme::app
