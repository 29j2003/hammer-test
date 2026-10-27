// toggle_switch.cpp - On/off switch (see toggle_switch.h).

#include "toggle_switch.h"

#include <QPainter>

namespace sme::app {

ToggleSwitch::ToggleSwitch(QWidget* parent) : QCheckBox(parent) {
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
}

QSize ToggleSwitch::sizeHint() const {
	return {34, 18};
}

bool ToggleSwitch::hitButton(const QPoint& pos) const {
	return rect().contains(pos);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF track = QRectF(1, (height() - 16) / 2.0, 32, 16);
	const bool on = isChecked();
	const QColor accent = palette().color(QPalette::Highlight);
	QColor fill = on ? accent : QColor(58, 62, 69);
	if (!isEnabled()) fill = fill.darker(150);
	p.setPen(hasFocus() ? QPen(accent.lighter(130), 1) : QPen(on ? accent : QColor(92, 98, 108), 1));
	p.setBrush(fill);
	p.drawRoundedRect(track, 8, 8);
	const double knob = 12;
	const double x = on ? track.right() - knob - 2 : track.left() + 2;
	p.setPen(Qt::NoPen);
	p.setBrush(on ? QColor(26, 27, 30) : QColor(200, 204, 210));
	p.drawEllipse(QRectF(x, track.top() + 2, knob, knob));
}

} // namespace sme::app
