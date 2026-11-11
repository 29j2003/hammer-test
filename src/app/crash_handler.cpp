// crash_handler.cpp - Last-chance handlers that write a crash file (see crash_handler.h).

#include "crash_handler.h"

#include "autosaver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <sme/crash_log.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sme::app {

namespace {

// Prepared at install time: the handlers must not allocate.
char g_version[64] = "?";
#ifdef _WIN32
wchar_t g_path[1024] = L"";
#else
char g_path[1024] = "";
#endif
std::atomic<bool> g_writing{false};
std::terminate_handler g_previousTerminate = nullptr;

constexpr int MAX_FRAMES = 62;

/// One open crash file; writes go straight to the OS.
struct CrashFile {
#ifdef _WIN32
	HANDLE handle = INVALID_HANDLE_VALUE;
	CrashFile() { handle = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); }
	~CrashFile() {
		if (handle != INVALID_HANDLE_VALUE) {
			FlushFileBuffers(handle);
			CloseHandle(handle);
		}
	}
	void write(const char* text, std::size_t length) {
		DWORD written = 0;
		if (handle != INVALID_HANDLE_VALUE) WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
	}
#else
	int fd = -1;
	CrashFile() { fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
	~CrashFile() {
		if (fd >= 0) ::close(fd);
	}
	void write(const char* text, std::size_t length) {
		if (fd >= 0) {
			const auto ignored = ::write(fd, text, length);
			(void)ignored;
		}
	}
#endif
	void write(const char* text) { write(text, std::strlen(text)); }
};

/// The module an address lies in and the offset from its start.
bool moduleOf(const void* address, char* module, std::size_t size, std::uint64_t& offset) {
#ifdef _WIN32
	HMODULE handle = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &handle) || !handle) {
		return false;
	}
	if (GetModuleFileNameA(handle, module, static_cast<DWORD>(size)) == 0) module[0] = '\0';
	offset = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(handle));
	return true;
#else
	Dl_info info{};
	if (!dladdr(address, &info) || !info.dli_fbase) return false;
	std::snprintf(module, size, "%s", info.dli_fname ? info.dli_fname : "?");
	offset = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
	return true;
#endif
}

void writeFrame(CrashFile& file, int index, const void* address) {
	char module[512] = "?";
	std::uint64_t offset = reinterpret_cast<std::uintptr_t>(address);
	if (!moduleOf(address, module, sizeof module, offset)) std::snprintf(module, sizeof module, "?");
	char line[640];
	const auto length = formatCrashFrame(line, sizeof line, index, module, offset);
	file.write(line, length);
}

/// Writes the crash file: the first line says what happened, then where, then the stack.
void writeCrash(const char* reason, const void* faultAddress) {
	if (g_writing.exchange(true)) return;   // a crash while writing the crash file
	CrashFile file;
	char line[512];
	std::snprintf(line, sizeof line, "Mapsmith %s crashed: %s\n", g_version, reason);
	file.write(line);
	if (faultAddress) {
		char module[512] = "?";
		std::uint64_t offset = 0;
		if (moduleOf(faultAddress, module, sizeof module, offset)) {
			char frame[640];
			const auto length = formatCrashFrame(frame, sizeof frame, 0, module, offset);
			// "  #0 Mapsmith.exe+0x..." -> "fault at Mapsmith.exe+0x..."
			const char* text = std::strchr(frame, ' ');
			text = text ? std::strchr(text + 1, ' ') : nullptr;
			file.write("fault at ");
			if (text) file.write(text + 1, length - static_cast<std::size_t>(text + 1 - frame));
			else file.write("?\n");
		}
	}
	file.write("stack:\n");
	void* frames[MAX_FRAMES] = {};
#ifdef _WIN32
	const int count = CaptureStackBackTrace(0, MAX_FRAMES, frames, nullptr);
#else
	const int count = backtrace(frames, MAX_FRAMES);
#endif
	for (int i = 0; i < count; ++i) writeFrame(file, i, frames[i]);
}

#ifdef _WIN32
LONG WINAPI unhandledException(EXCEPTION_POINTERS* info) {
	const auto* record = info ? info->ExceptionRecord : nullptr;
	char reason[256] = "unknown exception";
	if (record) {
		const auto code = static_cast<std::uint32_t>(record->ExceptionCode);
		const char* name = crashCodeName(code);
		if (code == 0xC0000005u && record->NumberParameters >= 2) {
			const char* how = record->ExceptionInformation[0] == 0 ? "reading" : record->ExceptionInformation[0] == 1 ? "writing" : "executing";
			std::snprintf(reason, sizeof reason, "%s (code 0x%08x, %s address 0x%llx)", name, code, how,
			              static_cast<unsigned long long>(record->ExceptionInformation[1]));
		} else {
			std::snprintf(reason, sizeof reason, "%s (code 0x%08x)", *name ? name : "exception", code);
		}
	}
	writeCrash(reason, record ? record->ExceptionAddress : nullptr);
	return EXCEPTION_CONTINUE_SEARCH;   // Windows' own error reporting still runs
}

void invalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
	writeCrash("invalid parameter passed to a C runtime function", nullptr);
	std::abort();
}

void pureCall() {
	writeCrash("pure virtual function call (an object used after it was destroyed)", nullptr);
	std::abort();
}
#endif

void fatalSignal(int signal) {
	const char* name = signal == SIGSEGV ? "segmentation fault (SIGSEGV)"
	                 : signal == SIGABRT ? "abort (SIGABRT: qFatal, a failed check or std::abort)"
	                 : signal == SIGFPE  ? "arithmetic error (SIGFPE)"
	                 : signal == SIGILL  ? "illegal instruction (SIGILL)"
	                                     : "fatal signal";
	writeCrash(name, nullptr);
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

void terminated() {
	char reason[512] = "std::terminate (an exception nobody caught)";
	try {
		if (const auto current = std::current_exception()) std::rethrow_exception(current);
	} catch (const std::exception& e) {
		std::snprintf(reason, sizeof reason, "uncaught exception: %s", e.what());
	} catch (...) {
	}
	writeCrash(reason, nullptr);
	if (g_previousTerminate) g_previousTerminate();
	std::abort();
}

} // namespace

QString crashFilePath() {
	return QDir(QFileInfo(Autosaver::markerPath()).absolutePath()).filePath("crash.txt");
}

void installCrashHandler(const QString& version) {
	std::snprintf(g_version, sizeof g_version, "%s", version.toUtf8().constData());
	const QString path = QDir::toNativeSeparators(crashFilePath());
	QDir().mkpath(QFileInfo(crashFilePath()).absolutePath());
#ifdef _WIN32
	const auto wide = path.toStdWString();
	wcsncpy_s(g_path, wide.c_str(), _TRUNCATE);
	SetUnhandledExceptionFilter(unhandledException);
	_set_invalid_parameter_handler(invalidParameter);
	_set_purecall_handler(pureCall);
	// A stack overflow leaves no stack for the filter: reserve some for it.
	ULONG reserve = 64 * 1024;
	SetThreadStackGuarantee(&reserve);
	for (const int s : {SIGABRT, SIGFPE, SIGILL, SIGSEGV}) std::signal(s, fatalSignal);
#else
	std::snprintf(g_path, sizeof g_path, "%s", QFile::encodeName(path).constData());
	for (const int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) std::signal(s, fatalSignal);
#endif
	g_previousTerminate = std::set_terminate(terminated);
	// backtrace() loads libgcc on first use: do it now, not inside a crash.
#ifndef _WIN32
	void* warm[1];
	backtrace(warm, 1);
#endif
}

QString takeCrashFile() {
	QFile file(crashFilePath());
	if (!file.open(QIODevice::ReadOnly)) return {};
	const QString text = QString::fromUtf8(file.readAll());
	file.close();
	const QString kept = QDir(QFileInfo(crashFilePath()).absolutePath()).filePath("last-crash-trace.txt");
	QFile::remove(kept);
	QFile::rename(crashFilePath(), kept);
	return text;
}

} // namespace sme::app
