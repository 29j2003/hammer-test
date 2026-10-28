// viewport.h - One map view: the 3D view (Source 2 style, fly camera) or a 2D view
// (top/front/side). Handles camera input, picking and the interactive tools.

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>

#include <optional>

class QMenu;
class QLabel;
class QToolButton;

#include "camera.h"
#include "editor.h"
#include <sme/displacement.h>

#include "renderer.h"

namespace sme::app {

/// One thing drawn over the view (text or the selection frame).
struct OverlayItem {
	QRectF rect;
	QString text;
	QColor color;
	int flags = Qt::AlignLeft | Qt::AlignTop;
	bool marquee = false;
	bool operator==(const OverlayItem&) const = default;
};

/// Collects the overlay of a frame, so it is only painted again when something changed.
struct OverlayRecorder {
	std::vector<OverlayItem> items;
	/// Records text (argument orders like QPainter::drawText) and the selection frame.
	void drawText(const QRectF& rect, const QString& text, const QColor& color, int flags = Qt::AlignLeft | Qt::AlignTop) {
		items.push_back({rect, text, color, flags, false});
	}
	void drawText(const QRectF& rect, const QColor& color, const QString& text) { drawText(rect, text, color); }
	void drawText(const QRectF& rect, const QColor& color, int flags, const QString& text) { drawText(rect, text, color, flags); }
	void drawMarquee(const QRectF& rect) { items.push_back({rect, {}, {}, 0, true}); }
};

/// One map view, 3D or 2D, with its own camera and renderer (see the file comment).
class Viewport : public QOpenGLWidget {
	Q_OBJECT

public:
	Viewport(Editor* editor, MaterialCache* materials, Camera::Kind kind, QWidget* parent = nullptr);
	~Viewport() override;

	/// The view's camera. setCameraKind() switches between 3D and top / front / side (and frames the
	/// selection when it changes between 3D and 2D).
	[[nodiscard]] Camera& camera() { return m_camera; }
	void setCameraKind(Camera::Kind kind);
	/// Moves the camera so the selection (or the whole map) fills the view.
	void frameSelection();
	/// "Show" section: switches for each kind of object (view filters).
	static void fillShowMenu(QMenu* menu, Editor* editor);
	/// Preview box under the mouse: the entity tool's armed class, or a model being dragged in from
	/// the browser (none while nothing is armed or the mouse is away).
	[[nodiscard]] std::optional<Bounds> ghostBox() const { return m_ghost ? std::optional<Bounds>(m_ghost->box) : std::nullopt; }
	/// Model of that preview ("" for a plain box).
	[[nodiscard]] QString ghostModel() const { return m_ghost ? m_ghost->model : QString(); }
	/// Box around the drawn content of a func_instance (empty before it was drawn or without content).
	[[nodiscard]] Bounds instanceBounds(int entityId) const { return m_renderer.instanceBounds(entityId); }
	/// Where pasted objects with this box go: their center under the mouse (resting on the
	/// surface in 3D), or nothing when the mouse is not over this view.
	[[nodiscard]] std::optional<Vec3> pasteCenter(const Bounds& box) const;
	/// The view's renderer (numbers about what it draws, for tests and the status bar).
	[[nodiscard]] const SceneRenderer& renderer() const { return m_renderer; }
	/// Map > Build cubemaps: draws every env_cubemap again now.
	void buildCubemaps();
	/// Asks for a frame, at most as often as Editor::maxFps() allows (use instead of update()).
	void requestFrame();
	/// How many frames this view has drawn so far (for the frame limit).
	[[nodiscard]] quint64 framesDrawn() const { return m_framesDrawn; }
	/// The door whose "Test open" is playing (0: none).
	[[nodiscard]] int doorTestId() const { return m_doorTest.id; }
	/// Line from a compile leak (.lin pointfile); empty clears it.
	void setLeakLine(const std::vector<Vec3>& points);

signals:
	/// World position under the mouse as text, for the status bar.
	void cursorWorldPosition(const QString& text);
	/// Right click without moving: the object under the cursor is selected, show the menu.
	void contextMenuRequested(const QPoint& globalPos);
	void statsChanged(const QString& text);
	/// Everything about the last frames (triangles, draw calls, lights ...), for a tooltip.
	void statsDetail(const QString& text);

protected:
	bool event(QEvent* event) override;
	void initializeGL() override;
	void paintGL() override;
	void resizeGL(int w, int h) override;
	/// Holds back repaints the window asks for faster than the frame limit (see requestFrame()).
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dragLeaveEvent(QDragLeaveEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	enum class Handle { None, AxisX, AxisY, AxisZ, PlaneXY, PlaneXZ, PlaneYZ, RingX, RingY, RingZ, ScaleMinX, ScaleMaxX, ScaleMinY, ScaleMaxY, ScaleMinZ, ScaleMaxZ, Move2D, Corner, RoomMinX, RoomMaxX, RoomMinY, RoomMaxY, RoomMinZ, RoomMaxZ,
	                    RadiusMinX, RadiusMaxX, RadiusMinY, RadiusMaxY, RadiusMinZ, RadiusMaxZ };
	enum class Drag { None, Pan, Orbit, Gizmo, Marquee, BlockBase, BlockHeight, Paint, ClipLine, Vertex, Sculpt };

	[[nodiscard]] Ray rayAt(const QPointF& pos) const;
	[[nodiscard]] double gizmoSize() const;
	[[nodiscard]] Handle hitHandle(const QPointF& pos) const;
	[[nodiscard]] std::optional<Vec3> rayPlane(const Ray& ray, const Vec3& point, const Vec3& normal) const;
	[[nodiscard]] Vec3 viewPlanePoint(const QPointF& pos) const;   ///< 2D: world point under the cursor (depth 0)
	[[nodiscard]] int viewAxis() const;                            ///< 2D: axis looked along (0, 1, 2)

	void clickSelect(const QPointF& pos, Qt::KeyboardModifiers modifiers);
	void marqueeSelect(const QRectF& rect, Qt::KeyboardModifiers modifiers);
	void placeEntity(const QPointF& pos);
	/// Moves the entity tool's preview to the mouse (or hides it).
	void updateGhost(const QPointF* pos);
	/// A model dragged in from the browser: where it would land under `pos` (nullptr: hide it).
	void updateDropGhost(const QPointF* pos);
	/// Where an object with box `local` (relative to its origin) comes to rest under `pos`: on the
	/// surface under the cursor in 3D, on the grid in 2D views. The box is centred on the cursor,
	/// so models whose origin lies far from their geometry still land where the mouse is.
	[[nodiscard]] std::optional<Vec3> restingPlace(const QPointF& pos, const Bounds& local) const;
	/// Places `classname` on the surface under the cursor (with extra keys, e.g. a dropped model).
	/// `at` places it there instead (the origin a drop preview showed).
	void placeEntityAt(const QPointF& pos, const std::string& classname, const std::vector<std::pair<std::string, std::string>>& keys = {},
	                   std::optional<Vec3> at = std::nullopt);
	void beginGizmoDrag(Handle handle, const QPointF& pos);
	void updateGizmoDrag(const QPointF& pos);
	void finishGizmoDrag(bool clone);
	[[nodiscard]] bool scaling() const;   ///< Scale handle being dragged
	/// The selection shows resize squares (Scale tool, or Select tool with brushes selected).
	[[nodiscard]] bool resizeHandles() const;
	/// Clouds and colour grading of mapbase-fidelity from the editor's look values.
	void updateLook();
	/// A resize square on that side of the box can be seen (3D: the side faces the camera).
	[[nodiscard]] bool handleFacesCamera(const Vec3& point, int axis, int side) const;
	/// Live preview brushes (scaled brushes with their real texture coordinates); empty clears.
	void setPreviewSolids(const std::vector<Solid>& solids);
	void updateBlockDrag(const QPointF& pos);
	void createBlockFromDrag();
	void updateShapePreview();
	void cancelInteraction();
	void pickMaterial(const QPointF& pos);
	void paintAt(const QPointF& pos);
	void setHoverSide(int sideId);
	void showHoverInfo(const QPointF& pos);
	void toggleFlyMode();
	void requestContextMenu(const QPointF& pos);

	// Clip, vertex and polygon tools (viewport_tools.cpp).
	struct VertexHandle {
		Vec3 position;
		std::vector<Vec3> corners;   ///< One corner, or the two ends of an edge
		bool edge = false;
	};
	[[nodiscard]] Vec3 toolPoint(const QPointF& pos, bool keepPlane);   ///< Snapped point under the cursor
	[[nodiscard]] std::optional<Plane> clipPlane() const;
	void updateClipPreview();
	void applyClip();
	void resetToolState();
	[[nodiscard]] std::vector<VertexHandle> vertexHandles() const;
	[[nodiscard]] int hitVertexHandle(const QPointF& pos) const;
	void updateVertexDrag(const QPointF& pos);
	void finishPolygon();
	bool toolMousePress(QMouseEvent* event);   ///< true if the clip/vertex/polygon tool used the click
	void drawToolLines(std::vector<LineVertex>& lines) const;
	bool toolKey(QKeyEvent* event);
	// Displacement tool (viewport_displace.cpp): create on a click, sculpt and paint by dragging.
	struct SculptFace {
		FaceRef ref;
		Side side;                 ///< Copy for texture coordinates while drawing
		FaceGeometry face;
		Displacement displacement; ///< Changed during the stroke, written into the map when it ends
		bool changed = false;
	};
	bool displaceMousePress(QMouseEvent* event);   ///< true if the displacement tool used the click
	void displaceMouseMove(const QPointF& pos);
	/// Applies the brush once at `point` to every displacement of the stroke.
	void sculptDab(const Vec3& point);
	/// Ends the stroke: one undo step for everything it changed.
	void finishSculpt();
	/// The brush circle follows the displacement under the mouse.
	void updateBrushCursor(const QPointF& pos);
	void drawBrushCursor(std::vector<LineVertex>& lines) const;
	/// Ctrl+wheel and [ ] change the brush radius; true if used.
	bool displaceWheel(QWheelEvent* event);
	bool displaceKey(QKeyEvent* event);
	/// Mouse look (right mouse or fly mode). Runs alongside a tool drag, so the camera can
	/// move while a block is being placed.
	void beginLook(const QPointF& anchor);
	void endLook();
	void applyLook();

	void drawGrid(std::vector<LineVertex>& lines) const;
	void drawGizmo(std::vector<LineVertex>& lines) const;
	/// The eight corners of the selection box (move tool squares).
	[[nodiscard]] std::vector<Vec3> selectionCorners() const;
	/// The room box of the one selected env_cubemap (its id in `cubemapId`), or nullopt.
	[[nodiscard]] std::optional<Bounds> selectedRoomBox(int* cubemapId = nullptr) const;
	void drawRoomBox(std::vector<LineVertex>& lines) const;
	/// The one selected soundscape entity whose area is a sphere (radius > 0, no trigger block),
	/// or nullptr. Only while a selection tool is active.
	[[nodiscard]] const Entity* selectedSoundscapeSphere() const;
	/// Its sphere (three circles) with a square on each side to drag the radius.
	void drawSoundscapeSphere(std::vector<LineVertex>& lines) const;
	/// Lines between the selected entities and the entities their outputs name (and back).
	void drawConnections(std::vector<LineVertex>& lines) const;
	/// The drawn connection line near `pos` (5 pixels): the entity whose output it is and the
	/// output's index; nullopt if none.
	[[nodiscard]] std::optional<std::pair<int, int>> connectionAt(const QPointF& pos) const;
	/// Connect mode: a line from the source entity to the cursor.
	void drawConnectLine(std::vector<LineVertex>& lines) const;
	// Doors (viewport_doors.cpp): how the selected doors move, and the "Test open" animation.
	[[nodiscard]] std::optional<DoorMotion> doorMotionOf(const Entity& door) const;
	void drawDoorPreview(std::vector<LineVertex>& lines) const;
	/// Ropes (viewport_ropes.cpp): move_rope chains drawn sagging, rebuilt only after a change.
	void drawRopes(std::vector<LineVertex>& lines);
	void startDoorTest(int doorId);
	/// After a frame: keeps the animation going while it runs.
	void advanceDoorTest();
	void drawOverlay();
	void paintOverlayImage();
	void updateViewButton();
	void fillViewMenu(QMenu* menu);
	void moveCamera();

	Editor* m_editor;
	MaterialCache* m_materials;
	SceneRenderer m_renderer;
	Camera m_camera;
	bool m_glReady = false;
	QToolButton* m_viewButton = nullptr;   ///< "3D · Lighting preview ▾" in the corner
	QLabel* m_densityLegend = nullptr;     ///< Colour key of the lightmap density view (bottom left)
	bool m_blockForEntity = false;   ///< The block drag belongs to the entity tool (brush entity)
	bool m_needsRebuild = true;
	bool m_selectionDirty = true;
	QString m_glError;

	// Input state
	Drag m_drag = Drag::None;
	QPointF m_pressPos, m_lastPos;
	/// Entity tool: the armed point entity where a click would place it (box, model).
	struct Ghost {
		Bounds box;
		QString model;
		QMatrix4x4 transform;
		Vec3 origin;
		bool dropped = false;   ///< A model dragged in from the browser (not the entity tool)
	};
	std::optional<Ghost> m_ghost;
	std::vector<SculptFace> m_sculptFaces;          ///< The displacements of the running stroke
	std::optional<std::pair<Vec3, Vec3>> m_brushCursor;   ///< Center and surface normal under the mouse
	Vec3 m_lastDab;
	Vec3 m_flattenPoint;                           ///< Flatten: where the stroke began
	unsigned m_sculptSeed = 1;
	/// Model dragged over this view from the browser, and the turn it gets (random yaw: chosen
	/// once per drag, so the preview shows the turn the drop uses).
	QString m_dropModel;
	QPointF m_dropPos;
	int m_dropYaw = 0;
	bool m_lookDirty = true;
	Qt::KeyboardModifiers m_pressModifiers;
	QSet<int> m_keys;
	void applyFrameLimit();
	QTimer m_moveTimer;
	QTimer m_frameTimer;                  ///< A frame that waits for the frame limit
	QTimer m_cubemapTimer;                ///< The next frame that draws cubemaps
	QElapsedTimer m_moveClock;
	float m_flySpeed = 600.f;
	Vec3 m_orbitPivot;
	Handle m_hover = Handle::None;
	bool m_flyMode = false;     ///< Z: look without holding the mouse button
	bool m_lookFlew = false;     ///< W A S D or the wheel was used while looking (no context menu then)
	QElapsedTimer m_lookClock;
	bool m_looking = false;     ///< Mouse look active (camera turns, W A S D fly)
	QPoint m_lookAnchor;        ///< Global cursor position the cursor is warped back to
	QPoint m_lookLast;          ///< Global cursor position of the last look update
	int m_lookTravel = 0;       ///< Pixels moved while looking (0 = a plain right click)
	int m_hoverSide = 0;        ///< Face highlighted under a dragged material or the paint brush
	int m_paintStroke = 0;      ///< Paint strokes are one undo step each
	QElapsedTimer m_hoverClock;

	// Gizmo drag
	Handle m_dragHandle = Handle::None;
	Bounds m_dragBounds;          ///< Selection bounds at drag start
	Vec3 m_dragCenter;
	Vec3 m_dragStartPoint;
	// Corner snapping (move tool): drag a corner square onto a corner of another object.
	Vec3 m_cornerStart;
	std::vector<Vec3> m_snapTargets;      ///< Corners of everything not selected (taken at drag start)
	std::optional<Vec3> m_snapHit;        ///< Target the dragged corner sits on now
	double m_dragStartParam = 0;
	Vec3 m_dragDelta;             ///< Move
	double m_dragAngle = 0;       ///< Rotate (degrees)
	Bounds m_dragNewBounds;       ///< Scale
	double m_dragRadius = 0;      ///< Soundscape radius while its square is dragged
	double m_dragStartRadius = 0;
	Bounds m_previewBounds;       ///< Scale bounds the preview brushes were made for
	QMatrix4x4 m_previewModel;

	// Block tool
	double m_blockPlane = 0;      ///< 3D: z of the base; 2D: unused
	Vec3 m_blockStart, m_blockEnd;
	double m_blockHeight = 64;
	Bounds m_blockBox;
	Bounds m_lastBlockDepth;      ///< 2D: depth range of the last block

	// Marquee
	QRectF m_marquee;

	// Clip tool: two points in a 2D view or three points in the 3D view define the cut.
	std::vector<Vec3> m_clipPoints;
	edit::ClipKeep m_clipKeep = edit::ClipKeep::Both;
	// Vertex tool
	std::vector<Vec3> m_vertexSelection;   ///< Selected corners
	Vec3 m_vertexGrab, m_vertexNormal, m_vertexDelta;
	// Polygon tool
	std::vector<Vec3> m_polygon;
	double m_polygonPlane = 0;

	std::vector<Vec3> m_leak;
	std::vector<LineVertex> m_ropeLines;   ///< Cached rope curves (drawRopes)
	bool m_ropesDirty = true;

	/// "Test open" of a door: which door, since when, how long its opening takes.
	struct DoorTest {
		int id = 0;
		QElapsedTimer clock;
		double seconds = 1;
	} m_doorTest;

	// Stats
	QElapsedTimer m_frameClock;
	std::vector<OverlayItem> m_overlayItems;
	QImage m_overlayImage;
	double m_frameMs = 0;
	int m_frames = 0;
	QElapsedTimer m_fpsClock;
	QElapsedTimer m_lastFrame;            ///< When the last frame was drawn (frame limit)
	quint64 m_framesDrawn = 0;
	QSize m_paintedSize;                  ///< Size of the last frame: a new size always draws
	double m_fps = 0;
};

} // namespace sme::app
