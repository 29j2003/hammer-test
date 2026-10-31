// road_dialog.h - "Road": lays a road material along the selected brushes' top faces (see
// sme/road_texture.h), with a live preview in the views. It stays open beside the map (not
// modal): every change shows the result at once (Editor::setBrushPreview); OK applies it as one
// undo step, Cancel or closing leaves the map as it was.

#pragma once

#include <QDialog>
#include <QTimer>

#include <sme/road_texture.h>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace sme::app {

class Editor;

class RoadDialog : public QDialog {
	Q_OBJECT

public:
	/// `material`: the road material to start with (empty: the current material, unless it is
	/// the editor's untouched default - then the one the road's faces already have).
	explicit RoadDialog(Editor* editor, const QString& material = {}, QWidget* parent = nullptr);
	~RoadDialog() override;

	/// The options as the fields show them.
	[[nodiscard]] RoadOptions options() const;
	/// The line under the fields (how many faces and chains, or why nothing).
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void done(int result) override;

private:
	/// Plans again and shows the preview (soon after a change, not on every key press).
	void refresh();

	Editor* m_editor;
	QLineEdit* m_material;
	QComboBox* m_along;
	QCheckBox* m_flip;
	QSpinBox* m_repeats;
	QLineEdit* m_side;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QTimer m_soon;
	bool m_finished = false;
};

} // namespace sme::app
