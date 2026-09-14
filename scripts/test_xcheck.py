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
                                             key; reformatted / reordered but
                                             JSON-equal is ALSO a DIFF; a
                                             difference confined to a
                                             time-stamp key is a MATCH with
                                             a note
  4. lib/*.a differs                      -> DIFF naming the archive
  5. a file or stage on one side only     -> DIFF naming it and the side
                                             (both directions)
  6. only objs/banner.o differs           -> MATCH by default, DIFF with
                                             --strict; a banner.o elsewhere
                                             (lib/banner.o) is never skipped
plus guards: objects.rsp differing is a DIFF; an empty comparison (no
stages) exits 1 rather than passing on nothing; an expected stage absent
from both sides (per BASELINE.json, or per the profile tables when there is
no record) exits 1; the report carries no build-machine path.
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


PAIR_STAGES = (("esp32s3", "minimal"), ("esp32", "minimal"))


def write_record(baseline: Path, stages, head="0123456789abcdef" * 2 + "01234567",
                 dirty=False) -> None:
    """The provenance record xcheck_baseline.py writes; the comparer takes
    the expected stage list from it."""
    baseline.mkdir(parents=True, exist_ok=True)
    (baseline / xcheck_compare.BASELINE_RECORD).write_text(json.dumps({
        "schema": 1, "head": head, "dirty": dirty, "dirtyCount": 0,
        "takenAt": "2026-09-15T00:00:00Z",
        "stages": [f"{chip}/{profile}" for chip, profile in stages],
    }, indent=2) + "\n", encoding="utf-8")


def make_pair(work: Path, stages=PAIR_STAGES, record=True) -> tuple[Path, Path]:
    baseline = work / "baseline"
    current = work / "current"
    for chip, profile in stages:
        make_stage(baseline, chip, profile)
        make_stage(current, chip, profile)
    if record:
        write_record(baseline, stages)
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
        expect(failures, "1 identical",
               "expected=2 compared=2 match=2 diff=0" in out, out)
        expect(failures, "1 identical",
               "esp32s3/minimal: MATCH (7 files, 1 skipped)" in out, out)
        expect(failures, "1 identical",
               "baseline: head=0123456789ab dirty=no" in out, out)

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
        expect(failures, "2 object",
               "expected=2 compared=2 match=1 diff=1" in out, out)

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
        #  3b. manifest differs only in a time-stamp key: MATCH, with a note
        base, cur = make_pair(work / "c3b")
        stamped = dict(MANIFEST)
        stamped["generatedAt"] = "2026-09-15T00:00:00Z"
        shutil.rmtree(cur / "esp32s3" / "minimal")
        make_stage(cur, "esp32s3", "minimal", stamped)
        rc, out = run(base, cur)
        expect(failures, "3b manifest time key", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "3b manifest time key",
               "expected=2 compared=2 match=2 diff=0" in out, out)
        expect(failures, "3b manifest time key",
               "note: link-manifest.json differs only in time-stamp key(s): "
               "generatedAt" in out, out)
        #  3c. manifest reformatted and reordered, JSON-equal: still a DIFF.
        #  The reviewer showed the first version passing this as MATCH.
        base, cur = make_pair(work / "c3c")
        manifest = cur / "esp32s3" / "minimal" / "link-manifest.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        reordered = {k: data[k] for k in reversed(list(data))}
        manifest.write_text(json.dumps(reordered, indent=4) + "\n",
                            encoding="utf-8")
        rc, out = run(base, cur)
        expect(failures, "3c manifest formatting", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "3c manifest formatting",
               "link-manifest.json: bytes differ but the JSON is equal" in out,
               out)
        #  3d. a time-stamp key AND a real key: the real one is not forgiven
        base, cur = make_pair(work / "c3d")
        both = dict(MANIFEST)
        both["generatedAt"] = "2026-09-15T00:00:00Z"
        both["objectCount"] = 4
        shutil.rmtree(cur / "esp32s3" / "minimal")
        make_stage(cur, "esp32s3", "minimal", both)
        rc, out = run(base, cur)
        expect(failures, "3d manifest mixed", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "3d manifest mixed",
               "JSON differs in key(s): generatedAt, objectCount" in out, out)

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
        expect(failures, "5 missing",
               "expected=2 compared=2 match=0 diff=2" in out, out)
        shutil.rmtree(cur / "esp32")
        rc, out = run(base, cur)
        expect(failures, "5 missing stage", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5 missing stage",
               "esp32/minimal: DIFF (1 files)" in out
               and "(stage): only in baseline" in out, out)
        #  5b. the other direction: a file and a stage only in current
        base, cur = make_pair(work / "c5b")
        (cur / "esp32s3" / "minimal" / "objs" / "extra.o").write_bytes(b"\x7fELF")
        make_stage(cur, "esp32", "wifi-connect")
        rc, out = run(base, cur)
        expect(failures, "5b only in current", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5b only in current",
               "objs/extra.o: only in current" in out, out)
        expect(failures, "5b only in current",
               "esp32/wifi-connect: DIFF (1 files)" in out
               and "(stage): only in current" in out, out)
        expect(failures, "5b only in current",
               "expected=2 compared=3 match=1 diff=2" in out, out)
        #  5c. an expected stage (per the record) absent from BOTH sides
        base, cur = make_pair(work / "c5c")
        shutil.rmtree(base / "esp32")
        shutil.rmtree(cur / "esp32")
        rc, out = run(base, cur)
        expect(failures, "5c expected absent", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5c expected absent",
               "esp32/minimal: DIFF (1 files)" in out
               and "(stage): missing on both sides" in out, out)
        expect(failures, "5c expected absent",
               "FAILED: expected stage(s) absent from both sides: esp32/minimal"
               in out, out)
        expect(failures, "5c expected absent",
               "expected=2 compared=1" in out, out)
        #  5d. no record at all: the expectation falls back to the full
        #  profile table (7 stages), so two fabricated stages cannot pass
        base, cur = make_pair(work / "c5d", record=False)
        rc, out = run(base, cur)
        expect(failures, "5d no record", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "5d no record",
               "baseline: no BASELINE.json" in out, out)
        expected_n = len(xcheck_compare.expected_stages_from_tables())
        expect(failures, "5d no record",
               f"expected={expected_n} compared=2" in out, out)
        expect(failures, "5d no record",
               "esp32s3/wifi-connect: DIFF (1 files)" in out
               and "(stage): missing on both sides" in out, out)

        #  6. only banner.o differs: excluded by default, caught by --strict
        base, cur = make_pair(work / "c6")
        (cur / "esp32s3" / "minimal" / "objs" / "banner.o").write_bytes(
            b"\x7fELF banner 12:00:01")
        rc, out = run(base, cur)
        expect(failures, "6 banner default", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "6 banner default",
               "expected=2 compared=2 match=2 diff=0" in out, out)
        rc, out = run(base, cur, "--strict")
        expect(failures, "6 banner strict", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "6 banner strict",
               out.count("objs/banner.o: sha256 differs") == 1, out)
        expect(failures, "6 banner strict",
               "expected=2 compared=2 match=1 diff=1" in out, out)
        #  6b. a banner.o that is NOT objs/banner.o is an ordinary file:
        #  the exclusion is by exact path, not by basename
        base, cur = make_pair(work / "c6b")
        (base / "esp32" / "minimal" / "lib" / "banner.o").write_bytes(b"lib A")
        (cur / "esp32" / "minimal" / "lib" / "banner.o").write_bytes(b"lib B")
        rc, out = run(base, cur)
        expect(failures, "6b nested banner", rc == 1, f"rc={rc}\n{out}")
        expect(failures, "6b nested banner",
               "lib/banner.o: sha256 differs" in out, out)
        expect(failures, "6b nested banner",
               "esp32s3/minimal: MATCH (7 files, 1 skipped)" in out, out)

        #  provenance warnings: a baseline taken at another commit, or on a
        #  dirty tree, is reported but does not fail the comparison. The
        #  HEAD warning needs git to answer for this repository; when it
        #  cannot (no git), the warning is simply not expected.
        base, cur = make_pair(work / "p1")
        write_record(base, PAIR_STAGES,
                     head=xcheck_compare.git_head(xcheck_compare.REPOSITORY)
                     or "0123456789abcdef" * 2 + "01234567")
        rc, out = run(base, cur)
        expect(failures, "provenance same head", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "provenance same head", "warning:" not in out, out)
        write_record(base, PAIR_STAGES, head="f" * 40, dirty=True)
        rc, out = run(base, cur)
        expect(failures, "provenance warnings", rc == 0, f"rc={rc}\n{out}")
        expect(failures, "provenance warnings",
               "baseline: head=ffffffffffff dirty=yes" in out, out)
        expect(failures, "provenance warnings",
               "warning: the baseline was taken on a dirty tree" in out, out)
        if xcheck_compare.git_head(xcheck_compare.REPOSITORY):
            expect(failures, "provenance warnings",
                   "differs from the baseline's ffffffffffff" in out, out)

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
        expect(failures, "guard empty", "compared=0 match=0" in out, out)
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

    print("cases: identical, object, manifest (key / time key / formatting / "
          "mixed), lib,\n       missing file and stage (both directions), "
          "expected stage absent (record / no record),\n       "
          "banner (default / --strict / nested lib/banner.o), objects.rsp, "
          "empty, absent dir,\n       host path")
    if failures:
        print(f"\nFAILED, {len(failures)} check(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("\nPASSED: every case behaves as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
