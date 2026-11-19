// photo_material.h - "New material from photo": a photo becomes a finished Source material.
//
// Steps (all on the CPU, no Qt):
//   - crop a square (centre by default),
//   - flatten the lighting: the photo's slow brightness changes (sun from one side, vignette)
//     are divided out so tiles do not show a light/dark pattern,
//   - make it tileable: a cross-fade with the picture shifted by half a tile, first along u, then
//     along v (each pass keeps the middle and blends the edges, so the edges meet their opposite),
//   - scale to a power of two (Lanczos, wrapping, from texture_enhance),
//   - bump map from the brightness (heightFromColor + normalMapFromHeight, as Game enhancements),
//   - optional gloss mask (bright = shiny): in the bump map's alpha.
// Shader: world (LightmappedGeneric; gloss = $envmap env_cubemap + $normalmapalphaenvmapmask, or a
// separate $envmapmask without bump map) or model (VertexLitGeneric; gloss = $phong with the mask
// in the bump map's alpha - phong needs that map, so a flat one is made when there is no bump).
// Writing goes through asset_import (ImportRecord: listed in cfg/mapsmith_imports.txt, undo).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/texture.h>

namespace sme {

/// Settings of the material assistant (all have usable defaults).
struct PhotoMaterialOptions {
	// Crop, as fractions of the photo: a square of side `cropSize` x the shorter side, its top-left
	// corner at (cropX, cropY) of the free room (0.5, 0.5 = centred).
	double cropX = 0.5;
	double cropY = 0.5;
	double cropSize = 1.0;     ///< 0.1 .. 1
	unsigned size = 0;         ///< Texture side (power of two, 64 .. 2048); 0: from the crop (at most 1024)
	double seamBlend = 0.25;   ///< 0 .. 0.5: width of the cross-fade at each edge (fraction of the side)
	double flattenLight = 0.6; ///< 0 .. 1: how much of the slow brightness change is taken out
	double bumpStrength = 1.0; ///< 0 .. 4; 0 = no bump map (a model with gloss still gets a flat one)
	double gloss = 0.0;        ///< 0 .. 1; 0 = matte (no mask)
	bool model = false;        ///< VertexLitGeneric instead of LightmappedGeneric
	std::string folder = "custom";
	std::string name;
	std::string surfaceProp;   ///< Empty: suggestPhotoSurfaceProp
};

/// The pictures of a made material (normal empty without bump map; its alpha holds the gloss).
struct PhotoMaterial {
	ImageRgba color;
	ImageRgba normal;
	ImageRgba mask;            ///< Separate $envmapmask (world material with gloss but no bump map)
	std::string surfaceProp;
};

/// The square crop (fractions as in PhotoMaterialOptions). Empty for an empty photo.
[[nodiscard]] ImageRgba cropPhotoSquare(const ImageRgba& photo, double x, double y, double size);
/// Divides out the slow brightness change (a wide blur of the brightness, wrapping) by `amount`.
void flattenPhotoLighting(ImageRgba& image, double amount);
/// Cross-fades the edges with the picture shifted by half a tile: tiles meet without a seam.
/// `blend` 0 .. 0.5 of the side; 0 leaves the picture as it is.
[[nodiscard]] ImageRgba makePhotoTileable(const ImageRgba& image, double blend);
/// The largest difference between opposite edges (0 = seamless), per colour channel 0..255.
[[nodiscard]] int photoSeamError(const ImageRgba& image);
/// The texture side for a crop of `side` pixels: the power of two at or below it, 64 .. 1024.
[[nodiscard]] unsigned photoTextureSize(unsigned side);
/// A surface property from the name ("brick_wall" -> "brick"), else from the colour (green -> grass,
/// grey -> concrete, brown -> dirt); "default" when nothing fits. Always one of commonSurfaceProps.
[[nodiscard]] std::string suggestPhotoSurfaceProp(std::string_view name, const ImageRgba& color);

/// Makes the pictures (no files). nullopt with `error` for an empty or too small photo.
[[nodiscard]] std::optional<PhotoMaterial> makePhotoMaterial(const ImageRgba& photo, const PhotoMaterialOptions& options, std::string* error = nullptr);
/// The material name ("custom/brick", models: "models/custom/brick").
[[nodiscard]] std::string photoMaterialName(const PhotoMaterialOptions& options);
/// The VMT text for the made pictures.
[[nodiscard]] std::string photoMaterialVmt(const PhotoMaterialOptions& options, const PhotoMaterial& material);
/// The files it writes (VMT, base VTF, then _normal / _mask VTFs when made).
[[nodiscard]] std::vector<std::filesystem::path> photoMaterialTargets(const std::filesystem::path& modDir, const PhotoMaterialOptions& options,
                                                                     const PhotoMaterial& material);
/// Writes VTFs and VMT into <mod>/materials/. With `overwrite` false an existing file stops it.
[[nodiscard]] std::optional<ImportRecord> writePhotoMaterial(const std::filesystem::path& modDir, const PhotoMaterialOptions& options,
                                                             const PhotoMaterial& material, bool overwrite, std::string* error = nullptr);

} // namespace sme
