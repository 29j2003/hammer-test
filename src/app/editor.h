// editor.h - Central editor state shared by all windows: the opened game, the map document,
// undo history, selection, current tool and tool settings.
//
// Every change to the map goes through commit(), which records undo and notifies the views.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include <sme/clipboard.h>
#include <sme/editing.h>
#include <sme/fidelity_look.h>
#include <sme/visibility.h>
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
	[[nodiscard]] bool modified() const { return m_history.modified() || m_footerModified || m_recovered; }
	void newMap();
	bool openMap(const QString& path, QString* error = nullptr);
	/// Saves; with the setting "editor/vmxBackup" (on by default) the previous file is kept as
	/// <name>.vmx first, like Hammer.
	bool saveMap(const QString& path, QString* error = nullptr);
	/// Opens an autosave in place of the map at `originalPath` (empty: a map never saved). The map
	/// counts as changed until it is saved, so the restored work is not lost by accident.
	bool openRecovered(const QString& autosavePath, const QString& originalPath, QString* error = nullptr);
	/// Records an undoable change and notifies everyone.
	void commit(Change change);
	/// Like commit(), but folds into the previous change of the same name (paint strokes).
	void commitMerged(Change change);
	void undo();
	void redo();
	/// Undoes or redoes until exactly `index` changes are applied (undo history panel).
	void undoTo(std::size_t index);

	// --- Selection -----------------------------------------------------------
	[[nodiscard]] const Selection& selection() const { return m_selection; }
	void setSelection(Selection selection);
	void clearSelection();
	void selectAll();

	// --- Hiding (editor only, not saved) -------------------------------------------
	/// Everything not drawn: hidden by hand (H) plus the kinds switched off in the view filters.
	[[nodiscard]] const Selection& hidden() const;
	/// View filters: kinds of objects that are not shown (mask of ObjectKind, editor only, kept).
	[[nodiscard]] std::uint32_t hiddenKinds() const { return m_hiddenKinds; }
	[[nodiscard]] bool kindVisible(ObjectKind kind) const { return (m_hiddenKinds & static_cast<std::uint32_t>(kind)) == 0; }
	void setKindVisible(ObjectKind kind, bool visible);
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
	/// View options (tool bar "View"): grid in the 3D view, names of entities in the 2D views,
	/// lines from the selected entities to the entities their outputs fire at (and back).
	[[nodiscard]] bool showGrid3D() const { return m_showGrid3D; }
	void setShowGrid3D(bool on);
	[[nodiscard]] bool showEntityNames() const { return m_showEntityNames; }
	void setShowEntityNames(bool on);
	[[nodiscard]] bool showConnections() const { return m_showConnections; }
	void setShowConnections(bool on);
	/// The mod has mapbase-fidelity (its cfg/fidelity.cfg or shaders): clouds, grading, relief.
	[[nodiscard]] bool hasFidelity() const { return m_hasFidelity; }
	/// Lighting preview shows mapbase-fidelity's look (clouds, colour grading, relief): on by
	/// default for mods that have it, switchable for any mod (to try it without deploying).
	[[nodiscard]] bool fidelityLook() const;
	void setFidelityLook(bool on);
	/// A look value as the game would use it: the map's, else cfg/fidelity.cfg's, else the add-on's default.
	[[nodiscard]] double lookValue(std::string_view cvar) const;
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
	/// Ctrl+C / Ctrl+X: the selected objects go to the clipboard as VMF text.
	void copySelection();
	void cutSelection();
	/// Pastes map objects from the clipboard (one undo step) and selects them; false if the
	/// clipboard holds no map objects.
	bool paste(const PasteOptions& options);
	/// Box of the objects in the clipboard (for placing them under the mouse).
	[[nodiscard]] std::optional<Bounds> clipboardBounds() const;
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
	/// Corners picked with the vertex tool (the views keep them in step; the edge actions use them).
	[[nodiscard]] const std::vector<Vec3>& vertexCorners() const { return m_vertexCorners; }
	void setVertexCorners(const std::vector<Vec3>& corners);
	/// Edges the edge actions work on: between two picked corners, or between two selected faces.
	[[nodiscard]] std::vector<edit::EdgeFaces> selectedEdges() const;
	/// Wedge, bevel or round the selected edges (one undo step); false if none could be done.
	bool shapeSelectedEdges(edit::EdgeShape shape, double size, int segments);
	void clipSelection(const Plane& plane, edit::ClipKeep keep);
	/// Vertex tool: moves corners of the selected brushes.
	void moveSelectedVertices(const std::vector<Vec3>& corners, const Vec3& delta);
	/// Polygon tool: a brush from a convex base (points) extruded by `height`.
	void createPolygonBrush(const std::vector<Vec3>& base, const Vec3& extrude);
	void dropSelectionToFloor();
	/// Hammer's Flip Objects: mirrors the selection across its center (axis 0 X, 1 Y, 2 Z).
	void flipSelection(int axis);
	/// Hammer's Snap Selected to Grid: moves the selection so its box starts on the grid.
	void snapSelectionToGrid();
	/// Exact move, rotation (degrees around X, Y, Z) and size factors around the selection's center.
	void transformSelection(const Vec3& move, const Vec3& rotation, const Vec3& scale);
	/// Source 2 Hammer's Select Similar: same class / model, brushes with the same main material.
	void selectSimilar();
	/// Hammer's Go to Brush Number: selects the brush or entity with this id; false if none.
	bool selectById(int id);
	/// Sets the map's sky; a known sky also brings its sun and ambient light (sky_presets.h).
	void setSky(const QString& name);
	/// Cubemap room boxes (Mapbase parallax correction): set/resize, fit to the room around
	/// the cubemap, remove.
	void setCubemapRoomBox(int cubemapId, const Bounds& box);
	void fitCubemapRoomBox(int cubemapId);
	void removeCubemapRoomBox(int cubemapId);
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
	void viewFilterChanged();    ///< A kind of object was shown or hidden
	void vertexCornersChanged();
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
	bool m_recovered = false;   ///< Opened from an autosave and not saved since
	Selection m_selection;
	Selection m_hidden;
	std::vector<Vec3> m_vertexCorners;
	std::uint32_t m_hiddenKinds = 0;
	mutable Selection m_hiddenAll;        ///< m_hidden plus the filtered kinds, made when needed
	mutable bool m_hiddenDirty = true;
	Tool m_tool = Tool::Select;
	SelectMode m_selectMode = SelectMode::Object;
	RenderMode m_renderMode = RenderMode::Lit;
	bool m_shadows = true;
	bool m_showGrid3D = true;
	bool m_showEntityNames = true;
	bool m_showConnections = true;
	bool m_hasFidelity = false;
	int m_fidelityLook = -1;                 ///< -1 automatic, 0 off, 1 on
	std::map<std::string, std::string> m_lookDefaults;   ///< From the mod's cfg/fidelity.cfg
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
