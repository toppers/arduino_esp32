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
are then copied whole to build/xcheck-baseline/<chip>/<profile>/. Leaving
build/prebuilt populated is deliberate: a compare run straight after this
must say MATCH for every stage, which is the cheapest check that the
comparison machinery itself is wired up; and install_platform.py finds
fresh stages where it expects them.

Anything already under build/xcheck-baseline/<chip> is removed first, so a
stale profile from an earlier baseline cannot linger next to the new ones.

    python scripts/xcheck_baseline.py
    python scripts/xcheck_baseline.py --clean          # from scratch
    python scripts/xcheck_baseline.py --chips esp32s3  # one chip only

Every option build_prebuilt_stages.py takes for locating tools and sources
(--cmake, --ninja, --arduino-data, --core-version, --m5gfx-source,
--m5unified-source, --library-root) is accepted here and passed through
unchanged. Exit status is 0 when every expected stage was built and saved,
1 otherwise; a build that produced fewer stages than expected is a failure,
not a shorter baseline.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_prebuilt_stages  # noqa: E402

#  Ordered so that the report and the baseline directory read the same way
#  every time.
CHIPS = ["esp32s3", "esp32"]

DEFAULT_BASELINE = Path("build") / "xcheck-baseline"
MANIFEST_NAME = "link-manifest.json"


def profiles_for(chip: str) -> list[str]:
    """Every profile a chip ships: the common set plus its chip-only ones."""
    shipped = list(build_prebuilt_stages.SHIPPED_PROFILES)
    only = [name for name, owner in build_prebuilt_stages.CHIP_ONLY_PROFILES.items()
            if owner == chip]
    return shipped + sorted(only)


def displayable(path: Path) -> str:
    """Relative to the working directory when it lies inside it; the report
    should not carry this machine's directory (see check_host_paths.py)."""
    try:
        relative = Path(os.path.relpath(path))
    except ValueError:
        return path.name
    if relative.parts and relative.parts[0] == "..":
        return path.name
    return relative.as_posix()


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

        destination = baseline_root / chip
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(output, destination)
        for name in profiles:
            saved.append(f"{chip}/{name}")

    elapsed = time.monotonic() - started
    print("\nX-check baseline saved\n")
    for stage in saved:
        print(f"  {stage}")
    print(f"\n  stages : {len(saved)}")
    print(f"  from   : {displayable(prebuilt_root)}")
    print(f"  to     : {displayable(baseline_root)}")
    for chip, seconds in per_chip:
        print(f"  {chip:<8} {format_duration(seconds)}")
    print(f"  total    {format_duration(elapsed)}")
    print("\nNext: change things, rebuild the stages, then\n"
          "  python scripts/xcheck_compare.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
