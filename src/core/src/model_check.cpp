// model_check.cpp - Checks a compiled model before/after a build (see model_check.h).

#include <sme/model_check.h>

#include <sme/anim_edit.h>
#include <sme/assets.h>
#include <sme/container_models.h>
#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/model_events.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <cstring>

namespace sme {

namespace {

std::string stem(std::string_view modelPath) {
	std::string path = normalizePath(modelPath);
	if (path.size() > 4 && toLower(path.substr(path.size() - 4)) == ".mdl") path.resize(path.size() - 4);
	return path;
}

bool powerOfTwo(unsigned v) {
	return v > 0 && (v & (v - 1)) == 0;
}

void add(std::vector<ModelIssue>& out, ModelIssue::Level level, std::string code, std::string message, std::string fix = {}, std::string subject = {}) {
	out.push_back({level, std::move(code), std::move(message), std::move(fix), std::move(subject)});
}

/// First word of an event's options (the particle system of AE_CL_CREATE_PARTICLE_EFFECT).
std::string firstWord(std::string_view text) {
	const auto begin = text.find_first_not_of(" \t");
	if (begin == std::string_view::npos) return {};
	const auto end = text.find_first_of(" \t", begin);
	return std::string(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
}

} // namespace

std::optional<int> vvdVertexCount(std::span<const std::byte> vvd) {
	// vertexFileHeader_t: id, version, checksum, numLODs, numLODVertexes[8] ... (public/studio.h)
	if (vvd.size() < 48) return std::nullopt;
	std::int32_t id = 0, lods = 0, count = 0;
	std::memcpy(&id, vvd.data(), 4);
	std::memcpy(&lods, vvd.data() + 12, 4);
	std::memcpy(&count, vvd.data() + 16, 4);
	if (id != 0x56534449 /* "IDSV" */ || lods < 0 || lods > 8 || count < 0) return std::nullopt;
	return int(count);
}

std::vector<ModelIssue> checkModel(const GameFileSystem& fs, std::string_view modelPath, const ModelCheckContext& context) {
	using L = ModelIssue::Level;
	std::vector<ModelIssue> out;
	const std::string base = stem(modelPath);
	const std::string mdlPath = base + ".mdl";
	const auto mdl = fs.read(mdlPath);
	if (!mdl) {
		add(out, L::Error, "missing_file", "The model file " + mdlPath + " is missing", "Pick another model or build it again", mdlPath);
		return out;
	}
	const auto info = readModelInfo(*mdl);
	if (!info) {
		add(out, L::Error, "missing_file", "The model file is damaged or of an unknown version", "Build the model again", mdlPath);
		return out;
	}

	// Files and checksums.
	const auto vvd = fs.read(base + ".vvd");
	const auto dx90 = fs.read(base + ".dx90.vtx");
	const auto dx80 = fs.read(base + ".dx80.vtx");
	const auto sw = fs.read(base + ".sw.vtx");
	const auto phy = fs.read(base + ".phy");
	ModelChecksums sums;
	sums.mdl = mdlChecksum(*mdl);
	if (vvd) sums.vvd = vvdChecksum(*vvd);
	if (dx90) sums.dx90 = vtxChecksum(*dx90);
	if (dx80) sums.dx80 = vtxChecksum(*dx80);
	if (sw) sums.sw = vtxChecksum(*sw);
	if (phy) sums.phy = phyChecksum(*phy);
	if (!vvd) add(out, L::Error, "missing_file", "The vertex file " + base + ".vvd is missing", "Build the model again", base + ".vvd");
	if (!dx90) add(out, L::Error, "missing_file", "The mesh file " + base + ".dx90.vtx is missing", "Build the model again", base + ".dx90.vtx");
	const auto mismatch = [&](const std::optional<int>& sum, std::string_view ext) {
		if (sum && sums.mdl && *sum != *sums.mdl) {
			add(out, L::Error, "checksum", "The " + std::string(ext) + " file does not belong to this .mdl (different checksum) - the game refuses the model",
			    "Build the model again, or restore it", base + std::string(ext));
		}
	};
	mismatch(sums.vvd, ".vvd");
	mismatch(sums.dx90, ".dx90.vtx");
	mismatch(sums.dx80, ".dx80.vtx");
	mismatch(sums.sw, ".sw.vtx");
	mismatch(sums.phy, ".phy");

	// Limits.
	if (info->boneCount > MODEL_MAX_BONES) {
		add(out, L::Error, "bones", "The model has " + std::to_string(info->boneCount) + " bones; the engine allows " + std::to_string(MODEL_MAX_BONES),
		    "Remove bones in the source files (or split the model)");
	}
	if (vvd) {
		if (const auto count = vvdVertexCount(*vvd)) {
			if (*count > MODEL_MAX_VERTICES) {
				add(out, L::Error, "vertices", "The model has " + std::to_string(*count) + " vertices; the engine allows " + std::to_string(MODEL_MAX_VERTICES),
				    "Use fewer polygons or split the model");
			} else if (*count > MODEL_MANY_VERTICES) {
				add(out, L::Warning, "vertices", "The model has " + std::to_string(*count) + " vertices - slow when many are on screen", "Add LODs ($lod) or use fewer polygons");
			}
		}
	}
	if (int(info->materials.size()) > MODEL_MAX_SKIN_TEXTURES) {
		add(out, L::Error, "skins", "The model uses " + std::to_string(info->materials.size()) + " textures; the engine allows " + std::to_string(MODEL_MAX_SKIN_TEXTURES),
		    "Use fewer materials");
	}

	// Materials: each texture through the $cdmaterials folders, as the engine looks them up.
	std::set<std::string> checkedTextures;
	for (const auto& texture : info->materials) {
		std::optional<Material> found;
		std::string foundName;
		for (const auto& dir : info->materialDirs) {
			std::string folder = normalizePath(dir);
			if (!folder.empty() && folder.back() != '/') folder += '/';
			const std::string name = toLower(folder + normalizePath(texture));
			if (!isSafeGamePath("materials/" + name + ".vmt")) continue;
			if (fs.exists("materials/" + name + ".vmt")) {
				found = loadMaterial(fs, name);
				foundName = name;
				break;
			}
		}
		if (foundName.empty()) {
			add(out, L::Warning, "material", "Material \"" + texture + "\" is missing - the game shows a purple/black checkerboard",
			    "Add the material to the mod, or fix $cdmaterials", texture);
			continue;
		}
		if (!found) continue;
		const std::string baseTexture = toLower(normalizePath(found->param("$basetexture")));
		if (baseTexture.empty() || !checkedTextures.insert(baseTexture).second) continue;
		const std::string vtf = "materials/" + baseTexture + ".vtf";
		if (!isSafeGamePath(vtf)) continue;
		const auto data = fs.read(vtf);
		if (!data) {
			add(out, L::Warning, "texture", "Texture \"" + baseTexture + "\" of material \"" + foundName + "\" is missing", "Add the texture to the mod", baseTexture);
			continue;
		}
		if (const auto tex = readTextureInfo(*data); tex && (!powerOfTwo(tex->width) || !powerOfTwo(tex->height))) {
			add(out, L::Warning, "texture_size",
			    "Texture \"" + baseTexture + "\" is " + std::to_string(tex->width) + " x " + std::to_string(tex->height) + " - not a power of two (blurry or wrong mipmaps)",
			    "Scale it to 256, 512, 1024 ... (Enhance textures does that)", baseTexture);
		}
	}

	// Physics.
	if (!phy && toLower(info->keyValues).find("prop_data") != std::string::npos) {
		add(out, L::Warning, "physics", "The model is set up as a physics prop (prop_data) but has no .phy - it falls through the world",
		    "Add $collisionmodel to its .qc and build it again");
	}

	// Events.
	for (const auto& sequence : readSequenceEvents(*mdl)) {
		for (const auto& event : sequence.events) {
			if (context.soundNames && isSoundEvent(event)) {
				const std::string sound = stripSoundChars(trim(event.options));
				if (sound.empty()) continue;
				const std::string lower = toLower(sound);
				const bool wave = lower.ends_with(".wav") || lower.ends_with(".mp3");
				const bool known = wave ? (isSafeGamePath("sound/" + lower) && fs.exists("sound/" + lower)) : context.soundNames->contains(lower);
				if (!known) {
					add(out, L::Warning, "sound", "Sequence \"" + sequence.sequence + "\" plays sound \"" + sound + "\", which the game does not have",
					    "Pick another sound on the Sounds tab", sound);
				}
			}
			const std::string id = toLower(event.id());
			if (context.particleNames && (id == "ae_cl_create_particle_effect" || id == "ae_cl_stop_particle_effect")) {
				const std::string system = firstWord(event.options);
				if (!system.empty() && !context.particleNames->contains(toLower(system))) {
					add(out, L::Warning, "particle", "Sequence \"" + sequence.sequence + "\" starts particle system \"" + system + "\", which no particle file has",
					    "Pick another effect or add its .pcf to the mod", system);
				}
			}
		}
	}
	return out;
}

bool hasModelErrors(const std::vector<ModelIssue>& issues) {
	for (const auto& issue : issues) {
		if (issue.level == ModelIssue::Level::Error) return true;
	}
	return false;
}

std::string modelCheckSummary(const std::vector<ModelIssue>& issues) {
	int errors = 0, warnings = 0;
	for (const auto& issue : issues) {
		if (issue.level == ModelIssue::Level::Error) ++errors;
		if (issue.level == ModelIssue::Level::Warning) ++warnings;
	}
	if (errors == 0 && warnings == 0) return "No problems found";
	std::string out;
	if (errors) out += std::to_string(errors) + (errors == 1 ? " error" : " errors");
	if (warnings) out += std::string(out.empty() ? "" : ", ") + std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
	return out;
}

} // namespace sme
