// main_window.cpp - Editor main window (see main_window.h).

#include "main_window.h"

#include "asset_browser.h"
#include "material_cache.h"
#include "panels.h"
#include "properties_panel.h"
#include "sound_browser.h"
#include "tool_panel.h"
#include "viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QWidgetAction>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <map>

#include <sme/checks.h>
#include <sme/game.h>
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
<tr><td><b>F5 / F6 / F8</b></td><td>Unlit / lighting preview / wireframe</td><td><b>F2 F3 F4</b></td><td>Top / front / side view</td></tr>
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
	setCentralWidget(m_rows);
	m_active = m_views[0];
	connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
		for (auto* view : m_views) {
			if (view == now) {
				m_active = view;
			}
		}
	});

	m_cursorLabel = new QLabel;
	m_statsLabel = new QLabel;
	statusBar()->addPermanentWidget(m_cursorLabel);
	statusBar()->addPermanentWidget(m_statsLabel);
	for (auto* view : m_views) {
		connect(view, &Viewport::cursorWorldPosition, m_cursorLabel, &QLabel::setText);
	}
	connect(m_views[0], &Viewport::statsChanged, m_statsLabel, &QLabel::setText);

	createDocks();
	createActions();

	connect(editor, &Editor::message, this, [this](const QString& text) {
		statusBar()->showMessage(text, 6000);
		m_log->append(text);
	});
	connect(editor, &Editor::documentChanged, this, &MainWindow::updateTitle);
	connect(editor, &Editor::gameAboutToChange, this, [this] { m_materials->setFileSystem(nullptr); });
	connect(editor, &Editor::gameChanged, this, [this] {
		m_materials->setFileSystem(m_editor->game() ? &m_editor->game()->files : nullptr);
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
	m_sounds = new SoundBrowser(m_editor);

	auto* tool = addDock(tr("Tool"), m_toolPanel, Qt::RightDockWidgetArea);
	auto* properties = addDock(tr("Object Properties"), m_properties, Qt::RightDockWidgetArea);
	auto* outliner = addDock(tr("Outliner"), m_outliner, Qt::RightDockWidgetArea);
	auto* problems = addDock(tr("Problems"), m_problems, Qt::RightDockWidgetArea);
	tabifyDockWidget(outliner, problems);
	outliner->raise();
	auto* assets = addDock(tr("Asset Browser"), m_assets, Qt::BottomDockWidgetArea);
	auto* sounds = addDock(tr("Sounds"), m_sounds, Qt::BottomDockWidgetArea);
	auto* log = addDock(tr("Log"), m_log, Qt::BottomDockWidgetArea);
	tabifyDockWidget(assets, sounds);
	tabifyDockWidget(assets, log);
	assets->raise();
	resizeDocks({properties}, {420}, Qt::Horizontal);
	resizeDocks({tool, properties, outliner}, {260, 460, 220}, Qt::Vertical);
	resizeDocks({assets}, {290}, Qt::Vertical);
	connect(m_toolPanel, &ToolPanel::wantsAttention, this, [tool] {
		tool->show();
		tool->raise();
	});
}

void MainWindow::raiseDock(const QString& name) {
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
	file->addAction(action(tr("Open ..."), QKeySequence::Open, [this] {
		if (!maybeSave()) return;
		const QString dir = m_editor->gameKey().isEmpty() ? QString() : m_editor->gameSettings().mapDir;
		const QString path = QFileDialog::getOpenFileName(this, tr("Open map"), dir, tr("Valve map (*.vmf)"));
		if (!path.isEmpty()) openMap(path);
	}));
	file->addAction(action(tr("Save"), QKeySequence::Save, [this] { save(false); }));
	file->addAction(action(tr("Save as ..."), QKeySequence("Ctrl+Shift+S"), [this] { save(true); }));
	file->addSeparator();
	file->addAction(action(tr("Exit"), QKeySequence::Quit, [this] { close(); }));

	// Edit
	auto* editMenu = menuBar()->addMenu(tr("&Edit"));
	editMenu->addAction(action(tr("Undo"), QKeySequence::Undo, [this] { m_editor->undo(); }));
	editMenu->addAction(action(tr("Redo"), QKeySequence("Ctrl+Y"), [this] { m_editor->redo(); }));
	action(tr("Redo"), QKeySequence("Ctrl+Shift+Z"), [this] { m_editor->redo(); });
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Delete"), QKeySequence::Delete, [this] { m_editor->deleteSelection(); }));
	editMenu->addAction(action(tr("Duplicate"), QKeySequence("Ctrl+D"), [this] { m_editor->duplicateSelection(); }));
	editMenu->addAction(action(tr("Select all"), QKeySequence::SelectAll, [this] { m_editor->selectAll(); }));
	editMenu->addAction(action(tr("Select nothing"), QKeySequence("Ctrl+Shift+D"), [this] { m_editor->clearSelection(); }));
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Apply current material"), QKeySequence("Shift+T"), [this] { m_editor->applyMaterialToSelection(m_editor->currentMaterial()); }));
	editMenu->addAction(action(tr("Tie to entity (func_detail)"), QKeySequence("Ctrl+T"), [this] { m_editor->tieSelectionToEntity("func_detail"); }));
	editMenu->addAction(action(tr("Move to world"), QKeySequence("Ctrl+Shift+W"), [this] { m_editor->moveSelectionToWorld(); }));
	editMenu->addSeparator();
	editMenu->addAction(action(tr("Hide selection"), QKeySequence("H"), [this] { m_editor->hideSelection(); }));
	editMenu->addAction(action(tr("Hide others"), QKeySequence("Ctrl+H"), [this] { m_editor->hideUnselected(); }));
	editMenu->addAction(action(tr("Show all"), QKeySequence("U"), [this] { m_editor->unhideAll(); }));

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

	// Tools (left tool bar)
	auto* tools = new QToolBar(tr("Tools"));
	tools->setObjectName("Tools");
	tools->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
	addToolBar(Qt::LeftToolBarArea, tools);
	m_toolGroup = new QActionGroup(this);
	m_toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);   // none checked in face mode
	const auto toolAction = [&](const QString& glyph, const QString& name, const QKeySequence& key, Editor::Tool tool) {
		auto* a = new QAction(glyph + "\n" + name, this);
		a->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString()));
		a->setShortcut(key);
		a->setCheckable(true);
		a->setData(int(tool));
		m_toolGroup->addAction(a);
		tools->addAction(a);
		connect(a, &QAction::triggered, this, [this, tool] { m_editor->setTool(tool); });
		return a;
	};
	toolAction(QStringLiteral("⬚"), tr("Select"), QKeySequence("Q"), Editor::Tool::Select)->setChecked(true);
	toolAction(QStringLiteral("✥"), tr("Move"), QKeySequence("T"), Editor::Tool::Move);
	toolAction(QStringLiteral("⟳"), tr("Rotate"), QKeySequence("R"), Editor::Tool::Rotate);
	toolAction(QStringLiteral("⤢"), tr("Scale"), QKeySequence("E"), Editor::Tool::Scale);
	tools->addSeparator();
	toolAction(QStringLiteral("■"), tr("Block"), QKeySequence("Shift+B"), Editor::Tool::Block);
	toolAction(QStringLiteral("✦"), tr("Entity"), QKeySequence("Shift+E"), Editor::Tool::Entity);
	tools->addSeparator();
	m_modeGroup = new QActionGroup(this);
	const auto modeAction = [&](const QString& glyph, const QString& name, const QKeySequence& key, Editor::SelectMode mode) {
		auto* a = new QAction(glyph + "\n" + name, this);
		a->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString()));
		a->setShortcuts({key});
		a->setCheckable(true);
		m_modeGroup->addAction(a);
		tools->addAction(a);
		connect(a, &QAction::triggered, this, [this, mode] { m_editor->setSelectMode(mode); });
		return a;
	};
	modeAction(QStringLiteral("▱"), tr("Faces"), QKeySequence("3"), Editor::SelectMode::Face);
	auto* objects = modeAction(QStringLiteral("▣"), tr("Objects"), QKeySequence("5"), Editor::SelectMode::Object);
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
	addToolBar(Qt::TopToolBarArea, top);
	top->addWidget(new QLabel(tr(" Game ")));
	m_gameButton = new QToolButton;
	m_gameButton->setPopupMode(QToolButton::InstantPopup);
	m_gameButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_gameButton->setMinimumWidth(260);
	m_gameButton->setToolTip(tr("Game or mod to edit for (all Source 1 games and mods found on this PC)"));
	m_gameMenu = new QMenu(m_gameButton);
	m_gameMenu->setToolTipsVisible(true);
	m_gameButton->setMenu(m_gameMenu);
	top->addWidget(m_gameButton);
	auto* gameSettings = top->addAction(tr("⚙"));
	gameSettings->setToolTip(tr("Game settings: FGDs, compile tools, game executable"));
	connect(gameSettings, &QAction::triggered, this, [this] {
		if (m_editor->gameKey().isEmpty()) return;
		GameSettingsDialog dialog(m_editor, m_editor->gameKey(), this);
		if (dialog.exec() == QDialog::Accepted) {
			m_editor->setGameSettings(m_editor->gameKey(), dialog.settings());
		}
	});
	top->addSeparator();
	top->addWidget(new QLabel(tr(" Grid ")));
	m_gridCombo = new QComboBox;
	for (const double g : {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0}) {
		m_gridCombo->addItem(QString::number(g), g);
	}
	m_gridCombo->setCurrentText(QString::number(m_editor->grid()));
	top->addWidget(m_gridCombo);
	connect(m_gridCombo, &QComboBox::activated, this, [this](int index) { m_editor->setGrid(m_gridCombo->itemData(index).toDouble()); });
	connect(m_editor, &Editor::settingsChanged, this, [this] { m_gridCombo->setCurrentText(QString::number(m_editor->grid())); });
	action(tr("Grid smaller"), QKeySequence("["), [this] { m_editor->setGrid(m_editor->grid() / 2); });
	action(tr("Grid bigger"), QKeySequence("]"), [this] { m_editor->setGrid(m_editor->grid() * 2); });
	auto* snapAction = top->addAction(tr("Snap"));
	snapAction->setCheckable(true);
	snapAction->setChecked(m_editor->snapping());
	snapAction->setShortcut(QKeySequence("Shift+W"));
	snapAction->setToolTip(tr("Snap to the grid and to the rotation step (Shift+W)"));
	connect(snapAction, &QAction::toggled, m_editor, &Editor::setSnapping);
	top->addWidget(new QLabel(tr(" Rotate ")));
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
	m_textureLock = top->addAction(tr("Texture lock"));
	m_textureLock->setCheckable(true);
	m_textureLock->setChecked(m_editor->textureLock());
	m_textureLock->setToolTip(tr("Textures stay glued to faces when moving/rotating"));
	connect(m_textureLock, &QAction::toggled, m_editor, &Editor::setTextureLock);
	top->addSeparator();

	m_renderGroup = new QActionGroup(this);
	const auto renderAction = [&](const QString& name, const QKeySequence& key, Editor::RenderMode mode, const QString& tip) {
		auto* a = top->addAction(name);
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
	top->addSeparator();
	auto* layout = top->addAction(tr("1 / 4 views"));
	layout->setShortcut(QKeySequence("Shift+Alt+Z"));
	layout->setToolTip(tr("Switch between one 3D view and 3D + top/front/side (Shift+Alt+Z)"));
	connect(layout, &QAction::triggered, this, [this] { setQuadLayout(!m_quad); });
	top->addSeparator();
	auto* check = top->addAction(tr("✔ Check"));
	check->setToolTip(tr("Check the map for problems"));
	connect(check, &QAction::triggered, this, [this] {
		m_problems->runChecks();
		raiseDock("Problems");
	});
	auto* run = top->addAction(tr("▶ Compile && Run"));
	run->setShortcut(QKeySequence("F9"));
	run->setToolTip(tr("Compile with VBSP/VVIS/VRAD and start the game (F9)"));
	connect(run, &QAction::triggered, this, &MainWindow::compile);

	// View
	auto* view = menuBar()->addMenu(tr("&View"));
	view->addAction(layout);
	view->addAction(action(tr("Frame selection"), QKeySequence("F"), [this] { activeView()->frameSelection(); }));
	action(tr("Frame selection"), QKeySequence("Ctrl+Shift+E"), [this] { activeView()->frameSelection(); });
	view->addAction(action(tr("Top view"), QKeySequence("F2"), [this] { activeView()->setCameraKind(Camera::Kind::Top); }));
	view->addAction(action(tr("Front view"), QKeySequence("F3"), [this] { activeView()->setCameraKind(Camera::Kind::Front); }));
	view->addAction(action(tr("Side view"), QKeySequence("F4"), [this] { activeView()->setCameraKind(Camera::Kind::Side); }));
	view->addAction(action(tr("3D view"), QKeySequence("Ctrl+F1"), [this] { activeView()->setCameraKind(Camera::Kind::Perspective); }));
	view->addSeparator();
	for (auto* dock : m_docks) {
		view->addAction(dock->toggleViewAction());
	}

	// Map
	auto* map = menuBar()->addMenu(tr("&Map"));
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

	auto* help = menuBar()->addMenu(tr("&Help"));
	help->addAction(action(tr("Shortcuts"), QKeySequence("F1"), [this] { QMessageBox::information(this, tr("Shortcuts"), SHORTCUT_HELP); }));
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
			makeAction(sub, *game, QStringLiteral("%1   (%2)").arg(QString::fromStdString(game->title), QString::fromStdString(game->folder())));
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
	if (!m_editor->openMap(path, &error)) {
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
	setWindowTitle(QStringLiteral("%1%2 - %3 - Source Map Editor").arg(file, m_editor->modified() ? "*" : "", game));
}

bool MainWindow::save(bool saveAs) {
	QString path = m_editor->filePath();
	if (saveAs || path.isEmpty()) {
		const QString dir = m_editor->gameKey().isEmpty() ? QString() : m_editor->gameSettings().mapDir;
		path = QFileDialog::getSaveFileName(this, tr("Save map"), dir, tr("Valve map (*.vmf)"));
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
	CompileDialog dialog(m_editor->filePath(), gameDir, m_editor->gameSettings(), this);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	for (auto* view : m_views) {
		view->setLeakLine({});
	}
	raiseDock("Log");
	m_log->append(tr("--- Compiling %1 ---").arg(QFileInfo(m_editor->filePath()).fileName()), QColor(140, 180, 255));
	m_compiler.start(m_editor->filePath(), gameDir, m_editor->gameSettings(), dialog.options());
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (!maybeSave()) {
		event->ignore();
		return;
	}
	QSettings().setValue("window/geometry", saveGeometry());
	event->accept();
}

} // namespace sme::app
