// round_edge_dialog.h - "Round or bevel edge": radius and segments for every corner the selected
// faces form (several brushes per side and several corners at once), with a live preview in the
// views. It stays open beside the map (not modal): every change of a value shows the result at
// once (Editor::setBrushPreview); OK applies it as one undo step, Cancel or closing leaves the map
// as it was. A radius that does not fit is shown as made smaller ("limited to N units"), never
// refused; segments default to Auto (from the radius). Pieces that can only be rounded as one
// brush get a "Merge and round" button - never done without asking (see sme/edge_round.h).

#pragma once

#include <QDialog>
#include <QTimer>

class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace sme::app {

class Editor;

class RoundEdgeDialog : public QDialog {
	Q_OBJECT

public:
	explicit RoundEdgeDialog(Editor* editor, QWidget* parent = nullptr);
	~RoundEdgeDialog() override;

	/// The line under the values (limits, what happens next to the floor, or why not).
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void done(int result) override;

private:
	/// Plans again and shows the preview (soon after a change, not on every key press).
	void refresh();

	Editor* m_editor;
	QDoubleSpinBox* m_radius;
	QSpinBox* m_segments;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QPushButton* m_merge;      ///< "Merge and round" (shown only when merging is the only way)
	QTimer m_soon;
	bool m_finished = false;
};

} // namespace sme::app
