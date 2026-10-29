// lazy_refresh.h - Work for a panel nobody sees waits until it is shown.
//
// Pages like Project › Menu or Texts read the mod's files whenever the game changes; at the start
// nobody looks at them, so reading them then only delays the window. request() refreshes at once
// while the widget is visible, else once the next time it is shown.

#pragma once

#include <QObject>

#include <functional>

class QWidget;

namespace sme::app {

/// Refresh of `widget` when it is (or next becomes) visible (see the file comment).
class RefreshWhenShown : public QObject {
	Q_OBJECT

public:
	RefreshWhenShown(QWidget* widget, std::function<void()> refresh);

	/// Refreshes now when the widget is visible, else the next time it is shown.
	void request();
	/// A refresh is waiting for the widget to be shown.
	[[nodiscard]] bool pending() const { return m_pending; }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QWidget* m_widget;
	std::function<void()> m_refresh;
	bool m_pending = false;
};

} // namespace sme::app
