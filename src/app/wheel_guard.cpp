// wheel_guard.cpp - The mouse wheel scrolls panels (see wheel_guard.h).

#include "wheel_guard.h"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSlider>
#include <QTabBar>

namespace sme::app {

namespace {

const char* RESTING = "smeWheelWhenResting";
constexpr qint64 REST_MS = 350;      ///< Mouse still on the field this long before the wheel may change it
constexpr qint64 SCROLLING_MS = 600; ///< The panel counts as being scrolled this long after a wheel step

/// The scroll area whose page holds `widget` (not crossing into another window), or nullptr.
QAbstractScrollArea* scrollAreaAround(QWidget* widget) {
	for (QWidget* child = widget, *parent = widget->parentWidget(); parent && !child->isWindow(); child = parent, parent = parent->parentWidget()) {
		if (auto* area = qobject_cast<QAbstractScrollArea*>(parent); area && child == area->viewport()) {
			return area;
		}
	}
	return nullptr;
}

/// Fields the wheel would change.
bool isValueField(QObject* object) {
	return qobject_cast<QComboBox*>(object) || qobject_cast<QAbstractSpinBox*>(object) || qobject_cast<QSlider*>(object);
}

/// Fields the wheel may change once the mouse has rested on them: number boxes and sliders always
/// (Julius: "the scroll wheel on the separate fields is really useful" - Scale, Shift, Rotation),
/// drop-downs only when marked with setWheelWhenResting() (a class or key must not change while
/// the panel is scrolled past it). A field can opt out with setWheelWhenResting(field, false).
bool wheelWhenResting(QObject* object) {
	const QVariant marked = object->property(RESTING);
	if (marked.isValid()) return marked.toBool();
	return qobject_cast<QAbstractSpinBox*>(object) || qobject_cast<QSlider*>(object);
}

/// The keyboard focus is on `field` or inside it (a number box's own line edit).
bool hasFocusWithin(const QWidget* field) {
	if (field->hasFocus()) return true;
	for (const QWidget* w = QApplication::focusWidget(); w; w = w->parentWidget()) {
		if (w == field) return true;
		if (w->isWindow()) break;
	}
	return false;
}

} // namespace

WheelGuard::WheelGuard(QObject* parent) : QObject(parent) {}

void WheelGuard::noteMouseAt(QPoint global, QObject* watched) {
	auto* widget = qobject_cast<QWidget*>(watched);
	if (global != m_restPos || m_restSince == 0) {
		m_restPos = global;
		m_restSince = QDateTime::currentMSecsSinceEpoch();
		m_restWidget = widget;
	} else if (widget && (!m_restWidget || m_restWidget->isAncestorOf(widget))) {
		m_restWidget = widget;   // same place, a deeper widget (Enter goes to the parents first)
	}
}

bool WheelGuard::restingOn(QWidget* field, QPoint global) {
	if (global != m_restPos) {
		// The mouse moved without telling (no mouse tracking there): it has not rested anywhere yet.
		m_restPos = global;
		m_restSince = 0;
		m_restWidget = nullptr;
		return false;
	}
	if (m_restSince == 0 || QDateTime::currentMSecsSinceEpoch() - m_restSince < REST_MS) return false;
	if (!m_restWidget) {
		// The field the mouse rested on is gone - a panel rebuilt after the last change put this
		// one in its place under the resting mouse: carry on with it.
		m_restWidget = field;
		return true;
	}
	// The mouse came to rest on this field (or inside it) - not on something the panel's
	// scrolling then slid it under.
	return m_restWidget == field || field->isAncestorOf(m_restWidget);
}

void WheelGuard::setWheelWhenResting(QWidget* field, bool on) {
	field->setProperty(RESTING, on);
	field->setAttribute(Qt::WA_Hover);
}

bool WheelGuard::eventFilter(QObject* watched, QEvent* event) {
	switch (event->type()) {
		case QEvent::Polish:
			// Qt gives these fields the focus by the wheel (WheelFocus) before any filter sees the
			// event; with StrongFocus only a click or Tab focuses them.
			if (isValueField(watched)) {
				auto* field = static_cast<QWidget*>(watched);
				if (field->focusPolicy() == Qt::WheelFocus) field->setFocusPolicy(Qt::StrongFocus);
			}
			break;
		case QEvent::Enter:
		case QEvent::HoverEnter:
		case QEvent::HoverMove:
		case QEvent::MouseMove: {
			// Where the mouse is and since when it stands there. Kept by position, not per field:
			// a panel that rebuilds after each change (Object Properties) puts a new field under the
			// resting mouse, and the wheel must keep working on it.
			QPointF global;
			if (event->type() == QEvent::Enter) global = static_cast<QEnterEvent*>(event)->globalPosition();
			else if (event->type() == QEvent::MouseMove) global = static_cast<QMouseEvent*>(event)->globalPosition();
			else global = static_cast<QHoverEvent*>(event)->globalPosition();
			noteMouseAt(global.toPoint(), watched);
			break;
		}
		case QEvent::Wheel: {
			auto* widget = qobject_cast<QWidget*>(watched);
			if (!widget) break;
			if (auto* area = qobject_cast<QAbstractScrollArea*>(widget->parentWidget()); area && area->viewport() == widget) {
				m_lastArea = area;   // the panel itself is scrolled
				m_lastScroll.start();
				break;
			}
			const bool tabs = qobject_cast<QTabBar*>(widget) != nullptr;
			if (!tabs && !isValueField(widget)) break;
			QAbstractScrollArea* area = scrollAreaAround(widget);
			if (!tabs) {
				if (!area || hasFocusWithin(widget)) break;   // outside panels, and focused fields: as usual
				if (wheelWhenResting(widget)) {
					const bool rested = restingOn(widget, static_cast<QWheelEvent*>(event)->globalPosition().toPoint());
					const bool scrolling = m_lastArea == area && m_lastScroll.isValid() && m_lastScroll.elapsed() < SCROLLING_MS;
					if (rested && !scrolling) break;
				}
			}
			if (area) QApplication::sendEvent(area->viewport(), event);   // the panel scrolls instead
			return true;   // tab bars never switch by the wheel
		}
		default:
			break;
	}
	return QObject::eventFilter(watched, event);
}

} // namespace sme::app
