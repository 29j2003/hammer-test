// crash_log.cpp - Crash file lines (see crash_log.h).

#include <sme/crash_log.h>

#include <cctype>
#include <cstdio>

namespace sme {

std::size_t formatCrashFrame(char* out, std::size_t size, int index, std::string_view module, std::uint64_t offset) {
	if (!out || size == 0) return 0;
	const auto slash = module.find_last_of("/\\");
	if (slash != std::string_view::npos) module = module.substr(slash + 1);
	if (module.empty()) module = "?";
	const int n = std::snprintf(out, size, "  #%d %.*s+0x%llx\n", index, static_cast<int>(module.size()), module.data(), static_cast<unsigned long long>(offset));
	if (n < 0) {
		out[0] = '\0';
		return 0;
	}
	return static_cast<std::size_t>(n) < size ? static_cast<std::size_t>(n) : size - 1;
}

const char* crashCodeName(std::uint32_t code) {
	switch (code) {
		case 0xC0000005u: return "access violation";
		case 0xC00000FDu: return "stack overflow";
		case 0xC0000094u: return "integer division by zero";
		case 0xC000001Du: return "illegal instruction";
		case 0xC0000409u: return "stack buffer overrun / fast fail";
		case 0xC0000374u: return "heap corruption";
		case 0xE06D7363u: return "C++ exception";
		case 0x80000003u: return "breakpoint";
		default: return "";
	}
}

std::string crashSummary(std::string_view text, std::string_view ownModule) {
	if (text.empty()) return {};
	const auto lower = [](std::string_view s) {
		std::string out(s);
		for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	};
	const auto firstEnd = text.find('\n');
	std::string summary(text.substr(0, firstEnd));
	while (!summary.empty() && (summary.back() == '\r' || summary.back() == ' ')) summary.pop_back();
	const std::string own = lower(ownModule);
	std::size_t at = firstEnd == std::string_view::npos ? text.size() : firstEnd + 1;
	while (at < text.size()) {
		auto end = text.find('\n', at);
		if (end == std::string_view::npos) end = text.size();
		std::string_view line = text.substr(at, end - at);
		at = end + 1;
		const auto hash = line.find('#');
		const auto space = line.find(' ', hash == std::string_view::npos ? 0 : hash);
		if (hash == std::string_view::npos || space == std::string_view::npos) continue;
		std::string_view frame = line.substr(space + 1);
		while (!frame.empty() && (frame.back() == '\r' || frame.back() == ' ')) frame.remove_suffix(1);
		if (lower(frame).find(own) != std::string::npos) {
			summary += " at ";
			summary += frame;
			break;
		}
	}
	return summary;
}

} // namespace sme
