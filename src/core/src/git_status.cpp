// git_status.cpp - Parsers for git's machine-readable output (see git_status.h).

#include <sme/git_status.h>

#include <charconv>

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

} // namespace sme
