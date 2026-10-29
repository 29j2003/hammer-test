// mod_page.h - The Overview tab of the Project page: what the editor
// knows about the chosen game or mod - title, folders, the maps in its map folder, the search
// paths of gameinfo.txt with what was found, the entity definitions (FGDs) and the mod's game
// enhancements (mapbase-fidelity's effects, see enhancements.h). A banner offers to add what a
// Half-Life 2 based mod lacks of its base game (skill files, HUD texts; sme/mod_base_files.h).

#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QTreeWidget;

namespace sme::app {

class Editor;

/// The Overview tab of the Project page (see the file comment).
class ModPage : public QWidget {
	Q_OBJECT

public:
	explicit ModPage(Editor* editor, QWidget* parent = nullptr);

	/// Reads everything again (game changed, map saved, page shown).
	void refresh();
	/// A line under the title (e.g. Mod code's "new changes"); it shows and hides itself.
	void addBanner(QWidget* banner);

signals:
	/// Buttons: open a map, make a new mod, open the compile settings.
	void openMapRequested(const QString& path);
	void newModRequested();
	void compileSettingsRequested();

private:
	Editor* m_editor;
	QLabel* m_title;
	QLabel* m_details;
	QWidget* m_baseFiles;        ///< Banner: the mod lacks Half-Life 2's skill files or texts
	QLabel* m_baseFilesText;
	QListWidget* m_maps;
	QTreeWidget* m_mounts;
	QTreeWidget* m_fgds;
	QString m_mapDir;
};

} // namespace sme::app
