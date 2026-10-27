// editor.h - Central editor state shared by all windows: the opened game, the map document,
// undo history, selection, current tool and tool settings.
//
// Every change to the map goes through commit(), which records undo and notifies the views.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include <sme/editing.h>
#include <sme/game.h>
#include <sme/history.h>
#include <sme/map.h>

namespace sme::app {

class ModelCache;

/// Settings stored per game/mod (FGDs, compile tools, folders).
struct GameSettings {
	QStringList fgds;
	QString vbsp, vvis, vrad;
	QString gameExe;
	QString mapDir;
	QString launchOptions = "-novid -dev -console -windowed";
};

class Editor : public QObject {
	Q_OBJECT

public:
	enum class Tool { Select, Move, Rotate, Scale, Block, Entity };
	enum class SelectMode { Object, Face };
	enum class RenderMode { Lit, Unlit, Wireframe };

	explicit Editor(QObject* parent = nullptr);
	~Editor() override;

	// --- Games ---------------------------------------------------------------
	void discover(const QString& steamRoot = {});
	[[nodiscard]] const DiscoveryResult& discovery() const { return m_discovery; }
	bool openGame(const QString& key, QString* error = nullptr);
	[[nodiscard]] LoadedGame* game() const { return m_game.get(); }
	[[nodiscard]] const GameEntry* gameEntry() const;
	[[nodiscard]] QString gameKey() const { return m_gameKey; }
	[[nodiscard]] GameSettings gameSettings(const QString& key = {}) const;
	void setGameSettings(const QString& key, const GameSettings& settings);
	[[nodiscard]] const EntityCatalog* entities() const { return m_game ? &m_game->entities : nullptr; }
	/// Models of the current game, loaded in the background (shared by all views and previews).
	[[nodiscard]] ModelCache* models() const { return m_models.get(); }

	// --- Document ------------------------------------------------------------
	[[nodiscard]] MapDocument& doc() { return m_doc; }
	[[nodiscard]] const MapDocument& doc() const { return m_doc; }
	[[nodiscard]] History& history() { return m_history; }
	[[nodiscard]] QString filePath() const { return m_filePath; }
	[[nodiscard]] bool modified() const { return m_history.modified(); }
	void newMap();
	bool openMap(const QString& path, QString* error = nullptr);
	bool saveMap(const QString& path, QString* error = nullptr);
	/// Records an undoable change and notifies everyone.
	void commit(Change change);
	/// Like commit(), but folds into the previous change of the same name (paint strokes).
	void commitMerged(Change change);
	void undo();
	void redo();

	// --- Selection -----------------------------------------------------------
	[[nodiscard]] const Selection& selection() const { return m_selection; }
	void setSelection(Selection selection);
	void clearSelection();
	void selectAll();

	// --- Hiding (editor only, not saved) -------------------------------------------
	[[nodiscard]] const Selection& hidden() const { return m_hidden; }
	void hideSelection();
	void hideUnselected();
	void unhideAll();

	// --- Tools and settings --------------------------------------------------
	[[nodiscard]] Tool tool() const { return m_tool; }
	void setTool(Tool tool);
	[[nodiscard]] SelectMode selectMode() const { return m_selectMode; }
	void setSelectMode(SelectMode mode);
	[[nodiscard]] RenderMode renderMode() const { return m_renderMode; }
	void setRenderMode(RenderMode mode);
	[[nodiscard]] double grid() const { return m_grid; }
	void setGrid(double grid);
	/// Grid used for snapping: the grid size, or 0 while snapping is off (Shift+W).
	[[nodiscard]] double snapGrid() const { return m_snapping ? m_grid : 0.0; }
	[[nodiscard]] bool snapping() const { return m_snapping; }
	void setSnapping(bool on);
	/// Rotation step in degrees for the rotate gizmo and Alt+arrow keys (used while snapping is on).
	[[nodiscard]] double rotationSnap() const { return m_rotationSnap; }
	void setRotationSnap(double degrees);
	/// Angle rounded to the rotation step (unchanged while snapping is off).
	[[nodiscard]] double snapAngle(double degrees) const;
	[[nodiscard]] bool textureLock() const { return m_textureLock; }
	void setTextureLock(bool on);
	[[nodiscard]] QString currentMaterial() const { return m_currentMaterial; }
	void setCurrentMaterial(const QString& material);
	/// Most recently used materials, newest first (shown at the top of the material browser).
	[[nodiscard]] const QStringList& recentMaterials() const { return m_recentMaterials; }
	[[nodiscard]] QString currentEntityClass() const { return m_currentEntityClass; }
	void setCurrentEntityClass(const QString& classname);

	// --- Operations on the selection -----------------------------------------
	void deleteSelection();
	void duplicateSelection();
	void nudgeSelection(const Vec3& delta);
	void rotateSelection(int axis, double degrees);
	void applyMaterialToSelection(const QString& material);
	void tieSelectionToEntity(const QString& classname);
	void moveSelectionToWorld();

signals:
	void gameAboutToChange();   ///< Background readers of the old game's files must stop now
	void gameChanged();
	void documentChanged();
	void documentReplaced();     ///< New/opened map (views reset the camera)
	void selectionChanged();
	void toolChanged();
	void settingsChanged();
	void recentMaterialsChanged();
	void message(const QString& text);

private:
	DiscoveryResult m_discovery;
	std::unique_ptr<LoadedGame> m_game;
	std::unique_ptr<ModelCache> m_models;   ///< After m_game: stops reading before the game goes away
	QString m_gameKey;
	MapDocument m_doc;
	History m_history;
	QString m_filePath;
	Selection m_selection;
	Selection m_hidden;
	Tool m_tool = Tool::Select;
	SelectMode m_selectMode = SelectMode::Object;
	RenderMode m_renderMode = RenderMode::Lit;
	double m_grid = 16;
	bool m_snapping = true;
	double m_rotationSnap = 15;
	bool m_textureLock = true;
	QString m_currentMaterial = "DEV/DEV_MEASUREGENERIC01";
	QStringList m_recentMaterials;
	QString m_currentEntityClass = "info_player_start";
};

/// Path helpers between Qt and std::filesystem (UTF-8 safe on Windows).
[[nodiscard]] std::filesystem::path toPath(const QString& path);
[[nodiscard]] QString fromPath(const std::filesystem::path& path);

} // namespace sme::app
