// texture_enhance.h - "Enhance textures": sharper, larger textures and new bump maps for the mod's
// materials, written into the mod so they cover the game's originals (what RTX Remix does with
// replacement assets, for Source 1 without Remix). The game's files are never changed.
//
// Upscaling: Lanczos-3 with a light unsharp mask, built in; or an AI upscaler (Real-ESRGAN ncnn
// Vulkan, a program next to Mapsmith) through a callback. Both keep tiling textures seamless: the
// picture wraps around its edges while it is scaled (clamped textures - CLAMPS/CLAMPT - repeat
// their edge instead), alpha is scaled on its own and never sharpened, cut-out alpha stays 0/255.
//
// Texture size in Source: world brushes store texels per world unit (VBSP: texinfo
// textureVecsTexelsPerWorldUnits) and the engine divides by the material's mapping width at run
// time (IMaterial::GetMappingWidth, the $basetexture's size from its .vtf header), so a texture
// twice as large covers twice the area on every map - also on maps compiled before. Model, sky and
// overlay coordinates are 0..1 and do not care. So world materials get the larger texture under a
// new name (<texture>_mapsmith_hd) and a copy of the material with "$basetexturetransform"
// "center 0 0 scale K K rotate 0 translate 0 0" (and the same for every other transform the
// shaders apply to those coordinates: bump, envmap mask, second base texture, blend mask): the
// coordinates are scaled back by K, the tiles are as large as before. Model textures are written
// at their own path (every material that uses them gets them, nothing else changes).
//
// What Mapsmith wrote is listed in <mod>/cfg/mapsmith_textures.txt; "Remove enhancements"
// deletes those files and takes the keys out of materials it changed. Game enhancements (relief,
// liquid ...) build their materials from the "layered" original (layerMaterialText), so both work
// together. Materials the mapper wrote by hand are never changed.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/texture.h>

namespace sme {

class GameFileSystem;
struct Material;

/// First line of the material copies Enhance textures writes (a MAPSMITH_MARKER line: deploy.bat
/// leaves it alone, Game enhancements builds on it).
inline constexpr std::string_view TEXTURE_ENHANCE_MARKER = "// mapsmith: game enhancement textures";
/// Suffix of a world material's larger base texture ("brick/wall01_mapsmith_hd").
inline constexpr std::string_view HD_TEXTURE_SUFFIX = "_mapsmith_hd";
/// Suffix of a bump map Enhance textures makes ("brick/wall01_mapsmith_bump").
inline constexpr std::string_view ENHANCE_BUMP_SUFFIX = "_mapsmith_bump";
/// Suffix of a reflection mask ($envmapmask) it makes.
inline constexpr std::string_view ENHANCE_MASK_SUFFIX = "_mapsmith_mask";
/// The largest side a texture may get (Source 2013 handles 4096; more costs too much memory).
inline constexpr unsigned ENHANCE_MAX_SIZE = 4096;

// --- Pictures -------------------------------------------------------------------------------

/// How the edges are continued while scaling: Wrap for tiling textures, Clamp for clamped ones.
enum class EdgeMode { Wrap, Clamp };

/// Lanczos-3 resampling to `width` x `height` (each 1..ENHANCE_MAX_SIZE). Colour and alpha are
/// filtered separately; `edgesU`/`edgesV` continue the picture across its edges. An empty picture
/// gives an empty one.
[[nodiscard]] ImageRgba resizeLanczos(const ImageRgba& image, unsigned width, unsigned height, EdgeMode edgesU = EdgeMode::Wrap,
                                      EdgeMode edgesV = EdgeMode::Wrap);
/// Unsharp mask on the colour (not the alpha): `amount` 0..1 of the difference to a 3x3 blur.
void sharpen(ImageRgba& image, double amount, EdgeMode edgesU = EdgeMode::Wrap, EdgeMode edgesV = EdgeMode::Wrap);
/// A normal map scaled: the vectors resampled and made unit length again (alpha kept as a mask).
/// `ssbump`: self-shadowed bump maps hold three weights, not a vector - only resampled.
[[nodiscard]] ImageRgba resizeNormalMap(const ImageRgba& image, unsigned width, unsigned height, bool ssbump, EdgeMode edgesU = EdgeMode::Wrap,
                                        EdgeMode edgesV = EdgeMode::Wrap);
/// The picture with `margin` texels of its own continuation on every side (wrapped or repeated
/// edge) - an AI upscaler then sees the neighbours a tile has in the game, and cropPicture takes
/// the margin (times the scale) off again.
[[nodiscard]] ImageRgba padPicture(const ImageRgba& image, unsigned margin, EdgeMode edgesU, EdgeMode edgesV);
/// A part of a picture (clamped to it).
[[nodiscard]] ImageRgba cropPicture(const ImageRgba& image, unsigned x, unsigned y, unsigned width, unsigned height);
/// Cut-out alpha (only 0 and 255 in the source) back to 0/255 after scaling.
void thresholdAlpha(ImageRgba& image);
/// A reflection mask from the brightness: dark parts reflect less (contrast stretched between the
/// 5 % and 95 % brightness), alpha 255.
[[nodiscard]] ImageRgba glossMaskFromColor(const ImageRgba& image);

/// An AI upscaler: the (padded) picture `factor` times larger, nullopt when it failed (`error`).
/// Called from a worker thread; it must stop soon when `cancel` turns true.
using AiUpscaler = std::function<std::optional<ImageRgba>(const ImageRgba& picture, unsigned factor, const std::atomic<bool>* cancel, std::string* error)>;

/// How a picture is made larger.
enum class UpscaleMethod {
	Sharp,   ///< Lanczos-3 and an unsharp mask (built in, fast)
	Ai,      ///< The AI upscaler (Real-ESRGAN); Sharp when there is none or it fails
};

/// A picture `factor` times larger, at most `maxSize` on its longer side, then scaled to
/// `width` x `height` exactly (power of two). `ai` may be null (Sharp then).
struct UpscaleResult {
	ImageRgba image;
	bool usedAi = false;
	std::string note;   ///< Why the AI was not used ("no Vulkan GPU ..."), empty when it was or was not asked for
};
[[nodiscard]] UpscaleResult upscalePicture(const ImageRgba& image, unsigned width, unsigned height, UpscaleMethod method, double sharpenAmount,
                                           EdgeMode edgesU, EdgeMode edgesV, const AiUpscaler* ai, const std::atomic<bool>* cancel = nullptr);

// --- Options and plan -----------------------------------------------------------------------

/// What Enhance textures does (the dialog's choices).
struct EnhanceOptions {
	unsigned factor = 2;                    ///< 1 (no upscaling), 2 or 4
	UpscaleMethod method = UpscaleMethod::Ai;
	unsigned maxSize = 2048;                ///< Longer side at most (power of two, up to ENHANCE_MAX_SIZE)
	bool bumpMaps = true;                   ///< A bump map for materials without one
	double bumpStrength = 1.0;
	bool upscaleBumpMaps = false;           ///< Their own bump maps larger too (resampled, not AI)
	bool glossMasks = false;                ///< A reflection mask for materials with $envmap but no mask
	double sharpen = 0.35;                  ///< Unsharp mask after the built-in scaling
	bool compress = true;                   ///< DXT1/DXT5 (else BGRA8888, 4-8x larger)
};

/// How the material's texture coordinates come about.
enum class EnhanceKind {
	World,   ///< Brushes and displacements: coordinates in texels, the transform keeps the size
	Model,   ///< Models, sky, overlays: 0..1, a larger texture fits as it is
};

/// One texture to make larger.
struct EnhanceTexture {
	std::string key;            ///< "$basetexture", "$basetexture2", "$bumpmap", "$bumpmap2"
	std::string source;         ///< "brick/wall01" (the game's texture)
	std::string target;         ///< Name it gets: the source (models) or source + HD_TEXTURE_SUFFIX
	unsigned fromWidth = 0, fromHeight = 0;
	unsigned toWidth = 0, toHeight = 0;
	std::uint32_t flags = 0;    ///< The source's VTF flags (kept)
	bool normalMap = false;     ///< A bump map (resampled as vectors, never by the AI)
	bool ssbump = false;
};

/// What Enhance textures would do with one material (planEnhancement).
struct MaterialEnhancement {
	std::string material;              ///< "brick/wall01"
	std::string shader;
	EnhanceKind kind = EnhanceKind::World;
	std::string skipped;               ///< Why nothing is done ("a decal: ..."); empty = something is done
	std::vector<EnhanceTexture> textures;
	std::string bumpName;              ///< A new bump map ("brick/wall01_mapsmith_bump"), empty = none
	std::string maskName;              ///< A new reflection mask, empty = none
	double scaleU = 1, scaleV = 1;     ///< The coordinate transform (world materials with a larger base texture)
	/// The material's file in the mod: none (the game's original - a copy is written), ours (an
	/// earlier Enhance textures copy), generated (by Game enhancements or deploy.bat: gets the keys).
	enum class File { None, Ours, Generated };
	File file = File::None;
	[[nodiscard]] bool needsMaterialText() const;   ///< A material text has to change (else only textures are written)
	[[nodiscard]] std::uint64_t bytesEstimate() const;   ///< About how large the new files are
};

/// Plans one material ("brick/wall01"): which textures get larger, what is new, why not. Reads
/// the game's files; writes nothing. Never touches the mod's hand-written materials (skipped with
/// the reason) except for model textures written at their own path.
/// `manifest`/`decisions`: the mod's record and decision file when the caller has them read
/// already (many materials at once); null: read here.
struct TextureManifest;
class MaterialDecisions;
[[nodiscard]] MaterialEnhancement planEnhancement(const GameFileSystem& fs, const std::filesystem::path& modDir, std::string_view material,
                                                  const EnhanceOptions& options, const TextureManifest* manifest = nullptr,
                                                  const MaterialDecisions* decisions = nullptr);

// --- The record in the mod ------------------------------------------------------------------

/// One enhanced material in <mod>/cfg/mapsmith_textures.txt.
struct EnhancedMaterial {
	std::string material;
	double scaleU = 1, scaleV = 1;
	std::map<std::string, std::string> renamed;   ///< Key -> new texture name ("$basetexture" -> "brick/wall01_mapsmith_hd")
	std::string bumpName, maskName;
	bool ownCopy = false;                         ///< The material file is Mapsmith's copy (deleted on removal)
	std::vector<std::string> files;               ///< Every file written for it, "materials/....vtf"
	bool operator==(const EnhancedMaterial&) const = default;
};
/// Every enhanced material of a mod.
struct TextureManifest {
	std::vector<EnhancedMaterial> materials;
	[[nodiscard]] const EnhancedMaterial* find(std::string_view material) const;
	void put(EnhancedMaterial record);
	bool erase(std::string_view material);
};
/// Where the record lives.
[[nodiscard]] std::filesystem::path textureManifestFile(const std::filesystem::path& modDir);
/// Reads the record (untrusted: unknown lines are skipped, unsafe paths dropped).
[[nodiscard]] TextureManifest parseTextureManifest(std::string_view text);
[[nodiscard]] std::string writeTextureManifest(const TextureManifest& manifest);
[[nodiscard]] TextureManifest loadTextureManifest(const std::filesystem::path& modDir);
bool saveTextureManifest(const std::filesystem::path& modDir, const TextureManifest& manifest);

// --- Material text --------------------------------------------------------------------------

/// The transform value Enhance textures writes for a scale.
[[nodiscard]] std::string mappingTransformText(double scaleU, double scaleV);
/// The texture coordinate scale of a material text: K of a "center 0 0 scale K K ..." transform
/// Enhance textures wrote on $basetexturetransform ({1, 1} without one). A texture K times larger
/// shows as large as one of its size / K: what Hammer's fit, the viewport and the relief rules use.
[[nodiscard]] std::pair<double, double> mappingScale(const Material& material);
/// Whether all coordinate transforms of the material are either absent or Enhance textures' own
/// uniform scale (the pattern relief accepts those: the height map stays in the base coordinates).
[[nodiscard]] bool onlyMappingTransforms(const Material& material);
/// The material text with the record's keys: renamed textures, the transforms, the new bump map /
/// mask. Patch materials get them in insert/replace (`resolved` = the patch resolved).
[[nodiscard]] std::string layerMaterialText(std::string_view vmt, const EnhancedMaterial& record, const Material* resolved = nullptr);
/// Takes those keys out again (texture names back, Mapsmith's transforms, bump map and mask gone).
[[nodiscard]] std::string unlayerMaterialText(std::string_view vmt, const EnhancedMaterial& record, const Material* resolved = nullptr);
/// Whether the text carries the record's keys (deploy.bat writes its materials anew and drops them).
[[nodiscard]] bool hasLayer(std::string_view vmt, const EnhancedMaterial& record);
/// The game's original material text with the mod's enhancement on it, for Game enhancements:
/// nullopt when the game has none; unchanged when the material is not enhanced.
[[nodiscard]] std::optional<std::string> enhancedOriginal(const GameFileSystem& fs, const std::filesystem::path& modDir, const TextureManifest& manifest,
                                                          std::string_view material);

// --- Making and writing ---------------------------------------------------------------------

/// A file to write into the mod.
struct EnhanceFile {
	std::string path;              ///< "materials/brick/wall01_mapsmith_hd.vtf" (safe, below materials/)
	std::vector<std::byte> bytes;
};
/// What making one material gives.
struct EnhanceOutput {
	std::string material;
	std::vector<EnhanceFile> files;      ///< Textures, and the material text when it changes
	EnhancedMaterial record;
	bool usedAi = false;
	std::vector<std::string> notes;      ///< "the AI upscaler failed: ... - built-in scaling"
	std::string error;                   ///< Non-empty: nothing to write
};
/// Makes the new textures (and material text) of a plan. Slow (decoding, scaling, compressing):
/// call it from a worker thread; `cancel` stops it between steps.
[[nodiscard]] EnhanceOutput makeEnhancement(const GameFileSystem& fs, const std::filesystem::path& modDir, const MaterialEnhancement& plan,
                                            const EnhanceOptions& options, const AiUpscaler* ai = nullptr, const std::atomic<bool>* cancel = nullptr);
/// The base texture before and after (for the dialog's comparison), at most `maxSize` on the
/// longer side (the after picture at the size it would get, then cut to the same part).
struct EnhancePreview {
	ImageRgba before;    ///< The original, scaled up smoothly to the new size (what the game shows now, larger)
	ImageRgba after;
	unsigned fromWidth = 0, fromHeight = 0, toWidth = 0, toHeight = 0;
	bool usedAi = false;
	std::string note;
};
[[nodiscard]] std::optional<EnhancePreview> previewEnhancement(const GameFileSystem& fs, const MaterialEnhancement& plan, const EnhanceOptions& options,
                                                               const AiUpscaler* ai = nullptr, const std::atomic<bool>* cancel = nullptr,
                                                               unsigned crop = 512);

/// Writes the outputs into the mod (temp file + rename each) and records them. Files the mod
/// already has and Mapsmith did not write are never overwritten (the material is reported and
/// left out). Returns the materials written; `problems` gets what failed.
std::vector<std::string> writeEnhancements(const std::filesystem::path& modDir, const std::vector<EnhanceOutput>& outputs, std::vector<std::string>* problems = nullptr,
                                           GameFileSystem* noteFs = nullptr);
/// Removes the enhancement of these materials (all of them when empty): deletes Mapsmith's files,
/// takes the keys out of generated materials, forgets them. Returns how many were removed.
int removeEnhancements(const GameFileSystem& fs, const std::filesystem::path& modDir, const std::vector<std::string>& materials = {},
                       std::vector<std::string>* problems = nullptr, GameFileSystem* noteFs = nullptr);
/// Puts the keys back into generated materials that lost them (deploy.bat wrote them anew).
/// Returns the materials fixed.
std::vector<std::string> refreshEnhancementLayers(const GameFileSystem& fs, const std::filesystem::path& modDir, GameFileSystem* noteFs = nullptr);

// --- The AI upscaler program ----------------------------------------------------------------

/// Real-ESRGAN ncnn Vulkan, next to Mapsmith in tools/realesrgan.
inline constexpr std::string_view REAL_ESRGAN_FOLDER = "tools/realesrgan";
[[nodiscard]] std::string realEsrganProgramName();   ///< "realesrgan-ncnn-vulkan.exe" on Windows
/// The model it uses (photographs and game textures; 4x).
inline constexpr std::string_view REAL_ESRGAN_MODEL = "realesrgan-x4plus";
/// Its command line (program not included): input and output PNG, models folder.
[[nodiscard]] std::vector<std::string> realEsrganArguments(const std::filesystem::path& input, const std::filesystem::path& output,
                                                           const std::filesystem::path& modelsDir);
/// The margin padPicture adds for the AI (enough for its receptive field at the edges).
[[nodiscard]] unsigned aiMargin(unsigned width, unsigned height);

} // namespace sme
