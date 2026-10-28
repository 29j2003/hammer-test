#!/usr/bin/env python3
"""Packs the built mod add-on into Mapsmith's feature bundle (features/).

After CI built Mapbase with the add-on from mod/ (see .github/workflows/mod.yml), this collects
what Mapsmith installs into a mod (sme/mod_install.h):

  bin/episodic/client.dll, server.dll   for mods on Half-Life 2: Episode Two
  bin/hl2/client.dll, server.dll        for mods on Half-Life 2
  bin/game_shader_dx6.dll               the add-on's shaders (Fidelity_Parallax, Fidelity_Liquid ...)
  vrad/vrad.exe, vrad_dll.dll           VRAD with baked ambient occlusion (-fidelityao)
  fidelity/game/...                     the add-on's game files (compiled shaders, cfg)
  fidelity/*.ps1, *.cs                  deploy.ps1 (generated materials), report.ps1 (Make report)
  LICENSE_SourceSDK2013.txt, thirdpartylegalnotices.txt
                                        the Source 1 SDK license the DLLs are built under, and its notices
  manifest.txt                          version and the SHA-256 of every file

Where a build puts a file differs (VPC's publish folder sp/game/..., or the linker's Release
folder next to the project); the newest one wins, as in the mod's update.ps1.

Usage: python3 tools/collect_mod_build.py --source <mapbase checkout> --mod mod --out features
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

GAMES = ("episodic", "hl2")
# Scripts the bundle needs at the user's: deploy.ps1 and what it loads, report.ps1, the sound tool.
# update.ps1 builds from source and stays out.
SCRIPT_SUFFIXES = (".ps1", ".cs")
SCRIPTS_LEFT_OUT = ("update.ps1",)


def newest(candidates: list[Path]) -> Path | None:
    found = [p for p in candidates if p.is_file()]
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def release_folders(project: Path, game: str | None = None) -> list[Path]:
    if not project.is_dir():
        return []
    out = []
    for child in project.iterdir():
        name = child.name.lower()
        if not child.is_dir() or not name.startswith("release"):
            continue
        if game and name not in (f"release_mod_{game}", f"release_{game}", f"releasemod_{game}", f"release{game}"):
            continue
        out.append(child)
    return out


def find_built(source: Path) -> dict[str, Path]:
    src = source / "sp" / "src"
    game_dir = source / "sp" / "game"
    files: dict[str, Path] = {}
    for game in GAMES:
        for name in ("client", "server"):
            built = newest([game_dir / f"mod_{game}" / "bin" / f"{name}.dll"] +
                           [f / f"{name}.dll" for f in release_folders(src / "game" / name, game)])
            if not built:
                raise SystemExit(f"{name}.dll for {game} was not built (looked in sp/game/mod_{game}/bin and sp/src/game/{name}/Release_mod_{game})")
            files[f"bin/{game}/{name}.dll"] = built
    # (OUTBINDIR is sp/game/$GAMENAME/bin: mod_episodic or mod_hl2 - both the same shaders)
    shader = newest(list(game_dir.rglob("game_shader_dx6.dll")) + list((src / "materialsystem" / "fidelity").rglob("game_shader_dx6.dll")))
    if not shader:
        raise SystemExit("game_shader_dx6.dll was not built (project Shaders (Fidelity) in games.sln)")
    files["bin/game_shader_dx6.dll"] = shader
    for name, project in (("vrad_dll.dll", "vrad"), ("vrad.exe", "vrad_launcher")):
        built = newest([game_dir / "bin" / name] + [f / name for f in release_folders(src / "utils" / project)])
        if not built:
            raise SystemExit(f"{name} was not built (maptools.sln, project {project})")
        files[f"vrad/{name}"] = built
    return files


def version_of(mod: Path) -> str:
    version = (mod / "fidelity" / "VERSION").read_text(encoding="utf-8").strip()
    exported = ""
    for line in (mod / "MAPBASE").read_text(encoding="utf-8").splitlines():
        if line.startswith("exported "):
            exported = line.split()[1][:7]
    return f"{version}+{exported}" if exported else version


def collect(source: Path, mod: Path, out: Path) -> list[tuple[str, str]]:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = dict(find_built(source))
    fidelity = mod / "fidelity"
    for path in sorted((fidelity / "game").rglob("*")):
        if path.is_file():
            files["fidelity/" + path.relative_to(fidelity).as_posix()] = path
    for path in sorted(fidelity.iterdir()):
        if path.is_file() and path.suffix in SCRIPT_SUFFIXES and path.name not in SCRIPTS_LEFT_OUT:
            files[f"fidelity/{path.name}"] = path
    for name in ("VERSION", "LICENSE"):
        files[f"fidelity/{name}"] = fidelity / name
    # The Source 1 SDK license and notices, from the Mapbase checkout (not kept in mod/).
    for source_name, name in (("LICENSE", "LICENSE_SourceSDK2013.txt"), ("thirdpartylegalnotices.txt", "thirdpartylegalnotices.txt")):
        if not (source / source_name).is_file():
            raise SystemExit(f"{source_name} is missing in the Mapbase checkout")
        files[name] = source / source_name
    listed = []
    for target, path in sorted(files.items()):
        destination = out / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        listed.append((hashlib.sha256(destination.read_bytes()).hexdigest(), target))
    manifest = "mapsmith-features 1\nversion " + version_of(mod) + "\n" + "".join(f"{h}  {p}\n" for h, p in listed)
    (out / "manifest.txt").write_text(manifest, encoding="utf-8", newline="\n")
    return listed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, required=True, help="the Mapbase checkout the add-on was built in")
    parser.add_argument("--mod", type=Path, default=Path(__file__).resolve().parent.parent / "mod")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    listed = collect(args.source, args.mod, args.out)
    print(f"feature bundle {version_of(args.mod)}: {len(listed)} files in {args.out}")
    for sha, path in listed:
        print(f"  {sha[:12]}  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
