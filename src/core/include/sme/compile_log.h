// compile_log.h - Understands the output of VBSP, VVIS and VRAD: which lines are errors or
// warnings, what they mean in plain words and how far a step is (the "0...1...2..." dots).

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sme {

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

} // namespace sme
