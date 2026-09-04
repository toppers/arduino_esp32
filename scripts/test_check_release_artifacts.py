#!/usr/bin/env python3
"""Regression tests for check_release_artifacts.py on a merged index.

The first release had a single-version index, and the checker grew up
expecting every entry's archive to be in the release directory. The first
index made with --merge-into (0.4.0 on top of 0.3.0) failed both of its
release checks for entries that were correct: the 0.3.0 archives live in the
v0.3.0 release, not the 0.4.0 one. These tests fix what the checker must do
with entries kept from an earlier release - probe their URL, not the disk -
and what still counts as a failure.

    python scripts/test_check_release_artifacts.py
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_release_artifacts as checker  # noqa: E402

DECLARED = "0.4.0"
KEPT = "0.3.0"
HOST = "x86_64-mingw32"
BASE = "https://example.invalid/releases/download"


def _zip_with(path: Path, name: str, text: str) -> tuple[str, int]:
    with zipfile.ZipFile(path, "w") as handle:
        handle.writestr(name, text)
    data = path.read_bytes()
    return hashlib.sha256(data).hexdigest(), len(data)


def _entry(version: str, archive: str, digest: str, size: int,
           tag: str) -> dict:
    return {
        "version": version,
        "archiveFileName": archive,
        "checksum": f"SHA-256:{digest}",
        "size": str(size),
        "url": f"{BASE}/{tag}/{archive}",
    }


class Release:
    """A release directory and a repository stub for the checker to read."""

    def __init__(self, root: Path, current_version: str = DECLARED) -> None:
        self.release = root / "release"
        self.repository = root / "repo"
        self.release.mkdir()
        (self.repository / "src").mkdir(parents=True)
        (self.repository / "packaging").mkdir()
        (self.repository / "scripts").mkdir()

        (self.repository / "library.properties").write_text(
            f"name=Stub\nversion={DECLARED}\n", encoding="utf-8")
        (self.repository / "src" / "ToppersFMP3_M5CoreS3.cpp").write_text(
            f'const char *version = "{DECLARED}";\n', encoding="utf-8")
        (self.repository / "packaging" / "release-allowlist.json").write_text(
            json.dumps({"releaseArtifacts": {"linkDriver": {
                "toolName": "fmp3-link", "hosts": [HOST]}}}),
            encoding="utf-8")
        #  The checker imports the host-path scan from the repository it is
        #  pointed at, so the stub carries the real one.
        shutil.copy(HERE / "check_host_paths.py",
                    self.repository / "scripts" / "check_host_paths.py")

        platform_name = f"toppers-esp32-{current_version}.zip"
        digest, size = _zip_with(self.release / platform_name, "boards.txt",
                                 "stub.name=Stub\n")
        current = _entry(current_version, platform_name, digest, size,
                         f"v{current_version}")
        current["toolsDependencies"] = [
            {"packager": "toppers", "name": "fmp3-link",
             "version": current_version}]

        kept_name = f"toppers-esp32-{KEPT}.zip"
        kept = _entry(KEPT, kept_name, "0" * 64, 1, f"v{KEPT}")
        kept["toolsDependencies"] = [
            {"packager": "toppers", "name": "fmp3-link", "version": KEPT}]

        driver_name = f"fmp3-link-{HOST}.zip"
        digest, size = _zip_with(self.release / driver_name, "fmp3-link.exe",
                                 "stub")
        driver_system = _entry(current_version, driver_name, digest, size,
                               f"v{current_version}")
        driver_system["host"] = HOST
        kept_system = _entry(KEPT, driver_name, "0" * 64, 1, f"v{KEPT}")
        kept_system["host"] = HOST

        self.kept_urls = {kept["url"], kept_system["url"]}
        index = {"packages": [{
            "name": "toppers",
            "platforms": [kept, current],
            "tools": [
                {"name": "fmp3-link", "version": KEPT,
                 "systems": [kept_system]},
                {"name": "fmp3-link", "version": current_version,
                 "systems": [driver_system]},
            ],
        }]}
        (self.release / checker.INDEX_NAME).write_text(
            json.dumps(index, indent=2), encoding="utf-8")

    def run(self, *extra: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = checker.main([
                "--release-dir", str(self.release),
                "--repository", str(self.repository),
                "--skip-driver-version", *extra])
        return code, out.getvalue()


class MergedIndex(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.saved_probe = checker.PROBE
        self.addCleanup(setattr, checker, "PROBE", self.saved_probe)

    def test_kept_entries_are_probed_at_their_url_not_looked_for_on_disk(self):
        release = Release(self.root)
        probed: list[str] = []

        def probe(url: str) -> str:
            probed.append(url)
            return ""

        checker.PROBE = probe
        code, out = release.run()
        self.assertEqual(code, 0, out)
        self.assertEqual(set(probed), release.kept_urls)
        self.assertNotIn("not present", out)

    def test_kept_entry_whose_url_does_not_answer_fails(self):
        release = Release(self.root)
        checker.PROBE = lambda url: "HTTP 404"
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("does not answer (HTTP 404)", out)
        self.assertIn(f"platform {KEPT}", out)

    def test_skip_url_probe_leaves_the_network_alone(self):
        release = Release(self.root)

        def probe(url: str) -> str:
            raise AssertionError(f"probed {url} despite --skip-url-probe")

        checker.PROBE = probe
        code, out = release.run("--skip-url-probe")
        self.assertEqual(code, 0, out)
        self.assertIn("URL not probed", out)

    def test_index_without_the_declared_version_fails(self):
        #  The tree says 0.4.0 but the index's newest entry is 0.5.0: nothing
        #  in the directory belongs to this release, and the checker must not
        #  quietly treat 0.5.0 as kept and pass.
        release = Release(self.root, current_version="0.5.0")
        checker.PROBE = lambda url: ""
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn(f"no platform at version '{DECLARED}'", out)


if __name__ == "__main__":
    unittest.main()
