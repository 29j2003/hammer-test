// material.h - Reads VMT materials, including "patch" materials that extend another VMT.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

class GameFileSystem;

/// A material after patch resolution. Parameter names are lowercase ("$basetexture").
struct Material {
	std::string name;                              ///< e.g. "concrete/concretewall001a"
	std::string shader;                            ///< e.g. "LightmappedGeneric"
	std::map<std::string, std::string> params;     ///< Top-level parameters only
	std::vector<std::string> includeChain;         ///< VMTs a patch material pulled in, in order
	std::vector<std::string> warnings;

	/// Value of a top-level parameter (case-insensitive, "$basetexture"), empty if missing.
	[[nodiscard]] std::string param(std::string_view key) const;
	[[nodiscard]] bool flag(std::string_view key) const;  ///< "$translucent" "1" etc.

	/// Texture shown in the editor: %tooltexture, then $basetexture, then other fallbacks.
	[[nodiscard]] std::string previewTexture() const;

	/// Tool materials (nodraw, clip, trigger, ...) - %compile* keys or the tools/ folder.
	[[nodiscard]] bool isTool() const;
};

/// One change to a material parameter: a new value, or removed (nullopt).
struct MaterialEdit {
	std::string key;                    ///< "$basetexture"
	std::optional<std::string> value;
};

/// The VMT text with its shader and parameters changed; everything else (proxies, other blocks,
/// the order of keys) stays. Patch materials get their changes in the "replace" block when the
/// material they include (`base`) has the key, else in "insert"; their shader cannot change.
/// Throws std::runtime_error on broken syntax.
[[nodiscard]] std::string editMaterialText(std::string_view vmt, std::string_view shader, const std::vector<MaterialEdit>& edits,
                                           const Material* base = nullptr);

/// A new VMT with the given shader and parameters (removals are ignored).
[[nodiscard]] std::string newMaterialText(std::string_view shader, const std::vector<MaterialEdit>& params);
/// The VMT with the material proxy `proxy` (e.g. "FidelityLiquid") in its Proxies block (`on`) or
/// without it (an empty Proxies block goes too). Other proxies and keys stay as they are.
[[nodiscard]] std::string setMaterialProxy(std::string_view vmt, std::string_view proxy, bool on);

/// "materials/concrete/wall.vmt", "concrete\\Wall" and "concrete/wall.vmt" all become "concrete/wall".
[[nodiscard]] std::string normalizeMaterialName(std::string_view name);

/// Parses VMT text without following patch includes.
[[nodiscard]] std::optional<Material> parseMaterial(std::string_view text, std::string_view name, std::string* error = nullptr);

/// Loads a material through the game's file system and resolves patch includes (max. 8 levels).
[[nodiscard]] std::optional<Material> loadMaterial(const GameFileSystem& fs, std::string_view name, std::string* error = nullptr);

} // namespace sme
