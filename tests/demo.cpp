// demo.cpp - Demo content for screenshots and manual testing: procedural textures, a model
// with animations, sound scripts, extra entity classes and a demo map (room with lights,
// window, pillars, terrain, an NPC, triggers and a few deliberate mistakes).

#include "fixture.h"

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/map.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <fstream>
#include <tuple>
#include <functional>
#include <numbers>

#include <vtfpp/VTF.h>

namespace sme::test {

namespace {

double hash(int x, int y, int seed) {
	unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + static_cast<unsigned>(seed) * 2147483647u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffff) / 65535.0;
}

/// Tileable value noise in 0..1.
double noise(double x, double y, int period, int seed) {
	const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
	const double fx = x - x0, fy = y - y0;
	const auto at = [&](int ix, int iy) { return hash(((ix % period) + period) % period, ((iy % period) + period) % period, seed); };
	const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
	const double a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
	const double b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
	return a + (b - a) * sy;
}

double fbm(double u, double v, int seed) {
	double sum = 0, amp = 0.5;
	int period = 4;
	for (int o = 0; o < 5; ++o) {
		sum += noise(u * period, v * period, period, seed + o) * amp;
		amp *= 0.5;
		period *= 2;
	}
	return sum;
}

struct Rgba {
	double r, g, b, a = 1;
};

std::vector<std::byte> makeTexture(unsigned size, const std::function<Rgba(double u, double v)>& shader, bool alpha = false) {
	std::vector<std::byte> pixels(static_cast<std::size_t>(size) * size * 4);
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			const auto c = shader((x + 0.5) / size, (y + 0.5) / size);
			auto* p = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
			p[0] = std::byte(static_cast<unsigned char>(std::clamp(c.r, 0.0, 1.0) * 255));
			p[1] = std::byte(static_cast<unsigned char>(std::clamp(c.g, 0.0, 1.0) * 255));
			p[2] = std::byte(static_cast<unsigned char>(std::clamp(c.b, 0.0, 1.0) * 255));
			p[3] = std::byte(static_cast<unsigned char>(std::clamp(c.a, 0.0, 1.0) * 255));
		}
	}
	vtfpp::VTF::CreationOptions options;
	options.outputFormat = alpha ? vtfpp::ImageFormat::DXT5 : vtfpp::ImageFormat::DXT1;
	options.computeReflectivity = false;
	return vtfpp::VTF::create(pixels, vtfpp::ImageFormat::RGBA8888, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size), options).bake();
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

constexpr std::string_view DEMO_FGD = R"FGD(
@SolidClass = worldspawn : "This is the world entity. Each map can only contain one, and it's automatically created for you."
[
	message(string) : "Map Description / Title"
	skyname(string) : "SkyBox Texture Name" : "sky_day01_01" : "Texture used for the 2D skybox."
	maxpropscreenwidth(float) : "Start Fade Pixels" : "-1" : "Number of pixels wide at which all props in the level start to fade (<0 = use fademaxdist)."
	detailvbsp(string) : "Detail.vbsp file" : "detail.vbsp" : "Detail.vbsp file to use for emitting detail props."
	detailmaterial(string) : "Detail material file" : "detail/detailsprites" : "Material for detail sprites to use for drawing detail props."
	coldworld(choices) : "World is cold" : 0 : "Emits steam from NPCs' mouths and similar effects." =
	[
		0 : "No"
		1 : "Yes"
	]
]

@BaseClass base(Targetname, Angles) = BaseNPC
[
	spawnflags(flags) =
	[
		1 : "Wait Till Seen" : 0
		2 : "Gag (No IDLE sounds until angry)" : 0
		4 : "Fall to ground (unchecked means *teleport* to ground)" : 1
		16 : "Efficient - Don't acquire enemies or avoid obstacles" : 0
		128 : "Wait For Script" : 0
		8192 : "Don't drop weapons" : 0
	]
	squadname(string) : "Squad Name" : : "NPCs in the same squad share information about enemies and take turns attacking."
	sleepstate(choices) : "Sleep State" : 0 : "Holds the NPC in stasis until the condition is met." =
	[
		0 : "None"
		1 : "Waiting for threat"
		2 : "Waiting for PVS"
	]
	input SetHealth(integer) : "Set this NPC's health."
	output OnDeath(void) : "Fired when this NPC is killed."
]

@NPCClass base(BaseNPC) studio("models/humans/group01/male_07.mdl") = npc_citizen : "A citizen of City 17."
[
	citizentype(choices) : "Type" : 0 : "Which clothes and head the citizen gets." =
	[
		0 : "Default"
		1 : "Downtrodden"
		2 : "Refugee"
		3 : "Rebel"
	]
	expressiontype(choices) : "Expression Type" : 0 =
	[
		0 : "Random"
		1 : "Scared"
		2 : "Normal"
		3 : "Angry"
	]
	input StartPatrolling(void) : "Patrol whenever I'm idle or alert."
	input StopPatrolling(void) : "Stop patrolling."
]

@PointClass base(Targetname, Angles) iconsprite("editor/scripted_sequence.vmt") color(255 0 255) = scripted_sequence : "Grabs an NPC and makes it play a set of animations, either in place or after moving to this entity."
[
	m_iszEntity(target_destination) : "Target NPC" : : "The name or class name of the NPC to use for this script."
	m_iszIdle(string) : "Pre Action Idle Animation" : "" : "Sequence or activity the NPC loops while it waits for the script to be triggered."
	m_iszEntry(string) : "Entry Animation" : "" : "Sequence played once before the action animation."
	m_iszPlay(string) : "Action Animation" : "" : "The main sequence of the script."
	m_iszPostIdle(string) : "Post Action Idle Animation" : "" : "Sequence looped after the action animation."
	m_fMoveTo(choices) : "Move to Position" : 1 =
	[
		0 : "No"
		1 : "Walk"
		2 : "Run"
		4 : "Instantaneous"
	]
	onplayerdeath(choices) : "On player death" : 0 =
	[
		0 : "Do Nothing"
		1 : "Cancel Script and return to AI"
	]
	spawnflags(flags) =
	[
		4 : "Repeatable" : 0
		16 : "Start on Spawn" : 0
		32 : "No Interruptions" : 0
		64 : "Override AI" : 0
		128 : "Don't Teleport NPC On End" : 0
		256 : "Loop in Post Idle" : 0
	]
	input BeginSequence(void) : "Summons the NPC to act out the scripted sequence."
	input CancelSequence(void) : "Stops the scripted sequence."
	output OnBeginSequence(void) : "Fires when the NPC begins the action animation."
	output OnEndSequence(void) : "Fires when the action animation completes."
]

@PointClass base(Targetname, Angles) studio() = prop_dynamic : "A prop that can play animations and be moved by parenting."
[
	model(studio) : "World Model" : : "The model this prop shows."
	DefaultAnim(string) : "Default Animation" : "" : "The idle animation this prop returns to whenever it finishes another animation."
	solid(choices) : "Collisions" : 6 =
	[
		0 : "Not Solid"
		2 : "Use Bounding Box"
		6 : "Use VPhysics"
	]
	skin(integer) : "Skin" : 0 : "Some models have several versions of their textures, called skins."
	input SetAnimation(string) : "Force the prop to play an animation."
]

@PointClass base(Angles) studioprop() = prop_static : "A prop that does not move. Cheapest kind of model: lit by the map's lighting and baked into the map."
[
	model(studio) : "World Model" : : "The model this prop shows."
	skin(integer) : "Skin" : 0 : "Some models have several versions of their textures, called skins."
	solid(choices) : "Collisions" : 6 =
	[
		0 : "Not Solid"
		2 : "Use Bounding Box"
		6 : "Use VPhysics"
	]
	disableshadows(boolean) : "Disable Shadows" : 0 : "Stops the prop from casting a lightmap shadow."
]

@PointClass base(Targetname, Angles) iconsprite("editor/info_particle_system.vmt") color(255 160 60) = info_particle_system : "A particle effect (fire, smoke, sparks) from the game's .pcf files."
[
	effect_name(particlesystem) : "Particle System Name" : "" : "Name of the particle system (see the Particles tab)."
	start_active(boolean) : "Start Active?" : 0 : "Plays as soon as the map starts."
	input Start(void) : "Starts the effect."
	input Stop(void) : "Stops the effect."
]

@PointClass base(Targetname) iconsprite("editor/env_soundscape.vmt") color(255 128 0) = env_soundscape : "Plays a soundscape (looping and random background sounds) while the player can see it."
[
	radius(integer) : "Radius" : 128 : "If the player is within this distance (-1 = everywhere), the soundscape plays."
	soundscape(string) : "Soundscape" : "Nothing" : "Name of the soundscape from scripts/soundscapes_*.txt."
]

@PointClass base(Targetname) iconsprite("editor/ambient_generic.vmt") color(80 200 255) = ambient_generic : "Plays and controls a single sound."
[
	message(sound) : "Sound Name" : "" : "Name of the sound script entry (or .wav file) to play."
	health(integer) : "Volume" : 10 : "Volume from 0 to 10."
	input PlaySound(void) : "Starts the sound."
	input StopSound(void) : "Stops the sound."
]

@PointClass base(Targetname) iconsprite("editor/logic_relay.vmt") color(0 200 120) = logic_relay : "Forwards a message: fires OnTrigger when it is triggered."
[
	input Trigger(void) : "Fire the OnTrigger output."
	output OnTrigger(void) : "Fired when the relay is triggered."
]

@PointClass base(Targetname, Angles) iconsprite("editor/light_spot.vmt") color(255 255 0) = light_spot : "An invisible, directional spotlight."
[
	_light(color255) : "Brightness" : "255 255 255 200" : "Color and brightness of the light."
	_inner_cone(integer) : "Inner (bright) angle" : 30 : "Angle of the fully lit cone."
	_cone(integer) : "Outer (fading) angle" : 45 : "Angle where the light has faded out."
	_exponent(integer) : "Focus" : 1 : "Changes the distance between the umbra and penumbra cone."
	pitch(angle_negative_pitch) : "Pitch" : "-90" : "Pitch of the light: -90 points straight down."
]

@SolidClass base(Targetname) = func_door : "A brush that moves between two positions, like a door."
[
	speed(integer) : "Speed" : 100 : "How fast the door moves."
	input Open(void) : "Open the door."
	input Close(void) : "Close the door."
	output OnFullyOpen(void) : "Fired when the door is fully open."
]
)FGD";

std::string vmt(std::string_view shader, std::string_view body) {
	return "\"" + std::string(shader) + "\"\n{\n" + std::string(body) + "}\n";
}

Bounds box(Vec3 a, Vec3 b) {
	Bounds out;
	out.add(a);
	out.add(b);
	return out;
}

} // namespace

void createDemo(Fixture& f) {
	const auto mat = f.mod / "materials";
	// Textures (DXT like the real game).
	writeBytes(mat / "demo/brickwall001.vtf", makeTexture(256, [](double u, double v) {
		const double row = std::floor(v * 8);
		const double bu = u * 4 + (std::fmod(row, 2) * 0.5);
		const bool mortar = std::fmod(v * 8, 1.0) < 0.08 || std::fmod(bu, 1.0) < 0.04;
		const double n = fbm(u, v, 3) * 0.35 + hash(int(bu), int(row), 9) * 0.25;
		if (mortar) return Rgba{0.62 + n * 0.1, 0.6 + n * 0.1, 0.55 + n * 0.1};
		return Rgba{0.48 + n * 0.4, 0.22 + n * 0.2, 0.15 + n * 0.12};
	}));
	writeBytes(mat / "demo/concretewall001.vtf", makeTexture(256, [](double u, double v) {
		const double n = fbm(u, v, 11);
		const double stain = std::max(0.0, fbm(u * 0.5, v, 21) - 0.55) * 0.6;
		return Rgba{0.55 + n * 0.25 - stain, 0.55 + n * 0.25 - stain, 0.52 + n * 0.22 - stain};
	}));
	writeBytes(mat / "demo/floortile001.vtf", makeTexture(256, [](double u, double v) {
		const bool grout = std::fmod(u * 4, 1.0) < 0.03 || std::fmod(v * 4, 1.0) < 0.03;
		const double n = fbm(u, v, 31) * 0.2;
		const double tint = hash(int(u * 4), int(v * 4), 5) * 0.08;
		if (grout) return Rgba{0.2, 0.2, 0.2};
		return Rgba{0.45 + n + tint, 0.43 + n + tint, 0.4 + n + tint};
	}));
	writeBytes(mat / "demo/metal001.vtf", makeTexture(128, [](double u, double v) {
		const double streak = noise(u * 2, v * 64, 64, 41) * 0.15;
		const bool rivet = std::hypot(std::fmod(u * 4, 1.0) - 0.5, std::fmod(v * 4, 1.0) - 0.5) < 0.06;
		const double base = 0.42 + streak + fbm(u, v, 43) * 0.1 + (rivet ? 0.2 : 0);
		return Rgba{base * 0.92, base * 0.96, base * 1.05};
	}));
	writeBytes(mat / "demo/wood001.vtf", makeTexture(256, [](double u, double v) {
		const double plank = std::floor(u * 5);
		const double grain = std::sin((v * 40 + noise(u * 20, v * 4, 20, 51) * 6) + plank * 3) * 0.5 + 0.5;
		const bool gap = std::fmod(u * 5, 1.0) < 0.03;
		const double t = 0.3 + grain * 0.18 + hash(int(plank), 1, 52) * 0.1;
		return gap ? Rgba{0.12, 0.08, 0.05} : Rgba{t * 1.5, t * 1.0, t * 0.6};
	}));
	writeBytes(mat / "demo/grass001.vtf", makeTexture(256, [](double u, double v) {
		const double n = fbm(u, v, 61);
		return Rgba{0.22 + n * 0.2, 0.38 + n * 0.3, 0.12 + n * 0.1};
	}));
	writeBytes(mat / "demo/dirt001.vtf", makeTexture(256, [](double u, double v) {
		const double n = fbm(u, v, 71);
		return Rgba{0.42 + n * 0.25, 0.32 + n * 0.2, 0.22 + n * 0.15};
	}));
	writeBytes(mat / "demo/glass001.vtf", makeTexture(128, [](double u, double v) {
		const double streak = std::max(0.0, std::sin((u + v) * 12) - 0.8) * 1.5;
		return Rgba{0.65 + streak, 0.8 + streak, 0.9 + streak, 0.32 + streak * 0.3};
	}, true));
	// Normal map of the bricks (mortar lies deeper), so relief can be made from it.
	writeBytes(mat / "demo/brickwall001_normal.vtf", makeTexture(256, [](double u, double v) {
		const auto height = [](double hu, double hv) {
			const double row = std::floor(hv * 8);
			const double bu = hu * 4 + (std::fmod(row, 2) * 0.5);
			const double mortarV = std::min(std::fmod(hv * 8, 1.0), 1 - std::fmod(hv * 8, 1.0));
			const double mortarU = std::min(std::fmod(bu, 1.0), 1 - std::fmod(bu, 1.0));
			return std::clamp(std::min(mortarV / 0.08, mortarU / 0.04), 0.0, 1.0);
		};
		const double e = 1.0 / 256;
		const double dx = (height(u + e, v) - height(u - e, v)) * 2, dy = (height(u, v + e) - height(u, v - e)) * 2;
		const double len = std::sqrt(dx * dx + dy * dy + 1);
		return Rgba{(-dx / len) * 0.5 + 0.5, (dy / len) * 0.5 + 0.5, (1 / len) * 0.5 + 0.5};
	}));
	// Waves for the water.
	writeBytes(mat / "demo/water001_normal.vtf", makeTexture(128, [](double u, double v) {
		const double dx = std::cos((u * 3 + v) * 2 * std::numbers::pi) * 0.3, dy = std::cos((v * 2 - u) * 2 * std::numbers::pi) * 0.3;
		const double len = std::sqrt(dx * dx + dy * dy + 1);
		return Rgba{(-dx / len) * 0.5 + 0.5, (dy / len) * 0.5 + 0.5, (1 / len) * 0.5 + 0.5};
	}));
	writeBytes(mat / "models/humans/male_sheet.vtf", makeTexture(128, [](double u, double v) {
		return Rgba{0.5 + fbm(u, v, 81) * 0.3, 0.35, 0.3};
	}));
	writeText(mat / "demo/brickwall001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/brickwall001\"\n\t\"$bumpmap\" \"demo/brickwall001_normal\"\n\t\"$surfaceprop\" \"brick\"\n"));
	writeText(mat / "demo/concretewall001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/concretewall001\"\n\t\"$surfaceprop\" \"concrete\"\n"));
	writeText(mat / "demo/floortile001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/floortile001\"\n\t\"$surfaceprop\" \"tile\"\n"));
	writeText(mat / "demo/metal001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/metal001\"\n\t\"$surfaceprop\" \"metal\"\n"));
	writeText(mat / "demo/wood001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/wood001\"\n\t\"$surfaceprop\" \"wood\"\n"));
	writeText(mat / "demo/grass001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/grass001\"\n"));
	writeText(mat / "demo/blend_grass_dirt.vmt",
	          vmt("WorldVertexTransition", "\t\"$basetexture\" \"demo/grass001\"\n\t\"$basetexture2\" \"demo/dirt001\"\n\t\"$surfaceprop\" \"dirt\"\n"));
	// A 2D skybox (six faces): a blue gradient with a bright horizon, the ground below.
	for (const char* face : {"up", "dn", "lf", "rt", "ft", "bk"}) {
		const bool up = std::string_view(face) == "up", down = std::string_view(face) == "dn";
		writeBytes(mat / "skybox" / (std::string("sky_demo01") + face + ".vtf"), makeTexture(64, [up, down](double u, double v) {
			if (up) return Rgba{0.25, 0.45, 0.85};
			if (down) return Rgba{0.3, 0.27, 0.22};
			const double t = std::clamp(v, 0.0, 1.0);
			const double cloud = std::max(0.0, fbm(u * 2, v, 91) - 0.55) * 1.2;
			return Rgba{0.3 + t * 0.45 + cloud, 0.5 + t * 0.35 + cloud, 0.88 + t * 0.08 + cloud};
		}));
		writeText(mat / "skybox" / (std::string("sky_demo01") + face + ".vmt"),
		          vmt("UnlitGeneric", "\t\"$basetexture\" \"skybox/sky_demo01" + std::string(face) + "\"\n\t\"$nofog\" \"1\"\n\t\"$ignorez\" \"1\"\n"));
	}
	writeText(mat / "demo/water001.vmt",
	          vmt("Water", "\t\"$normalmap\" \"demo/water001_normal\"\n\t\"$forcecheap\" \"1\"\n\t\"$fogenable\" \"1\"\n"
	                       "\t\"$fogcolor\" \"{30 50 50}\"\n\t\"$fogstart\" \"0\"\n\t\"$fogend\" \"300\"\n\t\"$surfaceprop\" \"water\"\n"));
	writeText(mat / "models/props_demo/bottle001.vmt",
	          vmt("VertexLitGeneric", "\t\"$basetexture\" \"demo/glass001\"\n\t\"$translucent\" \"1\"\n\t\"$surfaceprop\" \"glassbottle\"\n"));
	// mapbase-fidelity's shaders are installed (deploy.bat copies their .vcs files into the mod);
	// a stand-in file, enough for the editor to offer relief, water and liquid.
	writeText(f.mod / "shaders/fxc/fidelity_parallax_ps30.vcs", "stand-in for the test\n");
	writeText(mat / "demo/glass001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/glass001\"\n\t\"$translucent\" \"1\"\n\t\"$surfaceprop\" \"glass\"\n"));
	// Editor sprites (iconsprite in the FGD): a ring in the class color around a small symbol.
	struct Sprite {
		const char* name;
		Rgba color;
		int symbol;   // 0 dot, 1 square, 2 waves, 3 star
	};
	for (const auto& sprite : {Sprite{"light", {1, 0.9, 0.3}, 0}, Sprite{"light_spot", {1, 0.85, 0.2}, 0}, Sprite{"light_env", {1, 0.7, 0.2}, 3},
	                           Sprite{"logic_relay", {0.2, 0.85, 0.5}, 1}, Sprite{"ambient_generic", {0.35, 0.75, 1}, 2},
	                           Sprite{"env_soundscape", {1, 0.55, 0.15}, 2}, Sprite{"info_particle_system", {1, 0.6, 0.25}, 3},
	                           Sprite{"scripted_sequence", {1, 0.3, 1}, 1}}) {
		writeBytes(mat / "editor" / (std::string(sprite.name) + ".vtf"), makeTexture(64, [sprite](double u, double v) {
			const double dx = u - 0.5, dy = v - 0.5, r = std::sqrt(dx * dx + dy * dy);
			bool inner = false;
			switch (sprite.symbol) {
				case 0: inner = r < 0.17; break;
				case 1: inner = std::abs(dx) < 0.15 && std::abs(dy) < 0.15; break;
				case 2: inner = std::abs(std::fmod(r * 12, 2.0) - 1) < 0.35 && dx > -0.05 && r < 0.3; break;
				case 3: inner = r < 0.12 + 0.08 * std::cos(std::atan2(dy, dx) * 5); break;
			}
			if (r > 0.47) return Rgba{0, 0, 0, 0};
			if (r > 0.38) return Rgba{sprite.color.r, sprite.color.g, sprite.color.b, 1};
			if (inner) return Rgba{1, 1, 1, 1};
			return Rgba{0.12, 0.13, 0.16, 0.85};
		}, true));
		writeText(mat / "editor" / (std::string(sprite.name) + ".vmt"),
		          vmt("UnlitGeneric", "\t\"$basetexture\" \"editor/" + std::string(sprite.name) + "\"\n\t\"$translucent\" \"1\"\n"));
	}
	writeText(mat / "models/humans/male_sheet.vmt", vmt("VertexLitGeneric", "\t\"$basetexture\" \"models/humans/male_sheet\"\n"));

	// Model with animations (sequences shared through an include model, like HL2's citizens).
	// A stand-in person made of boxes (real .mdl/.vvd/.vtx files, so the viewport and the
	// model browser show it like any model of the game).
	writeStudioModel(f.mod / "models/humans/group01/male_07.mdl",
	                 makeStudioModel({{-7, -10, 0, 7, -2, 34}, {-7, 2, 0, 7, 10, 34},     // legs
	                                  {-8, -12, 34, 8, 12, 60}, {-6, -18, 36, 6, -12, 58}, {-6, 12, 36, 6, 18, 58},   // body, arms
	                                  {-5, -5, 60, 6, 5, 72}},                            // head
	                                 {"idle_subtle", "lineidle01", "lineidle02", "cower", "cower_idle"}, "models/humans/male_shared.mdl", "male_sheet",
	                                 "models/humans/"));
	// Hammer's green player start figure (info_player_start: studio("models/editor/playerstart.mdl")).
	writeBytes(mat / "models/editor/playerstart.vtf", makeTexture(32, [](double u, double v) {
		return Rgba{0.15, 0.75 - v * 0.2, 0.2 + u * 0.05};
	}));
	writeText(mat / "models/editor/playerstart.vmt", vmt("VertexLitGeneric", "\t\"$basetexture\" \"models/editor/playerstart\"\n"));
	writeStudioModel(f.mod / "models/editor/playerstart.mdl",
	                 makeStudioModel({{-7, -10, 0, 7, -2, 34}, {-7, 2, 0, 7, 10, 34}, {-8, -12, 34, 8, 12, 60}, {-6, -18, 36, 6, -12, 58},
	                                  {-6, 12, 36, 6, 18, 58}, {-5, -5, 60, 6, 5, 72}, {6, -2, 62, 12, 2, 66}},   // nose: shows the facing
	                                 {"idle"}, "", "playerstart", "models/editor/"));
	writeStudioModel(f.mod / "models/props_demo/crate01.mdl",
	                 makeStudioModel({{-24, -24, 0, 24, 24, 48}}, {"idle"}, "", "wood001", "demo/"));
	writeStudioModel(f.mod / "models/props_demo/barrel01.mdl",
	                 makeStudioModel({{-14, -14, 0, 14, 14, 44}, {-15, -15, 6, 15, 15, 10}, {-15, -15, 34, 15, 15, 38}}, {"idle"}, "", "metal001", "demo/"));
	writeBytes(f.mod / "models/humans/male_shared.mdl",
	           makeModelFile({"walk_all", "run_all", "sitchair1", "sit_ground", "lean_wall", "leanwall_idle", "Idle_Relaxed_SMG1_1", "crouch_idle"}, ""));

	// Sounds.
	writeText(f.mod / "scripts/game_sounds_manifest.txt", "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds_demo.txt\"\n}\n");
	writeText(f.mod / "scripts/game_sounds_demo.txt",
	          "\"Demo.Wind\" { \"channel\" \"CHAN_STATIC\" \"wave\" \"ambient/wind1.wav\" }\n"
	          "\"Demo.Alarm\" { \"wave\" \"ambient/alarm1.wav\" }\n\"NPC_Citizen.Hello\" { \"wave\" \"vo/hello.wav\" }\n");
	// Real WAV files (16-bit mono): a wind loop with a cue point and a short alarm beep.
	const auto writeWav = [](const std::filesystem::path& path, const std::vector<std::int16_t>& samples, std::optional<std::uint32_t> loop) {
		std::vector<std::byte> out;
		const auto put = [&](std::uint32_t v, int bytes) {
			for (int i = 0; i < bytes; ++i) out.push_back(std::byte((v >> (8 * i)) & 0xff));
		};
		const auto tag = [&](const char* t) {
			for (int i = 0; i < 4; ++i) out.push_back(std::byte(t[i]));
		};
		tag("RIFF");
		put(0, 4);
		tag("WAVE");
		tag("fmt ");
		put(16, 4);
		put(1, 2);
		put(1, 2);
		put(22050, 4);
		put(44100, 4);
		put(2, 2);
		put(16, 2);
		if (loop) {
			tag("cue ");
			put(28, 4);
			put(1, 4);
			for (const std::uint32_t v : {0u, 0u, 0x61746164u, 0u, 0u, *loop}) put(v, 4);
		}
		tag("data");
		put(std::uint32_t(samples.size() * 2), 4);
		for (const auto v : samples) put(std::uint16_t(v), 2);
		const auto size = std::uint32_t(out.size() - 8);
		for (int i = 0; i < 4; ++i) out[std::size_t(4 + i)] = std::byte((size >> (8 * i)) & 0xff);
		writeBytes(path, out);
	};
	{
		std::vector<std::int16_t> wind, alarm;
		double low = 0;
		for (int i = 0; i < 22050 * 2; ++i) {
			low = low * 0.97 + (hash(i, 7, 3) - 0.5) * 0.03;   // filtered noise
			wind.push_back(std::int16_t(low * 60000 * (0.6 + 0.4 * std::sin(i / 22050.0 * 3))));
		}
		for (int i = 0; i < 22050 / 2; ++i) {
			alarm.push_back(std::int16_t(std::sin(i * 2 * 3.14159265 * (i % 5512 < 2756 ? 880 : 660) / 22050) * 9000));
		}
		writeWav(f.mod / "sound/ambient/wind1.wav", wind, 0u);
		writeWav(f.mod / "sound/ambient/alarm1.wav", alarm, std::nullopt);
	}
	writeText(f.mod / "scripts/soundscapes_manifest.txt", "soundscapes_manifest\n{\n\t\"file\" \"scripts/soundscapes_demo.txt\"\n}\n");
	writeText(f.mod / "scripts/soundscapes_demo.txt",
	          "\"demo.outside\"\n{\n\t\"dsp\" \"0\"\n\t\"playlooping\" { \"volume\" \"0.4\" \"wave\" \"ambient/wind1.wav\" }\n"
	          "\t\"playrandom\" { \"time\" \"5,10\" \"rndwave\" { \"wave\" \"ambient/alarm1.wav\" } }\n}\n");

	// Particles (info_particle_system effect names).
	writeBytes(f.mod / "particles/demo_fire.pcf", makePcf({"demo_fire_small", "demo_smoke_column"}));
	writeBytes(f.mod / "particles/demo_sparks.pcf", makePcf({"demo_sparks"}));
	writeText(f.mod / "particles/particles_manifest.txt",
	          "particles_manifest\n{\n\t\"file\" \"!particles/demo_fire.pcf\"\n\t\"file\" \"particles/demo_sparks.pcf\"\n}\n");

	// Entity classes for the demo (appended to the mod's FGD).
	auto fgd = readFileText(f.mod / "mymod.fgd").value_or("");
	writeText(f.mod / "mymod.fgd", fgd + std::string(DEMO_FGD));

	// --- Demo map ---------------------------------------------------------------------
	MapDocument doc;
	doc.world.set("skyname", "sky_day01_01");
	const auto solid = [&](Vec3 a, Vec3 b, const char* material) -> SolidRef {
		auto [change, ref] = edit::createBlock(doc, box(a, b), material);
		return ref;
	};
	const char* BRICK = "DEMO/BRICKWALL001";
	const char* CONCRETE = "DEMO/CONCRETEWALL001";
	// Floor, walls (door gap in the south wall, window in the east wall), half roof + skybox.
	solid({-528, -528, -16}, {528, 528, 0}, "DEMO/FLOORTILE001");
	solid({-528, 512, 0}, {528, 528, 320}, BRICK);
	solid({-528, -528, 0}, {-512, 512, 320}, BRICK);
	solid({-528, -528, 0}, {-64, -512, 320}, BRICK);
	solid({64, -528, 0}, {528, -512, 320}, BRICK);
	solid({-64, -528, 128}, {64, -512, 320}, BRICK);
	solid({512, -512, 0}, {528, -128, 320}, CONCRETE);
	solid({512, 128, 0}, {528, 512, 320}, CONCRETE);
	solid({512, -128, 0}, {528, 128, 96}, CONCRETE);
	solid({512, -128, 224}, {528, 128, 320}, CONCRETE);
	solid({-528, -528, 320}, {528, 0, 336}, CONCRETE);
	solid({-528, 0, 320}, {528, 528, 336}, "TOOLS/TOOLSSKYBOX");
	// Shapes from the block tool: an arch over the doorway, round pillars, stairs, a rounded ledge.
	const auto shape = [&](Vec3 a, Vec3 b, ShapeOptions options, const char* material) {
		auto [change, refs] = edit::createShape(doc, box(a, b), options, material);
		return refs;
	};
	ShapeOptions arch;
	arch.kind = ShapeOptions::Kind::Arch;
	arch.sides = 8;
	arch.arc = 180;
	arch.wallWidth = 24;
	arch.startAngle = 0;
	arch.wallWidth = 16;
	// Arch in the top of the doorway: built lying flat, then stood up (turned around the X axis).
	auto archRefs = shape({-64, 0, 512}, {64, 128, 528}, arch, CONCRETE);
	Selection archSelection;
	archSelection.solids = archRefs;
	(void)edit::rotate(doc, archSelection, Mat3::rotation(0, 90), {0, 0, 0}, true);
	ShapeOptions pillar;
	pillar.kind = ShapeOptions::Kind::Cylinder;
	pillar.sides = 16;
	for (const double y : {-64.0, 160.0}) {
		shape({232, y, 0}, {280, y + 48, 320}, pillar, "DEMO/CONCRETEWALL001");
	}
	ShapeOptions stairs;
	stairs.kind = ShapeOptions::Kind::Stairs;
	stairs.steps = 6;
	shape({-512, -160, 0}, {-416, 32, 96}, stairs, CONCRETE);
	ShapeOptions spike;
	spike.kind = ShapeOptions::Kind::Spike;
	spike.sides = 4;
	shape({-200, 380, 0}, {-120, 460, 96}, spike, "DEMO/METAL001");
	auto ledge = solid({-512, 400, 0}, {-400, 512, 48}, CONCRETE);
	if (auto* s = doc.findSolid(ledge)) {
		int top = 0, south = 0;
		for (const auto& side : s->sides) {
			const auto n = side.plane().normal;
			if (n.z > 0.9) top = side.id;
			if (n.y < -0.9) south = side.id;
		}
		if (auto change = edit::roundEdge(doc, {ledge, top}, {ledge, south}, 16, 4)) {
			(void)change;
		}
	}
	// Pillars and crates.
	std::vector<SolidRef> details;
	for (const double x : {-256.0, 256.0}) {
		details.push_back(solid({x - 24, -280, 0}, {x + 24, -232, 320}, "DEMO/METAL001"));
	}
	solid({-448, 320, 0}, {-384, 384, 64}, "DEMO/WOOD001");
	solid({-440, 328, 64}, {-392, 376, 112}, "DEMO/WOOD001");
	solid({-360, 300, 0}, {-296, 364, 64}, "BRICK/BRICKWAL001");   // deliberate typo (Problems: autofix)
	// Glass window.
	auto glass = solid({516, -128, 96}, {524, 128, 224}, "DEMO/GLASS001");
	(void)glass;

	// Terrain: displacement on top of a thin block in the north-east.
	auto terrain = solid({64, 64, 0}, {448, 448, 8}, "DEMO/BLEND_GRASS_DIRT");
	if (auto* s = doc.findSolid(terrain)) {
		const auto geometry = buildBrushGeometry(*s);
		for (auto& side : s->sides) {
			if (side.plane().normal.z < 0.9) {
				continue;
			}
			constexpr int power = 3, n = (1 << power) + 1;
			auto dispinfo = KvNode::makeBlock("dispinfo");
			dispinfo.set("power", std::to_string(power));
			dispinfo.set("startposition", "[64 64 8]");
			dispinfo.set("flags", "0");
			dispinfo.set("elevation", "0");
			dispinfo.set("subdiv", "0");
			auto normals = KvNode::makeBlock("normals"), distances = KvNode::makeBlock("distances"), alphas = KvNode::makeBlock("alphas");
			for (int i = 0; i < n; ++i) {
				std::string nr, dr, ar;
				for (int j = 0; j < n; ++j) {
					const double x = double(i) / (n - 1) - 0.5, y = double(j) / (n - 1) - 0.5;
					const double h = std::max(0.0, 70 * std::exp(-(x * x + y * y) * 9) + 14 * std::sin(i * 1.3) * std::cos(j * 0.9));
					nr += "0 0 1 ";
					dr += formatNumber(std::round(h)) + " ";
					ar += formatNumber(std::round(std::clamp(h * 3.5, 0.0, 255.0))) + " ";
				}
				const auto row = "row" + std::to_string(i);
				normals.set(row, std::string(trim(nr)));
				distances.set(row, std::string(trim(dr)));
				alphas.set(row, std::string(trim(ar)));
			}
			dispinfo.children.push_back(normals);
			dispinfo.children.push_back(distances);
			dispinfo.children.push_back(alphas);
			side.extra.push_back(dispinfo);
		}
		(void)geometry;
	}

	// Brush entities.
	{
		Selection sel;
		sel.solids = details;
		(void)edit::tieToEntity(doc, sel, "func_detail", nullptr);
		auto door = solid({-64, -520, 0}, {64, -512, 128}, "DEMO/WOOD001");
		Selection doorSel;
		doorSel.solids.push_back(door);
		auto [c, doorId] = edit::tieToEntity(doc, doorSel, "func_door", nullptr);
		doc.findEntity(doorId)->set("targetname", "door_01");
		doc.findEntity(doorId)->set("speed", "100");
		auto trig = solid({-96, -480, 0}, {96, -400, 128}, "TOOLS/TOOLSTRIGGER");
		Selection trigSel;
		trigSel.solids.push_back(trig);
		auto [c2, triggerId] = edit::tieToEntity(doc, trigSel, "trigger_once", nullptr);
		auto* trigger = doc.findEntity(triggerId);
		trigger->set("targetname", "trigger_door");
		trigger->connections.push_back({"OnTrigger", "door_01", "Open", "", 0, -1, ','});
		trigger->connections.push_back({"OnTrigger", "alarm_sound", "PlaySound", "", 1.5, 1, ','});   // missing target
		trigger->connections.push_back({"OnTrigger", "relay_start", "Trigger", "", 0, -1, ','});
	}

	// Point entities.
	const auto place = [&](const char* cls, Vec3 origin) -> Entity& {
		auto [change, id] = edit::placeEntity(doc, cls, origin, nullptr);
		return *doc.findEntity(id);
	};
	place("info_player_start", {-380, -380, 0}).set("angles", "0 45 0");
	auto& sun = place("light_environment", {0, 256, 300});
	sun.set("angles", "0 35 0");
	sun.set("pitch", "-50");
	sun.set("_light", "255 236 205 450");
	sun.set("_ambient", "140 165 210 70");
	auto& warm = place("light", {-300, -160, 220});
	warm.set("_light", "255 180 110 260");
	auto& cold = place("light", {300, -360, 140});
	cold.set("_light", "110 160 255 200");
	auto& spot = place("light_spot", {0, -440, 300});
	spot.set("_light", "255 245 220 500");
	spot.set("angles", "0 90 0");
	spot.set("pitch", "-75");
	spot.set("_inner_cone", "25");
	spot.set("_cone", "40");
	auto& citizen = place("npc_citizen", {-120, 40, 0});
	citizen.set("targetname", "citizen_01");
	citizen.set("angles", "0 -60 0");
	citizen.set("model", "models/humans/group01/male_07.mdl");
	citizen.set("spawnflags", "4");
	auto& relay = place("logic_relay", {-200, -440, 32});
	relay.set("targetname", "relay_start");
	relay.connections.push_back({"OnTrigger", "citizen_01", "StartPatroling", "", 0, -1, ','});   // typo (autofix)
	auto& wind = place("ambient_generic", {300, 300, 200});
	wind.set("targetname", "wind");
	wind.set("message", "Demo.Wind");
	place("lihgt", {200, 200, 260}).set("_light", "255 255 255 100");   // misspelled class (autofix)
	for (const auto& [model, origin, yaw] : {std::tuple{"models/props_demo/crate01.mdl", Vec3{-430, 120, 0}, 15},
	                                         std::tuple{"models/props_demo/crate01.mdl", Vec3{-430, 120, 48}, 40},
	                                         std::tuple{"models/props_demo/barrel01.mdl", Vec3{-360, 190, 0}, 0},
	                                         std::tuple{"models/props_demo/barrel01.mdl", Vec3{400, -420, 0}, 0}}) {
		auto& prop = place("prop_static", origin);
		prop.set("model", model);
		prop.set("angles", "0 " + std::to_string(yaw) + " 0");
	}

	writeText(f.mod / "mapsrc/demo.vmf", doc.serialize());
}

std::vector<std::byte> makeModelFile(const std::vector<std::string>& sequences, const std::string& include, const std::string& texture,
                                     const std::string& cdmaterials) {
	std::vector<std::byte> data(408, std::byte{0});
	const auto put32 = [&](std::size_t offset, std::int32_t value) { std::memcpy(data.data() + offset, &value, 4); };
	const auto putString = [&](const std::string& s) {
		const auto offset = data.size();
		for (const char c : s) {
			data.push_back(std::byte(c));
		}
		data.push_back(std::byte{0});
		return offset;
	};
	std::memcpy(data.data(), "IDST", 4);
	put32(4, 48);
	std::memcpy(data.data() + 12, "test/model.mdl", 14);
	const std::size_t seqIndex = data.size();
	data.resize(data.size() + 212 * sequences.size(), std::byte{0});
	put32(188, static_cast<std::int32_t>(sequences.size()));
	put32(192, static_cast<std::int32_t>(seqIndex));
	for (std::size_t i = 0; i < sequences.size(); ++i) {
		const auto base = seqIndex + 212 * i;
		const auto name = putString(sequences[i]);
		const auto activity = putString(i == 0 ? "ACT_IDLE" : "");
		put32(base + 4, static_cast<std::int32_t>(name - base));
		put32(base + 8, static_cast<std::int32_t>(activity - base));
	}
	const std::size_t texIndex = data.size();
	data.resize(data.size() + 64, std::byte{0});
	const auto texName = putString(texture);
	put32(texIndex, static_cast<std::int32_t>(texName - texIndex));
	put32(204, 1);
	put32(208, static_cast<std::int32_t>(texIndex));
	const std::size_t cdIndex = data.size();
	data.resize(data.size() + 4, std::byte{0});
	put32(cdIndex, static_cast<std::int32_t>(putString(cdmaterials)));
	put32(212, 1);
	put32(216, static_cast<std::int32_t>(cdIndex));
	put32(308, static_cast<std::int32_t>(putString("flesh")));
	if (!include.empty()) {
		const std::size_t groupIndex = data.size();
		data.resize(data.size() + 8, std::byte{0});
		put32(groupIndex + 4, static_cast<std::int32_t>(putString(include) - groupIndex));
		put32(336, 1);
		put32(340, static_cast<std::int32_t>(groupIndex));
	}
	return data;
}

StudioModelFiles makeStudioModel(const std::vector<std::array<float, 6>>& boxes, const std::vector<std::string>& sequences,
                                 const std::string& include, const std::string& texture, const std::string& cdmaterials) {
	StudioModelFiles out;
	// Vertices and triangles of the boxes (4 vertices and 2 triangles per side, counter-clockwise from outside).
	struct V {
		float p[3], n[3], uv[2];
	};
	std::vector<V> verts;
	std::vector<std::uint16_t> indices;
	for (const auto& b : boxes) {
		const float lo[3] = {b[0], b[1], b[2]}, hi[3] = {b[3], b[4], b[5]};
		for (int axis = 0; axis < 3; ++axis) {
			for (int side = 0; side < 2; ++side) {
				const int u = (axis + 1) % 3, v = (axis + 2) % 3;
				const auto base = static_cast<std::uint16_t>(verts.size());
				const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
				for (const auto& c : corners) {
					V vert{};
					vert.p[axis] = side ? hi[axis] : lo[axis];
					vert.p[u] = c[0] ? hi[u] : lo[u];
					vert.p[v] = c[1] ? hi[v] : lo[v];
					vert.n[axis] = side ? 1.f : -1.f;
					vert.uv[0] = c[0];
					vert.uv[1] = 1 - c[1];
					verts.push_back(vert);
				}
				if (side) {
					indices.insert(indices.end(), {base, std::uint16_t(base + 1), std::uint16_t(base + 2), base, std::uint16_t(base + 2), std::uint16_t(base + 3)});
				} else {
					indices.insert(indices.end(), {base, std::uint16_t(base + 2), std::uint16_t(base + 1), base, std::uint16_t(base + 3), std::uint16_t(base + 2)});
				}
			}
		}
	}
	const auto put32 = [](std::vector<std::byte>& data, std::size_t offset, std::int32_t value) { std::memcpy(data.data() + offset, &value, 4); };
	const auto putFloat = [](std::vector<std::byte>& data, std::size_t offset, float value) { std::memcpy(data.data() + offset, &value, 4); };
	const auto n = static_cast<std::int32_t>(verts.size());

	// .mdl: header + sequences/texture (makeModelFile), then skin table, body part, model, mesh.
	auto& mdl = out.mdl;
	mdl = makeModelFile(sequences, include, texture, cdmaterials);
	float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
	for (const auto& v : verts) {
		for (int k = 0; k < 3; ++k) {
			lo[k] = std::min(lo[k], v.p[k]);
			hi[k] = std::max(hi[k], v.p[k]);
		}
	}
	for (int k = 0; k < 3; ++k) {
		putFloat(mdl, 104 + k * 4, lo[k]);   // hull_min
		putFloat(mdl, 116 + k * 4, hi[k]);   // hull_max
	}
	const std::size_t skins = mdl.size();
	mdl.resize(mdl.size() + 4, std::byte{0});   // one family, one reference: texture 0
	put32(mdl, 220, 1);
	put32(mdl, 224, 1);
	put32(mdl, 228, static_cast<std::int32_t>(skins));
	const std::size_t part = mdl.size();
	mdl.resize(mdl.size() + 16 + 148 + 116, std::byte{0});
	const std::size_t model = part + 16, mesh = model + 148;
	put32(mdl, 232, 1);
	put32(mdl, 236, static_cast<std::int32_t>(part));
	put32(mdl, part + 4, 1);                                            // nummodels
	put32(mdl, part + 8, 1);                                            // base
	put32(mdl, part + 12, static_cast<std::int32_t>(model - part));     // modelindex
	put32(mdl, model + 72, 1);                                          // nummeshes
	put32(mdl, model + 76, static_cast<std::int32_t>(mesh - model));    // meshindex
	put32(mdl, model + 80, n);                                          // numvertices
	put32(mdl, model + 84, 0);                                          // vertexindex
	put32(mdl, mesh + 0, 0);                                            // material (skin reference)
	put32(mdl, mesh + 4, static_cast<std::int32_t>(model - mesh));      // modelindex
	put32(mdl, mesh + 8, n);                                            // numvertices
	put32(mdl, mesh + 12, 0);                                           // vertexoffset

	// .vvd: header, vertices (48 bytes), tangents (16 bytes).
	auto& vvd = out.vvd;
	vvd.assign(64 + verts.size() * 64, std::byte{0});
	std::memcpy(vvd.data(), "IDSV", 4);
	put32(vvd, 4, 4);
	put32(vvd, 12, 1);
	for (int lod = 0; lod < 8; ++lod) put32(vvd, 16 + lod * 4, n);
	put32(vvd, 52, 64);
	put32(vvd, 56, 64);
	put32(vvd, 60, static_cast<std::int32_t>(64 + verts.size() * 48));
	for (std::size_t i = 0; i < verts.size(); ++i) {
		const auto base = 64 + i * 48;
		putFloat(vvd, base, 1.f);   // weight of bone 0
		vvd[base + 15] = std::byte{1};
		for (int k = 0; k < 3; ++k) {
			putFloat(vvd, base + 16 + k * 4, verts[i].p[k]);
			putFloat(vvd, base + 28 + k * 4, verts[i].n[k]);
		}
		putFloat(vvd, base + 40, verts[i].uv[0]);
		putFloat(vvd, base + 44, verts[i].uv[1]);
	}

	// .dx90.vtx (packed structs): header 36, body part 8, model 8, LOD 12, mesh 9, strip group 25,
	// vertices 9, indices 2, one triangle-list strip 27.
	auto& vtx = out.vtx;
	const std::size_t bodyPart = 36, vmodel = 44, lod = 52, vmesh = 64, group = 73, vertices = 98;
	const std::size_t indexStart = vertices + verts.size() * 9, strip = indexStart + indices.size() * 2;
	vtx.assign(strip + 27, std::byte{0});
	put32(vtx, 0, 7);
	put32(vtx, 4, 24);
	put32(vtx, 20, 1);                                              // numLODs
	put32(vtx, 28, 1);                                              // numBodyParts
	put32(vtx, 32, static_cast<std::int32_t>(bodyPart));
	put32(vtx, bodyPart, 1);
	put32(vtx, bodyPart + 4, static_cast<std::int32_t>(vmodel - bodyPart));
	put32(vtx, vmodel, 1);
	put32(vtx, vmodel + 4, static_cast<std::int32_t>(lod - vmodel));
	put32(vtx, lod, 1);
	put32(vtx, lod + 4, static_cast<std::int32_t>(vmesh - lod));
	put32(vtx, vmesh, 1);
	put32(vtx, vmesh + 4, static_cast<std::int32_t>(group - vmesh));
	put32(vtx, group, n);
	put32(vtx, group + 4, static_cast<std::int32_t>(vertices - group));
	put32(vtx, group + 8, static_cast<std::int32_t>(indices.size()));
	put32(vtx, group + 12, static_cast<std::int32_t>(indexStart - group));
	put32(vtx, group + 16, 1);
	put32(vtx, group + 20, static_cast<std::int32_t>(strip - group));
	for (std::size_t i = 0; i < verts.size(); ++i) {
		const auto id = static_cast<std::uint16_t>(i);
		vtx[vertices + i * 9 + 3] = std::byte{1};
		std::memcpy(vtx.data() + vertices + i * 9 + 4, &id, 2);
	}
	std::memcpy(vtx.data() + indexStart, indices.data(), indices.size() * 2);
	put32(vtx, strip, static_cast<std::int32_t>(indices.size()));
	put32(vtx, strip + 4, 0);
	put32(vtx, strip + 8, n);
	vtx[strip + 18] = std::byte{1};                                 // STRIP_IS_TRILIST
	return out;
}

void writeStudioModel(const std::filesystem::path& mdlPath, const StudioModelFiles& files) {
	auto stem = mdlPath;
	stem.replace_extension();
	writeBytes(mdlPath, files.mdl);
	writeBytes(stem.string() + ".vvd", files.vvd);
	writeBytes(stem.string() + ".dx90.vtx", files.vtx);
}

std::vector<std::byte> makePcf(const std::vector<std::string>& systems) {
	// Binary DMX, encoding version 2 (as Source 2013 writes .pcf files): header line ending in
	// "\n\0", string table (u16 count), elements (type as u16 string index, name, 16-byte id),
	// then the attributes of every element (none here).
	std::vector<std::byte> out;
	const auto put = [&](const void* data, std::size_t size) {
		const auto* bytes = static_cast<const std::byte*>(data);
		out.insert(out.end(), bytes, bytes + size);
	};
	const auto putString = [&](const std::string& text) { put(text.c_str(), text.size() + 1); };
	const std::string header = "<!-- dmx encoding binary 2 format pcf 1 -->\n";
	put(header.data(), header.size());
	out.push_back(std::byte{0});
	const std::vector<std::string> strings{"DmElement", "DmeParticleSystemDefinition"};
	const auto count = static_cast<std::uint16_t>(strings.size());
	put(&count, 2);
	for (const auto& text : strings) putString(text);
	const auto elements = static_cast<std::int32_t>(systems.size() + 1);
	put(&elements, 4);
	const auto element = [&](std::uint16_t type, const std::string& name, unsigned char id) {
		put(&type, 2);
		putString(name);
		std::array<std::byte, 16> uuid{};
		uuid[0] = std::byte{id};
		put(uuid.data(), uuid.size());
	};
	element(0, "untitled", 0);
	for (std::size_t i = 0; i < systems.size(); ++i) {
		element(1, systems[i], static_cast<unsigned char>(i + 1));
	}
	const std::int32_t noAttributes = 0;
	for (std::int32_t i = 0; i < elements; ++i) put(&noAttributes, 4);
	return out;
}

} // namespace sme::test
