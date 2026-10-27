// find_dialog.h - Finding entities while typing (class, name, key=value, output target, model,
// sound; see sme/entity_search.h) and the entity report: every entity of the map in one sortable,
// filterable table. Clicking selects, double-clicking also frames it in the 3D view.

#pragma once

#include <QDialog>

#include <functional>

class QComboBox;
class QLineEdit;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

namespace sme::app {

class Editor;

/// The Find entities dialog with its entity report tab (see the file comment).
class FindDialog : public QDialog {
	Q_OBJECT

public:
	/// `frameSelection` moves the view to the selection (double-click, "Go to").
	FindDialog(Editor* editor, std::function<void()> frameSelection, QWidget* parent = nullptr);

	/// Find tab: what to search in (EntityField order: Anything, Class, Name, Key = value, ...).
	void setSearch(int field, const QString& query);
	/// Shows the Find (0) or Entity report (1) tab.
	void showTab(int index);
	[[nodiscard]] QTreeWidget* results() const { return m_results; }
	[[nodiscard]] QTreeWidget* report() const { return m_report; }

private:
	void search();
	void fillReport();
	/// Selects the entities of the chosen rows; `frame` also moves the view there.
	void selectRows(QTreeWidget* tree, bool all, bool frame);

	Editor* m_editor;
	std::function<void()> m_frame;
	QTabWidget* m_tabs;
	QComboBox* m_field;
	QLineEdit* m_query;
	QTreeWidget* m_results;
	QLabel* m_count;
	QLineEdit* m_reportFilter;
	QComboBox* m_reportClass;
	QTreeWidget* m_report;
	QLabel* m_reportCount;
};

/// Shows the (one) Find dialog of `parent`, creating it on first use; `tab` 0 = Find, 1 = Entity report.
FindDialog* showFindDialog(Editor* editor, QWidget* parent, std::function<void()> frameSelection, int tab = 0);

} // namespace sme::app
