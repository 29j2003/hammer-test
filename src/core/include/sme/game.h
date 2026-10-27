// game.h - A game or mod opened for editing: gameinfo, mounted files and entity definitions.

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <sme/entity_catalog.h>
#include <sme/filesystem.h>
#include <sme/game_discovery.h>
#include <sme/gameinfo.h>

namespace sme {

/// Everything the editor needs of the chosen game: where it is, its gameinfo.txt, its files and its entity classes.
struct LoadedGame {
	GameEntry entry;
	GameInfo info;
	GameFileSystem files;
	EntityCatalog entities;
	std::vector<std::string> problems;   ///< Missing search paths, FGD errors, ... (shown to the user)
};

/// Opens a discovered game: reads gameinfo.txt, mounts its search paths and loads the FGDs.
/// Returns nullptr only if gameinfo.txt cannot be read (`error` says why).
[[nodiscard]] std::unique_ptr<LoadedGame> loadGame(const GameEntry& entry, const SteamInstallation& steam,
                                                   const std::vector<std::filesystem::path>& fgds, std::string* error = nullptr);

} // namespace sme
