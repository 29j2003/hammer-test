// particle_edit.h - Editing particle systems for the Particles page: a system of a .pcf as values
// that can be changed and written again, new systems from templates, and saving into the mod.
//
// How the game loads them (Source SDK 2013, game/shared/particle_parse.cpp): the client and the
// server read particles/particles_manifest.txt - one file through the GAME search path, so a
// mod's own manifest replaces the game's completely and has to list the game's .pcf files too
// (Mapsmith starts it from the game's) - and load every "file" in it; a "!" in front precaches
// the systems with every map. maps/<map>_particles.txt adds files for one map (only under
// particles/). Parts link to their systems by element inside one file, so a child system has
// to be in the same .pcf as its parent: saving copies the children of an edited system along.
//
// Mapsmith writes the mod's own file particles/<name>.pcf (binary DMX version 2, "format pcf
// 1", like the game's files) and lists it with "!" in the mod's manifest. A system named like
// one of the game's replaces it in the game only if the engine's particle library keeps the
// last definition of a name (its source is not part of the SDK; not tested in the game) - so
// the editor saves edited game systems under a new name by default.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/particles.h>

namespace sme {

/// A system of a file as values that can be written again. Values whose type cannot be written
/// (element references other than the parts, binary data, matrices, string arrays) are left out
/// and their keys added to `dropped` ("operators/Remap.some_key").
[[nodiscard]] PcfSystem toPcfSystem(const ParticleDefinition& definition, std::vector<std::string>* dropped = nullptr);
/// A value read from a file as a value to write (same type); nullopt for types that cannot be written.
[[nodiscard]] std::optional<PcfValue> toPcfValue(const ParticleValue& value);
/// A PcfValue as text for a field and back ("255 128 0 255", "0 0 -500", "1"); fromText keeps the
/// type of `like` and gives nullopt for text that does not fit it.
[[nodiscard]] std::string pcfValueText(const PcfValue& value);
[[nodiscard]] std::optional<PcfValue> pcfValueFromText(std::string_view text, const PcfValue& like);

/// The systems as the game would read them back (write, then read): what the preview shows is
/// exactly what saving writes.
[[nodiscard]] std::vector<ParticleDefinition> roundTrip(const std::vector<PcfSystem>& systems);

/// Starting points for new systems.
enum class ParticleTemplate { Sparks, Glow, Smoke, MuzzleFlash, Embers };
[[nodiscard]] std::vector<ParticleTemplate> particleTemplates();
[[nodiscard]] std::string_view particleTemplateLabel(ParticleTemplate kind);
/// A new system of the kind, named `name` (materials of HL2 that every Source 1 game has).
[[nodiscard]] PcfSystem particleFromTemplate(ParticleTemplate kind, std::string_view name);

/// The parts a system can have, with the values the editor offers when one is added (the
/// engine's function names, as HL2's own .pcf files use them). `group`: "emitters",
/// "initializers", "operators", "renderers".
struct ParticleFunctionInfo {
	std::string group;
	std::string name;
	std::string description;
	PcfValues defaults;
};
[[nodiscard]] const std::vector<ParticleFunctionInfo>& particleFunctionCatalog();

/// The systems to save: `edited` plus every child they name (from `library`, recursively,
/// each once), so the file is complete.
[[nodiscard]] std::vector<PcfSystem> withChildren(const std::vector<PcfSystem>& edited, const std::vector<ParticleDefinition>& library);

/// A system name as the game takes it: letters, digits, '_', '-', '.' (spaces become '_').
[[nodiscard]] std::string particleSystemName(std::string_view text);

/// The mod's own particle file Mapsmith edits: particles/<mod name>_mapsmith.pcf.
[[nodiscard]] std::string modParticleFile(std::string_view modName);

/// Writes `systems` (merged into the systems already in the mod's file `file`: same names are
/// replaced, the others kept) to <mod>/<file> and lists it in the mod's manifest (made from
/// `gameManifest` if the mod has none). The first time an existing file is kept as
/// <file>.mapsmith-backup. Returns the change (kind "particles"); undoImport takes it back.
[[nodiscard]] std::optional<ImportRecord> saveModParticles(const std::filesystem::path& modDir, std::string_view file, const std::vector<PcfSystem>& systems,
                                                           std::string_view gameManifest, std::string* error = nullptr);

} // namespace sme
