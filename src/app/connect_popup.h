// connect_popup.h - "Connect": after the source and the target entity are picked, a small popup
// lists the likely output -> input pairs (io_links.h) with the best one chosen, the full output
// and input lists to pick something else, parameter, delay and "only once". Enter or Connect
// adds the output in one undo step (the target gets a name if it has none) and shows it in
// Object Properties.

#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;

namespace sme::app {

class Editor;

/// The popup (see the file comment). It deletes itself when closed.
class ConnectPopup : public QDialog {
	Q_OBJECT

public:
	ConnectPopup(Editor* editor, int sourceId, int targetId, QWidget* parent);

	[[nodiscard]] QListWidget* suggestions() const { return m_suggestions; }
	[[nodiscard]] QComboBox* outputBox() const { return m_output; }
	[[nodiscard]] QComboBox* inputBox() const { return m_input; }
	[[nodiscard]] QDoubleSpinBox* delayBox() const { return m_delay; }

	/// Adds the connection as chosen and closes (Enter / Connect).
	void accept() override;

private:
	Editor* m_editor;
	int m_source;
	int m_target;
	QListWidget* m_suggestions;
	QComboBox* m_output;
	QComboBox* m_input;
	QLineEdit* m_parameter;
	QDoubleSpinBox* m_delay;
	QCheckBox* m_once;
};

} // namespace sme::app
