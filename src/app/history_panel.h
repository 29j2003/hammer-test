// history_panel.h - Undo history as a list: every change with its name, the current position
// marked, undone changes greyed out. Clicking an entry undoes or redoes up to it.

#pragma once

#include <QWidget>

class QListWidget;
class QLabel;

namespace sme::app {

class Editor;

class HistoryPanel : public QWidget {
	Q_OBJECT

public:
	explicit HistoryPanel(Editor* editor, QWidget* parent = nullptr);

	/// The list: row 0 is the state when the map was opened, row i the state after change i.
	[[nodiscard]] QListWidget* list() const { return m_list; }

private:
	/// Rebuilds the rows when the history changed (cheap: only when its revision moved).
	void refresh();

	Editor* m_editor;
	QListWidget* m_list;
	QLabel* m_summary;
	unsigned m_shownRevision = ~0u;
	bool m_updating = false;
};

} // namespace sme::app
