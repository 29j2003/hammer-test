// model_rebuild_job.cpp - Background rebuild of a model into the mod (see model_rebuild_job.h).

#include "model_rebuild_job.h"

#include "editor.h"
#include "import_dialog.h"
#include "model_cache.h"
#include "process_guard.h"
#include "ui_helpers.h"

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/text.h>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace sme::app {

ModelRebuildJob::ModelRebuildJob(Editor* editor, QObject* parent) : QObject(parent), m_editor(editor) {
	m_prepare.setParent(this);
	connect(&m_prepare, &QFutureWatcherBase::finished, this, [this] {
		auto [plan, error] = m_prepare.result();
		if (!plan) {
			done(false, tr("Could not write the sources: %1").arg(error));
			return;
		}
		m_plan = std::move(plan);
		for (const auto& note : m_plan->notes) m_log += qs(note) + '\n';
		compile();
	});
	// The game's file system goes away: the sources must be written before.
	connect(editor, &Editor::gameAboutToChange, this, [this] { m_prepare.waitForFinished(); });
}

ModelRebuildJob::~ModelRebuildJob() {
	stopOwnProcesses(this);
	m_prepare.waitForFinished();
}

QString ModelRebuildJob::studiomdl() const {
	return m_studiomdlOverride.isEmpty() ? ImportDialog::studiomdlPath(m_editor) : m_studiomdlOverride;
}

bool ModelRebuildJob::start(const QString& model, const ModelRebuildChanges& changes) {
	if (m_busy) {
		emit finished(false, tr("Another model is being built"));
		return false;
	}
	const auto* entry = m_editor->gameEntry();
	auto* game = m_editor->game();
	if (!entry || !game) {
		emit finished(false, tr("No game is open"));
		return false;
	}
	const QString program = studiomdl();
	if (program.isEmpty() || !QFileInfo::exists(program)) {
		emit finished(false, tr("studiomdl was not found (%1). It comes with the game's SDK tools, next to vbsp.exe - check the game's tools in Compile settings.")
		                         .arg(program.isEmpty() ? tr("no SDK tools set") : program));
		return false;
	}
	m_model = qs(normalizePath(model.toStdString()));
	m_log.clear();
	m_plan.reset();
	m_busy = true;
	const GameFileSystem* fs = &game->files;
	const std::filesystem::path modDir = entry->gameDir;
	const std::string path = m_model.toStdString();
	m_prepare.setFuture(QtConcurrent::run([fs, modDir, path, changes] {
		std::string error;
		auto plan = prepareModelRebuild(*fs, modDir, path, changes, &error);
		return std::pair{std::move(plan), qs(error)};
	}));
	return true;
}

void ModelRebuildJob::compile() {
	auto* process = new QProcess(this);
	m_process = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	process->setWorkingDirectory(fromPath(m_plan->folder));
#ifdef Q_OS_WIN
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
	connect(process, &QProcess::readyRead, this, [this, process] {
		const QString text = QString::fromLocal8Bit(process->readAll()).remove('\r').trimmed();
		if (text.isEmpty()) return;
		m_log += text + '\n';
		emit output(text);
	});
	connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
		process->deleteLater();
		const auto* entry = m_editor->gameEntry();
		if (!entry || !m_plan) {
			done(false, tr("The game was closed while building"));
			return;
		}
		std::string error;
		const bool ok = status == QProcess::NormalExit && code == 0 && finishModelRebuild(*m_plan, entry->gameDir, &error);
		noteModelFiles(m_model);
		if (ok) {
			done(true, tr("Built %1 into the mod").arg(m_model));
		} else {
			if (!error.empty()) m_log += qs(error) + '\n';
			done(false, tr("studiomdl failed (exit code %1) - its lines say why").arg(code));
		}
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		process->deleteLater();
		if (m_plan && m_editor->gameEntry()) finishModelRebuild(*m_plan, m_editor->gameEntry()->gameDir);   // puts the mod's own files back
		done(false, tr("Could not start studiomdl: %1").arg(process->errorString()));
	});
	QStringList arguments;
	for (const auto& argument : m_plan->arguments) arguments << qs(argument);
	process->start(studiomdl(), arguments);
}

void ModelRebuildJob::done(bool ok, const QString& message) {
	m_busy = false;
	emit finished(ok, message);
}

void ModelRebuildJob::noteModelFiles(const QString& model) {
	const auto* entry = m_editor->gameEntry();
	auto* game = m_editor->game();
	if (!entry || !game) return;
	for (const auto& file : modelCompiledFiles(entry->gameDir, model.toStdString())) {
		std::error_code ec;
		if (std::filesystem::exists(file, ec)) {
			game->files.noteWrittenFile(file);
		} else {
			game->files.noteRemovedFile(file);
		}
	}
	if (m_editor->models()) m_editor->models()->forget(model);
}

bool ModelRebuildJob::restore(const QString& model, QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return false;
	std::string why;
	const bool ok = restoreModel(entry->gameDir, model.toStdString(), &why);
	noteModelFiles(qs(normalizePath(model.toStdString())));
	if (!ok && error) *error = qs(why);
	return ok;
}

bool ModelRebuildJob::rebuilt(const QString& model) const {
	const auto* entry = m_editor->gameEntry();
	return entry && modelRebuildRecord(entry->gameDir, model.toStdString()).has_value();
}

ModelRebuildChanges ModelRebuildJob::storedChanges(const QString& model) const {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return {};
	const auto stored = loadModelChanges(entry->gameDir, model.toStdString());
	return stored ? stored->changes : ModelRebuildChanges{};
}

} // namespace sme::app
