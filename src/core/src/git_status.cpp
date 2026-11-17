// git_status.cpp - Parsers for git's machine-readable output (see git_status.h).

#include <sme/git_status.h>

#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>

namespace sme {

namespace {

/// Splits off `count` space-separated fields; the rest (a path, which may hold spaces) stays in `rest`.
bool splitFields(std::string_view line, std::size_t count, std::vector<std::string_view>& fields, std::string_view& rest) {
	fields.clear();
	for (std::size_t i = 0; i < count; ++i) {
		const auto space = line.find(' ');
		if (space == std::string_view::npos) return false;
		fields.push_back(line.substr(0, space));
		line.remove_prefix(space + 1);
	}
	rest = line;
	return true;
}

char state(char c) {
	return c == '.' ? ' ' : c;
}

int number(std::string_view text) {
	int value = 0;
	if (!text.empty() && (text.front() == '+' || text.front() == '-')) text.remove_prefix(1);
	std::from_chars(text.data(), text.data() + text.size(), value);
	return value;
}

} // namespace

char GitFileChange::letter() const {
	switch (kind) {
		case Kind::Untracked: return '?';
		case Kind::Ignored: return '!';
		case Kind::Conflicted: return 'U';
		case Kind::Renamed: return 'R';
		case Kind::Changed: break;
	}
	for (const char c : {staged, unstaged}) {
		if (c == 'A' || c == 'D') return c;
	}
	return 'M';
}

std::optional<GitStatus> parseGitStatus(std::string_view text) {
	GitStatus status;
	std::vector<std::string_view> tokens;
	while (!text.empty()) {
		const auto end = text.find('\0');
		tokens.push_back(text.substr(0, end));
		if (end == std::string_view::npos) break;
		text.remove_prefix(end + 1);
	}
	std::vector<std::string_view> fields;
	std::string_view rest;
	for (std::size_t i = 0; i < tokens.size(); ++i) {
		const std::string_view line = tokens[i];
		if (line.empty()) continue;
		if (line.size() < 2 || line[1] != ' ') return std::nullopt;
		switch (line[0]) {
			case '#': {
				const auto body = line.substr(2);
				const auto space = body.find(' ');
				const auto key = body.substr(0, space);
				const auto value = space == std::string_view::npos ? std::string_view{} : body.substr(space + 1);
				if (key == "branch.oid") {
					if (value != "(initial)") status.commit = std::string(value);
				} else if (key == "branch.head") {
					status.detached = value == "(detached)";
					if (!status.detached) status.branch = std::string(value);
				} else if (key == "branch.upstream") {
					status.upstream = std::string(value);
				} else if (key == "branch.ab") {
					const auto split = value.find(' ');
					status.ahead = number(value.substr(0, split));
					if (split != std::string_view::npos) status.behind = number(value.substr(split + 1));
				}
				break;
			}
			case '1':
			case '2': {
				const bool renamed = line[0] == '2';
				if (!splitFields(line.substr(2), renamed ? 8 : 7, fields, rest) || fields[0].size() != 2) return std::nullopt;
				GitFileChange change;
				change.kind = renamed ? GitFileChange::Kind::Renamed : GitFileChange::Kind::Changed;
				change.staged = state(fields[0][0]);
				change.unstaged = state(fields[0][1]);
				change.path = std::string(rest);
				if (renamed && i + 1 < tokens.size()) change.originalPath = std::string(tokens[++i]);   // -z: the old path follows
				status.files.push_back(std::move(change));
				break;
			}
			case 'u': {
				if (!splitFields(line.substr(2), 9, fields, rest)) return std::nullopt;
				GitFileChange change;
				change.kind = GitFileChange::Kind::Conflicted;
				change.staged = 'U';
				change.unstaged = 'U';
				change.path = std::string(rest);
				status.files.push_back(std::move(change));
				break;
			}
			case '?':
			case '!': {
				GitFileChange change;
				change.kind = line[0] == '?' ? GitFileChange::Kind::Untracked : GitFileChange::Kind::Ignored;
				change.path = std::string(line.substr(2));
				status.files.push_back(std::move(change));
				break;
			}
			default: return std::nullopt;
		}
	}
	return status;
}

std::string_view gitLogFormat() {
	return "%h%x1f%an%x1f%ar%x1f%s%x1e";
}

std::vector<GitCommit> parseGitLog(std::string_view text) {
	std::vector<GitCommit> out;
	while (!text.empty()) {
		const auto end = text.find('\x1e');
		std::string_view record = text.substr(0, end);
		text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
		while (!record.empty() && (record.front() == '\n' || record.front() == '\r')) record.remove_prefix(1);
		if (record.empty()) continue;
		std::string fields[4];
		for (int f = 0; f < 4; ++f) {
			const auto sep = record.find('\x1f');
			fields[f] = std::string(record.substr(0, sep));
			record = sep == std::string_view::npos ? std::string_view{} : record.substr(sep + 1);
		}
		if (fields[0].empty()) continue;
		out.push_back({fields[0], fields[1], fields[2], fields[3]});
	}
	return out;
}

std::string remoteWebUrl(std::string_view remote) {
	while (!remote.empty() && (remote.back() == '\n' || remote.back() == '\r' || remote.back() == ' ' || remote.back() == '/')) remote.remove_suffix(1);
	std::string_view host, path;
	if (const auto scheme = remote.find("://"); scheme != std::string_view::npos) {
		const auto protocol = remote.substr(0, scheme);
		if (protocol != "https" && protocol != "http" && protocol != "ssh" && protocol != "git") return {};
		auto rest = remote.substr(scheme + 3);
		const auto slash = rest.find('/');
		if (slash == std::string_view::npos) return {};
		host = rest.substr(0, slash);
		path = rest.substr(slash + 1);
		if (const auto at = host.rfind('@'); at != std::string_view::npos) host.remove_prefix(at + 1);   // user or token
		if (const auto colon = host.find(':'); colon != std::string_view::npos) host = host.substr(0, colon);   // ssh port
	} else if (const auto colon = remote.find(':'); colon != std::string_view::npos && remote.find('@') < colon) {
		// scp form: user@host:owner/repo.git
		host = remote.substr(remote.find('@') + 1, colon - remote.find('@') - 1);
		path = remote.substr(colon + 1);
	} else {
		return {};
	}
	if (path.ends_with(".git")) path.remove_suffix(4);
	if (host.empty() || path.empty() || host.find('.') == std::string_view::npos) return {};
	return "https://" + std::string(host) + "/" + std::string(path);
}

std::vector<std::string> gitConfigRisks(std::string_view configText) {
	const auto lower = [](std::string_view text) {
		std::string out(text);
		for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
		return out;
	};
	const auto trim = [](std::string_view text) {
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
		return text;
	};
	std::vector<std::string> risks;
	std::string section, subsection;
	std::size_t at = 0;
	while (at < configText.size()) {
		auto end = configText.find('\n', at);
		if (end == std::string_view::npos) end = configText.size();
		std::string_view line = trim(configText.substr(at, end - at));
		at = end + 1;
		if (line.empty() || line.front() == '#' || line.front() == ';') continue;
		if (line.front() == '[') {
			const auto close = line.find(']');
			std::string_view header = line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1);
			subsection.clear();
			if (const auto quote = header.find('"'); quote != std::string_view::npos) {
				section = lower(trim(header.substr(0, quote)));
				auto sub = header.substr(quote + 1);
				if (const auto last = sub.rfind('"'); last != std::string_view::npos) sub = sub.substr(0, last);
				subsection = std::string(sub);
			} else if (const auto dot = header.find('.'); dot != std::string_view::npos) {
				section = lower(trim(header.substr(0, dot)));
				subsection = lower(trim(header.substr(dot + 1)));
			} else {
				section = lower(trim(header));
			}
			// "[core] fsmonitor = x" on one line: the rest after the header is a key.
			if (close == std::string_view::npos) continue;
			line = trim(line.substr(close + 1));
			if (line.empty() || line.front() == '#' || line.front() == ';') continue;
		}
		const auto equals = line.find('=');
		const std::string key = lower(trim(line.substr(0, equals)));
		std::string value(equals == std::string_view::npos ? std::string_view("true") : trim(line.substr(equals + 1)));
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
		const std::string lowerValue = lower(value);
		const auto isBoolean = [&lowerValue] {
			for (const char* word : {"true", "false", "yes", "no", "on", "off", "1", "0", ""}) {
				if (lowerValue == word) return true;
			}
			return false;
		};
		bool risky = false;
		if (section == "core") {
			risky = (key == "fsmonitor" && !isBoolean()) || key == "hookspath" || key == "sshcommand" || key == "gitproxy" || key == "askpass";
		} else if (section == "include" || section == "includeif") {
			risky = key == "path";
		} else if (section == "filter") {
			risky = (key == "clean" || key == "smudge" || key == "process") && !lowerValue.starts_with("git-lfs ") &&
			        !lowerValue.starts_with("git lfs ");
		} else if (section == "diff") {
			risky = key == "external" || key == "textconv" || key == "command";
		} else if (section == "merge") {
			risky = key == "driver";
		} else if (section == "credential") {
			risky = key == "helper" && (value.starts_with('!') || value.find_first_of("/\\") != std::string::npos);
		} else if (section == "remote") {
			risky = key == "uploadpack" || key == "receivepack" || ((key == "url" || key == "pushurl") && lowerValue.starts_with("ext::"));
		} else if (section == "url") {
			risky = subsection.starts_with("ext::");   // url.<base>.insteadOf rewriting to the ext:: transport
		}
		if (risky) {
			std::string name = section + (subsection.empty() ? "" : "." + subsection) + "." + key;
			risks.push_back(name + " = " + value.substr(0, 120));
		}
	}
	return risks;
}

std::vector<std::string> gitRepositoryRisks(const std::filesystem::path& root) {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path gitDir = root / ".git";
	if (fs::is_regular_file(gitDir, ec)) {
		// A worktree or submodule: ".git" is a file "gitdir: <path>".
		std::ifstream in(gitDir);
		std::string line;
		std::getline(in, line);
		if (!line.starts_with("gitdir:")) return {};
		std::string target = line.substr(7);
		while (!target.empty() && std::isspace(static_cast<unsigned char>(target.front()))) target.erase(0, 1);
		while (!target.empty() && std::isspace(static_cast<unsigned char>(target.back()))) target.pop_back();
		gitDir = fs::path(target);
		if (gitDir.is_relative()) gitDir = root / gitDir;
	}
	std::vector<std::string> out;
	std::vector<fs::path> files{gitDir / "config", gitDir / "config.worktree"};
	// A linked worktree keeps the shared config in the main repository ("commondir").
	if (std::ifstream common(gitDir / "commondir"); common) {
		std::string line;
		std::getline(common, line);
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
		if (!line.empty()) {
			fs::path shared(line);
			if (shared.is_relative()) shared = gitDir / shared;
			files.push_back(shared / "config");
		}
	}
	for (const fs::path& file : files) {
		std::ifstream in(file, std::ios::binary);
		if (!in) continue;
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		for (auto& risk : gitConfigRisks(text)) out.push_back(std::move(risk));
	}
	return out;
}

} // namespace sme
