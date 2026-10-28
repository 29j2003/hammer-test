// particles.cpp - .pcf particle systems and their preview simulation (see particles.h).

#include <sme/particles.h>

#include <sme/text.h>

#include <kvpp/DMX.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <numbers>
#include <sstream>
#include <variant>

namespace sme {

namespace {

constexpr int MAX_SYSTEMS = 16;    ///< a system with its children, children of children ...
constexpr int MAX_DEPTH = 4;
constexpr double STEP = 1.0 / 60;  ///< simulation step (seconds)

std::string numberText(double v) {
	std::ostringstream out;
	out.precision(6);
	out << v;
	return out.str();
}

std::string joined(const std::vector<double>& numbers) {
	std::string out;
	for (const double v : numbers) {
		if (!out.empty()) out += ' ';
		out += numberText(v);
	}
	return out;
}

/// The value of a DMX attribute as text and numbers. Element references name the element.
ParticleValue toValue(const kvpp::DMXAttribute& attribute, const std::vector<kvpp::DMXElement>& elements) {
	ParticleValue out;
	out.key = std::string(attribute.getKey());
	const auto elementName = [&](const kvpp::DMXValue::Element& e) -> std::string {
		if (e.index >= 0 && static_cast<std::size_t>(e.index) < elements.size()) return std::string(elements[static_cast<std::size_t>(e.index)].getKey());
		return {};
	};
	std::visit(
		[&](const auto& v) {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, std::monostate>) {
			} else if constexpr (std::is_same_v<T, kvpp::DMXValue::Element>) {
				out.text = elementName(v);
			} else if constexpr (std::is_same_v<T, std::int32_t> || std::is_same_v<T, float> || std::is_same_v<T, std::uint64_t> ||
			                     std::is_same_v<T, std::uint8_t>) {
				out.numbers = {static_cast<double>(v)};
			} else if constexpr (std::is_same_v<T, bool>) {
				out.numbers = {v ? 1.0 : 0.0};
			} else if constexpr (std::is_same_v<T, std::string>) {
				out.text = v;
			} else if constexpr (std::is_same_v<T, kvpp::DMXValue::Time>) {
				out.numbers = {static_cast<double>(v.seconds)};
			} else if constexpr (std::is_same_v<T, kvpp::DMXValue::Color>) {
				out.numbers = {double(v.r), double(v.g), double(v.b), double(v.a)};
			} else if constexpr (std::is_same_v<T, kvpp::DMXValue::Vector2> || std::is_same_v<T, kvpp::DMXValue::Vector3> ||
			                     std::is_same_v<T, kvpp::DMXValue::Vector4> || std::is_same_v<T, kvpp::DMXValue::EulerAngles> ||
			                     std::is_same_v<T, kvpp::DMXValue::Quaternion>) {
				for (std::uint8_t i = 0; i < v.size(); ++i) out.numbers.push_back(static_cast<double>(v[i]));
			} else if constexpr (std::is_same_v<T, std::vector<kvpp::DMXValue::Element>>) {
				out.text = std::to_string(v.size()) + (v.size() == 1 ? " element" : " elements");
			} else if constexpr (std::is_same_v<T, std::vector<std::int32_t>> || std::is_same_v<T, std::vector<float>>) {
				for (const auto x : v) out.numbers.push_back(static_cast<double>(x));
			} else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
				for (const auto& s : v) out.text += (out.text.empty() ? "" : ", ") + s;
			} else {
				out.text = "...";   // binary, matrices and other arrays: not used by particle functions
			}
		},
		attribute.getValue());
	if (out.text.empty() && !out.numbers.empty()) {
		out.text = joined(out.numbers);
	}
	return out;
}

ParticleFunction toFunction(const kvpp::DMXElement& element, const std::vector<kvpp::DMXElement>& elements) {
	ParticleFunction out;
	out.name = std::string(element.getKey());
	for (const auto& attribute : element.getAttributes()) {
		const auto key = attribute.getKey();
		if (key == "functionName") {
			if (const auto* name = std::get_if<std::string>(&attribute.getValue()); name && !name->empty()) out.name = *name;
			continue;
		}
		if (key == "name") continue;
		out.values.push_back(toValue(attribute, elements));
	}
	return out;
}

double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

double clamp01(double v) {
	return std::clamp(v, 0.0, 1.0);
}

bool named(const ParticleFunction& f, std::string_view name) {
	return iequals(f.name, name);
}

} // namespace

const ParticleValue* ParticleFunction::find(std::string_view key) const {
	for (const auto& value : values) {
		if (iequals(value.key, key)) return &value;
	}
	return nullptr;
}

double ParticleFunction::number(std::string_view key, double fallback) const {
	const auto* value = find(key);
	return value && !value->numbers.empty() && std::isfinite(value->numbers.front()) ? value->numbers.front() : fallback;
}

Vec3 ParticleFunction::vector(std::string_view key, const Vec3& fallback) const {
	const auto* value = find(key);
	if (!value || value->numbers.size() < 3) return fallback;
	const Vec3 v{value->numbers[0], value->numbers[1], value->numbers[2]};
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) ? v : fallback;
}

std::vector<ParticleDefinition> readParticleDefinitions(std::span<const std::byte> pcf) {
	std::vector<ParticleDefinition> out;
	try {
		const kvpp::DMX dmx(pcf);
		if (!dmx) {
			return out;
		}
		const auto& elements = dmx.getElements();
		const auto elementAt = [&](const kvpp::DMXValue::Element& e) -> const kvpp::DMXElement* {
			return e.index >= 0 && static_cast<std::size_t>(e.index) < elements.size() ? &elements[static_cast<std::size_t>(e.index)] : nullptr;
		};
		for (const auto& element : elements) {
			if (element.getType() != "DmeParticleSystemDefinition" || element.getKey().empty()) continue;
			ParticleDefinition definition;
			definition.name = std::string(element.getKey());
			definition.properties.name = definition.name;
			for (const auto& attribute : element.getAttributes()) {
				const auto key = attribute.getKey();
				const auto* list = std::get_if<std::vector<kvpp::DMXValue::Element>>(&attribute.getValue());
				std::vector<ParticleFunction>* parts = nullptr;
				if (key == "emitters") parts = &definition.emitters;
				else if (key == "initializers") parts = &definition.initializers;
				else if (key == "operators") parts = &definition.operators;
				else if (key == "renderers") parts = &definition.renderers;
				else if (key == "forces") parts = &definition.forces;
				else if (key == "constraints") parts = &definition.constraints;
				if (parts && list) {
					for (const auto& ref : *list) {
						if (const auto* part = elementAt(ref)) parts->push_back(toFunction(*part, elements));
					}
					continue;
				}
				if (key == "children" && list) {
					for (const auto& ref : *list) {
						const auto* child = elementAt(ref);
						if (!child) continue;
						ParticleChild entry;
						for (const auto& a : child->getAttributes()) {
							if (a.getKey() == "child") {
								if (const auto* e = std::get_if<kvpp::DMXValue::Element>(&a.getValue())) {
									if (const auto* target = elementAt(*e)) entry.name = std::string(target->getKey());
								}
							} else if (a.getKey() == "delay") {
								const auto v = toValue(a, elements);
								if (!v.numbers.empty()) entry.delay = std::max(0.0, v.numbers.front());
							}
						}
						if (entry.name.empty()) entry.name = std::string(child->getKey());
						if (!entry.name.empty()) definition.children.push_back(std::move(entry));
					}
					continue;
				}
				if (key == "name") continue;
				definition.properties.values.push_back(toValue(attribute, elements));
			}
			out.push_back(std::move(definition));
		}
	} catch (const std::exception&) {
		out.clear();   // broken file: nothing rather than half of it
	}
	return out;
}

// --- Writing ---------------------------------------------------------------------------------------

namespace {

/// Binary DMX version 2: attribute types as numbered then (DmAttributeType_t of the Orange Box).
enum class DmxType : std::uint8_t { Element = 1, Int = 2, Float = 3, Bool = 4, String = 5, Color = 8, Vector3 = 10, ElementArray = 15 };

class DmxWriter {
public:
	void byte(std::uint8_t v) { m_out.push_back(std::byte(v)); }
	void u16(std::uint16_t v) {
		byte(std::uint8_t(v & 0xff));
		byte(std::uint8_t(v >> 8));
	}
	void i32(std::int32_t v) {
		const auto u = static_cast<std::uint32_t>(v);
		for (int i = 0; i < 4; ++i) byte(std::uint8_t((u >> (8 * i)) & 0xff));
	}
	void f32(float v) {
		std::uint32_t u = 0;
		std::memcpy(&u, &v, 4);
		i32(static_cast<std::int32_t>(u));
	}
	void text(std::string_view v) {
		for (const char c : v) byte(static_cast<std::uint8_t>(c));
		byte(0);
	}
	[[nodiscard]] std::vector<std::byte> take() { return std::move(m_out); }

private:
	std::vector<std::byte> m_out;
};

/// One element to write: its type, name and attributes (element references by index).
struct DmxElement {
	std::string type, name;
	PcfValues values;
	std::vector<std::pair<std::string, std::vector<std::int32_t>>> arrays;   ///< element arrays, written after the values
};

} // namespace

std::vector<std::byte> writeParticleFile(const std::vector<PcfSystem>& systems) {
	// Elements: the root, then each system followed by its parts.
	std::vector<DmxElement> elements(1);
	elements[0].type = "DmElement";
	elements[0].name = "untitled";
	std::vector<std::int32_t> definitions;
	for (const auto& system : systems) {
		const auto index = std::int32_t(elements.size());
		definitions.push_back(index);
		elements.push_back({"DmeParticleSystemDefinition", system.name, system.properties, {}});
		const std::pair<const char*, const std::vector<PcfFunction>*> lists[] = {{"renderers", &system.renderers},       {"operators", &system.operators},
		                                                                          {"initializers", &system.initializers}, {"emitters", &system.emitters},
		                                                                          {"forces", &system.forces},             {"constraints", &system.constraints}};
		std::vector<std::pair<std::string, std::vector<std::int32_t>>> arrays;
		for (const auto& [key, functions] : lists) {
			std::vector<std::int32_t> refs;
			for (const auto& function : *functions) {
				refs.push_back(std::int32_t(elements.size()));
				PcfValues values{{"functionName", function.functionName}};
				values.insert(values.end(), function.values.begin(), function.values.end());
				elements.push_back({"DmeParticleOperator", function.functionName, std::move(values), {}});
			}
			arrays.emplace_back(key, std::move(refs));
		}
		arrays.emplace_back("children", std::vector<std::int32_t>{});
		elements[std::size_t(index)].arrays = std::move(arrays);
	}
	elements[0].arrays.emplace_back("particleSystemDefinitions", std::move(definitions));

	// The string table: element types and attribute names (version 2 keeps element names and
	// string values inline).
	std::vector<std::string> strings;
	const auto stringIndex = [&strings](const std::string& s) {
		for (std::size_t i = 0; i < strings.size(); ++i) {
			if (strings[i] == s) return std::uint16_t(i);
		}
		strings.push_back(s);
		return std::uint16_t(strings.size() - 1);
	};
	for (const auto& e : elements) {
		stringIndex(e.type);
		for (const auto& [key, value] : e.values) stringIndex(key);
		for (const auto& [key, refs] : e.arrays) stringIndex(key);
	}

	DmxWriter w;
	w.text("<!-- dmx encoding binary 2 format pcf 1 -->\n");
	w.u16(std::uint16_t(strings.size()));
	for (const auto& s : strings) w.text(s);
	w.i32(std::int32_t(elements.size()));
	for (std::size_t i = 0; i < elements.size(); ++i) {
		const auto& e = elements[i];
		w.u16(stringIndex(e.type));
		w.text(e.name);
		// Id: FNV-1a of the index and name, spread over 16 bytes (stable between runs).
		std::uint64_t hash = 1469598103934665603ull ^ i;
		for (const char c : e.name) hash = (hash ^ std::uint8_t(c)) * 1099511628211ull;
		for (int k = 0; k < 16; ++k) {
			hash = (hash ^ std::uint64_t(k)) * 1099511628211ull;
			w.byte(std::uint8_t(hash >> 56));
		}
	}
	for (const auto& e : elements) {
		w.i32(std::int32_t(e.values.size() + e.arrays.size()));
		for (const auto& [key, value] : e.values) {
			w.u16(stringIndex(key));
			std::visit(
				[&w](const auto& v) {
					using T = std::decay_t<decltype(v)>;
					if constexpr (std::is_same_v<T, int>) {
						w.byte(std::uint8_t(DmxType::Int));
						w.i32(v);
					} else if constexpr (std::is_same_v<T, float>) {
						w.byte(std::uint8_t(DmxType::Float));
						w.f32(v);
					} else if constexpr (std::is_same_v<T, bool>) {
						w.byte(std::uint8_t(DmxType::Bool));
						w.byte(v ? 1 : 0);
					} else if constexpr (std::is_same_v<T, std::string>) {
						w.byte(std::uint8_t(DmxType::String));
						w.text(v);
					} else if constexpr (std::is_same_v<T, PcfColor>) {
						w.byte(std::uint8_t(DmxType::Color));
						w.byte(v.r);
						w.byte(v.g);
						w.byte(v.b);
						w.byte(v.a);
					} else {
						w.byte(std::uint8_t(DmxType::Vector3));
						w.f32(float(v.x));
						w.f32(float(v.y));
						w.f32(float(v.z));
					}
				},
				value);
		}
		for (const auto& [key, refs] : e.arrays) {
			w.u16(stringIndex(key));
			w.byte(std::uint8_t(DmxType::ElementArray));
			w.i32(std::int32_t(refs.size()));
			for (const auto ref : refs) w.i32(ref);
		}
	}
	return w.take();
}

// --- Simulation --------------------------------------------------------------------------------

ParticleSimulation::ParticleSimulation(std::vector<ParticleDefinition> library, std::string_view root, std::uint32_t seed)
	: m_library(std::move(library)), m_root(root), m_seed(seed) {
	restart();
}

void ParticleSimulation::restart() {
	m_random.seed(m_seed);
	m_systems.clear();
	m_info.clear();
	m_time = 0;
	for (const auto& definition : m_library) {
		if (iequals(definition.name, m_root)) {
			addSystem(definition, 0, 0);
			break;
		}
	}
}

void ParticleSimulation::addSystem(const ParticleDefinition& definition, double start, int depth) {
	if (static_cast<int>(m_systems.size()) >= MAX_SYSTEMS || depth > MAX_DEPTH) {
		return;
	}
	System system;
	system.definition = &definition;
	system.start = start;
	system.burstDone.assign(definition.emitters.size(), false);
	system.maxParticles = static_cast<std::size_t>(std::clamp(definition.properties.number("max_particles", 1000), 0.0, double(MAX_PARTICLES)));
	m_systems.push_back(std::move(system));
	SystemInfo info;
	info.name = definition.name;
	info.material = definition.material();
	for (const auto& renderer : definition.renderers) {
		if (toLower(renderer.name).find("trail") != std::string::npos) info.trail = true;
	}
	m_info.push_back(std::move(info));
	for (const auto& child : definition.children) {
		for (const auto& candidate : m_library) {
			if (iequals(candidate.name, child.name)) {
				addSystem(candidate, start + child.delay, depth + 1);
				break;
			}
		}
	}
}

double ParticleSimulation::random(double a, double b) {
	if (b < a) std::swap(a, b);
	return a + (b - a) * std::uniform_real_distribution<double>(0.0, 1.0)(m_random);
}

std::size_t ParticleSimulation::particleCount() const {
	std::size_t n = 0;
	for (const auto& system : m_systems) n += system.particles.size();
	return n;
}

void ParticleSimulation::spawn(System& system, std::size_t count) {
	const auto& def = *system.definition;
	const auto& props = def.properties;
	const Vec3 white{255, 255, 255};
	const auto* baseColor = props.find("color");
	for (std::size_t n = 0; n < count; ++n) {
		if (system.particles.size() >= system.maxParticles || particleCount() >= MAX_PARTICLES) {
			return;
		}
		Particle p;
		p.radius = p.baseRadius = std::max(0.0, props.number("radius", 5));
		if (baseColor && baseColor->numbers.size() >= 4) {
			p.r = baseColor->numbers[0] / 255;
			p.g = baseColor->numbers[1] / 255;
			p.b = baseColor->numbers[2] / 255;
			p.alpha = baseColor->numbers[3] / 255;
		}
		const auto randomDirection = [this] {
			// Uniform on the sphere.
			const double z = random(-1, 1), angle = random(0, 2 * std::numbers::pi);
			const double s = std::sqrt(std::max(0.0, 1 - z * z));
			return Vec3{s * std::cos(angle), s * std::sin(angle), z};
		};
		const auto randomBetween = [this](const Vec3& a, const Vec3& b) { return Vec3{random(a.x, b.x), random(a.y, b.y), random(a.z, b.z)}; };
		for (const auto& f : def.initializers) {
			if (named(f, "Lifetime Random")) {
				p.life = random(f.number("lifetime_min", 0), f.number("lifetime_max", 0));
			} else if (named(f, "Radius Random")) {
				const double t = std::pow(random(0, 1), std::max(0.01, f.number("radius_random_exponent", 1)));
				p.radius = lerp(f.number("radius_min", 1), f.number("radius_max", 1), t);
			} else if (named(f, "Color Random")) {
				const Vec3 c1 = f.vector("color1", white), c2 = f.vector("color2", white);
				const double t = random(0, 1);
				p.r = lerp(c1.x, c2.x, t) / 255;
				p.g = lerp(c1.y, c2.y, t) / 255;
				p.b = lerp(c1.z, c2.z, t) / 255;
			} else if (named(f, "Alpha Random")) {
				p.alpha = random(f.number("alpha_min", 255), f.number("alpha_max", 255)) / 255;
			} else if (named(f, "Position Within Sphere Random")) {
				const Vec3 dir = randomDirection();
				const Vec3 bias = f.vector("distance_bias", {1, 1, 1});
				const double distance = random(f.number("distance_min", 0), f.number("distance_max", 0));
				p.position = p.position + Vec3{dir.x * bias.x, dir.y * bias.y, dir.z * bias.z} * distance;
				p.velocity = p.velocity + dir * random(f.number("speed_min", 0), f.number("speed_max", 0)) +
				             randomBetween(f.vector("speed_in_local_coordinate_system_min", {}), f.vector("speed_in_local_coordinate_system_max", {}));
			} else if (named(f, "Position Within Box Random")) {
				p.position = p.position + randomBetween(f.vector("min", {}), f.vector("max", {}));
			} else if (named(f, "Position Modify Offset Random")) {
				p.position = p.position + randomBetween(f.vector("offset min", f.vector("offset_min", {})), f.vector("offset max", f.vector("offset_max", {})));
			} else if (named(f, "Velocity Random")) {
				p.velocity = p.velocity + randomDirection() * random(f.number("speed_min", 0), f.number("speed_max", 0)) +
				             randomBetween(f.vector("speed_in_local_coordinate_system_min", {}), f.vector("speed_in_local_coordinate_system_max", {}));
			} else if (named(f, "Velocity Noise")) {
				p.velocity = p.velocity + randomBetween(f.vector("output_minimum", {}), f.vector("output_maximum", {}));
			} else if (named(f, "Rotation Random")) {
				p.rotation = f.number("rotation_initial", 0) + random(f.number("rotation_offset_min", 0), f.number("rotation_offset_max", 360));
			} else if (named(f, "Rotation Speed Random")) {
				p.rotationSpeed = f.number("rotation_speed_constant", 0) +
				                  random(f.number("rotation_speed_random_min", 0), f.number("rotation_speed_random_max", 360));
			}
		}
		for (const auto& f : def.operators) {
			if (named(f, "Alpha Fade In Random")) p.fadeIn = random(f.number("fade_in_time_min", 0.25), f.number("fade_in_time_max", 0.25));
			if (named(f, "Alpha Fade Out Random")) p.fadeOut = random(f.number("fade_out_time_min", 0.25), f.number("fade_out_time_max", 0.25));
			if (named(f, "Alpha Fade In Simple")) p.fadeIn = f.number("proportional fade in time", 0.25);
			if (named(f, "Alpha Fade Out Simple")) p.fadeOut = f.number("proportional fade out time", 0.25);
		}
		p.life = std::max(0.01, p.life);
		p.baseRadius = p.radius;
		p.baseR = p.r;
		p.baseG = p.g;
		p.baseB = p.b;
		p.baseAlpha = p.alpha;
		system.particles.push_back(p);
	}
}

void ParticleSimulation::update(System& system, double dt) {
	const auto& def = *system.definition;
	const double t = m_time - system.start;
	if (t < 0) {
		return;
	}
	// Emit.
	if (t < dt * 0.5 + 1e-9 && def.properties.number("initial_particles", 0) > 0) {
		spawn(system, static_cast<std::size_t>(def.properties.number("initial_particles", 0)));
	}
	for (std::size_t i = 0; i < def.emitters.size(); ++i) {
		const auto& f = def.emitters[i];
		const double begin = f.number("emission_start_time", 0);
		if (named(f, "emit_instantaneously")) {
			if (!system.burstDone[i] && t >= begin) {
				system.burstDone[i] = true;
				spawn(system, static_cast<std::size_t>(std::clamp(f.number("num_to_emit", 100), 0.0, double(MAX_PARTICLES))));
			}
			continue;
		}
		double rate = 0;
		if (named(f, "emit_continuously")) rate = f.number("emission_rate", 100);
		else if (named(f, "emit noise")) rate = (f.number("emission_minimum", 0) + f.number("emission_maximum", 100)) / 2;
		else continue;
		const double duration = f.number("emission_duration", 0);
		if (t >= begin && (duration <= 0 || t < begin + duration)) {
			system.emitCarry += std::clamp(rate, 0.0, 100000.0) * dt;
			const auto count = static_cast<std::size_t>(system.emitCarry);
			system.emitCarry -= double(count);
			spawn(system, count);
		}
	}

	// Move and change.
	bool decays = false;
	for (const auto& f : def.operators) {
		decays = decays || named(f, "Lifespan Decay") || named(f, "Alpha Fade and Decay");
	}
	Vec3 gravity;
	double drag = 0;
	for (const auto& f : def.operators) {
		if (named(f, "Movement Basic")) {
			gravity = f.vector("gravity", {});
			drag = clamp01(f.number("drag", 0));
		}
	}
	Vec3 force;
	for (const auto& f : def.forces) {
		if (named(f, "random force")) force = force + Vec3{random(f.vector("min_force", {}).x, f.vector("max_force", {}).x),
		                                                   random(f.vector("min_force", {}).y, f.vector("max_force", {}).y),
		                                                   random(f.vector("min_force", {}).z, f.vector("max_force", {}).z)};
	}
	const double dragFactor = std::pow(1 - drag, dt * 30);
	for (auto& p : system.particles) {
		p.age += dt;
		p.velocity = (p.velocity + (gravity + force) * dt) * dragFactor;
		p.position = p.position + p.velocity * dt;
		p.rotation += p.rotationSpeed * dt;
		const double f = clamp01(p.age / p.life);
		double alpha = p.baseAlpha;
		double radius = p.baseRadius;
		double r = p.baseR, g = p.baseG, b = p.baseB;
		for (const auto& op : def.operators) {
			if (named(op, "Alpha Fade and Decay")) {
				const double startAlpha = op.number("start_alpha", 1), endAlpha = op.number("end_alpha", 0);
				const double inStart = op.number("start_fade_in_time", 0), inEnd = op.number("end_fade_in_time", 0.5);
				const double outStart = op.number("start_fade_out_time", 0.5), outEnd = op.number("end_fade_out_time", 1);
				double m = 1;
				if (f < inEnd && inEnd > inStart) m = lerp(startAlpha, 1, clamp01((f - inStart) / (inEnd - inStart)));
				else if (f > outStart && outEnd > outStart) m = lerp(1, endAlpha, clamp01((f - outStart) / (outEnd - outStart)));
				alpha *= m;
			} else if (named(op, "Alpha Fade In Random") || named(op, "Alpha Fade In Simple")) {
				if (p.fadeIn > 0) alpha *= clamp01(f / p.fadeIn);
			} else if (named(op, "Alpha Fade Out Random") || named(op, "Alpha Fade Out Simple")) {
				if (p.fadeOut > 0) alpha *= clamp01((1 - f) / p.fadeOut);
			} else if (named(op, "Radius Scale")) {
				const double s = op.number("start_time", 0), e = op.number("end_time", 1);
				const double k = e > s ? clamp01((f - s) / (e - s)) : (f >= s ? 1 : 0);
				radius *= lerp(op.number("radius_start_scale", 1), op.number("radius_end_scale", 1), k);
			} else if (named(op, "Color Fade")) {
				const Vec3 to = op.vector("color_fade", {255, 255, 255}) / 255.0;
				const double s = op.number("fade_start_time", 0), e = op.number("fade_end_time", 1);
				const double k = e > s ? clamp01((f - s) / (e - s)) : (f >= s ? 1 : 0);
				r = lerp(r, to.x, k);
				g = lerp(g, to.y, k);
				b = lerp(b, to.z, k);
			} else if (named(op, "Rotation Spin Roll")) {
				p.rotation += op.number("spin_rate_degrees", 0) * dt;
			}
		}
		p.alpha = clamp01(alpha);
		p.radius = std::max(0.0, radius);
		p.r = clamp01(r);
		p.g = clamp01(g);
		p.b = clamp01(b);
	}
	if (decays) {
		std::erase_if(system.particles, [](const Particle& p) { return p.age >= p.life; });
	}
}

void ParticleSimulation::step(double seconds) {
	if (m_systems.empty() || !(seconds > 0)) {
		return;
	}
	seconds = std::min(seconds, 2.0);   // a long pause (window hidden) does not burn through minutes
	while (seconds > 1e-9) {
		const double dt = std::min(STEP, seconds);
		seconds -= dt;
		m_time += dt;
		for (auto& system : m_systems) update(system, dt);
	}
	// A burst that is over starts again, so the preview keeps showing something.
	if (loop && m_time > 0.5 && particleCount() == 0 && !emitsLater()) {
		const auto infos = m_info;
		restart();
		m_info = infos;   // keeps what the caller filled in (additive)
	}
}

bool ParticleSimulation::emitsLater() const {
	for (const auto& system : m_systems) {
		const double t = m_time - system.start;
		if (t < 0) return true;
		const auto& def = *system.definition;
		for (std::size_t i = 0; i < def.emitters.size(); ++i) {
			const auto& f = def.emitters[i];
			if (named(f, "emit_instantaneously")) {
				if (!system.burstDone[i]) return true;
				continue;
			}
			if (!named(f, "emit_continuously") && !named(f, "emit noise")) continue;
			const double duration = f.number("emission_duration", 0);
			if (duration <= 0 || t < f.number("emission_start_time", 0) + duration) return true;
		}
	}
	return false;
}

std::vector<ParticleSprite> ParticleSimulation::sprites() const {
	std::vector<ParticleSprite> out;
	out.reserve(particleCount());
	for (std::size_t i = 0; i < m_systems.size(); ++i) {
		for (const auto& p : m_systems[i].particles) {
			out.push_back({p.position, p.velocity, p.radius, p.r, p.g, p.b, p.alpha, p.rotation, static_cast<int>(i)});
		}
	}
	return out;
}

} // namespace sme
