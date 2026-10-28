// mod_code_panel.h - Project > Mod code: the mod's source code (a Mapbase checkout with
// mapbase-fidelity) and its scripts, run by the editor - no .bat file, no console window.
// "Update mod" runs fidelity/update.ps1 in the background (Visual Studio 2022 builds the DLLs,
// they go into the mod, VRAD with baked AO into the SDK, the game files into the mod) with its
// steps, a progress bar, the log and a summary with advice when something fails; "Install game
// files only" runs deploy.ps1 and "Make report" report.ps1. The checkout is found by itself
// (next to the Source control folder, in the usual GitHub / Visual Studio folders) or chosen,
// and remembered per mod. Optional, per mod: update after every Pull, look for new commits
// when Mapsmith starts (git fetch) or pull and update then. The network is only used through
// the user's git, and only when one of these is switched on or a button pressed.

#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <optional>
#include <set>

#include <sme/mod_code.h>

class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;

namespace sme::app {

class Editor;

/// How "Make report" works, for the tooltips of its buttons (Map menu, Compile, Problems).
[[nodiscard]] QString reportHowTo();

/// The Mod code tab of the Project page (see the file comment).
class ModCodePanel : public QWidget {
	Q_OBJECT

public:
	/// Makes the command of a run (the default: modCodeCommand()). Tests put a tiny script in.
	using CommandFactory = std::function<std::optional<ScriptCommand>(ModCodeAction action, const ModCodeScripts& scripts,
	                                                                   const std::filesystem::path& modDir)>;

	explicit ModCodePanel(Editor* editor, QWidget* parent = nullptr);
	~ModCodePanel() override;

	void setCommandFactory(CommandFactory factory);

	/// Top folder of the checkout, empty when none is known.
	[[nodiscard]] QString repository() const { return m_root; }
	/// Uses this checkout for the current mod (remembered).
	void setRepository(const QString& folder);

	/// Starts a script; false (with the reason in the summary) when it cannot run now.
	bool start(ModCodeAction action);
	/// Stops the running script (and what it started, e.g. the build).
	void cancel();
	[[nodiscard]] bool running() const { return m_process != nullptr; }
	/// What the last run was.
	[[nodiscard]] ModCodeAction lastAction() const { return m_action; }
	/// Whether `action` can start now, else why not (in plain words).
	[[nodiscard]] bool canRun(ModCodeAction action, QString* why = nullptr) const;
	/// The last run: what it was and how it went.
	[[nodiscard]] const ScriptRun& lastRun() const { return m_run; }

	/// git fetch, then counts the commits the remote has that the checkout does not.
	void checkForChanges();
	/// git pull, then Update mod when the code changed since the last update.
	void pullAndUpdate();
	/// The Source control tab pulled `root`: updates when that is enabled and the code changed.
	void afterPull(const QString& root, bool changed);
	/// New commits on the remote (after checkForChanges()).
	[[nodiscard]] int newChanges() const { return m_newChanges; }

	/// The line for the Overview tab (hidden while there is nothing to say). Not parented: the
	/// owner puts it into its page.
	[[nodiscard]] QWidget* banner() const { return m_banner; }

signals:
	/// A script run ended.
	void finished(bool ok);
	/// Update mod or Install game files went through: the mod's files are new (reload the game).
	void modUpdated();
	/// The banner's "Show" was pressed.
	void showRequested();

private:
	void loadForGame();
	void findRepository();
	void showRepository();
	void readOutput(bool errorChannel);
	void handleLine(const QString& line, bool errorChannel);
	void processEnded(int exitCode);
	void showRun();
	void updateBanner();
	void updateButtons();
	void log(const QString& text, const QString& color = {});
	[[nodiscard]] QString settingsKey(const QString& name) const;
	[[nodiscard]] bool option(const QString& name) const;
	void setOption(const QString& name, bool on);
	/// Runs the system's git in the checkout (in the background, one after another).
	void git(const QStringList& args, std::function<void(int exitCode, const QString& out)> done);
	void compareWithLastUpdate(std::function<void(bool changed)> done);
	/// Once per mod and session: what the options ask for when Mapsmith starts.
	void runStartupCheck();

	Editor* m_editor;
	CommandFactory m_factory;
	QString m_gameKey;
	QString m_root;
	int m_searchGeneration = 0;
	std::set<QString> m_startChecked;       ///< Games whose start-up check ran this session
	QPointer<QProcess> m_process;
	QPointer<QProcess> m_gitProcess;
	ModCodeAction m_action = ModCodeAction::Update;
	ScriptRun m_run;
	bool m_hasRun = false;
	bool m_cancelled = false;
	bool m_updateQueued = false;
	QByteArray m_pending[2];                ///< Unfinished output lines (stdout, stderr)
	QElapsedTimer m_clock;
	int m_newChanges = 0;
	bool m_changedSinceUpdate = false;

	QLabel* m_folderLabel;
	QLabel* m_state;
	QPushButton* m_update;
	QPushButton* m_deploy;
	QPushButton* m_report;
	QPushButton* m_cancel;
	QPushButton* m_pullUpdate;
	QPushButton* m_check;
	QLabel* m_platformNote;
	QLabel* m_news;
	QCheckBox* m_noVrad;
	QCheckBox* m_autoAfterPull;
	QCheckBox* m_checkOnStart;
	QCheckBox* m_pullOnStart;
	QListWidget* m_steps;
	QProgressBar* m_progress;
	QLabel* m_summary;
	QPlainTextEdit* m_log;
	QPointer<QWidget> m_banner;   ///< Owned by the page it is put into
	QLabel* m_bannerText;
	QPushButton* m_bannerPull;
	QPushButton* m_bannerUpdate;
};

} // namespace sme::app
