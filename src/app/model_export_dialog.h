// model_export_dialog.h - "Export for editing ...": a model of the game (or one of its animations)
// taken apart into .smd files and a .qc for Blender's Source Tools and studiomdl (core:
// sme/model_export.h). The work runs in the background with a slim progress bar; at the end one
// line says what happened, with "Open folder".

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QStringList>

#include <atomic>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;

namespace sme::app {

class Editor;

/// The export dialog (see the file comment).
class ModelExportDialog : public QDialog {
	Q_OBJECT

public:
	/// `sequences` empty: the whole model with all its own animations; else only these animations
	/// (with the reference, so the skeleton comes along).
	ModelExportDialog(Editor* editor, const QString& model, const QStringList& sequences = {}, QWidget* parent = nullptr);
	~ModelExportDialog() override;

	/// The folder the files go to (default: <mod>/modelsrc/<model path>).
	[[nodiscard]] QString folder() const;
	void setFolder(const QString& folder);
	/// Starts the export (false if one runs or there is no game).
	bool exportNow();
	[[nodiscard]] bool busy() const;
	/// The line under the buttons.
	[[nodiscard]] QString resultText() const;

signals:
	/// The export ended (ok: files written).
	void exported(bool ok);

private:
	struct Result {
		bool ok = false;
		QString message;
		int files = 0;
		QStringList notes;
	};
	void stop();
	void showResult(const Result& result);

	Editor* m_editor;
	QString m_model;
	QStringList m_sequences;
	QLineEdit* m_folder;
	QCheckBox* m_animations;
	QCheckBox* m_physics;
	QCheckBox* m_materials;
	QProgressBar* m_progress;
	QLabel* m_result;
	QPushButton* m_export;
	QPushButton* m_open;
	QPushButton* m_close;
	QTimer* m_poll;
	QFutureWatcher<Result> m_watcher;
	std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<std::atomic<int>> m_done = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<int>> m_total = std::make_shared<std::atomic<int>>(0);
};

} // namespace sme::app
