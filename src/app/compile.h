// compile.h - Compiling and running a map (VBSP -> VVIS -> VRAD -> copy -> game), the
// compile dialog and the per-game settings dialog.

#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <vector>

#include <sme/math.h>

#include "editor.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

namespace sme::app {

struct CompileOptions {
	enum class Preset { Fast, Normal, Final };
	Preset preset = Preset::Normal;
	bool vbsp = true, vvis = true, vrad = true, copy = true, run = true, hdr = false;
};

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
	CompileDialog(const QString& vmfPath, const QString& gameDir, const GameSettings& settings, QWidget* parent = nullptr);
	[[nodiscard]] CompileOptions options() const;

private:
	void updatePreview();

	QString m_vmfPath, m_gameDir;
	GameSettings m_settings;
	QComboBox* m_preset;
	QCheckBox *m_vbsp, *m_vvis, *m_vrad, *m_copy, *m_run, *m_hdr;
	QLineEdit* m_preview;
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
