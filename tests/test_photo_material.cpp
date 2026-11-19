// test_photo_material.cpp - The material assistant (photo -> tileable texture, bump map, gloss,
// VMT) and the model check (checksums, missing files and materials, limits, events).

#include "fixture.h"

#include <sme/asset_import.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/model_check.h>
#include <sme/photo_material.h>
#include <sme/texture.h>

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>

using namespace sme;
namespace fs = std::filesystem;

namespace {

/// A "photo": a brick-like pattern under light that falls off from left to right.
ImageRgba photo(unsigned w, unsigned h) {
	ImageRgba image;
	image.width = std::uint16_t(w);
	image.height = std::uint16_t(h);
	image.pixels.resize(std::size_t(w) * h * 4);
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			const bool joint = (y % 32) < 4 || ((x + (y / 32 % 2) * 32) % 64) < 4;
			const double light = 1.2 - 0.8 * double(x) / double(w);   // strong gradient
			const double base = joint ? 90 : 170 + 20 * std::sin(double(x * 7 + y * 3));
			auto* p = &image.pixels[(std::size_t(y) * w + x) * 4];
			p[0] = std::byte(std::uint8_t(std::clamp(base * light, 0.0, 255.0)));
			p[1] = std::byte(std::uint8_t(std::clamp(base * 0.55 * light, 0.0, 255.0)));
			p[2] = std::byte(std::uint8_t(std::clamp(base * 0.4 * light, 0.0, 255.0)));
			p[3] = std::byte{255};
		}
	}
	return image;
}

double meanBrightness(const ImageRgba& image, unsigned x0, unsigned x1) {
	double sum = 0;
	std::size_t n = 0;
	for (unsigned y = 0; y < image.height; ++y) {
		for (unsigned x = x0; x < x1; ++x) {
			const auto* p = &image.pixels[(std::size_t(y) * image.width + x) * 4];
			sum += std::to_integer<int>(p[0]) + std::to_integer<int>(p[1]) + std::to_integer<int>(p[2]);
			++n;
		}
	}
	return sum / double(n * 3);
}

void writeBytes(const fs::path& path, const std::vector<std::byte>& data) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

bool hasIssue(const std::vector<ModelIssue>& issues, std::string_view code, ModelIssue::Level level) {
	for (const auto& issue : issues) {
		if (issue.code == code && issue.level == level) return true;
	}
	return false;
}

} // namespace

TEST_CASE("photo material: crop, flatten light, tileable, power-of-two size") {
	const auto source = photo(640, 480);
	const auto square = cropPhotoSquare(source, 0.5, 0.5, 1.0);
	CHECK(square.width == 480);
	CHECK(square.height == 480);
	CHECK(cropPhotoSquare(source, 0, 0, 0.5).width == 240);
	CHECK(cropPhotoSquare({}, 0.5, 0.5, 1).width == 0);
	CHECK(photoTextureSize(480) == 256);
	CHECK(photoTextureSize(5000) == 1024);
	CHECK(photoTextureSize(10) == 64);

	// Lighting: left and right halves end up about equally bright.
	auto flat = square;
	const double before = meanBrightness(flat, 0, 240) / meanBrightness(flat, 240, 480);
	flattenPhotoLighting(flat, 1.0);
	const double after = meanBrightness(flat, 0, 240) / meanBrightness(flat, 240, 480);
	CHECK(before > 1.4);
	CHECK(after < 1.25);
	CHECK(after > 0.87);

	// Tiling: the opposite edges meet.
	const int seamBefore = photoSeamError(square);
	const auto tiled = makePhotoTileable(square, 0.25);
	CHECK(seamBefore > 20);
	CHECK(photoSeamError(tiled) < seamBefore / 3);
	CHECK(makePhotoTileable(square, 0).pixels == square.pixels);
}

TEST_CASE("photo material: pictures, VMT for world and model, files with undo") {
	PhotoMaterialOptions options;
	options.name = "red brick wall";
	options.gloss = 0.5;
	const auto made = makePhotoMaterial(photo(700, 520), options);
	REQUIRE(made);
	CHECK(made->color.width == 512);   // 520 px square -> 512
	CHECK(made->normal.width == 512);   // bump map, gloss in its alpha
	CHECK(made->mask.width == 0);
	CHECK(made->surfaceProp == "brick");
	const auto vmt = photoMaterialVmt(options, *made);
	const auto m = parseMaterial(vmt, "custom/red_brick_wall");
	REQUIRE(m);
	CHECK(m->shader == "LightmappedGeneric");
	CHECK(m->param("$basetexture") == photoMaterialName(options));
	CHECK(m->param("$bumpmap") == photoMaterialName(options) + "_normal");
	CHECK(m->param("$envmap") == "env_cubemap");
	CHECK(m->param("$normalmapalphaenvmapmask") == "1");

	// No bump map: the gloss is its own $envmapmask.
	auto noBump = options;
	noBump.bumpStrength = 0;
	const auto plain = makePhotoMaterial(photo(300, 300), noBump);
	REQUIRE(plain);
	CHECK(plain->normal.width == 0);
	CHECK(plain->mask.width == plain->color.width);
	CHECK(parseMaterial(photoMaterialVmt(noBump, *plain), "x")->param("$envmapmask") == photoMaterialName(noBump) + "_mask");

	// Model: phong, the mask in a (flat) bump map.
	auto model = noBump;
	model.model = true;
	const auto prop = makePhotoMaterial(photo(300, 300), model);
	REQUIRE(prop);
	CHECK(prop->normal.width == prop->color.width);
	const auto pm = parseMaterial(photoMaterialVmt(model, *prop), "x");
	CHECK(pm->shader == "VertexLitGeneric");
	CHECK(pm->param("$phong") == "1");
	CHECK(photoMaterialName(model).starts_with("models/"));

	CHECK_FALSE(makePhotoMaterial(photo(20, 20), options));
	CHECK(suggestPhotoSurfaceProp("Grass_field", {}) == "grass");
	CHECK(suggestPhotoSurfaceProp("thing", {}) == "default");

	// Files: VMT + two VTFs, listed for undo; a second write stops on existing files.
	test::Fixture f;
	test::createFixture(f);
	std::string error;
	const auto record = writePhotoMaterial(f.mod, options, *made, false, &error);
	REQUIRE_MESSAGE(record, error);
	CHECK(record->created.size() == 3);
	const fs::path vmtFile = f.mod / "materials" / (photoMaterialName(options) + ".vmt");
	CHECK(fs::exists(vmtFile));
	std::vector<char> bytes;
	{
		// Closed again before undoImport: Windows cannot delete a file that is still open.
		std::ifstream in(f.mod / "materials" / (photoMaterialName(options) + ".vtf"), std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	const auto info = readTextureInfo(std::as_bytes(std::span(bytes.data(), bytes.size())));
	REQUIRE(info);
	CHECK(info->width == 512);
	CHECK_FALSE(writePhotoMaterial(f.mod, options, *made, false, &error));
	CHECK(undoImport(*record));
	CHECK_FALSE(fs::exists(vmtFile));
	options.name = "../x";
	CHECK(photoMaterialName(options).find("..") == std::string::npos);
}

TEST_CASE("model check: a good model, missing files, checksums, materials, physics, events") {
	test::Fixture f;
	test::createFixture(f);
	test::StudioModelSpec spec;
	spec.groups.push_back({"body", {{"box", {{-4, -4, 0, 4, 4, 8}}}}});
	spec.bones = {{"root", -1, {0, 0, 0}, {0, 0, 0}}};
	spec.boxBones = {0};
	spec.textures = {"crate"};
	spec.cdmaterials = "models/props/";
	test::StudioModelSpec::Animation idle{"idle", "ACT_IDLE", 30, true, {{{0, 0, 0, 0, 0, 0}}}};
	idle.events = {{0.5f, 0, "AE_CL_PLAYSOUND", "Missing.Sound"}, {0.6f, 0, "AE_CL_CREATE_PARTICLE_EFFECT", "no_such_fx follow_attachment muzzle"}};
	spec.animations = {idle};
	spec.keyValues = "prop_data { \"base\" \"Wooden.Small\" }";
	const auto files = test::makeStudioModel(spec);
	test::writeStudioModel(f.mod / "models/props/box.mdl", files);
	writeBytes(f.mod / "materials/models/props/crate.vtf", test::makeVtf(64, 200, 100, 50));
	test::writeText(f.mod / "materials/models/props/crate.vmt", "\"VertexLitGeneric\" { \"$basetexture\" \"models/props/crate\" }");

	// A fresh file system per check: it remembers which files exist.
	const auto mount = [&] {
		GameFileSystem files;
		files.addDirectory(f.mod, {"game", "mod"});
		return files;
	};
	GameFileSystem fsys = mount();
	const std::set<std::string> sounds{"weapon_pistol.single"};
	const std::set<std::string> particles{"muzzle_flash"};
	ModelCheckContext context{&sounds, &particles};

	auto issues = checkModel(fsys, "models/props/box.mdl", context);
	CHECK_FALSE(hasModelErrors(issues));
	CHECK(hasIssue(issues, "physics", ModelIssue::Level::Warning));   // prop_data without .phy
	CHECK(hasIssue(issues, "sound", ModelIssue::Level::Warning));
	CHECK(hasIssue(issues, "particle", ModelIssue::Level::Warning));
	CHECK_FALSE(hasIssue(issues, "material", ModelIssue::Level::Warning));
	CHECK(modelCheckSummary(issues) == "3 warnings");

	// A missing material and a texture that is not a power of two.
	fs::remove(f.mod / "materials/models/props/crate.vmt");
	fsys = mount();
	issues = checkModel(fsys, "models/props/box.mdl", context);
	CHECK(hasIssue(issues, "material", ModelIssue::Level::Warning));

	// A .vvd from another build: checksum error.
	auto vvd = files.vvd;
	vvd[8] = std::byte(std::to_integer<int>(vvd[8]) ^ 0x5A);
	writeBytes(f.mod / "models/props/box.vvd", vvd);
	fsys = mount();
	issues = checkModel(fsys, "models/props/box.mdl", context);
	CHECK(hasIssue(issues, "checksum", ModelIssue::Level::Error));
	CHECK(hasModelErrors(issues));

	// Missing .dx90.vtx and missing .mdl.
	fs::remove(f.mod / "models/props/box.dx90.vtx");
	fsys = mount();
	CHECK(hasIssue(checkModel(fsys, "models/props/box.mdl"), "missing_file", ModelIssue::Level::Error));
	CHECK(hasIssue(checkModel(fsys, "models/props/none.mdl"), "missing_file", ModelIssue::Level::Error));
	CHECK(modelCheckSummary({}) == "No problems found");

	CHECK(vvdVertexCount(files.vvd).has_value());
	CHECK_FALSE(vvdVertexCount(std::vector<std::byte>(10)));
}
