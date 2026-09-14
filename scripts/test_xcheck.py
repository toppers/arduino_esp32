#!/usr/bin/env python3
"""Self-test for xcheck_compare.py, on fabricated stages.

    python scripts/test_xcheck.py

A comparison that cannot fail is not a comparison, so every case here is
paired: the thing that must be reported, and the thing that must not be.
The stages are tiny fakes laid out like the real ones (link-manifest.json,
objects.rsp, objs/*.o, lib/*.a, ld/*.ld) so that the test does not need a
toolchain, and so that each case changes exactly one thing.

Cases (AC-0e of the stage 0 plan):
  1. identical trees                      -> MATCH, exit 0
  2. one objs/*.o differs                 -> DIFF naming that object, exit 1
  3. link-manifest.json differs           -> DIFF naming the manifest and the
                                             key; a difference confined to a
                                             time-stamp key is NOT reported
  4. lib/*.a differs                      -> DIFF naming the archive
  5. a file missing on one side           -> DIFF naming it and the side
  6. only banner.o differs                -> MATCH by default, DIFF with
                                             --strict
plus two guards: objects.rsp differing is a DIFF, and an empty comparison
(no stages) exits 1 rather than passing on nothing.
"""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xcheck_compare  # noqa: E402
from check_host_paths import contexts as host_path_hits  # noqa: E402

MANIFEST = {
    "schema": 1, "profile": "minimal", "chip": "esp32s3",
    "paddrMode": "runtime-mmu", "objectCount": 3,
    "objectOrder": ["alarm.o", "banner.o", "task.o"],
}


def make_stage(root: Path, chip: str, profile: str,
               manifest: dict | None = None) -> Path:
    stage = root / chip / profile
    (stage / "objs").mkdir(parents=True)
    (stage / "lib").mkdir()
    (stage / "ld").mkdir()
    data = dict(MANIFEST if manifest is None else manifest)
    data["profile"] = profile
    data["chip"] = chip
    (stage / "link-manifest.json").write_text(
        json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (stage / "objects.rsp").write_text(
        "objs/alarm.o\nobjs/banner.o\nobjs/task.o\n", encoding="utf-8")
    (stage / "objs" / "alarm.o").write_bytes(b"\x7fELF alarm " + b"\x01" * 64)
    (stage / "objs" / "banner.o").write_bytes(b"\x7fELF banner 12:00:00")
    (stage / "objs" / "task.o").write_bytes(b"\x7fELF task " + b"\x02" * 64)
    (stage / "lib" / "libsupplicant.a").write_bytes(b"!<arch>\n" + b"s" * 32)
    (stage / "ld" / f"{chip}_xip.ld").write_text("MEMORY {}\n", encoding="utf-8")
    return stage


def make_pair(work: Path, stages=(("esp32s3", "minimal"),
                                  ("esp32", "minimal"))) -> tuple[Path, Path]:
    baseline = work / "baseline"
    current = work / "current"
    for chip, profile in stages:
        make_stage(baseline, chip, profile)
        make_stage(current, chip, profile)
    return baseline, current


def run(baseline: Path, current: Path, *extra: str) -> tuple[int, str]:
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = xcheck_compare.main(["--baseline", str(baseline),
                                  "--current", str(current), *extra])
    return rc, out.getvalue()


def expect(failures: list[str], case: str, condition: bool, detail: str) -> None:
    if not condition:
        failures.append(f"{case}: {detail}")


def main() -> int:
    failures: list[str] = []
    work = Path(tempfile.mkdtemp(prefix="xcheck-test-"))
    try:
        #  1. identical
        base, cur = make_pair(work / "c1")
        rc, out = run(base, cur)
        expect(failures, "1 identical", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "1 identical", "compared=2 match=2 diff=0" in out, out)
        expect(failures, "1 identical",
               "esp32s3/minimal: MATCH (7 files, 1 skipped)" in out, out)

        #  2. one object differs: named, once, with the stage
        base, cur = make_pair(work / "c2")
        (cur / "esp32s3" / "minimal" / "objs" / "task.o").write_bytes(
            b"\x7fELF task " + b"\x03" * 64)
        rc, out = run(base, cur)
        expect(failures, "2 object", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "2 object", "esp32s3/minimal: DIFF (1 files)" in out, out)
        expect(failures, "2 object",
               out.count("objs/task.o: sha256 differs") == 1, out)
        expect(failures, "2 object", "esp32/minimal: MATCH" in out, out)
        expect(failures, "2 object", "compared=2 match=1 diff=1" in out, out)

        #  3a. manifest differs in a real key
        base, cur = make_pair(work / "c3a")
        changed = dict(MANIFEST)
        changed["objectOrder"] = ["alarm.o", "task.o", "banner.o"]
        shutil.rmtree(cur / "esp32s3" / "minimal")
        make_stage(cur, "esp32s3", "minimal", changed)
        rc, out = run(base, cur)
        expect(failures, "3a manifest", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "3a manifest",
               "link-manifest.json: JSON differs in key(s): objectOrder" in out,
               out)
        #  3b. manifest differs only in a time-stamp key: not a difference
        base, cur = make_pair(work / "c3b")
        stamped = dict(MANIFEST)
        stamped["generatedAt"] = "2026-09-15T00:00:00Z"
        shutil.rmtree(cur / "esp32s3" / "minimal")
        make_stage(cur, "esp32s3", "minimal", stamped)
        rc, out = run(base, cur)
        expect(failures, "3b manifest time key", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "3b manifest time key",
               "compared=2 match=2 diff=0" in out, out)

        #  4. lib/*.a differs
        base, cur = make_pair(work / "c4")
        (cur / "esp32" / "minimal" / "lib" / "libsupplicant.a").write_bytes(
            b"!<arch>\n" + b"t" * 32)
        rc, out = run(base, cur)
        expect(failures, "4 lib", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "4 lib", "esp32/minimal: DIFF (1 files)" in out, out)
        expect(failures, "4 lib", "lib/libsupplicant.a: bytes differ" in out, out)

        #  5. a file missing on one side, and a whole stage missing
        base, cur = make_pair(work / "c5")
        (cur / "esp32s3" / "minimal" / "objs" / "alarm.o").unlink()
        (base / "esp32" / "minimal" / "ld" / "extra.ld").write_text(
            "/* baseline only */\n", encoding="utf-8")
        rc, out = run(base, cur)
        expect(failures, "5 missing", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5 missing", "objs/alarm.o: only in baseline" in out, out)
        expect(failures, "5 missing", "ld/extra.ld: only in baseline" in out, out)
        expect(failures, "5 missing", "compared=2 match=0 diff=2" in out, out)
        shutil.rmtree(cur / "esp32")
        rc, out = run(base, cur)
        expect(failures, "5 missing stage", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5 missing stage",
               "esp32/minimal: DIFF (1 files)" in out
               and "(stage): only in baseline" in out, out)

        #  6. only banner.o differs: excluded by default, caught by --strict
        base, cur = make_pair(work / "c6")
        (cur / "esp32s3" / "minimal" / "objs" / "banner.o").write_bytes(
            b"\x7fELF banner 12:00:01")
        rc, out = run(base, cur)
        expect(failures, "6 banner default", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "6 banner default",
               "compared=2 match=2 diff=0" in out, out)
        rc, out = run(base, cur, "--strict")
        expect(failures, "6 banner strict", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "6 banner strict",
               out.count("objs/banner.o: sha256 differs") == 1, out)
        expect(failures, "6 banner strict", "compared=2 match=1 diff=1" in out,
               out)

        #  guard: objects.rsp differs
        base, cur = make_pair(work / "g1")
        (cur / "esp32s3" / "minimal" / "objects.rsp").write_text(
            "objs/alarm.o\nobjs/task.o\nobjs/banner.o\n", encoding="utf-8")
        rc, out = run(base, cur)
        expect(failures, "guard rsp", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "guard rsp", "objects.rsp: bytes differ" in out, out)

        #  guard: nothing to compare is a failure, not a pass
        (work / "g2" / "baseline").mkdir(parents=True)
        (work / "g2" / "current").mkdir(parents=True)
        rc, out = run(work / "g2" / "baseline", work / "g2" / "current")
        expect(failures, "guard empty", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "guard empty", "compared=0 match=0 diff=0" in out, out)
        rc, out = run(work / "g2" / "nowhere", work / "g2" / "current")
        expect(failures, "guard absent dir", rc == 1, f"rc={rc}\n{out}")

        #  the report must not carry the machine's directory: every path in
        #  it is relative to a stage, and the header names the roots relative
        #  to the working directory. Judged by the release detector itself,
        #  on a pair that lives outside the working directory (a temp dir),
        #  which is the case where a naive relpath still spells the leak.
        base, cur = make_pair(work / "g3")
        (cur / "esp32s3" / "minimal" / "objs" / "task.o").write_bytes(b"x")
        rc, out = run(base, cur)
        hits = host_path_hits(out.encode("utf-8"))
        expect(failures, "guard host path", not hits,
               f"build-machine path in the report: {hits}\n{out}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("cases: identical, object, manifest (key / time key), lib, "
          "missing file, missing stage,\n       banner (default / --strict), "
          "objects.rsp, empty, absent dir, host path")
    if failures:
        print(f"\nFAILED, {len(failures)} check(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("\nPASSED: every case behaves as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
