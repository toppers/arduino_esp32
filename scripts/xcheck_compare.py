#!/usr/bin/env python3
"""X-check: decide whether the prebuilt Xtensa stages changed.

The C6 port has to touch scripts and CMake files that the ESP32-S3 and ESP32
stages are built through. "The Xtensa boards are unaffected" is then a claim
about bytes, and this is what turns it into a measurement: it compares a
baseline set of stages (saved by xcheck_baseline.py before the work) with the
set built afterwards, stage by stage, file by file.

What is compared, per stage (<chip>/<profile>):

  link-manifest.json  byte for byte. When the bytes differ, the two are
                      parsed as JSON and the difference is allowed ONLY if it
                      is confined to time-stamp keys (TIME_KEYS below); the
                      stage then matches, with a note. A manifest that was
                      merely reformatted or reordered is a DIFF: the linker
                      never sees "equivalent JSON", it sees the file, and a
                      comparison that forgave formatting once let a rewritten
                      manifest through as MATCH.
  objects.rsp         byte for byte
  objs/*.o            SHA-256. Exactly objs/banner.o is skipped unless
                      --strict is given: it carries __DATE__/__TIME__ and
                      changes on every rebuild (BUILDING.md), and a check
                      that always fails is no check. --strict exists so that
                      the exclusion itself can be shown to be the only thing
                      hiding a difference. A banner.o anywhere else is
                      compared like any other file.
  lib/*.a             byte for byte
  everything else     byte for byte (the linker scripts under ld/, and
                      anything a future stage adds - an unknown file is
                      compared, never ignored)
  a file, or a whole stage, present on one side only counts as a difference.

The set of stages that MUST be present is fixed in advance - from the
BASELINE.json that xcheck_baseline.py writes, or failing that from the
profile tables in build_prebuilt_stages.py - and a missing expected stage on
either side is a failure. Without that, a baseline taken with one chip and a
rebuild of the other would compare nothing in common and pass.

Only the chips of that expected set are compared. A chip directory that is
not in the baseline at all (build/prebuilt/esp32c6 beside a baseline of the
two Xtensa chips) is reported on one line, "ignored (not in baseline):
<chip>", and does not enter the verdict: the check asks whether the Xtensa
stages moved, and a new chip's stages say nothing about that. A profile
directory that is new under a chip the baseline DOES cover is still a DIFF
(only in current), as is any extra file inside a covered stage.

BASELINE.json also records the commit the baseline was taken at and whether
the tree was dirty; the report prints both and warns (without failing) when
the current HEAD is a different commit, because a baseline taken after the
build scripts were edited proves nothing about the edit.

Output names files by their path relative to the stage, never by where the
stage lives: the report is meant to be pasted into records, and
check_host_paths.py must find nothing in it.

Exit status: 0 when every expected stage is present on both sides and
matched, 1 otherwise - including when nothing was compared at all, so a
misplaced --baseline cannot read as a pass.

    python scripts/xcheck_compare.py
    python scripts/xcheck_compare.py --strict
    python scripts/xcheck_compare.py --baseline build/xcheck-baseline \\
                                     --current build/prebuilt
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_prebuilt_stages  # noqa: E402

#  Defaults are under the repository, not the working directory, so that this
#  and xcheck_baseline.py agree on where the stages are wherever they are run
#  from. An explicit --baseline/--current is relative to the working directory.
REPOSITORY = Path(__file__).resolve().parent.parent
DEFAULT_BASELINE = REPOSITORY / "build" / "xcheck-baseline"
DEFAULT_CURRENT = REPOSITORY / "build" / "prebuilt"

MANIFEST_NAME = "link-manifest.json"
RESPONSE_NAME = "objects.rsp"
#  Provenance record written next to the stages by xcheck_baseline.py.
BASELINE_RECORD = "BASELINE.json"
#  The one object whose bytes are expected to move between two builds of the
#  same source (syssvc/banner.c bakes __DATE__/__TIME__ into .rodata). Matched
#  on its exact path within the stage.
BANNER_OBJECT = "objs/banner.o"
#  Manifest keys that would carry a build time. The manifest written by
#  prebuilt_stage.cmake has none of these today; the list is here so that if
#  one is ever added, the comparison forgives exactly that key and nothing
#  else.
TIME_KEYS = frozenset({
    "generatedAt", "generated_at", "buildTime", "build_time", "builtAt",
    "built_at", "timestamp", "date", "time",
})

#  The chips a baseline can cover, in the order the report lists them. The
#  profiles per chip come from build_prebuilt_stages.py's own tables, so a
#  profile added there is expected here without a second edit.
#
#  The two Xtensa chips first, and they are the DEFAULT: the check exists to
#  say whether the Xtensa boards' bytes moved while the C6 port was worked
#  on, and a default that quietly grew to include the C6 stages would make
#  every C6 change a DIFF in a report meant to be read as "Xtensa unchanged".
#  esp32c6 is in the list so that a C6 baseline can be taken and compared
#  (xcheck_baseline.py --chips esp32c6) once the C6 stages are something to
#  guard as well (stage 5, S5-6); which chips a given comparison covers is
#  whatever its BASELINE.json records.
#  esp32c5 likewise (C5 plan A4: the C5 stages are ignorable until a C5
#  baseline is wanted); the C5 plan's baseline is --chips esp32s3 esp32 esp32c6.
#  esp32p4 likewise (StampP4 plan stage A0: the 11-stage baseline is
#  --chips esp32s3 esp32 esp32c6 esp32c5, and the P4 stage is ignorable
#  while the P4 is the chip being worked on).
CHIPS = ["esp32s3", "esp32", "esp32c6", "esp32c5", "esp32p4"]
DEFAULT_CHIPS = ["esp32s3", "esp32"]


def profiles_for(chip: str) -> list[str]:
    """Every profile a chip ships: the common set plus its chip-only ones,
    narrowed to what the chip's port can stage at all (the C6 port has no
    m5-unified: the M5NanoC6 has no display)."""
    stageable = build_prebuilt_stages.CHIPS[chip].profiles
    shipped = [name for name in build_prebuilt_stages.SHIPPED_PROFILES
               if name in stageable]
    only = [name for name, owner in build_prebuilt_stages.CHIP_ONLY_PROFILES.items()
            if owner == chip and name in stageable]
    return shipped + sorted(only)


def expected_stages_from_tables(chips=DEFAULT_CHIPS) -> list[str]:
    return [f"{chip}/{profile}" for chip in chips for profile in profiles_for(chip)]


def git_head(repository: Path) -> str:
    """The commit the working tree is at, or "" when git cannot say."""
    try:
        completed = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=False)
    except OSError:
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def relative_files(stage: Path) -> set[str]:
    """Every regular file under a stage, as a POSIX path relative to it."""
    return {p.relative_to(stage).as_posix()
            for p in stage.rglob("*") if p.is_file()}


def find_stages(root: Path) -> set[str]:
    """Stages are the directories holding a link-manifest.json.

    Named <chip>/<profile> relative to root, which is how
    build_prebuilt_stages.py lays them out and how the report names them.
    """
    if not root.is_dir():
        return set()
    return {m.parent.relative_to(root).as_posix()
            for m in root.rglob(MANIFEST_NAME) if m.is_file()}


def compare_manifest(baseline: Path, current: Path) -> tuple[str, str]:
    """Returns (verdict, detail): ("same", ""), ("same", note) when only
    time-stamp keys moved, or ("diff", why)."""
    base_bytes = baseline.read_bytes()
    cur_bytes = current.read_bytes()
    if base_bytes == cur_bytes:
        return "same", ""
    try:
        base = json.loads(base_bytes.decode("utf-8"))
        cur = json.loads(cur_bytes.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return "diff", "bytes differ (not comparable as JSON)"
    if not isinstance(base, dict) or not isinstance(cur, dict):
        return "diff", "bytes differ (JSON top level is not an object)"
    changed = sorted(k for k in set(base) | set(cur)
                     if base.get(k, object()) != cur.get(k, object()))
    if not changed:
        return "diff", ("bytes differ but the JSON is equal: formatting or "
                        "key order changed, which is still a different file")
    if set(changed) <= TIME_KEYS:
        return "same", ("differs only in time-stamp key(s): "
                        + ", ".join(changed))
    return "diff", "JSON differs in key(s): " + ", ".join(changed)


def compare_file(relative: str, baseline: Path, current: Path,
                 strict: bool) -> tuple[str, str]:
    """Compare one file present on both sides.

    Returns (verdict, detail): verdict is "same", "diff" or "skipped";
    detail is the one-line explanation for a diff, or a note for a
    forgiven difference.
    """
    if relative == MANIFEST_NAME:
        return compare_manifest(baseline, current)
    if relative == BANNER_OBJECT and not strict:
        return "skipped", ""
    if relative.endswith(".o"):
        base_sum, cur_sum = sha256_of(baseline), sha256_of(current)
        if base_sum != cur_sum:
            return "diff", (f"sha256 differs (baseline {base_sum[:12]}, "
                            f"current {cur_sum[:12]})")
        return "same", ""
    #  objects.rsp, lib/*.a, ld/*.ld and anything else: byte for byte.
    if baseline.read_bytes() != current.read_bytes():
        return "diff", "bytes differ"
    return "same", ""


def compare_stage(base_root: Path, cur_root: Path, stage: str,
                  strict: bool) -> dict:
    """Returns {"files": n, "skipped": n, "diffs": [(relative, detail)...],
    "notes": [(relative, note)...]}."""
    base_dir = base_root / stage
    cur_dir = cur_root / stage
    diffs: list[tuple[str, str]] = []
    notes: list[tuple[str, str]] = []
    if not base_dir.is_dir() and not cur_dir.is_dir():
        return {"files": 0, "skipped": 0, "notes": notes,
                "diffs": [("(stage)", "missing on both sides")]}
    if not base_dir.is_dir():
        return {"files": 0, "skipped": 0, "notes": notes,
                "diffs": [("(stage)", "only in current")]}
    if not cur_dir.is_dir():
        return {"files": 0, "skipped": 0, "notes": notes,
                "diffs": [("(stage)", "only in baseline")]}
    base_files = relative_files(base_dir)
    cur_files = relative_files(cur_dir)
    skipped = 0
    for relative in sorted(base_files | cur_files):
        if relative not in cur_files:
            diffs.append((relative, "only in baseline"))
            continue
        if relative not in base_files:
            diffs.append((relative, "only in current"))
            continue
        verdict, detail = compare_file(relative, base_dir / relative,
                                       cur_dir / relative, strict)
        if verdict == "skipped":
            skipped += 1
        elif verdict == "diff":
            diffs.append((relative, detail))
        elif detail:
            notes.append((relative, detail))
    return {"files": len(base_files | cur_files), "skipped": skipped,
            "diffs": diffs, "notes": notes}


def read_baseline_record(baseline: Path) -> dict | None:
    record = baseline / BASELINE_RECORD
    if not record.is_file():
        return None
    try:
        data = json.loads(record.read_text(encoding="utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None
    return data if isinstance(data, dict) else None


def chip_of(stage: str) -> str:
    """The <chip> of a <chip>/<profile> stage name."""
    return stage.split("/", 1)[0]


def compare_trees(baseline: Path, current: Path, strict: bool,
                  expected: list[str]) -> dict:
    """Compare every expected stage, plus any stage found on either side
    under a chip the expected set covers.

    Stages under any other chip are left out of the comparison and listed
    in "ignored": the expected set says which chips the baseline is about,
    and a chip it never held cannot have changed relative to it.

    Returns {"stages": {name: result}, "expected": n, "compared": n,
    "match": n, "diff": n, "missing": [name, ...], "ignored": [chip, ...]}.
    """
    covered = {chip_of(stage) for stage in expected}
    found_all = find_stages(baseline) | find_stages(current)
    found = {stage for stage in found_all if chip_of(stage) in covered}
    ignored = sorted({chip_of(stage) for stage in found_all - found})
    stages = list(expected) + sorted(found - set(expected))
    results = {}
    for stage in stages:
        results[stage] = compare_stage(baseline, current, stage, strict)
    missing = [s for s in expected if s not in found]
    compared = sum(1 for s in stages if s in found)
    match = sum(1 for r in results.values() if not r["diffs"])
    return {"stages": results, "expected": len(expected), "compared": compared,
            "match": match, "diff": len(stages) - match, "missing": missing,
            "ignored": ignored}


def displayable(path: Path) -> str:
    """A path for the report, never absolute: relative to the working
    directory when it lies inside it, relative to the repository when it lies
    inside that, otherwise its last component only. A directory outside both
    would relativise to a chain of ".." followed by the absolute path minus
    its leading slash, which is the same leak spelled differently."""
    for root in (Path.cwd(), REPOSITORY):
        try:
            relative = Path(os.path.relpath(path, root))
        except ValueError:
            continue  # different drive on Windows
        if not (relative.parts and relative.parts[0] == ".."):
            return relative.as_posix()
    return path.name


def report(baseline: Path, current: Path, strict: bool, outcome: dict,
           record: dict | None, current_head: str) -> None:
    print(f"xcheck: baseline={displayable(baseline)} "
          f"current={displayable(current)} "
          f"strict={'yes' if strict else 'no'}"
          f"{'' if strict else ' (' + BANNER_OBJECT + ' excluded)'}")
    if record is None:
        print(f"baseline: no {BASELINE_RECORD} (provenance unknown; take the "
              "baseline with xcheck_baseline.py)")
    else:
        head = str(record.get("head", "")) or "unknown"
        dirty = record.get("dirty")
        dirty_text = "unknown" if dirty is None else ("yes" if dirty else "no")
        print(f"baseline: head={head[:12]} dirty={dirty_text} "
              f"takenAt={record.get('takenAt', 'unknown')} "
              f"stages={len(record.get('stages', []))}")
        if dirty:
            print("warning: the baseline was taken on a dirty tree; it may "
                  "not correspond to its recorded commit")
        if current_head and head != "unknown" and current_head != head:
            print(f"warning: current HEAD {current_head[:12]} differs from "
                  f"the baseline's {head[:12]}; a MATCH says the stages are "
                  "the same, not that the commits are")
    for stage, result in outcome["stages"].items():
        if result["diffs"]:
            print(f"{stage}: DIFF ({len(result['diffs'])} files)")
            for relative, detail in result["diffs"]:
                print(f"  {relative}: {detail}")
        else:
            skipped = result["skipped"]
            note = f", {skipped} skipped" if skipped else ""
            print(f"{stage}: MATCH ({result['files']} files{note})")
        for relative, detail in result["notes"]:
            print(f"  note: {relative} {detail}")
    for chip in outcome["ignored"]:
        print(f"ignored (not in baseline): {chip}")
    print(f"expected={outcome['expected']} compared={outcome['compared']} "
          f"match={outcome['match']} diff={outcome['diff']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", default="",
                        help="stages saved by xcheck_baseline.py "
                             "(default: build/xcheck-baseline under the "
                             "repository)")
    parser.add_argument("--current", default="",
                        help="stages built after the change "
                             "(default: build/prebuilt under the repository)")
    parser.add_argument("--strict", action="store_true",
                        help=f"compare {BANNER_OBJECT} too (it differs on "
                             "every rebuild; use this to show that the "
                             "exclusion is the only thing hiding a change)")
    args = parser.parse_args(argv)

    baseline = Path(args.baseline) if args.baseline else DEFAULT_BASELINE
    current = Path(args.current) if args.current else DEFAULT_CURRENT

    record = read_baseline_record(baseline)
    #  The stages that must be there: what the baseline says it holds, or
    #  the full table when the baseline carries no record.
    if record and isinstance(record.get("stages"), list) and record["stages"]:
        expected = [str(s) for s in record["stages"]]
    else:
        expected = expected_stages_from_tables()

    outcome = compare_trees(baseline, current, args.strict, expected)
    report(baseline, current, args.strict, outcome, record, git_head(REPOSITORY))

    if outcome["compared"] == 0:
        print("FAILED: no stage was compared (no link-manifest.json under "
              "either directory). A check that ran on nothing is not a pass.")
        return 1
    if outcome["missing"]:
        print(f"FAILED: expected stage(s) absent from both sides: "
              f"{' '.join(outcome['missing'])}")
        return 1
    if outcome["diff"]:
        print(f"FAILED: {outcome['diff']} stage(s) differ from the baseline")
        return 1
    print(f"PASSED: {outcome['match']} stage(s) match the baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
