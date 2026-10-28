// source_control_panel.h - Source control for the mod: a git repository (the mod folder or the
// mod's source code, chosen per game) with its branch, changes, commit, fetch / pull / push and
// history. It runs the system's git in the background; the network is only used when the user
// presses Fetch, Pull or Push.

#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <queue>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QCheckBox;

namespace sme::app {

class Editor;

/// The Source control tab of the Project page (see the file comment).
class SourceControlPanel : public QWidget {
	Q_OBJECT

public:
	explicit SourceControlPanel(Editor* editor, QWidget* parent = nullptr);
	/// Stops a git that still runs (its reply would reach a panel that is gone).
	~SourceControlPanel() override;

	/// Path of the git program, empty when none is installed.
	[[nodiscard]] static QString gitProgram();
	/// The folder shown (saved for the current game); may be a sub folder of the repository.
	[[nodiscard]] QString repository() const { return m_folder; }
	void setRepository(const QString& folder);
	/// Reads branch, changes, history and remote again (in the background; refreshed() follows).
	void refresh();
	/// A git command is running.
	[[nodiscard]] bool busy() const { return m_process != nullptr; }

signals:
	/// Everything was read again after refresh().
	void refreshed();
	/// A command the user started (commit, pull, ...) is done.
	void commandFinished(bool ok);
	/// A pull went through; `changed`: it brought new commits.
	void pulled(const QString& root, bool changed);
	/// "Update mod": the repository is the mod's source code (Project > Mod code runs it).
	void updateModRequested(const QString& root);

private:
	using Done = std::function<void(int exitCode, const QByteArray& out, const QByteArray& err)>;
	struct Job {
		QStringList args;
		Done done;
		bool logged = false;     ///< Show the command and its output in the log
	};

	void run(const QStringList& args, Done done, bool logged = false);
	void startNext();
	void loadForGame();
	void showState(int page, const QString& message = {});
	void setBusy(bool busy);
	void log(const QString& text, bool error = false);
	void commit();
	void pull();
	void network(const QStringList& args);
	void updateTools();
	[[nodiscard]] QStringList checkedPaths() const;

	Editor* m_editor;
	QString m_folder;          ///< As chosen
	QString m_root;            ///< Top folder of the repository (git paths are relative to it)
	QString m_branch;
	QString m_upstream;
	QString m_remoteUrl;
	QString m_solution;        ///< A Visual Studio solution in the repository, if any
	std::queue<Job> m_jobs;
	QPointer<QProcess> m_process;
	bool m_refreshing = false;

	QLabel* m_folderLabel;
	QLabel* m_branchLabel;
	QLabel* m_remoteLabel;
	QPushButton* m_openRemote;
	QPushButton* m_fetch;
	QPushButton* m_pull;
	QPushButton* m_push;
	QPushButton* m_openSolution;
	QPushButton* m_runUpdate;
	QStackedWidget* m_pages;
	QLabel* m_stateMessage;
	QPushButton* m_init;
	QTreeWidget* m_changes;
	QCheckBox* m_all;
	QPlainTextEdit* m_message;
	QPushButton* m_commit;
	QTreeWidget* m_history;
	QPlainTextEdit* m_log;
};

} // namespace sme::app
