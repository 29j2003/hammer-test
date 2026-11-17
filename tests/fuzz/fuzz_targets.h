// fuzz_targets.h - What each libFuzzer target in tests/fuzz feeds its input to. The fuzz_*.cpp
// files call these, and so does the unit test "fuzz: saved findings" (test_fuzz.cpp), which runs
// every input in tests/fuzz/regressions/<target>/ - each one once crashed, hung or ran out of
// memory; now none may.

#pragma once

#include "fuzz_common.h"

#include <sme/animation_import.h>
#include <sme/asset_import.h>
#include <sme/assets.h>
#include <sme/audio.h>
#include <sme/building_builder.h>
#include <sme/checks.h>
#include <sme/choreography.h>
#include <sme/compile_log.h>
#include <sme/container_models.h>
#include <sme/editor_report.h>
#include <sme/entity_catalog.h>
#include <sme/facade_texture.h>
#include <sme/fidelity_effects.h>
#include <sme/fidelity_look.h>
#include <sme/filesystem.h>
#include <sme/git_status.h>
#include <sme/hammer_config.h>
#include <sme/keyvalues.h>
#include <sme/localization.h>
#include <sme/map.h>
#include <sme/material.h>
#include <sme/material_decisions.h>
#include <sme/mod_features.h>
#include <sme/mod_menu.h>
#include <sme/model_export.h>
#include <sme/model_import.h>
#include <sme/particles.h>
#include <sme/recent_projects.h>
#include <sme/relief_pattern.h>
#include <sme/shipping.h>
#include <sme/soundscape.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>
#include <sme/texture.h>

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

namespace sme::fuzz {

/// Textures (.vtf) as the material browser and the viewport read them.
inline void vtf(const std::uint8_t* data, std::size_t size) {
	const auto in = bytes(data, size);
	(void)readTextureInfo(in);
	(void)decodeTexture(in, 64);
}

/// VPK archives (a single-file *_dir.vpk) mounted like a game's search path: index, list, read
/// the first files, path checks on the names inside.
inline void vpk(const std::uint8_t* data, std::size_t size) {
	static ScratchFile file("pak01_dir.vpk");
	file.write(data, size);
	GameFileSystem fs;
	fs.addVpk(file.path(), {"game"});
	if (fs.mounts().empty()) return;
	(void)fs.fileCount(0);
	const auto files = fs.list("", {});
	for (std::size_t i = 0; i < files.size() && i < 16; ++i) (void)fs.read(files[i]);
	(void)fs.read("materials/../../../escape.txt");
	(void)fs.exists("scripts/soundscapes_manifest.txt");
}

/// Models: .mdl + .vvd + .vtx + .ani in one input (fuzz_common.h): geometry, info, materials,
/// attachments, hitboxes, skins, skeleton and animation.
inline void studio(const std::uint8_t* data, std::size_t size) {
	auto parts = splitParts(data, size, 4);
	const auto& mdl = parts[0];
	const auto& vvd = parts[1];
	const auto& vtx = parts[2];
	(void)readModelInfo(mdl);
	(void)readAttachments(mdl);
	(void)readHitboxes(mdl);
	(void)readSkinFamilies(mdl);
	(void)readMdlMaterialTable(mdl);
	(void)buildModelGeometry({mdl, vvd, vtx}, 1, nullptr, {}, 3);
	(void)buildBodyPartGeometry({mdl, vvd, vtx}, 0, 1);
	if (auto model = AnimatedModel::fromFiles({{"models/fuzz.mdl", mdl, parts[3]}})) {
		const int sequences = int(std::min<std::size_t>(model->sequences().size(), 8));
		for (int s = 0; s < sequences; ++s) {
			(void)model->boneToModel(s, 0.37f);
			(void)model->skinning(s, 0.99f);
		}
	}
}

/// Physics models (.phy) and their export to .smd.
inline void phy(const std::uint8_t* data, std::size_t size) {
	if (const auto physics = readPhysicsModel(bytes(data, size))) (void)physicsSmd(*physics, {});
}

/// Particle files (.pcf, binary DMX) for the particle browser.
inline void pcf(const std::uint8_t* data, std::size_t size) {
	const auto in = bytes(data, size);
	(void)readParticleDefinitions(in);
	(void)particleSystemNames(in);
}

/// Sounds (WAV/ADPCM, MP3, OGG, FLAC). The first byte picks the file name the decoder is told.
inline void audio(const std::uint8_t* data, std::size_t size) {
	if (size < 1) return;
	static const char* const names[] = {"x.wav", "x.mp3", "x.ogg", "x.flac"};
	const auto in = bytes(data + 1, size - 1);
	(void)decodeAudio(in, names[data[0] % 4]);
	(void)soundFileLoops(in);
}

/// Pictures the importers read (PNG, JPEG, TGA, BMP) and the relief analysis on small results.
inline void image(const std::uint8_t* data, std::size_t size) {
	if (const auto picture = decodeImageFile(bytes(data, size))) {
		if (std::size_t(picture->width) * picture->height <= 128 * 128) {
			ReliefLayout layout;
			(void)heightFromTexture(*picture, &layout);
			(void)reliefFitByLook(*picture);
		}
	}
}

/// Compiled maps (.bsp) as the compile window reads them (cubemap samples, pakfile).
inline void bsp(const std::uint8_t* data, std::size_t size) {
	(void)readBspCubemaps(bytes(data, size), "test");
}

/// Maps (.vmf): parse, save again and read what was saved.
inline void vmf(const std::uint8_t* data, std::size_t size) {
	try {
		const auto doc = MapDocument::parse(text(data, size));
		(void)MapDocument::parse(doc.serialize());
	} catch (const std::exception&) {
		// A broken map is reported as an exception (the editor shows the message).
	}
}

/// Every text format of a game or mod that is not a map; the first byte picks the reader.
inline void textFormats(const std::uint8_t* data, std::size_t size) {
	if (size < 1) return;
	const std::string_view in = text(data + 1, size - 1);
	try {
		switch (data[0] % 20) {
		case 0: (void)parseKeyValues(in); break;
		case 1: (void)parseMaterial(in, "fuzz/material"); break;
		case 2: (void)parseHammerConfig(in, "GameConfig.txt"); break;
		case 3:
			for (const auto& scape : parseSoundscapes(in)) (void)upsertSoundscape(in, scape);
			(void)removeSoundscape(in, "town.street");
			break;
		case 4: {
			const auto file = parseLocalizationFile(in);
			(void)writeLocalizationFile(file);
			(void)compileCaptions(file);
			(void)parseLocalization(in);
			break;
		}
		case 5: (void)parseCaption(in); break;
		case 6: (void)writeGameMenu(parseGameMenu(in)); break;
		case 7:
			(void)writeChapterBackgrounds(parseChapterBackgrounds(in));
			(void)setChapterMap(in, "d1_town");
			break;
		case 8:
			(void)gameInfoValue(in, "title");
			(void)setGameInfoValue(in, "title", "NEW TITLE");
			break;
		case 9: (void)parseVcd(in); break;
		case 10: (void)MaterialDecisions::parse(in).lookup("brick/wall01"); break;
		case 11:
			(void)parseEnhancementSettings(in);
			(void)parseCfgValues(in);
			(void)parseFeaturesCfg(in);
			(void)parseRecentProjects(in);
			break;
		case 12:
			(void)parsePickLog(in);
			(void)parseSettings(in);
			(void)parseSolids(in);
			(void)parseFaces(in);
			break;
		case 13:
			(void)parseGitStatus(in);
			(void)parseGitLog(in);
			(void)remoteWebUrl(in);
			break;
		case 14:
			(void)classifyCompileLine(in);
			(void)readPointfile(in);
			break;
		case 15:
			(void)parseSmd(in);
			(void)readSmd(in);
			(void)readAnimationList(in);
			break;
		case 16: {
			const auto split = in.find('\0');
			const auto obj = in.substr(0, split);
			const auto mtl = split == std::string_view::npos ? std::string_view() : in.substr(split + 1);
			if (auto mesh = readObj(obj, mtl)) {
				ModelImportOptions options;
				const auto placed = placedMesh(*mesh, options);
				if (placed.triangles.size() <= 2000) (void)collisionMesh(placed, ModelImportOptions::Collision::Hull);
				(void)meshVolume(placed);
			}
			break;
		}
		case 17:
			(void)decodeBuildingOptions(in);
			(void)decodeFacadeWindows(in);
			break;
		case 18:
			(void)decodeChoreoStep(in);
			(void)decodeChoreoStart(in);
			(void)decodeChoreoCondition(in);
			break;
		case 19:
			(void)parseModelVariant(in);
			(void)decodeTextFile(Bytes(reinterpret_cast<const std::byte*>(in.data()), reinterpret_cast<const std::byte*>(in.data() + in.size())));
			break;
		}
	} catch (const std::exception&) {
		// Some readers report broken syntax as an exception; the editor shows the message.
	}
}

/// Entity definitions (.fgd) of a game or mod.
inline void fgd(const std::uint8_t* data, std::size_t size) {
	static ScratchFile file("mod.fgd");
	file.write(data, size);
	EntityCatalog catalog;
	std::vector<std::string> errors;
	if (catalog.load({file.path()}, &errors)) {
		const auto placeable = catalog.placeable();
		for (std::size_t i = 0; i < placeable.size() && i < 8; ++i) (void)catalog.find(placeable[i]->name);
	}
}

/// Mapsmith's own ZIP reader (editor reports replayed by sme-probe, packages).
inline void zip(const std::uint8_t* data, std::size_t size) {
	static ScratchFile file("report.zip");
	file.write(data, size);
	(void)zipEntryNames(file.path());
	(void)readZipEntry(file.path(), "scene.vmf");
}

/// Every target by its folder name (corpus and regressions/<name>).
inline const std::vector<std::pair<std::string_view, void (*)(const std::uint8_t*, std::size_t)>>& targets() {
	static const std::vector<std::pair<std::string_view, void (*)(const std::uint8_t*, std::size_t)>> all = {
		{"vtf", vtf}, {"vpk", vpk}, {"studio", studio}, {"phy", phy}, {"pcf", pcf}, {"audio", audio},
		{"image", image}, {"bsp", bsp}, {"vmf", vmf}, {"text", textFormats}, {"fgd", fgd}, {"zip", zip}};
	return all;
}

} // namespace sme::fuzz
