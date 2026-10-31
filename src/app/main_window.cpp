// main_window.cpp - Editor main window (see main_window.h).

#include "main_window.h"

#include "asset_browser.h"
#include "compile_window.h"
#include "connect_popup.h"
#include "enhancements.h"
#include "model_cache.h"
#include "map_look_panel.h"
#include "features_panel.h"
#include "mod_code_panel.h"
#include "mod_page.h"
#include "animations_page.h"
#include "models_page.h"
#include "maps_panel.h"
#include "menu_editor.h"
#include "start_page.h"
#include "texts_panel.h"
#include "new_mod_dialog.h"
#include "source_control_panel.h"
#include "ship_panel.h"
#include "history_panel.h"
#include "visgroups_panel.h"
#include "find_dialog.h"
#include "replace_materials_dialog.h"
#include "autosaver.h"
#include "icons.h"
#include "import_dialog.h"
#include "material_editor.h"
#include "material_cache.h"
#include "panels.h"
#include "particle_browser.h"
#include "properties_panel.h"
#include "round_edge_dialog.h"
#include "shortcut_dialog.h"
#include "sound_browser.h"
#include "tool_panel.h"
#include "updater.h"
#include "viewport.h"
#include "dialog_fit.h"
#include "dock_layout.h"
#include "wheel_guard.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QMimeData>
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
#include <QCursor>
#include <QScreen>
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

#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <QClipboard>

#include <QEnterEvent>
#include <QMouseEvent>
#include <QLayout>
#include <QPainter>

#include <sme/checks.h>
#include <sme/game.h>
#include <sme/assets.h>
#include <sme/map_tools.h>
#include <sme/scene_export.h>
#include <sme/text.h>

namespace sme::app {

namespace {

/// The page stack, sized by the page that is shown: QStackedWidget asks every page for its size,
/// so each show and resize of the window laid out all hidden pages (their texts above all).
class PageStack : public QStackedWidget {
public:
	PageStack() {
		connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
	}
	[[nodiscard]] QSize sizeHint() const override {
		QWidget* page = currentWidget();
		return page ? QWidgetItem(page).sizeHint() : QSize();
	}
	[[nodiscard]] QSize minimumSizeHint() const override {
		QWidget* page = currentWidget();
		return page ? QWidgetItem(page).minimumSize() : QSize();
	}
};

/// Opens a page when something is dragged over its side bar button (see the Map button).
class PageDropSwitch : public QObject {
public:
	PageDropSwitch(QObject* parent, std::function<void()> open) : QObject(parent), m_open(std::move(open)) {}

	bool eventFilter(QObject*, QEvent* event) override {
		if (event->type() != QEvent::DragEnter) return false;
		auto* drag = static_cast<QDragEnterEvent*>(event);
		const auto formats = drag->mimeData()->formats();
		const bool ours = std::any_of(formats.begin(), formats.end(), [](const QString& f) { return f.startsWith("application/x-sme-"); });
		if (!ours) return false;
		drag->acceptProposedAction();
		m_open();
		return true;
	}

private:
	std::function<void()> m_open;
};

/// Corner handle where the map views meet the bottom panels and a side panel: dragging it
/// moves both edges at once (the bottom panels' height and the side panels' width), so the
/// views grow or shrink in both directions with one drag.
class CornerGrip : public QWidget {
public:
	CornerGrip(QMainWindow* window, Qt::DockWidgetArea side) : QWidget(window), m_window(window), m_side(side) {
		setObjectName(side == Qt::LeftDockWidgetArea ? "cornerGripLeft" : "cornerGripRight");
		setFixedSize(14, 14);
		setCursor(side == Qt::LeftDockWidgetArea ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor);
		setToolTip(QObject::tr("Drag: resize the bottom and side panels together"));
	}

	/// Puts the handle on its corner of the map views; hidden when there is no panel to resize.
	void place(const QRect& views) {
		const bool any = !docks(Qt::BottomDockWidgetArea).isEmpty() && !docks(m_side).isEmpty();
		setVisible(any && views.isValid());
		if (!any) return;
		const QPoint corner = m_side == Qt::LeftDockWidgetArea ? views.bottomLeft() : views.bottomRight();
		move(corner - QPoint(width() / 2, height() / 2));
		raise();
	}

	/// Drags like the user would (for tests): `delta` in window pixels.
	void dragBy(const QPoint& delta) {
		begin();
		apply(delta);
	}

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(240, 160, 40, underMouse() || m_dragging ? 230 : 110));
		p.drawEllipse(QRectF(rect()).adjusted(3, 3, -3, -3));
	}
	void enterEvent(QEnterEvent*) override { update(); }
	void leaveEvent(QEvent*) override { update(); }
	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() != Qt::LeftButton) return;
		m_dragging = true;
		m_start = event->globalPosition().toPoint();
		begin();
	}
	void mouseMoveEvent(QMouseEvent* event) override {
		if (m_dragging) apply(event->globalPosition().toPoint() - m_start);
	}
	void mouseReleaseEvent(QMouseEvent*) override {
		m_dragging = false;
		update();
	}

private:
	/// The panels of an area that are on screen (of tabbed ones only the one in front).
	QList<QDockWidget*> docks(Qt::DockWidgetArea area) const {
		QList<QDockWidget*> out;
		for (auto* dock : m_window->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
			if (dock->isVisible() && !dock->isFloating() && m_window->dockWidgetArea(dock) == area) out << dock;
		}
		return out;
	}
	void begin() {
		m_bottom = docks(Qt::BottomDockWidgetArea);
		m_sides = docks(m_side);
		m_bottomHeight = m_bottom.isEmpty() ? 0 : m_bottom.front()->height();
		m_sideWidth = m_sides.isEmpty() ? 0 : m_sides.front()->width();
	}
	void apply(const QPoint& delta) {
		if (!m_bottom.isEmpty()) {
			QList<int> heights;
			for (int i = 0; i < m_bottom.size(); ++i) heights << std::max(40, m_bottomHeight - delta.y());
			m_window->resizeDocks(m_bottom, heights, Qt::Vertical);
		}
		if (!m_sides.isEmpty()) {
			const int dx = m_side == Qt::LeftDockWidgetArea ? delta.x() : -delta.x();
			QList<int> widths;
			for (int i = 0; i < m_sides.size(); ++i) widths << std::max(40, m_sideWidth + dx);
			m_window->resizeDocks(m_sides, widths, Qt::Horizontal);
		}
	}

	QMainWindow* m_window;
	Qt::DockWidgetArea m_side;
	bool m_dragging = false;
	QPoint m_start;
	QList<QDockWidget*> m_bottom, m_sides;
	int m_bottomHeight = 0, m_sideWidth = 0;
};

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
<tr><td><b>Shift+W</b></td><td>Snapping on/off (grid and rotation)</td><td><b>Shift+O</b></td><td>Snap to objects on/off (Alt while dragging: not now)</td></tr>
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
	// The mouse wheel scrolls panels instead of switching tabs or changing fields (once per app).
	DialogFitter::install();   // every dialog and window fits the screen (scrolls when taller)
	if (!qApp->property("smeWheelGuard").toBool()) {
		qApp->installEventFilter(new WheelGuard(qApp));
		qApp->setProperty("smeWheelGuard", true);
	}

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
	m_pages = new PageStack;
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
	m_gridLabel->setObjectName("statusGrid");
	m_gridLabel->setToolTip(tr("Click \"Snap\" (grid, Shift+W) or \"Object snap\" (corners of other objects, Shift+O) to switch them"));
	m_gridLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
	connect(m_gridLabel, &QLabel::linkActivated, this, [this](const QString& key) {
		if (key == "snap") m_editor->setSnapping(!m_editor->snapping());
		if (key == "objects") m_editor->setObjectSnap(!m_editor->objectSnap());
	});
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
	connect(editor, &Editor::liveBoundsChanged, this, &MainWindow::updateStatusInfo);
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
			// Search paths of gameinfo.txt that are not there (language packs, the episodes' bin
			// folders ...) are normal: one quiet line instead of a wall of warnings.
			QStringList missing;
			for (const auto& problem : m_editor->game()->problems) {
				const QString text = QString::fromStdString(problem);
				if (text.startsWith(QLatin1String("VPK not found")) || text.startsWith(QLatin1String("Folder not found"))) {
					missing << text;
				} else {
					m_log->append(text, QColor(255, 200, 100));
				}
			}
			if (!missing.isEmpty()) {
				m_log->append(tr("%n optional search path(s) of gameinfo.txt are not installed (language packs and the like) - that is normal.",
				                 nullptr, int(missing.size())),
				              QColor(140, 140, 140));
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
	// The layout's minimum must be current first: a stale one from while the panels were being
	// built (before their minimum sizes were set) would make the window wider than asked.
	if (layout()) layout()->activate();
	resize(1600, 960);
	restoreGeometry(QSettings().value("window/geometry").toByteArray());
	m_dockLayoutRestored = m_dockLayout->restore();
	updateTitle();
}

void MainWindow::createDocks() {
	// Every panel can float as its own window and go back (DockLayout: tabs and headers have the menu).
	m_dockLayout = new DockLayout(this);
	// The docks are placed and tabbed while still empty, and each panel is then built inside its
	// dock (host(title) as its parent): tabbing filled docks laid out the whole window with every
	// panel once per tab group, and moving a finished panel into its dock had the app's style
	// sheet polish and lay out all of its widgets again - together most of the start-up time.
	QHash<QString, QDockWidget*> docks;
	const auto placeDock = [this, &docks](const QString& title, Qt::DockWidgetArea area) {
		auto* dock = new QDockWidget(title, this);
		dock->setObjectName(title);
		addDockWidget(area, dock);
		m_dockLayout->addDock(dock);
		m_docks << dock;
		docks.insert(title, dock);
		return dock;
	};
	// Left: the tool's options, the Outliner, VisGroups and mapbase-fidelity's look per map as
	// tabs. Right: only the properties of what is selected. Bottom: browsers, compiling, problems.
	auto* tool = placeDock(tr("Tool"), Qt::LeftDockWidgetArea);
	auto* outliner = placeDock(tr("Outliner"), Qt::LeftDockWidgetArea);
	auto* mapLook = placeDock(tr("Map Look"), Qt::LeftDockWidgetArea);
	auto* visgroups = placeDock(tr("VisGroups"), Qt::LeftDockWidgetArea);
	tabifyDockWidget(tool, outliner);
	tabifyDockWidget(tool, visgroups);
	tabifyDockWidget(tool, mapLook);
	tool->raise();
	auto* properties = placeDock(tr("Object Properties"), Qt::RightDockWidgetArea);
	auto* assets = placeDock(tr("Asset Browser"), Qt::BottomDockWidgetArea);
	m_assetsDock = assets;
	m_soundsDock = placeDock(tr("Sounds"), Qt::BottomDockWidgetArea);
	m_particlesDock = placeDock(tr("Particles"), Qt::BottomDockWidgetArea);
	for (const QString& title : {tr("Compile"), tr("Problems"), tr("Map Budget"), tr("Log")}) {
		placeDock(title, Qt::BottomDockWidgetArea);
	}
	// Every undo step, next to the log (click one to go back or forward to it).
	placeDock(tr("History"), Qt::BottomDockWidgetArea);
	for (const QString& title : {tr("Sounds"), tr("Particles"), tr("Compile"), tr("Problems"), tr("Map Budget"), tr("History"), tr("Log")}) {
		tabifyDockWidget(assets, docks.value(title));
	}
	const auto host = [&docks](const QString& title) { return docks.value(title); };
	const auto fill = [&docks](const QString& title, QWidget* panel) { docks.value(title)->setWidget(panel); };
	m_assets = new AssetBrowser(m_editor, host(tr("Asset Browser")));
	// Make door picks door models and sounds from the browser's lists.
	m_editor->setAssetNames([assets = m_assets] { return assets->modelNames(); }, [assets = m_assets] { return assets->soundNames(); });
	m_properties = new PropertiesPanel(m_editor, m_assets, m_materials, host(tr("Object Properties")));
	// Connect: source and target picked - ask for the output and input next to the cursor.
	connect(m_editor, &Editor::connectRequested, this, [this](int sourceId, int targetId) {
		auto* popup = new ConnectPopup(m_editor, sourceId, targetId, this);
		popup->adjustSize();
		QPoint at = QCursor::pos() + QPoint(16, 16);
		if (const QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
			const QRect area = screen->availableGeometry();
			at.setX(std::min(at.x(), area.right() - popup->width()));
			at.setY(std::min(at.y(), area.bottom() - popup->height()));
		}
		popup->move(at);
		popup->show();
	});
	m_outliner = new Outliner(m_editor, host(tr("Outliner")));
	m_problems = new ProblemsPanel(m_editor, m_assets, host(tr("Problems")));
	m_budget = new BudgetPanel(m_editor, host(tr("Map Budget")));
	m_log = new LogPanel(host(tr("Log")));
	m_toolPanel = new ToolPanel(m_editor, host(tr("Tool")));
	m_toolPanel->entityPicker()->setMaterialCache(m_materials);
	m_toolPanel->entityPicker()->setModelThumbnails(m_assets->modelThumbnails());
	m_outliner->setIconProvider([picker = m_toolPanel->entityPicker()](const QString& cls) { return picker->iconFor(cls); });
	connect(m_toolPanel->entityPicker(), &EntityPicker::iconsChanged, m_outliner, &Outliner::refreshIcons);
	m_sounds = new SoundBrowser(m_editor, host(tr("Sounds")));
	m_particles = new ParticleBrowser(m_editor, m_assets, host(tr("Particles")));
	m_compileWindow = new CompileWindow(&m_compiler, host(tr("Compile")));
	// Pages go into the page stack as soon as they exist: adding a finished page moves every
	// widget in it once more (the app style sheet polishes them all again - 15-50 ms a page).
	m_materialEditor = new MaterialEditor(m_editor, m_pages);
	m_pages->addWidget(m_materialEditor);
	// Models page: every model with a 3D view; "Place in map" arms the entity tool with it.
	m_modelsPage = new ModelsPage(m_editor, m_assets, m_materials);
	m_pages->addWidget(m_modelsPage);
	m_animationsPage = new AnimationsPage(m_editor, m_assets, m_materials);
	m_pages->addWidget(m_animationsPage);
	connect(m_animationsPage, &AnimationsPage::entityRequested, this, [this](int entityId) {
		Selection one;
		one.entities.push_back(entityId);
		m_editor->setSelection(one);
		showPage(Page::Map);
		activeView()->frameSelection();
	});
	connect(m_modelsPage, &ModelsPage::placeRequested, this, [this](const QString& classname, const std::vector<std::pair<std::string, std::string>>& keys) {
		m_editor->setCurrentEntity(classname, keys);
		showPage(Page::Map);
		m_editor->setTool(Editor::Tool::Entity);
		emit m_editor->message(tr("Click in the map to place %1 as %2 - Esc or another tool stops").arg(m_modelsPage->currentModel().section('/', -1), classname));
	});
	connect(m_modelsPage, &ModelsPage::openMaterialRequested, this, [this](const QString& name) {
		m_materialEditor->openMaterial(name);
		showPage(Page::Materials);
	});
	// Project page: everything around the maps - overview, all maps with batch compiling, the
	// mod's main menu, source control and getting it ready to share.
	m_projectPage = new QTabWidget;
	m_projectPage->setObjectName("projectTabs");
	m_projectPage->setDocumentMode(true);
	m_pages->addWidget(m_projectPage);   // before its tabs (see above)
	// The tabs' panels are built inside the tab widget's page stack (like the docks' panels).
	auto* projectTabs = m_projectPage->findChild<QStackedWidget*>(QString(), Qt::FindDirectChildrenOnly);
	m_modPage = new ModPage(m_editor, projectTabs);
	const auto openFromProject = [this](const QString& path) {
		if (maybeSave() && openMap(path)) showPage(Page::Map);
	};
	connect(m_modPage, &ModPage::openMapRequested, this, openFromProject);
	connect(m_modPage, &ModPage::newModRequested, this, [this] { runNewModDialog(m_editor, this); });
	m_mapsPanel = new MapsPanel(m_editor, &m_compiler, projectTabs);
	connect(m_mapsPanel, &MapsPanel::openMapRequested, this, openFromProject);
	m_projectPage->addTab(m_modPage, tr("Overview"));
	m_projectPage->addTab(m_mapsPanel, tr("Maps"));
	auto* menuEditor = new MenuEditor(m_editor, projectTabs);
	m_projectPage->addTab(menuEditor, tr("Menu"));
	// Texts: the mod's localization and captions. Chapter titles live in the same file as the
	// menu's: each tab picks up what the other saved.
	m_textsPanel = new TextsPanel(m_editor, m_assets, projectTabs);
	m_projectPage->addTab(m_textsPanel, tr("Texts"));
	connect(m_textsPanel, &TextsPanel::saved, menuEditor, [menuEditor] { menuEditor->refreshTitles(); });
	connect(menuEditor, &MenuEditor::saved, m_textsPanel, [this] { m_textsPanel->reloadIfUnchanged(); });
	auto* sourceControl = new SourceControlPanel(m_editor, projectTabs);
	m_projectPage->addTab(sourceControl, tr("Source control"));
	// Build from source: for people who build the mod themselves (update after a pull, game
	// files, report); its line on the Overview says when new code is waiting or an update runs.
	// Normally Features installs what Mapsmith brings - this tab also runs that bundle's scripts.
	m_modCode = new ModCodePanel(m_editor, projectTabs);
	m_modPage->addBanner(m_modCode->banner());
	connect(sourceControl, &SourceControlPanel::pulled, m_modCode, &ModCodePanel::afterPull);
	connect(sourceControl, &SourceControlPanel::updateModRequested, this, [this](const QString& root) {
		if (QFileInfo(root).canonicalFilePath() != QFileInfo(m_modCode->repository()).canonicalFilePath()) m_modCode->setRepository(root);
		m_projectPage->setCurrentWidget(m_modCode);
		m_modCode->start(ModCodeAction::Update);
	});
	connect(m_modCode, &ModCodePanel::showRequested, this, [this] {
		showPage(Page::Project);
		m_projectPage->setCurrentWidget(m_modCode);
	});
	// New DLLs, shaders and materials in the mod: the game is read again (the open map stays),
	// so the views, browsers and the game enhancements see them.
	const auto reloadGame = [this] {
		if (m_reloadPending) return;
		m_reloadPending = true;
		m_log->append(tr("The mod was updated - reading its files again"));
		QTimer::singleShot(0, this, [this] {
			m_reloadPending = false;
			if (!m_editor->gameKey().isEmpty()) selectGame(m_editor->gameKey());
		});
	};
	connect(m_modCode, &ModCodePanel::modUpdated, this, reloadGame);
	m_projectPage->addTab(new ShipPanel(m_editor, projectTabs), tr("Ship"));
	m_projectPage->addTab(m_modCode, tr("Build from source"));
	// Features: Mapsmith's ready-built mapbase-fidelity installed into the mod (offered on the
	// Overview, kept up to date), and which parts of it the mod has.
	m_features = new FeaturesPanel(m_editor, m_modCode, projectTabs);
	m_projectPage->insertTab(1, m_features, tr("Features"));
	connect(m_features, &FeaturesPanel::installed, this, reloadGame);
	m_modPage->addBanner(m_features->offer());
	m_features->refreshOffer();
	// "Import ..." on the pages: the mapper's own sounds, pictures, models and particle files.
	connect(m_sounds, &SoundBrowser::importRequested, this, [this] { openImport(ImportDialog::Kind::Sound); });
	connect(m_materialEditor, &MaterialEditor::importRequested, this, [this] { openImport(ImportDialog::Kind::Material); });
	connect(m_modelsPage, &ModelsPage::importRequested, this, [this] { openImport(ImportDialog::Kind::Model); });
	connect(m_particles, &ParticleBrowser::importRequested, this, [this] { openImport(ImportDialog::Kind::Particles); });
	connect(m_features, &FeaturesPanel::showRequested, this, [this] {
		showPage(Page::Project);
		m_projectPage->setCurrentWidget(m_features);
	});
	// "Install features" where an effect needs the mod's shaders (Game enhancements, the material
	// editor): to the Features tab and install them right away.
	connect(m_editor->enhancements(), &GameEnhancements::installFeaturesRequested, this, [this] {
		showPage(Page::Project);
		m_projectPage->setCurrentWidget(m_features);
		m_features->installNow();
	});
	connect(m_features, &FeaturesPanel::modCodeRequested, this, [this] {
		showPage(Page::Project);
		m_projectPage->setCurrentWidget(m_modCode);
	});
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
	m_pages->addWidget(m_soundsPage);
	m_pages->addWidget(m_particlesPage);
	// The start page: continue, recent projects, start new (shown at start, File > Start page).
	m_startPage = new StartPage(m_pages);
	m_pages->addWidget(m_startPage);
	connect(m_startPage, &StartPage::openProject, this, &MainWindow::openProject);
	connect(m_startPage, &StartPage::newProjectRequested, this, &MainWindow::startNewProject);
	connect(m_startPage, &StartPage::closeRequested, this, [this] { showPage(Page::Map); });
	connect(m_startPage, &StartPage::openMapRequested, this, [this] {
		const QString dir = m_editor->gameKey().isEmpty() ? QString() : m_editor->gameSettings().mapDir;
		const QString path = QFileDialog::getOpenFileName(this, tr("Open map"), dir, tr("Valve map (*.vmf)"));
		if (!path.isEmpty() && openMap(path)) showPage(Page::Map);
	});
	// Every map opened or saved is a recent project (with a picture of the 3D view when saved).
	connect(m_editor, &Editor::documentReplaced, this, [this] {
		if (!m_editor->filePath().isEmpty()) rememberRecentProject(m_editor->gameKey(), m_editor->filePath());
	});
	for (int i = 1; i < m_pages->count(); ++i) {
		m_pages->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);   // see showPage()
	}

	m_mapLook = new MapLookPanel(m_editor, mapLook);
	fill(tr("Tool"), m_toolPanel);
	fill(tr("Outliner"), m_outliner);
	fill(tr("Map Look"), m_mapLook);
	fill(tr("VisGroups"), new VisGroupsPanel(m_editor, visgroups));
	fill(tr("Object Properties"), m_properties);
	fill(tr("Asset Browser"), m_assets);
	fill(tr("Sounds"), m_sounds);
	fill(tr("Particles"), m_particles);
	fill(tr("Compile"), m_compileWindow);
	fill(tr("Problems"), m_problems);
	fill(tr("Map Budget"), m_budget);
	fill(tr("Log"), m_log);
	fill(tr("History"), new HistoryPanel(m_editor, host(tr("History"))));
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
		} else if (currentPage() != Page::Materials) {
			// Picked while mapping: the Materials page shows it when it is opened next.
			m_pickedMaterial = name;
			m_editorAtPick = m_materialEditor->materialName();
		}
	});
	connect(m_assets, &AssetBrowser::editMaterialRequested, this, [this](const QString& name) {
		m_materialEditor->openMaterial(name);
		raiseDock("Material Editor");
	});
	// "Edit soundscape" on a soundscape entity: the Sounds page with that soundscape open.
	connect(m_properties, &PropertiesPanel::animationPreviewRequested, this, [this](const QString& model, const QString& sequence) {
		showPage(Page::Animations);
		m_animationsPage->showFor(model, sequence);
	});
	connect(m_properties, &PropertiesPanel::editSoundscapeRequested, this, [this](const QString& name) {
		showPage(Page::Sounds);
		m_sounds->reveal(name, SoundBrowser::SoundscapeEntry);
	});
	connect(m_sounds, &SoundBrowser::pageRequested, this, [this] { showPage(Page::Sounds); });
	// One source for every "Show in ..." menu.
	const AssetMenuFiller filler = [this](QMenu* menu, const AssetRef& asset, AssetPlace from) { fillAssetMenu(menu, asset, from); };
	m_assets->setAssetMenuFiller(filler);
	m_properties->setAssetMenuFiller(filler);
	m_modelsPage->setAssetMenuFiller(filler);
	connect(m_properties, &PropertiesPanel::editMaterialRequested, this, [this](const QString& name) {
		m_materialEditor->openMaterial(name);
		raiseDock("Material Editor");
	});
	// Changes in the material editor show in the views at once, before they are saved.
	connect(m_materialEditor, &MaterialEditor::liveChanged, this, [this](const QString& name, const QString& vmt, const QImage& heights) {
		m_materials->setLive(name, vmt, heights);
	});
	connect(m_materialEditor, &MaterialEditor::liveReverted, this, [this](const QString& name) { m_materials->clearLive(name); });
	connect(m_materialEditor, &MaterialEditor::materialSaved, this, [this](const QString& name) {
		m_materials->reload(name);
		m_assets->materialChanged(name);
	});
	// Game enhancements wrote materials of the mod: the views show them as the game will.
	connect(m_editor->enhancements(), &GameEnhancements::materialsChanged, this, [this](const QStringList& names) {
		for (const auto& name : names) {
			if (m_materials->isKnown(name)) m_materials->reload(name);
			m_assets->materialChanged(name);
		}
	});
	// ... and model copies of containers (their own liquid material): the views load them again.
	connect(m_editor->enhancements(), &GameEnhancements::modelsChanged, this, [this](const QStringList& models) {
		for (const auto& model : models) m_editor->models()->forget(model);
		for (auto* view : m_views) view->update();
	});
	connect(m_compileWindow, &CompileWindow::compileAgain, this, &MainWindow::compile);
	connect(m_compileWindow, &CompileWindow::reportRequested, this, &MainWindow::makeReport);
	connect(m_problems, &ProblemsPanel::reportRequested, this, &MainWindow::makeReport);
	connect(m_problems, &ProblemsPanel::actionRequested, this, [this](const QString& action) {
		if (action == QLatin1String("buildCubemaps")) buildCubemapsInCompile();
		if (action == QLatin1String("installFeatures")) {
			showPage(Page::Project);
			m_projectPage->setCurrentWidget(m_features);
			if (m_features->installNow()) QTimer::singleShot(0, m_problems, &ProblemsPanel::runChecks);
		}
	});
	connect(m_compileWindow, &CompileWindow::showLeak, this, [this] {
		for (auto* view : m_views) {
			view->setLeakLine(m_lastLeak);
		}
		raiseDock("Problems");
	});
	assets->raise();
	// Every panel may get small: a big minimum of one tab (a sound editor, the compile steps)
	// would hold the whole bottom row up. What does not fit gets cut, the panels scroll or
	// switch to a compact form (the Sounds panel does below 420 pixels).
	for (auto* dock : std::as_const(m_docks)) {
		const auto area = dockWidgetArea(dock);
		// The Asset Browser keeps one row of tiles with their names (its own minimum), the others
		// may get as low as 90 pixels.
		if (area == Qt::BottomDockWidgetArea) dock->widget()->setMinimumHeight(dock->widget() == m_assets ? m_assets->minimumSizeHint().height() : 90);
		if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) dock->widget()->setMinimumWidth(200);
		connect(dock, &QDockWidget::visibilityChanged, this, [this] { QTimer::singleShot(0, this, &MainWindow::placeCornerGrips); });
	}
	// Corner handles: the views' lower corners move the bottom and a side panel together.
	m_gripLeft = new CornerGrip(this, Qt::LeftDockWidgetArea);
	m_gripRight = new CornerGrip(this, Qt::RightDockWidgetArea);
	m_pages->installEventFilter(this);
	resizeDocks({properties, tool}, {380, 300}, Qt::Horizontal);
	resizeDocks({assets}, {210}, Qt::Vertical);
	setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
	setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
	m_dockLayout->captureFactory();
	const auto factorySizes = [this, assets, properties, tool] {
		resizeDocks({assets}, {std::max(180, height() / 5)}, Qt::Vertical);
		resizeDocks({properties, tool}, {380, 300}, Qt::Horizontal);
	};
	// Again once the window has its size: the map views get the most room (unless the mapper's
	// own layout of the last session was restored).
	QTimer::singleShot(0, this, [this, factorySizes] {
		if (!m_dockLayoutRestored) factorySizes();
	});
	connect(m_dockLayout, &DockLayout::layoutReset, this, factorySizes);
	connect(m_toolPanel, &ToolPanel::wantsAttention, this, [tool] {
		tool->show();
		tool->raise();
	});
}

void MainWindow::showMaterialEditor() {
	showPage(Page::Materials);
}

void MainWindow::showPage(Page page) {
	QWidget* const widgets[] = {m_rows, m_materialEditor, m_modelsPage, m_animationsPage, m_soundsPage, m_particlesPage, m_projectPage};
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
	// The map's docks go before another page shows: with them still there the bigger page widened
	// the window past its size and left the docks' tab bars, empty, around the page.
	if (page != Page::Map) setMapWorkspaceShown(false);
	m_pages->setCurrentWidget(target);
	// A stacked widget is as wide as its widest page; only the shown page may count, or the map
	// with its docks would need the Materials page's width too.
	for (auto* widget : widgets) {
		widget->setSizePolicy(widget == target ? QSizePolicy::Preferred : QSizePolicy::Ignored,
		                      widget == target ? QSizePolicy::Preferred : QSizePolicy::Ignored);
	}
	m_assets->setPageMode(page == Page::Materials);
	QTimer::singleShot(0, this, &MainWindow::placeCornerGrips);   // after the docks are back or gone
	// The page shows the current material (picked in the browser or with the eyedropper); a
	// material with unsaved changes stays open, with a hint which one is current.
	if (page == Page::Materials) {
		// The material picked in the browser since the editor's material was opened; without
		// one an empty page shows the browser's current material.
		const QString open = m_materialEditor->materialName();
		QString wanted = open == m_editorAtPick ? m_pickedMaterial : QString();
		if (open.isEmpty() && wanted.isEmpty()) wanted = m_assets->shownMaterial();
		m_pickedMaterial.clear();
		if (!wanted.isEmpty() && open.compare(wanted, Qt::CaseInsensitive) != 0) {
			if (open.isEmpty() || !m_materialEditor->hasUnsavedChanges()) {
				m_materialEditor->openMaterial(wanted);
				if (m_assets->shownMaterial().compare(wanted, Qt::CaseInsensitive) != 0) m_assets->selectMaterial(wanted);
			} else {
				emit m_editor->message(tr("%1 has unsaved changes and stays open - the current material is %2").arg(open, wanted));
			}
		}
	}
	setMapWorkspaceShown(page == Page::Map);
	if (m_pageGroup) {
		const auto actions = m_pageGroup->actions();
		if (int(page) < actions.size()) actions[int(page)]->setChecked(true);
	}
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
	if (watched == m_pages && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
		placeCornerGrips();
	}
	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::placeCornerGrips() {
	const QRect views = currentPage() == Page::Map ? m_pages->geometry() : QRect();
	for (auto* grip : {m_gripLeft, m_gripRight}) {
		if (grip) static_cast<CornerGrip*>(grip)->place(views);
	}
}

MainWindow::Page MainWindow::currentPage() const {
	QWidget* const widgets[] = {m_rows, m_materialEditor, m_modelsPage, m_animationsPage, m_soundsPage, m_particlesPage, m_projectPage};
	for (int i = 0; i < int(std::size(widgets)); ++i) {
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
		m_mapDockState = m_dockLayout->state();
		for (auto* dock : std::as_const(m_docks)) {
			// A panel floating as its own window stays (on another monitor), unless its browser
			// moves into the page.
			const bool browser = dock == m_assetsDock || dock == m_soundsDock || dock == m_particlesDock;
			if (dock->isFloating() && !browser) continue;
			if (!dock->isHidden()) {   // not isVisible(): before the window is shown nothing is visible
				m_hiddenForTab << dock;
				dock->hide();
			}
		}
		for (auto* bar : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
			if (!bar->isHidden() && bar != m_pageBar) {
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
	static const QMap<QString, Page> PAGES = {{"Materials", Page::Materials}, {"Models", Page::Models}, {"Animations", Page::Animations}, {"Mod", Page::Project}, {"Project", Page::Project}};
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
	file->addAction(action(tr("Start page"), QKeySequence(), [this] { showStartPage(); }));
	file->addAction(action(tr("New project (mod and first map) ..."), QKeySequence(), [this] { startNewProject(); }));
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
	auto* imports = file->addMenu(tr("Import into the mod"));
	imports->setObjectName("importMenu");
	imports->addAction(action(tr("Sound ..."), QKeySequence(), [this] { openImport(ImportDialog::Kind::Sound); }));
	imports->addAction(action(tr("Picture as material ..."), QKeySequence(), [this] { openImport(ImportDialog::Kind::Material); }));
	imports->addAction(action(tr("Model ..."), QKeySequence(), [this] { openImport(ImportDialog::Kind::Model); }));
	imports->addAction(action(tr("Particle file ..."), QKeySequence(), [this] { openImport(ImportDialog::Kind::Particles); }));
	imports->addSeparator();
	imports->addAction(action(tr("Imported files ..."), QKeySequence(), [this] { showImports(); }));
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
	auto* water = action(tr("Make water"), QKeySequence(), [this] { m_editor->makeWaterFromSelection(m_assets->materialNames()); });
	water->setToolTip(tr("Top face water, the other faces nodraw; adds a water_lod_control and a cubemap above the water if missing"));
	brushes->addAction(water);
	auto* rope = action(tr("Make rope"), QKeySequence(), [this] { m_editor->makeRopeFromSelection(); });
	rope->setObjectName("brushesMakeRope");
	rope->setToolTip(tr("A rope hanging between the selected objects (two or more), drawn sagging in the views"));
	brushes->addAction(rope);
	auto* ladder = action(tr("Make ladder"), QKeySequence(), [this] { m_editor->makeLadderFromSelection(); });
	ladder->setObjectName("brushesMakeLadder");
	ladder->setToolTip(tr("The selected ladder brush or model becomes climbable (Half-Life 2): func_useableladder and dismount points"));
	brushes->addAction(ladder);
	brushes->addSeparator();
	m_doorMenu = brushes->addMenu(tr("Make door"));
	m_doorMenu->setObjectName("brushesMakeDoor");
	m_doorMenu->setToolTip(tr("The selected block becomes a working door"));
	fillMakeDoorMenu(m_doorMenu, m_editor);
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
	m_contextActions = {hollow, carve, mergeAction, round, water, drop, track, nullptr};
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
	auto* mapPage = pageAction("page:map", tr("Map"), tr("The map: 3D and 2D views, tools and their panels"), Page::Map);
	mapPage->setChecked(true);
	pageAction("page:materials", tr("Materials"), tr("Material Editor: textures, shaders and mapbase-fidelity effects"), Page::Materials);
	pageAction("page:models", tr("Models"), tr("Every model with a 3D view, skins and body groups; place one in the map"), Page::Models);
	pageAction("page:animations", tr("Animations"), tr("Watch a model's animations, see which entities play them, put one on an entity"), Page::Animations);
	// Dragging a model, material, sound ... onto the Map button opens the map, so the drag can go
	// on into a view.
	if (auto* mapButton = m_pageBar->widgetForAction(mapPage)) {
		mapButton->setAcceptDrops(true);
		mapButton->installEventFilter(new PageDropSwitch(mapButton, [this] { showPage(Page::Map); }));
	}
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
	toolAction("tool:displace", tr("Displacement"), QKeySequence("Shift+D"), Editor::Tool::Displace);
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
	m_gameButton->setObjectName("gameButton");
	m_gameButton->setProperty("chooser", true);
	m_gameButton->setToolTip(tr("The mod (or game) you are working on - click to switch. Your own mods are listed first."));
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
	// Snap to objects: dragged corners and sides stop at corners of other objects (Alt: not now).
	auto* objectSnapAction = action(tr("Snap to objects"), QKeySequence("Shift+O"), [this] { m_editor->setObjectSnap(!m_editor->objectSnap()); });
	objectSnapAction->setCheckable(true);
	objectSnapAction->setChecked(m_editor->objectSnap());
	objectSnapAction->setIcon(icon("snap"));
	objectSnapAction->setIconText(tr("Object snap"));   // the tool bar's short name; menus keep "Snap to objects"
	objectSnapAction->setToolTip(tr("Moving, resizing and drawing stop at corners and sides of other objects, also off the grid (Shift+O; hold Alt to "
	                                "drag past them)"));
	top->addAction(objectSnapAction);
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
	connect(m_editor, &Editor::settingsChanged, this, [this, rotateCombo, snapAction, objectSnapAction] {
		const int index = rotateCombo->findData(m_editor->rotationSnap());
		if (index >= 0) rotateCombo->setCurrentIndex(index);
		snapAction->setChecked(m_editor->snapping());
		objectSnapAction->setChecked(m_editor->objectSnap());
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
	renderAction(tr("Lightmap density"), QKeySequence("Ctrl+F6"), Editor::RenderMode::LightmapDensity,
	             tr("Each face coloured by its lightmap scale, with a checker of the luxel size"));
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
			if (dockWidgetArea(d) == area && !d->isFloating()) out << d;
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
	auto* resetPanels = view->addAction(tr("Reset all panels"));
	resetPanels->setObjectName("resetPanels");
	resetPanels->setStatusTip(tr("Every panel back to where it was at the first start"));
	connect(resetPanels, &QAction::triggered, this, [this] {
		showPage(Page::Map);
		m_dockLayout->resetToFactory();
	});

	// Map
	auto* map = menuBar()->addMenu(tr("&Map"));
	// Hammer's Map Properties: the world's keys (sky, detail sprites, ...) in Object Properties.
	map->addAction(action(tr("Map properties ..."), QKeySequence(), [this] {
		m_editor->clearSelection();
		raiseDock("Object Properties");
	}));
	map->addAction(objectSnapAction);
	map->addSeparator();
	map->addAction(check);
	map->addAction(run);
	// After testing the map in the game: something looked wrong there -> a report to send.
	auto* report = action(tr("Make report from the game"), QKeySequence(), [this] { makeReport(); });
	report->setObjectName("makeReport");
	report->setToolTip(reportHowTo());
	report->setStatusTip(tr("Pack what fidelity_report wrote in the game into fidelity_report.zip"));
	map->addAction(report);
	map->setToolTipsVisible(true);
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
	// Like the game's buildcubemaps: the reflection preview draws every env_cubemap again.
	auto* buildCubemaps = action(tr("Build cubemaps"), QKeySequence(), [this] {
		for (auto* v : m_views) v->buildCubemaps();
		emit m_editor->message(tr("Cubemaps are drawn again - $envmap materials reflect them in the lighting preview"));
	});
	buildCubemaps->setObjectName("buildCubemaps");
	map->addAction(buildCubemaps);
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
	// The mapper's own mods first (sourcemods, a new one included), newest folder first: this is
	// where one switches between them.
	std::vector<const GameEntry*> ownMods;
	for (const auto& game : games) {
		if (game.isSourceMod) ownMods.push_back(&game);
	}
	if (!ownMods.empty()) {
		std::error_code ec;
		std::stable_sort(ownMods.begin(), ownMods.end(), [&ec](const GameEntry* a, const GameEntry* b) {
			return std::filesystem::last_write_time(a->gameDir, ec) > std::filesystem::last_write_time(b->gameDir, ec);
		});
		groups.push_back(m_gameMenu->addSection(tr("Your mods")));
		for (const auto* game : ownMods) {
			auto* action = makeAction(m_gameMenu, *game, entryText(*game) + QStringLiteral("   - sourcemods/") + QString::fromStdString(game->folder()));
			groups.push_back(action);
		}
	}
	groups.push_back(m_gameMenu->addSection(tr("Steam games")));
	std::map<std::string, std::vector<const GameEntry*>> byApp;
	for (const auto& game : games) {
		if (!game.isSourceMod) byApp[game.appName].push_back(&game);
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
	m_gameButton->setText(current ? tr("Mod:  %1  ▾").arg(entryText(*current).trimmed()) : tr("Choose your mod or game ...  ▾"));
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
	rememberRecentProject(m_editor->gameKey(), path, view3D()->grabFramebuffer());
	return true;
}

void MainWindow::showStartPage() {
	m_startPage->refresh();
	setMapWorkspaceShown(false);   // before the page (see showPage)
	m_pages->setCurrentWidget(m_startPage);
	for (auto* widget : {static_cast<QWidget*>(m_rows), static_cast<QWidget*>(m_materialEditor), static_cast<QWidget*>(m_modelsPage),
	                     static_cast<QWidget*>(m_animationsPage), static_cast<QWidget*>(m_soundsPage), static_cast<QWidget*>(m_particlesPage),
	                     static_cast<QWidget*>(m_projectPage)}) {
		widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	}
	m_startPage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	if (m_pageGroup) {
		// No side bar page is current while the start page shows.
		m_pageGroup->setExclusive(false);
		for (auto* a : m_pageGroup->actions()) a->setChecked(false);
		m_pageGroup->setExclusive(true);
	}
}

bool MainWindow::startPageShown() const {
	return m_pages->currentWidget() == m_startPage;
}

bool MainWindow::openProject(const QString& gameKey, const QString& mapPath) {
	if (!maybeSave()) return false;
	if (!gameKey.isEmpty() && gameKey != m_editor->gameKey()) selectGame(gameKey);
	if (!openMap(mapPath)) return false;
	showPage(Page::Map);
	return true;
}

bool MainWindow::startNewProject() {
	if (!maybeSave()) return false;
	// 1. The mod (Mapbase by default, the features go in with it).
	NewModDialog dialog(m_editor, this);
	dialog.setWindowTitle(tr("New project"));
	dialog.showFirstMapChoice();
	if (dialog.exec() != QDialog::Accepted) return false;
	QString error;
	const QString key = createModAndSwitch(m_editor, dialog.modTemplate(), dialog.mapsToCopy(), &error, dialog.featuresOff());
	if (key.isEmpty()) {
		QMessageBox::warning(this, tr("New project"), error);
		return false;
	}
	return makeStarterMap(QString::fromStdString(dialog.modTemplate().folder), dialog.tutorialMap());
}

bool MainWindow::makeStarterMap(const QString& name, bool tutorial) {
	// 2. A first map: the tutorial map or a closed room, in the mod's map folder.
	m_editor->newMap();
	const auto* game = m_editor->game();
	// The game's own materials when it has them (Half-Life 2's), else the dev textures.
	const auto pick = [game](std::initializer_list<const char*> names, const std::string& fallback) {
		if (!game) return fallback;
		for (const char* name : names) {
			if (game->files.exists(std::string("materials/") + name + ".vmt")) return QString::fromLatin1(name).toUpper().toStdString();
		}
		return fallback;
	};
	if (tutorial) {
		edit::TutorialMapOptions options;
		auto& m = options.materials;
		m.floor = pick({"concrete/concretefloor001a", "dev/dev_measuregeneric01b"}, m.floor);
		m.walls = pick({"plaster/plasterwall003a", "brick/brickwall001a", "concrete/concretewall001a"}, m.walls);
		m.ceiling = pick({"concrete/concreteceiling001a", "dev/dev_measuregeneric01"}, m.ceiling);
		if (game) {
			// A soundscape the game really has, picked by its name; else the template's own when
			// the game has it; else none.
			std::vector<std::string> names;
			for (const auto& soundscape : soundscapes(game->files)) names.push_back(soundscape.name);
			const std::string picked = edit::pickSoundscape(names, edit::SoundscapeArea::Indoor);
			if (!picked.empty()) options.soundscape = picked;
			else if (std::find(names.begin(), names.end(), options.soundscape) == names.end()) options.soundscape.clear();
		}
		const auto* entry = m_editor->gameEntry();
		if (game && entry) {
			// The map's own quiet room tone in the mod's soundscape file (the one the Sounds page
			// edits), from the game's sounds; without fitting sounds it keeps the one picked above.
			const std::string prefix = toLower(entry->folder());
			const auto scapes = edit::tutorialSoundscapes(prefix, [game](const std::string& wave) { return game->files.exists("sound/" + wave); });
			if (!scapes.empty() && writeTutorialSoundscapes(scapes)) options.soundscape = scapes.front().name;
			// Models' boxes from the game: ammo crates and props stand on the floor.
			options.modelBounds = [game](const std::string& model) -> std::optional<Bounds> {
				const auto data = game->files.read(model);
				if (!data) return std::nullopt;
				const auto info = readModelInfo(*data);
				if (!info) return std::nullopt;
				return Bounds{info->hullMin, info->hullMax};
			};
		}
		m_editor->commit(edit::tutorialMap(m_editor->doc(), options));
	} else {
		edit::StarterRoomMaterials materials;
		materials.floor = pick({"concrete/concretefloor001a", "dev/dev_measuregeneric01b"}, materials.floor);
		materials.walls = pick({"brick/brickwall001a", "concrete/concretewall001a", "dev/dev_measurewall01a"}, materials.walls);
		materials.ceiling = pick({"concrete/concreteceiling001a", "dev/dev_measuregeneric01"}, materials.ceiling);
		m_editor->commit(edit::starterRoom(m_editor->doc(), materials));
	}
	QDir().mkpath(m_editor->gameSettings().mapDir);
	QString path = QDir(m_editor->gameSettings().mapDir).filePath((name.isEmpty() ? QStringLiteral("first_map") : name) + QStringLiteral(".vmf"));
	for (int i = 2; QFileInfo::exists(path); ++i) path = QDir(m_editor->gameSettings().mapDir).filePath(QStringLiteral("%1_%2.vmf").arg(name).arg(i));
	QString error;
	if (!m_editor->saveMap(path, &error)) {
		QMessageBox::warning(this, tr("New project"), tr("Could not save the first map:\n%1").arg(error));
		return false;
	}
	updateTitle();
	showPage(Page::Map);
	for (auto* view : m_views) view->frameSelection();
	rememberRecentProject(m_editor->gameKey(), path);
	emit m_editor->message(tutorial ? tr("Your project is ready: %1, a tutorial room with a citizen, weapons and ammo. Look around, "
	                                     "change it, then Run map (F9) to play it.").arg(QFileInfo(path).fileName())
	                                : tr("Your project is ready: %1 with a first room. Build on it, then Run map (F9) to walk around in it.")
	                                      .arg(QFileInfo(path).fileName()));
	return true;
}

bool MainWindow::writeTutorialSoundscapes(const std::vector<SoundscapeScript>& scapes) {
	auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	if (!game || !entry) return false;
	const QString dir = fromPath(entry->gameDir);
	const QString file = QStringLiteral("scripts/soundscapes_%1.txt").arg(QString::fromStdString(toLower(entry->folder())));
	const auto readMod = [&dir](const QString& relative) {
		QFile in(dir + "/" + relative);
		return in.open(QIODevice::ReadOnly) ? in.readAll().toStdString() : std::string();
	};
	const auto writeMod = [&](const QString& relative, const std::string& text) {
		const QString path = dir + "/" + relative;
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(text.data(), qint64(text.size())) != qint64(text.size())) return false;
		out.close();
		game->files.noteWrittenFile(toPath(path));
		return true;
	};
	try {
		std::string text = readMod(file);
		for (const auto& scape : scapes) text = upsertSoundscape(text, scape);
		// The manifest: the mod's own, else a copy of the game's (it lists the game's soundscapes).
		std::string manifest = readMod(QStringLiteral("scripts/soundscapes_manifest.txt"));
		if (manifest.empty()) manifest = game->files.readText("scripts/soundscapes_manifest.txt").value_or(std::string());
		return writeMod(file, text) && writeMod(QStringLiteral("scripts/soundscapes_manifest.txt"), addToSoundscapeManifest(manifest, file.toStdString()));
	} catch (const std::exception&) {
		return false;   // a broken soundscape file of the mod: the map uses the game's soundscapes
	}
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

void MainWindow::makeReport() {
	// The run, its progress and the result (or why it cannot run) show on the Build from source tab
	// (with the report script Mapsmith brings when the mod has no source code of its own).
	showPage(Page::Project);
	m_projectPage->setCurrentWidget(m_modCode);
	m_modCode->start(ModCodeAction::Report);
}

void MainWindow::buildCubemapsInCompile() {
	// The compile dialog opens with the Cubemaps step on (it needs the copy into the game's maps).
	if (!m_editor->gameKey().isEmpty()) {
		auto options = loadCompileOptions(m_editor->gameKey());
		options.cubemaps = true;
		options.copy = true;
		saveCompileOptions(m_editor->gameKey(), options);
	}
	compile();
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
	std::unique_ptr<QMenu> menu(buildContextMenu());
	menu->exec(globalPos);
}

bool MainWindow::revealAsset(const AssetRef& asset, RevealIn where) {
	const QString name = QString::fromStdString(asset.name);
	switch (asset.kind) {
	case AssetKind::Material:
		if (where == RevealIn::Page) {
			showPage(Page::Materials);
			m_materialEditor->openMaterial(name);
			m_assets->selectMaterial(name);   // the browser is on the page too
			return true;
		}
		if (currentPage() != Page::Map) showPage(Page::Map);
		raiseDock("Asset Browser");
		return m_assets->selectMaterial(name);
	case AssetKind::Model:
		if (where == RevealIn::Page) {
			showPage(Page::Models);
			m_modelsPage->selectModel(name);
			return m_modelsPage->currentModel().compare(name, Qt::CaseInsensitive) == 0;
		}
		if (currentPage() != Page::Map) showPage(Page::Map);
		raiseDock("Asset Browser");
		return m_assets->selectModel(name);
	case AssetKind::Sound:
		showPage(Page::Sounds);
		return m_sounds->reveal(name) != nullptr;
	case AssetKind::Soundscape:
		showPage(Page::Sounds);
		return m_sounds->reveal(name, SoundBrowser::SoundscapeEntry) != nullptr;
	case AssetKind::Particle:
		showPage(Page::Particles);
		return m_particles->selectSystem(name);
	}
	return false;
}

int MainWindow::findInMap(const AssetRef& asset) {
	Selection found = objectsUsing(m_editor->doc(), asset, m_editor->entities());
	if (currentPage() != Page::Map) showPage(Page::Map);
	// Faces of a material are picked in face mode; decals and overlays naming it in object mode.
	if (!found.faces.empty()) {
		m_editor->setSelectMode(Editor::SelectMode::Face);
		found.entities.clear();
	} else if (!found.entities.empty()) {
		m_editor->setSelectMode(Editor::SelectMode::Object);
	}
	const int count = int(found.faces.size() + found.entities.size());
	m_editor->setSelection(std::move(found));
	emit m_editor->message(count ? tr("%n object(s) use %1", nullptr, count).arg(QString::fromStdString(asset.name))
	                             : tr("Nothing in the map uses %1").arg(QString::fromStdString(asset.name)));
	return count;
}

void MainWindow::fillAssetMenu(QMenu* menu, const AssetRef& asset, AssetPlace from) {
	const auto add = [&](const QString& text, const char* objectName, std::function<void()> run) {
		auto* action = menu->addAction(text, this, std::move(run));
		action->setObjectName(QString::fromLatin1(objectName));
	};
	switch (asset.kind) {
	case AssetKind::Material:
		if (from != AssetPlace::AssetBrowser) add(tr("Show in Asset Browser"), "showInAssetBrowser", [this, asset] { revealAsset(asset, RevealIn::AssetBrowser); });
		if (from != AssetPlace::MaterialsPage) add(tr("Edit in Materials page"), "showInPage", [this, asset] { revealAsset(asset, RevealIn::Page); });
		break;
	case AssetKind::Model:
		if (from != AssetPlace::AssetBrowser) add(tr("Show in Asset Browser"), "showInAssetBrowser", [this, asset] { revealAsset(asset, RevealIn::AssetBrowser); });
		if (from != AssetPlace::ModelsPage) add(tr("Show in Models page"), "showInPage", [this, asset] { revealAsset(asset, RevealIn::Page); });
		break;
	case AssetKind::Sound:
		add(tr("Show in Sounds page"), "showInPage", [this, asset] { revealAsset(asset, RevealIn::Page); });
		break;
	case AssetKind::Soundscape:
		add(tr("Edit soundscape in Sounds page"), "showInPage", [this, asset] { revealAsset(asset, RevealIn::Page); });
		break;
	case AssetKind::Particle:
		add(tr("Show in Particles page"), "showInPage", [this, asset] { revealAsset(asset, RevealIn::Page); });
		break;
	}
	add(tr("Find in map"), "findInMap", [this, asset] { findInMap(asset); });
	add(tr("Copy name"), "copyAssetName", [asset] { QApplication::clipboard()->setText(QString::fromStdString(asset.name)); });
}

QMenu* MainWindow::buildContextMenu() {
	auto* menuPointer = new QMenu(this);
	QMenu& menu = *menuPointer;
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
		// Doors from world brushes; connecting one entity to another.
		if (!selection.solids.empty() && selection.entities.empty() && selection.faces.empty()) {
			menu.addSeparator();
			menu.addMenu(m_doorMenu);
		}
		if (selection.entities.size() == 1 && selection.solids.empty() && selection.entities.front() != 0) {
			menu.addSeparator();
			const int id = selection.entities.front();
			menu.addAction(tr("Connect to ... (then click the target)"), this, [this, id] { m_editor->beginConnect(id); })->setObjectName("contextConnect");
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
	// What the selection uses: each asset with its "Show in ..." entries.
	const auto kindName = [](AssetKind kind) {
		switch (kind) {
		case AssetKind::Material: return tr("Material");
		case AssetKind::Model: return tr("Model");
		case AssetKind::Sound: return tr("Sound");
		case AssetKind::Particle: return tr("Particle system");
		case AssetKind::Soundscape: return tr("Soundscape");
		}
		return QString();
	};
	const auto assets = selectionAssets(m_editor->doc(), selection, m_editor->entities());
	for (const auto& asset : assets) {
		const QString title = tr("%1: %2").arg(kindName(asset.kind), QString::fromStdString(asset.name));
		if (assets.size() == 1) {
			menu.addSection(title);
			fillAssetMenu(&menu, asset, AssetPlace::View);
		} else {
			auto* sub = menu.addMenu(title);
			sub->setObjectName(QStringLiteral("asset:") + QString::fromStdString(asset.name));
			fillAssetMenu(sub, asset, AssetPlace::View);
		}
	}
	return menuPointer;
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
	// While dragging: the box as it is under the mouse (Editor::liveBounds).
	const auto box = m_editor->liveBounds() ? *m_editor->liveBounds()
	                                        : (selection.empty() ? Bounds{} : edit::selectionBounds(doc, selection, m_editor->entities()));
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
	// "Snap" and "Object snap" are links: a click switches them ("Objects" alone read like the
	// Objects/Faces selection mode next to it).
	const auto link = [](const QString& key, const QString& text) { return QStringLiteral("<a href='%1' style='text-decoration:none'>%2</a>").arg(key, text); };
	m_gridLabel->setText(tr("Grid %1  ·  %2 %3  ·  %4 %5  ·  Angle %6°  ·  Texture lock %7")
	                         .arg(m_editor->grid())
	                         .arg(link("snap", tr("Snap")), onOff(m_editor->snapping()))
	                         .arg(link("objects", tr("Object snap")), onOff(m_editor->objectSnap()))
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
	if (m_editor->selection().faces.size() < 2) {
		QMessageBox::information(this, tr("Round edge"),
		                         tr("Select the faces on both sides of the edge first:\nFaces mode (3), click a face, Ctrl+click the others.\n"
		                            "Each side may be made of several brushes; several corners can be rounded at once."));
		return;
	}
	if (m_roundDialog) {
		m_roundDialog->raise();
		m_roundDialog->activateWindow();
		return;
	}
	// Beside the map, not in front of it: the views show the result while the values change.
	m_roundDialog = new RoundEdgeDialog(m_editor, this);
	m_roundDialog->setAttribute(Qt::WA_DeleteOnClose);
	m_roundDialog->show();
	if (m_active) {
		const QPoint corner = m_active->mapToGlobal(QPoint(16, 16));
		m_roundDialog->move(corner);
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
	m_dockLayout->save(m_mapHidden ? m_mapDockState : QByteArray());
	event->accept();
}

void MainWindow::contextMenuEvent(QContextMenuEvent* event) {
	if (QDockWidget* dock = m_dockLayout->dockAtTab(event->globalPos())) {
		m_dockLayout->showMenu(dock, event->globalPos());
		return;
	}
	QMainWindow::contextMenuEvent(event);
}

ImportDialog* MainWindow::openImport(ImportDialog::Kind kind) {
	if (m_importDialog && m_importDialog->kind() != kind && !m_importDialog->busy()) m_importDialog->deleteLater();
	if (!m_importDialog || m_importDialog->kind() != kind) {
		m_importDialog = new ImportDialog(m_editor, kind, this);
		m_importDialog->setAttribute(Qt::WA_DeleteOnClose);
		connect(m_importDialog, &ImportDialog::modChanged, this, &MainWindow::reloadGameSoon);
	}
	m_importDialog->show();
	m_importDialog->raise();
	m_importDialog->activateWindow();
	return m_importDialog;
}

void MainWindow::showImports() {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return;
	const auto modDir = entry->gameDir;
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Imported files"));
	dialog.setMinimumSize(520, 320);
	auto* v = new QVBoxLayout(&dialog);
	auto* about = new QLabel(tr("Everything Mapsmith imported into this mod. Remove takes the files out again (and the lines it added to sound "
	                            "scripts and manifests); replaced files come back."));
	about->setWordWrap(true);
	v->addWidget(about);
	auto* list = new QListWidget;
	list->setObjectName("importList");
	v->addWidget(list, 1);
	const auto fill = [list, modDir] {
		list->clear();
		for (const auto& record : loadImports(modDir)) {
			list->addItem(QStringLiteral("%1  -  %2  (%3 files)").arg(QString::fromStdString(record.kind), QString::fromStdString(record.asset)).arg(record.created.size()));
		}
	};
	fill();
	auto* buttons = new QHBoxLayout;
	auto* remove = new QPushButton(tr("Remove import"));
	remove->setObjectName("importRemove");
	auto* close = new QPushButton(tr("Close"));
	buttons->addWidget(remove);
	buttons->addStretch(1);
	buttons->addWidget(close);
	v->addLayout(buttons);
	connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
	connect(remove, &QPushButton::clicked, &dialog, [this, list, modDir, fill] {
		const auto records = loadImports(modDir);
		const int row = list->currentRow();
		if (row < 0 || row >= int(records.size())) return;
		std::string error;
		if (!undoImport(records[std::size_t(row)], &error)) m_log->append(QString::fromStdString(error), QColor(255, 140, 120));
		forgetImport(modDir, records[std::size_t(row)]);
		m_log->append(tr("Removed the import %1").arg(QString::fromStdString(records[std::size_t(row)].asset)));
		fill();
		reloadGameSoon();
	});
	dialog.exec();
}

void MainWindow::reloadGameSoon() {
	if (m_reloadPending) return;
	m_reloadPending = true;
	m_log->append(tr("The mod's files changed - reading them again"));
	QTimer::singleShot(0, this, [this] {
		m_reloadPending = false;
		if (!m_editor->gameKey().isEmpty()) selectGame(m_editor->gameKey());
	});
}

} // namespace sme::app
