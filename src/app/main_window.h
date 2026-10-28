// main_window.h - The editor window of Mapsmith. A side bar on the left switches between whole
// pages: Map (the views with their docks), Materials (material editor), Models, Sounds, Particles and
// Project (tabs Overview, Maps, Menu, Source control, Ship). On the Map page the views are a
// single 3D view or 3D + top/front/side; docks sit left (Tool, Outliner, VisGroups, Map Look),
// at the bottom (Asset Browser, Sounds, Particles, Compile, Problems, Log, History) and right
// (Object Properties). Also the menus, tool bars, status bar and shortcuts.

#pragma once

#include "asset_menu.h"
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
class StartPage;
class ModPage;
class ModelsPage;
class MapsPanel;
class LogPanel;
class MaterialCache;
class Outliner;
class ParticleBrowser;
class ProblemsPanel;
class BudgetPanel;
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
	[[nodiscard]] BudgetPanel* budget() const { return m_budget; }
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
	/// The start page: continue, recent projects, start new (shown at start when wanted).
	void showStartPage();
	[[nodiscard]] bool startPageShown() const;
	[[nodiscard]] StartPage* startPage() const { return m_startPage; }
	/// Opens a recent project: its game or mod, then the map, on the Map page.
	bool openProject(const QString& gameKey, const QString& mapPath);
	/// The guided new project: a new mod (Mapbase, features installed), then a first map.
	bool startNewProject();
	/// A first map in the current mod: a closed room with light, player start and cubemap, saved
	/// as <name>.vmf in the mod's map folder and opened.
	bool makeStarterMap(const QString& name);
	[[nodiscard]] Page currentPage() const;
	[[nodiscard]] QStackedWidget* pages() const { return m_pages; }
	[[nodiscard]] ModPage* modPage() const { return m_modPage; }
	[[nodiscard]] ModelsPage* modelsPage() const { return m_modelsPage; }
	[[nodiscard]] QTabWidget* projectTabs() const { return m_projectPage; }
	/// Project › Texts (the mod's localization and caption files).
	[[nodiscard]] class TextsPanel* textsPanel() const { return m_textsPanel; }
	[[nodiscard]] MapsPanel* mapsPanel() const { return m_mapsPanel; }
	/// Project › Mod code (the mod's source code: Update mod, game files, report).
	[[nodiscard]] class ModCodePanel* modCodePanel() const { return m_modCode; }
	/// Project › Features (which mapbase-fidelity features the mod has, fresh install).
	[[nodiscard]] class FeaturesPanel* featuresPanel() const { return m_features; }
	/// "Make report": packs what `fidelity_report` wrote in the game (Project › Mod code runs
	/// report.ps1 and opens the zip's folder). Reachable from the Map menu, the Compile dock and
	/// the Problems dock - where one looks after testing a map in the game.
	void makeReport();
	/// Shows a dock by its title (on the Map page); "Map", "Materials", "Material Editor", "Models" and "Mod" show that page.
	void raiseDock(const QString& name);
	/// Puts the corner handles where the map views meet the panels (hidden on other pages).
	void placeCornerGrips();
	/// Where revealAsset shows an asset: in the Asset Browser under the map, or on its own page
	/// (Materials, Models, Sounds, Particles).
	enum class RevealIn { AssetBrowser, Page };
	/// Shows the asset where it is listed and selects it there; false when it is not found.
	bool revealAsset(const AssetRef& asset, RevealIn where);
	/// Selects everything in the map that uses the asset (faces for a material); the number found.
	int findInMap(const AssetRef& asset);
	/// The "Show in ..." / "Find in map" entries for an asset; every menu naming one uses this.
	void fillAssetMenu(QMenu* menu, const AssetRef& asset, AssetPlace from);
	/// The views' right-click menu for the current selection (the caller owns it; for tests too).
	[[nodiscard]] QMenu* buildContextMenu();
	/// Shows the Materials page.
	void showMaterialEditor();
	/// Update check against the GitHub releases. `manual` also reports "up to date" and errors.
	void checkForUpdates(bool manual);
	[[nodiscard]] Updater* updater() const { return m_updater; }

protected:
	void closeEvent(QCloseEvent* event) override;
	/// Keeps the corner handles on the views' corners when the views change size.
	bool eventFilter(QObject* watched, QEvent* event) override;

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
	QTabWidget* m_projectPage = nullptr;     ///< Overview, Features, Maps, Menu, Texts, Source control, Mod code, Ship
	class ModCodePanel* m_modCode = nullptr;
	class FeaturesPanel* m_features = nullptr;
	class TextsPanel* m_textsPanel = nullptr;
	MapsPanel* m_mapsPanel = nullptr;
	QWidget* m_soundsPage = nullptr;         ///< Holds the Sounds browser while its page is shown
	QWidget* m_particlesPage = nullptr;
	QDockWidget* m_assetsDock = nullptr;
	QWidget* m_gripLeft = nullptr;    ///< Corner handles (CornerGrip in main_window.cpp)
	QWidget* m_gripRight = nullptr;
	QDockWidget* m_soundsDock = nullptr;
	QDockWidget* m_particlesDock = nullptr;
	QList<QPointer<QWidget>> m_hiddenForTab;    ///< Docks and tool bars put away for another tab
	QList<QPointer<QAction>> m_disabledForTab;  ///< Map keys switched off for another tab
	bool m_mapHidden = false;
	bool m_reloadPending = false;               ///< The game is read again soon (the mod's files changed)
	bool m_quad = false;
	QPointer<Viewport> m_active;

	AssetBrowser* m_assets = nullptr;
	PropertiesPanel* m_properties = nullptr;
	ToolPanel* m_toolPanel = nullptr;
	SoundBrowser* m_sounds = nullptr;
	ParticleBrowser* m_particles = nullptr;
	CompileWindow* m_compileWindow = nullptr;
	StartPage* m_startPage = nullptr;
	QString m_pickedMaterial;   ///< Picked in the browser while mapping (the Materials page opens it)
	QString m_editorAtPick;     ///< The material editor's material at that moment
	MaterialEditor* m_materialEditor;
	std::vector<Vec3> m_lastLeak;
	Outliner* m_outliner = nullptr;
	ProblemsPanel* m_problems = nullptr;
	BudgetPanel* m_budget = nullptr;
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
	QMenu* m_doorMenu = nullptr;            ///< Brushes > Make door (also in the context menu)
	Updater* m_updater = nullptr;
	QToolButton* m_versionButton = nullptr;     ///< Bottom right: version, turns into "Update to x" when one is out
	bool m_manualCheck = false;
	bool m_quitForUpdate = false;               ///< Closing to install an update (already asked about saving)   ///< Right-click menu of the views (nullptr = separator)
};

} // namespace sme::app
