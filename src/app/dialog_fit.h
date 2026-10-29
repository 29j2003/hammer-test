// dialog_fit.h - Windows that fit the screen, whatever its resolution and scaling.
//
// Every dialog of Mapsmith opens no larger than the free area of its screen and centred on it.
// When its content is taller (or wider) than that, the content scrolls while the buttons at the
// bottom (OK, Create, Cancel ...) stay where they are. The main window opens no larger than the
// screen either. DialogFitter, installed once on the application, does this for every window when
// it is first shown - also for dialogs built on the fly inside functions - so no dialog has to
// remember it. Qt's own dialogs (message boxes, file and input dialogs) and popups are left alone.

#pragma once

#include <QObject>
#include <QRect>

class QDialog;
class QWidget;

namespace sme::app {

/// The area a window may use on the screen of `window` (or the primary screen): the screen's free
/// area (without task bars) less a small margin.
[[nodiscard]] QRect availableWindowArea(const QWidget* window);
/// Tests: pretend every screen has this free area (an empty rect switches back to the real one).
void setAvailableWindowAreaForTests(const QRect& area);

/// Puts the dialog's content into a scroll area (the buttons at its bottom stay outside it) and
/// sizes and centres it within availableWindowArea(). Safe to call more than once.
void fitDialogToScreen(QDialog* dialog);
/// Makes a top-level window no larger than availableWindowArea() and keeps it on that area.
void fitWindowToScreen(QWidget* window);

/// Application-wide filter calling fitDialogToScreen / fitWindowToScreen when a window is first
/// shown (see the file comment).
class DialogFitter : public QObject {
	Q_OBJECT

public:
	explicit DialogFitter(QObject* parent = nullptr);

	/// Installs one filter on the application (later calls do nothing).
	static void install();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace sme::app
