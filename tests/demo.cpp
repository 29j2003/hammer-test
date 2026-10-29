// demo.cpp - Demo content for screenshots and manual testing: procedural textures, a model
// with animations, sound scripts, extra entity classes and a demo map (room with lights,
// window, pillars, terrain, an NPC, triggers and a few deliberate mistakes).

#include "fixture.h"

#include <sme/brush.h>
#include <sme/doors.h>
#include <sme/editing.h>
#include <sme/map.h>
#include <sme/mod_menu.h>
#include <sme/studio_anim.h>
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

@PointClass base(env_soundscape) iconsprite("editor/env_soundscape.vmt") color(255 128 0) = env_soundscape_triggerable : "A soundscape that trigger_soundscape brushes switch to."
[
]

@SolidClass base(Targetname) = trigger_soundscape : "Switches to the named env_soundscape_triggerable while a player is inside."
[
	soundscape(target_destination) : "Soundscape" : "" : "The env_soundscape_triggerable to use."
]

@PointClass base(Targetname) iconsprite("editor/ambient_generic.vmt") color(80 200 255) = ambient_generic : "Plays and controls a single sound."
[
	message(sound) : "Sound Name" : "" : "Name of the sound script entry (or .wav file) to play."
	health(integer) : "Volume" : 10 : "Volume from 0 to 10."
	input PlaySound(void) : "Starts the sound."
	input StopSound(void) : "Stops the sound."
]

@PointClass color(200 0 0) = infodecal : "A decal (a texture with see-through parts) the engine puts on the surface at its position."
[
	texture(decal) : "Texture"
]

@PointClass color(200 0 200) = info_overlay : "A texture put on brush faces by the map compiler; it follows the faces it lists."
[
	material(material) : "Material"
	sides(sidelist) : "Brush faces"
	RenderOrder(integer) : "Render order" : 0
	StartU(float) : "U start" : "0.0"
	EndU(float) : "U end" : "1.0"
	StartV(float) : "V start" : "0.0"
	EndV(float) : "V end" : "1.0"
	BasisOrigin(string) : "Overlay basis origin (read-only)"
	BasisU(string) : "Overlay basis U (read-only)"
	BasisV(string) : "Overlay basis V (read-only)"
	BasisNormal(string) : "Overlay basis normal (read-only)"
	uv0(string) : "Overlay corner 0 (read-only)"
	uv1(string) : "Overlay corner 1 (read-only)"
	uv2(string) : "Overlay corner 2 (read-only)"
	uv3(string) : "Overlay corner 3 (read-only)"
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

@BaseClass base(Targetname) = Door
[
	speed(integer) : "Speed" : 100 : "How fast the door moves."
	wait(integer) : "Delay Before Reset (-1 stay)" : 4 : "Seconds until the door closes again; -1 stays open."
	lip(integer) : "Lip" : 0 : "Units the door keeps sticking out when open."
	noise1(sound) : "Start Sound" : "" : "Sound while it starts to move."
	noise2(sound) : "Stop Sound" : "" : "Sound when it stops."
	startclosesound(sound) : "Start Close Sound" : "" : "Sound while it starts to close."
	closesound(sound) : "Stop Close Sound" : "" : "Sound when it is fully closed."
	locked_sound(sound) : "Locked Sound" : "" : "Sound when a locked door is used."
	spawnflags(flags) =
	[
		32 : "Toggle" : 0
		256 : "Use Opens" : 1
		1024 : "Touch Opens" : 1
		2048 : "Starts locked" : 0
	]
	input Open(void) : "Open the door."
	input Close(void) : "Close the door."
	input Toggle(void) : "Open if closed, close if open."
	input Lock(void) : "Lock the door."
	input Unlock(void) : "Unlock the door."
	output OnOpen(void) : "Fired when the door starts to open."
	output OnFullyOpen(void) : "Fired when the door is fully open."
	output OnClose(void) : "Fired when the door starts to close."
	output OnFullyClosed(void) : "Fired when the door is fully closed."
]

@SolidClass base(Door) = func_door : "A brush that moves between two positions, like a door."
[
	movedir(angle) : "Move Direction (Pitch Yaw Roll)" : "0 0 0" : "The direction the door moves when it opens."
]

@SolidClass base(Door) = func_door_rotating : "A brush door that turns around its origin (the hinge)."
[
	distance(integer) : "Distance" : 90 : "Degrees the door turns."
	spawnflags(flags) =
	[
		2 : "Reverse Dir" : 0
		16 : "One-way" : 0
	]
]

@SolidClass base(Targetname) = func_button : "A brush the player can press."
[
	speed(integer) : "Speed" : 5 : "How fast the button moves in."
	wait(integer) : "Delay Before Reset (-1 stay)" : 3 : "Seconds until it can be pressed again."
	spawnflags(flags) =
	[
		1 : "Don't move" : 0
		1024 : "Use Activates" : 1
	]
	input Lock(void) : "Lock the button."
	input Unlock(void) : "Unlock the button."
	input Press(void) : "Press the button."
	output OnPressed(void) : "Fired when the button is pressed."
	output OnUseLocked(void) : "Fired when the button is used while locked."
]

@PointClass base(Targetname, Angles) studioprop() = prop_door_rotating : "A door model that turns around its hinge (the model's origin)."
[
	model(studio) : "World Model" : "models/props_demo/door01_left.mdl"
	slavename(target_destination) : "Slave name" : "" : "Doors that open and close with this one."
	hardware(choices) : "Hardware Type" : 1 =
	[
		0 : "<None>"
		1 : "Lever"
		2 : "Push bar"
		3 : "Keypad"
	]
	distance(float) : "Rotation Distance (deg)" : 90
	speed(integer) : "Speed" : 200
	returndelay(integer) : "Delay Before close (-1 stay open)" : -1
	opendir(choices) : "Open Direction" : 0 =
	[
		0 : "Both directions"
		1 : "Forward only"
		2 : "Backward only"
	]
	soundopenoverride(sound) : "Fully Open Sound"
	soundcloseoverride(sound) : "Fully Closed Sound"
	soundmoveoverride(sound) : "Moving Sound"
	soundlockedoverride(sound) : "Locked Sound"
	spawnflags(flags) =
	[
		2048 : "Starts locked" : 0
		8192 : "Use closes" : 1
	]
	input Open(void) : "Open the door."
	input Close(void) : "Close the door."
	input Toggle(void) : "Open if closed, close if open."
	input Lock(void) : "Lock the door."
	input Unlock(void) : "Unlock the door."
	output OnOpen(void) : "Fired when the door starts to open."
	output OnFullyOpen(void) : "Fired when the door is fully open."
	output OnFullyClosed(void) : "Fired when the door is fully closed."
]

@SolidClass base(Targetname) = func_areaportal : "A portal between two areas: closed, the engine does not draw what is behind it."
[
	target(target_destination) : "Name of Linked Door" : "" : "The door that opens and closes this portal."
	StartOpen(choices) : "Initial State" : 1 =
	[
		0 : "Closed"
		1 : "Open"
	]
	input Open(void) : "Open the portal."
	input Close(void) : "Close the portal."
	input Toggle(void) : "Toggle the portal."
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
	// mapbase-fidelity's shaders are installed (their .vcs files and the shader DLL in the mod's
	// bin folder); stand-in files, enough for the editor to offer relief, water and liquid. (The
	// game knows the shaders only from the DLL: without it they would be wireframes.)
	writeText(f.mod / "shaders/fxc/fidelity_parallax_ps30.vcs", "stand-in for the test\n");
	writeText(f.mod / "bin/game_shader_dx6.dll", "stand-in for the test\n");
	writeText(mat / "demo/glass001.vmt", vmt("LightmappedGeneric", "\t\"$basetexture\" \"demo/glass001\"\n\t\"$translucent\" \"1\"\n\t\"$surfaceprop\" \"glass\"\n"));
	// A decal: a yellow arrow on transparent ground (128 x 128 texture, $decalscale 0.5 = 64 units).
	writeBytes(mat / "decals/demo_arrow.vtf", makeTexture(128, [](double u, double v) {
		const double x = u - 0.5, y = v - 0.5;
		const bool head = y < 0 && y > -0.42 && std::abs(x) < (y + 0.42) * 0.9;
		const bool shaft = y >= 0 && y < 0.4 && std::abs(x) < 0.12;
		return (head || shaft) ? Rgba{1.0, 0.82, 0.1, 1} : Rgba{0, 0, 0, 0};
	}, true));
	writeText(mat / "decals/demo_arrow.vmt",
	          vmt("LightmappedGeneric", "\t\"$basetexture\" \"decals/demo_arrow\"\n\t\"$decal\" \"1\"\n\t\"$translucent\" \"1\"\n\t\"$decalscale\" \"0.5\"\n"));
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
	// It has a skeleton like HL2's (ValveBiped bones) and animations of its own; the walk cycles
	// come from the shared model, as in the game.
	const std::vector<StudioModelSpec::Bone> skeleton = {
		{"ValveBiped.Bip01_Pelvis", -1, {0, 0, 34}, {0, 0, 0}},     {"ValveBiped.Bip01_Spine", 0, {0, 0, 0}, {0, 0, 0}},
		{"ValveBiped.Bip01_Head1", 1, {0, 0, 26}, {0, 0, 0}},       {"ValveBiped.Bip01_L_UpperArm", 1, {0, 15, 24}, {0, 0, 0}},
		{"ValveBiped.Bip01_R_UpperArm", 1, {0, -15, 24}, {0, 0, 0}}, {"ValveBiped.Bip01_L_Thigh", 0, {0, 6, 0}, {0, 0, 0}},
		{"ValveBiped.Bip01_R_Thigh", 0, {0, -6, 0}, {0, 0, 0}}};
	// One frame: every bone at rest, some turned (bone index -> pitch about the side axis, roll).
	const auto pose = [&skeleton](std::initializer_list<std::tuple<int, float, float>> turns, float drop = 0) {
		std::vector<std::array<float, 6>> frame;
		for (const auto& bone : skeleton) frame.push_back({bone.pos[0], bone.pos[1], bone.pos[2], 0, 0, 0});
		frame[0][2] -= drop;
		for (const auto& [bone, pitch, roll] : turns) {
			frame[std::size_t(bone)][3] = roll;
			frame[std::size_t(bone)][4] = pitch;
		}
		return frame;
	};
	const auto cycle = [&pose](int frames, float legs, float arms, float drop = 0) {
		std::vector<std::vector<std::array<float, 6>>> out;
		for (int i = 0; i < frames; ++i) {
			const float s = std::sin(float(i) / float(frames - 1) * 2.f * float(std::numbers::pi));
			out.push_back(pose({{5, legs * s, 0}, {6, -legs * s, 0}, {3, -arms * s, 0}, {4, arms * s, 0}}, drop * std::abs(s)));
		}
		return out;
	};
	{
		StudioModelSpec citizen;
		citizen.groups.push_back({"body", {{"male_07",
		                                    {{-7, -10, 0, 7, -2, 34}, {-7, 2, 0, 7, 10, 34},                                          // legs
		                                     {-8, -12, 34, 8, 12, 60}, {-6, -18, 36, 6, -12, 58}, {-6, 12, 36, 6, 18, 58},   // body, arms
		                                     {-5, -5, 60, 6, 5, 72}}}}});                                                     // head
		citizen.boxBones = {6, 5, 1, 4, 3, 2};
		citizen.bones = skeleton;
		citizen.textures = {"male_sheet"};
		citizen.cdmaterials = "models/humans/";
		citizen.include = "models/humans/male_shared.mdl";
		citizen.animations = {
			{"idle_subtle", "ACT_IDLE", 15, true, {pose({}), pose({{1, 0.05f, 0}, {2, -0.08f, 0}}), pose({})}},
			{"lineidle01", "ACT_IDLE", 15, true, {pose({}), pose({{2, 0.3f, 0}}), pose({})}},
			{"lineidle02", "", 15, true, {pose({}), pose({{3, -0.4f, 0}, {4, -0.4f, 0}}), pose({})}},
			{"cower", "ACT_COWER", 10, false, {pose({}), pose({{1, 0.6f, 0}, {3, -2.2f, 0}, {4, -2.2f, 0}, {5, -1.2f, 0}, {6, -1.2f, 0}}, 14)}},
			{"cower_idle", "ACT_COWER", 10, true, {pose({{1, 0.6f, 0}, {3, -2.2f, 0}, {4, -2.2f, 0}, {5, -1.2f, 0}, {6, -1.2f, 0}}, 14)}},
		};
		writeStudioModel(f.mod / "models/humans/group01/male_07.mdl", makeStudioModel(citizen));
	}
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
	// A door model like HL2's: the origin is the hinge, the door runs along +Y from it.
	writeStudioModel(f.mod / "models/props_demo/door01_left.mdl",
	                 makeStudioModel({{-1, 0, 0, 1, 54, 104}, {1, 44, 46, 4, 50, 50}}, {"idle"}, "", "wood001", "demo/"));
	writeStudioModel(f.mod / "models/props_demo/barrel01.mdl",
	                 makeStudioModel({{-14, -14, 0, 14, 14, 44}, {-15, -15, 6, 15, 15, 10}, {-15, -15, 34, 15, 15, 38}}, {"idle"}, "", "metal001", "demo/"));
	{
		// A supply crate with a lid body group, two skins and prop settings (the Models page shows them).
		StudioModelSpec crate;
		crate.groups.push_back({"crate", {{"crate", {{-20, -14, 0, 20, 14, 26}}}}});
		crate.groups.push_back({"lid", {{"lid_closed", {{-21, -15, 26, 21, 15, 30}}}, {"blank", {}}}});
		crate.textures = {"wood001", "metal001"};
		crate.cdmaterials = "demo/";
		crate.staticProp = true;
		crate.mass = 40;
		crate.keyValues = "\"prop_data\" { \"base\" \"Wooden.Medium\" }";
		writeStudioModel(f.mod / "models/props_demo/supply_crate.mdl", makeStudioModel(crate));
		writeBytes(f.mod / "models/props_demo/supply_crate.phy", std::vector<std::byte>(16, std::byte{0}));
	}
	// A glass bottle of the base game (not the mod's own): game enhancements give it liquid by
	// themselves, as mapbase-fidelity's deploy.bat does for the game's bottles.
	writeText(f.sdkBase / "hl2/materials/models/props_junk/glassbottle01a.vmt",
	          vmt("VertexLitGeneric", "\t\"$basetexture\" \"demo/glass001\"\n\t\"$translucent\" \"1\"\n\t\"$surfaceprop\" \"glassbottle\"\n"));
	writeStudioModel(f.sdkBase / "hl2/models/props_junk/glassbottle01a.mdl",
	                 makeStudioModel({{-3, -3, 0, 3, 3, 8}, {-1.5f, -1.5f, 8, 1.5f, 1.5f, 13}}, {"idle"}, "", "glassbottle01a", "models/props_junk/"));
	{
		// The shared animations: a skeleton without triangles (like HL2's male_shared.mdl).
		StudioModelSpec shared;
		shared.bones = skeleton;
		shared.textures = {"combine_soldier_sheet"};
		shared.cdmaterials = "models/combine_soldier/";
		shared.animations = {
			{"walk_all", "ACT_WALK", 30, true, cycle(17, 0.5f, 0.35f)},
			{"run_all", "ACT_RUN", 30, true, cycle(13, 0.9f, 0.7f, 2)},
			{"sitchair1", "ACT_BUSY_SIT_CHAIR", 10, true, {pose({{5, -1.5f, 0}, {6, -1.5f, 0}}, 16)}},
			{"sit_ground", "ACT_BUSY_SIT_GROUND", 10, true, {pose({{5, -1.5f, 0}, {6, -1.5f, 0}}, 30)}},
			{"lean_wall", "", 10, false, {pose({}), pose({{1, -0.2f, 0}})}},
			{"leanwall_idle", "", 10, true, {pose({{1, -0.2f, 0}}), pose({{1, -0.22f, 0}})}},
			{"Idle_Relaxed_SMG1_1", "ACT_IDLE_RELAXED", 15, true, {pose({{3, -0.5f, 0}}), pose({{3, -0.55f, 0}})}},
			{"crouch_idle", "ACT_CROUCHIDLE", 10, true, {pose({{5, -1.0f, 0}, {6, -1.0f, 0}}, 12)}},
		};
		writeStudioModel(f.mod / "models/humans/male_shared.mdl", makeStudioModel(shared));
	}

	// Sounds.
	writeText(f.mod / "scripts/game_sounds_manifest.txt", "game_sounds_manifest\n{\n\t\"precache_file\" \"scripts/game_sounds_demo.txt\"\n}\n");
	writeText(f.mod / "scripts/game_sounds_demo.txt",
	          "\"Demo.Wind\" { \"channel\" \"CHAN_STATIC\" \"wave\" \"ambient/wind1.wav\" }\n"
	          "\"Demo.Alarm\" { \"wave\" \"ambient/alarm1.wav\" }\n\"NPC_Citizen.Hello\" { \"wave\" \"vo/hello.wav\" }\n"
	          "\"Weapon_Pistol.Single\" { \"channel\" \"CHAN_WEAPON\" \"soundlevel\" \"SNDLVL_GUNFIRE\" \"wave\" \")weapons/pistol/pistol_fire2.wav\" }\n"
	          "\"Doors.Squeak\" { \"wave\" \"doors/door_squeak1.wav\" }\n"
	          "\"Doors.Move1\" { \"wave\" \"doors/door_move1.wav\" }\n\"Doors.FullClose1\" { \"wave\" \"doors/door_close1.wav\" }\n"
	          "\"Doors.Locked1\" { \"wave\" \"doors/door_locked1.wav\" }\n");
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
		// One sound for some more categories of the Sounds page: a shot, a squeaking door, a metal hit, a voice.
		std::vector<std::int16_t> shot, squeak, impact, voice;
		for (int i = 0; i < 22050 * 4 / 10; ++i) {
			shot.push_back(std::int16_t((hash(i, 3, 11) - 0.5) * 50000 * std::exp(-i / 1800.0)));
		}
		for (int i = 0; i < 22050 * 6 / 10; ++i) {
			const double t = i / 22050.0;
			squeak.push_back(std::int16_t(std::sin(2 * 3.14159265 * (500 + 400 * std::sin(t * 9)) * t) * 7000 * std::min(1.0, t * 8)));
		}
		for (int i = 0; i < 22050 / 4; ++i) {
			const double t = i / 22050.0;
			impact.push_back(std::int16_t((std::sin(2 * 3.14159265 * 1250 * t) * 0.6 + (hash(i, 5, 2) - 0.5)) * 20000 * std::exp(-t * 18)));
		}
		for (int i = 0; i < 22050 * 8 / 10; ++i) {
			const double t = i / 22050.0;
			voice.push_back(std::int16_t(std::sin(2 * 3.14159265 * 140 * t + std::sin(2 * 3.14159265 * 5 * t) * 3) * 8000 * std::sin(3.14159265 * t / 0.8)));
		}
		writeWav(f.mod / "sound/weapons/pistol/pistol_fire2.wav", shot, std::nullopt);
		writeWav(f.mod / "sound/doors/door_squeak1.wav", squeak, std::nullopt);
		writeWav(f.mod / "sound/doors/door_move1.wav", squeak, std::nullopt);
		writeWav(f.mod / "sound/doors/door_close1.wav", impact, std::nullopt);
		writeWav(f.mod / "sound/doors/door_locked1.wav", impact, std::nullopt);
		writeWav(f.mod / "sound/physics/metal/metal_box_impact_hard1.wav", impact, std::nullopt);
		writeWav(f.mod / "sound/vo/hello.wav", voice, std::nullopt);
	}
	writeText(f.mod / "scripts/soundscapes_manifest.txt", "soundscapes_manifest\n{\n\t\"file\" \"scripts/soundscapes_demo.txt\"\n}\n");
	writeText(f.mod / "scripts/soundscapes_demo.txt",
	          "\"demo.outside\"\n{\n\t\"dsp\" \"0\"\n\t\"playlooping\" { \"volume\" \"0.4\" \"wave\" \"ambient/wind1.wav\" }\n"
	          "\t\"playrandom\" { \"time\" \"5,10\" \"rndwave\" { \"wave\" \"ambient/alarm1.wav\" } }\n}\n");

	// Texts: the mod's localization (English, a German file with a missing translation) and
	// captions, UTF-16 with BOM like the game's own files.
	writeBytes(f.mod / "resource/mymod_english.txt",
	           encodeUtf16Le("\"lang\"\n{\n\t\"Language\"\t\"English\"\n\t\"Tokens\"\n\t{\n"
	                         "\t\t// Hints\n"
	                         "\t\t\"MyMod_Hint_Flashlight\"\t\"Press F for the flashlight\"\n"
	                         "\t\t\"MyMod_Sign_Welcome\"\t\"Welcome to the demo\"\n"
	                         "\t\t\"MyMod_Hint_Pad\"\t\"Press A\"\t[$X360]\n"
	                         "\t}\n}\n"));
	writeBytes(f.mod / "resource/mymod_german.txt",
	           encodeUtf16Le("\"lang\"\n{\n\t\"Language\"\t\"German\"\n\t\"Tokens\"\n\t{\n"
	                         "\t\t\"MyMod_Hint_Flashlight\"\t\"Drücke F für die Taschenlampe\"\n"
	                         "\t}\n}\n"));
	writeBytes(f.mod / "resource/closecaption_english.txt",
	           encodeUtf16Le("\"lang\"\n{\n\t\"Language\"\t\"English\"\n\t\"Tokens\"\n\t{\n"
	                         "\t\t\"Demo.Alarm\"\t\"<sfx><clr:255,90,80>[Alarm blares]\"\n"
	                         "\t\t\"NPC_Citizen.Hello\"\t\"<clr:200,220,255>Hello there! <I>Stay close.<I>\"\n"
	                         "\t}\n}\n"));

	// Particles (info_particle_system effect names).
	writeBytes(f.mod / "particles/demo_fire.pcf", makeDemoFirePcf());
	writeBytes(f.mod / "particles/demo_sparks.pcf", makeDemoSparksPcf());
	{
		// Their sprites: a soft glow (additive) and a puff of smoke.
		const auto soft = [](double u, double v, double power) {
			const double d = std::sqrt((u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5)) * 2;
			return std::pow(std::clamp(1 - d, 0.0, 1.0), power);
		};
		const auto particles = f.mod / "materials/particle";
		writeBytes(particles / "demo_flame.vtf", makeTexture(64, [&](double u, double v) {
			const double a = soft(u, v, 1.5);
			return Rgba{a, a, a, a};
		}, true));
		writeBytes(particles / "demo_glow.vtf", makeTexture(32, [&](double u, double v) {
			const double a = soft(u, v, 2.5);
			return Rgba{a, a, a, a};
		}, true));
		writeBytes(particles / "demo_smoke.vtf", makeTexture(64, [&](double u, double v) {
			const double a = soft(u, v, 1.0) * (0.75 + 0.25 * std::sin(u * 17 + v * 11));
			return Rgba{1, 1, 1, a};
		}, true));
		writeText(particles / "demo_flame.vmt", vmt("SpriteCard", "\t\"$basetexture\" \"particle/demo_flame\"\n\t\"$additive\" \"1\"\n"));
		writeText(particles / "demo_glow.vmt", vmt("SpriteCard", "\t\"$basetexture\" \"particle/demo_glow\"\n\t\"$additive\" \"1\"\n"));
		writeText(particles / "demo_smoke.vmt", vmt("SpriteCard", "\t\"$basetexture\" \"particle/demo_smoke\"\n\t\"$translucent\" \"1\"\n"));
	}
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
		// A swinging door with its hinge on the left, opened by the trigger in front of it.
		DoorSettings doorSettings;
		doorSettings.type = DoorType::Swinging;
		doorSettings.sounds = {"Doors.Move1", "", "Doors.FullClose1", "Doors.Locked1"};
		auto [c, doorIds] = edit::makeDoor(doc, doorSel, doorSettings);
		doc.findEntity(doorIds.at(0))->set("targetname", "door_01");
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

namespace {

/// The .mdl header with sequences, textures, one $cdmaterials folder, surface prop and include.
std::vector<std::byte> modelHeader(const std::vector<std::string>& sequences, const std::string& include, const std::vector<std::string>& textures,
                                   const std::string& cdmaterials);

} // namespace

std::vector<std::byte> makeModelFile(const std::vector<std::string>& sequences, const std::string& include, const std::string& texture,
                                     const std::string& cdmaterials) {
	return modelHeader(sequences, include, {texture}, cdmaterials);
}

namespace {

std::vector<std::byte> modelHeader(const std::vector<std::string>& sequences, const std::string& include, const std::vector<std::string>& textures,
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
	data.resize(data.size() + 64 * textures.size(), std::byte{0});
	for (std::size_t i = 0; i < textures.size(); ++i) {
		const auto texName = putString(textures[i]);
		put32(texIndex + 64 * i, static_cast<std::int32_t>(texName - (texIndex + 64 * i)));
	}
	put32(204, static_cast<std::int32_t>(textures.size()));
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

/// Triangles of boxes (4 vertices and 2 triangles per side, counter-clockwise from outside).
struct BoxMesh {
	struct V {
		float p[3], n[3], uv[2];
	};
	std::vector<V> verts;
	std::vector<std::uint16_t> indices;
};

BoxMesh boxMesh(const std::vector<std::array<float, 6>>& boxes) {
	BoxMesh out;
	for (const auto& b : boxes) {
		const float lo[3] = {b[0], b[1], b[2]}, hi[3] = {b[3], b[4], b[5]};
		for (int axis = 0; axis < 3; ++axis) {
			for (int side = 0; side < 2; ++side) {
				const int u = (axis + 1) % 3, v = (axis + 2) % 3;
				const auto base = static_cast<std::uint16_t>(out.verts.size());
				const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
				for (const auto& c : corners) {
					BoxMesh::V vert{};
					vert.p[axis] = side ? hi[axis] : lo[axis];
					vert.p[u] = c[0] ? hi[u] : lo[u];
					vert.p[v] = c[1] ? hi[v] : lo[v];
					vert.n[axis] = side ? 1.f : -1.f;
					vert.uv[0] = c[0];
					vert.uv[1] = 1 - c[1];
					out.verts.push_back(vert);
				}
				if (side) {
					out.indices.insert(out.indices.end(), {base, std::uint16_t(base + 1), std::uint16_t(base + 2), base, std::uint16_t(base + 2), std::uint16_t(base + 3)});
				} else {
					out.indices.insert(out.indices.end(), {base, std::uint16_t(base + 2), std::uint16_t(base + 1), base, std::uint16_t(base + 3), std::uint16_t(base + 2)});
				}
			}
		}
	}
	return out;
}

} // namespace

StudioModelFiles makeStudioModel(const std::vector<std::array<float, 6>>& boxes, const std::vector<std::string>& sequences,
                                 const std::string& include, const std::string& texture, const std::string& cdmaterials) {
	StudioModelSpec spec;
	spec.groups.push_back({"body", {{"model", boxes}}});
	spec.sequences = sequences;
	spec.include = include;
	spec.textures = {texture};
	spec.cdmaterials = cdmaterials;
	return makeStudioModel(spec);
}

namespace {

std::uint16_t floatToHalf(float value) {
	std::uint32_t bits;
	std::memcpy(&bits, &value, 4);
	const std::uint32_t sign = (bits >> 16) & 0x8000;
	const int exponent = int((bits >> 23) & 0xff) - 127 + 15;
	const std::uint32_t mantissa = bits & 0x7fffff;
	if (exponent <= 0) return std::uint16_t(sign);
	if (exponent >= 31) return std::uint16_t(sign | 0x7bff);
	return std::uint16_t(sign | (std::uint32_t(exponent) << 10) | (mantissa >> 13));
}

constexpr float ROT_SCALE = 3.14159265f / 32768.f;   // radians per step of the stored angle values
constexpr float POS_SCALE = 1.f / 64.f;              // units per step of the stored positions

/// The mstudioanim_t chain of frames [first, last] of an animation (every bone, frame values relative
/// to the first frame of the run), as the reader's ExtractAnimValue expects it.
std::vector<std::byte> encodeAnimation(const StudioModelSpec& spec, const StudioModelSpec::Animation& anim, int first, int last) {
	std::vector<std::byte> out;
	const auto put16 = [&](std::size_t at, std::int16_t v) { std::memcpy(out.data() + at, &v, 2); };
	const auto valueOf = [&](int frame, std::size_t bone, int k) -> float {
		const auto& rest = spec.bones[bone];
		if (frame < int(anim.frames.size()) && bone < anim.frames[std::size_t(frame)].size()) return anim.frames[std::size_t(frame)][bone][std::size_t(k)];
		if (anim.delta) return 0.f;   // a delta leaves the bones it does not list as they are
		return k < 3 ? rest.pos[std::size_t(k)] : rest.rot[std::size_t(k - 3)];
	};
	const int n = last - first + 1;
	for (std::size_t bone = 0; bone < spec.bones.size(); ++bone) {
		const std::size_t entry = out.size();
		const bool raw = anim.rawFirstBone && bone == 0;
		if (raw) {
			out.resize(entry + 4 + 6 + 6, std::byte{0});
			out[entry] = std::byte(bone);
			out[entry + 1] = std::byte(0x02 | 0x01);   // RAWROT | RAWPOS
			const sme::AnimQuat q = sme::angleQuaternion({valueOf(first, bone, 3), valueOf(first, bone, 4), valueOf(first, bone, 5)});
			const auto x = std::uint16_t(std::clamp(int(std::lround(q.x * 32768.f)) + 32768, 0, 65535));
			const auto y = std::uint16_t(std::clamp(int(std::lround(q.y * 32768.f)) + 32768, 0, 65535));
			const auto z = std::uint16_t(std::clamp(int(std::lround(q.z * 16384.f)) + 16384, 0, 32767) | (q.w < 0 ? 0x8000 : 0));
			std::memcpy(out.data() + entry + 4, &x, 2);
			std::memcpy(out.data() + entry + 6, &y, 2);
			std::memcpy(out.data() + entry + 8, &z, 2);
			for (int k = 0; k < 3; ++k) {
				const std::uint16_t h = floatToHalf(valueOf(first, bone, k));
				std::memcpy(out.data() + entry + 10 + k * 2, &h, 2);
			}
		} else {
			// Header, rotation offsets, position offsets, then six streams of one run each.
			const std::size_t streamBytes = 2 + 2 * std::size_t(n);
			out.resize(entry + 4 + 12 + 6 * streamBytes, std::byte{0});
			out[entry] = std::byte(bone);
			out[entry + 1] = std::byte(0x08 | 0x04 | (anim.delta ? 0x10 : 0));   // ANIMROT | ANIMPOS (| DELTA)
			// A delta's values are offsets on their own; the others count from the rest pose.
			const auto rest = anim.delta ? StudioModelSpec::Bone{} : spec.bones[bone];
			for (int k = 0; k < 6; ++k) {
				const std::size_t stream = entry + 16 + std::size_t(k) * streamBytes;
				const std::size_t pointer = entry + 4 + std::size_t(k) * 2;          // rot x y z, then pos x y z
				const std::size_t base = k < 3 ? entry + 4 : entry + 10;             // offsets count from their own pointer block
				put16(pointer, std::int16_t(stream - base));
				out[stream] = std::byte(n);       // valid
				out[stream + 1] = std::byte(n);   // total
				for (int f = 0; f < n; ++f) {
					const float v = k < 3 ? (valueOf(first + f, bone, k + 3) - rest.rot[std::size_t(k)]) / ROT_SCALE
					                      : (valueOf(first + f, bone, k - 3) - rest.pos[std::size_t(k - 3)]) / POS_SCALE;
					put16(stream + 2 + std::size_t(f) * 2, std::int16_t(std::clamp(std::lround(v), -32767L, 32767L)));
				}
			}
		}
		const auto next = std::int16_t(bone + 1 < spec.bones.size() ? out.size() - entry : 0);
		std::memcpy(out.data() + entry + 2, &next, 2);
	}
	return out;
}

} // namespace

StudioModelFiles makeStudioModel(const StudioModelSpec& spec) {
	StudioModelFiles out;
	const auto put32 = [](std::vector<std::byte>& data, std::size_t offset, std::int32_t value) { std::memcpy(data.data() + offset, &value, 4); };
	const auto putFloat = [](std::vector<std::byte>& data, std::size_t offset, float value) { std::memcpy(data.data() + offset, &value, 4); };
	const auto grow = [](std::vector<std::byte>& data, std::size_t bytes) {
		const auto at = data.size();
		data.resize(at + bytes, std::byte{0});
		return at;
	};
	const auto putString = [](std::vector<std::byte>& data, const std::string& text) {
		const auto at = data.size();
		for (const char c : text) data.push_back(std::byte(c));
		data.push_back(std::byte{0});
		return at;
	};

	// Geometry of every option, in file order (group by group).
	std::vector<std::vector<BoxMesh>> meshes;
	for (const auto& group : spec.groups) {
		auto& list = meshes.emplace_back();
		for (const auto& option : group.options) list.push_back(boxMesh(option.second));
	}

	// .mdl: header (sequences, textures), flags, mass, $keyvalues, skin table, body parts.
	auto& mdl = out.mdl;
	std::vector<std::string> sequenceNames = spec.sequences;
	if (!spec.animations.empty()) {
		sequenceNames.clear();
		for (const auto& anim : spec.animations) sequenceNames.push_back(anim.name);
	}
	mdl = modelHeader(sequenceNames, spec.include, spec.textures, spec.cdmaterials);
	float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
	for (const auto& list : meshes) {
		for (const auto& mesh : list) {
			for (const auto& v : mesh.verts) {
				for (int k = 0; k < 3; ++k) {
					lo[k] = std::min(lo[k], v.p[k]);
					hi[k] = std::max(hi[k], v.p[k]);
				}
			}
		}
	}
	for (int k = 0; k < 3; ++k) {
		putFloat(mdl, 104 + k * 4, lo[k]);   // hull_min
		putFloat(mdl, 116 + k * 4, hi[k]);   // hull_max
	}
	put32(mdl, 152, spec.staticProp ? 0x10 : 0);   // flags (STUDIOHDR_FLAGS_STATIC_PROP)
	putFloat(mdl, 328, spec.mass);
	if (!spec.keyValues.empty()) {
		const auto at = putString(mdl, spec.keyValues);
		put32(mdl, 312, static_cast<std::int32_t>(at));
		put32(mdl, 316, static_cast<std::int32_t>(spec.keyValues.size() + 1));
	}
	const auto families = static_cast<std::int32_t>(std::max<std::size_t>(1, spec.textures.size()));
	const std::size_t skins = grow(mdl, 2 * static_cast<std::size_t>(families));   // one reference per family
	for (std::int32_t f = 0; f < families; ++f) {
		const auto texture = static_cast<std::int16_t>(f);
		std::memcpy(mdl.data() + skins + 2 * static_cast<std::size_t>(f), &texture, 2);
	}
	put32(mdl, 220, 1);
	put32(mdl, 224, families);
	put32(mdl, 228, static_cast<std::int32_t>(skins));

	const std::size_t parts = grow(mdl, 16 * spec.groups.size());
	put32(mdl, 232, static_cast<std::int32_t>(spec.groups.size()));
	put32(mdl, 236, static_cast<std::int32_t>(parts));
	std::int32_t base = 1, vertexStart = 0;
	for (std::size_t g = 0; g < spec.groups.size(); ++g) {
		const auto& group = spec.groups[g];
		const std::size_t part = parts + 16 * g;
		put32(mdl, part, static_cast<std::int32_t>(putString(mdl, group.name) - part));
		const std::size_t models = grow(mdl, 148 * group.options.size());
		put32(mdl, part + 4, static_cast<std::int32_t>(group.options.size()));   // nummodels
		put32(mdl, part + 8, base);
		put32(mdl, part + 12, static_cast<std::int32_t>(models - part));
		base *= static_cast<std::int32_t>(std::max<std::size_t>(1, group.options.size()));
		for (std::size_t m = 0; m < group.options.size(); ++m) {
			const std::size_t model = models + 148 * m;
			const auto& name = group.options[m].first;
			std::memcpy(mdl.data() + model, name.data(), std::min<std::size_t>(name.size(), 63));
			const auto n = static_cast<std::int32_t>(meshes[g][m].verts.size());
			put32(mdl, model + 80, n);                    // numvertices
			put32(mdl, model + 84, vertexStart * 48);     // vertexindex (bytes into the .vvd vertices)
			vertexStart += n;
			if (n == 0) continue;
			const std::size_t mesh = grow(mdl, 116);
			put32(mdl, model + 72, 1);                                          // nummeshes
			put32(mdl, model + 76, static_cast<std::int32_t>(mesh - model));    // meshindex
			put32(mdl, mesh + 0, 0);                                            // material (skin reference)
			put32(mdl, mesh + 4, static_cast<std::int32_t>(model - mesh));      // modelindex
			put32(mdl, mesh + 8, n);                                            // numvertices
			put32(mdl, mesh + 12, 0);                                           // vertexoffset
		}
	}

	// Skeleton: mstudiobone_t (216 bytes) with the matrix from model space at rest into the bone.
	if (!spec.bones.empty()) {
		const std::size_t bones = grow(mdl, 216 * spec.bones.size());
		put32(mdl, 156, static_cast<std::int32_t>(spec.bones.size()));
		put32(mdl, 160, static_cast<std::int32_t>(bones));
		std::vector<sme::BoneMatrix> world;
		for (std::size_t i = 0; i < spec.bones.size(); ++i) {
			const auto& bone = spec.bones[i];
			const std::size_t at = bones + 216 * i;
			put32(mdl, at, static_cast<std::int32_t>(putString(mdl, bone.name) - at));
			put32(mdl, at + 4, bone.parent);
			const sme::AnimQuat q = sme::angleQuaternion({bone.rot[0], bone.rot[1], bone.rot[2]});
			const float quat[4] = {q.x, q.y, q.z, q.w};
			for (int k = 0; k < 3; ++k) {
				putFloat(mdl, at + 32 + k * 4, bone.pos[std::size_t(k)]);
				putFloat(mdl, at + 60 + k * 4, bone.rot[std::size_t(k)]);
				putFloat(mdl, at + 72 + k * 4, POS_SCALE);
				putFloat(mdl, at + 84 + k * 4, ROT_SCALE);
			}
			for (int k = 0; k < 4; ++k) putFloat(mdl, at + 44 + k * 4, quat[k]);
			const sme::BoneMatrix local = sme::quaternionMatrix(q, {bone.pos[0], bone.pos[1], bone.pos[2]});
			world.push_back(bone.parent >= 0 ? sme::concat(world[std::size_t(bone.parent)], local) : local);
			const sme::BoneMatrix poseToBone = sme::invertRigid(world.back());
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 4; ++c) putFloat(mdl, at + 96 + std::size_t(r * 4 + c) * 4, poseToBone.m[r][c]);
			}
		}
	}
	// Animations: mstudioanimdesc_t (100 bytes) per sequence, the sequences pointing at them.
	if (!spec.animations.empty() && !spec.bones.empty()) {
		const std::size_t seqIndex = static_cast<std::size_t>([&] { std::int32_t v; std::memcpy(&v, mdl.data() + 192, 4); return v; }());
		const std::size_t descs = grow(mdl, 100 * spec.animations.size());
		put32(mdl, 180, static_cast<std::int32_t>(spec.animations.size()));
		put32(mdl, 184, static_cast<std::int32_t>(descs));
		std::vector<std::byte>& ani = out.ani;
		bool usesAni = false;
		for (std::size_t i = 0; i < spec.animations.size(); ++i) {
			const auto& anim = spec.animations[i];
			const std::size_t desc = descs + 100 * i;
			const std::size_t seq = seqIndex + 212 * i;
			const int frames = std::max<int>(1, static_cast<int>(anim.frames.size()));
			put32(mdl, desc + 4, static_cast<std::int32_t>(putString(mdl, "@" + anim.name) - desc));
			putFloat(mdl, desc + 8, anim.fps);
			put32(mdl, desc + 16, frames);
			put32(mdl, desc + 12, anim.delta ? 0x0004 : 0);
			// The sequence: flags, activity, its blend (this animation, or the listed ones).
			put32(mdl, seq + 12, (anim.loop ? 1 : 0) | (anim.delta ? 0x0004 : 0));
			put32(mdl, seq + 8, static_cast<std::int32_t>(putString(mdl, anim.activity) - seq));
			const std::vector<int> blendOf = anim.blend.empty() ? std::vector<int>{int(i)} : anim.blend;
			const std::size_t blend = grow(mdl, 2 * blendOf.size() + 2);
			for (std::size_t k = 0; k < blendOf.size(); ++k) {
				const auto index = static_cast<std::int16_t>(blendOf[k]);
				std::memcpy(mdl.data() + blend + 2 * k, &index, 2);
			}
			put32(mdl, seq + 56, static_cast<std::int32_t>(blendOf.size()));
			put32(mdl, seq + 60, static_cast<std::int32_t>(blend - seq));
			put32(mdl, seq + 68, static_cast<std::int32_t>(blendOf.size()));
			put32(mdl, seq + 72, 1);
			// Where a run of frames goes: into the .mdl (block 0, from the desc) or the .ani (block 1).
			const auto store = [&](int first, int last) -> std::pair<std::int32_t, std::int32_t> {
				const auto bytes = encodeAnimation(spec, anim, first, last);
				if (anim.inAni) {
					usesAni = true;
					const std::size_t at = ani.size();
					ani.insert(ani.end(), bytes.begin(), bytes.end());
					return {1, static_cast<std::int32_t>(at)};
				}
				const std::size_t at = grow(mdl, bytes.size());
				std::memcpy(mdl.data() + at, bytes.data(), bytes.size());
				return {0, static_cast<std::int32_t>(at - desc)};
			};
			if (anim.sectionFrames > 0 && frames > anim.sectionFrames) {
				// Sections of sectionFrames + 1 frames each, and the last frame on its own.
				const int count = frames / anim.sectionFrames + 2;
				const std::size_t sections = grow(mdl, 8 * static_cast<std::size_t>(count));
				put32(mdl, desc + 80, static_cast<std::int32_t>(sections - desc));
				put32(mdl, desc + 84, anim.sectionFrames);
				for (int section = 0; section < count; ++section) {
					const bool lastOnly = section == count - 1;
					const int first = lastOnly ? frames - 1 : std::min(section * anim.sectionFrames, frames - 1);
					const int last = lastOnly ? frames - 1 : std::min(first + anim.sectionFrames, frames - 1);
					const auto [block, at] = store(first, last);
					put32(mdl, sections + 8 * static_cast<std::size_t>(section), block);
					put32(mdl, sections + 8 * static_cast<std::size_t>(section) + 4, at);
				}
			} else {
				const auto [block, at] = store(0, frames - 1);
				put32(mdl, desc + 52, block);
				put32(mdl, desc + 56, at);
			}
		}
		if (usesAni) {
			// Block 0 is the .mdl itself; block 1 is the whole .ani.
			const std::size_t blocks = grow(mdl, 16);
			put32(mdl, blocks + 8, 0);
			put32(mdl, blocks + 12, static_cast<std::int32_t>(ani.size()));
			put32(mdl, 348, static_cast<std::int32_t>(putString(mdl, spec.aniName)));
			put32(mdl, 352, 2);
			put32(mdl, 356, static_cast<std::int32_t>(blocks));
		}
	}

	// .vvd: header, vertices (48 bytes), tangents (16 bytes).
	auto& vvd = out.vvd;
	const auto total = static_cast<std::size_t>(vertexStart);
	vvd.assign(64 + total * 64, std::byte{0});
	std::memcpy(vvd.data(), "IDSV", 4);
	put32(vvd, 4, 4);
	put32(vvd, 12, 1);
	for (int lod = 0; lod < 8; ++lod) put32(vvd, 16 + lod * 4, vertexStart);
	put32(vvd, 52, 64);
	put32(vvd, 56, 64);
	put32(vvd, 60, static_cast<std::int32_t>(64 + total * 48));
	std::size_t written = 0;
	std::size_t boxesBefore = 0;   // boxes of the options written so far (24 vertices per box)
	for (const auto& list : meshes) {
		for (const auto& mesh : list) {
			for (std::size_t vi = 0; vi < mesh.verts.size(); ++vi) {
				const auto& v = mesh.verts[vi];
				const auto at = 64 + written * 48;
				const std::size_t box = boxesBefore + vi / 24;
				const int bone = box < spec.boxBones.size() ? spec.boxBones[box] : 0;
				putFloat(vvd, at, 1.f);   // the whole weight on one bone
				vvd[at + 12] = std::byte(bone);
				vvd[at + 15] = std::byte{1};
				for (int k = 0; k < 3; ++k) {
					putFloat(vvd, at + 16 + k * 4, v.p[k]);
					putFloat(vvd, at + 28 + k * 4, v.n[k]);
				}
				putFloat(vvd, at + 40, v.uv[0]);
				putFloat(vvd, at + 44, v.uv[1]);
				++written;
			}
			boxesBefore += mesh.verts.size() / 24;
		}
	}

	// .dx90.vtx (packed structs): header 36, body parts 8, models 8, LOD 12, mesh 9, strip group
	// 25, vertices 9, indices 2, one triangle-list strip 27.
	auto& vtx = out.vtx;
	vtx.assign(36, std::byte{0});
	put32(vtx, 0, 7);
	put32(vtx, 4, 24);
	put32(vtx, 20, 1);                                              // numLODs
	put32(vtx, 28, static_cast<std::int32_t>(spec.groups.size()));  // numBodyParts
	const std::size_t vparts = grow(vtx, 8 * spec.groups.size());
	put32(vtx, 32, static_cast<std::int32_t>(vparts));
	for (std::size_t g = 0; g < spec.groups.size(); ++g) {
		const std::size_t vpart = vparts + 8 * g;
		const std::size_t vmodels = grow(vtx, 8 * spec.groups[g].options.size());
		put32(vtx, vpart, static_cast<std::int32_t>(spec.groups[g].options.size()));
		put32(vtx, vpart + 4, static_cast<std::int32_t>(vmodels - vpart));
		for (std::size_t m = 0; m < spec.groups[g].options.size(); ++m) {
			const auto& mesh = meshes[g][m];
			const std::size_t vmodel = vmodels + 8 * m;
			const std::size_t lod = grow(vtx, 12);
			put32(vtx, vmodel, 1);
			put32(vtx, vmodel + 4, static_cast<std::int32_t>(lod - vmodel));
			if (mesh.verts.empty()) continue;   // blank: a LOD without meshes
			const std::size_t vmesh = grow(vtx, 9);
			put32(vtx, lod, 1);
			put32(vtx, lod + 4, static_cast<std::int32_t>(vmesh - lod));
			const std::size_t group = grow(vtx, 25);
			put32(vtx, vmesh, 1);
			put32(vtx, vmesh + 4, static_cast<std::int32_t>(group - vmesh));
			const std::size_t vertices = grow(vtx, 9 * mesh.verts.size());
			const std::size_t indexStart = grow(vtx, 2 * mesh.indices.size());
			const std::size_t strip = grow(vtx, 27);
			const auto n = static_cast<std::int32_t>(mesh.verts.size());
			put32(vtx, group, n);
			put32(vtx, group + 4, static_cast<std::int32_t>(vertices - group));
			put32(vtx, group + 8, static_cast<std::int32_t>(mesh.indices.size()));
			put32(vtx, group + 12, static_cast<std::int32_t>(indexStart - group));
			put32(vtx, group + 16, 1);
			put32(vtx, group + 20, static_cast<std::int32_t>(strip - group));
			for (std::size_t i = 0; i < mesh.verts.size(); ++i) {
				const auto id = static_cast<std::uint16_t>(i);
				vtx[vertices + i * 9 + 3] = std::byte{1};
				std::memcpy(vtx.data() + vertices + i * 9 + 4, &id, 2);
			}
			std::memcpy(vtx.data() + indexStart, mesh.indices.data(), mesh.indices.size() * 2);
			put32(vtx, strip, static_cast<std::int32_t>(mesh.indices.size()));
			put32(vtx, strip + 4, 0);
			put32(vtx, strip + 8, n);
			vtx[strip + 18] = std::byte{1};                                 // STRIP_IS_TRILIST
		}
	}
	return out;
}

void writeStudioModel(const std::filesystem::path& mdlPath, const StudioModelFiles& files) {
	auto stem = mdlPath;
	stem.replace_extension();
	writeBytes(mdlPath, files.mdl);
	writeBytes(stem.string() + ".vvd", files.vvd);
	writeBytes(stem.string() + ".dx90.vtx", files.vtx);
	if (!files.ani.empty()) writeBytes(stem.string() + ".ani", files.ani);
}

namespace {

/// Writes binary DMX, encoding version 2 (as Source 2013 writes .pcf files): header line ending
/// in "\n\0", string table (u16 count) with the type and attribute names, the elements (type as
/// u16 string index, name inline, 16-byte id), then the attributes of every element (name as u16
/// string index, type byte, value; strings inline).
class PcfWriter {
public:
	int element(const std::string& type, const std::string& name) {
		m_elements.push_back({type, name, {}});
		return static_cast<int>(m_elements.size()) - 1;
	}
	void add(int element, const std::string& key, std::uint8_t type, std::vector<std::byte> data) {
		m_elements[static_cast<std::size_t>(element)].attributes.push_back({key, type, std::move(data)});
	}
	template<typename T>
	static std::vector<std::byte> raw(const T& value) {
		std::vector<std::byte> out(sizeof(T));
		std::memcpy(out.data(), &value, sizeof(T));
		return out;
	}
	void integer(int e, const std::string& key, std::int32_t v) { add(e, key, 2, raw(v)); }
	void number(int e, const std::string& key, float v) { add(e, key, 3, raw(v)); }
	void text(int e, const std::string& key, const std::string& v) {
		std::vector<std::byte> data(v.size() + 1);
		std::memcpy(data.data(), v.c_str(), v.size() + 1);
		add(e, key, 5, std::move(data));
	}
	void color(int e, const std::string& key, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
		add(e, key, 8, {std::byte{r}, std::byte{g}, std::byte{b}, std::byte{a}});
	}
	void vector(int e, const std::string& key, float x, float y, float z) {
		const std::array<float, 3> v{x, y, z};
		add(e, key, 10, raw(v));
	}
	void reference(int e, const std::string& key, std::int32_t target) { add(e, key, 1, raw(target)); }
	void references(int e, const std::string& key, const std::vector<int>& targets) {
		auto data = raw(static_cast<std::uint32_t>(targets.size()));
		for (const int t : targets) {
			const auto one = raw(static_cast<std::int32_t>(t));
			data.insert(data.end(), one.begin(), one.end());
		}
		add(e, key, 15, std::move(data));
	}

	/// A part of a system ("emit_continuously" ...) with float values.
	int function(const std::string& functionName, const std::vector<std::pair<std::string, float>>& values = {}) {
		const int e = element("DmeParticleOperator", functionName);
		text(e, "functionName", functionName);
		for (const auto& [key, value] : values) number(e, key, value);
		return e;
	}

	[[nodiscard]] std::vector<std::byte> bytes() const {
		std::vector<std::string> strings;
		const auto index = [&](const std::string& text) {
			const auto it = std::find(strings.begin(), strings.end(), text);
			if (it != strings.end()) return static_cast<std::uint16_t>(it - strings.begin());
			strings.push_back(text);
			return static_cast<std::uint16_t>(strings.size() - 1);
		};
		for (const auto& e : m_elements) {
			(void)index(e.type);
			for (const auto& a : e.attributes) (void)index(a.key);
		}
		std::vector<std::byte> out;
		const auto put = [&](const void* data, std::size_t size) {
			const auto* bytes = static_cast<const std::byte*>(data);
			out.insert(out.end(), bytes, bytes + size);
		};
		const std::string header = "<!-- dmx encoding binary 2 format pcf 1 -->\n";
		put(header.data(), header.size());
		out.push_back(std::byte{0});
		const auto count = static_cast<std::uint16_t>(strings.size());
		put(&count, 2);
		for (const auto& text : strings) put(text.c_str(), text.size() + 1);
		const auto elements = static_cast<std::int32_t>(m_elements.size());
		put(&elements, 4);
		for (std::size_t i = 0; i < m_elements.size(); ++i) {
			const auto type = index(m_elements[i].type);
			put(&type, 2);
			put(m_elements[i].name.c_str(), m_elements[i].name.size() + 1);
			std::array<std::byte, 16> uuid{};
			uuid[0] = std::byte(i & 0xff);
			uuid[1] = std::byte((i >> 8) & 0xff);
			put(uuid.data(), uuid.size());
		}
		for (const auto& e : m_elements) {
			const auto attributes = static_cast<std::int32_t>(e.attributes.size());
			put(&attributes, 4);
			for (const auto& a : e.attributes) {
				const auto key = index(a.key);
				put(&key, 2);
				put(&a.type, 1);
				put(a.data.data(), a.data.size());
			}
		}
		return out;
	}

private:
	struct Attribute {
		std::string key;
		std::uint8_t type;
		std::vector<std::byte> data;
	};
	struct Element {
		std::string type, name;
		std::vector<Attribute> attributes;
	};
	std::vector<Element> m_elements;
};

/// One system: its own values and parts (lists of element indices).
struct PcfSystem {
	std::string name, material;
	int maxParticles = 200;
	std::vector<int> emitters, initializers, operators, renderers, children;
};

int addSystem(PcfWriter& w, const PcfSystem& system) {
	const int e = w.element("DmeParticleSystemDefinition", system.name);
	w.text(e, "material", system.material);
	w.integer(e, "max_particles", system.maxParticles);
	w.number(e, "radius", 5);
	w.color(e, "color", 255, 255, 255);
	w.references(e, "emitters", system.emitters);
	w.references(e, "initializers", system.initializers);
	w.references(e, "operators", system.operators);
	w.references(e, "renderers", system.renderers);
	w.references(e, "children", system.children);
	return e;
}

int childOf(PcfWriter& w, int system, float delay) {
	const int c = w.element("DmeParticleChild", "child");
	w.reference(c, "child", system);
	w.number(c, "delay", delay);
	return c;
}

} // namespace

std::vector<std::byte> makePcf(const std::vector<std::string>& systems) {
	PcfWriter w;
	const int root = w.element("DmElement", "untitled");
	std::vector<int> definitions;
	for (const auto& name : systems) {
		PcfSystem system;
		system.name = name;
		system.material = "particle/demo_glow";
		system.emitters = {w.function("emit_continuously", {{"emission_rate", 20}})};
		system.initializers = {w.function("Lifetime Random", {{"lifetime_min", 1}, {"lifetime_max", 1}})};
		system.operators = {w.function("Movement Basic"), w.function("Lifespan Decay")};
		system.renderers = {w.function("render_animated_sprites")};
		definitions.push_back(addSystem(w, system));
	}
	w.references(root, "particleSystemDefinitions", definitions);
	return w.bytes();
}

std::vector<std::byte> makeDemoFirePcf() {
	PcfWriter w;
	const int root = w.element("DmElement", "untitled");
	// Smoke rising from the fire (a child of it, starting a bit later).
	PcfSystem smoke;
	smoke.name = "demo_smoke_column";
	smoke.material = "particle/demo_smoke";
	smoke.emitters = {w.function("emit_continuously", {{"emission_rate", 12}})};
	smoke.initializers = {w.function("Lifetime Random", {{"lifetime_min", 2}, {"lifetime_max", 3}}),
	                      w.function("Radius Random", {{"radius_min", 10}, {"radius_max", 18}}),
	                      w.function("Alpha Random", {{"alpha_min", 60}, {"alpha_max", 120}}),
	                      w.function("Position Within Sphere Random", {{"distance_max", 6}}),
	                      w.function("Rotation Random"),
	                      w.function("Rotation Speed Random", {{"rotation_speed_random_min", -20}, {"rotation_speed_random_max", 20}})};
	const int smokeVelocity = w.function("Velocity Random");
	w.vector(smokeVelocity, "speed_in_local_coordinate_system_min", -6, -6, 40);
	w.vector(smokeVelocity, "speed_in_local_coordinate_system_max", 6, 6, 60);
	smoke.initializers.push_back(smokeVelocity);
	const int smokeColor = w.function("Color Random");
	w.color(smokeColor, "color1", 90, 90, 95);
	w.color(smokeColor, "color2", 130, 125, 120);
	smoke.initializers.push_back(smokeColor);
	const int smokeMove = w.function("Movement Basic", {{"drag", 0.02f}});
	w.vector(smokeMove, "gravity", 6, 0, 0);
	smoke.operators = {smokeMove,
	                   w.function("Lifespan Decay"),
	                   w.function("Alpha Fade In Random", {{"fade_in_time_min", 0.2f}, {"fade_in_time_max", 0.3f}}),
	                   w.function("Alpha Fade Out Random", {{"fade_out_time_min", 0.4f}, {"fade_out_time_max", 0.6f}}),
	                   w.function("Radius Scale", {{"radius_start_scale", 0.6f}, {"radius_end_scale", 2.2f}}),
	                   w.function("Rotation Basic")};
	smoke.renderers = {w.function("render_animated_sprites")};
	const int smokeSystem = addSystem(w, smoke);

	PcfSystem fire;
	fire.name = "demo_fire_small";
	fire.material = "particle/demo_flame";
	fire.emitters = {w.function("emit_continuously", {{"emission_rate", 40}})};
	fire.initializers = {w.function("Lifetime Random", {{"lifetime_min", 0.5f}, {"lifetime_max", 1.0f}}),
	                     w.function("Radius Random", {{"radius_min", 6}, {"radius_max", 11}}),
	                     w.function("Position Within Sphere Random", {{"distance_max", 8}})};
	const int fireVelocity = w.function("Velocity Random");
	w.vector(fireVelocity, "speed_in_local_coordinate_system_min", -4, -4, 30);
	w.vector(fireVelocity, "speed_in_local_coordinate_system_max", 4, 4, 70);
	fire.initializers.push_back(fireVelocity);
	const int fireColor = w.function("Color Random");
	w.color(fireColor, "color1", 255, 210, 90);
	w.color(fireColor, "color2", 255, 130, 40);
	fire.initializers.push_back(fireColor);
	const int fireMove = w.function("Movement Basic", {{"drag", 0.05f}});
	w.vector(fireMove, "gravity", 0, 0, 30);
	const int fireFade = w.function("Color Fade", {{"fade_start_time", 0.3f}, {"fade_end_time", 1}});
	w.color(fireFade, "color_fade", 150, 40, 10);
	fire.operators = {fireMove, w.function("Lifespan Decay"),
	                  w.function("Alpha Fade and Decay", {{"start_alpha", 0}, {"end_fade_in_time", 0.15f}, {"start_fade_out_time", 0.5f}}),
	                  w.function("Radius Scale", {{"radius_start_scale", 1}, {"radius_end_scale", 0.3f}}), fireFade};
	fire.renderers = {w.function("render_animated_sprites")};
	fire.children = {childOf(w, smokeSystem, 0.3f)};
	const int fireSystem = addSystem(w, fire);
	w.references(root, "particleSystemDefinitions", {fireSystem, smokeSystem});
	return w.bytes();
}

std::vector<std::byte> makeDemoSparksPcf() {
	PcfWriter w;
	const int root = w.element("DmElement", "untitled");
	PcfSystem sparks;
	sparks.name = "demo_sparks";
	sparks.material = "particle/demo_glow";
	sparks.maxParticles = 120;
	sparks.emitters = {w.function("emit_instantaneously", {{"num_to_emit", 60}})};
	sparks.initializers = {w.function("Lifetime Random", {{"lifetime_min", 0.4f}, {"lifetime_max", 1.0f}}),
	                       w.function("Radius Random", {{"radius_min", 1}, {"radius_max", 2.5f}}),
	                       w.function("Position Within Sphere Random", {{"distance_max", 2}, {"speed_min", 120}, {"speed_max", 280}})};
	const int sparkColor = w.function("Color Random");
	w.color(sparkColor, "color1", 255, 240, 160);
	w.color(sparkColor, "color2", 255, 170, 60);
	sparks.initializers.push_back(sparkColor);
	const int sparkMove = w.function("Movement Basic", {{"drag", 0.03f}});
	w.vector(sparkMove, "gravity", 0, 0, -500);
	sparks.operators = {sparkMove, w.function("Lifespan Decay"),
	                    w.function("Alpha Fade Out Random", {{"fade_out_time_min", 0.3f}, {"fade_out_time_max", 0.5f}})};
	sparks.renderers = {w.function("render_sprite_trail")};
	w.references(root, "particleSystemDefinitions", {addSystem(w, sparks)});
	return w.bytes();
}

} // namespace sme::test
