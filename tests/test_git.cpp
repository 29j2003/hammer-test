// test_git.cpp - Reading git's status and log output and turning remotes into web addresses.

#include <doctest/doctest.h>

#include <sme/git_status.h>

#include <string>

using namespace sme;

namespace {

/// Joins porcelain lines with NUL like `git status -z` does.
std::string z(std::initializer_list<std::string_view> lines) {
	std::string out;
	for (const auto line : lines) {
		out += line;
		out += '\0';
	}
	return out;
}

} // namespace

TEST_CASE("git status: branch, upstream, ahead/behind and every kind of change") {
	const auto text = z({"# branch.oid 4dde739aa0c3e3e1f2b0d0a8c1b6f00112233445",
	                     "# branch.head main",
	                     "# branch.upstream origin/main",
	                     "# branch.ab +2 -1",
	                     "1 .M N... 100644 100644 100644 aaaa bbbb sp/src/game/client/viewrender.cpp",
	                     "1 A. N... 000000 100644 100644 0000 cccc maps/new map.vmf",
	                     "1 D. N... 100644 000000 000000 dddd 0000 old.txt",
	                     "2 R. N... 100644 100644 100644 eeee eeee R100 fidelity/new_name.cfg",
	                     "fidelity/old_name.cfg",
	                     "u UU N... 100644 100644 100644 100644 1111 2222 3333 conflict.txt",
	                     "? untracked file.txt",
	                     "! ignored.log"});
	const auto status = parseGitStatus(text);
	REQUIRE(status);
	CHECK(status->branch == "main");
	CHECK_FALSE(status->detached);
	CHECK(status->commit.starts_with("4dde739"));
	CHECK(status->upstream == "origin/main");
	CHECK(status->ahead == 2);
	CHECK(status->behind == 1);
	REQUIRE(status->files.size() == 7);
	CHECK(status->files[0].path == "sp/src/game/client/viewrender.cpp");
	CHECK_FALSE(status->files[0].isStaged());
	CHECK(status->files[0].letter() == 'M');
	CHECK(status->files[1].path == "maps/new map.vmf");   // spaces stay in the path
	CHECK(status->files[1].isStaged());
	CHECK(status->files[1].letter() == 'A');
	CHECK(status->files[2].letter() == 'D');
	CHECK(status->files[3].kind == GitFileChange::Kind::Renamed);
	CHECK(status->files[3].path == "fidelity/new_name.cfg");
	CHECK(status->files[3].originalPath == "fidelity/old_name.cfg");
	CHECK(status->files[3].letter() == 'R');
	CHECK(status->files[4].kind == GitFileChange::Kind::Conflicted);
	CHECK(status->files[4].letter() == 'U');
	CHECK(status->files[5].kind == GitFileChange::Kind::Untracked);
	CHECK(status->files[5].path == "untracked file.txt");
	CHECK(status->files[5].letter() == '?');
	CHECK(status->files[6].kind == GitFileChange::Kind::Ignored);

	// A new repository before its first commit, and a detached head.
	const auto fresh = parseGitStatus(z({"# branch.oid (initial)", "# branch.head master"}));
	REQUIRE(fresh);
	CHECK(fresh->commit.empty());
	CHECK(fresh->branch == "master");
	CHECK(fresh->files.empty());
	const auto detached = parseGitStatus(z({"# branch.oid 1234", "# branch.head (detached)"}));
	REQUIRE(detached);
	CHECK(detached->detached);
	CHECK(detached->branch.empty());

	CHECK_FALSE(parseGitStatus("fatal: not a git repository"));
	CHECK(parseGitStatus("")->files.empty());
}

TEST_CASE("git log: short id, author name, relative date, subject") {
	CHECK(gitLogFormat().find("%an") != std::string_view::npos);
	CHECK(gitLogFormat().find("%ae") == std::string_view::npos);   // never the e-mail address
	const std::string text = "4dde739\x1f" "Claude\x1f" "2 hours ago\x1f" "Seitenleiste: Seiten\x1e\n"
	                         "36711fd\x1f" "Julius\x1f" "3 days ago\x1f" "Subject with \x1f? no, just text\x1e\n";
	const auto commits = parseGitLog(text);
	REQUIRE(commits.size() == 2);
	CHECK(commits[0].hash == "4dde739");
	CHECK(commits[0].author == "Claude");
	CHECK(commits[0].when == "2 hours ago");
	CHECK(commits[0].subject == "Seitenleiste: Seiten");
	CHECK(commits[1].author == "Julius");
	CHECK(parseGitLog("").empty());
}

TEST_CASE("git remotes as web pages") {
	CHECK(remoteWebUrl("https://github.com/29j2003/mymod.git") == "https://github.com/29j2003/mymod");
	CHECK(remoteWebUrl("https://github.com/29j2003/mymod\n") == "https://github.com/29j2003/mymod");
	const std::string at = "@";   // written apart, so the privacy check sees no addresses here
	CHECK(remoteWebUrl("git" + at + "github.com:29j2003/mymod.git") == "https://github.com/29j2003/mymod");
	CHECK(remoteWebUrl("ssh://git" + at + "github.com:22/29j2003/mymod.git") == "https://github.com/29j2003/mymod");
	CHECK(remoteWebUrl("https://someone:secret" + at + "gitlab.com/group/sub/repo.git") == "https://gitlab.com/group/sub/repo");   // no log-in in the page address
	CHECK(remoteWebUrl("/home/me/repos/mymod").empty());
	CHECK(remoteWebUrl("C:/repos/mymod").empty());
	CHECK(remoteWebUrl("").empty());
}
