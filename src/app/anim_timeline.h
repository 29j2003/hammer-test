// anim_timeline.h - The animation editor's timeline, like Source Filmmaker's: a ruler with frame
// numbers, the playhead, and two rows of keys - the chosen bones' and all bones' (diamonds:
// smooth, squares: linear, square with a bar: step, triangles: ease in / ease out). Frames before
// the edit-from frame are shaded (they stay as the original). Click or drag in the ruler or an empty spot to scrub; drag a key to
// move it (Ctrl: copy); right-click a key for its menu. It only draws and reports; the window
// changes the clip.

#pragma once

#include <QWidget>

#include <map>
#include <vector>

#include <sme/anim_edit.h>

namespace sme::app {

/// See the file comment.
class AnimTimeline : public QWidget {
	Q_OBJECT

public:
	explicit AnimTimeline(QWidget* parent = nullptr);

	void setFrameCount(int frames);
	[[nodiscard]] int frameCount() const { return m_frames; }
	void setFrame(int frame);
	[[nodiscard]] int frame() const { return m_frame; }
	/// Keys of the chosen bones (frame -> how the curve leaves it) and of all bones.
	void setKeys(std::map<int, KeyInterp> selected, std::vector<int> all);
	/// Frames before this one are locked (shaded).
	void setEditFrom(int frame);

	/// The x of a frame, and the frame at an x (tests).
	[[nodiscard]] double frameX(int frame) const;
	[[nodiscard]] int frameAt(double x) const;
	/// Middle of the row of the chosen bones' keys / of all keys (tests).
	[[nodiscard]] int selectedRowY() const;
	[[nodiscard]] int allRowY() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void frameChanged(int frame);
	/// A key was dragged from one frame to another; `allBones`: from the all-bones row.
	void keyMoved(int from, int to, bool allBones, bool copy);
	/// Right-click on a key.
	void keyMenuRequested(int frame, bool allBones, const QPoint& globalPos);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	/// The key under a point: frame (-1: none) and whether it is in the all-bones row.
	[[nodiscard]] std::pair<int, bool> keyAt(const QPoint& at) const;

	int m_frames = 1;
	int m_frame = 0;
	int m_editFrom = 0;
	std::map<int, KeyInterp> m_selected;
	std::vector<int> m_all;
	// Dragging.
	enum class Drag { None, Scrub, Key } m_drag = Drag::None;
	int m_dragFrom = -1;
	int m_dragTo = -1;
	bool m_dragAll = false;
};

} // namespace sme::app
