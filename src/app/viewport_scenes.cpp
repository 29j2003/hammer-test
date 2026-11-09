// viewport_scenes.cpp - NPC scenes in the map view (see viewport.h and choreography.h).
//
// While an NPC with a scene, the scene entity or one of its point helpers is selected, every
// one of its scenes is drawn: the path each NPC walks (an arrow per Move step, in the actor's
// colour), a numbered marker with a short text for every step where it happens, and a line for
// Face steps. The step the Choreography panel shows is white. A ghost of the NPC shows its pose
// at that step (the animation at the panel's scrub time) or, during "Preview scene", every
// actor at the playing time. Nothing is drawn while no scene is selected, so normal frames pay
// nothing.
//
// What starts the scenes shown is drawn too: the trigger boxes of their starts (also while
// triggers are hidden), a dotted line from each box or entity that starts a scene to its NPC, and
// a label such as "enter → alyx_greet (if 2)".
//
// Clicks: while the panel asks for a point (Add walk, Place again) the next left click puts it
// on the floor under the mouse; otherwise a click on a marker shows that step in the panel, and
// for a Move/Face step selects its info_target so the gizmo moves it (with snapping and undo).

#include "viewport.h"

#include "scene_poser.h"

#include <QMouseEvent>

#include <cmath>

#include <sme/brush.h>
#include <sme/choreography.h>
#include <sme/text.h>

namespace sme::app {

namespace {

const QVector4D ACTOR_COLORS[] = {
	{0.35f, 0.85f, 1.0f, 1}, {1.0f, 0.6f, 0.25f, 1}, {0.55f, 1.0f, 0.45f, 1}, {1.0f, 0.45f, 0.75f, 1}, {0.85f, 0.8f, 0.35f, 1},
};
const QVector4D FOCUS_COLOR{1, 1, 1, 1};

QVector4D actorColor(const ChoreoScene& scene, const std::string& actor) {
	std::vector<std::string> actors;
	for (const auto& step : scene.steps) {
		if (std::find(actors.begin(), actors.end(), step.actor) == actors.end()) actors.push_back(step.actor);
	}
	const auto index = static_cast<std::size_t>(std::find(actors.begin(), actors.end(), actor) - actors.begin());
	return ACTOR_COLORS[index % std::size(ACTOR_COLORS)];
}

QColor toColor(const QVector4D& c) {
	return QColor::fromRgbF(c.x(), c.y(), c.z(), c.w());
}

void addLine(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

/// Where a step happens (its marker): the end of a walk, the point faced, else where the actor stands.
Vec3 stepAnchor(const ChoreoStep& step, const ChoreoTiming& timing) {
	switch (step.kind) {
		case ChoreoStep::Kind::Move: return timing.to;
		case ChoreoStep::Kind::Face: return step.point;
		default: return timing.from;
	}
}

QString shortened(const std::string& text, int length) {
	const QString value = QString::fromStdString(text);
	return value.size() > length ? value.left(length - 1) + QChar(0x2026) : value;
}

QString stepText(const ChoreoStep& step, std::size_t index, bool manyActors) {
	QString text;
	switch (step.kind) {
		case ChoreoStep::Kind::Move: text = step.run ? Viewport::tr("Run") : Viewport::tr("Walk"); break;
		case ChoreoStep::Kind::Animation:
			text = (step.gesture ? Viewport::tr("Gesture %1") : Viewport::tr("Anim %1")).arg(shortened(step.sequence, 22));
			break;
		case ChoreoStep::Kind::Say:
			text = step.caption.empty() ? Viewport::tr("Say %1").arg(shortened(step.sound, 24))
			                            : Viewport::tr("Say “%1”").arg(shortened(step.caption, 28));
			break;
		case ChoreoStep::Kind::Wait: text = Viewport::tr("Wait %1 s").arg(step.seconds); break;
		case ChoreoStep::Kind::Face: text = Viewport::tr("Face"); break;
		case ChoreoStep::Kind::SetVariable: text = Viewport::tr("Set %1").arg(shortened(step.variable, 22)); break;
	}
	QString prefix = QString::number(index + 1);
	if (step.withPrevious) prefix += QStringLiteral("+");
	if (manyActors && !step.actor.empty()) prefix += QStringLiteral(" ") + QString::fromStdString(step.actor);
	return prefix + QStringLiteral("  ") + text;
}

const QVector4D START_COLOR{1.0f, 0.82f, 0.3f, 1};

/// One way a shown scene starts, where it is in the map.
struct StartLink {
	int sceneId = 0;
	Vec3 from, to;           ///< The box or entity that starts it, and the scene's NPC (or marker)
	Bounds box;              ///< The trigger box (empty for entities)
	QString text;
};

std::vector<StartLink> startLinks(const MapDocument& doc, const std::vector<int>& scenes) {
	std::vector<StartLink> links;
	for (const int id : scenes) {
		const Entity* entity = doc.findEntity(id);
		if (!entity) continue;
		const ChoreoScene scene = readChoreoScene(doc, *entity);
		const auto actors = choreoActors(scene);
		const Entity* npc = actors.empty() ? nullptr : findNamedEntity(doc, actors.front());
		const Vec3 to = npc ? npc->origin() + Vec3{0, 0, 48} : entity->origin();
		const QString suffix = scene.conditions.empty() ? QString() : Viewport::tr(" (if %1)").arg(scene.conditions.size());
		for (std::size_t i = 0; i < scene.starts.size(); ++i) {
			const auto& start = scene.starts[i];
			const std::string number = std::to_string(i + 1);
			std::string source;
			QString word;
			switch (start.kind) {
				case ChoreoStart::Kind::Area: source = scene.name + "_area" + number; word = Viewport::tr("enter"); break;
				case ChoreoStart::Kind::LookAt: source = scene.name + "_look" + number; word = Viewport::tr("look"); break;
				case ChoreoStart::Kind::Near: source = scene.name + "_near" + number; word = Viewport::tr("near"); break;
				case ChoreoStart::Kind::Use: source = trim(start.target).empty() && !actors.empty() ? actors.front() : start.target; word = Viewport::tr("use"); break;
				case ChoreoStart::Kind::Output:
					source = trim(start.target).empty() && !actors.empty() ? actors.front() : start.target;
					word = QString::fromStdString(start.output);
					break;
				case ChoreoStart::Kind::AfterScene: source = start.target; word = Viewport::tr("after"); break;
				case ChoreoStart::Kind::MapStart:
				case ChoreoStart::Kind::Timer:
				case ChoreoStart::Kind::Input: continue;   // nowhere in particular
			}
			const Entity* from = findNamedEntity(doc, source);
			if (!from) continue;
			StartLink link;
			link.sceneId = id;
			link.to = to;
			for (const auto& solid : from->solids) link.box.add(buildBrushGeometry(solid).bounds);
			link.from = link.box.empty() ? from->origin() + Vec3{0, 0, 16} : link.box.center();
			link.text = word + QStringLiteral(" → ") + QString::fromStdString(scene.name) + suffix;
			links.push_back(std::move(link));
		}
	}
	return links;
}

} // namespace

std::vector<Viewport::SceneMarker> Viewport::sceneStartMarkers() const {
	std::vector<SceneMarker> markers;
	std::vector<std::pair<Vec3, int>> stacks;
	for (const auto& link : startLinks(m_editor->doc(), m_editor->visibleScenes())) {
		SceneMarker marker;
		marker.sceneId = link.sceneId;
		marker.anchor = link.from;
		if (!m_camera.project(link.from, width(), height(), marker.at)) continue;
		int row = 0;
		for (auto& [where, count] : stacks) {
			if (length(where - link.from) < 8) {
				row = count++;
				break;
			}
		}
		if (row == 0) stacks.emplace_back(link.from, 1);
		marker.text = link.text;
		marker.color = toColor(START_COLOR);
		marker.label = QRectF(marker.at.x() + 8, marker.at.y() - 8 + row * 16, 260, 16);
		markers.push_back(std::move(marker));
	}
	return markers;
}

std::vector<Viewport::SceneMarker> Viewport::sceneMarkers() const {
	std::vector<SceneMarker> markers;
	const auto& doc = m_editor->doc();
	const auto focus = m_editor->sceneFocus();
	for (const int id : m_editor->visibleScenes()) {
		const Entity* entity = doc.findEntity(id);
		if (!entity) continue;
		const ChoreoScene scene = readChoreoScene(doc, *entity);
		const auto timeline = choreoTimeline(doc, scene);
		bool manyActors = false;
		for (const auto& step : scene.steps) manyActors = manyActors || (!step.actor.empty() && step.actor != scene.steps.front().actor);
		// Steps at the same place stack their texts downwards.
		std::vector<std::pair<Vec3, int>> stacks;
		for (std::size_t i = 0; i < scene.steps.size() && i < timeline.size(); ++i) {
			const auto& step = scene.steps[i];
			if (step.kind == ChoreoStep::Kind::SetVariable) continue;   // happens nowhere
			SceneMarker marker;
			marker.sceneId = id;
			marker.step = int(i);
			marker.anchor = stepAnchor(step, timeline[i]);
			if (!m_camera.project(marker.anchor + Vec3{0, 0, 24}, width(), height(), marker.at)) continue;
			int row = 0;
			for (auto& [where, count] : stacks) {
				if (length(where - marker.anchor) < 8) {
					row = count++;
					break;
				}
			}
			if (row == 0) stacks.emplace_back(marker.anchor, 1);
			marker.text = stepText(step, i, manyActors);
			const bool focused = focus.sceneId == id && focus.step == int(i);
			marker.color = focused ? QColor(255, 255, 255) : toColor(actorColor(scene, step.actor));
			marker.label = QRectF(marker.at.x() + 8, marker.at.y() - 18 + row * 16, 260, 16);
			markers.push_back(std::move(marker));
		}
	}
	return markers;
}

void Viewport::drawSceneLines(std::vector<LineVertex>& lines) const {
	const auto& doc = m_editor->doc();
	const auto focus = m_editor->sceneFocus();
	for (const int id : m_editor->visibleScenes()) {
		const Entity* entity = doc.findEntity(id);
		if (!entity) continue;
		const ChoreoScene scene = readChoreoScene(doc, *entity);
		const auto timeline = choreoTimeline(doc, scene);
		for (std::size_t i = 0; i < scene.steps.size() && i < timeline.size(); ++i) {
			const auto& step = scene.steps[i];
			if (step.kind == ChoreoStep::Kind::SetVariable) continue;
			const auto& timing = timeline[i];
			const bool focused = focus.sceneId == id && focus.step == int(i);
			const QVector4D color = focused ? FOCUS_COLOR : actorColor(scene, step.actor);
			const Vec3 lift{0, 0, 3};
			if (step.kind == ChoreoStep::Kind::Move) {
				// The way there and an arrow head at its end, lying on the floor.
				const Vec3 a = timing.from + lift, b = timing.to + lift;
				const Vec3 d{b.x - a.x, b.y - a.y, 0};
				if (length(d) > 1) {
					if (step.run) {
						// Running: long dashes.
						const double total = length(b - a);
						for (double t = 0; t < total; t += 24) addLine(lines, a + (b - a) * (t / total), a + (b - a) * (std::min(total, t + 16) / total), color);
					} else {
						addLine(lines, a, b, color);
					}
					const Vec3 back = normalize(d) * -14.0;
					const Vec3 side = normalize(Vec3{-d.y, d.x, 0}) * 7.0;
					addLine(lines, b, b + back + side, color);
					addLine(lines, b, b + back - side, color);
				}
			} else if (step.kind == ChoreoStep::Kind::Face) {
				// From the eyes to the point faced, dotted.
				const Vec3 eye = timing.from + Vec3{0, 0, 64};
				const double total = length(step.point - eye);
				for (double t = 0; t < total; t += 12) addLine(lines, eye + (step.point - eye) * (t / total), eye + (step.point - eye) * (std::min(total, t + 6) / total), color);
			}
			// The marker: a post with a diamond on the floor.
			const Vec3 at = stepAnchor(step, timing);
			const double r = focused ? 10 : 6;
			addLine(lines, at + lift, at + Vec3{0, 0, 24}, color);
			const Vec3 corners[] = {at + Vec3{r, 0, 3}, at + Vec3{0, r, 3}, at + Vec3{-r, 0, 3}, at + Vec3{0, -r, 3}};
			for (int c = 0; c < 4; ++c) addLine(lines, corners[c], corners[(c + 1) % 4], color);
		}
	}
}

void Viewport::drawSceneStartLines(std::vector<LineVertex>& lines) const {
	for (const auto& link : startLinks(m_editor->doc(), m_editor->visibleScenes())) {
		if (!link.box.empty()) {
			// The box's twelve edges, slightly outside the brush so they show on top of it.
			const Vec3 a = link.box.min - Vec3{1, 1, 1}, b = link.box.max + Vec3{1, 1, 1};
			const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z}, {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
			for (int k = 0; k < 4; ++k) {
				addLine(lines, c[k], c[(k + 1) % 4], START_COLOR);
				addLine(lines, c[k + 4], c[(k + 1) % 4 + 4], START_COLOR);
				addLine(lines, c[k], c[k + 4], START_COLOR);
			}
		}
		// Dashes from what starts it to the NPC.
		const double total = length(link.to - link.from);
		for (double t = 0; t < total; t += 20) addLine(lines, link.from + (link.to - link.from) * (t / total), link.from + (link.to - link.from) * (std::min(total, t + 10) / total), START_COLOR);
	}
}

void Viewport::drawSceneLabels(OverlayRecorder& p) const {
	if (m_editor->scenePointPending()) {
		const QString hint = m_editor->scenePointHint().isEmpty() ? tr("Scene: click where the step should happen - Esc cancels") : m_editor->scenePointHint();
		p.drawText(QRectF(8, height() - 26, width() - 16, 20), hint, QColor(150, 230, 255));
	}
	for (const auto& marker : sceneMarkers()) {
		p.drawText(marker.label, marker.text, marker.color);
	}
	for (const auto& marker : sceneStartMarkers()) {
		p.drawText(marker.label, marker.text, marker.color);
	}
}

void Viewport::drawSceneGhosts(const SceneRenderer::Params& params) {
	const auto focus = m_editor->sceneFocus();
	if (focus.sceneId == 0 || (focus.step < 0 && focus.playTime < 0)) {
		return;
	}
	const auto visible = m_editor->visibleScenes();
	if (std::find(visible.begin(), visible.end(), focus.sceneId) == visible.end()) {
		return;   // nothing of the scene selected: the NPC just stands there
	}
	const auto& doc = m_editor->doc();
	const Entity* sceneEntity = doc.findEntity(focus.sceneId);
	if (!sceneEntity) {
		return;
	}
	const ChoreoScene scene = readChoreoScene(doc, *sceneEntity);
	const auto timeline = choreoTimeline(doc, scene);
	std::vector<std::string> actors;
	double time = focus.playTime;
	if (focus.playTime >= 0) {
		for (const auto& step : scene.steps) {
			if (!step.actor.empty() && std::find(actors.begin(), actors.end(), step.actor) == actors.end()) actors.push_back(step.actor);
		}
	} else if (focus.step < int(scene.steps.size()) && focus.step < int(timeline.size())) {
		const auto& timing = timeline[static_cast<std::size_t>(focus.step)];
		actors.push_back(scene.steps[static_cast<std::size_t>(focus.step)].actor);
		time = timing.start + std::clamp(focus.stepTime, 0.0, std::max(0.0, timing.end - timing.start - 1e-3));
	}
	ScenePoser* poser = m_editor->poser();
	for (const auto& actor : actors) {
		const Entity* npc = findNamedEntity(doc, actor);
		if (!npc) continue;
		const QString model = m_editor->entityModel(*npc);
		if (model.isEmpty()) continue;
		const ChoreoPose pose = choreoActorPose(doc, scene, timeline, actor, time);
		QString sequence;
		double seconds = time;
		if (pose.step >= 0) {
			const auto& step = scene.steps[static_cast<std::size_t>(pose.step)];
			seconds = pose.stepTime;
			if (step.kind == ChoreoStep::Kind::Animation) {
				sequence = QString::fromStdString(step.sequence);
			} else if (step.kind == ChoreoStep::Kind::Move) {
				sequence = poser->activitySequence(model, step.run ? "ACT_RUN" : "ACT_WALK");
			}
		}
		QMatrix4x4 transform;
		transform.translate(toQt(pose.position));
		transform.rotate(float(pose.yaw), 0, 0, 1);
		if (std::vector<ModelVertex> vertices; poser->pose(model, sequence, seconds, vertices)) {
			m_renderer.drawPosedGhost(params, model, vertices, transform, 0.8f);
		} else {
			m_renderer.drawGhostModel(params, model, transform, 0.8f);   // until the animations are read
		}
	}
}

std::optional<Vec3> Viewport::scenePointAt(const QPointF& pos) const {
	const double grid = m_editor->snapGrid();
	Vec3 point;
	if (m_camera.is2D()) {
		point = viewPlanePoint(pos);
		// Top view: onto the floor below a point high above it; other views keep their height.
		if (viewAxis() == 2) {
			const auto floor = m_renderer.pick(Ray{{point.x, point.y, 16384}, {0, 0, -1}}, true);
			point.z = floor ? floor->point.z : 0;
		}
	} else {
		const Ray ray = rayAt(pos);
		const auto hit = m_renderer.pick(ray, true);
		if (hit) {
			point = hit->point;
			if (hit->normal.z < 0.7) {
				// A wall or a ceiling: the floor below, a little away from it.
				const Vec3 start = hit->point + hit->normal * 16.0;
				const auto floor = m_renderer.pick(Ray{start, {0, 0, -1}}, true);
				point = floor ? floor->point : Vec3{start.x, start.y, 0};
			}
		} else if (const auto p = rayPlane(ray, {0, 0, 0}, {0, 0, 1})) {
			point = *p;
		} else {
			return std::nullopt;
		}
	}
	if (grid > 0) {
		point.x = snap(point.x, grid);
		point.y = snap(point.y, grid);
	}
	return point;
}

bool Viewport::sceneMousePress(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	if (m_editor->scenePointPending()) {
		if (const auto point = scenePointAt(event->position())) {
			m_editor->deliverScenePoint(*point);
		}
		return true;
	}
	if (event->modifiers() != Qt::NoModifier || m_editor->tool() != Editor::Tool::Select) {
		return false;
	}
	const QPointF pos = event->position();
	for (const auto& marker : sceneMarkers()) {
		if (QLineF(marker.at, pos).length() > 9 && !marker.label.adjusted(-2, -2, -150, 2).contains(pos)) continue;
		const Entity* sceneEntity = m_editor->doc().findEntity(marker.sceneId);
		if (!sceneEntity) continue;
		// Move/Face: select the point helper so the gizmo can move it.
		const std::string helper = choreoPointName(sceneEntity->get("targetname"), static_cast<std::size_t>(marker.step));
		if (const Entity* target = findNamedEntity(m_editor->doc(), helper)) {
			Selection selection;
			selection.entities.push_back(target->id);
			m_editor->setSelection(std::move(selection));
		}
		m_editor->setSceneFocus({marker.sceneId, marker.step, 0, -1});
		return true;
	}
	return false;
}

} // namespace sme::app
