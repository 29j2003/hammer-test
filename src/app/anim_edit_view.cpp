// anim_edit_view.cpp - The animation editor's 3D view (see anim_edit_view.h).

#include "anim_edit_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace sme::app {

namespace {

constexpr float GIZMO_PIXELS = 70;     // ring radius on screen
constexpr float PICK_PIXELS = 7;
constexpr int RING_POINTS = 64;

const QColor AXIS_COLORS[4] = {QColor(232, 72, 72), QColor(96, 200, 80), QColor(80, 140, 240), QColor(210, 210, 215)};

/// The vertical field of view of the game's viewmodel: viewmodel_fov 54 is horizontal at 4:3
/// (view.cpp; wider screens keep the vertical angle).
float viewmodelVerticalFov() {
	const double half = 54.0 * 0.5 * M_PI / 180.0;
	return float(2.0 * std::atan(std::tan(half) * 3.0 / 4.0) * 180.0 / M_PI);
}

double segmentDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
	const QPointF ab = b - a;
	const double len2 = QPointF::dotProduct(ab, ab);
	const double t = len2 > 1e-9 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
	const QPointF q = a + ab * t;
	return std::hypot(p.x() - q.x(), p.y() - q.y());
}

} // namespace

AnimEditView::AnimEditView(MaterialCache* materials, QWidget* parent) : ModelViewer(materials, parent) {
	setMouseTracking(true);
	setToolTip(tr("Drag an orange grip to place a hand or foot - the arm or leg follows.\n"
	              "Click a bone to choose it (Ctrl: add more) - drag a ring to turn it, an arrow to move it.\n"
	              "Left drag elsewhere turns the view, right drag moves it, the wheel zooms, F frames the chosen bone."));
}

void AnimEditView::setSkeleton(const std::vector<StudioBone>* bones, std::vector<BoneMatrix> placement) {
	m_bones = bones;
	m_placement = std::move(placement);
	update();
}

void AnimEditView::setSelection(std::vector<int> bones, int active) {
	m_selection = std::move(bones);
	m_active = active;
	update();
}

void AnimEditView::setKeyed(std::vector<bool> keyed) {
	m_keyed = std::move(keyed);
	update();
}

void AnimEditView::setGhosts(std::vector<std::vector<BoneMatrix>> ghosts) {
	m_ghosts = std::move(ghosts);
	update();
}

void AnimEditView::setGizmo(Gizmo gizmo) {
	m_gizmo = gizmo;
	update();
}

void AnimEditView::setCameraMode(CameraMode mode) {
	m_cameraMode = mode;
	update();
}

void AnimEditView::setSkeletonVisible(bool on) {
	m_showSkeleton = on;
	update();
}

void AnimEditView::setGrips(std::vector<Grip> grips) {
	m_grips = std::move(grips);
	update();
}

void AnimEditView::setMotionPath(std::vector<AnimVec> points, std::vector<bool> keyed, int current) {
	m_path = std::move(points);
	m_pathKeyed = std::move(keyed);
	m_pathCurrent = current;
	update();
}

void AnimEditView::setPreset(Preset preset) {
	// The model looks along +X with Z up (Source): "Front" looks at its face from +X.
	switch (preset) {
	case Preset::Front: setOrbit(0, 0); break;
	case Preset::Side: setOrbit(90, 0); break;
	case Preset::Top: setOrbit(0, 89); break;
	case Preset::Perspective: setOrbit(35, 18); break;
	}
}

void AnimEditView::focusBone(int bone) {
	if (bone < 0 || bone >= int(m_placement.size())) {
		frameModel();
		return;
	}
	focusOn(bonePosition(bone), 12);
}

int AnimEditView::pickGrip(const QPointF& at) const {
	const QMatrix4x4 viewProj = viewProjection();
	int best = -1;
	double bestDistance = 11;
	for (const auto& grip : m_grips) {
		QPointF s;
		if (grip.bone < 0 || !project(viewProj, bonePosition(grip.bone), s)) continue;
		const double d = std::max(std::fabs(s.x() - at.x()), std::fabs(s.y() - at.y()));
		if (d < bestDistance) {
			bestDistance = d;
			best = grip.bone;
		}
	}
	return best;
}

ModelViewer::Camera AnimEditView::camera() const {
	if (m_cameraMode == CameraMode::Free) return ModelViewer::camera();
	// The player's eye is the viewmodel's origin; the model looks along +X with Z up.
	Camera view;
	view.eye = {0, 0, 0};
	view.center = {1, 0, 0};
	view.up = {0, 0, 1};
	view.fov = viewmodelVerticalFov();
	view.nearPlane = 0.5f;
	view.farPlane = 4000;
	return view;
}

bool AnimEditView::project(const QMatrix4x4& viewProj, const QVector3D& p, QPointF& out) const {
	const QVector4D clip = viewProj * QVector4D(p, 1.0f);
	if (clip.w() <= 1e-4f) return false;
	const float x = clip.x() / clip.w(), y = clip.y() / clip.w();
	out = QPointF((x * 0.5 + 0.5) * width(), (0.5 - y * 0.5) * height());
	return true;
}

QVector3D AnimEditView::bonePosition(int bone) const {
	if (bone < 0 || bone >= int(m_placement.size())) return {};
	const auto& m = m_placement[std::size_t(bone)].m;
	return {m[0][3], m[1][3], m[2][3]};
}

QVector3D AnimEditView::boneAxis(int bone, int axis) const {
	if (bone < 0 || bone >= int(m_placement.size()) || axis < 0 || axis > 2) return {};
	const auto& m = m_placement[std::size_t(bone)].m;
	return QVector3D(m[0][axis], m[1][axis], m[2][axis]).normalized();
}

QVector3D AnimEditView::handleAxis(int axis) const {
	if (axis == 3) return (camera().eye - bonePosition(m_active)).normalized();
	if (m_gizmo == Gizmo::Rotate) return boneAxis(m_active, axis);
	return QVector3D(axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f);
}

float AnimEditView::gizmoRadius() const {
	const Camera view = camera();
	const QVector3D c = bonePosition(m_active);
	const QVector3D forward = (view.center - view.eye).normalized();
	const float depth = std::max(0.5f, QVector3D::dotProduct(c - view.eye, forward));
	const float worldPerPixel = 2.f * depth * std::tan(qDegreesToRadians(view.fov * 0.5f)) / float(std::max(1, height()));
	return GIZMO_PIXELS * worldPerPixel;
}

std::vector<QPointF> AnimEditView::handlePoints(int axis, std::vector<bool>* front) const {
	std::vector<QPointF> out;
	if (m_active < 0 || m_active >= int(m_placement.size())) return out;
	const QMatrix4x4 viewProj = viewProjection();
	const QVector3D c = bonePosition(m_active);
	const float r = gizmoRadius();
	const QVector3D a = handleAxis(axis);
	if (a.isNull()) return out;
	if (m_gizmo == Gizmo::Move && axis < 3) {
		QPointF p0, p1;
		if (project(viewProj, c, p0) && project(viewProj, c + a * r, p1)) out = {p0, p1};
		return out;
	}
	if (m_gizmo == Gizmo::Move) return out;   // no view ring for moving
	// u, v across the axis with v = axis x u, so the points run the way the bone turns.
	QVector3D u = QVector3D::crossProduct(a, std::fabs(a.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0)).normalized();
	const QVector3D v = QVector3D::crossProduct(a, u).normalized();
	const float radius = axis == 3 ? r * 1.2f : r;
	const QVector3D toEye = camera().eye - c;
	for (int i = 0; i <= RING_POINTS; ++i) {
		const float t = float(i) / float(RING_POINTS) * 2.f * float(M_PI);
		const QVector3D p = c + radius * (std::cos(t) * u + std::sin(t) * v);
		QPointF s;
		if (!project(viewProj, p, s)) {
			out.clear();
			if (front) front->clear();
			return out;
		}
		out.push_back(s);
		if (front) front->push_back(axis == 3 || QVector3D::dotProduct(p - c, toEye) >= -0.05f * radius);
	}
	return out;
}

int AnimEditView::pickHandle(const QPointF& at) const {
	if (m_active < 0) return -1;
	int best = -1;
	double bestDistance = PICK_PIXELS;
	for (int axis = 0; axis < 4; ++axis) {
		std::vector<bool> front;
		const auto points = handlePoints(axis, &front);
		for (std::size_t i = 0; i + 1 < points.size(); ++i) {
			if (!front.empty() && !(front[i] && front[i + 1])) continue;   // the far half of a ring is not grabbed
			const double d = segmentDistance(at, points[i], points[i + 1]);
			if (d < bestDistance) {
				bestDistance = d;
				best = axis;
			}
		}
	}
	return best;
}

int AnimEditView::pickBone(const QPointF& at) const {
	if (!m_bones || !m_showSkeleton) return -1;
	const QMatrix4x4 viewProj = viewProjection();
	int best = -1;
	double bestDistance = 10;
	for (std::size_t b = 0; b < m_placement.size(); ++b) {
		QPointF s;
		if (!project(viewProj, bonePosition(int(b)), s)) continue;
		const double d = std::hypot(s.x() - at.x(), s.y() - at.y());
		if (d < bestDistance) {
			bestDistance = d;
			best = int(b);
		}
	}
	return best;
}

bool AnimEditView::boneOnScreen(int bone, QPointF& at) const {
	if (bone < 0 || bone >= int(m_placement.size())) return false;
	return project(viewProjection(), bonePosition(bone), at);
}

bool AnimEditView::gizmoHandle(int axis, QPointF& at) const {
	std::vector<bool> front;
	const auto points = handlePoints(axis, &front);
	if (points.empty()) return false;
	if (m_gizmo == Gizmo::Move) {
		at = points.front() * 0.3 + points.back() * 0.7;
		return true;
	}
	// The front point farthest from the gizmo's middle on screen (the ring is widest there).
	QPointF centre;
	if (!project(viewProjection(), bonePosition(m_active), centre)) return false;
	double best = -1;
	for (std::size_t i = 0; i < points.size(); ++i) {
		if (!front.empty() && !front[i]) continue;
		const double d = std::hypot(points[i].x() - centre.x(), points[i].y() - centre.y());
		if (d > best) {
			best = d;
			at = points[i];
		}
	}
	return best >= 0;
}

void AnimEditView::paintOverlay(QPainter& painter, const QMatrix4x4& viewProj) {
	if (!m_bones || m_placement.empty()) return;
	painter.setRenderHint(QPainter::Antialiasing);
	const auto& bones = *m_bones;
	std::vector<QPointF> screen(m_placement.size());
	std::vector<bool> visible(m_placement.size(), false);
	for (std::size_t b = 0; b < m_placement.size(); ++b) visible[b] = project(viewProj, bonePosition(int(b)), screen[b]);
	const auto selected = [this](int b) { return std::find(m_selection.begin(), m_selection.end(), b) != m_selection.end(); };
	if (m_showSkeleton) {
		// Onion skin: other frames, faint.
		for (const auto& ghost : m_ghosts) {
			painter.setPen(QPen(QColor(140, 200, 255, 70), 1.2));
			for (std::size_t b = 0; b < ghost.size() && b < bones.size(); ++b) {
				const int parent = bones[b].parent;
				if (parent < 0 || std::size_t(parent) >= ghost.size()) continue;
				QPointF a, c;
				const auto& m = ghost[b].m;
				const auto& pm = ghost[std::size_t(parent)].m;
				if (project(viewProj, {pm[0][3], pm[1][3], pm[2][3]}, a) && project(viewProj, {m[0][3], m[1][3], m[2][3]}, c)) painter.drawLine(a, c);
			}
		}
		for (std::size_t b = 0; b < m_placement.size() && b < bones.size(); ++b) {
			const int parent = bones[b].parent;
			if (parent < 0 || std::size_t(parent) >= m_placement.size() || !visible[b] || !visible[std::size_t(parent)]) continue;
			const bool on = selected(int(b));
			painter.setPen(QPen(on ? QColor(255, 214, 90, 230) : QColor(150, 190, 255, 180), on ? 2.2 : 1.4));
			painter.drawLine(screen[std::size_t(parent)], screen[b]);
		}
		for (std::size_t b = 0; b < m_placement.size(); ++b) {
			if (!visible[b]) continue;
			const bool on = selected(int(b));
			const bool keyed = b < m_keyed.size() && m_keyed[b];
			painter.setPen(QPen(int(b) == m_active ? QColor(255, 255, 255) : QColor(20, 24, 30), 1));
			painter.setBrush(on ? QColor(255, 214, 90) : keyed ? QColor(255, 140, 40) : QColor(150, 190, 255));
			const double r = on ? 4.5 : 3.2;
			painter.drawEllipse(screen[b], r, r);
		}
	}
	// The motion path: a line through every frame, dots on keys, the current frame white.
	if (m_path.size() > 1) {
		std::vector<QPointF> points(m_path.size());
		std::vector<bool> shown(m_path.size(), false);
		for (std::size_t i = 0; i < m_path.size(); ++i) shown[i] = project(viewProj, {m_path[i].x, m_path[i].y, m_path[i].z}, points[i]);
		painter.setPen(QPen(QColor(120, 220, 160, 200), 1.6));
		for (std::size_t i = 0; i + 1 < points.size(); ++i) {
			if (shown[i] && shown[i + 1]) painter.drawLine(points[i], points[i + 1]);
		}
		painter.setPen(Qt::NoPen);
		for (std::size_t i = 0; i < points.size(); ++i) {
			if (!shown[i]) continue;
			const bool key = i < m_pathKeyed.size() && m_pathKeyed[i];
			const bool current = int(i) == m_pathCurrent;
			painter.setBrush(current ? QColor(255, 255, 255) : key ? QColor(255, 150, 50) : QColor(120, 220, 160, 170));
			const double r = current ? 4.0 : key ? 3.6 : 1.6;
			painter.drawEllipse(points[i], r, r);
		}
	}
	// Grips on hands and feet: orange squares to drag.
	for (const auto& grip : m_grips) {
		QPointF s;
		if (grip.bone < 0 || grip.bone >= int(m_placement.size()) || !project(viewProj, bonePosition(grip.bone), s)) continue;
		const bool hot = grip.bone == m_gripDrag || grip.bone == m_active;
		painter.setPen(QPen(QColor(20, 22, 26), 1.5));
		painter.setBrush(hot ? QColor(255, 214, 90) : QColor(255, 150, 50));
		painter.drawRect(QRectF(s.x() - 6, s.y() - 6, 12, 12));
		painter.setPen(QColor(255, 200, 140));
		painter.drawText(s + QPointF(10, -8), grip.label);
	}
	// The gizmo on the active bone.
	if (m_active >= 0 && m_active < int(m_placement.size())) {
		for (int axis = 3; axis >= 0; --axis) {
			std::vector<bool> front;
			const auto points = handlePoints(axis, &front);
			if (points.size() < 2) continue;
			const bool hot = axis == m_dragHandle || (m_dragHandle < 0 && axis == m_hoverHandle);
			QColor color = AXIS_COLORS[axis];
			if (hot) color = color.lighter(135);
			if (m_gizmo == Gizmo::Move) {
				painter.setPen(QPen(color, hot ? 3.5 : 2.5));
				painter.drawLine(points[0], points[1]);
				const QPointF d = points[1] - points[0];
				const double len = std::hypot(d.x(), d.y());
				if (len > 4) {
					const QPointF dir = d / len, side(-dir.y(), dir.x());
					QPolygonF head;
					head << points[1] + dir * 9 << points[1] + side * 5 << points[1] - side * 5;
					painter.setBrush(color);
					painter.drawPolygon(head);
				}
				continue;
			}
			for (std::size_t i = 0; i + 1 < points.size(); ++i) {
				const bool near = front.empty() || (front[i] && front[i + 1]);
				QColor c = color;
				if (!near) c.setAlpha(60);
				painter.setPen(QPen(c, near ? (hot ? 3.2 : 2.2) : 1.0));
				painter.drawLine(points[i], points[i + 1]);
			}
		}
	}
	painter.setRenderHint(QPainter::Antialiasing, false);
}

void AnimEditView::mousePressEvent(QMouseEvent* event) {
	m_pressAt = event->position();
	m_moved = false;
	m_orbiting = false;
	if (event->button() == Qt::LeftButton) {
		// A grip first: drag the hand or foot, the limb follows.
		if (const int grip = pickGrip(event->position()); grip >= 0) {
			m_gripDrag = grip;
			m_gripStart = bonePosition(grip);
			const Camera view = camera();
			const QVector3D forward = (view.center - view.eye).normalized();
			m_gripRight = QVector3D::crossProduct(forward, view.up).normalized();
			m_gripUp = QVector3D::crossProduct(m_gripRight, forward).normalized();
			const float depth = std::max(0.5f, QVector3D::dotProduct(m_gripStart - view.eye, forward));
			m_gripWorldPerPixel = 2.f * depth * std::tan(qDegreesToRadians(view.fov * 0.5f)) / float(std::max(1, height()));
			emit boneClicked(grip, false);
			emit dragStarted();
			update();
			return;
		}
		const int handle = pickHandle(event->position());
		if (handle >= 0) {
			m_dragHandle = handle;
			m_dragAxis = handleAxis(handle);
			m_dragRadius = gizmoRadius();
			const QMatrix4x4 viewProj = viewProjection();
			const QVector3D c = bonePosition(m_active);
			project(viewProj, c, m_dragCenter);
			if (m_gizmo == Gizmo::Move) {
				QPointF tip;
				project(viewProj, c + m_dragAxis * m_dragRadius, tip);
				m_dragScreenAxis = tip - m_dragCenter;
			} else {
				// The ring's direction of turning where it was grabbed (see handlePoints: the points
				// run the way the bone turns about the axis).
				std::vector<bool> front;
				const auto points = handlePoints(handle, &front);
				std::size_t nearest = 0;
				double best = 1e9;
				for (std::size_t i = 0; i + 1 < points.size(); ++i) {
					const double d = segmentDistance(event->position(), points[i], points[i + 1]);
					if (d < best) {
						best = d;
						nearest = i;
					}
				}
				m_dragScreenAxis = points.size() > 1 ? points[nearest + 1] - points[nearest] : QPointF(1, 0);
				const double len = std::hypot(m_dragScreenAxis.x(), m_dragScreenAxis.y());
				m_dragScreenAxis = len > 1e-6 ? m_dragScreenAxis / len : QPointF(1, 0);
			}
			emit dragStarted();
			update();
			return;
		}
	}
	if (m_cameraMode == CameraMode::Free) {
		m_orbiting = true;
		ModelViewer::mousePressEvent(event);
	}
}

void AnimEditView::mouseMoveEvent(QMouseEvent* event) {
	const QPointF delta = event->position() - m_pressAt;
	if (std::hypot(delta.x(), delta.y()) > 3) m_moved = true;
	if (m_gripDrag >= 0) {
		// In the plane through the grip facing the camera.
		const QVector3D target = m_gripStart + (m_gripRight * float(delta.x()) - m_gripUp * float(delta.y())) * m_gripWorldPerPixel;
		emit gripDragged(m_gripDrag, {target.x(), target.y(), target.z()});
		return;
	}
	if (m_dragHandle >= 0) {
		if (m_gizmo == Gizmo::Move) {
			const double len2 = QPointF::dotProduct(m_dragScreenAxis, m_dragScreenAxis);
			if (len2 < 4) return;   // the axis points at the camera: no way to drag along it
			const float t = float(QPointF::dotProduct(delta, m_dragScreenAxis) / len2) * m_dragRadius;
			emit moveDragged({m_dragAxis.x() * t, m_dragAxis.y() * t, m_dragAxis.z() * t});
		} else {
			// Pixels along the ring's direction, as an angle of the ring's size on screen.
			const float pixels = float(QPointF::dotProduct(delta, m_dragScreenAxis));
			const float angle = pixels / GIZMO_PIXELS;
			const float s = std::sin(angle * 0.5f);
			emit rotateDragged({m_dragAxis.x() * s, m_dragAxis.y() * s, m_dragAxis.z() * s, std::cos(angle * 0.5f)});
		}
		return;
	}
	if (m_orbiting) {
		ModelViewer::mouseMoveEvent(event);
		return;
	}
	const int hover = pickHandle(event->position());
	if (hover != m_hoverHandle) {
		m_hoverHandle = hover;
		update();
	}
}

void AnimEditView::mouseReleaseEvent(QMouseEvent* event) {
	if (m_gripDrag >= 0) {
		m_gripDrag = -1;
		emit dragFinished();
		update();
		return;
	}
	if (m_dragHandle >= 0) {
		m_dragHandle = -1;
		emit dragFinished();
		update();
		return;
	}
	if (m_orbiting) ModelViewer::mouseReleaseEvent(event);
	m_orbiting = false;
	// A click (no drag) chooses the bone under the mouse.
	if (event->button() == Qt::LeftButton && !m_moved) emit boneClicked(pickBone(event->position()), event->modifiers() & Qt::ControlModifier);
}

void AnimEditView::mouseDoubleClickEvent(QMouseEvent* event) {
	if (m_cameraMode == CameraMode::Free && pickBone(event->position()) < 0) ModelViewer::mouseDoubleClickEvent(event);
}

void AnimEditView::wheelEvent(QWheelEvent* event) {
	if (m_cameraMode == CameraMode::Free) ModelViewer::wheelEvent(event);
}

} // namespace sme::app
