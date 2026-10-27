// editor.cpp - Editor state and operations (see editor.h).

#include "editor.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

#include <sme/brush.h>
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

Editor::Editor(QObject* parent) : QObject(parent) {
	QSettings settings;
	m_grid = settings.value("editor/grid", 16.0).toDouble();
	m_textureLock = settings.value("editor/textureLock", true).toBool();
	m_currentMaterial = settings.value("editor/material", m_currentMaterial).toString();
	m_snapping = settings.value("editor/snapping", true).toBool();
	m_recentMaterials = settings.value("editor/recentMaterials").toStringList();
	m_rotationSnap = settings.value("editor/rotationSnap", 15.0).toDouble();
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
	if (!game) {
		if (error) {
			*error = QString::fromStdString(loadError);
		}
		return false;
	}
	emit gameAboutToChange();
	m_game = std::move(game);
	QSettings().setValue("editor/game", key);
	emit gameChanged();
	emit message(tr("Game: %1 (%2 entity classes, %3 search paths)")
	                 .arg(QString::fromStdString(m_game->entry.title))
	                 .arg(m_game->entities.size())
	                 .arg(m_game->files.mounts().size()));
	return true;
}

void Editor::newMap() {
	m_doc = MapDocument();
	m_history.clear();
	m_selection.clear();
	m_hidden.clear();
	m_filePath.clear();
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
	try {
		m_doc = MapDocument::parse(std::string_view(data.constData(), static_cast<std::size_t>(data.size())));
	} catch (const std::exception& e) {
		if (error) {
			*error = QString::fromStdString(e.what());
		}
		return false;
	}
	m_history.clear();
	m_selection.clear();
	m_hidden.clear();
	m_filePath = path;
	emit documentReplaced();
	emit documentChanged();
	emit selectionChanged();
	emit message(tr("Opened %1: %2 brushes, %3 entities").arg(QFileInfo(path).fileName()).arg(m_doc.solidCount()).arg(m_doc.entities.size()));
	return true;
}

bool Editor::saveMap(const QString& path, QString* error) {
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
	const std::string text = m_doc.serialize();
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
	emit documentChanged();
	emit message(tr("Saved %1").arg(QFileInfo(path).fileName()));
	return true;
}

void Editor::commit(Change change) {
	if (change.empty()) {
		return;
	}
	m_history.push(std::move(change));
	m_selection.prune(m_doc);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::commitMerged(Change change) {
	if (change.empty()) {
		return;
	}
	m_history.push(std::move(change), true);
	m_selection.prune(m_doc);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::undo() {
	const auto name = m_history.undoName();
	if (m_history.undo(m_doc)) {
		m_selection.prune(m_doc);
		emit documentChanged();
		emit selectionChanged();
		emit message(tr("Undo: %1").arg(QString::fromStdString(name)));
	}
}

void Editor::redo() {
	const auto name = m_history.redoName();
	if (m_history.redo(m_doc)) {
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
	if (m_tool != tool) {
		m_tool = tool;
		emit toolChanged();
	}
}

void Editor::setSelectMode(SelectMode mode) {
	if (m_selectMode == mode) {
		return;
	}
	m_selectMode = mode;
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
	m_currentEntityClass = classname;
	emit settingsChanged();
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
	m_history.push(std::move(change));
	m_selection = std::move(selection);
	emit documentChanged();
	emit selectionChanged();
}

void Editor::moveSelectionToWorld() {
	commit(edit::moveToWorld(m_doc, m_selection));
}

} // namespace sme::app
