// particles.h - Particle systems of Source's .pcf files (binary DMX) in full: every system with
// its own values and its parts (emitters, initializers, operators, renderers, forces,
// constraints, children), and a small CPU simulation of the common parts for a preview.
// The simulation follows the engine's functions by name ("emit_continuously", "Lifetime
// Random", "Movement Basic", ...); parts it does not know are listed but leave the particles
// alone, so the preview is an approximation of what the game shows.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sme/math.h>

namespace sme {

/// One value of a system or a part, as text for the panel and as numbers for the simulation
/// (a color has four numbers 0..255, a vector three; an element array has none).
struct ParticleValue {
	std::string key;
	std::string text;
	std::vector<double> numbers;
};

/// A part of a system: functionName ("emit_continuously") and its values.
struct ParticleFunction {
	std::string name;
	std::vector<ParticleValue> values;

	[[nodiscard]] const ParticleValue* find(std::string_view key) const;
	[[nodiscard]] double number(std::string_view key, double fallback) const;
	[[nodiscard]] Vec3 vector(std::string_view key, const Vec3& fallback) const;
	[[nodiscard]] bool has(std::string_view key) const { return find(key) != nullptr; }
};

struct ParticleChild {
	std::string name;
	double delay = 0;
};

struct ParticleDefinition {
	std::string name;
	ParticleFunction properties;   ///< The system's own values (material, max_particles, radius, color, ...)
	std::vector<ParticleFunction> emitters, initializers, operators, renderers, forces, constraints;
	std::vector<ParticleChild> children;

	[[nodiscard]] std::string material() const { return properties.find("material") ? properties.find("material")->text : std::string(); }
};

/// Every particle system in a .pcf file; empty for broken files.
[[nodiscard]] std::vector<ParticleDefinition> readParticleDefinitions(std::span<const std::byte> pcf);

/// One particle as the preview draws it.
struct ParticleSprite {
	Vec3 position;
	Vec3 velocity;
	double radius = 1;
	double r = 1, g = 1, b = 1, alpha = 1;   ///< 0..1
	double rotation = 0;                      ///< degrees
	int system = 0;                           ///< index into ParticleSimulation::systems()
};

/// Simulates a system and its children on the CPU. Deterministic for a seed.
class ParticleSimulation {
public:
	/// Hard cap of live particles over all systems (the game caps per system with max_particles).
	static constexpr std::size_t MAX_PARTICLES = 3000;

	/// `library`: all systems the children may come from (usually every system of the game).
	ParticleSimulation(std::vector<ParticleDefinition> library, std::string_view root, std::uint32_t seed = 1);

	/// Starts again at time 0.
	void restart();
	/// Advances by `seconds` (split into small steps).
	void step(double seconds);

	[[nodiscard]] double time() const { return m_time; }
	/// Starts again by itself once everything is over (bursts, emitters with a duration).
	bool loop = true;
	[[nodiscard]] bool valid() const { return !m_systems.empty(); }
	[[nodiscard]] std::size_t particleCount() const;
	/// Live particles of every system, with their system index.
	[[nodiscard]] std::vector<ParticleSprite> sprites() const;

	struct SystemInfo {
		std::string name;
		std::string material;
		bool additive = false;   ///< filled in by the caller from the material ($additive)
		bool trail = false;      ///< render_sprite_trail: drawn stretched along the velocity
	};
	[[nodiscard]] std::vector<SystemInfo>& systems() { return m_info; }
	[[nodiscard]] const std::vector<SystemInfo>& systems() const { return m_info; }

private:
	struct Particle {
		Vec3 position, velocity;
		double age = 0, life = 1;
		double radius = 5, baseRadius = 5;
		double r = 1, g = 1, b = 1, baseR = 1, baseG = 1, baseB = 1;
		double alpha = 1, baseAlpha = 1;
		double fadeIn = 0, fadeOut = 0;   ///< fractions of the life (Alpha Fade In/Out Random)
		double rotation = 0, rotationSpeed = 0;
	};
	struct System {
		const ParticleDefinition* definition = nullptr;
		double start = 0;          ///< time the system starts (children: their delay)
		double emitCarry = 0;      ///< fractional particles of continuous emitters
		std::vector<bool> burstDone;
		std::vector<Particle> particles;
		std::size_t maxParticles = 1000;
	};

	void addSystem(const ParticleDefinition& definition, double start, int depth);
	void spawn(System& system, std::size_t count);
	void update(System& system, double dt);
	[[nodiscard]] double random(double a, double b);
	[[nodiscard]] bool emitsLater() const;

	std::vector<ParticleDefinition> m_library;
	std::string m_root;
	std::uint32_t m_seed;
	std::mt19937 m_random;
	std::vector<System> m_systems;
	std::vector<SystemInfo> m_info;
	double m_time = 0;
};

} // namespace sme
