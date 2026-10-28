#!/usr/bin/env python3
"""Adds the build lines VPC leaves out of a Visual Studio solution.

VPC (Valve's project generator, used by Mapbase's create*projects.bat) writes a .sln with the
projects only, without the "Global" part that says which projects build in Release|Win32. Visual
Studio adds it when it opens the solution; MSBuild does not - it builds nothing and reports
success. This adds the part the way Visual Studio writes it, only when a C++ project of the
solution has no Release build line (a complete solution stays as it is). The same as
Repair-Solution in the mod's update.ps1.

Usage: python3 tools/repair_vpc_solution.py games.sln [maptools.sln ...]
Prints the platform name to use with MSBuild (Win32 or x86) per solution.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

CPP_PROJECT = re.compile(r'Project\("\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942\}"\)\s*=\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"(\{[0-9A-Fa-f-]+\})"')


def platform(text: str) -> str:
    match = re.search(r"Release\|([A-Za-z0-9]+)\s*=", text)
    return match.group(1) if match else "Win32"


def repair(path: Path) -> bool:
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw[3:].decode("utf-8") if bom else raw.decode("utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    guids = list(dict.fromkeys(g.upper() for g in CPP_PROJECT.findall(text)))
    if not guids:
        return False
    plat = platform(text)
    missing = [g for g in guids if not re.search(re.escape(g) + r"\.Release\|" + re.escape(plat) + r"\.Build\.0\s*=", text)]
    if not missing:
        return False
    lines = ["Global", "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution", "\t\tDebug|Win32 = Debug|Win32",
             "\t\tRelease|Win32 = Release|Win32", "\tEndGlobalSection", "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution"]
    for guid in guids:
        for config in ("Debug", "Release"):
            lines.append(f"\t\t{guid}.{config}|Win32.ActiveCfg = {config}|Win32")
            lines.append(f"\t\t{guid}.{config}|Win32.Build.0 = {config}|Win32")
    lines += ["\tEndGlobalSection", "\tGlobalSection(SolutionProperties) = preSolution", "\t\tHideSolutionNode = FALSE", "\tEndGlobalSection",
              "EndGlobal"]
    # An incomplete Global part (an older solution, a project added since) is replaced as a whole.
    body = re.sub(r"(?ms)^Global\s*$.*?^EndGlobal\s*$", "", text).rstrip()
    out = body + newline + newline.join(lines) + newline
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + out.encode("utf-8"))
    return True


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for name in sys.argv[1:]:
        path = Path(name)
        fixed = repair(path)
        print(f"{path.name}: {'build lines added' if fixed else 'complete'}; platform {platform(path.read_text(encoding='utf-8-sig'))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
