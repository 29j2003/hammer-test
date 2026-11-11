// crash_handler.h - Writes what happened when Mapsmith crashes, so the next start (and the
// editor report) can say where.
//
// installCrashHandler() hooks the last-chance handlers: Windows' unhandled-exception filter
// (access violation, stack overflow, ...), std::terminate (an exception nobody caught), abort
// (Qt's qFatal, failed asserts), the CRT's invalid-parameter and pure-virtual-call handlers, and
// on Linux/macOS the fatal signals. Each writes one small text file (crashFilePath): the reason
// and exception code, the faulting module + offset and up to 62 stack frames as module+offset
// (see crash_log.h - release builds have no symbols; the CI keeps the .pdb to look them up).
// The file is written with plain OS calls from preallocated buffers: no Qt, no heap.
//
// takeCrashFile() reads it on the next start (after a crash) and moves it aside; the crash
// prompt shows its summary and the editor report includes it.

#pragma once

#include <QString>

namespace sme::app {

/// Where the crash handler writes (in the app data folder, next to the autosave marker).
[[nodiscard]] QString crashFilePath();
/// Installs the handlers (once, at start). `version` goes into the file's first line.
void installCrashHandler(const QString& version);
/// The text the crash handler wrote in the last session (empty if none); the file is moved to
/// "last-crash-trace.txt" so it is read once.
[[nodiscard]] QString takeCrashFile();

} // namespace sme::app
