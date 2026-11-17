// system_paths.h - Where Mapsmith finds the Windows programs it starts, and the process-wide
// rules for finding programs and DLLs.
//
// Windows looks for a program given without a folder (and for DLLs) in the current folder too:
// started from a Downloads folder that holds a planted powershell.exe or a DLL with a system
// name, Mapsmith would run it. hardenSearchPaths() takes the current folder out of both searches
// (first thing in main); the programs Mapsmith starts on Windows come with their full path from
// the system folder (systemProgram), the others from PATH via QStandardPaths::findExecutable.

#pragma once

#include <QString>

namespace sme::app {

/// No DLLs and no programs from the current folder (Windows; does nothing elsewhere).
void hardenSearchPaths();

/// The full path of a program of Windows itself, e.g. "powershell.exe" ->
/// C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe, "taskkill.exe" ->
/// C:\Windows\System32\taskkill.exe. Other names and other systems: `name` unchanged.
[[nodiscard]] QString systemProgram(const QString& name);

} // namespace sme::app
