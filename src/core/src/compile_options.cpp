// compile_options.cpp - Compile steps and their command lines (see compile_options.h).

#include <sme/compile_options.h>

#include <sme/text.h>

#include <algorithm>
#include <charconv>

namespace sme {

namespace {

/// Launch options the settings decide (console, window, resolution, intro video): taken out of
/// the game's own launch options so they do not fight with the switches.
bool controlledByGameSettings(std::string_view argument) {
	for (const char* name : {"-console", "-windowed", "-window", "-sw", "-fullscreen", "-full", "-novid", "-w", "-h", "-width", "-height"}) {
		if (iequals(argument, name)) return true;
	}
	return false;
}

bool takesNumber(std::string_view argument) {
	return iequals(argument, "-w") || iequals(argument, "-h") || iequals(argument, "-width") || iequals(argument, "-height");
}

void append(std::vector<std::string>& out, const std::vector<std::string>& extra) {
	for (const auto& entry : extra) {
		for (auto& part : splitArguments(entry)) out.push_back(std::move(part));
	}
}

/// The game window and the game's own launch options.
std::vector<std::string> gameWindowArguments(const CompileSettings& s, std::string_view launchOptions) {
	std::vector<std::string> out;
	if (s.console) out.emplace_back("-console");
	if (s.windowed) out.emplace_back("-windowed");
	if (s.width > 0 && s.height > 0) {
		out.insert(out.end(), {"-w", std::to_string(s.width), "-h", std::to_string(s.height)});
	}
	if (s.novid) out.emplace_back("-novid");
	const auto own = splitArguments(launchOptions);
	for (std::size_t i = 0; i < own.size(); ++i) {
		if (controlledByGameSettings(own[i])) {
			if (takesNumber(own[i]) && i + 1 < own.size()) ++i;   // and its value
			continue;
		}
		out.push_back(own[i]);
	}
	return out;
}

std::string_view levelName(LightLevel level) {
	switch (level) {
		case LightLevel::Hdr: return "hdr";
		case LightLevel::Both: return "both";
		default: return "ldr";
	}
}

LightLevel parseLevel(std::string_view text) {
	if (text == "hdr") return LightLevel::Hdr;
	if (text == "both") return LightLevel::Both;
	return LightLevel::Ldr;
}

std::string_view qualityName(CompileQuality quality) {
	switch (quality) {
		case CompileQuality::Fast: return "fast";
		case CompileQuality::Final: return "final";
		default: return "normal";
	}
}

constexpr std::array<std::string_view, COMPILE_TOOL_COUNT> TOOL_KEYS = {"vbsp", "vvis", "vrad", "copy", "cubemaps", "game"};

} // namespace

std::string_view compileToolName(CompileTool tool) {
	static constexpr std::array<std::string_view, COMPILE_TOOL_COUNT> names = {"VBSP", "VVIS", "VRAD", "Copy", "Cubemaps", "Game"};
	return names[static_cast<std::size_t>(tool)];
}

const std::vector<CompileParameter>& knownParameters(CompileTool tool) {
	static const std::array<std::vector<CompileParameter>, COMPILE_TOOL_COUNT> lists = {{
		// VBSP
		{
			{"-onlyents", "", "Only update the entities of an already compiled BSP (fast, after entity changes)"},
			{"-onlyprops", "", "Only update the static props of an already compiled BSP"},
			{"-nodetail", "", "Leave out func_detail brushes"},
			{"-nowater", "", "Leave out water brushes"},
			{"-notjunc", "", "Do not fix T-junctions (can leave small cracks)"},
			{"-allowdetailcracks", "", "Do not fix T-junctions on func_detail"},
			{"-alldetail", "", "Turn every brush into detail (only for tests: no visibility)"},
			{"-fulldetail", "", "Treat func_detail as world brushes (for visibility tests)"},
			{"-leaktest", "", "Stop at a leak instead of compiling on"},
			{"-micro", "1.0", "Warn about brushes smaller than this volume"},
			{"-noweld", "", "Do not weld vertices together"},
			{"-nocsg", "", "Do not cut overlapping brushes"},
			{"-noshare", "", "Do not share vertices between faces"},
			{"-nomerge", "", "Do not merge faces"},
			{"-nomergewater", "", "Do not merge water faces"},
			{"-nosubdiv", "", "Do not subdivide large faces"},
			{"-noprune", "", "Do not prune the BSP tree"},
			{"-snapaxial", "", "Snap axial planes to integer coordinates"},
			{"-luxelscale", "1", "Scale every lightmap (smaller is sharper, bigger is faster)"},
			{"-lightifmissing", "", "Compile lights into the map even without lightmaps"},
			{"-bumpall", "", "Bump-map lightmaps on every face"},
			{"-forceskyvis", "", "Make 2D skybox faces see each other (fixes sky that disappears)"},
			{"-nodefaultcubemap", "", "Mapbase: no default cubemap when the map has none"},
			{"-defaultcubemapres", "32", "Mapbase: size of the default cubemap"},
			{"-low", "", "Run with low priority (the PC stays usable)"},
			{"-threads", "4", "Number of threads"},
			{"-verbose", "", "More output"},
		},
		// VVIS
		{
			{"-fast", "", "Quick visibility (for tests: more is drawn than needed)"},
			{"-radius_override", "2500", "Everything farther than this is not visible from a leaf"},
			{"-nosort", "", "Do not sort portals"},
			{"-low", "", "Run with low priority"},
			{"-threads", "4", "Number of threads"},
			{"-verbose", "", "More output"},
		},
		// VRAD
		{
			{"-ldr", "", "Low dynamic range light only (default)"},
			{"-hdr", "", "HDR light only"},
			{"-both", "", "LDR and HDR light (about twice the time)"},
			{"-fast", "", "Quick, rough lighting"},
			{"-final", "", "Best quality: more samples for the light from the sky"},
			{"-extrasky", "4", "More samples for the light from the sky (like -final)"},
			{"-bounce", "100", "Number of light bounces"},
			{"-noextra", "", "No extra samples (faster, rougher shadows)"},
			{"-smooth", "45", "Faces at a smaller angle are smoothed"},
			{"-softsun", "1", "Softer sun shadows (degrees)"},
			{"-luxeldensity", "1", "Scale all lightmaps (1 = as in the map)"},
			{"-chop", "4", "Size of light patches"},
			{"-maxchop", "4", "Largest light patches"},
			{"-StaticPropLighting", "", "Light static props per vertex (with -StaticPropPolys: their shadows too)"},
			{"-StaticPropPolys", "", "Static props cast shadows with their real shape"},
			{"-StaticPropSampleScale", "4", "More light samples on static props"},
			{"-StaticPropBounce", "4", "Mapbase: light bounces off static props"},
			{"-textureshadows", "", "Props with see-through textures (leaves, fences) cast fitting shadows"},
			{"-lights", "fidelity_lights.rad", "Read an extra lights file (glowing textures)"},
			{"-noskyboxrecurse", "", "Do not light the 3D skybox from the map"},
			{"-centersamples", "", "Sample lightmaps in the luxel center"},
			{"-dump", "", "Write patch data for debugging"},
			{"-low", "", "Run with low priority"},
			{"-threads", "4", "Number of threads"},
			{"-verbose", "", "More output"},
		},
		// Copy
		{},
		// Cubemaps
		{
			{"+mat_specular", "0", "Turn specular reflections off while building (cleaner cubemaps)"},
			{"-dxlevel", "95", "DirectX level to build with"},
			{"-nosound", "", "No sound while building"},
		},
		// Game
		{
			{"-console", "", "Open the developer console"},
			{"-windowed", "", "Run in a window"},
			{"-sw", "", "Run in a window (short form)"},
			{"-fullscreen", "", "Run full screen"},
			{"-noborder", "", "Window without a border"},
			{"-w", "1920", "Width of the window"},
			{"-h", "1080", "Height of the window"},
			{"-novid", "", "Skip the intro video"},
			{"-dev", "", "Developer mode: more messages"},
			{"-allowdebug", "", "Allow debugging (needed for some tools)"},
			{"-high", "", "Run with high priority"},
			{"-nosound", "", "Without sound"},
			{"-dxlevel", "95", "DirectX level"},
			{"+sv_cheats", "1", "Allow cheat commands (noclip, god ...)"},
			{"+developer", "1", "Developer messages on screen"},
			{"+mat_hdr_level", "2", "HDR level (0 LDR, 2 HDR)"},
			{"+skill", "2", "Difficulty (1 easy, 2 medium, 3 hard)"},
		},
	}};
	return lists[static_cast<std::size_t>(tool)];
}

void CompileSettings::applyPreset(Preset p) {
	switch (p) {
		case Preset::Fast:
			visFast = true;
			lightQuality = CompileQuality::Fast;
			break;
		case Preset::Normal:
			visFast = false;
			lightQuality = CompileQuality::Normal;
			break;
		case Preset::Final:
			visFast = false;
			lightQuality = CompileQuality::Final;
			break;
		case Preset::Custom:
			break;
	}
}

CompileSettings::Preset CompileSettings::preset() const {
	if (visFast && lightQuality == CompileQuality::Fast) return Preset::Fast;
	if (!visFast && lightQuality == CompileQuality::Normal) return Preset::Normal;
	if (!visFast && lightQuality == CompileQuality::Final) return Preset::Final;
	return Preset::Custom;
}

bool CompileSettings::runs(CompileTool tool) const {
	switch (tool) {
		case CompileTool::Vbsp: return vbsp;
		case CompileTool::Vvis: return vvis;
		case CompileTool::Vrad: return vrad;
		case CompileTool::Copy: return copy;
		case CompileTool::Cubemaps: return cubemaps && copy;
		case CompileTool::Game: return run;
	}
	return false;
}

void CompileSettings::setRuns(CompileTool tool, bool on) {
	switch (tool) {
		case CompileTool::Vbsp: vbsp = on; break;
		case CompileTool::Vvis: vvis = on; break;
		case CompileTool::Vrad: vrad = on; break;
		case CompileTool::Copy: copy = on; break;
		case CompileTool::Cubemaps: cubemaps = on; break;
		case CompileTool::Game: run = on; break;
	}
}

std::string CompileSettings::serialize() const {
	std::string out;
	const auto line = [&out](std::string_view key, std::string_view value) {
		out.append(key).append("=").append(value).append("\n");
	};
	const auto flag = [&line](std::string_view key, bool on) { line(key, on ? "1" : "0"); };
	flag("vbsp", vbsp);
	flag("detail", detail);
	flag("water", water);
	flag("vvis", vvis);
	flag("visFast", visFast);
	flag("vrad", vrad);
	line("lightQuality", qualityName(lightQuality));
	line("light", levelName(light));
	flag("copy", copy);
	flag("cubemaps", cubemaps);
	line("cubemapLevel", levelName(cubemapLevel));
	flag("run", run);
	flag("console", console);
	flag("windowed", windowed);
	flag("novid", novid);
	line("width", std::to_string(width));
	line("height", std::to_string(height));
	for (int t = 0; t < COMPILE_TOOL_COUNT; ++t) {
		for (const auto& entry : extra[static_cast<std::size_t>(t)]) {
			line(std::string(TOOL_KEYS[static_cast<std::size_t>(t)]) + "+", entry);
		}
	}
	return out;
}

CompileSettings CompileSettings::parse(std::string_view text) {
	CompileSettings s;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		const std::string_view line = text.substr(start, end - start);
		start = end + 1;
		const auto eq = line.find('=');
		if (eq == std::string_view::npos) continue;
		const std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
		const bool on = value == "1";
		const auto number = [&](int& target) {
			int v = 0;
			if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{} && v >= 0 && v <= 16384) target = v;
		};
		if (key == "vbsp") s.vbsp = on;
		else if (key == "detail") s.detail = on;
		else if (key == "water") s.water = on;
		else if (key == "vvis") s.vvis = on;
		else if (key == "visFast") s.visFast = on;
		else if (key == "vrad") s.vrad = on;
		else if (key == "lightQuality") s.lightQuality = value == "fast" ? CompileQuality::Fast : (value == "final" ? CompileQuality::Final : CompileQuality::Normal);
		else if (key == "light") s.light = parseLevel(value);
		else if (key == "copy") s.copy = on;
		else if (key == "cubemaps") s.cubemaps = on;
		else if (key == "cubemapLevel") s.cubemapLevel = parseLevel(value);
		else if (key == "run") s.run = on;
		else if (key == "console") s.console = on;
		else if (key == "windowed") s.windowed = on;
		else if (key == "novid") s.novid = on;
		else if (key == "width") number(s.width);
		else if (key == "height") number(s.height);
		else if (key.ends_with('+')) {
			const auto tool = std::find(TOOL_KEYS.begin(), TOOL_KEYS.end(), key.substr(0, key.size() - 1));
			if (tool != TOOL_KEYS.end() && !trim(value).empty()) {
				s.extra[static_cast<std::size_t>(tool - TOOL_KEYS.begin())].emplace_back(trim(value));
			}
		}
	}
	return s;
}

std::vector<std::string> toolArguments(const CompileSettings& s, CompileTool tool, std::string_view launchOptions) {
	std::vector<std::string> out;
	switch (tool) {
		case CompileTool::Vbsp:
			if (!s.detail) out.emplace_back("-nodetail");
			if (!s.water) out.emplace_back("-nowater");
			break;
		case CompileTool::Vvis:
			if (s.visFast) out.emplace_back("-fast");
			break;
		case CompileTool::Vrad:
			if (s.lightQuality == CompileQuality::Fast) out.insert(out.end(), {"-bounce", "2", "-noextra"});
			if (s.lightQuality == CompileQuality::Final) out.emplace_back("-final");
			if (s.light == LightLevel::Hdr) out.emplace_back("-hdr");
			if (s.light == LightLevel::Both) out.emplace_back("-both");
			break;
		case CompileTool::Copy:
			break;
		case CompileTool::Cubemaps:
		case CompileTool::Game:
			out = gameWindowArguments(s, launchOptions);
			break;
	}
	append(out, s.extraFor(tool));
	return out;
}

std::vector<CompileCommand> compileCommands(const CompileSettings& s, std::string_view mapBase, std::string_view gameDir, std::string_view launchOptions) {
	std::vector<CompileCommand> steps;
	const std::string base(mapBase), game(gameDir);
	std::string mapName = base;
	if (const auto slash = mapName.find_last_of("/\\"); slash != std::string::npos) mapName = mapName.substr(slash + 1);
	const auto tool = [&](CompileTool t) {
		CompileCommand command;
		command.tool = t;
		command.name = std::string(compileToolName(t));
		command.arguments = {"-game", game};
		const auto args = toolArguments(s, t, launchOptions);
		command.arguments.insert(command.arguments.end(), args.begin(), args.end());
		command.arguments.push_back(base);
		steps.push_back(std::move(command));
	};
	if (s.runs(CompileTool::Vbsp)) tool(CompileTool::Vbsp);
	if (s.runs(CompileTool::Vvis)) tool(CompileTool::Vvis);
	if (s.runs(CompileTool::Vrad)) tool(CompileTool::Vrad);
	if (s.runs(CompileTool::Copy)) {
		steps.push_back({CompileTool::Copy, "Copy", {base + ".bsp", game + "/maps/" + mapName + ".bsp"}, false});
	}
	if (s.runs(CompileTool::Cubemaps)) {
		// The game loads the copied map, builds the cubemaps into it and quits; LDR and HDR
		// each need a run of their own.
		std::vector<LightLevel> levels;
		if (s.cubemapLevel != LightLevel::Hdr) levels.push_back(LightLevel::Ldr);
		if (s.cubemapLevel != LightLevel::Ldr) levels.push_back(LightLevel::Hdr);
		for (const LightLevel level : levels) {
			CompileCommand command;
			command.tool = CompileTool::Cubemaps;
			command.name = levels.size() > 1 ? (level == LightLevel::Hdr ? "Cubemaps (HDR)" : "Cubemaps (LDR)") : "Cubemaps";
			command.arguments = toolArguments(s, CompileTool::Cubemaps, launchOptions);
			command.arguments.insert(command.arguments.end(),
			                         {"-game", game, "+mat_hdr_level", level == LightLevel::Hdr ? "2" : "0", "+map", mapName, "-buildcubemaps"});
			steps.push_back(std::move(command));
		}
	}
	if (s.runs(CompileTool::Game)) {
		CompileCommand command;
		command.tool = CompileTool::Game;
		command.name = "Game";
		command.arguments = toolArguments(s, CompileTool::Game, launchOptions);
		command.arguments.insert(command.arguments.end(), {"-game", game, "+map", mapName});
		command.detached = true;
		steps.push_back(std::move(command));
	}
	return steps;
}

std::vector<std::string> splitArguments(std::string_view text) {
	std::vector<std::string> out;
	std::string current;
	bool quoted = false, any = false;
	for (const char c : text) {
		if (c == '"') {
			quoted = !quoted;
			any = true;
		} else if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && !quoted) {
			if (any) out.push_back(std::move(current));
			current.clear();
			any = false;
		} else {
			current.push_back(c);
			any = true;
		}
	}
	if (any) out.push_back(std::move(current));
	return out;
}

std::string joinArguments(const std::vector<std::string>& arguments) {
	std::string out;
	for (const auto& argument : arguments) {
		if (!out.empty()) out.push_back(' ');
		if (argument.empty() || argument.find(' ') != std::string::npos) {
			out.append("\"").append(argument).append("\"");
		} else {
			out.append(argument);
		}
	}
	return out;
}

} // namespace sme
