// tool_panel.h - "Tool" dock: options of the active tool, like the tool properties in
// Source 2 Hammer. The entity tool shows the entity picker (search, groups, description),
// the block tool its shape settings, the other tools a short how-to.

#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class Editor;

/// Searchable list of every entity class the FGDs of the game define.
class EntityPicker : public QWidget {
	Q_OBJECT

public:
	explicit EntityPicker(Editor* editor, QWidget* parent = nullptr);
	void focusSearch();
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }

private:
	void reload();
	void filter(const QString& text);
	void showClass(const QString& name);
	void remember(const QString& name);

	Editor* m_editor;
	QLineEdit* m_search;
	QTreeWidget* m_tree;
	QTreeWidgetItem* m_recent = nullptr;
	QLabel* m_info;
	QLabel* m_summary;
};

class ToolPanel : public QWidget {
	Q_OBJECT

public:
	explicit ToolPanel(Editor* editor, QWidget* parent = nullptr);
	[[nodiscard]] EntityPicker* entityPicker() const { return m_entities; }
	/// Adds the page shown for the block tool (built by the block tool options).
	void setBlockPage(QWidget* page);

signals:
	/// The panel wants to be seen (entity tool chosen).
	void wantsAttention();

private:
	void refresh();

	Editor* m_editor;
	QStackedWidget* m_pages;
	QLabel* m_hint;
	EntityPicker* m_entities;
	QWidget* m_block = nullptr;
};

} // namespace sme::app
