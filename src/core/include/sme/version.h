// version.h - Version numbers ("0.2.0", "v0.10.1") and checksum lists, used by the update check.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sme {

/// Compares two versions part by part ("v" prefix and missing parts ignored):
/// negative if a < b, 0 if equal, positive if a > b. "0.10.0" is newer than "0.9.3".
[[nodiscard]] int compareVersions(std::string_view a, std::string_view b);

/// The SHA-256 for `fileName` from a checksum file ("<hex>  <name>" lines, as sha256sum and the
/// release workflow write them), lowercase; nullopt if it is not listed.
[[nodiscard]] std::optional<std::string> checksumFor(std::string_view checksumFile, std::string_view fileName);

} // namespace sme
