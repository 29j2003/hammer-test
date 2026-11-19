// model_check.h - "Check model": what would make a compiled model fail or look wrong in the game,
// before and after Mapsmith builds one (Weapons, NPCs, Animation editor, Effects).
//
// Checks (limits from Source SDK 2013 public/studio.h, optimize.h, phyfile.h):
//   - files: .mdl, .vvd and .dx90.vtx must be there; every file's header checksum must equal the
//     .mdl's (the engine refuses the model: "has a different checksum") - error
//   - bones: more than MAXSTUDIOBONES (128) - error
//   - vertices: more than MAXSTUDIOVERTS (65536) in the .vvd's top LOD - error; > 20000 - warning (slow)
//   - skins: more than MAXSTUDIOSKINS (32) textures - error
//   - materials: each texture through every $cdmaterials folder; none found - warning (the game
//     shows the purple/black checkerboard); its $basetexture .vtf missing - warning; a texture
//     side that is not a power of two - warning
//   - physics: $keyvalues with prop_data (a physics prop) but no .phy - warning
//   - events: a sound event whose sound is neither a sound script entry nor a wave file, a particle
//     event whose system no .pcf has - warning
// Every reader is bounds-checked (readModelInfo, readMdlMaterialTable, readSequenceEvents ...).

#pragma once

#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

class GameFileSystem;

/// One finding.
struct ModelIssue {
	enum class Level { Error, Warning, Info };
	Level level = Level::Warning;
	std::string code;      ///< "checksum", "missing_file", "bones", "vertices", "skins", "material", "texture", "texture_size", "physics", "sound", "particle"
	std::string message;   ///< One line for the list
	std::string fix;       ///< What to do (empty: nothing simple)
	std::string subject;   ///< The material, sound, particle or file it is about
};

/// What the check may look up (null: that part is skipped).
struct ModelCheckContext {
	const std::set<std::string>* soundNames = nullptr;      ///< Sound script entries, lower case
	const std::set<std::string>* particleNames = nullptr;   ///< Particle systems, lower case
};

/// Engine limits (Source SDK 2013 studio.h).
inline constexpr int MODEL_MAX_BONES = 128;
inline constexpr int MODEL_MAX_VERTICES = 65536;
inline constexpr int MODEL_MAX_SKIN_TEXTURES = 32;
inline constexpr int MODEL_MANY_VERTICES = 20000;

/// Checks "models/x.mdl" in the game's file system (the mod's copy first, as the engine).
[[nodiscard]] std::vector<ModelIssue> checkModel(const GameFileSystem& fs, std::string_view modelPath, const ModelCheckContext& context = {});
/// Whether any finding is an error (the model will not load or not compile).
[[nodiscard]] bool hasModelErrors(const std::vector<ModelIssue>& issues);
/// "2 errors, 1 warning" / "No problems found".
[[nodiscard]] std::string modelCheckSummary(const std::vector<ModelIssue>& issues);
/// Vertex count of the top LOD from a .vvd (nullopt for a broken file).
[[nodiscard]] std::optional<int> vvdVertexCount(std::span<const std::byte> vvd);

} // namespace sme
