// model_rebuild.h - Changing a compiled model of the game in place: the same model path, rebuilt
// with changes (other events in a sequence, a sequence with new frames, new sequences) into the
// mod's own folder, where it covers the game's copy - the game loads <mod>/models/... before its
// VPKs (search paths of gameinfo.txt, the mod's folder first). Used by the Weapons page (sound
// events of a viewmodel), the animation editor (new frames) and effects on models (particles or
// lights during an animation). The steps:
//
//   1. prepareModelRebuild(): takes the model apart (model_export.h decompileModel) with every
//      change stored so far plus the new ones, writes the sources and changes.txt to
//      <mod>/modelsrc/mapsmith_rebuild/<model path>/, keeps the mod's own copy of the compiled
//      files (if it had one before the first rebuild) as <file>.mapsmith-backup, and names the
//      studiomdl call.
//   2. The caller runs the game's studiomdl.exe with plan.arguments (Windows; it is not part of
//      Mapsmith).
//   3. finishModelRebuild(): records the compiled files in the mod's import list
//      (cfg/mapsmith_imports.txt, kind "model rebuild"), so "Restore original" (restoreModel)
//      takes them out again and puts the mod's own files back.
//
// Changes always apply to the base model, never to an earlier rebuild: the stored changes.txt has
// every change, and the model is taken apart from the game's copy without the mod's rebuilt files
// (base "game"), so editing twice does not decompile a decompile. If the mod had its own copy of
// the model before the first rebuild, that copy is the base (base "mod") and is decompiled as the
// game sees it.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sme/asset_import.h>
#include <sme/model_events.h>
#include <sme/model_export.h>

namespace sme {

class GameFileSystem;

/// Changes to a model (keys: sequence names in lower case, as ModelExportOptions).
struct ModelRebuildChanges {
	std::map<std::string, std::vector<ModelEvent>> events;          ///< A sequence's whole event list
	std::map<std::string, SequenceReplacement> replacements;        ///< A sequence's frames (or a new sequence)
	bool operator==(const ModelRebuildChanges&) const = default;
	[[nodiscard]] bool empty() const { return events.empty() && replacements.empty(); }
	/// Adds `newer` on top: its sequences replace these' (events and frames separately).
	void merge(const ModelRebuildChanges& newer);
};

/// The changes as stored in changes.txt (KeyValues; replacement frames as .smd files next to it,
/// named in `smdFiles` as "edits/<sequence>.smd" -> text).
struct StoredModelChanges {
	std::string model;              ///< "models/weapons/v_smg1.mdl"
	std::string base = "game";      ///< "game": the game's copy without the mod's rebuild; "mod": the mod's own copy
	ModelRebuildChanges changes;
};
[[nodiscard]] std::string writeModelChanges(const StoredModelChanges& stored, std::map<std::string, std::string>* smdFiles);
/// Reads changes.txt; `readSmd("edits/x.smd")` gives the text of a replacement file (empty if missing).
[[nodiscard]] std::optional<StoredModelChanges> readModelChanges(std::string_view text, const std::function<std::string(std::string_view)>& readSmd);

/// <mod>/modelsrc/mapsmith_rebuild/<model path without models/ and .mdl>
[[nodiscard]] std::filesystem::path modelRebuildFolder(const std::filesystem::path& modDir, std::string_view modelPath);
/// The changes stored for a model so far (nullopt if it was never rebuilt).
[[nodiscard]] std::optional<StoredModelChanges> loadModelChanges(const std::filesystem::path& modDir, std::string_view modelPath);
/// The compiled files of a model in the mod (<mod>/models/x.mdl, .vvd, .dx90.vtx, .dx80.vtx, .sw.vtx, .phy, .ani).
[[nodiscard]] std::vector<std::filesystem::path> modelCompiledFiles(const std::filesystem::path& modDir, std::string_view modelPath);
/// The import list entry of an earlier rebuild of the model, if any.
[[nodiscard]] std::optional<ImportRecord> modelRebuildRecord(const std::filesystem::path& modDir, std::string_view modelPath);

/// A copy of the file system without `folder` and the VPKs inside it (the mod's own files), as
/// GameFileSystem::readOriginal sees the game; with `front`, that folder is searched first.
[[nodiscard]] GameFileSystem fileSystemWithout(const GameFileSystem& fs, const std::filesystem::path& folder, const std::filesystem::path& front = {});

/// What prepareModelRebuild wrote and how to call studiomdl.
struct ModelRebuildPlan {
	std::string model;                          ///< "models/weapons/v_smg1.mdl"
	std::filesystem::path folder;               ///< The sources
	std::filesystem::path qc;
	std::vector<std::string> arguments;         ///< For studiomdl: -game <mod> -nop4 -verbose <qc>
	ImportRecord record;                        ///< "model rebuild": compiled files (after finish), backups of the mod's own
	bool earlierRebuild = false;                ///< The model was rebuilt before (its record is replaced on finish)
	std::vector<std::string> notes;             ///< What the decompile left out
};

/// Step 1 (see the file comment). `changes` come on top of the stored ones. The model must be a
/// path below models/ (isSafeGamePath). Writes into the mod only below modelsrc/ and, for the
/// mod's own copy, the .mapsmith-backup files.
[[nodiscard]] std::optional<ModelRebuildPlan> prepareModelRebuild(const GameFileSystem& fs, const std::filesystem::path& modDir, std::string_view modelPath,
                                                                  const ModelRebuildChanges& changes, std::string* error = nullptr,
                                                                  const std::function<bool(int, int)>& progress = {});
/// Step 3: after studiomdl. False (with `error`) if the .mdl is not there (the compile failed);
/// then the mod's own files are put back from their backups.
bool finishModelRebuild(ModelRebuildPlan& plan, const std::filesystem::path& modDir, std::string* error = nullptr);
/// Takes a rebuild out again: removes the compiled files, puts the mod's own back, forgets the
/// stored changes (the sources stay). True if there was nothing to do.
bool restoreModel(const std::filesystem::path& modDir, std::string_view modelPath, std::string* error = nullptr);

} // namespace sme
