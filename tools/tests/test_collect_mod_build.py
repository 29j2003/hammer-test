"""Tests for tools/collect_mod_build.py: what goes into the feature bundle.

Run: python -I -m unittest discover -s tools/tests
"""

from __future__ import annotations

import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("collect_mod_build", TOOLS / "collect_mod_build.py")
collect_mod_build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(collect_mod_build)


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


class CollectTest(unittest.TestCase):
    def make_tree(self, root: Path, shader_dx9: bool = True) -> tuple[Path, Path]:
        """A Mapbase checkout after the CI build and a mod/ export, with stand-in files."""
        source = root / "mapbase"
        game = source / "sp" / "game"
        for branch in ("episodic", "hl2"):
            write(game / f"mod_{branch}" / "bin" / "client.dll", f"client {branch}")
            write(game / f"mod_{branch}" / "bin" / "server.dll", f"server {branch}")
            if shader_dx9:
                # VPC's publish step may be missing; the linker's Release folder then holds it.
                write(source / "sp" / "src" / "materialsystem" / "stdshaders" / f"Release_dx9_mod_{branch}" / "game_shader_dx9.dll",
                      f"mapbase shaders {branch}")
        write(game / "mod_episodic" / "bin" / "game_shader_dx6.dll", "fidelity shaders")
        write(game / "bin" / "vrad.exe", "vrad launcher")
        write(game / "bin" / "vrad_dll.dll", "vrad")
        write(source / "LICENSE", "Source 1 SDK license")
        write(source / "thirdpartylegalnotices.txt", "notices")
        mod = root / "mod"
        write(mod / "MAPBASE", "base d9fd33535849\nexported 52465da4abcdef\n")
        write(mod / "fidelity" / "VERSION", "0.1.0\n")
        write(mod / "fidelity" / "LICENSE", "add-on license")
        write(mod / "fidelity" / "deploy.ps1", "# deploy")
        write(mod / "fidelity" / "update.ps1", "# builds from source - left out")
        write(mod / "fidelity" / "game" / "cfg" / "fidelity.cfg", "r_fidelity_ssao 1")
        return source, mod

    def test_bundle_has_mapbase_shaders_for_both_branches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, mod = self.make_tree(root)
            listed = dict((path, sha) for sha, path in collect_mod_build.collect(source, mod, root / "features"))
            for branch in ("episodic", "hl2"):
                for name in ("client", "server", "game_shader_dx9"):
                    self.assertIn(f"bin/{branch}/{name}.dll", listed)
            self.assertEqual((root / "features" / "bin" / "hl2" / "game_shader_dx9.dll").read_text(), "mapbase shaders hl2")
            self.assertIn("bin/game_shader_dx6.dll", listed)
            self.assertIn("fidelity/game/cfg/fidelity.cfg", listed)
            self.assertNotIn("fidelity/update.ps1", listed)
            manifest = (root / "features" / "manifest.txt").read_text()
            self.assertTrue(manifest.startswith("mapsmith-features 1\nversion 0.1.0+52465da\n"))
            data = (root / "features" / "bin" / "episodic" / "game_shader_dx9.dll").read_bytes()
            self.assertIn(f"{hashlib.sha256(data).hexdigest()}  bin/episodic/game_shader_dx9.dll", manifest)

    def test_missing_mapbase_shaders_stop_the_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, mod = self.make_tree(root, shader_dx9=False)
            with self.assertRaises(SystemExit) as stop:
                collect_mod_build.collect(source, mod, root / "features")
            self.assertIn("game_shader_dx9.dll", str(stop.exception))


if __name__ == "__main__":
    unittest.main()
