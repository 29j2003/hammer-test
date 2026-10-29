// container_models.cpp - Own model copies for containers on shared materials (see container_models.h).

#include <sme/container_models.h>

#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/mod_install.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>

namespace sme {

namespace {

// deploy.ps1's $BottleNames (the same as fidelity_effects' list).
const std::regex BOTTLE_NAMES("(bottle|jug|jar|flask|vial|decanter|carafe|beaker|healthkit|medkit)", std::regex::icase);
constexpr std::int64_t MAX_TEXTURES = 1024;
constexpr std::int64_t MAX_FOLDERS = 64;
constexpr std::size_t HEADER_SIZE = 220;   // up to the $cdmaterials index (offset 216)
const char* const CRLF = "\r\n";

std::int32_t int32At(std::span<const std::byte> data, std::size_t at) {
	std::int32_t value = 0;
	std::memcpy(&value, data.data() + at, sizeof(value));
	return value;
}

/// The zero-terminated string at `at`, nullopt when it runs past the end.
std::optional<std::string> cString(std::span<const std::byte> data, std::size_t at) {
	const auto begin = data.begin() + std::ptrdiff_t(at);
	const auto end = std::find(begin, data.end(), std::byte{0});
	if (end == data.end()) return std::nullopt;
	std::string out;
	out.reserve(std::size_t(end - begin));
	for (auto it = begin; it != end; ++it) out.push_back(char(*it));
	return out;
}

std::string slashesLower(std::string_view text) {
	std::string out = toLower(text);
	std::replace(out.begin(), out.end(), '\\', '/');
	return out;
}

std::string fileName(std::string_view path) {
	const auto slash = path.rfind('/');
	std::string name(slash == std::string_view::npos ? path : path.substr(slash + 1));
	if (const auto dot = name.rfind('.'); dot != std::string::npos) name.erase(dot);
	return name;
}

std::optional<std::string> readFile(const std::filesystem::path& file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return std::nullopt;
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string_view asText(const std::vector<std::byte>& bytes) {
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// A relative path from the manifest that stays inside the mod folder, or empty.
std::string safeManifestPath(std::string_view path) {
	std::string p = normalizePath(trim(path));
	if (p.empty() || p.front() == '/' || p.find(':') != std::string::npos) return {};
	for (const auto& part : splitTrimmed(p, '/')) {
		if (part == "..") return {};
	}
	return p;
}

std::string upperHex(std::string hex) {
	std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return hex;
}

} // namespace

std::optional<MdlMaterialTable> readMdlMaterialTable(std::span<const std::byte> mdl) {
	if (mdl.size() < HEADER_SIZE || std::memcmp(mdl.data(), "IDST", 4) != 0) return std::nullopt;
	const auto size = std::int64_t(mdl.size());
	MdlMaterialTable out;
	std::int64_t count = int32At(mdl, 204);
	std::int64_t index = int32At(mdl, 208);
	if (count < 0 || count > MAX_TEXTURES || index < 0) return std::nullopt;
	for (std::int64_t i = 0; i < count; ++i) {
		const std::int64_t at = index + 64 * i;   // mstudiotexture_t: the name's offset first, from the record
		if (at + 4 > size) return std::nullopt;
		const std::int64_t nameAt = at + int32At(mdl, std::size_t(at));
		if (nameAt <= 0 || nameAt >= size) return std::nullopt;
		auto name = cString(mdl, std::size_t(nameAt));
		if (!name) return std::nullopt;
		out.textures.push_back({std::move(*name), std::size_t(nameAt)});
	}
	count = int32At(mdl, 212);
	index = int32At(mdl, 216);
	if (count < 0 || count > MAX_FOLDERS || index < 0) return std::nullopt;
	for (std::int64_t i = 0; i < count; ++i) {
		const std::int64_t slot = index + 4 * i;
		if (slot + 4 > size) return std::nullopt;
		const std::int64_t at = int32At(mdl, std::size_t(slot));
		if (at <= 0 || at >= size) return std::nullopt;
		auto folder = cString(mdl, std::size_t(at));
		if (!folder) return std::nullopt;
		std::string f = slashesLower(*folder);
		if (!f.empty() && !f.ends_with('/')) f += '/';
		out.folders.push_back(std::move(f));
	}
	return out;
}

ContainerModelScan findContainerModels(const GameFileSystem& fs, const std::filesystem::path& modDir) {
	ContainerModelScan out;
	// Own material names: the last 4 letters of the name replaced (same length in the .mdl).
	// Names that would clash (...001a and ...001b both give ..._flq) take the next free suffix.
	std::vector<std::string> suffixes{std::string(OWN_MATERIAL_SUFFIX)};
	for (int i = 1; i <= 9; ++i) suffixes.push_back("_fq" + std::to_string(i));
	std::map<std::string, std::optional<std::string>> texts;   // the game's materials looked at
	const auto gameMaterial = [&](const std::string& path) -> bool {
		auto it = texts.find(path);
		if (it == texts.end()) it = texts.emplace(path, fs.readOriginal(path, modDir)).first;
		return it->second.has_value();
	};
	for (const auto& key : fs.list("models", ".mdl")) {   // sorted, lower case
		if (key.starts_with("models/props_foliage") || !std::regex_search(fileName(key), BOTTLE_NAMES)) continue;
		const auto original = fs.readOriginal(key, modDir);   // the game's, not the mod's own model
		if (!original) continue;
		std::vector<std::byte> bytes(original->size());
		std::memcpy(bytes.data(), original->data(), original->size());
		const auto table = readMdlMaterialTable(bytes);
		if (!table) continue;
		ContainerModel model;
		model.model = key;
		for (const auto& texture : table->textures) {
			const std::string name = slashesLower(texture.name);
			std::string material;   // where it is: the first $cdmaterials folder that has it
			for (const auto& folder : table->folders) {
				const std::string candidate = "materials/" + folder + name + ".vmt";
				if (gameMaterial(candidate)) {
					material = candidate;
					break;
				}
			}
			// Not found, or a container material anyway (that one gets the liquid shader itself).
			if (material.empty() || std::regex_search(material, BOTTLE_NAMES) || name.size() <= OWN_MATERIAL_SUFFIX.size() + 1) continue;
			std::string own, suffix;
			for (const auto& s : suffixes) {
				std::string candidate = material.substr(0, material.size() - 4 - s.size()) + s + ".vmt";
				const auto taken = out.ownSources.find(candidate);
				if (taken == out.ownSources.end() || taken->second == material) {
					own = std::move(candidate);
					suffix = s;
					break;
				}
			}
			if (own.empty()) {
				out.notes.push_back("kept " + key + " (no free name for its own material)");
				continue;
			}
			out.ownSources[own] = material;
			const std::string ownName = texture.name.substr(0, texture.name.size() - suffix.size()) + suffix;
			std::memcpy(bytes.data() + texture.offset, ownName.data(), ownName.size());   // same length, inside the name read above
			if (std::find(model.own.begin(), model.own.end(), own) == model.own.end()) model.own.push_back(own);
		}
		if (model.own.empty()) continue;
		model.bytes = std::move(bytes);
		out.models.push_back(std::move(model));
	}
	return out;
}

void planContainerModels(const GameFileSystem& fs, const std::filesystem::path& modDir, const EnhancementSettings& settings, bool shaderDll,
                         EnhancementPlan& plan, const ContainerModelScan* scan) {
	const ContainerModelScan found = scan ? ContainerModelScan{} : findContainerModels(fs, modDir);
	const ContainerModelScan& all = scan ? *scan : found;
	plan.modelNotes.insert(plan.modelNotes.end(), all.notes.begin(), all.notes.end());

	// The copies made before (by deploy.bat or Mapsmith): model -> SHA-256 ("" in old lists).
	const auto manifestFile = modDir / std::filesystem::path(std::string(CONTAINER_MODELS_MANIFEST));
	const auto manifestText = readFile(manifestFile);
	std::map<std::string, std::string> listed;
	if (manifestText) {
		for (const auto& line : splitTrimmed(*manifestText, '\n')) {
			if (line.empty() || line.starts_with("//")) continue;
			const auto tab = line.find('\t');
			const std::string path = toLower(safeManifestPath(line.substr(0, tab)));
			if (path.empty()) continue;
			listed[path] = tab == std::string::npos ? std::string() : std::string(trim(std::string_view(line).substr(tab + 1)));
		}
	}
	// A listed copy that is still as it was made (the mod's own model put there later stays).
	const auto unchanged = [&](const std::string& model, const std::string& bytes) {
		const auto it = listed.find(model);
		if (it == listed.end()) return false;
		if (it->second.empty()) return bytes.find(OWN_MATERIAL_SUFFIX) != std::string::npos;
		return iequals(sha256Hex(bytes), it->second);
	};
	std::set<std::filesystem::path> planned;   // files the materials of the plan write already
	for (const auto& change : plan.changes) planned.insert(change.file);
	std::map<std::string, bool> ownDone;   // own material -> it is a liquid one after the plan (shared by several copies)
	std::set<std::string> kept(plan.keptOwn.begin(), plan.keptOwn.end());

	const bool liquidOn = settings.enabled(GameEffect::Liquid);
	std::map<std::string, std::string> manifest;   // the copies after this run: model -> SHA-256
	std::set<std::string> handled;
	for (const auto& model : all.models) {
		plan.containerModels.push_back(model.model);
		handled.insert(model.model);
		const bool want = liquidOn && shaderDll && !settings.isExcluded(GameEffect::Liquid, model.model);
		// Its own materials first: the copy only goes in when each of them is a liquid one (else
		// the game would show the error texture on it).
		bool materialsReady = true;
		for (const auto& own : model.own) {
			if (const auto done = ownDone.find(own); done != ownDone.end()) {
				materialsReady = materialsReady && done->second;
				continue;
			}
			ownDone[own] = false;
			const std::string material = normalizeMaterialName(own);
			const auto file = modDir / "materials" / std::filesystem::path(material + ".vmt");
			const auto existing = readFile(file);
			if (existing && !isGeneratedMaterial(*existing)) {
				// The mapper's own material by that name: never touched.
				if (kept.insert(material).second) plan.keptOwn.push_back(material);
				materialsReady = false;
				continue;
			}
			if (!shaderDll) {
				materialsReady = false;   // the generated liquid materials go with the others (planEnhancements)
				continue;
			}
			const auto source = all.ownSources.find(own);
			const auto original = source == all.ownSources.end() ? std::nullopt : fs.readOriginal(source->second, modDir);
			if (!original || planned.count(file)) {
				materialsReady = false;
				continue;
			}
			std::string text;
			if (want) {
				auto made = makeEffectMaterial(GameEffect::Liquid, own, *original, ContainerKind::Tinted, DEPLOY_MARKER);
				if (!made) {
					plan.modelNotes.push_back("kept " + model.model + " (its material is no VertexLitGeneric one)");
					materialsReady = false;
					continue;
				}
				text = std::move(*made);
				++plan.active[GameEffect::Liquid];
				ownDone[own] = true;
			} else {
				// A copy of the original keeps deploy.bat from making it (and so the model copy).
				materialsReady = false;
				text = std::string(MAPSMITH_MARKER) + " off (liquid) from " + own + " (Mapsmith > Project > Game enhancements switches it on again)" +
				       CRLF + *original;
			}
			planned.insert(file);
			if (existing && *existing == text) continue;   // already so
			EffectChange change;
			change.effect = GameEffect::Liquid;
			change.material = material;
			change.file = file;
			change.text = std::move(text);
			plan.changes.push_back(std::move(change));
		}

		// The copy of the model.
		const auto target = modDir / toNativePath(model.model);
		const auto current = readFile(target);
		if (current && !unchanged(model.model, *current)) {
			plan.modelNotes.push_back("kept " + model.model + (listed.count(model.model) ? " (changed since it was made)" : " (the mod has its own)"));
			continue;
		}
		if (want && materialsReady) {
			manifest[model.model] = upperHex(sha256Hex(asText(model.bytes)));
			plan.liquidModels.push_back(model.model);
			if (current && *current == asText(model.bytes)) continue;   // already so
			plan.modelChanges.push_back({model.model, target, false, model.bytes});
		} else if (current) {
			plan.modelChanges.push_back({model.model, target, true, {}});
		}
	}
	// Copies of models that are no containers on shared materials any more (or no longer there).
	for (const auto& [model, hash] : listed) {
		if (handled.count(model)) continue;
		const auto target = modDir / toNativePath(model);
		const auto current = readFile(target);
		if (!current) continue;
		if (unchanged(model, *current)) plan.modelChanges.push_back({model, target, true, {}});
		else plan.modelNotes.push_back("kept " + model + " (changed since it was made)");
	}

	// The list, in deploy.ps1's form (written before the models, see the caller).
	if (manifest.empty()) {
		if (manifestText) plan.modelsManifest = std::string();
		return;
	}
	std::string text = std::string(CONTAINER_MODELS_HEADER) + CRLF;
	for (const auto& [model, hash] : manifest) text += model + "\t" + hash + CRLF;
	if (!manifestText || *manifestText != text) plan.modelsManifest = std::move(text);
}

} // namespace sme
