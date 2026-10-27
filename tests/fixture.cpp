// fixture.cpp - Fake Steam installation for tests (see fixture.h).

#include "fixture.h"

#include <sme/text.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <vpkpp/format/VPK.h>
#include <vtfpp/VTF.h>

namespace sme::test {

namespace {

std::string quoted(const std::filesystem::path& path) {
	return "\"" + displayPath(path) + "\"";
}

void makeVpk(const std::filesystem::path& dirFile, const std::vector<std::pair<std::string, std::vector<std::byte>>>& entries) {
	std::filesystem::create_directories(dirFile.parent_path());
	auto vpk = vpkpp::VPK::create(displayPath(dirFile));
	if (!vpk) {
		throw std::runtime_error("cannot create VPK " + displayPath(dirFile));
	}
	for (const auto& [path, data] : entries) {
		vpk->addEntry(path, std::vector<std::byte>(data));
	}
	if (!vpk->bake()) {
		throw std::runtime_error("cannot bake VPK " + displayPath(dirFile));
	}
}

std::vector<std::byte> bytes(std::string_view text) {
	std::vector<std::byte> out(text.size());
	std::memcpy(out.data(), text.data(), text.size());
	return out;
}

constexpr std::string_view BASE_FGD = R"FGD(// Test base FGD
@BaseClass = Targetname
[
	targetname(target_source) : "Name" : : "The name that other entities refer to this entity by."
	input Kill(void) : "Removes this entity from the world."
	output OnUser1(void) : "Fired in response to FireUser1 input."
]
@BaseClass = Angles
[
	angles(angle) : "Pitch Yaw Roll (Y Z X)" : "0 0 0"
]
@BaseClass base(Targetname) = Parentname
[
	parentname(target_destination) : "Parent"
]
)FGD";

constexpr std::string_view HL2_FGD = R"FGD(@include "base.fgd"

@PointClass base(Targetname, Angles) studio("models/editor/playerstart.mdl") size(-16 -16 0, 16 16 72) = info_player_start : "Player spawn point."
[
	spawnflags(flags) =
	[
		1 : "Master (Has priority if multiple info_player_starts exist)" : 0
	]
]

@PointClass base(Targetname, Parentname) iconsprite("editor/light.vmt") color(255 255 0) = light : "An invisible omnidirectional light source."
[
	_light(color255) : "Brightness" : "255 255 255 200"
	_quadratic_attn(string) : "Quadratic" : "1"
	style(choices) : "Appearance" : 0 =
	[
		0 : "Normal"
		10 : "Fluorescent flicker"
	]
	spawnflags(flags) =
	[
		1 : "Initially dark" : 0
	]
	input TurnOn(void) : "Turn the light on."
	input TurnOff(void) : "Turn the light off."
]

@PointClass base(Angles) iconsprite("editor/light_env.vmt") = light_environment : "Sets the color and angle of the light from the sun and sky."
[
	pitch(integer) : "Pitch" : 0
	_light(color255) : "Brightness" : "255 255 255 200"
	_ambient(color255) : "Ambient" : "255 255 255 20"
]

@SolidClass = func_detail : "Brushes that do not block visibility."
[
]

@SolidClass base(Targetname, Parentname) = trigger_once : "A trigger volume that fires once."
[
	output OnTrigger(void) : "Fired whenever the trigger is activated."
]
)FGD";

constexpr std::string_view MOD_FGD = R"FGD(@PointClass base(Targetname) = logic_mymod : "Mod-only logic entity."
[
	mode(choices) : "Mode" : 1 = [ 1 : "Fast" 2 : "Slow" ]
]
)FGD";

std::string gameInfo(std::string_view game, int appId, std::string_view searchPaths) {
	return std::string("\"GameInfo\"\n{\n\tgame \"") + std::string(game) + "\"\n\ttype singleplayer_only\n\tFileSystem\n\t{\n\t\tSteamAppId " +
	       std::to_string(appId) + "\n\t\tSearchPaths\n\t\t{\n" + std::string(searchPaths) + "\t\t}\n\t}\n}\n";
}

std::string vmt(std::string_view shader, std::string_view baseTexture, std::string_view extra = {}) {
	return "\"" + std::string(shader) + "\"\n{\n\t\"$basetexture\" \"" + std::string(baseTexture) + "\"\n" + std::string(extra) + "}\n";
}

} // namespace

Fixture::~Fixture() {
	if (owned && !root.empty()) {
		std::error_code ec;
		std::filesystem::remove_all(root, ec);
	}
}

void writeText(const std::filesystem::path& path, std::string_view text) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	if (!out) {
		throw std::runtime_error("cannot write " + displayPath(path));
	}
}

std::vector<std::byte> makeVtf(unsigned size, unsigned char r, unsigned char g, unsigned char b, bool dxt) {
	std::vector<std::byte> pixels(static_cast<std::size_t>(size) * size * 4);
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			const bool dark = ((x / std::max(1u, size / 4)) + (y / std::max(1u, size / 4))) % 2 == 1;
			auto* p = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
			p[0] = std::byte(dark ? r / 2 : r);
			p[1] = std::byte(dark ? g / 2 : g);
			p[2] = std::byte(dark ? b / 2 : b);
			p[3] = std::byte(255);
		}
	}
	vtfpp::VTF::CreationOptions options;
	options.outputFormat = dxt ? vtfpp::ImageFormat::DXT1 : vtfpp::ImageFormat::RGBA8888;
	options.computeReflectivity = false;
	const auto vtf = vtfpp::VTF::create(pixels, vtfpp::ImageFormat::RGBA8888, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size), options);
	return vtf.bake();
}

void createFixture(Fixture& f, const std::filesystem::path& dir) {
	static std::atomic<int> counter{0};
	if (dir.empty()) {
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		f.root = std::filesystem::temp_directory_path() / ("sme-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
	} else {
		f.root = dir;
		f.owned = false;
	}
	f.steam = f.root / "Steam";
	f.library2 = f.root / "Library2";
	const auto hl2App = f.steam / "steamapps/common/Half-Life 2";
	f.hl2 = hl2App / "hl2";
	f.sdkBase = f.library2 / "steamapps/common/Source SDK Base 2013 Singleplayer";
	f.mod = f.steam / "steamapps/sourcemods/mymod";

	// Steam: two libraries; the second is written with escaped backslashes like Steam does on Windows.
	std::string library2Escaped;
	for (const char c : displayPath(f.library2)) {
		library2Escaped += c == '/' ? std::string("\\\\") : std::string(1, c);
	}
	writeText(f.steam / "steamapps/libraryfolders.vdf",
	          "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t" + quoted(f.steam) + "\n\t\t\"apps\" { \"220\" \"1\" }\n\t}\n"
	          "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + library2Escaped + "\"\n\t}\n}\n");
	writeText(f.steam / "steamapps/appmanifest_220.acf",
	          "\"AppState\"\n{\n\t\"appid\"\t\t\"220\"\n\t\"name\"\t\t\"Half-Life 2\"\n\t\"installdir\"\t\t\"Half-Life 2\"\n}\n");
	writeText(f.library2 / "steamapps/appmanifest_243730.acf",
	          "\"AppState\"\n{\n\t\"appid\"\t\t\"243730\"\n\t\"name\"\t\t\"Source SDK Base 2013 Singleplayer\"\n"
	          "\t\"installdir\"\t\t\"Source SDK Base 2013 Singleplayer\"\n}\n");

	// Half-Life 2 (search paths as in the real gameinfo.txt, trimmed).
	writeText(f.hl2 / "gameinfo.txt", gameInfo("HALF-LIFE 2", 220,
		"\t\t\tgame+mod\t\t\thl2/custom/*\n"
		"\t\t\tgame_lv\t\t\t\thl2/hl2_lv.vpk\n"
		"\t\t\tgame+mod+mod_write+default_write_path\t\t|gameinfo_path|.\n"
		"\t\t\tgamebin\t\t\t\t|gameinfo_path|bin\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2/hl2_textures.vpk\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2/hl2_misc.vpk\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2\n"
		"\t\t\tplatform\t\t\t|all_source_engine_paths|platform\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2_x360 [$X360]\n"));
	makeVpk(f.hl2 / "hl2_textures_dir.vpk", {
		{"materials/concrete/wall01.vmt", bytes(vmt("LightmappedGeneric", "concrete/wall01", "\t\"$surfaceprop\" \"concrete\"\n"))},
		{"materials/concrete/wall01.vtf", makeVtf(64, 160, 160, 150)},
		{"materials/concrete/floor01.vmt", bytes(vmt("LightmappedGeneric", "concrete/floor01"))},
		{"materials/concrete/floor01.vtf", makeVtf(64, 110, 100, 90, true)},
		{"materials/dev/dev_measuregeneric01.vmt", bytes(vmt("LightmappedGeneric", "dev/dev_measuregeneric01"))},
		{"materials/dev/dev_measuregeneric01.vtf", makeVtf(128, 230, 140, 40)},
		{"materials/tools/toolsnodraw.vmt", bytes("\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsnodraw\"\n\t\"%compilenodraw\" 1\n}\n")},
		{"materials/tools/toolsnodraw.vtf", makeVtf(32, 230, 200, 30)},
		{"materials/tools/toolsskybox.vmt", bytes("\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsskybox\"\n\t\"%compilesky\" 1\n}\n")},
		{"materials/tools/toolsskybox.vtf", makeVtf(32, 120, 180, 230)},
		{"materials/tools/toolstrigger.vmt", bytes("\"LightmappedGeneric\"\n{\n\t\"%tooltexture\" \"tools/toolstrigger\"\n\t\"$basetexture\" \"tools/toolstrigger\"\n\t\"%compiletrigger\" 1\n}\n")},
		{"materials/tools/toolstrigger.vtf", makeVtf(32, 220, 120, 40)},
	});
	makeVpk(f.hl2 / "hl2_misc_dir.vpk", {
		{"scripts/test.txt", bytes("from misc vpk")},
		{"materials/concrete/wall02.vmt", bytes(vmt("LightmappedGeneric", "concrete/wall01"))},
	});
	// Loose file in hl2/ that overrides the VPK version (|gameinfo_path|. comes first).
	writeText(f.hl2 / "materials/Concrete/Wall01.vmt", vmt("LightmappedGeneric", "concrete/wall01", "\t\"$surfaceprop\" \"loose\"\n"));
	writeText(f.hl2 / "custom/mypack/materials/custom/override.vmt", vmt("UnlitGeneric", "concrete/floor01"));
	writeText(hl2App / "bin/base.fgd", BASE_FGD);
	writeText(hl2App / "bin/halflife2.fgd", HL2_FGD);
	writeText(hl2App / "bin/hl2mp.fgd", "@include \"base.fgd\"\n@PointClass = info_player_deathmatch : \"MP spawn\" []\n");
	writeText(hl2App / "platform/resource/readme.txt", "platform");
	writeText(hl2App / "hl2.exe", "not a real exe");

	// Source SDK Base 2013 Singleplayer: hl2 content plus Hammer++ in bin/.
	writeText(f.sdkBase / "hl2/gameinfo.txt", gameInfo("Half-Life 2", 243730,
		"\t\t\tgame+mod+mod_write+default_write_path\t\t|gameinfo_path|.\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2\n"));
	makeVpk(f.sdkBase / "hl2/hl2_textures_dir.vpk", {
		{"materials/concrete/wall01.vmt", bytes(vmt("LightmappedGeneric", "concrete/wall01"))},
		{"materials/concrete/wall01.vtf", makeVtf(64, 160, 160, 150)},
		{"materials/tools/toolsnodraw.vmt", bytes("\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsnodraw\"\n\t\"%compilenodraw\" 1\n}\n")},
		{"materials/tools/toolsnodraw.vtf", makeVtf(32, 230, 200, 30)},
		{"materials/tools/toolsskybox.vmt", bytes("\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsskybox\"\n\t\"%compilesky\" 1\n}\n")},
		{"materials/tools/toolsskybox.vtf", makeVtf(32, 120, 180, 230)},
		{"materials/tools/toolstrigger.vmt", bytes("\"LightmappedGeneric\"\n{\n\t\"%tooltexture\" \"tools/toolstrigger\"\n\t\"$basetexture\" \"tools/toolstrigger\"\n\t\"%compiletrigger\" 1\n}\n")},
		{"materials/tools/toolstrigger.vtf", makeVtf(32, 220, 120, 40)},
	});
	writeText(f.sdkBase / "hl2.exe", "not a real exe");
	writeText(f.sdkBase / "bin/base.fgd", BASE_FGD);
	writeText(f.sdkBase / "bin/halflife2.fgd", HL2_FGD);
	writeText(f.sdkBase / "bin/hammerplusplus/hammerplusplus_gameconfig.txt",
		"\"Configs\"\n{\n\t\"Games\"\n\t{\n\t\t\"My Mapbase Mod\"\n\t\t{\n\t\t\t\"GameDir\"\t\t" + quoted(f.mod) + "\n"
		"\t\t\t\"Hammer\"\n\t\t\t{\n"
		"\t\t\t\t\"GameData0\"\t\t" + quoted(f.sdkBase / "bin/halflife2.fgd") + "\n"
		"\t\t\t\t\"GameData1\"\t\t" + quoted(f.mod / "mymod.fgd") + "\n"
		"\t\t\t\t\"TextureFormat\"\t\t\"5\"\n"
		"\t\t\t\t\"DefaultTextureScale\"\t\t\"0.250000\"\n"
		"\t\t\t\t\"DefaultLightmapScale\"\t\t\"16\"\n"
		"\t\t\t\t\"GameExe\"\t\t" + quoted(f.sdkBase / "hl2.exe") + "\n"
		"\t\t\t\t\"DefaultSolidEntity\"\t\t\"func_detail\"\n"
		"\t\t\t\t\"DefaultPointEntity\"\t\t\"info_player_start\"\n"
		"\t\t\t\t\"BSP\"\t\t" + quoted(f.sdkBase / "bin/vbsp.exe") + "\n"
		"\t\t\t\t\"Vis\"\t\t" + quoted(f.sdkBase / "bin/vvis.exe") + "\n"
		"\t\t\t\t\"Light\"\t\t" + quoted(f.sdkBase / "bin/vrad.exe") + "\n"
		"\t\t\t\t\"MapDir\"\t\t" + quoted(f.mod / "mapsrc") + "\n"
		"\t\t\t\t\"BSPDir\"\t\t" + quoted(f.mod / "maps") + "\n"
		"\t\t\t\t\"CordonTexture\"\t\t\"tools/toolsskybox\"\n"
		"\t\t\t}\n\t\t}\n\t}\n}\n");

	// Mapbase-style sourcemod running on the SDK base.
	writeText(f.mod / "gameinfo.txt", gameInfo("My Mapbase Mod", 243730,
		"\t\t\tgame+mod\t\t\tmymod/custom/*\n"
		"\t\t\tgame+mod+mod_write+default_write_path\t\t|gameinfo_path|.\n"
		"\t\t\tgamebin\t\t\t\t|gameinfo_path|bin\n"
		"\t\t\tgame+mod\t\t\t|gameinfo_path|../mapbase_shared/shared_misc.vpk\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2/hl2_textures.vpk\n"
		"\t\t\tgame\t\t\t\t|all_source_engine_paths|hl2\n"
		"\t\t\tgame\t\t\t\t|appid_220|hl2/hl2_misc.vpk\n"));
	writeText(f.mod / "mymod.fgd", MOD_FGD);
	// Mapbase's FGDs live with its shared content; its halflife2.fgd builds on its base.fgd.
	const auto mapbaseBin = f.steam / "steamapps/sourcemods/mapbase_shared/shared_misc/bin";
	writeText(mapbaseBin / "base.fgd", std::string(BASE_FGD) +
		"// Mapbase additions\n@PointClass base(Targetname) = env_global_light : \"Mapbase: cascaded shadows from the sun.\" []\n");
	writeText(mapbaseBin / "halflife2.fgd", HL2_FGD);
	writeText(f.mod / "materials/mymod/patched.vmt",
		"patch\n{\n\tinclude \"materials/concrete/wall01.vmt\"\n\tinsert\n\t{\n\t\t\"$detail\" \"detail/noise\"\n\t}\n"
		"\treplace\n\t{\n\t\t\"$surfaceprop\" \"metal\"\n\t}\n}\n");
	writeText(f.mod / "materials/mymod/loop1.vmt", "patch { include \"materials/mymod/loop2.vmt\" }\n");
	writeText(f.mod / "materials/mymod/loop2.vmt", "patch { include \"materials/mymod/loop1.vmt\" }\n");
	writeText(f.mod / "mapsrc/readme.txt", "maps go here");
}

} // namespace sme::test
