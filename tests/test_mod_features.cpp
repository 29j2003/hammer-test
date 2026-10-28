// test_mod_features.cpp - The add-on's features as a checklist: the list, the cfg the game runs,
// the autoexec line and what a fresh install removes.

#include <doctest/doctest.h>

#include <sme/mod_features.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, const std::string& text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
	std::ifstream stream(path, std::ios::binary);
	std::ostringstream out;
	out << stream.rdbuf();
	return out.str();
}

bool listed(const std::vector<GeneratedFile>& files, const std::string& path) {
	return std::any_of(files.begin(), files.end(), [&](const GeneratedFile& f) { return f.path.generic_string() == path; });
}

} // namespace

TEST_CASE("mod features: the list - unique ids, every switch once, material effects") {
	std::set<std::string> ids;
	std::set<std::string> convars;
	std::set<FeatureGroup> groups;
	for (const auto& feature : modFeatures()) {
		CHECK(ids.insert(feature.id).second);
		CHECK(std::string(feature.title).size() > 3);
		CHECK(std::string(feature.description).size() > 10);
		CHECK((!feature.convars.empty() || feature.effect.has_value()));
		groups.insert(feature.group);
		for (const char* convar : feature.convars) {
			CHECK(convars.insert(convar).second);
			const std::string name = convar;
			CHECK((name.starts_with("r_fidelity") || name.starts_with("snd_fidelity") || name.starts_with("cl_fidelity")));
		}
	}
	CHECK(groups.size() == 4);
	CHECK(findModFeature("ssao")->convars.front() == std::string("r_fidelity_ssao"));
	CHECK(findModFeature("liquid")->effect == GameEffect::Liquid);
	CHECK(findModFeature("relief")->effect == GameEffect::Relief);
	CHECK(findModFeature("nothing") == nullptr);
	// The material effects follow the choice; left-out models stay.
	EnhancementSettings settings;
	settings.setExcluded(GameEffect::Liquid, "models/a.mdl", true);
	settings.off.insert(GameEffect::Water);
	const auto chosen = withFeatureEffects(settings, {"liquid", "ssao"});
	CHECK(!chosen.enabled(GameEffect::Liquid));
	CHECK(chosen.enabled(GameEffect::Water));
	CHECK(chosen.isExcluded(GameEffect::Liquid, "models/a.mdl"));
	CHECK(featuresOffBySettings(chosen) == std::set<std::string>{"liquid"});
	// Look values are never features (they belong to fidelity.cfg and the maps).
	CHECK_FALSE(convars.count("r_fidelity_ssao_strength"));
	CHECK_FALSE(convars.count("r_fidelity_godrays_intensity"));
}

TEST_CASE("mod features: the cfg - every switch written, read back, comments and CRLF") {
	const std::set<std::string> off = {"ssao", "muzzlelight", "reverb", "wind"};
	const std::string text = writeFeaturesCfg(off);
	CHECK(text.starts_with(FEATURES_MARKER));
	CHECK(text.find("r_fidelity_ssao 0\n") != std::string::npos);
	CHECK(text.find("r_fidelity_godrays 1\n") != std::string::npos);
	CHECK(text.find("r_fidelity_muzzlelight 0\n") != std::string::npos);
	CHECK(text.find("r_fidelity_flashes 0\n") != std::string::npos);   // both switches of the feature
	CHECK(text.find("r_fidelity_wind 0\n") != std::string::npos);
	CHECK(text.find("liquid") == std::string::npos);                   // material-only effects are not here
	CHECK(parseFeaturesCfg(text) == off);
	CHECK(parseFeaturesCfg(writeFeaturesCfg({})).empty());

	// Edited by hand: CRLF, quotes, comments, unknown lines; one of two switches at 0 is off.
	const std::string edited = "// mine\r\nr_fidelity_ssao \"0\" // no\r\nr_fidelity_sunshadow 1\r\nr_fidelity_sun_follow 0\r\nfoo bar\r\n";
	CHECK(parseFeaturesCfg(edited) == std::set<std::string>{"ssao", "sunshadows"});
}

TEST_CASE("mod features: saved into the mod, autoexec runs it once, the line can go again") {
	const auto dir = fs::temp_directory_path() / "sme_mod_features_save";
	fs::remove_all(dir);
	CHECK_FALSE(loadFeatureChoice(dir));
	CHECK_FALSE(hookFeatureChoice(dir));   // no choice yet: nothing to run

	// Without the add-on the file is written but autoexec is left alone.
	REQUIRE(saveFeatureChoice(dir, {"fxaa"}, false));
	CHECK(loadFeatureChoice(dir) == std::set<std::string>{"fxaa"});
	CHECK_FALSE(fs::exists(dir / "cfg" / "autoexec.cfg"));
	// The add-on arrived: the line is added once, after the lines that were there (CRLF kept).
	write(dir / "cfg" / "autoexec.cfg", "sv_cheats 1\r\nexec fidelity\t// mapbase-fidelity\r\n");
	CHECK(hookFeatureChoice(dir));
	CHECK_FALSE(hookFeatureChoice(dir));
	const std::string autoexec = read(dir / "cfg" / "autoexec.cfg");
	CHECK(autoexec == "sv_cheats 1\r\nexec fidelity\t// mapbase-fidelity\r\nexec mapsmith_features\t// mapsmith\r\n");
	CHECK(runsFeatures(autoexec));
	REQUIRE(saveFeatureChoice(dir, {}, true));
	CHECK(read(dir / "cfg" / "autoexec.cfg") == autoexec);   // never twice
	CHECK(loadFeatureChoice(dir)->empty());

	CHECK(withoutFeaturesLine(autoexec) == "sv_cheats 1\r\nexec fidelity\t// mapbase-fidelity\r\n");
	CHECK(withFeaturesLine("") == "exec mapsmith_features\t// mapsmith\n");
	CHECK(withFeaturesLine("echo hi") == "echo hi\nexec mapsmith_features\t// mapsmith\n");
	CHECK(runsFeatures("  exec \"mapsmith_features.cfg\"\n"));
	CHECK_FALSE(runsFeatures("// exec mapsmith_features\n"));
	fs::remove_all(dir);
}

TEST_CASE("mod features: a fresh install lists only what deploy.bat and Mapsmith made") {
	const auto dir = fs::temp_directory_path() / "sme_mod_features_fresh";
	fs::remove_all(dir);
	write(dir / "materials" / "brick" / "wall01.vmt", std::string(DEPLOY_MARKER) + " (parallax) from materials/brick/wall01.vmt\n\"Fidelity_Parallax\"\n{\n}\n");
	write(dir / "materials" / "models" / "bottle.vmt", std::string(MAPSMITH_MARKER) + " off (liquid) from ...\n\"VertexLitGeneric\"\n{\n}\n");
	write(dir / "materials" / "Mine" / "Own.vmt", "\"LightmappedGeneric\"\n{\n}\n");             // the mapper's own
	write(dir / "materials" / "mine" / "notes.txt", std::string(DEPLOY_MARKER) + "\n");          // not a material
	write(dir / "materials" / "brick" / "wall01_fidh.vtf", "VTF");
	write(dir / "fidelity_heights.txt", "// mapbase-fidelity: generated by deploy.ps1 (heights)\nmaterials/brick/wall01_fidh.vtf\tABC\n"
	                                     "materials/brick/gone_fidh.vtf\tDEF\n../outside.vtf\tX\n");
	write(dir / "shaders" / "fxc" / "fidelity_parallax_ps30.vcs", "x");
	write(dir / "shaders" / "fxc" / "lightmappedgeneric_ps30.vcs", "x");   // not the add-on's
	write(dir / "bin" / "game_shader_dx6.dll", "x");
	write(dir / "bin" / "client.dll", "x");
	write(dir / "cfg" / "fidelity.cfg", "x");
	write(dir / "cfg" / "autoexec.cfg", "x");
	write(dir / "fidelity_lights.rad", "noshadow mapbase_fidelity_generated_by_deploy_ps1\nlights/screen 255 255 255 100\n");   // deploy.ps1 since VRAD has no comments

	const auto files = generatedModFiles(dir);
	CHECK(listed(files, "materials/brick/wall01.vmt"));
	CHECK(listed(files, "materials/models/bottle.vmt"));
	CHECK(listed(files, "materials/brick/wall01_fidh.vtf"));
	CHECK(listed(files, "fidelity_heights.txt"));
	CHECK(listed(files, "shaders/fxc/fidelity_parallax_ps30.vcs"));
	CHECK(listed(files, "bin/game_shader_dx6.dll"));
	CHECK(listed(files, "cfg/fidelity.cfg"));
	CHECK(listed(files, "fidelity_lights.rad"));
	CHECK_FALSE(listed(files, "materials/Mine/Own.vmt"));
	CHECK_FALSE(listed(files, "materials/mine/notes.txt"));
	CHECK_FALSE(listed(files, "materials/brick/gone_fidh.vtf"));
	CHECK_FALSE(listed(files, "shaders/fxc/lightmappedgeneric_ps30.vcs"));
	CHECK_FALSE(listed(files, "bin/client.dll"));
	CHECK_FALSE(listed(files, "cfg/autoexec.cfg"));
	CHECK(std::count_if(files.begin(), files.end(), [](const GeneratedFile& f) { return f.byEditor; }) == 1);
	CHECK(std::is_sorted(files.begin(), files.end(), [](const GeneratedFile& a, const GeneratedFile& b) {
		return a.path.generic_string() < b.path.generic_string();
	}) == true);

	// The editor removes its own (deploy.ps1 -Remove takes out the rest); a file the mapper
	// changed since (no marker any more) stays.
	write(dir / "materials" / "models" / "jar.vmt", std::string(MAPSMITH_MARKER) + " off\n");
	auto again = generatedModFiles(dir);
	write(dir / "materials" / "models" / "jar.vmt", "\"VertexLitGeneric\"\n{\n}\n");
	CHECK(removeEditorFiles(dir, again) == 1);
	CHECK_FALSE(fs::exists(dir / "materials" / "models" / "bottle.vmt"));
	CHECK(fs::exists(dir / "materials" / "models" / "jar.vmt"));
	CHECK(fs::exists(dir / "materials" / "brick" / "wall01.vmt"));
	fs::remove_all(dir);
}
