#!/usr/bin/env python3
"""X-check, first half: build every Xtensa stage and keep a copy as the baseline.

Run this BEFORE touching anything the ESP32-S3 / ESP32 stages are built
through (build_prebuilt_stages.py, prebuilt_stage.cmake, fmp3_link.py,
install_platform.py, ports/m5stack_xtensa/...). Afterwards, rebuild the
stages the usual way and run xcheck_compare.py: it reports, per stage and
per file, whether the Xtensa boards' distributed bytes changed.

It builds through build_prebuilt_stages.py - the same script, the same
arguments, the same SDK and toolchain resolution (arduino_sdk.resolve) that
the release uses - once per chip, with every profile that chip ships:

    esp32s3   minimal  m5-unified  wifi-connect
    esp32     minimal  m5-unified  wifi-connect  bt-classic

The profile sets are taken from build_prebuilt_stages.py itself
(SHIPPED_PROFILES plus CHIP_ONLY_PROFILES), so adding a shipped profile
there makes it part of the baseline here without a second edit.

The stages land where they always do, build/prebuilt/<chip>/<profile>/, and
each listed profile is then copied to build/xcheck-baseline/<chip>/<profile>/
(only the listed ones - whatever else sits under build/prebuilt/<chip> is
not part of the baseline). Leaving build/prebuilt populated is deliberate: a
compare run straight after this must say MATCH for every stage, which is the
cheapest check that the comparison machinery itself is wired up; and
install_platform.py finds fresh stages where it expects them.

Next to the stages it writes BASELINE.json: the commit the tree was at
(git rev-parse HEAD), whether the tree was dirty (git status --porcelain,
as a count - no paths), when it was taken, and the chips, profiles and
stages it holds. xcheck_compare.py prints that and warns when the current
HEAD is a different commit. A baseline is only evidence if it was taken on
a clean tree at the commit the stage starts from, BEFORE the build scripts
are edited; so an existing baseline is never overwritten silently - pass
--force to replace it, and mean it.

    python scripts/xcheck_baseline.py
    python scripts/xcheck_baseline.py --clean          # from scratch
    python scripts/xcheck_baseline.py --chips esp32s3  # one chip only
    python scripts/xcheck_baseline.py --force          # replace an old one

Every option build_prebuilt_stages.py takes for locating tools and sources
(--cmake, --ninja, --arduino-data, --core-version, --m5gfx-source,
--m5unified-source, --library-root) is accepted here and passed through
unchanged. Exit status is 0 when every expected stage was built and saved,
1 otherwise; a build that produced fewer stages than expected is a failure,
not a shorter baseline.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
#  The chip list, the per-chip profile table (derived from
#  build_prebuilt_stages.py), the record name and the git helper are shared
#  with the comparer so the two cannot disagree about what a baseline holds.
from xcheck_compare import (BASELINE_RECORD, CHIPS, MANIFEST_NAME,  # noqa: E402
                            git_head, profiles_for)

DEFAULT_BASELINE = Path("build") / "xcheck-baseline"


def git_dirty_count(repository: Path) -> int | None:
    """How many paths git status reports (tracked changes and untracked
    files alike), or None when git cannot say. The count is what gets
    recorded - never the paths."""
    try:
        completed = subprocess.run(
            ["git", "-C", str(repository), "status", "--porcelain"],
            capture_output=True, text=True, check=False)
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    return sum(1 for line in completed.stdout.splitlines() if line.strip())


def displayable(path: Path) -> str:
    """Relative to the working directory or the repository when inside one
    of them, otherwise the last component only; the report should not carry
    this machine's directory (see check_host_paths.py)."""
    for root in (Path.cwd(), Path(__file__).resolve().parent.parent):
        try:
            relative = Path(os.path.relpath(path, root))
        except ValueError:
            continue
        if not (relative.parts and relative.parts[0] == ".."):
            return relative.as_posix()
    return path.name


def format_duration(seconds: float) -> str:
    whole = int(round(seconds))
    minutes, secs = divmod(whole, 60)
    return f"{minutes}m{secs:02d}s ({whole} s)"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline-directory", default="",
                        help="where to keep the copy (default: "
                             "build/xcheck-baseline under the repository)")
    parser.add_argument("--chips", nargs="+", choices=CHIPS, default=CHIPS,
                        help="chips to build (default: both)")
    parser.add_argument("--clean", action="store_true",
                        help="pass --clean to build_prebuilt_stages.py "
                             "(rebuild the stages from scratch)")
    parser.add_argument("--force", action="store_true",
                        help="replace an existing baseline (without this, "
                             "an existing one is an error: a baseline must "
                             "be taken before the change it is meant to "
                             "catch, and retaking it after the fact proves "
                             "nothing)")
    #  Pass-through options; build_prebuilt_stages.py owns their meaning.
    passthrough = ["--library-root", "--cmake", "--ninja", "--arduino-data",
                   "--core-version", "--m5gfx-source", "--m5unified-source"]
    for option in passthrough:
        parser.add_argument(option, default="",
                            help="passed to build_prebuilt_stages.py")
    args = parser.parse_args(argv)

    builder = Path(__file__).resolve().parent / "build_prebuilt_stages.py"
    library_root = Path(args.library_root).resolve() if args.library_root \
        else builder.parent.parent
    prebuilt_root = library_root / "build" / "prebuilt"
    #  Given explicitly: relative to the working directory, like every other
    #  path option in these scripts. Defaulted: under the repository, so the
    #  result lands in the same place whatever the working directory.
    baseline_root = Path(args.baseline_directory).resolve() \
        if args.baseline_directory else library_root / DEFAULT_BASELINE

    existing = [p for p in (baseline_root / BASELINE_RECORD,
                            *(baseline_root / chip for chip in CHIPS))
                if p.exists()]
    if existing and not args.force:
        print(f"xcheck_baseline: a baseline already exists under "
              f"{displayable(baseline_root)} ({len(existing)} item(s)). "
              "Compare against it with xcheck_compare.py, or pass --force "
              "to replace it - only if this tree is the clean start of the "
              "stage.", file=sys.stderr)
        return 1
    #  Replacing means replacing the whole set, so that a --chips subset does
    #  not leave the other chip's old stages beside a record that does not
    #  list them. Only the known items are removed, never the directory
    #  itself: --baseline-directory could name something that holds more.
    for stale in existing:
        if stale.is_dir():
            shutil.rmtree(stale)
        else:
            stale.unlink()

    head = git_head(library_root)
    dirty_count = git_dirty_count(library_root)
    if not head:
        print("xcheck_baseline: warning: git HEAD unavailable; the baseline "
              "record will carry no commit", file=sys.stderr)
    elif dirty_count:
        print(f"xcheck_baseline: warning: the tree is dirty "
              f"({dirty_count} path(s) in git status); the baseline will be "
              "recorded as such", file=sys.stderr)

    forwarded: list[str] = []
    for option in passthrough:
        value = getattr(args, option.lstrip("-").replace("-", "_"))
        if value:
            forwarded += [option, value]
    if args.clean:
        forwarded.append("--clean")

    started = time.monotonic()
    per_chip: list[tuple[str, float]] = []
    saved: list[str] = []
    for chip in args.chips:
        profiles = profiles_for(chip)
        output = prebuilt_root / chip
        command = [sys.executable, str(builder),
                   "--chip", chip, "--profiles", *profiles,
                   "--output-directory", str(output), *forwarded]
        print(f"\n### xcheck baseline: {chip} ({' '.join(profiles)})",
              flush=True)
        chip_started = time.monotonic()
        completed = subprocess.run(command)
        if completed.returncode != 0:
            print(f"xcheck_baseline: build_prebuilt_stages.py failed for "
                  f"{chip} (exit={completed.returncode})", file=sys.stderr)
            return 1
        per_chip.append((chip, time.monotonic() - chip_started))

        #  Every expected stage must be there. The builder stops on the
        #  first failure, so this is belt and braces - but the baseline is
        #  what later comparisons are judged against, and a short one would
        #  make every later run pass on the missing stages.
        missing = [name for name in profiles
                   if not (output / name / MANIFEST_NAME).is_file()]
        if missing:
            print(f"xcheck_baseline: {chip} produced no stage for: "
                  f"{' '.join(missing)}", file=sys.stderr)
            return 1

        #  Only the listed profiles: build/prebuilt/<chip> may hold other
        #  things (an all-in-one experiment, a self-test flavour) that are
        #  not part of what the release ships and so not part of the claim.
        destination = baseline_root / chip
        for name in profiles:
            shutil.copytree(output / name, destination / name)
            saved.append(f"{chip}/{name}")

    elapsed = time.monotonic() - started
    record = {
        "schema": 1,
        "head": head,
        "dirty": None if dirty_count is None else dirty_count > 0,
        "dirtyCount": dirty_count,
        "takenAt": datetime.datetime.now(datetime.timezone.utc)
                                    .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "chips": list(args.chips),
        "profiles": {chip: profiles_for(chip) for chip in args.chips},
        "stages": saved,
        "clean": bool(args.clean),
        "elapsedSeconds": int(round(elapsed)),
    }
    baseline_root.mkdir(parents=True, exist_ok=True)
    (baseline_root / BASELINE_RECORD).write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print("\nX-check baseline saved\n")
    for stage in saved:
        print(f"  {stage}")
    print(f"\n  stages : {len(saved)}")
    print(f"  head   : {head[:12] if head else 'unknown'}"
          f"  dirty: {'unknown' if dirty_count is None else ('yes' if dirty_count else 'no')}")
    print(f"  from   : {displayable(prebuilt_root)}")
    print(f"  to     : {displayable(baseline_root)} ({BASELINE_RECORD} written)")
    for chip, seconds in per_chip:
        print(f"  {chip:<8} {format_duration(seconds)}")
    print(f"  total    {format_duration(elapsed)}")
    print("\nNext: change things, rebuild the stages, then\n"
          "  python scripts/xcheck_compare.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
