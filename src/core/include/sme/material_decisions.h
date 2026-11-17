// material_decisions.h - The mod's decisions about its materials, in <mod>/cfg/mapsmith_materials.txt
// (it travels with the mod, like mapsmith_enhancements.txt). One line per material or wildcard:
//
//     building_template/building_template007a  role=facade windows=2x1 floors=1
//     brick/brickwall031d  role=wall parallax=on depth=0.008 pattern=brick
//     brick/*  parallax=off            // every brick material, unless a more specific line says otherwise
//     concrete/concretefloor001a  role=ground parallax=off note="street"
//     concrete/concretewall004a  bump=generate bumpstrength=1.5
//
// Written by hand, by Mapsmith's per-material controls (Parallax: Auto / On / Off, Depth; Bump map:
// Auto / Generate / Off, strength) or from
// the chat: the mapper exports the materials for review (sme/material_review.h), sends the pictures,
// and pastes the answer back (Load material decisions). What a line says wins over Mapsmith's own
// reading of the texture everywhere: the building builder (role, windows, floors, scale), the relief
// of Game enhancements (parallax on / off, depth, pattern) and the material editor.
//
// Rules: names as in the material browser (any case, with or without "materials/" and ".vmt");
// `*` matches any run of characters (also "/"), `?` one character; per key the most specific line
// wins (an exact name before any wildcard, a wildcard with more fixed characters before a shorter
// one, a later line before an earlier one of the same weight). Comments start with //. Unknown keys
// are kept when Mapsmith rewrites the file; lines it does not change stay exactly as written. A bad
// value is reported with its line number and ignored - the file never stops anything.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sme/facade_texture.h>

namespace sme {

/// Parallax (Fidelity_Parallax relief) for a material.
enum class ParallaxChoice {
	Auto,   ///< Mapsmith decides (by name, look and pattern)
	On,     ///< Always relief (also where Mapsmith would not give it)
	Off,    ///< Never relief
};
[[nodiscard]] const char* parallaxChoiceKey(ParallaxChoice choice);   ///< "auto", "on", "off"
[[nodiscard]] std::optional<ParallaxChoice> parallaxChoiceFromKey(std::string_view key);

/// A bump map ($bumpmap) Mapsmith generates from the relief's heights (`<material>_mapsmith_normal`).
enum class BumpChoice {
	Auto,       ///< Where the material has none and Mapsmith makes the relief (pattern or parallax=on)
	Generate,   ///< Always (replaces the material's own)
	Off,        ///< Never
};
[[nodiscard]] const char* bumpChoiceKey(BumpChoice choice);   ///< "auto", "generate", "off"
[[nodiscard]] std::optional<BumpChoice> bumpChoiceFromKey(std::string_view key);
/// bumpstrength=...: how steep the generated bump map is (1 = as the relief; 0.1 .. 4).
inline constexpr double BUMP_STRENGTH_MIN = 0.1;
inline constexpr double BUMP_STRENGTH_MAX = 4.0;

/// What the lines say about one material (each key only when a line sets it).
struct MaterialDecision {
	std::optional<std::string> role;          ///< facade | wall | trim | roof | shop | ground | interior | unsuitable
	std::optional<int> windowColumns;         ///< windows=CxR: windows across one tile
	std::optional<int> windowRows;            ///<              and down one tile
	std::optional<int> floors;                ///< Floors one tile spans
	std::optional<std::vector<FacadeWindowRect>> windowRects;   ///< windowrects=u0,v0,u1,v1;...
	std::optional<double> scale;              ///< Texture scale on buildings
	std::optional<ParallaxChoice> parallax;
	std::optional<double> depth;              ///< $parallaxdepth
	std::optional<std::string> pattern;       ///< brick | tile | plank | stone | none
	std::optional<BumpChoice> bump;           ///< bump=auto|generate|off
	std::optional<double> bumpStrength;       ///< bumpstrength=0.1 .. 4
	std::optional<std::string> note;
	std::vector<std::pair<std::string, std::string>> extra;   ///< Keys Mapsmith does not know (kept)

	[[nodiscard]] bool empty() const;
	/// Takes every key `other` sets (other wins).
	void mergeFrom(const MaterialDecision& other);
	/// "role=wall parallax=on depth=0.008" (the keys in the file's order).
	[[nodiscard]] std::string keysText() const;
	bool operator==(const MaterialDecision&) const = default;
};

/// The allowed roles and parallax patterns (for messages and the UI).
[[nodiscard]] const std::vector<std::string>& decisionRoles();
[[nodiscard]] const std::vector<std::string>& decisionPatterns();

/// A problem in the file (1-based line).
struct DecisionProblem {
	int line = 0;
	std::string message;
};

/// The file: its lines in order (comments and untouched lines kept as written).
class MaterialDecisions {
public:
	struct Line {
		std::string raw;          ///< As written (comment and blank lines: only this)
		std::string pattern;      ///< Normalised name or wildcard ("brick/*"); empty for comment lines
		MaterialDecision decision;
		std::string comment;      ///< Trailing "// ..." (without the slashes)
		bool changed = false;     ///< Written anew by serialize()
	};

	/// Reads the text; bad values go to problems() and are ignored.
	[[nodiscard]] static MaterialDecisions parse(std::string_view text);
	/// The text again: unchanged lines exactly as read, changed and new ones in the canonical form.
	[[nodiscard]] std::string serialize() const;

	/// Everything the lines say about a material (most specific per key).
	[[nodiscard]] MaterialDecision lookup(std::string_view material) const;
	/// The line for exactly this name or wildcard, or null.
	[[nodiscard]] const MaterialDecision* exact(std::string_view pattern) const;
	/// Changes the line for exactly this name or wildcard (made at the end when missing). An emptied
	/// line goes away.
	template<typename F>
	void edit(std::string_view pattern, F&& change) {
		MaterialDecision& d = lineFor(pattern);
		change(d);
		cleanUp();
	}
	/// The incoming lines (from the chat) into this file: a pattern it has gets the keys the incoming
	/// line sets, a new one is added at the end. Comment lines of the incoming text are left out.
	void merge(const MaterialDecisions& incoming);

	[[nodiscard]] const std::vector<Line>& lines() const { return m_lines; }
	[[nodiscard]] const std::vector<DecisionProblem>& problems() const { return m_problems; }
	/// Lines with a material or wildcard.
	[[nodiscard]] int entryCount() const;
	[[nodiscard]] bool operator==(const MaterialDecisions& other) const { return serialize() == other.serialize(); }

private:
	MaterialDecision& lineFor(std::string_view pattern);
	void cleanUp();
	std::vector<Line> m_lines;
	std::vector<DecisionProblem> m_problems;
};

/// "BRICK/BrickWall01", "materials/brick/brickwall01.vmt" -> "brick/brickwall01" (wildcards kept).
[[nodiscard]] std::string decisionKey(std::string_view name);
/// Whether a name matches a pattern (both decisionKey'd; `*` any run, `?` one character).
[[nodiscard]] bool decisionPatternMatches(std::string_view pattern, std::string_view name);

/// What changes for each line between two files (for the preview of Load material decisions).
struct DecisionChange {
	std::string pattern;
	std::string before;   ///< keysText before ("" = no line)
	std::string after;    ///< keysText after ("" = removed)
};
[[nodiscard]] std::vector<DecisionChange> compareDecisions(const MaterialDecisions& before, const MaterialDecisions& after);

/// Where the file lives in a mod folder.
[[nodiscard]] std::filesystem::path materialDecisionsFile(const std::filesystem::path& modDir);
/// Reads the mod's file (empty when missing).
[[nodiscard]] MaterialDecisions loadMaterialDecisions(const std::filesystem::path& modDir);
/// Writes it (the file goes when no line is left); false when it could not be written.
bool saveMaterialDecisions(const std::filesystem::path& modDir, const MaterialDecisions& decisions);

/// A decision on top of what the picture says (building builder): role, windows (a regular grid
/// for windows=CxR without exact rectangles), floors and scale. Unchanged when the line sets none.
void applyMaterialDecision(TextureAnalysis& analysis, const MaterialDecision& decision);
/// The TextureRole of a decision's role ("" when it has none). ground and interior are no wall
/// textures of a facade: ground is Unsuitable for walls, interior a plain Wall.
[[nodiscard]] TextureRole decisionTextureRole(std::string_view role);
/// The decision file's word for a role Mapsmith found ("facade", "wall" ...; "" for Unknown).
[[nodiscard]] const char* decisionRoleKey(TextureRole role);

} // namespace sme
