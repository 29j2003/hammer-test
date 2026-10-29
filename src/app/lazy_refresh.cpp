// lazy_refresh.cpp - Refresh when shown (see lazy_refresh.h).

#include "lazy_refresh.h"

#include <QEvent>
#include <QWidget>

namespace sme::app {

RefreshWhenShown::RefreshWhenShown(QWidget* widget, std::function<void()> refresh)
	: QObject(widget), m_widget(widget), m_refresh(std::move(refresh)) {
	widget->installEventFilter(this);
}

void RefreshWhenShown::request() {
	if (m_widget->isVisible()) {
		m_pending = false;
		m_refresh();
	} else {
		m_pending = true;
	}
}

bool RefreshWhenShown::eventFilter(QObject* watched, QEvent* event) {
	if (watched == m_widget && event->type() == QEvent::Show && m_pending) {
		m_pending = false;
		m_refresh();
	}
	return QObject::eventFilter(watched, event);
}

} // namespace sme::app
