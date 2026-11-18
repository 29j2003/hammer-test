// version.h - Version numbers ("0.2.0", "v0.10.1"), test build numbers and checksum lists, used by the
// update check.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sme {

/// Compares two versions part by part ("v" prefix and missing parts ignored):
/// negative if a < b, 0 if equal, positive if a > b. "0.10.0" is newer than "0.9.3".
[[nodiscard]] int compareVersions(std::string_view a, std::string_view b);

/// A test build's package: the version it was built from and the build number.
struct TestPackage {
	std::string version;   ///< "0.2.0"
	long build = 0;        ///< CI run number, > 0
};
/// Reads a test build's package name, "Mapsmith-0.2.0-test.133-windows.zip" (the CI publishes every
/// push as such a package, see the app's updater.h); nullopt for any other name.
[[nodiscard]] std::optional<TestPackage> testPackage(std::string_view packageName);

/// The SHA-256 for `fileName` from a checksum file ("<hex>  <name>" lines, as sha256sum and the
/// release workflow write them), lowercase; nullopt if it is not listed.
[[nodiscard]] std::optional<std::string> checksumFor(std::string_view checksumFile, std::string_view fileName);

} // namespace sme
