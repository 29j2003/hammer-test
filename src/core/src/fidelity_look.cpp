// fidelity_look.cpp - Look values of mapbase-fidelity per map (see fidelity_look.h).

#include <sme/fidelity_look.h>

#include <sme/text.h>

#include <algorithm>

namespace sme {

namespace {

// Delay of the commands after the map is loaded: the player (who receives them) exists by then.
constexpr double COMMAND_DELAY = 0.1;

Entity* findNamed(MapDocument& doc, std::string_view classname, std::string_view name) {
	for (auto& entity : doc.entities) {
		if (iequals(entity.classname(), classname) && iequals(entity.get("targetname"), name)) return &entity;
	}
	return nullptr;
}

const Entity* findNamed(const MapDocument& doc, std::string_view classname, std::string_view name) {
	return findNamed(const_cast<MapDocument&>(doc), classname, name);
}

} // namespace

const std::vector<LookSetting>& lookSettings() {
	static const std::vector<LookSetting> settings = {
		{"r_fidelity_sky_clouds", "Clouds", "Clouds", "How visible the drifting clouds in the skybox are, 0 = none.", 0, 1, 0.85, 2},
		{"r_fidelity_sky_clouds_coverage", "Clouds", "Coverage", "How much of the sky they cover: 0.3 = a lot .. 0.7 = a few wisps.", 0, 1, 0.38, 2},
		{"r_fidelity_sky_clouds_darkness", "Clouds", "Darkness", "How dark their thick parts are, 0 = as bright as the sky.", 0, 1, 0.5, 2},
		{"r_fidelity_sky_clouds_scale", "Clouds", "Pattern size", "Bigger = smaller clouds.", 0.1, 4, 1, 2},
		{"r_fidelity_sky_clouds_speed", "Clouds", "Speed", "How fast they drift, times the wind.", 0, 4, 1, 2},
		{"r_fidelity_godrays_intensity", "Light shafts", "Brightness", "Brightness of the light shafts from the sun.", 0, 2, 0.8, 2},
		{"r_fidelity_godrays_length", "Light shafts", "Length", "How far the shafts reach towards the sun (share of the distance).", 0, 1, 0.85, 2},
		{"r_fidelity_godrays_decay", "Light shafts", "Fade along", "How quickly a shaft fades further along it.", 0.5, 1, 0.94, 3},
		{"r_fidelity_godrays_threshold", "Light shafts", "Sky brightness needed", "Sky brightness from which the sky casts shafts.", 0, 1, 0.55, 2},
		{"r_fidelity_godrays_radius", "Light shafts", "Glow size", "Size of the glowing area around the sun, in screen heights.", 0, 0.5, 0.1, 2},
		{"r_fidelity_lensflare", "Light shafts", "Lens flare", "Faint lens flare from the sun, 0 = none.", 0, 1, 0.1, 2},
		{"r_fidelity_exposure", "Image", "Exposure", "Brightness of the whole picture.", 0.25, 3, 1, 2},
		{"r_fidelity_contrast", "Image", "Contrast", "Soft S-curve contrast.", 0, 1, 0.18, 2},
		{"r_fidelity_saturation", "Image", "Saturation", "1 = unchanged, 0 = grey.", 0, 2, 1, 2},
		{"r_fidelity_vibrance", "Image", "Vibrance", "Extra saturation for dull colours.", 0, 1, 0.2, 2},
		{"r_fidelity_temperature", "Image", "Temperature", "-1 = cold, blue .. 1 = warm, orange.", -1, 1, 0, 2},
		{"r_fidelity_tint", "Image", "Tint", "-1 = magenta .. 1 = green.", -1, 1, 0, 2},
		{"r_fidelity_lift", "Image", "Lift shadows", "Lifts the darkest parts.", 0, 0.2, 0, 3},
		{"r_fidelity_gamma", "Image", "Mid-tones", "Above 1 brightens the mid-tones.", 0.5, 2, 1, 2},
		{"r_fidelity_gain", "Image", "Highlights", "Gain of the bright parts.", 0.5, 2, 1, 2},
		{"r_fidelity_clarity", "Image", "Clarity", "Local contrast: brings out relief and texture detail.", 0, 1, 0.35, 2},
		{"r_fidelity_vignette", "Image", "Vignette", "Darker corners.", 0, 1, 0.18, 2},
		{"r_fidelity_water_glint", "Water", "Sun glint", "How brightly the sun glints on the waves (Water_Fidelity).", 0, 6, 3, 2},
		{"r_fidelity_water_foam", "Water", "Shore foam", "Foam where the water is shallow at the shore.", 0, 2, 1, 2},
	};
	return settings;
}

const LookSetting* findLookSetting(std::string_view cvar) {
	for (const auto& setting : lookSettings()) {
		if (iequals(setting.cvar, cvar)) return &setting;
	}
	return nullptr;
}

std::map<std::string, std::string> parseCfgValues(std::string_view text) {
	std::map<std::string, std::string> values;
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		std::string line(text.substr(start, end - start));
		start = end + 1;
		if (const auto comment = line.find("//"); comment != std::string::npos) line.erase(comment);
		line = std::string(trim(line));
		const auto space = line.find_first_of(" \t");
		if (line.empty() || space == std::string::npos) continue;
		std::string value(trim(line.substr(space + 1)));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
		values[toLower(line.substr(0, space))] = value;
	}
	return values;
}

std::map<std::string, std::string> mapLookValues(const MapDocument& doc) {
	std::map<std::string, std::string> values;
	const Entity* relay = findNamed(doc, "logic_auto", LOOK_RELAY_NAME);
	if (!relay) return values;
	for (const auto& connection : relay->connections) {
		if (!iequals(connection.target, LOOK_COMMAND_NAME) || !iequals(connection.input, "Command")) continue;
		const std::string command(trim(connection.parameter));
		const auto space = command.find(' ');
		if (space == std::string::npos) continue;
		values[toLower(command.substr(0, space))] = std::string(trim(command.substr(space + 1)));
	}
	return values;
}

Change setMapLookValues(MapDocument& doc, const std::map<std::string, std::string>& values, const Vec3& place) {
	Transaction t(doc, "Map look");
	Entity* relay = findNamed(doc, "logic_auto", LOOK_RELAY_NAME);
	Entity* command = findNamed(doc, "point_clientcommand", LOOK_COMMAND_NAME);
	if (values.empty()) {
		for (Entity* entity : {relay, command}) {
			if (entity) {
				t.touchEntity(entity->id);
				const int id = entity->id;
				std::erase_if(doc.entities, [id](const Entity& e) { return e.id == id; });
			}
		}
		return t.finish();
	}
	if (!command) {
		Entity entity;
		entity.id = doc.newEntityId();
		entity.set("classname", "point_clientcommand");
		entity.set("targetname", std::string(LOOK_COMMAND_NAME));
		entity.setOrigin(place + Vec3{16, 0, 0});
		t.touchEntity(entity.id);
		doc.entities.push_back(std::move(entity));
	}
	relay = findNamed(doc, "logic_auto", LOOK_RELAY_NAME);   // the vector may have moved
	if (!relay) {
		Entity entity;
		entity.id = doc.newEntityId();
		entity.set("classname", "logic_auto");
		entity.set("targetname", std::string(LOOK_RELAY_NAME));
		entity.set("spawnflags", "0");   // fires on every load (also of a save), not just once
		entity.setOrigin(place);
		t.touchEntity(entity.id);
		doc.entities.push_back(std::move(entity));
		relay = &doc.entities.back();
	} else {
		t.touchEntity(relay->id);
	}
	// Our commands are replaced; other outputs of the relay stay.
	std::erase_if(relay->connections, [](const Connection& c) { return iequals(c.target, LOOK_COMMAND_NAME); });
	for (const auto& [cvar, value] : values) {
		Connection connection;
		connection.output = "OnMapSpawn";
		connection.target = std::string(LOOK_COMMAND_NAME);
		connection.input = "Command";
		connection.parameter = cvar + " " + value;
		connection.delay = COMMAND_DELAY;
		relay->connections.push_back(std::move(connection));
	}
	return t.finish();
}

} // namespace sme
