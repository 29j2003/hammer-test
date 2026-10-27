// editor.cpp - Editor state and operations (see editor.h).

#include "editor.h"

#include "model_cache.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

#include <sme/brush.h>
#include <sme/csg.h>
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
	QSettings settings;
	m_grid = settings.value("editor/grid", 16.0).toDouble();
	m_textureLock = settings.value("editor/textureLock", true).toBool();
	m_currentMaterial = settings.value("editor/material", m_currentMaterial).toString();
	m_snapping = settings.value("editor/snapping", true).toBool();
	m_shadows = settings.value("editor/shadows", true).toBool();
	m_recentMaterials = settings.value("editor/recentMaterials").toStringList();
	m_shapeOptions.kind = static_cast<ShapeOptions::Kind>(std::clamp(settings.value("shape/kind", 0).toInt(), 0, int(ShapeOptions::Kind::Stairs)));
	m_shapeOptions.sides = settings.value("shape/sides", m_shapeOptions.sides).toInt();
	m_shapeOptions.arc = settings.value("shape/arc", m_shapeOptions.arc).toDouble();
	m_shapeOptions.startAngle = settings.value("shape/start", m_shapeOptions.startAngle).toDouble();
	m_shapeOptions.wallWidth = settings.value("shape/width", m_shapeOptions.wallWidth).toDouble();
	m_shapeOptions.addHeight = settings.value("shape/addHeight", m_shapeOptions.addHeight).toDouble();
	m_shapeOptions.steps = settings.value("shape/steps", m_shapeOptions.steps).toInt();
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
	m_models->setFileSystem(nullptr, nullptr);
	m_game = std::move(game);
	m_models->setFileSystem(&m_game->files, &m_game->entities);
	QSettings().setValue("editor/game", key);
	emit gameChanged();
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
	m_lastShape = {};
	m_footerModified = false;
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
	m_footerModified = false;
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
	// Face mode is its own tool (like the texture tool in Hammer): picking any other tool
	// works on whole objects again.
	if (m_selectMode == SelectMode::Face) {
		m_tool = tool;
		setSelectMode(SelectMode::Object);
		return;
	}
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

void Editor::setShadows(bool on) {
	if (m_shadows != on) {
		m_shadows = on;
		QSettings().setValue("editor/shadows", on);
		emit settingsChanged();
	}
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
	if (shapeEditable()) {
		// Rebuild the shape just made; merged into its "Create" step.
		auto solids = makeShape(m_doc, m_lastShape.box, m_shapeOptions, m_lastShape.material);
		if (!solids.empty()) {
			auto [change, refs] = edit::replaceSolids(m_doc, m_lastShape.refs, std::move(solids), m_lastShape.name);
			commitMerged(std::move(change));
			m_lastShape.refs = refs;
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
	auto [change, refs] = edit::createShape(m_doc, box, m_shapeOptions, material);
	if (refs.empty()) {
		emit message(tr("The %1 does not fit into this box - make it bigger or change the settings in the Tool panel")
		                 .arg(QString::fromStdString(shapeName(m_shapeOptions.kind))));
		return;
	}
	m_lastShape = {box, refs, change.name, material, 0, true};
	commit(std::move(change));
	m_lastShape.revision = m_history.revision();
	Selection selection;
	selection.solids = refs;
	setSelection(std::move(selection));
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

void Editor::roundSelectedEdge(double radius, int segments) {
	if (m_selection.faces.size() != 2) {
		emit message(tr("Select the two faces next to the edge (face mode, Ctrl+click)"));
		return;
	}
	auto change = edit::roundEdge(m_doc, m_selection.faces[0], m_selection.faces[1], radius, segments);
	if (!change) {
		emit message(tr("These faces do not share an edge of one brush, or the radius is too big"));
		return;
	}
	commit(std::move(*change));
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

void Editor::dropSelectionToFloor() {
	auto change = edit::dropToFloor(m_doc, m_selection, entities());
	if (change.empty()) {
		emit message(tr("Nothing to drop: the selection already rests on something (or there is nothing below)"));
		return;
	}
	commit(std::move(change));
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
