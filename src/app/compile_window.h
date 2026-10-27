// compile_window.h - "Compile" dock: every step (VBSP, VVIS, VRAD, copy, game) with status,
// time and progress, the warnings and errors with a plain explanation, a summary and the
// full log. Fed by the signals of CompileRunner.

#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace sme::app {

class CompileRunner;

/// The Compile dock (see the file comment).
class CompileWindow : public QWidget {
	Q_OBJECT

public:
	enum class StepState { Waiting, Running, Done, Failed, Skipped };

	explicit CompileWindow(CompileRunner* runner, QWidget* parent = nullptr);

	/// State of the steps, the problem counts and a one-line summary (also read by tests).
	[[nodiscard]] int stepCount() const;
	[[nodiscard]] StepState stepState(int index) const;
	[[nodiscard]] int warningCount() const { return m_warnings; }
	[[nodiscard]] int errorCount() const { return m_errors; }
	[[nodiscard]] QString summary() const;

signals:
	/// The buttons "compile again" and "show leak" were pressed.
	void compileAgain();
	void showLeak();

private:
	void setState(int index, StepState state, const QString& detail = {});
	void updateTimes();
	void finish(bool ok);

	CompileRunner* m_runner;
	QLabel* m_title;
	QLabel* m_time;
	QProgressBar* m_total;
	QPushButton* m_cancel;
	QPushButton* m_again;
	QPushButton* m_leakButton;
	QTreeWidget* m_steps;
	QTreeWidget* m_problems;
	QLabel* m_summary;
	QPlainTextEdit* m_log;
	QTimer m_tick;
	QElapsedTimer m_clock;
	QElapsedTimer m_stepClock;
	int m_running = -1;
	int m_warnings = 0;
	int m_errors = 0;
	bool m_leaked = false;
	QString m_mapName;
};

} // namespace sme::app
