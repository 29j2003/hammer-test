// material.cpp - VMT parsing and patch resolution (see material.h).

#include <sme/material.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <kvpp/KV1.h>

namespace sme {

namespace {

constexpr int MAX_PATCH_DEPTH = 8;

void readParams(const kvpp::KV1ElementReadable<>& block, std::map<std::string, std::string>& params) {
	for (const auto& child : block) {
		if (child.getChildCount() > 0) {
			continue; // Proxies, fallback blocks ("<dx90") etc. are not plain parameters.
		}
		params[toLower(child.getKey())] = std::string(child.getValue());
	}
}

std::optional<Material> loadMaterialRecursive(const GameFileSystem& fs, const std::string& name, int depth, std::string* error) {
	const auto text = fs.readText("materials/" + name + ".vmt");
	if (!text) {
		if (error) {
			*error = "materials/" + name + ".vmt not found";
		}
		return std::nullopt;
	}
	const kvpp::KV1<> kv(*text);
	if (kv.getChildCount() == 0) {
		if (error) {
			*error = "materials/" + name + ".vmt is empty or unreadable";
		}
		return std::nullopt;
	}
	const auto& root = kv[0u];
	if (!iequals(root.getKey(), "patch")) {
		return parseMaterial(*text, name, error);
	}

	// patch { include "materials/base.vmt" insert { ... } replace { ... } }
	const auto includeName = normalizeMaterialName(root["include"].getValue());
	if (includeName.empty() || depth >= MAX_PATCH_DEPTH) {
		if (error) {
			*error = depth >= MAX_PATCH_DEPTH ? "patch materials nested too deep" : "patch material without include";
		}
		return std::nullopt;
	}
	auto base = loadMaterialRecursive(fs, includeName, depth + 1, error);
	if (!base) {
		return std::nullopt;
	}
	base->includeChain.insert(base->includeChain.begin(), includeName);
	for (const char* section : {"insert", "replace"}) {
		if (const auto& block = root[section]; !block.isInvalid()) {
			readParams(block, base->params);
		}
	}
	base->name = name;
	return base;
}

} // namespace

std::string Material::param(std::string_view key) const {
	const auto it = params.find(toLower(key));
	return it == params.end() ? std::string{} : it->second;
}

bool Material::flag(std::string_view key) const {
	const auto value = trim(param(key));
	return !value.empty() && value != "0";
}

std::string Material::previewTexture() const {
	for (const char* key : {"%tooltexture", "$basetexture", "$basetexture2", "$normalmap", "$bumpmap", "$envmapmask"}) {
		if (auto value = param(key); !value.empty()) {
			return normalizeMaterialName(value);
		}
	}
	return {};
}

bool Material::isTool() const {
	if (name.starts_with("tools/")) {
		return true;
	}
	for (const auto& [key, value] : params) {
		if (key.starts_with("%compile") && trim(value) != "0") {
			return true;
		}
	}
	return false;
}

std::string normalizeMaterialName(std::string_view name) {
	auto normalized = normalizePath(name);
	if (normalized.starts_with("materials/")) {
		normalized.erase(0, 10);
	}
	for (const char* extension : {".vmt", ".vtf"}) {
		if (normalized.ends_with(extension)) {
			normalized.erase(normalized.size() - 4);
		}
	}
	return normalized;
}

std::optional<Material> parseMaterial(std::string_view text, std::string_view name, std::string* error) {
	const kvpp::KV1<> kv(text);
	if (kv.getChildCount() == 0) {
		if (error) {
			*error = "material is empty or unreadable";
		}
		return std::nullopt;
	}
	const auto& root = kv[0u];
	Material material;
	material.name = normalizeMaterialName(name);
	material.shader = std::string(root.getKey());
	readParams(root, material.params);
	if (kv.getChildCount() > 1) {
		material.warnings.emplace_back("extra data after the first block is ignored");
	}
	return material;
}

std::optional<Material> loadMaterial(const GameFileSystem& fs, std::string_view name, std::string* error) {
	return loadMaterialRecursive(fs, normalizeMaterialName(name), 0, error);
}

} // namespace sme
