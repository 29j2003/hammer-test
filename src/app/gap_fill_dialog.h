// gap_fill_dialog.h - "Fill gap": closes the space between the selected brushes (see
// sme/gap_fill.h) with a live preview. Not modal, like the other brush dialogs: the views show the
// fill at once (Editor::setBrushPreview) and follow a changed selection; Fill applies it as one
// undo step, Cancel or closing leaves the map as it was.

#pragma once

#include <QDialog>
#include <QTimer>

#include <sme/gap_fill.h>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;

namespace sme::app {

class Editor;

class GapFillDialog : public QDialog {
	Q_OBJECT

public:
	explicit GapFillDialog(Editor* editor, QWidget* parent = nullptr);
	~GapFillDialog() override;

	[[nodiscard]] GapFillOptions options() const;
	/// What the fill will be, or why there is none.
	[[nodiscard]] QString note() const;

public slots:
	void accept() override;
	void done(int result) override;

private:
	void refresh();

	Editor* m_editor;
	QRadioButton* m_hull;
	QRadioButton* m_box;
	QLineEdit* m_material;
	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QTimer m_soon;
	bool m_finished = false;
};

} // namespace sme::app
