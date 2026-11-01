// texture_flow_dialog.h - "Texture flow": lets the texture run on over the selected faces (see
// sme/texture_flow.h) - wrapped over edges, round curves, along strips, or continuing a
// neighbour - with a live preview. Like the road dialog it stays open beside the map: every
// change shows the result at once (Editor::setBrushPreview); OK applies it as one undo step,
// Cancel or closing leaves the map as it was.

#pragma once

#include <QDialog>
#include <QTimer>

#include <sme/texture_flow.h>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace sme::app {

class Editor;

class TextureFlowDialog : public QDialog {
	Q_OBJECT

public:
	/// `material`: the material to start with (empty: the seed face's own).
	explicit TextureFlowDialog(Editor* editor, const QString& material = {}, QWidget* parent = nullptr);
	~TextureFlowDialog() override;

	/// The options as the fields show them.
	[[nodiscard]] FlowOptions options() const;
	/// The line under the fields (what the tool does, or why nothing).
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void done(int result) override;

private:
	/// Plans again and shows the preview (soon after a change).
	void refresh();
	/// Shows the fields the mode uses.
	void updateFields(const FlowPlan* plan);

	Editor* m_editor;
	QComboBox* m_mode;
	QComboBox* m_preset;
	QLineEdit* m_material;
	QCheckBox* m_flip;
	QCheckBox* m_curve;
	QSpinBox* m_repeats;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QTimer m_soon;
	bool m_finished = false;
};

} // namespace sme::app
