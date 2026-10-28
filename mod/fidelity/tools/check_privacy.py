#!/usr/bin/env python3
"""Privacy check: no e-mail addresses or secrets in our own files and commits.

The mod repository is a Mapbase fork, so Valve's and Mapbase's files and their
commit history (with real e-mail addresses of other people) are not ours to
check. This script looks at:

- the files tracked by git below OWN_PATHS (text files only): e-mail
  addresses and typical secret formats;
- the commits after --since (default: origin/master, i.e. the commits of the
  current branch): author and committer addresses, and the message (e-mail
  addresses in trailers such as Co-Authored-By, secrets).

Only noreply addresses are allowed (GitHub user noreply, GitHub web commits,
Anthropic). Exit code 1 on findings. Run from anywhere inside the repository:

    python fidelity/tools/check_privacy.py [--since <commit>]
"""

import argparse
import os
import re
import subprocess
import sys

# Repository paths that belong to this project (relative to the repository root)
OWN_PATHS = [
    ".claude",
    ".github/workflows/fidelity.yml",
    "docs",
    "fidelity",
    "sp/src/game/client/fidelity",
    "sp/src/materialsystem/fidelity",
]

EMAIL = re.compile(rb"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
ALLOWED_EMAIL = re.compile(
    rb"^(?:[0-9]+\+)?[A-Za-z0-9-]+@users\.noreply\.github\.com$|^noreply@(?:anthropic|github)\.com$"
)

SECRETS = [
    ("GitHub token", re.compile(rb"\b(?:ghp|gho|ghu|ghs|ghr|github_pat)_[A-Za-z0-9_]{20,}")),
    ("AWS key", re.compile(rb"\bAKIA[0-9A-Z]{16}\b")),
    ("private key", re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("Slack token", re.compile(rb"\bxox[abprs]-[A-Za-z0-9-]{10,}")),
    ("Steam Web API key", re.compile(rb"(?i)steam[_-]?api[_-]?key\s*[:=]\s*['\"]?[0-9A-F]{32}")),
]


def git(*args):
    """Run git in the repository root and return stdout as bytes."""
    return subprocess.run(["git", *args], check=True, capture_output=True, cwd=REPO_ROOT).stdout


REPO_ROOT = (
    subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        capture_output=True,
        cwd=os.path.dirname(os.path.abspath(__file__)),
    )
    .stdout.decode()
    .strip()
)


def is_binary(data):
    """Treat files containing NUL bytes as binary."""
    return b"\0" in data[:8192]


def check_files():
    """Findings in tracked files as (path, line, message)."""
    findings = []
    for path in git("ls-files", "-z", "--", *OWN_PATHS).split(b"\0"):
        if not path:
            continue
        name = path.decode("utf-8", "replace")
        try:
            with open(os.path.join(REPO_ROOT.encode(), path), "rb") as f:
                data = f.read()
        except OSError:
            continue
        if is_binary(data):
            continue
        for number, line in enumerate(data.splitlines(), 1):
            for match in EMAIL.finditer(line):
                if not ALLOWED_EMAIL.match(match.group(0)):
                    findings.append((name, number, "e-mail address"))
            for label, pattern in SECRETS:
                if pattern.search(line):
                    findings.append((name, number, label))
    return findings


def check_commits(since):
    """Findings in the commits after since (addresses and message) as (commit, 0, message)."""
    findings = []
    # Records end with \x01 (a message has line breaks of its own), fields are split by \x00
    log = git("log", "--format=%H%x00%ae%x00%ce%x00%B%x01", "%s..HEAD" % since)
    for entry in log.split(b"\x01"):
        entry = entry.strip(b"\n")
        if not entry:
            continue
        sha, author, committer, message = entry.split(b"\0", 3)
        where = sha[:10].decode()
        for role, address in (("author", author), ("committer", committer)):
            if not ALLOWED_EMAIL.match(address):
                findings.append((where, 0, "%s address is not a noreply address" % role))
        for match in EMAIL.finditer(message):
            if not ALLOWED_EMAIL.match(match.group(0)):
                findings.append((where, 0, "e-mail address in the commit message"))
        for label, pattern in SECRETS:
            if pattern.search(message):
                findings.append((where, 0, "%s in the commit message" % label))
    return findings


def main():
    """Run all checks and report."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--since", default="origin/master", help="check the commits after this one (default: %(default)s)"
    )
    args = parser.parse_args()
    findings = check_files() + check_commits(args.since)
    for where, line, message in findings:
        print("%s:%d: %s" % (where, line, message) if line else "%s: %s" % (where, message))
    if findings:
        print("privacy check failed: %d finding(s)" % len(findings))
        return 1
    print("privacy check ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
