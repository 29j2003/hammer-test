// new_mod_dialog.h - "New mod ...": turns one or more maps into a Source mod of their own. Asks
// for the title, the folder name and what the mod is built on, creates it in Steam's sourcemods
// folder (see sme/mod_template.h), copies the chosen maps into its map folder and switches the
// editor to the new mod. The mod's mapbase-fidelity features can be chosen right away (all on):
// they are kept in the mod and used once the add-on is installed (sme/mod_features.h).

#pragma once

#include <QDialog>
#include <QStringList>

#include <optional>
#include <set>
#include <string>

#include <sme/mod_template.h>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace sme::app {

class Editor;
class FeatureChecklist;

/// Creates the mod, rediscovers the games and opens the new mod in the editor. Returns the
/// mod's game key ("sourcemods/<folder>"), or empty with `error` set. `featuresOff`: the
/// mapbase-fidelity features the mod should not have (written into it before it opens).
QString createModAndSwitch(Editor* editor, const ModTemplate& mod, const QStringList& maps, QString* error = nullptr,
                           const std::optional<std::set<std::string>>& featuresOff = std::nullopt);

/// Shows the dialog; true if a mod was created (the editor then has it open).
bool runNewModDialog(Editor* editor, QWidget* parent = nullptr);

/// The dialog itself; runNewModDialog() shows it and does the work.
class NewModDialog : public QDialog {
	Q_OBJECT

public:
	explicit NewModDialog(Editor* editor, QWidget* parent = nullptr);

	/// What the fields say.
	[[nodiscard]] ModTemplate modTemplate() const;
	/// The checked maps to copy into the new mod.
	[[nodiscard]] QStringList mapsToCopy() const;
	/// The features left unticked.
	[[nodiscard]] std::set<std::string> featuresOff() const;
	[[nodiscard]] FeatureChecklist* features() const { return m_features; }

private:
	void validate();

	Editor* m_editor;
	QLineEdit* m_title;
	QLineEdit* m_folder;
	QButtonGroup* m_base;          ///< What the mod is based on (id = ModBase)
	QListWidget* m_maps;
	FeatureChecklist* m_features;
	QLabel* m_where;
	QLabel* m_problem;
	QPushButton* m_create;
	bool m_folderEdited = false;   ///< The folder name follows the title until it is typed in
};

} // namespace sme::app
