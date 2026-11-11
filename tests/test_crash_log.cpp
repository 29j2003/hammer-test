// test_crash_log.cpp - Crash file lines: frames as module+offset, the summary for the next start.

#include <doctest/doctest.h>

#include <sme/crash_log.h>

#include <string>

using namespace sme;

TEST_CASE("crash log: frames are the module's file name plus the offset, the summary names the first own frame") {
	char line[128];
	CHECK(formatCrashFrame(line, sizeof line, 0, "C:\\Users\\x\\AppData\\Local\\Programs\\Mapsmith\\Mapsmith.exe", 0x1a2b3c) ==
	      std::string("  #0 Mapsmith.exe+0x1a2b3c\n").size());
	CHECK(std::string(line) == "  #0 Mapsmith.exe+0x1a2b3c\n");
	formatCrashFrame(line, sizeof line, 12, "/usr/lib/libQt6Core.so.6", 0x10);
	CHECK(std::string(line) == "  #12 libQt6Core.so.6+0x10\n");
	formatCrashFrame(line, sizeof line, 3, "", 0);
	CHECK(std::string(line) == "  #3 ?+0x0\n");
	// A small buffer is cut, never overrun, and stays terminated.
	char tiny[8];
	CHECK(formatCrashFrame(tiny, sizeof tiny, 0, "Mapsmith.exe", 0xffff) == 7);
	CHECK(std::string(tiny).size() == 7);

	CHECK(std::string(crashCodeName(0xC0000005u)) == "access violation");
	CHECK(std::string(crashCodeName(0x12345678u)).empty());

	const std::string text = "Mapsmith 0.2.0 crashed: access violation (code 0xc0000005, reading address 0x0)\n"
	                         "fault at ntdll.dll+0x2f3a\n"
	                         "stack:\n"
	                         "  #0 ntdll.dll+0x2f3a\n"
	                         "  #1 Qt6Core.dll+0x123\n"
	                         "  #2 Mapsmith.exe+0x4d2f10\n"
	                         "  #3 Mapsmith.exe+0x100\n";
	CHECK(crashSummary(text) == "Mapsmith 0.2.0 crashed: access violation (code 0xc0000005, reading address 0x0) at Mapsmith.exe+0x4d2f10");
	CHECK(crashSummary("Mapsmith crashed: abort\n  #0 libc.so.6+0x10\n") == "Mapsmith crashed: abort");
	CHECK(crashSummary("").empty());
}
