// viewport_doors.cpp - How a selected door moves, drawn in the map view (see viewport.h).
//
// Swinging and prop doors: the hinge axis, an arc with an arrow for each way the door can turn
// (two when it opens away from the player) and a ghost of the door fully open. Sliding doors:
// an arrow to the open position and a ghost there. "Test open" in the Door box plays the
// motion once at the door's speed. Only the selected doors are drawn, so nothing is added to
// the normal frame.

#include "viewport.h"

#include "model_cache.h"

#include <QTimer>

#include <cmath>

#include <sme/doors.h>

namespace sme::app {

namespace {

const QVector4D DOOR_COLOR{1.0f, 0.62f, 0.15f, 1};
const QVector4D DOOR_SECOND{1.0f, 0.62f, 0.15f, 0.45f};
const QVector4D DOOR_GHOST{1.0f, 0.62f, 0.15f, 0.75f};
const QVector4D HINGE_COLOR{1.0f, 0.9f, 0.3f, 1};
const QVector4D TEST_COLOR{1, 1, 1, 1};

void addLine(std::vector<LineVertex>& out, const Vec3& a, const Vec3& b, const QVector4D& color) {
	out.push_back({toQt(a), color});
	out.push_back({toQt(b), color});
}

/// The 12 edges of a box given as 8 corners (bit 1: x, bit 2: y, bit 4: z, as doorMotion gives them).
void addCornerBox(std::vector<LineVertex>& out, const std::vector<Vec3>& corners, const QVector4D& color) {
	if (corners.size() != 8) return;
	for (int i = 0; i < 8; ++i) {
		for (const int bit : {1, 2, 4}) {
			if (!(i & bit)) addLine(out, corners[i], corners[i | bit], color);
		}
	}
}

/// An arrow head at `tip` pointing along `direction` (lying in the plane with `side`).
void addArrowHead(std::vector<LineVertex>& out, const Vec3& tip, const Vec3& direction, const Vec3& side, double size, const QVector4D& color) {
	const Vec3 back = normalize(direction) * -size;
	const Vec3 across = normalize(side) * (size * 0.55);
	addLine(out, tip, tip + back + across, color);
	addLine(out, tip, tip + back - across, color);
}

std::vector<Vec3> turned(const std::vector<Vec3>& corners, const Vec3& hinge, double degrees) {
	const Mat3 rotation = Mat3::rotation(2, degrees);
	std::vector<Vec3> out;
	for (const auto& c : corners) out.push_back(hinge + rotation * (c - hinge));
	return out;
}

std::vector<Vec3> moved(const std::vector<Vec3>& corners, const Vec3& offset) {
	std::vector<Vec3> out;
	for (const auto& c : corners) out.push_back(c + offset);
	return out;
}

} // namespace

std::optional<DoorMotion> Viewport::doorMotionOf(const Entity& door) const {
	if (doorType(door.classname()) == DoorType::Prop) {
		// The model's own box once it is loaded (its hinge side and size).
		if (const auto geometry = m_editor->models()->get(QString::fromStdString(door.get("model"))); geometry && !geometry->bounds.empty()) {
			return doorMotion(door, &geometry->bounds);
		}
	}
	return doorMotion(door);
}

void Viewport::drawDoorPreview(std::vector<LineVertex>& lines) const {
	const auto& selection = m_editor->selection();
	if (selection.entities.empty() || selection.entities.size() > 16) {
		return;
	}
	for (const int id : selection.entities) {
		const Entity* door = id == 0 ? nullptr : m_editor->doc().findEntity(id);
		if (!door || !isDoor(*door)) continue;
		const auto motion = doorMotionOf(*door);
		if (!motion) continue;
		const double size = std::clamp(length(motion->closed.size()) * 0.06, 4.0, 16.0);
		// Test open: the door at its current point of the motion, in white.
		double progress = -1;
		if (m_doorTest.id == id && m_doorTest.clock.isValid()) {
			const double t = m_doorTest.clock.elapsed() / 1000.0 / m_doorTest.seconds;
			progress = std::clamp(t, 0.0, 1.0);
			progress = progress * progress * (3 - 2 * progress);   // eased like the game's start and stop
		}
		if (motion->type == DoorType::Sliding) {
			if (length(motion->slide) < 1e-6) continue;
			const Vec3 from = motion->closed.center();
			const Vec3 to = from + motion->slide;
			addLine(lines, from, to, DOOR_COLOR);
			Vec3 side = cross(motion->slide, {0, 0, 1});
			if (length(side) < 1e-6) side = {1, 0, 0};
			addArrowHead(lines, to, motion->slide, side, size, DOOR_COLOR);
			addCornerBox(lines, moved(motion->corners, motion->slide), DOOR_GHOST);
			if (progress >= 0) addCornerBox(lines, moved(motion->corners, motion->slide * progress), TEST_COLOR);
			continue;
		}
		// The hinge: a vertical line where the door turns.
		const Vec3 hinge = motion->hinge;
		addLine(lines, {hinge.x, hinge.y, motion->closed.min.z}, {hinge.x, hinge.y, motion->closed.max.z}, HINGE_COLOR);
		const Vec3 tip = motion->leaf * motion->leafLength;
		for (std::size_t i = 0; i < motion->turns.size(); ++i) {
			const double turn = motion->turns[i];
			const QVector4D color = i == 0 ? DOOR_COLOR : DOOR_SECOND;
			// The arc the free edge travels, at the door's middle height, and an arrow at its end.
			constexpr int SEGMENTS = 24;
			Vec3 previous = hinge + tip;
			for (int s = 1; s <= SEGMENTS; ++s) {
				const Vec3 point = hinge + Mat3::rotation(2, turn * s / SEGMENTS) * tip;
				addLine(lines, previous, point, color);
				previous = point;
			}
			const Vec3 end = Mat3::rotation(2, turn) * motion->leaf;
			const Vec3 tangent = Vec3{-end.y, end.x, 0} * (turn >= 0 ? 1.0 : -1.0);
			addArrowHead(lines, previous, tangent, end, size, color);
			addLine(lines, hinge, previous, color);   // the open door's edge from the hinge
			addCornerBox(lines, turned(motion->corners, hinge, turn), i == 0 ? DOOR_GHOST : QVector4D(1.0f, 0.62f, 0.15f, 0.3f));
		}
		if (progress >= 0 && !motion->turns.empty()) {
			addCornerBox(lines, turned(motion->corners, hinge, motion->turns.front() * progress), TEST_COLOR);
		}
	}
}

void Viewport::startDoorTest(int doorId) {
	const Entity* door = m_editor->doc().findEntity(doorId);
	if (!door || !isDoor(*door)) {
		return;
	}
	const auto motion = doorMotionOf(*door);
	if (!motion) {
		return;
	}
	m_doorTest.id = doorId;
	m_doorTest.seconds = doorTravelTime(*motion, readDoor(*door).speed);
	m_doorTest.clock.start();
	requestFrame();
}

void Viewport::advanceDoorTest() {
	if (m_doorTest.id == 0) {
		return;
	}
	// Keep drawing while it moves and a moment after (the door holds open), then stop.
	if (m_doorTest.clock.elapsed() / 1000.0 > m_doorTest.seconds + 0.8) {
		m_doorTest.id = 0;
		QTimer::singleShot(0, this, &Viewport::requestFrame);
		return;
	}
	QTimer::singleShot(0, this, &Viewport::requestFrame);
}

} // namespace sme::app
