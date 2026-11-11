// crash_log.h - The text of a crash file: what the crash handler writes when Mapsmith dies
// (exception code, the faulting module + offset, one stack frame per line) and the one line the
// next start shows about it.
//
// Frames are "module+offset" (module file name, offset from where it was loaded), not names:
// release builds carry no symbols, the matching .pdb (Windows CI artifact "Mapsmith-windows-pdb")
// turns an offset into a function and line later. Formatting writes into a caller's buffer with
// no heap use, so a crash handler can call it while the process is falling apart.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sme {

/// Writes "  #<index> <module file name>+0x<offset>\n" into `out` (at most `size` bytes, always
/// zero-terminated); the module may be given with its folder (only the file name is kept).
/// Returns the length written. No heap use (crash handlers call it).
std::size_t formatCrashFrame(char* out, std::size_t size, int index, std::string_view module, std::uint64_t offset);

/// The name of a Windows exception code ("access violation" for 0xC0000005), or "" if unknown.
[[nodiscard]] const char* crashCodeName(std::uint32_t code);

/// One line about a crash file for the next start's prompt: its first line (what happened) plus
/// the first frame in Mapsmith's own code ("... at Mapsmith.exe+0x1a2b3c"). Empty for an empty text.
[[nodiscard]] std::string crashSummary(std::string_view text, std::string_view ownModule = "mapsmith");

} // namespace sme
