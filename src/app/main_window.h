// main_window.h - The editor window: viewports (single 3D or 3D + three 2D views), tool bar,
// docks (asset browser, object properties, outliner, problems, log), menus and shortcuts.

#pragma once

#include <QMainWindow>
#include <QPointer>

#include <array>

#include "compile.h"
#include "editor.h"

class QActionGroup;
class QComboBox;
class QMenu;
class QToolButton;
class QProgressDialog;
class QLabel;
class QSplitter;
class QTabWidget;
class QDockWidget;

namespace sme::app {

class AssetBrowser;
class CompileWindow;
class MapLookPanel;
class MaterialEditor;
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

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(Editor* editor, QWidget* parent = nullptr);

	void selectGame(const QString& key);
	bool openMap(const QString& path);
	void setQuadLayout(bool quad);
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
	/// Shows a dock by its title; "Material Editor" and "Map" switch the tab in the middle.
	void raiseDock(const QString& name);
	/// Opens the Material Editor tab (adds it again after it was closed).
	void showMaterialEditor();
	[[nodiscard]] QTabWidget* centralTabs() const { return m_centralTabs; }
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
	/// Status bar: selection, its size and center, grid / snap / angle.
	void updateStatusInfo();
	/// Ctrl+Shift+V: several copies with offset, rotation and new names (Hammer's Paste Special).
	void pasteSpecialDialog();
	void offerUpdate(const ReleaseInfo& release, bool manual);
	void runUpdate(const ReleaseInfo& release);
	[[nodiscard]] Viewport* activeView() const;
	/// The map's docks, tool bars and keys are away while another tab (Material Editor) is shown.
	void setMapWorkspaceShown(bool shown);

	Editor* m_editor;
	MaterialCache* m_materials;
	std::array<Viewport*, 4> m_views{};
	QSplitter* m_rows = nullptr;
	std::array<QSplitter*, 2> m_columns{};
	QTabWidget* m_centralTabs = nullptr;     ///< "Map" (the views) and the Material Editor
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
