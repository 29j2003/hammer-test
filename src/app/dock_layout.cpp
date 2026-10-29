// dock_layout.cpp - Floating panels, homes and the saved layout (see dock_layout.h).

#include "dock_layout.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

namespace sme::app {

namespace {

constexpr int STATE_VERSION = 1;   ///< saveState() version: a layout of another version is not restored
const char* STATE_KEY = "window/state";
const char* HOME_KEY = "window/dockHome/";

/// Window flags of a floating panel: a normal window with frame, minimise, maximise and close.
constexpr Qt::WindowFlags REAL_WINDOW = Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                        Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint;

} // namespace

DockLayout::DockLayout(QMainWindow* window) : QObject(window), m_window(window) {}

void DockLayout::addDock(QDockWidget* dock) {
	m_docks << dock;
	// Header: the name and a menu button (the menu also opens with a right click on it).
	auto* header = new QWidget(dock);
	header->setObjectName("dockHeader");
	header->setAttribute(Qt::WA_StyledBackground);
	auto* row = new QHBoxLayout(header);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);
	auto* title = new QLabel(dock->windowTitle());
	title->setObjectName("dockTitle");
	row->addWidget(title, 1);
	auto* button = new QToolButton;
	button->setObjectName("dockMenuButton");
	button->setText(QStringLiteral("⋯"));
	button->setAutoRaise(true);
	button->setToolTip(tr("Float as window, reattach, original position"));
	connect(button, &QToolButton::clicked, this, [this, dock, button] { showMenu(dock, button->mapToGlobal(QPoint(0, button->height()))); });
	row->addWidget(button);
	header->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(header, &QWidget::customContextMenuRequested, this, [this, dock, header](const QPoint& at) { showMenu(dock, header->mapToGlobal(at)); });
	m_headers.insert(dock, header);
	m_blanks.insert(dock, new QWidget(dock));
	header->hide();
	dock->setTitleBarWidget(m_blanks.value(dock));
	dock->installEventFilter(this);
	connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
		if (floating) makeRealWindowAfterDrag(dock);
		scheduleHeaders();
	});
	connect(dock, &QDockWidget::dockLocationChanged, this, &DockLayout::scheduleHeaders);
	connect(dock, &QDockWidget::visibilityChanged, this, &DockLayout::scheduleHeaders);
	scheduleHeaders();
}

void DockLayout::captureFactory() {
	m_factory.clear();
	for (const auto& dock : std::as_const(m_docks)) {
		if (dock) m_factory.insert(dock->objectName(), currentPlace(dock));
	}
	m_factoryState = m_window->saveState(STATE_VERSION);
}

bool DockLayout::restore() {
	const QByteArray state = QSettings().value(STATE_KEY).toByteArray();
	if (state.isEmpty() || !m_window->restoreState(state, STATE_VERSION)) return false;
	for (const auto& dock : std::as_const(m_docks)) {
		if (dock && dock->isFloating()) makeRealWindow(dock);
	}
	updateHeaders();
	return true;
}

QByteArray DockLayout::state() const {
	return m_window->saveState(STATE_VERSION);
}

void DockLayout::save(const QByteArray& state) {
	QSettings().setValue(STATE_KEY, state.isEmpty() ? this->state() : state);
}

QDockWidget* DockLayout::dockAtTab(const QPoint& globalPos) const {
	auto* bar = qobject_cast<QTabBar*>(QApplication::widgetAt(globalPos));
	// The main window's own tab bars (a tab bar inside a panel is the panel's business).
	if (!bar || bar->parentWidget() != m_window) return nullptr;
	const int index = bar->tabAt(bar->mapFromGlobal(globalPos));
	if (index < 0) return nullptr;
	const QString title = bar->tabText(index);
	for (const auto& dock : m_docks) {
		if (dock && dock->windowTitle() == title) return dock;
	}
	return nullptr;
}

QMenu* DockLayout::showMenu(QDockWidget* dock, const QPoint& globalPos) {
	auto* menu = new QMenu(m_window);
	menu->setObjectName("dockMenu");
	menu->setAttribute(Qt::WA_DeleteOnClose);
	QPointer<QDockWidget> target(dock);
	const auto add = [&](const QString& text, const char* name, bool enabled, auto&& run) {
		QAction* action = menu->addAction(text);
		action->setObjectName(QLatin1String(name));
		action->setEnabled(enabled);
		connect(action, &QAction::triggered, this, [target, run] {
			if (target) run(target.data());
		});
	};
	add(tr("Float as window"), "dockFloat", !dock->isFloating(), [this](QDockWidget* d) { floatAsWindow(d); });
	add(tr("Reattach"), "dockReattach", dock->isFloating(), [this](QDockWidget* d) { reattach(d); });
	menu->addSeparator();
	add(tr("Back to original position"), "dockHome", true, [this](QDockWidget* d) { backToHome(d); });
	add(tr("Set this as original position"), "dockSetHome", true, [this](QDockWidget* d) { setHome(d); });
	menu->addSeparator();
	add(tr("Reset all panels"), "dockReset", true, [this](QDockWidget*) { resetToFactory(); });
	menu->popup(globalPos);
	return menu;
}

void DockLayout::floatAsWindow(QDockWidget* dock) {
	if (!dock->isFloating()) {
		// Docked panels are often flat: the window gets a useful size, over the main window.
		const QSize size = dock->size().expandedTo(QSize(640, 440));
		dock->setFloating(true);
		const QRect frame = m_window->geometry();
		QRect rect(QPoint(0, 0), size);
		rect.moveCenter(frame.center());
		if (const QScreen* screen = m_window->screen()) rect = rect.intersected(screen->availableGeometry());
		dock->setGeometry(rect);
	}
	makeRealWindow(dock);
	dock->show();
	dock->raise();
	dock->activateWindow();
}

void DockLayout::reattach(QDockWidget* dock) {
	if (!dock->isFloating()) return;
	dock->setFloating(false);   // to the place it left (Qt keeps it)
	if (dock->isFloating() || m_window->dockWidgetArea(dock) == Qt::NoDockWidgetArea) {
		backToHome(dock);
		return;
	}
	dock->show();
	dock->raise();
}

void DockLayout::backToHome(QDockWidget* dock) {
	const Home home = homeOf(dock);
	if (home.floating) {
		floatAsWindow(dock);
		if (home.geometry.isValid()) dock->setGeometry(home.geometry);
		return;
	}
	const auto area = Qt::DockWidgetArea(home.area ? home.area : int(Qt::BottomDockWidgetArea));
	if (dock->isFloating()) dock->setFloating(false);
	// Next to a panel of its group that is docked in that area, else alone there.
	QDockWidget* buddy = nullptr;
	for (const auto& name : home.group) {
		QDockWidget* other = dockNamed(name);
		if (other && other != dock && !other->isFloating() && m_window->dockWidgetArea(other) == area) {
			buddy = other;
			break;
		}
	}
	if (buddy) {
		m_window->tabifyDockWidget(buddy, dock);
	} else {
		m_window->addDockWidget(area, dock);
	}
	dock->show();
	dock->raise();
}

void DockLayout::setHome(QDockWidget* dock) {
	const Home home = currentPlace(dock);
	QVariantMap value;
	value["area"] = home.area;
	value["group"] = home.group;
	value["floating"] = home.floating;
	value["geometry"] = home.geometry;
	QSettings().setValue(HOME_KEY + dock->objectName(), value);
}

void DockLayout::resetToFactory() {
	QSettings().remove(QString::fromLatin1(HOME_KEY).chopped(1));
	for (const auto& dock : std::as_const(m_docks)) {
		if (dock && dock->isFloating()) dock->setFloating(false);
	}
	if (!m_factoryState.isEmpty()) m_window->restoreState(m_factoryState, STATE_VERSION);
	for (const auto& dock : std::as_const(m_docks)) {
		if (dock && dock->isFloating()) backToHome(dock);
	}
	updateHeaders();
	emit layoutReset();
}

bool DockLayout::isRealWindow(const QDockWidget* dock) {
	return dock->isFloating() && (dock->windowFlags() & Qt::WindowType_Mask) == Qt::Window;
}

bool DockLayout::eventFilter(QObject* watched, QEvent* event) {
	// Closing a panel's window puts the panel back (it is not lost on a second monitor).
	if (event->type() == QEvent::Close) {
		auto* dock = qobject_cast<QDockWidget*>(watched);
		if (dock && dock->isFloating() && !m_window->isHidden() && event->spontaneous()) {
			event->ignore();
			QTimer::singleShot(0, this, [this, target = QPointer<QDockWidget>(dock)] {
				if (target) reattach(target);
			});
			return true;
		}
	}
	return QObject::eventFilter(watched, event);
}

DockLayout::Home DockLayout::currentPlace(QDockWidget* dock) const {
	Home home;
	home.floating = dock->isFloating();
	if (home.floating) {
		home.geometry = dock->geometry();
		return home;
	}
	home.area = int(m_window->dockWidgetArea(dock));
	home.group << dock->objectName();
	for (auto* other : m_window->tabifiedDockWidgets(dock)) home.group << other->objectName();
	return home;
}

DockLayout::Home DockLayout::homeOf(QDockWidget* dock) const {
	const QVariantMap value = QSettings().value(HOME_KEY + dock->objectName()).toMap();
	if (!value.isEmpty()) {
		Home home;
		home.area = value.value("area").toInt();
		home.group = value.value("group").toStringList();
		home.floating = value.value("floating").toBool();
		home.geometry = value.value("geometry").toRect();
		return home;
	}
	return m_factory.value(dock->objectName());
}

QDockWidget* DockLayout::dockNamed(const QString& name) const {
	for (const auto& dock : m_docks) {
		if (dock && dock->objectName() == name) return dock;
	}
	return nullptr;
}

void DockLayout::makeRealWindow(QDockWidget* dock) {
	if (!dock->isFloating() || isRealWindow(dock)) return;
	const QRect geometry = dock->geometry();
	const bool shown = dock->isVisible();
	dock->setWindowFlags(REAL_WINDOW);   // hides it
	dock->setGeometry(geometry);
	if (shown) dock->show();
	scheduleHeaders();
}

void DockLayout::makeRealWindowAfterDrag(QDockWidget* dock) {
	// Changing the window while Qt still drags it would end the drag.
	QPointer<QDockWidget> target(dock);
	auto* timer = new QTimer(this);
	timer->setInterval(60);
	connect(timer, &QTimer::timeout, this, [this, timer, target] {
		if (target && QGuiApplication::mouseButtons() != Qt::NoButton) return;
		timer->deleteLater();
		if (target && target->isFloating()) makeRealWindow(target);
	});
	timer->start();
}

void DockLayout::scheduleHeaders() {
	if (m_headersPending) return;
	m_headersPending = true;
	QTimer::singleShot(0, this, &DockLayout::updateHeaders);
}

void DockLayout::updateHeaders() {
	m_headersPending = false;
	for (const auto& dock : std::as_const(m_docks)) {
		if (!dock) continue;
		// In a tab group the tabs name the panels; floating or alone it needs its own header.
		const bool tabbed = !dock->isFloating() && !m_window->tabifiedDockWidgets(dock).isEmpty();
		QWidget* wanted = tabbed ? m_blanks.value(dock) : m_headers.value(dock);
		if (dock->titleBarWidget() != wanted) dock->setTitleBarWidget(wanted);
	}
}

} // namespace sme::app
