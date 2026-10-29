// test_app.cpp - Drives the real editor window with simulated mouse input: block tool,
// selection, move gizmo (and Shift-copy), undo, entity tool, face mode, autofix button,
// 2D block creation, save and reload.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "fixture.h"

#include <QApplication>
#include <QCursor>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QTableWidget>
#include <QDesktopServices>
#include <QLayout>
#include <QTabBar>
#include <QProgressBar>
#include <QAbstractSpinBox>
#include <QEnterEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QComboBox>
#include <QStyle>
#include <QPalette>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <QSlider>
#include <QSemaphore>
#include <QSettings>
#include <QStatusBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QThreadPool>
#include <QtConcurrent>
#include <QTemporaryDir>
#include <QTest>
#include <QTabWidget>
#include <QMessageBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QContextMenuEvent>
#include <QFrame>
#include <QOpenGLWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QWheelEvent>

#include <sme/studio_anim.h>
#include <sme/animation_use.h>
#include <sme/npc_scripts.h>
#include <sme/audio.h>
#include <sme/sound_edit.h>
#include <sme/samples.h>
#include <sme/model_import.h>
#include <sme/asset_import.h>
#include <sme/asset_refs.h>
#include <sme/brush.h>
#include <sme/displacement.h>
#include <sme/doors.h>
#include <sme/map_helpers.h>
#include <sme/entity_search.h>
#include <sme/text.h>
#include <sme/fidelity_effects.h>
#include <sme/fidelity_look.h>
#include <sme/heightmap.h>
#include <sme/mod_features.h>
#include <sme/soundscape.h>
#include <sme/material.h>
#include <sme/texture.h>
#include <sme/map_tools.h>
#include <sme/shipping.h>
#include <sme/soundscape_area.h>

#include <cmath>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <memory>
#include <cstdio>
#include <functional>

#include "asset_browser.h"
#include "import_dialog.h"
#include "samples_folder.h"
#include "class_picker.h"
#include "autosaver.h"
#include "find_dialog.h"
#include "history_panel.h"
#include "replace_materials_dialog.h"
#include "dialog_fit.h"
#include "dock_layout.h"
#include "lazy_refresh.h"
#include "menu_editor.h"
#include "shortcut_dialog.h"
#include "compile.h"
#include "compile_window.h"
#include "properties_panel.h"
#include "ship_panel.h"
#include "toggle_switch.h"
#include "connect_popup.h"
#include "copy_paste.h"
#include "editor.h"
#include "enhancements.h"
#include "features_panel.h"
#include "main_window.h"
#include "start_page.h"
#include "mod_code_panel.h"
#include "mod_page.h"
#include "model_viewer.h"
#include "animations_page.h"
#include "model_export_dialog.h"
#include "models_page.h"
#include "map_look_panel.h"
#include "material_preview.h"
#include "material_cache.h"
#include "material_editor.h"
#include "panels.h"
#include "particle_browser.h"
#include "sound_player.h"
#include "style.h"
#include "sound_browser.h"
#include "soundscape_editor.h"
#include "source_control_panel.h"
#include <sme/visgroups.h>
#include <sme/visibility.h>
#include "visgroups_panel.h"
#include "maps_panel.h"
#include "new_mod_dialog.h"
#include <sme/mod_menu.h>
#include "menu_editor.h"
#include "texts_panel.h"
#include <sme/localization.h>
#include "tool_panel.h"
#include "updater.h"
#include "model_cache.h"
#include "viewport.h"

using namespace sme;
using namespace sme::app;

namespace {

QPoint screen(Viewport* view, const Vec3& world) {
	QPointF p;
	REQUIRE(view->camera().project(world, view->width(), view->height(), p));
	return p.toPoint();
}

void settle() {
	for (int i = 0; i < 5; ++i) {
		QApplication::processEvents();
		QTest::qWait(20);
	}
}

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

Bounds boundsOf(const Editor& editor, const SolidRef& ref) {
	return buildBrushGeometry(*editor.doc().findSolid(ref)).bounds;
}

} // namespace

TEST_CASE("editing with the mouse in the real window") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);

	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	editor.setGrid(16);
	settle();

	{
		// Game menu: every game and mod has its own name; mods based on several games are grouped.
		settle();
		QStringList names;
		const std::function<void(QMenu*)> collect = [&](QMenu* menu) {
			for (auto* action : menu->actions()) {
				if (action->menu()) {
					collect(action->menu());
				} else if (action->isCheckable() && action->isVisible()) {
					names << action->text();
				}
			}
		};
		collect(window.gameMenu());
		CHECK(names.size() == int(editor.discovery().games.size()));
		CHECK(names.removeDuplicates() == 0);
		CHECK(names.filter("[Mapbase]").size() >= 1);
		window.gameMenu()->grab().save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/game_menu.png");
	}

	Viewport* view = window.view3D();
	auto& camera = view->camera();
	camera.position = QVector3D(0, -700, 500);
	camera.yaw = 90;
	camera.pitch = -35;
	view->update();
	settle();

	// Block tool: drag the base, then move up for the height and click.
	editor.setTool(Editor::Tool::Block);
	QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-64, -64, 0}));
	QTest::mouseMove(view, screen(view, {64, 64, 0}));
	QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {64, 64, 0}));
	{
		// Right mouse during the block tool looks around without cancelling the block.
		const QPoint anchor = screen(view, {0, 0, 64});
		QCursor::setPos(view->mapToGlobal(anchor));
		QTest::mousePress(view, Qt::RightButton, {}, anchor);
		QCursor::setPos(view->mapToGlobal(anchor + QPoint(30, 0)));
		QTest::mouseMove(view, anchor + QPoint(30, 0));
		settle();
		QTest::mouseRelease(view, Qt::RightButton, {}, anchor + QPoint(30, 0));
		settle();
		// Only where the window system lets programs move the cursor (not Wayland).
		if (QCursor::pos() == view->mapToGlobal(anchor)) {
			CHECK(camera.yaw == doctest::Approx(90 - 30 * 0.2));
		}
		camera.yaw = 90;
		camera.pitch = -35;
		view->update();
		settle();
	}
	QTest::mouseMove(view, screen(view, {0, 0, 128}));
	settle();
	QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {0, 0, 128}));
	QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {0, 0, 128}));
	settle();
	REQUIRE(editor.doc().world.solids.size() == 1);
	const SolidRef block{0, editor.doc().world.solids.front().id};
	auto b = boundsOf(editor, block);
	CHECK(b.min.x == doctest::Approx(-64));
	CHECK(b.max.y == doctest::Approx(64));
	CHECK(b.min.z == doctest::Approx(0));
	CHECK(b.max.z == doctest::Approx(128).epsilon(0.15));
	CHECK(editor.selection().solids.size() == 1);
	{
		// The block tool stays ready: Enter creates, the next drag starts the next block right away.
		for (int i = 1; i <= 2; ++i) {
			const double x = 64 + 160 * i;
			QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {x, -64, 0}));
			QTest::mouseMove(view, screen(view, {x + 96, 64, 0}));
			QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {x + 96, 64, 0}));
			QTest::mouseMove(view, screen(view, {x + 48, 0, 64}));
			settle();
			QTest::keyClick(view, Qt::Key_Return);
			settle();
			CHECK(editor.doc().world.solids.size() == std::size_t(1 + i));
			CHECK(editor.tool() == Editor::Tool::Block);
		}
		// While the height is set, dragging somewhere else creates this block and starts the next.
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-320, -64, 0}));
		QTest::mouseMove(view, screen(view, {-224, 64, 0}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {-224, 64, 0}));
		QTest::mouseMove(view, screen(view, {-272, 0, 64}));
		settle();
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-320, -320, 0}));
		CHECK(editor.doc().world.solids.size() == 4);
		QTest::mouseMove(view, screen(view, {-224, -224, 0}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {-224, -224, 0}));
		QTest::mouseMove(view, screen(view, {-272, -272, 64}));
		settle();
		QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {-272, -272, 64}));
		settle();
		CHECK(editor.doc().world.solids.size() == 5);
		for (int i = 0; i < 4; ++i) {
			editor.undo();
		}
		REQUIRE(editor.doc().world.solids.size() == 1);
	}

	// Clicking empty space clears, clicking the block selects it.
	editor.setTool(Editor::Tool::Select);
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {400, 300, 0}));
	settle();
	CHECK(editor.selection().empty());
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {0, 0, b.max.z}));
	settle();
	REQUIRE(editor.selection().solids.size() == 1);

	// Move gizmo: drag the X arrow by 64 units.
	editor.setTool(Editor::Tool::Move);
	view->update();
	settle();
	const Vec3 center = b.center();
	const double size = std::max(8.0, length(fromQt(camera.position) - center) * 0.16);
	const Vec3 grab = center + Vec3{size * 0.6, 0, 0};
	QTest::mousePress(view, Qt::LeftButton, {}, screen(view, grab));
	QTest::mouseMove(view, screen(view, grab + Vec3{32, 0, 0}));
	QTest::mouseMove(view, screen(view, grab + Vec3{64, 0, 0}));
	QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, grab + Vec3{64, 0, 0}));
	settle();
	b = boundsOf(editor, block);
	CHECK(b.min.x == doctest::Approx(0));
	CHECK(b.max.x == doctest::Approx(128));

	// Shift+drag copies.
	const Vec3 grab2 = b.center() + Vec3{size * 0.6, 0, 0};
	QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, screen(view, grab2));
	QTest::mouseMove(view, screen(view, grab2 + Vec3{160, 0, 0}));
	QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, screen(view, grab2 + Vec3{160, 0, 0}));
	settle();
	CHECK(editor.doc().world.solids.size() == 2);
	editor.undo();
	CHECK(editor.doc().world.solids.size() == 1);

	// Scale: drag the +X handle; the texture keeps its scale (no stretching).
	{
		sme::Selection only;
		only.solids.push_back(block);
		editor.setSelection(only);
		editor.setTool(Editor::Tool::Scale);
		view->update();
		settle();
		const auto uaxisBefore = editor.doc().findSolid(block)->sides.front().u;
		const Vec3 handle{b.max.x, b.center().y, b.center().z};
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, handle));
		QTest::mouseMove(view, screen(view, handle + Vec3{32, 0, 0}));
		QTest::mouseMove(view, screen(view, handle + Vec3{64, 0, 0}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, handle + Vec3{64, 0, 0}));
		settle();
		const auto scaled = boundsOf(editor, block);
		CHECK(scaled.min.x == doctest::Approx(0));
		CHECK(scaled.max.x == doctest::Approx(192));
		CHECK(editor.doc().findSolid(block)->sides.front().u.scale == doctest::Approx(uaxisBefore.scale));
		if (scaled.max.x != b.max.x) {
			editor.undo();
		}
		b = boundsOf(editor, block);
		CHECK(b.max.x == doctest::Approx(128));
		editor.setTool(Editor::Tool::Select);
		// Select tool: the same squares are there - drag the top one to make the block taller.
		view->update();
		settle();
		const Vec3 top{b.center().x, b.center().y, b.max.z};
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, top));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 16}));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 32}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, top + Vec3{0, 0, 32}));
		settle();
		CHECK(boundsOf(editor, block).max.z == doctest::Approx(b.max.z + 32));
		CHECK(boundsOf(editor, block).min.z == doctest::Approx(b.min.z));
		CHECK(editor.selection().solids == std::vector<SolidRef>{block});   // still selected, not a marquee
		editor.undo();
		CHECK(boundsOf(editor, block).max.z == doctest::Approx(b.max.z));
	}
	{
		// Preview volume in decibels: half way is quiet, the bottom silent, the top as recorded.
		CHECK(SoundPlayer::gain(0) == 0.f);
		CHECK(SoundPlayer::gain(1) == doctest::Approx(1));
		CHECK(SoundPlayer::gain(0.5f) < 0.1f);
		CHECK(SoundPlayer::gain(0.1f) < 0.01f);
	}
	{
		// The Tool panel lists the keys of the current tool, with the shortcuts as they are set.
		editor.setTool(Editor::Tool::Select);
		settle();
		auto* keys = window.findChild<QTableWidget*>("toolKeys");
		REQUIRE(keys);
		QStringList operations;
		for (int row = 0; row < keys->rowCount(); ++row) operations << keys->item(row, 0)->text() + " " + keys->item(row, 1)->text();
		CHECK(operations.join('\n').contains("Paste several copies"));
		CHECK(operations.join('\n').contains("[Ctrl+Alt+O] Select similar objects"));
	}
	{
		// Hammer tools on the selection: transform (one undo step), flip, snap to grid, go to id, select similar.
		sme::Selection only;
		only.solids.push_back(block);
		editor.setSelection(only);
		const auto start = boundsOf(editor, block);
		editor.transformSelection({32, 0, 0}, {0, 0, 0}, {2, 1, 1});
		auto now = boundsOf(editor, block);
		CHECK(now.size().x == doctest::Approx(start.size().x * 2));
		CHECK(now.center().x == doctest::Approx(start.center().x + 32));
		editor.undo();
		CHECK(boundsOf(editor, block).min.x == doctest::Approx(start.min.x));
		editor.flipSelection(2);
		CHECK(boundsOf(editor, block).min.z == doctest::Approx(start.min.z));   // flipped in place
		CHECK(isConvexValid(*editor.doc().findSolid(block)));
		editor.undo();
		editor.nudgeSelection({3, 0, 0});
		editor.snapSelectionToGrid();
		CHECK(std::fmod(boundsOf(editor, block).min.x, editor.grid()) == doctest::Approx(0));
		editor.undo();
		editor.undo();
		CHECK(boundsOf(editor, block).min.x == doctest::Approx(start.min.x));
		editor.clearSelection();
		REQUIRE(editor.selectById(block.solidId));
		CHECK(editor.selection().solids == std::vector<SolidRef>{block});
		CHECK_FALSE(editor.selectById(987654));
		editor.selectSimilar();
		CHECK(!editor.selection().solids.empty());
	}
	{
		// Copy and paste: Ctrl+C puts the block on the clipboard as VMF text, Ctrl+V pastes it
		// under the mouse (one undo step), Paste special makes several copies.
		sme::Selection only;
		only.solids.push_back(block);
		editor.setSelection(only);
		{
			// The status bar names the selection and gives its size and center.
			QStringList texts;
			for (auto* label : window.statusBar()->findChildren<QLabel*>()) texts << label->text();
			CHECK(texts.contains("Brush (6 faces)"));
			const QString size = QStringLiteral("%1w %2l %3h").arg(b.size().x).arg(b.size().y).arg(b.size().z);
			CHECK(std::any_of(texts.begin(), texts.end(), [&](const QString& t) { return t.startsWith(size); }));
			CHECK(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Grid 16"); }));
		}
		editor.copySelection();
		CHECK(QApplication::clipboard()->text().contains("world"));
		const std::size_t before = editor.doc().world.solids.size();
		const Vec3 target{-256, -256, 0};
		QCursor::setPos(view->mapToGlobal(screen(view, target)));
		settle();
		QAction* pasteAction = nullptr;
		for (auto* a : window.actions()) {
			if (a->objectName() == "Paste") pasteAction = a;
		}
		REQUIRE(pasteAction);
		pasteAction->trigger();
		settle();
		REQUIRE(editor.doc().world.solids.size() == before + 1);
		REQUIRE(editor.selection().solids.size() == 1);
		const auto pasted = boundsOf(editor, editor.selection().solids.front());
		CHECK(pasted.size().x == doctest::Approx(b.size().x));
		if (view->mapFromGlobal(QCursor::pos()) == screen(view, target)) {   // the platform moved the cursor
			CHECK(std::abs(pasted.center().x - target.x) <= 64);
			CHECK(std::abs(pasted.center().y - target.y) <= 64);
			CHECK(pasted.min.z == doctest::Approx(0));   // rests on the floor
		}
		editor.undo();
		CHECK(editor.doc().world.solids.size() == before);
		PasteOptions options;
		options.copies = 3;
		options.offset = {0, 256, 0};
		REQUIRE(editor.paste(options));
		CHECK(editor.doc().world.solids.size() == before + 3);
		CHECK(editor.selection().solids.size() == 3);
		editor.undo();
		CHECK(editor.doc().world.solids.size() == before);
	}

	// Entity tool: an info_player_start on top of the block stands on it.
	editor.clearSelection();
	editor.setCurrentEntityClass("info_player_start");
	editor.setTool(Editor::Tool::Entity);
	// Before the click: a preview stands where it would go.
	QTest::mouseMove(view, screen(view, {64, 0, b.max.z}));
	settle();
	REQUIRE(view->ghostBox());
	CHECK(view->ghostBox()->min.z == doctest::Approx(b.max.z));
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {64, 0, b.max.z}));
	settle();
	REQUIRE(editor.doc().entities.size() == 1);
	CHECK(editor.doc().entities.front().origin().z == doctest::Approx(b.max.z));
	{
		// The entity tool shows the entity list in the Tool panel; a search finds classes.
		auto* tree = window.toolPanel()->entityPicker()->tree();
		CHECK(tree->isVisible());
		CHECK_FALSE(tree->findItems("info_player_start", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
		CHECK_FALSE(tree->findItems("npc_citizen", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
		// Point and brush entities in their own branches.
		const auto trigger = tree->findItems("trigger_once", Qt::MatchExactly | Qt::MatchRecursive);
		REQUIRE_FALSE(trigger.isEmpty());
		CHECK(trigger.front()->parent()->text(0).startsWith("Brush entities"));
		// "global light" finds Mapbase's env_global_light (words match across the underscore).
		auto* search = window.toolPanel()->entityPicker()->findChild<QLineEdit*>();
		REQUIRE(search);
		search->setText("global light");
		const auto global = tree->findItems("env_global_light", Qt::MatchExactly | Qt::MatchRecursive);
		REQUIRE(global.size() == 1);
		CHECK_FALSE(global.front()->isHidden());
		CHECK(tree->findItems("npc_citizen", Qt::MatchExactly | Qt::MatchRecursive).front()->isHidden());
		search->clear();
		// Editor sprites become the icons (light_spot: white dot inside its ring).
		const auto spot = tree->findItems("light_spot", Qt::MatchExactly | Qt::MatchRecursive);
		REQUIRE_FALSE(spot.isEmpty());
		QColor middle;
		for (int i = 0; i < 100; ++i) {
			const QImage icon = spot.front()->icon(0).pixmap(20, 20).toImage();
			middle = icon.pixelColor(icon.width() / 2, icon.height() / 2);
			if (middle.red() > 230 && middle.green() > 230 && middle.blue() > 230) break;
			settle();
		}
		CHECK(middle.blue() > 230);   // the class color square would be yellow
		// Classes without a sprite show their editor model (the green player start).
		const auto start = tree->findItems("info_player_start", Qt::MatchExactly | Qt::MatchRecursive);
		REQUIRE_FALSE(start.isEmpty());
		bool green = false;
		for (int i = 0; i < 200 && !green; ++i) {
			const QImage icon = start.back()->icon(0).pixmap(20, 20).toImage();
			for (int y = 0; y < icon.height() && !green; ++y) {
				for (int x = 0; x < icon.width() && !green; ++x) {
					const QColor c = icon.pixelColor(x, y);
					green = c.alpha() > 200 && c.green() > 100 && c.green() > c.red() * 2 && c.green() > c.blue() * 2;
				}
			}
			settle();
		}
		CHECK(green);   // the class color square would be magenta
	}
	{
		// Move tool: drag a corner square onto a corner of another brush - they line up exactly.
		const std::size_t before = editor.doc().world.solids.size();
		editor.setShapeOptions(ShapeOptions{});
		editor.createShape(box({-320, 128, 0}, {-256, 192, 64}));
		const SolidRef moving = editor.selection().solids.front();
		editor.createShape(box({-176, 240, 0}, {-112, 304, 48}));
		REQUIRE(editor.doc().world.solids.size() == before + 2);
		Selection select;
		select.solids.push_back(moving);
		editor.setSelection(select);
		editor.setTool(Editor::Tool::Move);
		view->update();
		settle();
		const Vec3 grabCorner{-256, 192, 64}, targetCorner{-176, 240, 48};
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, grabCorner));
		QTest::mouseMove(view, screen(view, grabCorner + Vec3{30, 20, 0}));
		QTest::mouseMove(view, screen(view, targetCorner) + QPoint(3, -2));   // close is enough
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, targetCorner) + QPoint(3, -2));
		settle();
		const auto moved = boundsOf(editor, moving);
		CHECK(moved.max.x == doctest::Approx(-176));
		CHECK(moved.max.y == doctest::Approx(240));
		CHECK(moved.max.z == doctest::Approx(48));
		editor.undo();
		editor.undo();
		editor.undo();
		CHECK(editor.doc().world.solids.size() == before);
		editor.clearSelection();
		editor.setTool(Editor::Tool::Entity);
	}
	{
		// Object properties: every entity gets a Name; Smart Edit off shows plain keys (like Hammer),
		// keys can be added, copied and pasted ("key" "value" lines from a wiki or another map).
		auto* panel = window.properties();
		window.raiseDock("Object Properties");
		const auto* sunClass = editor.entities()->find("light_environment");
		REQUIRE(sunClass);
		auto [change, sunId] = edit::placeEntity(editor.doc(), "light_environment", {0, 0, 400}, sunClass);
		editor.commit(std::move(change));
		Selection sunSel;
		sunSel.entities.push_back(sunId);
		editor.setSelection(sunSel);
		settle();
		bool nameField = false;
		for (auto* label : panel->findChildren<QLabel*>()) {
			nameField = nameField || label->text() == "Name";
		}
		CHECK(nameField);   // light_environment's FGD has no Targetname, the panel adds it

		auto* smart = panel->findChild<QCheckBox*>("smartEdit");
		REQUIRE(smart);
		smart->setChecked(false);
		settle();
		auto* table = panel->findChild<QTableWidget*>("rawKeys");
		REQUIRE(table);
		QStringList keys;
		for (int i = 0; i < table->rowCount(); ++i) keys << table->item(i, 0)->text();
		CHECK(keys.contains("classname"));
		CHECK(keys.contains("targetname"));
		CHECK(keys.contains("_ambient"));
		// Edit a value as text.
		const int pitchRow = int(keys.indexOf("pitch"));
		REQUIRE(pitchRow >= 0);
		table->item(pitchRow, 1)->setText("-60");
		settle();
		CHECK(editor.doc().findEntity(sunId)->get("pitch") == "-60");
		// Paste several keys at once.
		table = panel->findChild<QTableWidget*>("rawKeys");
		QApplication::clipboard()->setText("\"_light\" \"255 230 200 350\"\n\"SunSpreadAngle\" \"5\"");
		table->setFocus();
		QTest::keyClick(table, Qt::Key_V, Qt::ControlModifier);
		settle();
		CHECK(editor.doc().findEntity(sunId)->get("_light") == "255 230 200 350");
		CHECK(editor.doc().findEntity(sunId)->get("SunSpreadAngle") == "5");
		// Add a key the FGD does not know.
		panel->findChild<QLineEdit*>("newKey")->setText("mykey");
		panel->findChild<QLineEdit*>("newValue")->setText("42");
		for (auto* button : panel->findChildren<QPushButton*>()) {
			if (button->text() == "+ Add") {
				button->click();
				break;
			}
		}
		settle();
		CHECK(editor.doc().findEntity(sunId)->get("mykey") == "42");
		// Copy rows: they come out as "key" "value" lines.
		table = panel->findChild<QTableWidget*>("rawKeys");
		for (int i = 0; i < table->rowCount(); ++i) {
			if (table->item(i, 0)->text() == "mykey") table->selectRow(i);
		}
		table->setFocus();
		QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
		CHECK(QApplication::clipboard()->text() == "\"mykey\" \"42\"");
		smart->setChecked(true);
		settle();
		// Text from a wiki: tabs, colons and quotes all work.
		const auto pairs = parseKeyValueLines("_light\t255 255 255 200\nskyname: sky_day01_01\n\"pitch\" \"-45\"\nnot-a-pair");
		REQUIRE(pairs.size() == 3);
		CHECK(pairs[1].first == "skyname");
		CHECK(pairs[1].second == "sky_day01_01");
		CHECK(pairs[2].second == "-45");
		// Ctrl+C on an entity class copies its name.
		auto* classes = window.toolPanel()->entityPicker()->tree();
		const auto spot = classes->findItems("light_spot", Qt::MatchExactly | Qt::MatchRecursive);
		REQUIRE_FALSE(spot.isEmpty());
		classes->setCurrentItem(spot.back());
		classes->setFocus();
		QTest::keyClick(classes, Qt::Key_C, Qt::ControlModifier);
		CHECK(QApplication::clipboard()->text() == "light_spot");
		for (int i = 0; i < 4; ++i) {   // mykey, paste, pitch, the entity
			editor.undo();
		}
		CHECK_FALSE(editor.doc().findEntity(sunId));
		editor.clearSelection();
	}
	{
		// Properties: tabs with counts, keys grouped by the FGD class that defines them,
		// flags and yes/no keys as switches, incoming outputs with "Go to"; the world's sky is
		// picked from the game's skies; Map > Map properties shows the world.
		auto* panel = window.properties();
		window.raiseDock("Object Properties");
		const std::size_t entitiesBefore = editor.doc().entities.size();
		auto [placeLight, lightId] = edit::placeEntity(editor.doc(), "light", {0, 0, 128}, editor.entities()->find("light"), {{"targetname", "lamp1"}});
		editor.commit(std::move(placeLight));
		auto [placeRelay, relayId] = edit::placeEntity(editor.doc(), "logic_relay", {32, 0, 128}, editor.entities()->find("logic_relay"));
		editor.commit(std::move(placeRelay));
		editor.commit(edit::setConnections(editor.doc(), relayId, {Connection{"OnTrigger", "lamp1", "TurnOn", "", 0, -1}}));
		Selection lightSel;
		lightSel.entities.push_back(lightId);
		editor.setSelection(lightSel);
		settle();
		auto* tabs = panel->findChild<QTabBar*>("propertyTabs");
		REQUIRE(tabs);
		CHECK(tabs->tabText(0).startsWith("Keyvalues"));
		CHECK(tabs->tabText(1) == "Outputs  0");
		CHECK(tabs->tabText(2) == "Inputs  1");
		CHECK(panel->findChild<QPushButton*>("group:Targetname"));
		CHECK(panel->findChild<QPushButton*>("group:Parentname"));
		CHECK(panel->findChild<QPushButton*>("group:light"));
		// A group folds away and stays folded.
		auto* lightGroup = panel->findChild<QPushButton*>("group:light");
		lightGroup->click();
		editor.setSelection(lightSel);
		settle();
		CHECK(panel->findChild<QPushButton*>("group:light")->text().contains(QStringLiteral("▸")));
		panel->findChild<QPushButton*>("group:light")->click();
		// Flags are switches: "Initially dark" sets bit 1.
		ToggleSwitch* dark = nullptr;
		for (auto* toggle : panel->findChildren<ToggleSwitch*>()) {
			if (toggle->property("help").toString().contains("Initially dark")) dark = toggle;
		}
		REQUIRE(dark);
		dark->click();
		settle();
		CHECK(editor.doc().findEntity(lightId)->get("spawnflags") == "1");
		editor.undo();
		// Inputs: who sends something here, with Go to.
		settle();
		tabs = panel->findChild<QTabBar*>("propertyTabs");
		tabs->setCurrentIndex(2);
		QToolButton* go = nullptr;
		for (auto* button : panel->findChildren<QToolButton*>()) {
			if (button->text() == "Go to" && button->isVisibleTo(panel)) go = button;
		}
		REQUIRE(go);
		go->click();
		settle();
		CHECK(editor.selection().entities == std::vector<int>{relayId});
		panel->findChild<QTabBar*>("propertyTabs")->setCurrentIndex(0);
		// Map > Map properties: nothing selected, the world's keys.
		QAction* mapProperties = nullptr;
		for (auto* a : window.actions()) {
			if (a->objectName() == "Map properties ...") mapProperties = a;
		}
		REQUIRE(mapProperties);
		mapProperties->trigger();
		settle();
		CHECK(editor.selection().empty());
		CHECK(panel->findChild<QPushButton*>("group:worldspawn"));
		// "World is cold" (0 No / 1 Yes) is a switch.
		ToggleSwitch* cold = nullptr;
		for (auto* toggle : panel->findChildren<ToggleSwitch*>()) {
			if (toggle->parentWidget()->property("help").toString().contains("coldworld")) cold = toggle;
		}
		REQUIRE(cold);
		CHECK_FALSE(cold->isChecked());
		cold->click();
		settle();
		CHECK(editor.doc().world.get("coldworld") == "1");
		editor.undo();
		settle();
		// The sky: the demo's sky is listed (read in the background) and picking it is one undo step.
		QComboBox* sky = nullptr;
		for (int i = 0; i < 100; ++i) {
			sky = panel->findChild<QComboBox*>("skyPicker");
			if (sky && sky->findText("sky_demo01") >= 0) break;
			QTest::qWait(50);
		}
		REQUIRE(sky);
		const int skyIndex = sky->findText("sky_demo01");
		REQUIRE(skyIndex >= 0);
		CHECK_FALSE(sky->itemIcon(skyIndex).isNull());   // a picture of the sky
		const std::string skyBefore = editor.doc().world.get("skyname");
		sky->setCurrentIndex(skyIndex);
		emit sky->activated(skyIndex);
		settle();
		CHECK(editor.doc().world.get("skyname") == "sky_demo01");
		editor.undo();
		CHECK(editor.doc().world.get("skyname") == skyBefore);
		// A sky from the games' list brings its light: typed in, one undo step.
		settle();
		sky = panel->findChild<QComboBox*>("skyPicker");
		REQUIRE(sky);
		const auto lights = [&] {
			int n = 0;
			for (const auto& e : editor.doc().entities) n += e.classname() == "light_environment";
			return n;
		};
		const int lightsBefore = lights();
		sky->setEditText("sky_day02_05");
		emit sky->lineEdit()->editingFinished();
		settle();
		CHECK(editor.doc().world.get("skyname") == "sky_day02_05");
		CHECK(lights() == std::max(lightsBefore, 1));
		for (const auto& e : editor.doc().entities) {
			if (e.classname() == "light_environment") CHECK(e.get("_light") == "255 255 249 448");
		}
		editor.undo();
		CHECK(editor.doc().world.get("skyname") == skyBefore);
		CHECK(lights() == lightsBefore);
		while (editor.doc().entities.size() > entitiesBefore) editor.undo();
		editor.clearSelection();
	}
	{
		// Cubemap room box: fitted to the walls around the cubemap, resized by dragging its side square.
		const std::size_t solidsBefore = editor.doc().world.solids.size();
		const std::size_t entitiesBefore = editor.doc().entities.size();
		editor.setShapeOptions(ShapeOptions{});
		editor.createShape(box({-512, 300, 0}, {-256, 556, 160}));
		editor.hollowSelection(16);
		const auto* cubemapClass = editor.entities()->find("env_cubemap");
		REQUIRE(cubemapClass);
		auto [placed, cubemapId] = edit::placeEntity(editor.doc(), "env_cubemap", {-384, 428, 64}, cubemapClass);
		editor.commit(std::move(placed));
		Selection cubemapSel;
		cubemapSel.entities.push_back(cubemapId);
		editor.setSelection(cubemapSel);
		editor.setTool(Editor::Tool::Select);
		window.raiseDock("Object Properties");
		settle();
		auto* group = window.properties()->findChild<QGroupBox*>("cubemapBox");
		REQUIRE(group);
		QPushButton* add = nullptr;
		for (auto* button : group->findChildren<QPushButton*>()) {
			if (button->text().startsWith("Add room box")) add = button;
		}
		REQUIRE(add);
		add->click();
		settle();
		const Entity* obb = cubemapRoomBox(editor.doc(), *editor.doc().findEntity(cubemapId));
		REQUIRE(obb);
		Bounds room;
		for (const auto& solid : obb->solids) room.add(buildBrushGeometry(solid).bounds);
		CHECK(room.min.x == doctest::Approx(-496));
		CHECK(room.max.y == doctest::Approx(540));
		CHECK(room.max.z == doctest::Approx(144));
		// Drag the top square up by 32 units.
		view->update();
		settle();
		const Vec3 top{-384, 428, 144};
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, top));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 16}));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 32}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, top + Vec3{0, 0, 32}));
		settle();
		obb = cubemapRoomBox(editor.doc(), *editor.doc().findEntity(cubemapId));
		REQUIRE(obb);
		room = {};
		for (const auto& solid : obb->solids) room.add(buildBrushGeometry(solid).bounds);
		CHECK(room.max.z == doctest::Approx(176));
		CHECK(room.min.z == doctest::Approx(16));
		CHECK(editor.selection().entities == std::vector<int>{cubemapId});   // the cubemap stays selected
		while (editor.doc().entities.size() > entitiesBefore || editor.doc().world.solids.size() > solidsBefore) {
			editor.undo();
		}
		editor.clearSelection();
		editor.setTool(Editor::Tool::Entity);
	}
	{
		// Soundscape area: a sphere whose radius follows its top square, or a trigger_soundscape block.
		const std::size_t entitiesBefore = editor.doc().entities.size();
		const auto* scapeClass = editor.entities()->find("env_soundscape");
		REQUIRE(scapeClass);
		auto [placed, scapeId] = edit::placeEntity(editor.doc(), "env_soundscape", {-384, 428, 64}, scapeClass);
		editor.commit(std::move(placed));
		editor.commit(edit::setKeyValue(editor.doc(), {scapeId}, "soundscape", std::string("Demo.Outside")));
		Selection scapeSel;
		scapeSel.entities.push_back(scapeId);
		editor.setSelection(scapeSel);
		editor.setTool(Editor::Tool::Select);
		window.raiseDock("Object Properties");
		settle();
		auto* area = window.properties()->findChild<QGroupBox*>("soundscapeArea");
		REQUIRE(area);
		CHECK(area->findChild<QLabel*>("soundscapeName")->text().contains("Demo.Outside"));
		QSignalSpy editRequest(window.properties(), &PropertiesPanel::editSoundscapeRequested);
		area->findChild<QPushButton*>("editSoundscape")->click();
		REQUIRE(editRequest.count() == 1);
		CHECK(editRequest.front().front().toString() == "Demo.Outside");
		// The window opens it on the Sounds page; back to the map for the rest.
		CHECK(window.currentPage() == MainWindow::Page::Sounds);
		window.showPage(MainWindow::Page::Map);
		settle();
		CHECK(area->findChild<QPushButton*>("soundscapeSphere")->isChecked());
		CHECK(area->findChild<QSpinBox*>("soundscapeRadius")->value() == 128);

		// Drag the top square of the sphere up by 32: radius 160, one undo step.
		view->update();
		settle();
		const Vec3 top{-384, 428, 64 + 128};
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, top));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 16}));
		QTest::mouseMove(view, screen(view, top + Vec3{0, 0, 32}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, top + Vec3{0, 0, 32}));
		settle();
		CHECK(editor.doc().findEntity(scapeId)->get("radius") == "160");
		CHECK(editor.selection().entities == std::vector<int>{scapeId});
		editor.undo();
		CHECK(soundscapeRadius(*editor.doc().findEntity(scapeId)) == doctest::Approx(128));

		// Block: a trigger_soundscape naming the (now triggerable) soundscape; undo brings the sphere back.
		settle();
		area = window.properties()->findChild<QGroupBox*>("soundscapeArea");
		REQUIRE(area);
		area->findChild<QPushButton*>("soundscapeBlock")->click();
		settle();
		const Entity* scape = editor.doc().findEntity(scapeId);
		CHECK(scape->classname() == "env_soundscape_triggerable");
		const Entity* trigger = soundscapeTrigger(editor.doc(), *scape);
		REQUIRE(trigger);
		CHECK(trigger->get("soundscape") == scape->get("targetname"));
		area = window.properties()->findChild<QGroupBox*>("soundscapeArea");
		REQUIRE(area);
		CHECK(area->findChild<QPushButton*>("soundscapeBlock")->isChecked());
		CHECK(area->findChild<QDoubleSpinBox*>("soundscapeBlockSize0")->value() == doctest::Approx(256));
		const int triggerId = trigger->id;
		area->findChild<QPushButton*>("selectSoundscapeBlock")->click();
		CHECK(editor.selection().entities == std::vector<int>{triggerId});
		editor.undo();
		scape = editor.doc().findEntity(scapeId);
		CHECK(scape->classname() == "env_soundscape");
		CHECK_FALSE(soundscapeTrigger(editor.doc(), *scape));
		while (editor.doc().entities.size() > entitiesBefore) editor.undo();
		editor.clearSelection();
		editor.setTool(Editor::Tool::Entity);
	}
	{
		// Material editor: fields change the VMT, the preview draws it, Save writes into the mod.
		auto* materials = window.materialEditor();
		QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
		materials->openMaterial("demo/brickwall001");
		window.raiseDock("Material Editor");
		REQUIRE(previewed.wait(10000));
		// Its own page: the map's docks and tool bars step aside (and come back with the Map page).
		CHECK(window.pages()->currentWidget() == materials);
		CHECK(window.currentPage() == MainWindow::Page::Materials);
		CHECK(window.assets()->isVisible());   // the material list comes along to the page
		CHECK(materials->recentMaterials().value(0) == "demo/brickwall001");
		CHECK(materials->value("$basetexture") == "demo/brickwall001");
		CHECK(materials->value("$surfaceprop") == "brick");
		settle();
		auto* preview = materials->findChild<MaterialPreview*>();
		REQUIRE(preview);
		const QImage shot = preview->grabFramebuffer();
		const QColor middle = shot.pixelColor(shot.width() / 2, shot.height() / 2);
		CHECK(middle.red() > middle.blue());   // brick red, not the grey background
		auto* reflection = materials->findChild<QCheckBox*>("reflectionOn");
		REQUIRE(reflection);
		CHECK_FALSE(materials->findChild<QComboBox*>("reflection")->isVisibleTo(materials));   // folded while off
		reflection->setChecked(true);
		CHECK(materials->findChild<QComboBox*>("reflection")->isVisibleTo(materials));
		// A feature turned off keeps its parameters aside; on again brings them back.
		auto* normalMap = materials->findChild<QCheckBox*>("normalMapOn");
		REQUIRE(normalMap);
		CHECK(normalMap->isChecked());
		normalMap->setChecked(false);
		CHECK(materials->value("$bumpmap").isEmpty());
		normalMap->setChecked(true);
		CHECK(materials->value("$bumpmap") == "demo/brickwall001_normal");
		auto* surface = materials->findChild<QComboBox*>("surfaceprop");
		REQUIRE(surface);
		surface->setCurrentText("metal");
		REQUIRE(materials->save());
		QFile vmt(materials->savePath("demo/brickwall001"));
		REQUIRE(vmt.open(QIODevice::ReadOnly));
		const QString text = vmt.readAll();
		CHECK(text.contains("\"$envmap\" \"env_cubemap\""));
		CHECK(text.contains("\"$surfaceprop\" \"metal\""));
		CHECK(text.contains("demo/brickwall001"));
		// Save as new: a copy under another name, found by the game's file system right away.
		REQUIRE(materials->save("demo/brickwall_dark"));
		CHECK(QFileInfo::exists(materials->savePath("demo/brickwall_dark")));
		const auto copy = loadMaterial(editor.game()->files, "demo/brickwall_dark");
		REQUIRE(copy);
		CHECK(copy->param("$envmap") == "env_cubemap");
	}
	{
		// mapbase-fidelity relief per material: on, depth, height map from the normal map, upside down,
		// painted zones; Save writes the height map next to the material.
		auto* materials = window.materialEditor();
		REQUIRE(materials->fidelityShaders());
		QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
		QSignalSpy heightsReady(materials, &MaterialEditor::heightsReady);
		materials->openMaterial("demo/brickwall001");
		REQUIRE(previewed.wait(10000));
		auto* relief = materials->findChild<QCheckBox*>("relief");
		REQUIRE(relief);
		CHECK(relief->isVisibleTo(materials));
		CHECK(relief->isEnabled());
		CHECK_FALSE(relief->isChecked());
		relief->setChecked(true);
		CHECK(materials->shader() == "Fidelity_Parallax");
		CHECK(materials->value("$parallaxdepth") == "0.006");   // a normal map: shallow (Julius: 0.006 looked right)
		CHECK(materials->value("$parallaxao") == "0.25");
		{
			// The slider's number is always in sight, also with the column as narrow as it goes
			// (Julius: the digits showed only after widening it).
			auto* number = materials->findChild<QLabel*>("$parallaxdepth-value");
			REQUIRE(number);
			QApplication::processEvents();
			CHECK(number->text() == "0.006");
			CHECK(number->isVisibleTo(materials));
			QScrollArea* area = nullptr;
			for (QWidget* w = number->parentWidget(); w && !area; w = w->parentWidget()) area = qobject_cast<QScrollArea*>(w);
			REQUIRE(area);
			auto* split = qobject_cast<QSplitter*>(area->parentWidget());
			REQUIRE(split);
			const int at = split->indexOf(area);
			QList<int> sizes = split->sizes();
			const int squeezed = sizes[at] - 1;
			sizes[at] = 1;   // as narrow as the splitter lets it
			sizes[(at + 1) % sizes.size()] += squeezed;
			split->setSizes(sizes);
			QApplication::processEvents();
			const QRect shownAt(number->mapTo(area->viewport(), QPoint(0, 0)), number->size());
			CHECK(shownAt.left() >= 0);
			CHECK(shownAt.right() < area->viewport()->width());
			CHECK(number->width() >= number->fontMetrics().horizontalAdvance(number->text()));
		}
		// The height map may be ready already (made while the slider was checked above).
		REQUIRE((heightsReady.count() > 0 || heightsReady.wait(10000)));
		const auto made = materials->currentHeights();
		REQUIRE(made);
		const auto rowAverage = [&](const HeightMap& map, unsigned y) {
			double sum = 0;
			for (unsigned x = 0; x < map.width; ++x) sum += map.values[std::size_t(y) * map.width + x];
			return sum / map.width;
		};
		const unsigned brickRow = made->height / 16, mortarRow = made->height / 8;   // middle of a brick row, a mortar line
		CHECK(rowAverage(*made, brickRow) > rowAverage(*made, mortarRow) + 0.1);    // the mortar lies deeper
		auto* depth = materials->findChild<QSlider*>("$parallaxdepth");
		REQUIRE(depth);
		CHECK(depth->isVisibleTo(materials));
		depth->setValue(500);
		CHECK(materials->value("$parallaxdepth") == "0.050");
		// Upside down: what stuck out goes in.
		const std::size_t probe = std::size_t(brickRow) * made->width + made->width / 3;
		auto* invert = materials->findChild<QCheckBox*>("heightInvert");
		REQUIRE(invert);
		invert->setChecked(true);
		const auto inverted = materials->currentHeights();
		REQUIRE(inverted);
		CHECK(inverted->values[probe] == doctest::Approx(1 - made->values[probe]));
		// Zones: one place stays flat, another is turned over again.
		std::vector<std::uint8_t> zones(std::size_t(made->width) * made->height, std::uint8_t(HeightZone::Normal));
		zones[probe] = std::uint8_t(HeightZone::Flat);
		zones[probe + 1] = std::uint8_t(HeightZone::Inverted);
		materials->setHeightZones(zones, made->width, made->height);
		const auto zoned = materials->currentHeights();
		REQUIRE(zoned);
		CHECK(zoned->values[probe] == doctest::Approx(1));
		CHECK(zoned->values[probe + 1] == doctest::Approx(made->values[probe + 1]));
		REQUIRE(materials->save());
		CHECK(materials->value("$parallaxmap") == "demo/brickwall001_relief");
		const auto saved = loadMaterial(editor.game()->files, "demo/brickwall001");
		REQUIRE(saved);
		CHECK(saved->shader == "Fidelity_Parallax");
		CHECK(saved->param("$parallaxdepth") == "0.050");
		CHECK(saved->param("$bumpmap") == "demo/brickwall001_normal");
		const auto heightTexture = loadTexture(editor.game()->files, "demo/brickwall001_relief");
		REQUIRE(heightTexture);
		CHECK(heightTexture->width == made->width);
		// Relief off again: back to LightmappedGeneric without the relief parameters.
		REQUIRE(heightsReady.wait(10000));   // the saved height map is read again
		relief->setChecked(false);
		CHECK(materials->shader() == "LightmappedGeneric");
		CHECK(materials->value("$parallaxmap").isEmpty());
		CHECK(materials->value("$parallaxdepth").isEmpty());
		CHECK_FALSE(depth->isVisibleTo(materials));
	}
	{
		// The zone painter: left paints the chosen zone, right erases; the texture repeats.
		QImage texture(64, 64, QImage::Format_RGB32);
		texture.fill(Qt::gray);
		HeightZonePainter painter(texture, 32, 32, std::vector<std::uint8_t>(32 * 32, 0));
		painter.resize(320, 320);   // the picture fills it with a 4 pixel border
		painter.show();
		REQUIRE(QTest::qWaitForWindowExposed(&painter));
		painter.setBrush(4);
		QTest::mouseClick(&painter, Qt::LeftButton, {}, QPoint(160, 160));
		CHECK(painter.zones()[16 * 32 + 16] == std::uint8_t(HeightZone::Flat));
		painter.setZone(HeightZone::Inverted);
		QTest::mouseClick(&painter, Qt::LeftButton, {}, QPoint(5, 160));
		CHECK(painter.zones()[16 * 32 + 31] == std::uint8_t(HeightZone::Inverted));   // over the edge: the other side
		QTest::mouseClick(&painter, Qt::RightButton, {}, QPoint(160, 160));
		CHECK(painter.zones()[16 * 32 + 16] == std::uint8_t(HeightZone::Normal));
	}
	{
		// Water: the fidelity switch makes it Water_Fidelity with glint and foam; walls' sections are hidden.
		auto* materials = window.materialEditor();
		QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
		materials->openMaterial("demo/water001");
		REQUIRE(previewed.wait(10000));
		auto* water = materials->findChild<QCheckBox*>("fidelityWater");
		REQUIRE(water);
		CHECK(water->isVisibleTo(materials));
		CHECK_FALSE(materials->findChild<QCheckBox*>("relief")->isVisibleTo(materials));
		CHECK_FALSE(materials->findChild<QCheckBox*>("liquid")->isVisibleTo(materials));
		auto* glint = materials->findChild<QSlider*>("$fidelityglint");
		REQUIRE(glint);
		CHECK_FALSE(glint->isVisibleTo(materials));
		water->setChecked(true);
		CHECK(materials->shader() == "Water_Fidelity");
		CHECK(glint->isVisibleTo(materials));
		glint->setValue(500);
		CHECK(materials->value("$fidelityglint") == "1.50");
		REQUIRE(materials->save());
		const auto saved = loadMaterial(editor.game()->files, "demo/water001");
		REQUIRE(saved);
		CHECK(saved->shader == "Water_Fidelity");
		CHECK(saved->param("$normalmap") == "demo/water001_normal");
		CHECK(saved->param("$fidelityglint") == "1.50");
		water->setChecked(false);
		CHECK(materials->shader() == "Water");
		CHECK(materials->value("$fidelityglint").isEmpty());
	}
	{
		// Liquid in a glass prop: Fidelity_Liquid, the fill level fixed or different in every prop.
		auto* materials = window.materialEditor();
		QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
		materials->openMaterial("models/props_demo/bottle001");
		REQUIRE(previewed.wait(10000));
		auto* liquid = materials->findChild<QCheckBox*>("liquid");
		REQUIRE(liquid);
		CHECK(liquid->isVisibleTo(materials));
		CHECK_FALSE(materials->findChild<QCheckBox*>("relief")->isVisibleTo(materials));
		liquid->setChecked(true);
		CHECK(materials->shader() == "Fidelity_Liquid");
		auto* fill = materials->findChild<QSlider*>("$fill");
		REQUIRE(fill);
		CHECK_FALSE(fill->isVisibleTo(materials));   // random by default
		auto* fillMode = materials->findChild<QComboBox*>("fillMode");
		REQUIRE(fillMode);
		fillMode->setCurrentIndex(1);
		CHECK(fill->isVisibleTo(materials));
		CHECK(materials->value("$fill") == "0.6");
		REQUIRE(materials->save());
		const auto saved = loadMaterial(editor.game()->files, "models/props_demo/bottle001");
		REQUIRE(saved);
		CHECK(saved->shader == "Fidelity_Liquid");
		CHECK(saved->param("$fill") == "0.6");
	}
	{
		// A material deploy.bat made: the editor says so; saving makes it the mod's own (marker gone).
		auto* materials = window.materialEditor();
		const QString path = materials->savePath("demo/generated001");
		QDir().mkpath(QFileInfo(path).path());
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write("// mapbase-fidelity: generated by deploy.ps1 (parallax) from demo/concretewall001\n"
		           "\"Fidelity_Parallax\"\n{\n\t\"$basetexture\" \"demo/concretewall001\"\n\t\"$parallaxdepth\" \"0.02\"\n}\n");
		file.close();
		editor.game()->files.noteWrittenFile(path.toStdString());
		materials->openMaterial("demo/generated001");
		QLabel* found = nullptr;
		for (auto* label : materials->findChildren<QLabel*>()) {
			if (label->text().contains("deploy.bat") && label->isVisibleTo(materials)) found = label;
		}
		REQUIRE(found);
		CHECK(found->text().contains("parallax"));
		CHECK(materials->findChild<QCheckBox*>("relief")->isChecked());
		REQUIRE(materials->save());
		QFile written(path);
		REQUIRE(written.open(QIODevice::ReadOnly));
		CHECK_FALSE(QString(written.readAll()).contains("mapbase-fidelity: generated"));
		// Back to the map with the side bar: its docks return.
		QAction* mapPage = nullptr;
		for (auto* a : window.findChildren<QAction*>()) {
			if (a->objectName() == "page:Map") mapPage = a;
		}
		REQUIRE(mapPage);
		CHECK(mapPage->isEnabled());   // the side bar works on every page
		mapPage->trigger();
		CHECK(window.currentPage() == MainWindow::Page::Map);
		CHECK(window.assets()->isVisible());
		window.showMaterialEditor();
		CHECK(window.pages()->currentWidget() == materials);
		window.raiseDock("Map");
		CHECK(window.assets()->isVisible());
		// Sounds and Particles: the dock's browser fills the page and goes back to its dock.
		window.showPage(MainWindow::Page::Sounds);
		CHECK(window.sounds()->isVisible());
		CHECK(window.sounds()->parentWidget() == window.pages()->currentWidget());
		CHECK_FALSE(window.assets()->isVisible());
		window.raiseDock("Sounds");
		CHECK(window.currentPage() == MainWindow::Page::Map);
		CHECK(window.sounds()->isVisible());
		CHECK(window.sounds()->parentWidget() != window.pages()->widget(3));
		// Project page, Overview tab: the game's maps, search paths and FGDs.
		window.showPage(MainWindow::Page::Project);
		auto* mounts = window.modPage()->findChild<QTreeWidget*>("modMounts");
		REQUIRE(mounts);
		CHECK(mounts->topLevelItemCount() > 0);
		auto* fgds = window.modPage()->findChild<QTreeWidget*>("modFgds");
		REQUIRE(fgds);
		CHECK(fgds->topLevelItemCount() >= 2);
		auto* maps = window.modPage()->findChild<QListWidget*>("modMaps");
		REQUIRE(maps);
		CHECK(maps->count() > 0);
		// Particles page: picking a system shows it moving and lists its parts as cards.
		window.showPage(MainWindow::Page::Particles);
		auto* particles = window.findChild<ParticleBrowser*>();
		REQUIRE(particles);
		for (int i = 0; i < 200 && particles->tree()->topLevelItemCount() == 0; ++i) QTest::qWait(50);
		REQUIRE(particles->selectSystem("demo_fire_small"));
		auto* preview = particles->preview();
		CHECK(preview->isVisible());
		for (int i = 0; i < 100 && preview->time() < 0.6; ++i) QTest::qWait(20);   // runs by itself while shown
		CHECK(preview->time() > 0.3);
		CHECK(preview->particleCount() > 0);
		QStringList titles;
		for (auto* l : particles->findChildren<QLabel*>()) {
			if (l->property("role").toString() == "cardTitle") titles << l->text();
		}
		CHECK(titles.contains("emit_continuously"));
		CHECK(titles.contains("Lifetime Random"));
		CHECK(titles.contains("render_animated_sprites"));
		CHECK(titles.contains("demo_smoke_column"));   // its child
		// Pause stops it; hidden it does not run.
		particles->findChild<QPushButton*>("particlePlay")->click();
		const double paused = preview->time();
		QTest::qWait(150);
		CHECK(preview->time() == doctest::Approx(paused));
		particles->findChild<QPushButton*>("particlePlay")->click();
		// Sparks: a burst, drawn as streaks.
		REQUIRE(particles->selectSystem("demo_sparks"));
		preview->advance(0.1);
		CHECK(preview->particleCount() == 60);
		window.showPage(MainWindow::Page::Map);
		CHECK(window.assets()->isVisible());
		// Back in its dock while mapping; with the dock closed it is not seen and does not run.
		QDockWidget* particlesDock = nullptr;
		for (QWidget* w = particles; w; w = w->parentWidget()) {
			if (auto* dock = qobject_cast<QDockWidget*>(w)) particlesDock = dock;
		}
		REQUIRE(particlesDock);
		particlesDock->hide();
		QTest::qWait(50);
		CHECK_FALSE(preview->isVisible());
		const double hidden = preview->time();
		QTest::qWait(150);
		CHECK(preview->time() == doctest::Approx(hidden));
		particlesDock->show();
	}
	{
		// View filters: hiding a kind of object (lights here) hides every one of them, the view's
		// button says so, and showing them again brings them back.
		int lightId = 0;
		bool placed = false;
		for (const auto& entity : editor.doc().entities) {
			if (entity.classname().starts_with("light")) lightId = entity.id;
		}
		if (lightId == 0) {
			auto [change, id] = sme::edit::placeEntity(editor.doc(), "light", {0, 0, 64}, nullptr);
			editor.commit(std::move(change));
			lightId = id;
			placed = true;
		}
		CHECK_FALSE(editor.hidden().containsEntity(lightId));
		auto* button = view->findChild<QToolButton*>("viewMenu");
		REQUIRE(button);
		emit button->menu()->aboutToShow();
		QAction* lights = nullptr;
		for (auto* action : button->menu()->actions()) {
			if (action->text() == "Lights") lights = action;
		}
		REQUIRE(lights);
		CHECK(lights->isChecked());
		lights->trigger();
		CHECK_FALSE(editor.kindVisible(ObjectKind::Lights));
		CHECK(editor.hidden().containsEntity(lightId));
		CHECK(button->text().contains("hidden"));
		editor.setKindVisible(ObjectKind::Lights, true);
		CHECK_FALSE(editor.hidden().containsEntity(lightId));
		CHECK_FALSE(button->text().contains("hidden"));
		if (placed) editor.undo();
	}
	{
		// Map Look: a slider sets the value in the map (logic_auto -> point_clientcommand, one undo
		// step per drag), the reset button takes it out again; the preview switch is the editor's.
		auto* panel = window.findChild<MapLookPanel*>();
		REQUIRE(panel);
		const std::size_t entitiesBefore = editor.doc().entities.size();
		auto* clouds = panel->findChild<QSlider*>("r_fidelity_sky_clouds");
		REQUIRE(clouds);
		clouds->setValue(300);   // 0.3
		CHECK(sme::mapLookValues(editor.doc()).at("r_fidelity_sky_clouds") == "0.3");
		CHECK(editor.lookValue("r_fidelity_sky_clouds") == doctest::Approx(0.3));
		CHECK(editor.doc().entities.size() == entitiesBefore + 2);
		panel->setValue("r_fidelity_temperature", 0.4);
		CHECK(sme::mapLookValues(editor.doc()).size() == 2);
		panel->resetValue("r_fidelity_temperature");
		CHECK(sme::mapLookValues(editor.doc()).size() == 1);
		auto* preview = panel->findChild<QCheckBox*>("lookPreview");
		REQUIRE(preview);
		const bool wasOn = editor.fidelityLook();
		preview->setChecked(!wasOn);
		CHECK(editor.fidelityLook() == !wasOn);
		editor.setFidelityLook(wasOn);
		while (editor.doc().entities.size() > entitiesBefore) editor.undo();
		CHECK(sme::mapLookValues(editor.doc()).empty());
	}
	{
		// The corner menu of a view switches the look (and shows it on the button).
		auto* button = view->findChild<QToolButton*>("viewMenu");
		REQUIRE(button);
		CHECK(button->text().startsWith("3D"));
		auto* menu = button->menu();
		emit menu->aboutToShow();
		QAction* wire = nullptr;
		for (auto* action : menu->actions()) {
			if (action->text().startsWith("Wireframe")) wire = action;
		}
		REQUIRE(wire);
		wire->trigger();
		CHECK(editor.renderMode() == Editor::RenderMode::Wireframe);
		CHECK(button->text().contains("Wireframe"));
		editor.setRenderMode(Editor::RenderMode::Lit);
		CHECK(button->text().contains("Lighting preview"));
	}
	{
		// Entity tool with a brush class armed: the dragged box becomes that entity (trigger texture).
		const std::size_t before = editor.doc().entities.size();
		editor.setCurrentEntityClass("trigger_once");
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-320, -320, 0}));
		QTest::mouseMove(view, screen(view, {-192, -224, 0}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {-192, -224, 0}));
		QTest::mouseMove(view, screen(view, {-256, -272, 64}));
		settle();
		QTest::keyClick(view, Qt::Key_Return);
		settle();
		REQUIRE(editor.doc().entities.size() == before + 1);
		const auto& trigger = editor.doc().entities.back();
		CHECK(trigger.classname() == "trigger_once");
		REQUIRE(trigger.solids.size() == 1);
		CHECK(trigger.solids.front().sides.front().material == "TOOLS/TOOLSTRIGGER");
		CHECK(editor.selection().entities == std::vector<int>{trigger.id});
		editor.undo();
		CHECK(editor.doc().entities.size() == before);

		// Random yaw for point entities placed in 3D.
		editor.setRandomYaw(true);
		editor.setCurrentEntityClass("light_spot");
		QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {-256, -256, 0}));
		settle();
		REQUIRE(editor.doc().entities.size() == before + 1);
		Vec3 angles{-1, -1, -1};
		CHECK(parseVec3(editor.doc().entities.back().get("angles"), angles));
		CHECK(angles.y >= 0);
		CHECK(angles.y < 360);
		editor.undo();
		editor.setRandomYaw(false);

		// Nothing armed: a click places nothing.
		editor.setCurrentEntityClass({});
		QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {-256, -256, 0}));
		settle();
		CHECK(editor.doc().entities.size() == before);
		editor.setCurrentEntityClass("info_player_start");
	}

	{
		// Model browser: every model gets a preview picture.
		if (window.assets()->modelView()->model()->rowCount() == 0) {
			CHECK(indexed.wait(10000));
		}
		window.assets()->showModels();
		window.raiseDock("Asset Browser");
		auto* models = window.assets()->modelView()->model();
		CHECK(models->rowCount() >= 3);
		const auto crate = models->match(models->index(0, 0), Qt::UserRole, "models/props_demo/crate01.mdl", 1, Qt::MatchExactly);
		REQUIRE(crate.size() == 1);
		(void)crate.front().data(Qt::DecorationRole);   // asks for the preview
		QPixmap preview;
		for (int i = 0; i < 100; ++i) {
			settle();
			preview = crate.front().data(Qt::DecorationRole).value<QPixmap>();
			const QImage image = preview.toImage();
			if (!image.isNull() && image.pixelColor(image.width() / 2, image.height() / 2).alpha() == 255 &&
			    image.pixelColor(2, 2).alpha() == 0) {
				break;   // drawn: solid in the middle, transparent in the corner
			}
		}
		const QImage image = preview.toImage();
		CHECK(image.pixelColor(image.width() / 2, image.height() / 2).alpha() == 255);
		CHECK(image.pixelColor(2, 2).alpha() == 0);
		window.assets()->grab().save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/models_tab.png");
		window.assets()->showMaterials();
	}

	// Choosing a tool leaves face mode.
	editor.setSelectMode(Editor::SelectMode::Face);
	editor.setTool(Editor::Tool::Move);
	CHECK(editor.selectMode() == Editor::SelectMode::Object);
	editor.setSelectMode(Editor::SelectMode::Face);
	CHECK(editor.tool() == Editor::Tool::Select);
	editor.setSelectMode(Editor::SelectMode::Object);
	editor.clearSelection();

	{
		// Sounds page and dock: sound scripts and soundscapes; dropping them places the matching entity.
		auto* sounds = window.sounds();
		if (sounds->tree()->topLevelItemCount() == 0) {
			QSignalSpy soundsIndexed(sounds, &SoundBrowser::indexed);
			CHECK(soundsIndexed.wait(10000));
		}
		CHECK_FALSE(sounds->tree()->findItems("demo.outside", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
		CHECK_FALSE(sounds->tree()->findItems("Demo.Wind", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
		{
			// A click shows the sound: decoded with its loop point (playing needs an audio device).
			const auto wind = sounds->tree()->findItems("wind1.wav", Qt::MatchExactly | Qt::MatchRecursive);
			REQUIRE_FALSE(wind.isEmpty());
			QSignalSpy loaded(sounds, &SoundBrowser::audioLoaded);
			sounds->tree()->setCurrentItem(wind.front());
			REQUIRE(loaded.wait(10000));
			const auto audio = sounds->currentAudio();
			REQUIRE(audio);
			CHECK(audio->sampleRate == 22050);
			CHECK(audio->seconds() == doctest::Approx(2.0));
			REQUIRE(audio->loopStart);
			CHECK(*audio->loopStart == 0);

			// Sound script editor: a new entry goes into the mod's own script and its manifest.
			auto* name = sounds->findChild<QLineEdit*>("soundEntryName");
			REQUIRE(name);
			name->setText("MyMod.Wind");
			// Only the sound script entry's buttons (the soundscape editor has its own "+" and "Save").
			const auto entryButtons = [&] {
				QList<QPushButton*> out;
				for (auto* button : sounds->findChildren<QPushButton*>()) {
					if (!sounds->soundscapeEditor()->isAncestorOf(button) && button->objectName() != "newSoundscape") out << button;
				}
				return out;
			};
			for (auto* button : entryButtons()) {
				if (button->text().startsWith("+")) button->click();
			}
			QSignalSpy reindexed(sounds, &SoundBrowser::indexed);
			for (auto* button : entryButtons()) {
				if (button->text() == "Save to the mod") button->click();
			}
			REQUIRE(reindexed.wait(10000));
			CHECK_FALSE(sounds->tree()->findItems("MyMod.Wind", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
			QFile manifest(fromPath(f.mod / "scripts/game_sounds_manifest.txt"));
			REQUIRE(manifest.open(QIODevice::ReadOnly));
			const QString text = manifest.readAll();
			CHECK(text.contains("scripts/game_sounds_mymod.txt"));
			CHECK(text.contains("scripts/game_sounds_demo.txt"));
		}
		{
			// Soundscape editor: clicking a soundscape shows its rules as cards; changes, a new rule and a
			// new soundscape are saved into the mod's own soundscape file and its manifest.
			auto* editor = sounds->soundscapeEditor();
			const auto outside = sounds->tree()->findItems("demo.outside", Qt::MatchExactly | Qt::MatchRecursive);
			REQUIRE_FALSE(outside.isEmpty());
			sounds->tree()->setCurrentItem(outside.front());
			CHECK(editor->isVisibleTo(sounds));
			CHECK(editor->soundscape().name == "demo.outside");
			REQUIRE(editor->soundscape().rules.size() == 2);
			CHECK(editor->soundscape().rules[0].wave == "ambient/wind1.wav");
			CHECK(editor->soundscape().rules[1].time == "5,10");
			CHECK(editor->findChildren<QFrame*>("soundscapeRule").size() == 2);
			// A sound file of the random rule plays in the browser's player.
			QSignalSpy heard(sounds, &SoundBrowser::audioLoaded);
			editor->findChildren<QToolButton*>("ruleListen").at(1)->click();
			REQUIRE(heard.wait(10000));
			CHECK(sounds->currentAudio());
			// Room effect: concrete; one more looping sound; save.
			auto* dsp = editor->findChild<QComboBox*>("soundscapeDsp");
			REQUIRE(dsp);
			dsp->setCurrentIndex(dsp->findData("17"));
			editor->findChild<QPushButton*>("addLooping")->click();
			REQUIRE(editor->soundscape().rules.size() == 3);
			auto waves = editor->findChildren<QLineEdit*>("ruleWave");
			REQUIRE(waves.size() == 2);
			waves.back()->setText("ambient/alarm1.wav");
			QSignalSpy scapesIndexed(sounds, &SoundBrowser::indexed);
			editor->findChild<QPushButton*>("saveSoundscape")->click();
			REQUIRE(scapesIndexed.wait(10000));
			QFile scapeFile(fromPath(f.mod / "scripts/soundscapes_mymod.txt"));
			REQUIRE(scapeFile.open(QIODevice::ReadOnly));
			const auto saved = parseSoundscapes(scapeFile.readAll().toStdString());
			REQUIRE(saved.size() == 1);
			CHECK(saved[0].name == "demo.outside");
			CHECK(saved[0].dsp == "17");
			REQUIRE(saved[0].rules.size() == 3);
			CHECK(saved[0].rules[2].wave == "ambient/alarm1.wav");
			QFile scapeManifest(fromPath(f.mod / "scripts/soundscapes_manifest.txt"));
			REQUIRE(scapeManifest.open(QIODevice::ReadOnly));
			const QString manifestText = scapeManifest.readAll();
			CHECK(manifestText.indexOf("soundscapes_mymod.txt") < manifestText.indexOf("soundscapes_demo.txt"));   // the mod's copy first
			// Selected again from the mod's file, now deletable there.
			CHECK(editor->soundscape().rules.size() == 3);
			CHECK(editor->findChild<QPushButton*>("deleteSoundscape")->isEnabled());

			// A new soundscape with a random rule and another soundscape inside.
			sounds->findChild<QPushButton*>("newSoundscape")->click();
			editor->findChild<QLineEdit*>("soundscapeName")->setText("mymod.cave");
			editor->findChild<QPushButton*>("addRandom")->click();
			editor->findChild<QPushButton*>("addSubSoundscape")->click();
			auto* inside = editor->findChild<QComboBox*>("ruleSoundscape");
			REQUIRE(inside);
			CHECK(inside->findText("demo.outside") >= 0);
			inside->setCurrentText("demo.outside");
			QSignalSpy again(sounds, &SoundBrowser::indexed);
			editor->findChild<QPushButton*>("saveSoundscape")->click();
			REQUIRE(again.wait(10000));
			CHECK_FALSE(sounds->tree()->findItems("mymod.cave", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
			CHECK(editor->soundscape().name == "mymod.cave");
			REQUIRE(editor->soundscape().rules.size() == 2);
			CHECK(editor->soundscape().rules[1].name == "demo.outside");
			// Removed again: only from the mod's file.
			QSignalSpy removed(sounds, &SoundBrowser::indexed);
			editor->findChild<QPushButton*>("deleteSoundscape")->click();
			REQUIRE(removed.wait(10000));
			CHECK(sounds->tree()->findItems("mymod.cave", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
			CHECK_FALSE(sounds->tree()->findItems("demo.outside", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
		}
		{
			// Categories: the sounds sorted by what they are, scripts beside the files of their folder.
			window.showPage(MainWindow::Page::Sounds);
			settle();
			QStringList categoryTexts;
			for (int i = 0; i < sounds->categories()->count(); ++i) categoryTexts << sounds->categories()->item(i)->text();
			CHECK(categoryTexts.front().startsWith("All sounds"));
			REQUIRE(categoryTexts.filter("Weapons").size() == 1);
			CHECK(categoryTexts.filter("Weapons").front().endsWith(" 2"));   // Weapon_Pistol.Single and its file
			CHECK(categoryTexts.filter("Doors and buttons").front().endsWith(" 8"));   // 4 door scripts and their files
			CHECK(categoryTexts.filter("Voice and dialogue").front().endsWith(" 2"));   // NPC_Citizen.Hello speaks
			CHECK(categoryTexts.back().startsWith("Soundscapes"));
			QSignalSpy loaded(sounds, &SoundBrowser::audioLoaded);
			auto* pistol = sounds->reveal("pistol_fire2.wav", SoundBrowser::FileSound);
			REQUIRE(pistol);
			CHECK(sounds->categories()->currentItem()->text().startsWith("Weapons"));
			CHECK_FALSE(pistol->isHidden());
			CHECK(pistol->parent()->text(0) == "weapons/pistol");
			CHECK(pistol->parent()->child(0)->text(0) == "Weapon_Pistol.Single");
			// Searching finds things in other categories too.
			sounds->findChild<QLineEdit*>("soundSearch")->setText("squeak");
			QTest::qWait(400);
			const auto squeak = sounds->tree()->findItems("Doors.Squeak", Qt::MatchExactly | Qt::MatchRecursive);
			REQUIRE(squeak.size() == 1);
			CHECK_FALSE(squeak.front()->isHidden());
			CHECK(pistol->isHidden());
			sounds->findChild<QLineEdit*>("soundSearch")->clear();
			QTest::qWait(400);
			REQUIRE((sounds->currentAudio() || loaded.wait(10000)));
			auto audio = sounds->currentAudio();
			REQUIRE(audio);
			const std::size_t frames = audio->frames();
			CHECK(frames == 22050 * 4 / 10);

			// Edit: select the middle half with the mouse, trim to it, fade out, undo and redo.
			auto* wave = sounds->waveform();
			REQUIRE(wave->isVisible());
			const int w = wave->width(), h = wave->height();
			QTest::mousePress(wave, Qt::LeftButton, {}, QPoint(w / 4, h / 2));
			QTest::mouseMove(wave, QPoint(w / 2, h / 2));
			QTest::mouseMove(wave, QPoint(w * 3 / 4, h / 2));
			QTest::mouseRelease(wave, Qt::LeftButton, {}, QPoint(w * 3 / 4, h / 2));
			const auto selected = wave->selection();
			CHECK(double(selected.size()) == doctest::Approx(frames / 2.0).epsilon(0.03));
			const auto click = [&](const char* name) {
				auto* button = sounds->findChild<QToolButton*>(name);
				REQUIRE(button);
				REQUIRE(button->isEnabled());
				button->click();
			};
			click("soundTrim");
			REQUIRE(sounds->currentAudio()->frames() == selected.size());
			CHECK(sounds->edited());
			CHECK(sounds->currentAudio()->samples.front() == audio->samples[selected.begin]);
			click("soundFadeOut");   // no selection after the length changed: the last tenth
			CHECK(sounds->currentAudio()->samples.back() == 0);
			click("soundUndo");
			click("soundUndo");
			CHECK(sounds->currentAudio()->frames() == frames);
			CHECK_FALSE(sounds->edited());
			click("soundRedo");
			click("soundRedo");
			CHECK(sounds->currentAudio()->samples.back() == 0);
			const auto editedFrames = sounds->currentAudio()->frames();

			// Save as a WAV into the mod, with a sound script entry for it.
			sounds->findChild<QLineEdit*>("soundSaveName")->setText("mymod/pistol_short.wav");
			sounds->findChild<QLineEdit*>("soundEntryName")->setText("MyMod.PistolShort");
			sounds->findChild<QCheckBox*>("soundSaveScript")->setChecked(true);
			QSignalSpy savedIndex(sounds, &SoundBrowser::indexed);
			sounds->findChild<QPushButton*>("soundSaveWav")->click();
			REQUIRE(savedIndex.wait(10000));
			QFile savedFile(fromPath(f.mod / "sound/mymod/pistol_short.wav"));
			REQUIRE(savedFile.open(QIODevice::ReadOnly));
			const QByteArray bytes = savedFile.readAll();
			const auto back = decodeAudio(std::span(reinterpret_cast<const std::byte*>(bytes.constData()), std::size_t(bytes.size())));
			REQUIRE(back);
			CHECK(back->frames() == editedFrames);
			CHECK(back->sampleRate == 22050);
			CHECK(back->format == "PCM 16 bit");
			QFile script(fromPath(f.mod / "scripts/game_sounds_mymod.txt"));
			REQUIRE(script.open(QIODevice::ReadOnly));
			const QString scriptText = script.readAll();
			CHECK(scriptText.contains("MyMod.PistolShort"));
			CHECK(scriptText.contains("mymod/pistol_short.wav"));
			CHECK_FALSE(sounds->tree()->findItems("pistol_short.wav", Qt::MatchExactly | Qt::MatchRecursive).isEmpty());
			CHECK_FALSE(sounds->edited());
			window.grab().save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/sounds_page.png");
			window.showPage(MainWindow::Page::Map);
			settle();
		}
		const auto drop = [&](const char* format, const QByteArray& data) {
			QMimeData mime;
			mime.setData(format, data);
			const QPoint at = screen(view, {-200, -200, 0});
			QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
			QApplication::sendEvent(view, &enter);
			QDropEvent event(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, {});
			QApplication::sendEvent(view, &event);
			settle();
			return editor.doc().entities.back();
		};
		CHECK(drop("application/x-sme-sound", "3:demo.outside").get("soundscape") == "demo.outside");
		CHECK(drop("application/x-sme-sound", "1:Demo.Wind").get("message") == "Demo.Wind");
		const auto prop = drop("application/x-sme-model", "models/humans/group01/male_07.mdl");
		CHECK(prop.classname() == "prop_dynamic");   // no $staticprop, no collision model
		CHECK(prop.get("model") == "models/humans/group01/male_07.mdl");
		const auto particle = drop("application/x-sme-particle", "demo_sparks");
		CHECK(particle.classname() == "info_particle_system");
		CHECK(particle.get("effect_name") == "demo_sparks");
		CHECK(editor.doc().entities.size() == 5);
		CHECK(window.assets()->particleNames().contains("demo_fire_small"));
		editor.undo();
		editor.undo();
		editor.undo();
		editor.undo();
		CHECK(editor.doc().entities.size() == 1);
	}
	{
		// A model dragged in follows the mouse as a see-through preview resting on the surface under
		// it, centred on its box: map pieces have their origin far away from their geometry.
		test::writeStudioModel(f.mod / "models/props_demo/far_piece.mdl",
		                       test::makeStudioModel({{936, 1960, 40, 1064, 2040, 120}}, {"idle"}, "", "metal001", "demo/"));
		for (const char* ext : {".mdl", ".vvd", ".dx90.vtx"}) {
			editor.game()->files.noteWrittenFile(f.mod / (std::string("models/props_demo/far_piece") + ext));
		}
		const QString farPiece = "models/props_demo/far_piece.mdl";
		QSignalSpy ready(editor.models(), &ModelCache::modelReady);
		for (int i = 0; i < 100 && !editor.models()->get(farPiece); ++i) {
			ready.wait(100);
		}
		INFO(editor.models()->error(farPiece).toStdString());
		REQUIRE(editor.models()->get(farPiece));
		camera.position = QVector3D(0, -420, 330);
		camera.yaw = 90;
		camera.pitch = -32;
		view->update();
		settle();
		const Bounds top = boundsOf(editor, block);
		const std::size_t before = editor.doc().entities.size();
		QMimeData mime;
		mime.setData("application/x-sme-model", farPiece.toUtf8());
		const Vec3 middle{top.center().x + 16, top.min.y + 24, top.max.z};   // on the top, clear of the player start
		const QPoint at = screen(view, middle);
		QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
		QApplication::sendEvent(view, &enter);
		const QPoint over = screen(view, middle + Vec3{8, 8, 0});
		QDragMoveEvent move(over, Qt::CopyAction, &mime, Qt::LeftButton, {});
		QApplication::sendEvent(view, &move);
		settle();
		REQUIRE(view->ghostBox());
		CHECK(view->ghostModel() == farPiece);
		const Bounds ghost = *view->ghostBox();
		CHECK(ghost.min.z == doctest::Approx(top.max.z));          // standing on the block
		CHECK(ghost.size().x == doctest::Approx(128));             // the model's own size, not a default box
		CHECK(std::abs(ghost.center().x - middle.x - 8) <= 8.5);   // under the cursor (origin on the grid)
		CHECK(std::abs(ghost.center().y - middle.y - 8) <= 8.5);
		view->grabFramebuffer().save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/model_drag.png");
		// The card that follows the mouse while dragging from the browser.
		QPixmap thumb(96, 96);
		thumb.fill(QColor(120, 90, 60));
		const QPixmap card = assetDragCard(thumb, "far_piece", farPiece, 1.0);
		CHECK(card.size() == QSize(260, 72));
		CHECK(card.toImage().pixelColor(0, 0).alpha() == 0);       // rounded corner
		CHECK(card.toImage().pixelColor(36, 36) == QColor(120, 90, 60));
		card.save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/model_drag_card.png");
		QDropEvent dropped(QPointF(over), Qt::CopyAction, &mime, Qt::LeftButton, {});
		QApplication::sendEvent(view, &dropped);
		settle();
		CHECK_FALSE(view->ghostBox());
		REQUIRE(editor.doc().entities.size() == before + 1);
		const auto& placed = editor.doc().entities.back();
		CHECK(placed.classname() == "prop_dynamic");   // the class the model is made for (no $staticprop)
		CHECK(placed.get("model") == farPiece.toStdString());
		// Exactly where the preview stood.
		CHECK(placed.origin().x == doctest::Approx(ghost.min.x - 936));
		CHECK(placed.origin().y == doctest::Approx(ghost.min.y - 1960));
		CHECK(placed.origin().z == doctest::Approx(top.max.z - 40));
		REQUIRE(editor.selection().entities.size() == 1);
		CHECK(editor.selection().entities.front() == placed.id);
		// Leaving the view without dropping takes the preview away.
		QDragEnterEvent again(at, Qt::CopyAction, &mime, Qt::LeftButton, {});
		QApplication::sendEvent(view, &again);
		CHECK(view->ghostBox());
		QDragLeaveEvent leave;
		QApplication::sendEvent(view, &leave);
		CHECK_FALSE(view->ghostBox());
		editor.undo();
		CHECK(editor.doc().entities.size() == before);
	}

	// Rotation follows the rotation snap: the entity turns by 45°, the brush stays on the grid.
	editor.setRotationSnap(45);
	sme::Selection entitySelection;
	entitySelection.entities.push_back(editor.doc().entities.front().id);
	editor.setSelection(entitySelection);
	editor.rotateSelection(2, editor.rotationSnap());
	CHECK(editor.doc().entities.front().get("angles") == "0 45 0");
	sme::Selection brushSelection;
	brushSelection.solids.push_back(block);
	editor.setSelection(brushSelection);
	editor.rotateSelection(2, 90);
	const auto rotated = boundsOf(editor, block);
	CHECK(std::fmod(std::abs(rotated.min.x), 8.0) == doctest::Approx(0));
	CHECK(std::fmod(std::abs(rotated.min.y), 8.0) == doctest::Approx(0));
	editor.undo();
	editor.undo();
	b = boundsOf(editor, block);

	// Face mode: click the top face, apply a material.
	editor.setTool(Editor::Tool::Select);
	editor.clearSelection();
	editor.setSelectMode(Editor::SelectMode::Face);
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {16, -40, b.max.z}));
	settle();
	REQUIRE(editor.selection().faces.size() == 1);
	editor.applyMaterialToSelection("BRICK/BRICKWAL001");   // misspelled on purpose
	const auto* solid = editor.doc().findSolid(block);
	int typos = 0;
	for (const auto& side : solid->sides) {
		typos += side.material == "BRICK/BRICKWAL001" ? 1 : 0;
	}
	CHECK(typos == 1);

	// Problems panel: the autofix button corrects the typo.
	if (indexed.isEmpty()) {
		REQUIRE(indexed.wait(20000));
	}
	window.problems()->runChecks();
	settle();
	QPushButton* fix = nullptr;
	for (auto* button : window.problems()->findChildren<QPushButton*>()) {
		if (button->text().startsWith("Use DEMO/BRICKWALL001")) {
			fix = button;
		}
	}
	REQUIRE(fix);
	QTest::mouseClick(fix, Qt::LeftButton);
	settle();
	for (const auto& side : editor.doc().findSolid(block)->sides) {
		CHECK(side.material != "BRICK/BRICKWAL001");
	}

	// Paint: Ctrl + right mouse over the top face and the south face, one undo step.
	editor.setCurrentMaterial("DEMO/WOOD001");
	QTest::mousePress(view, Qt::RightButton, Qt::ControlModifier, screen(view, {64, 0, b.max.z}));
	QTest::mouseMove(view, screen(view, {64, -64, 32}));
	QTest::mouseRelease(view, Qt::RightButton, Qt::ControlModifier, screen(view, {64, -64, 32}));
	settle();
	int wood = 0;
	for (const auto& side : editor.doc().findSolid(block)->sides) {
		wood += side.material == "DEMO/WOOD001" ? 1 : 0;
	}
	CHECK(wood == 2);
	editor.undo();
	wood = 0;
	for (const auto& side : editor.doc().findSolid(block)->sides) {
		wood += side.material == "DEMO/WOOD001" ? 1 : 0;
	}
	CHECK(wood == 0);

	// Double-click in face mode selects the whole wall.
	editor.setSelectMode(Editor::SelectMode::Face);
	QTest::mouseDClick(view, Qt::LeftButton, {}, screen(view, {64, -64, 32}));
	settle();
	CHECK(editor.selection().faces.size() == 1);

	// 2D top view: drag a block.
	editor.setSelectMode(Editor::SelectMode::Object);
	window.setQuadLayout(true);
	settle();
	Viewport* top = window.view(1);
	top->camera().position = QVector3D(0, 0, 0);
	top->camera().zoom = 1;
	top->update();
	settle();
	editor.setTool(Editor::Tool::Block);
	QTest::mousePress(top, Qt::LeftButton, {}, screen(top, {-256, -256, 0}));
	QTest::mouseMove(top, screen(top, {-192, -160, 0}));
	QTest::mouseRelease(top, Qt::LeftButton, {}, screen(top, {-192, -160, 0}));
	settle();
	REQUIRE(editor.doc().world.solids.size() == 2);
	const auto b2 = buildBrushGeometry(editor.doc().world.solids.back()).bounds;
	CHECK(b2.size().x == doctest::Approx(64));
	CHECK(b2.size().y == doctest::Approx(96));

#ifndef _WIN32
	{
		// Compile window: steps, progress, warnings with explanations (tools faked by shell scripts).
		const auto script = [&](const QString& name, const QString& body) {
			const QString path = home.filePath(name);
			QFile file(path);
			REQUIRE(file.open(QIODevice::WriteOnly));
			file.write(("#!/bin/sh\n" + body + "\n").toUtf8());
			file.close();
			file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
			return path;
		};
		GameSettings tools;
		tools.vbsp = script("vbsp", "printf 'ProcessBlock_Thread: 0...1...2...3...4...5...6...7...8...9...10\\n'\n"
		                            "echo 'Warning: Error loading studio model \"models/missing.mdl\"!'\n"
		                            "for last; do :; done; echo BSP > \"$last.bsp\"");
		tools.vvis = script("vvis", "printf 'PortalFlow: 0...5...10\\n'");
		tools.vrad = script("vrad", "printf 'BuildFacelights: 0...1...2'; sleep 0.2; printf '...10\\n'");
		const QString vmf = home.filePath("compiled.vmf");
		QString saveError;
		REQUIRE(editor.saveMap(vmf, &saveError));
		CompileOptions options;
		options.run = false;
		QSignalSpy done(&window.compiler(), &CompileRunner::finished);
		window.compiler().start(vmf, home.filePath("game"), tools, options);
		REQUIRE(done.wait(20000));
		CHECK(done.front().front().toBool());
		auto* compile = window.compileWindow();
		REQUIRE(compile->stepCount() == 4);
		for (int i = 0; i < 4; ++i) {
			CHECK(compile->stepState(i) == CompileWindow::StepState::Done);
		}
		CHECK(compile->warningCount() == 1);
		CHECK(compile->errorCount() == 0);
		CHECK(compile->summary().contains("Finished"));
		CHECK(QFileInfo::exists(home.filePath("game/maps/compiled.bsp")));
		window.raiseDock("Compile");
		settle();
		compile->grab().save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/compile.png");

		// A missing tool stops the compile with an error; later steps are skipped.
		tools.vvis = home.filePath("missing-vvis");
		QSignalSpy failed(&window.compiler(), &CompileRunner::finished);
		window.compiler().start(vmf, home.filePath("game"), tools, options);
		REQUIRE(failed.wait(20000));
		CHECK_FALSE(failed.front().front().toBool());
		CHECK(compile->stepState(0) == CompileWindow::StepState::Done);
		CHECK(compile->stepState(1) == CompileWindow::StepState::Failed);
		CHECK(compile->stepState(2) == CompileWindow::StepState::Skipped);
		CHECK(compile->errorCount() == 1);
	}
#endif

	{
		// Compile dialog: simple cards and advanced parameters change the same settings; the
		// command lines below follow. Defaults: console, windowed, 1920x1080.
		GameSettings tools;
		tools.vbsp = "vbsp.exe";
		tools.vvis = "vvis.exe";
		tools.vrad = "vrad.exe";
		tools.gameExe = "hl2.exe";
		QSettings().remove("compile/test_game");
		CompileDialog dialog(home.filePath("arena.vmf"), home.filePath("game"), tools, &window, "test_game");
		dialog.setAdvanced(false);
		dialog.show();
		settle();
		CHECK(dialog.commandText().contains("-console -windowed -w 1920 -h 1080 -novid"));
		CHECK(dialog.findChild<QComboBox*>("preset")->currentText().startsWith("Normal"));
		auto* detail = dialog.findChild<QCheckBox*>("detail");
		REQUIRE(detail);
		detail->click();
		CHECK(dialog.commandText().contains("-nodetail"));
		CHECK_FALSE(dialog.options().detail);
		// Preset Fast: VVIS -fast and quick light.
		auto* preset = dialog.findChild<QComboBox*>("preset");
		preset->setCurrentIndex(0);
		emit preset->activated(0);
		CHECK(dialog.commandText().contains("-fast"));
		CHECK(dialog.commandText().contains("-bounce 2 -noextra"));
		// Advanced: VRAD gets -StaticPropLighting from the list; switching VVIS off leaves it out.
		dialog.findChild<QPushButton*>("advancedView")->click();
		auto* processes = dialog.findChild<QListWidget*>("processes");
		REQUIRE(processes);
		processes->setCurrentRow(2);
		auto* parameters = dialog.findChild<QTreeWidget*>("parameters");
		REQUIRE(parameters);
		auto* search = dialog.findChild<QLineEdit*>("parameterSearch");
		search->setText("StaticPropLighting");
		REQUIRE(parameters->topLevelItemCount() == 1);
		emit parameters->itemDoubleClicked(parameters->topLevelItem(0), 0);
		CHECK(dialog.commandText().contains("-StaticPropLighting"));
		processes->item(1)->setCheckState(Qt::Unchecked);
		CHECK_FALSE(dialog.options().vvis);
		CHECK_FALSE(dialog.commandText().contains("vvis.exe"));
		CHECK_FALSE(dialog.findChild<QCheckBox*>("vvis")->isChecked());   // the simple view follows
		if (qEnvironmentVariableIsSet("SME_COMPILE_SHOTS")) {
			// Pictures with the editor's own look.
			const QString sheet = qApp->styleSheet();
			const QPalette palette = qApp->palette();
			const QString styleName = qApp->style()->name();
			applyDarkStyle(*qApp);
			dialog.resize(1100, 720);
			search->clear();
			settle();
			dialog.grab().save(qEnvironmentVariable("SME_COMPILE_SHOTS") + "/advanced.png");
			dialog.findChild<QPushButton*>("simpleView")->click();
			settle();
			dialog.grab().save(qEnvironmentVariable("SME_COMPILE_SHOTS") + "/simple.png");
			qApp->setStyleSheet(sheet);
			qApp->setPalette(palette);
			qApp->setStyle(styleName);
		}
		// Compile saves the settings for the game; the next dialog starts with them.
		dialog.accept();
		emit dialog.findChild<QDialogButtonBox*>()->accepted();
		CompileDialog again(home.filePath("arena.vmf"), home.filePath("game"), tools, &window, "test_game");
		CHECK_FALSE(again.options().detail);
		CHECK_FALSE(again.options().vvis);
		CHECK(again.commandText().contains("-StaticPropLighting"));
		QSettings().remove("compile/test_game");
	}

	{
		// Shapes: an arch from the block tool, changed afterwards in the Tool panel (live, one undo step).
		const auto before = editor.doc().world.solids.size();
		ShapeOptions arch;
		arch.kind = ShapeOptions::Kind::Arch;
		arch.sides = 4;
		arch.arc = 180;
		arch.wallWidth = 32;
		editor.clearSelection();
		editor.setShapeOptions(arch);   // nothing selected: changes no shape that is already there
		CHECK(editor.doc().world.solids.size() == before);
		editor.setTool(Editor::Tool::Block);
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-256, 128, 0}));
		QTest::mouseMove(view, screen(view, {-64, 320, 0}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {-64, 320, 0}));
		QTest::mouseMove(view, screen(view, {-160, 224, 64}));
		settle();
		QTest::mousePress(view, Qt::LeftButton, {}, screen(view, {-160, 224, 64}));
		QTest::mouseRelease(view, Qt::LeftButton, {}, screen(view, {-160, 224, 64}));
		settle();
		CHECK(editor.doc().world.solids.size() == before + 4);
		CHECK(editor.selection().solids.size() == 4);
		CHECK(editor.shapeEditable());
		arch.sides = 8;
		editor.setShapeOptions(arch);
		CHECK(editor.doc().world.solids.size() == before + 8);
		editor.undo();   // one step removes the whole (changed) arch
		CHECK(editor.doc().world.solids.size() == before);
		editor.setShapeOptions(ShapeOptions{});
		CHECK(editor.doc().world.solids.size() == before);

		// Brush tools: hollow a block into a room, merge two blocks, then undo both.
		editor.createShape(box({512, 512, 0}, {640, 640, 128}));
		const auto room = editor.selection();
		REQUIRE(room.solids.size() == 1);
		editor.hollowSelection(16);
		CHECK(editor.selection().solids.size() == 6);
		editor.undo();
		CHECK(editor.doc().findSolid(room.solids.front()));
		editor.createShape(box({640, 512, 0}, {704, 640, 128}));
		Selection pair = room;
		pair.solids.push_back(editor.selection().solids.front());
		editor.setSelection(pair);
		editor.mergeSelection();
		REQUIRE(editor.selection().solids.size() == 1);
		CHECK(boundsOf(editor, editor.selection().solids.front()).size().x == doctest::Approx(192));
		editor.undo();
		editor.undo();
		editor.undo();

		// A right click without moving opens the context menu for the object under the cursor.
		editor.setTool(Editor::Tool::Select);
		editor.clearSelection();
		QSignalSpy menu(view, &Viewport::contextMenuRequested);
		const QPoint onBlock = screen(view, {16, -48, boundsOf(editor, block).max.z});   // beside the player start
		QCursor::setPos(view->mapToGlobal(onBlock));
		QTimer closer;   // the menu waits for a click: close it as soon as it is open
		closer.setInterval(50);
		QObject::connect(&closer, &QTimer::timeout, [&closer] {
			if (auto* popup = QApplication::activePopupWidget()) {
				popup->close();
				closer.stop();
			}
		});
		closer.start();
		QTest::mousePress(view, Qt::RightButton, {}, onBlock);
		QTest::mouseRelease(view, Qt::RightButton, {}, onBlock);
		settle();
		CHECK(menu.size() == 1);
		CHECK(editor.selection().contains(block));
		// Holding the right button to look around or fly (even without moving the mouse) and
		// letting go opens no menu.
		QTest::mousePress(view, Qt::RightButton, {}, onBlock);
		QTest::qWait(600);
		QTest::mouseRelease(view, Qt::RightButton, {}, onBlock);
		settle();
		CHECK(menu.size() == 1);
		closer.stop();
	}

	{
		// Clip, vertex and polygon tools in the top view.
		const SolidRef small{0, editor.doc().world.solids.back().id};
		const auto count = editor.doc().world.solids.size();
		Selection only;
		only.solids.push_back(small);
		editor.setSelection(only);
		editor.setTool(Editor::Tool::Clip);
		top->setFocus();
		QTest::mousePress(top, Qt::LeftButton, {}, screen(top, {-224, -320, 0}));
		QTest::mouseMove(top, screen(top, {-224, -200, 0}));
		QTest::mouseMove(top, screen(top, {-224, -96, 0}));
		QTest::mouseRelease(top, Qt::LeftButton, {}, screen(top, {-224, -96, 0}));
		QTest::keyClick(top, Qt::Key_Return);
		settle();
		CHECK(editor.doc().world.solids.size() == count + 1);
		CHECK(editor.selection().solids.size() == 2);
		editor.undo();
		CHECK(editor.doc().world.solids.size() == count);

		editor.setSelection(only);
		editor.setTool(Editor::Tool::Vertex);
		settle();
		const Vec3 corner{-192, -160, 0};
		QTest::mousePress(top, Qt::LeftButton, {}, screen(top, corner));
		QTest::mouseMove(top, screen(top, corner + Vec3{16, 0, 0}));
		QTest::mouseMove(top, screen(top, corner + Vec3{32, 0, 0}));
		QTest::mouseRelease(top, Qt::LeftButton, {}, screen(top, corner + Vec3{32, 0, 0}));
		settle();
		const auto pulled = boundsOf(editor, small);
		CHECK(pulled.max.x == doctest::Approx(-160));
		CHECK(isConvexValid(*editor.doc().findSolid(small)));
		editor.undo();
		CHECK(boundsOf(editor, small).max.x == doctest::Approx(-192));

		// Edges: clicking an edge middle picks it (in a 2D view the edges behind it too); the
		// tool panel's Edge box makes a wedge, bevel or rounding of it.
		const auto b = boundsOf(editor, small);
		QTest::mouseClick(top, Qt::LeftButton, {}, screen(top, {b.max.x, (b.min.y + b.max.y) / 2, 0}));
		settle();
		CHECK(editor.vertexCorners().size() == 4);
		CHECK(editor.selectedEdges().size() == 4);   // the four edges of the side x = max
		auto* edgeBox = window.findChild<QGroupBox*>("edgeTools");
		REQUIRE(edgeBox);
		CHECK(edgeBox->isVisibleTo(&window));
		// Just the top edge of that side.
		editor.setVertexCorners({{b.max.x, b.min.y, b.max.z}, {b.max.x, b.max.y, b.max.z}});
		CHECK(editor.selectedEdges().size() == 1);
		auto* wedge = edgeBox->findChild<QPushButton*>("edgeWedge");
		REQUIRE(wedge);
		CHECK(wedge->isEnabled());
		const std::size_t sidesBefore = editor.doc().findSolid(small)->sides.size();
		wedge->click();
		settle();
		CHECK(editor.doc().findSolid(small)->sides.size() == sidesBefore - 1);   // box -> ramp
		CHECK(editor.vertexCorners().empty());
		CHECK_FALSE(wedge->isEnabled());
		editor.undo();
		CHECK(editor.doc().findSolid(small)->sides.size() == sidesBefore);
		editor.setVertexCorners({{b.max.x, b.min.y, b.max.z}, {b.max.x, b.max.y, b.max.z}});
		edgeBox->findChild<QDoubleSpinBox*>("edgeSize")->setValue(4);
		edgeBox->findChild<QPushButton*>("edgeRound")->click();
		settle();
		CHECK(editor.doc().findSolid(small)->sides.size() > sidesBefore + 1);
		CHECK(isConvexValid(*editor.doc().findSolid(small)));
		editor.undo();
		CHECK(editor.doc().findSolid(small)->sides.size() == sidesBefore);

		editor.setTool(Editor::Tool::Polygon);
		for (const Vec3 p : {Vec3{-448, -448, 0}, Vec3{-384, -448, 0}, Vec3{-416, -384, 0}}) {
			QTest::mouseClick(top, Qt::LeftButton, {}, screen(top, p));
		}
		QTest::keyClick(top, Qt::Key_Return);
		settle();
		REQUIRE(editor.doc().world.solids.size() == count + 1);
		CHECK(editor.doc().world.solids.back().sides.size() == 5);   // triangle prism
		editor.undo();
		editor.setTool(Editor::Tool::Select);
	}

	// Save and load again.
	const QString path = home.filePath("test.vmf");
	QString error;
	REQUIRE(editor.saveMap(path, &error));
	REQUIRE(editor.openMap(path, &error));
	CHECK(editor.doc().world.solids.size() == 2);
	CHECK(editor.doc().entities.size() == 1);
	CHECK_FALSE(editor.modified());
}

TEST_CASE("displacement tool: make, sculpt with the mouse, undo, create by click, save and load") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();

	auto [made, ref] = edit::createBlock(editor.doc(), box({0, 0, 0}, {256, 256, 16}), "DEV/DEV_MEASUREGENERIC01");
	editor.commit(std::move(made));
	Selection selection;
	selection.solids.push_back(ref);
	editor.setSelection(selection);
	editor.setTool(Editor::Tool::Displace);
	settle();
	const auto topSide = [&]() -> const Side* {
		const Solid* solid = editor.doc().findSolid(ref);
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			if (face.normal.z > 0.9) return &solid->sides[static_cast<std::size_t>(face.sideIndex)];
		}
		return nullptr;
	};
	REQUIRE(topSide());
	CHECK_FALSE(topSide()->dispInfo());

	// Tool panel: power 3, Make turns the selected brush's top into a displacement.
	auto* box3 = window.findChild<QWidget*>("displaceTools");
	REQUIRE(box3);
	CHECK(box3->isVisible());
	auto* power = window.findChild<QComboBox*>("displacePower");
	REQUIRE(power);
	power->setCurrentIndex(power->findData(3));
	CHECK(editor.displaceSettings().power == 3);
	auto* make = window.findChild<QPushButton*>("displaceMake");
	REQUIRE(make);
	make->click();
	settle();
	REQUIRE(topSide()->dispInfo());
	CHECK(topSide()->dispInfo()->get("power") == "3");
	const std::string flatInfo = writeKeyValues([&] {
		KvNode root;
		root.children.push_back(*topSide()->dispInfo());
		return root;
	}());

	// Raise: press on the middle, drag a little, let go - one undo step.
	window.findChild<QPushButton*>(QStringLiteral("displace:%1").arg(int(Editor::DisplaceMode::Raise)))->click();
	auto settings = editor.displaceSettings();
	settings.radius = 96;
	settings.strength = 100;
	editor.setDisplaceSettings(settings);
	CHECK(window.findChild<QSlider*>("displaceRadius")->value() == 96);
	Viewport* view = window.view3D();
	view->camera().position = QVector3D(128, -260, 360);
	view->camera().yaw = 90;
	view->camera().pitch = -50;
	view->update();
	settle();
	const std::size_t historyBefore = editor.history().size();
	const QPoint middle = screen(view, {128, 128, 16});
	QTest::mouseMove(view, middle);
	settle();
	QTest::mousePress(view, Qt::LeftButton, {}, middle);
	for (int i = 1; i <= 6; ++i) {
		QTest::mouseMove(view, middle + QPoint(i * 3, 0));
		QApplication::processEvents();
	}
	QTest::mouseRelease(view, Qt::LeftButton, {}, middle + QPoint(18, 0));
	settle();
	CHECK(editor.history().size() == historyBefore + 1);
	auto sculpted = readDisplacement(*topSide());
	REQUIRE(sculpted);
	const std::size_t center = 4 * 9 + 4;
	CHECK(sculpted->vector(center).z > 0);
	CHECK(sculpted->vector(0).z == doctest::Approx(0));   // the corner is outside the brush
	editor.undo();
	settle();
	CHECK(readDisplacement(*topSide())->vector(center).z == doctest::Approx(0));
	editor.redo();
	settle();
	CHECK(readDisplacement(*topSide())->vector(center).z > 0);

	// Ctrl+wheel and ] size the brush.
	QWheelEvent wheel(QPointF(middle), view->mapToGlobal(QPointF(middle)), {}, QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
	                  Qt::NoScrollPhase, false);
	QApplication::sendEvent(view, &wheel);
	CHECK(editor.displaceSettings().radius > 96);
	const double before = editor.displaceSettings().radius;
	view->setFocus();
	QTest::keyClick(view, Qt::Key_BracketRight);
	CHECK(editor.displaceSettings().radius > before);

	// Create mode: a click on a face makes it a displacement.
	auto [second, ref2] = edit::createBlock(editor.doc(), box({-256, 0, 0}, {0, 256, 16}), "DEV/DEV_MEASUREGENERIC01");
	editor.commit(std::move(second));
	settle();
	window.findChild<QPushButton*>(QStringLiteral("displace:%1").arg(int(Editor::DisplaceMode::Create)))->click();
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {-128, 128, 16}));
	settle();
	{
		const Solid* solid = editor.doc().findSolid(ref2);
		int displacements = 0;
		for (const auto& side : solid->sides) displacements += side.dispInfo() ? 1 : 0;
		CHECK(displacements == 1);
	}

	// Sew: the step between the raised and the new flat one is closed (nothing here is apart:
	// both edges are flat, so nothing moves) - the button runs without a selection.
	editor.clearSelection();
	window.findChild<QPushButton*>("displaceSew")->click();
	settle();

	// Save and load: the same dispinfo; Flat again removes it.
	const QString path = home.filePath("terrain.vmf");
	QString error;
	REQUIRE(editor.saveMap(path, &error));
	const std::string savedInfo = writeKeyValues([&] {
		KvNode root;
		root.children.push_back(*topSide()->dispInfo());
		return root;
	}());
	REQUIRE(editor.openMap(path, &error));
	settle();
	REQUIRE(topSide());
	REQUIRE(topSide()->dispInfo());
	const std::string loadedInfo = writeKeyValues([&] {
		KvNode root;
		root.children.push_back(*topSide()->dispInfo());
		return root;
	}());
	CHECK(loadedInfo == savedInfo);
	CHECK(loadedInfo != flatInfo);
	editor.setSelection(selection);
	window.findChild<QPushButton*>("displaceDestroy")->click();
	settle();
	CHECK_FALSE(topSide()->dispInfo());
}

TEST_CASE("performance: a big map opens, edits and selects quickly") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	settle();

	// 6000 blocks in a grid (36000 faces, several materials) and 1500 entities with models.
	MapDocument big;
	const char* materials[] = {"demo/concrete", "demo/brick", "demo/metal", "tools/toolsnodraw"};
	for (int i = 0; i < 6000; ++i) {
		const double x = (i % 80) * 80.0, y = (i / 80) * 80.0;
		big.world.solids.push_back(makeBlock(big, box({x, y, 0}, {x + 64, y + 64, 64.0 + (i % 7) * 16.0}), materials[i % 4]));
	}
	for (int i = 0; i < 1500; ++i) {
		Entity e;
		e.id = big.newEntityId();
		e.keyValues = {{"classname", i % 3 ? "prop_static" : "light"}, {"origin", std::to_string((i % 50) * 120) + " " + std::to_string((i / 50) * 120) + " 96"}};
		if (i % 3) e.set("model", "models/props_demo/crate01.mdl");
		big.entities.push_back(std::move(e));
	}
	QTemporaryDir maps;
	const QString path = maps.filePath("big.vmf");
	{
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write(QByteArray::fromStdString(big.serialize()));
	}
	QElapsedTimer clock;
	clock.start();
	REQUIRE(window.openMap(path));
	window.view3D()->repaint();
	QApplication::processEvents();
	const qint64 openMs = clock.elapsed();

	// A small edit (move one block): commit plus everything listening to the change.
	Selection one;
	one.solids.push_back(SolidRef{0, editor.doc().world.solids[10].id});
	editor.setSelection(one);
	settle();
	clock.restart();
	for (int i = 0; i < 5; ++i) {
		editor.commit(edit::translate(editor.doc(), editor.selection(), {16, 0, 0}, false));
		QApplication::processEvents();
	}
	window.view3D()->repaint();
	QApplication::processEvents();
	const qint64 editMs = clock.elapsed() / 5;

	// Selecting other objects: flags only, no rebuild of the geometry.
	clock.restart();
	for (int i = 0; i < 20; ++i) {
		Selection s;
		s.solids.push_back(SolidRef{0, editor.doc().world.solids[std::size_t(100 + i * 50)].id});
		editor.setSelection(s);
		QApplication::processEvents();
	}
	window.view3D()->repaint();
	QApplication::processEvents();
	const qint64 selectMs = clock.elapsed() / 20;

	// Frames of the full view.
	clock.restart();
	for (int i = 0; i < 10; ++i) {
		window.view3D()->camera().yaw += 2.f;
		window.view3D()->repaint();
	}
	const qint64 frameMs = clock.elapsed() / 10;
	MESSAGE("big map: open " << openMs << " ms, edit " << editMs << " ms, select " << selectMs << " ms, frame " << frameMs << " ms");
	// Generous bounds (software OpenGL on CI): they catch a change that makes these many times slower.
	CHECK(openMs < 60000);
	CHECK(editMs < 3000);
	CHECK(selectMs < 1500);
	CHECK(frameMs < 3000);
}

TEST_CASE("view: frame limit, hiding tools and triggers, the Materials page lists every material") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	REQUIRE((indexed.count() > 0 || indexed.wait(10000)));
	settle();

	CHECK(window.width() <= 1400);   // opens at the size asked for, not wider

	// Frame limit: default 144, chosen in View > Frame rate limit, remembered.
	CHECK(editor.maxFps() == 144);
	auto* fpsMenu = window.findChild<QMenu*>("fpsMenu");
	REQUIRE(fpsMenu);
	QAction* sixty = nullptr;
	for (auto* a : fpsMenu->actions()) {
		if (a->data().toInt() == 60) sixty = a;
	}
	REQUIRE(sixty);
	sixty->trigger();
	CHECK(editor.maxFps() == 60);
	CHECK(QSettings().value("view/maxFps").toInt() == 60);
	{
		// Many requests in a row draw only a few frames.
		Viewport* view = window.view3D();
		const quint64 before = view->framesDrawn();
		QElapsedTimer clock;
		clock.start();
		while (clock.elapsed() < 500) {
			view->camera().yaw += 0.1f;
			view->requestFrame();
			QApplication::processEvents();
		}
		const quint64 frames = view->framesDrawn() - before;
		// 60 fps for the time that passed; some slack for repaints the window itself asks for.
		// Without the limit every pass of this loop may draw a frame.
		const double allowed = clock.elapsed() * 60.0 / 1000.0 * 1.5 + 5;
		CHECK(double(frames) <= allowed);
		CHECK(frames >= 3);
	}

	{
		// Selecting a prop is quick: its model field shares one list of all models (copying and
		// measuring tens of thousands of names on every click took seconds).
		auto [place, propId] = edit::placeEntity(editor.doc(), "prop_static", {0, 0, 0}, editor.entities()->find("prop_static"),
		                                         {{"model", "models/props_demo/crate01.mdl"}});
		editor.commit(std::move(place));
		const auto modelBox = [&]() -> QComboBox* {
			for (auto* combo : window.properties()->findChildren<QComboBox*>()) {
				if (combo->isEditable() && combo->currentText() == "models/props_demo/crate01.mdl") return combo;
			}
			return nullptr;
		};
		Selection one;
		one.entities.push_back(propId);
		editor.setSelection(one);
		settle();
		auto* first = modelBox();
		REQUIRE(first);
		CHECK(first->model()->rowCount() == window.assets()->modelNames().size());
		CHECK(first->sizeAdjustPolicy() == QComboBox::AdjustToMinimumContentsLengthWithIcon);
		const QAbstractItemModel* shared = first->model();
		editor.clearSelection();
		settle();
		editor.setSelection(one);
		settle();
		REQUIRE(modelBox());
		CHECK(modelBox()->model() == shared);

		// The wheel over a drop-down without focus scrolls the panel, it never changes the value.
		auto* box = modelBox();
		const QString before = box->currentText();
		QWheelEvent wheel(QPointF(5, 5), box->mapToGlobal(QPointF(5, 5)), {}, {0, -120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(box, &wheel);
		CHECK(box->currentText() == before);

		// Class picker: a search list with the related classes on top, arrows and Enter.
		auto* picker = window.properties()->findChild<ClassPicker*>("entityClass");
		REQUIRE(picker);
		CHECK(picker->className() == "prop_static");
		picker->openList();
		settle();
		REQUIRE(picker->list());
		CHECK_FALSE(picker->list()->item(0)->flags().testFlag(Qt::ItemIsEnabled));   // "Related to prop_static"
		QStringList related;
		for (int i = 1; i < picker->list()->count() && picker->list()->item(i)->flags().testFlag(Qt::ItemIsEnabled); ++i) related << picker->list()->item(i)->text();
		CHECK(related.contains("prop_dynamic"));
		QTest::keyClicks(picker->searchField(), "info");
		CHECK(picker->list()->currentItem()->text().startsWith("info_"));
		const QString second = picker->list()->count() > 1 ? picker->list()->item(1)->text() : QString();
		QTest::keyClick(picker->searchField(), Qt::Key_Down);
		if (!second.isEmpty()) CHECK(picker->list()->currentItem()->text() == second);
		picker->searchField()->clear();
		QTest::keyClicks(picker->searchField(), "prop dyn");
		QTest::keyClick(picker->searchField(), Qt::Key_Return);
		settle();
		CHECK(editor.doc().findEntity(propId)->classname() == "prop_dynamic");
		editor.undo();
		editor.undo();
		settle();
	}

	{
		// Drop-downs show their arrow, and a soundscape is picked from the game's list.
		CHECK(QFile::exists(":/ui/chevron-down.png"));
		auto [place, scapeId] = edit::placeEntity(editor.doc(), "env_soundscape", {64, 0, 32}, editor.entities()->find("env_soundscape"));
		editor.commit(std::move(place));
		Selection one;
		one.entities.push_back(scapeId);
		editor.setSelection(one);
		settle();
		REQUIRE(window.assets()->soundscapeNames().contains("demo.outside"));
		QComboBox* scapes = nullptr;
		for (auto* combo : window.properties()->findChildren<QComboBox*>()) {
			if (combo->isEditable() && combo->model()->rowCount() == window.assets()->soundscapeNames().size() &&
			    combo->findText("demo.outside") >= 0) {
				scapes = combo;
			}
		}
		REQUIRE(scapes);
		CHECK(scapes->maxVisibleItems() >= 16);
		scapes->setCurrentIndex(scapes->findText("demo.outside"));
		emit scapes->activated(scapes->currentIndex());
		settle();
		CHECK(editor.doc().findEntity(scapeId)->get("soundscape") == "demo.outside");
		editor.undo();
		editor.undo();
		settle();
	}

	// Hide tools: one action hides triggers and tool brushes and brings them back.
	auto* hideTools = window.findChild<QAction*>("hideTools");
	REQUIRE(hideTools);
	CHECK_FALSE(hideTools->isChecked());
	hideTools->trigger();
	CHECK_FALSE(editor.kindVisible(ObjectKind::Triggers));
	CHECK_FALSE(editor.kindVisible(ObjectKind::ToolBrushes));
	CHECK(editor.kindVisible(ObjectKind::World));
	editor.setKindVisible(ObjectKind::Triggers, true);
	CHECK_FALSE(hideTools->isChecked());
	editor.setKindVisible(ObjectKind::Triggers, false);
	CHECK(hideTools->isChecked());
	hideTools->trigger();
	CHECK(editor.kindVisible(ObjectKind::Triggers));
	CHECK(editor.kindVisible(ObjectKind::ToolBrushes));
	CHECK(window.findChild<QToolButton*>("showMenuButton"));

	// Materials page: the whole material list is there and the page opens a material right away.
	window.showPage(MainWindow::Page::Materials);
	settle();
	auto* holder = window.materialEditor()->findChild<QWidget*>("materialBrowserHolder");
	REQUIRE(holder);
	CHECK(window.assets()->parentWidget() == holder);
	CHECK(window.assets()->isVisible());
	CHECK_FALSE(window.materialEditor()->materialName().isEmpty());
	auto* list = window.assets()->findChild<QListView*>("materialList");
	REQUIRE(list);
	CHECK(list->model()->rowCount() > 5);
	list->setCurrentIndex(list->model()->index(3, 0));
	settle();
	CHECK(window.materialEditor()->materialName() == list->model()->index(3, 0).data(Qt::UserRole).toString());
	window.showPage(MainWindow::Page::Map);
	settle();
	CHECK(window.assets()->parentWidget() != holder);
	CHECK(window.assets()->isVisible());

	// Fits a laptop screen (1366 wide) on every page and with every dock under the map.
	const auto tooWide = [&window](const QString& where) {
		const int width = window.minimumSizeHint().width();
		if (width > 1366) {
			QStringList wide;
			QList<QPointer<QWidget>> widgets;
			for (auto* w : window.findChildren<QWidget*>()) widgets << w;
			for (const auto& w : widgets) {
				if (w && w->isVisible() && w->minimumSizeHint().width() > 500) {
					wide << QString("%1 '%2' %3").arg(w->metaObject()->className(), w->objectName()).arg(w->minimumSizeHint().width());
				}
			}
			MESSAGE(where.toStdString() << ": " << wide.join("; ").toStdString());
		}
		return width;
	};
	for (const auto page : {MainWindow::Page::Map, MainWindow::Page::Materials, MainWindow::Page::Models, MainWindow::Page::Sounds,
	                        MainWindow::Page::Particles, MainWindow::Page::Project}) {
		window.showPage(page);
		settle();
		CHECK(tooWide(QString("page %1").arg(int(page))) <= 1366);
	}
	window.showPage(MainWindow::Page::Map);
	for (const QString dock : {"Asset Browser", "Sounds", "Particles", "Compile", "Problems", "History", "Log"}) {
		window.raiseDock(dock);
		settle();
		CHECK(tooWide(dock) <= 1366);
	}
	// Every panel can be made small: the bottom ones low, the side ones narrow (dragging the
	// splitter must not stop at a big minimum, whichever tab is in front).
	for (auto* dock : window.findChildren<QDockWidget*>()) {
		if (!dock->widget()) continue;
		// What the dock layout uses: an explicit minimum wins over the widget's own wish.
		const QWidget* content = dock->widget();
		const QSize minimum(content->minimumWidth() > 0 ? content->minimumWidth() : content->minimumSizeHint().width(),
		                    content->minimumHeight() > 0 ? content->minimumHeight() : content->minimumSizeHint().height());
		const auto area = window.dockWidgetArea(dock);
		INFO(dock->objectName().toStdString() << " " << minimum.width() << "x" << minimum.height());
		if (area == Qt::BottomDockWidgetArea) CHECK(minimum.height() <= 140);
		if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) CHECK(minimum.width() <= 260);
	}
	{
		// Corner handle: one drag makes the bottom panels lower and the right panel narrower.
		window.raiseDock("Asset Browser");
		settle();
		auto* grip = window.findChild<QWidget*>("cornerGripRight");
		REQUIRE(grip);
		CHECK(grip->isVisible());
		auto* bottom = window.findChild<QDockWidget*>("Asset Browser");
		auto* right = window.findChild<QDockWidget*>("Object Properties");
		REQUIRE(bottom);
		REQUIRE(right);
		window.resizeDocks({bottom}, {260}, Qt::Vertical);   // room above its minimum (a row of tiles)
		window.resizeDocks({right}, {320}, Qt::Horizontal);  // and above the right panel's (the window fits the screen)
		settle();
		const int bottomBefore = bottom->height(), rightBefore = right->width();
		const QPoint start = grip->mapToGlobal(grip->rect().center());
		const auto send = [&](QEvent::Type type, const QPoint& global, Qt::MouseButtons buttons) {
			QMouseEvent event(type, grip->mapFromGlobal(global), global, Qt::LeftButton, buttons, Qt::NoModifier);
			QApplication::sendEvent(grip, &event);
		};
		send(QEvent::MouseButtonPress, start, Qt::LeftButton);
		send(QEvent::MouseMove, start + QPoint(40, 50), Qt::LeftButton);
		send(QEvent::MouseButtonRelease, start + QPoint(40, 50), Qt::NoButton);
		settle();
		CHECK(bottom->height() < bottomBefore - 30);
		CHECK(right->width() < rightBefore - 20);
		// Still on the views' corner afterwards.
		const QRect views = window.pages()->geometry();
		CHECK((grip->geometry().center() - views.bottomRight()).manhattanLength() <= 4);
		window.showPage(MainWindow::Page::Materials);
		settle();
		CHECK_FALSE(grip->isVisible());
		window.showPage(MainWindow::Page::Map);
		settle();
		CHECK(grip->isVisible());
	}
}

TEST_CASE("pages: leaving the map takes its docks away completely, also right after the start") {
	// The map's docks stayed as empty tab bars around the Materials page (their room kept, the page
	// squeezed, the window wider than the screen) when the page was shown before the docks went.
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	REQUIRE(window.openMap(fromPath(f.mod / "mapsrc/demo.vmf")));
	window.resize(1400, 760);
	window.show();
	QTest::qWait(200);   // as --page does: a page shown right after the start
	// The docks' tab bars in the window (Qt parks unused ones, empty, outside it).
	const auto dockTabsShown = [&window] {
		const auto bars = window.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly);
		return std::count_if(bars.begin(), bars.end(), [&window](QTabBar* bar) {
			return bar->isVisible() && bar->count() > 0 && window.rect().intersects(bar->geometry());
		});
	};
	for (const auto page : {MainWindow::Page::Materials, MainWindow::Page::Project, MainWindow::Page::Models}) {
		window.showPage(page);
		settle();
		CAPTURE(int(page));
		CHECK(dockTabsShown() == 0);
		window.showPage(MainWindow::Page::Map);
		settle();
		CHECK(dockTabsShown() > 0);
	}
}

TEST_CASE("panels: the wheel scrolls instead of switching, low docks show whole tiles, the panel keeps its place") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.resize(1400, 700);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	REQUIRE((indexed.count() > 0 || indexed.wait(10000)));
	settle();

	const auto wheelOn = [](QWidget* target, int delta) {
		QWheelEvent wheel(QPointF(5, 5), target->mapToGlobal(QPointF(5, 5)), {}, {0, delta}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(target, &wheel);
	};
	auto* scroll = window.properties()->findChild<QScrollArea*>();
	REQUIRE(scroll);

	// Two props: picking the second keeps the panel where it was scrolled to.
	int ids[2] = {0, 0};
	for (int i = 0; i < 2; ++i) {
		auto [place, id] = edit::placeEntity(editor.doc(), "prop_static", {double(i) * 64, 0, 0}, editor.entities()->find("prop_static"),
		                                     {{"model", "models/props_demo/crate01.mdl"}});
		editor.commit(std::move(place));
		ids[i] = id;
	}
	Selection first;
	first.entities.push_back(ids[0]);
	editor.setSelection(first);
	settle();
	REQUIRE(scroll->verticalScrollBar()->maximum() > 0);
	const int scrolled = scroll->verticalScrollBar()->maximum() / 2;
	scroll->verticalScrollBar()->setValue(scrolled);
	Selection second;
	second.entities.push_back(ids[1]);
	editor.setSelection(second);
	settle();
	CHECK(std::abs(scroll->verticalScrollBar()->value() - scrolled) <= 4);
	editor.setSelection(first);
	settle();
	CHECK(std::abs(scroll->verticalScrollBar()->value() - scrolled) <= 4);

	// The wheel over the Keyvalues/Outputs/Inputs tabs scrolls the panel, it never switches the tab.
	auto* tabs = window.properties()->findChild<QTabBar*>("propertyTabs");
	REQUIRE(tabs);
	const int tab = tabs->currentIndex();
	for (int i = 0; i < 3; ++i) wheelOn(tabs, -120);
	CHECK(tabs->currentIndex() == tab);
	// Tab bars elsewhere (the docks) do not switch by the wheel either.
	for (auto* bar : window.findChildren<QTabBar*>()) {
		if (bar == tabs || !bar->isVisible() || bar->count() < 2) continue;
		const int before = bar->currentIndex();
		wheelOn(bar, -120);
		CHECK(bar->currentIndex() == before);
	}
	// A number box without focus does not change either (no wheel focus).
	if (auto* spin = window.properties()->findChild<QAbstractSpinBox*>()) {
		CHECK(spin->focusPolicy() == Qt::StrongFocus);
	}

	// The sky drop-down: resting the mouse on it lets the wheel go through the skies.
	editor.clearSelection();
	settle();
	{
		QPointer<QComboBox> sky = window.properties()->findChild<QComboBox*>("skyPicker");
		REQUIRE(sky);
		sky->blockSignals(true);
		while (sky->count() < 3) sky->addItem(QStringLiteral("test_sky_%1").arg(sky->count()));
		sky->setCurrentIndex(0);
		sky->blockSignals(false);
		const QString next = sky->itemText(1);
		QTest::qWait(700);   // the panel is not being scrolled any more
		QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), sky->mapToGlobal(QPointF(5, 5)));
		QApplication::sendEvent(sky, &enter);
		wheelOn(sky, -120);
		CHECK(sky->currentIndex() == 0);   // just arrived: it scrolls the panel
		QTest::qWait(700);
		wheelOn(sky, -120);   // rested, the panel still: the wheel picks the next sky
		settle();
		CHECK(editor.doc().world.get("skyname") == next.toStdString());
	}

	// Asset browser in a low dock: whole tiles with their names, one row at least; the recent
	// row makes room.
	auto* list = window.assets()->findChild<QListView*>("materialList");
	REQUIRE(list);
	QDockWidget* dock = nullptr;
	for (QWidget* w = window.assets(); w && !dock; w = w->parentWidget()) dock = qobject_cast<QDockWidget*>(w);
	REQUIRE(dock);
	editor.setCurrentMaterial("demo/concrete");
	editor.applyMaterialToSelection("demo/concrete");
	window.resizeDocks({dock}, {150}, Qt::Vertical);
	settle();
	const int low = list->iconSize().height();
	CHECK(low < 96);
	CHECK(low >= 40);
	CHECK((list->gridSize().height() <= list->viewport()->height() || low == 40));
	auto* recent = window.assets()->findChild<QListWidget*>();
	if (recent && window.assets()->height() < 250) CHECK_FALSE(recent->isVisible());
	window.resizeDocks({dock}, {420}, Qt::Vertical);
	settle();
	CHECK(list->iconSize().height() > low);
}

TEST_CASE("ux: whole tiles in a low dock, recent popup, the Materials page follows, targets, mod chooser, copying, start page") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.resize(1400, 800);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	REQUIRE((indexed.count() > 0 || indexed.wait(10000)));
	settle();

	// 1. The Asset Browser never gets lower than one row of tiles with their names.
	AssetBrowser* assets = window.assets();
	const int minimum = assets->minimumSizeHint().height();
	CHECK(minimum <= 170);
	QDockWidget* dock = nullptr;
	for (QWidget* w = assets; w && !dock; w = w->parentWidget()) dock = qobject_cast<QDockWidget*>(w);
	REQUIRE(dock);
	window.resizeDocks({dock}, {30}, Qt::Vertical);
	settle();
	CHECK(assets->height() >= minimum);
	auto* list = assets->findChild<QListView*>("materialList");
	REQUIRE(list);
	CHECK(list->gridSize().height() <= list->viewport()->height());   // a whole tile, name included
	// Recently used materials: a button with a popup, not a row of their own.
	editor.setCurrentMaterial("demo/concrete");
	auto* recentButton = assets->findChild<QToolButton*>("recentMaterials");
	REQUIRE(recentButton);
	CHECK(recentButton->isEnabled());
	assets->showRecent();
	settle();
	auto* popup = assets->findChild<QWidget*>("recentPopup");
	REQUIRE(popup);
	CHECK(popup->isVisible());
	auto* recent = popup->findChild<QListWidget*>();
	REQUIRE(recent);
	REQUIRE(recent->count() >= 1);
	QTest::mouseClick(recent->viewport(), Qt::LeftButton, {}, recent->visualItemRect(recent->item(0)).center());
	settle();
	CHECK_FALSE(popup->isVisible());
	window.resizeDocks({dock}, {260}, Qt::Vertical);
	settle();

	// 2. The Materials page shows the material picked last in the browser.
	REQUIRE(list->model()->rowCount() > 4);
	for (const int row : {2, 4}) {
		window.showPage(MainWindow::Page::Map);
		settle();
		list->setCurrentIndex(list->model()->index(row, 0));
		settle();
		const QString picked = list->model()->index(row, 0).data(Qt::UserRole).toString();
		window.showPage(MainWindow::Page::Materials);
		settle();
		CHECK(window.materialEditor()->materialName().compare(picked, Qt::CaseInsensitive) == 0);
	}
	window.showPage(MainWindow::Page::Map);
	settle();

	// 3. A target field lists the entities without a name too; picking one names it.
	auto [placeNpc, npcId] = edit::placeEntity(editor.doc(), "npc_citizen", {0, 0, 0}, editor.entities()->find("npc_citizen"));
	editor.commit(std::move(placeNpc));
	auto [placeScript, scriptId] = edit::placeEntity(editor.doc(), "scripted_sequence", {64, 0, 0}, editor.entities()->find("scripted_sequence"));
	editor.commit(std::move(placeScript));
	Selection script;
	script.entities.push_back(scriptId);
	editor.setSelection(script);
	settle();
	QComboBox* target = nullptr;
	int unnamed = -1;
	for (auto* combo : window.properties()->findChildren<QComboBox*>()) {
		for (int i = 0; i < combo->count(); ++i) {
			if (combo->itemData(i, TARGET_ENTITY_ROLE).toInt() == npcId) {
				target = combo;
				unnamed = i;
			}
		}
	}
	REQUIRE(target);
	CHECK(target->itemText(unnamed).contains("npc_citizen"));
	emit target->activated(unnamed);
	settle();
	const std::string npcName = editor.doc().findEntity(npcId)->get("targetname");
	CHECK_FALSE(npcName.empty());
	CHECK(editor.doc().findEntity(scriptId)->get("m_iszEntity") == npcName);
	// The panel fits its dock: nothing reaches out to the right (arrows of drop-downs included).
	auto* scroll = window.properties()->findChild<QScrollArea*>();
	REQUIRE(scroll);
	QDockWidget* right = window.findChild<QDockWidget*>("Object Properties");
	REQUIRE(right);
	window.resizeDocks({right}, {320}, Qt::Horizontal);
	settle();
	CHECK(scroll->widget()->width() <= scroll->viewport()->width());
	editor.clearSelection();
	settle();

	// 4. The mod chooser says what it is and lists the own mods first.
	auto* gameButton = window.findChild<QToolButton*>("gameButton");
	REQUIRE(gameButton);
	CHECK(gameButton->text().startsWith("Mod:"));
	QStringList sections;
	for (auto* a : gameButton->menu()->actions()) {
		if (a->isSeparator() && !a->text().isEmpty()) sections << a->text();
	}
	REQUIRE(sections.size() >= 2);
	CHECK(sections.front() == "Your mods");

	// 7. What Mapsmith shows as text can be copied.
	window.problems()->runChecks();
	CHECK_FALSE(window.problems()->problemsText().isEmpty());
	CHECK(window.findChild<QPushButton*>("problemsCopyAll"));
	CHECK(window.findChild<QPushButton*>("compileCopyAll"));

	// 9. The start page: a first map - the tutorial map by default, or an empty room - then
	// Continue opens it again.
	// A sound the game has: the tutorial map's own tunnel soundscape uses it (in the mod's file).
	{
		const QString drip = fromPath(editor.gameEntry()->gameDir) + "/sound/ambient/water/drip1.wav";
		QDir().mkpath(QFileInfo(drip).absolutePath());
		QFile file(drip);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write("RIFF");
		file.close();
		editor.game()->files.noteWrittenFile(toPath(drip));
	}
	REQUIRE(window.makeStarterMap("ux_tutorial"));
	CHECK(QFileInfo(editor.filePath()).fileName() == "ux_tutorial.vmf");
	CHECK(editor.doc().world.solids.size() > 30);
	const auto classCount = [&editor](const char* classname) {
		return std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [classname](const Entity& e) { return e.classname() == classname; });
	};
	CHECK(classCount("env_soundscape") == 6);
	{
		const QString prefix = QString::fromStdString(toLower(editor.gameEntry()->folder()));
		QFile scapes(fromPath(editor.gameEntry()->gameDir) + "/scripts/soundscapes_" + prefix + ".txt");
		REQUIRE(scapes.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(scapes.readAll());
		CHECK(text.contains(prefix + ".tutorial_tunnel"));
		CHECK(text.contains("ambient/water/drip1.wav"));
		QFile manifest(fromPath(editor.gameEntry()->gameDir) + "/scripts/soundscapes_manifest.txt");
		REQUIRE(manifest.open(QIODevice::ReadOnly));
		CHECK(QString::fromUtf8(manifest.readAll()).contains("scripts/soundscapes_" + prefix + ".txt"));
		const auto tunnelScapes = std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [&prefix](const Entity& e) {
			return e.classname() == "env_soundscape" && QString::fromStdString(e.get("soundscape")) == prefix + ".tutorial_tunnel";
		});
		CHECK(tunnelScapes == 3);
	}
	CHECK(classCount("item_suit") == 1);
	CHECK(classCount("sky_camera") == 0);
	REQUIRE(window.makeStarterMap("ux_room", false));
	const QString room = editor.filePath();
	CHECK(QFileInfo(room).fileName() == "ux_room.vmf");
	CHECK(QFileInfo::exists(room));
	CHECK(editor.doc().world.solids.size() == 6);
	CHECK(window.currentPage() == MainWindow::Page::Map);
	REQUIRE_FALSE(loadRecentProjects().empty());
	CHECK(QDir::cleanPath(QString::fromStdString(loadRecentProjects().front().mapPath)) == QDir::cleanPath(room));
	editor.newMap();
	// Every other page gives way to the start page (a page shown before kept its minimum size).
	window.showPage(MainWindow::Page::Animations);
	window.showStartPage();
	settle();
	CHECK(window.startPageShown());
	CHECK(window.animationsPage()->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored);
	auto* start = window.startPage();
	REQUIRE(start);
	auto* continueButton = start->findChild<QPushButton*>("startContinue");
	REQUIRE(continueButton);
	CHECK(continueButton->isVisible());
	QTest::mouseClick(continueButton, Qt::LeftButton);
	settle();
	CHECK_FALSE(window.startPageShown());
	CHECK(QDir::cleanPath(editor.filePath()) == QDir::cleanPath(room));
	auto* showAtStart = start->findChild<QCheckBox*>("startShowAtStartup");
	REQUIRE(showAtStart);
	CHECK(StartPage::showAtStartup());
	showAtStart->setChecked(false);
	CHECK_FALSE(StartPage::showAtStartup());
}

TEST_CASE("update check: release information and the version button") {
	const QByteArray json = R"({"tag_name": "v9.1.0", "html_url": "https://github.com/29j2003/hammer-test/releases/tag/v9.1.0",
		"body": "New: things", "draft": false, "prerelease": false, "assets": [
		{"name": "Mapsmith-9.1.0-windows.zip", "browser_download_url": "https://example.invalid/a.zip"},
		{"name": "Mapsmith-9.1.0-windows.zip.sha256", "browser_download_url": "https://example.invalid/a.zip.sha256"},
		{"name": "Mapsmith-Setup-9.1.0.exe", "browser_download_url": "https://example.invalid/setup.exe"},
		{"name": "Mapsmith-Setup-9.1.0.exe.sha256", "browser_download_url": "https://example.invalid/setup.exe.sha256"}]})";
	const auto release = parseRelease(json);
	REQUIRE(release);
	CHECK(release->version == "9.1.0");
	CHECK(release->zipName == "Mapsmith-9.1.0-windows.zip");
	// The ZIP's checksum, not the installer's that the release lists after it (assets come in upload order).
	CHECK(release->checksumUrl.toString() == "https://example.invalid/a.zip.sha256");
	CHECK(release->notes == "New: things");
	CHECK_FALSE(parseRelease(R"({"tag_name": "v9.2.0", "prerelease": true})"));
	CHECK_FALSE(parseRelease("not json"));

	QTemporaryDir home;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().setValue("update/skipVersion", "9.1.0");   // no question at start for this version
	Editor editor;
	MainWindow window(&editor);
	emit window.updater()->updateAvailable(*release);
	bool found = false;
	for (auto* b : window.findChildren<QToolButton*>()) {
		found = found || b->text().contains("Update to v9.1.0");
	}
	CHECK(found);
}

TEST_CASE("source control: a new repository, its changes, a commit and the history") {
	const QString gitProgram = SourceControlPanel::gitProgram();
	if (gitProgram.isEmpty()) {
		MESSAGE("git is not installed - skipped");
		return;
	}
	QTemporaryDir home;
	QTemporaryDir repo;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	// Who commits, for this test only (environment, not a file; the address is put together here so
	// no address stands in the source).
	QFile emptyConfig(home.filePath("gitconfig"));
	REQUIRE(emptyConfig.open(QIODevice::WriteOnly));
	emptyConfig.close();
	const QByteArray address = QByteArray("test") + '@' + "example.invalid";
	const QList<QPair<const char*, QByteArray>> environment{{"GIT_AUTHOR_NAME", "Test"},     {"GIT_COMMITTER_NAME", "Test"},
	                                                        {"GIT_AUTHOR_EMAIL", address},   {"GIT_COMMITTER_EMAIL", address},
	                                                        {"GIT_CONFIG_GLOBAL", emptyConfig.fileName().toUtf8()}, {"GIT_CONFIG_NOSYSTEM", "1"}};
	for (const auto& [name, value] : environment) qputenv(name, value);
	const auto git = [&](const QStringList& args) {
		QProcess process;
		process.setWorkingDirectory(repo.path());
		process.start(gitProgram, args);
		process.waitForFinished(10000);
		return process.exitCode();
	};
	const auto write = [&](const QString& name, const QByteArray& text) {
		QFile file(QDir(repo.path()).filePath(name));
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write(text);
	};

	Editor editor;
	SourceControlPanel panel(&editor);
	panel.resize(1000, 700);
	panel.show();
	// Waits until every queued git command is done.
	const auto idle = [&] {
		for (int i = 0; i < 500; ++i) {
			QTest::qWait(20);
			if (!panel.busy()) {
				QTest::qWait(40);
				if (!panel.busy()) return true;
			}
		}
		return false;
	};
	auto* changes = panel.findChild<QTreeWidget*>("scmChanges");
	auto* history = panel.findChild<QTreeWidget*>("scmHistory");
	auto* message = panel.findChild<QPlainTextEdit*>("scmMessage");
	auto* commit = panel.findChild<QPushButton*>("scmCommit");
	auto* init = panel.findChild<QPushButton*>("scmInit");
	REQUIRE(changes);
	REQUIRE(history);
	REQUIRE(message);
	REQUIRE(commit);
	REQUIRE(init);
	const auto changedFiles = [&] {
		QStringList out;
		for (int i = 0; i < changes->topLevelItemCount(); ++i) out << changes->topLevelItem(i)->text(0);
		return out;
	};

	// An empty folder: not a repository yet, one click starts one.
	panel.setRepository(repo.path());
	REQUIRE(idle());
	CHECK(init->isVisible());
	init->click();
	REQUIRE(idle());
	CHECK_FALSE(init->isVisible());
	CHECK(changes->topLevelItemCount() == 0);

	// A new file shows up as new; committing it puts it into the history.
	write("map_notes.txt", "first\n");
	panel.refresh();
	REQUIRE(idle());
	REQUIRE(changedFiles() == QStringList{"map_notes.txt"});
	CHECK(changes->topLevelItem(0)->text(1) == "?");
	CHECK(changes->topLevelItem(0)->checkState(0) == Qt::Checked);
	message->setPlainText("Notizen zur Karte");
	QSignalSpy finished(&panel, &SourceControlPanel::commandFinished);
	commit->click();
	REQUIRE(idle());
	REQUIRE(!finished.isEmpty());
	CHECK(finished.last().at(0).toBool());
	CHECK(changes->topLevelItemCount() == 0);
	REQUIRE(history->topLevelItemCount() == 1);
	CHECK(history->topLevelItem(0)->text(1) == "Notizen zur Karte");
	CHECK(history->topLevelItem(0)->text(2) == "Test");
	CHECK(message->toPlainText().isEmpty());

	// Only checked files go into a commit.
	write("map_notes.txt", "second\n");
	write("later.txt", "not yet\n");
	panel.refresh();
	REQUIRE(idle());
	REQUIRE(changes->topLevelItemCount() == 2);
	for (int i = 0; i < changes->topLevelItemCount(); ++i) {
		auto* item = changes->topLevelItem(i);
		if (item->text(0) == "later.txt") item->setCheckState(0, Qt::Unchecked);
		if (item->text(0) == "map_notes.txt") CHECK(item->text(1) == "M");
	}
	message->setPlainText("Nur die Notizen");
	commit->click();
	REQUIRE(idle());
	CHECK(changedFiles() == QStringList{"later.txt"});
	CHECK(history->topLevelItemCount() == 2);

	// A remote gives a web page and the network buttons (not pressed here: no network in tests).
	CHECK(git({"remote", "add", "origin", "https://github.com/example/mymod.git"}) == 0);
	panel.refresh();
	REQUIRE(idle());
	CHECK(panel.findChild<QLabel*>("scmRemote")->text() == "https://github.com/example/mymod");
	CHECK(panel.findChild<QPushButton*>("scmOpenRemote")->isVisible());
	CHECK(panel.findChild<QPushButton*>("scmPush")->isEnabled());
	CHECK(panel.findChild<QLabel*>("scmBranch")->text().contains("<b>"));
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		panel.grab().save(QDir(shots).filePath("source_control.png"));
	}

	for (const auto& entry : environment) qunsetenv(entry.first);
}

TEST_CASE("ship panel: what the open map needs, share ZIP, Workshop item") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	test::writeText(f.mod / "maps/demo.bsp", "VBSP fake map");
	Editor editor;
	editor.discover(fromPath(f.steam));
	REQUIRE(editor.openGame("sourcemods/mymod"));
	QString error;
	REQUIRE(editor.openMap(fromPath(f.mod / "mapsrc/demo.vmf"), &error));

	ShipPanel panel(&editor);
	panel.resize(1000, 900);
	panel.show();
	CHECK(QDir::fromNativeSeparators(panel.bspPath()).endsWith("maps/demo.bsp"));   // the open map's BSP in the mod

	// What the map needs: the demo's own materials are the mod's ("Your mod"), with their textures.
	QSignalSpy scanned(&panel, &ShipPanel::scanned);
	auto* scan = panel.findChild<QPushButton*>("shipScan");
	REQUIRE(scan);
	scan->click();
	REQUIRE(scanned.wait(15000));
	auto* tree = panel.findChild<QTreeWidget*>("shipFiles");
	REQUIRE(tree);
	QTreeWidgetItem* custom = nullptr;
	for (int i = 0; i < tree->topLevelItemCount(); ++i) {
		if (tree->topLevelItem(i)->data(0, Qt::UserRole).toInt() == int(ShipFile::Origin::Custom)) custom = tree->topLevelItem(i);
	}
	REQUIRE(custom);
	QStringList files;
	for (int i = 0; i < custom->childCount(); ++i) files << custom->child(i)->text(0);
	CHECK(files.contains("materials/demo/brickwall001.vmt"));
	CHECK(files.contains("materials/demo/brickwall001.vtf"));
	CHECK(files.contains("models/humans/group01/male_07.mdl"));
	CHECK(files.contains("sound/ambient/wind1.wav"));
	CHECK_FALSE(panel.findChild<QLabel*>("shipSummary")->text().isEmpty());

	// Share ZIP: the map and every file of the mod it needs.
	QSignalSpy finished(&panel, &ShipPanel::taskFinished);
	const QString zip = QDir(home.path()).filePath("share/demo.zip");
	panel.makeShareZip(zip);
	REQUIRE(finished.wait(15000));
	CHECK(finished.takeFirst().at(0).toBool());
	const auto names = zipEntryNames(toPath(zip));
	for (const char* expected : {"maps/demo.bsp", "materials/demo/brickwall001.vmt", "materials/demo/brickwall001.vtf",
	                             "models/humans/group01/male_07.mdl", "sound/ambient/wind1.wav"}) {
		INFO(expected);
		CHECK(std::find(names.begin(), names.end(), expected) != names.end());
	}
	CHECK(std::none_of(names.begin(), names.end(), [](const std::string& n) { return n.starts_with("materials/concrete/"); }));

	// Without bspzip next to the compile tools, packing says so instead of failing silently.
	panel.packIntoBsp();
	REQUIRE(finished.count() == 1);
	CHECK_FALSE(finished.takeFirst().at(0).toBool());

	// Mod ZIP: the mod without map sources.
	panel.makeModZip(QDir(home.path()).filePath("share/mymod.zip"));
	REQUIRE(finished.wait(15000));
	CHECK(finished.takeFirst().at(0).toBool());
	const auto modNames = zipEntryNames(toPath(QDir(home.path()).filePath("share/mymod.zip")));
	CHECK(std::find(modNames.begin(), modNames.end(), "mymod/gameinfo.txt") != modNames.end());
	CHECK(std::none_of(modNames.begin(), modNames.end(), [](const std::string& n) { return n.ends_with(".vmf"); }));

	// Workshop: the content folder and the item file for steamcmd; no password anywhere.
	panel.findChild<QLineEdit*>("shipTitle")->setText("Demo map");
	const QString command = panel.prepareWorkshop();
	CHECK(command.contains("+workshop_build_item"));
	CHECK_FALSE(command.contains("password", Qt::CaseInsensitive));
	CHECK(panel.findChild<QLineEdit*>("shipWorkshopCommand")->text() == command);
}

TEST_CASE("safety and search: autosave, crash recovery, .vmx backup, undo history, find, replace materials") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_DATA_HOME", QDir(home.path()).filePath("data").toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	REQUIRE(editor.openGame("sourcemods/mymod"));
	const QString mapPath = fromPath(f.mod / "mapsrc/demo.vmf");
	// The file is older than anything written now.
	{
		QFile file(mapPath);
		REQUIRE(file.open(QIODevice::ReadWrite));
		REQUIRE(file.setFileTime(QDateTime::currentDateTime().addSecs(-120), QFileDevice::FileModificationTime));
	}
	QString error;
	REQUIRE(editor.openMap(mapPath, &error));
	const auto cube = [](Vec3 a, Vec3 b) {
		Bounds out;
		out.add(a);
		out.add(b);
		return out;
	};

	{
		// Autosave: nothing while unchanged, a copy in mapsrc/autosave after a change.
		CHECK_FALSE(recoveryAfterCrash());   // no earlier session
		Autosaver saver(&editor);
		CHECK(QFile::exists(Autosaver::markerPath()));
		CHECK(saver.autosaveNow().isEmpty());
		auto [block, ref] = edit::createBlock(editor.doc(), cube({1000, 1000, 0}, {1064, 1064, 64}), "DEMO/BRICKWALL001");
		editor.commit(std::move(block));
		const QString written = saver.autosaveNow();
		REQUIRE_FALSE(written.isEmpty());
		saver.waitForWrites();
		CHECK(QFileInfo(written).absolutePath() == QFileInfo(mapPath).absolutePath() + "/autosave");
		QFile copy(written);
		REQUIRE(copy.open(QIODevice::ReadOnly));
		const QByteArray text = copy.readAll();
		CHECK(MapDocument::parse(std::string_view(text.constData(), static_cast<std::size_t>(text.size()))).solidCount() == editor.doc().solidCount());
		CHECK(saver.autosaveNow().isEmpty());   // no new change, no new copy

		// Opening the map again offers the newer autosave.
		const auto offer = recoveryForMap(mapPath);
		REQUIRE(offer);
		CHECK(offer->autosaves.value(0) == written);
		CHECK_FALSE(offer->afterCrash);
		// The session did not end (no endSession): after a "crash" it is offered as well.
		const auto crash = recoveryAfterCrash();
		REQUIRE(crash);
		CHECK(crash->afterCrash);
		CHECK(crash->mapPath == mapPath);

		// Restoring keeps the map's own path and counts as unsaved work.
		REQUIRE(editor.openRecovered(written, mapPath, &error));
		CHECK(editor.filePath() == mapPath);
		CHECK(editor.modified());
		const auto solids = editor.doc().solidCount();

		// Saving keeps the previous file as demo.vmx.
		REQUIRE(editor.saveMap(mapPath, &error));
		CHECK_FALSE(editor.modified());
		CHECK(QFile::exists(fromPath(f.mod / "mapsrc/demo.vmx")));
		CHECK_FALSE(recoveryForMap(mapPath));   // the file is now the newest
		saver.endSession();
		CHECK_FALSE(QFile::exists(Autosaver::markerPath()));
		CHECK_FALSE(recoveryAfterCrash());
		CHECK(editor.doc().solidCount() == solids);
	}

	{
		// Undo history: every change listed; clicking an earlier line undoes up to it.
		HistoryPanel panel(&editor);
		auto [a, refA] = edit::createBlock(editor.doc(), cube({2000, 0, 0}, {2064, 64, 64}), "DEMO/BRICKWALL001");
		editor.commit(std::move(a));
		auto [b, refB] = edit::createBlock(editor.doc(), cube({2100, 0, 0}, {2164, 64, 64}), "DEMO/BRICKWALL001");
		editor.commit(std::move(b));
		auto* list = panel.list();
		REQUIRE(list->count() == static_cast<int>(editor.history().size()) + 1);
		CHECK(list->currentRow() == static_cast<int>(editor.history().index()));
		const auto before = editor.history().index();
		list->setCurrentRow(static_cast<int>(before) - 2);
		QTRY_COMPARE(editor.history().index(), before - 2);
		CHECK_FALSE(editor.doc().findSolid(refA));
		CHECK(list->item(static_cast<int>(before))->font().italic());   // undone, still there to redo
		list->setCurrentRow(static_cast<int>(before));
		QTRY_COMPARE(editor.history().index(), before);
		CHECK(editor.doc().findSolid(refB));
	}

	{
		// Find: key=value with a wildcard finds the demo's relay; a click selects it, "Go to" frames.
		bool framed = false;
		FindDialog dialog(&editor, [&framed] { framed = true; });
		dialog.setSearch(int(EntityField::KeyValue), "targetname=relay_*");
		auto* results = dialog.results();
		REQUIRE(results->topLevelItemCount() == 1);
		CHECK(results->topLevelItem(0)->text(0) == "logic_relay");
		results->setCurrentItem(results->topLevelItem(0));
		REQUIRE(editor.selection().entities.size() == 1);
		CHECK(editor.doc().findEntity(editor.selection().entities.front())->get("targetname") == "relay_start");
		dialog.findChild<QPushButton*>("findGoTo")->click();
		CHECK(framed);
		dialog.setSearch(int(EntityField::Anything), "target:relay_start");
		CHECK(results->topLevelItemCount() >= 1);   // the trigger that fires at it
		// Entity report: every entity, narrowed by class.
		dialog.showTab(1);
		CHECK(dialog.report()->topLevelItemCount() == static_cast<int>(editor.doc().entities.size()));
		auto* classes = dialog.findChild<QComboBox*>("reportClass");
		REQUIRE(classes);
		const int relayClass = classes->findData("logic_relay");
		REQUIRE(relayClass > 0);
		classes->setCurrentIndex(relayClass);
		CHECK(dialog.report()->topLevelItemCount() == 1);
		editor.clearSelection();
	}

	{
		// Replace materials: the floor tiles become bricks across the map; undo brings them back.
		const auto faces = [&](const char* material) {
			int n = 0;
			for (const auto& solid : editor.doc().world.solids) {
				for (const auto& side : solid.sides) n += iequals(side.material, material) ? 1 : 0;
			}
			return n;
		};
		const int tiles = faces("DEMO/FLOORTILE001");
		REQUIRE(tiles > 0);
		ReplaceMaterialsDialog dialog(&editor, {});
		dialog.setFind("demo/floortile001");
		dialog.setReplaceWith("demo/brickwall001");
		CHECK(dialog.matchCount() == tiles);
		CHECK(dialog.findChild<QLabel*>("replaceCount")->text().startsWith(QString::number(tiles)));
		CHECK(dialog.apply() == tiles);
		CHECK(faces("DEMO/FLOORTILE001") == 0);
		editor.undo();
		CHECK(faces("DEMO/FLOORTILE001") == tiles);
		// Wildcards, nothing selected: the scope is the whole map.
		dialog.setWildcards(true);
		dialog.setFind("demo/floor*");
		CHECK(dialog.matchCount() == tiles);
	}
	qunsetenv("XDG_DATA_HOME");
}

TEST_CASE("visgroups panel and func_instance: group, hide, show; instance preview and collapse") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();

	// Two blocks; the first goes into a group.
	auto [c1, a] = edit::createBlock(editor.doc(), box({0, 0, 0}, {64, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	editor.commit(std::move(c1));
	auto [c2, b] = edit::createBlock(editor.doc(), box({128, 0, 0}, {192, 64, 64}), "DEV/DEV_MEASUREGENERIC01");
	editor.commit(std::move(c2));
	VisGroupsPanel groups(&editor);
	groups.show();
	Selection onlyA;
	onlyA.solids.push_back(a);
	editor.setSelection(onlyA);
	groups.findChild<QLineEdit*>("visGroupName")->setText("Room");
	groups.findChild<QPushButton*>("visNew")->click();
	settle();
	const auto list = visGroups(editor.doc());
	REQUIRE(list.size() == 1);
	CHECK(list[0].name == "Room");
	const int id = list[0].id;
	CHECK(groups.currentGroup() == id);
	auto* tree = groups.findChild<QTreeWidget*>("visGroupTree");
	REQUIRE(tree);
	REQUIRE(tree->topLevelItemCount() == 1);
	CHECK(tree->topLevelItem(0)->text(1) == "1");
	// Untick: the member is hidden (and no longer selected); tick again: back.
	tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
	settle();
	CHECK(editor.hidden().contains(a));
	CHECK_FALSE(editor.hidden().contains(b));
	CHECK(editor.selection().solids.empty());
	tree->topLevelItem(0)->setCheckState(0, Qt::Checked);
	settle();
	CHECK_FALSE(editor.hidden().contains(a));
	// Add the second block, select the members, undo the addition.
	Selection onlyB;
	onlyB.solids.push_back(b);
	editor.setSelection(onlyB);
	groups.findChild<QPushButton*>("visAdd")->click();
	settle();
	CHECK(visGroupMembers(editor.doc(), id).solids.size() == 2);
	groups.findChild<QPushButton*>("visSelect")->click();
	CHECK(editor.selection().solids.size() == 2);
	editor.undo();
	CHECK(visGroupMembers(editor.doc(), id).solids.size() == 1);
	// Hidden groups are saved as visgroupshown 0 and come back hidden.
	editor.setVisGroupVisible(id, false);
	const QString mapPath = home.filePath("level.vmf");
	QString error;
	REQUIRE(editor.saveMap(mapPath, &error));
	REQUIRE(editor.openMap(mapPath, &error));
	CHECK_FALSE(editor.visGroupVisible(id));
	CHECK_FALSE(editor.hidden().solids.empty());
	editor.setVisGroupVisible(id, true);

	// An instance next to the map: a block and a named light.
	MapDocument instance;
	auto [ci, block] = edit::createBlock(instance, box({0, 0, 0}, {64, 64, 32}), "DEV/DEV_MEASUREGENERIC01");
	(void)ci;
	(void)block;
	Entity lamp;
	lamp.id = instance.newEntityId();
	lamp.set("classname", "light");
	lamp.set("targetname", "lamp");
	lamp.setOrigin({32, 32, 48});
	instance.entities.push_back(lamp);
	QDir(home.path()).mkpath("instances");
	{
		QFile out(home.filePath("instances/box.vmf"));
		REQUIRE(out.open(QIODevice::WriteOnly));
		const auto text = instance.serialize();
		out.write(text.data(), static_cast<qint64>(text.size()));
	}
	Transaction t(editor.doc(), "Place instance");
	Entity func;
	func.id = editor.doc().newEntityId();
	func.set("classname", "func_instance");
	func.set("targetname", "box1");
	func.set("file", "instances/box");
	func.setOrigin({512, 0, 0});
	t.touchEntity(func.id);
	editor.doc().entities.push_back(func);
	editor.commit(t.finish());
	REQUIRE(editor.instancePreview(*editor.doc().findEntity(func.id)));
	// The view draws its content (read-only) where the func_instance stands.
	Bounds shown;
	for (int i = 0; i < 100 && shown.empty(); ++i) {
		window.view3D()->update();
		settle();
		shown = window.view3D()->instanceBounds(func.id);
	}
	REQUIRE_FALSE(shown.empty());
	CHECK(shown.min.x == doctest::Approx(512));
	CHECK(shown.max.x == doctest::Approx(576));

	// The properties show the instance; Collapse puts its content into the map.
	Selection onlyFunc;
	onlyFunc.entities.push_back(func.id);
	editor.setSelection(onlyFunc);
	window.raiseDock("Object Properties");
	settle();
	auto* instanceBox = window.properties()->findChild<QGroupBox*>("instanceBox");
	REQUIRE(instanceBox);
	CHECK(instanceBox->findChild<QLineEdit*>("instanceFile")->text() == "instances/box");
	auto* collapse = instanceBox->findChild<QPushButton*>("instanceCollapse");
	REQUIRE(collapse);
	REQUIRE(collapse->isEnabled());
	const auto solidsBefore = editor.doc().world.solids.size();
	collapse->click();
	settle();
	CHECK(editor.doc().world.solids.size() == solidsBefore + 1);
	CHECK_FALSE(editor.doc().findEntity(func.id));
	bool named = false;
	for (const auto& entity : editor.doc().entities) named = named || entity.get("targetname") == "box1-lamp";
	CHECK(named);
	editor.undo();
	CHECK(editor.doc().findEntity(func.id));
	CHECK(editor.doc().world.solids.size() == solidsBefore);
}

TEST_CASE("maps panel: map list with compile state, new map, batch compile; a new mod from the open map") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	REQUIRE(editor.openGame("sourcemods/mymod"));
	CompileRunner compiler;
	MapsPanel panel(&editor, &compiler);
	panel.resize(900, 600);
	panel.show();

	// The demo map is listed and was never compiled.
	const QString demo = fromPath(f.mod / "mapsrc/demo.vmf");
	REQUIRE(panel.maps().contains(demo));
	const QString gameDir = fromPath(f.mod);
	CHECK(bspState(demo, gameDir) == BspState::NotCompiled);
	// A BSP newer than the map: up to date; the map saved again afterwards: outdated.
	test::writeText(f.mod / "maps/demo.bsp", "VBSP fake map");
	QFile bsp(fromPath(f.mod / "maps/demo.bsp"));
	REQUIRE(bsp.open(QIODevice::ReadWrite));
	bsp.setFileTime(QFileInfo(demo).lastModified().addSecs(60), QFileDevice::FileModificationTime);
	bsp.close();
	CHECK(bspState(demo, gameDir) == BspState::UpToDate);
	QFile vmf(demo);
	REQUIRE(vmf.open(QIODevice::ReadWrite));
	vmf.setFileTime(QFileInfo(bsp).lastModified().addSecs(60), QFileDevice::FileModificationTime);
	vmf.close();
	CHECK(bspState(demo, gameDir) == BspState::Outdated);

	// New map: an empty map file with a sky, listed at once; bad names are refused.
	QString error;
	CHECK(panel.createMap("bad name", &error).isEmpty());
	CHECK_FALSE(error.isEmpty());
	const QString second = panel.createMap("second", &error);
	REQUIRE_FALSE(second.isEmpty());
	CHECK(panel.maps().contains(second));
	QFile secondFile(second);
	REQUIRE(secondFile.open(QIODevice::ReadOnly));
	const QByteArray secondText = secondFile.readAll();
	CHECK(secondText.contains("worldspawn"));
	CHECK(secondText.contains("skyname"));
	const QString copy = panel.duplicateMap(second, &error);
	CHECK(copy.endsWith("second_copy.vmf"));
	const QString renamed = panel.renameMap(copy, "third", &error);
	CHECK(renamed.endsWith("third.vmf"));
	CHECK_FALSE(QFileInfo::exists(copy));

	// Batch compile: the fixture's VBSP does not exist, so every map fails - one after another,
	// without hanging, and each failure is remembered.
	panel.setChecked(demo, true);
	panel.setChecked(second, true);
	CHECK(panel.checkedMaps().size() == 2);
	QSignalSpy started(&panel, &MapsPanel::compileStarted);
	QSignalSpy finished(&panel, &MapsPanel::batchFinished);
	REQUIRE(panel.compileMaps(panel.checkedMaps(), false));
	CHECK(started.count() == 1);
	CHECK_FALSE(panel.compileMaps({demo}, false));   // one batch at a time
	REQUIRE(finished.wait(10000));
	CHECK(finished.front()[0].toInt() == 0);
	CHECK(finished.front()[1].toInt() == 2);
	CHECK_FALSE(panel.batchRunning());
	CHECK(panel.lastResult(demo) == "failed");
	CHECK(panel.lastResult(second) == "failed");
	CHECK(panel.lastResult(renamed).isEmpty());
	CHECK((panel.deleteMap(renamed, &error) || !error.isEmpty()));   // the trash may be missing on a test machine

	// A new mod from the open map: created in sourcemods, found, opened, the map copied.
	REQUIRE(editor.openMap(demo, &error));
	ModTemplate mod{"Fresh Mod", "freshmod", ModBase::Mapbase, mapbaseMounts(f.steam / "steamapps/sourcemods")};
	const QString key = createModAndSwitch(&editor, mod, {demo}, &error);
	INFO(error.toStdString());
	REQUIRE(key == "sourcemods/freshmod");
	CHECK(editor.gameKey() == key);
	REQUIRE(editor.gameEntry());
	CHECK(editor.gameEntry()->usesMapbase);
	CHECK(QFileInfo::exists(fromPath(f.steam / "steamapps/sourcemods/freshmod/mapsrc/demo.vmf")));
	// The maps panel follows the game: it lists the new mod's map.
	CHECK(panel.mapFolder().contains("freshmod"));
	CHECK(panel.maps().size() == 1);
	// Not twice.
	CHECK(createModAndSwitch(&editor, mod, {}, &error).isEmpty());

	// The dialog: the title fills in the folder name, a taken folder is refused.
	NewModDialog dialog(&editor);
	dialog.findChild<QLineEdit*>("newModTitle")->setText("Fresh Mod");
	CHECK(dialog.findChild<QLineEdit*>("newModFolder")->text() == "fresh_mod");
	CHECK(dialog.modTemplate().folder == "fresh_mod");
	dialog.findChild<QLineEdit*>("newModFolder")->setText("freshmod");
	emit dialog.findChild<QLineEdit*>("newModFolder")->textEdited("freshmod");
	QPushButton* create = nullptr;
	for (auto* b : dialog.findChildren<QPushButton*>()) {
		if (b->text() == "Create mod") create = b;
	}
	REQUIRE(create);
	CHECK_FALSE(create->isEnabled());
}

TEST_CASE("models page: folders, search, 3D view with skins and body groups, place in map") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();

	// The page lists every model at once (no empty page while a game is loaded).
	window.showPage(MainWindow::Page::Models);
	CHECK(window.currentPage() == MainWindow::Page::Models);
	auto* page = window.modelsPage();
	REQUIRE(page);
	for (int i = 0; i < 200 && page->list()->model()->rowCount() == 0; ++i) QTest::qWait(50);
	const int all = page->list()->model()->rowCount();
	CHECK(all >= 5);
	// Folders with their counts; picking one shows only its models.
	QTreeWidgetItem* demoFolder = nullptr;
	for (QTreeWidgetItemIterator it(page->folders()); *it; ++it) {
		if ((*it)->text(0) == "props_demo") demoFolder = *it;
	}
	REQUIRE(demoFolder);
	CHECK(demoFolder->text(1) == "4");
	page->folders()->setCurrentItem(demoFolder);
	CHECK(page->list()->model()->rowCount() == 4);
	page->search()->setText("supply");
	CHECK(page->list()->model()->rowCount() == 1);
	page->search()->clear();
	page->folders()->setCurrentItem(page->folders()->topLevelItem(0));
	CHECK(page->list()->model()->rowCount() == all);

	// A model in 3D with its skins, body groups and what it is made of.
	const QString crate = "models/props_demo/supply_crate.mdl";
	page->selectModel(crate);
	CHECK(page->currentModel() == crate);
	auto* viewer = page->viewer();
	for (int i = 0; i < 200 && !viewer->model(); ++i) QTest::qWait(20);
	REQUIRE(viewer->model());
	CHECK(viewer->model()->triangleCount() == 24);   // crate and closed lid
	CHECK(page->info().skinCount == 2);
	CHECK(page->info().staticProp);
	CHECK(page->skins()->count() == 2);
	REQUIRE(page->bodyGroupBoxes().size() == 1);
	CHECK(page->bodyGroupBoxes()[0]->objectName() == "bodygroup:lid");
	CHECK(page->placeClass() == "prop_static");
	REQUIRE(page->materialList()->count() == 1);
	CHECK(page->materialList()->item(0)->text() == "demo/wood001");
	const QString details = page->findChild<QLabel*>("modelDetails")->text();
	CHECK(details.contains("42 x 30 x 30"));
	CHECK(details.contains("Wooden.Medium"));
	CHECK(details.contains("40.0 kg"));
	// The view draws the model (not only the background).
	settle();
	const QImage shot = viewer->grabFramebuffer();
	const QColor background = shot.pixelColor(2, 2);
	const QColor middle = shot.pixelColor(shot.width() / 2, shot.height() / 2);
	CHECK(std::abs(middle.red() - background.red()) + std::abs(middle.green() - background.green()) + std::abs(middle.blue() - background.blue()) > 30);
	// Turning keeps the model; the wireframe goes on top.
	const float yaw = viewer->yaw();
	const QPoint c = viewer->rect().center();
	QTest::mousePress(viewer, Qt::LeftButton, {}, c);
	QTest::mouseMove(viewer, c + QPoint(40, 0));
	QTest::mouseRelease(viewer, Qt::LeftButton, {}, c + QPoint(40, 0));
	CHECK(viewer->yaw() != doctest::Approx(yaw));
	page->findChild<QCheckBox*>("modelWireframe")->setChecked(true);
	CHECK(viewer->wireframe());
	// Lid off (body group "nothing") and the metal skin load in the background.
	page->bodyGroupBoxes()[0]->setCurrentIndex(1);
	for (int i = 0; i < 200 && viewer->model()->triangleCount() != 12; ++i) QTest::qWait(20);
	CHECK(viewer->model()->triangleCount() == 12);
	page->skins()->setCurrentIndex(1);
	for (int i = 0; i < 200 && page->materialList()->item(0)->text() != "demo/metal001"; ++i) QTest::qWait(20);
	CHECK(page->materialList()->item(0)->text() == "demo/metal001");

	// Place in map: arms the entity tool with the model and its skin and shows the map.
	page->findChild<QPushButton*>("modelPlace")->click();
	CHECK(window.currentPage() == MainWindow::Page::Map);
	CHECK(editor.tool() == Editor::Tool::Entity);
	CHECK(editor.currentEntityClass() == "prop_static");
	const auto& keys = editor.currentEntityKeys();
	CHECK(std::find(keys.begin(), keys.end(), std::pair<std::string, std::string>{"model", crate.toStdString()}) != keys.end());
	CHECK(std::find(keys.begin(), keys.end(), std::pair<std::string, std::string>{"skin", "1"}) != keys.end());
	CHECK(std::none_of(keys.begin(), keys.end(), [](const auto& kv) { return kv.first == "body"; }));   // prop_static has none
	Viewport* view = window.view3D();
	view->camera().position = QVector3D(0, -500, 300);
	view->camera().yaw = 90;
	view->camera().pitch = -30;
	view->update();
	settle();
	const std::size_t before = editor.doc().entities.size();
	QTest::mouseMove(view, view->rect().center());
	settle();
	QTest::mouseClick(view, Qt::LeftButton, {}, view->rect().center());
	settle();
	REQUIRE(editor.doc().entities.size() == before + 1);
	const auto& placed = editor.doc().entities.back();
	CHECK(placed.get("classname") == "prop_static");
	CHECK(placed.get("model") == crate.toStdString());
	CHECK(placed.get("skin") == "1");
	// Another class from the Tool panel arms it without the model.
	editor.setCurrentEntityClass("info_player_start");
	CHECK(editor.currentEntityKeys().empty());

	// prop_dynamic keeps the body group.
	window.showPage(MainWindow::Page::Models);
	page->findChild<QComboBox*>("modelPlaceAs")->setCurrentText("prop_dynamic");
	const auto dynamicKeys = page->placeKeys();
	CHECK(std::find(dynamicKeys.begin(), dynamicKeys.end(), std::pair<std::string, std::string>{"body", "1"}) != dynamicKeys.end());

	// A material of the model opens in the material editor.
	auto* materials = page->materialList();
	QTest::mouseClick(materials->viewport(), Qt::LeftButton, {}, materials->visualItemRect(materials->item(0)).center());
	CHECK(window.currentPage() == MainWindow::Page::Materials);

	// Dragging a model onto the Map button opens the map, so the drag goes on into a view.
	window.showPage(MainWindow::Page::Models);
	QWidget* mapButton = nullptr;
	for (auto* a : window.findChildren<QAction*>()) {
		if (a->objectName() == "page:Map") {
			for (auto* w : a->associatedObjects()) {
				if (auto* button = qobject_cast<QToolButton*>(w)) mapButton = button;
			}
		}
	}
	REQUIRE(mapButton);
	QMimeData mime;
	mime.setData("application/x-sme-model", crate.toUtf8());
	QDragEnterEvent enter(mapButton->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(mapButton, &enter);
	CHECK(window.currentPage() == MainWindow::Page::Map);
	CHECK(enter.isAccepted());
}

TEST_CASE("show in ...: right-click menus lead to the asset in the browsers and pages, find in map") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	QString error;
	REQUIRE(editor.openMap(fromPath(f.mod / "mapsrc/demo.vmf"), &error));
	REQUIRE((indexed.count() > 0 || indexed.wait(10000)));
	settle();

	const auto action = [](QMenu* menu, const QString& name) -> QAction* {
		for (auto* a : menu->findChildren<QAction*>()) {
			if (a->objectName() == name) return a;
		}
		return nullptr;
	};

	// A face of the brick wall: its material in the Asset Browser, then everything that uses it.
	FaceRef brick;
	for (const auto& solid : editor.doc().world.solids) {
		for (const auto& side : solid.sides) {
			if (normalizedMaterial(side.material) == "demo/brickwall001" && brick.sideId == 0) brick = {{0, solid.id}, side.id};
		}
	}
	REQUIRE(brick.sideId != 0);
	editor.setSelectMode(Editor::SelectMode::Face);
	Selection faceSelection;
	faceSelection.faces.push_back(brick);
	editor.setSelection(faceSelection);
	settle();
	{
		std::unique_ptr<QMenu> menu(window.buildContextMenu());
		QAction* show = action(menu.get(), "showInAssetBrowser");
		REQUIRE(show);
		CHECK(action(menu.get(), "showInPage"));
		CHECK(action(menu.get(), "findInMap"));
		show->trigger();
		settle();
		CHECK(window.currentPage() == MainWindow::Page::Map);
		auto* list = window.assets()->findChild<QListView*>("materialList");
		REQUIRE(list);
		CHECK(list->currentIndex().data(Qt::UserRole).toString().compare("demo/brickwall001", Qt::CaseInsensitive) == 0);
		CHECK(list->isVisible());
	}
	{
		std::unique_ptr<QMenu> menu(window.buildContextMenu());
		QAction* find = action(menu.get(), "findInMap");
		REQUIRE(find);
		find->trigger();
		settle();
		const auto expected = objectsUsing(editor.doc(), {AssetKind::Material, "demo/brickwall001"}, editor.entities());
		CHECK(editor.selection().faces.size() == expected.faces.size());
		CHECK(editor.selection().faces.size() > 1);
		CHECK(editor.selectMode() == Editor::SelectMode::Face);
	}
	{
		// The face's material field in Object Properties has the same entries.
		editor.setSelection(faceSelection);
		settle();
		auto* field = window.findChild<QLineEdit*>("faceMaterial");
		REQUIRE(field);
		std::unique_ptr<QMenu> menu(window.findChild<PropertiesPanel*>()->assetFieldMenu(field, AssetKind::Material));
		CHECK(action(menu.get(), "showInAssetBrowser"));
		QAction* edit = action(menu.get(), "showInPage");
		REQUIRE(edit);
		edit->trigger();
		settle();
		CHECK(window.currentPage() == MainWindow::Page::Materials);
		CHECK(window.materialEditor()->materialName().compare("demo/brickwall001", Qt::CaseInsensitive) == 0);
		window.showPage(MainWindow::Page::Map);
		settle();
	}

	// A prop: its model on the Models page (and no Asset Browser entry where it already is).
	editor.setSelectMode(Editor::SelectMode::Object);
	const auto entityOf = [&](const std::string& cls) {
		for (const auto& entity : editor.doc().entities) {
			if (entity.classname() == cls) return entity.id;
		}
		return 0;
	};
	const int prop = entityOf("prop_static");
	REQUIRE(prop);
	const std::string model = editor.doc().findEntity(prop)->get("model");
	Selection propSelection;
	propSelection.entities.push_back(prop);
	editor.setSelection(propSelection);
	settle();
	{
		std::unique_ptr<QMenu> menu(window.buildContextMenu());
		QAction* show = action(menu.get(), "showInPage");
		REQUIRE(show);
		CHECK(show->text().contains("Models page"));
		show->trigger();
		settle();
		CHECK(window.currentPage() == MainWindow::Page::Models);
		CHECK(window.modelsPage()->currentModel().compare(QString::fromStdString(model), Qt::CaseInsensitive) == 0);
		window.showPage(MainWindow::Page::Map);
		settle();
		CHECK(window.revealAsset({AssetKind::Model, model}, MainWindow::RevealIn::AssetBrowser));
		CHECK(window.assets()->modelView()->currentIndex().data(Qt::UserRole).toString().compare(QString::fromStdString(model), Qt::CaseInsensitive) == 0);
		QMenu fromBrowser;
		window.fillAssetMenu(&fromBrowser, {AssetKind::Model, model}, AssetPlace::AssetBrowser);
		CHECK_FALSE(action(&fromBrowser, "showInAssetBrowser"));
		CHECK(action(&fromBrowser, "showInPage"));
		QMenu fromPage;
		window.fillAssetMenu(&fromPage, {AssetKind::Model, model}, AssetPlace::ModelsPage);
		CHECK(action(&fromPage, "showInAssetBrowser"));
		CHECK_FALSE(action(&fromPage, "showInPage"));
		// Find in map: every prop with this model.
		CHECK(window.findInMap({AssetKind::Model, model}) == int(objectsUsing(editor.doc(), {AssetKind::Model, model}, editor.entities()).entities.size()));
		CHECK(editor.selection().entities.size() >= 2);
	}

	// A sound entity: its sound on the Sounds page.
	const int ambient = entityOf("ambient_generic");
	REQUIRE(ambient);
	Selection soundSelection;
	soundSelection.entities.push_back(ambient);
	editor.setSelection(soundSelection);
	settle();
	{
		std::unique_ptr<QMenu> menu(window.buildContextMenu());
		QAction* show = action(menu.get(), "showInPage");
		REQUIRE(show);
		CHECK(show->text().contains("Sounds page"));
		show->trigger();
		settle();
		CHECK(window.currentPage() == MainWindow::Page::Sounds);
		REQUIRE(window.sounds()->tree()->currentItem());
		CHECK(window.sounds()->tree()->currentItem()->text(0).contains("Demo.Wind"));
		window.showPage(MainWindow::Page::Map);
		settle();
	}
}

TEST_CASE("menu editor: title, a new chapter and a menu entry go into the mod's files") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	test::writeText(f.mod / "maps/demo.bsp", "VBSP fake map");
	Editor editor;
	editor.discover(fromPath(f.steam));
	REQUIRE(editor.openGame("sourcemods/mymod"));
	const std::string gameinfoBefore = readFileText(f.mod / "gameinfo.txt").value_or(std::string());
	REQUIRE_FALSE(gameinfoBefore.empty());

	MenuEditor menu(&editor);
	menu.resize(1200, 800);
	menu.show();
	QApplication::processEvents();
	// The mod has no GameMenu.res: the default menu is shown (main menu without the in-game entries).
	CHECK_FALSE(menu.menu().menuFromMod);
	CHECK(menu.preview()->entries().contains("New Game"));
	CHECK_FALSE(menu.preview()->entries().contains("Resume Game"));
	menu.findChild<QCheckBox*>("menuInGame")->setChecked(true);
	CHECK(menu.preview()->entries().contains("Resume Game"));

	// The title, typed like a user does.
	auto* title = menu.findChild<QLineEdit*>("menuInfo_title");
	REQUIRE(title);
	title->selectAll();
	QTest::keyClicks(title, "DEMO STORY");
	CHECK(menu.menu().info.at("title") == "DEMO STORY");

	// A chapter starting the demo map, with a title.
	menu.findChild<QPushButton*>("menuAddChapter")->click();
	QApplication::processEvents();
	REQUIRE(menu.menu().chapters.size() == 1);
	auto* map = menu.findChild<QComboBox*>("chapterMap");
	REQUIRE(map);
	CHECK(map->findText("demo") >= 0);   // offered from maps/ and mapsrc/
	map->setCurrentText("demo");
	auto* chapterTitle = menu.findChild<QLineEdit*>("chapterTitle");
	REQUIRE(chapterTitle);
	chapterTitle->clear();
	QTest::keyClicks(chapterTitle, "THE BEGINNING");

	// A menu entry that starts the map directly, moved to the top.
	menu.findChild<QPushButton*>("menuAddItem")->click();
	QApplication::processEvents();
	auto labels = menu.findChildren<QLineEdit*>("itemLabel");
	REQUIRE_FALSE(labels.isEmpty());
	labels.back()->clear();
	QTest::keyClicks(labels.back(), "Play the demo");
	menu.findChildren<QComboBox*>("itemCommand").back()->setCurrentText("engine map demo");
	CHECK(menu.preview()->entries().contains("Play the demo"));

	QString error;
	REQUIRE(menu.save(&error));
	CHECK(error.isEmpty());
	// The files in the mod.
	const std::string gameinfo = readFileText(f.mod / "gameinfo.txt").value_or(std::string());
	CHECK(gameInfoValue(gameinfo, "title") == "DEMO STORY");
	CHECK(gameinfo.find("SearchPaths") != std::string::npos);   // the rest stayed
	CHECK(readFileText(f.mod / "gameinfo.txt.bak").value_or(std::string()) == gameinfoBefore);
	CHECK(chapterMap(readFileText(f.mod / "cfg/chapter1.cfg").value_or(std::string())) == "demo");
	const auto locText = readFileText(f.mod / "resource/mymod_english.txt").value_or(std::string());
	REQUIRE(locText.size() > 2);
	CHECK(static_cast<unsigned char>(locText[0]) == 0xFF);   // UTF-16 LE with BOM
	std::vector<std::byte> locBytes(locText.size());
	std::transform(locText.begin(), locText.end(), locBytes.begin(), [](char c) { return static_cast<std::byte>(c); });
	CHECK(parseLocalization(decodeTextFile(locBytes)).get("MYMOD_Chapter1_Title") == "THE BEGINNING");
	const auto items = parseGameMenu(readFileText(f.mod / "resource/GameMenu.res").value_or(std::string()));
	REQUIRE_FALSE(items.empty());
	CHECK(items.back().label == "Play the demo");
	CHECK(items.back().command == "engine map demo");

	// Reading again gives the same.
	menu.reload();
	CHECK(menu.menu().menuFromMod);
	REQUIRE(menu.menu().chapters.size() == 1);
	CHECK(menu.menu().chapters[0].title == "THE BEGINNING");
	CHECK(menu.findChild<QLineEdit*>("menuInfo_title")->text() == "DEMO STORY");
	QApplication::processEvents();   // the rebuilt cards show on the next round of events
	CHECK(menu.findChildren<QFrame*>("chapterCard").size() == 1);
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		menu.grab().save(QDir(shots).filePath("menu_editor.png"));
	}
}

TEST_CASE("decals and overlays: placed on a face with the current material, drawn on the brushes, picked") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();

	auto [made, ref] = edit::createBlock(editor.doc(), box({0, 0, 0}, {256, 256, 64}), "DEMO/CONCRETEWALL001");
	editor.commit(std::move(made));
	const auto topSideId = [&] {
		const Solid* solid = editor.doc().findSolid(ref);
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			if (face.normal.z > 0.9) return solid->sides[static_cast<std::size_t>(face.sideIndex)].id;
		}
		return 0;
	}();
	REQUIRE(topSideId != 0);
	Viewport* view = window.view3D();
	view->camera().position = QVector3D(128, -220, 360);
	view->camera().yaw = 90;
	view->camera().pitch = -55;
	view->update();
	settle();

	const auto findClass = [&](const char* classname) -> const Entity* {
		for (const auto& entity : editor.doc().entities) {
			if (entity.classname() == classname) return &entity;
		}
		return nullptr;
	};
	// The arrow material (128 x 128, $decalscale 0.5) loads first, so sizes are known.
	editor.setCurrentMaterial("DECALS/DEMO_ARROW");
	editor.setTool(Editor::Tool::Entity);
	editor.setCurrentEntityClass("info_overlay");
	QTest::mouseMove(view, screen(view, {128, 128, 64}));
	for (int i = 0; i < 20; ++i) settle();
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {128, 128, 64}));
	settle();
	const Entity* overlay = findClass("info_overlay");
	REQUIRE(overlay);
	const int overlayId = overlay->id;
	CHECK(overlay->get("material") == "decals/demo_arrow");
	CHECK(overlay->get("sides") == std::to_string(topSideId));
	CHECK(overlay->get("BasisNormal") == "0 0 1");
	CHECK(overlay->get("BasisU") == "1 0 0");
	CHECK(overlay->origin().z == doctest::Approx(64));
	Vec3 corner;
	REQUIRE(parseVec3(overlay->get("uv2"), corner));
	CHECK(corner.x == doctest::Approx(32));   // half of 128 x 0.5
	view->update();
	settle();
	CHECK(view->renderer().overlayPolygonCount() == 2);

	// An infodecal on the same face: drawn with the texture's size.
	editor.setCurrentEntityClass("infodecal");
	QTest::mouseMove(view, screen(view, {48, 200, 64}));
	settle();
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {48, 200, 64}));
	settle();
	const Entity* decal = findClass("infodecal");
	REQUIRE(decal);
	CHECK(decal->get("texture") == "decals/demo_arrow");
	CHECK(decal->origin().z == doctest::Approx(64));
	view->update();
	settle();
	CHECK(view->renderer().decalPolygonCount() == 1);
	CHECK(view->renderer().overlayPolygonCount() == 2);

	// Clicking the drawn overlay (away from its entity box) selects the overlay.
	editor.setTool(Editor::Tool::Select);
	editor.clearSelection();
	settle();
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {152, 140, 64}));
	settle();
	CHECK(editor.selection().entities == std::vector<int>{overlayId});

	// Undo takes the decal away again.
	editor.undo();
	view->update();
	settle();
	CHECK(view->renderer().decalPolygonCount() == 0);
	CHECK(view->renderer().overlayPolygonCount() == 2);
}

TEST_CASE("cubemap reflections: env_cubemaps drawn into the preview, $envmap faces reflect the nearest one") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	// A shiny floor: the demo's metal with a full reflection.
	test::writeText(f.mod / "materials/demo/shiny_floor.vmt",
	                "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"demo/metal001\"\n\t\"$envmap\" \"env_cubemap\"\n\t\"$envmaptint\" \"[1 1 1]\"\n}\n");
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	editor.setRenderMode(Editor::RenderMode::Lit);
	CHECK(editor.reflections());   // on by default
	settle();

	// A room: the shiny floor and a bright wall it can reflect.
	auto [floorMade, floorRef] = edit::createBlock(editor.doc(), box({0, 0, 0}, {512, 512, 16}), "DEMO/SHINY_FLOOR");
	editor.commit(std::move(floorMade));
	auto [wallMade, wallRef] = edit::createBlock(editor.doc(), box({0, 480, 16}, {512, 512, 256}), "DEMO/BRICKWALL001");
	editor.commit(std::move(wallMade));
	const auto topSideId = [&] {
		const Solid* solid = editor.doc().findSolid(floorRef);
		for (const auto& face : buildBrushGeometry(*solid).faces) {
			if (face.normal.z > 0.9) return solid->sides[static_cast<std::size_t>(face.sideIndex)].id;
		}
		return 0;
	}();
	REQUIRE(topSideId != 0);
	Viewport* view = window.view3D();
	view->camera().position = QVector3D(256, 40, 120);
	view->camera().yaw = 90;
	view->camera().pitch = -30;
	view->update();
	for (int i = 0; i < 10; ++i) settle();
	CHECK(view->renderer().cubemapCount() == 0);
	CHECK(view->renderer().cubemapOfSide(topSideId) == -1);
	const QPoint floorPoint = screen(view, {256, 300, 16});
	const QColor withSky = view->grabFramebuffer().pixelColor(floorPoint);   // no cubemap yet: the default reflection

	// An env_cubemap above the floor: drawn a moment later, a few faces per frame.
	const auto* definition = editor.entities() ? editor.entities()->find("env_cubemap") : nullptr;
	auto [placed, cubemapId] = edit::placeEntity(editor.doc(), "env_cubemap", {256, 256, 96}, definition, {});
	editor.commit(std::move(placed));
	view->update();
	settle();
	CHECK(view->renderer().cubemapCount() == 1);
	CHECK(view->renderer().cubemapOfSide(topSideId) == 0);
	for (int i = 0; i < 100 && view->renderer().builtCubemapCount() < 1; ++i) {
		QTest::qWait(30);
	}
	CHECK(view->renderer().builtCubemapCount() == 1);
	CHECK_FALSE(view->renderer().cubemapWorkPending());
	settle();
	const QColor reflected = view->grabFramebuffer().pixelColor(floorPoint);
	CHECK(reflected != withSky);   // the floor shows the room now, not the default picture

	// The switch in the view's corner menu turns the reflection off (the floor gets darker).
	editor.setReflections(false);
	CHECK_FALSE(QSettings().value("editor/reflections", true).toBool());
	view->update();
	settle();
	const QColor plain = view->grabFramebuffer().pixelColor(floorPoint);
	CHECK(plain.lightness() < reflected.lightness());
	editor.setReflections(true);
	settle();

	// Map > Build cubemaps draws it again; a selected env_cubemap shows its preview sphere.
	auto* build = window.findChild<QAction*>("buildCubemaps");
	REQUIRE(build);
	build->trigger();
	CHECK(view->renderer().cubemapWorkPending());
	for (int i = 0; i < 100 && view->renderer().cubemapWorkPending(); ++i) {
		QTest::qWait(30);
	}
	CHECK_FALSE(view->renderer().cubemapWorkPending());
	const QPoint spherePoint = screen(view, {256, 256, 96});
	const QColor beforeSelect = view->grabFramebuffer().pixelColor(spherePoint);
	Selection selection;
	selection.entities.push_back(cubemapId);
	editor.setSelection(selection);
	view->update();
	settle();
	const QImage shot = view->grabFramebuffer();
	CHECK(shot.pixelColor(spherePoint) != beforeSelect);
	shot.save(qEnvironmentVariable("SME_TEST_SHOTS", QDir::tempPath()) + "/cubemap_reflections.png");
}

TEST_CASE("texts: the mod's localization and captions, edited, translated, saved and compiled") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	window.showPage(MainWindow::Page::Project);
	window.projectTabs()->setCurrentWidget(window.textsPanel());
	settle();
	auto* texts = window.textsPanel();
	REQUIRE(texts);
	CHECK(texts->isVisible());
	CHECK(window.projectTabs()->tabText(window.projectTabs()->indexOf(texts)) == "Texts");
	CHECK(window.projectTabs()->indexOf(texts) == window.projectTabs()->indexOf(window.findChild<MenuEditor*>()) + 1);

	// The mod's own texts first: English and German, a row per token, the missing German one marked.
	CHECK(texts->languages() == QStringList{"english", "german"});
	CHECK_FALSE(texts->modified());
	auto* table = texts->table();
	REQUIRE(table->rowCount() == 3);
	CHECK(table->item(0, 0)->text() == "MyMod_Hint_Flashlight");
	CHECK(table->item(0, 2)->text() == QString::fromUtf8("Drücke F für die Taschenlampe"));
	CHECK(table->item(1, 2)->text().isEmpty());
	CHECK(table->item(1, 2)->background().color() != table->item(0, 2)->background().color());
	texts->findChild<QLineEdit*>("textsSearch")->setText("welcome");
	CHECK_FALSE(table->isRowHidden(1));
	CHECK(table->isRowHidden(0));
	texts->findChild<QLineEdit*>("textsSearch")->clear();
	texts->findChild<QCheckBox*>("textsOnlyMissing")->setChecked(true);
	CHECK(table->isRowHidden(0));
	CHECK_FALSE(table->isRowHidden(1));
	texts->findChild<QCheckBox*>("textsOnlyMissing")->setChecked(false);

	// Edit in the table and in the field below it; translate; a chapter title shows where the menu uses it.
	table->item(1, 1)->setText("Welcome!");
	CHECK(texts->text("MyMod_Sign_Welcome") == "Welcome!");
	CHECK(texts->modified());
	table->setCurrentCell(1, 2);
	texts->editorField()->setPlainText("Willkommen!");
	CHECK(texts->text("MyMod_Sign_Welcome", "german") == "Willkommen!");
	CHECK(table->item(1, 2)->text() == "Willkommen!");
	texts->setText("MYMOD_Chapter1_Title", "english", "THE BEGINNING");
	REQUIRE(table->rowCount() == 4);
	CHECK(table->item(3, table->columnCount() - 1)->text().contains("chapter 1"));
	texts->addLanguage("french");
	CHECK(texts->languages() == QStringList{"english", "french", "german"});
	REQUIRE(texts->save());
	CHECK_FALSE(texts->modified());

	// On disk: UTF-16 with BOM, the condition kept, the new language its own file.
	const auto bytesOf = [](const std::filesystem::path& path) {
		const std::string text = readFileText(path).value_or(std::string());
		std::vector<std::byte> bytes(text.size());
		std::transform(text.begin(), text.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
		return bytes;
	};
	const auto english = bytesOf(f.mod / "resource/mymod_english.txt");
	REQUIRE(english.size() > 2);
	CHECK(static_cast<unsigned char>(english[0]) == 0xFF);
	CHECK(static_cast<unsigned char>(english[1]) == 0xFE);
	const auto englishFile = parseLocalizationFile(decodeTextFile(english));
	CHECK(englishFile.get("MyMod_Sign_Welcome") == "Welcome!");
	CHECK(englishFile.get("MYMOD_Chapter1_Title") == "THE BEGINNING");
	REQUIRE(englishFile.find("MyMod_Hint_Pad") >= 0);
	CHECK(englishFile.tokens[std::size_t(englishFile.find("MyMod_Hint_Pad"))].condition == "[$X360]");
	CHECK(englishFile.tokens[0].comments == std::vector<std::string>{"// Hints"});
	CHECK(parseLocalizationFile(decodeTextFile(bytesOf(f.mod / "resource/mymod_german.txt"))).get("MyMod_Sign_Welcome") == "Willkommen!");
	const auto french = bytesOf(f.mod / "resource/mymod_french.txt");
	REQUIRE(french.size() > 2);
	CHECK(static_cast<unsigned char>(french[0]) == 0xFF);
	CHECK(parseLocalizationFile(decodeTextFile(french)).language == "French");
	// The Menu tab sees the chapter title the Texts tab wrote.
	CHECK(loadModMenu(f.mod).localization.get("MYMOD_Chapter1_Title") == "THE BEGINNING");
	texts->reload();
	CHECK(texts->languages() == QStringList{"english", "french", "german"});
	CHECK(texts->text("MyMod_Sign_Welcome") == "Welcome!");

	// Captions: the preview shows the colour and the tags; compiling writes the .dat.
	REQUIRE(texts->showSet("closecaption"));
	CHECK(texts->findChild<QPushButton*>("textsCompile")->isVisible());
	REQUIRE(table->rowCount() == 2);
	table->setCurrentCell(0, 1);
	settle();
	CHECK(texts->preview()->text().contains("rgb(255,90,80)"));
	CHECK(texts->preview()->text().contains("sound effect"));
	table->setCurrentCell(1, 1);
	CHECK(texts->preview()->text().contains("<i>"));
	REQUIRE(texts->compileCaptions());
	const auto dat = bytesOf(f.mod / "resource/closecaption_english.dat");
	REQUIRE(dat.size() == 512 + 8192);
	CHECK(std::string(reinterpret_cast<const char*>(dat.data()), 4) == "VCCD");
	CHECK(static_cast<unsigned char>(dat[16]) == 2);   // two captions in the directory
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		window.grab().save(QDir(shots).filePath("texts.png"));
	}
}

TEST_CASE("game enhancements: on by default, off for one model or the whole mod") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);   // mapbase-fidelity installed, a glass bottle in the base game
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	auto* enhancements = editor.enhancements();
	REQUIRE(enhancements);
	REQUIRE(enhancements->available());
	const auto waitIdle = [&] {
		for (int i = 0; i < 400 && (enhancements->busy() || !enhancements->ready()); ++i) QTest::qWait(25);
		settle();
	};
	waitIdle();
	REQUIRE(enhancements->ready());
	const QString bottle = "materials/models/props_junk/glassbottle01a.vmt";
	const QString model = "models/props_junk/glassbottle01a.mdl";
	const auto bottleText = [&] { return editor.game()->files.readText(bottle.toStdString()).value_or(""); };
	const auto modFile = f.mod / "materials/models/props_junk/glassbottle01a.vmt";

	// On by default: the game's bottle got its liquid material in the mod, as deploy.bat writes it.
	CHECK(std::filesystem::exists(modFile));
	CHECK(bottleText().starts_with(DEPLOY_MARKER));
	CHECK(effectIsOn(GameEffect::Liquid, bottleText()));
	CHECK(enhancements->candidateCount(GameEffect::Liquid) == 1);
	CHECK(enhancements->activeCount(GameEffect::Liquid) == 1);
	CHECK(enhancements->effectsFor(model) == std::vector<GameEffect>{GameEffect::Liquid});
	CHECK(enhancements->effectsFor("models/props_demo/crate01.mdl").empty());
	// The demo's water and bottle001 are the mod's own materials: untouched.
	CHECK_FALSE(isGeneratedMaterial(editor.game()->files.readText("materials/demo/water001.vmt").value_or("")));

	// Project > Overview: a switch per effect, the counts, the status.
	window.showPage(MainWindow::Page::Project);
	window.projectTabs()->setCurrentWidget(window.modPage());
	settle();
	auto* section = window.modPage()->findChild<QGroupBox*>("enhancementsSection");
	REQUIRE(section);
	CHECK(section->isVisible());
	auto* liquidMod = section->findChild<QCheckBox*>("enhancementMod:liquid");
	REQUIRE(liquidMod);
	CHECK(liquidMod->isChecked());
	CHECK(section->findChild<QCheckBox*>("enhancementMod:wind")->isChecked());
	auto* status = section->findChild<QLabel*>("enhancementStatus");
	REQUIRE(status);
	CHECK(status->text().startsWith("Up to date: 1 material"));
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		window.grab().save(QDir(shots).filePath("enhancements.png"));
	}

	// One prop: its switch in Object Properties; off leaves the model out and the original comes back.
	window.showPage(MainWindow::Page::Map);
	auto [placed, propId] = edit::placeEntity(editor.doc(), "prop_physics", {0, 0, 0}, editor.entities()->find("prop_physics"),
	                                          {{"model", model.toStdString()}});
	editor.commit(std::move(placed));
	Selection propSel;
	propSel.entities.push_back(propId);
	editor.setSelection(propSel);
	window.raiseDock("Object Properties");
	settle();
	auto* switches = window.properties()->findChild<EnhancementSwitches*>("enhancementSwitches");
	REQUIRE(switches);
	CHECK(switches->isVisible());
	auto* liquidProp = switches->findChild<QCheckBox*>("enhancement:liquid");
	REQUIRE(liquidProp);
	CHECK(liquidProp->isChecked());
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		window.properties()->grab().save(QDir(shots).filePath("enhancements_prop.png"));
	}
	QTest::mouseClick(liquidProp, Qt::LeftButton);
	waitIdle();
	CHECK(enhancements->settings().isExcluded(GameEffect::Liquid, model.toStdString()));
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, bottleText()));
	CHECK(bottleText().starts_with(std::string(MAPSMITH_MARKER) + " off (liquid)"));
	CHECK(loadEnhancementSettings(f.mod).isExcluded(GameEffect::Liquid, model.toStdString()));   // kept in the mod
	liquidProp = window.properties()->findChild<QCheckBox*>("enhancement:liquid");
	REQUIRE(liquidProp);
	CHECK_FALSE(liquidProp->isChecked());

	// The Models page shows the same state.
	window.showPage(MainWindow::Page::Models);
	window.modelsPage()->selectModel(model);
	settle();
	auto* pageSwitch = window.modelsPage()->findChild<QCheckBox*>("enhancement:liquid");
	REQUIRE(pageSwitch);
	CHECK(pageSwitch->isVisibleTo(window.modelsPage()));
	CHECK_FALSE(pageSwitch->isChecked());

	// The Overview lists it; "Remove from list" brings the effect back.
	window.showPage(MainWindow::Page::Project);
	settle();
	auto* excluded = section->findChild<QTreeWidget*>("enhancementExcluded");
	REQUIRE(excluded);
	REQUIRE(excluded->topLevelItemCount() == 1);
	CHECK(excluded->topLevelItem(0)->text(0) == model);
	excluded->topLevelItem(0)->setSelected(true);
	QTest::mouseClick(section->findChild<QPushButton*>("enhancementRemove"), Qt::LeftButton);
	waitIdle();
	CHECK(enhancements->settings().excluded.empty());
	CHECK(effectIsOn(GameEffect::Liquid, bottleText()));
	pageSwitch = window.modelsPage()->findChild<QCheckBox*>("enhancement:liquid");
	REQUIRE(pageSwitch);
	CHECK(pageSwitch->isChecked());

	// Off for the whole mod, and on again.
	QTest::mouseClick(liquidMod, Qt::LeftButton);
	waitIdle();
	CHECK_FALSE(enhancements->settings().enabled(GameEffect::Liquid));
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, bottleText()));
	CHECK(status->text().startsWith("Up to date: 0 material"));
	QTest::mouseClick(liquidMod, Qt::LeftButton);
	waitIdle();
	CHECK(effectIsOn(GameEffect::Liquid, bottleText()));
	CHECK(loadEnhancementSettings(f.mod) == EnhancementSettings{});

	// A material of the mod's own (no marker) is never overwritten, only reported.
	const std::string own = "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"demo/glass001\"\n}\n";
	test::writeText(modFile, own);
	editor.game()->files.noteWrittenFile(modFile);
	QTest::mouseClick(liquidMod, Qt::LeftButton);
	waitIdle();
	CHECK(bottleText() == own);
	CHECK(enhancements->keptOwn() == QStringList{"models/props_junk/glassbottle01a"});
	CHECK(status->text().contains("1 kept (your own material)"));
	std::filesystem::remove(modFile);
	editor.game()->files.noteRemovedFile(modFile);

	// Walls of the open map get their relief from the texture's pattern - in the game too: a
	// brick wall of the base game (the demo's bricks) on a block, a moment after the edit.
	const auto baseGame = f.sdkBase / "hl2";   // a folder the mod mounts (the base game's own files)
	std::filesystem::create_directories(baseGame / "materials/brick");
	std::filesystem::copy_file(f.mod / "materials/demo/brickwall001.vtf", baseGame / "materials/brick/testwall.vtf",
	                           std::filesystem::copy_options::overwrite_existing);
	test::writeText(baseGame / "materials/brick/testwall.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/testwall\"\n}\n");
	editor.game()->files.noteWrittenFile(baseGame / "materials/brick/testwall.vmt");
	editor.game()->files.noteWrittenFile(baseGame / "materials/brick/testwall.vtf");
	auto [wall, wallRef] = edit::createBlock(editor.doc(), Bounds{{3000, 0, 0}, {3064, 64, 64}}, "BRICK/TESTWALL");
	editor.commit(std::move(wall));
	for (int i = 0; i < 200 && !std::filesystem::exists(f.mod / "materials/brick/testwall.vmt"); ++i) QTest::qWait(25);
	waitIdle();
	const auto wallText = readFileText(f.mod / "materials/brick/testwall.vmt").value_or(std::string());
	CHECK(wallText.starts_with(std::string(PATTERN_RELIEF_MARKER)));
	CHECK(effectIsOn(GameEffect::Relief, wallText));
	CHECK(std::filesystem::exists(f.mod / "materials/brick/testwall_mapsmith_relief.vtf"));
	CHECK(enhancements->patternReliefCount() >= 1);
	CHECK(status->text().contains("of your maps with relief from their pattern"));

	// Without the mod's shader DLL: nothing that the game would draw as a wireframe stays, the
	// Overview says why and offers Install features; the material editor will not save such a shader.
	enhancements->setEffectEnabled(GameEffect::Liquid, true);
	waitIdle();
	CHECK(effectIsOn(GameEffect::Liquid, bottleText()));
	auto* dllBox = section->findChild<QWidget*>("enhancementDllMissing");
	REQUIRE(dllBox);
	CHECK_FALSE(dllBox->isVisible());
	std::filesystem::remove(f.mod / "bin/game_shader_dx6.dll");
	enhancements->sync();
	waitIdle();
	CHECK_FALSE(enhancements->shaderDll());
	CHECK_FALSE(std::filesystem::exists(f.mod / "materials/brick/testwall.vmt"));
	CHECK_FALSE(std::filesystem::exists(f.mod / "materials/brick/testwall_mapsmith_relief.vtf"));
	CHECK_FALSE(effectIsOn(GameEffect::Liquid, bottleText()));
	CHECK(enhancements->heldBackCount() >= 1);
	CHECK(dllBox->isVisibleTo(section));
	// (the main window would install them from the bundle: not in this test)
	QObject::disconnect(enhancements, &GameEnhancements::installFeaturesRequested, nullptr, nullptr);
	QSignalSpy installAsked(enhancements, &GameEnhancements::installFeaturesRequested);
	auto* install = section->findChild<QPushButton*>("enhancementInstallFeatures");
	REQUIRE(install);
	QTest::mouseClick(install, Qt::LeftButton);
	CHECK(installAsked.count() == 1);

	auto* materials = window.materialEditor();
	window.showPage(MainWindow::Page::Materials);
	materials->openMaterial("demo/brickwall001");
	settle();
	CHECK_FALSE(materials->fidelityShaders());
	auto* relief = materials->findChild<QCheckBox*>("relief");
	REQUIRE(relief);
	relief->setChecked(true);
	settle();
	auto* materialWarning = materials->findChild<QWidget*>("materialDllMissing");
	REQUIRE(materialWarning);
	CHECK(materialWarning->isVisibleTo(materials));
	const std::string before = readFileText(f.mod / "materials/demo/brickwall001.vmt").value_or(std::string());
	CHECK_FALSE(materials->save());
	CHECK(readFileText(f.mod / "materials/demo/brickwall001.vmt").value_or(std::string()) == before);   // not written: it would be a see-through wireframe
	CHECK_FALSE(std::filesystem::exists(f.mod / "materials/demo/brickwall001_relief.vtf"));
	relief->setChecked(false);
	settle();
	CHECK_FALSE(materialWarning->isVisibleTo(materials));
	test::writeText(f.mod / "bin/game_shader_dx6.dll", "stand-in for the test\n");
}

TEST_CASE("doors and connections: make door, Door box, test open, sounds, areaportal, connect by clicking") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	auto* assets = window.findChild<AssetBrowser*>();
	REQUIRE(assets);
	for (int i = 0; i < 100 && !assets->soundNames().contains("Doors.Move1"); ++i) settle();
	REQUIRE(assets->soundNames().contains("Doors.Move1"));
	auto* properties = window.findChild<PropertiesPanel*>();
	REQUIRE(properties);

	Viewport* view = window.view3D();
	view->camera().position = QVector3D(0, -320, 70);
	view->camera().yaw = 90;
	view->camera().pitch = -4;
	view->update();
	settle();
	const auto find = [&]<typename T>(T*, const char* name) {
		settle();
		return window.findChild<T*>(QString::fromLatin1(name));
	};
	const auto selectBlock = [&](Vec3 a, Vec3 b) {
		auto [made, ref] = edit::createBlock(editor.doc(), box(a, b), "DEMO/WOOD001");
		editor.commit(std::move(made));
		Selection selection;
		selection.solids.push_back(ref);
		editor.setSelection(std::move(selection));
		settle();
	};

	// Make door from a block: a swinging door that works at once.
	selectBlock({-32, -4, 0}, {32, 4, 112});
	auto* makeButton = find((QPushButton*)nullptr, "actionMakeDoor");
	REQUIRE(makeButton);
	CHECK(makeButton->isVisible());
	REQUIRE(window.findChild<QMenu*>("brushesMakeDoor"));
	auto* swinging = window.findChild<QAction*>("makeDoorSwinging");
	REQUIRE(swinging);
	swinging->trigger();
	settle();
	REQUIRE(editor.selection().entities.size() == 1);
	const int doorId = editor.selection().entities.front();
	{
		const Entity& door = *editor.doc().findEntity(doorId);
		CHECK(door.classname() == "func_door_rotating");
		CHECK(door.origin().x == doctest::Approx(32));   // hinge on the left edge seen from the front (+Y), no origin brush
		CHECK(door.get("noise1") == "Doors.Move1");
		CHECK(door.get("closesound") == "Doors.FullClose1");
		CHECK((std::stoi(door.get("spawnflags")) & 256) != 0);
	}
	CHECK(editor.history().undoName() == "Make door");

	// The Door box: hinge on the right moves the origin, one undo step.
	auto* opens = find((QComboBox*)nullptr, "doorOpens");
	REQUIRE(opens);
	REQUIRE(find((QGroupBox*)nullptr, "doorBox"));
	const int right = opens->findText("Swings - hinge on the right");
	REQUIRE(right >= 0);
	opens->setCurrentIndex(right);
	emit opens->activated(right);
	settle();
	CHECK(editor.doc().findEntity(doorId)->origin().x == doctest::Approx(-32));
	CHECK(editor.history().undoName() == "Edit door");
	auto* swing = find((QComboBox*)nullptr, "doorSwing");
	REQUIRE(swing);
	const int front = swing->findData(int(DoorSwing::Front));
	swing->setCurrentIndex(front);
	emit swing->activated(front);
	settle();
	CHECK((std::stoi(editor.doc().findEntity(doorId)->get("spawnflags")) & 16) != 0);
	CHECK(readDoor(*editor.doc().findEntity(doorId)).swing == DoorSwing::Front);
	auto* summary = find((QLabel*)nullptr, "doorSummary");
	REQUIRE(summary);
	CHECK(summary->text().contains("always to the front"));

	// Test open plays the motion in the view; the sound's play button decodes the sound.
	auto* test = find((QPushButton*)nullptr, "doorTest");
	REQUIRE(test);
	QTest::mouseClick(test, Qt::LeftButton);
	CHECK(view->doorTestId() == doorId);
	const quint64 frames = view->framesDrawn();
	for (int i = 0; i < 10; ++i) settle();
	CHECK(view->framesDrawn() > frames + 2);   // it animates
	for (int i = 0; i < 60 && view->doorTestId() != 0; ++i) settle();
	CHECK(view->doorTestId() == 0);            // and stops again
	auto* play = find((QToolButton*)nullptr, "doorPlay:0");
	REQUIRE(play);
	QTest::mouseClick(play, Qt::LeftButton);
	for (int i = 0; i < 100 && !properties->lastPlayedSound(); ++i) settle();
	CHECK(properties->lastPlayedSound());

	// An areaportal in the doorway: the box offers to link it.
	auto [portalBlock, portalRef] = edit::createBlock(editor.doc(), box({-32, -1, 0}, {32, 1, 112}), "TOOLS/TOOLSAREAPORTAL");
	editor.commit(std::move(portalBlock));
	Selection portalSelection;
	portalSelection.solids.push_back(portalRef);
	editor.setSelection(portalSelection);
	editor.tieSelectionToEntity("func_areaportal");
	const int portalId = editor.selection().entities.at(0);
	Selection doorSelection;
	doorSelection.entities.push_back(doorId);
	editor.setSelection(doorSelection);
	auto* link = find((QPushButton*)nullptr, "doorLinkPortal");
	REQUIRE(link);
	QTest::mouseClick(link, Qt::LeftButton);
	settle();
	CHECK(editor.doc().findEntity(doorId)->get("targetname") == "door_1");
	CHECK(editor.doc().findEntity(portalId)->get("target") == "door_1");
	CHECK(find((QLabel*)nullptr, "doorPortalLinked"));

	// Sliding: the class changes and movedir points up.
	opens = find((QComboBox*)nullptr, "doorOpens");
	const int up = opens->findText("Slides up");
	opens->setCurrentIndex(up);
	emit opens->activated(up);
	settle();
	CHECK(editor.doc().findEntity(doorId)->classname() == "func_door");
	CHECK(editor.doc().findEntity(doorId)->get("movedir") == "-90 0 0");
	editor.undo();
	settle();
	CHECK(editor.doc().findEntity(doorId)->classname() == "func_door_rotating");

	// Connect: a button triggers the door - pick the source, click the target in the view.
	selectBlock({80, -8, 40}, {96, -4, 56});
	editor.tieSelectionToEntity("func_button");
	const int buttonId = editor.selection().entities.at(0);
	CHECK(editor.doc().findEntity(buttonId)->get("targetname").empty());
	auto* connectButton = find((QPushButton*)nullptr, "actionConnect");
	REQUIRE(connectButton);
	QTest::mouseClick(connectButton, Qt::LeftButton);
	CHECK(editor.connectSource() == buttonId);
	QTest::keyClick(view, Qt::Key_Escape);   // Esc cancels
	CHECK(editor.connectSource() == 0);
	QTest::mouseClick(connectButton, Qt::LeftButton);
	QTest::mouseMove(view, screen(view, {-8, -4, 60}));
	settle();
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, {-8, -4, 60}));
	settle();
	CHECK(editor.connectSource() == 0);
	auto* popup = window.findChild<ConnectPopup*>("connectPopup");
	REQUIRE(popup);
	REQUIRE(popup->suggestions()->count() > 0);
	CHECK(popup->outputBox()->currentText() == "OnPressed");
	CHECK(popup->inputBox()->currentText() == "Open");
	popup->delayBox()->setValue(0.5);
	QTest::keyClick(popup->suggestions(), Qt::Key_Return);
	settle();
	CHECK_FALSE(window.findChild<ConnectPopup*>("connectPopup"));
	{
		const Entity& button = *editor.doc().findEntity(buttonId);
		REQUIRE(button.connections.size() == 1);
		CHECK(button.connections[0].output == "OnPressed");
		CHECK(button.connections[0].target == "door_1");
		CHECK(button.connections[0].input == "Open");
		CHECK(button.connections[0].delay == doctest::Approx(0.5));
	}
	CHECK(editor.selection().entities == std::vector<int>{buttonId});
	auto* card = find((QFrame*)nullptr, "outputCard:0");
	REQUIRE(card);
	CHECK(card->isVisible());
	CHECK(card->property("highlight").toBool());
	editor.undo();   // one step: the output goes again
	CHECK(editor.doc().findEntity(buttonId)->connections.empty());
	editor.redo();

	// Clicking the connection line (door selected) shows the button's output.
	editor.setSelection(doorSelection);
	settle();
	const Vec3 hinge = editor.doc().findEntity(doorId)->origin();
	const Vec3 buttonCenter{88, -6, 48};
	const Vec3 middle = (hinge + buttonCenter) * 0.5;
	QTest::mouseClick(view, Qt::LeftButton, {}, screen(view, middle));
	settle();
	CHECK(editor.selection().entities == std::vector<int>{buttonId});
	card = find((QFrame*)nullptr, "outputCard:0");
	REQUIRE(card);
	CHECK(card->property("highlight").toBool());

	// A door placed with the entity tool also works at once (one undo step).
	editor.setCurrentEntityClass("func_door_rotating");
	editor.createBrushEntity(box({-200, -4, 0}, {-136, 4, 112}));
	settle();
	{
		const int placed = editor.selection().entities.at(0);
		const Entity& door = *editor.doc().findEntity(placed);
		CHECK(door.origin().x == doctest::Approx(-136));
		CHECK(door.get("noise1") == "Doors.Move1");
		const std::size_t before = editor.doc().entities.size();
		editor.undo();
		CHECK(editor.doc().entities.size() == before - 1);
	}

	// Double door and prop door from blocks.
	selectBlock({200, -4, 0}, {328, 4, 112});
	window.findChild<QAction*>("makeDoorDouble")->trigger();
	settle();
	REQUIRE(editor.selection().entities.size() == 2);
	const Entity& left = *editor.doc().findEntity(editor.selection().entities[0]);
	const Entity& rightHalf = *editor.doc().findEntity(editor.selection().entities[1]);
	CHECK(left.get("targetname") == rightHalf.get("targetname"));
	CHECK(left.origin().x == doctest::Approx(328));
	CHECK(rightHalf.origin().x == doctest::Approx(200));
	selectBlock({-400, -4, 0}, {-346, 4, 104});
	window.findChild<QAction*>("makeDoorProp")->trigger();
	settle();
	REQUIRE(editor.selection().entities.size() == 1);
	const Entity& prop = *editor.doc().findEntity(editor.selection().entities[0]);
	CHECK(prop.classname() == "prop_door_rotating");
	CHECK(prop.get("model") == "models/props_demo/door01_left.mdl");
	CHECK(prop.origin().x == doctest::Approx(-346));
	CHECK(prop.get("angles") == "0 90 0");   // the leaf (+Y of the model) runs to -X
	CHECK(find((QComboBox*)nullptr, "doorModel"));
}

TEST_CASE("quick wins: make water, map budget, lightmap density, loop and prop class fixes, used models") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	if (indexed.isEmpty()) {
		REQUIRE(indexed.wait(20000));
	}
	settle();

	// Make water: select a block, Brushes > Make water. The current material is not water, so the
	// game's first water material is used; nodraw elsewhere, lod control and cubemap added.
	auto [made, pool] = edit::createBlock(editor.doc(), box({0, 0, 0}, {256, 256, 64}), "DEMO/BRICKWALL001");
	editor.commit(std::move(made));
	Selection poolSel;
	poolSel.solids.push_back(pool);
	editor.setSelection(poolSel);
	editor.setCurrentMaterial("DEMO/BRICKWALL001");
	QAction* makeWater = nullptr;
	for (auto* action : window.findChildren<QAction*>()) {
		if (action->text() == "Make water") makeWater = action;
	}
	REQUIRE(makeWater);
	const std::size_t entitiesBefore = editor.doc().entities.size();
	makeWater->trigger();
	settle();
	int waterFaces = 0, nodrawFaces = 0;
	for (const auto& side : editor.doc().findSolid(pool)->sides) {
		waterFaces += iequals(side.material, "demo/water001") ? 1 : 0;
		nodrawFaces += side.material == "TOOLS/TOOLSNODRAW" ? 1 : 0;
	}
	CHECK(waterFaces == 1);
	CHECK(nodrawFaces == 5);
	REQUIRE(editor.doc().entities.size() == entitiesBefore + 2);
	CHECK(editor.doc().entities[entitiesBefore].classname() == "water_lod_control");
	CHECK(editor.doc().entities[entitiesBefore + 1].classname() == "env_cubemap");
	editor.undo();   // one step takes it all back
	CHECK(editor.doc().entities.size() == entitiesBefore);
	CHECK(editor.doc().findSolid(pool)->sides[0].material == "DEMO/BRICKWALL001");
	editor.redo();
	settle();

	// Map budget: the dock counts the map against the engine limits.
	window.raiseDock("Map Budget");
	settle();
	auto* budget = window.budget();
	REQUIRE(budget);
	const auto budgetItem = [&](const std::string& name) -> const BudgetItem* {
		for (const auto& item : budget->items()) {
			if (item.name == name) return &item;
		}
		return nullptr;
	};
	REQUIRE(budgetItem("Brushes"));
	CHECK(budgetItem("Brushes")->count == 1);
	CHECK(budgetItem("Brushes")->limit == 8192);
	CHECK(budgetItem("Cubemaps")->count == 1);
	auto* brushCount = budget->findChild<QLabel*>("budgetCount:Brushes");
	REQUIRE(brushCount);
	CHECK(brushCount->text() == "1 / 8192");
	// A new brush shows up a moment later.
	auto [second, secondRef] = edit::createBlock(editor.doc(), box({288, 0, 0}, {352, 64, 64}), "DEMO/BRICKWALL001");
	editor.commit(std::move(second));
	for (int i = 0; i < 20 && budgetItem("Brushes")->count != 2; ++i) QTest::qWait(50);
	CHECK(budgetItem("Brushes")->count == 2);
	CHECK(brushCount->text() == "2 / 8192");
	CHECK(budget->findChild<QLabel*>("budgetSummary")->text().contains("within"));

	// Lightmap density: the corner menu's look, the legend, and the faces coloured by their scale.
	Viewport* view = window.view3D();
	view->camera().position = QVector3D(176, -350, 250);
	view->camera().yaw = 90;
	view->camera().pitch = -40;
	editor.setSelection({});
	editor.setRenderMode(Editor::RenderMode::Lit);
	view->update();
	settle();
	auto* legend = view->findChild<QLabel*>("densityLegend");
	REQUIRE(legend);
	CHECK_FALSE(legend->isVisible());
	const QPoint sidePoint = screen(view, {128, 0, 32});   // the pool's front face (nodraw: no lightmap)
	const QPoint blockPoint = screen(view, {320, 0, 32});  // the second block's front face (scale 16)
	QAction* densityAction = nullptr;
	for (auto* action : window.findChildren<QAction*>()) {
		if (action->text() == "Lightmap density") densityAction = action;
	}
	REQUIRE(densityAction);
	densityAction->trigger();
	CHECK(editor.renderMode() == Editor::RenderMode::LightmapDensity);
	view->update();
	settle();
	CHECK(legend->isVisible());
	CHECK(legend->text().contains("16"));
	const QColor green = view->grabFramebuffer().pixelColor(blockPoint);
	CHECK(green.green() > green.red());   // 16 = green
	CHECK(green.green() > green.blue());
	// A finer lightmap turns it red.
	auto* secondSolid = editor.doc().findSolid(secondRef);
	std::vector<FaceRef> faces;
	for (const auto& side : secondSolid->sides) faces.push_back({secondRef, side.id});
	editor.commit(edit::modifyFaces(editor.doc(), faces, "Lightmap scale", [](Side& side) { side.lightmapScale = 4; }));
	view->update();
	settle();
	const QColor red = view->grabFramebuffer().pixelColor(blockPoint);
	CHECK(red.red() > red.green());
	const QColor grey = view->grabFramebuffer().pixelColor(sidePoint);
	CHECK(std::abs(grey.red() - grey.green()) < 30);   // nodraw has no lightmap: grey
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		view->grabFramebuffer().save(QDir(shots).filePath("lightmap_density.png"));
	}
	editor.setRenderMode(Editor::RenderMode::Lit);
	settle();
	CHECK_FALSE(legend->isVisible());

	// Looping sound: wind1.wav loops (cue point) but "Is NOT Looped" is on - Object Properties
	// says so and fixes it; the Problems panel finds the opposite case and fixes that.
	const auto* ambientClass = editor.entities()->find("ambient_generic");
	auto [ambientMade, ambientId] = edit::placeEntity(editor.doc(), "ambient_generic", {64, 64, 128}, ambientClass,
	                                                  {{"message", "ambient/wind1.wav"}, {"spawnflags", "33"}});
	editor.commit(std::move(ambientMade));
	auto [alarmMade, alarmId] = edit::placeEntity(editor.doc(), "ambient_generic", {96, 64, 128}, ambientClass,
	                                              {{"message", "Demo.Alarm"}, {"spawnflags", "0"}});
	editor.commit(std::move(alarmMade));
	Selection ambientSel;
	ambientSel.entities.push_back(ambientId);
	editor.setSelection(ambientSel);
	editor.setTool(Editor::Tool::Select);
	window.raiseDock("Object Properties");
	settle();
	auto* loopBox = window.properties()->findChild<QGroupBox*>("soundLoop");
	REQUIRE(loopBox);
	CHECK(loopBox->findChild<QLabel*>("soundLoopState")->text().contains("loops"));
	auto* loopFix = loopBox->findChild<QPushButton*>("soundLoopFix");
	REQUIRE(loopFix);
	loopFix->click();
	settle();
	CHECK(editor.doc().findEntity(ambientId)->get("spawnflags") == "1");
	auto* fixedBox = window.properties()->findChild<QGroupBox*>("soundLoop");
	REQUIRE(fixedBox);
	CHECK_FALSE(fixedBox->findChild<QPushButton*>("soundLoopFix"));

	const auto clickFix = [&](const QString& label) {
		window.raiseDock("Problems");
		window.problems()->runChecks();
		settle();
		QPushButton* fix = nullptr;
		for (auto* button : window.problems()->findChildren<QPushButton*>()) {
			if (button->text() == label) fix = button;
		}
		REQUIRE_MESSAGE(fix, label.toStdString());
		QTest::mouseClick(fix, Qt::LeftButton);
		settle();
	};
	clickFix("Turn \"Is NOT Looped\" on");
	CHECK(editor.doc().findEntity(alarmId)->get("spawnflags") == "32");

	// Prop class: a prop_physics without a collision model and a prop_static of a model that is
	// not a static prop are fixed by their buttons; dropping a model picks the class itself.
	auto [physicsMade, physicsId] = edit::placeEntity(editor.doc(), "prop_physics", {300, 300, 0}, nullptr,
	                                                  {{"model", "models/props_demo/crate01.mdl"}});
	editor.commit(std::move(physicsMade));
	auto [staticMade, staticId] = edit::placeEntity(editor.doc(), "prop_static", {340, 300, 0}, nullptr,
	                                                {{"model", "models/props_demo/barrel01.mdl"}});
	editor.commit(std::move(staticMade));
	auto [goodMade, goodId] = edit::placeEntity(editor.doc(), "prop_static", {380, 300, 0}, nullptr,
	                                            {{"model", "models/props_demo/supply_crate.mdl"}});
	editor.commit(std::move(goodMade));
	clickFix("Make prop_dynamic");
	clickFix("Make prop_dynamic");
	CHECK(editor.doc().findEntity(physicsId)->classname() == "prop_dynamic");
	CHECK(editor.doc().findEntity(staticId)->classname() == "prop_dynamic");
	CHECK(editor.doc().findEntity(goodId)->classname() == "prop_static");
	window.problems()->runChecks();
	settle();
	for (auto* button : window.problems()->findChildren<QPushButton*>()) {
		CHECK_FALSE(button->text().startsWith("Make prop_"));
	}

	// Used in this map: the model tab lists only the three props, with how often each is used.
	window.raiseDock("Asset Browser");
	settle();
	auto* usedOnly = window.assets()->modelsUsedOnly();
	REQUIRE(usedOnly);
	auto* models = window.assets()->modelView()->model();
	const int all = models->rowCount();
	usedOnly->setChecked(true);
	settle();
	CHECK(models->rowCount() == 3);
	CHECK(all > 3);
	bool counted = false;
	for (int row = 0; row < models->rowCount(); ++row) {
		const QModelIndex index = models->index(row, 0);
		CHECK(index.data(ModelListModel::UsageRole).toInt() == 1);
		counted |= index.data(Qt::DisplayRole).toString().endsWith(QStringLiteral("1×"));
	}
	CHECK(counted);
	usedOnly->setChecked(false);
	settle();
	CHECK(models->rowCount() == all);
}

TEST_CASE("mod code: update the mod from the editor - steps, failure advice, cancel, after a pull, game files") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());   // the search for the source code stays in here
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	// A fake checkout of the mod's source code. Its "scripts" are the lines the real ones print;
	// the runner put in below prints them with the system's shell (PowerShell is not needed).
	QTemporaryDir repo;
	const auto root = toPath(repo.path());
	test::writeText(root / "update.bat", "@echo off\r\n");
	test::writeText(root / "sp" / "src" / "readme.txt", "source\n");
	const auto updateScript = root / "fidelity" / "update.ps1";
	const std::string success = "mapbase-fidelity update -> mymod\n"
	                            "  found    C:\\VS\\MSBuild.exe\n"
	                            "  ok       games.sln is up to date\n"
	                            "  building games.sln\n"
	                            "  copied   bin\\client.dll (episodic)\n"
	                            "  copied   bin\\server.dll (episodic)\n"
	                            "  skipped  VRAD - Source SDK Base 2013 Singleplayer not found (Steam library)\n"
	                            "mapbase-fidelity game files -> mymod\n"
	                            "Done. Start the mod\n"
	                            "All set. Start the mod, or compile a map - nothing else to do.\n";
	test::writeText(updateScript, success);
	test::writeText(root / "fidelity" / "deploy.ps1", "mapbase-fidelity game files -> mymod\n  copied   shaders\nDone. Start the mod\n");
	test::writeText(root / "fidelity" / "report.ps1", "mapbase-fidelity report -> mymod\n");

	test::Fixture f;
	test::createFixture(f);
	// Declared before the window: the runner below keeps references to them.
	int exitCode = 0;
	bool slow = false;
	std::vector<std::filesystem::path> ranScripts;
	std::filesystem::path ranFor;
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	auto* panel = window.modCodePanel();
	REQUIRE(panel);
	panel->setCommandFactory([&](ModCodeAction action, const ModCodeScripts& scripts, const std::filesystem::path& modDir) {
		const auto script = action == ModCodeAction::Deploy ? scripts.deploy : action == ModCodeAction::Report ? scripts.report : scripts.update;
		ranScripts.push_back(script);
		ranFor = modDir;
		ScriptCommand command;
		command.workingDir = scripts.root;
#ifdef Q_OS_WIN
		command.program = "cmd.exe";
		command.args = {"/c", "type", fromPath(script).replace('/', '\\').toStdString(), "&"};
		if (slow) command.args.insert(command.args.end(), {"ping", "-n", "30", "127.0.0.1", ">nul", "&"});
		command.args.insert(command.args.end(), {"exit", "/b", std::to_string(exitCode)});
#else
		command.program = "sh";
		command.args = {"-c", slow ? "cat \"$1\"; sleep 30; exit \"$2\"" : "cat \"$1\"; exit \"$2\"", "sh", fromPath(script).toStdString(),
		                std::to_string(exitCode)};
#endif
		return std::optional<ScriptCommand>(command);
	});
	const auto waitDone = [&] {
		for (int i = 0; i < 1000 && panel->running(); ++i) QTest::qWait(20);
		settle();
		return !panel->running();
	};
	auto* state = panel->findChild<QLabel*>("modCodeState");
	auto* update = panel->findChild<QPushButton*>("modCodeUpdate");
	auto* deploy = panel->findChild<QPushButton*>("modCodeDeploy");
	auto* steps = panel->findChild<QListWidget*>("modCodeSteps");
	auto* summary = panel->findChild<QLabel*>("modCodeSummary");
	auto* log = panel->findChild<QPlainTextEdit*>("modCodeLog");
	auto* afterPull = panel->findChild<QCheckBox*>("modCodeAutoAfterPull");
	REQUIRE(state);
	REQUIRE(update);
	REQUIRE(deploy);
	REQUIRE(steps);
	REQUIRE(summary);
	REQUIRE(log);
	REQUIRE(afterPull);
	const auto marks = [&] {
		QStringList out;
		for (int i = 0; i < steps->count(); ++i) out << steps->item(i)->data(Qt::UserRole).toString();
		return out;
	};

	// Nothing found in the (empty) home folder: chosen by hand, remembered for the mod.
	settle();
	CHECK(panel->repository().isEmpty());
	CHECK_FALSE(update->isEnabled());
	panel->setRepository(repo.path());
	CHECK(state->text().contains("update, deploy, report"));
	CHECK(QSettings().value("games/sourcemods_mymod/modCode/folder").toString() == QDir::cleanPath(repo.path()));
	window.showPage(MainWindow::Page::Project);
	window.projectTabs()->setCurrentWidget(panel);
	settle();
	REQUIRE(update->isEnabled());

	// Update mod: runs in the background, every step ticks off, the game is read again.
	QSignalSpy finished(panel, &ModCodePanel::finished);
	QSignalSpy updated(panel, &ModCodePanel::modUpdated);
	QSignalSpy reloaded(&editor, &Editor::gameChanged);
	QTest::mouseClick(update, Qt::LeftButton);
	CHECK(panel->running());
	CHECK_FALSE(update->isEnabled());
	REQUIRE(waitDone());
	REQUIRE(finished.count() == 1);
	CHECK(finished.last().at(0).toBool());
	CHECK(updated.count() == 1);
	CHECK(reloaded.count() >= 1);
	REQUIRE(editor.game());
	CHECK(ranScripts.back() == updateScript);
	CHECK(ranFor == f.mod);
	CHECK(marks() == QStringList{"✓", "✓", "✓", "✓", "–", "✓", "✓"});   // VRAD skipped
	CHECK(summary->text().contains("The mod is up to date"));
	CHECK(summary->text().contains("Source SDK Base 2013 Singleplayer not found"));
	CHECK(log->toPlainText().contains("All set."));
	CHECK(update->isEnabled());
	CHECK(panel->repository() == QDir::cleanPath(repo.path()));   // reading the game again keeps it
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		window.grab().save(QDir(shots).filePath("mod_code.png"));
	}

	// Visual Studio missing: the step it stopped at, the advice, and a line on the Overview.
	test::writeText(updateScript, "mapbase-fidelity update -> mymod\n"
	                              "\n"
	                              "  FAILED   Visual Studio 2022 with 'Desktop development with C++' not found (Visual Studio Installer)\n");
	exitCode = 1;
	QTest::mouseClick(update, Qt::LeftButton);
	REQUIRE(waitDone());
	CHECK_FALSE(finished.last().at(0).toBool());
	CHECK(updated.count() == 1);
	CHECK(marks().front() == "✗");
	CHECK(summary->text().contains("Visual Studio 2022 was not found"));
	CHECK(summary->text().contains("Desktop development with C++"));
	window.projectTabs()->setCurrentWidget(window.modPage());
	settle();
	auto* banner = window.modPage()->findChild<QFrame*>("modCodeBanner");
	REQUIRE(banner);
	CHECK(banner->isVisible());
	CHECK(banner->findChild<QLabel*>("modCodeBannerText")->text().contains("failed"));
	CHECK(banner->findChild<QPushButton*>("modCodeBannerUpdate")->isVisible());
	QTest::mouseClick(banner->findChild<QPushButton*>("modCodeBannerShow"), Qt::LeftButton);
	settle();
	CHECK(window.projectTabs()->currentWidget() == panel);

	// Cancel stops it (and what it started).
	test::writeText(updateScript, success);
	exitCode = 0;
	slow = true;
	REQUIRE(panel->start(ModCodeAction::Update));
	QTest::qWait(150);
	CHECK(panel->running());
	QTest::mouseClick(panel->findChild<QPushButton*>("modCodeCancel"), Qt::LeftButton);
	REQUIRE(waitDone());
	CHECK_FALSE(finished.last().at(0).toBool());
	CHECK(panel->lastRun().advice().find("stopped") != std::string::npos);
	slow = false;

	// After a pull: only when switched on, only for this checkout, only with new code.
	const int runs = int(ranScripts.size());
	panel->afterPull(repo.path(), true);
	CHECK_FALSE(panel->running());
	afterPull->click();
	CHECK(afterPull->isChecked());
	CHECK(QSettings().value("games/sourcemods_mymod/modCode/autoAfterPull").toBool());
	panel->afterPull(repo.path(), false);
	panel->afterPull(home.path(), true);
	CHECK_FALSE(panel->running());
	CHECK(int(ranScripts.size()) == runs);
	panel->afterPull(repo.path(), true);
	CHECK(panel->running());
	REQUIRE(waitDone());
	CHECK(finished.last().at(0).toBool());
	CHECK(updated.count() == 2);

	// Game files only: deploy.ps1, no steps of its own.
	QTest::mouseClick(deploy, Qt::LeftButton);
	REQUIRE(waitDone());
	CHECK(ranScripts.back() == root / "fidelity" / "deploy.ps1");
	CHECK(finished.last().at(0).toBool());
	CHECK(updated.count() == 3);
	CHECK(summary->text().contains("Game files installed"));
	CHECK_FALSE(steps->isVisible());

	// Source control's "Update mod" (shown for this checkout) runs the same thing here.
	if (const QString gitProgram = SourceControlPanel::gitProgram(); !gitProgram.isEmpty()) {
		QProcess git;
		git.setWorkingDirectory(repo.path());
		git.start(gitProgram, {"init", "-q"});
		REQUIRE(git.waitForFinished(10000));
		auto* sourceControl = window.findChild<SourceControlPanel*>("sourceControl");
		REQUIRE(sourceControl);
		window.projectTabs()->setCurrentWidget(sourceControl);
		sourceControl->setRepository(repo.path());
		for (int i = 0; i < 250 && sourceControl->busy(); ++i) QTest::qWait(20);
		settle();
		auto* scmUpdate = sourceControl->findChild<QPushButton*>("scmUpdate");
		REQUIRE(scmUpdate);
		CHECK(scmUpdate->isVisible());
		QTest::mouseClick(scmUpdate, Qt::LeftButton);
		CHECK(window.projectTabs()->currentWidget() == panel);
		CHECK(panel->running());
		REQUIRE(waitDone());
		CHECK(finished.last().at(0).toBool());
	}
}

TEST_CASE("features: the mod's features as one checklist, offered once, fresh install, Make report where one tests") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	// A fake checkout of the mod's source code; its "scripts" print what the real ones print.
	QTemporaryDir repo;
	const auto root = toPath(repo.path());
	test::writeText(root / "update.bat", "@echo off\r\n");
	test::writeText(root / "sp" / "src" / "readme.txt", "source\n");
	test::writeText(root / "fidelity" / "update.ps1", "mapbase-fidelity update -> mymod\nAll set. Start the mod, or compile a map - nothing else to do.\n");
	test::writeText(root / "fidelity" / "deploy.ps1", "mapbase-fidelity game files -> mymod\n  copied   shaders\nDone. Start the mod\n");
	test::writeText(root / "fidelity" / "remove.txt", "mapbase-fidelity game files -> mymod\nDone. The fidelity game files are gone.\n");
	test::writeText(root / "fidelity" / "report.ps1", "mapbase-fidelity report -> mymod\n");

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);   // mapbase-fidelity installed
	std::vector<ModCodeAction> ran;
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	REQUIRE(editor.hasFidelity());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	auto* enhancements = editor.enhancements();
	const auto waitIdle = [&] {
		for (int i = 0; i < 400 && (enhancements->busy() || !enhancements->ready()); ++i) QTest::qWait(25);
		settle();
	};
	waitIdle();
	auto* features = window.featuresPanel();
	auto* modCode = window.modCodePanel();
	REQUIRE(features);
	REQUIRE(modCode);
	CHECK(window.projectTabs()->indexOf(features) == 1);   // right after the Overview
	modCode->setCommandFactory([&](ModCodeAction action, const ModCodeScripts& scripts, const std::filesystem::path&) {
		ran.push_back(action);
		const auto script = action == ModCodeAction::RemoveGameFiles ? scripts.root / "fidelity" / "remove.txt"
		                    : action == ModCodeAction::Deploy       ? scripts.deploy
		                    : action == ModCodeAction::Report       ? scripts.report
		                                                            : scripts.update;
		ScriptCommand command;
		command.workingDir = scripts.root;
#ifdef Q_OS_WIN
		command.program = "cmd.exe";
		command.args = {"/c", "type", fromPath(script).replace('/', '\\').toStdString()};
#else
		command.program = "sh";
		command.args = {"-c", "cat \"$1\"", "sh", fromPath(script).toStdString()};
#endif
		return std::optional<ScriptCommand>(command);
	});
	const auto cfg = f.mod / "cfg" / "mapsmith_features.cfg";
	const auto cfgText = [&] { return QString::fromStdString(readFileText(cfg).value_or("")); };

	// First time with the add-on: the Overview offers the checklist once; nothing written yet.
	auto* offer = features->offer();
	REQUIRE(offer);
	CHECK_FALSE(offer->isHidden());
	CHECK_FALSE(features->chosen());
	CHECK_FALSE(std::filesystem::exists(cfg));
	for (const auto& feature : modFeatures()) CHECK(features->isOn(QString::fromUtf8(feature.id)));
	auto* choose = offer->findChild<QPushButton*>("featuresOfferChoose");
	REQUIRE(choose);
	window.showPage(MainWindow::Page::Project);
	window.projectTabs()->setCurrentIndex(0);
	settle();
	CHECK(offer->isVisible());
	QTest::mouseClick(choose, Qt::LeftButton);
	CHECK(window.projectTabs()->currentWidget() == features);
	CHECK(offer->isHidden());
	CHECK(features->chosen());
	CHECK(cfgText().contains("r_fidelity_ssao 1"));
	CHECK(runsFeatures(readFileText(f.mod / "cfg" / "autoexec.cfg").value_or("")));

	// A tick is saved at once: the switch in the cfg, a material effect in the Game enhancements.
	auto* ssao = features->checklist()->box("ssao");
	REQUIRE(ssao);
	settle();
	CHECK(ssao->isVisible());
	QTest::mouseClick(ssao, Qt::LeftButton, {}, QPoint(8, ssao->height() / 2));
	CHECK_FALSE(features->isOn("ssao"));
	CHECK(cfgText().contains("r_fidelity_ssao 0"));
	CHECK(cfgText().contains("r_fidelity_godrays 1"));
	features->setOn("liquid", false);
	waitIdle();
	CHECK_FALSE(enhancements->settings().enabled(GameEffect::Liquid));
	features->setOn("liquid", true);
	waitIdle();
	CHECK(enhancements->settings().enabled(GameEffect::Liquid));
	// Read back when the mod is opened again; never offered again.
	window.selectGame("sourcemods/mymod");
	settle();
	CHECK_FALSE(features->isOn("ssao"));
	CHECK_FALSE(features->checklist()->box("ssao")->isChecked());
	CHECK(offer->isHidden());
	waitIdle();

	// Fresh install: the confirmation lists only generated files, then remove -> features -> install.
	const auto own = f.mod / "materials" / "mine" / "wall.vmt";
	const auto editorMade = f.mod / "materials" / "models" / "mapsmith_test.vmt";
	test::writeText(own, "\"LightmappedGeneric\"\n{\n}\n");
	test::writeText(editorMade, std::string(MAPSMITH_MARKER) + " off (liquid) from test\n\"VertexLitGeneric\"\n{\n}\n");
	test::writeText(f.mod / "materials" / "brick" / "generated.vmt", std::string(DEPLOY_MARKER) + " (parallax) from test\n\"Fidelity_Parallax\"\n{\n}\n");
	auto* freshButton = features->findChild<QPushButton*>("freshInstall");
	auto* confirm = features->findChild<QWidget*>("freshInstallConfirm");
	auto* go = features->findChild<QPushButton*>("freshInstallGo");
	auto* build = features->findChild<QCheckBox*>("freshInstallBuild");
	REQUIRE(freshButton);
	REQUIRE(confirm);
	REQUIRE(go);
	REQUIRE(build);
	// Without the source code it says what is missing and cannot go.
	QTest::mouseClick(freshButton, Qt::LeftButton);
	CHECK(confirm->isVisible());
	CHECK_FALSE(go->isEnabled());
	modCode->setRepository(repo.path());
	QTest::mouseClick(freshButton, Qt::LeftButton);
	REQUIRE(go->isEnabled());
	QStringList listed;
	for (int i = 0; i < features->freshList()->count(); ++i) listed << features->freshList()->item(i)->data(Qt::UserRole).toString();
	CHECK(listed.contains("materials/models/mapsmith_test.vmt"));
	CHECK(listed.contains("materials/brick/generated.vmt"));
	CHECK_FALSE(listed.contains("materials/mine/wall.vmt"));
	CHECK(std::filesystem::exists(editorMade));   // nothing happens before the confirmation
	build->setChecked(false);
	QSignalSpy done(features, &FeaturesPanel::freshInstallFinished);
	QTest::mouseClick(go, Qt::LeftButton);
	CHECK_FALSE(confirm->isVisible());
	for (int i = 0; i < 500 && done.isEmpty(); ++i) QTest::qWait(20);
	REQUIRE(done.count() == 1);
	CHECK(done.last().at(0).toBool());
	CHECK(features->freshStep() == FeaturesPanel::FreshStep::Done);
	CHECK(ran == std::vector<ModCodeAction>{ModCodeAction::RemoveGameFiles, ModCodeAction::Deploy});
	CHECK_FALSE(std::filesystem::exists(editorMade));
	CHECK(std::filesystem::exists(own));
	CHECK(cfgText().contains("r_fidelity_ssao 0"));   // the choice was written again
	auto* steps = features->findChild<QLabel*>("freshInstallSteps");
	REQUIRE(steps);
	CHECK(steps->text().contains("Fresh install done"));
	settle();

	// Make report: from the Map menu, the Compile dock and the Problems dock, each explaining how.
	ran.clear();
	window.showPage(MainWindow::Page::Map);
	auto* menuReport = window.findChild<QAction*>("makeReport");
	REQUIRE(menuReport);
	CHECK(menuReport->toolTip().contains("fidelity_report"));
	menuReport->trigger();
	CHECK(window.projectTabs()->currentWidget() == modCode);
	for (int i = 0; i < 500 && modCode->running(); ++i) QTest::qWait(20);
	settle();
	CHECK(ran == std::vector<ModCodeAction>{ModCodeAction::Report});
	auto* problemsReport = window.findChild<QPushButton*>("problemsMakeReport");
	auto* compileReport = window.compileWindow()->findChild<QPushButton*>("compileMakeReport");
	REQUIRE(problemsReport);
	REQUIRE(compileReport);
	CHECK(problemsReport->toolTip().contains("fidelity_report"));
	CHECK(compileReport->toolTip().contains("fidelity_report"));
	window.showPage(MainWindow::Page::Map);
	problemsReport->click();
	for (int i = 0; i < 500 && modCode->running(); ++i) QTest::qWait(20);
	settle();
	CHECK(ran.size() == 2);
	CHECK(ran.back() == ModCodeAction::Report);

	// New mod: the features can be chosen up front; they wait in the mod for the add-on.
	{
		NewModDialog dialog(&editor);
		REQUIRE(dialog.features());
		CHECK(dialog.features()->isHidden());
		auto* toggle = dialog.findChild<QPushButton*>("newModChooseFeatures");
		REQUIRE(toggle);
		toggle->click();
		CHECK_FALSE(dialog.features()->isHidden());
		dialog.features()->box("godrays")->click();
		dialog.features()->box("water")->click();
		CHECK(dialog.featuresOff() == std::set<std::string>{"godrays", "water"});
		ModTemplate mod;
		mod.title = "Feature Mod";
		mod.folder = "featuremod";
		QString error;
		REQUIRE_FALSE(createModAndSwitch(&editor, mod, {}, &error, dialog.featuresOff()).isEmpty());
		const auto made = f.steam / "steamapps" / "sourcemods" / "featuremod";
		CHECK(loadFeatureChoice(made) == std::set<std::string>{"godrays"});   // water is a material effect: not a switch
		CHECK_FALSE(loadEnhancementSettings(made).enabled(GameEffect::Water));
		CHECK_FALSE(runsFeatures(readFileText(made / "cfg" / "autoexec.cfg").value_or("")));   // no add-on there yet
		settle();
		CHECK_FALSE(features->isOn("godrays"));
		CHECK_FALSE(features->isOn("water"));
		CHECK(features->isOn("ssao"));
		CHECK(offer->isHidden());

		// Half-Life 2's skill values (ammo limits) and HUD texts: the fixture's game has none, so
		// the Overview offers to add them; they come from the bundle's copy of Valve's mod template.
		window.modPage()->refresh();
		auto* baseFiles = window.modPage()->findChild<QWidget*>("modBaseFiles");
		auto* addBase = window.modPage()->findChild<QPushButton*>("modBaseFilesAdd");
		REQUIRE(baseFiles);
		REQUIRE(addBase);
		CHECK_FALSE(baseFiles->isHidden());
		QTemporaryDir templateBundle;
		const auto root = toPath(templateBundle.path()) / "template";
		test::writeText(root / "hl2/cfg/skill.cfg", "sk_max_ar2 60\n");
		test::writeText(root / "episodic/cfg/skill_episodic.cfg", "sk_zombie_soldier_health 100\n");
		test::writeText(root / "episodic/resource/mod_episodic_english.txt",
		                "\"lang\"\n{\n\"Language\" \"English\"\n\"Tokens\"\n{\n\"HL2_Pulse_Rifle\" \"PULSE RIFLE\"\n}\n}\n");
		qputenv("SME_FEATURES_DIR", templateBundle.path().toUtf8());
		addBase->click();
		qunsetenv("SME_FEATURES_DIR");
		settle();
		CHECK(std::filesystem::exists(made / "cfg/skill.cfg"));
		CHECK(std::filesystem::exists(made / "cfg/skill_episodic.cfg"));
		CHECK(readFileText(made / "cfg/skill_manifest.cfg").value_or("").find("exec skill_episodic.cfg") != std::string::npos);
		const auto texts = loadLocalizationFile(made / "resource/featuremod_english.txt");
		REQUIRE(texts);
		CHECK(texts->get("HL2_Pulse_Rifle") == "PULSE RIFLE");
		CHECK(texts->get("GameTitle") == "Feature Mod");
		CHECK(baseFiles->isHidden());
	}
}

TEST_CASE("relief: the bricks found in the texture, whole bricks as zones, changes live in the views") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	REQUIRE(editor.fidelityLook());   // the views show the relief
	auto [made, wallRef] = edit::createBlock(editor.doc(), box({0, 0, 0}, {256, 16, 256}), "DEMO/BRICKWALL001");
	editor.commit(std::move(made));
	settle();
	auto* cache = window.findChild<MaterialCache*>();
	REQUIRE(cache);
	const auto shown = [&]() -> std::shared_ptr<const MaterialInfo> {
		std::shared_ptr<const MaterialInfo> info;
		for (int i = 0; i < 300 && !(info = cache->get("demo/brickwall001")); ++i) QTest::qWait(20);
		return info;
	};
	const auto waitFor = [&](const std::function<bool(const MaterialInfo&)>& condition) {
		for (int i = 0; i < 300; ++i) {
			if (const auto info = cache->get("demo/brickwall001"); info && condition(*info)) return true;
			QTest::qWait(20);
		}
		return false;
	};
	REQUIRE(shown());
	CHECK(shown()->shader.compare("LightmappedGeneric", Qt::CaseInsensitive) == 0);

	// Relief on in the material editor: the bricks are found in the texture and stand up, whatever their colour.
	auto* materials = window.materialEditor();
	QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
	QSignalSpy heightsReady(materials, &MaterialEditor::heightsReady);
	materials->openMaterial("demo/brickwall001");
	REQUIRE(previewed.wait(10000));
	materials->findChild<QCheckBox*>("relief")->setChecked(true);
	REQUIRE(heightsReady.wait(10000));
	const ReliefLayout& layout = materials->reliefLayout();
	REQUIRE(layout.found());
	CHECK(layout.kind == ReliefLayout::Kind::RunningBond);
	CHECK(layout.courses() == 8);
	const auto heights = materials->currentHeights();
	REQUIRE(heights);
	CHECK(materials->findChild<QPushButton*>("heightFromPattern")->isVisibleTo(materials));

	// The views show it at once, before Save: the editor's shader, its height map, its depth.
	REQUIRE(waitFor([](const MaterialInfo& info) { return info.shader == "Fidelity_Parallax" && !info.height.isNull(); }));
	CHECK(shown()->parallaxDepth == doctest::Approx(0.006f));
	CHECK(shown()->height.width() == int(heights->width));
	auto* depth = materials->findChild<QSlider*>("$parallaxdepth");
	REQUIRE(depth);
	depth->setValue(600);
	REQUIRE(waitFor([](const MaterialInfo& info) { return std::abs(info.parallaxDepth - 0.06f) < 1e-4f; }));
	CHECK_FALSE(editor.game()->files.readText("materials/demo/brickwall001.vmt")->find("Fidelity") != std::string::npos);   // nothing written yet

	// Whole bricks as zones: a click sets the brick under it, a right click takes it back.
	QImage texture(256, 256, QImage::Format_RGB32);
	texture.fill(Qt::darkRed);
	HeightZonePainter painter(texture, heights->width, heights->height, std::vector<std::uint8_t>(std::size_t(heights->width) * heights->height, 0));
	painter.setLayout(&layout);
	painter.setUnitMode(true);
	CHECK(painter.unitMode());
	painter.resize(264, 264);   // the picture: 256 x 256 with a 4 pixel border
	painter.show();
	REQUIRE(QTest::qWaitForWindowExposed(&painter));
	// The middle of a brick: away from every joint.
	QPoint inBrick;
	for (int y = 8; y < 248 && inBrick.isNull(); y += 4) {
		for (int x = 8; x < 248 && inBrick.isNull(); x += 4) {
			const int lx = x * int(layout.width) / 256, ly = y * int(layout.height) / 256;
			bool inner = true;
			for (int d = -6; d <= 6 && inner; d += 3) inner = layout.unitAt(lx + d, ly) >= 0 && layout.unitAt(lx, ly + d) >= 0;
			if (inner) inBrick = QPoint(x + 4, y + 4);
		}
	}
	REQUIRE_FALSE(inBrick.isNull());
	const auto unit = layout.unitAt((inBrick.x() - 4) * int(layout.width) / 256, (inBrick.y() - 4) * int(layout.height) / 256);
	QTest::mouseClick(&painter, Qt::LeftButton, {}, inBrick);
	int marked = 0, others = 0;
	for (unsigned y = 0; y < heights->height; ++y) {
		for (unsigned x = 0; x < heights->width; ++x) {
			if (painter.zones()[std::size_t(y) * heights->width + x] != std::uint8_t(HeightZone::Flat)) continue;
			++marked;
			others += layout.unitAt(int(x * layout.width / heights->width), int(y * layout.height / heights->height)) != unit ? 1 : 0;
		}
	}
	CHECK(marked > int(heights->width * heights->height / 64));   // a whole brick, not a dot
	CHECK(others == 0);
	QTest::mouseClick(&painter, Qt::RightButton, {}, inBrick);
	CHECK(std::count(painter.zones().begin(), painter.zones().end(), std::uint8_t(HeightZone::Flat)) == 0);
	painter.hide();

	// The zones go into the views too; leaving the material without saving shows the file again.
	std::vector<std::uint8_t> zones(std::size_t(heights->width) * heights->height, std::uint8_t(HeightZone::Flat));
	materials->setHeightZones(zones, heights->width, heights->height);
	REQUIRE(waitFor([](const MaterialInfo& info) { return !info.height.isNull() && QColor(info.height.pixel(5, 5)).red() == 255; }));
	materials->openMaterial("demo/concretewall001");
	REQUIRE(waitFor([](const MaterialInfo& info) { return info.shader.compare("LightmappedGeneric", Qt::CaseInsensitive) == 0; }));
}

TEST_CASE("features from Mapsmith: offered on the Overview, installed by one click, updated by themselves, removed again") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();
	// The feature bundle the Windows package carries next to Mapsmith.exe.
	QTemporaryDir bundleDir;
	const auto bundle = toPath(bundleDir.path());
	test::makeFeatureBundle(bundle, "1.0+aaa", "one");
	qputenv("SME_FEATURES_DIR", bundleDir.path().toUtf8());

	test::Fixture f;
	test::createFixture(f);   // a Mapbase mod (HL2) without the add-on, Mapbase's own DLL in bin
	test::writeText(f.mod / "bin" / "client.dll", "mapbase client");
	test::writeText(f.sdkBase / "bin" / "vrad.exe", "mapbase vrad");
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	REQUIRE_FALSE(editor.hasFidelity());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	auto* features = window.featuresPanel();
	auto* modCode = window.modCodePanel();
	REQUIRE(features);
	REQUIRE(modCode);
	REQUIRE(features->bundle());
	CHECK(features->installState() == FeaturesPanel::InstallState::NotInstalled);
	// No PowerShell in the test: the bundle's scripts print what the real ones print.
	std::vector<ModCodeAction> ran;
	std::vector<std::filesystem::path> ranScripts;
	modCode->setCommandFactory([&](ModCodeAction action, const ModCodeScripts& scripts, const std::filesystem::path&) {
		ran.push_back(action);
		const auto script = action == ModCodeAction::Report ? scripts.report : scripts.deploy;
		ranScripts.push_back(script);
		ScriptCommand command;
		command.workingDir = scripts.root;
#ifdef Q_OS_WIN
		command.program = "cmd.exe";
		command.args = {"/c", "type", fromPath(script).replace('/', '\\').toStdString()};
#else
		command.program = "sh";
		command.args = {"-c", "cat \"$1\"; echo Done.", "sh", fromPath(script).toStdString()};
#endif
		return std::optional<ScriptCommand>(command);
	});
	QSignalSpy installed(features, &FeaturesPanel::installed);
	const auto waitInstalled = [&](int count) {
		for (int i = 0; i < 500 && installed.count() < count; ++i) QTest::qWait(20);
		settle();
		return installed.count() >= count;
	};

	// The Overview offers the install (a Mapbase mod without the features).
	auto* offer = features->offer();
	REQUIRE(offer);
	CHECK_FALSE(offer->isHidden());
	auto* offerInstall = offer->findChild<QPushButton*>("featuresOfferInstall");
	REQUIRE(offerInstall);
	CHECK_FALSE(offerInstall->isHidden());
	CHECK(offer->findChild<QPushButton*>("featuresOfferChoose")->isHidden());
	// Build from source is out of the way: last tab, and without source code its "Update mod"
	// installs these features.
	CHECK(window.projectTabs()->indexOf(modCode) == window.projectTabs()->count() - 1);
	CHECK(window.projectTabs()->tabText(window.projectTabs()->indexOf(modCode)) == "Build from source");
	CHECK_FALSE(modCode->hasCheckout());
	CHECK(modCode->findChild<QPushButton*>("modCodeUpdate")->text() == "Install features");
	CHECK(modCode->canRun(ModCodeAction::Report));   // Make report works without the mod's source code
	CHECK(modCode->scripts()->report == bundle / "fidelity" / "report.ps1");

	// One click: the files are in, then the bundle's deploy.ps1 makes the generated materials.
	QTest::mouseClick(offerInstall, Qt::LeftButton);
	REQUIRE(waitInstalled(1));
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "hl2 client one");
	CHECK(readFileText(f.mod / "bin" / "client.dll.mapsmith-backup") == "mapbase client");
	CHECK(readFileText(f.mod / "bin" / "game_shader_dx6.dll") == "shaders one");
	CHECK(readFileText(f.mod / "shaders" / "fxc" / "fidelity_ssao_ps20b.vcs") == "ssao one");
	CHECK(readFileText(f.sdkBase / "bin" / "vrad.exe") == "vrad one");
	CHECK(readFileText(f.sdkBase / "bin" / "fidelity_backup" / "vrad.exe") == "mapbase vrad");
	CHECK(ran == std::vector<ModCodeAction>{ModCodeAction::Deploy});
	CHECK(ranScripts.back() == bundle / "fidelity" / "deploy.ps1");
	settle();
	CHECK(editor.hasFidelity());   // the game was read again: the views and enhancements see the add-on
	CHECK(features->installState() == FeaturesPanel::InstallState::Current);
	auto* state = features->findChild<QLabel*>("featuresInstallState");
	REQUIRE(state);
	CHECK(state->text().contains("1.0+aaa"));
	CHECK(state->text().contains("Installed"));   // in green, with the date
	// What it installed is listed, and the progress ended with "Done".
	REQUIRE(features->findChild<QLabel*>("featuresWhat"));
	CHECK(features->findChild<QLabel*>("featuresWhat")->text().contains("client.dll"));
	auto* progress = features->findChild<QProgressBar*>("featuresInstallProgress");
	REQUIRE(progress);
	CHECK_FALSE(progress->isHidden());
	CHECK(progress->value() == progress->maximum());
	CHECK(features->findChild<QLabel*>("featuresInstallStep")->text().contains("Done"));
	CHECK(features->findChild<QPushButton*>("featuresInstall")->text() == "Install again");
	CHECK_FALSE(features->findChild<QPushButton*>("featuresRemove")->isHidden());
	// The Overview now offers the checklist instead (the add-on is there, no choice yet).
	CHECK(offerInstall->isHidden());
	CHECK_FALSE(offer->findChild<QPushButton*>("featuresOfferChoose")->isHidden());

	// A newer Mapsmith: the mod gets the new version by itself when it is opened.
	test::makeFeatureBundle(bundle, "2.0+bbb", "two");
	features->reloadBundle();
	REQUIRE(waitInstalled(2));
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "hl2 client two");
	CHECK(readFileText(f.mod / "bin" / "client.dll.mapsmith-backup") == "mapbase client");
	CHECK(features->installState() == FeaturesPanel::InstallState::Current);
	CHECK(state->text().contains("2.0+bbb"));
	// Switched off, a newer one waits for a click.
	features->findChild<QCheckBox*>("featuresAutoUpdate")->setChecked(false);
	test::makeFeatureBundle(bundle, "3.0+ccc", "three");
	features->reloadBundle();
	settle();
	QTest::qWait(100);
	CHECK(features->installState() == FeaturesPanel::InstallState::Outdated);
	CHECK(features->findChild<QPushButton*>("featuresInstall")->text() == "Update features");
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "hl2 client two");

	// Remove: what the mod had before comes back.
	QTest::mouseClick(features->findChild<QPushButton*>("featuresRemove"), Qt::LeftButton);
	REQUIRE(waitInstalled(3));
	CHECK(ran.back() == ModCodeAction::RemoveGameFiles);
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "mapbase client");
	CHECK_FALSE(std::filesystem::exists(f.mod / "bin" / "game_shader_dx6.dll"));
	CHECK(readFileText(f.sdkBase / "bin" / "vrad.exe") == "mapbase vrad");
	CHECK(features->installState() == FeaturesPanel::InstallState::NotInstalled);

	// "Update mod" without source code (Build from source tab, the notes elsewhere): installs.
	QTest::mouseClick(modCode->findChild<QPushButton*>("modCodeUpdate"), Qt::LeftButton);
	REQUIRE(waitInstalled(4));
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "hl2 client three");

	// A new Mapbase mod has the game code and the features from the start; its generated
	// materials follow when it opens.
	QString error;
	ModTemplate fresh{"Fresh Mod", "freshmod", ModBase::Mapbase, mapbaseMounts(f.steam / "steamapps/sourcemods")};
	REQUIRE(createModAndSwitch(&editor, fresh, {}, &error) == "sourcemods/freshmod");
	const auto freshDir = f.steam / "steamapps/sourcemods/freshmod";
	const auto freshInstalled = readInstalledFeatures(freshDir);
	REQUIRE(freshInstalled);
	CHECK(freshInstalled->version == "3.0+ccc");
	// (the template mounts Episode Two: its DLLs)
	CHECK(freshInstalled->branch == modBranch(readFileText(freshDir / "gameinfo.txt").value_or("")));
	CHECK(readFileText(freshDir / "bin" / "server.dll") ==
	      std::string(modBranchName(freshInstalled->branch)) + " server three");
	REQUIRE(waitInstalled(5));
	CHECK(ran.back() == ModCodeAction::Deploy);
	CHECK(editor.hasFidelity());
	qunsetenv("SME_FEATURES_DIR");
}

TEST_CASE("problems: a Mapbase mod without Mapbase's shaders is warned about, Install features fills its bin") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();
	QTemporaryDir bundleDir;
	const auto bundle = toPath(bundleDir.path());
	test::makeFeatureBundle(bundle, "1.0+aaa", "one");
	qputenv("SME_FEATURES_DIR", bundleDir.path().toUtf8());

	// What the new mod test2 had: Mapbase's game code, but no game_shader_dx9.dll - the game drew
	// the SDK_* materials and screen passes as wireframe.
	test::Fixture f;
	test::createFixture(f);
	test::writeText(f.mod / "bin" / "client.dll", "mapbase client");
	test::writeText(f.mod / "bin" / "server.dll", "mapbase server");
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.gameEntry());
	REQUIRE(editor.gameEntry()->usesMapbase);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	auto* features = window.featuresPanel();
	REQUIRE(features);
	REQUIRE(features->bundle());
	window.modCodePanel()->setCommandFactory([](ModCodeAction, const ModCodeScripts& scripts, const std::filesystem::path&) {
		ScriptCommand command;   // no PowerShell in the test
		command.workingDir = scripts.root;
#ifdef Q_OS_WIN
		command.program = "cmd.exe";
		command.args = {"/c", "echo", "Done."};
#else
		command.program = "sh";
		command.args = {"-c", "echo Done."};
#endif
		return std::optional<ScriptCommand>(command);
	});
	QSignalSpy installed(features, &FeaturesPanel::installed);
	const auto waitInstalled = [&](int count) {
		for (int i = 0; i < 500 && installed.count() < count; ++i) QTest::qWait(20);
		settle();
		return installed.count() >= count;
	};
	const auto warning = [&]() -> QPushButton* {
		window.raiseDock("Problems");
		window.problems()->runChecks();
		settle();
		bool listed = false;
		for (auto* label : window.problems()->findChildren<QLabel*>()) {
			listed |= label->text().contains("bin/game_shader_dx9.dll") && label->text().contains("wireframe");
		}
		if (!listed) return nullptr;
		for (auto* button : window.problems()->findChildren<QPushButton*>()) {
			if (button->text() == "Install features") return button;
		}
		return nullptr;
	};

	// The Problems list warns and offers the install; one click puts the files in.
	auto* install = warning();
	REQUIRE(install);
	QTest::mouseClick(install, Qt::LeftButton);
	REQUIRE(waitInstalled(1));
	CHECK(window.projectTabs()->currentWidget() == features);
	CHECK(readFileText(f.mod / "bin" / "game_shader_dx9.dll") == "hl2 mapbase shaders one");
	CHECK(readFileText(f.mod / "bin" / "client.dll") == "hl2 client one");
	CHECK(features->installState() == FeaturesPanel::InstallState::Current);
	settle();
	CHECK_FALSE(warning());

	// An install that lost a file (or came from a Mapsmith without Mapbase's shaders) is
	// "Incomplete" on the Features card; Install again adds it.
	features->findChild<QCheckBox*>("featuresAutoUpdate")->setChecked(false);
	std::filesystem::remove(f.mod / "bin" / "game_shader_dx9.dll");
	features->reloadBundle();
	settle();
	CHECK(features->installState() == FeaturesPanel::InstallState::Outdated);
	auto* state = features->findChild<QLabel*>("featuresInstallState");
	REQUIRE(state);
	CHECK(state->text().contains("Incomplete"));
	CHECK(state->text().contains("bin/game_shader_dx9.dll"));
	CHECK(warning());
	QTest::mouseClick(features->findChild<QPushButton*>("featuresInstall"), Qt::LeftButton);
	REQUIRE(waitInstalled(2));
	CHECK(readFileText(f.mod / "bin" / "game_shader_dx9.dll") == "hl2 mapbase shaders one");
	CHECK(features->installState() == FeaturesPanel::InstallState::Current);
	CHECK_FALSE(warning());
	qunsetenv("SME_FEATURES_DIR");
}

TEST_CASE("animations page: a model's sequences played in 3D, where the map uses them, put on an entity") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QString error;
	REQUIRE(editor.openMap(fromPath(f.mod / "mapsrc/demo.vmf"), &error));
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	settle();
	const QString citizenModel = "models/humans/group01/male_07.mdl";
	int citizen = 0;
	for (const auto& e : editor.doc().entities) {
		if (e.get("targetname") == "citizen_01") citizen = e.id;
	}
	REQUIRE(citizen != 0);
	// A prop playing one of the citizen's animations and a script for the citizen.
	auto [placeProp, propId] = edit::placeEntity(editor.doc(), "prop_dynamic", {200, 0, 0}, editor.entities()->find("prop_dynamic"),
	                                             {{"model", citizenModel.toStdString()}, {"DefaultAnim", "lineidle01"}});
	editor.commit(std::move(placeProp));
	auto [placeScript, scriptId] = edit::placeEntity(editor.doc(), "scripted_sequence", {-100, 40, 0}, editor.entities()->find("scripted_sequence"),
	                                                 {{"m_iszEntity", "citizen_01"}, {"m_iszPlay", "cower"}});
	editor.commit(std::move(placeScript));

	// The side bar has the page next to Models.
	QAction* pageAction = nullptr;
	for (auto* a : window.findChildren<QAction*>()) {
		if (a->objectName() == "page:Animations") pageAction = a;
	}
	REQUIRE(pageAction);
	pageAction->trigger();
	settle();
	auto* page = window.animationsPage();
	REQUIRE(page);
	CHECK(page->isVisible());
	// "Add a sample ..." opens the shipped samples (build/samples next to the test program).
	CHECK(page->findChild<QPushButton*>("animationSample")->isEnabled());
	// Only models that really animate: their headers are read in the background.
	for (int i = 0; i < 400 && page->modelsToCheck() > 0; ++i) QTest::qWait(25);
	CHECK(page->modelsToCheck() == 0);
	QTest::qWait(400);   // the list refills once the check is done
	settle();
	// The map's animated models come first.
	REQUIRE(page->modelList()->count() > 1);
	CHECK_FALSE(page->modelList()->item(0)->flags().testFlag(Qt::ItemIsEnabled));   // "In this map"
	CHECK(page->modelList()->item(1)->data(Qt::UserRole).toString() == citizenModel);
	// Then the characters and the props apart, each under its own header.
	{
		QString section;
		QMap<QString, QString> sectionOf;
		for (int i = 0; i < page->modelList()->count(); ++i) {
			auto* item = page->modelList()->item(i);
			if (!item->flags().testFlag(Qt::ItemIsEnabled)) section = item->text();
			else if (section != "In this map") sectionOf[item->data(Qt::UserRole).toString()] = section;
		}
		CHECK(sectionOf.value(citizenModel) == "NPCs");
		// A crate or a bottle has only its still "idle", the shared animations nothing to show.
		CHECK_FALSE(sectionOf.contains("models/props_demo/crate01.mdl"));
		CHECK_FALSE(sectionOf.contains("models/props_junk/glassbottle01a.mdl"));
		CHECK_FALSE(sectionOf.contains("models/humans/male_shared.mdl"));
		for (int i = 0; i < page->modelList()->count(); ++i) {
			CHECK_FALSE(page->modelList()->item(i)->text().contains("crate01"));   // not "In this map" either
		}
	}

	QSignalSpy loaded(page, &AnimationsPage::modelLoaded);
	page->modelList()->setCurrentRow(1);
	REQUIRE((loaded.count() > 0 || loaded.wait(10000)));
	REQUIRE(page->animated());
	// Its own sequences and those of the shared model, with activity and origin.
	auto* list = page->sequenceList();
	REQUIRE(list->topLevelItemCount() == 13);
	QStringList names;
	for (int i = 0; i < list->topLevelItemCount(); ++i) names << list->topLevelItem(i)->text(0);
	CHECK(names.mid(0, 5) == QStringList{"idle_subtle", "lineidle01", "lineidle02", "cower", "cower_idle"});
	CHECK(names.contains("walk_all"));
	CHECK(page->currentSequence() == "idle_subtle");   // the idle is shown first
	// Filters: weapon, stance, kind (with counts); choosing a hidden animation resets them.
	const auto shown = [list] {
		QStringList out;
		for (int i = 0; i < list->topLevelItemCount(); ++i) {
			if (list->topLevelItem(i)->flags() & Qt::ItemIsSelectable) out << list->topLevelItem(i)->text(0);
		}
		return out;
	};
	const auto choose = [](QComboBox* combo, int value) {
		const int index = combo->findData(value);
		REQUIRE(index > 0);
		combo->setCurrentIndex(index);
		emit combo->activated(index);
	};
	choose(page->weaponFilter(), int(sequence_weapon::SMG1));
	CHECK(page->weaponFilter()->currentText() == "SMG1 (1)");
	CHECK(page->weaponFilter()->findData(int(sequence_weapon::PISTOL)) < 0);   // none in this model
	CHECK(shown() == QStringList{"Idle_Relaxed_SMG1_1"});
	page->weaponFilter()->setCurrentIndex(0);
	choose(page->stanceFilter(), 1);
	CHECK(shown() == QStringList{"crouch_idle"});
	page->stanceFilter()->setCurrentIndex(0);
	choose(page->kindFilter(), int(SequenceKind::Move));
	CHECK(shown() == QStringList{"walk_all", "run_all"});
	choose(page->kindFilter(), int(SequenceKind::Scene));
	CHECK(shown() == QStringList{"lean_wall"});
	REQUIRE(page->selectSequence("walk_all"));
	CHECK(page->kindFilter()->currentIndex() == 0);
	CHECK(list->topLevelItemCount() == 13);
	CHECK(list->currentItem()->text(1) == "ACT_WALK");
	CHECK(list->currentItem()->text(3) == "male_shared.mdl");
	CHECK(list->currentItem()->text(0) == "walk_all");
	// An include model's animation has its length (not "pose") and frames to play.
	CHECK(list->currentItem()->text(2).endsWith(" s"));
	CHECK(page->animated()->sequences()[std::size_t(page->animated()->findSequence("walk_all"))].frames > 1);

	// The pose moves the vertices: a quarter into the walk the legs swing (the right leg forward).
	for (int i = 0; i < 20 && !page->viewer()->model(); ++i) QTest::qWait(50);
	const auto rest = page->viewer()->model();
	REQUIRE(rest);
	const int uploads = page->viewer()->posesUploaded();
	page->setCycle(0.25f);
	settle();
	CHECK(page->viewer()->posesUploaded() > uploads);
	std::vector<ModelVertex> posed;
	skinVertices(*rest, page->animated()->skinning(page->animated()->findSequence("walk_all"), 0.25f), posed);
	float moved = 0;
	for (std::size_t i = 0; i < posed.size(); ++i) moved = std::max(moved, std::abs(posed[i].position[0] - rest->vertices[i].position[0]));
	CHECK(moved > 5.f);
	// Play runs on its own; pause stops it; without Loop it stops on the last frame.
	page->setCycle(0);
	page->setPlaying(true);
	QTest::qWait(300);
	CHECK(page->cycle() > 0.1f);
	page->setPlaying(false);
	const float paused = page->cycle();
	QTest::qWait(150);
	CHECK(page->cycle() == paused);
	page->setLooping(false);
	page->setSpeed(2);
	page->setCycle(0.95f);
	page->setPlaying(true);
	QTest::qWait(400);
	CHECK(page->cycle() == 1.f);
	CHECK_FALSE(page->playing());
	page->setLooping(true);

	// Where the map uses the model's animations: the prop and the script (through its NPC).
	QStringList uses;
	for (int i = 0; i < page->usesList()->count(); ++i) uses << page->usesList()->item(i)->text();
	CHECK(uses.join(QStringLiteral("\n")).contains("Default animation: lineidle01"));
	CHECK(uses.join('\n').contains("citizen_01"));
	CHECK(uses.join('\n').contains("Action: cower"));

	// Use on selected entity: the prop's DefaultAnim, the NPC's pose helper, a script's field.
	CHECK_FALSE(page->useButton()->isEnabled());   // nothing selected
	Selection one;
	one.entities.push_back(propId);
	editor.setSelection(one);
	settle();
	CHECK(page->useButton()->isEnabled());
	REQUIRE(page->useOnSelection());
	CHECK(editor.doc().findEntity(propId)->get("DefaultAnim") == "walk_all");
	one.entities = {citizen};
	editor.setSelection(one);
	REQUIRE(page->selectSequence("cower_idle"));
	REQUIRE(page->useOnSelection());
	const Entity* helper = nullptr;
	for (const auto& e : editor.doc().entities) {
		if (e.get("targetname") == "citizen_01_pose") helper = &e;
	}
	REQUIRE(helper);
	CHECK(helper->get("m_iszIdle") == "cower_idle");
	CHECK(helper->get("m_iszEntity") == "citizen_01");
	CHECK(helper->get("spawnflags") == "112");   // Start on Spawn: it idles from map start
	one.entities = {scriptId};
	editor.setSelection(one);
	REQUIRE(page->selectSequence("lean_wall"));
	REQUIRE(page->useOnSelection("m_iszEntry"));
	CHECK(editor.doc().findEntity(scriptId)->get("m_iszEntry") == "lean_wall");
	editor.undo();
	CHECK(editor.doc().findEntity(scriptId)->get("m_iszEntry").empty());

	// Double-click in "Used in this map": the entity is selected in the map.
	settle();
	QListWidgetItem* propUse = nullptr;
	for (int i = 0; i < page->usesList()->count(); ++i) {
		if (page->usesList()->item(i)->data(Qt::UserRole).toInt() == propId) propUse = page->usesList()->item(i);
	}
	REQUIRE(propUse);
	emit page->usesList()->itemDoubleClicked(propUse);
	settle();
	CHECK(window.currentPage() == MainWindow::Page::Map);
	REQUIRE(editor.selection().entities.size() == 1);
	CHECK(editor.selection().entities.front() == propId);

	// ▶ next to an animation field in Object Properties opens the page on that animation, playing.
	one.entities = {scriptId};
	editor.setSelection(one);
	settle();
	QToolButton* preview = nullptr;
	for (auto* button : window.properties()->findChildren<QToolButton*>("previewSequence")) {
		const auto* box = button->parentWidget()->findChild<QComboBox*>();
		if (box && box->currentText() == "cower") preview = button;   // the Action field
	}
	REQUIRE(preview);
	page->selectModel("models/props_demo/crate01.mdl");   // another model first
	preview->click();
	settle();
	CHECK(window.currentPage() == MainWindow::Page::Animations);
	CHECK(page->currentModel() == citizenModel);
	for (int i = 0; i < 100 && page->currentSequence().isEmpty(); ++i) QTest::qWait(50);
	for (int i = 0; i < 100 && !page->playing(); ++i) QTest::qWait(50);
	CHECK(page->playing());
	if (const auto shot = qEnvironmentVariable("SME_ANIMATION_SHOT"); !shot.isEmpty()) {
		page->selectSequence("walk_all");
		page->setPlaying(false);
		page->setCycle(0.25f);
		settle();
		window.grab().save(shot);
	}
	page->setPlaying(false);
}

TEST_CASE("npc scenes: clicking an NPC and its script, no resize squares, the scripted_sequence box, a fixed head") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QString error;
	REQUIRE(editor.openMap(fromPath(f.mod / "mapsrc/demo.vmf"), &error));
	window.setQuadLayout(false);
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	settle();
	int citizen = 0;
	for (const auto& e : editor.doc().entities) {
		if (e.get("targetname") == "citizen_01") citizen = e.id;
	}
	REQUIRE(citizen != 0);
	{
		Transaction t(editor.doc(), "Face +X");
		t.touchEntity(citizen);
		editor.doc().findEntity(citizen)->set("angles", "0 0 0");
		editor.commit(t.finish());
	}
	// The pose helper stands exactly where its NPC stands and shows the NPC see-through.
	editor.commit(edit::poseNpc(editor.doc(), citizen, "cower_idle"));
	const int helper = editor.doc().entities.back().id;
	REQUIRE(editor.doc().findEntity(helper)->classname() == "scripted_sequence");
	const Vec3 feet = editor.doc().findEntity(citizen)->origin();

	// Clicking: the body picks the NPC (before, the see-through stand-in won the tie and the NPC
	// could not be clicked), the spot at its feet the script.
	Viewport* view = window.view3D();
	REQUIRE(view);
	const auto pickAt = [view](const Vec3& from, const Vec3& to) {
		const auto hit = view->renderer().pick(Ray{from, normalize(to - from)});
		return hit ? hit->entityId : -1;
	};
	const Vec3 chest = feet + Vec3{0, 0, 48};
	int picked = -1;
	for (int i = 0; i < 100 && picked != citizen; ++i) {   // the model loads in the background
		settle();
		picked = pickAt(chest + Vec3{200, 0, 0}, chest);
	}
	CHECK(picked == citizen);
	CHECK(pickAt(feet + Vec3{0, 0, 200}, feet + Vec3{0, 0, 66}) == citizen);   // the head from above
	CHECK(pickAt(feet + Vec3{200, 0, 8}, feet + Vec3{0, 0, 8}) == helper);     // between the legs: the script's sprite

	// No resize squares for point entities with Select or Scale - only for brushes.
	Selection only;
	only.entities = {citizen};
	editor.setSelection(only);
	editor.setTool(Editor::Tool::Select);
	CHECK_FALSE(view->showsResizeHandles());
	editor.setTool(Editor::Tool::Scale);
	CHECK_FALSE(view->showsResizeHandles());
	only.entities = {helper};
	editor.setSelection(only);
	CHECK_FALSE(view->showsResizeHandles());
	only.entities.clear();
	only.solids.push_back(SolidRef{0, editor.doc().world.solids.front().id});
	editor.setSelection(only);
	CHECK(view->showsResizeHandles());
	editor.setTool(Editor::Tool::Select);
	CHECK(view->showsResizeHandles());

	// The scripted_sequence box: Julius' setup - Start on Spawn on a named script with an Action.
	auto [placeScript, scriptId] = edit::placeEntity(editor.doc(), "scripted_sequence", feet + Vec3{64, 0, 0}, editor.entities()->find("scripted_sequence"),
	                                                 {{"targetname", "watch"}, {"m_iszEntity", "citizen_01"}, {"m_iszIdle", "lineidle01"},
	                                                  {"m_iszPlay", "cower"}, {"spawnflags", "16"}});
	editor.commit(std::move(placeScript));
	only = {};
	only.entities = {scriptId};
	editor.setSelection(only);
	settle();
	auto* props = window.properties();
	auto* box = props->findChild<QGroupBox*>("scriptBox");
	REQUIRE(box);
	CHECK(box->findChild<QLabel*>("scriptStart")->text().contains("Action never plays"));
	// Loop the action.
	auto* loop = box->findChild<QComboBox*>("scriptLoop");
	REQUIRE(loop);
	const int loopAction = loop->findData(int(ScriptState::Loop::Action));
	loop->setCurrentIndex(loopAction);
	emit loop->activated(loopAction);
	settle();
	CHECK(editor.doc().findEntity(scriptId)->get("m_bLoopActionSequence") == "1");
	// Move to position: instantly.
	box = props->findChild<QGroupBox*>("scriptBox");
	auto* moveTo = box->findChild<QComboBox*>("scriptMoveTo");
	const int instantly = moveTo->findData(QStringLiteral("4"));
	moveTo->setCurrentIndex(instantly);
	emit moveTo->activated(instantly);
	settle();
	CHECK(editor.doc().findEntity(scriptId)->get("m_fMoveTo") == "4");
	// Start with a trigger: one undo step, the trigger is selected to be moved into place.
	const std::size_t before = editor.doc().entities.size();
	box = props->findChild<QGroupBox*>("scriptBox");
	box->findChild<QPushButton*>("scriptTrigger")->click();
	settle();
	REQUIRE(editor.doc().entities.size() == before + 1);
	const Entity& trigger = editor.doc().entities.back();
	CHECK(trigger.classname() == "trigger_once");
	REQUIRE(trigger.connections.size() == 1);
	CHECK(trigger.connections[0].target == "watch");
	CHECK(trigger.connections[0].input == "BeginSequence");
	REQUIRE(editor.selection().entities.size() == 1);
	CHECK(editor.selection().entities.front() == trigger.id);
	editor.setSelection(only);
	settle();
	box = props->findChild<QGroupBox*>("scriptBox");
	CHECK(box->findChild<QLabel*>("scriptStart")->text().contains("watch_trigger"));
	editor.undo();
	CHECK(editor.doc().entities.size() == before);
	// Start on map start: a logic_auto sends BeginSequence.
	settle();
	box = props->findChild<QGroupBox*>("scriptBox");
	box->findChild<QPushButton*>("scriptAtMapStart")->click();
	settle();
	CHECK(scriptState(editor.doc(), scriptId).start == ScriptState::Start::AtMapStart);
	box = props->findChild<QGroupBox*>("scriptBox");
	CHECK_FALSE(box->findChild<QPushButton*>("scriptAtMapStart")->isEnabled());

	// The citizen: random head until "Keep this head".
	only.entities = {citizen};
	editor.setSelection(only);
	settle();
	auto* look = props->findChild<QGroupBox*>("citizenLook");
	REQUIRE(look);
	CHECK(look->findChild<QLabel*>("citizenLookState")->text().contains("Random head"));
	auto* heads = look->findChild<QComboBox*>("citizenHead");
	CHECK(heads->currentData().toString() == "models/humans/group01/male_07.mdl");
	heads->setCurrentIndex(heads->findData(QStringLiteral("models/humans/group03/female_02.mdl")));
	look->findChild<QPushButton*>("citizenKeepLook")->click();
	settle();
	CHECK(editor.doc().findEntity(citizen)->get("citizentype") == "4");
	CHECK(editor.doc().findEntity(citizen)->get("model") == "models/humans/group03/female_02.mdl");
	look = props->findChild<QGroupBox*>("citizenLook");
	CHECK(look->findChild<QLabel*>("citizenLookState")->text().contains("female_02"));
}

#ifndef _WIN32
TEST_CASE("animations page: add an animation (.smd), preview it and build it into the mod (studiomdl faked)") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	QString error;
	REQUIRE(editor.openMap(fromPath(f.mod / "mapsrc/demo.vmf"), &error));
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	settle();
	const QString citizenModel = "models/humans/group01/male_07.mdl";
	window.showPage(MainWindow::Page::Animations);
	auto* page = window.animationsPage();
	QSignalSpy loaded(page, &AnimationsPage::modelLoaded);
	page->selectModel(citizenModel);
	REQUIRE((loaded.count() > 0 || loaded.wait(10000)));
	REQUIRE(page->animated());
	const int before = page->sequenceList()->topLevelItemCount();

	// A wave made for the same skeleton (one extra bone the model lacks is left out).
	const QString smd = home.filePath("Wave Hello.smd");
	{
		QFile file(smd);
		REQUIRE(file.open(QIODevice::WriteOnly));
		QByteArray text = "version 1\nnodes\n0 \"ValveBiped.Bip01_Pelvis\" -1\n1 \"ValveBiped.Bip01_Spine\" 0\n"
		                  "2 \"ValveBiped.Bip01_R_UpperArm\" 1\n3 \"ValveBiped.Bip01_R_Finger0\" 2\nend\nskeleton\n";
		for (int frame = 0; frame < 10; ++frame) {
			text += "time " + QByteArray::number(frame) + "\n0 0 0 34 0 0 -1.570796\n1 0 0 0 0 0 0\n";
			text += "2 0 -15 24 " + QByteArray::number(-1.2 * std::sin(frame * 0.6), 'f', 4) + " 0 0\n3 0 0 -10 0 0 0\n";
		}
		file.write(text + "end\n");
	}
	ImportedAnimation options;
	options.name = "wave_hello";
	options.fps = 20;
	options.loop = true;
	options.activity = "ACT_SIGNAL1";
	REQUIRE(page->importAnimation(smd, options, &error));
	// It plays at once, marked as not built; the bar offers to build it.
	CHECK(page->sequenceList()->topLevelItemCount() == before + 1);
	CHECK(page->currentSequence() == "wave_hello");
	CHECK(page->playing());
	CHECK(page->sequenceList()->currentItem()->text(1) == "ACT_SIGNAL1");
	CHECK(page->sequenceList()->currentItem()->text(3).contains("not built"));
	CHECK(page->pendingImports() == 1);
	auto* compile = page->findChild<QPushButton*>("animationCompile");
	REQUIRE(compile);
	CHECK(compile->isVisible());
	if (const auto shot = qEnvironmentVariable("SME_ANIMATION_IMPORT_SHOT"); !shot.isEmpty()) {
		page->setPlaying(false);
		page->setCycle(0.3f);
		settle();
		window.grab().save(shot);
	}
	// A file of another skeleton is refused.
	const QString other = home.filePath("dog.smd");
	{
		QFile file(other);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write("version 1\nnodes\n0 \"Dog.Root\" -1\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\nend\n");
	}
	CHECK_FALSE(page->importAnimation(other, {}, &error));
	CHECK(error.contains("None of"));
	CHECK(page->pendingImports() == 1);

	// No studiomdl: a clear message, nothing runs.
	page->setStudiomdlProgram(home.filePath("missing/studiomdl"));
	CHECK_FALSE(page->compileImports());
	CHECK(page->compileLog().contains("studiomdl was not found"));
	CHECK(page->pendingImports() == 1);

	// A failing studiomdl: the log keeps its lines, the animation stays pending.
	const auto script = [&](const QString& name, const QString& body) {
		const QString path = home.filePath(name);
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write(("#!/bin/sh\n" + body + "\n").toUtf8());
		file.close();
		file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
		return path;
	};
	QSignalSpy finished(page, &AnimationsPage::compileFinished);
	page->setStudiomdlProgram(script("studiomdl_bad", "echo 'ERROR: bone ValveBiped.Bip01_R_Finger0 not found'; exit 1"));
	REQUIRE(page->compileImports());
	REQUIRE(finished.wait(10000));
	CHECK_FALSE(finished.takeFirst().at(0).toBool());
	CHECK(page->compileLog().contains("bone ValveBiped"));
	CHECK(page->compileLog().contains("exit code 1"));
	CHECK(page->pendingImports() == 1);

	// The real run (faked): studiomdl gets -game <mod> and the .qc; it "builds" the model by
	// copying the original's files to models/mapsmith/... and the page shows the new model.
	const QString modDir = fromPath(f.mod);
	const QString args = home.filePath("studiomdl_args.txt");
	page->setStudiomdlProgram(script("studiomdl_ok",
	                                 "echo \"$@\" > '" + args + "'\n"
	                                 "echo 'Processing model.qc'\n"
	                                 "m=\"$2/models/humans/group01/male_07\"; o=\"$2/models/mapsmith/humans/group01\"\n"
	                                 "mkdir -p \"$o\" && for e in mdl vvd dx90.vtx; do cp \"$m.$e\" \"$o/male_07.$e\"; done"));
	REQUIRE(page->compileImports());
	REQUIRE(finished.wait(10000));
	CHECK(finished.takeFirst().at(0).toBool());
	CHECK(page->pendingImports() == 0);
	const QDir sources(modDir + "/modelsrc/mapsmith/humans/group01/male_07");
	for (const char* name : {"model.qc", "reference.smd", "wave_hello.smd", "animations.txt"}) {
		INFO(name);
		CHECK(sources.exists(name));
	}
	{
		QFile qc(sources.filePath("model.qc"));
		REQUIRE(qc.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(qc.readAll());
		CHECK(text.contains("$modelname \"mapsmith/humans/group01/male_07.mdl\""));
		CHECK(text.contains("$includemodel \"humans/group01/male_07.mdl\""));
		CHECK(text.contains("wave_hello"));
		CHECK(text.contains("ACT_SIGNAL1"));
		QFile argsFile(args);
		REQUIRE(argsFile.open(QIODevice::ReadOnly));
		const QString line = QString::fromUtf8(argsFile.readAll()).trimmed();
		CHECK(line.startsWith("-game " + modDir));
		CHECK(line.endsWith("model.qc"));
	}
	CHECK(page->currentModel() == "models/mapsmith/humans/group01/male_07.mdl");

	// Use on an NPC: it switches to the new model in the same undo step.
	int citizen = 0;
	for (const auto& e : editor.doc().entities) {
		if (e.get("targetname") == "citizen_01") citizen = e.id;
	}
	REQUIRE(citizen != 0);
	for (int i = 0; i < 200 && !page->animated(); ++i) QTest::qWait(50);
	REQUIRE(page->animated());
	Selection one;
	one.entities = {citizen};
	editor.setSelection(one);
	settle();
	REQUIRE(page->selectSequence("idle_subtle"));
	REQUIRE(page->useOnSelection());
	CHECK(editor.doc().findEntity(citizen)->get("model") == "models/mapsmith/humans/group01/male_07.mdl");
	editor.undo();
	CHECK(editor.doc().findEntity(citizen)->get("model") == citizenModel.toStdString());
	page->setPlaying(false);
}
#endif

TEST_CASE("import: own sounds, pictures, models and particle files come into the mod") {
	namespace fs = std::filesystem;
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	settle();
	const auto mod = editor.gameEntry()->gameDir;
	QTemporaryDir sources;
	const auto source = [&](const QString& name) { return sources.filePath(name); };
	const auto write = [](const QString& path, const std::vector<std::byte>& data) {
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write(reinterpret_cast<const char*>(data.data()), qint64(data.size()));
	};
	const auto writeText = [](const QString& path, const QString& text) {
		QFile file(path);
		REQUIRE(file.open(QIODevice::WriteOnly));
		file.write(text.toUtf8());
	};
	const auto dialogFor = [&](const char* button) -> ImportDialog* {
		auto* open = window.findChild<QPushButton*>(button);
		REQUIRE(open);
		open->click();
		settle();
		auto* dialog = window.findChild<ImportDialog*>("importDialog");
		REQUIRE(dialog);
		REQUIRE(dialog->isVisible());
		return dialog;
	};

	// A sound from the Sounds page: a WAV at 48 kHz stereo becomes mono 22 kHz with a script name.
	{
		DecodedAudio tone;
		tone.sampleRate = 48000;
		tone.channels = 2;
		for (int i = 0; i < 4800; ++i) {
			const auto s = std::int16_t(6000 * std::sin(i * 0.05));
			tone.samples.insert(tone.samples.end(), {s, s});
		}
		write(source("Engine Hum.wav"), encodeWav(tone));
		auto* dialog = dialogFor("importSounds");
		CHECK(dialog->kind() == ImportDialog::Kind::Sound);
		CHECK(dialog->findChild<QPushButton*>("importSample")->isEnabled());
		auto* importButton = dialog->findChild<QPushButton*>("importNow");
		CHECK_FALSE(importButton->isEnabled());   // nothing chosen yet
		REQUIRE(dialog->setSourceFile(source("Engine Hum.wav")));
		CHECK(dialog->findChild<QLabel*>("importPreview")->text().contains("48000"));
		CHECK(dialog->findChild<QLineEdit*>("importName")->text() == "engine_hum");
		dialog->findChild<QComboBox*>("importRate")->setCurrentIndex(1);   // 22050
		dialog->findChild<QCheckBox*>("importScript")->setChecked(true);
		CHECK(dialog->findChild<QLineEdit*>("importScriptName")->text() == "Custom.engine_hum");
		CHECK(dialog->findChild<QLabel*>("importTarget")->text().contains("sound/custom/engine_hum.wav"));
		REQUIRE(importButton->isEnabled());
		QSignalSpy changed(dialog, &ImportDialog::modChanged);
		importButton->click();
		CHECK(dialog->resultText().contains("Custom.engine_hum"));
		CHECK(changed.count() == 1);
		const auto wav = mod / "sound/custom/engine_hum.wav";
		REQUIRE(fs::exists(wav));
		std::ifstream in(wav, std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), {});
		const auto decoded = decodeAudio(std::as_bytes(std::span(bytes.data(), bytes.size())), "x.wav");
		REQUIRE(decoded);
		CHECK(decoded->sampleRate == 22050);
		CHECK(decoded->channels == 1);
		CHECK(fs::exists(mod / "scripts/game_sounds_mapsmith.txt"));
		// The same name again: only with Replace ticked.
		REQUIRE(dialog->setSourceFile(source("Engine Hum.wav")));
		auto* replace = dialog->findChild<QCheckBox*>("importReplace");
		CHECK(replace->isVisibleTo(dialog));
		CHECK_FALSE(importButton->isEnabled());
		replace->setChecked(true);
		CHECK(importButton->isEnabled());
		dialog->findChild<QPushButton*>("importUndo")->click();   // the first one is taken back
		CHECK_FALSE(fs::exists(wav));
		CHECK(dialog->resultText().contains("Taken back"));
		REQUIRE(dialog->setSourceFile(source("Engine Hum.wav")));
		REQUIRE(dialog->importNow());
		CHECK(fs::exists(wav));
		dialog->close();
		settle();
	}

	// A picture from the Materials page: VTF + VMT, a _normal picture next to it is the bump map.
	{
		ImageRgba image;
		image.width = 64;
		image.height = 32;
		for (int i = 0; i < 64 * 32; ++i) image.pixels.insert(image.pixels.end(), {std::byte(i % 256), std::byte(90), std::byte(40), std::byte(255)});
		write(source("Red Bricks.png"), encodePng(image));
		write(source("Red Bricks_normal.png"), encodePng(image));
		window.showMaterialEditor();
		settle();
		auto* dialog = dialogFor("importMaterial");
		REQUIRE(dialog->setSourceFile(source("Red Bricks.png")));
		CHECK_FALSE(dialog->findChild<QLabel*>("importPreview")->pixmap().isNull());
		CHECK(dialog->findChild<QLabel*>("importNormal")->text().contains("_normal"));
		dialog->findChild<QComboBox*>("importSurface")->setCurrentText("brick");
		REQUIRE(dialog->importNow());
		CHECK(dialog->resultText().contains("custom/red_bricks"));
		const std::string vmt = [&] {
			std::ifstream in(mod / "materials/custom/red_bricks.vmt");
			return std::string((std::istreambuf_iterator<char>(in)), {});
		}();
		CHECK(vmt.find("\"$bumpmap\" \"custom/red_bricks_normal\"") != std::string::npos);
		CHECK(vmt.find("\"$surfaceprop\" \"brick\"") != std::string::npos);
		CHECK_FALSE(dialog->setSourceFile(source("Engine Hum.wav")));   // not a picture: said so, nothing to import
		CHECK(dialog->findChild<QLabel*>("importPreview")->text().contains("Not a picture"));
		CHECK_FALSE(dialog->findChild<QPushButton*>("importNow")->isEnabled());
		dialog->close();
		settle();
	}

	// A model from the Models page: .obj + .mtl + picture, compiled by (a stand-in for) studiomdl.
	{
		writeText(source("crate.obj"), "mtllib crate.mtl\nv -0.5 0 -0.5\nv 0.5 0 -0.5\nv 0.5 1 -0.5\nv -0.5 1 -0.5\nv -0.5 0 0.5\nv 0.5 0 0.5\nv 0.5 1 0.5\n"
		                                "v -0.5 1 0.5\nvt 0 0\nvt 1 0\nvt 1 1\nusemtl Crate Wood\nf 1/1 2/2 3/3 4/1\nf 5/1 8/2 7/3 6/1\nf 1/1 5/2 6/3 2/1\n"
		                                "f 2/1 6/2 7/3 3/1\nf 3/1 7/2 8/3 4/1\nf 4/1 8/2 5/3 1/1\n");
		writeText(source("crate.mtl"), "newmtl Crate Wood\nmap_Kd crate_wood.png\n");
		ImageRgba wood;
		wood.width = wood.height = 16;
		wood.pixels.assign(16 * 16 * 4, std::byte(150));
		write(source("crate_wood.png"), encodePng(wood));
		// The stand-in writes what studiomdl would (or fails when asked to).
		ModelImportOptions expected;
		expected.name = "crate";
		const auto outputs = modelOutputFiles(mod, expected);
		QString script;
#ifdef Q_OS_WIN
		const QString fake = source("studiomdl.bat");
		script = "@echo off\r\nif exist \"" + source("fail") + "\" (echo ERROR: too many bones & exit /b 1)\r\nmkdir \"" +
		         QDir::toNativeSeparators(fromPath(outputs[0].parent_path())) + "\" 2>nul\r\n";
		for (const auto& file : outputs) script += "echo compiled> \"" + QDir::toNativeSeparators(fromPath(file)) + "\"\r\n";
#else
		const QString fake = source("studiomdl");
		script = "#!/bin/sh\nif [ -e '" + source("fail") + "' ]; then echo 'ERROR: too many bones'; exit 1; fi\nmkdir -p '" + fromPath(outputs[0].parent_path()) +
		         "'\necho \"$@\" > '" + source("args.txt") + "'\n";
		for (const auto& file : outputs) script += "echo compiled > '" + fromPath(file) + "'\n";
#endif
		writeText(fake, script);
		QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		{
			QSettings settings;
			settings.beginGroup("games/" + QString(editor.gameKey()).replace('/', '_'));
			settings.setValue("studiomdl", fake);
		}
		CHECK(ImportDialog::studiomdlPath(&editor) == fake);
		window.raiseDock("Models");
		settle();
		auto* dialog = dialogFor("importModel");
		REQUIRE(dialog->setSourceFile(source("crate.obj")));
		auto* materials = dialog->findChild<QListWidget*>("importMaterials");
		REQUIRE(materials->count() == 1);
		CHECK(materials->item(0)->text().contains("crate_wood.png"));
		CHECK(dialog->findChild<QLabel*>("importPreview")->text().contains(QStringLiteral("39 × 39 × 39 units")));   // 1 m cube in game units
		dialog->findChild<QComboBox*>("importUnits")->setCurrentIndex(2);   // inches: tiny
		CHECK(dialog->findChild<QLabel*>("importPreview")->text().contains(QStringLiteral("1 × 1 × 1 units")));
		dialog->findChild<QComboBox*>("importUnits")->setCurrentIndex(0);
		dialog->findChild<QComboBox*>("importModelKind")->setCurrentIndex(1);   // physics
		QSignalSpy done(dialog, &ImportDialog::finished);
		REQUIRE(dialog->importNow());
		REQUIRE((done.count() > 0 || done.wait(10000)));
		CHECK(done.last().at(0).toBool());
		CHECK(dialog->resultText().contains("models/custom/crate.mdl"));
		for (const auto& file : outputs) CHECK(fs::exists(file));
		CHECK(fs::exists(mod / "materials/models/custom/crate_wood.vmt"));
		CHECK(fs::exists(mod / "modelsrc/custom/crate/crate.qc"));
#ifndef Q_OS_WIN
		QFile args(source("args.txt"));
		REQUIRE(args.open(QIODevice::ReadOnly));
		CHECK(QString::fromUtf8(args.readAll()).contains("crate.qc"));
#endif
		// Undo takes the model, its sources and its material out again.
		REQUIRE(dialog->undoLast());
		for (const auto& file : outputs) CHECK_FALSE(fs::exists(file));
		CHECK_FALSE(fs::exists(mod / "materials/models/custom/crate_wood.vmt"));
		// A failing compile: the reason is shown and nothing stays behind.
		writeText(source("fail"), "x");
		REQUIRE(dialog->setSourceFile(source("crate.obj")));
		done.clear();
		REQUIRE(dialog->importNow());
		REQUIRE((done.count() > 0 || done.wait(10000)));
		CHECK_FALSE(done.last().at(0).toBool());
		CHECK(dialog->resultText().contains("too many bones"));
		CHECK_FALSE(fs::exists(mod / "modelsrc/custom/crate/crate.qc"));
		CHECK_FALSE(fs::exists(mod / "materials/models/custom/crate_wood.vmt"));
		dialog->close();
		settle();
	}

	// A particle file from the Particles page: into particles/ and the manifest.
	{
		write(source("my_fire.pcf"), test::makePcf({"my_fire", "my_embers"}));
		window.raiseDock("Particles");
		settle();
		auto* dialog = dialogFor("importParticles");
		REQUIRE(dialog->setSourceFile(source("my_fire.pcf")));
		CHECK(dialog->findChild<QListWidget*>("importSystems")->count() == 2);
		REQUIRE(dialog->importNow());
		CHECK(fs::exists(mod / "particles/my_fire.pcf"));
		std::ifstream in(mod / "particles/particles_manifest.txt");
		const std::string manifest((std::istreambuf_iterator<char>(in)), {});
		CHECK(manifest.find("!particles/my_fire.pcf") != std::string::npos);
		dialog->close();
		settle();
	}

	// The game is read again: the imports are there; the mod lists them for "Remove import".
	settle();
	QTest::qWait(200);
	settle();
	REQUIRE(editor.game());
	CHECK(editor.game()->files.exists("sound/custom/engine_hum.wav"));
	CHECK(editor.game()->files.exists("materials/custom/red_bricks.vmt"));
	CHECK(editor.game()->files.exists("particles/my_fire.pcf"));
	const auto records = loadImports(mod);
	REQUIRE(records.size() == 3);   // sound, material, particles (the model was taken back)
	CHECK(records[1].asset == "custom/red_bricks");
	CHECK(window.findChild<QMenu*>("importMenu"));
}

TEST_CASE("problems: shiny materials without env_cubemap get one, unbuilt cubemaps open the compile with Cubemaps on") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	test::writeText(f.mod / "materials/test/shiny_floor.vmt",
	                "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"demo/concrete\"\n\t\"$envmap\" \"env_cubemap\"\n}\n");
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();

	auto [floor, floorRef] = edit::createBlock(editor.doc(), Bounds{{0, 0, 0}, {512, 512, 16}}, "TEST/SHINY_FLOOR");
	editor.commit(std::move(floor));
	auto [start, startId] = edit::placeEntity(editor.doc(), "info_player_start", {64, 64, 16}, editor.entities()->find("info_player_start"));
	editor.commit(std::move(start));
	(void)floorRef;
	(void)startId;
	const auto vmf = QDir(fromPath(f.mod)).filePath("mapsrc/shiny.vmf");
	REQUIRE(editor.saveMap(vmf));

	const auto button = [&](const QString& label) -> QPushButton* {
		window.raiseDock("Problems");
		window.problems()->runChecks();
		settle();
		for (auto* b : window.problems()->findChildren<QPushButton*>()) {
			if (b->text() == label) return b;
		}
		return nullptr;
	};
	const auto cubemapCount = [&] {
		return std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [](const Entity& e) { return e.classname() == "env_cubemap"; });
	};

	// 1. Shiny floor, no env_cubemap: one click places it above the player start, undoable.
	auto* place = button("Place env_cubemap");
	REQUIRE(place);
	CHECK(window.problems()->problemsText().contains("mirror the sky"));
	QTest::mouseClick(place, Qt::LeftButton);
	settle();
	CHECK(cubemapCount() == 1);
	CHECK_FALSE(button("Place env_cubemap"));
	editor.undo();
	settle();
	CHECK(cubemapCount() == 0);
	editor.redo();
	settle();
	CHECK(cubemapCount() == 1);
	REQUIRE(editor.saveMap(vmf));

	// 2. The compiled map in the game's maps folder has the env_cubemap but nothing built.
	const auto bsp = f.mod / "maps/shiny.bsp";
	std::filesystem::create_directories(bsp.parent_path());
	{
		const auto bytes = test::makeCubemapBsp(1, {"materials/maps/shiny/cubemapdefault.vtf"});
		std::ofstream out(bsp, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	}
	std::filesystem::last_write_time(toPath(vmf), std::filesystem::last_write_time(bsp) - std::chrono::minutes(2));
	auto* build = button("Build cubemaps");
	REQUIRE(build);
	CHECK(window.problems()->problemsText().contains("never built"));
	CHECK_FALSE(loadCompileOptions(editor.gameKey()).cubemaps);
	// The button opens the compile dialog with the Cubemaps step on (a question about errors or
	// saving is answered first); the test cancels the dialog.
	bool sawDialog = false, cubemapsOn = false;
	QTimer watcher;
	watcher.setInterval(50);
	QObject::connect(&watcher, &QTimer::timeout, [&] {
		QWidget* modal = QApplication::activeModalWidget();
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			if (auto* yes = box->button(QMessageBox::Yes)) yes->click();
			return;
		}
		if (auto* dialog = qobject_cast<CompileDialog*>(modal)) {
			sawDialog = true;
			if (auto* toggle = dialog->findChild<QCheckBox*>("cubemaps")) cubemapsOn = toggle->isChecked();
			dialog->reject();
		}
	});
	watcher.start();
	QTest::mouseClick(build, Qt::LeftButton);
	watcher.stop();
	settle();
	CHECK(sawDialog);
	CHECK(cubemapsOn);
	CHECK(loadCompileOptions(editor.gameKey()).cubemaps);
	CHECK(loadCompileOptions(editor.gameKey()).copy);

	// Built into the map: the problem is gone.
	{
		const auto bytes = test::makeCubemapBsp(1, {"materials/maps/shiny/cubemapdefault.vtf", "materials/maps/shiny/c64_64_80.vtf"});
		std::ofstream out(bsp, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	}
	std::filesystem::last_write_time(bsp, std::filesystem::last_write_time(toPath(vmf)) + std::chrono::minutes(5));
	CHECK_FALSE(button("Build cubemaps"));
}

TEST_CASE("layout checks, ropes and ladders: player start in the floor, NPCs without nodes, a rope and a ladder from the menu") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();

	// A floor and a wall; the player start sunk into the floor, an NPC without nodes.
	{
		Transaction t(editor.doc(), "Room");
		Solid floor = makeBlock(editor.doc(), box({-512, -512, -16}, {512, 512, 0}), "DEMO/CONCRETE");
		Solid wall = makeBlock(editor.doc(), box({-64, -128, 0}, {0, 128, 128}), "DEMO/CONCRETE");
		t.touchSolid({0, floor.id});
		t.touchSolid({0, wall.id});
		editor.doc().world.solids.push_back(floor);
		editor.doc().world.solids.push_back(wall);
		editor.commit(t.finish());
	}
	auto [startMade, startId] = edit::placeEntity(editor.doc(), "info_player_start", {256, 0, -10}, editor.entities()->find("info_player_start"));
	editor.commit(std::move(startMade));
	auto [npcMade, npcId] = edit::placeEntity(editor.doc(), "npc_citizen", {200, 200, 0}, editor.entities()->find("npc_citizen"));
	editor.commit(std::move(npcMade));
	(void)npcId;

	const auto clickFix = [&](const QString& label) {
		window.raiseDock("Problems");
		window.problems()->runChecks();
		settle();
		QPushButton* fix = nullptr;
		for (auto* button : window.problems()->findChildren<QPushButton*>()) {
			if (button->text() == label) fix = button;
		}
		REQUIRE_MESSAGE(fix, label.toStdString());
		QTest::mouseClick(fix, Qt::LeftButton);
		settle();
	};
	clickFix("Move onto the floor");
	CHECK(editor.doc().findEntity(startId)->origin().z == doctest::Approx(1));
	const auto nodesBefore = std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [](const Entity& e) { return e.classname() == "info_node"; });
	CHECK(nodesBefore == 0);
	clickFix("Place nodes around the NPCs");
	const auto nodes = std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [](const Entity& e) { return e.classname() == "info_node"; });
	CHECK(nodes >= 4);
	editor.undo();   // one undo step for all of them
	settle();
	CHECK(std::count_if(editor.doc().entities.begin(), editor.doc().entities.end(), [](const Entity& e) { return e.classname() == "info_node"; }) == 0);

	const auto menuAction = [&](const char* name) -> QAction* {
		for (auto* action : window.findChildren<QAction*>()) {
			if (action->objectName() == name) return action;
		}
		return nullptr;
	};
	// A rope between two entities: a move_rope chain, selected afterwards.
	auto [aMade, aId] = edit::placeEntity(editor.doc(), "info_target", {-200, 0, 200}, nullptr);
	editor.commit(std::move(aMade));
	auto [bMade, bId] = edit::placeEntity(editor.doc(), "info_target", {200, 0, 200}, nullptr);
	editor.commit(std::move(bMade));
	Selection pair;
	pair.entities = {aId, bId};
	editor.setSelection(pair);
	auto* rope = menuAction("brushesMakeRope");
	REQUIRE(rope);
	rope->trigger();
	settle();
	REQUIRE(editor.selection().entities.size() == 2);
	const auto* moveRope = editor.doc().findEntity(editor.selection().entities[0]);
	CHECK(moveRope->classname() == "move_rope");
	CHECK(moveRope->get("NextKey") == editor.doc().findEntity(editor.selection().entities[1])->get("targetname"));
	CHECK(ropeCurves(editor.doc()).size() == 1);
	window.view3D()->requestFrame();
	settle();

	// A ladder against the wall: the brush selected, then Make ladder.
	Solid ladderBrush = makeBlock(editor.doc(), box({0, -16, 0}, {8, 16, 128}), "DEMO/CONCRETE");
	{
		Transaction t(editor.doc(), "Ladder brush");
		t.touchSolid({0, ladderBrush.id});
		editor.doc().world.solids.push_back(ladderBrush);
		editor.commit(t.finish());
	}
	Selection ladderSel;
	ladderSel.solids.push_back({0, ladderBrush.id});
	editor.setSelection(ladderSel);
	auto* ladder = menuAction("brushesMakeLadder");
	REQUIRE(ladder);
	ladder->trigger();
	settle();
	REQUIRE(editor.selection().entities.size() == 3);
	const auto* useable = editor.doc().findEntity(editor.selection().entities[0]);
	CHECK(useable->classname() == "func_useableladder");
	CHECK(useable->get("point1") == "26 0 128");
	// Nothing selected: a message instead of a ladder.
	editor.clearSelection();
	const auto count = editor.doc().entities.size();
	ladder->trigger();
	CHECK(editor.doc().entities.size() == count);
}

TEST_CASE("small screens: every dialog and the window fit, the content scrolls, the buttons stay reachable") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();
	QSettings().setValue("start/show", false);

	// A 1366 x 768 laptop: what is left for a window without the task bar and title bar.
	const QRect area(8, 36, 1366 - 16, 768 - 36 - 48);
	setAvailableWindowAreaForTests(area);
	struct Restore {
		~Restore() { setAvailableWindowAreaForTests({}); }
	} restore;

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1600, 960);   // what it asks for on a big screen
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	settle();
	CHECK(window.width() <= area.width());
	CHECK(window.height() <= area.height());
	CHECK(window.minimumSizeHint().height() <= area.height());
	window.showStartPage();
	settle();
	CHECK(window.minimumSizeHint().height() <= area.height());   // the start page scrolls inside itself

	// Inside the area, no larger than it, and its button boxes visible inside the window - not
	// in the part that scrolls.
	const auto checkFits = [&](QDialog& dialog, const char* name) {
		INFO(name);
		dialog.show();
		REQUIRE(QTest::qWaitForWindowExposed(&dialog));
		settle();
		CHECK(dialog.width() <= area.width());
		CHECK(dialog.height() <= area.height());
		CHECK(area.contains(QRect(dialog.pos(), dialog.size())));
		auto* scroll = dialog.findChild<QScrollArea*>("dialogFitScroll", Qt::FindDirectChildrenOnly);
		for (auto* buttons : dialog.findChildren<QDialogButtonBox*>()) {
			if (!buttons->isVisibleTo(&dialog)) continue;
			const QRect inDialog(buttons->mapTo(&dialog, QPoint(0, 0)), buttons->size());
			CHECK(dialog.rect().contains(inDialog));
			if (scroll) CHECK_FALSE(scroll->isAncestorOf(buttons));
		}
		dialog.hide();
	};

	{
		// New project with every feature shown: much taller than the screen, so it scrolls.
		NewModDialog dialog(&editor, &window);
		if (auto* toggle = dialog.findChild<QPushButton*>("newModChooseFeatures"); toggle && !toggle->isChecked()) toggle->click();
		checkFits(dialog, "new project");
		auto* scroll = dialog.findChild<QScrollArea*>("dialogFitScroll", Qt::FindDirectChildrenOnly);
		REQUIRE(scroll);
		CHECK(dialog.height() <= area.height());
		CHECK(scroll->widget()->height() >= scroll->viewport()->height());
	}
	{
		GameSettings tools;
		CompileDialog dialog(home.filePath("arena.vmf"), home.filePath("game"), tools, &window, "test_game");
		checkFits(dialog, "compile");
	}
	{
		GameSettingsDialog dialog(&editor, editor.gameKey(), &window);
		checkFits(dialog, "game settings");
	}
	{
		FindDialog dialog(&editor, [] {}, &window);
		checkFits(dialog, "find");
	}
	{
		ReplaceMaterialsDialog dialog(&editor, {}, &window);
		checkFits(dialog, "replace materials");
	}
	{
		ShortcutDialog dialog(&window, &window);
		checkFits(dialog, "shortcuts");
	}
	for (const auto kind : {ImportDialog::Kind::Sound, ImportDialog::Kind::Material, ImportDialog::Kind::Model, ImportDialog::Kind::Particles}) {
		ImportDialog dialog(&editor, kind, &window);
		checkFits(dialog, "import");
	}
	{
		// Any dialog built on the fly: 80 rows of settings and OK/Cancel at the bottom.
		QDialog dialog(&window);
		auto* v = new QVBoxLayout(&dialog);
		for (int i = 0; i < 80; ++i) v->addWidget(new QLabel(QStringLiteral("Setting %1").arg(i)));
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		v->addWidget(buttons);
		checkFits(dialog, "on the fly");
		CHECK(dialog.height() == area.height());
		auto* scroll = dialog.findChild<QScrollArea*>("dialogFitScroll", Qt::FindDirectChildrenOnly);
		REQUIRE(scroll);
		CHECK(scroll->verticalScrollBar()->maximum() > 0);
		CHECK(buttons->button(QDialogButtonBox::Ok)->isVisibleTo(&dialog));
		// A short one stays small (no empty space, no scroll bar).
		QDialog small(&window);
		auto* sv = new QVBoxLayout(&small);
		sv->addWidget(new QLabel("Just one line"));
		sv->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok));
		checkFits(small, "small");
		CHECK(small.height() < 200);
	}
	{
		// Even a much smaller screen: the buttons stay reachable.
		setAvailableWindowAreaForTests(QRect(0, 0, 800, 480));
		NewModDialog dialog(&editor, &window);
		dialog.show();
		REQUIRE(QTest::qWaitForWindowExposed(&dialog));
		settle();
		CHECK(dialog.width() <= 800);
		CHECK(dialog.height() <= 480);
		auto* buttons = dialog.findChild<QDialogButtonBox*>();
		REQUIRE(buttons);
		CHECK(dialog.rect().contains(QRect(buttons->mapTo(&dialog, QPoint(0, 0)), buttons->size())));
		setAvailableWindowAreaForTests(area);
	}
}

TEST_CASE("sounds: a sound still being read when the game changes is finished first") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	auto* sounds = window.sounds();
	if (sounds->tree()->topLevelItemCount() == 0) {
		QSignalSpy indexed(sounds, &SoundBrowser::indexed);
		REQUIRE(indexed.wait(10000));
	}
	const auto wind = sounds->tree()->findItems("wind1.wav", Qt::MatchExactly | Qt::MatchRecursive);
	REQUIRE_FALSE(wind.isEmpty());
	// The decoding reads the game's files in the background: when the game is about to change
	// (its file system goes away), nothing of the browser may still be reading.
	// The pool's threads are kept busy so that the decoding is still waiting when the game changes.
	QSemaphore release;
	QList<QFuture<void>> busy;
	bool stillReading = false;
	QObject::connect(&editor, &Editor::gameAboutToChange, sounds, [&] {
		for (auto* watcher : sounds->findChildren<QFutureWatcherBase*>()) stillReading = stillReading || !watcher->isFinished();
		release.release(int(busy.size()));
	});
	for (int i = 0; i < QThreadPool::globalInstance()->maxThreadCount(); ++i) busy << QtConcurrent::run([&release] { release.acquire(); });
	sounds->tree()->setCurrentItem(wind.front());
	window.selectGame("sourcemods/mymod");   // the same mod again: the game is read anew
	for (auto& f : busy) f.waitForFinished();
	CHECK_FALSE(stillReading);
	REQUIRE(editor.game());
}

TEST_CASE("models page: export for editing writes the model as .smd files and a .qc, an animation alone from the Animations page") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	test::createDemo(f);
	Editor editor;
	editor.discover(fromPath(f.steam));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	REQUIRE(editor.game());
	window.resize(1400, 900);
	window.show();
	REQUIRE(QTest::qWaitForWindowExposed(&window));
	editor.newMap();
	const QString citizen = "models/humans/group01/male_07.mdl";

	window.showPage(MainWindow::Page::Models);
	auto* page = window.modelsPage();
	for (int i = 0; i < 200 && page->currentModel() != citizen; ++i) {
		page->selectModel(citizen);
		QTest::qWait(25);
	}
	REQUIRE(page->currentModel() == citizen);
	auto* exportButton = page->findChild<QPushButton*>("modelExport");
	REQUIRE(exportButton);
	exportButton->click();
	auto* dialog = page->findChild<ModelExportDialog*>("modelExportDialog");
	REQUIRE(dialog);
	CHECK(QDir::fromNativeSeparators(dialog->folder()).endsWith("modelsrc/humans/group01/male_07"));
	QTemporaryDir out;
	dialog->setFolder(out.path());
	QSignalSpy exported(dialog, &ModelExportDialog::exported);
	REQUIRE(dialog->exportNow());
	CHECK(dialog->busy());
	REQUIRE(exported.wait(30000));
	CHECK(exported.first().first().toBool());
	CHECK(dialog->resultText().startsWith(QString::fromUtf8("✓")));
	CHECK(dialog->findChild<QPushButton*>("modelExportOpen")->isVisible());
	CHECK(QFile::exists(out.filePath("male_07.qc")));
	CHECK_FALSE(QDir(out.filePath("anims")).entryList({"*.smd"}).isEmpty());
	if (const QString shots = qEnvironmentVariable("SME_TEST_SCREENSHOTS"); !shots.isEmpty()) {
		dialog->grab().save(QDir(shots).filePath("model_export.png"));
	}
	dialog->close();
	settle();

	// One animation from the Animations page: only it, with the model's reference.
	window.showPage(MainWindow::Page::Animations);
	auto* animations = window.animationsPage();
	animations->selectModel(citizen);
	for (int i = 0; i < 400 && !animations->animated(); ++i) QTest::qWait(25);
	REQUIRE(animations->animated());
	REQUIRE_FALSE(animations->animated()->sequences().empty());
	const QString sequence = QString::fromStdString(animations->animated()->sequences().front().name);
	auto* single = animations->exportSequence(sequence);
	REQUIRE(single);
	QTemporaryDir out2;
	single->setFolder(out2.path());
	QSignalSpy exported2(single, &ModelExportDialog::exported);
	REQUIRE(single->exportNow());
	REQUIRE(exported2.wait(30000));
	CHECK(exported2.first().first().toBool());
	CHECK(QDir(out2.filePath("anims")).entryList({"*.smd"}).size() == 1);
	CHECK_FALSE(QDir(out2.filePath("materials")).exists());
	single->close();
	settle();
}

namespace {

/// Watches for windows that pop up by accident: a widget shown before it is put into a layout is
/// briefly its own top-level window (a tiny empty "Mapsmith" window in the user's video). Such a
/// widget is shown without a parent and gets one right after; widgets the tests open on purpose
/// as windows keep having none.
/// Catches what the app opens outside itself (folders, web pages): the tests must not start a
/// file manager or browser (on CI one kept the test run from ending).
class ExternalOpenTrap : public QObject {
	Q_OBJECT

public:
	QList<QUrl> opened;

public slots:
	void open(const QUrl& url) { opened << url; }
};

class StrayWindowWatch : public QObject {
public:
	QStringList strays;

	bool eventFilter(QObject* object, QEvent* event) override {
		if (event->type() != QEvent::Show || !object->isWidgetType()) return false;
		auto* widget = static_cast<QWidget*>(object);
		if (!widget->isWindow() || widget->parentWidget()) return false;
		QPointer<QWidget> watched(widget);
		const QString name = QString::fromLatin1(widget->metaObject()->className()) + " '" + widget->objectName() + "'";
		QTimer::singleShot(0, this, [this, watched, name] {
			if (watched && watched->parentWidget()) strays << name;
		});
		return false;
	}
};

} // namespace

int main(int argc, char** argv) {
	QSurfaceFormat format;
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	QApplication app(argc, argv);
	QApplication::setOrganizationName("hammer-test-tests");
	QApplication::setApplicationName("Mapsmith Tests");
	StrayWindowWatch strays;
	app.installEventFilter(&strays);
	ExternalOpenTrap external;
	for (const char* scheme : {"file", "http", "https"}) QDesktopServices::setUrlHandler(QString::fromLatin1(scheme), &external, "open");
	doctest::Context context(argc, argv);
	const int result = context.run();
	for (const char* scheme : {"file", "http", "https"}) QDesktopServices::unsetUrlHandler(QString::fromLatin1(scheme));
	for (const auto& stray : strays.strays) {
		std::fprintf(stderr, "Shown as its own window before it had a parent: %s\n", qPrintable(stray));
	}
	return strays.strays.isEmpty() ? result : 1;
}

#include "test_app.moc"

TEST_CASE("samples: \"Use a sample\" points to the shipped sample files") {
	// The build writes the samples to build/samples; the test program in build/tests finds them
	// like Mapsmith.exe finds the samples folder next to it.
	const QString root = samplesFolder();
	REQUIRE_FALSE(root.isEmpty());
	CHECK(QFileInfo(QDir(root).filePath("README.txt")).isFile());
	const std::pair<ImportDialog::Kind, const char*> expected[] = {
		{ImportDialog::Kind::Sound, "hum_loop.wav"},
		{ImportDialog::Kind::Material, "terracotta_tiles.png"},
		{ImportDialog::Kind::Model, "crate/crate.obj"},
		{ImportDialog::Kind::Particles, "mapsmith_samples.pcf"},
	};
	for (const auto& [kind, file] : expected) {
		const QString folder = ImportDialog::sampleFolder(kind);
		CAPTURE(folder.toStdString());
		CHECK(QFileInfo(folder).isDir());
		CHECK(QFileInfo(QDir(folder).filePath(file)).isFile());
	}
	CHECK(QFileInfo(QDir(samplesFolder(SAMPLE_ANIMATIONS)).filePath("hl2_human_wave.smd")).isFile());
}

TEST_CASE("panels: short help with More, panels float as windows and come back, the layout is kept, the start is quick") {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());
	QSettings().clear();

	test::Fixture f;
	test::createFixture(f);
	// A class with a long description (real FGDs have whole paragraphs).
	{
		std::ofstream fgd(f.mod / "mymod.fgd", std::ios::app);
		fgd << "\n@PointClass base(Targetname) = logic_longtext : \"Keeps a long text. It goes on about many things nobody needs "
		       "while mapping, line after line, until the panel has no room left for the keys - these are the last words.\" []\n";
	}
	QElapsedTimer clock;
	clock.start();
	Editor editor;
	editor.discover(fromPath(f.steam));
	{
		MainWindow window(&editor);
		window.selectGame("sourcemods/mymod");
		REQUIRE(editor.game());
		window.setQuadLayout(false);
		window.resize(1400, 900);
		window.show();
		REQUIRE(QTest::qWaitForWindowExposed(&window));
		// 1. The start: window and game in a few seconds even with software OpenGL (a guard against
		// something slow sneaking in, not a benchmark); pages nobody looks at are read when shown.
		const qint64 startMs = clock.elapsed();
		INFO("window with game shown after " << startMs << " ms");
		CHECK(startMs < 8000);
		auto* menuEditor = window.findChild<MenuEditor*>();
		REQUIRE(menuEditor);
		auto* menuLazy = menuEditor->findChild<RefreshWhenShown*>();
		REQUIRE(menuLazy);
		CHECK(menuLazy->pending());
		window.showPage(MainWindow::Page::Project);
		window.projectTabs()->setCurrentWidget(menuEditor);
		settle();
		CHECK_FALSE(menuLazy->pending());
		window.showPage(MainWindow::Page::Map);
		settle();

		// 2. Object Properties: the description is short, "More..." shows all of it (remembered).
		window.raiseDock("Object Properties");
		const auto* longClass = editor.entities()->find("logic_longtext");
		REQUIRE(longClass);
		auto [placed, longId] = edit::placeEntity(editor.doc(), "logic_longtext", {0, 0, 64}, longClass);
		editor.commit(std::move(placed));
		Selection one;
		one.entities.push_back(longId);
		editor.setSelection(one);
		settle();
		auto* help = window.properties()->findChild<QLabel*>("propertyHelp");
		REQUIRE(help);
		CHECK(help->isVisible());
		CHECK(help->text().contains("Keeps a long text."));
		CHECK_FALSE(help->text().contains("last words"));
		CHECK(help->text().contains("#more"));
		emit help->linkActivated("#more");
		CHECK(help->text().contains("last words"));
		CHECK(help->text().contains("#less"));
		CHECK(QSettings().value("properties/helpExpanded").toBool());
		emit help->linkActivated("#less");
		CHECK_FALSE(help->text().contains("last words"));
		CHECK_FALSE(QSettings().value("properties/helpExpanded").toBool());

		// 3. A right click on a bottom panel's tab: its menu; "Float as window" makes a real window.
		auto* layout = window.dockLayout();
		REQUIRE(layout);
		auto* log = window.findChild<QDockWidget*>("Log");
		auto* assets = window.findChild<QDockWidget*>("Asset Browser");
		auto* problems = window.findChild<QDockWidget*>("Problems");
		REQUIRE(log);
		REQUIRE(assets);
		REQUIRE(problems);
		CHECK(window.tabifiedDockWidgets(log).contains(assets));
		window.raiseDock("Log");
		settle();
		QTabBar* bar = nullptr;
		int index = -1;
		for (auto* candidate : window.findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
			for (int i = 0; candidate->isVisible() && i < candidate->count(); ++i) {
				if (candidate->tabText(i) == "Log") {
					bar = candidate;
					index = i;
				}
			}
		}
		REQUIRE(bar);
		const QPoint at = bar->tabRect(index).center();
		CHECK(layout->dockAtTab(bar->mapToGlobal(at)) == log);
		QContextMenuEvent rightClick(QContextMenuEvent::Mouse, at, bar->mapToGlobal(at));
		QApplication::sendEvent(bar, &rightClick);
		settle();
		auto* menu = window.findChild<QMenu*>("dockMenu");
		REQUIRE(menu);
		for (const char* name : {"dockFloat", "dockReattach", "dockHome", "dockSetHome", "dockReset"}) {
			INFO(name);
			CHECK(menu->findChild<QAction*>(name));
		}
		CHECK(menu->findChild<QAction*>("dockFloat")->isEnabled());
		CHECK_FALSE(menu->findChild<QAction*>("dockReattach")->isEnabled());
		menu->findChild<QAction*>("dockFloat")->trigger();
		menu->close();
		settle();
		CHECK(log->isFloating());
		CHECK(DockLayout::isRealWindow(log));
		CHECK(log->isVisible());
		CHECK((log->windowFlags() & Qt::WindowMaximizeButtonHint));
		CHECK(log->titleBarWidget()->objectName() == "dockHeader");   // a header with the menu button
		CHECK(log->width() >= 640);
		log->showMaximized();
		settle();
		CHECK(log->isMaximized());
		log->showNormal();
		settle();

		// 4. Reattach: back into the bottom tabs.
		layout->reattach(log);
		settle();
		CHECK_FALSE(log->isFloating());
		CHECK(window.dockWidgetArea(log) == Qt::BottomDockWidgetArea);
		CHECK(window.tabifiedDockWidgets(log).contains(assets));
		CHECK(log->titleBarWidget()->objectName() != "dockHeader");   // the tab names it

		// 5. Moved to the left: "Back to original position" puts it next to the Asset Browser again.
		window.addDockWidget(Qt::LeftDockWidgetArea, log);
		settle();
		CHECK(window.dockWidgetArea(log) == Qt::LeftDockWidgetArea);
		layout->backToHome(log);
		settle();
		CHECK(window.dockWidgetArea(log) == Qt::BottomDockWidgetArea);
		CHECK(window.tabifiedDockWidgets(log).contains(assets));

		// 6. "Set this as original position": Problems as a window on its own is its home now.
		layout->floatAsWindow(problems);
		problems->setGeometry(120, 120, 700, 500);
		settle();
		layout->setHome(problems);
		layout->reattach(problems);
		settle();
		CHECK_FALSE(problems->isFloating());
		layout->backToHome(problems);
		settle();
		CHECK(problems->isFloating());
		CHECK(DockLayout::isRealWindow(problems));

		// The window may close with a floating panel on another page: the layout is saved as on the map.
		window.showPage(MainWindow::Page::Project);
		settle();
		CHECK(problems->isVisible());   // a floating panel stays on another page
		CHECK_FALSE(assets->isVisible());
		window.showPage(MainWindow::Page::Map);
		settle();
		layout->save();
	}
	{
		// 7. The next start: the same layout, Problems floating as a real window again.
		MainWindow second(&editor);
		second.resize(1400, 900);
		second.show();
		REQUIRE(QTest::qWaitForWindowExposed(&second));
		settle();
		auto* problems = second.findChild<QDockWidget*>("Problems");
		auto* assets = second.findChild<QDockWidget*>("Asset Browser");
		REQUIRE(problems);
		CHECK(problems->isFloating());
		CHECK(DockLayout::isRealWindow(problems));
		// 8. "Reset all panels": the factory layout, the own home forgotten.
		second.dockLayout()->resetToFactory();
		settle();
		CHECK_FALSE(problems->isFloating());
		CHECK(second.dockWidgetArea(problems) == Qt::BottomDockWidgetArea);
		CHECK(second.tabifiedDockWidgets(problems).contains(assets));
		CHECK_FALSE(QSettings().contains("window/dockHome/Problems"));
	}
}
