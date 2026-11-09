// surface_dialogs.h - "Blend along selection" and "Overlay strip": small dialogs that stay open
// beside the map (not modal). Every change of a value applies at once, replacing the previous
// result of the same dialog in the undo history (one undo step in the end); OK keeps it, Cancel
// takes it back.

#pragma once

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace sme::app {

class Editor;

/// Paints the blend of displacements by distance to the selected road/path brushes (sme::blendNear).
class BlendAlongDialog : public QDialog {
	Q_OBJECT

public:
	explicit BlendAlongDialog(Editor* editor, QWidget* parent = nullptr);
	/// The line under the fields (what happened, or why nothing).
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void reject() override;

private:
	void apply();

	Editor* m_editor;
	QDoubleSpinBox* m_width;
	QDoubleSpinBox* m_softness;
	QDoubleSpinBox* m_noise;
	QComboBox* m_near;
	QLabel* m_note;
	QTimer m_soon;
	bool m_applied = false;
};

/// A band of overlays (current material) along the selected faces (sme::planOverlayStrip).
class OverlayStripDialog : public QDialog {
	Q_OBJECT

public:
	explicit OverlayStripDialog(Editor* editor, QWidget* parent = nullptr);
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void reject() override;

private:
	void apply();

	Editor* m_editor;
	QComboBox* m_align;
	QDoubleSpinBox* m_width;
	QDoubleSpinBox* m_offset;
	QCheckBox* m_reverse;
	QLabel* m_note;
	QTimer m_soon;
	bool m_applied = false;
};

} // namespace sme::app
