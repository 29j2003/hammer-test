// process_guard.h - Stops the tools a panel started when the panel goes away.
//
// A QProcess child that still runs while its owner is destroyed is killed by ~QProcess, which
// then emits finished() - into the owner's slots, on an object that is half gone (a crash on
// closing Mapsmith while git, studiomdl or a compile tool runs). Owners call this first in their
// destructor: the signals to them are cut, then the tools are stopped.
// Background reads of a panel (QFutureWatcher children) that use the game's file system are waited
// for before that file system goes away (Editor::gameAboutToChange): waitForOwnTasks().

#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>

namespace sme::app {

/// Disconnects every QProcess child of `owner` from it and stops the running ones.
inline void stopOwnProcesses(QObject* owner) {
	for (auto* process : owner->findChildren<QProcess*>(Qt::FindDirectChildrenOnly)) {
		process->disconnect(owner);
		if (process->state() != QProcess::NotRunning) {
			process->kill();
			process->waitForFinished(2000);
		}
	}
}

/// Waits for the background jobs of `owner` (its QFutureWatcher children) to finish.
inline void waitForOwnTasks(QObject* owner) {
	for (auto* watcher : owner->findChildren<QFutureWatcherBase*>(Qt::FindDirectChildrenOnly)) watcher->waitForFinished();
}

} // namespace sme::app
