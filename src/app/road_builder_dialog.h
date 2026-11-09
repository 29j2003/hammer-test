// road_builder_dialog.h - "Build road": builds a road's brushes (see sme/road_builder.h) along
// points clicked in the views or through the street the selected floor brushes make (centred,
// parking lanes beside it, the floor cut around it), or only lays the road texture over the
// selected blocks (road_texture.h), with a live preview. Like the
// other brush dialogs it stays open beside the map (not modal): every change shows the result at
// once (Editor::setBrushPreview); Build applies it as one undo step, Cancel or closing leaves the
// map as it was. While drawing, each click in a 3D or top view adds a point (Enter builds,
// Backspace takes the last point back, Esc stops clicking).

#pragma once

#include <QDialog>
#include <QTimer>

#include <sme/road_builder.h>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace sme::app {

class Editor;

class RoadBuilderDialog : public QDialog {
	Q_OBJECT

public:
	/// `material`: the road material to start with (the selection's when building from it).
	explicit RoadBuilderDialog(Editor* editor, const QString& material = {}, QWidget* parent = nullptr);
	~RoadBuilderDialog() override;

	/// The options as the fields show them.
	[[nodiscard]] RoadBuildOptions options() const;
	/// Building from the selected floor brushes (else along the clicked points).
	[[nodiscard]] bool fromSelection() const;
	/// "Only lay the road texture": the selected blocks stay, their tops get the road (road_texture.h).
	[[nodiscard]] bool textureOnly() const;
	[[nodiscard]] RoadOptions textureOptions() const;
	/// The clicked points.
	[[nodiscard]] const std::vector<Vec3>& points() const { return m_points; }
	/// Adds a point as a click would (tests, the editor report's replay).
	void addPoint(const Vec3& point);
	/// The line under the fields (what the road will be, or why nothing).
	[[nodiscard]] QString note() const;
	/// A plain asphalt of the game (no lane lines): through a street whose floor has the road
	/// material, what is left beside the road gets it unless a material is set there.
	void setPlainMaterial(const QString& material);

public slots:
	void accept() override;
	void done(int result) override;

private:
	/// Plans again and shows the preview (soon after a change, not on every key press).
	void refresh();
	void refreshTexture();
	/// Asks the views for the next point (while drawing).
	void requestPoint();
	void stopDrawing();
	/// Draw / along the selection switched: the point buttons follow, the selection's blocks give
	/// the width, thickness and material.
	void modeChanged();
	void undoPoint();

	Editor* m_editor;
	QRadioButton* m_drawMode;
	QRadioButton* m_selectionMode;
	QRadioButton* m_textureMode;
	QPushButton* m_clickPoints;
	QPushButton* m_undoPoint;
	QPushButton* m_clearPoints;
	QComboBox* m_preset;
	QDoubleSpinBox* m_width;
	QDoubleSpinBox* m_thickness;
	QDoubleSpinBox* m_radius;
	QSpinBox* m_segments;
	QLineEdit* m_material;
	QComboBox* m_along;
	QCheckBox* m_flip;
	QLineEdit* m_side;
	QLineEdit* m_junction;
	QCheckBox* m_displacement;
	QComboBox* m_power;
	QDoubleSpinBox* m_crown;
	QDoubleSpinBox* m_roughness;
	QCheckBox* m_kerbs;
	QDoubleSpinBox* m_kerbWidth;
	QDoubleSpinBox* m_kerbHeight;
	QLineEdit* m_kerbMaterial;
	QCheckBox* m_fit;
	QDoubleSpinBox* m_clearance;
	QDoubleSpinBox* m_parking;
	QCheckBox* m_wholeWidth;
	QLineEdit* m_fill;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QTimer m_soon;
	std::vector<Vec3> m_points;
	bool m_drawing = false;
	bool m_finished = false;
	QString m_plainMaterial;
	bool m_fillEdited = false;   ///< The user typed a "Beside the road" material
};

} // namespace sme::app
