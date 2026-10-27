// mod_page.h - The "Mod" page of the side bar: what the editor
// knows about the chosen game or mod - title, folders, the maps in its map folder, the search
// paths of gameinfo.txt with what was found, and the entity definitions (FGDs).

#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QTreeWidget;

namespace sme::app {

class Editor;

class ModPage : public QWidget {
	Q_OBJECT

public:
	explicit ModPage(Editor* editor, QWidget* parent = nullptr);

	/// Reads everything again (game changed, map saved, page shown).
	void refresh();

signals:
	void openMapRequested(const QString& path);
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
