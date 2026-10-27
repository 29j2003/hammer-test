// shortcut_dialog.h - Keyboard shortcut editor: every command of the window with its key, a
// search, conflict warnings and "reset to default". Changes are stored in the settings and
// applied at every start.

#pragma once

#include <QDialog>

class QLineEdit;
class QTableWidget;
class QLabel;

namespace sme::app {

/// The keyboard shortcut editor (see the file comment).
class ShortcutDialog : public QDialog {
	Q_OBJECT

public:
	/// `owner` is the window whose actions are listed (actions need an objectName).
	ShortcutDialog(QWidget* owner, QWidget* parent = nullptr);
	/// Applies the shortcuts saved in the settings to the owner's actions.
	static void applySaved(QWidget* owner);

private:
	void fill();
	void checkConflicts();

	QWidget* m_owner;
	QLineEdit* m_search;
	QTableWidget* m_table;
	QLabel* m_conflicts;
};

} // namespace sme::app
