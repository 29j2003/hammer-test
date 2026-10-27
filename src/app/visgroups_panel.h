// visgroups_panel.h - The VisGroups panel: the map's groups as a tree. The tick of a group shows
// or hides its members, the square shows its colour, the name can be edited in place;
// buttons make a group from the selection, add or remove the selection, select the members.
// All changes to the groups are undo steps (sme/visgroups.h); showing and hiding is not.

#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class Editor;

/// The VisGroups dock (see the file comment).
class VisGroupsPanel : public QWidget {
	Q_OBJECT

public:
	explicit VisGroupsPanel(Editor* editor, QWidget* parent = nullptr);

	/// Group made from the current selection (the name field's text, or "Group N"); returns its id.
	int createFromSelection();
	/// Id of the group marked in the tree, 0 if none.
	[[nodiscard]] int currentGroup() const;
	/// Marks a group in the tree (for scripts and tests).
	void setCurrentGroup(int id);

private:
	void rebuild();
	void updateButtons();
	void addSelection(bool add);
	void selectMembers(int id);
	void chooseColor();

	Editor* m_editor;
	QTreeWidget* m_tree;
	QLineEdit* m_name;
	QPushButton* m_add;
	QPushButton* m_remove;
	QPushButton* m_select;
	QPushButton* m_color;
	QPushButton* m_delete;
	bool m_rebuilding = false;
};

} // namespace sme::app
