// mod_code.h - The mod's source code (a Mapbase checkout with mapbase-fidelity): finding the
// checkout, the command lines of its scripts (update.ps1 builds and installs everything after a
// pull, deploy.ps1 copies the game files, report.ps1 packs what `fidelity_report` wrote) and
// reading update.ps1's output as steps, errors and a summary - so the editor can run them in the
// background instead of the user starting .bat files.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// The scripts of a mod source checkout (all in its fidelity folder).
struct ModCodeScripts {
	std::filesystem::path root;      ///< Top folder of the checkout
	std::filesystem::path update;    ///< fidelity/update.ps1
	std::filesystem::path deploy;    ///< fidelity/deploy.ps1 (may be missing)
	std::filesystem::path report;    ///< fidelity/report.ps1 (may be missing)
};

/// The scripts when `folder` is a mod source checkout: fidelity/update.ps1 next to sp/src or
/// update.bat. nullopt for any other folder.
[[nodiscard]] std::optional<ModCodeScripts> modCodeScripts(const std::filesystem::path& folder);

/// Finds the checkout of a mod named `modName` (its folder name in sourcemods): each search root
/// itself, then its sub folders - first the ones named like the mod, in all roots, then any other
/// (at most 300 per root). Roots that do not exist are skipped.
[[nodiscard]] std::optional<std::filesystem::path> findModCode(std::string_view modName,
                                                               const std::vector<std::filesystem::path>& roots);

/// Where checkouts usually are: GitHub Desktop's and Visual Studio's folders in the home
/// folder, the home folder itself, and folders like GitHub or repos at the top of each drive.
[[nodiscard]] std::vector<std::filesystem::path> modCodeSearchRoots(const std::filesystem::path& home,
                                                                    const std::vector<std::filesystem::path>& drives);

/// What a script run does.
enum class ModCodeAction {
	Update,          ///< update.ps1: projects, build, DLLs into the mod, VRAD, game files
	UpdateNoVrad,    ///< update.ps1 -NoVrad
	Deploy,          ///< deploy.ps1: only the game files (no build)
	Report,          ///< report.ps1: packs fidelity_report into fidelity_report.zip
	RemoveGameFiles, ///< deploy.ps1 -Remove: takes out what deploy.ps1 put into the mod (fresh install)
};

/// Building the DLLs needs Windows with Visual Studio 2022; deploy and report also run with pwsh.
[[nodiscard]] bool modCodeNeedsWindows(ModCodeAction action);

/// A program with its arguments, ready for a process (no shell, nothing to quote).
struct ScriptCommand {
	std::string program;
	std::vector<std::string> args;
	std::filesystem::path workingDir;
};

/// The command for `action`: Windows PowerShell on Windows, else `pwsh`, always without profile,
/// prompts or a console of its own and with -GamePath, so the scripts never wait for input.
/// nullopt when the checkout lacks that script.
[[nodiscard]] std::optional<ScriptCommand> modCodeCommand(const ModCodeScripts& scripts, ModCodeAction action,
                                                          const std::filesystem::path& modDir, bool windows);

/// The steps of update.ps1, in order.
enum class UpdateStep { Start, VisualStudio, Projects, Build, Dlls, Vrad, GameFiles, Done };
inline constexpr int updateStepCount = 8;

/// English name of a step ("Build game code").
[[nodiscard]] std::string_view updateStepName(UpdateStep step);

/// One line of the scripts' output, read.
struct ScriptLine {
	enum class Kind {
		Info,        ///< Anything else
		Ok,          ///< "ok", "copied", "fixed" ...: a step went well
		Skipped,     ///< "skipped ..." (worth showing, not a failure)
		Warning,     ///< A compiler or MSBuild warning
		Error,       ///< A compiler / MSBuild / PowerShell error line
		Failed,      ///< "FAILED <what to do>": the script stops
		Success,     ///< "All set." / "Done.": the script finished
	};
	Kind kind = Kind::Info;
	std::optional<UpdateStep> step;  ///< The step this line starts or belongs to
	std::string message;             ///< The text without the status word ("FAILED" ...)
};

/// Reads one output line of update.ps1, deploy.ps1 or report.ps1.
[[nodiscard]] ScriptLine classifyScriptLine(std::string_view line);

/// A whole run, fed line by line: the current step only goes forward.
class ScriptRun {
public:
	void feed(std::string_view line);
	/// A line on the error channel (PowerShell's own errors): always an error.
	void feedError(std::string_view line);
	/// The process ended (exit code -1: crashed or cancelled).
	void finish(int exitCode);

	[[nodiscard]] UpdateStep step() const { return m_step; }
	[[nodiscard]] bool finished() const { return m_finished; }
	[[nodiscard]] bool succeeded() const { return m_finished && m_exitCode == 0 && m_failure.empty(); }
	/// The "FAILED" message, empty if none.
	[[nodiscard]] const std::string& failure() const { return m_failure; }
	/// The first error lines (at most 12), for the summary.
	[[nodiscard]] const std::vector<std::string>& errors() const { return m_errors; }
	/// "skipped" lines (e.g. VRAD without Source SDK Base 2013 Singleplayer).
	[[nodiscard]] const std::vector<std::string>& skipped() const { return m_skipped; }
	/// Files the script named as written (report: the zips to send).
	[[nodiscard]] const std::vector<std::string>& outputs() const { return m_outputs; }
	/// What the user can do about the failure, in plain words (empty after success).
	[[nodiscard]] std::string advice() const;

private:
	UpdateStep m_step = UpdateStep::Start;
	bool m_finished = false;
	int m_exitCode = 0;
	std::string m_failure;
	std::vector<std::string> m_errors;
	std::vector<std::string> m_skipped;
	std::vector<std::string> m_outputs;
};

/// Advice for a "FAILED" message of update.ps1 (empty when there is nothing better than the message).
[[nodiscard]] std::string updateFailureAdvice(std::string_view failure);

} // namespace sme
