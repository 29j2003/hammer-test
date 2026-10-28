// main_window.cpp - Editor main window (see main_window.h).

#include "main_window.h"

#include "asset_browser.h"
#include "compile_window.h"
#include "map_look_panel.h"
#include "mod_page.h"
#include "maps_panel.h"
#include "menu_editor.h"
#include "new_mod_dialog.h"
#include "source_control_panel.h"
#include "ship_panel.h"
#include "history_panel.h"
#include "visgroups_panel.h"
#include "find_dialog.h"
#include "replace_materials_dialog.h"
#include "autosaver.h"
#include "icons.h"
#include "material_editor.h"
#include "material_cache.h"
#include "panels.h"
#include "particle_browser.h"
#include "properties_panel.h"
#include "shortcut_dialog.h"
#include "sound_browser.h"
#include "tool_panel.h"
#include "updater.h"
#include "viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFileInfo>
#include <QSpinBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
#include <QMenuBar>
#include <QMessageBox>
#include <QCheckBox>
#include <QDesktopServices>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QSet>
#include <QTabBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <limits>
#include <map>

#include <sme/checks.h>
#include <sme/game.h>
#include <sme/scene_export.h>
#include <sme/text.h>

namespace sme::app {

namespace {

const char* SHORTCUT_HELP = R"(<h3>Camera</h3>
<table cellpadding="3">
<tr><td><b>Right mouse (hold)</b></td><td>Look around; W A S D move, Q / E down / up, Shift faster, wheel changes speed</td></tr>
<tr><td><b>Z</b></td><td>Fly mode on/off (look without holding the mouse)</td></tr>
<tr><td><b>Alt + left mouse</b></td><td>Orbit around the selection</td></tr>
<tr><td><b>Middle mouse</b></td><td>Pan</td></tr>
<tr><td><b>Wheel</b></td><td>Move forward/back (2D: zoom)</td></tr>
<tr><td><b>F / Ctrl+Shift+E</b></td><td>Frame the selection</td></tr>
</table>
<h3>Tools (as in Source 2 Hammer)</h3>
<table cellpadding="3">
<tr><td><b>Q</b></td><td>Select</td><td><b>T</b></td><td>Move (Shift+drag copies)</td></tr>
<tr><td><b>R</b></td><td>Rotate (Ctrl: 1° steps)</td><td><b>E</b></td><td>Scale / resize</td></tr>
<tr><td><b>Shift+B</b></td><td>Block tool: drag the base, then the height, click/Enter</td><td><b>Shift+E</b></td><td>Entity tool: click to place</td></tr>
<tr><td><b>3</b></td><td>Face mode</td><td><b>4 / 5</b></td><td>Object mode</td></tr>
<tr><td><b>Shift+T</b></td><td>Apply the current material</td><td><b>Shift+right click, Ctrl+M</b></td><td>Pick the material under the cursor</td></tr>
<tr><td><b>Ctrl+right mouse drag</b></td><td>Paint the current material on faces</td><td><b>Double-click (faces)</b></td><td>Select the whole wall</td></tr>
<tr><td><b>[ / ]</b></td><td>Grid smaller / bigger</td><td><b>Arrows, PgUp/PgDn</b></td><td>Nudge by the grid</td></tr>
<tr><td><b>Ctrl+[ / Ctrl+]</b></td><td>Rotation snap smaller / bigger</td><td><b>Alt+arrows</b></td><td>Rotate by the rotation snap</td></tr>
<tr><td><b>Shift+W</b></td><td>Snapping on/off (grid and rotation)</td><td></td><td></td></tr>
<tr><td><b>H</b></td><td>Hide selection</td><td><b>Ctrl+H / U</b></td><td>Hide others / show all</td></tr>
<tr><td><b>Ctrl+T</b></td><td>Tie brushes to entity</td><td><b>Ctrl+Shift+W</b></td><td>Move to world</td></tr>
<tr><td><b>Ctrl+D</b></td><td>Duplicate</td><td><b>Del</b></td><td>Delete</td></tr>
<tr><td><b>F5 / F6 / F8, F7</b></td><td>Unlit / lighting preview / wireframe, shadows on/off</td><td><b>F2 F3 F4</b></td><td>Top / front / side view</td></tr>
<tr><td><b>Shift+Alt+Z</b></td><td>One view / four views</td><td><b>F9</b></td><td>Compile and run</td></tr>
</table>)";

} // namespace

MainWindow::MainWindow(Editor* editor, QWidget* parent)
	: QMainWindow(parent), m_editor(editor), m_materials(new MaterialCache(MaterialCache::Size::Viewport, this)) {
	setDockNestingEnabled(true);
	setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);

	// Viewports: [3D | Top] over [Front | Side]; single layout shows only the 3D view.
	m_views[0] = new Viewport(editor, m_materials, Camera::Kind::Perspective);
	m_views[1] = new Viewport(editor, m_materials, Camera::Kind::Top);
	m_views[2] = new Viewport(editor, m_materials, Camera::Kind::Front);
	m_views[3] = new Viewport(editor, m_materials, Camera::Kind::Side);
	m_rows = new QSplitter(Qt::Vertical);
	for (int r = 0; r < 2; ++r) {
		m_columns[static_cast<std::size_t>(r)] = new QSplitter(Qt::Horizontal);
		m_columns[static_cast<std::size_t>(r)]->addWidget(m_views[static_cast<std::size_t>(r * 2)]);
		m_columns[static_cast<std::size_t>(r)]->addWidget(m_views[static_cast<std::size_t>(r * 2 + 1)]);
		m_rows->addWidget(m_columns[static_cast<std::size_t>(r)]);
	}
	// Pages of the side bar: the map (the views and their docks), Materials, Sounds,
	// Particles and the Mod overview, each filling the middle.
	m_pages = new QStackedWidget;
	m_pages->setObjectName("workspacePages");
	m_pages->addWidget(m_rows);
	setCentralWidget(m_pages);
	m_active = m_views[0];
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
		for (auto* view : m_views) {
			if (view == now) {
				m_active = view;
			}
		}
	});

	// Status bar like Source 2 Hammer: what is selected, its size and place, where the mouse is,
	// grid / snap / rotation step, and how long a frame takes.
	m_selectionLabel = new QLabel;
	m_sizeLabel = new QLabel;
	m_sizeLabel->setToolTip(tr("Size of the selection (width, length, height) and its center"));
	m_cursorLabel = new QLabel;
	m_gridLabel = new QLabel;
	m_statsLabel = new QLabel;
	for (auto* label : {m_selectionLabel, m_sizeLabel, m_cursorLabel, m_gridLabel, m_statsLabel}) {
		label->setContentsMargins(6, 0, 6, 0);
		statusBar()->addPermanentWidget(label);
	}
	// Version at the bottom right; it becomes the update button when a newer version is out.
	m_updater = new Updater(this);
	m_versionButton = new QToolButton;
	m_versionButton->setAutoRaise(true);
	m_versionButton->setText(QStringLiteral("v%1").arg(QApplication::applicationVersion()));
	m_versionButton->setToolTip(tr("Version of the editor - click to check for updates"));
	statusBar()->addPermanentWidget(m_versionButton);
	connect(m_versionButton, &QToolButton::clicked, this, [this] {
		if (const auto& release = m_updater->available()) {
			offerUpdate(*release, true);
		} else {
			checkForUpdates(true);
		}
	});
	connect(m_updater, &Updater::updateAvailable, this, [this](const ReleaseInfo& release) {
		m_versionButton->setText(tr("⬆ Update to v%1").arg(release.version));
		m_versionButton->setToolTip(tr("Version %1 is out (you have %2) - click to update").arg(release.version, QApplication::applicationVersion()));
		m_versionButton->setStyleSheet("QToolButton { color: #1b1d22; background: #f0a030; border-radius: 3px; padding: 1px 6px; font-weight: bold; }");
		const bool manual = m_manualCheck;
		m_manualCheck = false;
		// At start only once per version unless the mapper asked.
		if (manual || QSettings().value("update/skipVersion").toString() != release.version) {
			offerUpdate(release, manual);
		}
	});
	connect(m_updater, &Updater::upToDate, this, [this] {
		if (m_manualCheck) {
			QMessageBox::information(this, tr("Updates"), tr("You have the newest version (%1).").arg(QApplication::applicationVersion()));
		}
		m_manualCheck = false;
	});
	connect(m_updater, &Updater::checkFailed, this, [this](const QString& error) {
		if (m_manualCheck) {
			QMessageBox::warning(this, tr("Updates"), tr("Could not check for updates:\n%1").arg(error));
		} else {
			m_log->append(tr("Update check failed: %1").arg(error), QColor(160, 160, 170));
		}
		m_manualCheck = false;
	});
	for (auto* view : m_views) {
		connect(view, &Viewport::cursorWorldPosition, m_cursorLabel, &QLabel::setText);
		connect(view, &Viewport::contextMenuRequested, this, &MainWindow::showContextMenu);
	}
	connect(m_views[0], &Viewport::statsChanged, m_statsLabel, &QLabel::setText);
	connect(m_views[0], &Viewport::statsDetail, m_statsLabel, &QLabel::setToolTip);
	connect(editor, &Editor::selectionChanged, this, &MainWindow::updateStatusInfo);
	connect(editor, &Editor::documentChanged, this, &MainWindow::updateStatusInfo);
	connect(editor, &Editor::settingsChanged, this, &MainWindow::updateStatusInfo);
	updateStatusInfo();

	createDocks();
	createActions();

	connect(editor, &Editor::message, this, [this](const QString& text) {
		statusBar()->showMessage(text, 6000);
		m_log->append(text);
	});
	connect(editor, &Editor::documentChanged, this, &MainWindow::updateTitle);
	connect(editor, &Editor::gameAboutToChange, this, [this] { m_materials->setFileSystem(nullptr); });
	// Relief preview (mapbase-fidelity) with the editor's look switch.
	connect(editor, &Editor::settingsChanged, this, [this] { m_materials->setRelief(m_editor->fidelityLook()); });
	connect(editor, &Editor::gameChanged, this, [this] {
		m_materials->setFileSystem(m_editor->game() ? &m_editor->game()->files : nullptr);
		m_materials->setRelief(m_editor->fidelityLook());
		if (m_editor->game()) {
			for (const auto& problem : m_editor->game()->problems) {
				m_log->append(QString::fromStdString(problem), QColor(255, 200, 100));
			}
		}
		QTimer::singleShot(0, this, &MainWindow::refreshGames);   // not while its menu action is still running
		updateTitle();
	});
	connect(&m_compiler, &CompileRunner::output, m_log, &LogPanel::append);
	connect(&m_compiler, &CompileRunner::leak, this, [this](const std::vector<Vec3>& line) {
		m_lastLeak = line;
		m_problems->addLeak(line);
		m_views[0]->setLeakLine(line);
		raiseDock("Problems");
	});
	connect(m_problems, &ProblemsPanel::showLeak, this, [this](const std::vector<Vec3>& line) {
		for (auto* view : m_views) {
			view->setLeakLine(line);
		}
	});
	connect(m_problems, &ProblemsPanel::frameRequested, this, [this] { activeView()->frameSelection(); });
	connect(m_outliner, &Outliner::frameRequested, this, [this] { activeView()->frameSelection(); });

	refreshGames();
	setQuadLayout(QSettings().value("window/quad", false).toBool());
	resize(1600, 960);
	restoreGeometry(QSettings().value("window/geometry").toByteArray());
	updateTitle();
}

void MainWindow::createDocks() {
	const auto addDock = [this](const QString& title, QWidget* widget, Qt::DockWidgetArea area) {
		auto* dock = new QDockWidget(title, this);
		dock->setObjectName(title);
		dock->setWidget(widget);
		// The tabs name the panels; only the lone panel on the right gets a slim header.
		if (area == Qt::RightDockWidgetArea) {
			auto* header = new QLabel(title);
			header->setObjectName("dockTitle");
			dock->setTitleBarWidget(header);
		} else {
			dock->setTitleBarWidget(new QWidget);
		}
		addDockWidget(area, dock);
		m_docks << dock;
		return dock;
	};
	m_assets = new AssetBrowser(m_editor);
	m_properties = new PropertiesPanel(m_editor, m_assets, m_materials);
	m_outliner = new Outliner(m_editor);
	m_problems = new ProblemsPanel(m_editor, m_assets);
	m_log = new LogPanel;
	m_toolPanel = new ToolPanel(m_editor);
	m_toolPanel->entityPicker()->setMaterialCache(m_materials);
	m_toolPanel->entityPicker()->setModelThumbnails(m_assets->modelThumbnails());
	m_outliner->setIconProvider([picker = m_toolPanel->entityPicker()](const QString& cls) { return picker->iconFor(cls); });
	connect(m_toolPanel->entityPicker(), &EntityPicker::iconsChanged, m_outliner, &Outliner::refreshIcons);
	m_sounds = new SoundBrowser(m_editor);
	m_particles = new ParticleBrowser(m_editor, m_assets);
	m_compileWindow = new CompileWindow(&m_compiler);
	m_materialEditor = new MaterialEditor(m_editor, this);
	// Project page: everything around the maps - overview, all maps with batch compiling, the
	// mod's main menu, source control and getting it ready to share.
	m_modPage = new ModPage(m_editor);
	const auto openFromProject = [this](const QString& path) {
		if (maybeSave() && openMap(path)) showPage(Page::Map);
	};
	connect(m_modPage, &ModPage::openMapRequested, this, openFromProject);
	connect(m_modPage, &ModPage::newModRequested, this, [this] { runNewModDialog(m_editor, this); });
	m_mapsPanel = new MapsPanel(m_editor, &m_compiler);
	connect(m_mapsPanel, &MapsPanel::openMapRequested, this, openFromProject);
	m_projectPage = new QTabWidget;
	m_projectPage->setObjectName("projectTabs");
	m_projectPage->setDocumentMode(true);
	m_projectPage->addTab(m_modPage, tr("Overview"));
	m_projectPage->addTab(m_mapsPanel, tr("Maps"));
	m_projectPage->addTab(new MenuEditor(m_editor), tr("Menu"));
	m_projectPage->addTab(new SourceControlPanel(m_editor), tr("Source control"));
	m_projectPage->addTab(new ShipPanel(m_editor), tr("Ship"));
	connect(m_modPage, &ModPage::compileSettingsRequested, this, [this] {
		showPage(Page::Map);
		compile();
	});
	// Sounds and Particles are docks while mapping (drag them into the map) and whole pages from
	// the side bar: showPage() moves the browser between its dock and its page.
	m_soundsPage = new QWidget;
	m_soundsPage->setObjectName("soundsPage");
	(new QVBoxLayout(m_soundsPage))->setContentsMargins(0, 0, 0, 0);
	m_particlesPage = new QWidget;
	m_particlesPage->setObjectName("particlesPage");
	(new QVBoxLayout(m_particlesPage))->setContentsMargins(0, 0, 0, 0);
	m_pages->addWidget(m_materialEditor);
	m_pages->addWidget(m_soundsPage);
	m_pages->addWidget(m_particlesPage);
	m_pages->addWidget(m_projectPage);

	// Left: the tool's options, the Outliner and
	// mapbase-fidelity's look per map as tabs. Right: only the properties of what is selected.
	auto* tool = addDock(tr("Tool"), m_toolPanel, Qt::LeftDockWidgetArea);
	auto* outliner = addDock(tr("Outliner"), m_outliner, Qt::LeftDockWidgetArea);
	m_mapLook = new MapLookPanel(m_editor);
	auto* mapLook = addDock(tr("Map Look"), m_mapLook, Qt::LeftDockWidgetArea);
	auto* visgroups = addDock(tr("VisGroups"), new VisGroupsPanel(m_editor), Qt::LeftDockWidgetArea);
	tabifyDockWidget(tool, outliner);
	tabifyDockWidget(tool, visgroups);
	tabifyDockWidget(tool, mapLook);
	tool->raise();
	auto* properties = addDock(tr("Object Properties"), m_properties, Qt::RightDockWidgetArea);
	auto* assets = addDock(tr("Asset Browser"), m_assets, Qt::BottomDockWidgetArea);
	m_assetsDock = assets;
	auto* sounds = addDock(tr("Sounds"), m_sounds, Qt::BottomDockWidgetArea);
	auto* particles = addDock(tr("Particles"), m_particles, Qt::BottomDockWidgetArea);
	m_soundsDock = sounds;
	m_particlesDock = particles;
	auto* compile = addDock(tr("Compile"), m_compileWindow, Qt::BottomDockWidgetArea);
	auto* problems = addDock(tr("Problems"), m_problems, Qt::BottomDockWidgetArea);
	auto* log = addDock(tr("Log"), m_log, Qt::BottomDockWidgetArea);
	tabifyDockWidget(assets, sounds);
	tabifyDockWidget(assets, particles);
	tabifyDockWidget(assets, compile);
	tabifyDockWidget(assets, problems);
	// Every undo step, next to the log (click one to go back or forward to it).
	auto* history = addDock(tr("History"), new HistoryPanel(m_editor), Qt::BottomDockWidgetArea);
	tabifyDockWidget(assets, history);
	// Materials: edit from the browser's menu or the face panel; saved changes show up everywhere.
	connect(m_assets, &AssetBrowser::materialsIndexed, this, [this] {
		// The Materials page opened before the list was ready.
		if (currentPage() == Page::Materials && m_materialEditor->materialName().isEmpty() && !m_assets->shownMaterial().isEmpty()) {
			m_materialEditor->openMaterial(m_assets->shownMaterial());
		}
	});
	// On the Materials page a click in the browser opens the material.
	connect(m_assets, &AssetBrowser::materialPicked, this, [this](const QString& name) {
		if (currentPage() == Page::Materials && name.compare(m_materialEditor->materialName(), Qt::CaseInsensitive) != 0) {
			m_materialEditor->openMaterial(name);
		}
	});
	connect(m_assets, &AssetBrowser::editMaterialRequested, this, [this](const QString& name) {
		m_materialEditor->openMaterial(name);
		raiseDock("Material Editor");
	});
	// "Edit soundscape" on a soundscape entity: the Sounds page with that soundscape open.
	connect(m_properties, &PropertiesPanel::editSoundscapeRequested, this, [this](const QString& name) {
		showPage(Page::Sounds);
		const auto found = m_sounds->tree()->findItems(name, Qt::MatchFixedString | Qt::MatchRecursive);
		if (!found.isEmpty()) m_sounds->tree()->setCurrentItem(found.front());
	});
	connect(m_properties, &PropertiesPanel::editMaterialRequested, this, [this](const QString& name) {
		m_materialEditor->openMaterial(name);
		raiseDock("Material Editor");
	});
	connect(m_materialEditor, &MaterialEditor::materialSaved, this, [this](const QString& name) {
		m_materials->reload(name);
		m_assets->materialChanged(name);
	});
	tabifyDockWidget(assets, log);
	connect(m_compileWindow, &CompileWindow::compileAgain, this, &MainWindow::compile);
	connect(m_compileWindow, &CompileWindow::showLeak, this, [this] {
		for (auto* view : m_views) {
			view->setLeakLine(m_lastLeak);
		}
		raiseDock("Problems");
	});
	assets->raise();
	resizeDocks({properties, tool}, {380, 300}, Qt::Horizontal);
	resizeDocks({assets}, {210}, Qt::Vertical);
	// Again once the window has its size: the map views get the most room.
	QTimer::singleShot(0, this, [this, assets, properties, tool] {
		resizeDocks({assets}, {std::max(180, height() / 5)}, Qt::Vertical);
		resizeDocks({properties, tool}, {380, 300}, Qt::Horizontal);
	});
	setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
	setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
	connect(m_toolPanel, &ToolPanel::wantsAttention, this, [tool] {
		tool->show();
		tool->raise();
	});
}

void MainWindow::showMaterialEditor() {
	showPage(Page::Materials);
}

void MainWindow::showPage(Page page) {
	QWidget* const widgets[] = {m_rows, m_materialEditor, m_soundsPage, m_particlesPage, m_projectPage};
	QWidget* target = widgets[int(page)];
	// The browsers that are docks while mapping move to their page and back.
	const auto borrow = [](QWidget* browser, QDockWidget* dock, QWidget* holder, bool toPage) {
		if (!browser || !dock) return;
		if (toPage && browser->parentWidget() != holder) {
			holder->layout()->addWidget(browser);
			browser->show();
		} else if (!toPage && dock->widget() != browser) {
			dock->setWidget(browser);
		}
	};
	borrow(m_assets, m_assetsDock, m_materialEditor->browserHolder(), page == Page::Materials);
	borrow(m_sounds, m_soundsDock, m_soundsPage, page == Page::Sounds);
	borrow(m_particles, m_particlesDock, m_particlesPage, page == Page::Particles);
	if (page == Page::Project) m_modPage->refresh();
	m_pages->setCurrentWidget(target);
	m_assets->setPageMode(page == Page::Materials);
	// The page never starts empty: without an open material it shows the browser's current one.
	if (page == Page::Materials && m_materialEditor->materialName().isEmpty() && !m_assets->shownMaterial().isEmpty()) {
		m_materialEditor->openMaterial(m_assets->shownMaterial());
	}
	setMapWorkspaceShown(page == Page::Map);
	if (m_pageGroup) {
		const auto actions = m_pageGroup->actions();
		if (int(page) < actions.size()) actions[int(page)]->setChecked(true);
	}
}

MainWindow::Page MainWindow::currentPage() const {
	QWidget* const widgets[] = {m_rows, m_materialEditor, m_soundsPage, m_particlesPage, m_projectPage};
	for (int i = 0; i < 5; ++i) {
		if (m_pages->currentWidget() == widgets[i]) return Page(i);
	}
	return Page::Map;
}

void MainWindow::setMapWorkspaceShown(bool shown) {
	if (shown == !m_mapHidden) return;
	m_mapHidden = !shown;
	if (!shown) {
		// A full tab: the map's docks and tool bars go away, and its keys (Delete, H, tools ...) must not
		// change the map unseen. File and Help keep working.
		for (auto* dock : std::as_const(m_docks)) {
			if (dock->isVisible()) {
				m_hiddenForTab << dock;
				dock->hide();
			}
		}
		for (auto* bar : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
			if (bar->isVisible() && bar != m_pageBar) {
				m_hiddenForTab << bar;
				bar->hide();
			}
		}
		QSet<QAction*> keep;
		for (auto* entry : menuBar()->actions()) {
			if (entry->menu() && (entry->text() == tr("&File") || entry->text() == tr("&Help"))) {
				for (auto* a : entry->menu()->actions()) keep.insert(a);
			}
		}
		for (auto* a : findChildren<QAction*>()) {
			if (a->menu() || a->shortcut().isEmpty() || keep.contains(a) || !a->isEnabled()) continue;
			if (a->objectName() == tr("Material Editor") || (m_pageGroup && m_pageGroup->actions().contains(a))) continue;
			a->setEnabled(false);
			m_disabledForTab << a;
		}
	} else {
		for (const auto& widget : std::as_const(m_hiddenForTab)) {
			if (widget) widget->show();
		}
		for (const auto& a : std::as_const(m_disabledForTab)) {
			if (a) a->setEnabled(true);
		}
		m_hiddenForTab.clear();
		m_disabledForTab.clear();
	}
}

void MainWindow::raiseDock(const QString& name) {
	if (name == tr("Material Editor") || name == "Material Editor") {
		showMaterialEditor();
		return;
	}
	static const QMap<QString, Page> PAGES = {{"Materials", Page::Materials}, {"Mod", Page::Project}, {"Project", Page::Project}};
	if (const auto it = PAGES.constFind(name); it != PAGES.constEnd()) {
		showPage(*it);
		return;
	}
	if (currentPage() != Page::Map) showPage(Page::Map);   // the docks belong to the map
	if (name == tr("Map") || name == "Map") return;
	for (auto* dock : m_docks) {
		if (dock->objectName() == name || dock->windowTitle() == name) {
			dock->show();
			dock->raise();
		}
	}
}

Viewport* MainWindow::activeView() const {
	return m_active ? m_active.data() : m_views[0];
}

void MainWindow::createActions() {
	const auto action = [this](const QString& text, const QKeySequence& key, auto slot) {
		auto* a = new QAction(text, this);
		a->setObjectName(text);   // key for the shortcut settings
		if (!key.isEmpty()) {
			a->setShortcut(key);
		}
		connect(a, &QAction::triggered, this, slot);
		addAction(a);
		return a;
	};

	// File
	auto* file = menuBar()->addMenu(tr("&File"));
	file->addAction(action(tr("New map"), QKeySequence::New, [this] {
		if (maybeSave()) m_editor->newMap();
	}));
	file->addAction(action(tr("New mod ..."), QKeySequence(), [this] { runNewModDialog(m_editor, this); }));
	file->addAction(action(tr("Open ..."), QKeySequence::Open, [this] {
		if (!maybeSave()) return;
		const QString dir = m_editor->gameKey().isEmpty() ? QString() : m_editor->gameSettings().mapDir;
		const QString path = QFileDialog::getOpenFileName(this, tr("Open map"), dir, tr("Valve map (*.vmf)"));
		if (!path.isEmpty()) openMap(path);
	}));
	file->addAction(action(tr("Save"), QKeySequence::Save, [this] { save(false); }));
	file->addAction(action(tr("Save as ..."), QKeySequence("Ctrl+Shift+S"), [this] { save(true); }));
	file->addSeparator();
	file->addAction(action(tr("Export for other programs ..."), QKeySequence("Ctrl+Alt+E"), [this] {
		// glTF / OBJ for Blender, Unity, Unreal: meters, Y up, textures as PNG, props included.
		QString filter;
		const QString base = m_editor->filePath().isEmpty() ? QStringLiteral("map") : QFileInfo(m_editor->filePath()).completeBaseName();
		const QString folder = m_editor->filePath().isEmpty() ? QDir::homePath() : QFileInfo(m_editor->filePath()).absolutePath();
		const QString path = QFileDialog::getSaveFileName(this, tr("Export map"), folder + "/" + base + ".gltf",
		                                                  tr("glTF 2.0 (*.gltf);;Wavefront OBJ (*.obj)"), &filter);
		if (path.isEmpty()) return;
		ExportOptions options;
		options.format = path.endsWith(".obj", Qt::CaseInsensitive) ? ExportFormat::Obj : ExportFormat::Gltf;
		QApplication::setOverrideCursor(Qt::WaitCursor);
		std::string error;
		const auto result = exportMap(m_editor->doc(), m_editor->entities(), m_editor->game() ? &m_editor->game()->files : nullptr, toPath(path), options, &error);
		QApplication::restoreOverrideCursor();
		if (!result) {
			QMessageBox::warning(this, tr("Export"), QString::fromStdString(error));
			return;
		}
		for (const auto& warning : result->warnings) m_log->append(QString::fromStdString(warning), QColor(255, 200, 100));
		emit m_editor->message(tr("Exported %1: %2 triangles, %3 materials, %4 textures, %5 models")
		                           .arg(QFileInfo(path).fileName())
		                           .arg(result->triangles)
		                           .arg(result->materials)
		                           .arg(result->textures)
		                           .arg(result->models));
	}));
	file->addSeparator();
	file->addAction(action(tr("Exit"), QKeySequence::Quit, [this] { close(); }));

	// Edit
	auto* editMenu = menuBar()->addMenu(tr("&Edit"));
	editMenu->addAction(action(tr("Undo"), QKeySequence::Undo, [this] { m_editor->undo(); }));
	// Find and replace (Hammer's Find Entities, Entity Report and Replace Textures, in one place).
	const auto frame = [this] { activeView()->frameSelection(); };
	auto* findEntities = action(tr("Find entities ..."), QKeySequence("Ctrl+F"), [this, frame] { showFindDialog(m_editor, this, frame, 0); });
	auto* entityReport = action(tr("Entity report ..."), QKeySequence(), [this, frame] { showFindDialog(m_editor, this, frame, 1); });
	auto* replaceMaterials = action(tr("Replace materials ..."), QKeySequence("Ctrl+Alt+R"), [this] {
		QStringList names;
		for (const auto& name : m_assets->materialNames()) names << QString::fromStdString(name);
		showReplaceMaterialsDialog(m_editor, this, names);
	});
	editMenu->addAction(action(tr("Redo"), QKeySequence("Ctrl+Y"), [this] { m_editor->redo(); }));
	editMenu->addSeparator();
	editMenu->addAction(findEntities);
	editMenu->addAction(entityReport);
	editMenu->addAction(replaceMaterials);
	action(tr("Redo (second key)"), QKeySequence("Ctrl+Shift+Z"), [this] { m_editor->redo(); });
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Cut"), QKeySequence::Cut, [this] { m_editor->cutSelection(); }));
	editMenu->addAction(action(tr("Copy"), QKeySequence::Copy, [this] { m_editor->copySelection(); }));
	editMenu->addAction(action(tr("Paste"), QKeySequence::Paste, [this] { pasteHere(); }));
	editMenu->addAction(action(tr("Paste special ..."), QKeySequence("Ctrl+Shift+V"), [this] { pasteSpecialDialog(); }));
	editMenu->addAction(action(tr("Delete"), QKeySequence::Delete, [this] { m_editor->deleteSelection(); }));
	editMenu->addAction(action(tr("Duplicate"), QKeySequence("Ctrl+D"), [this] { m_editor->duplicateSelection(); }));
	editMenu->addAction(action(tr("Select all"), QKeySequence::SelectAll, [this] { m_editor->selectAll(); }));
	editMenu->addAction(action(tr("Select nothing"), QKeySequence("Ctrl+Shift+D"), [this] { m_editor->clearSelection(); }));
	editMenu->addAction(action(tr("Select similar"), QKeySequence("Ctrl+Alt+O"), [this] { m_editor->selectSimilar(); }));
	editMenu->addAction(action(tr("Go to ID ..."), QKeySequence("Ctrl+Shift+G"), [this] {
		bool ok = false;
		const int id = QInputDialog::getInt(this, tr("Go to ID"), tr("Brush or entity id (as in the .vmf and in compile messages):"), 1, 1,
		                                    std::numeric_limits<int>::max(), 1, &ok);
		if (ok && m_editor->selectById(id)) {
			activeView()->frameSelection();
		}
	}));
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Apply current material"), QKeySequence("Shift+T"), [this] { m_editor->applyMaterialToSelection(m_editor->currentMaterial()); }));
	editMenu->addAction(action(tr("Tie to entity (func_detail)"), QKeySequence("Ctrl+T"), [this] { m_editor->tieSelectionToEntity("func_detail"); }));
	editMenu->addAction(action(tr("Move to world"), QKeySequence("Ctrl+Shift+W"), [this] { m_editor->moveSelectionToWorld(); }));
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Hide selection"), QKeySequence("H"), [this] { m_editor->hideSelection(); }));
	editMenu->addAction(action(tr("Hide others"), QKeySequence("Ctrl+H"), [this] { m_editor->hideUnselected(); }));
	editMenu->addAction(action(tr("Show all"), QKeySequence("U"), [this] { m_editor->unhideAll(); }));

	// Brushes: Hammer's carve / hollow, merging, rounding edges.
	auto* brushes = menuBar()->addMenu(tr("&Brushes"));
	auto* hollow = action(tr("Hollow ..."), QKeySequence("Ctrl+Shift+H"), [this] {
		bool ok = false;
		const double thickness = QInputDialog::getDouble(this, tr("Hollow"),
		                                                 tr("Wall thickness in units\n(negative: the walls go outside, the inside stays as it is)"),
		                                                 QSettings().value("brushes/hollow", 16.0).toDouble(), -4096, 4096, 0, &ok);
		if (ok) {
			QSettings().setValue("brushes/hollow", thickness);
			m_editor->hollowSelection(thickness);
		}
	});
	hollow->setToolTip(tr("Turns the selected brushes into walls around their inside (a room from a block)"));
	auto* carve = action(tr("Carve with selection"), QKeySequence("Ctrl+Shift+C"), [this] { m_editor->carveWithSelection(); });
	carve->setToolTip(tr("The selected brushes cut themselves out of every brush they touch (doors, windows)"));
	auto* mergeAction = action(tr("Merge into one brush"), QKeySequence("Ctrl+Shift+M"), [this] { m_editor->mergeSelection(); });
	auto* round = action(tr("Round or bevel edge ..."), QKeySequence("Ctrl+Shift+R"), [this] { roundEdgeDialog(); });
	round->setToolTip(tr("Select the two faces next to an edge (Faces mode, Ctrl+click), then round or bevel the edge"));
	brushes->addAction(hollow);
	brushes->addAction(carve);
	brushes->addAction(mergeAction);
	brushes->addAction(round);
	brushes->addSeparator();
	brushes->addSeparator();
	auto* flip = brushes->addMenu(tr("Flip"));
	flip->addAction(action(tr("Flip left/right (X)"), QKeySequence("Ctrl+L"), [this] { m_editor->flipSelection(0); }));
	flip->addAction(action(tr("Flip front/back (Y)"), QKeySequence("Ctrl+Alt+L"), [this] { m_editor->flipSelection(1); }));
	flip->addAction(action(tr("Flip up/down (Z)"), QKeySequence("Ctrl+I"), [this] { m_editor->flipSelection(2); }));
	auto* snapGrid = action(tr("Snap to grid"), QKeySequence("Ctrl+B"), [this] { m_editor->snapSelectionToGrid(); });
	snapGrid->setToolTip(tr("Moves the selection so its corner lies on the grid"));
	brushes->addAction(snapGrid);
	brushes->addAction(action(tr("Transform ..."), QKeySequence("Ctrl+Alt+M"), [this] { transformDialog(); }));
	brushes->addSeparator();
	auto* drop = action(tr("Drop to floor"), QKeySequence("Ctrl+Shift+F"), [this] { m_editor->dropSelectionToFloor(); });
	drop->setToolTip(tr("Lets the selected objects fall straight down onto what is below them"));
	brushes->addAction(drop);
	auto* track = action(tr("Select whole path"), QKeySequence("Ctrl+Shift+P"), [this] { m_editor->selectTrackChain(); });
	track->setToolTip(tr("Selects every path_track / path_corner of the selected path"));
	brushes->addAction(track);
	m_contextActions = {hollow, carve, mergeAction, round, drop, track, nullptr};
	for (auto* a : editMenu->actions()) {
		if (a->text().startsWith(tr("Tie to entity")) || a->text() == tr("Move to world") || a->text() == tr("Duplicate") || a->text() == tr("Delete") ||
		    a->text() == tr("Hide selection") || a->text() == tr("Apply current material")) {
			m_contextActions << a;
		}
	}

	// Nudging and rotating by keys.
	const auto nudge = [this](int axis, double sign) {
		return [this, axis, sign] {
			Vec3 delta;
			// Arrows move relative to the 3D camera's facing (snapped to the nearest world axis).
			if (axis < 2 && !activeView()->camera().is2D()) {
				const auto f = activeView()->camera().forward();
				const auto r = activeView()->camera().right();
				const QVector3D dir = axis == 0 ? r : QVector3D(f.x(), f.y(), 0);
				if (std::abs(dir.x()) >= std::abs(dir.y())) {
					delta.x = (dir.x() >= 0 ? 1 : -1) * sign * m_editor->grid();
				} else {
					delta.y = (dir.y() >= 0 ? 1 : -1) * sign * m_editor->grid();
				}
			} else {
				delta[axis] = sign * m_editor->grid();
			}
			m_editor->nudgeSelection(delta);
		};
	};
	action(tr("Nudge right"), QKeySequence(Qt::Key_Right), nudge(0, 1));
	action(tr("Nudge left"), QKeySequence(Qt::Key_Left), nudge(0, -1));
	action(tr("Nudge forward"), QKeySequence(Qt::Key_Up), nudge(1, 1));
	action(tr("Nudge back"), QKeySequence(Qt::Key_Down), nudge(1, -1));
	action(tr("Nudge up"), QKeySequence(Qt::Key_PageUp), nudge(2, 1));
	action(tr("Nudge down"), QKeySequence(Qt::Key_PageDown), nudge(2, -1));
	// Alt+arrows rotate by the rotation snap: left/right around Z (yaw), up/down around the
	// horizontal world axis that points most to the camera's right (pitch).
	const auto rotateStep = [this] { return m_editor->snapping() ? m_editor->rotationSnap() : 15.0; };
	action(tr("Rotate left"), QKeySequence("Alt+Left"), [this, rotateStep] { m_editor->rotateSelection(2, rotateStep()); });
	action(tr("Rotate right"), QKeySequence("Alt+Right"), [this, rotateStep] { m_editor->rotateSelection(2, -rotateStep()); });
	const auto pitchAxis = [this] {
		const auto r = activeView()->camera().right();
		return std::abs(r.x()) >= std::abs(r.y()) ? 0 : 1;
	};
	action(tr("Rotate up"), QKeySequence("Alt+Up"), [this, rotateStep, pitchAxis] { m_editor->rotateSelection(pitchAxis(), rotateStep()); });
	action(tr("Rotate down"), QKeySequence("Alt+Down"), [this, rotateStep, pitchAxis] { m_editor->rotateSelection(pitchAxis(), -rotateStep()); });

	// Side bar of pages, left of the tools.
	m_pageBar = new QToolBar(tr("Pages"));
	m_pageBar->setObjectName("Pages");
	m_pageBar->setMovable(false);
	m_pageBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	m_pageBar->setIconSize(QSize(30, 30));
	addToolBar(Qt::LeftToolBarArea, m_pageBar);
	addToolBarBreak(Qt::LeftToolBarArea);   // the tools get their own strip next to it
	m_pageGroup = new QActionGroup(this);
	const auto pageAction = [&](const QString& iconName, const QString& name, const QString& tip, Page page) {
		auto* a = new QAction(icon(iconName), name, this);
		a->setObjectName("page:" + name);
		a->setToolTip(tip);
		a->setCheckable(true);
		m_pageGroup->addAction(a);
		m_pageBar->addAction(a);
		connect(a, &QAction::triggered, this, [this, page] { showPage(page); });
		return a;
	};
	pageAction("page:map", tr("Map"), tr("The map: 3D and 2D views, tools and their panels"), Page::Map)->setChecked(true);
	pageAction("page:materials", tr("Materials"), tr("Material Editor: textures, shaders and mapbase-fidelity effects"), Page::Materials);
	pageAction("page:sounds", tr("Sounds"), tr("Sounds, sound scripts and soundscapes"), Page::Sounds);
	pageAction("page:particles", tr("Particles"), tr("Particle systems of the game"), Page::Particles);
	pageAction("page:mod", tr("Project"), tr("Your mod: overview, all maps (batch compile), main menu, source control, ship"), Page::Project);

	// Tools: a narrow strip of icons with their key in the corner.
	auto* tools = new QToolBar(tr("Tools"));
	tools->setObjectName("Tools");
	tools->setMovable(false);
	tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
	tools->setIconSize(QSize(26, 26));
	addToolBar(Qt::LeftToolBarArea, tools);
	m_toolGroup = new QActionGroup(this);
	m_toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);   // none checked in face mode
	const auto toolAction = [&](const QString& iconName, const QString& name, const QKeySequence& key, Editor::Tool tool) {
		const QString letter = key.toString().section('+', -1);
		auto* a = new QAction(icon(iconName, letter), name, this);
		a->setObjectName(name);
		a->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString()));
		a->setShortcut(key);
		a->setCheckable(true);
		a->setData(int(tool));
		m_toolGroup->addAction(a);
		tools->addAction(a);
		connect(a, &QAction::triggered, this, [this, tool] { m_editor->setTool(tool); });
		return a;
	};
	toolAction("tool:select", tr("Select"), QKeySequence("Q"), Editor::Tool::Select)->setChecked(true);
	toolAction("tool:move", tr("Move"), QKeySequence("T"), Editor::Tool::Move);
	toolAction("tool:rotate", tr("Rotate"), QKeySequence("R"), Editor::Tool::Rotate);
	toolAction("tool:scale", tr("Scale"), QKeySequence("E"), Editor::Tool::Scale);
	tools->addSeparator();
	toolAction("tool:block", tr("Block"), QKeySequence("Shift+B"), Editor::Tool::Block);
	toolAction("tool:entity", tr("Entity"), QKeySequence("Shift+E"), Editor::Tool::Entity);
	toolAction("tool:clip", tr("Clip"), QKeySequence("Shift+X"), Editor::Tool::Clip);
	toolAction("tool:vertex", tr("Vertex"), QKeySequence("Shift+V"), Editor::Tool::Vertex);
	toolAction("tool:polygon", tr("Polygon"), QKeySequence("Shift+P"), Editor::Tool::Polygon);
	m_modeGroup = new QActionGroup(this);
	const auto modeAction = [&](const QString& iconName, const QString& name, const QKeySequence& key, Editor::SelectMode mode) {
		auto* a = new QAction(icon(iconName), name, this);
		a->setObjectName(name);
		a->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString()));
		a->setShortcuts({key});
		a->setCheckable(true);
		m_modeGroup->addAction(a);
		connect(a, &QAction::triggered, this, [this, mode] { m_editor->setSelectMode(mode); });
		return a;
	};
	auto* facesMode = modeAction("mode:faces", tr("Faces"), QKeySequence("3"), Editor::SelectMode::Face);
	auto* objects = modeAction("mode:objects", tr("Objects"), QKeySequence("5"), Editor::SelectMode::Object);
	objects->setShortcuts({QKeySequence("5"), QKeySequence("4")});
	objects->setChecked(true);
	connect(m_editor, &Editor::toolChanged, this, [this] {
		const bool faces = m_editor->selectMode() == Editor::SelectMode::Face;
		for (auto* a : m_toolGroup->actions()) {
			a->setChecked(!faces && a->data().toInt() == int(m_editor->tool()));
		}
		m_modeGroup->actions()[m_editor->selectMode() == Editor::SelectMode::Face ? 0 : 1]->setChecked(true);
	});

	// Top bar
	auto* top = new QToolBar(tr("Main"));
	top->setObjectName("Main");
	top->setMovable(false);
	top->setIconSize(QSize(18, 18));
	top->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	addToolBar(Qt::TopToolBarArea, top);
	m_gameButton = new QToolButton;
	m_gameButton->setPopupMode(QToolButton::InstantPopup);
	m_gameButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_gameButton->setMinimumWidth(260);
	m_gameButton->setToolTip(tr("Game or mod to edit for (all Source 1 games and mods found on this PC)"));
	m_gameMenu = new QMenu(m_gameButton);
	m_gameMenu->setToolTipsVisible(true);
	m_gameButton->setMenu(m_gameMenu);
	top->addWidget(m_gameButton);
	const auto iconOnly = [top](QAction* a) {
		if (auto* button = qobject_cast<QToolButton*>(top->widgetForAction(a))) {
			button->setToolButtonStyle(Qt::ToolButtonIconOnly);
			button->setProperty("iconOnly", true);
		}
	};
	auto* gameSettings = top->addAction(icon("gear"), tr("Game settings"));
	iconOnly(gameSettings);
	gameSettings->setToolTip(tr("Game settings: FGDs, compile tools, game executable"));
	connect(gameSettings, &QAction::triggered, this, [this] {
		if (m_editor->gameKey().isEmpty()) return;
		GameSettingsDialog dialog(m_editor, m_editor->gameKey(), this);
		if (dialog.exec() == QDialog::Accepted) {
			m_editor->setGameSettings(m_editor->gameKey(), dialog.settings());
		}
	});
	top->addSeparator();
	for (const char* name : {"Undo", "Redo"}) {
		for (auto* a : findChildren<QAction*>()) {
			if (a->objectName() == tr(name)) {
				a->setIcon(icon(QString::fromLatin1(name).toLower()));
				top->addAction(a);
				iconOnly(a);
				break;
			}
		}
	}
	top->addSeparator();
	// Selection modes as one segmented row.
	top->addAction(objects);
	top->addAction(facesMode);
	top->widgetForAction(objects)->setProperty("segment", "first");
	top->widgetForAction(facesMode)->setProperty("segment", "last");
	top->addSeparator();
	auto* gridIcon = new QLabel;
	gridIcon->setPixmap(icon("grid").pixmap(16, 16));
	gridIcon->setToolTip(tr("Grid size ([ and ])"));
	top->addWidget(gridIcon);
	m_gridCombo = new QComboBox;
	for (const double g : {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0}) {   // microgrid like Hammer++
		m_gridCombo->addItem(QString::number(g), g);
	}
	m_gridCombo->setCurrentText(QString::number(m_editor->grid()));
	top->addWidget(m_gridCombo);
	connect(m_gridCombo, &QComboBox::activated, this, [this](int index) { m_editor->setGrid(m_gridCombo->itemData(index).toDouble()); });
	connect(m_editor, &Editor::settingsChanged, this, [this] { m_gridCombo->setCurrentText(QString::number(m_editor->grid())); });
	action(tr("Grid smaller"), QKeySequence("["), [this] { m_editor->setGrid(m_editor->grid() / 2); });
	action(tr("Grid bigger"), QKeySequence("]"), [this] { m_editor->setGrid(m_editor->grid() * 2); });
	auto* snapAction = top->addAction(icon("snap"), tr("Snap"));
	snapAction->setObjectName("Snap");
	snapAction->setCheckable(true);
	snapAction->setChecked(m_editor->snapping());
	snapAction->setShortcut(QKeySequence("Shift+W"));
	snapAction->setToolTip(tr("Snap to the grid and to the rotation step (Shift+W)"));
	connect(snapAction, &QAction::toggled, m_editor, &Editor::setSnapping);
	auto* rotateIcon = new QLabel;
	rotateIcon->setPixmap(icon("tool:rotate").pixmap(16, 16));
	top->addWidget(rotateIcon);
	auto* rotateCombo = new QComboBox;
	rotateCombo->setToolTip(tr("Rotation snap for the rotate gizmo and Alt+arrow keys (Ctrl+[ / Ctrl+])"));
	static const double ROTATION_STEPS[] = {1, 5, 15, 22.5, 45, 90};
	for (const double step : ROTATION_STEPS) {
		rotateCombo->addItem(QStringLiteral("%1°").arg(step), step);
	}
	rotateCombo->setCurrentIndex(std::max(0, rotateCombo->findData(m_editor->rotationSnap())));
	top->addWidget(rotateCombo);
	connect(rotateCombo, &QComboBox::activated, this, [this, rotateCombo](int index) { m_editor->setRotationSnap(rotateCombo->itemData(index).toDouble()); });
	connect(m_editor, &Editor::settingsChanged, this, [this, rotateCombo, snapAction] {
		const int index = rotateCombo->findData(m_editor->rotationSnap());
		if (index >= 0) rotateCombo->setCurrentIndex(index);
		snapAction->setChecked(m_editor->snapping());
	});
	const auto stepRotation = [this](int direction) {
		return [this, direction] {
			const auto* begin = std::begin(ROTATION_STEPS);
			const auto* end = std::end(ROTATION_STEPS);
			const auto* it = std::find(begin, end, m_editor->rotationSnap());
			const auto index = it == end ? 2 : std::clamp<long>(static_cast<long>(it - begin) + direction, 0, static_cast<long>(end - begin) - 1);
			m_editor->setRotationSnap(ROTATION_STEPS[index]);
		};
	};
	action(tr("Rotation snap smaller"), QKeySequence("Ctrl+["), stepRotation(-1));
	action(tr("Rotation snap bigger"), QKeySequence("Ctrl+]"), stepRotation(1));
	m_textureLock = top->addAction(icon("lock"), tr("Texture lock"));
	m_textureLock->setCheckable(true);
	m_textureLock->setChecked(m_editor->textureLock());
	m_textureLock->setToolTip(tr("Textures stay glued to faces when moving/rotating"));
	connect(m_textureLock, &QAction::toggled, m_editor, &Editor::setTextureLock);
	top->addSeparator();

	m_renderGroup = new QActionGroup(this);
	// Render mode and shadows live in each view's corner menu; here they keep their keys (View menu).
	const auto renderAction = [&](const QString& name, const QKeySequence& key, Editor::RenderMode mode, const QString& tip) {
		auto* a = new QAction(name, this);
		addAction(a);
		a->setObjectName(name);
		a->setShortcut(key);
		a->setCheckable(true);
		a->setToolTip(QStringLiteral("%1 (%2)").arg(tip, key.toString()));
		m_renderGroup->addAction(a);
		connect(a, &QAction::triggered, this, [this, mode] { m_editor->setRenderMode(mode); });
		return a;
	};
	renderAction(tr("Lit"), QKeySequence("F6"), Editor::RenderMode::Lit, tr("Lighting preview from the light entities"))->setChecked(true);
	renderAction(tr("Unlit"), QKeySequence("F5"), Editor::RenderMode::Unlit, tr("Textures without lighting"));
	renderAction(tr("Wire"), QKeySequence("F8"), Editor::RenderMode::Wireframe, tr("Wireframe"));
	auto* shadows = new QAction(tr("Shadows"), this);
	addAction(shadows);
	shadows->setObjectName("Shadows");
	shadows->setCheckable(true);
	shadows->setChecked(m_editor->shadows());
	shadows->setShortcut(QKeySequence("F7"));
	shadows->setToolTip(tr("Real-time shadows in the lighting preview: sun, nearest spot and point lights (F7)"));
	connect(shadows, &QAction::toggled, m_editor, &Editor::setShadows);
	connect(m_editor, &Editor::settingsChanged, this, [this, shadows] {
		// The views' corner menus change these too.
		const auto actions = m_renderGroup->actions();
		const int index = static_cast<int>(m_editor->renderMode());
		if (index < actions.size()) actions[index]->setChecked(true);
		if (shadows->isChecked() != m_editor->shadows()) shadows->setChecked(m_editor->shadows());
	});
	auto* layout = top->addAction(icon("views"), tr("1 / 4 views"));
	layout->setObjectName("1 / 4 views");
	layout->setShortcut(QKeySequence("Shift+Alt+Z"));
	layout->setToolTip(tr("Switch between one 3D view and 3D + top/front/side (Shift+Alt+Z)"));
	connect(layout, &QAction::triggered, this, [this] { setQuadLayout(!m_quad); });
	top->addSeparator();
	// What the views show: kinds of objects plus names, connection lines and the 3D grid.
	const auto viewToggle = [&](const QString& iconName, const QString& tip, std::function<bool()> get, std::function<void(bool)> set) {
		auto* a = top->addAction(icon(iconName), tip);
		a->setObjectName(QStringLiteral("view:") + iconName.section(':', 1));
		a->setToolTip(tip);
		a->setCheckable(true);
		a->setChecked(get());
		connect(a, &QAction::toggled, this, [set](bool on) { set(on); });
		connect(m_editor, &Editor::settingsChanged, a, [a, get] { a->setChecked(get()); });
		connect(m_editor, &Editor::viewFilterChanged, a, [a, get] { a->setChecked(get()); });
		iconOnly(a);
	};
	const auto kindToggle = [&](const QString& iconName, const QString& tip, ObjectKind kind) {
		viewToggle(iconName, tip, [this, kind] { return m_editor->kindVisible(kind); }, [this, kind](bool on) { m_editor->setKindVisible(kind, on); });
	};
	// One click (Ctrl+Alt+T) hides what is in the way while building: triggers and tool brushes
	// (clip, nodraw, hint, skip ...), like Source 2 Hammer's tool material filter.
	auto* hideTools = top->addAction(icon("view:tools"), tr("Hide tools"));
	hideTools->setObjectName("hideTools");
	hideTools->setCheckable(true);
	hideTools->setShortcut(QKeySequence("Ctrl+Alt+T"));
	hideTools->setToolTip(tr("Hide triggers, clips and other tool brushes while building (Ctrl+Alt+T) - they stay in the map"));
	const auto toolsHidden = [this] { return !m_editor->kindVisible(ObjectKind::Triggers) && !m_editor->kindVisible(ObjectKind::ToolBrushes); };
	hideTools->setChecked(toolsHidden());
	connect(hideTools, &QAction::toggled, this, [this, toolsHidden](bool hide) {
		if (hide == toolsHidden()) return;
		m_editor->setKindVisible(ObjectKind::Triggers, !hide);
		m_editor->setKindVisible(ObjectKind::ToolBrushes, !hide);
	});
	connect(m_editor, &Editor::viewFilterChanged, hideTools, [hideTools, toolsHidden] { hideTools->setChecked(toolsHidden()); });
	kindToggle("view:helpers", tr("Show helper entities (logic, info, paths, sounds ...)"), ObjectKind::Helpers);
	kindToggle("page:models", tr("Show models (props)"), ObjectKind::Models);
	kindToggle("tool:entity", tr("Show lights"), ObjectKind::Lights);
	kindToggle("view:cubemaps", tr("Show cubemaps and their room boxes"), ObjectKind::Cubemaps);
	kindToggle("view:triggers", tr("Show triggers"), ObjectKind::Triggers);
	kindToggle("view:tools", tr("Show tool brushes and faces (clip, nodraw, hint ...)"), ObjectKind::ToolBrushes);
	kindToggle("view:sky", tr("Show sky brushes"), ObjectKind::Sky);
	viewToggle("view:names", tr("Entity names in the 2D views"), [this] { return m_editor->showEntityNames(); },
	           [this](bool on) { m_editor->setShowEntityNames(on); });
	viewToggle("view:connections", tr("Lines between the selected entities and what their outputs fire at"), [this] { return m_editor->showConnections(); },
	           [this](bool on) { m_editor->setShowConnections(on); });
	viewToggle("view:grid3d", tr("Grid in the 3D view"), [this] { return m_editor->showGrid3D(); }, [this](bool on) { m_editor->setShowGrid3D(on); });
	auto* showMore = new QToolButton;
	showMore->setObjectName("showMenuButton");
	showMore->setIcon(icon("eye"));
	showMore->setText(tr("Show"));
	showMore->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	showMore->setToolTip(tr("Show or hide each kind of object: world, brush entities, triggers, tool brushes, sky, models, NPCs, lights, cubemaps, helpers"));
	showMore->setPopupMode(QToolButton::InstantPopup);
	auto* showMenu = new QMenu(showMore);
	connect(showMenu, &QMenu::aboutToShow, this, [this, showMenu] {
		showMenu->clear();
		Viewport::fillShowMenu(showMenu, m_editor);
	});
	showMore->setMenu(showMenu);
	top->addWidget(showMore);
	auto* spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	top->addWidget(spacer);
	auto* check = top->addAction(icon("check"), tr("Check"));
	check->setToolTip(tr("Check the map for problems"));
	connect(check, &QAction::triggered, this, [this] {
		m_problems->runChecks();
		raiseDock("Problems");
	});
	auto* run = top->addAction(icon("run"), tr("Run map"));
	run->setObjectName("Compile and run");
	run->setShortcut(QKeySequence("F9"));
	run->setToolTip(tr("Compile with VBSP/VVIS/VRAD and start the game (F9)"));
	connect(run, &QAction::triggered, this, &MainWindow::compile);
	top->widgetForAction(run)->setProperty("accent", true);
	top->addSeparator();
	// Panels on and off.
	const auto panelToggle = [&](const QString& iconName, const QString& tip, std::function<QList<QDockWidget*>()> docks) {
		auto* a = top->addAction(icon(iconName), tip);
		a->setObjectName(tip);
		a->setToolTip(tip);
		connect(a, &QAction::triggered, this, [docks] {
			const auto list = docks();
			const bool anyShown = std::any_of(list.begin(), list.end(), [](QDockWidget* d) { return d->isVisible(); });
			for (auto* d : list) d->setVisible(!anyShown);
			if (!anyShown && !list.isEmpty()) list.front()->raise();
		});
		iconOnly(a);
	};
	const auto docksIn = [this](Qt::DockWidgetArea area) {
		QList<QDockWidget*> out;
		for (auto* d : m_docks) {
			if (dockWidgetArea(d) == area) out << d;
		}
		return out;
	};
	panelToggle("panel:left", tr("Left panel"), [docksIn] { return docksIn(Qt::LeftDockWidgetArea); });
	panelToggle("panel:bottom", tr("Bottom panel"), [docksIn] { return docksIn(Qt::BottomDockWidgetArea); });
	panelToggle("panel:right", tr("Right panel"), [docksIn] { return docksIn(Qt::RightDockWidgetArea); });

	// View
	auto* view = menuBar()->addMenu(tr("&View"));
	view->addAction(action(tr("Material Editor"), QKeySequence(), [this] { showMaterialEditor(); }));
	view->addSeparator();
	view->addAction(layout);
	view->addAction(action(tr("Frame selection"), QKeySequence("F"), [this] { activeView()->frameSelection(); }));
	action(tr("Frame selection"), QKeySequence("Ctrl+Shift+E"), [this] { activeView()->frameSelection(); });
	view->addAction(action(tr("Top view"), QKeySequence("F2"), [this] { activeView()->setCameraKind(Camera::Kind::Top); }));
	view->addAction(action(tr("Front view"), QKeySequence("F3"), [this] { activeView()->setCameraKind(Camera::Kind::Front); }));
	view->addAction(action(tr("Side view"), QKeySequence("F4"), [this] { activeView()->setCameraKind(Camera::Kind::Side); }));
	view->addAction(action(tr("3D view"), QKeySequence("Ctrl+F1"), [this] { activeView()->setCameraKind(Camera::Kind::Perspective); }));
	view->addSeparator();
	auto* show = view->addMenu(tr("Show"));
	connect(show, &QMenu::aboutToShow, this, [this, show] {
		show->clear();
		Viewport::fillShowMenu(show, m_editor);
	});
	view->addSeparator();
	view->addActions(m_renderGroup->actions());
	view->addAction(shadows);
	// Frame limit: the map views never draw faster than this (saves the graphics card).
	auto* fpsMenu = view->addMenu(tr("Frame rate limit"));
	fpsMenu->setObjectName("fpsMenu");
	auto* fpsGroup = new QActionGroup(fpsMenu);
	for (const int fps : {30, 60, 120, 144, 200, 240, 0}) {
		auto* a = fpsMenu->addAction(fps > 0 ? tr("%1 fps").arg(fps) : tr("No limit"));
		a->setCheckable(true);
		a->setData(fps);
		a->setChecked(m_editor->maxFps() == fps);
		fpsGroup->addAction(a);
		connect(a, &QAction::triggered, this, [this, fps] { m_editor->setMaxFps(fps); });
	}
	connect(fpsMenu, &QMenu::aboutToShow, fpsMenu, [this, fpsGroup] {
		for (auto* a : fpsGroup->actions()) a->setChecked(a->data().toInt() == m_editor->maxFps());
	});
	view->addSeparator();
	for (auto* dock : m_docks) {
		view->addAction(dock->toggleViewAction());
	}

	// Map
	auto* map = menuBar()->addMenu(tr("&Map"));
	// Hammer's Map Properties: the world's keys (sky, detail sprites, ...) in Object Properties.
	map->addAction(action(tr("Map properties ..."), QKeySequence(), [this] {
		m_editor->clearSelection();
		raiseDock("Object Properties");
	}));
	map->addAction(check);
	map->addAction(run);
	map->addAction(action(tr("Load pointfile (.lin) ..."), QKeySequence(), [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Load pointfile"), QFileInfo(m_editor->filePath()).absolutePath(), tr("Pointfile (*.lin)"));
		QFile file(path);
		if (file.open(QIODevice::ReadOnly)) {
			const QByteArray text = file.readAll();
			const auto line = readPointfile(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
			m_problems->addLeak(line);
			for (auto* v : m_views) v->setLeakLine(line);
			raiseDock("Problems");
		}
	}));
	map->addAction(action(tr("Clear leak line"), QKeySequence(), [this] {
		for (auto* v : m_views) v->setLeakLine({});
	}));
	map->addSeparator();
	// Cordons: compile only a part of the map (several boxes, like CS:GO Hammer and Hammer++).
	map->addAction(action(tr("Add cordon around selection"), QKeySequence("Ctrl+Shift+K"), [this] {
		auto bounds = edit::selectionBounds(m_editor->doc(), m_editor->selection(), m_editor->entities());
		if (bounds.empty()) {
			QMessageBox::information(this, tr("Cordon"), tr("Select what should be compiled first."));
			return;
		}
		const double grid = std::max(16.0, m_editor->grid());
		bounds.min = snap(bounds.min - Vec3{grid, grid, grid}, grid);
		bounds.max = snap(bounds.max + Vec3{grid, grid, grid}, grid);
		auto cordons = m_editor->cordons();
		cordons.cordons.push_back({"cordon " + std::to_string(cordons.cordons.size() + 1), bounds, true});
		cordons.active = true;
		m_editor->setCordons(cordons);
		emit m_editor->message(tr("Cordon added - compiling now only builds what is inside the cordons (Map > Cordons on/off)"));
	}));
	auto* cordonToggle = action(tr("Cordons on (compile only inside)"), QKeySequence(), [this] {
		auto cordons = m_editor->cordons();
		cordons.active = !cordons.active;
		m_editor->setCordons(cordons);
	});
	cordonToggle->setCheckable(true);
	connect(m_editor, &Editor::documentChanged, cordonToggle, [this, cordonToggle] {
		const auto cordons = m_editor->cordons();
		cordonToggle->setEnabled(!cordons.cordons.empty());
		cordonToggle->setChecked(cordons.active);
	});
	connect(m_editor, &Editor::documentReplaced, cordonToggle, [this, cordonToggle] {
		const auto cordons = m_editor->cordons();
		cordonToggle->setEnabled(!cordons.cordons.empty());
		cordonToggle->setChecked(cordons.active);
	});
	map->addAction(cordonToggle);
	map->addAction(action(tr("Remove all cordons"), QKeySequence(), [this] { m_editor->setCordons({}); }));

	auto* help = menuBar()->addMenu(tr("&Help"));
	help->addAction(action(tr("Shortcuts"), QKeySequence("F1"), [this] { QMessageBox::information(this, tr("Shortcuts"), SHORTCUT_HELP); }));
	help->addAction(action(tr("Change shortcuts ..."), QKeySequence(), [this] {
		ShortcutDialog dialog(this, this);
		dialog.exec();
	}));
	help->addSeparator();
	help->addAction(action(tr("Check for updates"), QKeySequence(), [this] { checkForUpdates(true); }));
	auto* autoCheck = action(tr("Check for updates at start"), QKeySequence(), [](bool) {});
	autoCheck->setCheckable(true);
	autoCheck->setChecked(QSettings().value("update/checkOnStart", true).toBool());
	connect(autoCheck, &QAction::toggled, this, [](bool on) { QSettings().setValue("update/checkOnStart", on); });
	help->addAction(autoCheck);
	help->addAction(action(tr("About"), QKeySequence(), [this] {
		QMessageBox::about(this, tr("About"),
		                   tr("<b>Mapsmith</b> %1<br>A modern map editor for Source 1 games and mods.<br>"
		                      "<a href=\"https://github.com/29j2003/hammer-test\">github.com/29j2003/hammer-test</a>")
		                       .arg(QApplication::applicationVersion()));
	}));
	// Defaults remembered, then the mapper's own keys applied.
	for (auto* a : findChildren<QAction*>()) {
		if (!a->objectName().isEmpty()) {
			a->setProperty("defaultShortcut", a->shortcut().toString(QKeySequence::PortableText));
		}
	}
	ShortcutDialog::applySaved(this);
}

void MainWindow::refreshGames() {
	// Menu like Steam's library: games grouped by their Steam app, mods grouped by the game they
	// are based on, and a search field on top that lists every match directly.
	m_gameMenu->clear();
	const auto& games = m_editor->discovery().games;
	if (games.empty()) {
		m_gameMenu->addAction(tr("No Source games found"))->setEnabled(false);
		m_gameButton->setText(tr("No Source games found"));
		return;
	}
	const auto makeAction = [this](QMenu* menu, const GameEntry& game, const QString& text) {
		auto* action = menu->addAction(text);
		QString tip = QString::fromStdString(displayPath(game.gameDir));
		if (!game.branch.empty()) tip += QStringLiteral("\n") + QString::fromStdString(game.branch);
		action->setToolTip(tip);
		action->setCheckable(true);
		action->setChecked(QString::fromStdString(game.key()) == m_editor->gameKey());
		const QString key = QString::fromStdString(game.key());
		connect(action, &QAction::triggered, this, [this, key] { selectGame(key); });
		return action;
	};
	const auto entryText = [this](const GameEntry& game) {
		QString text = QString::fromStdString(game.label);
		if (game.usesMapbase) text += tr("   [Mapbase]");
		return text;
	};

	auto* search = new QLineEdit;
	search->setPlaceholderText(tr("Search games and mods..."));
	search->setClearButtonEnabled(true);
	auto* searchAction = new QWidgetAction(m_gameMenu);
	searchAction->setDefaultWidget(search);
	m_gameMenu->addAction(searchAction);
	QList<QAction*> matches;
	for (const auto& game : games) {
		auto* action = makeAction(m_gameMenu, game, entryText(game) + QStringLiteral("   - ") +
		                                                QString::fromStdString(game.isSourceMod ? "sourcemods/" + game.folder() : game.appName));
		action->setVisible(false);
		action->setData(QString::fromStdString(game.label + " " + game.folder() + " " + game.appName + " " + game.branch).toLower());
		matches.push_back(action);
	}
	QList<QAction*> groups;
	groups.push_back(m_gameMenu->addSection(tr("Steam games")));
	std::map<std::string, std::vector<const GameEntry*>> byApp, modsByBase;
	for (const auto& game : games) {
		(game.isSourceMod ? modsByBase : byApp)[game.appName].push_back(&game);
	}
	for (const auto& [app, list] : byApp) {
		if (list.size() == 1) {
			groups.push_back(makeAction(m_gameMenu, *list.front(), entryText(*list.front())));
			continue;
		}
		auto* sub = m_gameMenu->addMenu(QString::fromStdString(app.empty() ? "Other" : app));
		sub->setToolTipsVisible(true);
		groups.push_back(sub->menuAction());
		for (const auto* game : list) {
			QString text = QStringLiteral("%1   (%2)").arg(QString::fromStdString(game->title), QString::fromStdString(game->folder()));
			if (game->baseContent) {
				// hl2/ inside Entropy : Zero 2 and the like: only content, the game is the other folder.
				text += tr("   - base content, not playable");
			} else if (game->usesMapbase) {
				text += tr("   [Mapbase]");
			}
			auto* action = makeAction(sub, *game, text);
			if (game->baseContent) {
				QFont font = action->font();
				font.setItalic(true);
				action->setFont(font);
			}
		}
	}
	if (!modsByBase.empty()) {
		groups.push_back(m_gameMenu->addSection(tr("Source mods")));
		for (const auto& [base, list] : modsByBase) {
			QMenu* target = m_gameMenu;
			if (modsByBase.size() > 1) {
				target = m_gameMenu->addMenu(tr("Based on %1").arg(QString::fromStdString(base.empty() ? "unknown game" : base)));
				target->setToolTipsVisible(true);
				groups.push_back(target->menuAction());
			}
			for (const auto* game : list) {
				auto* action = makeAction(target, *game, entryText(*game));
				if (target == m_gameMenu) groups.push_back(action);
			}
		}
	}
	connect(search, &QLineEdit::textChanged, m_gameMenu, [matches, groups](const QString& text) {
		const QString needle = text.trimmed().toLower();
		for (auto* action : groups) action->setVisible(needle.isEmpty());
		for (auto* action : matches) action->setVisible(!needle.isEmpty() && action->data().toString().contains(needle));
	});
	connect(m_gameMenu, &QMenu::aboutToShow, search, [search] {
		search->clear();
		QTimer::singleShot(0, search, [search] { search->setFocus(); });
	});
	const GameEntry* current = m_editor->gameEntry();
	m_gameButton->setText(current ? entryText(*current) + QStringLiteral("  ▾") : tr("Choose a game...  ▾"));
}

void MainWindow::selectGame(const QString& key) {
	QString error;
	if (!m_editor->openGame(key, &error)) {
		m_log->append(error, QColor(255, 110, 110));
	}
}

bool MainWindow::openMap(const QString& path) {
	QString error;
	if (!openMapWithRecovery(m_editor, this, path, &error)) {
		if (error.isEmpty()) return false;   // the mapper chose not to open it
		QMessageBox::warning(this, tr("Open map"), tr("Could not open %1:\n%2").arg(path, error));
		return false;
	}
	updateTitle();
	return true;
}

void MainWindow::setQuadLayout(bool quad) {
	m_quad = quad;
	m_views[1]->setVisible(quad);
	m_columns[1]->setVisible(quad);
	QSettings().setValue("window/quad", quad);
	if (quad) {
		m_rows->setSizes({1, 1});
		m_columns[0]->setSizes({1, 1});
		m_columns[1]->setSizes({1, 1});
		for (auto* view : m_views) {
			view->frameSelection();
		}
	}
}

void MainWindow::updateTitle() {
	const QString file = m_editor->filePath().isEmpty() ? tr("untitled") : QFileInfo(m_editor->filePath()).fileName();
	const QString game = m_editor->game() ? QString::fromStdString(m_editor->game()->entry.title) : tr("no game");
	setWindowTitle(QStringLiteral("%1%2 - %3 - Mapsmith").arg(file, m_editor->modified() ? "*" : "", game));
}

bool MainWindow::save(bool saveAs) {
	QString path = m_editor->filePath();
	if (saveAs || path.isEmpty()) {
		QString start = m_editor->filePath();
		if (start.isEmpty()) {
			// The game's map folder (made if needed: a missing folder makes the dialog offer
			// its name as the file name) and a real file name.
			const QString dir = m_editor->gameKey().isEmpty() ? QString() : m_editor->gameSettings().mapDir;
			if (!dir.isEmpty()) {
				QDir().mkpath(dir);
			}
			start = (dir.isEmpty() ? QDir::homePath() : dir) + "/untitled.vmf";
		}
		path = QFileDialog::getSaveFileName(this, tr("Save map"), start, tr("Valve map (*.vmf)"));
		if (path.isEmpty()) {
			return false;
		}
		if (!path.endsWith(".vmf", Qt::CaseInsensitive)) {
			path += ".vmf";
		}
	}
	QString error;
	if (!m_editor->saveMap(path, &error)) {
		QMessageBox::warning(this, tr("Save map"), tr("Could not save:\n%1").arg(error));
		return false;
	}
	updateTitle();
	return true;
}

bool MainWindow::maybeSave() {
	if (!m_editor->modified()) {
		return true;
	}
	const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save the changes to the map first?"),
	                                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	if (answer == QMessageBox::Save) {
		return save(false);
	}
	return answer == QMessageBox::Discard;
}

void MainWindow::compile() {
	if (!m_editor->game()) {
		QMessageBox::information(this, tr("Compile"), tr("Choose a game first."));
		return;
	}
	if (m_compiler.running()) {
		m_compiler.cancel();
		return;
	}
	if (const auto& entry = m_editor->game()->entry; entry.baseContent) {
		// hl2/ inside Entropy : Zero 2: the game would stop with "Could not load library client".
		if (QMessageBox::question(this, tr("Compile"),
		                          tr("%1 is only base content of %2, not a game of its own - the game will not start with it.\n"
		                             "Choose %2's own folder in the game list. Compile anyway?")
		                              .arg(QString::fromStdString(entry.folder()), QString::fromStdString(entry.appName))) != QMessageBox::Yes) {
			return;
		}
	}
	m_problems->runChecks();
	if (m_problems->errorCount() > 0) {
		raiseDock("Problems");
		if (QMessageBox::question(this, tr("Compile"), tr("The map has %1 error(s) (see Problems). Compile anyway?").arg(m_problems->errorCount())) !=
		    QMessageBox::Yes) {
			return;
		}
	}
	if ((m_editor->modified() || m_editor->filePath().isEmpty()) && !save(false)) {
		return;
	}
	const QString gameDir = QString::fromStdString(displayPath(m_editor->game()->entry.gameDir));
	CompileDialog dialog(m_editor->filePath(), gameDir, m_editor->gameSettings(), this, m_editor->gameKey());
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	for (auto* view : m_views) {
		view->setLeakLine({});
	}
	raiseDock("Compile");
	QString vmf = m_editor->filePath();
	const auto cordons = m_editor->cordons();
	if (cordons.active && std::any_of(cordons.cordons.begin(), cordons.cordons.end(), [](const Cordon& c) { return c.active; })) {
		// Like Hammer: only what is inside the cordons, sealed with sky brushes (map name + "_cordon").
		const QFileInfo info(vmf);
		vmf = info.absolutePath() + "/" + info.completeBaseName() + "_cordon.vmf";
		QFile file(vmf);
		const auto text = cordonedCopy(m_editor->doc(), cordons, m_editor->entities()).serialize();
		if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())) {
			QMessageBox::warning(this, tr("Compile"), tr("Could not write %1").arg(vmf));
			return;
		}
		m_log->append(tr("Cordons are on: compiling only what is inside them as %1").arg(QFileInfo(vmf).fileName()), QColor(255, 200, 100));
	}
	m_log->append(tr("--- Compiling %1 ---").arg(QFileInfo(vmf).fileName()), QColor(140, 180, 255));
	m_compiler.start(vmf, gameDir, m_editor->gameSettings(), dialog.options());
}

void MainWindow::showContextMenu(const QPoint& globalPos) {
	QMenu menu(this);
	const auto& selection = m_editor->selection();
	if (selection.empty()) {
		menu.addAction(tr("Nothing selected - right click an object"))->setEnabled(false);
	} else {
		menu.addSection(selection.faces.empty() ? tr("%1 selected").arg(selection.solids.size() + selection.entities.size())
		                                        : tr("%1 face(s)").arg(selection.faces.size()));
		for (auto* a : m_contextActions) {
			if (a) {
				menu.addAction(a);
			} else {
				menu.addSeparator();
			}
		}
		menu.addSeparator();
		menu.addAction(tr("Frame selection (F)"), this, [this] { activeView()->frameSelection(); });
	}
	// Edges picked with the vertex tool: the same as the Edge box of the tool panel.
	if (m_editor->tool() == Editor::Tool::Vertex) {
		if (const auto edges = m_editor->selectedEdges().size(); edges > 0) {
			const double size = QSettings().value("brushes/edgeSize", 16.0).toDouble();
			const int segments = std::max(2, QSettings().value("brushes/roundSegments", 4).toInt());
			menu.addSection(tr("%n edge(s)", nullptr, int(edges)));
			menu.addAction(tr("Make wedge"), this, [this] { m_editor->shapeSelectedEdges(edit::EdgeShape::Wedge, 0, 0); });
			menu.addAction(tr("Bevel (%1 units)").arg(size), this, [this, size] { m_editor->shapeSelectedEdges(edit::EdgeShape::Bevel, size, 0); });
			menu.addAction(tr("Round (%1 units, %2 segments)").arg(size).arg(segments), this,
			               [this, size, segments] { m_editor->shapeSelectedEdges(edit::EdgeShape::Round, size, segments); });
		}
	}
	menu.exec(globalPos);
}

void MainWindow::updateStatusInfo() {
	const auto& selection = m_editor->selection();
	const auto& doc = m_editor->doc();
	QString what;
	if (!selection.faces.empty()) {
		what = tr("%n face(s)", nullptr, int(selection.faces.size()));
	} else if (selection.solids.size() == 1 && selection.entities.empty()) {
		const auto* solid = doc.findSolid(selection.solids.front());
		what = tr("Brush (%n faces)", nullptr, solid ? int(solid->sides.size()) : 0);
	} else if (selection.solids.empty() && selection.entities.size() == 1) {
		const auto* entity = doc.findEntity(selection.entities.front());
		what = entity ? QString::fromStdString(entity->classname()) : QString();
		if (entity && !entity->get("targetname").empty()) what += QStringLiteral(" \"%1\"").arg(QString::fromStdString(entity->get("targetname")));
	} else if (selection.hasObjects()) {
		what = tr("%1 brushes, %2 entities").arg(selection.solids.size()).arg(selection.entities.size());
	} else {
		what = tr("no selection");
	}
	m_selectionLabel->setText(what);
	const auto box = selection.empty() ? Bounds{} : edit::selectionBounds(doc, selection, m_editor->entities());
	if (box.empty()) {
		m_sizeLabel->clear();
	} else {
		const Vec3 size = box.size(), center = box.center();
		const auto number = [](double v) { return QString::number(std::round(v * 100) / 100, 'g', 8); };
		m_sizeLabel->setText(QStringLiteral("%1w %2l %3h  @(%4 %5 %6)")
		                         .arg(number(size.x), number(size.y), number(size.z), number(center.x), number(center.y), number(center.z)));
	}
	// "on" in green, "off" in grey.
	const auto onOff = [this](bool on) {
		return on ? QStringLiteral("<span style='color:#5ccf7a'>%1</span>").arg(tr("on")) : QStringLiteral("<span style='color:#7a7a7a'>%1</span>").arg(tr("off"));
	};
	m_gridLabel->setText(tr("Grid %1  ·  Snap %2  ·  Angle %3°  ·  Texture lock %4")
	                         .arg(m_editor->grid())
	                         .arg(onOff(m_editor->snapping()))
	                         .arg(m_editor->rotationSnap())
	                         .arg(onOff(m_editor->textureLock())));
}

void MainWindow::transformDialog() {
	if (!m_editor->selection().hasObjects()) {
		statusBar()->showMessage(tr("Select objects to transform"), 4000);
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Transform"));
	auto* form = new QFormLayout(&dialog);
	form->setContentsMargins(16, 16, 16, 12);
	form->setHorizontalSpacing(12);
	form->setVerticalSpacing(8);
	const auto row = [&](double fallback, double limit, const QString& suffix, int decimals) {
		auto* w = new QWidget;
		auto* h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(6);
		std::array<QDoubleSpinBox*, 3> boxes{};
		const char* axes[3] = {"X", "Y", "Z"};
		for (int i = 0; i < 3; ++i) {
			boxes[i] = new QDoubleSpinBox;
			boxes[i]->setRange(-limit, limit);
			boxes[i]->setDecimals(decimals);
			boxes[i]->setPrefix(QString(axes[i]) + "  ");
			boxes[i]->setSuffix(suffix);
			boxes[i]->setValue(fallback);
			h->addWidget(boxes[i], 1);
		}
		return std::pair{w, boxes};
	};
	auto [moveRow, move] = row(0, 65536, QString(), 2);
	form->addRow(tr("Move by"), moveRow);
	auto [turnRow, turn] = row(0, 360, QStringLiteral("°"), 2);
	form->addRow(tr("Rotate by"), turnRow);
	auto [scaleRow, scale] = row(1, 100, QStringLiteral(" ×"), 3);
	for (auto* box : scale) box->setMinimum(0.001);
	form->addRow(tr("Scale by"), scaleRow);
	auto* note = new QLabel(tr("Around the selection's center; applied as one step (undo takes it back)."));
	note->setProperty("role", "hint");
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() == QDialog::Accepted) {
		m_editor->transformSelection({move[0]->value(), move[1]->value(), move[2]->value()}, {turn[0]->value(), turn[1]->value(), turn[2]->value()},
		                             {scale[0]->value(), scale[1]->value(), scale[2]->value()});
	}
}

void MainWindow::pasteHere() {
	// Under the mouse when it is over a view, else where the objects were copied.
	PasteOptions options;
	if (const auto box = m_editor->clipboardBounds()) {
		for (auto* view : m_views) {
			if (const auto center = view->pasteCenter(*box)) {
				options.center = center;
				break;
			}
		}
	}
	m_editor->paste(options);
}

void MainWindow::pasteSpecialDialog() {
	const auto box = m_editor->clipboardBounds();
	if (!box) {
		statusBar()->showMessage(tr("The clipboard holds no map objects - copy brushes or entities first (Ctrl+C)"), 5000);
		return;
	}
	// Under the mouse: where it is now, before the dialog takes the focus.
	std::optional<Vec3> underMouse;
	for (auto* view : m_views) {
		if ((underMouse = view->pasteCenter(*box))) break;
	}
	QSettings settings;
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Paste special"));
	auto* form = new QFormLayout(&dialog);
	form->setContentsMargins(16, 16, 16, 12);
	form->setHorizontalSpacing(12);
	form->setVerticalSpacing(8);
	auto* copies = new QSpinBox;
	copies->setRange(1, 1000);
	copies->setValue(settings.value("pasteSpecial/copies", 1).toInt());
	form->addRow(tr("Number of copies"), copies);
	const auto vectorRow = [&](const QString& key, double fallback, double limit, const QString& suffix) {
		auto* row = new QWidget;
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(6);
		std::array<QDoubleSpinBox*, 3> boxes{};
		const char* axes[3] = {"X", "Y", "Z"};
		for (int i = 0; i < 3; ++i) {
			boxes[i] = new QDoubleSpinBox;
			boxes[i]->setRange(-limit, limit);
			boxes[i]->setDecimals(2);
			boxes[i]->setSuffix(suffix);
			boxes[i]->setPrefix(QString(axes[i]) + "  ");
			boxes[i]->setValue(settings.value(key + QString::number(i), i == 0 ? fallback : 0.0).toDouble());
			h->addWidget(boxes[i], 1);
		}
		return std::pair{row, boxes};
	};
	auto [offsetRow, offset] = vectorRow("pasteSpecial/offset", m_editor->grid() * 4, 65536, QString());
	form->addRow(tr("Offset per copy"), offsetRow);
	auto [rotationRow, rotation] = vectorRow("pasteSpecial/rotation", 0, 360, QStringLiteral("°"));
	form->addRow(tr("Rotation per copy"), rotationRow);
	auto* place = new QComboBox;
	place->addItem(tr("Next to the copied objects"));
	place->addItem(tr("Under the mouse"));
	place->setCurrentIndex(underMouse ? settings.value("pasteSpecial/place", 0).toInt() : 0);
	place->setEnabled(underMouse.has_value());
	form->addRow(tr("First copy"), place);
	auto* unique = new QCheckBox(tr("Make entity names unique (door -> door_2, outputs follow)"));
	unique->setChecked(settings.value("pasteSpecial/unique", true).toBool());
	form->addRow(QString(), unique);
	auto* prefix = new QLineEdit(settings.value("pasteSpecial/prefix").toString());
	prefix->setPlaceholderText(tr("optional, e.g. room2_"));
	form->addRow(tr("Put before names"), prefix);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Paste"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	PasteOptions options;
	options.copies = copies->value();
	options.offset = {offset[0]->value(), offset[1]->value(), offset[2]->value()};
	options.rotation = {rotation[0]->value(), rotation[1]->value(), rotation[2]->value()};
	if (place->currentIndex() == 1 && underMouse) options.center = underMouse;
	options.uniqueNames = unique->isChecked();
	options.namePrefix = prefix->text().trimmed().toStdString();
	options.textureLock = m_editor->textureLock();
	settings.setValue("pasteSpecial/copies", options.copies);
	for (int i = 0; i < 3; ++i) {
		settings.setValue("pasteSpecial/offset" + QString::number(i), offset[i]->value());
		settings.setValue("pasteSpecial/rotation" + QString::number(i), rotation[i]->value());
	}
	settings.setValue("pasteSpecial/place", place->currentIndex());
	settings.setValue("pasteSpecial/unique", options.uniqueNames);
	settings.setValue("pasteSpecial/prefix", prefix->text().trimmed());
	m_editor->paste(options);
}

void MainWindow::roundEdgeDialog() {
	if (m_editor->selection().faces.size() != 2) {
		QMessageBox::information(this, tr("Round edge"),
		                         tr("Select the two faces next to the edge first:\nFaces mode (3), click one face, Ctrl+click the other."));
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Round or bevel edge"));
	auto* form = new QFormLayout(&dialog);
	auto* radius = new QDoubleSpinBox;
	radius->setRange(1, 4096);
	radius->setValue(QSettings().value("brushes/roundRadius", 16.0).toDouble());
	radius->setSuffix(tr(" units"));
	auto* segments = new QSpinBox;
	segments->setRange(1, 32);
	segments->setValue(QSettings().value("brushes/roundSegments", 4).toInt());
	form->addRow(tr("Radius"), radius);
	form->addRow(tr("Segments (1 = bevel)"), segments);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() == QDialog::Accepted) {
		QSettings().setValue("brushes/roundRadius", radius->value());
		QSettings().setValue("brushes/roundSegments", segments->value());
		m_editor->roundSelectedEdge(radius->value(), segments->value());
	}
}

void MainWindow::checkForUpdates(bool manual) {
	m_manualCheck = manual;
	m_updater->check();
}

void MainWindow::offerUpdate(const ReleaseInfo& release, bool manual) {
	QMessageBox box(this);
	box.setIcon(QMessageBox::Information);
	box.setWindowTitle(tr("Update available"));
	box.setText(tr("<b>Version %1 is available</b> (you have %2).<br>Update now?").arg(release.version, QApplication::applicationVersion()));
	QString reason;
	const bool installable = Updater::canInstall(&reason);
	box.setInformativeText(installable ? tr("The editor downloads it, checks it and restarts. Your map is saved first if you want.")
	                                   : reason + tr(".<br>The download page opens instead."));
	if (!release.notes.isEmpty()) {
		box.setDetailedText(release.notes);
	}
	auto* yes = box.addButton(installable ? tr("Update now") : tr("Open download page"), QMessageBox::AcceptRole);
	box.addButton(tr("Later"), QMessageBox::RejectRole);
	QPushButton* skip = manual ? nullptr : box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
	auto* autoCheck = new QCheckBox(tr("Check for updates when the editor starts"));
	autoCheck->setChecked(QSettings().value("update/checkOnStart", true).toBool());
	box.setCheckBox(autoCheck);
	box.exec();
	QSettings().setValue("update/checkOnStart", autoCheck->isChecked());
	if (box.clickedButton() == skip && skip) {
		QSettings().setValue("update/skipVersion", release.version);
		return;
	}
	if (box.clickedButton() != yes) {
		return;
	}
	if (!installable) {
		QDesktopServices::openUrl(release.page);
		return;
	}
	runUpdate(release);
}

void MainWindow::runUpdate(const ReleaseInfo& release) {
	if (!maybeSave()) {
		return;
	}
	auto* progress = new QProgressDialog(tr("Downloading version %1 ...").arg(release.version), tr("Cancel"), 0, 100, this);
	progress->setWindowTitle(tr("Update"));
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(0);
	progress->setAutoClose(false);
	progress->setAutoReset(false);
	progress->setValue(0);
	auto* connections = new QObject(progress);   // all hooks die with the dialog
	connect(progress, &QProgressDialog::canceled, m_updater, &Updater::cancelDownload);
	connect(m_updater, &Updater::downloadProgress, connections, [progress](qint64 received, qint64 total) {
		if (total > 0) progress->setValue(int(received * 100 / total));
	});
	connect(m_updater, &Updater::downloadFailed, connections, [this, progress](const QString& error) {
		progress->deleteLater();
		if (error != tr("Cancelled")) {
			QMessageBox::warning(this, tr("Update"), tr("The update was not installed:\n%1").arg(error));
		}
	});
	connect(m_updater, &Updater::downloaded, connections, [this, progress](const QString& zip) {
		progress->deleteLater();
		QString error;
		if (!Updater::install(zip, &error)) {
			QMessageBox::warning(this, tr("Update"), tr("The update was not installed:\n%1").arg(error));
			return;
		}
		// The helper waits for the editor to close, replaces the files and starts the new version.
		m_quitForUpdate = true;
		close();
	});
	m_updater->download(release);
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (!m_quitForUpdate && !maybeSave()) {
		event->ignore();
		return;
	}
	QSettings().setValue("window/geometry", saveGeometry());
	event->accept();
}

} // namespace sme::app
