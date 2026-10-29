// ship_panel.h - "Ship": getting the open map or the whole mod ready for players. Lists what the
// map needs (the mod's own files, other mod folders, the base game, missing files), packs the
// mod's own files into the BSP with bspzip, writes a share ZIP or a ZIP of the mod without the
// mapper's files, and prepares a Steam Workshop upload for steamcmd (which asks for the login
// itself; the editor never sees or stores a password).

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <filesystem>
#include <functional>

#include <sme/shipping.h>

class QLabel;
class QVBoxLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QComboBox;
class QTreeWidget;
class QProcess;

namespace sme::app {

class Editor;

/// The Ship tab of the Project page (see the file comment).
class ShipPanel : public QWidget {
	Q_OBJECT

public:
	explicit ShipPanel(Editor* editor, QWidget* parent = nullptr);
	~ShipPanel() override;

	/// Finds what the open map needs, in the background (signal scanned()).
	void scan();
	[[nodiscard]] const MapDependencies& dependencies() const { return m_deps; }

	/// The BSP the jobs work on (by default the open map's BSP in the mod's maps folder).
	[[nodiscard]] QString bspPath() const;

	/// The jobs below run in the background and end with taskFinished().
	void makeShareZip(const QString& zipPath);
	void makeModZip(const QString& zipPath);
	/// Packs the mod's own files into the BSP (bspzip of the game's bin folder).
	void packIntoBsp();
	/// Workshop: writes the content folder and the item file; returns the steamcmd command.
	QString prepareWorkshop();

	[[nodiscard]] bool busy() const { return m_busy; }
	/// The full path of bspzip next to the compile tools (empty if there is none).
	[[nodiscard]] QString bspzipPath() const;

signals:
	/// The scan / a background job finished.
	void scanned();
	void taskFinished(bool ok, const QString& message);

private:
	struct ScanResult {
		MapDependencies deps;
		int generation = 0;
	};
	struct TaskResult {
		bool ok = false;
		QString message;
	};

	void buildMapCard(QVBoxLayout* layout);
	void buildModCard(QVBoxLayout* layout);
	void buildWorkshopCard(QVBoxLayout* layout);
	void showDependencies();
	void startTask(const QString& what, std::function<TaskResult()> job);
	void setBusy(bool busy);
	void log(const QString& line, bool problem = false);
	void gameChanged();
	[[nodiscard]] QString defaultBsp() const;
	[[nodiscard]] std::filesystem::path modDir() const;

	Editor* m_editor;
	MapDependencies m_deps;
	int m_generation = 0;
	bool m_busy = false;
	bool m_bspEdited = false;

	QLineEdit* m_bsp = nullptr;
	QLabel* m_summary = nullptr;
	QTreeWidget* m_files = nullptr;
	QPlainTextEdit* m_readme = nullptr;
	QLabel* m_modSummary = nullptr;
	QTreeWidget* m_excluded = nullptr;
	QWidget* m_workshop = nullptr;
	QSpinBox* m_appId = nullptr;
	QLineEdit* m_publishedId = nullptr;
	QLineEdit* m_title = nullptr;
	QPlainTextEdit* m_description = nullptr;
	QLineEdit* m_changeNote = nullptr;
	QLineEdit* m_preview = nullptr;
	QComboBox* m_visibility = nullptr;
	QLineEdit* m_steamcmd = nullptr;
	QLineEdit* m_steamUser = nullptr;
	QLineEdit* m_command = nullptr;
	QPlainTextEdit* m_log = nullptr;
	QList<QPushButton*> m_jobButtons;

	QFutureWatcher<ScanResult> m_scanWatcher;
	QFutureWatcher<TaskResult> m_taskWatcher;
	QProcess* m_bspzip = nullptr;
};

} // namespace sme::app
