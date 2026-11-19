// particle_edit.cpp - Particle systems as editable values, templates, saving into the mod (see particle_edit.h).

#include <sme/particle_edit.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <sstream>

namespace sme {

namespace fs = std::filesystem;

namespace {

std::string number(double v) {
	std::ostringstream out;
	out.precision(6);
	out << (std::abs(v) < 1e-9 ? 0.0 : v);
	return out.str();
}

float f(double v) {
	return std::isfinite(v) ? static_cast<float>(v) : 0.f;
}

int i(double v) {
	return std::isfinite(v) ? static_cast<int>(std::clamp(std::llround(v), -2147483647LL, 2147483647LL)) : 0;
}

std::uint8_t byte255(double v) {
	return static_cast<std::uint8_t>(std::clamp(std::lround(std::isfinite(v) ? v : 0.0), 0L, 255L));
}

PcfFunction convert(const ParticleFunction& function, std::string_view group, std::vector<std::string>* dropped) {
	PcfFunction out;
	out.functionName = function.name;
	for (const auto& value : function.values) {
		if (auto v = toPcfValue(value)) {
			out.values.emplace_back(value.key, std::move(*v));
		} else if (dropped) {
			dropped->push_back(std::string(group) + "/" + function.name + "." + value.key);
		}
	}
	return out;
}

std::vector<double> numbersIn(std::string_view text) {
	std::vector<double> out;
	std::string token;
	const auto flush = [&]() -> bool {
		if (token.empty()) return true;
		try {
			std::size_t used = 0;
			const double v = std::stod(token, &used);
			if (used != token.size() || !std::isfinite(v)) return false;
			out.push_back(v);
		} catch (const std::exception&) {
			return false;
		}
		token.clear();
		return true;
	};
	for (const char c : text) {
		if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == '[' || c == ']' || c == '{' || c == '}') {
			if (!flush()) return {std::nan("")};
		} else {
			token += c;
		}
	}
	if (!flush()) return {std::nan("")};
	return out;
}

PcfFunction fn(std::string name, PcfValues values = {}) {
	return {std::move(name), std::move(values)};
}

} // namespace

std::optional<PcfValue> toPcfValue(const ParticleValue& value) {
	const auto& n = value.numbers;
	const auto num = [&](std::size_t k) { return k < n.size() ? n[k] : 0.0; };
	switch (value.type) {
	case ParticleValueType::Int: return PcfValue(i(num(0)));
	case ParticleValueType::Float: return PcfValue(f(num(0)));
	case ParticleValueType::Bool: return PcfValue(num(0) != 0);
	case ParticleValueType::String: return PcfValue(value.text);
	case ParticleValueType::Color: return PcfValue(PcfColor{byte255(num(0)), byte255(num(1)), byte255(num(2)), byte255(n.size() > 3 ? num(3) : 255)});
	case ParticleValueType::Vector2: return PcfValue(PcfVector2{f(num(0)), f(num(1))});
	case ParticleValueType::Vector3: return PcfValue(Vec3{num(0), num(1), num(2)});
	case ParticleValueType::Vector4: return PcfValue(PcfVector4{f(num(0)), f(num(1)), f(num(2)), f(num(3))});
	case ParticleValueType::Angles: return PcfValue(PcfAngles{f(num(0)), f(num(1)), f(num(2))});
	case ParticleValueType::IntArray: {
		std::vector<int> out;
		for (const double v : n) out.push_back(i(v));
		return PcfValue(std::move(out));
	}
	case ParticleValueType::FloatArray: {
		std::vector<float> out;
		for (const double v : n) out.push_back(f(v));
		return PcfValue(std::move(out));
	}
	case ParticleValueType::Element:
	case ParticleValueType::Other: break;
	}
	return std::nullopt;
}

PcfSystem toPcfSystem(const ParticleDefinition& definition, std::vector<std::string>* dropped) {
	PcfSystem out;
	out.name = definition.name;
	for (const auto& value : definition.properties.values) {
		if (auto v = toPcfValue(value)) {
			out.properties.emplace_back(value.key, std::move(*v));
		} else if (dropped) {
			dropped->push_back("properties." + value.key);
		}
	}
	const auto list = [&](const std::vector<ParticleFunction>& from, std::vector<PcfFunction>& to, std::string_view group) {
		for (const auto& function : from) to.push_back(convert(function, group, dropped));
	};
	list(definition.emitters, out.emitters, "emitters");
	list(definition.initializers, out.initializers, "initializers");
	list(definition.operators, out.operators, "operators");
	list(definition.renderers, out.renderers, "renderers");
	list(definition.forces, out.forces, "forces");
	list(definition.constraints, out.constraints, "constraints");
	out.children = definition.children;
	return out;
}

std::string pcfValueText(const PcfValue& value) {
	return std::visit(
		[](const auto& v) -> std::string {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, int>) return std::to_string(v);
			else if constexpr (std::is_same_v<T, float>) return number(v);
			else if constexpr (std::is_same_v<T, bool>) return v ? "1" : "0";
			else if constexpr (std::is_same_v<T, std::string>) return v;
			else if constexpr (std::is_same_v<T, PcfColor>) return std::to_string(v.r) + " " + std::to_string(v.g) + " " + std::to_string(v.b) + " " + std::to_string(v.a);
			else if constexpr (std::is_same_v<T, Vec3>) return number(v.x) + " " + number(v.y) + " " + number(v.z);
			else if constexpr (std::is_same_v<T, PcfVector2>) return number(v.x) + " " + number(v.y);
			else if constexpr (std::is_same_v<T, PcfVector4>) return number(v.x) + " " + number(v.y) + " " + number(v.z) + " " + number(v.w);
			else if constexpr (std::is_same_v<T, PcfAngles>) return number(v.pitch) + " " + number(v.yaw) + " " + number(v.roll);
			else {
				std::string out;
				for (const auto x : v) out += (out.empty() ? "" : " ") + number(x);
				return out;
			}
		},
		value);
}

std::optional<PcfValue> pcfValueFromText(std::string_view text, const PcfValue& like) {
	if (std::holds_alternative<std::string>(like)) {
		std::string s;
		for (const char c : text) {
			if (static_cast<unsigned char>(c) >= 32 && c != 127) s += c;
		}
		return PcfValue(s);
	}
	if (std::holds_alternative<bool>(like)) {
		const auto t = toLower(trim(text));
		if (t == "true" || t == "yes" || t == "on") return PcfValue(true);
		if (t == "false" || t == "no" || t == "off") return PcfValue(false);
	}
	const auto n = numbersIn(text);
	if (!n.empty() && std::isnan(n.front())) return std::nullopt;
	return std::visit(
		[&](const auto& v) -> std::optional<PcfValue> {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, int>) {
				if (n.size() != 1) return std::nullopt;
				return PcfValue(i(n[0]));
			} else if constexpr (std::is_same_v<T, float>) {
				if (n.size() != 1) return std::nullopt;
				return PcfValue(f(n[0]));
			} else if constexpr (std::is_same_v<T, bool>) {
				if (n.size() != 1) return std::nullopt;
				return PcfValue(n[0] != 0);
			} else if constexpr (std::is_same_v<T, PcfColor>) {
				if (n.size() != 3 && n.size() != 4) return std::nullopt;
				return PcfValue(PcfColor{byte255(n[0]), byte255(n[1]), byte255(n[2]), n.size() == 4 ? byte255(n[3]) : std::uint8_t(255)});
			} else if constexpr (std::is_same_v<T, Vec3>) {
				if (n.size() != 3) return std::nullopt;
				return PcfValue(Vec3{n[0], n[1], n[2]});
			} else if constexpr (std::is_same_v<T, PcfVector2>) {
				if (n.size() != 2) return std::nullopt;
				return PcfValue(PcfVector2{f(n[0]), f(n[1])});
			} else if constexpr (std::is_same_v<T, PcfVector4>) {
				if (n.size() != 4) return std::nullopt;
				return PcfValue(PcfVector4{f(n[0]), f(n[1]), f(n[2]), f(n[3])});
			} else if constexpr (std::is_same_v<T, PcfAngles>) {
				if (n.size() != 3) return std::nullopt;
				return PcfValue(PcfAngles{f(n[0]), f(n[1]), f(n[2])});
			} else if constexpr (std::is_same_v<T, std::vector<int>>) {
				std::vector<int> out;
				for (const double x : n) out.push_back(i(x));
				return PcfValue(std::move(out));
			} else if constexpr (std::is_same_v<T, std::vector<float>>) {
				std::vector<float> out;
				for (const double x : n) out.push_back(f(x));
				return PcfValue(std::move(out));
			} else {
				return std::nullopt;
			}
		},
		like);
}

std::vector<ParticleDefinition> roundTrip(const std::vector<PcfSystem>& systems) {
	return readParticleDefinitions(writeParticleFile(systems));
}

std::vector<ParticleTemplate> particleTemplates() {
	return {ParticleTemplate::Glow, ParticleTemplate::Sparks, ParticleTemplate::MuzzleFlash, ParticleTemplate::Smoke, ParticleTemplate::Embers};
}

std::string_view particleTemplateLabel(ParticleTemplate kind) {
	switch (kind) {
	case ParticleTemplate::Sparks: return "Sparks";
	case ParticleTemplate::Glow: return "Glow (a soft light sprite)";
	case ParticleTemplate::Smoke: return "Smoke";
	case ParticleTemplate::MuzzleFlash: return "Muzzle flash";
	case ParticleTemplate::Embers: return "Embers";
	}
	return "Glow";
}

PcfSystem particleFromTemplate(ParticleTemplate kind, std::string_view name) {
	PcfSystem s;
	s.name = particleSystemName(name).empty() ? "mapsmith_effect" : particleSystemName(name);
	const auto sphere = [](float distMax, float speedMin, float speedMax, Vec3 localMin, Vec3 localMax) {
		return fn("Position Within Sphere Random", {{"distance_min", 0.0f},
		                                            {"distance_max", distMax},
		                                            {"speed_min", speedMin},
		                                            {"speed_max", speedMax},
		                                            {"speed_in_local_coordinate_system_min", localMin},
		                                            {"speed_in_local_coordinate_system_max", localMax}});
	};
	switch (kind) {
	case ParticleTemplate::Glow:
		// One soft sprite that stays while the system runs (an emissive "light" on a weapon):
		// sprites/light_glow02_add is additive in every HL2-based game.
		s.properties = {{"material", std::string("sprites/light_glow02_add.vmt")},
		                {"max_particles", 4},
		                {"initial_particles", 0},
		                {"radius", 6.0f},
		                {"color", PcfColor{120, 200, 255, 255}}};
		s.emitters = {fn("emit_instantaneously", {{"emission_start_time", 0.0f}, {"num_to_emit", 1}})};
		s.initializers = {fn("Position Within Sphere Random", {{"distance_min", 0.0f}, {"distance_max", 0.0f}}),
		                  fn("Lifetime Random", {{"lifetime_min", 3600.0f}, {"lifetime_max", 3600.0f}}),
		                  fn("Radius Random", {{"radius_min", 5.0f}, {"radius_max", 6.0f}}),
		                  fn("Alpha Random", {{"alpha_min", 200}, {"alpha_max", 230}})};
		s.operators = {fn("Alpha Fade In Random", {{"fade_in_time_min", 0.05f}, {"fade_in_time_max", 0.1f}}), fn("Lifespan Decay")};
		s.renderers = {fn("render_animated_sprites", {{"animation rate", 1.0f}})};
		break;
	case ParticleTemplate::Sparks:
		s.properties = {{"material", std::string("effects/spark.vmt")},
		                {"max_particles", 60},
		                {"initial_particles", 0},
		                {"radius", 1.5f},
		                {"color", PcfColor{255, 210, 140, 255}}};
		s.emitters = {fn("emit_instantaneously", {{"emission_start_time", 0.0f}, {"num_to_emit", 24}})};
		s.initializers = {sphere(1.0f, 80.0f, 220.0f, Vec3{-40, -40, 40}, Vec3{40, 40, 140}),
		                  fn("Lifetime Random", {{"lifetime_min", 0.3f}, {"lifetime_max", 0.8f}}),
		                  fn("Color Random", {{"color1", PcfColor{255, 230, 160, 255}}, {"color2", PcfColor{255, 140, 50, 255}}}),
		                  fn("Radius Random", {{"radius_min", 0.6f}, {"radius_max", 1.4f}})};
		s.operators = {fn("Movement Basic", {{"gravity", Vec3{0, 0, -600}}, {"drag", 0.03f}}),
		               fn("Alpha Fade Out Random", {{"fade_out_time_min", 0.3f}, {"fade_out_time_max", 0.6f}}), fn("Lifespan Decay")};
		s.renderers = {fn("render_sprite_trail", {{"min length", 2.0f}, {"max length", 10.0f}, {"length fade in time", 0.0f}})};
		break;
	case ParticleTemplate::MuzzleFlash:
		s.properties = {{"material", std::string("effects/muzzleflash1.vmt")},
		                {"max_particles", 6},
		                {"initial_particles", 0},
		                {"radius", 6.0f},
		                {"color", PcfColor{255, 220, 170, 255}}};
		s.emitters = {fn("emit_instantaneously", {{"emission_start_time", 0.0f}, {"num_to_emit", 3}})};
		s.initializers = {sphere(0.5f, 0.0f, 0.0f, Vec3{20, 0, 0}, Vec3{60, 0, 0}),
		                  fn("Lifetime Random", {{"lifetime_min", 0.05f}, {"lifetime_max", 0.08f}}),
		                  fn("Radius Random", {{"radius_min", 4.0f}, {"radius_max", 8.0f}}),
		                  fn("Rotation Random", {{"rotation_initial", 0.0f}, {"rotation_offset_min", 0.0f}, {"rotation_offset_max", 360.0f}})};
		s.operators = {fn("Movement Basic", {{"drag", 0.2f}}), fn("Radius Scale", {{"radius_start_scale", 1.0f}, {"radius_end_scale", 1.6f}}),
		               fn("Alpha Fade Out Random", {{"fade_out_time_min", 0.5f}, {"fade_out_time_max", 0.8f}}), fn("Lifespan Decay")};
		s.renderers = {fn("render_animated_sprites", {{"animation rate", 1.0f}})};
		break;
	case ParticleTemplate::Smoke:
		s.properties = {{"material", std::string("particle/particle_smokegrenade.vmt")},
		                {"max_particles", 40},
		                {"initial_particles", 0},
		                {"radius", 8.0f},
		                {"color", PcfColor{170, 170, 170, 255}}};
		s.emitters = {fn("emit_continuously", {{"emission_rate", 8.0f}, {"emission_duration", 0.0f}, {"emission_start_time", 0.0f}})};
		s.initializers = {sphere(2.0f, 0.0f, 4.0f, Vec3{-3, -3, 12}, Vec3{3, 3, 24}),
		                  fn("Lifetime Random", {{"lifetime_min", 1.5f}, {"lifetime_max", 2.5f}}),
		                  fn("Radius Random", {{"radius_min", 4.0f}, {"radius_max", 7.0f}}),
		                  fn("Rotation Random", {{"rotation_initial", 0.0f}, {"rotation_offset_min", 0.0f}, {"rotation_offset_max", 360.0f}}),
		                  fn("Alpha Random", {{"alpha_min", 40}, {"alpha_max", 90}})};
		s.operators = {fn("Movement Basic", {{"gravity", Vec3{0, 0, 10}}, {"drag", 0.05f}}),
		               fn("Radius Scale", {{"radius_start_scale", 0.7f}, {"radius_end_scale", 2.0f}}),
		               fn("Alpha Fade In Random", {{"fade_in_time_min", 0.1f}, {"fade_in_time_max", 0.2f}}),
		               fn("Alpha Fade Out Random", {{"fade_out_time_min", 0.4f}, {"fade_out_time_max", 0.7f}}), fn("Lifespan Decay")};
		s.renderers = {fn("render_animated_sprites", {{"animation rate", 0.5f}})};
		break;
	case ParticleTemplate::Embers:
		s.properties = {{"material", std::string("effects/spark.vmt")},
		                {"max_particles", 50},
		                {"initial_particles", 0},
		                {"radius", 1.0f},
		                {"color", PcfColor{255, 140, 60, 255}}};
		s.emitters = {fn("emit_continuously", {{"emission_rate", 12.0f}, {"emission_duration", 0.0f}, {"emission_start_time", 0.0f}})};
		s.initializers = {sphere(4.0f, 2.0f, 8.0f, Vec3{-4, -4, 10}, Vec3{4, 4, 30}),
		                  fn("Lifetime Random", {{"lifetime_min", 1.0f}, {"lifetime_max", 2.0f}}),
		                  fn("Color Random", {{"color1", PcfColor{255, 180, 80, 255}}, {"color2", PcfColor{255, 90, 30, 255}}}),
		                  fn("Radius Random", {{"radius_min", 0.6f}, {"radius_max", 1.2f}})};
		s.operators = {fn("Movement Basic", {{"gravity", Vec3{0, 0, 20}}, {"drag", 0.1f}}),
		               fn("Alpha Fade Out Random", {{"fade_out_time_min", 0.3f}, {"fade_out_time_max", 0.6f}}), fn("Lifespan Decay")};
		s.renderers = {fn("render_animated_sprites", {{"animation rate", 1.0f}})};
		break;
	}
	return s;
}

const std::vector<ParticleFunctionInfo>& particleFunctionCatalog() {
	static const std::vector<ParticleFunctionInfo> catalog{
		{"emitters", "emit_continuously", "Emits a steady stream of particles.", {{"emission_start_time", 0.0f}, {"emission_rate", 20.0f}, {"emission_duration", 0.0f}}},
		{"emitters", "emit_instantaneously", "Emits a number of particles at once.", {{"emission_start_time", 0.0f}, {"num_to_emit", 10}}},
		{"initializers", "Position Within Sphere Random", "Starts particles inside a sphere, with a speed.",
		 {{"distance_min", 0.0f}, {"distance_max", 4.0f}, {"speed_min", 0.0f}, {"speed_max", 0.0f},
		  {"speed_in_local_coordinate_system_min", Vec3{0, 0, 0}}, {"speed_in_local_coordinate_system_max", Vec3{0, 0, 0}}}},
		{"initializers", "Lifetime Random", "How long each particle lives (seconds).", {{"lifetime_min", 0.5f}, {"lifetime_max", 1.0f}}},
		{"initializers", "Radius Random", "Size of each particle.", {{"radius_min", 2.0f}, {"radius_max", 4.0f}}},
		{"initializers", "Color Random", "Colour between two colours.", {{"color1", PcfColor{255, 255, 255, 255}}, {"color2", PcfColor{255, 255, 255, 255}}}},
		{"initializers", "Alpha Random", "Opacity (0..255).", {{"alpha_min", 255}, {"alpha_max", 255}}},
		{"initializers", "Rotation Random", "Start rotation of sprites (degrees).", {{"rotation_initial", 0.0f}, {"rotation_offset_min", 0.0f}, {"rotation_offset_max", 360.0f}}},
		{"initializers", "Rotation Speed Random", "Spins sprites (degrees per second).", {{"rotation_speed_constant", 0.0f}, {"rotation_speed_random_min", 0.0f}, {"rotation_speed_random_max", 90.0f}}},
		{"operators", "Movement Basic", "Moves particles by their speed, gravity and drag.", {{"gravity", Vec3{0, 0, 0}}, {"drag", 0.0f}}},
		{"operators", "Lifespan Decay", "Removes particles at the end of their life (needed by almost every system).", {}},
		{"operators", "Alpha Fade In Random", "Fades particles in (part of their life).", {{"fade_in_time_min", 0.1f}, {"fade_in_time_max", 0.2f}}},
		{"operators", "Alpha Fade Out Random", "Fades particles out (part of their life).", {{"fade_out_time_min", 0.3f}, {"fade_out_time_max", 0.5f}}},
		{"operators", "Radius Scale", "Grows or shrinks particles over their life.", {{"radius_start_scale", 1.0f}, {"radius_end_scale", 1.0f}}},
		{"operators", "Color Fade", "Fades the colour towards another one.", {{"color_fade", PcfColor{255, 255, 255, 255}}, {"fade_start_time", 0.0f}, {"fade_end_time", 1.0f}}},
		{"operators", "Rotation Basic", "Applies the rotation speed.", {}},
		{"operators", "Oscillate Scalar", "Makes a value go up and down (flicker).", {{"oscillation rate min", 1.0f}, {"oscillation rate max", 2.0f}}},
		{"renderers", "render_animated_sprites", "Draws each particle as a sprite.", {{"animation rate", 1.0f}}},
		{"renderers", "render_sprite_trail", "Draws particles as streaks along their movement (sparks).", {{"min length", 2.0f}, {"max length", 10.0f}}},
	};
	return catalog;
}

std::vector<PcfSystem> withChildren(const std::vector<PcfSystem>& edited, const std::vector<ParticleDefinition>& library) {
	std::vector<PcfSystem> out = edited;
	std::set<std::string> names;
	for (const auto& s : out) names.insert(toLower(s.name));
	// Breadth first over the children; each system once (cycles end there).
	for (std::size_t next = 0; next < out.size() && out.size() < 256; ++next) {
		const auto children = out[next].children;
		for (const auto& child : children) {
			if (names.count(toLower(child.name))) continue;
			const auto it = std::find_if(library.begin(), library.end(), [&](const ParticleDefinition& d) { return iequals(d.name, child.name); });
			if (it == library.end()) continue;
			names.insert(toLower(it->name));
			out.push_back(toPcfSystem(*it));
		}
	}
	return out;
}

std::string particleSystemName(std::string_view text) {
	std::string out;
	for (const char c : trim(text)) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') out += c;
		else if (c == ' ') out += '_';
	}
	return out.substr(0, 96);
}

std::string modParticleFile(std::string_view modName) {
	std::string name = importName(modName);
	if (name.empty()) name = "mod";
	return "particles/" + name + "_mapsmith.pcf";
}

std::optional<ImportRecord> saveModParticles(const fs::path& modDir, std::string_view file, const std::vector<PcfSystem>& systems, std::string_view gameManifest,
                                             std::string* error) {
	const std::string path = normalizePath(file);
	if (!startsWithI(path, "particles/") || !path.ends_with(".pcf") || !isSafeGamePath(path) || path.find('/', 10) != std::string::npos) {
		if (error) *error = "the particle file must be particles/<name>.pcf";
		return std::nullopt;
	}
	if (systems.empty()) {
		if (error) *error = "nothing to save";
		return std::nullopt;
	}
	// Merge with the systems already in the mod's file: same names are replaced.
	std::vector<PcfSystem> merged;
	{
		std::ifstream in(modDir / path, std::ios::binary);
		if (in) {
			std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			const auto bytes = std::as_bytes(std::span(raw.data(), raw.size()));
			for (const auto& definition : readParticleDefinitions(bytes)) {
				const bool replaced = std::any_of(systems.begin(), systems.end(), [&](const PcfSystem& s) { return iequals(s.name, definition.name); });
				if (!replaced) merged.push_back(toPcfSystem(definition));
			}
		}
	}
	merged.insert(merged.end(), systems.begin(), systems.end());
	const std::string stem = path.substr(10, path.size() - 10 - 4);
	return importParticles(modDir, writeParticleFile(merged), stem, gameManifest, true, true, error);
}

} // namespace sme
