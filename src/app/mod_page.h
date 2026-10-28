// mod_page.h - The Overview tab of the Project page: what the editor
// knows about the chosen game or mod - title, folders, the maps in its map folder, the search
// paths of gameinfo.txt with what was found, the entity definitions (FGDs) and the mod's game
// enhancements (mapbase-fidelity's effects, see enhancements.h).

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

signals:
	/// Buttons: open a map, make a new mod, open the compile settings.
	void openMapRequested(const QString& path);
	void newModRequested();
	void compileSettingsRequested();

private:
	Editor* m_editor;
	QLabel* m_title;
	QLabel* m_details;
	QListWidget* m_maps;
	QTreeWidget* m_mounts;
	QTreeWidget* m_fgds;
	QString m_mapDir;
};

} // namespace sme::app
