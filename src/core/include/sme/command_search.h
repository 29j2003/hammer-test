// command_search.h - Matching for the command search (Ctrl+K): which menu entries, tools, pages
// and assets fit what was typed, best first.
//
// Every typed word has to appear in the entry (case does not matter; "rd bld" does not match
// "Build road", "road bu" does). Entries rank higher when the whole query starts the name, when
// words start where words of the name start, and when the name is short; where the entry lives
// ("Brushes", "Materials") counts too, but less than the name.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sme {

/// How well `query` fits an entry called `name` found under `where`: -1 if a word is missing,
/// else higher is better. An empty query fits everything with 0.
[[nodiscard]] int commandMatchScore(std::string_view query, std::string_view name, std::string_view where = {});

/// Indices of `names` (with their `wheres`, same length or empty) that fit, best first, at most
/// `limit`; ties keep the list's order.
[[nodiscard]] std::vector<std::size_t> rankCommands(std::string_view query, const std::vector<std::string>& names,
                                                    const std::vector<std::string>& wheres = {}, std::size_t limit = 50);

} // namespace sme
