// material_review_dialog.h - The round trip for the mod's materials (sme/material_review.h,
// sme/material_decisions.h):
//   - "Export materials for review ...": contact sheets (JPEG, 12 materials each), index.csv/json and
//     a README in ZIP parts of at most 20 MB, made in the background - to send into a chat;
//   - "Load material decisions ...": the answer (pasted or a .txt) with a preview of what changes,
//     merged into (or replacing) the mod's cfg/mapsmith_materials.txt;
//   - openMaterialDecisionsFile: the file in the system's text editor (made with a short header
//     when missing).

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <sme/material_decisions.h>
#include <sme/material_review.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTreeWidget;

namespace sme {
class GameFileSystem;
}

namespace sme::app {

class Editor;

/// What an export wrote.
struct ReviewExportResult {
	int materials = 0;
	int sheets = 0;
	QString folder;          ///< The folder with the sheets, index and README
	QStringList zips;        ///< The ZIP parts (each at most the limit)
	QString error;           ///< Empty when it worked
	bool cancelled = false;
};

/// The export itself (any thread): sheets drawn with QPainter, written as JPEG into `folder`, then
/// the index, the README and the ZIP parts (`partLimit` bytes each). `progress(done, total)` returns
/// false to stop.
[[nodiscard]] ReviewExportResult exportMaterialReview(const GameFileSystem& fs, const std::vector<std::string>& materials, const MaterialDecisions& decisions,
                                                      const QString& folder, const QString& game, const std::function<bool(int, int)>& progress = {},
                                                      qint64 partLimit = 20ll * 1024 * 1024);
/// One contact sheet (12 materials: 4 x 3), readable when scaled down to 1600 pixels wide.
[[nodiscard]] QImage drawReviewSheet(const std::vector<ReviewEntry>& entries, int sheet, int sheets);
/// The brush materials of the open map (lower case).
[[nodiscard]] std::set<std::string> openMapMaterialNames(const Editor* editor);

/// "Export materials for review ...".
class MaterialReviewDialog : public QDialog {
	Q_OBJECT

public:
	explicit MaterialReviewDialog(Editor* editor, QWidget* parent = nullptr);
	~MaterialReviewDialog() override;

	/// The materials the chosen scope lists (for the count and the export).
	[[nodiscard]] std::vector<std::string> chosenMaterials() const;
	/// Starts the export (as the button does).
	void start();
	[[nodiscard]] bool running() const { return m_watcher.isRunning(); }
	[[nodiscard]] const ReviewExportResult& result() const { return m_result; }

signals:
	void exportFinished(const ReviewExportResult& result);

private:
	void updateCount();
	Editor* m_editor;
	QRadioButton* m_maps;
	QRadioButton* m_folderScope;
	QRadioButton* m_all;
	QLineEdit* m_folderFilter;
	QCheckBox* m_tools;
	QCheckBox* m_skybox;
	QCheckBox* m_decals;
	QLineEdit* m_output;
	QLabel* m_count;
	QProgressBar* m_progress;
	QLabel* m_status;
	QPushButton* m_start;
	QPushButton* m_cancel;
	QPushButton* m_openFolder;
	QFutureWatcher<ReviewExportResult> m_watcher;
	QTimer m_tick;
	std::shared_ptr<std::atomic<int>> m_done = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<int>> m_total = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<bool>> m_stop = std::make_shared<std::atomic<bool>>(false);
	ReviewExportResult m_result;
};

/// "Load material decisions ...": paste the answer or open a .txt, see what changes, apply.
class MaterialDecisionsDialog : public QDialog {
	Q_OBJECT

public:
	explicit MaterialDecisionsDialog(Editor* editor, QWidget* parent = nullptr);

	/// The pasted text (tests set it).
	void setText(const QString& text);
	/// Merge (keep the file's other lines) or replace the whole file.
	void setReplace(bool replace);
	/// What the file would be after Apply.
	[[nodiscard]] MaterialDecisions result() const;
	/// Lines of the preview: "material: before -> after".
	[[nodiscard]] QStringList previewLines() const;
	/// Writes the result (as the Apply button).
	bool apply();

private:
	void updatePreview();
	Editor* m_editor;
	QPlainTextEdit* m_text;
	QComboBox* m_mode;
	QTreeWidget* m_preview;
	QLabel* m_problems;
	QPushButton* m_apply;
};

/// Opens the mod's decision file in the system's editor (made with a short header when missing).
void openMaterialDecisionsFile(Editor* editor, QWidget* parent);

} // namespace sme::app
