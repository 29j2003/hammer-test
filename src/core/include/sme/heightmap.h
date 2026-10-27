// heightmap.h - Height maps for relief (parallax occlusion mapping) shaders such as the
// Fidelity_Parallax shader of mapbase-fidelity: white = top, read from the red channel, in the
// normal map's texture coordinates.
//
// Made like mapbase-fidelity's own generator (deploy.ps1 / height_map.cs) so both agree:
// from a normal map (also ssbump) the slopes are integrated in the frequency domain
// (Frankot-Chellappa, slow waves taken out) and scaled by the 1 % and 99 % heights; from a base
// texture its brightness, band-passed, with flatter tops. Zones painted by the mapper can be
// kept flat (no relief) or turned upside down.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/texture.h>

namespace sme {

struct HeightMap {
	unsigned width = 0;
	unsigned height = 0;
	std::vector<float> values;   ///< 0..1, rows top to bottom

	[[nodiscard]] float at(unsigned x, unsigned y) const { return values[std::size_t(y) * width + x]; }
};

/// Zones painted onto a height map (one byte per texel, same size).
enum class HeightZone : std::uint8_t { Normal = 0, Flat = 1, Inverted = 2 };

/// From a normal map (green along v, down in the image, as Source's tools make them; a map made
/// the other way round is detected). `ssbump`: the three lightmap weights of a self-shadowed bump map.
[[nodiscard]] std::optional<HeightMap> heightFromNormalMap(const ImageRgba& normalMap, bool ssbump, std::string* error = nullptr);

/// From a base texture's brightness (walls without a normal map).
[[nodiscard]] std::optional<HeightMap> heightFromColor(const ImageRgba& baseTexture, std::string* error = nullptr);

/// An existing height map texture (its red channel).
[[nodiscard]] HeightMap heightFromImage(const ImageRgba& image);

/// Upside down: what was top is the bottom.
void invertHeights(HeightMap& map);

/// Applies painted zones (`zones.size()` = width * height): Flat stays on top (no relief there),
/// Inverted is turned upside down. Other sizes are scaled to the map.
void applyHeightZones(HeightMap& map, const std::vector<std::uint8_t>& zones, unsigned zonesWidth, unsigned zonesHeight);

/// Gray RGBA image of the heights (previews).
[[nodiscard]] ImageRgba heightImage(const HeightMap& map);

/// Where mapbase-fidelity's deploy.bat takes a wall material's relief from (deploy.ps1,
/// Get-ParallaxSource): a material already on Fidelity_Parallax with its own height map, a
/// bump-mapped wall (its normal map), or a wall without one whose name tells of relief (brick,
/// stone, tile ...: the base texture). Wood stays flat, as deploy.bat leaves it by default.
struct ReliefSource {
	enum class Kind { HeightMap, NormalMap, SSBump, Color };
	Kind kind = Kind::NormalMap;
	std::string texture;          ///< Texture name the heights come from ("brick/brickwall001a_normal")
	double depth = 0.02;          ///< $parallaxdepth (texture units)
};
struct Material;
[[nodiscard]] std::optional<ReliefSource> fidelityReliefSource(std::string_view materialName, const Material& material);

/// An I8 .vtf with all mip levels (what the Fidelity_Parallax shader reads).
[[nodiscard]] std::vector<std::byte> encodeHeightVtf(const HeightMap& map);

} // namespace sme
