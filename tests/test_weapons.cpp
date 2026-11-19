// test_weapons.cpp - Animation events (read from a .mdl, written into the .qc), rebuilding a
// model in place in the mod (changes, backups, restore), weapon scripts, skill values and
// sounds replaced at the same path.

#include "fixture.h"

#include <sme/asset_import.h>
#include <sme/audio.h>
#include <sme/filesystem.h>
#include <sme/model_events.h>
#include <sme/model_export.h>
#include <sme/model_rebuild.h>
#include <sme/texture.h>
#include <sme/weapons.h>

#include <doctest/doctest.h>

#include <fstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

/// A viewmodel-like gun with a reload that plays two sounds and a fire with a muzzle flash.
test::StudioModelSpec viewModel() {
	test::StudioModelSpec spec;
	spec.groups.push_back({"studio", {{"gun_reference", {{-4, -2, 0, 12, 2, 6}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}, {"Hand", 0, {8, 0, 6}, {0, 0, 0}}};
	spec.boxBones = {0};
	spec.textures = {"gun_skin"};
	spec.cdmaterials = "models/weapons/gun/";
	test::StudioModelSpec::Animation idle{"idle", "ACT_VM_IDLE", 30, true, {{{0, 0, 0, 0, 0, 0}, {8, 0, 6, 0, 0, 0}}}};
	test::StudioModelSpec::Animation reload{"reload", "ACT_VM_RELOAD", 30, false, {}};
	for (int f = 0; f < 11; ++f) reload.frames.push_back({{0, 0, 0, 0, 0, 0}, {8, 0, 6 - 0.1f * float(f), 0, 0, 0}});
	reload.events = {{0.2f, 0, "AE_CL_PLAYSOUND", "Weapon_Gun.ClipOut"}, {0.7f, 5004, "", "Weapon_Gun.ClipIn"}};
	test::StudioModelSpec::Animation fire{"fire", "ACT_VM_PRIMARYATTACK", 15, false, {{{0, 0, 0, 0, 0, 0}, {8, 0, 6, 0, 0, 0}}, {{0, 0, 0, 0, 0, 0}, {8, 0, 7, 0, 0, 0}}}};
	fire.events = {{0, 5001, "", "1"}};
	spec.animations = {idle, reload, fire};
	return spec;
}

void writeBytes(const fs::path& path, const std::vector<std::byte>& data) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

std::string readAll(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

DecodedAudio tone(int rate, int channels, std::size_t frames) {
	DecodedAudio audio;
	audio.sampleRate = rate;
	audio.channels = channels;
	audio.samples.assign(frames * std::size_t(channels), 1000);
	return audio;
}

} // namespace

TEST_CASE("model events: read from the .mdl with names, numbers, frames and options") {
	const auto files = test::makeStudioModel(viewModel());
	const auto sequences = readSequenceEvents(files.mdl);
	REQUIRE(sequences.size() == 3);
	CHECK(sequences[0].sequence == "idle");
	CHECK(sequences[0].events.empty());
	const auto& reload = sequences[1];
	CHECK(reload.sequence == "reload");
	CHECK(reload.frames == 11);
	REQUIRE(reload.events.size() == 2);
	CHECK(reload.events[0].name == "AE_CL_PLAYSOUND");
	CHECK(reload.events[0].number == 0);
	CHECK(reload.events[0].options == "Weapon_Gun.ClipOut");
	CHECK(reload.events[0].frame(reload.frames) == 2);
	CHECK(reload.events[1].id() == "5004");
	CHECK(reload.events[1].frame(reload.frames) == 7);
	CHECK(isSoundEvent(reload.events[0]));
	CHECK(isSoundEvent(reload.events[1]));
	CHECK(modelEventLabel(reload.events[1]) == "5004 AE_CL_PLAYSOUND");
	CHECK_FALSE(isSoundEvent(sequences[2].events.at(0)));

	CHECK(qcEventLine(reload.events[0], reload.frames) == "{ event AE_CL_PLAYSOUND 2 \"Weapon_Gun.ClipOut\" }");
	ModelEvent moved = reload.events[0];
	moved.setFrame(5, 11);
	CHECK(moved.cycle == doctest::Approx(0.5f));
	moved.options = "bad \"quote\" {brace}\n";
	CHECK(qcEventLine(moved, 11) == "{ event AE_CL_PLAYSOUND 5 \"bad quote brace\" }");
	CHECK(cleanEventOptions(std::string(100, 'x')).size() == MODEL_EVENT_OPTIONS_MAX);

	CHECK(validEventId("AE_CL_PLAYSOUND"));
	CHECK(validEventId("5004"));
	CHECK_FALSE(validEventId(""));
	CHECK_FALSE(validEventId("0"));
	CHECK_FALSE(validEventId("1bad"));
	CHECK_FALSE(validEventId("AE CL"));
	ModelEvent e;
	CHECK(setEventId(e, "6001"));
	CHECK(e.number == 6001);
	CHECK(e.name.empty());
	CHECK_FALSE(setEventId(e, "x;y"));
	CHECK(e.number == 6001);
	const auto* known = findKnownModelEvent("cl_event_muzzleflash0");
	REQUIRE(known);
	CHECK(knownEventId(*known) == "5001");
	CHECK(knownEventId(*findKnownModelEvent("AE_CL_PLAYSOUND")) == "AE_CL_PLAYSOUND");

	// Broken data: nothing, never a crash.
	CHECK(readSequenceEvents({}).empty());
	auto broken = files.mdl;
	const std::int32_t huge = 1 << 30;
	std::memcpy(broken.data() + 188, &huge, 4);
	CHECK(readSequenceEvents(broken).empty());
}

TEST_CASE("model export: events go into the $sequence block, changes replace them and add sequences") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "events/hl2";
	test::writeStudioModel(game / "models/weapons/v_gun.mdl", test::makeStudioModel(viewModel()));
	GameFileSystem files;
	files.addDirectory(game, {"game"});
	std::string error;
	ModelExportOptions options;
	options.materials = false;
	const auto exported = decompileModel(files, "models/weapons/v_gun.mdl", options, &error);
	REQUIRE_MESSAGE(exported, error);
	const std::string& qc = exported->texts.at("v_gun.qc");
	CHECK(qc.find("$sequence \"idle\" \"anims/idle.smd\" fps 30 loop activity \"ACT_VM_IDLE\" 1\n") != std::string::npos);
	CHECK(qc.find("$sequence \"reload\" {\n\t\"anims/reload.smd\"\n\tfps 30 activity \"ACT_VM_RELOAD\" 1\n"
	              "\t{ event AE_CL_PLAYSOUND 2 \"Weapon_Gun.ClipOut\" }\n\t{ event 5004 7 \"Weapon_Gun.ClipIn\" }\n}\n") != std::string::npos);
	CHECK(qc.find("{ event 5001 0 \"1\" }") != std::string::npos);
	CHECK(smdFrameCount(exported->texts.at("anims/reload.smd")) == 11);

	// Changes: other events for the reload, no events for the fire, new frames for the idle, a new sequence.
	ModelEvent clip;
	clip.name = "AE_CL_PLAYSOUND";
	clip.options = "Weapon_Other.Reload";
	clip.setFrame(4, 11);
	options.events["reload"] = {clip};
	options.events["fire"] = {};
	const std::string twoFrames = "version 1\nnodes\n0 \"root\" -1\n1 \"Hand\" 0\nend\nskeleton\ntime 0\n0 0 0 0 0 0 0\n1 8 0 6 0 0 0\ntime 1\n0 0 0 0 0 0 0\n1 8 0 9 0 0 0\nend\n";
	options.replacements["idle"] = {twoFrames, 24, -1, ""};
	options.replacements["inspect"] = {twoFrames, 0, 0, "ACT_VM_FIDGET"};
	const auto changed = decompileModel(files, "models/weapons/v_gun.mdl", options, &error);
	REQUIRE_MESSAGE(changed, error);
	const std::string& qc2 = changed->texts.at("v_gun.qc");
	CHECK(qc2.find("{ event AE_CL_PLAYSOUND 4 \"Weapon_Other.Reload\" }") != std::string::npos);
	CHECK(qc2.find("ClipIn") == std::string::npos);
	CHECK(qc2.find("5001") == std::string::npos);
	CHECK(qc2.find("$sequence \"idle\" \"anims/idle.smd\" fps 24 loop activity \"ACT_VM_IDLE\" 1\n") != std::string::npos);
	CHECK(changed->texts.at("anims/idle.smd") == twoFrames);
	CHECK(qc2.find("$sequence \"inspect\" \"anims/inspect.smd\" fps 30 activity \"ACT_VM_FIDGET\" 1\n") != std::string::npos);
	CHECK(changed->sequences == 4);
}

TEST_CASE("model rebuild: the stored changes round-trip and sit on the original") {
	StoredModelChanges stored;
	stored.model = "models/weapons/v_gun.mdl";
	ModelEvent e;
	e.name = "AE_CL_PLAYSOUND";
	e.cycle = 0.25f;
	e.options = "Weapon_Gun.ClipOut";
	stored.changes.events["reload"] = {e};
	stored.changes.events["fire"] = {};
	stored.changes.replacements["idle"] = {"version 1\nnodes\nend\nskeleton\ntime 0\nend\n", 24, 1, "ACT_VM_IDLE"};
	std::map<std::string, std::string> smd;
	const std::string text = writeModelChanges(stored, &smd);
	REQUIRE(smd.count("edits/idle.smd"));
	const auto back = readModelChanges(text, [&smd](std::string_view file) { return smd[std::string(file)]; });
	REQUIRE(back);
	CHECK(back->model == stored.model);
	CHECK(back->base == "game");
	CHECK(back->changes == stored.changes);
	// Unsafe or missing replacement files are dropped, broken text gives nothing.
	CHECK(readModelChanges(text, [](std::string_view) { return std::string(); })->changes.replacements.empty());
	CHECK_FALSE(readModelChanges("{{{", {}));

	ModelRebuildChanges older;
	older.events["reload"] = {};
	older.events["draw"] = {e};
	ModelRebuildChanges newer;
	newer.events["reload"] = {e};
	older.merge(newer);
	CHECK(older.events.at("reload").size() == 1);
	CHECK(older.events.count("draw"));

	CHECK(modelRebuildFolder("mod", "models/weapons/v_gun.mdl") == fs::path("mod") / "modelsrc" / "mapsmith_rebuild" / "weapons" / "v_gun");
	CHECK(modelRebuildFolder("mod", "models/../x.mdl").empty());
	CHECK(modelRebuildFolder("mod", "materials/x.vmt").empty());
}

TEST_CASE("model rebuild: sources from the game's copy, backups of the mod's own, restore") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "rebuild/hl2";
	const auto mod = f.root / "rebuild/mymod";
	test::writeStudioModel(game / "models/weapons/v_gun.mdl", test::makeStudioModel(viewModel()));
	fs::create_directories(mod);
	GameFileSystem files;
	files.addDirectory(mod, {"game", "mod"});
	files.addDirectory(game, {"game"});
	std::string error;

	// First rebuild of a model the mod does not have: base "game", nothing to back up.
	ModelRebuildChanges changes;
	ModelEvent e;
	e.name = "AE_CL_PLAYSOUND";
	e.options = "Weapon_Mine.Reload";
	e.setFrame(3, 11);
	changes.events["reload"] = {e};
	auto plan = prepareModelRebuild(files, mod, "models/weapons/v_gun.mdl", changes, &error);
	REQUIRE_MESSAGE(plan, error);
	CHECK_FALSE(plan->earlierRebuild);
	CHECK(plan->record.replaced.empty());
	CHECK(plan->qc == modelRebuildFolder(mod, "models/weapons/v_gun.mdl") / "v_gun.qc");
	CHECK(plan->arguments.size() == 5);
	CHECK(plan->arguments[0] == "-game");
	CHECK(readAll(plan->qc).find("Weapon_Mine.Reload") != std::string::npos);
	CHECK(loadModelChanges(mod, "models/weapons/v_gun.mdl")->base == "game");
	// studiomdl did not run: finishing fails and records nothing.
	CHECK_FALSE(finishModelRebuild(*plan, mod, &error));
	CHECK_FALSE(modelRebuildRecord(mod, "models/weapons/v_gun.mdl"));
	// "studiomdl" writes the model into the mod.
	test::writeStudioModel(mod / "models/weapons/v_gun.mdl", test::makeStudioModel(viewModel()));
	REQUIRE(finishModelRebuild(*plan, mod, &error));
	const auto record = modelRebuildRecord(mod, "models/weapons/v_gun.mdl");
	REQUIRE(record);
	CHECK(record->created.size() == 3);   // .mdl, .vvd, .dx90.vtx

	// A second change sits on the game's copy again (not on the rebuild) and keeps the first.
	ModelRebuildChanges more;
	more.events["fire"] = {};
	auto second = prepareModelRebuild(files, mod, "models/weapons/v_gun.mdl", more, &error);
	REQUIRE_MESSAGE(second, error);
	CHECK(second->earlierRebuild);
	const std::string qc = readAll(second->qc);
	CHECK(qc.find("Weapon_Mine.Reload") != std::string::npos);
	CHECK(qc.find("5001") == std::string::npos);
	REQUIRE(finishModelRebuild(*second, mod, &error));
	CHECK(modelRebuildRecord(mod, "models/weapons/v_gun.mdl")->created.size() == 3);

	// Restore: the mod's copy goes, the game's counts again.
	REQUIRE(restoreModel(mod, "models/weapons/v_gun.mdl", &error));
	CHECK_FALSE(fs::exists(mod / "models/weapons/v_gun.mdl"));
	CHECK_FALSE(modelRebuildRecord(mod, "models/weapons/v_gun.mdl"));
	CHECK_FALSE(loadModelChanges(mod, "models/weapons/v_gun.mdl"));
	CHECK(restoreModel(mod, "models/weapons/v_gun.mdl", &error));   // Nothing left: fine

	// A mod with its own copy: it is the base, kept as backups and put back on restore.
	auto ownSpec = viewModel();
	ownSpec.animations[1].events = {{0.5f, 0, "AE_CL_PLAYSOUND", "Weapon_Own.Reload"}};
	test::writeStudioModel(mod / "models/weapons/v_gun.mdl", test::makeStudioModel(ownSpec));
	const std::string ownBytes = readAll(mod / "models/weapons/v_gun.mdl");
	GameFileSystem files2;
	files2.addDirectory(mod, {"game", "mod"});
	files2.addDirectory(game, {"game"});
	ModelRebuildChanges none;
	none.events["fire"] = {};
	auto own = prepareModelRebuild(files2, mod, "models/weapons/v_gun.mdl", none, &error);
	REQUIRE_MESSAGE(own, error);
	CHECK(loadModelChanges(mod, "models/weapons/v_gun.mdl")->base == "mod");
	CHECK(own->record.replaced.size() == 3);
	CHECK(fs::exists(mod / "models/weapons/v_gun.mdl.mapsmith-backup"));
	CHECK(readAll(own->qc).find("Weapon_Own.Reload") != std::string::npos);
	test::writeStudioModel(mod / "models/weapons/v_gun.mdl", test::makeStudioModel(viewModel()));   // "studiomdl"
	REQUIRE(finishModelRebuild(*own, mod, &error));
	// Rebuilt again: still from the mod's own original (kept in base/), not from the rebuild.
	auto again = prepareModelRebuild(files2, mod, "models/weapons/v_gun.mdl", {}, &error);
	REQUIRE_MESSAGE(again, error);
	CHECK(readAll(again->qc).find("Weapon_Own.Reload") != std::string::npos);
	REQUIRE(restoreModel(mod, "models/weapons/v_gun.mdl", &error));
	CHECK(readAll(mod / "models/weapons/v_gun.mdl") == ownBytes);
	CHECK_FALSE(fs::exists(mod / "models/weapons/v_gun.mdl.mapsmith-backup"));

	CHECK_FALSE(prepareModelRebuild(files, mod, "models/../../evil.mdl", {}, &error));
}

TEST_CASE("weapons: scripts found, read, written back and saved as the mod's copy") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "weapons/hl2";
	const auto mod = f.root / "weapons/mymod";
	test::writeText(game / "scripts/weapon_smg1.txt", R"(// SMG1
WeaponData
{
	"printname"	"#HL2_SMG1"
	"viewmodel"	"models/weapons/v_smg1.mdl"
	"playermodel"	"models/weapons/w_smg1.mdl"
	"clip_size"	"45"
	"primary_ammo"	"SMG1"
	"secondary_ammo"	"SMG1_Grenade"
	"custom_key"	"kept"
	SoundData
	{
		"reload"	"Weapon_SMG1.Reload"
		"single_shot"	"Weapon_SMG1.Single"
		"empty"	"Weapon_Pistol.Empty"
	}
	TextureData
	{
		"weapon" { "font" "WeaponIcons" "character" "a" }
	}
}
)");
	test::writeText(game / "scripts/weapon_crowbar.txt", "WeaponData\n{\n\t\"printname\" \"Crowbar\"\n\t\"clip_size\" \"-1\"\n}\n");
	test::writeText(game / "scripts/weapon_broken.txt", "WeaponData\n{\n\t\"printname\" \"x\"\n");
	test::writeText(game / "scripts/game_sounds_weapons.txt", "\"Weapon_SMG1.Reload\"\n{\n\t\"wave\" \"weapons/smg1/smg1_reload.wav\"\n}\n");
	test::writeText(game / "scripts/sub/weapon_nested.txt", "WeaponData\n{\n}\n");
	fs::create_directories(mod);
	GameFileSystem files;
	files.addDirectory(mod, {"game", "mod"});
	files.addDirectory(game, {"game"});

	auto weapons = findWeapons(files, mod);
	REQUIRE(weapons.size() == 2);   // Broken and nested files are left out
	CHECK(weapons[0].className == "weapon_crowbar");
	const auto& smg = weapons[1];
	CHECK(smg.className == "weapon_smg1");
	CHECK(smg.file == "scripts/weapon_smg1.txt");
	CHECK_FALSE(smg.inMod);
	CHECK(smg.viewModel() == "models/weapons/v_smg1.mdl");
	CHECK(smg.worldModel() == "models/weapons/w_smg1.mdl");
	const auto sounds = smg.sounds();
	REQUIRE(sounds.size() == 3);
	CHECK(sounds[0] == std::pair<std::string, std::string>{"empty", "Weapon_Pistol.Empty"});   // In the game's slot order
	CHECK(sounds[1].first == "single_shot");
	CHECK(weaponSoundSlotLabel("single_shot") == "Shot (player)");
	CHECK(weaponDisplayName(smg, [](std::string_view token) { return token == "HL2_SMG1" ? std::string("SMG") : std::string(); }) == "SMG");
	CHECK(weaponDisplayName(smg) == "smg1");
	CHECK(weaponDisplayName(weapons[0]) == "Crowbar");

	const auto skills = weaponSkillValues(smg);
	REQUIRE(skills.size() == 6);
	CHECK(skills[0].name == "sk_plr_dmg_smg1");
	CHECK(skills[2].name == "sk_max_smg1");
	CHECK(skills[3].name == "sk_plr_dmg_smg1_grenade");
	CHECK(weaponSkillValues(weapons[0]).at(0).name == "sk_plr_dmg_crowbar");

	// Saved with a change: the mod's copy, unknown keys and blocks kept; restore takes it out.
	auto changed = smg;
	changed.data.set("clip_size", "60");
	std::string error;
	REQUIRE(saveWeaponScript(mod, changed, &error));
	CHECK(weaponScriptSaved(mod, "scripts/weapon_smg1.txt"));
	files.noteWrittenFile(mod / "scripts/weapon_smg1.txt");   // As the app does after saving
	const auto again = findWeapons(files, mod);
	CHECK(again[1].inMod);
	CHECK(again[1].get("clip_size") == "60");
	CHECK(again[1].get("custom_key") == "kept");
	CHECK(again[1].data.find("TextureData")->find("weapon")->get("character") == "a");
	REQUIRE(restoreWeaponScript(mod, "scripts/weapon_smg1.txt", &error));
	CHECK_FALSE(fs::exists(mod / "scripts/weapon_smg1.txt"));
	CHECK_FALSE(weaponScriptSaved(mod, "scripts/weapon_smg1.txt"));

	// The mod's own earlier copy is backed up and comes back.
	test::writeText(mod / "scripts/weapon_crowbar.txt", "WeaponData\n{\n\t\"printname\" \"My bar\"\n}\n");
	auto bar = *parseWeaponScript(readAll(mod / "scripts/weapon_crowbar.txt"), "weapon_crowbar");
	bar.data.set("weight", "5");
	REQUIRE(saveWeaponScript(mod, bar, &error));
	REQUIRE(saveWeaponScript(mod, bar, &error));   // Twice: the backup stays the mod's own
	REQUIRE(restoreWeaponScript(mod, "scripts/weapon_crowbar.txt", &error));
	CHECK(readAll(mod / "scripts/weapon_crowbar.txt").find("My bar") != std::string::npos);
	CHECK(readAll(mod / "scripts/weapon_crowbar.txt").find("weight") == std::string::npos);

	auto evil = smg;
	evil.file = "../evil.txt";
	CHECK_FALSE(saveWeaponScript(mod, evil, &error));
	CHECK_FALSE(parseWeaponScript("nothing here", "weapon_x"));
}

TEST_CASE("weapons: skill values read and set in the mod's skill.cfg") {
	const std::string cfg = "// skill\nsk_plr_dmg_smg1\t\t\"4\"\r\nsk_npc_dmg_smg1 3 // npc\n\"sk_max_smg1\" \"225\"\nexec other.cfg\n";
	const auto values = readSkillValues(cfg);
	CHECK(values.at("sk_plr_dmg_smg1") == "4");
	CHECK(values.at("sk_npc_dmg_smg1") == "3");
	CHECK(values.at("sk_max_smg1") == "225");
	CHECK(values.size() == 3);
	const std::string changed = withSkillValue(cfg, "sk_plr_dmg_smg1", "7");
	CHECK(readSkillValues(changed).at("sk_plr_dmg_smg1") == "7");
	CHECK(changed.find("exec other.cfg") != std::string::npos);
	CHECK(changed.find("sk_plr_dmg_smg1\t\"7\"\r\n") != std::string::npos);
	const std::string added = withSkillValue(cfg, "sk_plr_dmg_ar2", "8; quit");
	CHECK(readSkillValues(added).at("sk_plr_dmg_ar2") == "8");   // Only a number gets in

	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "skill/mymod";
	fs::create_directories(mod);
	std::string error;
	REQUIRE(saveSkillValues(mod, {{"sk_plr_dmg_smg1", "9"}, {"not_a_skill", "1"}}, cfg, &error));
	const std::string written = readAll(mod / "cfg/skill.cfg");
	CHECK(readSkillValues(written).at("sk_plr_dmg_smg1") == "9");
	CHECK(written.find("not_a_skill") == std::string::npos);
	CHECK(readAll(mod / "cfg/skill_manifest.cfg") == "exec skill.cfg\n");
	CHECK_FALSE(fs::exists(mod / "cfg/skill.cfg.mapsmith-backup"));
	REQUIRE(saveSkillValues(mod, {{"sk_max_smg1", "300"}}, cfg, &error));
	CHECK(fs::exists(mod / "cfg/skill.cfg.mapsmith-backup"));
	CHECK(readSkillValues(readAll(mod / "cfg/skill.cfg")).at("sk_plr_dmg_smg1") == "9");
}

TEST_CASE("weapons: a sound replaced at the same path, like the original, and restored") {
	const DecodedAudio original = tone(22050, 1, 100);
	auto options = soundReplaceOptions(")weapons/smg1/smg1_fire1.wav", &original);
	REQUIRE(options);
	CHECK(options->folder == "weapons/smg1");
	CHECK(options->name == "smg1_fire1");
	CHECK(options->sampleRate == 22050);
	CHECK(options->channels == SoundImportOptions::Channels::Mono);
	CHECK_FALSE(options->script);
	CHECK_FALSE(soundReplaceOptions("weapons/smg1/fire.mp3", nullptr));
	CHECK_FALSE(soundReplaceOptions("../x.wav", nullptr));
	CHECK_FALSE(soundReplaceOptions("weapons/My Gun/fire.wav", nullptr));   // The importer would rename it
	CHECK(soundReplaceOptions("weapons/smg1/fire.wav", nullptr)->sampleRate == 44100);

	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "sound/mymod";
	fs::create_directories(mod);
	std::string error;
	const auto record = replaceSound(mod, ")weapons/smg1/smg1_fire1.wav", tone(44100, 2, 4410), &original, &error);
	REQUIRE_MESSAGE(record, error);
	const fs::path wav = mod / "sound/weapons/smg1/smg1_fire1.wav";
	REQUIRE(fs::exists(wav));
	const auto bytes = readAll(wav);
	const auto decoded = decodeAudio(std::as_bytes(std::span(bytes.data(), bytes.size())), "x.wav", &error);
	REQUIRE(decoded);
	CHECK(decoded->sampleRate == 22050);   // Like the original
	CHECK(decoded->channels == 1);
	CHECK(soundReplacement(mod, "weapons/smg1/smg1_fire1.wav"));
	// Again: one record, still restorable to nothing.
	REQUIRE(replaceSound(mod, "weapons/smg1/smg1_fire1.wav", tone(44100, 1, 100), &original, &error));
	CHECK(loadImports(mod).size() == 1);
	REQUIRE(restoreSound(mod, "weapons/smg1/smg1_fire1.wav", &error));
	CHECK_FALSE(fs::exists(wav));
	CHECK_FALSE(soundReplacement(mod, "weapons/smg1/smg1_fire1.wav"));

	// The mod's own file is backed up and comes back.
	test::writeText(wav, "own");
	REQUIRE(replaceSound(mod, "weapons/smg1/smg1_fire1.wav", tone(44100, 1, 100), nullptr, &error));
	REQUIRE(replaceSound(mod, "weapons/smg1/smg1_fire1.wav", tone(44100, 1, 100), nullptr, &error));
	REQUIRE(restoreSound(mod, "weapons/smg1/smg1_fire1.wav", &error));
	CHECK(readAll(wav) == "own");
}

TEST_CASE("weapons: a texture replaced at the same path and restored") {
	test::Fixture f;
	test::createFixture(f);
	const auto mod = f.root / "texture/mymod";
	fs::create_directories(mod);
	ImageRgba image;
	image.width = 8;
	image.height = 8;
	image.pixels.assign(8 * 8 * 4, std::byte{200});
	std::string error;
	REQUIRE_MESSAGE(replaceTexture(mod, "models/weapons/v_gun/gun_skin", image, 2048, &error), error);
	const fs::path vtf = mod / "materials/models/weapons/v_gun/gun_skin.vtf";
	REQUIRE(fs::exists(vtf));
	const auto bytes = readAll(vtf);
	const auto info = readTextureInfo(std::as_bytes(std::span(bytes.data(), bytes.size())), &error);
	REQUIRE(info);
	CHECK(info->width == 8);
	CHECK(textureReplacement(mod, "models/weapons/v_gun/gun_skin"));
	REQUIRE(replaceTexture(mod, "models/weapons/v_gun/gun_skin", image, 2048, &error));
	CHECK(loadImports(mod).size() == 1);
	REQUIRE(restoreTexture(mod, "models/weapons/v_gun/gun_skin", &error));
	CHECK_FALSE(fs::exists(vtf));
	CHECK_FALSE(replaceTexture(mod, "../evil", image, 2048, &error));
}
