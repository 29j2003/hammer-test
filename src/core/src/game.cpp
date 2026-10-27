// game.cpp - Opening a game for editing (see game.h).

#include <sme/game.h>

#include <sme/text.h>

namespace sme {

std::unique_ptr<LoadedGame> loadGame(const GameEntry& entry, const SteamInstallation& steam,
                                     const std::vector<std::filesystem::path>& fgds, std::string* error) {
	auto info = GameInfo::load(entry.gameDir, error);
	if (!info) {
		return nullptr;
	}
	auto game = std::make_unique<LoadedGame>();
	game->entry = entry;
	resolveSearchPaths(*info, makeSearchPathContext(*info, &steam));
	game->info = std::move(*info);
	game->files = GameFileSystem::fromGameInfo(game->info);

	for (const auto& warning : game->info.warnings) {
		game->problems.push_back(warning);
	}
	for (const auto& mount : game->files.mounts()) {
		if (!mount.present) {
			game->problems.push_back(mount.problem + ": " + (mount.path.empty() ? mount.source : displayPath(mount.path)));
		}
	}
	if (!fgds.empty()) {
		game->entities.load(fgds, &game->problems);
	} else {
		game->problems.emplace_back("No FGD selected - entities are unavailable");
	}
	return game;
}

} // namespace sme
