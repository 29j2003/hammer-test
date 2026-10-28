// wheel_guard.h - The mouse wheel scrolls panels, it does not change things by accident.
//
// Scrolling down a panel passes over drop-downs, number boxes, sliders and tab bars; Qt lets
// each of them take the wheel (a class or key changes, Keyvalues jumps to Outputs). The guard,
// installed once on the application, sends such wheel events to the scroll area around them:
// - tab bars never switch by the wheel (outside scroll areas the wheel does nothing there),
// - drop-downs, number boxes and sliders in a scroll area change by the wheel only while they
//   have the keyboard focus (after a click), or - when marked with setWheelWhenResting() - once
//   the mouse has rested on them and the panel is not being scrolled (the sky picker: hover it,
//   then wheel through the skies).

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class QAbstractScrollArea;
class QWidget;

namespace sme::app {

/// Application-wide wheel filter (see the file comment).
class WheelGuard : public QObject {
	Q_OBJECT

public:
	explicit WheelGuard(QObject* parent = nullptr);

	/// Lets the wheel change `field` without focus when the mouse rests on it (see the file comment).
	static void setWheelWhenResting(QWidget* field, bool on = true);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QPointer<QAbstractScrollArea> m_lastArea;   ///< Scroll area the wheel last scrolled
	QElapsedTimer m_lastScroll;                 ///< Since that scroll
};

} // namespace sme::app
