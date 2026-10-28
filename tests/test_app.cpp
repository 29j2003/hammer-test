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
#include <QTabBar>
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
#include <QSettings>
#include <QStatusBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTabWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QFrame>
#include <QOpenGLWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QWheelEvent>

#include <sme/audio.h>
#include <sme/brush.h>
#include <sme/displacement.h>
#include <sme/entity_search.h>
#include <sme/text.h>
#include <sme/fidelity_look.h>
#include <sme/heightmap.h>
#include <sme/soundscape.h>
#include <sme/material.h>
#include <sme/texture.h>
#include <sme/map_tools.h>
#include <sme/shipping.h>
#include <sme/soundscape_area.h>

#include <cmath>
#include <cstdio>
#include <functional>

#include "asset_browser.h"
#include "autosaver.h"
#include "find_dialog.h"
#include "history_panel.h"
#include "replace_materials_dialog.h"
#include "compile.h"
#include "compile_window.h"
#include "properties_panel.h"
#include "ship_panel.h"
#include "toggle_switch.h"
#include "copy_paste.h"
#include "editor.h"
#include "main_window.h"
#include "mod_page.h"
#include "model_viewer.h"
#include "models_page.h"
#include "map_look_panel.h"
#include "material_preview.h"
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
#include "tool_panel.h"
#include "updater.h"
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
		CHECK(materials->value("$parallaxdepth") == "0.02");
		REQUIRE(heightsReady.wait(10000));
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
			CHECK(categoryTexts.filter("Doors and buttons").front().endsWith(" 2"));
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
		CHECK(prop.classname() == "prop_static");
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
		int frames = 0;
		const auto counting = QObject::connect(view, &QOpenGLWidget::frameSwapped, view, [&frames] { ++frames; });
		QElapsedTimer clock;
		clock.start();
		while (clock.elapsed() < 500) {
			view->camera().yaw += 0.1f;
			view->requestFrame();
			QApplication::processEvents();
		}
		settle();
		QObject::disconnect(counting);   // `frames` ends with this block
		CHECK(frames <= 40);   // 60 fps for half a second, with some slack
		CHECK(frames >= 3);
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
}

TEST_CASE("update check: release information and the version button") {
	const QByteArray json = R"({"tag_name": "v9.1.0", "html_url": "https://github.com/29j2003/hammer-test/releases/tag/v9.1.0",
		"body": "New: things", "draft": false, "prerelease": false, "assets": [
		{"name": "Mapsmith-9.1.0-windows.zip", "browser_download_url": "https://example.invalid/a.zip"},
		{"name": "Mapsmith-9.1.0-windows.zip.sha256", "browser_download_url": "https://example.invalid/a.zip.sha256"}]})";
	const auto release = parseRelease(json);
	REQUIRE(release);
	CHECK(release->version == "9.1.0");
	CHECK(release->zipName == "Mapsmith-9.1.0-windows.zip");
	CHECK(release->checksumUrl.toString().endsWith(".sha256"));
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
	CHECK(demoFolder->text(1) == "3");
	page->folders()->setCurrentItem(demoFolder);
	CHECK(page->list()->model()->rowCount() == 3);
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

namespace {

/// Watches for windows that pop up by accident: a widget shown before it is put into a layout is
/// briefly its own top-level window (a tiny empty "Mapsmith" window in the user's video). Such a
/// widget is shown without a parent and gets one right after; widgets the tests open on purpose
/// as windows keep having none.
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
	doctest::Context context(argc, argv);
	const int result = context.run();
	for (const auto& stray : strays.strays) {
		std::fprintf(stderr, "Shown as its own window before it had a parent: %s\n", qPrintable(stray));
	}
	return strays.strays.isEmpty() ? result : 1;
}
