// new_mod_dialog.h - "New mod ...": turns one or more maps into a Source mod of their own. Asks
// for the title, the folder name and what the mod is built on, creates it in Steam's sourcemods
// folder (see sme/mod_template.h), copies the chosen maps into its map folder and switches the
// editor to the new mod.

#pragma once

#include <QDialog>
#include <QStringList>

#include <sme/mod_template.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace sme::app {

class Editor;

/// Creates the mod, rediscovers the games and opens the new mod in the editor. Returns the
/// mod's game key ("sourcemods/<folder>"), or empty with `error` set.
QString createModAndSwitch(Editor* editor, const ModTemplate& mod, const QStringList& maps, QString* error = nullptr);

/// Shows the dialog; true if a mod was created (the editor then has it open).
bool runNewModDialog(Editor* editor, QWidget* parent = nullptr);

class NewModDialog : public QDialog {
	Q_OBJECT

public:
	explicit NewModDialog(Editor* editor, QWidget* parent = nullptr);

	/// What the fields say.
	[[nodiscard]] ModTemplate modTemplate() const;
	/// The checked maps to copy into the new mod.
	[[nodiscard]] QStringList mapsToCopy() const;

private:
	void validate();

	Editor* m_editor;
	QLineEdit* m_title;
	QLineEdit* m_folder;
	QComboBox* m_base;
	QListWidget* m_maps;
	QLabel* m_where;
	QLabel* m_problem;
	QPushButton* m_create;
	bool m_folderEdited = false;   ///< The folder name follows the title until it is typed in
};

} // namespace sme::app
