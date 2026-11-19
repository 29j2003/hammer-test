// model_glow.h - Makes parts of a model glow ("Glow ..." on the Models and Weapons pages): the
// material gets $selfillum with a mask texture of its own, so only the chosen parts glow, in a
// colour, steady, pulsing, or only while the model plays certain animations.
//
// How the game draws it (Source SDK 2013, Mapbase's stdshaders): VertexLitGeneric (and its phong
// path, skin_ps20b) mixes the lit colour towards $selfillumtint * base colour where the mask is
// white (vertexlit_and_unlit_generic_ps2x.fxc: lerp(diffuse, g_SelfIllumTint * albedo, mask)). So
// the glow keeps the texture's own colour, tinted; a tint above 1 makes it brighter. The mask is
// $selfillummask (its RGB) instead of the base alpha, which models often use for phong or the
// envmap - so nothing else changes. Under the player's flashlight the phong path draws no
// self-illumination (skin_ps20b's SKIP of FLASHLIGHT && SELFILLUM).
//
// Pulsing uses the stock math proxies of the client (functionproxy.cpp, mathproxy.cpp): "Sine"
// writes 0..1 into a helper vector, "Multiply" multiplies the colour with it into $selfillumtint
// (a float result on a vector variable sets every component, CResultProxy::SetFloatResult).
// "Only during an animation" needs to know what the model plays, which no stock proxy does.
// With mapbase-fidelity (GlowProxy::Fidelity) Mapsmith writes its C++ proxy "FidelityAnimGlow"
// (fidelity_animglow.cpp in the mod): it reads $mapsmith_glowsettings, sets $selfillumtint per draw,
// and the mod's glow light follows the same amount - real light only while the animation plays.
// Otherwise (GlowProxy::VScript) Mapbase's "VScriptProxy" (vscript_client.cpp) runs a small script on every draw with the drawn
// entity, and C_BaseAnimating offers GetSequence, GetSequenceActivityName, GetSequenceName and
// GetCycle to scripts - Mapsmith writes that script to scripts/vscripts/mapsmith/glow/. Without
// Mapbase (or with scripting off) the proxy does nothing and the material keeps its tint of 0:
// no glow, never a wrong one.
//
// mapbase-fidelity's glow lights (fidelity_glow.cpp) read $selfillum, $selfillummask and
// $selfillumtint once per model, so a steady glow also lights the surroundings; a glow during
// animations lights them only with the Fidelity proxy (the VScript one changes the tint only).
//
// The files go into the mod at the material's own path (a copy of the VMT that overrides the one
// in the game's VPKs) with a mask texture next to it; an existing file in the mod is kept as
// <file>.mapsmith-backup. The returned ImportRecord takes everything back (undoImport).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/texture.h>

namespace sme {

class GameFileSystem;
struct Material;

/// How a material glows.
struct GlowSettings {
	/// Where it glows: bright and colourful parts of the texture (lamps, screens, sights), a
	/// picture (white = glows), or everywhere.
	enum class Mask { Auto, Picture, Whole };
	Mask mask = Mask::Auto;
	double threshold = 0.6;              ///< Auto: how bright a part must be (0..1); higher = fewer parts
	double red = 1, green = 1, blue = 1; ///< Colour (0..1) - multiplies the texture's own colour
	double strength = 1;                 ///< Brightness (0..8); 1 = the texture's colour at full
	bool pulse = false;                  ///< Brightness goes up and down
	double pulseSeconds = 1;             ///< One pulse
	double pulseLow = 0.25;              ///< Darkest point of a pulse (0..1 of the brightness)
	enum class When { Always, DuringAnimation };
	When when = When::Always;
	/// DuringAnimation: activities ("ACT_VM_RELOAD") or sequence names ("reload"), any case.
	std::vector<std::string> animations;
	double fromCycle = 0, toCycle = 1;   ///< Part of those animations (0 = start, 1 = end)
	double fadeCycle = 0.05;             ///< Soft start and end (part of the animation)

	bool operator==(const GlowSettings&) const = default;
};

/// The mask the Auto setting finds: white where a texel is bright (its brightest channel at least
/// `threshold`) and either colourful or nearly white, soft at the edges (a 3x3 blur). RGBA grey.
[[nodiscard]] ImageRgba autoGlowMask(const ImageRgba& base, double threshold);
/// A picture as a mask of the given size: its brightness (and alpha) scaled with bilinear filtering.
[[nodiscard]] ImageRgba pictureGlowMask(const ImageRgba& picture, unsigned width, unsigned height);
/// Share of the mask that glows (0..1, weighted by brightness).
[[nodiscard]] double glowCoverage(const ImageRgba& mask);

/// Which proxy drives a glow during animations: Mapbase's VScriptProxy with a script, or
/// mapbase-fidelity's C++ proxy "FidelityAnimGlow" (no script, real light; needs the mod's client.dll).
enum class GlowProxy { VScript, Fidelity };

/// "models/weapons/v_smg1/smg1" -> "models/weapons/v_smg1/smg1_glowmask".
[[nodiscard]] std::string glowMaskName(std::string_view material);
/// The script for DuringAnimation, relative to scripts/vscripts ("mapsmith/glow/models_weapons_v_smg1_smg1.nut").
[[nodiscard]] std::string glowScriptName(std::string_view material);

/// The VMT text with the glow: $selfillum, $selfillummask, $selfillumtint and, as needed, the
/// pulse and animation proxies plus Mapsmith's own keys ($mapsmith_glow...) that readGlowSettings
/// reads back. A glow Mapsmith wrote before is replaced, not stacked. Other keys and proxies stay.
/// Throws std::runtime_error on broken VMT syntax.
[[nodiscard]] std::string glowMaterialText(std::string_view vmt, std::string_view material, const GlowSettings& settings,
                                           GlowProxy proxy = GlowProxy::VScript);
/// The VScript (Squirrel) for DuringAnimation: sets var0 ($selfillumtint) to the colour while the
/// drawn entity plays one of the animations in the cycle range, with the soft edges and the pulse.
[[nodiscard]] std::string glowScriptText(const GlowSettings& settings);
/// The settings Mapsmith wrote into a material, if it has a Mapsmith glow (Mask is Picture
/// when the mask texture was not made from the texture - the caller keeps the picture).
[[nodiscard]] std::optional<GlowSettings> readGlowSettings(const Material& material);

/// Everything one glow writes.
struct GlowFiles {
	std::string material;       ///< "models/weapons/v_smg1/smg1"
	std::string vmtText;        ///< -> materials/<material>.vmt
	std::vector<std::byte> maskVtf;   ///< -> materials/<glowMaskName>.vtf
	std::string scriptText;     ///< -> scripts/vscripts/<glowScriptName> (DuringAnimation with GlowProxy::VScript only)
	double coverage = 0;        ///< glowCoverage of the mask
};

/// Builds the files for a material of the game (its VMT and base texture are read through `fs`).
/// `picture` is the mask picture for Mask::Picture. nullopt (with `error`) when the material
/// cannot be read, has no base texture, is no model material (VertexLitGeneric and friends) or
/// the mask cannot be made.
[[nodiscard]] std::optional<GlowFiles> planGlow(const GameFileSystem& fs, std::string_view material, const GlowSettings& settings,
                                                const ImageRgba* picture = nullptr, std::string* error = nullptr,
                                                GlowProxy proxy = GlowProxy::VScript);
/// Writes the files into the mod (existing ones kept as <file>.mapsmith-backup, the first time
/// only) and returns what was changed (kind "glow"); undoImport takes it back.
[[nodiscard]] std::optional<ImportRecord> applyGlow(const std::filesystem::path& modDir, const GlowFiles& files, std::string* error = nullptr);

} // namespace sme
