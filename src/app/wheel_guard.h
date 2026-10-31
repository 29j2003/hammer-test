// wheel_guard.h - The mouse wheel scrolls panels, it does not change things by accident.
//
// Scrolling down a panel passes over drop-downs, number boxes, sliders and tab bars; Qt lets
// each of them take the wheel (a class or key changes, Keyvalues jumps to Outputs). The guard,
// installed once on the application, sends such wheel events to the scroll area around them:
// - tab bars never switch by the wheel (outside scroll areas the wheel does nothing there),
// - drop-downs, number boxes and sliders in a scroll area change by the wheel while they have
//   the keyboard focus (after a click; also when the focus is in a number box's own line edit),
// - number boxes and sliders also change once the mouse has rested on them (350 ms) and the panel
//   has not been scrolled for 600 ms - so scrolling down a panel past them never changes them, but
//   hovering Scale or Rotation and turning the wheel does, tick after tick: the rest is kept by
//   mouse position, so Object Properties rebuilding after each change keeps the wheel working,
//   while a field the panel's scrolling slid under the mouse does not count as rested on,
// - drop-downs need a click first, unless marked with setWheelWhenResting() (the sky picker:
//   hover it, then wheel through the skies); setWheelWhenResting(field, false) opts a number box out.

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QPointer>

class QAbstractScrollArea;
class QWidget;

namespace sme::app {

/// Application-wide wheel filter (see the file comment).
class WheelGuard : public QObject {
	Q_OBJECT

public:
	explicit WheelGuard(QObject* parent = nullptr);

	/// Lets the wheel change `field` without focus when the mouse rests on it, or not (see the file comment).
	static void setWheelWhenResting(QWidget* field, bool on = true);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	/// The mouse stands at `global` over `watched` (a new place restarts the rest).
	void noteMouseAt(QPoint global, QObject* watched);
	/// The mouse has rested on `field` long enough for the wheel to change it.
	bool restingOn(QWidget* field, QPoint global);

	QPoint m_restPos;                           ///< Where the mouse last stood
	qint64 m_restSince = 0;                     ///< Since when (ms since epoch), 0 = not known
	QPointer<QWidget> m_restWidget;             ///< What it stood on (gone: a panel rebuilt under it)
	QPointer<QAbstractScrollArea> m_lastArea;   ///< Scroll area the wheel last scrolled
	QElapsedTimer m_lastScroll;                 ///< Since that scroll
};

} // namespace sme::app
