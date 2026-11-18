// test_texture_enhance.cpp - Enhance textures: seamless scaling, alpha on its own, normal maps
// stay unit length, the AI path (a stand-in upscaler), the coordinate transform that keeps world
// textures as large as before, the record, writing into a mod and removing it again exactly.

#include <doctest/doctest.h>

#include "fixture.h"

#include <sme/asset_import.h>
#include <sme/fidelity_effects.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/road_texture.h>
#include <sme/texture.h>
#include <sme/texture_enhance.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sme;

namespace {

ImageRgba solid(unsigned w, unsigned h, int r, int g, int b, int a = 255) {
	ImageRgba image;
	image.width = std::uint16_t(w);
	image.height = std::uint16_t(h);
	image.pixels.resize(std::size_t(w) * h * 4);
	for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
		image.pixels[i * 4] = std::byte(r);
		image.pixels[i * 4 + 1] = std::byte(g);
		image.pixels[i * 4 + 2] = std::byte(b);
		image.pixels[i * 4 + 3] = std::byte(a);
	}
	return image;
}

int px(const ImageRgba& image, unsigned x, unsigned y, int c) { return std::to_integer<int>(image.pixels[(std::size_t(y) * image.width + x) * 4 + std::size_t(c)]); }

void setPx(ImageRgba& image, unsigned x, unsigned y, int r, int g, int b, int a = 255) {
	const std::size_t i = (std::size_t(y) * image.width + x) * 4;
	image.pixels[i] = std::byte(r);
	image.pixels[i + 1] = std::byte(g);
	image.pixels[i + 2] = std::byte(b);
	image.pixels[i + 3] = std::byte(a);
}

/// A tiling pattern with detail (diagonal stripes that run across the edges).
ImageRgba pattern(unsigned size) {
	ImageRgba image = solid(size, size, 0, 0, 0);
	for (unsigned y = 0; y < size; ++y) {
		for (unsigned x = 0; x < size; ++x) {
			const int v = ((x + y) / 4) % 2 ? 200 : 60;
			setPx(image, x, y, v, v / 2, 255 - v);
		}
	}
	return image;
}

std::string readAll(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

void writeBytes(const std::filesystem::path& file, const std::vector<std::byte>& bytes) {
	std::filesystem::create_directories(file.parent_path());
	std::ofstream out(file, std::ios::binary);
	out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void writeVtf(const std::filesystem::path& file, const ImageRgba& image, bool normal = false) {
	const auto vtf = encodeVtf(image, !normal, normal, 4096);
	REQUIRE(vtf);
	writeBytes(file, *vtf);
}

} // namespace

TEST_CASE("texture enhance: Lanczos keeps flat colour, wraps tiling textures, clamps clamped ones") {
	const ImageRgba flat = solid(8, 8, 100, 150, 200, 255);
	const ImageRgba big = resizeLanczos(flat, 32, 32);
	REQUIRE(big.width == 32);
	REQUIRE(big.height == 32);
	for (unsigned y = 0; y < 32; y += 7) {
		for (unsigned x = 0; x < 32; x += 5) {
			CHECK(px(big, x, y, 0) == 100);
			CHECK(px(big, x, y, 1) == 150);
			CHECK(px(big, x, y, 2) == 200);
		}
	}
	// A bright column at the left edge: wrapped, it shows at the right edge too (the next tile's);
	// clamped, the right edge stays dark.
	ImageRgba edge = solid(16, 16, 0, 0, 0);
	for (unsigned y = 0; y < 16; ++y) setPx(edge, 0, y, 255, 255, 255);
	const ImageRgba wrapped = resizeLanczos(edge, 64, 64, EdgeMode::Wrap, EdgeMode::Wrap);
	const ImageRgba clamped = resizeLanczos(edge, 64, 64, EdgeMode::Clamp, EdgeMode::Clamp);
	CHECK(px(wrapped, 63, 30, 0) > 40);
	CHECK(px(clamped, 63, 30, 0) < 10);
	// Seamless: across the tile's edge the step is no larger than inside.
	const ImageRgba tiled = resizeLanczos(pattern(32), 128, 128);
	int inside = 0, across = 0;
	for (unsigned y = 0; y < 128; ++y) {
		inside = std::max(inside, std::abs(px(tiled, 64, y, 0) - px(tiled, 63, y, 0)));
		across = std::max(across, std::abs(px(tiled, 0, y, 0) - px(tiled, 127, y, 0)));
	}
	CHECK(across <= inside + 2);
	// Downscaling works too (wider kernel), and empty pictures stay empty.
	CHECK(resizeLanczos(pattern(64), 16, 16).width == 16);
	CHECK(resizeLanczos(ImageRgba{}, 16, 16).pixels.empty());
}

TEST_CASE("texture enhance: sharpening leaves alpha, cut-out alpha stays 0/255, pad and crop") {
	ImageRgba image = pattern(16);
	for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = std::byte((i / 4) % 3 ? 255 : 0);
	const ImageRgba before = image;
	sharpen(image, 0.5);
	for (std::size_t i = 3; i < image.pixels.size(); i += 4) CHECK(image.pixels[i] == before.pixels[i]);
	ImageRgba flat = solid(8, 8, 90, 90, 90);
	sharpen(flat, 1.0);
	CHECK(px(flat, 3, 3, 0) == 90);

	// A cut-out texture scaled up: alpha only 0 or 255 again.
	const auto up = upscalePicture(before, 64, 64, UpscaleMethod::Sharp, 0.35, EdgeMode::Wrap, EdgeMode::Wrap, nullptr);
	REQUIRE(up.image.width == 64);
	for (std::size_t i = 3; i < up.image.pixels.size(); i += 4) {
		const int a = std::to_integer<int>(up.image.pixels[i]);
		CHECK((a == 0 || a == 255));
	}

	const ImageRgba padded = padPicture(pattern(16), 4, EdgeMode::Wrap, EdgeMode::Wrap);
	CHECK(padded.width == 24);
	CHECK(px(padded, 0, 4, 0) == px(pattern(16), 12, 0, 0));   // left margin = the tile's right side
	const ImageRgba back = cropPicture(padded, 4, 4, 16, 16);
	CHECK(back.pixels == pattern(16).pixels);
	const ImageRgba clampPad = padPicture(pattern(16), 4, EdgeMode::Clamp, EdgeMode::Clamp);
	CHECK(px(clampPad, 0, 4, 0) == px(pattern(16), 0, 0, 0));
}

TEST_CASE("texture enhance: normal maps are resampled as unit vectors") {
	ImageRgba normal = solid(16, 16, 128, 128, 255);
	for (unsigned y = 0; y < 16; ++y) setPx(normal, 8, y, 230, 128, 160);   // a ridge
	const ImageRgba big = resizeNormalMap(normal, 64, 64, false);
	REQUIRE(big.width == 64);
	for (unsigned y = 0; y < 64; y += 9) {
		for (unsigned x = 0; x < 64; x += 3) {
			const double nx = px(big, x, y, 0) / 127.5 - 1, ny = px(big, x, y, 1) / 127.5 - 1, nz = px(big, x, y, 2) / 127.5 - 1;
			CHECK(std::sqrt(nx * nx + ny * ny + nz * nz) == doctest::Approx(1.0).epsilon(0.03));
			CHECK(nz > 0);
		}
	}
	CHECK(px(big, 2, 2, 2) > 250);   // flat stays flat
}

TEST_CASE("texture enhance: the AI path pads, crops and keeps alpha; a failing AI falls back") {
	ImageRgba image = pattern(32);
	for (std::size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = std::byte(128);
	unsigned seenW = 0;
	const AiUpscaler nearest = [&](const ImageRgba& in, unsigned factor, const std::atomic<bool>*, std::string*) -> std::optional<ImageRgba> {
		seenW = in.width;
		ImageRgba out;
		out.width = std::uint16_t(in.width * factor);
		out.height = std::uint16_t(in.height * factor);
		out.pixels.resize(std::size_t(out.width) * out.height * 4);
		for (unsigned y = 0; y < out.height; ++y) {
			for (unsigned x = 0; x < out.width; ++x) {
				const std::size_t s = (std::size_t(y / factor) * in.width + x / factor) * 4, d = (std::size_t(y) * out.width + x) * 4;
				for (int c = 0; c < 3; ++c) out.pixels[d + std::size_t(c)] = in.pixels[s + std::size_t(c)];
				out.pixels[d + 3] = std::byte(9);   // the AI's alpha is not used
			}
		}
		return out;
	};
	const auto up = upscalePicture(image, 64, 64, UpscaleMethod::Ai, 0.35, EdgeMode::Wrap, EdgeMode::Wrap, &nearest);
	CHECK(up.usedAi);
	CHECK(seenW == 32 + 2 * aiMargin(32, 32));
	REQUIRE(up.image.width == 64);
	CHECK(px(up.image, 10, 10, 3) == 128);   // alpha scaled on its own
	// The picture itself lines up with the source (the margin was cut off): texel (0,0) of the source.
	const auto direct = resizeLanczos(cropPicture(nearest(padPicture(image, aiMargin(32, 32), EdgeMode::Wrap, EdgeMode::Wrap), 4, nullptr, nullptr).value(),
	                                              aiMargin(32, 32) * 4, aiMargin(32, 32) * 4, 128, 128),
	                                  64, 64);
	CHECK(px(up.image, 5, 5, 0) == px(direct, 5, 5, 0));

	const AiUpscaler broken = [](const ImageRgba&, unsigned, const std::atomic<bool>*, std::string* error) -> std::optional<ImageRgba> {
		if (error) *error = "no Vulkan device";
		return std::nullopt;
	};
	const auto fallback = upscalePicture(image, 64, 64, UpscaleMethod::Ai, 0.35, EdgeMode::Wrap, EdgeMode::Wrap, &broken);
	CHECK_FALSE(fallback.usedAi);
	CHECK(fallback.note == "no Vulkan device");
	CHECK(fallback.image.width == 64);
	const auto none = upscalePicture(image, 64, 64, UpscaleMethod::Ai, 0.35, EdgeMode::Wrap, EdgeMode::Wrap, nullptr);
	CHECK(none.note.find("no AI upscaler") != std::string::npos);

	const auto args = realEsrganArguments("in.png", "out.png", "models");
	CHECK(std::find(args.begin(), args.end(), std::string(REAL_ESRGAN_MODEL)) != args.end());
	CHECK(std::find(args.begin(), args.end(), "4") != args.end());
}

TEST_CASE("texture enhance: transforms, layer and unlayer") {
	CHECK(mappingTransformText(2, 2) == "center 0 0 scale 2 2 rotate 0 translate 0 0");
	const std::string vmt = "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/wall01\"\n\t\"$bumpmap\" \"brick/wall01_normal\"\n\t\"$surfaceprop\" \"brick\"\n}\n";
	EnhancedMaterial record;
	record.material = "brick/wall01";
	record.scaleU = record.scaleV = 2;
	record.renamed["$basetexture"] = "brick/wall01_mapsmith_hd";
	const std::string layered = layerMaterialText(vmt, record);
	const auto m = parseMaterial(layered, "brick/wall01");
	REQUIRE(m);
	CHECK(m->param("$basetexture") == "brick/wall01_mapsmith_hd");
	CHECK(m->param("$basetexturetransform") == "center 0 0 scale 2 2 rotate 0 translate 0 0");
	CHECK(m->param("$bumptransform") == "center 0 0 scale 2 2 rotate 0 translate 0 0");   // the bump map's coordinates shrink as well
	CHECK(m->param("$envmapmasktransform").empty());
	CHECK(m->param("$surfaceprop") == "brick");
	CHECK(mappingScale(*m) == std::pair{2.0, 2.0});
	CHECK(onlyMappingTransforms(*m));
	CHECK(hasLayer(layered, record));
	const auto back = parseMaterial(unlayerMaterialText(layered, record), "brick/wall01");
	REQUIRE(back);
	CHECK(back->params == parseMaterial(vmt, "brick/wall01")->params);
	CHECK_FALSE(hasLayer(vmt, record));

	// A generated material keeps its marker line on top (it stays generated for deploy.bat and Game enhancements).
	const std::string generated = std::string(DEPLOY_MARKER) + " (parallax) from materials/brick/wall01.vmt\r\n\"Fidelity_Parallax\"\r\n{\r\n\t\"$basetexture\" \"brick/wall01\"\r\n}\r\n";
	const std::string generatedLayered = layerMaterialText(generated, record);
	CHECK(generatedLayered.starts_with(DEPLOY_MARKER));
	CHECK(isGeneratedMaterial(generatedLayered));
	CHECK(unlayerMaterialText(generatedLayered, record).starts_with(DEPLOY_MARKER));

	// Someone else's transform is not ours to judge; identities are fine.
	const auto other = parseMaterial("\"LightmappedGeneric\"\n{\n\t\"$basetexturetransform\" \"center .5 .5 scale 1 1 rotate 45 translate 0 0\"\n}\n", "x");
	CHECK_FALSE(onlyMappingTransforms(*other));
	CHECK(mappingScale(*other) == std::pair{1.0, 1.0});
	const auto identity = parseMaterial("\"LightmappedGeneric\"\n{\n\t\"$basetexturetransform\" \"center .5 .5 scale 1 1 rotate 0 translate 0 0\"\n}\n", "x");
	CHECK(onlyMappingTransforms(*identity));
}

TEST_CASE("texture enhance: the record reads back and drops unsafe paths") {
	TextureManifest manifest;
	EnhancedMaterial a;
	a.material = "brick/wall01";
	a.scaleU = a.scaleV = 4;
	a.renamed["$basetexture"] = "brick/wall01_mapsmith_hd";
	a.bumpName = "brick/wall01_mapsmith_bump";
	a.ownCopy = true;
	a.files = {"materials/brick/wall01_mapsmith_hd.vtf", "materials/brick/wall01_mapsmith_bump.vtf"};
	manifest.put(a);
	EnhancedMaterial b;
	b.material = "models/props/crate";
	b.files = {"materials/models/props/crate.vtf"};
	manifest.put(b);
	const auto again = parseTextureManifest(writeTextureManifest(manifest));
	REQUIRE(again.materials.size() == 2);
	CHECK(*again.find("brick/wall01") == a);
	CHECK(*again.find("models/props/crate") == b);

	const auto hostile = parseTextureManifest(
	    "\"mapsmith_textures\"\n{\n\t\"../../evil\"\n\t{\n\t\t\"file\" \"materials/x.vtf\"\n\t}\n\t\"ok\"\n\t{\n\t\t\"file\" \"../../bin/client.dll\"\n\t\t\"file\" "
	    "\"materials/../../x.vtf\"\n\t\t\"file\" \"c:/windows/x.vtf\"\n\t\t\"file\" \"cfg/autoexec.vtf\"\n\t\t\"file\" \"materials/fine.vtf\"\n\t\t\"scale\" \"1e9 -3\"\n\t}\n}\n");
	REQUIRE(hostile.materials.size() == 1);
	CHECK(hostile.materials[0].material == "ok");
	CHECK(hostile.materials[0].files == std::vector<std::string>{"materials/fine.vtf"});
	CHECK(hostile.materials[0].scaleU == 1);
	CHECK(parseTextureManifest("not { keyvalues").materials.empty());
}

TEST_CASE("texture enhance: world, model and skipped materials in a mod, written and removed exactly") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "enhance/hl2";
	const auto mod = f.root / "enhance/mymod";
	std::filesystem::create_directories(mod / "materials");
	// A brick wall (world), a crate (model), a decal, a tool, an animated texture's material.
	test::writeText(game / "materials/brick/wall01.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/wall01\"\n\t\"$surfaceprop\" \"brick\"\n}\n");
	writeVtf(game / "materials/brick/wall01.vtf", pattern(64));
	test::writeText(game / "materials/models/props/crate.vmt", "\"VertexLitGeneric\"\n{\n\t\"$basetexture\" \"models/props/crate\"\n}\n");
	writeVtf(game / "materials/models/props/crate.vtf", pattern(32));
	test::writeText(game / "materials/decals/splat.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"decals/splat\"\n\t\"$decal\" 1\n}\n");
	writeVtf(game / "materials/decals/splat.vtf", pattern(32));
	test::writeText(game / "materials/tools/toolsnodraw.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"tools/toolsnodraw\"\n\t\"%compilenodraw\" 1\n}\n");
	test::writeText(game / "materials/glass/window.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"glass/window\"\n\t\"$basetexturetransform\" \"center .5 .5 scale 2 2 rotate 30 translate 0 0\"\n}\n");
	writeVtf(game / "materials/glass/window.vtf", pattern(32));
	// The mod's own material (hand-written): never changed.
	test::writeText(mod / "materials/concrete/mine.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"concrete/mine\"\n}\n");
	writeVtf(mod / "materials/concrete/mine.vtf", pattern(32));
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});

	EnhanceOptions options;
	options.factor = 2;
	options.method = UpscaleMethod::Sharp;
	CHECK(planEnhancement(fs, mod, "decals/splat", options).skipped.find("decal") != std::string::npos);
	CHECK(planEnhancement(fs, mod, "tools/toolsnodraw", options).skipped == "a tool material");
	CHECK(planEnhancement(fs, mod, "glass/window", options).skipped.find("own texture transform") != std::string::npos);
	CHECK(planEnhancement(fs, mod, "concrete/mine", options).skipped.find("own material") != std::string::npos);
	CHECK(planEnhancement(fs, mod, "../../evil", options).skipped == "not a material name");
	CHECK(planEnhancement(fs, mod, "nothing/here", options).skipped == "material not found");

	const auto wall = planEnhancement(fs, mod, "brick/wall01", options);
	REQUIRE(wall.skipped.empty());
	CHECK(wall.kind == EnhanceKind::World);
	REQUIRE(wall.textures.size() == 1);
	CHECK(wall.textures[0].target == "brick/wall01_mapsmith_hd");
	CHECK(wall.textures[0].toWidth == 128);
	CHECK(wall.scaleU == 2);
	CHECK(wall.bumpName == "brick/wall01_mapsmith_bump");
	CHECK(wall.needsMaterialText());
	CHECK(wall.bytesEstimate() > 0);

	const auto crate = planEnhancement(fs, mod, "models/props/crate", options);
	REQUIRE(crate.skipped.empty());
	CHECK(crate.kind == EnhanceKind::Model);
	REQUIRE(crate.textures.size() == 1);
	CHECK(crate.textures[0].target == "models/props/crate");   // at its own path: no transform, no copy
	CHECK(crate.scaleU == 1);

	// Max size: a texture already that large is left alone.
	EnhanceOptions small = options;
	small.maxSize = 64;
	small.bumpMaps = false;
	CHECK(planEnhancement(fs, mod, "brick/wall01", small).skipped.find("already") != std::string::npos);

	const auto wallOut = makeEnhancement(fs, mod, wall, options);
	const auto crateOut = makeEnhancement(fs, mod, crate, options);
	REQUIRE(wallOut.error.empty());
	REQUIRE(crateOut.error.empty());
	std::vector<std::string> problems;
	const auto written = writeEnhancements(mod, {wallOut, crateOut}, &problems, &fs);
	CHECK(problems.empty());
	CHECK(written.size() == 2);
	CHECK(std::filesystem::exists(mod / "materials/brick/wall01_mapsmith_hd.vtf"));
	CHECK(std::filesystem::exists(mod / "materials/brick/wall01_mapsmith_bump.vtf"));
	CHECK(std::filesystem::exists(mod / "materials/models/props/crate.vtf"));
	// The crate's copy only carries its new bump map: same texture name, no transform (0..1 coordinates).
	const auto crateMaterial = loadMaterial(fs, "models/props/crate");
	REQUIRE(crateMaterial);
	CHECK(crateMaterial->param("$basetexture") == "models/props/crate");
	CHECK(crateMaterial->param("$bumpmap") == "models/props/crate_mapsmith_bump");
	CHECK(crateMaterial->param("$basetexturetransform").empty());
	const std::string copy = readAll(mod / "materials/brick/wall01.vmt");
	CHECK(copy.starts_with(TEXTURE_ENHANCE_MARKER));
	CHECK(isGeneratedMaterial(copy));   // a Mapsmith file: deploy.bat leaves it alone
	// The game now loads the larger texture with the transform; its size on the walls stays.
	const auto loaded = loadMaterial(fs, "brick/wall01");
	REQUIRE(loaded);
	CHECK(loaded->param("$basetexture") == "brick/wall01_mapsmith_hd");
	CHECK(loaded->param("$bumpmap") == "brick/wall01_mapsmith_bump");
	const auto hd = readTextureInfo(*fs.read("materials/brick/wall01_mapsmith_hd.vtf"));
	REQUIRE(hd);
	CHECK(hd->width == 128);
	CHECK(materialTextureSize(fs, "brick/wall01") == std::pair{64, 64});
	const auto crateTexture = readTextureInfo(*fs.read("materials/models/props/crate.vtf"));
	REQUIRE(crateTexture);
	CHECK(crateTexture->width == 64);
	// Game enhancements builds on the enhanced original.
	const auto manifest = loadTextureManifest(mod);
	REQUIRE(manifest.find("brick/wall01"));
	const auto original = enhancedOriginal(fs, mod, manifest, "brick/wall01");
	REQUIRE(original);
	CHECK(original->find("wall01_mapsmith_hd") != std::string::npos);
	CHECK_FALSE(original->starts_with(TEXTURE_ENHANCE_MARKER));

	// Again (a different choice): the earlier files that are no longer made go.
	EnhanceOptions noBump = options;
	noBump.bumpMaps = false;
	const auto again = planEnhancement(fs, mod, "brick/wall01", noBump);
	CHECK(again.file == MaterialEnhancement::File::Ours);
	CHECK(writeEnhancements(mod, {makeEnhancement(fs, mod, again, noBump)}, &problems, &fs).size() == 1);
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/wall01_mapsmith_bump.vtf"));
	CHECK(loadMaterial(fs, "brick/wall01")->param("$bumpmap").empty());

	// Remove: as before - the mod's own file untouched.
	CHECK(removeEnhancements(fs, mod, {}, &problems, &fs) == 2);
	CHECK(problems.empty());
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/wall01.vmt"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/brick/wall01_mapsmith_hd.vtf"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/models/props/crate.vtf"));
	CHECK_FALSE(std::filesystem::exists(mod / "materials/models/props/crate.vmt"));
	CHECK_FALSE(std::filesystem::exists(textureManifestFile(mod)));
	CHECK(std::filesystem::exists(mod / "materials/concrete/mine.vmt"));
	CHECK(loadMaterial(fs, "brick/wall01")->param("$basetexture") == "brick/wall01");
}

TEST_CASE("texture enhance: a generated material gets the keys, keeps its marker and loses them on removal") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "enhance2/hl2";
	const auto mod = f.root / "enhance2/mymod";
	test::writeText(game / "materials/brick/wall02.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/wall02\"\n\t\"$bumpmap\" \"brick/wall02_normal\"\n}\n");
	writeVtf(game / "materials/brick/wall02.vtf", pattern(64));
	writeVtf(game / "materials/brick/wall02_normal.vtf", solid(64, 64, 128, 128, 255), true);
	const std::string deployText = std::string(DEPLOY_MARKER) +
	                               " (parallax) from materials/brick/wall02.vmt\r\n\"Fidelity_Parallax\"\r\n{\r\n\t\"$parallaxmap\" \"brick/wall02_height\"\r\n\t\"$basetexture\" "
	                               "\"brick/wall02\"\r\n\t\"$bumpmap\" \"brick/wall02_normal\"\r\n}\r\n";
	test::writeText(mod / "materials/brick/wall02.vmt", deployText);
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});
	EnhanceOptions options;
	options.method = UpscaleMethod::Sharp;
	options.upscaleBumpMaps = true;
	const auto plan = planEnhancement(fs, mod, "brick/wall02", options);
	REQUIRE(plan.skipped.empty());
	CHECK(plan.file == MaterialEnhancement::File::Generated);
	CHECK(plan.bumpName.empty());   // it has one
	REQUIRE(plan.textures.size() == 2);
	CHECK(plan.textures[1].normalMap);
	const auto out = makeEnhancement(fs, mod, plan, options);
	REQUIRE(out.error.empty());
	CHECK(writeEnhancements(mod, {out}, nullptr, &fs).size() == 1);
	const std::string layered = readAll(mod / "materials/brick/wall02.vmt");
	CHECK(layered.starts_with(DEPLOY_MARKER));
	const auto m = parseMaterial(layered.substr(layered.find('\n') + 1), "brick/wall02");
	REQUIRE(m);
	CHECK(m->shader == "Fidelity_Parallax");
	CHECK(m->param("$bumpmap") == "brick/wall02_normal_mapsmith_hd");
	CHECK(m->param("$parallaxmap") == "brick/wall02_height");
	CHECK(onlyMappingTransforms(*m));

	// deploy.bat writes it anew (without the keys): refresh puts them back.
	test::writeText(mod / "materials/brick/wall02.vmt", deployText);
	CHECK(refreshEnhancementLayers(fs, mod, &fs) == std::vector<std::string>{"brick/wall02"});
	CHECK(readAll(mod / "materials/brick/wall02.vmt").find("_mapsmith_hd") != std::string::npos);
	CHECK(refreshEnhancementLayers(fs, mod, &fs).empty());

	CHECK(removeEnhancements(fs, mod, {"brick/wall02"}, nullptr, &fs) == 1);
	const std::string plain = readAll(mod / "materials/brick/wall02.vmt");
	CHECK(plain.starts_with(DEPLOY_MARKER));
	const auto restored = parseMaterial(plain.substr(plain.find('\n') + 1), "brick/wall02");
	REQUIRE(restored);
	CHECK(restored->params == parseMaterial(deployText.substr(deployText.find('\n') + 1), "brick/wall02")->params);
}

TEST_CASE("texture enhance: a reflection mask only where the material reflects without one") {
	const ImageRgba mask = glossMaskFromColor(pattern(32));
	REQUIRE(mask.width == 32);
	int lo = 255, hi = 0;
	for (std::size_t i = 0; i < mask.pixels.size(); i += 4) {
		lo = std::min(lo, std::to_integer<int>(mask.pixels[i]));
		hi = std::max(hi, std::to_integer<int>(mask.pixels[i]));
	}
	CHECK(lo == 0);
	CHECK(hi == 255);

	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "enhance3/hl2";
	const auto mod = f.root / "enhance3/mymod";
	std::filesystem::create_directories(mod);
	test::writeText(game / "materials/metal/floor.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"metal/floor\"\n\t\"$envmap\" \"env_cubemap\"\n}\n");
	writeVtf(game / "materials/metal/floor.vtf", pattern(32));
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});
	EnhanceOptions options;
	options.factor = 1;
	options.bumpMaps = false;
	CHECK(planEnhancement(fs, mod, "metal/floor", options).skipped == "nothing to do");
	options.glossMasks = true;
	const auto plan = planEnhancement(fs, mod, "metal/floor", options);
	REQUIRE(plan.skipped.empty());
	CHECK(plan.maskName == "metal/floor_mapsmith_mask");
	CHECK(plan.scaleU == 1);
	const auto out = makeEnhancement(fs, mod, plan, options);
	REQUIRE(out.error.empty());
	writeEnhancements(mod, {out}, nullptr, &fs);
	CHECK(loadMaterial(fs, "metal/floor")->param("$envmapmask") == "metal/floor_mapsmith_mask");
	CHECK(loadMaterial(fs, "metal/floor")->param("$basetexturetransform").empty());
}

TEST_CASE("texture enhance: Game enhancements keeps the enhanced copy instead of removing a generated material") {
	test::Fixture f;
	test::createFixture(f);
	const auto game = f.root / "enhance4/hl2";
	const auto mod = f.root / "enhance4/mymod";
	test::writeText(game / "materials/brick/wall04.vmt", "\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"brick/wall04\"\n}\n");
	writeVtf(game / "materials/brick/wall04.vtf", pattern(64));
	GameFileSystem fs;
	fs.addDirectory(mod, {"game", "mod"});
	fs.addDirectory(game, {"game"});
	EnhanceOptions options;
	options.method = UpscaleMethod::Sharp;
	options.bumpMaps = false;
	const auto plan = planEnhancement(fs, mod, "brick/wall04", options);
	REQUIRE(plan.skipped.empty());
	REQUIRE(writeEnhancements(mod, {makeEnhancement(fs, mod, plan, options)}, nullptr, &fs).size() == 1);
	// Game enhancements made relief from it (deploy.bat or Mapsmith) - a generated material on the
	// mod's shader DLL. Without the DLL the plan removes such materials: the enhanced copy comes back.
	const std::string relief = std::string(PATTERN_RELIEF_MARKER) + " (relief) from materials/brick/wall04.vmt hash 1 \r\n\"Fidelity_Parallax\"\r\n{\r\n" +
	                           "\t\"$basetexture\" \"brick/wall04_mapsmith_hd\"\r\n\t\"$basetexturetransform\" \"" + mappingTransformText(2, 2) + "\"\r\n}\r\n";
	test::writeText(mod / "materials/brick/wall04.vmt", relief);
	EnhancementOptions noDll;
	noDll.shaderDll = false;
	const EffectCandidates none;
	const auto ge = planEnhancements(fs, mod, EnhancementSettings{}, {}, &none, noDll);
	const auto it = std::find_if(ge.changes.begin(), ge.changes.end(), [](const EffectChange& c) { return c.material == "brick/wall04"; });
	REQUIRE(it != ge.changes.end());
	CHECK_FALSE(it->remove);
	CHECK(it->text.starts_with(TEXTURE_ENHANCE_MARKER));
	CHECK(it->text.find("brick/wall04_mapsmith_hd") != std::string::npos);
	CHECK(it->text.find("LightmappedGeneric") != std::string::npos);
	// Written, the same plan has nothing more to do for it.
	test::writeText(mod / "materials/brick/wall04.vmt", it->text);
	const auto again = planEnhancements(fs, mod, EnhancementSettings{}, {}, &none, noDll);
	CHECK(std::none_of(again.changes.begin(), again.changes.end(), [](const EffectChange& c) { return c.material == "brick/wall04"; }));
}

TEST_CASE("texture enhance: time for one 1024 texture (run with --no-skip)" * doctest::skip()) {
	ImageRgba image = pattern(1024);
	const auto t0 = std::chrono::steady_clock::now();
	const auto up = upscalePicture(image, 2048, 2048, UpscaleMethod::Sharp, 0.35, EdgeMode::Wrap, EdgeMode::Wrap, nullptr);
	const auto t1 = std::chrono::steady_clock::now();
	const auto vtf = encodeVtf(up.image, true, false, 4096);
	const auto t2 = std::chrono::steady_clock::now();
	const auto normal = resizeNormalMap(image, 2048, 2048, false);
	const auto t3 = std::chrono::steady_clock::now();
	const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
	MESSAGE("1024 -> 2048 sharp " << ms(t0, t1) << " ms, DXT1 with mipmaps " << ms(t1, t2) << " ms, normal map " << ms(t2, t3) << " ms");
	CHECK(vtf);
	CHECK(normal.width == 2048);
}
