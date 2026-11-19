// anim_edit_view.h - The 3D view of the animation editor: the posed model (ModelViewer) with its
// skeleton on top, bones to click (Ctrl adds), and a gizmo on the chosen bone - rings to turn it
// in its own axes (and an outer ring about the view direction), or arrows to move it along the
// model's axes. Grips (orange squares) on hands and feet: drag one and the limb follows (IK, in
// the window). The motion path of the chosen bone shows where it goes over the whole animation.
// Two cameras: free (orbit around the model, presets Front/Side/Top) and Viewmodel, the player's
// view in the game (eye at the model's origin looking along +X, viewmodel_fov 54 at 4:3 as in
// view.cpp) for weapons. The view only says what the mouse did; the window turns that into keys.

#pragma once

#include "model_viewer.h"

#include <QPointF>

#include <vector>

#include <sme/studio_anim.h>

namespace sme::app {

/// See the file comment.
class AnimEditView : public ModelViewer {
	Q_OBJECT

public:
	enum class Gizmo { Rotate, Move };
	enum class CameraMode { Free, Viewmodel };

	explicit AnimEditView(MaterialCache* materials, QWidget* parent = nullptr);

	/// The skeleton to draw: bones (for the parents) and their placement in model space now,
	/// which bones are chosen, and which have a key on this frame.
	void setSkeleton(const std::vector<StudioBone>* bones, std::vector<BoneMatrix> placement);
	void setSelection(std::vector<int> bones, int active);
	void setKeyed(std::vector<bool> keyed);
	/// Faint skeletons of other frames (onion skin); empty: none.
	void setGhosts(std::vector<std::vector<BoneMatrix>> ghosts);
	void setGizmo(Gizmo gizmo);
	[[nodiscard]] Gizmo gizmo() const { return m_gizmo; }
	void setCameraMode(CameraMode mode);
	[[nodiscard]] CameraMode cameraMode() const { return m_cameraMode; }
	void setSkeletonVisible(bool on);
	/// Grips to drag (IK chain ends: a bone and its label, "Right hand").
	struct Grip {
		int bone = -1;
		QString label;
	};
	void setGrips(std::vector<Grip> grips);
	/// The chosen bone's path over the animation (model space), which points are keys, and the
	/// point of the current frame (-1: none). Empty: no path.
	void setMotionPath(std::vector<AnimVec> points, std::vector<bool> keyed, int current);
	[[nodiscard]] std::size_t motionPathPoints() const { return m_path.size(); }
	[[nodiscard]] std::size_t gripCount() const { return m_grips.size(); }
	/// Camera presets for the free view.
	enum class Preset { Perspective, Front, Side, Top };
	void setPreset(Preset preset);
	/// Puts the free camera around a bone (F), or the whole model for -1.
	void focusBone(int bone);

	/// Where a bone is on screen (widget pixels); false if behind the camera (tests).
	bool boneOnScreen(int bone, QPointF& at) const;
	/// Where a grip is on screen (tests).
	bool gripOnScreen(int bone, QPointF& at) const { return boneOnScreen(bone, at); }
	/// A point on the gizmo's ring/arrow for an axis (0 x, 1 y, 2 z, 3 view ring) to drag (tests).
	bool gizmoHandle(int axis, QPointF& at) const;

signals:
	/// A bone was clicked (-1: empty space); `add`: with Ctrl.
	void boneClicked(int bone, bool add);
	/// A gizmo drag starts / moves (the whole turn or move since the start, in model space) / ends.
	void dragStarted();
	void rotateDragged(const sme::AnimQuat& rotation);
	void moveDragged(const sme::AnimVec& offset);
	/// A grip is dragged: where its bone should be now (model space).
	void gripDragged(int bone, const sme::AnimVec& target);
	void dragFinished();

protected:
	Camera camera() const override;
	void paintOverlay(QPainter& painter, const QMatrix4x4& viewProj) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

private:
	[[nodiscard]] bool project(const QMatrix4x4& viewProj, const QVector3D& p, QPointF& out) const;
	[[nodiscard]] QVector3D bonePosition(int bone) const;
	[[nodiscard]] QVector3D boneAxis(int bone, int axis) const;
	/// The gizmo's size in world units at the active bone (a constant size on screen).
	[[nodiscard]] float gizmoRadius() const;
	/// Points of a ring (axis 0-2: the bone's axes; 3: the view direction) or of an arrow, on screen.
	[[nodiscard]] std::vector<QPointF> handlePoints(int axis, std::vector<bool>* front = nullptr) const;
	/// Which handle is under the mouse (-1: none).
	[[nodiscard]] int pickHandle(const QPointF& at) const;
	[[nodiscard]] int pickBone(const QPointF& at) const;
	/// The grip under the mouse: its bone, -1 if none.
	[[nodiscard]] int pickGrip(const QPointF& at) const;
	[[nodiscard]] QVector3D handleAxis(int axis) const;

	const std::vector<StudioBone>* m_bones = nullptr;
	std::vector<BoneMatrix> m_placement;
	std::vector<std::vector<BoneMatrix>> m_ghosts;
	std::vector<int> m_selection;
	int m_active = -1;
	std::vector<bool> m_keyed;
	Gizmo m_gizmo = Gizmo::Rotate;
	CameraMode m_cameraMode = CameraMode::Free;
	bool m_showSkeleton = true;
	std::vector<Grip> m_grips;
	std::vector<AnimVec> m_path;
	std::vector<bool> m_pathKeyed;
	int m_pathCurrent = -1;
	// A grip drag: the grip's bone, where it started (model space) and the plane's screen axes.
	int m_gripDrag = -1;
	QVector3D m_gripStart, m_gripRight, m_gripUp;
	float m_gripWorldPerPixel = 1;
	// A gizmo drag.
	int m_dragHandle = -1;
	int m_hoverHandle = -1;
	QPointF m_pressAt;
	QVector3D m_dragAxis;
	QPointF m_dragCenter;
	QPointF m_dragScreenAxis;   ///< Move: the axis on screen (pixels per gizmo radius)
	float m_dragRadius = 1;
	bool m_orbiting = false;
	bool m_moved = false;
};

} // namespace sme::app
