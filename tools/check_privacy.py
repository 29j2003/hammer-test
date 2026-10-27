#!/usr/bin/env python3
"""Privacy check: no e-mail addresses or secrets in the repository.

Checks every file tracked by git (text files only) for e-mail addresses and
typical secret formats, and every commit for author/committer addresses.
Only noreply addresses are allowed (GitHub user noreply, GitHub web commits,
Anthropic). Exit code 1 on findings.

    python tools/check_privacy.py
"""

import re
import subprocess
import sys

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
    """Run git and return stdout as bytes."""
    return subprocess.run(["git", *args], check=True, capture_output=True).stdout


def is_binary(data):
    """Treat files containing NUL bytes as binary."""
    return b"\0" in data[:8192]


def check_files():
    """Findings in tracked files as (path, line, message)."""
    findings = []
    for path in git("ls-files", "-z").split(b"\0"):
        if not path:
            continue
        name = path.decode("utf-8", "replace")
        try:
            with open(path, "rb") as f:
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


def check_commits():
    """Findings in commit metadata as (commit, 0, message)."""
    findings = []
    log = git("log", "--all", "--format=%H%x00%ae%x00%ce")
    for entry in log.splitlines():
        sha, author, committer = entry.split(b"\0")
        for role, address in (("author", author), ("committer", committer)):
            if not ALLOWED_EMAIL.match(address):
                findings.append((sha[:10].decode(), 0, "%s address is not a noreply address" % role))
    return findings


def main():
    """Run all checks and report."""
    findings = check_files() + check_commits()
    for where, line, message in findings:
        print("%s:%d: %s" % (where, line, message) if line else "%s: %s" % (where, message))
    if findings:
        print("privacy check failed: %d finding(s)" % len(findings))
        return 1
    print("privacy check ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
