// material.cpp - VMT parsing and patch resolution (see material.h).

#include <sme/material.h>

#include <algorithm>

#include <sme/keyvalues.h>

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
	const std::string raw = param(key);   // trim() views into it: keep it alive
	const auto value = trim(raw);
	return !value.empty() && value != "0";
}

bool Material::reflectsWhenDry() const {
	const std::string raw = param("$envmap");
	const auto envmap = trim(raw);
	return !envmap.empty() && envmap != "0" && !flag("$envmapwetonly");
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
	for (const char* extension : {".vmt", ".vtf", ".spr"}) {   // .spr: old sprite names map to the .vmt
		if (normalized.ends_with(extension)) {
			normalized.erase(normalized.size() - 4);
		}
	}
	return normalized;
}

namespace {

void eraseKey(KvNode& block, std::string_view key) {
	std::erase_if(block.children, [&](const KvNode& child) { return !child.block && iequals(child.key, key); });
}

KvNode& childBlock(KvNode& parent, std::string_view key) {
	for (auto& child : parent.children) {
		if (child.block && iequals(child.key, key)) return child;
	}
	parent.children.push_back(KvNode::makeBlock(std::string(key)));
	return parent.children.back();
}

} // namespace

std::string editMaterialText(std::string_view vmt, std::string_view shader, const std::vector<MaterialEdit>& edits, const Material* base) {
	KvNode root = parseKeyValues(vmt);
	KvNode* material = nullptr;
	for (auto& child : root.children) {
		if (child.block) {
			material = &child;
			break;
		}
	}
	if (!material) {
		root.children.push_back(KvNode::makeBlock(shader.empty() ? "LightmappedGeneric" : std::string(shader)));
		material = &root.children.back();
	}
	if (iequals(material->key, "patch")) {
		KvNode& replace = childBlock(*material, "replace");
		KvNode& insert = childBlock(*material, "insert");
		for (const auto& edit : edits) {
			eraseKey(replace, edit.key);
			eraseKey(insert, edit.key);
			if (edit.value) {
				const bool inBase = base && base->params.contains(toLower(edit.key));
				(inBase ? replace : insert).set(edit.key, *edit.value);
			}
		}
		std::erase_if(material->children, [](const KvNode& child) { return child.block && child.children.empty() && (iequals(child.key, "replace") || iequals(child.key, "insert")); });
	} else {
		if (!shader.empty()) {
			material->key = std::string(shader);
		}
		for (const auto& edit : edits) {
			if (edit.value) {
				material->set(edit.key, *edit.value);
			} else {
				eraseKey(*material, edit.key);
			}
		}
	}
	return writeKeyValues(root);
}

std::string newMaterialText(std::string_view shader, const std::vector<MaterialEdit>& params) {
	KvNode root;
	root.children.push_back(KvNode::makeBlock(shader.empty() ? "LightmappedGeneric" : std::string(shader)));
	for (const auto& edit : params) {
		if (edit.value) root.children.back().set(edit.key, *edit.value);
	}
	return writeKeyValues(root);
}

std::string setMaterialProxy(std::string_view vmt, std::string_view proxy, bool on) {
	KvNode root = parseKeyValues(vmt);
	KvNode* material = nullptr;
	for (auto& child : root.children) {
		if (child.block) {
			material = &child;
			break;
		}
	}
	if (!material || iequals(material->key, "patch")) return std::string(vmt);
	KvNode* proxies = nullptr;
	for (auto& child : material->children) {
		if (child.block && iequals(child.key, "Proxies")) proxies = &child;
	}
	if (on) {
		if (!proxies) {
			material->children.push_back(KvNode::makeBlock("Proxies"));
			proxies = &material->children.back();
		}
		const bool has = std::any_of(proxies->children.begin(), proxies->children.end(), [&](const KvNode& p) { return iequals(p.key, proxy); });
		if (!has) proxies->children.push_back(KvNode::makeBlock(std::string(proxy)));
	} else if (proxies) {
		std::erase_if(proxies->children, [&](const KvNode& p) { return iequals(p.key, proxy); });
		if (proxies->children.empty()) {
			std::erase_if(material->children, [](const KvNode& c) { return c.block && iequals(c.key, "Proxies") && c.children.empty(); });
		}
	}
	return writeKeyValues(root);
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
