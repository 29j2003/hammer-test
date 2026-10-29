// dock_layout.h - The main window's panels: float as real windows, reattach, original places,
// and the layout kept from one start to the next.
//
// Every panel (Object Properties, Asset Browser, Sounds, Log, ...) is a QDockWidget. A right
// click on its tab or its header offers:
// - "Float as window": a real top-level window (own frame with maximise, any monitor, scaled by
//   that monitor's DPI) - not Qt's small tool window. A panel dragged out by its tab becomes one
//   too once the mouse is released.
// - "Reattach": back where it was docked (closing its window does the same).
// - "Back to original position" / "Set this as original position": each panel has a home - the
//   factory place, or the place the mapper set - with its area and the panels it shares tabs with.
// - "Reset all panels": the factory layout.
// The layout (QMainWindow::saveState) is saved when the window closes and restored at the start.
// A panel shows a slim header with its name and a menu button where no tab names it (floating, or
// alone in its area); in a tab group the tabs are the header.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>

class QDockWidget;
class QMainWindow;
class QMenu;
class QPoint;
class QWidget;

namespace sme::app {

/// Floating, homes and persistence of a main window's docks (see the file comment).
class DockLayout : public QObject {
	Q_OBJECT

public:
	explicit DockLayout(QMainWindow* window);

	/// Takes `dock` under management (header, menu, floating as a window). Call after it was added.
	void addDock(QDockWidget* dock);
	/// The current layout is the factory one (homes and the state "Reset all panels" restores).
	void captureFactory();
	/// Restores the layout saved by save(); false when there was none (or it did not fit).
	bool restore();
	/// The current layout, for save() later (the docks may be hidden by then).
	[[nodiscard]] QByteArray state() const;
	/// Saves the layout (or `state`, taken earlier by state()) for the next start.
	void save(const QByteArray& state = QByteArray());

	/// The dock whose tab is under `globalPos`, or nullptr.
	[[nodiscard]] QDockWidget* dockAtTab(const QPoint& globalPos) const;
	/// The menu for `dock` (Float as window, Reattach, homes, reset), shown at `globalPos`.
	QMenu* showMenu(QDockWidget* dock, const QPoint& globalPos);

	void floatAsWindow(QDockWidget* dock);
	void reattach(QDockWidget* dock);
	void backToHome(QDockWidget* dock);
	void setHome(QDockWidget* dock);
	void resetToFactory();

	/// True when `dock` floats as a real window (not Qt's tool window).
	[[nodiscard]] static bool isRealWindow(const QDockWidget* dock);

signals:
	/// The factory layout was restored: the window sets its panel sizes again.
	void layoutReset();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	/// Where a dock belongs: its area and the docks it shares tabs with, or a floating window.
	struct Home {
		int area = 0;          ///< Qt::DockWidgetArea
		QStringList group;     ///< Object names of the tab group (itself included)
		bool floating = false;
		QRect geometry;        ///< Window geometry when floating
	};

	[[nodiscard]] Home currentPlace(QDockWidget* dock) const;
	[[nodiscard]] Home homeOf(QDockWidget* dock) const;
	[[nodiscard]] QDockWidget* dockNamed(const QString& name) const;
	/// Qt's floating tool window -> a real window with frame, maximise and close.
	void makeRealWindow(QDockWidget* dock);
	/// Once the mouse is released after a drag out: makeRealWindow().
	void makeRealWindowAfterDrag(QDockWidget* dock);
	/// Shows the header where no tab names the dock (queued: many docks move at once).
	void scheduleHeaders();
	void updateHeaders();

	QMainWindow* m_window;
	QList<QPointer<QDockWidget>> m_docks;
	QHash<QDockWidget*, QWidget*> m_headers;   ///< Name and menu button
	QHash<QDockWidget*, QWidget*> m_blanks;    ///< Empty title bar (tabs name the dock)
	QHash<QString, Home> m_factory;
	QByteArray m_factoryState;
	bool m_headersPending = false;
};

} // namespace sme::app
