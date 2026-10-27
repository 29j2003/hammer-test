// viewport.cpp - Map view input, tools and drawing (see viewport.h).

#include "viewport.h"

#include "model_cache.h"

#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QRandomGenerator>
#include <QWheelEvent>

#include <cmath>

#include <sme/brush.h>
#include <sme/csg.h>
#include <sme/map_tools.h>
#include <sme/shapes.h>
#include <sme/soundscape_area.h>
#include <sme/text.h>

namespace sme::app {

namespace {

/// Where lines and names of an entity start: its origin, else the middle of its brushes.
Vec3 entityCenter(const Entity& entity) {
	if (entity.hasOrigin() || entity.solids.empty()) {
		return entity.origin();
	}
	Bounds box;
	for (const auto& solid : entity.solids) {
		for (const auto& corner : solidCorners(solid)) box.add(corner);
	}
	return box.empty() ? entity.origin() : box.center();
}

/// Output target names may end in '*' (Source matches the start then).
bool targetMatches(std::string_view target, std::string_view name) {
	if (target.empty() || name.empty()) return false;
	if (target.back() == '*') {
		const auto prefix = target.substr(0, target.size() - 1);
		return name.size() >= prefix.size() && iequals(name.substr(0, prefix.size()), prefix);
	}
	return iequals(target, name);
}

const QVector4D AXIS_COLORS[3] = {{0.95f, 0.25f, 0.25f, 1}, {0.35f, 0.9f, 0.3f, 1}, {0.3f, 0.5f, 1.0f, 1}};
const QVector4D HOVER_COLOR{1.0f, 0.85f, 0.2f, 1};

void line(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

void boxLines(std::vector<LineVertex>& out, const Bounds& b, const QVector4D& color) {
	const Vec3 lo = b.min, hi = b.max;
	const Vec3 c[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
	                   {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
	const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	for (const auto& edge : e) {
		line(out, c[edge[0]], c[edge[1]], color);
	}
}

Vec3 axisVector(int axis) {
	Vec3 v;
	v[axis] = 1;
	return v;
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
	const QPointF ab = b - a, ap = p - a;
	const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
	const double t = len2 > 0 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / len2, 0.0, 1.0) : 0.0;
	const QPointF q = a + ab * t - p;
	return std::sqrt(q.x() * q.x() + q.y() * q.y());
}

/// Parameter along the line `origin + axis * t` closest to the ray.
double closestOnAxis(const Vec3& origin, const Vec3& axis, const Ray& ray) {
	const Vec3 w0 = origin - ray.origin;
	const double b = dot(axis, ray.direction), d = dot(axis, w0), e = dot(ray.direction, w0);
	const double denom = 1 - b * b;
	return std::abs(denom) < 1e-6 ? 0 : (b * e - d) / denom;
}

const char* viewName(Camera::Kind kind) {
	switch (kind) {
		case Camera::Kind::Top: return "Top (X/Y)";
		case Camera::Kind::Front: return "Front (Y/Z)";
		case Camera::Kind::Side: return "Side (X/Z)";
		default: return "3D";
	}
}

} // namespace

Viewport::Viewport(Editor* editor, MaterialCache* materials, Camera::Kind kind, QWidget* parent)
	: QOpenGLWidget(parent), m_editor(editor), m_materials(materials) {
	m_camera.kind = kind;
	if (kind != Camera::Kind::Perspective) {
		m_camera.position = {0, 0, 0};
	}
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAcceptDrops(true);
	setMinimumSize(160, 120);
	m_lastBlockDepth.add({0, 0, 0});
	m_lastBlockDepth.add({64, 64, 64});

	m_moveTimer.setInterval(0);   // as fast as the display allows while flying
	connect(&m_moveTimer, &QTimer::timeout, this, &Viewport::moveCamera);
	connect(editor, &Editor::documentChanged, this, [this] {
		m_needsRebuild = true;
		m_selectionDirty = true;
		update();
	});
	connect(editor, &Editor::documentReplaced, this, [this] {
		m_needsRebuild = true;
		m_leak.clear();
		QTimer::singleShot(0, this, [this] {
			if (m_glReady) {
				makeCurrent();
				m_renderer.rebuild(m_editor->doc(), m_editor->entities());
				m_needsRebuild = false;
				doneCurrent();
			}
			frameSelection();
		});
	});
	// mapbase-fidelity look (clouds, grading): read again when the map, the game or the switch changes.
	for (const auto signal : {&Editor::documentChanged, &Editor::settingsChanged, &Editor::gameChanged}) {
		connect(editor, signal, this, [this] {
			m_lookDirty = true;
			update();
		});
	}
	connect(editor, &Editor::selectionChanged, this, [this] {
		m_selectionDirty = true;
		update();
	});
	connect(editor, &Editor::settingsChanged, this, qOverload<>(&QWidget::update));
	connect(editor, &Editor::toolChanged, this, &Viewport::resetToolState);
	connect(editor, &Editor::selectionChanged, this, [this] {
		// The clip preview and the selected corners belong to the old selection.
		if (m_drag != Drag::Vertex) {
			m_vertexSelection.clear();
			m_editor->setVertexCorners(m_vertexSelection);
		}
		if (!m_clipPoints.empty() && m_drag != Drag::ClipLine) {
			updateClipPreview();
		}
	});
	connect(editor, &Editor::vertexCornersChanged, this, [this] {
		// Picked in another view, or used up by an edge action.
		if (m_drag != Drag::Vertex) {
			m_vertexSelection = m_editor->vertexCorners();
			update();
		}
	});
	connect(editor, &Editor::gameChanged, this, [this] {
		m_needsRebuild = true;
		update();
	});
	connect(editor->models(), &ModelCache::modelReady, this, [this] {
		// Several models finish between two frames: the next paint rebuilds once.
		m_needsRebuild = true;
		update();
	});
	connect(materials, &MaterialCache::materialReady, this, [this](const QString& name) {
		m_renderer.materialReady(name);
		update();
	});
	m_fpsClock.start();

	// View menu in the corner (like Hammer's "3D Shaded Textured" box): view kind, render mode, shadows.
	m_viewButton = new QToolButton(this);
	m_viewButton->setObjectName("viewMenu");
	m_viewButton->setPopupMode(QToolButton::InstantPopup);
	m_viewButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_viewButton->setCursor(Qt::ArrowCursor);
	m_viewButton->setFocusPolicy(Qt::NoFocus);
	m_viewButton->setStyleSheet("QToolButton { background: rgba(28, 30, 36, 210); color: #d6dae2; border: 1px solid rgba(255, 255, 255, 35);"
	                            " border-radius: 3px; padding: 2px 8px; }"
	                            "QToolButton:hover { border-color: rgba(255, 160, 40, 180); }"
	                            "QToolButton::menu-indicator { image: none; width: 0; }");
	m_viewButton->move(6, 5);
	auto* menu = new QMenu(m_viewButton);
	m_viewButton->setMenu(menu);
	connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillViewMenu(menu); });
	connect(editor, &Editor::settingsChanged, this, &Viewport::updateViewButton);
	connect(editor, &Editor::viewFilterChanged, this, &Viewport::updateViewButton);
	updateViewButton();
}

void Viewport::updateViewButton() {
	QString text = QString::fromLatin1(viewName(m_camera.kind));
	if (!m_camera.is2D()) {
		switch (m_editor->renderMode()) {
			case Editor::RenderMode::Lit: text += tr("  ·  Lighting preview") + (m_editor->shadows() ? tr(" + shadows") : QString()); break;
			case Editor::RenderMode::Unlit: text += tr("  ·  Textured (unlit)"); break;
			case Editor::RenderMode::Wireframe: text += tr("  ·  Wireframe"); break;
		}
	}
	if (const int hiddenKinds = std::popcount(m_editor->hiddenKinds()); hiddenKinds > 0) {
		text += tr("  ·  %n kind(s) hidden", nullptr, hiddenKinds);   // so nobody wonders where things went
	}
	m_viewButton->setText(text + QStringLiteral("  ▾"));
	m_viewButton->adjustSize();
}

void Viewport::fillViewMenu(QMenu* menu) {
	menu->clear();
	menu->addSection(tr("View"));
	for (const auto& [kind, name, key] : {std::tuple{Camera::Kind::Perspective, tr("3D"), QStringLiteral("Ctrl+F1")},
	                                      std::tuple{Camera::Kind::Top, tr("Top (X/Y)"), QStringLiteral("F2")},
	                                      std::tuple{Camera::Kind::Front, tr("Front (Y/Z)"), QStringLiteral("F3")},
	                                      std::tuple{Camera::Kind::Side, tr("Side (X/Z)"), QStringLiteral("F4")}}) {
		auto* action = menu->addAction(QStringLiteral("%1\t%2").arg(name, key));
		action->setCheckable(true);
		action->setChecked(m_camera.kind == kind);
		connect(action, &QAction::triggered, this, [this, kind] {
			setCameraKind(kind);
			updateViewButton();
		});
	}
	menu->addSection(tr("Look (3D)"));
	for (const auto& [mode, name, key] : {std::tuple{Editor::RenderMode::Lit, tr("Lighting preview"), QStringLiteral("F6")},
	                                      std::tuple{Editor::RenderMode::Unlit, tr("Textured (unlit)"), QStringLiteral("F5")},
	                                      std::tuple{Editor::RenderMode::Wireframe, tr("Wireframe"), QStringLiteral("F8")}}) {
		auto* action = menu->addAction(QStringLiteral("%1\t%2").arg(name, key));
		action->setCheckable(true);
		action->setChecked(m_editor->renderMode() == mode);
		connect(action, &QAction::triggered, m_editor, [this, mode] { m_editor->setRenderMode(mode); });
	}
	auto* shadows = menu->addAction(tr("Real-time shadows\tF7"));
	shadows->setCheckable(true);
	shadows->setChecked(m_editor->shadows());
	shadows->setEnabled(m_editor->renderMode() == Editor::RenderMode::Lit);
	shadows->setToolTip(tr("Sun, nearest spot and point lights cast shadows in the lighting preview"));
	connect(shadows, &QAction::toggled, m_editor, &Editor::setShadows);
	auto* look = menu->addAction(tr("mapbase-fidelity look (clouds, colour grading)"));
	look->setCheckable(true);
	look->setChecked(m_editor->fidelityLook());
	look->setObjectName("fidelityLook");
	look->setToolTip(m_editor->hasFidelity() ? tr("As the mod's graphics add-on shows the map in the game (Map > Map look sets the values)")
	                                         : tr("This mod does not have mapbase-fidelity - try its look anyway"));
	connect(look, &QAction::toggled, m_editor, &Editor::setFidelityLook);
	fillShowMenu(menu, m_editor);
}

void Viewport::updateLook() {
	SceneRenderer::SkyLook sky;
	SceneRenderer::Grade grade;
	if (m_editor->fidelityLook()) {
		const auto value = [this](const char* cvar) { return float(m_editor->lookValue(cvar)); };
		sky.clouds = value("r_fidelity_sky_clouds");
		sky.coverage = value("r_fidelity_sky_clouds_coverage");
		sky.darkness = value("r_fidelity_sky_clouds_darkness");
		sky.scale = value("r_fidelity_sky_clouds_scale");
		grade.on = true;
		grade.exposure = value("r_fidelity_exposure");
		grade.contrast = value("r_fidelity_contrast");
		grade.saturation = value("r_fidelity_saturation");
		grade.vibrance = value("r_fidelity_vibrance");
		grade.temperature = value("r_fidelity_temperature");
		grade.tint = value("r_fidelity_tint");
		grade.lift = value("r_fidelity_lift");
		grade.gamma = value("r_fidelity_gamma");
		grade.gain = value("r_fidelity_gain");
		grade.vignette = value("r_fidelity_vignette");
	}
	m_renderer.setSkyLook(sky);
	m_renderer.setGrade(grade);
}

void Viewport::fillShowMenu(QMenu* menu, Editor* editor) {
	menu->addSection(tr("Show"));
	const std::pair<ObjectKind, QString> kinds[] = {
		{ObjectKind::World, tr("World brushes")},
		{ObjectKind::BrushEntities, tr("Brush entities (doors, func_detail ...)")},
		{ObjectKind::Triggers, tr("Triggers")},
		{ObjectKind::ToolBrushes, tr("Tool brushes and faces (clip, nodraw, hint ...)")},
		{ObjectKind::Sky, tr("Sky brushes (toolsskybox)")},
		{ObjectKind::Models, tr("Models (props)")},
		{ObjectKind::Npcs, tr("NPCs")},
		{ObjectKind::Lights, tr("Lights")},
		{ObjectKind::Cubemaps, tr("Cubemaps and their room boxes")},
		{ObjectKind::Helpers, tr("Other entities (logic, info, paths, sounds ...)")},
	};
	for (const auto& [kind, name] : kinds) {
		auto* action = menu->addAction(name);
		action->setCheckable(true);
		action->setChecked(editor->kindVisible(kind));
		action->setObjectName(QStringLiteral("show%1").arg(static_cast<std::uint32_t>(kind)));
		connect(action, &QAction::toggled, editor, [editor, kind](bool on) { editor->setKindVisible(kind, on); });
	}
	auto* all = menu->addAction(tr("Show everything"));
	all->setEnabled(editor->hiddenKinds() != 0);
	connect(all, &QAction::triggered, editor, [editor] {
		for (std::uint32_t bit = 1; bit <= ALL_OBJECT_KINDS; bit <<= 1) editor->setKindVisible(static_cast<ObjectKind>(bit), true);
	});
}

Viewport::~Viewport() {
	makeCurrent();
	m_renderer.release();
	doneCurrent();
}

void Viewport::setCameraKind(Camera::Kind kind) {
	if (m_camera.kind == kind) {
		return;
	}
	const bool was2D = m_camera.is2D();
	m_camera.kind = kind;
	if (was2D != m_camera.is2D()) {
		frameSelection();
	}
	updateViewButton();
	update();
}

void Viewport::initializeGL() {
	QString error;
	m_glReady = m_renderer.initialize(m_materials, &error);
	m_renderer.setModels(m_editor->models());
	// func_instance content comes from the editor's instance cache.
	m_renderer.setInstanceProvider([this](const Entity& entity) { return m_editor->instancePreview(entity); });
	if (!m_glReady) {
		m_glError = error;
		emit statsChanged(tr("OpenGL error: %1").arg(error));
	}
	m_needsRebuild = true;
}

void Viewport::resizeGL(int, int) {}

Ray Viewport::rayAt(const QPointF& pos) const {
	return m_camera.rayAt(pos.x(), pos.y(), width(), height());
}

int Viewport::viewAxis() const {
	switch (m_camera.kind) {
		case Camera::Kind::Top: return 2;
		case Camera::Kind::Front: return 0;
		case Camera::Kind::Side: return 1;
		default: return 2;
	}
}

Vec3 Viewport::viewPlanePoint(const QPointF& pos) const {
	const Ray ray = rayAt(pos);
	Vec3 p = ray.origin;
	p[viewAxis()] = 0;
	return p;
}

std::optional<Vec3> Viewport::rayPlane(const Ray& ray, const Vec3& point, const Vec3& normal) const {
	const double denom = dot(normal, ray.direction);
	if (std::abs(denom) < 1e-6) {
		return std::nullopt;
	}
	const double t = dot(point - ray.origin, normal) / denom;
	if (t < 0) {
		return std::nullopt;
	}
	return ray.origin + ray.direction * t;
}

double Viewport::gizmoSize() const {
	if (m_camera.is2D()) {
		return 70.0 / m_camera.zoom;
	}
	const auto bounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
	const double dist = length(fromQt(m_camera.position) - bounds.center());
	return std::max(8.0, dist * 0.16);
}

void Viewport::frameSelection() {
	Bounds box = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
	if (box.empty()) {
		box = m_renderer.sceneBounds();
	}
	if (box.empty()) {
		// Not drawn yet (hidden view): measure the document directly.
		for (const auto& solid : m_editor->doc().world.solids) {
			box.add(buildBrushGeometry(solid).bounds);
		}
		for (const auto& entity : m_editor->doc().entities) {
			box.add(edit::entityBounds(entity, m_editor->entities()));
		}
	}
	if (box.empty()) {
		box.add({-256, -256, 0});
		box.add({256, 256, 256});
	}
	const Vec3 center = box.center();
	const double radius = std::max(64.0, length(box.size()) * 0.5);
	if (m_camera.is2D()) {
		m_camera.position = toQt(center);
		const int a = viewAxis() == 0 ? 1 : 0, b = viewAxis() == 2 ? 1 : 2;
		const double sizeA = std::max(64.0, box.size()[a] * 1.15), sizeB = std::max(64.0, box.size()[b] * 1.15);
		m_camera.zoom = float(std::clamp(std::min(width() / sizeA, height() / sizeB), 0.005, 32.0));
	} else {
		const double dist = radius / std::tan(qDegreesToRadians(m_camera.fov) / 2) * 1.1;
		m_camera.position = toQt(center) - m_camera.forward() * float(dist);
	}
	update();
}

void Viewport::setLeakLine(const std::vector<Vec3>& points) {
	m_leak = points;
	update();
}

// --- Drawing -------------------------------------------------------------------

void Viewport::paintGL() {
	m_frameClock.start();
	auto* gl = QOpenGLContext::currentContext()->functions();
	const QColor bg = m_camera.is2D() ? QColor(24, 25, 28) : QColor(54, 58, 66);
	gl->glClearColor(bg.redF(), bg.greenF(), bg.blueF(), 1);
	gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (!m_glReady) {
		QPainter p(this);
		p.setPen(Qt::white);
		p.drawText(rect(), Qt::AlignCenter, tr("OpenGL 3.3 is required.\n%1").arg(m_glError));
		return;
	}
	if (m_needsRebuild) {
		m_renderer.rebuild(m_editor->doc(), m_editor->entities());
		m_needsRebuild = false;
		m_selectionDirty = true;
	}
	if (m_lookDirty) {
		updateLook();
		m_lookDirty = false;
	}
	if (m_selectionDirty) {
		m_renderer.setSelection(m_editor->doc(), m_editor->selection(), m_editor->hidden(), m_hoverSide);
		m_selectionDirty = false;
	}

	SceneRenderer::Params params;
	params.view = m_camera.view();
	params.projection = m_camera.projection(width(), height());
	params.cameraPosition = m_camera.position;
	params.shadows = m_editor->shadows();
	if (m_camera.is2D()) {
		params.mode = SceneRenderer::Mode::Flat2D;
	} else {
		switch (m_editor->renderMode()) {
			case Editor::RenderMode::Lit: params.mode = SceneRenderer::Mode::Lit; break;
			case Editor::RenderMode::Unlit: params.mode = SceneRenderer::Mode::Unlit; break;
			case Editor::RenderMode::Wireframe: params.mode = SceneRenderer::Mode::Wireframe; break;
		}
	}
	const QMatrix4x4 viewProj = params.projection * params.view;

	std::vector<LineVertex> lines;
	if (m_camera.is2D()) {
		drawGrid(lines);
		m_renderer.drawLines(lines, viewProj, true);
		lines.clear();
	}

	const bool dragging = m_drag == Drag::Gizmo;
	// Clip and vertex previews replace the selected brushes while they are shown.
	const bool toolPreview = m_drag == Drag::Vertex || (m_editor->tool() == Editor::Tool::Clip && clipPlane());
	m_renderer.setHideSelected(dragging || toolPreview);
	m_renderer.render(params);
	if (toolPreview) {
		m_renderer.drawPreviewSolids(params);
	}
	if (dragging && scaling()) {
		// Brushes are shown scaled with their real texture coordinates (no stretching).
		m_renderer.drawPreview(params, m_previewModel, false);
		m_renderer.drawPreviewSolids(params);
	} else if (dragging) {
		m_renderer.drawPreview(params, m_previewModel);
	}

	if (!m_camera.is2D() && m_editor->showGrid3D()) {
		drawGrid(lines);
		m_renderer.drawLines(lines, viewProj, false);
		lines.clear();
	}

	// Tool overlays.
	if (m_ghost && m_editor->tool() == Editor::Tool::Entity) {
		if (!m_ghost->model.isEmpty()) {
			m_renderer.drawGhostModel(params, m_ghost->model, m_ghost->transform);
		}
		boxLines(lines, m_ghost->box, {0.3f, 1.0f, 0.5f, 0.9f});
	}
	if (m_drag == Drag::BlockBase || m_drag == Drag::BlockHeight) {
		boxLines(lines, m_blockBox, {1.0f, 0.75f, 0.2f, 0.5f});
		m_renderer.drawPreviewSolids(params);
	}
	// Cordons: red while on (only what is inside gets compiled), grey while off.
	const auto cordons = m_editor->cordons();
	for (const auto& cordon : cordons.cordons) {
		boxLines(lines, cordon.box, cordons.active && cordon.active ? QVector4D(1, 0.3f, 0.3f, 0.9f) : QVector4D(0.6f, 0.6f, 0.65f, 0.5f));
	}
	if (!m_leak.empty()) {
		for (std::size_t i = 0; i + 1 < m_leak.size(); ++i) {
			line(lines, m_leak[i], m_leak[i + 1], {1, 0.1f, 0.1f, 1});
		}
	}
	m_renderer.drawLines(lines, viewProj, false, 2.f);
	lines.clear();
	drawToolLines(lines);
	drawRoomBox(lines);
	drawSoundscapeSphere(lines);
	drawConnections(lines);
	drawGizmo(lines);
	m_renderer.drawLines(lines, viewProj, true, 2.f);

	m_renderer.endFrame();
	drawOverlay();

	// Frame statistics.
	m_frameMs = m_frameClock.nsecsElapsed() / 1e6;
	++m_frames;
	if (m_fpsClock.elapsed() > 500) {
		m_fps = m_frames * 1000.0 / m_fpsClock.elapsed();
		m_frames = 0;
		m_fpsClock.restart();
		// The view only draws when something changes, so frames per second say little; the time
		// a frame takes says what the view could do.
		emit statsChanged(tr("%1 ms/frame (%2 fps possible)").arg(m_frameMs, 0, 'f', 1).arg(m_frameMs > 0.01 ? std::min(9999.0, 1000.0 / m_frameMs) : 9999.0, 0, 'f', 0));
		emit statsDetail(tr("%1 triangles, %2 draw calls, %3 lights (%6 with shadows), %4 ms CPU/frame, %5 frames drawn per second")
		                      .arg(m_renderer.triangleCount())
		                      .arg(m_renderer.lastDrawCalls())
		                      .arg(m_renderer.lightCount())
		                      .arg(m_frameMs, 0, 'f', 2)
		                      .arg(m_fps, 0, 'f', 0)
		                      .arg(m_renderer.shadowCasterCount()));
	}
}

void Viewport::drawGrid(std::vector<LineVertex>& lines) const {
	const double grid = m_editor->grid();
	if (m_camera.is2D()) {
		const int a = viewAxis() == 0 ? 1 : 0;   // horizontal screen axis
		const int b = viewAxis() == 2 ? 1 : 2;   // vertical screen axis
		double spacing = grid;
		while (spacing * m_camera.zoom < 6) {
			spacing *= 2;
		}
		const Vec3 lo = viewPlanePoint({0, double(height())});
		const Vec3 hi = viewPlanePoint({double(width()), 0});
		const double minA = std::min(lo[a], hi[a]), maxA = std::max(lo[a], hi[a]);
		const double minB = std::min(lo[b], hi[b]), maxB = std::max(lo[b], hi[b]);
		const auto colorFor = [&](double v) {
			if (std::abs(v) < 1e-6) return QVector4D(0.25f, 0.45f, 0.6f, 1);
			if (std::fmod(std::abs(v), 1024.0) < 1e-6) return QVector4D(0.36f, 0.36f, 0.4f, 1);
			if (std::fmod(std::abs(v), 64.0) < 1e-6) return QVector4D(0.24f, 0.24f, 0.27f, 1);
			return QVector4D(0.16f, 0.16f, 0.18f, 1);
		};
		for (double v = std::floor(minA / spacing) * spacing; v <= maxA; v += spacing) {
			Vec3 p0, p1;
			p0[a] = v;
			p0[b] = minB;
			p1[a] = v;
			p1[b] = maxB;
			line(lines, p0, p1, colorFor(v));
		}
		for (double v = std::floor(minB / spacing) * spacing; v <= maxB; v += spacing) {
			Vec3 p0, p1;
			p0[b] = v;
			p0[a] = minA;
			p1[b] = v;
			p1[a] = maxA;
			line(lines, p0, p1, colorFor(v));
		}
		return;
	}
	// 3D: grid on the ground plane around the point below the camera, fading out.
	double spacing = std::max(grid, 8.0);
	const double height = std::abs(m_camera.position.z());
	while (spacing < height / 24) {
		spacing *= 2;
	}
	const int count = 48;
	const double cx = std::round(m_camera.position.x() / spacing) * spacing;
	const double cy = std::round(m_camera.position.y() / spacing) * spacing;
	const double extent = count * spacing;
	for (int i = -count; i <= count; ++i) {
		const double fade = 1.0 - std::abs(i) / double(count);
		const double x = cx + i * spacing, y = cy + i * spacing;
		const bool majorX = std::fmod(std::abs(x), spacing * 8) < 1e-6, majorY = std::fmod(std::abs(y), spacing * 8) < 1e-6;
		const QVector4D cxColor = std::abs(x) < 1e-6 ? QVector4D(0.3f, 0.8f, 0.3f, float(0.8 * fade)) : QVector4D(1, 1, 1, float((majorX ? 0.22 : 0.09) * fade));
		const QVector4D cyColor = std::abs(y) < 1e-6 ? QVector4D(0.9f, 0.3f, 0.3f, float(0.8 * fade)) : QVector4D(1, 1, 1, float((majorY ? 0.22 : 0.09) * fade));
		// Slightly below 0 so a floor at z = 0 hides the grid instead of flickering with it.
		line(lines, {x, cy - extent, -0.5}, {x, cy + extent, -0.5}, cxColor);
		line(lines, {cx - extent, y, -0.5}, {cx + extent, y, -0.5}, cyColor);
	}
}

std::optional<Bounds> Viewport::selectedRoomBox(int* cubemapId) const {
	const auto& selection = m_editor->selection();
	const auto tool = m_editor->tool();
	if (selection.entities.size() != 1 || !selection.solids.empty() ||
	    (tool != Editor::Tool::Select && tool != Editor::Tool::Move && tool != Editor::Tool::Rotate && tool != Editor::Tool::Scale)) {
		return std::nullopt;
	}
	const Entity* cubemap = m_editor->doc().findEntity(selection.entities.front());
	if (!cubemap || !iequals(cubemap->classname(), "env_cubemap")) {
		return std::nullopt;
	}
	const Entity* obb = cubemapRoomBox(m_editor->doc(), *cubemap);
	if (!obb || obb->solids.empty()) {
		return std::nullopt;
	}
	Bounds box;
	for (const auto& solid : obb->solids) box.add(buildBrushGeometry(solid).bounds);
	if (cubemapId) *cubemapId = cubemap->id;
	return box;
}

void Viewport::drawRoomBox(std::vector<LineVertex>& lines) const {
	// Cubemap room box: cyan outline with a square in the middle of every side to drag.
	auto box = selectedRoomBox();
	if (!box) {
		return;
	}
	const bool dragging = m_drag == Drag::Gizmo && m_dragHandle >= Handle::RoomMinX && m_dragHandle <= Handle::RoomMaxZ;
	if (dragging) {
		box = m_dragNewBounds;
	}
	boxLines(lines, *box, {0.3f, 0.85f, 1.0f, 0.9f});
	const double hs = gizmoSize() * 0.035;
	for (int axis = 0; axis < 3; ++axis) {
		if (m_camera.is2D() && axis == viewAxis()) continue;
		for (int side = 0; side < 2; ++side) {
			const Handle h = static_cast<Handle>(static_cast<int>(Handle::RoomMinX) + axis * 2 + side);
			Vec3 p = box->center();
			p[axis] = side == 0 ? box->min[axis] : box->max[axis];
			Bounds square;
			square.add(p - Vec3{hs, hs, hs});
			square.add(p + Vec3{hs, hs, hs});
			boxLines(lines, square, (m_hover == h || m_dragHandle == h) ? HOVER_COLOR : QVector4D(0.3f, 0.85f, 1.0f, 1.0f));
		}
	}
}

const Entity* Viewport::selectedSoundscapeSphere() const {
	const auto& selection = m_editor->selection();
	const auto tool = m_editor->tool();
	if (selection.entities.size() != 1 || !selection.solids.empty() ||
	    (tool != Editor::Tool::Select && tool != Editor::Tool::Move && tool != Editor::Tool::Rotate && tool != Editor::Tool::Scale)) {
		return nullptr;
	}
	const Entity* entity = m_editor->doc().findEntity(selection.entities.front());
	if (!entity || !isSoundscapeEntity(*entity) || soundscapeRadius(*entity) <= 0 || soundscapeTrigger(m_editor->doc(), *entity)) {
		return nullptr;   // unlimited radius or a block: nothing round to show
	}
	return entity;
}

void Viewport::drawSoundscapeSphere(std::vector<LineVertex>& lines) const {
	// Where the soundscape is heard: a pink wire sphere (three great circles) with a square on
	// each side to drag the radius, like the cubemap room box.
	const Entity* entity = selectedSoundscapeSphere();
	if (!entity) {
		return;
	}
	const bool dragging = m_drag == Drag::Gizmo && m_dragHandle >= Handle::RadiusMinX && m_dragHandle <= Handle::RadiusMaxZ;
	const double radius = dragging ? m_dragRadius : soundscapeRadius(*entity);
	const Vec3 center = entity->origin();
	const QVector4D color(1.0f, 0.55f, 0.85f, 0.9f);
	constexpr int SEGMENTS = 64;
	for (int plane = 0; plane < 3; ++plane) {
		const int a = plane == 0 ? 0 : (plane == 1 ? 0 : 1), b = plane == 0 ? 1 : 2;
		Vec3 previous;
		for (int i = 0; i <= SEGMENTS; ++i) {
			const double angle = i * 2 * M_PI / SEGMENTS;
			Vec3 p = center;
			p[a] += std::cos(angle) * radius;
			p[b] += std::sin(angle) * radius;
			if (i > 0) line(lines, previous, p, color);
			previous = p;
		}
	}
	const double hs = gizmoSize() * 0.035;
	for (int axis = 0; axis < 3; ++axis) {
		if (m_camera.is2D() && axis == viewAxis()) continue;
		for (int side = 0; side < 2; ++side) {
			const Handle h = static_cast<Handle>(static_cast<int>(Handle::RadiusMinX) + axis * 2 + side);
			Vec3 p = center;
			p[axis] += side == 0 ? -radius : radius;
			Bounds square;
			square.add(p - Vec3{hs, hs, hs});
			square.add(p + Vec3{hs, hs, hs});
			boxLines(lines, square, (m_hover == h || m_dragHandle == h) ? HOVER_COLOR : QVector4D(1.0f, 0.55f, 0.85f, 1.0f));
		}
	}
}

std::vector<Vec3> Viewport::selectionCorners() const {
	const Bounds box = m_drag == Drag::Gizmo ? m_dragBounds : edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
	std::vector<Vec3> corners;
	if (box.empty()) {
		return corners;
	}
	for (int i = 0; i < 8; ++i) {
		corners.push_back({(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z});
	}
	return corners;
}

void Viewport::drawGizmo(std::vector<LineVertex>& lines) const {
	const auto tool = m_editor->tool();
	const auto& selection = m_editor->selection();
	if (!selection.hasObjects() || (tool != Editor::Tool::Move && tool != Editor::Tool::Rotate && !resizeHandles())) {
		if (!selection.empty() && m_camera.is2D()) {
			auto box = edit::selectionBounds(m_editor->doc(), selection, m_editor->entities());
			if (m_drag == Drag::Gizmo) {
				box.min += m_dragDelta;
				box.max += m_dragDelta;
			}
			boxLines(lines, box, {1, 0.3f, 0.2f, 0.6f});
		}
		if (m_drag == Drag::Marquee) {
			// Marquee drawn by QPainter overlay.
		}
		return;
	}
	const bool dragging = m_drag == Drag::Gizmo;
	Bounds bounds = dragging ? m_dragBounds : edit::selectionBounds(m_editor->doc(), selection, m_editor->entities());
	Vec3 center = dragging ? m_dragCenter : bounds.center();
	if (dragging && tool == Editor::Tool::Move) {
		center += m_dragDelta;
	}
	const double size = gizmoSize();
	const auto colorOf = [&](Handle h, int axis) {
		return (m_hover == h || m_dragHandle == h) ? HOVER_COLOR : AXIS_COLORS[axis];
	};

	if (tool == Editor::Tool::Move) {
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) {
				continue;
			}
			const Handle h = static_cast<Handle>(static_cast<int>(Handle::AxisX) + axis);
			const Vec3 dir = axisVector(axis);
			const Vec3 tip = center + dir * size;
			line(lines, center, tip, colorOf(h, axis));
			// Arrow head.
			const Vec3 side1 = axisVector((axis + 1) % 3) * (size * 0.06), side2 = axisVector((axis + 2) % 3) * (size * 0.06);
			const Vec3 back = center + dir * (size * 0.82);
			for (const Vec3& s : {side1, -side1, side2, -side2}) {
				line(lines, tip, back + s, colorOf(h, axis));
			}
		}
		if (!m_camera.is2D()) {
			const Handle planes[3] = {Handle::PlaneYZ, Handle::PlaneXZ, Handle::PlaneXY};
			for (int normal = 0; normal < 3; ++normal) {
				const Vec3 u = axisVector((normal + 1) % 3) * (size * 0.3), v = axisVector((normal + 2) % 3) * (size * 0.3);
				const auto color = (m_hover == planes[normal] || m_dragHandle == planes[normal]) ? HOVER_COLOR : AXIS_COLORS[normal] * 0.8f;
				line(lines, center + u, center + u + v, color);
				line(lines, center + v, center + u + v, color);
			}
		}
		// Corner squares: drag one onto a corner of another brush or model to line them up.
		const double hs = size * 0.022;
		const Vec3 offset = dragging ? m_dragDelta : Vec3{};
		const QVector4D cornerColor = (m_hover == Handle::Corner || m_dragHandle == Handle::Corner) ? HOVER_COLOR : QVector4D(0.95f, 0.95f, 1.0f, 0.9f);
		for (const Vec3& corner : selectionCorners()) {
			Bounds square;
			square.add(corner + offset - Vec3{hs, hs, hs});
			square.add(corner + offset + Vec3{hs, hs, hs});
			boxLines(lines, square, cornerColor);
		}
		if (dragging && m_dragHandle == Handle::Corner && m_snapHit) {
			Bounds target;
			target.add(*m_snapHit - Vec3{hs * 2, hs * 2, hs * 2});
			target.add(*m_snapHit + Vec3{hs * 2, hs * 2, hs * 2});
			boxLines(lines, target, {0.3f, 1.0f, 0.4f, 1.0f});
		}
	} else if (tool == Editor::Tool::Rotate) {
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis != viewAxis()) {
				continue;
			}
			const Handle h = static_cast<Handle>(static_cast<int>(Handle::RingX) + axis);
			const Vec3 u = axisVector((axis + 1) % 3) * size, v = axisVector((axis + 2) % 3) * size;
			constexpr int segments = 64;
			for (int i = 0; i < segments; ++i) {
				const double a0 = 2 * M_PI * i / segments, a1 = 2 * M_PI * (i + 1) / segments;
				line(lines, center + u * std::cos(a0) + v * std::sin(a0), center + u * std::cos(a1) + v * std::sin(a1), colorOf(h, axis));
			}
		}
	} else if (resizeHandles()) {
		const Bounds box = dragging ? m_dragNewBounds : bounds;
		boxLines(lines, box, {1, 0.6f, 0.2f, 1});
		const double hs = size * 0.05;
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) {
				continue;
			}
			for (int side = 0; side < 2; ++side) {
				const Handle h = static_cast<Handle>(static_cast<int>(Handle::ScaleMinX) + axis * 2 + side);
				Vec3 p = box.center();
				p[axis] = side == 0 ? box.min[axis] : box.max[axis];
				Bounds handle;
				handle.add(p - Vec3{hs, hs, hs});
				handle.add(p + Vec3{hs, hs, hs});
				boxLines(lines, handle, colorOf(h, axis));
			}
		}
	}
}

void Viewport::drawConnections(std::vector<LineVertex>& lines) const {
	// Hammer's entity connection lines: green to what the selection's outputs fire at, blue from
	// what fires at the selection.
	if (!m_editor->showConnections() || m_editor->selection().entities.empty()) {
		return;
	}
	const auto& doc = m_editor->doc();
	const QVector4D outgoing(0.35f, 1.0f, 0.45f, 0.95f), incoming(0.4f, 0.7f, 1.0f, 0.95f);
	int count = 0;
	for (const int id : m_editor->selection().entities) {
		const Entity* source = doc.findEntity(id);
		if (!source || id == 0) continue;
		const Vec3 from = entityCenter(*source);
		const std::string name = source->get("targetname");
		for (const auto& other : doc.entities) {
			if (other.id == id || count > 400) continue;
			bool fires = false, firedAt = false;
			for (const auto& c : source->connections) fires = fires || targetMatches(c.target, other.get("targetname"));
			for (const auto& c : other.connections) firedAt = firedAt || targetMatches(c.target, name);
			if (fires) line(lines, from, entityCenter(other), outgoing);
			if (firedAt) line(lines, entityCenter(other), from, incoming);
			count += fires + firedAt;
		}
	}
}

void Viewport::drawOverlay() {
	// The overlay is recorded first and painted on the CPU only when it changed; the GPU just
	// draws the finished picture. (QPainter straight onto the GL widget garbles or loses text
	// on some drivers.)
	OverlayRecorder p;
	if (m_drag == Drag::Marquee) {
		p.drawMarquee(m_marquee);
	}
	if (m_drag == Drag::BlockHeight) {
		p.drawText(QRectF(8, height() - 26, width() - 16, 20), QColor(255, 200, 80), tr("Move the mouse to set the height (%1) - click or Enter creates it (drag on for the next block), Esc cancels, right mouse looks around")
		                                                             .arg(m_blockBox.size().z));
	}
	const auto hint = [&](const QString& text) {
		p.drawText(QRectF(8, height() - 26, width() - 16, 20), text, QColor(150, 230, 255));
	};
	if (m_editor->tool() == Editor::Tool::Clip) {
		const QString keep = m_clipKeep == edit::ClipKeep::Both ? tr("both parts") : (m_clipKeep == edit::ClipKeep::Back ? tr("back part") : tr("front part"));
		hint(m_camera.is2D() ? tr("Clip: drag a line through the selection - keeps %1 (Tab), Enter cuts").arg(keep)
		                     : tr("Clip: click three points (%1 of 3) - keeps %2 (Tab), Enter cuts").arg(m_clipPoints.size()).arg(keep));
	} else if (m_editor->tool() == Editor::Tool::Vertex) {
		hint(m_editor->selection().solids.empty() ? tr("Vertex: select a brush, then drag its corners or edge middles (Ctrl+click: several)")
		                                          : tr("Vertex: drag a corner or edge middle; Ctrl+click selects several; brushes stay convex"));
	} else if (m_editor->tool() == Editor::Tool::Polygon) {
		hint(tr("Polygon: click the corners of the base (%1), Enter or double-click makes the brush, Backspace removes a corner").arg(m_polygon.size()));
	}
	if (m_drag == Drag::Gizmo && m_dragHandle >= Handle::RadiusMinX && m_dragHandle <= Handle::RadiusMaxZ) {
		p.drawText(QRectF(8, height() - 26, 300, 20), QColor(255, 170, 220), tr("Soundscape radius %1").arg(m_dragRadius));
	} else if (m_drag == Drag::Gizmo && m_editor->tool() == Editor::Tool::Rotate) {
		p.drawText(QRectF(8, height() - 26, 200, 20), tr("%1°").arg(m_dragAngle), QColor(255, 220, 120));
	}
	const bool areaHandle = m_dragHandle >= Handle::RoomMinX && m_dragHandle <= Handle::RadiusMaxZ;
	if (m_drag == Drag::Gizmo && !areaHandle && (m_editor->tool() == Editor::Tool::Move || m_camera.is2D())) {
		p.drawText(QRectF(8, height() - 26, 400, 20), QColor(255, 220, 120), tr("Move %1 %2 %3%4")
		                                                  .arg(m_dragDelta.x)
		                                                  .arg(m_dragDelta.y)
		                                                  .arg(m_dragDelta.z)
		                                                  .arg((m_pressModifiers & Qt::ShiftModifier) ? tr("  (copy)") : QString()));
	}
	if (m_editor->doc().solidCount() == 0 && m_editor->doc().entities.empty() && !m_camera.is2D()) {
		p.drawText(QRectF(rect().adjusted(0, 0, 0, -40)), QColor(170, 175, 185), Qt::AlignCenter,
		           tr("Empty map\nB: block tool - drag on the grid, then set the height\nShift+E: entity tool\n"
		              "Right mouse: look around, with W A S D Q E to fly\nCtrl+O: open a .vmf"));
	}

	// Entity names in the 2D views (Hammer's "show entity names"), when zoomed in far enough.
	if (m_camera.is2D() && m_editor->showEntityNames() && !m_editor->doc().entities.empty()) {
		// How many pixels 64 units take here (the 2D view shows two of the three axes).
		const Vec3 probe = m_editor->doc().entities.front().origin();
		double pixels = 0;
		QPointF a;
		if (m_camera.project(probe, width(), height(), a)) {
			for (const Vec3& step : {Vec3{64, 0, 0}, Vec3{0, 64, 0}, Vec3{0, 0, 64}}) {
				QPointF b;
				if (m_camera.project(probe + step, width(), height(), b)) pixels = std::max(pixels, QLineF(a, b).length());
			}
		}
		const bool near = pixels >= 18;
		if (near) {
			const auto& hidden = m_editor->hidden().entities;
			int shown = 0;
			for (const auto& entity : m_editor->doc().entities) {
				if (shown >= 400) break;
				if (std::find(hidden.begin(), hidden.end(), entity.id) != hidden.end()) continue;
				QPointF at;
				if (!m_camera.project(entityCenter(entity), width(), height(), at) || !rect().contains(at.toPoint())) continue;
				const std::string targetname = entity.get("targetname");
				const QString text = QString::fromStdString(targetname.empty() ? entity.classname() : targetname);
				p.drawText(QRectF(at.x() - 90, at.y() + 9, 180, 16), text, targetname.empty() ? QColor(150, 150, 150) : QColor(235, 235, 235),
				           Qt::AlignHCenter | Qt::AlignTop);
				++shown;
			}
		}
	}

	const bool changed = p.items != m_overlayItems || m_overlayImage.size() != size() * devicePixelRatio();
	if (changed) {
		m_overlayItems = std::move(p.items);
		paintOverlayImage();
	}
	m_renderer.drawOverlay(m_overlayImage, changed);
}

void Viewport::paintOverlayImage() {
	const qreal dpr = devicePixelRatio();
	m_overlayImage = QImage(size() * dpr, QImage::Format_RGBA8888_Premultiplied);
	m_overlayImage.setDevicePixelRatio(dpr);
	m_overlayImage.fill(Qt::transparent);
	QPainter painter(&m_overlayImage);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);
	QFont font = this->font();
	font.setPointSizeF(9);
	painter.setFont(font);
	for (const auto& item : m_overlayItems) {
		if (item.marquee) {
			painter.setPen(QPen(QColor(255, 160, 40), 1, Qt::DashLine));
			painter.setBrush(QColor(255, 160, 40, 30));
			painter.drawRect(item.rect);
			continue;
		}
		// A soft dark outline keeps the text readable on bright textures.
		painter.setPen(QColor(0, 0, 0, 170));
		painter.drawText(item.rect.translated(1, 1), item.flags, item.text);
		painter.setPen(item.color);
		painter.drawText(item.rect, item.flags, item.text);
	}
}

// --- Camera ----------------------------------------------------------------------

void Viewport::moveCamera() {
	const double dt = std::min(0.1, m_moveClock.nsecsElapsed() / 1e9);
	m_moveClock.restart();
	if (!m_looking) {
		m_moveTimer.stop();
		return;
	}
	applyLook();   // turn with the current cursor position every frame, not only on mouse events
	QVector3D dir;
	if (m_keys.contains(Qt::Key_W)) dir += m_camera.forward();
	if (m_keys.contains(Qt::Key_S)) dir -= m_camera.forward();
	if (m_keys.contains(Qt::Key_D)) dir += m_camera.right();
	if (m_keys.contains(Qt::Key_A)) dir -= m_camera.right();
	if (m_keys.contains(Qt::Key_E)) dir += QVector3D(0, 0, 1);
	if (m_keys.contains(Qt::Key_Q)) dir -= QVector3D(0, 0, 1);
	if (dir.isNull()) {
		return;
	}
	const float boost = m_keys.contains(Qt::Key_Shift) ? 3.f : 1.f;
	m_camera.position += dir.normalized() * float(m_flySpeed * boost * dt);
	m_lookFlew = true;
	update();
}

void Viewport::wheelEvent(QWheelEvent* event) {
	const double steps = event->angleDelta().y() / 120.0;
	if (m_camera.is2D()) {
		const Vec3 before = viewPlanePoint(event->position());
		m_camera.zoom = float(std::clamp(m_camera.zoom * std::pow(1.2, steps), 0.005, 64.0));
		const Vec3 after = viewPlanePoint(event->position());
		m_camera.position += toQt(before - after);
	} else if (m_looking) {
		m_flySpeed = float(std::clamp(m_flySpeed * std::pow(1.25, steps), 50.0, 20000.0));
		m_lookFlew = true;   // the wheel changed the speed: no menu when letting go
		emit statsChanged(tr("Fly speed: %1").arg(m_flySpeed, 0, 'f', 0));
	} else {
		m_camera.position += m_camera.forward() * float(steps * std::max(32.0, m_flySpeed * 0.25));
	}
	update();
}

// --- Mouse -----------------------------------------------------------------------

Viewport::Handle Viewport::hitHandle(const QPointF& pos) const {
	const auto tool = m_editor->tool();
	if (!m_editor->selection().hasObjects()) {
		return Handle::None;
	}
	const auto bounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
	const Vec3 center = bounds.center();
	const double size = gizmoSize();
	QPointF c;
	if (!m_camera.project(center, width(), height(), c)) {
		return Handle::None;
	}
	constexpr double TOLERANCE = 8;
	if (const auto room = selectedRoomBox()) {
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) continue;
			for (int side = 0; side < 2; ++side) {
				Vec3 p = room->center();
				p[axis] = side == 0 ? room->min[axis] : room->max[axis];
				QPointF q;
				if (m_camera.project(p, width(), height(), q) && QLineF(q, pos).length() < 9) {
					return static_cast<Handle>(static_cast<int>(Handle::RoomMinX) + axis * 2 + side);
				}
			}
		}
	}
	if (const Entity* scape = selectedSoundscapeSphere()) {
		const double radius = soundscapeRadius(*scape);
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) continue;
			for (int side = 0; side < 2; ++side) {
				Vec3 p = scape->origin();
				p[axis] += side == 0 ? -radius : radius;
				QPointF q;
				if (m_camera.project(p, width(), height(), q) && QLineF(q, pos).length() < 9) {
					return static_cast<Handle>(static_cast<int>(Handle::RadiusMinX) + axis * 2 + side);
				}
			}
		}
	}
	if (tool == Editor::Tool::Move) {
		for (const Vec3& corner : selectionCorners()) {
			QPointF q;
			// Corners right at the gizmo (tiny or far objects) leave the arrows usable.
			if (m_camera.project(corner, width(), height(), q) && QLineF(q, pos).length() < 7 && QLineF(q, c).length() > 16) {
				return Handle::Corner;
			}
		}
		if (!m_camera.is2D()) {
			const Handle planes[3] = {Handle::PlaneYZ, Handle::PlaneXZ, Handle::PlaneXY};
			for (int normal = 0; normal < 3; ++normal) {
				const Vec3 u = axisVector((normal + 1) % 3) * (size * 0.15), v = axisVector((normal + 2) % 3) * (size * 0.15);
				QPointF q;
				if (m_camera.project(center + u + v, width(), height(), q) && QLineF(q, pos).length() < 12) {
					return planes[normal];
				}
			}
		}
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) {
				continue;
			}
			QPointF tip;
			if (m_camera.project(center + axisVector(axis) * size, width(), height(), tip) && distanceToSegment(pos, c, tip) < TOLERANCE) {
				return static_cast<Handle>(static_cast<int>(Handle::AxisX) + axis);
			}
		}
	} else if (tool == Editor::Tool::Rotate) {
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis != viewAxis()) {
				continue;
			}
			const Vec3 u = axisVector((axis + 1) % 3) * size, v = axisVector((axis + 2) % 3) * size;
			QPointF prev;
			bool havePrev = false;
			for (int i = 0; i <= 48; ++i) {
				const double a = 2 * M_PI * i / 48;
				QPointF q;
				if (!m_camera.project(center + u * std::cos(a) + v * std::sin(a), width(), height(), q)) {
					havePrev = false;
					continue;
				}
				if (havePrev && distanceToSegment(pos, prev, q) < TOLERANCE) {
					return static_cast<Handle>(static_cast<int>(Handle::RingX) + axis);
				}
				prev = q;
				havePrev = true;
			}
		}
	} else if (resizeHandles()) {
		// The nearest square on screen; squares on sides facing away from the camera lose ties
		// (the one on the bottom of a block lies right behind the top one when looking down).
		Handle best = Handle::None;
		double bestDistance = 10;
		for (int axis = 0; axis < 3; ++axis) {
			if (m_camera.is2D() && axis == viewAxis()) {
				continue;
			}
			for (int side = 0; side < 2; ++side) {
				Vec3 p = center;
				p[axis] = side == 0 ? bounds.min[axis] : bounds.max[axis];
				QPointF q;
				if (!m_camera.project(p, width(), height(), q)) {
					continue;
				}
				const double distance = QLineF(q, pos).length() + (handleFacesCamera(p, axis, side) ? 0 : 6);
				if (distance < bestDistance) {
					bestDistance = distance;
					best = static_cast<Handle>(static_cast<int>(Handle::ScaleMinX) + axis * 2 + side);
				}
			}
		}
		return best;
	}
	return Handle::None;
}

void Viewport::mousePressEvent(QMouseEvent* event) {
	setFocus();
	m_pressPos = m_lastPos = event->position();
	m_pressModifiers = event->modifiers();
	const auto tool = m_editor->tool();

	if (event->button() == Qt::RightButton && !m_camera.is2D() && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
		// Right mouse always looks around - also while a block is being placed.
		if (!m_looking) {
			beginLook(event->position());
		}
		return;
	}
	if (m_looking) {
		return;
	}
	if (m_drag == Drag::BlockHeight) {
		if (event->button() != Qt::LeftButton) {
			return;
		}
		// The click creates the block and may already start the next one: dragging on goes
		// straight into the next base, a plain click just creates.
		createBlockFromDrag();
	}
	if (m_drag != Drag::None) {
		return;   // another button pressed during a drag
	}

	if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)) {
		pickMaterial(event->position());
		return;
	}
	if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ControlModifier)) {
		// Ctrl + right mouse: paint the current material on every face the cursor passes.
		m_drag = Drag::Paint;
		++m_paintStroke;
		paintAt(event->position());
		return;
	}
	if (event->button() == Qt::RightButton) {
		m_drag = Drag::Pan;   // 2D views
		return;
	}
	if (event->button() == Qt::MiddleButton) {
		m_drag = Drag::Pan;
		return;
	}
	if (event->button() != Qt::LeftButton) {
		return;
	}
	if (!m_camera.is2D() && (event->modifiers() & Qt::AltModifier)) {
		m_drag = Drag::Orbit;
		const auto hit = m_renderer.pick(rayAt(event->position()));
		const auto selBounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
		m_orbitPivot = !selBounds.empty() ? selBounds.center() : (hit ? hit->point : fromQt(m_camera.position + m_camera.forward() * 512));
		return;
	}

	if (toolMousePress(event)) {
		return;
	}
	// The block tool, and the entity tool with a brush class armed (the box becomes that entity).
	if (tool == Editor::Tool::Block || (tool == Editor::Tool::Entity && m_editor->armedBrushEntity())) {
		m_blockForEntity = tool == Editor::Tool::Entity;
		if (m_camera.is2D()) {
			m_blockStart = snap(viewPlanePoint(event->position()), m_editor->snapGrid());
			m_blockEnd = m_blockStart;
		} else {
			const Ray ray = rayAt(event->position());
			const auto hit = m_renderer.pick(ray);
			m_blockPlane = (hit && hit->normal.z > 0.7) ? snap(hit->point.z, m_editor->snapGrid()) : 0.0;
			const auto p = rayPlane(ray, {0, 0, m_blockPlane}, {0, 0, 1});
			if (!p) {
				return;
			}
			m_blockStart = m_blockEnd = snap(*p, m_editor->snapGrid());
			m_blockStart.z = m_blockEnd.z = m_blockPlane;
		}
		m_drag = Drag::BlockBase;
		updateBlockDrag(event->position());
		return;
	}
	if (tool == Editor::Tool::Entity) {
		placeEntity(event->position());
		return;
	}

	const Handle handle = hitHandle(event->position());
	if (handle != Handle::None) {
		beginGizmoDrag(handle, event->position());
		return;
	}
	// 2D views: dragging inside the selection moves it (Hammer style).
	if (m_camera.is2D() && m_editor->selection().hasObjects() && tool != Editor::Tool::Rotate) {
		const auto box = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
		const Vec3 p = viewPlanePoint(event->position());
		const int a = viewAxis() == 0 ? 1 : 0, b = viewAxis() == 2 ? 1 : 2;
		if (p[a] >= box.min[a] && p[a] <= box.max[a] && p[b] >= box.min[b] && p[b] <= box.max[b]) {
			beginGizmoDrag(Handle::Move2D, event->position());
			return;
		}
	}
	m_drag = Drag::Marquee;
	m_marquee = QRectF(event->position(), QSizeF());
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
	if (m_looking) {
		applyLook();
		return;
	}
	const QPointF pos = event->position();
	const QPointF delta = pos - m_lastPos;
	m_lastPos = pos;

	if (m_camera.is2D()) {
		const Vec3 p = snap(viewPlanePoint(pos), m_editor->snapGrid());
		emit cursorWorldPosition(QStringLiteral("%1 %2 %3").arg(p.x).arg(p.y).arg(p.z));
	}

	switch (m_drag) {
		case Drag::Pan: {
			if (m_camera.is2D()) {
				m_camera.position -= m_camera.right() * float(delta.x() / m_camera.zoom) - m_camera.up() * float(delta.y() / m_camera.zoom);
			} else {
				const float speed = std::max(0.5f, m_flySpeed / 600.f);
				m_camera.position += (-m_camera.right() * float(delta.x()) + m_camera.up() * float(delta.y())) * speed;
			}
			update();
			return;
		}
		case Drag::Orbit: {
			const QVector3D pivot = toQt(m_orbitPivot);
			const float distance = (m_camera.position - pivot).length();
			m_camera.yaw -= float(delta.x() * 0.3);
			m_camera.pitch = std::clamp(m_camera.pitch - float(delta.y() * 0.3), -89.f, 89.f);
			m_camera.position = pivot - m_camera.forward() * distance;
			update();
			return;
		}
		case Drag::Gizmo:
			updateGizmoDrag(pos);
			return;
		case Drag::BlockBase:
			updateBlockDrag(pos);
			return;
		case Drag::BlockHeight: {
			const Ray ray = rayAt(pos);
			const Vec3 center = m_blockBox.center();
			Vec3 normal = fromQt(-m_camera.forward());
			normal.z = 0;
			normal = normalize(normal);
			if (const auto p = rayPlane(ray, center, normal)) {
				const double minimum = std::max(m_editor->snapGrid(), 1.0);
				const double h = snap(p->z - m_blockPlane, m_editor->snapGrid());
				m_blockHeight = std::abs(h) < minimum ? minimum : h;
				const double z0 = m_blockPlane, z1 = m_blockPlane + m_blockHeight;
				m_blockBox.min.z = std::min(z0, z1);
				m_blockBox.max.z = std::max(z0, z1);
				updateShapePreview();
			}
			update();
			return;
		}
		case Drag::Marquee:
			m_marquee = QRectF(m_pressPos, pos).normalized();
			update();
			return;
		case Drag::ClipLine:
			m_clipPoints.back() = toolPoint(pos, false);
			updateClipPreview();
			return;
		case Drag::Vertex:
			updateVertexDrag(pos);
			return;
		case Drag::Paint:
			paintAt(pos);
			return;
		case Drag::None:
			break;
	}
	if (!m_camera.is2D()) {
		showHoverInfo(pos);
	}
	updateGhost(&pos);
	const Handle hover = hitHandle(pos);
	if (hover != m_hover) {
		m_hover = hover;
		update();
	}
}

void Viewport::mouseReleaseEvent(QMouseEvent* event) {
	const Drag drag = m_drag;
	if (event->button() == Qt::RightButton && m_looking) {
		if (!m_flyMode) {
			// Only a quick click opens the menu - not letting go after looking around or flying
			// with W A S D (the mouse may not have moved at all then).
			const bool click = m_lookTravel <= 3 && !m_lookFlew && m_lookClock.elapsed() < 400;
			const QPointF at = mapFromGlobal(m_lookAnchor);
			endLook();
			if (click && m_drag == Drag::None) {
				requestContextMenu(at);
			}
		}
		return;
	}
	if (event->button() == Qt::RightButton && drag == Drag::Pan && m_camera.is2D() && QLineF(m_pressPos, event->position()).length() < 4) {
		m_drag = Drag::None;
		requestContextMenu(event->position());
		return;
	}
	// While looking the cursor is parked: finish with the last position before the look.
	const QPointF releasePos = m_looking ? m_lastPos : event->position();
	if (drag == Drag::Pan || drag == Drag::Orbit) {
		m_drag = Drag::None;
		return;
	}
	if (drag == Drag::Paint) {
		m_drag = Drag::None;
		setHoverSide(0);
		return;
	}
	if (event->button() != Qt::LeftButton) {
		return;
	}
	switch (drag) {
		case Drag::Gizmo:
			finishGizmoDrag(m_pressModifiers & Qt::ShiftModifier);
			break;
		case Drag::BlockBase: {
			updateBlockDrag(releasePos);
			const Vec3 size = m_blockBox.size();
			if (QLineF(m_pressPos, releasePos).length() < 4) {
				m_drag = Drag::None;   // a click, not a drag
				setPreviewSolids({});
				update();
				break;
			}
			if (m_camera.is2D()) {
				m_drag = Drag::None;
				const int a = viewAxis() == 0 ? 1 : 0, b = viewAxis() == 2 ? 1 : 2;
				if (size[a] > 0 && size[b] > 0) {
					createBlockFromDrag();
				}
			} else if (size.x > 0 && size.y > 0) {
				m_drag = Drag::BlockHeight;
				setMouseTracking(true);
			} else {
				m_drag = Drag::None;
			}
			update();
			break;
		}
		case Drag::ClipLine:
			m_drag = Drag::None;
			if (m_clipPoints.size() == 2 && length(m_clipPoints[1] - m_clipPoints[0]) < 0.01) {
				m_clipPoints.clear();   // a click, not a line
			}
			updateClipPreview();
			break;
		case Drag::Vertex: {
			m_drag = Drag::None;
			setPreviewSolids({});
			const Vec3 delta = m_vertexDelta;
			m_vertexDelta = {};
			if (length(delta) > 0) {
				const auto corners = m_vertexSelection;
				m_editor->moveSelectedVertices(corners, delta);
				m_vertexSelection.clear();
				for (const auto& c : corners) m_vertexSelection.push_back(c + delta);
			}
			m_editor->setVertexCorners(m_vertexSelection);
			update();
			break;
		}
		case Drag::Marquee: {
			m_drag = Drag::None;
			if (QLineF(m_pressPos, releasePos).length() < 4) {
				clickSelect(releasePos, event->modifiers());
			} else {
				marqueeSelect(m_marquee, event->modifiers());
			}
			update();
			break;
		}
		default:
			break;
	}
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) {
		return;
	}
	if (m_editor->tool() == Editor::Tool::Polygon) {
		finishPolygon();
		return;
	}
	if (m_editor->selectMode() == Editor::SelectMode::Face && !m_camera.is2D()) {
		// Double-click a face: select the whole wall (all touching faces in the same plane).
		if (const auto hit = m_renderer.pick(rayAt(event->position()), true)) {
			Selection selection = (event->modifiers() & Qt::ControlModifier) ? m_editor->selection() : Selection{};
			for (const auto& face : edit::coplanarConnectedFaces(m_editor->doc(), {hit->solid, hit->sideId})) {
				if (!selection.contains(face)) {
					selection.faces.push_back(face);
				}
			}
			m_editor->setSelection(std::move(selection));
			emit m_editor->message(tr("%1 faces of this wall selected - pick a material (or Shift+T)").arg(m_editor->selection().faces.size()));
		}
		return;
	}
	if (m_editor->tool() == Editor::Tool::Select) {
		frameSelection();
	}
}

void Viewport::paintAt(const QPointF& pos) {
	const auto hit = m_renderer.pick(rayAt(pos), true);
	if (!hit) {
		setHoverSide(0);
		return;
	}
	setHoverSide(hit->sideId);
	const std::string material = m_editor->currentMaterial().toStdString();
	const auto* solid = m_editor->doc().findSolid(hit->solid);
	if (!solid) {
		return;
	}
	for (const auto& side : solid->sides) {
		if (side.id == hit->sideId && iequals(side.material, material)) {
			return;   // already painted
		}
	}
	const std::string name = "Paint material " + std::to_string(m_paintStroke);
	m_editor->commitMerged(edit::modifyFaces(m_editor->doc(), {FaceRef{hit->solid, hit->sideId}}, name, [&](Side& side) { side.material = material; }));
}

void Viewport::setHoverSide(int sideId) {
	if (m_hoverSide != sideId) {
		m_hoverSide = sideId;
		m_selectionDirty = true;
		update();
	}
}

void Viewport::showHoverInfo(const QPointF& pos) {
	if (m_hoverClock.isValid() && m_hoverClock.elapsed() < 60) {
		return;
	}
	m_hoverClock.start();
	const auto hit = m_renderer.pick(rayAt(pos), true);
	QString text;
	if (hit && m_editor->selectMode() != Editor::SelectMode::Face) {
		// Where the mouse points and how far away that is (Source 2 Hammer's [dist]).
		const Vec3 p = hit->point;
		const double distance = length(p - fromQt(m_camera.position));
		text = tr("Cursor %1 %2 %3  ·  dist %4").arg(std::round(p.x)).arg(std::round(p.y)).arg(std::round(p.z)).arg(distance, 0, 'f', 0);
	} else if (hit) {
		if (const auto* solid = m_editor->doc().findSolid(hit->solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == hit->sideId) {
					text = tr("Material: %1  (Shift+right click picks it, Ctrl+right drag paints %2)")
					           .arg(QString::fromStdString(side.material), m_editor->currentMaterial());
				}
			}
		}
	}
	emit cursorWorldPosition(text);
}

void Viewport::clickSelect(const QPointF& pos, Qt::KeyboardModifiers modifiers) {
	const bool faceMode = m_editor->selectMode() == Editor::SelectMode::Face;
	const bool add = modifiers & Qt::ControlModifier;
	Selection selection = add ? m_editor->selection() : Selection{};
	const auto hit = m_renderer.pick(rayAt(pos), faceMode);

	if (hit && faceMode && (modifiers & Qt::AltModifier)) {
		// Alt+click in face mode: pick the material of the face (eyedropper).
		if (const auto* solid = m_editor->doc().findSolid(hit->solid)) {
			for (const auto& side : solid->sides) {
				if (side.id == hit->sideId) {
					m_editor->setCurrentMaterial(QString::fromStdString(side.material));
					emit m_editor->message(tr("Material picked: %1").arg(QString::fromStdString(side.material)));
				}
			}
		}
		return;
	}
	if (hit) {
		if (faceMode) {
			const FaceRef face{hit->solid, hit->sideId};
			if (add) {
				selection.toggle(face);
			} else {
				selection.faces = {face};
			}
		} else if (hit->entityId != 0) {
			if (add) {
				selection.toggleEntity(hit->entityId);
			} else {
				selection.entities = {hit->entityId};
			}
		} else {
			if (add) {
				selection.toggle(hit->solid);
			} else {
				selection.solids = {hit->solid};
			}
		}
	}
	m_editor->setSelection(std::move(selection));
}

void Viewport::marqueeSelect(const QRectF& rect, Qt::KeyboardModifiers modifiers) {
	Selection selection = (modifiers & Qt::ControlModifier) ? m_editor->selection() : Selection{};
	const auto inside = [&](const Bounds& box) {
		QPointF a, b;
		if (!m_camera.project(box.min, width(), height(), a) || !m_camera.project(box.max, width(), height(), b)) {
			return false;
		}
		if (m_camera.is2D()) {
			return rect.contains(QRectF(a, b).normalized());
		}
		QPointF c;
		return m_camera.project(box.center(), width(), height(), c) && rect.contains(c);
	};
	const auto& doc = m_editor->doc();
	for (const auto& solid : doc.world.solids) {
		const SolidRef ref{0, solid.id};
		if (inside(buildBrushGeometry(solid).bounds) && !selection.contains(ref)) {
			selection.solids.push_back(ref);
		}
	}
	for (const auto& entity : doc.entities) {
		if (inside(edit::entityBounds(entity, m_editor->entities())) && !selection.containsEntity(entity.id)) {
			selection.entities.push_back(entity.id);
		}
	}
	m_editor->setSelection(std::move(selection));
}

void Viewport::updateGhost(const QPointF* pos) {
	std::optional<Ghost> ghost;
	const QString classname = m_editor->currentEntityClass();
	if (pos && m_editor->tool() == Editor::Tool::Entity && m_drag == Drag::None && !classname.isEmpty() && !m_editor->armedBrushEntity()) {
		const auto* catalog = m_editor->entities();
		Entity probe;
		probe.set("classname", classname.toStdString());
		probe.setOrigin({0, 0, 0});
		const Bounds local = edit::entityBounds(probe, catalog);
		if (const auto origin = restingPlace(*pos, local)) {
			Ghost g;
			g.box.add(local.min + *origin);
			g.box.add(local.max + *origin);
			g.model = QString::fromStdString(edit::entityModel(probe, catalog));
			g.transform.translate(float(origin->x), float(origin->y), float(origin->z));
			ghost = std::move(g);
		}
	}
	const bool changed = ghost.has_value() != m_ghost.has_value() ||
	                     (ghost && (ghost->box.min != m_ghost->box.min || ghost->box.max != m_ghost->box.max || ghost->model != m_ghost->model));
	m_ghost = std::move(ghost);
	if (changed) {
		update();
	}
}

void Viewport::leaveEvent(QEvent* event) {
	updateGhost(nullptr);
	QOpenGLWidget::leaveEvent(event);
}

void Viewport::placeEntity(const QPointF& pos) {
	if (m_editor->currentEntityClass().isEmpty()) {
		emit m_editor->message(tr("Nothing armed: pick an entity class in the Tool panel first"));
		return;
	}
	placeEntityAt(pos, m_editor->currentEntityClass().toStdString());
}

std::optional<Vec3> Viewport::restingPlace(const QPointF& pos, const Bounds& local) const {
	const double grid = m_editor->snapGrid();
	Vec3 origin;
	if (m_camera.is2D()) {
		origin = snap(viewPlanePoint(pos), grid);
		origin[viewAxis()] = viewAxis() == 2 ? -local.min.z : 0;
		return origin;
	}
	const Ray ray = rayAt(pos);
	const auto hit = m_renderer.pick(ray);
	Vec3 point, normal{0, 0, 1};
	if (hit) {
		point = hit->point;
		normal = hit->normal;
	} else if (const auto p = rayPlane(ray, {0, 0, 0}, {0, 0, 1})) {
		point = *p;
	} else {
		return std::nullopt;
	}
	int axis = 0;
	for (int i = 1; i < 3; ++i) {
		if (std::abs(normal[i]) > std::abs(normal[axis])) {
			axis = i;
		}
	}
	origin = snap(point, grid);
	// Push the box out of the surface so it rests on it.
	origin[axis] = point[axis] - (normal[axis] > 0 ? local.min[axis] : local.max[axis]);
	return origin;
}

std::optional<Vec3> Viewport::pasteCenter(const Bounds& box) const {
	const QPointF pos = mapFromGlobal(QCursor::pos());
	if (!rect().contains(pos.toPoint()) || box.empty()) {
		return std::nullopt;
	}
	Bounds local;
	local.add(box.min - box.center());
	local.add(box.max - box.center());
	return restingPlace(pos, local);
}

void Viewport::placeEntityAt(const QPointF& pos, const std::string& classname, const std::vector<std::pair<std::string, std::string>>& keys) {
	const auto* catalog = m_editor->entities();
	const EntityClass* definition = catalog ? catalog->find(classname) : nullptr;
	if (definition && definition->isSolid()) {
		emit m_editor->message(tr("%1 is a brush entity: select brushes and use Tie to Entity (Ctrl+T)").arg(QString::fromStdString(classname)));
		return;
	}
	// Size of the entity box relative to its origin.
	Entity probe;
	probe.set("classname", classname);
	for (const auto& [key, value] : keys) {
		probe.set(key, value);
	}
	probe.setOrigin({0, 0, 0});
	const Bounds local = edit::entityBounds(probe, catalog);
	const auto rest = restingPlace(pos, local);
	if (!rest) {
		return;
	}
	const Vec3 origin = *rest;
	auto finalKeys = keys;
	const bool hasAngles = definition && definition->property("angles");
	const bool anglesGiven = std::any_of(keys.begin(), keys.end(), [](const auto& kv) { return kv.first == "angles"; });
	if (!m_camera.is2D() && m_editor->randomYaw() && hasAngles && !anglesGiven) {
		// Random yaw: props and debris do not all face the same way.
		finalKeys.emplace_back("angles", "0 " + std::to_string(QRandomGenerator::global()->bounded(360)) + " 0");
	}
	auto [change, id] = edit::placeEntity(m_editor->doc(), classname, origin, definition, finalKeys);
	m_editor->commit(std::move(change));
	Selection selection;
	selection.entities.push_back(id);
	m_editor->setSelection(std::move(selection));
}

void Viewport::beginGizmoDrag(Handle handle, const QPointF& pos) {
	m_drag = Drag::Gizmo;
	m_dragHandle = handle;
	m_dragBounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
	m_dragCenter = m_dragBounds.center();
	if (handle >= Handle::RingX && handle <= Handle::RingZ) {
		m_dragCenter = snap(m_dragCenter, m_editor->snapGrid() / 2);   // pivot on the grid keeps brushes on the grid
	}
	m_dragDelta = {};
	m_dragAngle = 0;
	m_dragNewBounds = m_dragBounds;
	m_previewModel.setToIdentity();
	const Ray ray = rayAt(pos);
	if (handle >= Handle::AxisX && handle <= Handle::AxisZ) {
		m_dragStartParam = closestOnAxis(m_dragCenter, axisVector(static_cast<int>(handle) - static_cast<int>(Handle::AxisX)), ray);
	} else if (handle >= Handle::ScaleMinX && handle <= Handle::ScaleMaxZ) {
		const int axis = (static_cast<int>(handle) - static_cast<int>(Handle::ScaleMinX)) / 2;
		m_dragStartParam = closestOnAxis(m_dragCenter, axisVector(axis), ray);
	} else if (handle >= Handle::PlaneXY && handle <= Handle::PlaneYZ) {
		const int normal = handle == Handle::PlaneXY ? 2 : (handle == Handle::PlaneXZ ? 1 : 0);
		m_dragStartPoint = rayPlane(ray, m_dragCenter, axisVector(normal)).value_or(m_dragCenter);
	} else if (handle >= Handle::RingX && handle <= Handle::RingZ) {
		const int axis = static_cast<int>(handle) - static_cast<int>(Handle::RingX);
		m_dragStartPoint = rayPlane(ray, m_dragCenter, axisVector(axis)).value_or(m_dragCenter);
		if (m_camera.is2D()) {
			m_dragStartPoint = viewPlanePoint(pos);
			m_dragStartPoint[axis] = m_dragCenter[axis];
		}
	} else if (handle == Handle::Move2D) {
		m_dragStartPoint = viewPlanePoint(pos);
	} else if (handle >= Handle::RoomMinX && handle <= Handle::RoomMaxZ) {
		const int axis = (static_cast<int>(handle) - static_cast<int>(Handle::RoomMinX)) / 2;
		m_dragBounds = m_dragNewBounds = selectedRoomBox().value_or(m_dragBounds);
		m_dragCenter = m_dragBounds.center();
		m_dragStartParam = closestOnAxis(m_dragCenter, axisVector(axis), ray);
	} else if (handle >= Handle::RadiusMinX && handle <= Handle::RadiusMaxZ) {
		const int axis = (static_cast<int>(handle) - static_cast<int>(Handle::RadiusMinX)) / 2;
		if (const Entity* scape = selectedSoundscapeSphere()) {
			m_dragCenter = scape->origin();
			m_dragStartRadius = m_dragRadius = soundscapeRadius(*scape);
		}
		m_dragStartParam = closestOnAxis(m_dragCenter, axisVector(axis), ray);
	} else if (handle == Handle::Corner) {
		// The corner under the cursor, and every corner it could snap to.
		double best = 1e9;
		for (const Vec3& corner : selectionCorners()) {
			QPointF q;
			if (m_camera.project(corner, width(), height(), q) && QLineF(q, pos).length() < best) {
				best = QLineF(q, pos).length();
				m_cornerStart = corner;
			}
		}
		m_snapTargets.clear();
		m_snapHit.reset();
		const auto& doc = m_editor->doc();
		const auto& selection = m_editor->selection();
		const auto& hidden = m_editor->hidden();
		const auto isSelected = [&](const SolidRef& ref) {
			return std::find(selection.solids.begin(), selection.solids.end(), ref) != selection.solids.end() ||
			       std::find(selection.entities.begin(), selection.entities.end(), ref.entityId) != selection.entities.end() ||
			       std::find(hidden.solids.begin(), hidden.solids.end(), ref) != hidden.solids.end();
		};
		const auto addSolid = [&](const Solid& solid, int entityId) {
			if (!isSelected({entityId, solid.id})) {
				const auto corners = solidCorners(solid);
				m_snapTargets.insert(m_snapTargets.end(), corners.begin(), corners.end());
			}
		};
		for (const auto& solid : doc.world.solids) {
			addSolid(solid, 0);
		}
		for (const auto& entity : doc.entities) {
			for (const auto& solid : entity.solids) {
				addSolid(solid, entity.id);
			}
			const bool skip = std::find(selection.entities.begin(), selection.entities.end(), entity.id) != selection.entities.end() ||
			                  std::find(hidden.entities.begin(), hidden.entities.end(), entity.id) != hidden.entities.end();
			if (entity.solids.empty() && !skip) {
				const Bounds box = edit::entityBounds(entity, m_editor->entities());   // props: their model's box
				for (int i = 0; i < 8; ++i) {
					m_snapTargets.push_back({(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z});
				}
			}
		}
	}
	update();
}

void Viewport::updateGizmoDrag(const QPointF& pos) {
	const Ray ray = rayAt(pos);
	const double grid = m_editor->snapGrid();
	const double minimumSize = std::max(grid, 1.0);
	const auto handle = m_dragHandle;
	m_previewModel.setToIdentity();
	if (handle >= Handle::AxisX && handle <= Handle::AxisZ) {
		const int axis = static_cast<int>(handle) - static_cast<int>(Handle::AxisX);
		const double moved = m_camera.is2D() ? viewPlanePoint(pos)[axis] - viewPlanePoint(m_pressPos)[axis]
		                                     : closestOnAxis(m_dragCenter, axisVector(axis), ray) - m_dragStartParam;
		m_dragDelta = {};
		m_dragDelta[axis] = snap(moved, grid);
	} else if (handle >= Handle::PlaneXY && handle <= Handle::PlaneYZ) {
		const int normal = handle == Handle::PlaneXY ? 2 : (handle == Handle::PlaneXZ ? 1 : 0);
		if (const auto p = rayPlane(ray, m_dragCenter, axisVector(normal))) {
			m_dragDelta = snap(*p - m_dragStartPoint, grid);
			m_dragDelta[normal] = 0;
		}
	} else if (handle == Handle::Move2D) {
		m_dragDelta = snap(viewPlanePoint(pos) - m_dragStartPoint, grid);
		m_dragDelta[viewAxis()] = 0;
	} else if (handle >= Handle::RoomMinX && handle <= Handle::RoomMaxZ) {
		const int index = static_cast<int>(handle) - static_cast<int>(Handle::RoomMinX);
		const int axis = index / 2;
		const double t = m_camera.is2D() ? viewPlanePoint(pos)[axis] - viewPlanePoint(m_pressPos)[axis]
		                                 : closestOnAxis(m_dragCenter, axisVector(axis), ray) - m_dragStartParam;
		m_dragNewBounds = m_dragBounds;
		if (index % 2 == 1) {
			m_dragNewBounds.max[axis] = std::max(m_dragBounds.min[axis] + minimumSize, snap(m_dragBounds.max[axis] + t, grid));
		} else {
			m_dragNewBounds.min[axis] = std::min(m_dragBounds.max[axis] - minimumSize, snap(m_dragBounds.min[axis] + t, grid));
		}
		update();
		return;
	} else if (handle >= Handle::RadiusMinX && handle <= Handle::RadiusMaxZ) {
		// The square follows the mouse along its axis; the radius grows outward on both sides.
		const int index = static_cast<int>(handle) - static_cast<int>(Handle::RadiusMinX);
		const int axis = index / 2;
		const double t = m_camera.is2D() ? viewPlanePoint(pos)[axis] - viewPlanePoint(m_pressPos)[axis]
		                                 : closestOnAxis(m_dragCenter, axisVector(axis), ray) - m_dragStartParam;
		const double outward = index % 2 == 1 ? t : -t;
		m_dragRadius = std::max(minimumSize, snap(m_dragStartRadius + outward, grid));
		update();
		return;
	} else if (handle == Handle::Corner) {
		// Nearest corner of another object on screen wins; otherwise the corner follows the
		// cursor on the grid (2D: in the view plane, 3D: on the floor plane through it, or a wall
		// plane when looking flat across the map).
		constexpr double SNAP_PIXELS = 14;
		m_snapHit.reset();
		double best = SNAP_PIXELS;
		for (const Vec3& target : m_snapTargets) {
			QPointF q;
			if (m_camera.project(target, width(), height(), q)) {
				const double d = QLineF(q, pos).length();
				if (d < best) {
					best = d;
					m_snapHit = target;
				}
			}
		}
		if (m_snapHit) {
			m_dragDelta = *m_snapHit - m_cornerStart;
		} else if (m_camera.is2D()) {
			Vec3 p = snap(viewPlanePoint(pos), grid);
			p[viewAxis()] = m_cornerStart[viewAxis()];
			m_dragDelta = p - m_cornerStart;
		} else {
			const Vec3 forward = fromQt(m_camera.forward());
			Vec3 normal{0, 0, 1};
			if (std::abs(forward.z) < 0.35) {
				normal = std::abs(forward.x) > std::abs(forward.y) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
			}
			if (const auto p = rayPlane(ray, m_cornerStart, normal)) {
				m_dragDelta = snap(*p, grid) - m_cornerStart;
				for (int a = 0; a < 3; ++a) {
					if (normal[a] != 0) m_dragDelta[a] = 0;
				}
			}
		}
	} else if (handle >= Handle::RingX && handle <= Handle::RingZ) {
		const int axis = static_cast<int>(handle) - static_cast<int>(Handle::RingX);
		std::optional<Vec3> p = m_camera.is2D() ? std::optional<Vec3>(viewPlanePoint(pos)) : rayPlane(ray, m_dragCenter, axisVector(axis));
		if (p) {
			Vec3 a = m_dragStartPoint - m_dragCenter, b = *p - m_dragCenter;
			a[axis] = 0;
			b[axis] = 0;
			const double angle = std::atan2(dot(axisVector(axis), cross(a, b)), dot(a, b)) * 180 / M_PI;
			// Rotation snap like the grid (Shift+W turns both off); Ctrl gives 1° steps.
			m_dragAngle = (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) ? std::round(angle) : m_editor->snapAngle(angle);
			QVector3D axisQt(0, 0, 0);
			axisQt[axis] = 1;
			m_previewModel.translate(toQt(m_dragCenter));
			m_previewModel.rotate(float(m_dragAngle), axisQt);
			m_previewModel.translate(-toQt(m_dragCenter));
		}
		update();
		return;
	} else if (handle >= Handle::ScaleMinX && handle <= Handle::ScaleMaxZ) {
		const int index = static_cast<int>(handle) - static_cast<int>(Handle::ScaleMinX);
		const int axis = index / 2;
		const bool isMax = index % 2 == 1;
		double t = closestOnAxis(m_dragCenter, axisVector(axis), ray) - m_dragStartParam;
		if (m_camera.is2D()) {
			t = viewPlanePoint(pos)[axis] - viewPlanePoint(m_pressPos)[axis];
		}
		m_dragNewBounds = m_dragBounds;
		if (isMax) {
			m_dragNewBounds.max[axis] = std::max(m_dragBounds.min[axis] + minimumSize, snap(m_dragBounds.max[axis] + t, grid));
		} else {
			m_dragNewBounds.min[axis] = std::min(m_dragBounds.max[axis] - minimumSize, snap(m_dragBounds.min[axis] + t, grid));
		}
		if (!(m_previewBounds.min == m_dragNewBounds.min && m_previewBounds.max == m_dragNewBounds.max)) {
			m_previewBounds = m_dragNewBounds;
			std::vector<Solid> solids;
			const auto add = [&](const Solid& solid) {
				Solid copy = solid;
				if (scaleSolid(copy, m_dragBounds, m_dragNewBounds) && isConvexValid(copy)) {
					solids.push_back(std::move(copy));
				}
			};
			const auto& doc = m_editor->doc();
			for (const auto& ref : m_editor->selection().solids) {
				if (const auto* solid = doc.findSolid(ref)) add(*solid);
			}
			for (const int id : m_editor->selection().entities) {
				if (const auto* entity = doc.findEntity(id)) {
					for (const auto& solid : entity->solids) add(solid);
				}
			}
			setPreviewSolids(solids);
		}
		const Vec3 from = m_dragBounds.size(), to = m_dragNewBounds.size();
		m_previewModel.translate(toQt(m_dragNewBounds.min));
		m_previewModel.scale(float(to.x / std::max(from.x, 1e-6)), float(to.y / std::max(from.y, 1e-6)), float(to.z / std::max(from.z, 1e-6)));
		m_previewModel.translate(-toQt(m_dragBounds.min));
		update();
		return;
	}
	m_previewModel.translate(toQt(m_dragDelta));
	update();
}

bool Viewport::handleFacesCamera(const Vec3& point, int axis, int side) const {
	if (m_camera.is2D()) {
		return true;
	}
	const double toCamera = double(m_camera.position[axis]) - point[axis];
	return side == 0 ? toCamera < 0 : toCamera > 0;
}

bool Viewport::resizeHandles() const {
	const auto tool = m_editor->tool();
	if (tool == Editor::Tool::Scale) {
		return true;
	}
	// Select tool: squares on the sides of selected brushes resize them right away (Hammer's 2D
	// views, Source 2's bounding box handles) - no need to switch to Scale.
	if (tool != Editor::Tool::Select || m_editor->selectMode() != Editor::SelectMode::Object) {
		return false;
	}
	const auto& selection = m_editor->selection();
	if (!selection.solids.empty()) {
		return true;
	}
	for (const int id : selection.entities) {
		if (const auto* entity = m_editor->doc().findEntity(id); entity && !entity->solids.empty()) {
			return true;
		}
	}
	return false;
}

bool Viewport::scaling() const {
	return m_drag == Drag::Gizmo && m_dragHandle >= Handle::ScaleMinX && m_dragHandle <= Handle::ScaleMaxZ;
}

void Viewport::setPreviewSolids(const std::vector<Solid>& solids) {
	if (!m_glReady) {
		return;
	}
	makeCurrent();
	m_renderer.setPreviewSolids(solids);
	doneCurrent();
}

void Viewport::finishGizmoDrag(bool clone) {
	const auto handle = m_dragHandle;
	m_previewBounds = {};
	setPreviewSolids({});
	m_drag = Drag::None;
	m_dragHandle = Handle::None;
	auto& doc = m_editor->doc();
	const auto& selection = m_editor->selection();
	m_snapTargets.clear();
	m_snapHit.reset();
	if ((handle >= Handle::AxisX && handle <= Handle::PlaneYZ) || handle == Handle::Move2D || handle == Handle::Corner) {
		if (length(m_dragDelta) > 0) {
			if (clone) {
				auto [change, copies] = edit::duplicate(doc, selection, m_dragDelta);
				m_editor->commit(std::move(change));
				m_editor->setSelection(std::move(copies));
			} else {
				m_editor->commit(edit::translate(doc, selection, m_dragDelta, m_editor->textureLock()));
			}
		}
	} else if (handle >= Handle::RoomMinX && handle <= Handle::RoomMaxZ) {
		int cubemapId = 0;
		if (selectedRoomBox(&cubemapId) && !(m_dragNewBounds.min == m_dragBounds.min && m_dragNewBounds.max == m_dragBounds.max)) {
			m_editor->setCubemapRoomBox(cubemapId, m_dragNewBounds);
		}
	} else if (handle >= Handle::RadiusMinX && handle <= Handle::RadiusMaxZ) {
		// One undo step for the whole drag.
		if (std::abs(m_dragRadius - m_dragStartRadius) > 1e-6 && selection.entities.size() == 1) {
			m_editor->commit(edit::setSoundscapeRadius(doc, selection.entities.front(), m_dragRadius));
		}
	} else if (handle >= Handle::RingX && handle <= Handle::RingZ) {
		if (std::abs(m_dragAngle) > 1e-6) {
			const int axis = static_cast<int>(handle) - static_cast<int>(Handle::RingX);
			m_editor->commit(edit::rotate(doc, selection, Mat3::rotation(axis, m_dragAngle), m_dragCenter, m_editor->textureLock()));
		}
	} else if (handle >= Handle::ScaleMinX && handle <= Handle::ScaleMaxZ) {
		if (!(m_dragNewBounds.min == m_dragBounds.min && m_dragNewBounds.max == m_dragBounds.max)) {
			m_editor->commit(edit::scale(doc, selection, m_dragBounds, m_dragNewBounds));
		}
	}
	m_dragDelta = {};
	m_dragAngle = 0;
	update();
}

void Viewport::updateShapePreview() {
	// The shape itself (not just its box) while dragging, for everything but plain blocks.
	if (m_editor->shapeOptions().kind == ShapeOptions::Kind::Block || m_blockBox.empty()) {
		setPreviewSolids({});
		return;
	}
	MapDocument scratch;
	setPreviewSolids(makeShape(scratch, m_blockBox, m_editor->shapeOptions(), m_editor->currentMaterial().toStdString()));
}

void Viewport::updateBlockDrag(const QPointF& pos) {
	const double grid = m_editor->snapGrid();
	if (m_camera.is2D()) {
		m_blockEnd = snap(viewPlanePoint(pos), grid);
		const int axis = viewAxis();
		m_blockBox = {};
		Vec3 a = m_blockStart, b = m_blockEnd;
		a[axis] = m_lastBlockDepth.min[axis];
		b[axis] = m_lastBlockDepth.max[axis];
		m_blockBox.add(a);
		m_blockBox.add(b);
	} else {
		if (const auto p = rayPlane(rayAt(pos), {0, 0, m_blockPlane}, {0, 0, 1})) {
			m_blockEnd = snap(*p, grid);
			m_blockEnd.z = m_blockPlane;
		}
		m_blockBox = {};
		m_blockBox.add(m_blockStart);
		m_blockBox.add(m_blockEnd + Vec3{0, 0, m_blockHeight});
		if (m_blockHeight < 0) {
			m_blockBox = {};
			m_blockBox.add(m_blockStart + Vec3{0, 0, m_blockHeight});
			m_blockBox.add(m_blockEnd);
		}
	}
	updateShapePreview();
	update();
}

void Viewport::createBlockFromDrag() {
	m_drag = Drag::None;
	const Vec3 size = m_blockBox.size();
	if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
		update();
		return;
	}
	m_lastBlockDepth = m_blockBox;
	setPreviewSolids({});
	if (m_blockForEntity && m_editor->armedBrushEntity()) {
		m_editor->createBrushEntity(m_blockBox);   // trigger, func_door, ... from the entity tool
	} else {
		m_editor->createShape(m_blockBox);   // block, wedge, cylinder, arch, ... (Tool panel)
	}
	update();
}

bool Viewport::event(QEvent* event) {
	// While looking around, letter keys fly the camera instead of switching tools.
	if (event->type() == QEvent::ShortcutOverride && m_looking) {
		event->accept();
		return true;
	}
	// Tab cycles the clip tool's mode (Qt would move the focus otherwise).
	if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Tab && m_editor->tool() == Editor::Tool::Clip) {
		return toolKey(static_cast<QKeyEvent*>(event));
	}
	// Enter and Backspace belong to the tools while they collect points.
	if (event->type() == QEvent::ShortcutOverride && (m_editor->tool() == Editor::Tool::Clip || m_editor->tool() == Editor::Tool::Polygon)) {
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Backspace || key == Qt::Key_Tab) {
			event->accept();
			return true;
		}
	}
	return QOpenGLWidget::event(event);
}

void Viewport::pickMaterial(const QPointF& pos) {
	const auto hit = m_renderer.pick(rayAt(pos), true);
	if (!hit) {
		return;
	}
	if (const auto* solid = m_editor->doc().findSolid(hit->solid)) {
		for (const auto& side : solid->sides) {
			if (side.id == hit->sideId) {
				m_editor->setCurrentMaterial(QString::fromStdString(side.material));
				emit m_editor->message(tr("Material picked: %1 (Shift+T applies it)").arg(QString::fromStdString(side.material)));
			}
		}
	}
}

void Viewport::toggleFlyMode() {
	if (m_camera.is2D()) {
		return;
	}
	m_flyMode = !m_flyMode;
	if (m_flyMode) {
		const QPointF center(width() / 2.0, height() / 2.0);
		QCursor::setPos(mapToGlobal(center.toPoint()));
		beginLook(center);
	} else {
		endLook();
	}
}

void Viewport::requestContextMenu(const QPointF& pos) {
	// Like Hammer: the menu works on what was clicked; an unselected object becomes the selection.
	const bool faceMode = m_editor->selectMode() == Editor::SelectMode::Face;
	if (!m_camera.is2D()) {
		if (const auto hit = m_renderer.pick(rayAt(pos), faceMode)) {
			const auto& selection = m_editor->selection();
			const bool selected = faceMode ? selection.contains(FaceRef{hit->solid, hit->sideId})
			                               : (hit->entityId != 0 ? selection.containsEntity(hit->entityId) : selection.contains(hit->solid));
			if (!selected) {
				clickSelect(pos, {});
			}
		}
	}
	emit contextMenuRequested(mapToGlobal(pos.toPoint()));
}

void Viewport::beginLook(const QPointF& anchor) {
	m_looking = true;
	m_lookAnchor = mapToGlobal(anchor.toPoint());
	m_lookLast = QCursor::pos();
	m_lookTravel = 0;
	m_lookFlew = false;
	m_lookClock.start();
	m_moveClock.restart();
	m_moveTimer.start();
	setCursor(Qt::BlankCursor);
	grabMouse();
}

void Viewport::endLook() {
	if (!m_looking) {
		return;
	}
	m_looking = false;
	m_flyMode = false;
	m_moveTimer.stop();
	releaseMouse();
	unsetCursor();
	QCursor::setPos(m_lookAnchor);
	update();
}

void Viewport::applyLook() {
	// Always measured from the real cursor position (not from queued mouse events): events that
	// were already queued when the cursor was warped back would otherwise turn the camera twice
	// and make it jump.
	const QPoint now = QCursor::pos();
	const QPoint delta = now - m_lookLast;
	m_lookLast = now;
	if (delta.isNull()) {
		return;
	}
	m_lookTravel += delta.manhattanLength();
	m_camera.yaw -= float(delta.x() * 0.2);
	m_camera.pitch = std::clamp(m_camera.pitch - float(delta.y() * 0.2), -89.f, 89.f);
	// Park the cursor again before it reaches the screen edge so turning never stops.
	if ((now - m_lookAnchor).manhattanLength() > 64) {
		QCursor::setPos(m_lookAnchor);
		m_lookLast = QCursor::pos();   // where it really is (warping is not possible everywhere)
	}
	update();
}

void Viewport::cancelInteraction() {
	endLook();
	if (m_drag == Drag::BlockBase || m_drag == Drag::BlockHeight) {
		setPreviewSolids({});
	}
	if (scaling()) {
		m_previewBounds = {};
		setPreviewSolids({});
	}
	m_drag = Drag::None;
	m_dragHandle = Handle::None;
	m_dragDelta = {};
	m_dragAngle = 0;
	update();
}

// --- Keyboard ---------------------------------------------------------------------

void Viewport::keyPressEvent(QKeyEvent* event) {
	if (event->isAutoRepeat()) {
		return;
	}
	m_keys.insert(event->key());
	if (event->key() == Qt::Key_M && (event->modifiers() & Qt::ControlModifier)) {
		pickMaterial(mapFromGlobal(QCursor::pos()));   // Ctrl+M: material under the cursor (Source 2 Hammer)
		return;
	}
	if (event->key() == Qt::Key_Z && !(event->modifiers() & Qt::ControlModifier)) {
		toggleFlyMode();
		return;
	}
	if (m_looking) {
		if (event->key() == Qt::Key_Escape) {
			endLook();
		}
		return; // WASD/QE fly while looking; tool shortcuts are off
	}
	if (toolKey(event)) {
		return;
	}
	if (event->key() == Qt::Key_Escape) {
		if (m_drag != Drag::None) {
			cancelInteraction();
		} else {
			m_editor->clearSelection();
		}
		return;
	}
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_drag == Drag::BlockHeight) {
		createBlockFromDrag();
		return;
	}
	QOpenGLWidget::keyPressEvent(event);
}

void Viewport::keyReleaseEvent(QKeyEvent* event) {
	if (!event->isAutoRepeat()) {
		m_keys.remove(event->key());
	}
	QOpenGLWidget::keyReleaseEvent(event);
}

void Viewport::focusOutEvent(QFocusEvent* event) {
	m_keys.clear();
	endLook();
	QOpenGLWidget::focusOutEvent(event);
}

// --- Drag and drop from the asset browser -------------------------------------------

void Viewport::dragEnterEvent(QDragEnterEvent* event) {
	for (const char* format : {"application/x-sme-material", "application/x-sme-entity", "application/x-sme-model", "application/x-sme-sound", "application/x-sme-particle"}) {
		if (event->mimeData()->hasFormat(format)) {
			event->acceptProposedAction();
			return;
		}
	}
}

void Viewport::dragMoveEvent(QDragMoveEvent* event) {
	if (event->mimeData()->hasFormat("application/x-sme-material")) {
		// Show which face the material would land on.
		const auto hit = m_renderer.pick(rayAt(event->position()), true);
		setHoverSide(hit ? hit->sideId : 0);
	}
	event->acceptProposedAction();
}

void Viewport::dragLeaveEvent(QDragLeaveEvent* event) {
	setHoverSide(0);
	QOpenGLWidget::dragLeaveEvent(event);
}

void Viewport::dropEvent(QDropEvent* event) {
	setHoverSide(0);
	const auto* mime = event->mimeData();
	if (mime->hasFormat("application/x-sme-entity")) {
		m_editor->setCurrentEntityClass(QString::fromUtf8(mime->data("application/x-sme-entity")));
		placeEntity(event->position());
		event->acceptProposedAction();
		return;
	}
	if (mime->hasFormat("application/x-sme-model")) {
		// A model becomes a prop_static standing on the surface (change the class in Object Properties).
		placeEntityAt(event->position(), "prop_static", {{"model", QString::fromUtf8(mime->data("application/x-sme-model")).toStdString()}});
		event->acceptProposedAction();
		return;
	}
	if (mime->hasFormat("application/x-sme-particle")) {
		placeEntityAt(event->position(), "info_particle_system",
		              {{"effect_name", QString::fromUtf8(mime->data("application/x-sme-particle")).toStdString()}, {"start_active", "1"}});
		event->acceptProposedAction();
		return;
	}
	if (mime->hasFormat("application/x-sme-sound")) {
		// "<kind>:<name>" from the Sounds browser: 3 = soundscape, otherwise a sound.
		const QString payload = QString::fromUtf8(mime->data("application/x-sme-sound"));
		const QString name = payload.section(':', 1);
		if (payload.section(':', 0, 0).toInt() == 3) {
			placeEntityAt(event->position(), "env_soundscape", {{"soundscape", name.toStdString()}});
		} else {
			placeEntityAt(event->position(), "ambient_generic", {{"message", name.toStdString()}});
		}
		event->acceptProposedAction();
		return;
	}
	const QString material = QString::fromUtf8(mime->data("application/x-sme-material"));
	const auto hit = m_renderer.pick(rayAt(event->position()), true);
	if (!hit || material.isEmpty()) {
		return;
	}
	Selection target;
	const bool hitSelected = m_editor->selection().contains(FaceRef{hit->solid, hit->sideId}) || m_editor->selection().contains(hit->solid) ||
	                         m_editor->selection().containsEntity(hit->entityId);
	if (hitSelected) {
		target = m_editor->selection();       // dropped on the selection: apply to all of it
	} else if (event->modifiers() & Qt::ShiftModifier) {
		if (hit->entityId != 0) {
			target.entities.push_back(hit->entityId);
		} else {
			target.solids.push_back(hit->solid);
		}
	} else {
		target.faces.push_back({hit->solid, hit->sideId});
	}
	m_editor->commit(edit::applyMaterial(m_editor->doc(), target, material.toUpper().toStdString()));
	m_editor->setCurrentMaterial(material.toUpper());
	event->acceptProposedAction();
}

} // namespace sme::app
