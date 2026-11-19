// hud_layout.h - The HUD of a Source 2013 mod as the HUD editor sees it: scripts/HudLayout.res
// (where each HUD element sits and how big it is) and resource/ClientScheme.res (colours and
// fonts).
//
// How the game reads them (Mapbase source: game/client/hud.cpp, clientmode_shared.cpp,
// vgui_controls/Panel.cpp):
//   - Both are KeyValues text. Blocks and values may end in a platform condition ([$WIN32],
//     [$X360], [!$OSX] ...); the first entry whose condition holds wins (KeyValues::FindKey).
//   - The HUD viewport is proportional: numbers are in a 640x480 space and scaled by
//     screen height / 480 (vgui2 GetProportionalScaledValue). Positions may start with
//     r (from the right/bottom edge), c (from the centre), then s/p (fraction of own size /
//     parent size). Sizes may start with f (parent size minus value), o (the other axis), p.
//   - ClientScheme: Colors (named colours), BaseSettings (FgColor, BgColor ... – a colour or a
//     name from Colors), Fonts (each font has numbered entries; an entry with "yres" "min max" is
//     used only in that screen height range, its "tall" is pixels there; an entry without yres is
//     scaled like the HUD: tall * height / 480), CustomFontFiles (TTFs such as HALFLIFE2.ttf).
//
// Edits are made in the text itself (values replaced, missing keys added before the block's
// closing brace), so comments, conditions and everything the editor does not know stay as they
// were. Files are untrusted: the parser has limits and never throws out of parseResFile.

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>

namespace sme {

/// One entry of a .res file with where it stands in the text.
struct ResNode {
	std::string key;
	std::string value;                 ///< Empty for blocks
	std::string condition;             ///< "[$WIN32]" or empty
	bool block = false;
	std::vector<ResNode> children;
	std::size_t valueBegin = 0;        ///< Value token (with its quotes) in the text
	std::size_t valueEnd = 0;
	std::size_t closeBrace = 0;        ///< Offset of the block's '}'
	int depth = 0;

	/// Whether the condition holds on Windows PC (no condition: true).
	[[nodiscard]] bool active() const;
	/// The first active child with the key (case-insensitive), or nullptr.
	[[nodiscard]] const ResNode* find(std::string_view childKey) const;
	/// Value of the first active value child with the key, or `fallback`.
	[[nodiscard]] std::string get(std::string_view childKey, std::string_view fallback = {}) const;
};

/// A parsed .res file: its text and its tree (root = the file's children).
struct ResFile {
	std::string text;
	ResNode root;
	std::vector<std::string> includes;   ///< #base / #include lines (not followed)
};

/// Parses a .res file; nullopt (with `error`) on broken syntax or a file over the limits.
[[nodiscard]] std::optional<ResFile> parseResFile(std::string_view text, std::string* error = nullptr);

/// Whether a KeyValues condition holds for the Windows PC game: [$WIN32], [$WINDOWS] yes;
/// [$X360], [$OSX], [$LINUX], [$POSIX], [$PS3], [$GAMECONSOLE] ... no; with ! && || and brackets.
[[nodiscard]] bool resConditionTrue(std::string_view condition);

/// The text with `key` of `block` set to `value`: the active entry's value is replaced, or a new
/// line is added before the block's '}'. `block` must come from parseResFile(file.text).
[[nodiscard]] std::string withResValue(const ResFile& file, const ResNode& block, std::string_view key, std::string_view value);

/// A path of block keys from the root, each taking the first active block of that name.
[[nodiscard]] const ResNode* findResBlock(const ResFile& file, const std::vector<std::string_view>& path);

// ---------------------------------------------------------------------------------------------
// Positions

/// Screen pixels of `value` (640x480 units) on a screen `screenTall` high: value * tall / 480.
[[nodiscard]] int hudScaled(double value, int screenTall);

/// A HUD element's rectangle in screen pixels (vgui Panel::ComputePos/ComputeWide/ComputeTall
/// with a proportional panel on the whole screen). Missing values count as 0.
struct HudRect {
	int x = 0, y = 0, wide = 0, tall = 0;
	bool operator==(const HudRect&) const = default;
};
[[nodiscard]] HudRect resolveHudRect(std::string_view xpos, std::string_view ypos, std::string_view wide, std::string_view tall, int screenWide,
                                     int screenTall);

/// The text for a new position `pixel` that keeps the old text's anchor: "r150" stays right
/// anchored, "c-18" centred, "16" from the left/top (s/p fractions become plain offsets).
[[nodiscard]] std::string hudPositionText(std::string_view oldText, int pixel, int size, int screenSize, int screenTall);
/// The text for a new size in pixels, keeping an f (fill) prefix.
[[nodiscard]] std::string hudSizeText(std::string_view oldText, int pixels, int screenSize, int screenTall);

/// How a position is anchored.
enum class HudAnchor { Start, Center, End };
[[nodiscard]] HudAnchor hudAnchor(std::string_view positionText);
/// The number after the anchor (and s/p) prefix: "r150" -> 150.
[[nodiscard]] double hudPositionNumber(std::string_view positionText);
/// Text for an anchor and a number in 640x480 units ("r150", "c-18", "16").
[[nodiscard]] std::string hudPositionFrom(HudAnchor anchor, double number);

// ---------------------------------------------------------------------------------------------
// HudLayout.res

/// One element of HudLayout.res (its active block).
struct HudElement {
	std::string name;                  ///< "HudHealth"
	std::string xpos, ypos, wide, tall;
	bool visible = true;
	bool hasPosition = false;          ///< Has xpos and ypos (else the code places it)
	bool fullScreen = false;           ///< 640x480 at 0,0: draws over the whole screen (crosshair ...)
};

/// The active elements in file order (inactive platform variants left out, each name once).
[[nodiscard]] std::vector<HudElement> hudElements(const ResFile& layout);
/// The active block of an element, or nullptr.
[[nodiscard]] const ResNode* hudElementBlock(const ResFile& layout, std::string_view name);

/// What an element shows, for the preview and the list.
struct HudElementInfo {
	std::string_view title;            ///< "Health"
	std::string_view label;            ///< "HEALTH" (empty: none)
	std::string_view sample;           ///< "100"
	std::string_view sample2;          ///< Second, small number ("180" for the ammo reserve)
	std::string_view description;
};
/// Known HL2 elements; a generic entry for others.
[[nodiscard]] HudElementInfo hudElementInfo(std::string_view name);

// ---------------------------------------------------------------------------------------------
// ClientScheme.res

/// RGBA colour.
using HudColor = std::array<int, 4>;
/// "255 220 0 100" (alpha optional, 255) -> colour; nullopt if it is not four/three numbers.
[[nodiscard]] std::optional<HudColor> parseHudColor(std::string_view text);
[[nodiscard]] std::string hudColorText(const HudColor& color);

/// A colour of the scheme as the editor lists it.
struct SchemeColor {
	std::string name;                  ///< "FgColor"
	std::string section;               ///< "BaseSettings" or "Colors"
	std::string text;                  ///< As written ("255 220 0 100" or a name)
	std::optional<HudColor> color;     ///< Resolved
};
/// Colours of Colors and BaseSettings (BaseSettings entries that are colours only).
[[nodiscard]] std::vector<SchemeColor> schemeColors(const ResFile& scheme);
/// A colour by name (BaseSettings first, then Colors, names followed up to 8 times) or a colour
/// text; nullopt if unknown.
[[nodiscard]] std::optional<HudColor> resolveSchemeColor(const ResFile& scheme, std::string_view nameOrColor);

/// One entry of a font ("1", "2" ...).
struct SchemeFontEntry {
	std::string id;                    ///< "1"
	std::string family;                ///< "name": "HalfLife2", "Verdana"
	int tall = 12;
	int weight = 400;
	int yresMin = 0, yresMax = 0;      ///< 0/0: every resolution
	bool additive = false, antialias = false, custom = false;
	int blur = 0, scanlines = 0;
};
struct SchemeFont {
	std::string name;                  ///< "HudNumbers"
	std::vector<SchemeFontEntry> entries;   ///< Active entries in order
};
[[nodiscard]] std::vector<SchemeFont> schemeFonts(const ResFile& scheme);
/// The entry the game uses on a screen `screenTall` high: the first whose yres range holds it,
/// else the first without yres; nullptr if none.
[[nodiscard]] const SchemeFontEntry* pickFontEntry(const SchemeFont& font, int screenTall);
/// Pixel height of the entry on that screen (yres entries as written, others scaled).
[[nodiscard]] int fontPixelTall(const SchemeFontEntry& entry, int screenTall);
/// The custom font files (CustomFontFiles), e.g. "resource/HALFLIFE2.ttf".
[[nodiscard]] std::vector<std::string> schemeFontFiles(const ResFile& scheme);

// ---------------------------------------------------------------------------------------------
// Files in the mod

inline constexpr std::string_view HUD_LAYOUT_FILE = "scripts/HudLayout.res";
inline constexpr std::string_view HUD_SCHEME_FILE = "resource/ClientScheme.res";

/// Writes one HUD file into the mod. The first time a file Mapsmith did not write is kept as
/// <file>.mapsmith-backup; the change is remembered in cfg/mapsmith_imports.txt (kind "hud") so
/// restoreHudFiles() can take it back. False with `error` if it could not be written.
bool saveHudFile(const std::filesystem::path& modDir, std::string_view relativePath, std::string_view text, std::string* error = nullptr);
/// Puts the mod's HUD files back as they were before Mapsmith changed them. False with `error`
/// if a file could not be changed.
bool restoreHudFiles(const std::filesystem::path& modDir, std::string* error = nullptr);
/// Whether Mapsmith changed the mod's HUD files.
[[nodiscard]] bool hudFilesChanged(const std::filesystem::path& modDir);

} // namespace sme
