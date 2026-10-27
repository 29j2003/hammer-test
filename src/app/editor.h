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
#include <sme/map_tools.h>

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
	enum class Tool { Select, Move, Rotate, Scale, Block, Entity, Clip, Vertex, Polygon };
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
	[[nodiscard]] bool modified() const { return m_history.modified() || m_footerModified; }
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
	/// Real-time shadows in the lighting preview.
	[[nodiscard]] bool shadows() const { return m_shadows; }
	void setShadows(bool on);
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
	/// Entity class the entity tool places ("armed"); empty = nothing armed.
	[[nodiscard]] QString currentEntityClass() const { return m_currentEntityClass; }
	void setCurrentEntityClass(const QString& classname);
	/// The armed class is a brush entity: the entity tool drags a box like the block tool.
	[[nodiscard]] bool armedBrushEntity() const;
	/// Point entities placed in the 3D view get a random yaw (props, foliage, debris).
	[[nodiscard]] bool randomYaw() const { return m_randomYaw; }
	void setRandomYaw(bool on);

	// --- Shapes (block tool) --------------------------------------------------
	[[nodiscard]] const ShapeOptions& shapeOptions() const { return m_shapeOptions; }
	/// Changes the shape settings; the shape created last is rebuilt with them as long as
	/// nothing else changed since (live editing, still one undo step).
	void setShapeOptions(const ShapeOptions& options);
	/// Creates the current shape in `box` with the current material and selects it.
	void createShape(const Bounds& box);
	/// Creates the armed brush entity class from the current shape in `box` and selects it.
	void createBrushEntity(const Bounds& box);
	/// The shape created last can still be changed through setShapeOptions().
	[[nodiscard]] bool shapeEditable() const;

	// --- Operations on the selection -----------------------------------------
	void deleteSelection();
	void duplicateSelection();
	void nudgeSelection(const Vec3& delta);
	void rotateSelection(int axis, double degrees);
	void applyMaterialToSelection(const QString& material);
	void tieSelectionToEntity(const QString& classname);
	void moveSelectionToWorld();
	// Brush tools (Hammer: carve, hollow, merge; rounding edges between two selected faces).
	void hollowSelection(double thickness);
	void carveWithSelection();
	void mergeSelection();
	void roundSelectedEdge(double radius, int segments);
	void clipSelection(const Plane& plane, edit::ClipKeep keep);
	/// Vertex tool: moves corners of the selected brushes.
	void moveSelectedVertices(const std::vector<Vec3>& corners, const Vec3& delta);
	/// Polygon tool: a brush from a convex base (points) extruded by `height`.
	void createPolygonBrush(const std::vector<Vec3>& base, const Vec3& extrude);
	void dropSelectionToFloor();
	/// Selects the whole path (path_track, path_corner ...) of the selected entity.
	void selectTrackChain();

	// --- Cordons (stored in the map, compile only what is inside) -------------
	[[nodiscard]] CordonSet cordons() const;
	void setCordons(const CordonSet& cordons);

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
	bool m_shadows = true;
	double m_grid = 16;
	bool m_snapping = true;
	double m_rotationSnap = 15;
	bool m_textureLock = true;
	QString m_currentMaterial = "DEV/DEV_MEASUREGENERIC01";
	QStringList m_recentMaterials;
	QString m_currentEntityClass = "info_player_start";
	bool m_randomYaw = false;
	ShapeOptions m_shapeOptions;
	bool m_footerModified = false;   ///< Cordons changed (not part of undo, like in Hammer)
	struct LastShape {
		Bounds box;
		std::vector<SolidRef> refs;
		std::string name;
		std::string material;
		unsigned revision = 0;
		bool valid = false;
	} m_lastShape;
};

/// Path helpers between Qt and std::filesystem (UTF-8 safe on Windows).
[[nodiscard]] std::filesystem::path toPath(const QString& path);
[[nodiscard]] QString fromPath(const std::filesystem::path& path);

} // namespace sme::app
