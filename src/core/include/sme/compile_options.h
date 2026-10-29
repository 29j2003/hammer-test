// compile_options.h - What a map compile runs: the steps (VBSP, VVIS, VRAD, copy, cubemaps,
// game), the simple switches (quality, HDR, detail, water ...), the game window (console,
// windowed, resolution) and extra parameters per tool, and the command lines they make.
//
// Qt-free so the command lines can be tested; the editor fills in the programs' paths.

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

enum class CompileTool { Vbsp, Vvis, Vrad, Copy, Cubemaps, Game };
constexpr int COMPILE_TOOL_COUNT = 6;

/// "VBSP", "VVIS", "VRAD", "Copy", "Cubemaps", "Game".
[[nodiscard]] std::string_view compileToolName(CompileTool tool);

/// A parameter a compile tool understands (Source SDK 2013 and Mapbase compile tools).
struct CompileParameter {
	std::string name;          ///< "-leaktest", "+mat_specular"
	std::string value;         ///< Example value when it takes one ("2", "fidelity_lights.rad"), empty for a switch
	std::string description;
};
/// Every parameter the tool understands, for the advanced compile view.
[[nodiscard]] const std::vector<CompileParameter>& knownParameters(CompileTool tool);

enum class CompileQuality { Fast, Normal, Final };
enum class LightLevel { Ldr, Hdr, Both };

/// Everything the compile dialog can set: which steps run, their quality, the game window and extra parameters.
struct CompileSettings {
	enum class Preset { Fast, Normal, Final, Custom };

	// Geometry (VBSP)
	bool vbsp = true;
	bool detail = true;                ///< Off: -nodetail (func_detail left out)
	bool water = true;                 ///< Off: -nowater
	// Visibility (VVIS)
	bool vvis = true;
	bool visFast = false;              ///< -fast
	// Lighting (VRAD)
	bool vrad = true;
	CompileQuality lightQuality = CompileQuality::Normal;
	LightLevel light = LightLevel::Ldr;
	// After compiling
	bool copy = true;                  ///< BSP into the game's maps folder
	bool cubemaps = false;             ///< Start the game with -buildcubemaps (needs the copy)
	LightLevel cubemapLevel = LightLevel::Ldr;
	bool run = true;                   ///< Start the game with the map
	// The game window (game and cubemaps)
	bool console = true;               ///< -console
	bool windowed = true;              ///< -windowed
	bool novid = true;                 ///< -novid: no intro video
	/// +sv_cheats 1 when the game starts with the map (impulse 101, noclip, god while testing;
	/// not for building cubemaps)
	bool cheats = true;
	int width = 1920, height = 1080;   ///< -w -h; 0 = the game's own setting
	/// Parameters added per tool, as typed ("-leaktest", "-lights fidelity_lights.rad").
	std::array<std::vector<std::string>, COMPILE_TOOL_COUNT> extra;

	/// Extra parameters typed for one tool.
	[[nodiscard]] std::vector<std::string>& extraFor(CompileTool tool) { return extra[static_cast<std::size_t>(tool)]; }
	[[nodiscard]] const std::vector<std::string>& extraFor(CompileTool tool) const { return extra[static_cast<std::size_t>(tool)]; }
	/// Quality presets: Fast (VVIS -fast, VRAD fast), Normal, Final (VRAD -final). Custom changes nothing.
	void applyPreset(Preset preset);
	/// The preset these settings match, or Custom.
	[[nodiscard]] Preset preset() const;
	/// Whether a step runs at all (cubemaps need the copy).
	[[nodiscard]] bool runs(CompileTool tool) const;
	void setRuns(CompileTool tool, bool on);

	/// "key=value" lines (unknown keys are ignored when reading, missing ones keep the defaults).
	[[nodiscard]] std::string serialize() const;
	[[nodiscard]] static CompileSettings parse(std::string_view text);
};

/// One step of a compile: the tool, a name for the progress list and its arguments.
/// Copy steps have the source and target BSP as their two arguments.
struct CompileCommand {
	CompileTool tool = CompileTool::Vbsp;
	std::string name;                  ///< "VBSP", "Cubemaps (HDR)", ...
	std::vector<std::string> arguments;
	bool detached = false;             ///< The game is started and left running
};

/// The arguments a tool gets from the settings, without -game and the map (for the preview).
[[nodiscard]] std::vector<std::string> toolArguments(const CompileSettings& settings, CompileTool tool, std::string_view launchOptions = {});
/// Every step in order. `mapBase` is the .vmf path without the extension; `launchOptions` the
/// game's own launch options (game settings) - console, window and resolution there are left
/// to the settings.
[[nodiscard]] std::vector<CompileCommand> compileCommands(const CompileSettings& settings, std::string_view mapBase, std::string_view gameDir,
                                                          std::string_view launchOptions = {});
/// Splits a command line at spaces; "quoted parts" stay together.
[[nodiscard]] std::vector<std::string> splitArguments(std::string_view text);
/// Joins arguments for display, quoting those with spaces.
[[nodiscard]] std::string joinArguments(const std::vector<std::string>& arguments);

} // namespace sme
