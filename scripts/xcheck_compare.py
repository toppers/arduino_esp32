#!/usr/bin/env python3
"""X-check: decide whether the prebuilt Xtensa stages changed.

The C6 port has to touch scripts and CMake files that the ESP32-S3 and ESP32
stages are built through. "The Xtensa boards are unaffected" is then a claim
about bytes, and this is what turns it into a measurement: it compares a
baseline set of stages (saved by xcheck_baseline.py before the work) with the
set built afterwards, stage by stage, file by file.

What is compared, per stage (<chip>/<profile>):

  link-manifest.json  parsed as JSON; time-stamp keys, if the manifest ever
                      grows any, are dropped before the two are compared.
                      Falls back to a byte comparison when either side is not
                      valid JSON.
  objects.rsp         byte for byte
  objs/*.o            SHA-256. banner.o is skipped unless --strict is given:
                      it carries __DATE__/__TIME__ and changes on every
                      rebuild (BUILDING.md), and a check that always fails is
                      no check. --strict exists so that the exclusion itself
                      can be shown to be the only thing hiding a difference.
  lib/*.a             byte for byte
  everything else     byte for byte (the linker scripts under ld/, and
                      anything a future stage adds - an unknown file is
                      compared, never ignored)
  a file, or a whole stage, present on one side only counts as a difference.

Output names files by their path relative to the stage, never by where the
stage lives: the report is meant to be pasted into records, and
check_host_paths.py must find nothing in it.

Exit status: 0 when every stage matched, 1 when any differed - and also 1
when nothing was compared at all (no stages found), so a misplaced --baseline
cannot read as a pass.

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
import sys
from pathlib import Path

DEFAULT_BASELINE = Path("build") / "xcheck-baseline"
DEFAULT_CURRENT = Path("build") / "prebuilt"

MANIFEST_NAME = "link-manifest.json"
RESPONSE_NAME = "objects.rsp"
#  The one object whose bytes are expected to move between two builds of the
#  same source (syssvc/banner.c bakes __DATE__/__TIME__ into .rodata).
BANNER_OBJECT = "banner.o"
#  Manifest keys that would carry a build time. The manifest written by
#  prebuilt_stage.cmake has none of these today; the list is here so that if
#  one is ever added, the comparison ignores exactly that key and nothing else.
TIME_KEYS = frozenset({
    "generatedAt", "generated_at", "buildTime", "build_time", "builtAt",
    "built_at", "timestamp", "date", "time",
})


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


def compare_manifest(baseline: Path, current: Path) -> str:
    """"" when equal, otherwise one line saying how they differ."""
    base_bytes = baseline.read_bytes()
    cur_bytes = current.read_bytes()
    if base_bytes == cur_bytes:
        return ""
    try:
        base = json.loads(base_bytes.decode("utf-8"))
        cur = json.loads(cur_bytes.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return "bytes differ (not comparable as JSON)"
    if not isinstance(base, dict) or not isinstance(cur, dict):
        return "JSON differs (top level is not an object)"
    base = {k: v for k, v in base.items() if k not in TIME_KEYS}
    cur = {k: v for k, v in cur.items() if k not in TIME_KEYS}
    if base == cur:
        return ""
    changed = sorted(k for k in set(base) | set(cur)
                     if base.get(k, object()) != cur.get(k, object()))
    return "JSON differs in key(s): " + ", ".join(changed)


def compare_file(relative: str, baseline: Path, current: Path,
                 strict: bool) -> tuple[str, str]:
    """Compare one file present on both sides.

    Returns (verdict, detail): verdict is "same", "diff" or "skipped";
    detail is the one-line explanation for a diff.
    """
    name = Path(relative).name
    if relative == MANIFEST_NAME:
        detail = compare_manifest(baseline, current)
        return ("diff", detail) if detail else ("same", "")
    if name.endswith(".o"):
        if name == BANNER_OBJECT and not strict:
            return "skipped", ""
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
    """Returns {"files": n, "skipped": n, "diffs": [(relative, detail), ...]}."""
    base_dir = base_root / stage
    cur_dir = cur_root / stage
    diffs: list[tuple[str, str]] = []
    if not base_dir.is_dir():
        return {"files": 0, "skipped": 0,
                "diffs": [("(stage)", "only in current")]}
    if not cur_dir.is_dir():
        return {"files": 0, "skipped": 0,
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
    return {"files": len(base_files | cur_files), "skipped": skipped,
            "diffs": diffs}


def compare_trees(baseline: Path, current: Path, strict: bool) -> dict:
    """Compare every stage found on either side.

    Returns {"stages": {name: result}, "compared": n, "match": n, "diff": n}.
    """
    stages = sorted(find_stages(baseline) | find_stages(current))
    results = {}
    for stage in stages:
        results[stage] = compare_stage(baseline, current, stage, strict)
    match = sum(1 for r in results.values() if not r["diffs"])
    return {"stages": results, "compared": len(stages), "match": match,
            "diff": len(stages) - match}


def displayable(path: Path) -> str:
    """A path for the report: relative to the working directory, never
    absolute, so the output holds no build-machine directory.

    A directory outside the working directory would relativise to a chain
    of ".." followed by the absolute path minus its leading slash, which is
    the same leak spelled differently; only its last component is shown then.
    """
    try:
        relative = Path(os.path.relpath(path))
    except ValueError:
        #  Different drive on Windows: relpath cannot be formed.
        return path.name + " (outside the working directory)"
    if relative.parts and relative.parts[0] == "..":
        return path.name + " (outside the working directory)"
    return relative.as_posix()


def report(baseline: Path, current: Path, strict: bool, outcome: dict) -> None:
    print(f"xcheck: baseline={displayable(baseline)} "
          f"current={displayable(current)} "
          f"strict={'yes' if strict else 'no'}"
          f"{'' if strict else ' (' + BANNER_OBJECT + ' excluded)'}")
    for stage, result in outcome["stages"].items():
        if result["diffs"]:
            print(f"{stage}: DIFF ({len(result['diffs'])} files)")
            for relative, detail in result["diffs"]:
                print(f"  {relative}: {detail}")
        else:
            skipped = result["skipped"]
            note = f", {skipped} skipped" if skipped else ""
            print(f"{stage}: MATCH ({result['files']} files{note})")
    print(f"compared={outcome['compared']} match={outcome['match']} "
          f"diff={outcome['diff']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", default=str(DEFAULT_BASELINE),
                        help="stages saved by xcheck_baseline.py "
                             "(default: build/xcheck-baseline)")
    parser.add_argument("--current", default=str(DEFAULT_CURRENT),
                        help="stages built after the change "
                             "(default: build/prebuilt)")
    parser.add_argument("--strict", action="store_true",
                        help=f"compare {BANNER_OBJECT} too (it differs on "
                             "every rebuild; use this to show that the "
                             "exclusion is the only thing hiding a change)")
    args = parser.parse_args(argv)

    baseline = Path(args.baseline)
    current = Path(args.current)
    outcome = compare_trees(baseline, current, args.strict)
    report(baseline, current, args.strict, outcome)

    if outcome["compared"] == 0:
        print("FAILED: no stage was compared (no link-manifest.json under "
              "either directory). A check that ran on nothing is not a pass.")
        return 1
    if outcome["diff"]:
        print(f"FAILED: {outcome['diff']} stage(s) differ from the baseline")
        return 1
    print(f"PASSED: {outcome['match']} stage(s) match the baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
