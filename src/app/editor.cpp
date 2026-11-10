// editor.cpp - Editor state and operations (see editor.h).

#include "editor.h"

#include <sme/grid_snap.h>

#include "startup_trace.h"

#include "enhancements.h"
#include "model_cache.h"
#include "scene_poser.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <chrono>

#include <sme/assets.h>
#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/autosave.h>
#include <sme/csg.h>
#include <sme/displacement.h>
#include <sme/doors.h>
#include <sme/visgroups.h>
#include <sme/sky_presets.h>
#include <sme/text.h>

namespace sme::app {

std::filesystem::path toPath(const QString& path) {
#ifdef _WIN32
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(path.toStdString());
#endif
}

QString fromPath(const std::filesystem::path& path) {
#ifdef _WIN32
	return QString::fromStdWString(path.generic_wstring());
#else
	return QString::fromStdString(path.generic_string());
#endif
}

Editor::Editor(QObject* parent) : QObject(parent), m_models(std::make_unique<ModelCache>()) {
	// Plans made before a change of the map (on another thread) are stale.
	connect(this, &Editor::documentChanged, this, [this] { ++m_docSerial; });
	m_poser = std::make_unique<ScenePoser>(this);
	QSettings settings;
	m_grid = settings.value("editor/grid", 16.0).toDouble();
	m_textureLock = settings.value("editor/textureLock", true).toBool();
	m_currentMaterial = settings.value("editor/material", m_currentMaterial).toString();
	m_snapping = settings.value("editor/snapping", true).toBool();
	m_objectSnap = settings.value("editor/objectSnap", true).toBool();
	m_shadows = settings.value("editor/shadows", true).toBool();
	m_reflections = settings.value("editor/reflections", true).toBool();
	m_showGrid3D = settings.value("view/grid3D", true).toBool();
	m_maxFps = std::max(0, settings.value("view/maxFps", 144).toInt());
	m_displace.mode = static_cast<DisplaceMode>(std::clamp(settings.value("displace/mode", int(DisplaceMode::Raise)).toInt(), 0, int(DisplaceMode::Alpha)));
	m_displace.radius = std::clamp(settings.value("displace/radius", 64.0).toDouble(), 4.0, 1024.0);
	m_displace.strength = std::clamp(settings.value("displace/strength", 25).toInt(), 1, 100);
	m_displace.power = std::clamp(settings.value("displace/power", 3).toInt(), 2, 4);
	m_displace.axis = static_cast<DisplaceSettings::Axis>(std::clamp(settings.value("displace/axis", 0).toInt(), 0, int(DisplaceSettings::Axis::Camera)));
	m_showEntityNames = settings.value("view/entityNames", true).toBool();
	m_showConnections = settings.value("view/connections", true).toBool();
	m_hiddenKinds = settings.value("view/hiddenKinds", 0u).toUInt() & ALL_OBJECT_KINDS;
	m_fidelityLook = std::clamp(settings.value("view/fidelityLook", -1).toInt(), -1, 1);
	// Hidden objects follow every change of the map (and of what was hidden by hand).
	connect(this, &Editor::documentChanged, this, [this] { m_hiddenDirty = true; });
	connect(this, &Editor::selectionChanged, this, [this] { m_hiddenDirty = true; });
	connect(this, &Editor::gameChanged, this, [this] { m_hiddenDirty = true; });
	m_recentMaterials = settings.value("editor/recentMaterials").toStringList();
	m_shapeOptions.kind = static_cast<ShapeOptions::Kind>(std::clamp(settings.value("shape/kind", 0).toInt(), 0, int(ShapeOptions::Kind::Stairs)));
	m_shapeOptions.sides = settings.value("shape/sides", m_shapeOptions.sides).toInt();
	m_shapeOptions.arc = settings.value("shape/arc", m_shapeOptions.arc).toDouble();
	m_shapeOptions.startAngle = settings.value("shape/start", m_shapeOptions.startAngle).toDouble();
	m_shapeOptions.wallWidth = settings.value("shape/width", m_shapeOptions.wallWidth).toDouble();
	m_shapeOptions.addHeight = settings.value("shape/addHeight", m_shapeOptions.addHeight).toDouble();
	m_shapeOptions.steps = settings.value("shape/steps", m_shapeOptions.steps).toInt();
	m_shapeOptions.fit = settings.value("shape/fit", m_shapeOptions.fit).toBool();
	m_rotationSnap = settings.value("editor/rotationSnap", 15.0).toDouble();
	m_randomYaw = settings.value("entities/randomYaw", false).toBool();
	m_enhancements = std::make_unique<GameEnhancements>(this);
}

Editor::~Editor() = default;

void Editor::discover(const QString& steamRoot) {
	m_discovery = discoverGames(steamRoot.isEmpty() ? SteamInstallation::detect() : SteamInstallation::load(toPath(steamRoot)));
}

const GameEntry* Editor::gameEntry() const {
	for (const auto& game : m_discovery.games) {
		if (QString::fromStdString(game.key()) == m_gameKey) {
			return &game;
		}
	}
	return nullptr;
}

GameSettings Editor::gameSettings(const QString& key) const {
	const QString gameKey = key.isEmpty() ? m_gameKey : key;
	const GameEntry* entry = nullptr;
	for (const auto& game : m_discovery.games) {
		if (QString::fromStdString(game.key()) == gameKey) {
			entry = &game;
		}
	}

	// Defaults: Hammer/Hammer++ config of this game, else the base game's bin folder.
	GameSettings defaults;
	if (entry) {
		for (const auto& fgd : entry->fgds) {
			defaults.fgds << fromPath(fgd);
		}
		const HammerGameConfig* hammer = nullptr;
		for (const auto& file : m_discovery.hammerConfigs) {
			for (const auto& config : file.games) {
				if (normalizePath(displayPath(config.gameDir)) == normalizePath(displayPath(entry->gameDir))) {
					hammer = &config;
				}
			}
		}
		const auto appId = entry->isSourceMod ? entry->baseAppId : entry->ownerAppId;
		const auto* app = m_discovery.steam.findApp(appId);
		const auto bin = app ? app->installDir / "bin" : std::filesystem::path{};
		const auto pick = [&](const std::filesystem::path& fromHammer, const char* exe) {
			if (!fromHammer.empty()) {
				return fromPath(fromHammer);
			}
			return bin.empty() ? QString{} : fromPath(bin / exe);
		};
		defaults.vbsp = pick(hammer ? hammer->vbsp : std::filesystem::path{}, "vbsp.exe");
		defaults.vvis = pick(hammer ? hammer->vvis : std::filesystem::path{}, "vvis.exe");
		defaults.vrad = pick(hammer ? hammer->vrad : std::filesystem::path{}, "vrad.exe");
		if (hammer && !hammer->gameExe.empty()) {
			defaults.gameExe = fromPath(hammer->gameExe);
		} else if (app) {
			for (const char* exe : {"hl2.exe", "bms.exe", "hl2_linux", "hl2.sh"}) {
				if (auto found = findChildI(app->installDir, exe)) {
					defaults.gameExe = fromPath(*found);
					break;
				}
			}
		}
		defaults.mapDir = fromPath(hammer && !hammer->mapDir.empty() ? hammer->mapDir : entry->gameDir / "mapsrc");
	}

	QSettings settings;
	settings.beginGroup("games/" + QString(gameKey).replace('/', '_'));
	GameSettings out;
	out.fgds = settings.value("fgds", defaults.fgds).toStringList();
	out.vbsp = settings.value("vbsp", defaults.vbsp).toString();
	out.vvis = settings.value("vvis", defaults.vvis).toString();
	out.vrad = settings.value("vrad", defaults.vrad).toString();
	out.gameExe = settings.value("gameExe", defaults.gameExe).toString();
	out.mapDir = settings.value("mapDir", defaults.mapDir).toString();
	out.launchOptions = settings.value("launchOptions", defaults.launchOptions).toString();
	return out;
}

void Editor::setGameSettings(const QString& key, const GameSettings& s) {
	QSettings settings;
	settings.beginGroup("games/" + QString(key).replace('/', '_'));
	settings.setValue("fgds", s.fgds);
	settings.setValue("vbsp", s.vbsp);
	settings.setValue("vvis", s.vvis);
	settings.setValue("vrad", s.vrad);
	settings.setValue("gameExe", s.gameExe);
	settings.setValue("mapDir", s.mapDir);
	settings.setValue("launchOptions", s.launchOptions);
	settings.endGroup();
	if (key == m_gameKey) {
		QString error;
		openGame(key, &error);
	}
}

bool Editor::openGame(const QString& key, QString* error) {
	const GameEntry* entry = nullptr;
	for (const auto& game : m_discovery.games) {
		if (QString::fromStdString(game.key()) == key) {
			entry = &game;
		}
	}
	if (!entry) {
		if (error) {
			*error = tr("Game not found: %1").arg(key);
		}
		return false;
	}
	m_gameKey = key;
	std::vector<std::filesystem::path> fgds;
	for (const auto& fgd : gameSettings(key).fgds) {
		fgds.push_back(toPath(fgd));
	}
	std::string loadError;
	auto game = loadGame(*entry, m_discovery.steam, fgds, &loadError);
	startupMark("game read (mounts, FGDs)");
	if (!game) {
		if (error) {
			*error = QString::fromStdString(loadError);
		}
		return false;
	}
	emit gameAboutToChange();
	m_models->setFileSystem(nullptr, nullptr);
	m_game = std::move(game);
	m_models->setFileSystem(&m_game->files, &m_game->entities);
	// mapbase-fidelity: its config gives the look values players get; it or its shaders tell
	// that the mod has the add-on.
	const auto cfg = m_game->files.readText("cfg/fidelity.cfg");
	m_lookDefaults = cfg ? parseCfgValues(*cfg) : std::map<std::string, std::string>{};
	m_hasFidelity = cfg.has_value() || m_game->files.exists("shaders/fxc/fidelity_parallax_ps30.vcs") ||
	                std::filesystem::exists(m_game->entry.gameDir / "bin" / "game_shader_dx6.dll");
	QSettings().setValue("editor/game", key);
	emit gameChanged();
	startupMark("game shown in the panels");
	emit message(tr("Game: %1 (%2 entity classes, %3 search paths)")
	                 .arg(QString::fromStdString(m_game->entry.title))
	                 .arg(m_game->entities.size())
	                 .arg(m_game->files.mounts().size()));
	for (const auto& note : entry->notes) {
		emit message(QString::fromStdString(note));
	}
	if (entry->usesMapbase && std::none_of(fgds.begin(), fgds.end(), [](const auto& fgd) { return isMapbaseFgd(fgd); })) {
		emit message(entry->mapbaseFgd.empty()
		                 ? tr("Warning: this mod uses Mapbase, but no Mapbase FGD was found - Mapbase entities (env_global_light, ...) are missing. "
		                      "Add Mapbase's halflife2.fgd in the game settings (⚙).")
		                 : tr("Warning: this mod uses Mapbase, but its FGD is not loaded - Mapbase entities (env_global_light, ...) are missing. "
		                      "Add %1 in the game settings (⚙).")
		                       .arg(fromPath(entry->mapbaseFgd)));
	}
	return true;
}

void Editor::newMap() {
	m_lastShape = {};
	m_footerModified = false;
	m_doc = MapDocument();
	m_history.clear();
	m_editStamps.clear();
	m_selection.clear();
	m_hidden.clear();
	m_hiddenVisGroups.clear();
	m_filePath.clear();
	m_recovered = false;
	emit documentReplaced();
	emit documentChanged();
	emit selectionChanged();
}

bool Editor::openMap(const QString& path, QString* error) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	const QByteArray data = file.readAll();
	VmfLoadReport report;
	try {
		m_doc = MapDocument::parse(std::string_view(data.constData(), static_cast<std::size_t>(data.size())), &report);
	} catch (const std::exception& e) {
		if (error) {
			*error = QString::fromStdString(e.what());
		}
		return false;
	}
	m_history.clear();
	m_editStamps.clear();
	m_lastShape = {};
	m_footerModified = false;
	m_selection.clear();
	m_hidden.clear();
	m_hiddenVisGroups = hiddenVisGroupsInFile(m_doc);   // groups that were hidden when it was saved
	m_hiddenDirty = true;
	m_filePath = path;
	m_recovered = false;
	emit documentReplaced();
	emit documentChanged();
	emit selectionChanged();
	emit message(tr("Opened %1: %2 brushes, %3 entities").arg(QFileInfo(path).fileName()).arg(m_doc.solidCount()).arg(m_doc.entities.size()));
	if (!report.clean()) {
		// The file was damaged (Hammer would refuse it) or had ids twice; say what was repaired.
		emit message(tr("The file needed repairs: %1 lines with stray quotes, %2 broken brushes left out, %3 missing or doubled ids renumbered. Saving writes a clean file.")
		                 .arg(report.repairedLines)
		                 .arg(report.skippedSolids)
		                 .arg(report.renumbered));
		for (const auto& note : report.notes) {
			emit message(QStringLiteral("  ") + QString::fromStdString(note));
		}
	}
	return true;
}

bool Editor::saveMap(const QString& path, QString* error) {
	// Scene files live in scenes/<map>/: a scene made before the first save (or kept through
	// Save As) moves to the folder of the name the map is saved under (one undo step).
	{
		const std::string base = QFileInfo(path).completeBaseName().toStdString();
		Change moved;
		for (const int id : choreoScenes(m_doc)) {
			const Entity* scene = m_doc.findEntity(id);
			const std::string wanted = choreoSceneFile(base, scene->get("targetname"));
			const std::string current = scene->get("SceneFile");
			if (current != wanted && (current.empty() || current.starts_with("scenes/"))) {
				auto change = edit::setKeyValue(m_doc, {id}, "SceneFile", wanted);
				moved.entities.insert(moved.entities.end(), change.entities.begin(), change.entities.end());
			}
		}
		if (!moved.empty()) {
			moved.name = "Scene file paths";
			commit(std::move(moved));
		}
	}
	// Hammer counts saves in versioninfo/mapversion and the world's mapversion.
	int version = 0;
	for (auto& block : m_doc.headerBlocks) {
		if (iequals(block.key, "versioninfo")) {
			version = std::atoi(block.get("mapversion", "0").c_str()) + 1;
			block.set("mapversion", std::to_string(version));
		}
	}
	if (version > 0) {
		m_doc.world.set("mapversion", std::to_string(version));
	}
	std::string text;
	if (visGroups(m_doc).empty()) {
		text = m_doc.serialize();
	} else {
		// Which groups are hidden goes into the file (visgroupshown), not into the undo history.
		MapDocument saved = m_doc;
		writeVisGroupShown(saved, m_hiddenVisGroups);
		text = saved.serialize();
	}
	// The previous version stays as <name>.vmx (one step back, besides undo and the autosaves).
	if (QSettings().value("editor/vmxBackup", true).toBool() && QFileInfo::exists(path)) {
		const QString backup = fromPath(backupPath(toPath(path)));
		QFile::remove(backup);
		QFile::copy(path, backup);
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()) ||
	    !file.commit()) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	m_filePath = path;
	m_history.markSaved();
	m_footerModified = false;
	m_recovered = false;
	emit documentChanged();
	emit message(tr("Saved %1").arg(QFileInfo(path).fileName()));
	// NPC scenes play from .vcd files in the mod: keep them in step with the saved map.
	if (QString sceneError; !writeSceneFiles(&sceneError)) {
		emit message(tr("Could not write the scene file %1").arg(sceneError));
	}
	return true;
}

bool Editor::openRecovered(const QString& autosavePath, const QString& originalPath, QString* error) {
	if (!openMap(autosavePath, error)) {
		return false;
	}
	m_filePath = originalPath;
	m_recovered = true;
	emit documentChanged();
	emit message(tr("Restored the autosave %1 - save to keep it").arg(QFileInfo(autosavePath).fileName()));
	return true;
}

namespace {

long long editKey(const SolidRef& ref) {
	return (static_cast<long long>(ref.entityId) << 32) ^ static_cast<unsigned int>(ref.solidId);
}

} // namespace

std::uint64_t Editor::editStamp(const SolidRef& ref) const {
	const auto it = m_editStamps.find(editKey(ref));
	return it == m_editStamps.end() ? 0 : it->second;
}

void Editor::stampEdits(const Change& change) {
	++m_editCounter;
	for (const auto& state : change.solids) {
		if (state.after) m_editStamps[editKey(state.ref)] = m_editCounter;
	}
	for (const auto& state : change.entities) {
		if (!state.after) continue;
		for (const auto& solid : state.after->solids) m_editStamps[editKey({state.entityId, solid.id})] = m_editCounter;
	}
}

void Editor::commit(Change change) {
	if (change.empty()) {
		return;
	}
	stampEdits(change);
	noteEdit(describeChange(change));
	m_history.push(std::move(change));
	m_selection.prune(m_doc);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::commitMerged(Change change) {
	if (change.empty()) {
		return;
	}
	stampEdits(change);
	noteEdit(describeChange(change), true);
	m_history.push(std::move(change), true);
	m_selection.prune(m_doc);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::undo() {
	const auto name = m_history.undoName();
	if (m_history.undo(m_doc)) {
		noteEdit("undo \"" + name + "\"\n");
		m_selection.prune(m_doc);
		emit documentChanged();
		emit selectionChanged();
		emit message(tr("Undo: %1").arg(QString::fromStdString(name)));
	}
}

void Editor::undoTo(std::size_t index) {
	if (index == m_history.index() || index > m_history.size()) {
		return;
	}
	const bool back = index < m_history.index();
	while (m_history.index() != index && (back ? m_history.undo(m_doc) : m_history.redo(m_doc))) {
	}
	m_selection.prune(m_doc);
	emit documentChanged();
	emit selectionChanged();
	const QString at = m_history.index() == 0 ? tr("the opened map") : QString::fromStdString(m_history.undoName());
	emit message(back ? tr("Undone back to: %1").arg(at) : tr("Redone up to: %1").arg(at));
}

void Editor::redo() {
	const auto name = m_history.redoName();
	if (m_history.redo(m_doc)) {
		noteEdit("redo \"" + name + "\"\n");
		m_selection.prune(m_doc);
		emit documentChanged();
		emit selectionChanged();
		emit message(tr("Redo: %1").arg(QString::fromStdString(name)));
	}
}

void Editor::setSelection(Selection selection) {
	m_selection = std::move(selection);
	m_selection.prune(m_doc);
	emit selectionChanged();
}

void Editor::clearSelection() {
	setSelection({});
}

void Editor::selectAll() {
	Selection all;
	for (const auto& solid : m_doc.world.solids) {
		all.solids.push_back({0, solid.id});
	}
	for (const auto& entity : m_doc.entities) {
		all.entities.push_back(entity.id);
	}
	setSelection(std::move(all));
}

const Selection& Editor::hidden() const {
	if (!m_hiddenDirty) {
		return m_hiddenAll;
	}
	m_hiddenAll = m_hidden;
	m_hiddenAll.solids.insert(m_hiddenAll.solids.end(), m_previewReplaced.begin(), m_previewReplaced.end());
	if (!m_hiddenVisGroups.empty()) {
		const auto grouped = hiddenByVisGroups(m_doc, m_hiddenVisGroups);
		m_hiddenAll.solids.insert(m_hiddenAll.solids.end(), grouped.solids.begin(), grouped.solids.end());
		m_hiddenAll.entities.insert(m_hiddenAll.entities.end(), grouped.entities.begin(), grouped.entities.end());
	}
	if (m_hiddenKinds != 0) {
		auto filtered = objectsOfKinds(m_doc, m_hiddenKinds, entities());
		m_hiddenAll.solids.insert(m_hiddenAll.solids.end(), filtered.solids.begin(), filtered.solids.end());
		m_hiddenAll.entities.insert(m_hiddenAll.entities.end(), filtered.entities.begin(), filtered.entities.end());
		m_hiddenAll.faces = std::move(filtered.faces);
	}
	m_hiddenDirty = false;
	return m_hiddenAll;
}

void Editor::setKindVisible(ObjectKind kind, bool visible) {
	const auto bit = static_cast<std::uint32_t>(kind);
	const std::uint32_t mask = visible ? (m_hiddenKinds & ~bit) : (m_hiddenKinds | bit);
	if (mask == m_hiddenKinds) {
		return;
	}
	m_hiddenKinds = mask;
	QSettings().setValue("view/hiddenKinds", m_hiddenKinds);
	// What disappears is no longer selected (it could not be seen while being changed).
	if (!visible) {
		const auto gone = objectsOfKinds(m_doc, bit, entities());
		std::erase_if(m_selection.solids, [&](const SolidRef& ref) { return gone.contains(ref); });
		std::erase_if(m_selection.entities, [&](int id) { return gone.containsEntity(id); });
	}
	m_hiddenDirty = true;
	emit selectionChanged();
	emit viewFilterChanged();
}

void Editor::setVisGroupVisible(int id, bool visible) {
	if (visible == visGroupVisible(id)) {
		return;
	}
	if (visible) {
		m_hiddenVisGroups.erase(id);
	} else {
		m_hiddenVisGroups.insert(id);
		// What disappears is no longer selected.
		const auto gone = hiddenByVisGroups(m_doc, {id});
		std::erase_if(m_selection.solids, [&](const SolidRef& ref) { return gone.contains(ref); });
		std::erase_if(m_selection.entities, [&](int e) { return gone.containsEntity(e); });
	}
	m_hiddenDirty = true;
	emit selectionChanged();
	emit viewFilterChanged();
}

std::vector<std::filesystem::path> Editor::instanceFolders() const {
	std::vector<std::filesystem::path> sources;
	if (const auto* entry = gameEntry()) {
		const auto mapDir = gameSettings().mapDir;
		if (!mapDir.isEmpty()) sources.push_back(toPath(mapDir));
		sources.push_back(entry->gameDir / "mapsrc");
		sources.push_back(entry->gameDir / "maps");
		// Newer branches keep map sources next to the game folder (sdk_content/maps).
		sources.push_back(entry->gameDir.parent_path() / "sdk_content" / "maps");
	}
	return instanceSearchFolders(m_filePath.isEmpty() ? std::filesystem::path{} : toPath(m_filePath), sources);
}

std::optional<std::filesystem::path> Editor::instanceFile(const Entity& funcInstance) const {
	return resolveInstanceFile(funcInstance.get("file"), instanceFolders());
}

const InstancePreview* Editor::instancePreview(const Entity& funcInstance) const {
	const auto file = instanceFile(funcInstance);
	if (!file) {
		return nullptr;
	}
	const auto doc = m_instanceFiles.load(*file);
	if (!doc) {
		return nullptr;
	}
	// The preview depends on the file (and its time, through the loaded document) and on every
	// key of the func_instance (origin, angles, fixups).
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(*file, ec).time_since_epoch().count();
	std::string key = file->string() + '|' + std::to_string(time);
	for (const auto& [k, v] : funcInstance.keyValues) {
		key += '|' + k + '=' + v;
	}
	if (const auto it = m_instancePreviews.find(key); it != m_instancePreviews.end()) {
		return &it->second;
	}
	if (m_instancePreviews.size() > 256) {
		m_instancePreviews.clear();   // edits of many instances: start over instead of growing
	}
	auto preview = buildInstancePreview(prepareInstance(*doc, instanceSettings(funcInstance), entities()), entities());
	return &m_instancePreviews.emplace(std::move(key), std::move(preview)).first->second;
}

bool Editor::collapseInstance(int funcInstanceId) {
	const Entity* entity = m_doc.findEntity(funcInstanceId);
	if (!entity || !isInstanceEntity(*entity)) {
		return false;
	}
	const auto file = instanceFile(*entity);
	std::string error;
	const auto doc = file ? m_instanceFiles.load(*file, &error) : nullptr;
	if (!doc) {
		emit message(file ? tr("Could not read the instance: %1").arg(QString::fromStdString(error))
		                  : tr("Instance file not found: %1").arg(QString::fromStdString(entity->get("file"))));
		return false;
	}
	Selection added;
	auto change = sme::collapseInstance(m_doc, funcInstanceId, *doc, entities(), &added);
	if (change.empty()) {
		return false;
	}
	commit(std::move(change));
	setSelection(std::move(added));
	emit message(tr("Instance collapsed: its content is now part of the map"));
	return true;
}

void Editor::reloadInstances() {
	m_instanceFiles.clear();
	m_instancePreviews.clear();
	emit documentChanged();   // the views build their geometry again
}

void Editor::hideSelection() {
	for (const auto& ref : m_selection.solids) {
		if (!m_hidden.contains(ref)) m_hidden.solids.push_back(ref);
	}
	for (const int id : m_selection.entities) {
		if (!m_hidden.containsEntity(id)) m_hidden.entities.push_back(id);
	}
	for (const auto& face : m_selection.faces) {
		if (!m_hidden.contains(face.solid)) m_hidden.solids.push_back(face.solid);
	}
	m_selection.clear();
	emit selectionChanged();
	emit message(tr("Hidden: %1 brushes, %2 entities (U shows them again)").arg(m_hidden.solids.size()).arg(m_hidden.entities.size()));
}

void Editor::hideUnselected() {
	Selection hidden;
	for (const auto& solid : m_doc.world.solids) {
		if (!m_selection.contains(SolidRef{0, solid.id})) hidden.solids.push_back({0, solid.id});
	}
	for (const auto& entity : m_doc.entities) {
		if (!m_selection.containsEntity(entity.id)) hidden.entities.push_back(entity.id);
	}
	m_hidden = std::move(hidden);
	emit selectionChanged();
}

void Editor::unhideAll() {
	m_hidden.clear();
	emit selectionChanged();
}

void Editor::setTool(Tool tool) {
	// Face mode is its own tool (like the texture tool in Hammer): picking any other tool
	// works on whole objects again.
	if (m_selectMode == SelectMode::Face) {
		m_tool = tool;
		setSelectMode(SelectMode::Object);
		return;
	}
	if (m_tool != tool) {
		cancelScenePoint();
		if (m_tool == Tool::Measure) clearMeasure();
		m_tool = tool;
		emit toolChanged();
	}
}

void Editor::setSelectMode(SelectMode mode) {
	if (m_selectMode == mode) {
		return;
	}
	m_selectMode = mode;
	if (mode == SelectMode::Face) {
		m_tool = Tool::Select;
	}
	// Switching modes converts the selection: faces -> their brushes, brushes -> all their faces.
	Selection converted;
	if (mode == SelectMode::Object) {
		for (const auto& face : m_selection.faces) {
			if (face.solid.entityId == 0) {
				if (!converted.contains(face.solid)) {
					converted.solids.push_back(face.solid);
				}
			} else if (!converted.containsEntity(face.solid.entityId)) {
				converted.entities.push_back(face.solid.entityId);
			}
		}
	} else {
		const auto addSolid = [&](const SolidRef& ref) {
			if (const auto* solid = m_doc.findSolid(ref)) {
				for (const auto& side : solid->sides) {
					converted.faces.push_back({ref, side.id});
				}
			}
		};
		for (const auto& ref : m_selection.solids) {
			addSolid(ref);
		}
		for (const int id : m_selection.entities) {
			if (const auto* entity = m_doc.findEntity(id)) {
				for (const auto& solid : entity->solids) {
					addSolid({id, solid.id});
				}
			}
		}
	}
	m_selection = std::move(converted);
	emit toolChanged();
	emit selectionChanged();
}

void Editor::setReflections(bool on) {
	if (m_reflections != on) {
		m_reflections = on;
		QSettings().setValue("editor/reflections", on);
		emit settingsChanged();
	}
}

void Editor::setShadows(bool on) {
	if (m_shadows != on) {
		m_shadows = on;
		QSettings().setValue("editor/shadows", on);
		emit settingsChanged();
	}
}

namespace {

bool setViewOption(bool& member, bool on, const char* key) {
	if (member == on) return false;
	member = on;
	QSettings().setValue(key, on);
	return true;
}

} // namespace

void Editor::setShowGrid3D(bool on) {
	if (setViewOption(m_showGrid3D, on, "view/grid3D")) emit settingsChanged();
}

void Editor::setMaxFps(int fps) {
	fps = std::max(0, fps);
	if (fps == m_maxFps) return;
	m_maxFps = fps;
	QSettings().setValue("view/maxFps", fps);
	emit settingsChanged();
}

void Editor::setShowEntityNames(bool on) {
	if (setViewOption(m_showEntityNames, on, "view/entityNames")) emit settingsChanged();
}

void Editor::setShowConnections(bool on) {
	if (setViewOption(m_showConnections, on, "view/connections")) emit settingsChanged();
}

bool Editor::fidelityLook() const {
	return m_fidelityLook < 0 ? m_hasFidelity : m_fidelityLook == 1;
}

void Editor::setFidelityLook(bool on) {
	if (fidelityLook() == on && m_fidelityLook >= 0) {
		return;
	}
	m_fidelityLook = on ? 1 : 0;
	QSettings().setValue("view/fidelityLook", m_fidelityLook);
	emit settingsChanged();
}

double Editor::lookValue(std::string_view cvar) const {
	return resolveLookValue(cvar, mapLookValues(m_doc));
}

std::map<std::string, double> Editor::lookValues() const {
	const auto mapValues = mapLookValues(m_doc);   // finds the map's look entities: once, not per value
	std::map<std::string, double> values;
	for (const auto& setting : lookSettings()) values[toLower(setting.cvar)] = resolveLookValue(setting.cvar, mapValues);
	return values;
}

double Editor::resolveLookValue(std::string_view cvar, const std::map<std::string, std::string>& mapValues) const {
	const std::string key = toLower(cvar);
	bool ok = false;
	if (const auto it = mapValues.find(key); it != mapValues.end()) {
		const double v = QString::fromStdString(it->second).toDouble(&ok);
		if (ok) return v;
	}
	if (const auto it = m_lookDefaults.find(key); it != m_lookDefaults.end()) {
		const double v = QString::fromStdString(it->second).toDouble(&ok);
		if (ok) return v;
	}
	const auto* setting = findLookSetting(cvar);
	return setting ? setting->fallback : 0.0;
}

void Editor::setRenderMode(RenderMode mode) {
	m_renderMode = mode;
	emit settingsChanged();
}

void Editor::setGrid(double grid) {
	m_grid = std::clamp(grid, 0.125, 1024.0);
	QSettings().setValue("editor/grid", m_grid);
	emit settingsChanged();
	emit message(tr("Grid: %1").arg(m_grid));
}

void Editor::setSnapping(bool on) {
	m_snapping = on;
	QSettings().setValue("editor/snapping", on);
	emit settingsChanged();
	emit message(on ? tr("Snapping on (grid %1, rotation %2°)").arg(m_grid).arg(m_rotationSnap) : tr("Snapping off - free moving and rotating"));
}

void Editor::setObjectSnap(bool on) {
	if (on == m_objectSnap) return;
	m_objectSnap = on;
	QSettings().setValue("editor/objectSnap", on);
	emit settingsChanged();
	emit message(on ? tr("Snap to objects on: dragged corners and sides stop at other objects") : tr("Snap to objects off - only the grid"));
}

void Editor::setLiveBounds(const std::optional<Bounds>& bounds) {
	const auto same = [](const std::optional<Bounds>& a, const std::optional<Bounds>& b) {
		if (a.has_value() != b.has_value()) return false;
		return !a || (a->min == b->min && a->max == b->max);
	};
	if (same(bounds, m_liveBounds)) return;
	m_liveBounds = bounds;
	emit liveBoundsChanged();
}

void Editor::setRotationSnap(double degrees) {
	m_rotationSnap = std::clamp(degrees, 0.1, 180.0);
	QSettings().setValue("editor/rotationSnap", m_rotationSnap);
	emit settingsChanged();
	emit message(tr("Rotation snap: %1°").arg(m_rotationSnap));
}

double Editor::snapAngle(double degrees) const {
	return m_snapping ? std::round(degrees / m_rotationSnap) * m_rotationSnap : degrees;
}

void Editor::setTextureLock(bool on) {
	m_textureLock = on;
	QSettings().setValue("editor/textureLock", on);
	emit settingsChanged();
}

void Editor::setCurrentMaterial(const QString& material) {
	m_currentMaterial = material.toUpper();
	QSettings().setValue("editor/material", m_currentMaterial);
	m_recentMaterials.removeAll(m_currentMaterial);
	m_recentMaterials.prepend(m_currentMaterial);
	while (m_recentMaterials.size() > 16) {
		m_recentMaterials.removeLast();
	}
	QSettings().setValue("editor/recentMaterials", m_recentMaterials);
	emit settingsChanged();
	emit recentMaterialsChanged();
}

void Editor::setCurrentEntityClass(const QString& classname) {
	setCurrentEntity(classname, {});
}

void Editor::setCurrentEntity(const QString& classname, std::vector<std::pair<std::string, std::string>> keys) {
	m_currentEntityClass = classname;
	m_currentEntityKeys = std::move(keys);
	emit settingsChanged();
}

bool Editor::armedBrushEntity() const {
	const auto* definition = entities() && !m_currentEntityClass.isEmpty() ? entities()->find(m_currentEntityClass.toStdString()) : nullptr;
	return definition && definition->isSolid();
}

void Editor::setRandomYaw(bool on) {
	m_randomYaw = on;
	QSettings().setValue("entities/randomYaw", on);
	emit settingsChanged();
}

void Editor::setCubemapRoomBox(int cubemapId, const Bounds& box) {
	const auto* obbClass = entities() ? entities()->find("parallax_obb") : nullptr;
	auto [change, id] = edit::setCubemapRoomBox(m_doc, cubemapId, box, obbClass);
	if (id == 0) {
		emit message(tr("The room box needs a size in every direction"));
		return;
	}
	commit(std::move(change));
}

void Editor::fitCubemapRoomBox(int cubemapId) {
	const Entity* cubemap = m_doc.findEntity(cubemapId);
	if (!cubemap) {
		return;
	}
	const Bounds room = roomAround(m_doc, cubemap->origin());
	if (room.size().x >= 2048 * 2 - 1 && room.size().y >= 2048 * 2 - 1) {
		emit message(tr("No walls found around the cubemap - the box got the largest size; drag its sides to fit"));
	}
	setCubemapRoomBox(cubemapId, room);
}

void Editor::removeCubemapRoomBox(int cubemapId) {
	commit(edit::removeCubemapRoomBox(m_doc, cubemapId));
}

void Editor::createBrushEntity(const Bounds& box) {
	const std::string classname = m_currentEntityClass.toStdString();
	const auto* definition = entities() ? entities()->find(classname) : nullptr;
	// Triggers, area portals ... get their tool texture, everything else the current material.
	std::string material = QString::fromStdString(edit::brushEntityMaterial(classname)).toUpper().toStdString();
	if (material.empty()) {
		material = m_currentMaterial.toStdString();
	}
	auto [change, id] = edit::createBrushEntity(m_doc, box, m_shapeOptions, classname, definition, material);
	if (id == 0) {
		emit message(tr("The %1 does not fit into this box - make it bigger or change the settings in the Tool panel")
		                 .arg(QString::fromStdString(shapeName(m_shapeOptions.kind))));
		return;
	}
	const std::string name = change.name;
	commit(std::move(change));
	setUpNewDoor(id, name);
	Selection selection;
	selection.entities.push_back(id);
	setSelection(std::move(selection));
}

void Editor::deleteSelection() {
	if (m_selectMode == SelectMode::Face) {
		emit message(tr("Faces cannot be deleted - switch to object mode (4)"));
		return;
	}
	commit(edit::remove(m_doc, m_selection));
}

void Editor::duplicateSelection() {
	if (!m_selection.hasObjects()) {
		return;
	}
	auto [change, copies] = edit::duplicate(m_doc, m_selection, {m_grid, m_grid, 0});
	m_history.push(std::move(change));
	m_selection = std::move(copies);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::copySelection() {
	if (!m_selection.hasObjects()) {
		emit message(tr("Select objects to copy"));
		return;
	}
	QApplication::clipboard()->setText(QString::fromStdString(edit::copyObjects(m_doc, m_selection)));
	const auto count = m_selection.solids.size() + m_selection.entities.size();
	emit message(tr("Copied %n object(s) - Ctrl+V pastes them under the mouse, Ctrl+Shift+V several at once", nullptr, int(count)));
}

void Editor::cutSelection() {
	if (!m_selection.hasObjects()) {
		return;
	}
	copySelection();
	deleteSelection();
}

bool Editor::paste(const PasteOptions& options) {
	const std::string text = QApplication::clipboard()->text().toStdString();
	if (!edit::looksLikeMapObjects(text)) {
		emit message(tr("The clipboard holds no map objects - copy brushes or entities first (Ctrl+C)"));
		return false;
	}
	std::string error;
	auto pasted = edit::pasteObjects(m_doc, text, options, entities(), &error);
	if (!pasted) {
		emit message(tr("Could not paste: %1").arg(QString::fromStdString(error)));
		return false;
	}
	commit(std::move(pasted->first));
	setSelection(std::move(pasted->second));
	return true;
}

std::optional<Bounds> Editor::clipboardBounds() const {
	const std::string text = QApplication::clipboard()->text().toStdString();
	return edit::looksLikeMapObjects(text) ? edit::copiedBounds(text, entities()) : std::nullopt;
}

void Editor::nudgeSelection(const Vec3& delta) {
	if (m_selection.hasObjects()) {
		commit(edit::translate(m_doc, m_selection, delta, m_textureLock));
	}
}

void Editor::rotateSelection(int axis, double degrees) {
	if (!m_selection.hasObjects()) {
		return;
	}
	// Rotate around the center snapped to half the grid, so brushes stay on the grid.
	const auto bounds = edit::selectionBounds(m_doc, m_selection, entities());
	commit(edit::rotate(m_doc, m_selection, Mat3::rotation(axis, degrees), snap(bounds.center(), snapGrid() / 2), m_textureLock));
}

void Editor::applyMaterialToSelection(const QString& material) {
	if (m_selection.empty()) {
		emit message(tr("Select faces (3) or objects first - or paint with Ctrl+right mouse"));
		return;
	}
	if (material.toUpper() != m_currentMaterial) {
		setCurrentMaterial(material);
	}
	commit(edit::applyMaterial(m_doc, m_selection, material.toUpper().toStdString()));
}

void Editor::tieSelectionToEntity(const QString& classname) {
	const auto* definition = entities() ? entities()->find(classname.toStdString()) : nullptr;
	auto [change, id] = edit::tieToEntity(m_doc, m_selection, classname.toStdString(), definition);
	if (id == 0) {
		emit message(tr("Select world brushes first"));
		return;
	}
	Selection selection;
	selection.entities.push_back(id);
	const std::string name = change.name;
	m_history.push(std::move(change));
	m_selection = std::move(selection);
	setUpNewDoor(id, name);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::setUpNewDoor(int id, const std::string& changeName) {
	// A door placed with the entity tool or tied from brushes works right away: it opens with +use,
	// swings around its edge (not the map origin) and makes the game's door sounds. Same undo step.
	const Entity* door = m_doc.findEntity(id);
	if (!door || !isDoor(*door) || door->solids.empty()) {
		return;
	}
	DoorSettings settings;
	settings.type = *doorType(door->classname());
	settings.slide = DoorSlide::Up;
	if (const auto* definition = entities() ? entities()->find(door->classname()) : nullptr) {
		if (const auto* speed = definition->property("speed"); speed && !speed->defaultValue.empty()) {
			settings.speed = std::atof(speed->defaultValue.c_str());
		}
	}
	settings.sounds = defaultSoundsFor(settings.type);
	Change defaults = edit::setDoor(m_doc, id, settings);
	defaults.name = changeName;
	m_history.push(std::move(defaults), true);
	emit documentChanged();
}

void Editor::setAssetNames(std::function<QStringList()> models, std::function<QStringList()> sounds) {
	m_modelNames = std::move(models);
	m_soundNames = std::move(sounds);
}

std::vector<std::string> Editor::defaultSoundsFor(DoorType type) const {
	std::vector<std::string> names;
	if (m_soundNames) {
		for (const auto& name : m_soundNames()) names.push_back(name.toStdString());
	}
	auto sounds = defaultDoorSounds(type, names);
	if (sounds.empty()) {
		sounds.assign(doorSoundSlots(type).size(), std::string());   // props: the model's own sounds
	}
	return sounds;
}

std::optional<Bounds> Editor::modelBounds(const std::string& model) const {
	if (model.empty() || !m_models) {
		return std::nullopt;
	}
	const auto geometry = m_models->get(QString::fromStdString(model));
	if (!geometry || geometry->bounds.empty()) {
		return std::nullopt;
	}
	return geometry->bounds;
}

void Editor::makeDoor(DoorType type, bool doubleDoor) {
	const std::string cls = doorClass(type);
	const auto* definition = entities() ? entities()->find(cls) : nullptr;
	DoorSettings settings;
	settings.type = type;
	settings.doubleDoor = doubleDoor;
	settings.slide = doubleDoor ? DoorSlide::Left : DoorSlide::Up;
	settings.closeAfter = type == DoorType::Prop ? -1 : 4;   // HL2's own doors stay open
	if (definition) {
		if (const auto* speed = definition->property("speed"); speed && !speed->defaultValue.empty()) {
			settings.speed = std::atof(speed->defaultValue.c_str());
		}
	}
	settings.sounds = defaultSoundsFor(type);
	std::optional<Bounds> bounds;
	if (type == DoorType::Prop) {
		std::vector<std::string> models;
		if (m_modelNames) {
			for (const auto& name : m_modelNames()) models.push_back(name.toStdString());
		}
		const auto doors = doorModels(models);
		if (doors.empty()) {
			emit message(tr("No door model found in this game - make a brush door instead, or set a model in the Door box"));
		} else {
			// HL2 names its left-hinged doors ..._left: prefer one of those as the start.
			const auto left = std::find_if(doors.begin(), doors.end(), [](const std::string& m) { return toLower(m).find("left") != std::string::npos; });
			settings.model = left != doors.end() ? *left : doors.front();
			bounds = modelBounds(settings.model);
		}
	}
	auto [change, ids] = edit::makeDoor(m_doc, m_selection, settings, definition, bounds ? &*bounds : nullptr);
	if (ids.empty()) {
		emit message(tr("Select world brushes first: the block becomes the door"));
		return;
	}
	m_history.push(std::move(change));
	Selection selection;
	selection.entities = ids;
	m_selection = std::move(selection);
	emit documentChanged();
	emit selectionChanged();
	emit message(tr("Door made: it opens when the player presses Use. Object Properties > Door sets how it opens."));
}

void Editor::beginConnect(int sourceId) {
	if (sourceId == 0 || !m_doc.findEntity(sourceId)) {
		return;
	}
	m_connectSource = sourceId;
	emit connectModeChanged();
	emit message(tr("Connect: click the entity this one should trigger (Esc cancels)"));
}

void Editor::cancelConnect() {
	if (m_connectSource == 0) {
		return;
	}
	m_connectSource = 0;
	emit connectModeChanged();
}

void Editor::finishConnect(int targetId) {
	const int source = m_connectSource;
	cancelConnect();
	if (source != 0 && targetId != 0 && m_doc.findEntity(targetId)) {
		emit connectRequested(source, targetId);
	}
}

// --- NPC scenes -------------------------------------------------------------

void Editor::setSceneFocus(const SceneFocus& focus) {
	if (focus.sceneId == m_sceneFocus.sceneId && focus.step == m_sceneFocus.step && focus.stepTime == m_sceneFocus.stepTime &&
	    focus.playTime == m_sceneFocus.playTime) {
		return;
	}
	m_sceneFocus = focus;
	emit sceneFocusChanged();
}

std::vector<int> Editor::visibleScenes() const {
	std::vector<int> scenes;
	const auto add = [&](int id) {
		if (id != 0 && std::find(scenes.begin(), scenes.end(), id) == scenes.end()) scenes.push_back(id);
	};
	for (const int id : m_selection.entities) {
		const Entity* entity = m_doc.findEntity(id);
		if (!entity) continue;
		if (isChoreoScene(*entity)) {
			add(id);
			continue;
		}
		// A point helper or trigger of a scene shows its scene.
		const std::string tag = entity->get(std::string(CHOREO_SCENE_KEY));
		if (!tag.empty() && tag != "1") {
			if (const Entity* scene = findNamedEntity(m_doc, tag); scene && isChoreoScene(*scene)) add(scene->id);
			continue;
		}
		const std::string name = entity->get("targetname");
		if (!name.empty()) {
			for (const int scene : choreoScenesOfActor(m_doc, name)) add(scene);
		}
	}
	return scenes;
}

void Editor::requestScenePoint(const QString& hint, std::function<void(const Vec3&)> done) {
	m_scenePoint = std::move(done);
	m_scenePointHint = hint;
	emit scenePointRequestChanged();
	if (!hint.isEmpty()) emit message(hint);
}

void Editor::cancelScenePoint() {
	if (!m_scenePoint) {
		return;
	}
	m_scenePoint = nullptr;
	emit scenePointRequestChanged();
}

void Editor::deliverScenePoint(const Vec3& point) {
	auto done = std::move(m_scenePoint);
	m_scenePoint = nullptr;
	emit scenePointRequestChanged();
	if (done) done(point);
}

bool Editor::writeSceneFiles(QString* error) {
	const GameEntry* game = gameEntry();
	if (!game || choreoScenes(m_doc).empty()) {
		return true;
	}
	std::string failed;
	writeChoreoFiles(m_doc, game->gameDir, &failed);
	if (!failed.empty()) {
		if (error) *error = QString::fromStdString(failed);
		return false;
	}
	return true;
}

QString Editor::mapName() const {
	return m_filePath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_filePath).completeBaseName();
}

QString Editor::entityModel(const Entity& entity) const {
	return QString::fromStdString(edit::entityModelKey(entity, entities()));
}

void Editor::showConnection(int entityId, int index) {
	if (!m_doc.findEntity(entityId) || entityId == 0) {
		return;
	}
	Selection selection;
	selection.entities.push_back(entityId);
	setSelection(std::move(selection));
	emit connectionFocusRequested(entityId, index);
}

void Editor::moveSelectionToWorld() {
	commit(edit::moveToWorld(m_doc, m_selection));
}

// --- Shapes ----------------------------------------------------------------------------

void Editor::setShapeOptions(const ShapeOptions& options) {
	m_shapeOptions = options;
	QSettings settings;
	settings.setValue("shape/kind", int(options.kind));
	settings.setValue("shape/sides", options.sides);
	settings.setValue("shape/arc", options.arc);
	settings.setValue("shape/start", options.startAngle);
	settings.setValue("shape/width", options.wallWidth);
	settings.setValue("shape/addHeight", options.addHeight);
	settings.setValue("shape/steps", options.steps);
	settings.setValue("shape/fit", options.fit);
	if (shapeEditable()) {
		// Rebuild the shape just made: its "Create" step is taken back (with what fitting it into
		// the map changed around it) and made again with the new settings - still one undo step.
		const auto shape = m_lastShape;
		if (m_history.undo(m_doc)) {
			ShapePlan plan = planShape(m_doc, m_doc, shape.box, m_shapeOptions, shape.material);
			if (!plan.ok()) plan = planShape(m_doc, m_doc, shape.box, ShapeOptions{}, shape.material);   // never leave nothing
			auto [change, refs] = applyShapePlan(m_doc, plan, shape.name);
			m_shapeLimits = plan.limits;
			m_lastShape = shape;
			m_lastShape.refs = refs;
			commit(std::move(change));
			m_lastShape.revision = m_history.revision();
			Selection selection;
			selection.solids = refs;
			setSelection(std::move(selection));
		}
	}
	emit settingsChanged();
}

bool Editor::shapeEditable() const {
	// Only right after making it: still in the block tool, nothing changed, the shape still selected.
	if (!m_lastShape.valid || m_tool != Tool::Block || m_history.revision() != m_lastShape.revision || m_lastShape.refs.empty()) {
		return false;
	}
	return m_selection.solids == m_lastShape.refs && m_selection.entities.empty() && m_selection.faces.empty();
}

void Editor::createShape(const Bounds& box) {
	const std::string material = m_currentMaterial.toStdString();
	const ShapePlan plan = planShape(m_doc, m_doc, box, m_shapeOptions, material);
	if (!plan.ok()) {
		emit message(tr("The %1 does not fit into this box - make it bigger or change the settings in the Tool panel")
		                 .arg(QString::fromStdString(shapeName(m_shapeOptions.kind))));
		return;
	}
	const std::string name = "Create " + shapeName(m_shapeOptions.kind);
	auto [change, refs] = applyShapePlan(m_doc, plan, name);
	m_shapeLimits = plan.limits;
	m_lastShape = {box, refs, name, material, 0, true};
	commit(std::move(change));
	m_lastShape.revision = m_history.revision();
	Selection selection;
	selection.solids = refs;
	setSelection(std::move(selection));
	if (plan.limits.limited()) {
		emit message(tr("%1: %2").arg(QString::fromStdString(shapeName(m_shapeOptions.kind)),
		                                QString::fromStdString(plan.limits.notes.front())));
	}
}

// --- Brush tools ---------------------------------------------------------------------------

void Editor::hollowSelection(double thickness) {
	auto [change, walls] = edit::hollow(m_doc, m_selection, thickness);
	if (change.empty()) {
		emit message(tr("Nothing hollowed: the walls (%1 units) are too thick for the selected brushes").arg(thickness));
		return;
	}
	commit(std::move(change));
	setSelection(std::move(walls));
	emit message(tr("Hollowed: %1 wall brushes").arg(m_selection.solids.size() + m_selection.entities.size()));
}

void Editor::carveWithSelection() {
	auto change = edit::carve(m_doc, m_selection);
	if (change.empty()) {
		emit message(tr("Nothing to carve: the selected brushes do not cut into other brushes"));
		return;
	}
	commit(std::move(change));
	emit message(tr("Carved - the selected brushes cut themselves out of the brushes they touched (delete them now if they were only for cutting)"));
}

void Editor::mergeSelection() {
	auto outcome = edit::merge(m_doc, m_selection);
	if (!outcome) {
		emit message(tr("Select two or more brushes to merge"));
		return;
	}
	commit(std::move(outcome->change));
	Selection selection;
	selection.solids.push_back(outcome->merged);
	setSelection(std::move(selection));
	emit message(outcome->exact ? tr("Merged into one brush")
	                            : tr("Merged into one brush - it also fills the space between them (Source brushes are always convex; Ctrl+Z undoes)"));
}

void Editor::roundSelectedEdge(double radius, int segments, bool allowMerge) {
	clearBrushPreview();
	const auto plan = planEdgeRounds(m_doc, m_doc, m_selection.faces, radius, segments, EdgeRoundOptions{allowMerge});
	auto change = applyEdgeRound(m_doc, plan, plan.segments > 1 ? "Round edge" : "Bevel edge");
	if (!change) {
		emit message(tr("Not rounded: %1").arg(QString::fromStdString(plan.problem)));
		return;
	}
	commit(std::move(*change));
	// The faces picked for the corner are gone or cut: select the brushes that came out of it
	// instead (face mode keeps an empty face selection).
	Selection selection;
	if (m_selectMode == SelectMode::Object) {
		for (const auto& replacement : plan.replacements) {
			for (const auto& piece : replacement.pieces) selection.solids.push_back({replacement.ref.entityId, piece.id});
		}
	}
	setSelection(std::move(selection));
	if (plan.clamped) {
		emit message(tr("Rounded with %1 units - the largest radius that fits here").arg(plan.radius));
	} else if (plan.raised) {
		emit message(tr("Rounded with %1 units - at this flat bend a smaller rounding would be thinner than half a unit").arg(plan.radius));
	}
}

RoadPlan Editor::planSelectedRoad(const RoadOptions& options) const {
	const GameFileSystem* files = m_game ? &m_game->files : nullptr;
	const auto size = [files](const std::string& material) {
		return files ? materialTextureSize(*files, material) : std::pair<int, int>{512, 512};
	};
	auto plan = planRoad(m_doc, roadFaces(m_doc, m_selection), options, size);
	m_roadReplay.clear();
	addRoadSettings(m_roadReplay, options, size(options.material));
	m_roadReplay.emplace_back("road.faces", formatFaces(m_selection.faces));
	m_roadReplay.emplace_back("road.solids", formatSolids(m_selection.solids));
	notePlan(describeRoadPlan(plan, options));
	return plan;
}

void Editor::applySelectedRoad(const RoadOptions& options) {
	clearBrushPreview();
	const GameFileSystem* files = m_game ? &m_game->files : nullptr;
	const auto size = [files](const std::string& material) {
		return files ? materialTextureSize(*files, material) : std::pair<int, int>{512, 512};
	};
	// Planned again with the map's own ids: curve pieces become real brushes.
	const auto plan = planRoad(m_doc, roadFaces(m_doc, m_selection), options, size, &m_doc);
	if (plan.empty()) {
		emit message(tr("No road: select the road's brushes (or their top faces) first"));
		return;
	}
	notePlan("applied:\n" + describeRoadPlan(plan, options));
	commit(edit::applyRoad(m_doc, plan));
	if (!plan.corners.empty() && !m_selection.solids.empty()) {
		// The curve pieces stand where the corner brush was: selected like it.
		Selection selection = m_selection;
		for (const auto& corner : plan.corners) {
			for (const auto& piece : corner.pieces) {
				const SolidRef ref{corner.original.entityId, piece.id};
				if (std::find(selection.solids.begin(), selection.solids.end(), ref) == selection.solids.end()) selection.solids.push_back(ref);
			}
		}
		setSelection(std::move(selection));
	}
	emit message(tr("Road laid over %n face(s)", nullptr, plan.faceCount()));
}

std::vector<RoadPath> Editor::selectedRoadPaths(RoadBuildOptions* suggested) const {
	return roadPathsFromSolids(m_doc, m_selection.solids, suggested);
}

Editor::RoadBuildJob Editor::roadBuildJob(const RoadBuildOptions& options, const std::vector<RoadPath>& paths, bool fromSelection) const {
	RoadBuildJob job;
	if (!m_docSnapshot || m_docSnapshotSerial != m_docSerial || m_docSnapshot->idCounters() != m_doc.idCounters()) {
		m_docSnapshot = std::make_shared<const MapDocument>(m_doc);
		m_docSnapshotSerial = m_docSerial;
	}
	job.doc = m_docSnapshot;
	job.idCounters = m_doc.idCounters();
	job.serial = m_docSerial;
	job.revision = m_history.revision();
	job.solids = m_selection.solids;
	job.paths = paths;
	job.options = options;
	job.fromSelection = fromSelection;
	// Texture sizes of what the plan may use: the road material, else the floor's tops.
	const GameFileSystem* files = m_game ? &m_game->files : nullptr;
	const auto size = [files](const std::string& material) {
		return files ? materialTextureSize(*files, material) : std::pair<int, int>{512, 512};
	};
	std::set<std::string> materials;
	if (!options.material.empty()) materials.insert(options.material);
	if (fromSelection && options.material.empty()) {
		for (const auto& ref : m_selection.solids) {
			const Solid* solid = m_doc.findSolid(ref);
			if (!solid) continue;
			for (const auto& side : solid->sides) {
				if (side.plane().normal.z > 0.7) materials.insert(side.material);
			}
			if (materials.size() > 32) break;
		}
	}
	for (const auto& material : materials) job.textureSizes[material] = size(material);
	return job;
}

Editor::RoadBuildResult Editor::runRoadBuildJob(const RoadBuildJob& job) {
	RoadBuildResult result;
	const auto t0 = std::chrono::steady_clock::now();
	try {
		MapDocument ids;
		ids.setIdCounters(job.idCounters);
		const auto& sizes = job.textureSizes;
		const TextureSizeFn size = [&sizes](const std::string& material) {
			const auto it = sizes.find(material);
			return it != sizes.end() ? it->second : std::pair<int, int>{512, 512};
		};
		if (!job.doc) return result;
		if (job.fromSelection) {
			result.plan = planRoadFromSelection(*job.doc, ids, job.solids, job.options, size, &result.street);
		} else {
			result.plan = planRoadBuild(*job.doc, ids, job.paths, job.options, size);
		}
	} catch (const std::exception& e) {
		result.plan = {};
		result.error = e.what();
	} catch (...) {
		result.plan = {};
		result.error = "unknown error";
	}
	result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	return result;
}

std::string Editor::roadJobKey(const RoadBuildJob& job) {
	std::vector<std::pair<std::string, std::string>> settings;
	std::vector<Vec3> points;
	if (!job.fromSelection && !job.paths.empty()) points = job.paths.front().points;
	addRoadBuildSettings(settings, job.options, job.fromSelection, points, {0, 0});
	settings.emplace_back("build.solids", formatSolids(job.solids));
	std::string key;
	for (const auto& [k, v] : settings) key += k + "=" + v + "\n";
	return key;
}

bool Editor::roadJobCurrent(const RoadBuildJob& job) const {
	return job.serial == m_docSerial && job.revision == m_history.revision() && job.idCounters == m_doc.idCounters();
}

void Editor::noteRoadBuildStarted(const RoadBuildJob& job) const {
	std::vector<Vec3> points;
	if (!job.fromSelection && !job.paths.empty()) points = job.paths.front().points;
	m_buildReplay.clear();
	const std::string material = job.options.material;
	const auto it = job.textureSizes.find(material);
	addRoadBuildSettings(m_buildReplay, job.options, job.fromSelection, points, it != job.textureSizes.end() ? it->second : std::pair<int, int>{512, 512});
	m_buildReplay.emplace_back("build.solids", formatSolids(job.solids));
	std::string text = "road builder: planning " + std::string(job.fromSelection ? "through the selected street" : "along clicked points") + ", " +
	                   std::to_string(job.solids.size()) + " brush(es) selected:";
	for (const auto& [k, v] : m_buildReplay) text += "\n  " + k + " = " + v;
	notePlan(text);
}

void Editor::noteRoadBuildDone(const RoadBuildJob& job, const RoadBuildResult& result) const {
	std::string text = (job.fromSelection ? describeRoadCorridor(result.street) : std::string()) + describeRoadBuild(result.plan);
	if (!result.error.empty()) text += "error: " + result.error + "\n";
	text += "planned in " + std::to_string(std::lround(result.milliseconds)) + " ms\n";
	notePlan(text);
}

RoadBuildPlan Editor::planRoadBuilder(const RoadBuildOptions& options, const std::vector<RoadPath>& paths, bool fromSelection, RoadCorridor* corridor) const {
	const auto job = roadBuildJob(options, paths, fromSelection);
	noteRoadBuildStarted(job);
	auto result = runRoadBuildJob(job);
	noteRoadBuildDone(job, result);
	if (corridor) *corridor = std::move(result.street);
	return std::move(result.plan);
}

void Editor::applyRoadBuilder(const RoadBuildOptions& options, const std::vector<RoadPath>& paths, bool fromSelection) {
	clearBrushPreview();
	auto job = roadBuildJob(options, paths, fromSelection);
	job.options.cancelled = nullptr;
	const auto result = runRoadBuildJob(job);
	applyRoadBuildJob(job, result);
}

void Editor::applyRoadBuildJob(const RoadBuildJob& job, const RoadBuildResult& result) {
	clearBrushPreview();
	if (!roadJobCurrent(job)) {
		// The map changed since: planned again with the map as it is.
		applyRoadBuilder(job.options, job.paths, job.fromSelection);
		return;
	}
	if (!result.plan.ok()) {
		emit message(result.error.empty() ? tr("No road: click at least two points (or select the street's floor brushes)")
		                                  : tr("No road: %1").arg(QString::fromStdString(result.error)));
		return;
	}
	notePlan("applied:\n" + (job.fromSelection ? describeRoadCorridor(result.street) : std::string()) + describeRoadBuild(result.plan));
	auto [change, refs] = edit::applyRoadBuild(m_doc, result.plan);
	// The plan's new ids came from a copy of the counters: the map's own catch up.
	m_doc.updateIdCounters();
	commit(std::move(change));
	Selection selection;
	selection.solids = refs;
	setSelection(std::move(selection));
	emit message(tr("Road built: %n brush(es)", nullptr, int(refs.size())));
}

FlowPlan Editor::planSelectedTextureFlow(const FlowOptions& options) const {
	const GameFileSystem* files = m_game ? &m_game->files : nullptr;
	const auto size = [files](const std::string& material) {
		return files ? materialTextureSize(*files, material) : std::pair<int, int>{512, 512};
	};
	auto plan = planTextureFlow(m_doc, flowFaces(m_doc, m_selection), options, size);
	m_flowReplay.clear();
	addFlowSettings(m_flowReplay, options, size(options.material));
	m_flowReplay.emplace_back("flow.faces", formatFaces(m_selection.faces));
	m_flowReplay.emplace_back("flow.solids", formatSolids(m_selection.solids));
	notePlan(describeFlowPlan(plan, options));
	return plan;
}

void Editor::applySelectedTextureFlow(const FlowOptions& options) {
	clearBrushPreview();
	const GameFileSystem* files = m_game ? &m_game->files : nullptr;
	const auto size = [files](const std::string& material) {
		return files ? materialTextureSize(*files, material) : std::pair<int, int>{512, 512};
	};
	// Planned again with the map's own ids (road corners cut into curve pieces become real brushes).
	const auto plan = planTextureFlow(m_doc, flowFaces(m_doc, m_selection), options, size, &m_doc);
	if (plan.empty()) {
		emit message(tr("No texture flow: select faces (or brushes) that touch each other first"));
		return;
	}
	notePlan("applied:\n" + describeFlowPlan(plan, options));
	commit(edit::applyTextureFlow(m_doc, plan));
	if (!plan.plan.corners.empty() && !m_selection.solids.empty()) {
		Selection selection = m_selection;
		for (const auto& corner : plan.plan.corners) {
			for (const auto& piece : corner.pieces) {
				const SolidRef ref{corner.original.entityId, piece.id};
				if (std::find(selection.solids.begin(), selection.solids.end(), ref) == selection.solids.end()) selection.solids.push_back(ref);
			}
		}
		setSelection(std::move(selection));
	}
	emit message(tr("Texture flow over %n face(s)", nullptr, plan.faceCount()));
}

EdgeRoundPlan Editor::planSelectedEdgeRound(double radius, int segments, bool allowMerge) const {
	MapDocument scratch;   // ids for the preview only; applying plans again with the map's own
	return planEdgeRounds(m_doc, scratch, m_selection.faces, radius, segments, EdgeRoundOptions{allowMerge});
}

GapFillPlan Editor::planSelectedGapFill(const GapFillOptions& options) const {
	MapDocument scratch;   // ids for the preview only (the map's own when applying)
	return planGapFill(m_doc, scratch, m_selection.solids, options);
}

bool Editor::applySelectedGapFill(const GapFillOptions& options) {
	clearBrushPreview();
	const auto plan = planGapFill(m_doc, m_doc, m_selection.solids, options);
	if (!plan.ok()) {
		emit message(tr("Nothing to fill: %1").arg(plan.notes.empty() ? tr("select the brushes around the gap") : QString::fromStdString(plan.notes.front())));
		return false;
	}
	auto [change, refs] = applyGapFill(m_doc, plan);
	commit(std::move(change));
	Selection selection;
	selection.solids = refs;
	setSelection(std::move(selection));
	emit message(tr("Gap filled: %n brush(es)", nullptr, int(refs.size())));
	return true;
}

void Editor::setPropPreview(const QString& model, std::vector<PropPlacement> placements) {
	if (placements.empty() && m_propPreview.empty()) return;
	m_propPreviewModel = model;
	m_propPreview = std::move(placements);
	emit propPreviewChanged();
}

int Editor::placeProps(const QString& classname, const QString& model, const std::vector<PropPlacement>& placements) {
	clearPropPreview();
	if (placements.empty() || model.isEmpty()) return 0;
	// The class's FGD defaults, as the entity tool gives them (spawnflags included).
	std::vector<std::pair<std::string, std::string>> defaults;
	if (const auto* catalog = entities()) {
		if (const auto* definition = catalog->find(classname.toStdString())) {
			for (const auto& property : definition->properties) {
				if (property.kind == EntityProperty::Kind::Flags) {
					std::uint64_t flags = 0;
					for (const auto& flag : property.flags) flags |= flag.enabledByDefault ? flag.value : 0;
					defaults.emplace_back(property.name, std::to_string(flags));
				} else if (!property.defaultValue.empty() && !iequals(property.name, "origin")) {
					defaults.emplace_back(property.name, property.defaultValue);
				}
			}
		}
	}
	auto entities = makePropEntities(m_doc, placements, classname.toStdString(), model.toStdString(), defaults);
	auto [change, ids] = applyPropPlacements(m_doc, std::move(entities), "Place props");
	commit(std::move(change));
	Selection selection;
	selection.entities = ids;
	setSelection(std::move(selection));
	emit message(tr("%n prop(s) placed", nullptr, int(ids.size())));
	return int(ids.size());
}

std::vector<Vec3> Editor::selectedEdgePath() const {
	return edgeChainPath(m_doc, selectedEdges());
}

void Editor::measureClick(const Vec3& point) {
	if (!m_measure.a || m_measure.b) {
		m_measure = {point, std::nullopt, std::nullopt};
	} else {
		m_measure.b = point;
		m_measure.hover.reset();
	}
	emit measureChanged();
}

void Editor::setMeasureHover(std::optional<Vec3> point) {
	if (!m_measure.a || m_measure.b) point.reset();
	if (point == m_measure.hover) return;
	m_measure.hover = point;
	emit measureChanged();
}

void Editor::clearMeasure() {
	if (!m_measure.a && !m_measure.b) return;
	m_measure = {};
	emit measureChanged();
}

void Editor::setBrushPreview(std::vector<Solid> solids, std::vector<SolidRef> replaced) {
	if (solids.empty() && replaced.empty() && m_brushPreview.empty() && m_previewReplaced.empty()) {
		return;
	}
	m_brushPreview = std::move(solids);
	m_previewReplaced = std::move(replaced);
	m_hiddenDirty = true;
	emit brushPreviewChanged();
}

void Editor::setVertexCorners(const std::vector<Vec3>& corners) {
	const bool same = corners.size() == m_vertexCorners.size() &&
	                  std::equal(corners.begin(), corners.end(), m_vertexCorners.begin(), [](const Vec3& a, const Vec3& b) { return length(a - b) < 0.01; });
	if (same) {
		return;
	}
	m_vertexCorners = corners;
	emit vertexCornersChanged();
}

std::vector<edit::EdgeFaces> Editor::selectedEdges() const {
	if (m_selection.faces.size() == 2 && m_selection.faces[0].solid == m_selection.faces[1].solid) {
		return {{m_selection.faces[0], m_selection.faces[1]}};
	}
	if (m_tool != Tool::Vertex || m_vertexCorners.size() < 2) {
		return {};
	}
	return edit::edgesAtCorners(m_doc, m_selection, m_vertexCorners);
}

bool Editor::shapeSelectedEdges(edit::EdgeShape shape, double size, int segments) {
	const auto edges = selectedEdges();
	if (edges.empty()) {
		emit message(tr("Pick an edge first: vertex tool, click the middle of an edge (Ctrl+click for more), or select the two faces next to it"));
		return false;
	}
	auto change = edit::shapeEdges(m_doc, edges, shape, size, segments);
	if (!change) {
		emit message(shape == edit::EdgeShape::Wedge ? tr("This edge cannot become a wedge")
		                                             : tr("The size is too big for the faces next to this edge"));
		return false;
	}
	commit(std::move(*change));
	setVertexCorners({});   // the corners moved
	return true;
}

void Editor::setDisplaceSettings(const DisplaceSettings& settings) {
	DisplaceSettings next = settings;
	next.radius = std::clamp(next.radius, 4.0, 1024.0);
	next.strength = std::clamp(next.strength, 1, 100);
	next.power = std::clamp(next.power, 2, 4);
	if (next.mode == m_displace.mode && next.radius == m_displace.radius && next.strength == m_displace.strength && next.power == m_displace.power &&
	    next.axis == m_displace.axis) {
		return;
	}
	m_displace = next;
	QSettings settingsFile;
	settingsFile.setValue("displace/mode", int(next.mode));
	settingsFile.setValue("displace/radius", next.radius);
	settingsFile.setValue("displace/strength", next.strength);
	settingsFile.setValue("displace/power", next.power);
	settingsFile.setValue("displace/axis", int(next.axis));
	emit displaceSettingsChanged();
}

bool Editor::createDisplacements(const std::vector<FaceRef>& faces) {
	int skipped = 0;
	auto change = edit::createDisplacements(m_doc, faces, m_displace.power, 0, &skipped);
	if (change.empty()) {
		emit message(faces.empty() ? tr("Select faces (or brushes) first, or click a face with the displacement tool's Create mode")
		                           : tr("Only faces with four corners that are not displacements yet can become one"));
		return false;
	}
	commit(std::move(change));
	if (skipped > 0) emit message(tr("%n face(s) skipped: not four corners, or already a displacement", nullptr, skipped));
	return true;
}

bool Editor::createDisplacementsOnSelection() {
	std::vector<FaceRef> faces = m_selection.faces;
	if (faces.empty()) {
		// Whole brushes: their faces that point up (the ground), like Hammer's usual first step.
		const auto addSolid = [&](const SolidRef& ref) {
			const Solid* solid = m_doc.findSolid(ref);
			if (!solid) return;
			for (const auto& face : buildBrushGeometry(*solid).faces) {
				if (face.normal.z > 0.7) faces.push_back({ref, solid->sides[static_cast<std::size_t>(face.sideIndex)].id});
			}
		};
		for (const auto& ref : m_selection.solids) addSolid(ref);
		for (const int id : m_selection.entities) {
			if (const Entity* entity = m_doc.findEntity(id)) {
				for (const auto& solid : entity->solids) addSolid({id, solid.id});
			}
		}
	}
	return createDisplacements(faces);
}

void Editor::destroySelectedDisplacements() {
	auto change = edit::destroyDisplacements(m_doc, edit::displacementFaces(m_doc, m_selection));
	if (change.empty()) {
		emit message(tr("Select the displacements (their brushes or faces) first"));
		return;
	}
	commit(std::move(change));
}

void Editor::sewDisplacements() {
	std::vector<FaceRef> faces = edit::displacementFaces(m_doc, m_selection);
	if (m_selection.empty()) {
		Selection all;
		for (const auto& solid : m_doc.world.solids) all.solids.push_back({0, solid.id});
		for (const auto& entity : m_doc.entities) {
			if (!entity.solids.empty()) all.entities.push_back(entity.id);
		}
		faces = edit::displacementFaces(m_doc, all);
	}
	int moved = 0;
	auto change = edit::sewDisplacements(m_doc, faces, &moved);
	if (change.empty()) {
		emit message(tr("Nothing to sew: no neighbouring displacements with different edges"));
		return;
	}
	commit(std::move(change));
	emit message(tr("Sewed %n vertex/vertices", nullptr, moved));
}

bool Editor::blendAlongSelection(const BlendNearOptions& options, bool replacePrevious) {
	if (replacePrevious && m_lastBlendRevision != 0 && m_history.revision() == m_lastBlendRevision && m_history.undoName() == "Blend along selection") {
		m_history.undo(m_doc);   // the dialog's previous values: replaced, not stacked
		m_lastBlendRevision = 0;
	}
	int count = 0;
	auto change = edit::blendAlongSelection(m_doc, m_selection, options, &count);
	if (change.empty()) {
		emit documentChanged();   // an undone previous blend must disappear from the view
		emit message(tr("Select the road or path (brushes, not displacements) - the blend is painted on the displacements around it"));
		return false;
	}
	commit(std::move(change));
	m_lastBlendRevision = m_history.revision();
	emit message(tr("Blend painted on %n displacement(s)", nullptr, count));
	return true;
}

void Editor::snapVerticesToGrid() {
	if (!m_selection.hasObjects()) {
		emit message(tr("Select the brushes whose corners should go onto the grid"));
		return;
	}
	int snapped = 0, refused = 0;
	auto change = edit::snapToGrid(m_doc, m_selection, m_grid, &snapped, &refused);
	if (!change.empty()) commit(std::move(change));
	if (snapped == 0 && refused == 0) {
		emit message(tr("All corners are on the %1-unit grid already").arg(m_grid));
	} else if (refused == 0) {
		emit message(tr("%n brush(es) snapped to the %1-unit grid", nullptr, snapped).arg(m_grid));
	} else {
		emit message(tr("%n brush(es) snapped; %1 left as they were (they would lose a face on this grid - try a finer one)", nullptr, snapped).arg(refused));
	}
}

namespace {

std::vector<int> selectedSideIds(const Selection& selection) {
	std::vector<int> out;
	for (const auto& face : selection.faces) out.push_back(face.sideId);
	return out;
}

} // namespace

bool Editor::overlayOnSelectedFaces(double tileSize) {
	if (m_selection.faces.empty()) {
		emit message(tr("Select the faces first (face mode, Ctrl+click adds faces)"));
		return false;
	}
	const std::string material = m_currentMaterial.toStdString();
	double aspect = 1;
	if (m_game) {
		const auto [w, h] = materialTextureSize(m_game->files, material);
		if (w > 0 && h > 0) aspect = double(w) / h;
	}
	int dropped = 0;
	const auto overlay = overlayOnFaces(collectSurfaceFaces(m_doc), selectedSideIds(m_selection), aspect, tileSize, &dropped);
	if (!overlay) {
		emit message(tr("No overlay: the selected faces point different ways"));
		return false;
	}
	const auto* definition = entities() ? entities()->find("info_overlay") : nullptr;
	commit(edit::placeOverlays(m_doc, {*overlay}, material, definition));
	if (dropped > 0) {
		emit message(tr("Overlay placed on %n face(s); %1 left out (turned away, or more than 64)", nullptr, int(overlay->sides.size())).arg(dropped));
	} else {
		emit message(tr("Overlay placed on %n face(s)", nullptr, int(overlay->sides.size())));
	}
	return true;
}

bool Editor::overlayStripOnSelectedFaces(const OverlayStripOptions& options, bool replacePrevious) {
	Selection faces = m_selection;
	if (replacePrevious && m_lastStripRevision != 0 && m_history.revision() == m_lastStripRevision && m_history.undoName() == "Overlay strip") {
		m_history.undo(m_doc);
		m_lastStripRevision = 0;
		faces = m_stripSelection;
	}
	m_lastStripCount = 0;
	if (faces.faces.empty()) {
		emit documentChanged();
		emit message(tr("Select the faces the strip runs along (face mode: the road pieces' tops, a kerb)"));
		return false;
	}
	const std::string material = m_currentMaterial.toStdString();
	OverlayStripOptions o = options;
	if (m_game) {
		const auto [w, h] = materialTextureSize(m_game->files, material);
		if (w > 0 && h > 0) o.textureAspect = double(w) / h;
	}
	const auto strip = planOverlayStrip(collectSurfaceFaces(m_doc), selectedSideIds(faces), o);
	if (strip.empty()) {
		emit documentChanged();
		emit message(tr("No strip: the selected faces have no surface under the band"));
		return false;
	}
	const auto* definition = entities() ? entities()->find("info_overlay") : nullptr;
	auto change = edit::placeOverlays(m_doc, strip, material, definition);
	change.name = "Overlay strip";
	commit(std::move(change));
	m_lastStripRevision = m_history.revision();
	m_stripSelection = faces;
	m_lastStripCount = static_cast<int>(strip.size());
	emit message(tr("Overlay strip of %n piece(s)", nullptr, m_lastStripCount));
	return true;
}

void Editor::clipSelection(const Plane& plane, edit::ClipKeep keep) {
	auto [change, parts] = edit::clip(m_doc, m_selection, plane, keep);
	if (change.empty()) {
		return;
	}
	commit(std::move(change));
	setSelection(std::move(parts));
}

void Editor::moveSelectedVertices(const std::vector<Vec3>& corners, const Vec3& delta) {
	std::vector<SolidRef> refs = m_selection.solids;
	for (const int id : m_selection.entities) {
		if (const auto* entity = m_doc.findEntity(id); entity && id != 0) {
			for (const auto& solid : entity->solids) refs.push_back({id, solid.id});
		}
	}
	auto outcome = edit::moveVertices(m_doc, refs, corners, delta);
	if (outcome.refused) {
		emit message(tr("Not moved: the brush would become flat"));
		return;
	}
	commit(std::move(outcome.change));
	if (outcome.cornersLost) {
		emit message(tr("The brush folded in - corners merged so it stays convex (Source brushes must be convex). Ctrl+Z undoes"));
	}
}

void Editor::createPolygonBrush(const std::vector<Vec3>& base, const Vec3& extrude) {
	std::vector<Vec3> points = base;
	for (const auto& p : base) points.push_back(p + extrude);
	auto solid = solidFromPoints(m_doc, points, m_currentMaterial.toStdString());
	if (!solid) {
		emit message(tr("The polygon needs at least three points that are not on one line"));
		return;
	}
	auto [change, refs] = edit::replaceSolids(m_doc, {}, {std::move(*solid)}, "Create polygon");
	commit(std::move(change));
	Selection selection;
	selection.solids = refs;
	setSelection(std::move(selection));
}

void Editor::flipSelection(int axis) {
	if (!m_selection.hasObjects()) {
		return;
	}
	const auto bounds = edit::selectionBounds(m_doc, m_selection, entities());
	commit(edit::mirror(m_doc, m_selection, axis, snap(bounds.center(), snapGrid() / 2), m_textureLock));
}

void Editor::snapSelectionToGrid() {
	if (!m_selection.hasObjects() || m_grid <= 0) {
		return;
	}
	const auto bounds = edit::selectionBounds(m_doc, m_selection, entities());
	const Vec3 delta = snap(bounds.min, m_grid) - bounds.min;
	if (length(delta) > 1e-9) {
		commit(edit::translate(m_doc, m_selection, delta, m_textureLock));
	}
}

void Editor::transformSelection(const Vec3& move, const Vec3& rotation, const Vec3& scale) {
	if (!m_selection.hasObjects()) {
		return;
	}
	// One undo step: the parts are folded into one ("Transform").
	const auto bounds = edit::selectionBounds(m_doc, m_selection, entities());
	const Vec3 center = bounds.center();
	std::vector<Change> parts;
	if (scale.x != 1 || scale.y != 1 || scale.z != 1) {
		Bounds to;
		const Vec3 half = bounds.size() * 0.5;
		to.add(center - Vec3{half.x * std::abs(scale.x), half.y * std::abs(scale.y), half.z * std::abs(scale.z)});
		to.add(center + Vec3{half.x * std::abs(scale.x), half.y * std::abs(scale.y), half.z * std::abs(scale.z)});
		parts.push_back(edit::scale(m_doc, m_selection, bounds, to));
	}
	if (rotation.x != 0 || rotation.y != 0 || rotation.z != 0) {
		const Mat3 turn = Mat3::rotation(2, rotation.z) * Mat3::rotation(1, rotation.y) * Mat3::rotation(0, rotation.x);
		parts.push_back(edit::rotate(m_doc, m_selection, turn, center, m_textureLock));
	}
	if (move.x != 0 || move.y != 0 || move.z != 0) {
		parts.push_back(edit::translate(m_doc, m_selection, move, m_textureLock));
	}
	for (std::size_t i = 0; i < parts.size(); ++i) {
		parts[i].name = "Transform";
		if (i == 0) {
			commit(std::move(parts[i]));
		} else {
			commitMerged(std::move(parts[i]));
		}
	}
}

void Editor::selectSimilar() {
	if (m_selection.empty()) {
		emit message(tr("Select an object first - Select similar then adds everything like it"));
		return;
	}
	auto similar = edit::similarObjects(m_doc, m_selection);
	const auto count = similar.solids.size() + similar.entities.size();
	setSelection(std::move(similar));
	emit message(tr("%n similar object(s) selected", nullptr, int(count)));
}

void Editor::setSky(const QString& name) {
	const std::string sky = name.trimmed().toStdString();
	if (sky.empty() || sky == m_doc.world.get("skyname")) {
		return;
	}
	// New light goes above the player start, else to the middle of the map.
	Bounds box;
	for (const auto& solid : m_doc.world.solids) {
		for (const auto& side : solid.sides) {
			for (const auto& p : side.planePoints) box.add(p);
		}
	}
	commit(applySky(m_doc, sky, box.empty() ? Vec3{} : box.center()));
}

bool Editor::selectById(int id) {
	auto found = edit::objectWithId(m_doc, id);
	if (found.empty()) {
		emit message(tr("There is no brush or entity with id %1").arg(id));
		return false;
	}
	setSelection(std::move(found));
	return true;
}

void Editor::dropSelectionToFloor() {
	auto change = edit::dropToFloor(m_doc, m_selection, entities());
	if (change.empty()) {
		emit message(tr("Nothing to drop: the selection already rests on something (or there is nothing below)"));
		return;
	}
	commit(std::move(change));
}

void Editor::makeWaterFromSelection(const std::vector<std::string>& materialNames) {
	if (!m_selection.hasObjects() && m_selection.faces.empty()) {
		emit message(tr("Select the brushes that should become water first"));
		return;
	}
	const std::string material = m_game ? pickWaterMaterial(m_game->files, m_currentMaterial.toStdString(), materialNames) : std::string();
	if (material.empty()) {
		emit message(tr("No water material found: pick one in the Asset Browser (category Water) and try again"));
		return;
	}
	auto result = edit::makeWater(m_doc, m_selection, material, entities());
	if (result.brushes == 0) {
		emit message(tr("Nothing to make into water: the selection has no brush with a top face"));
		return;
	}
	commit(std::move(result.change));
	QStringList added;
	if (result.lodControlId) added << QStringLiteral("water_lod_control");
	if (result.cubemapId) added << QStringLiteral("env_cubemap");
	emit message(tr("Made %1 brushes into water (%2)%3")
	                 .arg(result.brushes)
	                 .arg(QString::fromStdString(material))
	                 .arg(added.isEmpty() ? QString() : tr(", added %1").arg(added.join(", "))));
}

void Editor::selectTrackChain() {
	if (m_selection.entities.empty()) {
		emit message(tr("Select a path_track (or path_corner) first"));
		return;
	}
	Selection selection;
	for (const int id : m_selection.entities) {
		for (const int member : trackChain(m_doc, id)) {
			if (!selection.containsEntity(member)) selection.entities.push_back(member);
		}
	}
	setSelection(std::move(selection));
	emit message(tr("%1 entities in the path").arg(m_selection.entities.size()));
}

CordonSet Editor::cordons() const {
	return readCordons(m_doc);
}

void Editor::setCordons(const CordonSet& cordons) {
	writeCordons(m_doc, cordons);
	m_footerModified = true;
	emit documentChanged();
}

} // namespace sme::app

namespace sme::app {

// --- Editor report journal ----------------------------------------------------

const char* Editor::toolName(Tool tool) {
	switch (tool) {
		case Tool::Select: return "Select";
		case Tool::Move: return "Move";
		case Tool::Rotate: return "Rotate";
		case Tool::Scale: return "Scale";
		case Tool::Block: return "Block";
		case Tool::Entity: return "Entity";
		case Tool::Clip: return "Clip";
		case Tool::Vertex: return "Vertex";
		case Tool::Polygon: return "Polygon";
		case Tool::Displace: return "Displace";
		case Tool::Measure: return "Measure";
	}
	return "?";
}

void Editor::notePick(const PickTrace& trace) {
	++m_journalRevision;
	m_pickJournal.push_back(formatPickTrace(trace, ++m_pickCount));
	while (m_pickJournal.size() > 50) m_pickJournal.pop_front();
}

void Editor::noteDrag(const std::string& line) {
	++m_journalRevision;
	m_pickJournal.push_back(line + "\n");
	while (m_pickJournal.size() > 50) m_pickJournal.pop_front();
}

void Editor::noteEdit(const std::string& text, bool merged) {
	++m_journalRevision;
	const std::string entry = "#" + std::to_string(m_editCounter) + " " + text;
	// A paint stroke merges into one undo step: one entry, the latest.
	if (merged && !m_editJournal.empty() && m_editJournal.back().find(text.substr(0, text.find('\n'))) != std::string::npos) {
		m_editJournal.back() = entry;
		return;
	}
	m_editJournal.push_back(entry);
	while (m_editJournal.size() > 30) m_editJournal.pop_front();
}

void Editor::notePlan(std::string text) const {
	// The live preview plans on every change of the dialog: the same plan once.
	if (!m_planJournal.empty() && m_planJournal.back() == text) return;
	++m_journalRevision;
	m_planJournal.push_back(std::move(text));
	while (m_planJournal.size() > 6) m_planJournal.pop_front();
}

namespace {

std::string joined(const std::deque<std::string>& entries) {
	std::string out;
	for (const auto& entry : entries) out += entry + "\n";
	return out;
}

} // namespace

std::string Editor::pickJournal() const {
	return joined(m_pickJournal);
}

std::string Editor::editJournal() const {
	return joined(m_editJournal);
}

std::string Editor::planJournal() const {
	return joined(m_planJournal);
}

std::vector<std::pair<std::string, std::string>> Editor::replaySettings() const {
	std::vector<std::pair<std::string, std::string>> out;
	const Selection& hidden = this->hidden();
	out.emplace_back("hidden.solids", formatSolids(hidden.solids));
	std::string entities;
	for (const int id : hidden.entities) entities += (entities.empty() ? "" : ",") + std::to_string(id);
	out.emplace_back("hidden.entities", entities);
	out.emplace_back("hidden.faces", formatFaces(hidden.faces));
	out.emplace_back("hidden.kinds", std::to_string(m_hiddenKinds));
	out.emplace_back("selection.solids", formatSolids(m_selection.solids));
	std::string selected;
	for (const int id : m_selection.entities) selected += (selected.empty() ? "" : ",") + std::to_string(id);
	out.emplace_back("selection.entities", selected);
	out.emplace_back("selection.faces", formatFaces(m_selection.faces));
	out.emplace_back("grid", reportNumber(m_grid));
	out.emplace_back("snap", reportNumber(snapGrid()));
	out.emplace_back("objectSnap", m_objectSnap ? "1" : "0");
	out.emplace_back("textureLock", m_textureLock ? "1" : "0");
	out.insert(out.end(), m_roadReplay.begin(), m_roadReplay.end());
	out.insert(out.end(), m_buildReplay.begin(), m_buildReplay.end());
	out.insert(out.end(), m_flowReplay.begin(), m_flowReplay.end());
	return out;
}

} // namespace sme::app
