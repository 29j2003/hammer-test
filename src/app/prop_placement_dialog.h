// prop_placement_dialog.h - "Place props ...": many props at once (see sme/prop_placement.h),
// along a path (clicked points or the selected kerb edges: fences, kerb stones, lamp posts at a
// fixed spacing, turned with the curve) or scattered over the selected faces (trees, rocks; kept
// off a road). Not modal: the views show the props as ghosts while the settings change
// (Editor::setPropPreview); Place adds them as one undo step, Cancel leaves the map as it was.

#pragma once

#include <QDialog>
#include <QTimer>

#include <functional>
#include <vector>

#include <sme/editing.h>
#include <sme/prop_placement.h>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace sme::app {

class Editor;

class PropPlacementDialog : public QDialog {
	Q_OBJECT

public:
	/// `browserModel` gives the model picked in the asset browser ("" if none).
	PropPlacementDialog(Editor* editor, std::function<QString()> browserModel, QWidget* parent = nullptr);
	~PropPlacementDialog() override;

	/// The placements the settings give now (what the preview shows).
	[[nodiscard]] std::vector<PropPlacement> placements(bool* limited = nullptr) const;
	[[nodiscard]] QString model() const;
	[[nodiscard]] bool scatterMode() const;
	/// Path points (clicked or from the selected edges); adds one as a click would.
	[[nodiscard]] const std::vector<Vec3>& points() const { return m_points; }
	void addPoint(const Vec3& point);
	/// Takes the selected faces (or the brushes' tops) as the ground to scatter on.
	void useSelectedSurface();
	/// Takes the selected brushes as what the props keep away from.
	void useSelectedAvoid();
	/// Takes the selected edges as the path.
	bool useSelectedEdges();
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void done(int result) override;

private:
	void refresh();
	void requestPoint();
	void stopDrawing();
	void modeChanged();

	Editor* m_editor;
	std::function<QString()> m_browserModel;
	QLineEdit* m_model;
	QComboBox* m_class;
	QRadioButton* m_pathMode;
	QRadioButton* m_scatterMode;
	QWidget* m_pathBox;
	QWidget* m_scatterBox;
	// Along a path
	QPushButton* m_clickPoints;
	QDoubleSpinBox* m_spacing;
	QDoubleSpinBox* m_sideOffset;
	QDoubleSpinBox* m_yawOffset;
	QCheckBox* m_align;
	QCheckBox* m_includeEnd;
	QDoubleSpinBox* m_jitter;
	// Scatter
	QLabel* m_surfaceLabel;
	QLabel* m_avoidLabel;
	QDoubleSpinBox* m_density;
	QDoubleSpinBox* m_maxSlope;
	QDoubleSpinBox* m_minSpacing;
	QDoubleSpinBox* m_keepAway;
	QCheckBox* m_randomYaw;
	QCheckBox* m_tilt;
	// Both
	QDoubleSpinBox* m_scaleMin;
	QDoubleSpinBox* m_scaleMax;
	QSpinBox* m_seed;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;

	std::vector<Vec3> m_points;
	std::vector<FaceRef> m_surface;
	std::vector<SolidRef> m_avoid;
	QTimer m_soon;
	bool m_drawing = false;
	bool m_finished = false;
};

} // namespace sme::app
