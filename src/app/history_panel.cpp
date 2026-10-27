// history_panel.cpp - Undo history list (see history_panel.h).

#include "history_panel.h"

#include "editor.h"

#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QVBoxLayout>

namespace sme::app {

HistoryPanel::HistoryPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("historyPanel");
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	m_summary = new QLabel;
	m_summary->setProperty("role", "description");
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);
	m_list = new QListWidget;
	m_list->setObjectName("historyList");
	m_list->setUniformItemSizes(true);
	layout->addWidget(m_list, 1);
	// A click goes back (or forward) to that point.
	// Queued: undoing rebuilds this list, which must not happen inside the list's own signal.
	connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (m_updating || row < 0) return;
		QMetaObject::invokeMethod(this, [this, row] { m_editor->undoTo(static_cast<std::size_t>(row)); }, Qt::QueuedConnection);
	});
	connect(editor, &Editor::documentChanged, this, &HistoryPanel::refresh);
	connect(editor, &Editor::documentReplaced, this, &HistoryPanel::refresh);
	refresh();
}

void HistoryPanel::refresh() {
	const auto& history = m_editor->history();
	if (history.revision() == m_shownRevision && m_list->count() == static_cast<int>(history.size()) + 1) {
		return;
	}
	m_shownRevision = history.revision();
	m_updating = true;
	m_list->clear();
	auto* start = new QListWidgetItem(tr("Map opened"));
	start->setForeground(QColor(150, 150, 150));
	m_list->addItem(start);
	const auto applied = history.index();
	for (std::size_t i = 0; i < history.size(); ++i) {
		auto* item = new QListWidgetItem(QString::fromStdString(history.nameAt(i)));
		if (i >= applied) {
			// Undone: still there to redo, until something new is done.
			QFont font = item->font();
			font.setItalic(true);
			item->setFont(font);
			item->setForeground(QColor(120, 120, 120));
		}
		m_list->addItem(item);
	}
	m_list->setCurrentRow(static_cast<int>(applied));
	m_list->scrollToItem(m_list->currentItem());
	m_summary->setText(tr("%n change(s), %1 undone - click a line to go back to it", nullptr, int(history.size())).arg(history.size() - applied));
	m_updating = false;
}

} // namespace sme::app
