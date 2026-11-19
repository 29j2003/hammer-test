// model_check_dialog.h - "Check model": the findings of sme/model_check.h for one model in a list
// (errors first, what to do next to each), opened from the Models and Weapons pages; the same
// check runs before and after ModelRebuildJob builds a model.

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QString>

#include <vector>

#include <sme/model_check.h>

class QLabel;
class QTreeWidget;

namespace sme::app {

class Editor;

/// Runs the check with the game's sound scripts and particle systems (reads them: call it off the
/// UI thread for a large game, or accept a short wait).
[[nodiscard]] std::vector<ModelIssue> runModelCheck(const Editor* editor, const QString& model, bool withSoundsAndParticles = true);

class ModelCheckDialog : public QDialog {
	Q_OBJECT

public:
	ModelCheckDialog(Editor* editor, const QString& model, QWidget* parent = nullptr);

	/// Checks again (in the background; checked() follows).
	void recheck();
	[[nodiscard]] const std::vector<ModelIssue>& issues() const { return m_issues; }

signals:
	void checked();

private:
	void showIssues(const std::vector<ModelIssue>& issues);

	Editor* m_editor;
	QString m_model;
	std::vector<ModelIssue> m_issues;
	QFutureWatcher<std::vector<ModelIssue>> m_watcher;
	QLabel* m_summary = nullptr;
	QTreeWidget* m_list = nullptr;
};

/// Opens the dialog for `model` (non-modal, deleted when closed).
ModelCheckDialog* openModelCheckDialog(Editor* editor, const QString& model, QWidget* parent);

} // namespace sme::app
