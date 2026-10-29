"""Tests for tools/export_mod_addon.py: the export writes mod/NOTICE.txt with the licenses.

Run: python -I -m unittest discover -s tools/tests
"""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("export_mod_addon", TOOLS / "export_mod_addon.py")
export_mod_addon = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(export_mod_addon)


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()


class ExportTest(unittest.TestCase):
    def test_notice_names_the_sdk_license_and_the_public_domain_parts(self):
        notice = export_mod_addon.NOTICE
        self.assertIn("Source 1 SDK License", notice)
        self.assertIn("mapbase.patch", notice)
        self.assertIn("fidelity/LICENSE", notice)
        self.assertNotIn("@", notice)

    def test_export_writes_the_notice_next_to_the_add_on(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "mymod"
            (repo / "sp" / "src").mkdir(parents=True)
            (repo / "sp" / "src" / "base.cpp").write_text("base\n", encoding="utf-8")
            git(repo.parent, "init", "-q", str(repo))
            for key, value in (("user.name", "Test"), ("user.email", "test@users.noreply.github.com"), ("commit.gpgsign", "false")):
                git(repo, "config", key, value)
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "base")
            base = git(repo, "rev-parse", "HEAD")
            (repo / "sp" / "src" / "fidelity.cpp").write_text("add-on\n", encoding="utf-8")
            (repo / "fidelity").mkdir()
            (repo / "fidelity" / "LICENSE").write_text("public domain\n", encoding="utf-8")
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "add-on")
            out = Path(temp) / "mod"
            export_mod_addon.export(repo, "HEAD", base, out)
            self.assertEqual((out / "NOTICE.txt").read_text(encoding="utf-8"), export_mod_addon.NOTICE)
            self.assertTrue((out / "sp" / "src" / "fidelity.cpp").is_file())
            self.assertTrue((out / "fidelity" / "LICENSE").is_file())


if __name__ == "__main__":
    unittest.main()
