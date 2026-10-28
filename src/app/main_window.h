// main_window.h - The editor window of Mapsmith. A side bar on the left switches between whole
// pages: Map (the views with their docks), Materials (material editor), Models, Sounds, Particles and
// Project (tabs Overview, Maps, Menu, Source control, Ship). On the Map page the views are a
// single 3D view or 3D + top/front/side; docks sit left (Tool, Outliner, VisGroups, Map Look),
// at the bottom (Asset Browser, Sounds, Particles, Compile, Problems, Log, History) and right
// (Object Properties). Also the menus, tool bars, status bar and shortcuts.

#pragma once

#include <QMainWindow>
#include <QPointer>

#include <array>

#include "compile.h"
#include "editor.h"

class QActionGroup;
class QComboBox;
class QMenu;
class QToolBar;
class QToolButton;
class QProgressDialog;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QDockWidget;

namespace sme::app {

class AssetBrowser;
class CompileWindow;
class MapLookPanel;
class MaterialEditor;
class ModPage;
class ModelsPage;
class MapsPanel;
class LogPanel;
class MaterialCache;
class Outliner;
class ParticleBrowser;
class ProblemsPanel;
class PropertiesPanel;
class SoundBrowser;
class ToolPanel;
class Updater;
struct ReleaseInfo;
class Viewport;

/// The main window: side bar pages, views, docks, menus and tool bars (see the file comment).
class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(Editor* editor, QWidget* parent = nullptr);

	/// Opens a game by its key (errors go to the log).
	void selectGame(const QString& key);
	/// Opens a map (offers a newer autosave first); false if it was not opened.
	bool openMap(const QString& path);
	/// One 3D view, or the 3D view plus top, front and side views.
	void setQuadLayout(bool quad);
	/// Parts of the window, for wiring and tests. view(0) is the 3D view, 1-3 top, front and side.
	[[nodiscard]] Viewport* view3D() const { return m_views[0]; }
	[[nodiscard]] Viewport* view(int index) const { return m_views[static_cast<std::size_t>(index)]; }
	[[nodiscard]] ProblemsPanel* problems() const { return m_problems; }
	[[nodiscard]] AssetBrowser* assets() const { return m_assets; }
	[[nodiscard]] QMenu* gameMenu() const { return m_gameMenu; }
	[[nodiscard]] ToolPanel* toolPanel() const { return m_toolPanel; }
	[[nodiscard]] SoundBrowser* sounds() const { return m_sounds; }
	[[nodiscard]] PropertiesPanel* properties() const { return m_properties; }
	[[nodiscard]] CompileWindow* compileWindow() const { return m_compileWindow; }
	[[nodiscard]] MaterialEditor* materialEditor() const { return m_materialEditor; }
	[[nodiscard]] CompileRunner& compiler() { return m_compiler; }
	/// Pages of the side bar on the left.
	enum class Page { Map, Materials, Models, Sounds, Particles, Project };
	void showPage(Page page);
	[[nodiscard]] Page currentPage() const;
	[[nodiscard]] QStackedWidget* pages() const { return m_pages; }
	[[nodiscard]] ModPage* modPage() const { return m_modPage; }
	[[nodiscard]] ModelsPage* modelsPage() const { return m_modelsPage; }
	[[nodiscard]] QTabWidget* projectTabs() const { return m_projectPage; }
	[[nodiscard]] MapsPanel* mapsPanel() const { return m_mapsPanel; }
	/// Shows a dock by its title (on the Map page); "Map", "Materials", "Material Editor", "Models" and "Mod" show that page.
	void raiseDock(const QString& name);
	/// Shows the Materials page.
	void showMaterialEditor();
	/// Update check against the GitHub releases. `manual` also reports "up to date" and errors.
	void checkForUpdates(bool manual);
	[[nodiscard]] Updater* updater() const { return m_updater; }

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	void createActions();
	void createDocks();
	void refreshGames();
	void updateTitle();
	bool maybeSave();
	bool save(bool saveAs);
	void compile();
	void showContextMenu(const QPoint& globalPos);
	void roundEdgeDialog();
	/// Ctrl+V: pastes the copied objects under the mouse (or where they were copied).
	void pasteHere();
	/// Brushes > Transform: exact move, rotation and scale.
	void transformDialog();
	/// Status bar: selection, its size and center, grid / snap / angle.
	void updateStatusInfo();
	/// Ctrl+Shift+V: several copies with offset, rotation and new names (Hammer's Paste Special).
	void pasteSpecialDialog();
	void offerUpdate(const ReleaseInfo& release, bool manual);
	void runUpdate(const ReleaseInfo& release);
	[[nodiscard]] Viewport* activeView() const;
	/// The map's docks, tool bars and keys are away while another page is shown.
	void setMapWorkspaceShown(bool shown);

	Editor* m_editor;
	MaterialCache* m_materials;
	std::array<Viewport*, 4> m_views{};
	QSplitter* m_rows = nullptr;
	std::array<QSplitter*, 2> m_columns{};
	QStackedWidget* m_pages = nullptr;       ///< The side bar's pages: Map (the views), Materials, ...
	QToolBar* m_pageBar = nullptr;
	QActionGroup* m_pageGroup = nullptr;
	ModPage* m_modPage = nullptr;           ///< Project page, tab "Overview"
	ModelsPage* m_modelsPage = nullptr;     ///< Models page: browser and 3D view of the game's models
	QTabWidget* m_projectPage = nullptr;     ///< Overview, Maps, Menu, Source control, Ship
	MapsPanel* m_mapsPanel = nullptr;
	QWidget* m_soundsPage = nullptr;         ///< Holds the Sounds browser while its page is shown
	QWidget* m_particlesPage = nullptr;
	QDockWidget* m_assetsDock = nullptr;
	QDockWidget* m_soundsDock = nullptr;
	QDockWidget* m_particlesDock = nullptr;
	QList<QPointer<QWidget>> m_hiddenForTab;    ///< Docks and tool bars put away for another tab
	QList<QPointer<QAction>> m_disabledForTab;  ///< Map keys switched off for another tab
	bool m_mapHidden = false;
	bool m_quad = false;
	QPointer<Viewport> m_active;

	AssetBrowser* m_assets = nullptr;
	PropertiesPanel* m_properties = nullptr;
	ToolPanel* m_toolPanel = nullptr;
	SoundBrowser* m_sounds = nullptr;
	ParticleBrowser* m_particles = nullptr;
	CompileWindow* m_compileWindow = nullptr;
	MaterialEditor* m_materialEditor;
	std::vector<Vec3> m_lastLeak;
	Outliner* m_outliner = nullptr;
	ProblemsPanel* m_problems = nullptr;
	LogPanel* m_log = nullptr;
	QList<QDockWidget*> m_docks;

	QToolButton* m_gameButton = nullptr;
	QMenu* m_gameMenu = nullptr;
	QComboBox* m_gridCombo = nullptr;
	MapLookPanel* m_mapLook = nullptr;
	QLabel* m_selectionLabel = nullptr;
	QLabel* m_sizeLabel = nullptr;
	QLabel* m_gridLabel = nullptr;
	QLabel* m_cursorLabel = nullptr;
	QLabel* m_statsLabel = nullptr;
	QActionGroup* m_toolGroup = nullptr;
	QActionGroup* m_modeGroup = nullptr;
	QActionGroup* m_renderGroup = nullptr;
	QAction* m_textureLock = nullptr;
	CompileRunner m_compiler;
	QList<QAction*> m_contextActions;
	Updater* m_updater = nullptr;
	QToolButton* m_versionButton = nullptr;     ///< Bottom right: version, turns into "Update to x" when one is out
	bool m_manualCheck = false;
	bool m_quitForUpdate = false;               ///< Closing to install an update (already asked about saving)   ///< Right-click menu of the views (nullptr = separator)
};

} // namespace sme::app
