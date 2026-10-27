// test_app.cpp - Drives the real editor window with simulated mouse input: block tool,
// selection, move gizmo (and Shift-copy), undo, entity tool, face mode, autofix button,
// 2D block creation, save and reload.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "fixture.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QTableWidget>
#include <QLabel>
#include <QGroupBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QTreeWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <sme/brush.h>
#include <sme/material.h>
#include <sme/map_tools.h>

#include <functional>

#include "asset_browser.h"
#include "compile.h"
#include "compile_window.h"
#include "properties_panel.h"
#include "copy_paste.h"
#include "editor.h"
#include "main_window.h"
#include "material_preview.h"
#include "material_editor.h"
#include "panels.h"
#include "sound_browser.h"
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
	}

	// Entity tool: an info_player_start on top of the block stands on it.
	editor.clearSelection();
	editor.setCurrentEntityClass("info_player_start");
	editor.setTool(Editor::Tool::Entity);
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

		auto* smart = panel->findChild<QCheckBox*>();
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
		// Material editor: fields change the VMT, the preview draws it, Save writes into the mod.
		auto* materials = window.materialEditor();
		QSignalSpy previewed(materials, &MaterialEditor::previewUpdated);
		materials->openMaterial("demo/brickwall001");
		window.raiseDock("Material Editor");
		REQUIRE(previewed.wait(10000));
		CHECK(materials->value("$basetexture") == "demo/brickwall001");
		CHECK(materials->value("$surfaceprop") == "brick");
		settle();
		auto* preview = materials->findChild<MaterialPreview*>();
		REQUIRE(preview);
		const QImage shot = preview->grabFramebuffer();
		const QColor middle = shot.pixelColor(shot.width() / 2, shot.height() / 2);
		CHECK(middle.red() > middle.blue());   // brick red, not the grey background
		auto* reflection = materials->findChild<QComboBox*>("reflection");
		REQUIRE(reflection);
		reflection->setCurrentIndex(1);
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
		// Sounds tab: sound scripts and soundscapes; dropping them places the matching entity.
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
			for (auto* button : sounds->findChildren<QPushButton*>()) {
				if (button->text().startsWith("+")) button->click();
			}
			QSignalSpy reindexed(sounds, &SoundBrowser::indexed);
			for (auto* button : sounds->findChildren<QPushButton*>()) {
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

TEST_CASE("update check: release information and the version button") {
	const QByteArray json = R"({"tag_name": "v9.1.0", "html_url": "https://github.com/29j2003/hammer-test/releases/tag/v9.1.0",
		"body": "New: things", "draft": false, "prerelease": false, "assets": [
		{"name": "SourceMapEditor-9.1.0-windows.zip", "browser_download_url": "https://example.invalid/a.zip"},
		{"name": "SourceMapEditor-9.1.0-windows.zip.sha256", "browser_download_url": "https://example.invalid/a.zip.sha256"}]})";
	const auto release = parseRelease(json);
	REQUIRE(release);
	CHECK(release->version == "9.1.0");
	CHECK(release->zipName == "SourceMapEditor-9.1.0-windows.zip");
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

int main(int argc, char** argv) {
	QSurfaceFormat format;
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	QApplication app(argc, argv);
	QApplication::setOrganizationName("hammer-test-tests");
	QApplication::setApplicationName("Source Map Editor Tests");
	doctest::Context context(argc, argv);
	return context.run();
}
