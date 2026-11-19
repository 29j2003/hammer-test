// model_rebuild_job.h - Runs a model rebuild (sme/model_rebuild.h) for any page: the sources
// are written in the background, the game's studiomdl compiles them into the mod at the model's
// own path, and the game's file list and the model cache see the new files at once. One job at a
// time per object; the Weapons page uses it for sound events, the animation editor for new
// frames, effects on models for particles and lights during an animation.
//
//     auto* job = new ModelRebuildJob(editor, this);
//     connect(job, &ModelRebuildJob::finished, this, [](bool ok, const QString& message) { ... });
//     job->start("models/weapons/v_smg1.mdl", changes);
//
// restore() takes a rebuild out again (the game's model, or the mod's own earlier copy, counts again).

#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

#include <sme/model_check.h>
#include <sme/model_rebuild.h>

class QProcess;

namespace sme::app {

class Editor;

/// One rebuild at a time (see the file comment).
class ModelRebuildJob : public QObject {
	Q_OBJECT

public:
	explicit ModelRebuildJob(Editor* editor, QObject* parent = nullptr);
	~ModelRebuildJob() override;

	/// Starts the rebuild with `changes` on top of the model's stored ones. False (with
	/// finished(false, why) already sent) if it cannot start: no mod, no studiomdl, busy.
	bool start(const QString& model, const ModelRebuildChanges& changes);
	[[nodiscard]] bool busy() const { return m_busy; }
	/// The model being rebuilt (empty when idle).
	[[nodiscard]] QString model() const { return m_busy ? m_model : QString(); }
	/// What the model check found in the model the last successful build made.
	[[nodiscard]] const std::vector<ModelIssue>& lastCheck() const { return m_lastCheck; }
	/// Everything studiomdl printed for the last rebuild.
	[[nodiscard]] QString log() const { return m_log; }
	/// studiomdl to use instead of the game's (tests: a script that writes the model).
	void setStudiomdl(const QString& program) { m_studiomdlOverride = program; }

	/// Takes the model's rebuild out of the mod. False with `error` if a file could not be changed.
	bool restore(const QString& model, QString* error = nullptr);
	/// Whether the mod has a rebuild of the model.
	[[nodiscard]] bool rebuilt(const QString& model) const;
	/// The changes stored for the model so far (empty if none).
	[[nodiscard]] ModelRebuildChanges storedChanges(const QString& model) const;

signals:
	/// A line of studiomdl's output.
	void output(const QString& line);
	/// The rebuild is done (ok) or failed (`message` says why, the log has studiomdl's lines).
	void finished(bool ok, const QString& message);

private:
	void compile();
	void done(bool ok, const QString& message);
	/// Tells the file system and the model cache that the model's files in the mod changed.
	void noteModelFiles(const QString& model);
	[[nodiscard]] QString studiomdl() const;

	Editor* m_editor;
	QString m_model;
	std::vector<ModelIssue> m_lastCheck;
	QString m_log;
	QString m_studiomdlOverride;
	bool m_busy = false;
	std::optional<ModelRebuildPlan> m_plan;
	QFutureWatcher<std::pair<std::optional<ModelRebuildPlan>, QString>> m_prepare;
	QPointer<QProcess> m_process;
};

} // namespace sme::app
