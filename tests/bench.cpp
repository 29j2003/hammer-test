// bench.cpp - sme-bench: measures Mapsmith on a big generated map and mod (not part of the test run).
//
// Builds (once, cached in --data) the fixture Steam installation with a mod of 3000 brush materials
// (256x256 DXT1, folders like a real game) and a map with 20 000 brushes, 2000 entities, 500
// displacements and 300 overlays, then times what a mapper does: open the map, frames, selecting,
// moving a big selection with undo/redo, autosave, the checks, the Materials and Models pages, the
// live previews (building, road, round edge), Game enhancements, the review export and the compile
// guards. Times are of the UI thread (CPU) unless marked "wall"; deferred work (timers after an edit)
// is included by waiting for it.
//
//   sme-bench [--data DIR] [--only NAME] [--repeat N]
//
// Profiles of exactly the measured steps (with `--only NAME`): under valgrind --tool=callgrind
// --instr-atstart=no, or with perf: mkfifo ctl; SME_PERF_FIFO=ctl perf record --control=fifo:ctl -D -1 -g ...

#include "fixture.h"

#include <sme/brush.h>
#include <sme/building_builder.h>
#include <sme/checks.h>
#include <sme/decals.h>
#include <sme/displacement.h>
#include <sme/editing.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/map.h>
#include <sme/map_budget.h>
#include <sme/material_review.h>

#include "asset_browser.h"
#include "autosaver.h"
#include "editor.h"
#include "main_window.h"
#include "panels.h"
#include "plural_text.h"
#include "viewport.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListView>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define SME_CALLGRIND_ON() CALLGRIND_START_INSTRUMENTATION
#define SME_CALLGRIND_OFF() CALLGRIND_STOP_INSTRUMENTATION
#else
#define SME_CALLGRIND_ON()
#define SME_CALLGRIND_OFF()
#endif

using namespace sme;
using namespace sme::app;
namespace fs = std::filesystem;

namespace {

constexpr int MATERIALS = 3000;
constexpr int BRUSHES = 20000;
constexpr int ENTITIES = 2000;
constexpr int DISPLACEMENTS = 500;
constexpr int OVERLAYS = 300;

QString g_only;
int g_repeat = 3;
std::FILE* g_perfControl = nullptr;   // perf record --control=fifo:<SME_PERF_FIFO> -D -1

/// Collects a profile only while a step is measured (callgrind, or perf through its control fifo).
void profileOn() {
	SME_CALLGRIND_ON();
	if (g_perfControl) {
		std::fputs("enable\n", g_perfControl);
		std::fflush(g_perfControl);
	}
}
void profileOff() {
	if (g_perfControl) {
		std::fputs("disable\n", g_perfControl);
		std::fflush(g_perfControl);
	}
	SME_CALLGRIND_OFF();
}

/// CPU time of this thread in milliseconds (waiting does not count).
double threadMs() {
#ifdef CLOCK_THREAD_CPUTIME_ID
	timespec t{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
#else
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

bool wanted(const char* name) {
	return g_only.isEmpty() || QString::fromLatin1(name).contains(g_only, Qt::CaseInsensitive);
}

void report(const char* name, double ms, const char* unit = "ms") {
	std::printf("%-46s %10.1f %s\n", name, ms, unit);
	std::fflush(stdout);
}

/// Lets queued work and short timers (outliner, checks, budget: up to 300 ms) run.
void drain(int ms = 450) {
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
}

/// Median of `g_repeat` runs of `body` (UI-thread CPU, including what it leaves for the next `wait` ms).
void measure(const char* name, const std::function<void()>& body, int wait = 0, const std::function<void()>& after = {}) {
	if (!wanted(name)) return;
	std::vector<double> runs;
	for (int i = 0; i < g_repeat; ++i) {
		drain(100);
		profileOn();
		const double t0 = threadMs();
		body();
		if (wait > 0) drain(wait);
		const double t1 = threadMs();
		profileOff();
		runs.push_back(t1 - t0);
		if (after) after();
	}
	std::sort(runs.begin(), runs.end());
	report(name, runs[runs.size() / 2]);
}

/// Wall time of one run (work on other threads counts). `always`: a step the later ones need (runs
/// even when --only leaves it out, then without profile and report).
void measureWall(const char* name, const std::function<void()>& body, bool always = false) {
	if (!wanted(name)) {
		if (always) body();
		return;
	}
	profileOn();
	QElapsedTimer t;
	t.start();
	body();
	const double ms = static_cast<double>(t.nsecsElapsed()) / 1e6;
	profileOff();
	report(name, ms, "ms wall");
}

const char* FOLDERS[] = {"brick", "concrete", "metal", "wood", "plaster", "tile", "stone", "nature", "buildings", "glass"};

std::string materialName(int i) {
	const char* folder = FOLDERS[i % 10];
	return std::string("bench/") + folder + "/" + folder + "_" + std::to_string(i);
}

/// The mod's 3000 materials (VMT + 256x256 DXT1 VTF each), written once.
void writeMaterials(const fs::path& mod) {
	const auto vtf = [](int i) {
		return test::makeVtf(256, static_cast<unsigned char>(60 + (i * 37) % 180), static_cast<unsigned char>(60 + (i * 53) % 180),
		                     static_cast<unsigned char>(60 + (i * 71) % 180), true);
	};
	for (int i = 0; i < MATERIALS; ++i) {
		const std::string name = materialName(i);
		std::string vmt = "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"" + name + "\"\n";
		if (i % 4 == 0) vmt += "\t\"$surfaceprop\" \"brick\"\n";
		if (i % 10 == 9) vmt += "\t\"$translucent\" \"1\"\n";
		if (i % 7 == 0) vmt += "\t\"$envmap\" \"env_cubemap\"\n";
		vmt += "}\n";
		test::writeText(mod / "materials" / (name + ".vmt"), vmt);
		const auto bytes = vtf(i);
		std::ofstream out(mod / "materials" / (name + ".vtf"), std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
}

/// The big map: a grid of blocks, some with displacements and overlays, lots of entities.
MapDocument makeBigMap() {
	MapDocument doc;
	doc.world.set("skyname", "sky_day01_01");
	const int side = 142;   // 142 x 142 > 20 000
	for (int i = 0; i < BRUSHES; ++i) {
		const double x = (i % side) * 96.0 - 6800, y = (i / side) * 96.0 - 6800;
		Bounds box;
		box.add({x, y, 0});
		box.add({x + 64, y + 64, 64.0 + (i % 7) * 16.0});
		doc.world.solids.push_back(makeBlock(doc, box, i % 50 == 0 ? "tools/toolsnodraw" : materialName(i % MATERIALS)));
	}
	// Displacements on the tops of every 40th block; overlays on others.
	int displacements = 0, overlays = 0;
	for (int i = 0; i < BRUSHES && (displacements < DISPLACEMENTS || overlays < OVERLAYS); ++i) {
		auto& solid = doc.world.solids[static_cast<std::size_t>(i)];
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			auto& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			if (side.plane().normal.z < 0.9) continue;
			if (i % 40 == 1 && displacements < DISPLACEMENTS) {
				if (auto disp = makeDisplacement(face, 3, 4.0)) {
					storeDisplacement(side, *disp);
					++displacements;
				}
			} else if (i % 40 == 21 && overlays < OVERLAYS) {
				Entity overlay;
				overlay.id = doc.newEntityId();
				overlay.keyValues = {{"classname", "info_overlay"}};
				Vec3 center;
				for (const auto& p : face.vertices) center = center + p * (1.0 / static_cast<double>(face.vertices.size()));
				for (auto& [k, v] : overlayKeysOnFace(center, side.plane().normal, side.id, materialName(i % MATERIALS), 48, 48)) {
					overlay.set(k, v);
				}
				doc.entities.push_back(std::move(overlay));
				++overlays;
			}
		}
	}
	const char* classes[] = {"prop_static", "prop_static", "prop_static", "light", "info_node", "prop_physics", "ambient_generic", "logic_relay"};
	for (int i = 0; i < ENTITIES; ++i) {
		Entity e;
		e.id = doc.newEntityId();
		const char* cls = i == 0 ? "info_player_start" : classes[i % 8];
		const double x = (i % 45) * 300.0 - 6750, y = (i / 45) * 300.0 - 6750;
		e.keyValues = {{"classname", cls}, {"origin", std::to_string(x) + " " + std::to_string(y) + " 200"}};
		if (std::strncmp(cls, "prop_", 5) == 0) e.set("model", "models/props_demo/crate01.mdl");
		if (std::strcmp(cls, "logic_relay") == 0) e.set("targetname", "relay" + std::to_string(i));
		doc.entities.push_back(std::move(e));
	}
	// A wide floor slab for the road builder.
	Bounds slab;
	slab.add({-6800, 7000, 0});
	slab.add({-4800, 7512, 16});
	doc.world.solids.push_back(makeBlock(doc, slab, materialName(1)));
	return doc;
}

/// The cached data: fixture + demo + materials + map. Returns the map path.
fs::path prepare(test::Fixture& f, const fs::path& data) {
	const fs::path map = data / "big.vmf";
	const bool cached = fs::exists(data / "ready");
	if (!cached) {
		fs::remove_all(data);
		std::printf("Writing the test data to %s (once) ...\n", data.string().c_str());
		std::fflush(stdout);
	}
	test::createFixture(f, data / "fixture");
	f.owned = false;
	if (!cached) {
		test::createDemo(f);
		writeMaterials(f.mod);
		const auto doc = makeBigMap();
		std::ofstream out(map, std::ios::binary);
		out << doc.serialize();
		std::ofstream(data / "ready") << "1\n";
	}
	return map;
}

Selection selectionInBox(const MapDocument& doc, double x0, double y0, double x1, double y1) {
	Selection s;
	for (const auto& solid : doc.world.solids) {
		const auto b = buildBrushGeometry(solid).bounds;
		if (b.min.x >= x0 && b.max.x <= x1 && b.min.y >= y0 && b.max.y <= y1) s.solids.push_back({0, solid.id});
	}
	return s;
}

} // namespace

int main(int argc, char** argv) {
	QSurfaceFormat format;
	format.setVersion(3, 3);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	QApplication app(argc, argv);
	QApplication::setOrganizationName("hammer-test-bench");
	QApplication::setApplicationName("Mapsmith Bench");
	PluralTranslator plurals;
	QApplication::installTranslator(&plurals);

	fs::path data = fs::temp_directory_path() / "sme-bench";
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
		else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) g_only = QString::fromLocal8Bit(argv[++i]);
		else if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc) g_repeat = std::max(1, std::atoi(argv[++i]));
	}
	if (const char* fifo = std::getenv("SME_PERF_FIFO")) g_perfControl = std::fopen(fifo, "w");
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

	test::Fixture f;
	QElapsedTimer prepTimer;
	prepTimer.start();
	const fs::path mapPath = prepare(f, data);
	fs::remove_all(data / "autosave");   // else opening offers to recover the last run's autosaves
	std::printf("data ready in %.1f s: %d materials, %d brushes, %d entities, %d displacements, %d overlays\n\n", prepTimer.elapsed() / 1000.0, MATERIALS,
	            BRUSHES, ENTITIES, DISPLACEMENTS, OVERLAYS);

	// --- Core, no window -------------------------------------------------------------------
	MapDocument doc;
	{
		std::ifstream in(mapPath, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		measure("core: read the map (parse)", [&] { doc = MapDocument::parse(text); });
	}
	measure("core: write the map (serialize)", [&] { (void)doc.serialize(); });
	measure("core: copy the map", [&] { MapDocument copy = doc; (void)copy; });
	{
		Selection all;
		for (const auto& s : doc.world.solids) all.solids.push_back({0, s.id});
		measure("core: prune a selection of all brushes", [&] { Selection s = all; s.prune(doc); });
	}
	measure("core: map budget", [&] { (void)mapBudget(doc, budgetLimitsForApp(0)); });
	measure("core: compile guards (non-quad, oversized, edge-on)", [&] {
		(void)nonQuadDisplacements(doc);
		(void)oversizedDisplacements(doc);
		(void)edit::edgeOnTextureFaces(doc);
	});
	measure("core: surface faces for decals/overlays", [&] { (void)collectSurfaceFaces(doc); });

	// --- The window ------------------------------------------------------------------------
	Editor editor;
	editor.discover(QString::fromStdString(f.steam.string()));
	MainWindow window(&editor);
	window.selectGame("sourcemods/mymod");
	if (!editor.game()) {
		std::fprintf(stderr, "the fixture's mod was not found\n");
		return 1;
	}
	QSignalSpy indexed(window.assets(), &AssetBrowser::materialsIndexed);
	window.resize(1600, 1000);
	window.show();
	if (!QTest::qWaitForWindowExposed(&window)) std::fprintf(stderr, "the window was not shown\n");
	drain(300);

	const auto& game = *editor.game();
	measure("core: checks (checkMap, as Problems)", [&] {
		CheckCache cache;
		CheckContext context;
		context.entities = editor.entities();
		context.files = &game.files;
		context.cache = &cache;
		(void)checkMap(doc, context);
	});

	measureWall("open the map (until the first frame)", [&] {
		window.openMap(QString::fromStdString(mapPath.string()));
		window.view3D()->repaint();
		QApplication::processEvents();
	}, true);
	measure("after opening: deferred work (timers)", [] {}, 1500);
	if (wanted("materials: index")) {
		QElapsedTimer t;
		t.start();
		if (indexed.count() == 0) indexed.wait(120000);
		report("materials: index (wall, since window shown)", static_cast<double>(t.elapsed()), "ms wall (rest)");
	}

	Viewport* view = window.view3D();
	view->camera().position = {0, -7500, 1600};
	view->camera().pitch = -25;
	view->camera().yaw = 90;
	const auto frames = [&](const char* name) {
		measure(name, [&] {
			for (int i = 0; i < 10; ++i) {
				view->camera().yaw += 1.f;
				view->repaint();
			}
		});
	};
	editor.setShadows(true);
	drain(300);
	frames("view: 10 frames lit + shadows");
	editor.setShadows(false);
	drain(300);
	frames("view: 10 frames lit, no shadows");
	editor.setShadows(true);

	// Selecting.
	const auto& d = editor.doc();
	measure("select: one brush (+ deferred)", [&] {
		Selection s;
		s.solids.push_back({0, d.world.solids[500].id});
		editor.setSelection(std::move(s));
		view->repaint();
	}, 300);
	const Selection area = selectionInBox(d, -6800, -6800, -6800 + 96 * 45, -6800 + 96 * 45);   // ~2000 brushes
	std::printf("(area selection: %zu brushes)\n", area.solids.size());
	measure("select: 2000 brushes (+ deferred)", [&] {
		editor.setSelection(area);
		view->repaint();
	}, 300);
	measure("select: all (+ deferred)", [&] {
		editor.selectAll();
		view->repaint();
	}, 300);
	editor.setSelection({});
	drain(300);

	// Edits.
	measure("edit: move one brush, commit (+ deferred)", [&] {
		Selection s;
		s.solids.push_back({0, d.world.solids[500].id});
		editor.setSelection(std::move(s));
		editor.commit(edit::translate(editor.doc(), editor.selection(), {16, 0, 0}, false));
		view->repaint();
	}, 500);
	editor.setSelection(area);
	drain(300);
	measure("edit: move 2000 brushes, commit (+ deferred)", [&] {
		editor.commit(edit::translate(editor.doc(), editor.selection(), {0, 0, 16}, false));
		view->repaint();
	}, 500);
	measure("edit: undo of the 2000 (+ deferred)", [&] {
		editor.undo();
		view->repaint();
	}, 500, [&] { editor.redo(); drain(400); });
	editor.setSelection({});
	drain(300);

	// Autosave (the part on the UI thread).
	{
		Autosaver autosaver(&editor);
		measure("autosave: UI thread", [&] { autosaver.autosaveNow(true); });
		autosaver.waitForWrites();
		fs::remove_all(data / "autosave");
	}

	// Panels.
	measure("problems: run the checks (button)", [&] { window.problems()->runChecks(); });
	measure("budget: refresh", [&] { window.budget()->refresh(); });

	// Compile guard as Editor::commit runs it on a change of all brushes.
	measure("edit: commit a change of 2000 brushes' material", [&] {
		editor.setSelection(area);
		editor.commit(edit::applyMaterial(editor.doc(), editor.selection(), materialName(7 + static_cast<int>(threadMs()) % 5)));
	}, 500);
	editor.setSelection({});

	// Live previews.
	{
		Selection one;
		one.solids.push_back({0, d.world.solids[20].id});
		editor.setSelection(one);
		drain(200);
		measure("preview: building job (one block)", [&] {
			const auto job = editor.buildingJob(defaultBuildingOptions());
			(void)Editor::runBuildingJob(job);
		});
		Selection floor;
		floor.solids.push_back({0, d.world.solids.back().id});
		editor.setSelection(floor);
		drain(200);
		measure("preview: road job (through the slab)", [&] {
			RoadBuildOptions options;
			options.material = materialName(2);
			const auto job = editor.roadBuildJob(options, {}, true);
			(void)Editor::runRoadBuildJob(job);
		});
		// Round edge: two side faces of one block meeting at an edge.
		const Solid& block = d.world.solids[30];
		Selection faces;
		int picked = 0;
		for (const auto& side : block.sides) {
			const auto n = side.plane().normal;
			if ((std::abs(n.x) > 0.9 && picked == 0) || (std::abs(n.y) > 0.9 && picked == 1)) {
				faces.faces.push_back({{0, block.id}, side.id});
				++picked;
			}
		}
		editor.setSelection(faces);
		drain(200);
		measure("preview: round edge plan", [&] { (void)editor.planSelectedEdgeRound(32, 4); });
		editor.setSelection({});
	}

	// Pages.
	measure("page: Materials shown (+ deferred)", [&] { window.showPage(MainWindow::Page::Materials); }, 1000,
	        [&] { window.showPage(MainWindow::Page::Map); drain(300); });
	if (wanted("page: Materials search + scroll")) {
		window.showPage(MainWindow::Page::Materials);
		drain(500);
		auto* search = window.assets()->findChild<QLineEdit*>();
		QListView* list = nullptr;
		for (auto* l : window.assets()->findChildren<QListView*>()) {
			if (!list || l->model() && list->model() && l->model()->rowCount() > list->model()->rowCount()) list = l;
		}
		measure("page: Materials search typed (5 keys + deferred)", [&] {
			if (!search) return;
			search->clear();
			for (const char* text : {"b", "br", "bri", "bric", "brick"}) {
				search->setText(QString::fromLatin1(text));
				QApplication::processEvents();
			}
		}, 600, [&] { if (search) search->clear(); drain(400); });
		measureWall("page: Materials scrolled to the end (thumbnails loaded)", [&] {
			if (!list) return;
			auto* bar = list->verticalScrollBar();
			for (int v = 0; v <= bar->maximum(); v += std::max(1, bar->pageStep())) {
				bar->setValue(v);
				QApplication::processEvents();
			}
			drain(1500);
		});
		window.showPage(MainWindow::Page::Map);
		drain(300);
	}
	measure("page: Models shown (+ deferred)", [&] { window.showPage(MainWindow::Page::Models); }, 1000,
	        [&] { window.showPage(MainWindow::Page::Map); drain(300); });

	// Game enhancements and the review export (the work that runs in the background).
	measureWall("enhancements: candidates + plan (3000 materials)", [&] {
		const auto candidates = findEffectCandidates(game.files, f.mod);
		const auto plan = planEnhancements(game.files, f.mod, EnhancementSettings{}, {}, &candidates);
		std::printf("  (%zu changes)\n", plan.changes.size());
	});
	measureWall("review export: list + 300 entries with pictures", [&] {
		ReviewScope scope;
		scope.kind = ReviewScope::Kind::Folder;
		scope.folder = "bench/";
		const auto names = reviewMaterialList(game.files, scope, {});
		int n = 0;
		for (const auto& name : names) {
			if (++n > 300) break;
			auto entry = reviewEntry(game.files, name, nullptr);
			ReviewPictures pictures;
			reviewRelief(game.files, f.mod, entry, nullptr, true, &pictures);
		}
		std::printf("  (%zu materials listed)\n", names.size());
	});

	// No close(): the map is changed, closing would ask whether to save it.
	return 0;
}
