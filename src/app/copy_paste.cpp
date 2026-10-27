// copy_paste.cpp - Copy and paste helpers (see copy_paste.h).

#include "copy_paste.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QKeyEvent>

namespace sme::app {

namespace {

class StandardKeyFilter : public QObject {
public:
	StandardKeyFilter(QObject* parent, QKeySequence::StandardKey key, std::function<void()> action)
		: QObject(parent), m_key(key), m_action(std::move(action)) {}

protected:
	bool eventFilter(QObject*, QEvent* event) override {
		if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) {
			return false;
		}
		auto* key = static_cast<QKeyEvent*>(event);
		if (!key->matches(m_key)) {
			return false;
		}
		if (event->type() == QEvent::ShortcutOverride) {
			event->accept();   // ours, not a window shortcut's
			return true;
		}
		m_action();
		return true;
	}

private:
	QKeySequence::StandardKey m_key;
	std::function<void()> m_action;
};

} // namespace

void onStandardKey(QWidget* widget, QKeySequence::StandardKey key, std::function<void()> action) {
	widget->installEventFilter(new StandardKeyFilter(widget, key, std::move(action)));
}

void addCopyShortcut(QAbstractItemView* view, int role) {
	onStandardKey(view, QKeySequence::Copy, [view, role] {
		const QModelIndex index = view->currentIndex().siblingAtColumn(0);
		const QString name = index.data(role).toString();
		if (!name.isEmpty()) {
			QApplication::clipboard()->setText(name);
		}
	});
}

std::vector<std::pair<QString, QString>> parseKeyValueLines(const QString& text) {
	std::vector<std::pair<QString, QString>> out;
	static const QRegularExpression quoted(R"re(^\s*"([^"]+)"\s+"([^"]*)"\s*$)re");
	static const QRegularExpression separated(R"re(^\s*([A-Za-z_][\w.\-]*)\s*(?:\t|=|:|\s)\s*(.*?)\s*$)re");
	for (const QString& line : text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
		if (const auto m = quoted.match(line); m.hasMatch()) {
			out.emplace_back(m.captured(1).trimmed(), m.captured(2));
			continue;
		}
		if (const auto m = separated.match(line); m.hasMatch() && !m.captured(2).isEmpty()) {
			QString value = m.captured(2);
			if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
				value = value.mid(1, value.size() - 2);
			}
			out.emplace_back(m.captured(1), value);
		}
	}
	return out;
}

} // namespace sme::app
