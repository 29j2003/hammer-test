// report.h - Builds the setup report (JSON) that users send to the developers.
//
// The report describes Steam libraries, Source games/mods, their search paths, FGDs and
// Hammer configs. Personal parts of paths (home folder, user name) are masked.

#pragma once

#include <filesystem>
#include <string>

#include <sme/game_discovery.h>

namespace sme::probe {

/// Replaces the home folder with "~" and "Users/<name>" with "Users/<user>".
[[nodiscard]] std::string maskPersonal(std::string text);

/// Full report as pretty-printed JSON.
[[nodiscard]] std::string buildReport(const DiscoveryResult& discovery);

} // namespace sme::probe
