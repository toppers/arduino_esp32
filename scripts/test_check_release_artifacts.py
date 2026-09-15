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


def _zip_with(path: Path, name: str, text: str,
              more: dict[str, str] | None = None) -> tuple[str, int]:
    with zipfile.ZipFile(path, "w") as handle:
        handle.writestr(name, text)
        for extra_name, extra_text in (more or {}).items():
            handle.writestr(extra_name, extra_text)
    data = path.read_bytes()
    return hashlib.sha256(data).hexdigest(), len(data)


#  The stage and tool tables of the real allowlist, in the shape the stub
#  repository gets when a test asks for them. Kept literal here so that the
#  cases below read as "this chip, these profiles"; the last test holds the
#  literal against packaging/release-allowlist.json and the generator.
STAGES = {
    "esp32s3": ["minimal", "m5-unified", "wifi-connect"],
    "esp32": ["minimal", "m5-unified", "wifi-connect", "bt-classic"],
    "esp32c6": ["minimal", "wifi-connect"],
}
CHIP_TOOLS = {
    "esp32c6": [
        {"packager": "m5stack", "name": "esp-rv32", "version": "2601"},
        {"packager": "m5stack", "name": "esp32c6-libs", "version": "3.3.8"},
    ],
}
XTENSA_BOARDS = {"m5cores3_fmp3": "esp32s3", "m5sticks3_fmp3": "esp32s3",
                 "m5core_fmp3": "esp32"}
C6_BOARD = {"m5nanoc6_fmp3": "esp32c6"}


def _platform_files(root: str, stages: dict[str, list[str]],
                    boards: dict[str, str]) -> dict[str, str]:
    """A platform archive's members: boards.txt with one board per entry
    and a link-manifest.json per stage, laid out as make_package_index.py
    lays them out (<root>/fmp3-prebuilt/<chip>/<profile>/...)."""
    files = {}
    for chip, profiles in stages.items():
        for profile in profiles:
            files[f"{root}/fmp3-prebuilt/{chip}/{profile}/link-manifest.json"] = \
                json.dumps({"chip": chip, "profile": profile})
    return files


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

    def __init__(self, root: Path, current_version: str = DECLARED,
                 stages: dict[str, list[str]] | None = None,
                 boards: dict[str, str] | None = None,
                 chip_tools: list[dict] | None = None,
                 tables: bool = False) -> None:
        """stages / boards / chip_tools shape the platform archive and the
        index entry; tables=True gives the stub allowlist the prebuiltStages
        and chipToolDependencies tables. All default to the bare stub the
        merged-index cases were written against."""
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
        platform_section: dict = {"kind": "platform"}
        if tables:
            platform_section["prebuiltStages"] = STAGES
            platform_section["chipToolDependencies"] = CHIP_TOOLS
        (self.repository / "packaging" / "release-allowlist.json").write_text(
            json.dumps({"releaseArtifacts": {
                "linkDriver": {"toolName": "fmp3-link", "hosts": [HOST]},
                "platformArchive": platform_section}}),
            encoding="utf-8")
        #  The checker imports the host-path scan from the repository it is
        #  pointed at, so the stub carries the real one.
        shutil.copy(HERE / "check_host_paths.py",
                    self.repository / "scripts" / "check_host_paths.py")

        platform_name = f"toppers-esp32-{current_version}.zip"
        root_name = f"toppers-esp32-{current_version}"
        boards_txt = "stub.name=Stub\n" + "".join(
            f"{board}.name={board}\n{board}.build.toppers_chip={chip}\n"
            for board, chip in (boards or {}).items())
        digest, size = _zip_with(
            self.release / platform_name, f"{root_name}/boards.txt",
            boards_txt, _platform_files(root_name, stages or {}, boards or {}))
        current = _entry(current_version, platform_name, digest, size,
                         f"v{current_version}")
        current["toolsDependencies"] = [
            {"packager": "toppers", "name": "fmp3-link",
             "version": current_version}] + list(chip_tools or [])

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


class PlatformContents(unittest.TestCase):
    """The stages and the chip tools, against the allowlist tables.

    The C6 board (stage 5) brought the case these exist for: its stages
    link only with the RISC-V toolchain and the ESP32-C6 SDK, which
    make_package_index.py declares only when the C6 stages are packaged.
    A release that ships the stages without the tools installs and then
    fails at the first include - on a clean machine, never the developer's.
    """

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.saved_probe = checker.PROBE
        self.addCleanup(setattr, checker, "PROBE", self.saved_probe)
        checker.PROBE = lambda url: ""

    def test_four_boards_with_c6_tools_pass(self):
        release = Release(self.root, stages=STAGES,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 0, out)
        self.assertIn("ok   stages esp32c6", out)
        self.assertIn("ok   stages esp32s3", out)
        self.assertIn("ok   stages esp32", out)
        self.assertNotIn("note no ", out)

    def test_c6_stages_without_the_tools_fail(self):
        #  The negative that matters: the same release, tools not declared.
        release = Release(self.root, stages=STAGES,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=[], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("does not declare tool m5stack:esp-rv32@2601", out)
        self.assertIn("does not declare tool m5stack:esp32c6-libs@3.3.8", out)
        #  One of the two declared is still a failure naming the other.
        (self.root / "half").mkdir()
        release = Release(self.root / "half", stages=STAGES,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=CHIP_TOOLS["esp32c6"][:1], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertNotIn("esp-rv32@2601, which", out)
        self.assertIn("does not declare tool m5stack:esp32c6-libs@3.3.8", out)

    def test_xtensa_only_release_needs_no_c6_tools(self):
        #  Without C6 stages the C6 tools are not required, and the report
        #  says the chip is absent rather than silently passing over it.
        xtensa = {chip: STAGES[chip] for chip in ("esp32s3", "esp32")}
        release = Release(self.root, stages=xtensa, boards=XTENSA_BOARDS,
                          chip_tools=[], tables=True)
        code, out = release.run()
        self.assertEqual(code, 0, out)
        self.assertIn("note no esp32c6 stages in this release", out)
        self.assertNotIn("esp-rv32", out)

    def test_c6_stage_missing_or_stray_fails(self):
        #  wifi-connect absent: the board offers a runtime that cannot link.
        stages = dict(STAGES)
        stages["esp32c6"] = ["minimal"]
        release = Release(self.root, stages=stages,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("esp32c6 ships without the wifi-connect stage(s)", out)
        #  A stage no C6 board offers (m5-unified: the M5NanoC6 has no
        #  display) was packaged anyway.
        stages["esp32c6"] = ["minimal", "wifi-connect", "m5-unified"]
        (self.root / "stray").mkdir()
        release = Release(self.root / "stray", stages=stages,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("esp32c6 ships stage(s) no board offers: m5-unified",
                      out)

    def test_board_and_stages_must_agree(self):
        #  A C6 board line without C6 stages, and C6 stages without a board.
        xtensa = {chip: STAGES[chip] for chip in ("esp32s3", "esp32")}
        release = Release(self.root, stages=xtensa,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("board m5nanoc6_fmp3 names build.toppers_chip=esp32c6, "
                      "and the archive holds no esp32c6 stage", out)
        (self.root / "noboard").mkdir()
        release = Release(self.root / "noboard", stages=STAGES,
                          boards=XTENSA_BOARDS,
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("esp32c6 stages are packaged but no board in boards.txt "
                      "has build.toppers_chip=esp32c6", out)

    def test_unlisted_chip_fails(self):
        stages = dict(STAGES)
        stages["esp32h2"] = ["minimal"]
        release = Release(self.root, stages=stages,
                          boards={**XTENSA_BOARDS, **C6_BOARD,
                                  "h2_fmp3": "esp32h2"},
                          chip_tools=CHIP_TOOLS["esp32c6"], tables=True)
        code, out = release.run()
        self.assertEqual(code, 1, out)
        self.assertIn("stages for esp32h2 (minimal), a chip", out)

    def test_without_tables_the_check_says_it_did_not_run(self):
        release = Release(self.root, stages=STAGES,
                          boards={**XTENSA_BOARDS, **C6_BOARD},
                          chip_tools=[], tables=False)
        code, out = release.run()
        self.assertEqual(code, 0, out)
        self.assertIn("not checked (the allowlist has no prebuiltStages", out)

    def test_tables_match_the_real_allowlist_and_the_generator(self):
        """The literal tables above, packaging/release-allowlist.json and
        the tables the other scripts build from must be one table: a chip
        or a profile added in one place and not the others is exactly the
        drift the release check exists to catch."""
        real = json.loads(
            (HERE.parent / "packaging" / "release-allowlist.json")
            .read_text(encoding="utf-8"))["releaseArtifacts"]["platformArchive"]
        self.assertEqual(real["prebuiltStages"], STAGES)
        self.assertEqual(real["chipToolDependencies"], CHIP_TOOLS)
        import make_package_index
        self.assertEqual(
            {chip: [{"packager": p, "name": n, "version": v}
                    for p, n, v in rows]
             for chip, rows in make_package_index.CHIP_TOOL_DEPENDENCIES.items()},
            CHIP_TOOLS)
        #  install_platform.py: what every board of the chip must have
        #  (EXPECTED_PROFILES) plus the chip-only menu entries.
        import install_platform
        self.assertEqual(
            {chip: set(profiles) for chip, profiles in STAGES.items()},
            {chip: set(profiles) | {
                profile for _, _, profile
                in install_platform.CHIP_ONLY_ENTRIES.get(chip, [])}
             for chip, profiles in install_platform.EXPECTED_PROFILES.items()})
        import xcheck_compare
        for chip, profiles in STAGES.items():
            self.assertEqual(xcheck_compare.profiles_for(chip), profiles,
                             chip)


if __name__ == "__main__":
    unittest.main()
