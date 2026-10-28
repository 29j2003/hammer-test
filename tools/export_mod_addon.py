#!/usr/bin/env python3
"""Exports the mapbase-fidelity add-on from the mod's source repository into mod/.

The mod (a fork of Mapbase, Source SDK 2013) is developed in its own repository. Mapsmith builds
the add-on itself and installs the finished files into a mod, so nobody needs a compiler. This
script copies what the add-on adds or changes on top of Mapbase, from a commit of the mod's
repository, into mod/ of this repository:

  mod/MAPBASE          the Mapbase repository and commit the add-on goes on, the exported commit
  mod/sp/...           every file the add-on adds under sp/ (C++ of client, server, shaders, VRAD)
  mod/mapbase.patch    every change to Mapbase's own files under sp/ (the "// mapbase-fidelity"
                       hooks, project lists, VS 2022 switch)
  mod/fidelity/...     the add-on's game files and scripts (shaders, cfg, deploy.ps1 ...)

The Source 1 SDK license and notices the code is under are Mapbase's own files (LICENSE,
thirdpartylegalnotices.txt); they hold e-mail addresses, so they stay out of this repository and the
CI copies them from the Mapbase checkout into the bundle.

CI builds Mapbase + mod/ (see .github/workflows) and Mapsmith ships the result. Everything is
read from the committed tree of `--ref`, never from a working copy, so the same commit always
gives the same files. `--check` exports into a temporary folder and fails if mod/ as committed (staged) differs.

Usage: python3 tools/export_mod_addon.py --repo ../mymod [--ref master] [--check]
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAPBASE_URL = "https://github.com/mapbase-source/source-sdk-2013"
# The Mapbase commit the mod's add-on was started on (develop, 2026-05-17). Change it (and
# re-export) when the mod takes a newer Mapbase.
DEFAULT_BASE = "d9fd33535849b70f647895077b6b011b229c373c"
# Parts of fidelity/ that are only for working on the add-on, not for building or installing it.
FIDELITY_SKIP = ("fidelity/tests/", "fidelity/requirements/", "fidelity/pyproject.toml")


def git(repo: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True).stdout


def changed_files(repo: Path, base: str, ref: str) -> tuple[list[str], list[str]]:
    """Files under sp/ the add-on adds and the Mapbase files it modifies."""
    added, modified = [], []
    for line in git(repo, "diff", "--name-status", "--no-renames", base, ref, "--", "sp").decode().splitlines():
        status, path = line.split("\t", 1)
        if status == "A":
            added.append(path)
        elif status == "M":
            modified.append(path)
        else:
            raise SystemExit(f"unexpected change {status} {path}: the add-on only adds or modifies files")
    return sorted(added), sorted(modified)


def export(repo: Path, ref: str, base: str, out: Path) -> None:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    commit = git(repo, "rev-parse", ref).decode().strip()
    (out / "MAPBASE").write_text(f"repository {MAPBASE_URL}\nbase {base}\nexported {commit}\n", encoding="utf-8", newline="\n")
    added, modified = changed_files(repo, base, ref)
    for path in added:
        target = out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(git(repo, "show", f"{ref}:{path}"))
    patch = git(repo, "diff", "--binary", "--full-index", "--no-renames", base, ref, "--", *modified) if modified else b""
    (out / "mapbase.patch").write_bytes(patch)
    for path in git(repo, "ls-tree", "-r", "--name-only", ref, "--", "fidelity").decode().splitlines():
        if path.startswith(FIDELITY_SKIP) or "__pycache__" in path:
            continue
        target = out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(git(repo, "show", f"{ref}:{path}"))


def committed_differences(fresh: Path, here: Path) -> list[str]:
    """Paths where the export differs from mod/ as git stores it (the index: line endings
    normalized, so a Windows checkout with CRLF scripts still compares equal)."""
    stored = set(git(here, "ls-files", "--", "mod").decode().splitlines())
    exported = {"mod/" + p.relative_to(fresh).as_posix() for p in fresh.rglob("*") if p.is_file()}
    diffs = sorted(stored ^ exported)
    for path in sorted(stored & exported):
        if git(here, "show", f":{path}") != (fresh / path[4:]).read_bytes():
            diffs.append(path)
    return diffs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, required=True, help="the mod's source repository (a Mapbase fork)")
    parser.add_argument("--ref", default="master", help="commit or branch to export (default: master)")
    parser.add_argument("--base", default=DEFAULT_BASE, help="the Mapbase commit the add-on goes on")
    parser.add_argument("--check", action="store_true", help="only check that mod/ is what the export gives")
    args = parser.parse_args()
    target = Path(__file__).resolve().parent.parent / "mod"
    if args.check:
        with tempfile.TemporaryDirectory() as temp:
            fresh = Path(temp) / "mod"
            export(args.repo, args.ref, args.base, fresh)
            diffs = committed_differences(fresh, target.parent)
            if diffs:
                print(f"mod/ is not the export of {args.ref} - these differ:")
                for path in diffs[:40]:
                    print("  " + path)
                return 1
            print("mod/ is up to date")
            return 0
    export(args.repo, args.ref, args.base, target)
    print("exported", args.ref, "into", target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
