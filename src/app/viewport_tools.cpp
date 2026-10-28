// viewport_tools.cpp - Clip, vertex and polygon tools of the map view (see viewport.h).
//
// Clip: in a 2D view drag a line, in the 3D view click three points; Tab switches which part
// stays (both, back, front), Enter cuts. Vertex: drag corners or edge middles of the selected
// brushes (Ctrl+click selects several); brushes always stay convex. Polygon: click the corners
// of the base, Enter (or double-click) makes the brush.

#include "viewport.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <sme/brush.h>
#include <sme/csg.h>

namespace sme::app {

namespace {

const QVector4D CLIP_COLOR{0.3f, 0.95f, 1.0f, 1};
const QVector4D VERTEX_COLOR{1, 1, 1, 1};
const QVector4D VERTEX_SELECTED{1.0f, 0.85f, 0.2f, 1};
const QVector4D EDGE_COLOR{0.6f, 0.65f, 0.75f, 1};

void addLine(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

void addMarker(std::vector<LineVertex>& out, const Vec3& p, double size, const QVector4D& color) {
	for (int axis = 0; axis < 3; ++axis) {
		Vec3 d;
		d[axis] = size;
		addLine(out, p - d, p + d, color);
	}
}

Vec3 dominantAxis(const QVector3D& forward) {
	const Vec3 f = fromQt(forward);
	int axis = 0;
	for (int i = 1; i < 3; ++i) {
		if (std::abs(f[i]) > std::abs(f[axis])) axis = i;
	}
	Vec3 n;
	n[axis] = 1;
	return n;
}

} // namespace

Vec3 Viewport::toolPoint(const QPointF& pos, bool keepPlane) {
	const double grid = m_editor->snapGrid();
	if (m_camera.is2D()) {
		Vec3 p = snap(viewPlanePoint(pos), grid);
		if (keepPlane && !m_polygon.empty()) {
			p[viewAxis()] = m_polygon.front()[viewAxis()];
		}
		return p;
	}
	const Ray ray = rayAt(pos);
	if (keepPlane) {
		if (const auto p = rayPlane(ray, {0, 0, m_polygonPlane}, {0, 0, 1})) {
			Vec3 q = snap(*p, grid);
			q.z = m_polygonPlane;
			return q;
		}
	}
	if (const auto hit = m_renderer.pick(ray)) {
		return snap(hit->point, grid);
	}
	if (const auto p = rayPlane(ray, {0, 0, 0}, {0, 0, 1})) {
		return snap(*p, grid);
	}
	return snap(fromQt(m_camera.position + m_camera.forward() * 256), grid);
}

std::optional<Plane> Viewport::clipPlane() const {
	Vec3 a, b, c;
	if (m_clipPoints.size() == 2) {
		// 2D: the plane stands along the view direction through the line.
		a = m_clipPoints[0];
		b = m_clipPoints[1];
		Vec3 depth;
		depth[viewAxis()] = 64;
		c = a + depth;
	} else if (m_clipPoints.size() == 3) {
		a = m_clipPoints[0];
		b = m_clipPoints[1];
		c = m_clipPoints[2];
	} else {
		return std::nullopt;
	}
	Vec3 normal = cross(b - a, c - a);
	if (length(normal) < 1e-6) {
		return std::nullopt;
	}
	normal = normalize(normal);
	return Plane{normal, dot(normal, a)};
}

void Viewport::updateClipPreview() {
	const auto plane = clipPlane();
	if (!plane) {
		setPreviewSolids({});
		requestFrame();
		return;
	}
	// The parts that stay, as they will be (the scratch document only hands out ids).
	MapDocument scratch;
	std::vector<Solid> kept;
	for (const auto& ref : m_editor->selection().solids) {
		const auto* solid = m_editor->doc().findSolid(ref);
		if (!solid) continue;
		auto [front, back] = splitSolid(scratch, *solid, *plane, solid->sides.front());
		if (m_clipKeep != edit::ClipKeep::Front && back) kept.push_back(std::move(*back));
		if (m_clipKeep != edit::ClipKeep::Back && front) kept.push_back(std::move(*front));
	}
	setPreviewSolids(kept);
	requestFrame();
}

void Viewport::applyClip() {
	const auto plane = clipPlane();
	if (!plane || m_editor->selection().solids.empty()) {
		emit m_editor->message(tr("Clip: select brushes, then drag a line (2D view) or click three points (3D view)"));
		return;
	}
	m_editor->clipSelection(*plane, m_clipKeep);
	m_clipPoints.clear();
	setPreviewSolids({});
	requestFrame();
}

void Viewport::resetToolState() {
	if (m_drag == Drag::Sculpt) finishSculpt();
	if (m_editor->tool() != Editor::Tool::Displace) m_brushCursor.reset();
	const bool hadPreview = !m_clipPoints.empty() || m_drag == Drag::Vertex;
	m_clipPoints.clear();
	m_vertexSelection.clear();
	m_editor->setVertexCorners(m_vertexSelection);
	m_polygon.clear();
	if (m_drag == Drag::ClipLine || m_drag == Drag::Vertex) {
		m_drag = Drag::None;
	}
	if (hadPreview) {
		setPreviewSolids({});
	}
	requestFrame();
}

std::vector<Viewport::VertexHandle> Viewport::vertexHandles() const {
	std::vector<VertexHandle> handles;
	const auto& doc = m_editor->doc();
	const auto addSolid = [&](const Solid& solid) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& corner : solidCorners(solid)) {
			if (std::none_of(handles.begin(), handles.end(), [&](const VertexHandle& h) { return !h.edge && length(h.position - corner) < 0.01; })) {
				handles.push_back({corner, {corner}, false});
			}
		}
		for (const auto& face : geometry.faces) {
			for (std::size_t i = 0; i < face.vertices.size(); ++i) {
				const Vec3 a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
				const Vec3 mid = (a + b) * 0.5;
				if (std::none_of(handles.begin(), handles.end(), [&](const VertexHandle& h) { return h.edge && length(h.position - mid) < 0.01; })) {
					handles.push_back({mid, {a, b}, true});
				}
			}
		}
	};
	for (const auto& ref : m_editor->selection().solids) {
		if (const auto* solid = doc.findSolid(ref)) addSolid(*solid);
	}
	for (const int id : m_editor->selection().entities) {
		if (const auto* entity = doc.findEntity(id); entity && id != 0) {
			for (const auto& solid : entity->solids) addSolid(solid);
		}
	}
	return handles;
}

int Viewport::hitVertexHandle(const QPointF& pos) const {
	const auto handles = vertexHandles();
	int best = -1;
	double bestDistance = 9;
	for (std::size_t i = 0; i < handles.size(); ++i) {
		QPointF p;
		if (!m_camera.project(handles[i].position, width(), height(), p)) continue;
		// Corners win over edge middles at the same spot.
		const double d = QLineF(p, pos).length() + (handles[i].edge ? 2 : 0);
		if (d < bestDistance) {
			bestDistance = d;
			best = static_cast<int>(i);
		}
	}
	return best;
}

void Viewport::updateVertexDrag(const QPointF& pos) {
	Vec3 delta;
	if (m_camera.is2D()) {
		delta = viewPlanePoint(pos) - viewPlanePoint(m_pressPos);
		delta[viewAxis()] = 0;
	} else if (const auto p = rayPlane(rayAt(pos), m_vertexGrab, m_vertexNormal)) {
		delta = *p - m_vertexGrab;
	}
	delta = snap(delta, m_editor->snapGrid());
	if (delta == m_vertexDelta) {
		return;
	}
	m_vertexDelta = delta;
	MapDocument scratch;
	std::vector<Solid> moved;
	for (const auto& ref : m_editor->selection().solids) {
		if (const auto* solid = m_editor->doc().findSolid(ref)) {
			if (auto result = moveVertices(scratch, *solid, m_vertexSelection, delta)) {
				moved.push_back(std::move(result->solid));
			} else {
				moved.push_back(*solid);
			}
		}
	}
	setPreviewSolids(moved);
	requestFrame();
}

void Viewport::finishPolygon() {
	if (m_polygon.size() < 3) {
		emit m_editor->message(tr("Polygon: click at least three corners of the base first"));
		return;
	}
	Vec3 extrude;
	if (m_camera.is2D()) {
		// 2D: as deep as the last block, like the block tool.
		const int axis = viewAxis();
		std::vector<Vec3> base = m_polygon;
		for (auto& p : base) p[axis] = m_lastBlockDepth.min[axis];
		extrude[axis] = std::max(1.0, m_lastBlockDepth.max[axis] - m_lastBlockDepth.min[axis]);
		m_editor->createPolygonBrush(base, extrude);
	} else {
		extrude.z = std::abs(m_blockHeight) > 0 ? std::abs(m_blockHeight) : 64;
		m_editor->createPolygonBrush(m_polygon, extrude);
	}
	m_polygon.clear();
	requestFrame();
}

bool Viewport::toolMousePress(QMouseEvent* event) {
	if (m_editor->tool() == Editor::Tool::Displace && displaceMousePress(event)) {
		return true;
	}
	const auto tool = m_editor->tool();
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	const QPointF pos = event->position();
	if (tool == Editor::Tool::Clip) {
		if (m_camera.is2D()) {
			const Vec3 p = toolPoint(pos, false);
			m_clipPoints = {p, p};
			m_drag = Drag::ClipLine;
		} else {
			if (m_clipPoints.size() >= 3) m_clipPoints.clear();
			m_clipPoints.push_back(toolPoint(pos, false));
			updateClipPreview();
		}
		requestFrame();
		return true;
	}
	if (tool == Editor::Tool::Vertex) {
		const int hit = hitVertexHandle(pos);
		if (hit < 0) {
			return false;   // select brushes as usual
		}
		const auto handles = vertexHandles();
		auto handle = handles[static_cast<std::size_t>(hit)];
		if (m_camera.is2D()) {
			// Corners on top of each other in a 2D view are taken together (like Hammer).
			QPointF at;
			(void)m_camera.project(handle.position, width(), height(), at);
			for (const auto& other : handles) {
				QPointF q;
				if (other.edge == handle.edge && m_camera.project(other.position, width(), height(), q) && QLineF(q, at).length() < 1.5) {
					for (const auto& c : other.corners) {
						if (std::none_of(handle.corners.begin(), handle.corners.end(), [&](const Vec3& k) { return length(k - c) < 0.01; })) {
							handle.corners.push_back(c);
						}
					}
				}
			}
		}
		const auto selected = [&](const Vec3& c) {
			return std::any_of(m_vertexSelection.begin(), m_vertexSelection.end(), [&](const Vec3& s) { return length(s - c) < 0.01; });
		};
		const bool all = std::all_of(handle.corners.begin(), handle.corners.end(), selected);
		if (event->modifiers() & Qt::ControlModifier) {
			if (all) {
				std::erase_if(m_vertexSelection, [&](const Vec3& s) {
					return std::any_of(handle.corners.begin(), handle.corners.end(), [&](const Vec3& c) { return length(s - c) < 0.01; });
				});
			} else {
				for (const auto& c : handle.corners) if (!selected(c)) m_vertexSelection.push_back(c);
			}
			m_editor->setVertexCorners(m_vertexSelection);
			requestFrame();
			return true;
		}
		if (!all) {
			m_vertexSelection = handle.corners;
			m_editor->setVertexCorners(m_vertexSelection);
		}
		m_drag = Drag::Vertex;
		m_vertexGrab = handle.position;
		m_vertexNormal = m_camera.is2D() ? Vec3{} : dominantAxis(m_camera.forward());
		m_vertexDelta = {};
		updateVertexDrag(pos);
		return true;
	}
	if (tool == Editor::Tool::Polygon) {
		if (m_polygon.empty() && !m_camera.is2D()) {
			const auto hit = m_renderer.pick(rayAt(pos));
			m_polygonPlane = (hit && hit->normal.z > 0.7) ? snap(hit->point.z, m_editor->snapGrid()) : 0.0;
		}
		const Vec3 p = toolPoint(pos, true);
		if (m_polygon.empty() || length(p - m_polygon.back()) > 0.01) {
			m_polygon.push_back(p);
		}
		requestFrame();
		return true;
	}
	return false;
}

bool Viewport::toolKey(QKeyEvent* event) {
	const auto tool = m_editor->tool();
	if (tool == Editor::Tool::Displace && displaceKey(event)) {
		return true;
	}
	const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
	if (tool == Editor::Tool::Clip) {
		if (enter) {
			applyClip();
			return true;
		}
		if (event->key() == Qt::Key_Tab) {
			m_clipKeep = m_clipKeep == edit::ClipKeep::Both ? edit::ClipKeep::Back : (m_clipKeep == edit::ClipKeep::Back ? edit::ClipKeep::Front : edit::ClipKeep::Both);
			updateClipPreview();
			return true;
		}
		if (event->key() == Qt::Key_Escape && !m_clipPoints.empty()) {
			resetToolState();
			return true;
		}
	}
	if (tool == Editor::Tool::Polygon) {
		if (enter) {
			finishPolygon();
			return true;
		}
		if (event->key() == Qt::Key_Backspace && !m_polygon.empty()) {
			m_polygon.pop_back();
			requestFrame();
			return true;
		}
		if (event->key() == Qt::Key_Escape && !m_polygon.empty()) {
			resetToolState();
			return true;
		}
	}
	if (tool == Editor::Tool::Vertex && event->key() == Qt::Key_Escape && !m_vertexSelection.empty()) {
		m_vertexSelection.clear();
		m_editor->setVertexCorners(m_vertexSelection);
		requestFrame();
		return true;
	}
	return false;
}

void Viewport::drawToolLines(std::vector<LineVertex>& lines) const {
	const auto tool = m_editor->tool();
	const double marker = m_camera.is2D() ? 4.0 / m_camera.zoom : std::max(1.0, gizmoSize() * 0.025);
	if (tool == Editor::Tool::Clip) {
		for (const auto& p : m_clipPoints) {
			addMarker(lines, p, marker * 1.5, CLIP_COLOR);
		}
		for (std::size_t i = 0; i + 1 < m_clipPoints.size(); ++i) {
			addLine(lines, m_clipPoints[i], m_clipPoints[i + 1], CLIP_COLOR);
		}
		if (const auto plane = clipPlane()) {
			// Outline of the cutting plane over the selection.
			auto bounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
			if (bounds.empty()) bounds.add(m_clipPoints.front());
			const double half = std::max(64.0, length(bounds.size()) * 0.6);
			const Vec3 n = plane->normal;
			const Vec3 helper = std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
			const Vec3 u = normalize(cross(helper, n)), v = cross(n, u);
			const Vec3 c = bounds.center() - n * (dot(n, bounds.center()) - plane->dist);
			const Vec3 q[4] = {c + (u + v) * half, c + (u - v) * half, c - (u + v) * half, c - (u - v) * half};
			for (int i = 0; i < 4; ++i) addLine(lines, q[i], q[(i + 1) % 4], CLIP_COLOR * 0.8f);
			addLine(lines, c, c + n * (half * 0.25), CLIP_COLOR);   // front side
		}
	} else if (tool == Editor::Tool::Vertex) {
		for (const auto& handle : vertexHandles()) {
			const bool selected = std::all_of(handle.corners.begin(), handle.corners.end(), [&](const Vec3& c) {
				return std::any_of(m_vertexSelection.begin(), m_vertexSelection.end(), [&](const Vec3& s) { return length(s - c) < 0.01; });
			});
			Vec3 at = handle.position;
			if (m_drag == Drag::Vertex && selected) at += m_vertexDelta;
			const double size = handle.edge ? marker * 0.6 : marker;
			Bounds b;
			b.add(at - Vec3{size, size, size});
			b.add(at + Vec3{size, size, size});
			const QVector4D color = selected ? VERTEX_SELECTED : (handle.edge ? EDGE_COLOR : VERTEX_COLOR);
			const Vec3 lo = b.min, hi = b.max;
			const Vec3 k[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
			                   {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
			const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (const auto& edge : e) addLine(lines, k[edge[0]], k[edge[1]], color);
			// A picked edge shows along its whole length (like Source 2's edge selection).
			if (handle.edge && selected && handle.corners.size() == 2) {
				const Vec3 shift = m_drag == Drag::Vertex ? m_vertexDelta : Vec3{};
				addLine(lines, handle.corners[0] + shift, handle.corners[1] + shift, VERTEX_SELECTED);
			}
		}
	} else if (tool == Editor::Tool::Displace) {
		drawBrushCursor(lines);
	} else if (tool == Editor::Tool::Polygon) {
		for (std::size_t i = 0; i < m_polygon.size(); ++i) {
			addMarker(lines, m_polygon[i], marker * 1.5, VERTEX_SELECTED);
			if (i + 1 < m_polygon.size()) addLine(lines, m_polygon[i], m_polygon[i + 1], VERTEX_SELECTED);
		}
		if (m_polygon.size() >= 3) {
			addLine(lines, m_polygon.back(), m_polygon.front(), VERTEX_SELECTED * 0.6f);
		}
	}
}

} // namespace sme::app
