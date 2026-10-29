// mod_features.cpp - The add-on's features as one checklist (see mod_features.h).

#include <sme/mod_features.h>

#include <sme/text.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace sme {

namespace fs = std::filesystem;

namespace {

std::optional<std::string> readFile(const fs::path& file) {
	std::ifstream stream(file, std::ios::binary);
	if (!stream) return std::nullopt;
	std::ostringstream text;
	text << stream.rdbuf();
	return text.str();
}

bool writeFile(const fs::path& file, std::string_view text) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	return bool(out);
}

std::vector<std::string_view> lines(std::string_view text) {
	std::vector<std::string_view> out;
	std::size_t start = 0;
	while (start <= text.size()) {
		const auto end = text.find('\n', start);
		std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		out.push_back(line);
		if (end == std::string_view::npos) break;
		start = end + 1;
	}
	return out;
}

/// The line without a trailing // comment, trimmed.
std::string_view code(std::string_view line) {
	if (const auto comment = line.find("//"); comment != std::string_view::npos) line = line.substr(0, comment);
	return trim(line);
}

/// First word and the rest (quotes taken off the rest).
std::pair<std::string_view, std::string_view> command(std::string_view line) {
	line = code(line);
	const auto space = line.find_first_of(" \t");
	if (space == std::string_view::npos) return {line, {}};
	std::string_view rest = trim(line.substr(space + 1));
	if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') rest = rest.substr(1, rest.size() - 2);
	return {line.substr(0, space), rest};
}

bool isFeaturesExec(std::string_view line) {
	const auto [word, rest] = command(line);
	return iequals(word, "exec") && (iequals(rest, "mapsmith_features") || iequals(rest, "mapsmith_features.cfg"));
}

std::string firstLine(const fs::path& file) {
	std::ifstream stream(file, std::ios::binary);
	std::string line;
	std::getline(stream, line);
	if (!line.empty() && line.back() == '\r') line.pop_back();
	// A UTF-8 byte order mark would hide the marker.
	if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
	    static_cast<unsigned char>(line[2]) == 0xBF) {
		line.erase(0, 3);
	}
	return line;
}

/// Forward slashes, no doubled ones, the case kept.
std::string slashes(std::string_view path) {
	std::string out;
	for (const char c : path) {
		const char n = c == '\\' ? '/' : c;
		if (n == '/' && !out.empty() && out.back() == '/') continue;
		out.push_back(n);
	}
	while (out.starts_with("./")) out.erase(0, 2);
	return out;
}

/// A relative path from a manifest that stays inside the mod folder, or empty.
std::string safeRelative(std::string_view path) {
	std::string p = slashes(trim(path));
	if (p.empty() || p.front() == '/' || p.find(':') != std::string::npos) return {};
	for (const auto& part : splitTrimmed(p, '/')) {
		if (part == "..") return {};
	}
	return p;
}

} // namespace

std::string_view featureGroupName(FeatureGroup group) {
	switch (group) {
		case FeatureGroup::Graphics: return "Graphics";
		case FeatureGroup::Effects: return "Effects";
		case FeatureGroup::Sound: return "Sound";
		case FeatureGroup::Materials: return "Materials";
	}
	return {};
}

const std::vector<ModFeature>& modFeatures() {
	using G = FeatureGroup;
	// The on/off switches of the add-on's client code (all FCVAR_ARCHIVE, default 1); quality
	// steps and counts (r_fidelity_ssao_quality, r_fidelity_glow_count ...) are not features.
	static const std::vector<ModFeature> features = {
		{"ssao", G::Graphics, "Ambient occlusion (SSAO)", "Corners, crevices and the ground under things get soft contact shadows.", {"r_fidelity_ssao"}, {}},
		{"godrays", G::Graphics, "Light shafts", "Shafts of sunlight around the sun (maps with env_sun or env_global_light).", {"r_fidelity_godrays"}, {}},
		{"volumetric", G::Graphics, "Light in the air", "The flashlight and lights with shadows show their beam in the air; sun haze and ground mist.", {"r_fidelity_volumetric"}, {}},
		{"sunshadows", G::Graphics, "Better sun shadows", "Steady, soft sun shadows that are sharper near you; the sunlight comes from where env_sun is.",
		 {"r_fidelity_sunshadow", "r_fidelity_sunshadow_cascade", "r_fidelity_sun_follow"}, {}},
		{"projshadow", G::Graphics, "Sharper spotlight shadows", "Shadows of env_projectedtexture use their real resolution.", {"r_fidelity_projshadow"}, {}},
		{"dynlights", G::Graphics, "Per-pixel dynamic lights", "The nearest dynamic lights light up the walls' relief and add a highlight.", {"r_fidelity_dynlights"}, {}},
		{"exposure", G::Graphics, "Calm exposure", "The brightness adapts like an eye and does not pump with every small head movement.", {"r_fidelity_exposure_smooth"}, {}},
		{"cubemaps", G::Graphics, "Reflections that fit the room", "Each env_cubemap's room is measured when the map loads, so reflections sit right.", {"r_fidelity_cubemaps"}, {}},
		{"sky", G::Graphics, "Drifting clouds", "Clouds drift over the skybox with the wind.", {"r_fidelity_sky"}, {}},
		{"wetness", G::Graphics, "Wet in the rain", "In the rain, walls and floors open to the sky get wet and puddles gather.", {"r_fidelity_wetness"}, {}},
		{"fxaa", G::Graphics, "Smooth edges (FXAA)", "Jagged edges are smoothed.", {"r_fidelity_fxaa"}, {}},
		{"grade", G::Graphics, "Final image pass", "A little sharpening, colour and vignette over the whole picture.", {"r_fidelity_grade"}, {}},
		{"glow", G::Effects, "Glowing things light the room", "Chargers, Combine lights, grenade blinks and the like light their surroundings.", {"r_fidelity_glow"}, {}},
		{"muzzlelight", G::Effects, "Muzzle flash and explosion light", "Shots and explosions light the world for a moment, with bump maps and highlights.",
		 {"r_fidelity_muzzlelight", "r_fidelity_flashes"}, {}},
		{"sparklight", G::Effects, "Sparks light metal", "Bullets hitting metal light it up for a moment.", {"r_fidelity_sparklight"}, {}},
		{"impactdust", G::Effects, "Impact dust", "Bullet hits on stone, dirt and wood leave dust hanging in the air.", {"r_fidelity_impactdust"}, {}},
		{"muzzlesmoke", G::Effects, "Muzzle smoke", "The barrel smokes after quick shots.", {"r_fidelity_muzzlesmoke"}, {}},
		{"rainsplash", G::Effects, "Rain splashes", "Rain splashes on floors, roofs and walls, not only on water.", {"r_fidelity_rainsplash"}, {}},
		{"bloodpools", G::Effects, "Blood pools", "The dead bleed a pool onto the floor beneath them.", {"r_fidelity_bloodpools"}, {}},
		{"splatter", G::Effects, "Bug bait splatter", "Bug bait bursts in a big splat with flying drops.", {"r_fidelity_splatter"}, {}},
		{"deadeyes", G::Effects, "Eyes go dark", "Glowing eyes of Combine soldiers and others go dark when they die.", {"r_fidelity_dead_eyes"}, {}},
		{"weaponsway", G::Effects, "Weapon sway", "The weapon in the hand swings behind turns, leans when strafing and dips when landing.", {"r_fidelity_weapon_sway"}, {}},
		{"weaponwall", G::Effects, "Weapon pulls back at walls", "The weapon in the hand pulls back a little near walls.", {"r_fidelity_weapon_wall"}, {}},
		{"npcblend", G::Effects, "Smoother NPC animations", "NPCs blend between their body animations (flinch, aim, run) instead of snapping.", {"cl_fidelity_npc_blend"}, {}},
		{"mixer", G::Sound, "Sound mixer", "Volumes per kind of sound; music and voices duck under loud shots.", {"snd_fidelity"}, {}},
		{"headroom", G::Sound, "Headroom", "The mix is compressed when many loud sounds overlap, so it does not distort.", {"snd_fidelity_headroom"}, {}},
		{"occlusion", G::Sound, "Sounds behind walls", "Sounds behind walls are quieter.", {"snd_fidelity_occlusion"}, {}},
		{"reverb", G::Sound, "Room reverb", "Rooms get a fitting reverb where the soundscape sets none.", {"snd_fidelity_reverb"}, {}},
		{"shock", G::Sound, "Shell shock", "Explosions nearby numb the ears for a moment; death fades the world out.", {"snd_fidelity_shock"}, {}},
		{"distant", G::Sound, "Distant shots", "Shots and explosions far away echo, delayed as sound is.", {"snd_fidelity_distant"}, {}},
		{"liquid", G::Materials, "Liquid in bottles", "Bottles, jars and vials hold liquid that sloshes and pours out.", {}, GameEffect::Liquid},
		{"wind", G::Materials, "Swaying plants", "Leaves, bushes and plants move in the wind; a light breeze in maps without env_wind.",
		 {"r_fidelity_wind"}, GameEffect::Wind},
		{"water", G::Materials, "Water with sun glint and foam", "The water reflects the sun and gets foam where it meets the shore.", {}, GameEffect::Water},
		{"relief", G::Materials, "Walls with depth", "Bricks, stones and tiles stand out of walls and floors as you walk past.",
		 {"r_fidelity_parallax"}, GameEffect::Relief},
	};
	return features;
}

const ModFeature* findModFeature(std::string_view id) {
	for (const auto& feature : modFeatures()) {
		if (id == feature.id) return &feature;
	}
	return nullptr;
}

fs::path featuresCfgFile(const fs::path& modDir) {
	return modDir / "cfg" / "mapsmith_features.cfg";
}

std::string writeFeaturesCfg(const std::set<std::string>& off) {
	std::string out(FEATURES_MARKER);
	out += " (Project > Features in Mapsmith)\n"
	       "// Run from autoexec.cfg when the game starts. Mapsmith writes this file again whenever a\n"
	       "// feature is ticked or unticked: change them there. 1 = on, 0 = off.\n";
	for (const auto& feature : modFeatures()) {
		if (feature.convars.empty()) continue;
		out += "\n// ";
		out += feature.title;
		out += '\n';
		for (const char* convar : feature.convars) {
			out += convar;
			out += off.count(feature.id) ? " 0\n" : " 1\n";
		}
	}
	return out;
}

std::set<std::string> parseFeaturesCfg(std::string_view text) {
	std::map<std::string, std::string> values;
	for (const auto line : lines(text)) {
		const auto [name, value] = command(line);
		if (!name.empty() && !value.empty()) values[toLower(name)] = std::string(value);
	}
	std::set<std::string> off;
	for (const auto& feature : modFeatures()) {
		for (const char* convar : feature.convars) {
			const auto it = values.find(toLower(convar));
			if (it != values.end() && trim(it->second) == "0") off.insert(feature.id);
		}
	}
	return off;
}

std::optional<std::set<std::string>> loadFeatureChoice(const fs::path& modDir) {
	const auto text = readFile(featuresCfgFile(modDir));
	if (!text) return std::nullopt;
	return parseFeaturesCfg(*text);
}

EnhancementSettings withFeatureEffects(EnhancementSettings settings, const std::set<std::string>& off) {
	for (const auto& feature : modFeatures()) {
		if (!feature.effect) continue;
		if (off.count(feature.id)) settings.off.insert(*feature.effect);
		else settings.off.erase(*feature.effect);
	}
	return settings;
}

std::set<std::string> featuresOffBySettings(const EnhancementSettings& settings) {
	std::set<std::string> off;
	for (const auto& feature : modFeatures()) {
		if (feature.effect && !settings.enabled(*feature.effect)) off.insert(feature.id);
	}
	return off;
}

bool runsFeatures(std::string_view autoexec) {
	const auto all = lines(autoexec);
	return std::any_of(all.begin(), all.end(), isFeaturesExec);
}

std::string withFeaturesLine(std::string_view autoexec) {
	if (runsFeatures(autoexec)) return std::string(autoexec);
	const std::string_view newline = autoexec.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
	std::string out(autoexec);
	if (!out.empty() && out.back() != '\n') out += newline;
	out += FEATURES_EXEC_LINE;
	out += newline;
	return out;
}

std::string withoutFeaturesLine(std::string_view autoexec) {
	std::string out;
	std::size_t start = 0;
	while (start < autoexec.size()) {
		auto end = autoexec.find('\n', start);
		end = end == std::string_view::npos ? autoexec.size() : end + 1;
		const std::string_view line = autoexec.substr(start, end - start);
		if (!isFeaturesExec(line)) out += line;
		start = end;
	}
	return out;
}

bool saveFeatureChoice(const fs::path& modDir, const std::set<std::string>& off, bool hook) {
	if (!writeFile(featuresCfgFile(modDir), writeFeaturesCfg(off))) return false;
	if (!hook) return true;
	const auto autoexec = modDir / "cfg" / "autoexec.cfg";
	const std::string text = readFile(autoexec).value_or(std::string());
	return runsFeatures(text) || writeFile(autoexec, withFeaturesLine(text));
}

bool hookFeatureChoice(const fs::path& modDir) {
	std::error_code ec;
	if (!fs::exists(featuresCfgFile(modDir), ec)) return false;
	const auto autoexec = modDir / "cfg" / "autoexec.cfg";
	const std::string text = readFile(autoexec).value_or(std::string());
	if (runsFeatures(text)) return false;
	return writeFile(autoexec, withFeaturesLine(text));
}

std::vector<GeneratedFile> generatedModFiles(const fs::path& modDir) {
	std::map<std::string, GeneratedFile> found;   // by lower-case path: each file once
	std::error_code ec;
	const auto add = [&](const fs::path& file, std::string what, bool byEditor) {
		const std::string relative = slashes(fs::relative(file, modDir, ec).generic_string());
		if (ec || relative.empty() || relative.starts_with("..")) return;
		found.emplace(toLower(relative), GeneratedFile{fs::path(relative), std::move(what), byEditor});
	};
	const auto isFile = [&](const fs::path& file) { return fs::is_regular_file(file, ec); };

	// Materials with a marker in their first line.
	const auto materials = modDir / "materials";
	if (fs::is_directory(materials, ec)) {
		for (fs::recursive_directory_iterator it(materials, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
		     it.increment(ec)) {
			if (!it->is_regular_file(ec) || !endsWithI(it->path().filename().string(), ".vmt")) continue;
			const std::string first = firstLine(it->path());
			if (first.starts_with(DEPLOY_MARKER)) add(it->path(), "material made by deploy.bat", false);
			else if (first.starts_with(MAPSMITH_MARKER)) add(it->path(), "material made by Mapsmith (Game enhancements)", true);
		}
		ec.clear();
	}
	// Copies deploy.ps1 lists in its manifests (models, steeper bump maps, height maps, sounds).
	const std::pair<const char*, const char*> manifests[] = {
		{"fidelity_models.txt", "model copy with liquid (deploy.bat or Game enhancements)"},
		{"fidelity_bumps.txt", "bump map made by deploy.bat"},
		{"fidelity_heights.txt", "height map made by deploy.bat"},
		{"fidelity_distant.txt", "distant sound made by deploy.bat"},
	};
	for (const auto& [name, what] : manifests) {
		const auto manifest = modDir / name;
		const auto text = readFile(manifest);
		if (!text) continue;
		add(manifest, "list of what deploy.bat made", false);
		for (const auto line : lines(*text)) {
			if (trim(line).empty() || trim(line).starts_with("//")) continue;
			const std::string relative = safeRelative(line.substr(0, line.find('\t')));
			if (!relative.empty() && isFile(modDir / toNativePath(relative))) add(modDir / toNativePath(relative), what, false);
		}
	}
	// The add-on's own files.
	const auto shaders = modDir / "shaders" / "fxc";
	if (fs::is_directory(shaders, ec)) {
		for (fs::directory_iterator it(shaders, ec), end; !ec && it != end; it.increment(ec)) {
			const std::string name = it->path().filename().string();
			if (startsWithI(name, "fidelity_") && endsWithI(name, ".vcs")) add(it->path(), "shader of mapbase-fidelity", false);
		}
		ec.clear();
	}
	for (const char* dll : {"game_shader_dx6.dll", "game_shader_dx9_fidelity.dll"}) {
		if (isFile(modDir / "bin" / dll)) add(modDir / "bin" / dll, "shader DLL of mapbase-fidelity", false);
	}
	if (isFile(modDir / "cfg" / "fidelity.cfg")) add(modDir / "cfg" / "fidelity.cfg", "settings of mapbase-fidelity", false);
	if (const auto lights = modDir / "fidelity_lights.rad"; isFile(lights) && (firstLine(lights).starts_with("// mapbase-fidelity: generated") ||
	                                                                           firstLine(lights) == "noshadow mapbase_fidelity_generated_by_deploy_ps1")) {
		add(lights, "light list made by deploy.bat", false);
	}
	const auto previews = modDir / "fidelity_preview";
	if (fs::is_directory(previews, ec)) {
		for (fs::directory_iterator it(previews, ec), end; !ec && it != end; it.increment(ec)) {
			const std::string name = it->path().filename().string();
			if (endsWithI(name, ".bmp") || endsWithI(name, ".vtf")) add(it->path(), "preview made by deploy.bat", false);
		}
	}
	std::vector<GeneratedFile> out;
	out.reserve(found.size());
	for (auto& [key, file] : found) out.push_back(std::move(file));
	return out;
}

int removeEditorFiles(const fs::path& modDir, const std::vector<GeneratedFile>& files) {
	int removed = 0;
	for (const auto& file : files) {
		if (!file.byEditor) continue;
		const auto path = modDir / file.path;
		// Only what still carries the editor's marker: a file the mapper took over stays.
		if (!firstLine(path).starts_with(MAPSMITH_MARKER)) continue;
		std::error_code ec;
		if (fs::remove(path, ec)) ++removed;
	}
	return removed;
}

} // namespace sme
