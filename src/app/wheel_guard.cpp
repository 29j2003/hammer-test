// wheel_guard.cpp - The mouse wheel scrolls panels (see wheel_guard.h).

#include "wheel_guard.h"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QSlider>
#include <QTabBar>

namespace sme::app {

namespace {

const char* RESTING = "smeWheelWhenResting";
const char* HOVER_SINCE = "smeWheelHoverSince";
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

} // namespace

WheelGuard::WheelGuard(QObject* parent) : QObject(parent) {}

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
			if (watched->property(RESTING).toBool()) watched->setProperty(HOVER_SINCE, QDateTime::currentMSecsSinceEpoch());
			break;
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
				if (!area || widget->hasFocus()) break;   // outside panels, and focused fields: as usual
				if (widget->property(RESTING).toBool()) {
					const qint64 since = widget->property(HOVER_SINCE).toLongLong();
					const bool rested = since > 0 && QDateTime::currentMSecsSinceEpoch() - since >= REST_MS;
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
