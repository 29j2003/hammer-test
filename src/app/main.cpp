// main.cpp - Source Map Editor entry point.
//
// Options (mainly for automation and screenshots):
//   --steam DIR          use this Steam folder instead of the detected one
//   --game KEY           game/mod to open (e.g. 220/hl2, sourcemods/mymod)
//   --quad               start with four views
//   --select-class NAME  select the first entity of this class
//   --select-face        select the first face of the first brush (face mode)
//   --camera "x y z yaw pitch"
//   --render lit|unlit|wire
//   --dock NAME          raise a dock (Problems, Log, Outliner, ...)
//   --page NAME          show a page of the side bar (map, materials, sounds, particles, mod)
//   --pick-edge          vertex tool with an edge of the first brush picked
//   --particle NAME      show a particle system in the Particles browser (with --page particles)
//   --tool NAME          start with a tool (block, entity, ...)
//   --material NAME      open a material in the Material Editor
//   --check              run the problem checks after loading
//   --screenshot FILE    save a picture of the window after loading and quit
//   --no-update-check    do not ask GitHub for a newer version at start
//   FILE.vmf             map to open

#include <QApplication>
#include <QMap>
#include <QCommandLineParser>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>

#include "asset_browser.h"
#include "editor.h"
#include "main_window.h"
#include "material_editor.h"
#include "panels.h"
#include "particle_browser.h"
#include "style.h"
#include "viewport.h"

#include <sme/csg.h>

using namespace sme::app;

int main(int argc, char** argv) {
	QSurfaceFormat format;
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	format.setStencilBufferSize(8);
	format.setSamples(4);
	QSurfaceFormat::setDefaultFormat(format);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	QApplication app(argc, argv);
	QApplication::setOrganizationName("hammer-test");
	QApplication::setApplicationName("Source Map Editor");
	QApplication::setApplicationVersion(SME_VERSION);
	applyDarkStyle(app);

	QCommandLineParser parser;
	parser.setApplicationDescription("Modern 3D-first map editor for Source 1 games");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({"steam", "Steam folder.", "dir"});
	parser.addOption({"game", "Game or mod key.", "key"});
	parser.addOption({"quad", "Four views."});
	parser.addOption({"select-class", "Select the first entity of a class.", "class"});
	parser.addOption({"select-face", "Select the first face (face mode)."});
	parser.addOption({"camera", "Camera \"x y z yaw pitch\".", "camera"});
	parser.addOption({"render", "lit, unlit or wire.", "mode"});
	parser.addOption({"dock", "Raise a dock.", "name"});
	parser.addOption({"page", "Show a page of the side bar (map, materials, sounds, particles, mod).", "page"});
	parser.addOption({"pick-edge", "Vertex tool with the top front edge of the first brush picked."});
	parser.addOption({"particle", "Show a particle system in the Particles browser.", "name"});
	parser.addOption({"material", "Open a material in the Material Editor.", "name"});
	parser.addOption({"tool", "Start with a tool (select, move, rotate, scale, block, entity, clip, vertex, polygon).", "tool"});
	parser.addOption({"check", "Run the problem checks."});
	parser.addOption({"screenshot", "Save a screenshot and quit.", "file"});
	parser.addOption({"wait", "Milliseconds before the screenshot.", "ms", "2500"});
	parser.addOption({"no-update-check", "Do not check for a newer version at start."});
	parser.addPositionalArgument("map", "Map (.vmf) to open.");
	parser.process(app);

	Editor editor;
	editor.discover(parser.value("steam"));
	MainWindow window(&editor);

	QString game = parser.value("game");
	if (game.isEmpty()) {
		game = QSettings().value("editor/game").toString();
	}
	if (game.isEmpty() && !editor.discovery().games.empty()) {
		game = QString::fromStdString(editor.discovery().games.front().key());
	}
	if (!game.isEmpty()) {
		window.selectGame(game);
	}
	if (parser.isSet("quad")) {
		window.setQuadLayout(true);
	}
	window.show();

	if (!parser.positionalArguments().isEmpty()) {
		window.openMap(parser.positionalArguments().first());
	}
	if (parser.isSet("render")) {
		const QString mode = parser.value("render");
		editor.setRenderMode(mode == "unlit" ? Editor::RenderMode::Unlit : (mode == "wire" ? Editor::RenderMode::Wireframe : Editor::RenderMode::Lit));
	}

	// Things that need the views to exist first.
	QTimer::singleShot(200, &window, [&] {
		if (parser.isSet("camera")) {
			const auto parts = parser.value("camera").split(' ', Qt::SkipEmptyParts);
			if (parts.size() == 5) {
				auto& camera = window.view3D()->camera();
				camera.position = QVector3D(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat());
				camera.yaw = parts[3].toFloat();
				camera.pitch = parts[4].toFloat();
				window.view3D()->update();
			}
		}
		if (parser.isSet("select-class")) {
			for (const auto& entity : editor.doc().entities) {
				if (QString::fromStdString(entity.classname()) == parser.value("select-class")) {
					sme::Selection selection;
					selection.entities.push_back(entity.id);
					editor.setSelection(selection);
					break;
				}
			}
		}
		if (parser.isSet("select-face") && !editor.doc().world.solids.empty()) {
			editor.setSelectMode(Editor::SelectMode::Face);
			const auto& solid = editor.doc().world.solids.front();
			sme::Selection selection;
			selection.faces.push_back({{0, solid.id}, solid.sides.front().id});
			editor.setSelection(selection);
		}
		if (parser.isSet("dock")) {
			window.raiseDock(parser.value("dock"));
		}
		if (parser.isSet("page")) {
			static const QMap<QString, MainWindow::Page> PAGES = {{"map", MainWindow::Page::Map}, {"materials", MainWindow::Page::Materials},
				{"sounds", MainWindow::Page::Sounds}, {"particles", MainWindow::Page::Particles}, {"mod", MainWindow::Page::Mod}};
			window.showPage(PAGES.value(parser.value("page").toLower(), MainWindow::Page::Map));
		}
		if (parser.isSet("particle")) {
			// The systems are read in the background: try until they are there.
			auto* retry = new QTimer(&window);
			retry->setInterval(100);
			QObject::connect(retry, &QTimer::timeout, &window, [&window, retry, name = parser.value("particle")] {
				auto* particles = window.findChild<ParticleBrowser*>();
				if (!particles || particles->selectSystem(name) || retry->property("tries").toInt() > 100) retry->stop();
				retry->setProperty("tries", retry->property("tries").toInt() + 1);
			});
			retry->start();
		}
		if (parser.isSet("pick-edge") && !editor.doc().world.solids.empty()) {
			const auto& solid = editor.doc().world.solids.front();
			sme::Selection selection;
			selection.solids.push_back({0, solid.id});
			editor.setSelection(selection);
			editor.setTool(Editor::Tool::Vertex);
			sme::Bounds box;
			for (const auto& corner : sme::solidCorners(solid)) box.add(corner);
			editor.setVertexCorners({{box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z}});
		}
		if (parser.isSet("material")) {
			window.materialEditor()->openMaterial(parser.value("material"));
			window.raiseDock("Material Editor");
		}
		if (parser.isSet("tool")) {
			static const QMap<QString, Editor::Tool> TOOLS = {
				{"select", Editor::Tool::Select}, {"move", Editor::Tool::Move},     {"rotate", Editor::Tool::Rotate},
				{"scale", Editor::Tool::Scale},   {"block", Editor::Tool::Block},   {"entity", Editor::Tool::Entity},
				{"clip", Editor::Tool::Clip},     {"vertex", Editor::Tool::Vertex}, {"polygon", Editor::Tool::Polygon},
			};
			editor.setTool(TOOLS.value(parser.value("tool").toLower(), Editor::Tool::Select));
		}
	});
	// Newer version on GitHub? Asked once at start (Help > Check for updates at start turns it off).
	const bool automated = parser.isSet("screenshot") || parser.isSet("no-update-check") || qEnvironmentVariableIsSet("SME_NO_UPDATE_CHECK");
	if (!automated && QSettings().value("update/checkOnStart", true).toBool()) {
		QTimer::singleShot(1500, &window, [&window] { window.checkForUpdates(false); });
	}
	if (parser.isSet("check")) {
		QObject::connect(window.assets(), &AssetBrowser::materialsIndexed, &window, [&] { window.problems()->runChecks(); });
	}
	if (parser.isSet("screenshot")) {
		QTimer::singleShot(parser.value("wait").toInt(), &window, [&] {
			window.grab().save(parser.value("screenshot"));
			app.quit();
		});
	}
	return app.exec();
}
