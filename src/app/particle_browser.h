// particle_browser.h - Particles tab: every particle system of the game's .pcf files, grouped by
// file and searchable. Drag one into the map for an info_particle_system, or double-click to
// set it on the selected one.

#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidget;

namespace sme::app {

class AssetBrowser;
class Editor;

class ParticleBrowser : public QWidget {
	Q_OBJECT

public:
	ParticleBrowser(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }

private:
	void fill();
	void filter(const QString& text);

	Editor* m_editor;
	AssetBrowser* m_assets;
	QLineEdit* m_search;
	QTreeWidget* m_tree;
	QLabel* m_info;
};

} // namespace sme::app
