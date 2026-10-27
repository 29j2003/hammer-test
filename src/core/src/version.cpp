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
