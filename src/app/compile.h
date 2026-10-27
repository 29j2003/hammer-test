// compile.h - Compiling and running a map (VBSP -> VVIS -> VRAD -> copy -> cubemaps -> game),
// the compile dialog (a simple view with cards and an advanced view with every
// parameter of each tool) and the per-game settings dialog.

#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <vector>

#include <sme/compile_options.h>
#include <sme/math.h>

#include "editor.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;

namespace sme::app {

/// What a compile runs (steps, quality, game window, extra parameters); see compile_options.h.
using CompileOptions = CompileSettings;

/// Compile settings of a game, as last used (QSettings "compile/<game>"); defaults if none.
[[nodiscard]] CompileOptions loadCompileOptions(const QString& gameKey);
void saveCompileOptions(const QString& gameKey, const CompileOptions& options);

class CompileRunner : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;

	void start(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options);
	void cancel();
	[[nodiscard]] bool running() const { return m_process != nullptr; }

	/// Command lines a compile would run (also shown in the dialog).
	[[nodiscard]] static QStringList describe(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options);

	/// Steps of the running (or last) compile.
	[[nodiscard]] QStringList stepNames() const;

signals:
	void output(const QString& text, const QColor& color);
	void leak(const std::vector<Vec3>& line);
	void finished(bool ok);
	// Structured progress for the compile window.
	void started(const QString& vmfPath, const QStringList& steps);
	void stepStarted(int index, const QString& command);
	void stepProgress(int index, const QString& phase, int percent);
	void stepFinished(int index, bool ok, qint64 milliseconds);
	void stepSkipped(int index, const QString& reason);
	/// A warning or error line (severity: 1 warning, 2 error) with a plain explanation if known.
	void problem(int index, const QString& line, int severity, const QString& explanation);

private:
	struct Step {
		QString name;
		QString program;
		QStringList arguments;
		bool detached = false;   ///< The game is started and left running
		bool copy = false;       ///< Copy the BSP instead of running a program
	};
	static std::vector<Step> plan(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, const CompileOptions& options);
	void next();
	void handleLine(const QString& line);
	void fail(const QString& text);

	std::vector<Step> m_steps;
	std::size_t m_index = 0;
	QProcess* m_process = nullptr;
	QString m_vmfPath, m_gameDir;
	bool m_leaked = false;
	int m_current = -1;
	bool m_cancelled = false;
	QElapsedTimer m_stepClock;
	QString m_partial;       ///< Output after the last line break (tools print progress dots without one)
	QString m_phase;
};

class CompileDialog : public QDialog {
	Q_OBJECT

public:
	/// With a `gameKey` the settings are read from and (on Compile) saved for that game.
	CompileDialog(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, QWidget* parent = nullptr, const QString& gameKey = {});
	[[nodiscard]] CompileOptions options() const { return m_options; }
	/// Simple (cards) or advanced (parameters of each tool) view.
	void setAdvanced(bool advanced);
	/// The command lines the compile would run, one per line (shown at the bottom).
	[[nodiscard]] QString commandText() const;

private:
	QWidget* buildSimple();
	QWidget* buildAdvanced();
	QFrame* card(const QString& title, const QString& tool, const QString& description, QCheckBox** toggle);
	QWidget* segmented(const QString& name, const QStringList& labels, QButtonGroup** group);
	void loadIntoWidgets();
	void changed();
	void selectTool(int index);
	void fillParameters();
	void fillAdded();
	void addParameter(const QString& text);

	QString m_vmfPath, m_gameDir, m_gameKey;
	GameSettings m_settings;
	CompileOptions m_options;
	bool m_loading = false;
	CompileTool m_tool = CompileTool::Vbsp;

	QComboBox* m_preset;
	QPushButton* m_simpleButton;
	QPushButton* m_advancedButton;
	QStackedWidget* m_pages;
	// Simple view
	QCheckBox *m_detail, *m_water, *m_vvis, *m_vrad, *m_cubemaps, *m_copy, *m_run;
	QCheckBox *m_console, *m_windowed, *m_novid;
	QButtonGroup *m_visQuality, *m_lightLevel, *m_lightQuality, *m_cubemapLevel;
	QLabel* m_bothWarning;
	QLabel* m_cubemapNote;
	QComboBox* m_resolution;
	QSpinBox *m_width, *m_height;
	QWidget* m_customSize;
	// Advanced view
	QListWidget* m_processes;
	QLabel* m_toolTitle;
	QLineEdit* m_search;
	QTreeWidget* m_parameters;
	QListWidget* m_added;
	QLineEdit* m_custom;
	QPlainTextEdit* m_commands;
};

class GameSettingsDialog : public QDialog {
	Q_OBJECT

public:
	GameSettingsDialog(Editor* editor, const QString& gameKey, QWidget* parent = nullptr);
	[[nodiscard]] GameSettings settings() const;

private:
	QListWidget* m_fgds;
	QLineEdit *m_vbsp, *m_vvis, *m_vrad, *m_gameExe, *m_launch, *m_mapDir;
};

} // namespace sme::app
