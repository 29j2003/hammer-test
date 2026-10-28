// compile_log.h - Understands the output of VBSP, VVIS and VRAD: which lines are errors or
// warnings, what they mean in plain words and how far a step is (the "0...1...2..." dots).

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sme {

/// What one line of compile output means for the mapper (see classifyCompileLine).
struct CompileMessage {
	enum class Severity { Info, Warning, Error };
	Severity severity = Severity::Info;
	std::string explanation;   ///< What it means and what to do (empty for unknown lines)
	bool leak = false;         ///< The map leaks (a .lin pointfile was written)
};

/// Classifies one output line; nullopt for ordinary progress/info lines.
[[nodiscard]] std::optional<CompileMessage> classifyCompileLine(std::string_view line);

/// Progress of the current phase in percent from the tools' "0...1...2...10" dots, or nullopt.
[[nodiscard]] std::optional<int> compileProgress(std::string_view text);

/// Name of the phase a progress line belongs to ("PortalFlow" in "PortalFlow: 0...1..."), or empty.
[[nodiscard]] std::string compilePhase(std::string_view line);

/// The cubemaps in a compiled map: its env_cubemap sample count (lump 42) and how many of them
/// the game has built into its pakfile (lump 40: materials/maps/<map>/c<x>_<y>_<z>[.hdr].vtf;
/// VBSP's cubemapdefault does not count). nullopt if the bytes are no readable .bsp.
struct BspCubemaps {
	int samples = 0;
	int built = 0;
};
[[nodiscard]] std::optional<BspCubemaps> readBspCubemaps(std::span<const std::byte> bsp, std::string_view mapName);

/// Whether a -buildcubemaps run of the game worked. The game ends with exit code 1 after building
/// (it quits itself), so the exit code says nothing: what counts is the map the game loads
/// (before/after: readBspCubemaps of maps/<map>.bsp, nullopt when unreadable).
struct CubemapVerdict {
	bool ok = false;
	std::string message;   ///< One line for the compile log
};
[[nodiscard]] CubemapVerdict judgeCubemapBuild(int exitCode, bool crashed, const std::optional<BspCubemaps>& before,
                                               const std::optional<BspCubemaps>& after, bool mapRewritten);

} // namespace sme
