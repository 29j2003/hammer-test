// anim_timeline.cpp - The animation editor's timeline (see anim_timeline.h).

#include "anim_timeline.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace sme::app {

namespace {

constexpr int MARGIN = 14;      // left and right, so the first and last frame's keys fit
constexpr int RULER = 20;
constexpr int ROW = 24;
constexpr int LABEL = 78;       // row names on the left

} // namespace

AnimTimeline::AnimTimeline(QWidget* parent) : QWidget(parent) {
	setObjectName("animTimeline");
	setMouseTracking(false);
	setFocusPolicy(Qt::ClickFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setToolTip(tr("Click or drag to move through the animation - drag a key to move it (Ctrl: copy) - right-click a key for more"));
}

QSize AnimTimeline::sizeHint() const {
	return {600, RULER + 2 * ROW + 6};
}

QSize AnimTimeline::minimumSizeHint() const {
	return {240, RULER + 2 * ROW + 6};
}

void AnimTimeline::setFrameCount(int frames) {
	m_frames = std::max(1, frames);
	m_frame = std::clamp(m_frame, 0, m_frames - 1);
	update();
}

void AnimTimeline::setFrame(int frame) {
	frame = std::clamp(frame, 0, m_frames - 1);
	if (frame == m_frame) return;
	m_frame = frame;
	update();
}

void AnimTimeline::setKeys(std::map<int, KeyInterp> selected, std::vector<int> all) {
	m_selected = std::move(selected);
	m_all = std::move(all);
	update();
}

void AnimTimeline::setEditFrom(int frame) {
	if (frame == m_editFrom) return;
	m_editFrom = frame;
	update();
}

double AnimTimeline::frameX(int frame) const {
	const double left = LABEL + MARGIN, right = width() - MARGIN;
	if (m_frames <= 1) return left;
	return left + (right - left) * double(frame) / double(m_frames - 1);
}

int AnimTimeline::frameAt(double x) const {
	const double left = LABEL + MARGIN, right = width() - MARGIN;
	if (m_frames <= 1 || right <= left) return 0;
	return std::clamp(int(std::lround((x - left) / (right - left) * double(m_frames - 1))), 0, m_frames - 1);
}

int AnimTimeline::selectedRowY() const {
	return RULER + ROW / 2;
}

int AnimTimeline::allRowY() const {
	return RULER + ROW + ROW / 2;
}

std::pair<int, bool> AnimTimeline::keyAt(const QPoint& at) const {
	const auto nearest = [&](const auto& frames, auto frameOf) {
		int best = -1;
		double bestDistance = 7;
		for (const auto& entry : frames) {
			const int f = frameOf(entry);
			const double d = std::fabs(frameX(f) - at.x());
			if (d < bestDistance) {
				bestDistance = d;
				best = f;
			}
		}
		return best;
	};
	if (std::abs(at.y() - selectedRowY()) <= ROW / 2) return {nearest(m_selected, [](const auto& e) { return e.first; }), false};
	if (std::abs(at.y() - allRowY()) <= ROW / 2) return {nearest(m_all, [](int f) { return f; }), true};
	return {-1, false};
}

void AnimTimeline::paintEvent(QPaintEvent*) {
	QPainter p(this);
	const QPalette pal = palette();
	p.fillRect(rect(), pal.color(QPalette::Base));
	// Rows.
	p.fillRect(QRect(0, RULER, width(), ROW), pal.color(QPalette::AlternateBase));
	p.setPen(pal.color(QPalette::Mid));
	p.drawLine(0, RULER, width(), RULER);
	p.drawLine(0, RULER + ROW, width(), RULER + ROW);
	// Locked frames (before edit-from): they stay as the original.
	if (m_editFrom > 0 && m_frames > 1) {
		const double x = frameX(m_editFrom);
		p.fillRect(QRectF(0, RULER, x, height() - RULER), QColor(0, 0, 0, 70));
		p.setPen(QPen(QColor(120, 220, 160), 2));
		p.drawLine(QPointF(x, RULER), QPointF(x, height()));
	}
	p.drawLine(0, RULER + 2 * ROW, width(), RULER + 2 * ROW);
	p.setPen(pal.color(QPalette::Text));
	QFont small = font();
	small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1));
	p.setFont(small);
	p.drawText(QRect(6, RULER, LABEL - 8, ROW), Qt::AlignVCenter | Qt::AlignLeft, tr("Chosen"));
	p.drawText(QRect(6, RULER + ROW, LABEL - 8, ROW), Qt::AlignVCenter | Qt::AlignLeft, tr("All bones"));
	// Ruler: a tick per frame where there is room, numbers every few frames.
	const double perFrame = m_frames > 1 ? (frameX(m_frames - 1) - frameX(0)) / double(m_frames - 1) : 100;
	int every = 1;
	for (const int step : {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000}) {
		every = step;
		if (perFrame * step >= 34) break;
	}
	const int tick = perFrame >= 4 ? 1 : every;
	p.setPen(pal.color(QPalette::Mid));
	for (int f = 0; f < m_frames; f += tick) {
		const int x = int(frameX(f));
		p.drawLine(x, f % every == 0 ? 10 : 15, x, RULER);
	}
	p.setPen(pal.color(QPalette::Text));
	for (int f = 0; f < m_frames; f += every) {
		const int x = int(frameX(f));
		p.drawText(QRect(x - 20, 0, 40, 12), Qt::AlignCenter, QString::number(f));
	}
	p.setRenderHint(QPainter::Antialiasing);
	const auto drawKey = [&](int frame, int y, double size, KeyInterp interp, const QColor& color) {
		const double x = frameX(frame);
		p.setPen(QPen(QColor(20, 20, 24), 1));
		p.setBrush(color);
		if (interp == KeyInterp::Linear) {
			p.drawRect(QRectF(x - size * 0.75, y - size * 0.75, size * 1.5, size * 1.5));
		} else if (interp == KeyInterp::Step) {
			// A step: a square with a bar (the value holds, then cuts).
			p.drawRect(QRectF(x - size * 0.75, y - size * 0.75, size * 1.5, size * 1.5));
			p.drawLine(QPointF(x - size * 0.75, y), QPointF(x + size * 0.75, y));
		} else if (interp == KeyInterp::EaseIn || interp == KeyInterp::EaseOut) {
			// A triangle: flat side where the motion is slow.
			QPolygonF tri;
			const double s = interp == KeyInterp::EaseIn ? -1 : 1;
			tri << QPointF(x - s * size, y - size) << QPointF(x - s * size, y + size) << QPointF(x + s * size, y);
			p.drawPolygon(tri);
		} else {
			QPolygonF diamond;
			diamond << QPointF(x, y - size) << QPointF(x + size, y) << QPointF(x, y + size) << QPointF(x - size, y);
			p.drawPolygon(diamond);
		}
	};
	for (const int f : m_all) {
		if (!(m_drag == Drag::Key && m_dragAll && f == m_dragFrom)) drawKey(f, allRowY(), 4.5, KeyInterp::Smooth, QColor(160, 170, 185));
	}
	for (const auto& [f, interp] : m_selected) {
		if (!(m_drag == Drag::Key && !m_dragAll && f == m_dragFrom)) drawKey(f, selectedRowY(), 6, interp, f == m_frame ? QColor(255, 214, 90) : QColor(255, 150, 50));
	}
	if (m_drag == Drag::Key && m_dragTo >= 0) drawKey(m_dragTo, m_dragAll ? allRowY() : selectedRowY(), m_dragAll ? 4.5 : 6, KeyInterp::Smooth, QColor(255, 255, 255));
	// Playhead.
	const double x = frameX(m_frame);
	p.setPen(QPen(QColor(240, 80, 70), 2));
	p.drawLine(QPointF(x, 0), QPointF(x, height()));
}

void AnimTimeline::mousePressEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) return;
	const auto [key, all] = keyAt(event->pos());
	if (key >= 0) {
		m_drag = Drag::Key;
		m_dragFrom = key;
		m_dragTo = key;
		m_dragAll = all;
		if (key != m_frame) {
			m_frame = key;
			emit frameChanged(key);
		}
		update();
		return;
	}
	m_drag = Drag::Scrub;
	const int f = frameAt(event->position().x());
	if (f != m_frame) {
		m_frame = f;
		emit frameChanged(f);
	}
	update();
}

void AnimTimeline::mouseMoveEvent(QMouseEvent* event) {
	const int f = frameAt(event->position().x());
	if (m_drag == Drag::Scrub && f != m_frame) {
		m_frame = f;
		emit frameChanged(f);
		update();
	} else if (m_drag == Drag::Key && f != m_dragTo) {
		m_dragTo = f;
		update();
	}
}

void AnimTimeline::mouseReleaseEvent(QMouseEvent* event) {
	if (m_drag == Drag::Key && m_dragTo >= 0 && m_dragTo != m_dragFrom) {
		const int from = m_dragFrom, to = m_dragTo;
		const bool all = m_dragAll;
		m_drag = Drag::None;
		m_frame = to;
		emit keyMoved(from, to, all, event->modifiers() & Qt::ControlModifier);
		emit frameChanged(to);
	}
	m_drag = Drag::None;
	m_dragFrom = m_dragTo = -1;
	update();
}

void AnimTimeline::contextMenuEvent(QContextMenuEvent* event) {
	const auto [key, all] = keyAt(event->pos());
	if (key < 0) return;
	if (key != m_frame) {
		m_frame = key;
		emit frameChanged(key);
		update();
	}
	emit keyMenuRequested(key, all, event->globalPos());
}

} // namespace sme::app
