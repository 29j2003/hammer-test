// viewport.h - One map view: the 3D view (Source 2 style, fly camera) or a 2D view
// (top/front/side). Handles camera input, picking and the interactive tools.

#pragma once

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QSet>
#include <QTimer>

#include <optional>

#include "camera.h"
#include "editor.h"
#include "renderer.h"

namespace sme::app {

class Viewport : public QOpenGLWidget {
	Q_OBJECT

public:
	Viewport(Editor* editor, MaterialCache* materials, Camera::Kind kind, QWidget* parent = nullptr);
	~Viewport() override;

	[[nodiscard]] Camera& camera() { return m_camera; }
	void setCameraKind(Camera::Kind kind);
	/// Moves the camera so the selection (or the whole map) fills the view.
	void frameSelection();
	/// Line from a compile leak (.lin pointfile); empty clears it.
	void setLeakLine(const std::vector<Vec3>& points);

signals:
	void cursorWorldPosition(const QString& text);
	void statsChanged(const QString& text);

protected:
	bool event(QEvent* event) override;
	void initializeGL() override;
	void paintGL() override;
	void resizeGL(int w, int h) override;
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

private:
	enum class Handle { None, AxisX, AxisY, AxisZ, PlaneXY, PlaneXZ, PlaneYZ, RingX, RingY, RingZ, ScaleMinX, ScaleMaxX, ScaleMinY, ScaleMaxY, ScaleMinZ, ScaleMaxZ, Move2D };
	enum class Drag { None, Pan, Orbit, Gizmo, Marquee, BlockBase, BlockHeight, Paint };

	[[nodiscard]] Ray rayAt(const QPointF& pos) const;
	[[nodiscard]] double gizmoSize() const;
	[[nodiscard]] Handle hitHandle(const QPointF& pos) const;
	[[nodiscard]] std::optional<Vec3> rayPlane(const Ray& ray, const Vec3& point, const Vec3& normal) const;
	[[nodiscard]] Vec3 viewPlanePoint(const QPointF& pos) const;   ///< 2D: world point under the cursor (depth 0)
	[[nodiscard]] int viewAxis() const;                            ///< 2D: axis looked along (0, 1, 2)

	void clickSelect(const QPointF& pos, Qt::KeyboardModifiers modifiers);
	void marqueeSelect(const QRectF& rect, Qt::KeyboardModifiers modifiers);
	void placeEntity(const QPointF& pos);
	/// Places `classname` on the surface under the cursor (with extra keys, e.g. a dropped model).
	void placeEntityAt(const QPointF& pos, const std::string& classname, const std::vector<std::pair<std::string, std::string>>& keys = {});
	void beginGizmoDrag(Handle handle, const QPointF& pos);
	void updateGizmoDrag(const QPointF& pos);
	void finishGizmoDrag(bool clone);
	[[nodiscard]] bool scaling() const;   ///< Scale handle being dragged
	/// Live preview brushes (scaled brushes with their real texture coordinates); empty clears.
	void setPreviewSolids(const std::vector<Solid>& solids);
	void updateBlockDrag(const QPointF& pos);
	void createBlockFromDrag();
	void cancelInteraction();
	void pickMaterial(const QPointF& pos);
	void paintAt(const QPointF& pos);
	void setHoverSide(int sideId);
	void showHoverMaterial(const QPointF& pos);
	void toggleFlyMode();
	/// Mouse look (right mouse or fly mode). Runs alongside a tool drag, so the camera can
	/// move while a block is being placed.
	void beginLook(const QPointF& anchor);
	void endLook();
	void applyLook();

	void drawGrid(std::vector<LineVertex>& lines) const;
	void drawGizmo(std::vector<LineVertex>& lines) const;
	void drawOverlay();
	void moveCamera();

	Editor* m_editor;
	MaterialCache* m_materials;
	SceneRenderer m_renderer;
	Camera m_camera;
	bool m_glReady = false;
	bool m_needsRebuild = true;
	bool m_selectionDirty = true;
	QString m_glError;

	// Input state
	Drag m_drag = Drag::None;
	QPointF m_pressPos, m_lastPos;
	Qt::KeyboardModifiers m_pressModifiers;
	QSet<int> m_keys;
	QTimer m_moveTimer;
	QElapsedTimer m_moveClock;
	float m_flySpeed = 600.f;
	Vec3 m_orbitPivot;
	Handle m_hover = Handle::None;
	bool m_flyMode = false;     ///< Z: look without holding the mouse button
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
	double m_dragStartParam = 0;
	Vec3 m_dragDelta;             ///< Move
	double m_dragAngle = 0;       ///< Rotate (degrees)
	Bounds m_dragNewBounds;       ///< Scale
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

	std::vector<Vec3> m_leak;

	// Stats
	QElapsedTimer m_frameClock;
	double m_frameMs = 0;
	int m_frames = 0;
	QElapsedTimer m_fpsClock;
	double m_fps = 0;
};

} // namespace sme::app
