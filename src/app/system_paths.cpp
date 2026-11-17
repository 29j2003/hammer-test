// system_paths.cpp - Program and DLL search rules (see system_paths.h).

#include "system_paths.h"

#include <QDir>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sme::app {

void hardenSearchPaths() {
#ifdef _WIN32
	// DLLs: the current folder leaves the search order (LoadLibrary without a path).
	SetDllDirectoryW(L"");
	// SearchPath (which finds programs and files without a folder): the current folder after the
	// system folders, for the whole process.
	SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);
	// Programs started without a folder: not from the current folder (honoured by CreateProcess's
	// search and by the children, e.g. PowerShell and cmd).
	SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", L"1");
	qputenv("NoDefaultCurrentDirectoryInExePath", "1");
#endif
}

QString systemProgram(const QString& name) {
#ifdef _WIN32
	wchar_t buffer[MAX_PATH];
	const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return name;
	const QDir system(QString::fromWCharArray(buffer, int(length)));
	const QString lower = name.toLower();
	if (lower == QLatin1String("powershell") || lower == QLatin1String("powershell.exe")) {
		return QDir::toNativeSeparators(system.filePath(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe")));
	}
	if (lower == QLatin1String("taskkill") || lower == QLatin1String("taskkill.exe")) {
		return QDir::toNativeSeparators(system.filePath(QStringLiteral("taskkill.exe")));
	}
#endif
	return name;
}

} // namespace sme::app
