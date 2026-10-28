// mod_code.cpp - The mod's source code and its scripts (see mod_code.h).

#include <sme/mod_code.h>

#include <sme/text.h>

#include <algorithm>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

bool isDir(const fs::path& path) {
	std::error_code ec;
	return fs::is_directory(path, ec);
}

bool isFile(const fs::path& path) {
	std::error_code ec;
	return fs::is_regular_file(path, ec);
}

/// Sub folders of `dir` (no hidden ones), at most `limit`, sorted by name.
std::vector<fs::path> subFolders(const fs::path& dir, std::size_t limit) {
	std::vector<fs::path> folders;
	std::error_code ec;
	fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	for (; !ec && it != fs::directory_iterator() && folders.size() < limit; it.increment(ec)) {
		const auto name = it->path().filename().string();
		if (name.empty() || name.front() == '.' || name.front() == '$') continue;
		std::error_code typeError;
		if (it->is_directory(typeError)) folders.push_back(it->path());
	}
	std::sort(folders.begin(), folders.end());
	return folders;
}

/// The first word and the rest of a status line ("  copied   bin\client.dll").
std::pair<std::string_view, std::string_view> splitStatus(std::string_view line) {
	line = trim(line);
	const auto space = line.find(' ');
	if (space == std::string_view::npos) return {line, {}};
	return {line.substr(0, space), trim(line.substr(space + 1))};
}

bool contains(std::string_view text, std::string_view part) {
	return toLower(text).find(toLower(part)) != std::string::npos;
}

/// Compiler, linker and MSBuild errors: "file.cpp(12): error C2065: ...", "LINK : fatal error LNK1104",
/// "error MSB3073: ...".
bool isErrorLine(std::string_view line) {
	return contains(line, ": error ") || contains(line, ": fatal error ") || startsWithI(trim(line), "error ") ||
	       startsWithI(trim(line), "fatal error ");
}

bool isWarningLine(std::string_view line) {
	return contains(line, ": warning ") || startsWithI(trim(line), "warning ");
}

/// UTF-8 text of a path with the system's separators (what the process gets).
std::string nativeText(fs::path path) {
	const auto text = path.make_preferred().u8string();
	return std::string(text.begin(), text.end());
}

/// A .zip path in a line ("  D:\...\fidelity_report.zip  (3.2 MB)").
std::string zipIn(std::string_view line) {
	line = trim(line);
	const auto lower = toLower(line);
	const auto end = lower.find(".zip");
	if (end == std::string::npos) return {};
	return std::string(line.substr(0, end + 4));
}

} // namespace

std::optional<ModCodeScripts> modCodeScripts(const fs::path& folder) {
	if (folder.empty()) return std::nullopt;
	const fs::path tools = folder / "fidelity";
	if (!isFile(tools / "update.ps1")) return std::nullopt;
	if (!isDir(folder / "sp" / "src") && !isFile(folder / "update.bat")) return std::nullopt;
	ModCodeScripts scripts;
	scripts.root = folder;
	scripts.update = tools / "update.ps1";
	if (isFile(tools / "deploy.ps1")) scripts.deploy = tools / "deploy.ps1";
	if (isFile(tools / "report.ps1")) scripts.report = tools / "report.ps1";
	return scripts;
}

std::optional<fs::path> findModCode(std::string_view modName, const std::vector<fs::path>& roots) {
	std::vector<fs::path> existing;
	for (const auto& root : roots) {
		if (root.empty() || !isDir(root)) continue;
		if (std::find(existing.begin(), existing.end(), root) != existing.end()) continue;
		if (modCodeScripts(root)) return root;
		existing.push_back(root);
	}
	// Named like the mod first (D:\GitHub\mymod for sourcemods\mymod), in every root.
	if (!modName.empty()) {
		for (const auto& root : existing) {
			if (const auto child = findChildI(root, modName); child && modCodeScripts(*child)) return *child;
		}
	}
	for (const auto& root : existing) {
		for (const auto& child : subFolders(root, 300)) {
			if (modCodeScripts(child)) return child;
		}
	}
	return std::nullopt;
}

std::vector<fs::path> modCodeSearchRoots(const fs::path& home, const std::vector<fs::path>& drives) {
	std::vector<fs::path> roots;
	if (!home.empty()) {
		// GitHub Desktop, Visual Studio, and the usual others.
		for (const char* sub : {"Documents/GitHub", "GitHub", "source/repos", "Documents/source/repos", "repos", "Documents", "dev",
		                        "src", "Projects", "Desktop"}) {
			roots.push_back(home / fs::path(sub));
		}
		roots.push_back(home);
	}
	for (const auto& drive : drives) {
		for (const char* sub : {"GitHub", "Git", "repos", "Projects", "dev", "src", "Source", "Modding"}) {
			roots.push_back(drive / sub);
		}
		roots.push_back(drive);
	}
	return roots;
}

bool modCodeNeedsWindows(ModCodeAction action) {
	return action == ModCodeAction::Update || action == ModCodeAction::UpdateNoVrad;
}

std::optional<ScriptCommand> modCodeCommand(const ModCodeScripts& scripts, ModCodeAction action, const fs::path& modDir, bool windows) {
	fs::path script;
	std::vector<std::string> extra;
	switch (action) {
		case ModCodeAction::Update: script = scripts.update; break;
		case ModCodeAction::UpdateNoVrad:
			script = scripts.update;
			extra.emplace_back("-NoVrad");
			break;
		case ModCodeAction::Deploy: script = scripts.deploy; break;
		case ModCodeAction::Report: script = scripts.report; break;
		case ModCodeAction::RemoveGameFiles:
			script = scripts.deploy;
			extra.emplace_back("-Remove");
			break;
	}
	if (script.empty() || modDir.empty()) return std::nullopt;
	ScriptCommand command;
	// -NonInteractive: a prompt the scripts might still show fails instead of waiting forever.
	command.program = windows ? "powershell.exe" : "pwsh";
	command.args = {"-NoLogo", "-NoProfile", "-NonInteractive"};
	if (windows) command.args.insert(command.args.end(), {"-ExecutionPolicy", "Bypass"});
	command.args.insert(command.args.end(), {"-File", nativeText(script)});
	command.args.insert(command.args.end(), {"-GamePath", nativeText(modDir)});
	command.args.insert(command.args.end(), extra.begin(), extra.end());
	command.workingDir = scripts.root;
	return command;
}

std::string_view updateStepName(UpdateStep step) {
	switch (step) {
		case UpdateStep::Start: return "Start";
		case UpdateStep::VisualStudio: return "Find Visual Studio 2022";
		case UpdateStep::Projects: return "Project files";
		case UpdateStep::Build: return "Build game code";
		case UpdateStep::Dlls: return "Copy DLLs into the mod";
		case UpdateStep::Vrad: return "VRAD with baked AO";
		case UpdateStep::GameFiles: return "Game files (shaders, materials, cfg)";
		case UpdateStep::Done: return "Done";
	}
	return {};
}

ScriptLine classifyScriptLine(std::string_view line) {
	ScriptLine out;
	const auto [word, rest] = splitStatus(line);
	out.message = std::string(trim(line));
	const bool maptools = contains(rest, "maptools.sln");
	if (startsWithI(trim(line), "mapbase-fidelity update ->")) {
		out.step = UpdateStep::Start;
	} else if (startsWithI(trim(line), "mapbase-fidelity game files ->")) {
		out.step = UpdateStep::GameFiles;
	} else if (word == "FAILED") {
		out.kind = ScriptLine::Kind::Failed;
		out.message = std::string(rest);
	} else if (word == "found") {
		out.kind = ScriptLine::Kind::Ok;
		out.step = UpdateStep::VisualStudio;
	} else if (word == "fixed") {
		out.kind = ScriptLine::Kind::Ok;
		out.step = maptools ? UpdateStep::Vrad : startsWithI(rest, "newer_vs_toolsets") ? UpdateStep::VisualStudio : UpdateStep::Projects;
	} else if (word == "making" || (word == "ok" && contains(rest, ".sln"))) {
		out.kind = ScriptLine::Kind::Ok;
		out.step = maptools ? UpdateStep::Vrad : UpdateStep::Projects;
	} else if (word == "building") {
		out.step = maptools ? UpdateStep::Vrad : UpdateStep::Build;
	} else if ((word == "ok" || word == "copied") && startsWithI(rest, "bin\\")) {
		out.kind = ScriptLine::Kind::Ok;
		out.step = UpdateStep::Dlls;
	} else if (word == "vrad" || word == "saved" || (word == "copied" && startsWithI(rest, "vrad"))) {
		out.kind = ScriptLine::Kind::Ok;
		out.step = UpdateStep::Vrad;
	} else if (word == "skipped") {
		out.kind = ScriptLine::Kind::Skipped;
		if (startsWithI(rest, "VRAD")) out.step = UpdateStep::Vrad;
	} else if (startsWithI(trim(line), "All set.")) {
		out.kind = ScriptLine::Kind::Success;
		out.step = UpdateStep::Done;
	} else if (startsWithI(trim(line), "Done.")) {
		out.kind = ScriptLine::Kind::Success;
	} else if (isErrorLine(line)) {
		out.kind = ScriptLine::Kind::Error;
	} else if (isWarningLine(line) || word == "warning") {
		out.kind = ScriptLine::Kind::Warning;
	} else if (word == "ok" || word == "copied" || word == "restored" || word == "packed") {
		out.kind = ScriptLine::Kind::Ok;
	}
	return out;
}

void ScriptRun::feed(std::string_view line) {
	const ScriptLine read = classifyScriptLine(line);
	if (read.step && *read.step > m_step) m_step = *read.step;
	switch (read.kind) {
		case ScriptLine::Kind::Failed:
			if (m_failure.empty()) m_failure = read.message;
			break;
		case ScriptLine::Kind::Error: {
			// The same error comes once per project and once more in MSBuild's summary.
			const bool seen = std::find(m_errors.begin(), m_errors.end(), read.message) != m_errors.end();
			if (!seen && m_errors.size() < 12) m_errors.push_back(read.message);
			break;
		}
		case ScriptLine::Kind::Skipped:
			// deploy.ps1 skips many things on purpose; only update's own skips matter.
			if (m_step < UpdateStep::GameFiles) m_skipped.push_back(std::string(splitStatus(line).second));
			break;
		default: break;
	}
	if (const auto zip = zipIn(line); !zip.empty() && std::find(m_outputs.begin(), m_outputs.end(), zip) == m_outputs.end()) {
		m_outputs.push_back(zip);
	}
}

void ScriptRun::feedError(std::string_view line) {
	const std::string message(trim(line));
	if (message.empty() || m_errors.size() >= 12) return;
	if (std::find(m_errors.begin(), m_errors.end(), message) == m_errors.end()) m_errors.push_back(message);
}

void ScriptRun::finish(int exitCode) {
	m_finished = true;
	m_exitCode = exitCode;
	if (exitCode == 0 && m_failure.empty() && m_step > UpdateStep::Start) m_step = UpdateStep::Done;
}

std::string ScriptRun::advice() const {
	if (succeeded()) return {};
	if (!m_failure.empty()) {
		auto advice = updateFailureAdvice(m_failure);
		if (!advice.empty()) return advice;
		return m_failure;
	}
	if (m_finished && m_exitCode < 0) return "The run was stopped before it finished. Start it again when you are ready.";
	return "The script stopped with an error - the red lines in the log say why.";
}

std::string updateFailureAdvice(std::string_view failure) {
	if (contains(failure, "Visual Studio 2022")) {
		return "Visual Studio 2022 was not found. Install it (the free Community edition is enough) with the workload "
		       "\"Desktop development with C++\", then press Update mod again.";
	}
	if (contains(failure, "build of") && contains(failure, "failed")) {
		return "The game code did not compile. The first errors are listed above; the whole build log is fidelity/update.log "
		       "in the source folder. Pull again later, or send that log to whoever maintains the code.";
	}
	if (contains(failure, "close the mod")) return "The game is still running. Close it, then press Update mod again.";
	if (contains(failure, "close Hammer")) return "Hammer or a map compile is still running. Close them, then press Update mod again.";
	if (contains(failure, "VPC could not make")) {
		return "The project files could not be made. Check that the source folder is complete (Pull again), then press Update mod again.";
	}
	if (contains(failure, "is missing - is this")) return "This folder is not the mod's source code. Choose the right folder above.";
	if (contains(failure, "was not built")) {
		return "The build finished, but a file is missing. The whole build log is fidelity/update.log in the source folder.";
	}
	if (contains(failure, "no saved VRAD")) return "There is no saved VRAD to put back - nothing was changed.";
	return {};
}

} // namespace sme
