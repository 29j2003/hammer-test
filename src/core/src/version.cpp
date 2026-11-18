// version.cpp - Version comparison and checksum files (see version.h).

#include <sme/version.h>

#include <sme/text.h>

#include <cctype>
#include <vector>

namespace sme {

namespace {

std::vector<long> parts(std::string_view version) {
	if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) {
		version.remove_prefix(1);
	}
	std::vector<long> out;
	long current = 0;
	bool digits = false;
	for (const char c : version) {
		if (std::isdigit(static_cast<unsigned char>(c))) {
			current = current * 10 + (c - '0');
			digits = true;
			if (current > 1'000'000) current = 1'000'000;
		} else if (c == '.') {
			out.push_back(current);
			current = 0;
			digits = false;
		} else {
			break;   // "-beta", "+build": not compared
		}
	}
	if (digits || !out.empty()) {
		out.push_back(current);
	}
	return out;
}

} // namespace

int compareVersions(std::string_view a, std::string_view b) {
	const auto pa = parts(a), pb = parts(b);
	for (std::size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
		const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
		if (x != y) {
			return x < y ? -1 : 1;
		}
	}
	return 0;
}

std::optional<TestPackage> testPackage(std::string_view name) {
	constexpr std::string_view prefix = "Mapsmith-", marker = "-test.", suffix = "-windows.zip";
	if (name.size() > 128 || name.size() <= prefix.size() + marker.size() + suffix.size() || name.substr(0, prefix.size()) != prefix ||
	    name.substr(name.size() - suffix.size()) != suffix) {
		return std::nullopt;
	}
	const auto middle = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
	const auto at = middle.rfind(marker);
	if (at == std::string_view::npos || at == 0) return std::nullopt;
	const auto version = middle.substr(0, at), digits = middle.substr(at + marker.size());
	for (const char c : version) {
		if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return std::nullopt;
	}
	if (digits.empty() || digits.size() > 9) return std::nullopt;
	long number = 0;
	for (const char c : digits) {
		if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
		number = number * 10 + (c - '0');
	}
	if (number <= 0) return std::nullopt;
	return TestPackage{std::string(version), number};
}

std::optional<std::string> checksumFor(std::string_view checksumFile, std::string_view fileName) {
	for (const auto& line : splitTrimmed(checksumFile, '\n')) {
		const auto space = line.find_first_of(" \t");
		if (space == std::string::npos) continue;
		auto hash = toLower(line.substr(0, space));
		auto name = std::string(trim(std::string_view(line).substr(space)));
		if (!name.empty() && name.front() == '*') name.erase(0, 1);   // binary mode marker
		if (hash.size() == 64 && name == fileName) {
			return hash;
		}
	}
	return std::nullopt;
}

} // namespace sme
