#!/usr/bin/env python3
"""Audit a prebuilt stage's objects for duplicate strong global definitions.

Ported from the reference port's cmake/a1_dupsym_audit.sh Written
in Python rather than bash because the stage build already requires Python and
the release path should not grow a bash dependency.

Why this is needed - "the linker will catch it" is not true here
----------------------------------------------------------------
The final link always carries -Wl,--allow-multiple-definition (see
scripts/fmp3_link.py and runtime/cmake/xip_build.cmake). A duplicate symbol
therefore does NOT fail the link: ld silently takes the first definition, and
"first" is decided by the ordinal order of the object file names. So "no
multiple definition in the link log" is no information at all about duplicates.

That matters most for the combined M5 + Wi-Fi profile, where the m5 mini shim's
duplicates are dropped with #ifndef M5_USE_ESP_SHIM so esp/shim provides them
instead. Miss one guard and the build still succeeds, with the surviving
definition chosen by how the filenames happen to sort.

What this audit does NOT look at (so nobody reads more into a pass)
------------------------------------------------------------------
* Only the staged .o files against each other. Prebuilt archives (the Wi-Fi
  blobs, libsupplicant.a, libmbedcrypto.a) and libgcc/libc are not examined.
* Symbols a linker script PROVIDEs have no definition in any .o, so they never
  appear here.
* Weak (W/V) and common (C) symbols are excluded. C++ inline functions,
  templates and vtables are weak, and duplicates among those are legitimate.
  What is left is only ever "two or more strong definitions" - a real
  duplicate.

Exit codes: 0 = clean (after the allowlist), 1 = duplicates found,
2 = the audit itself could not run. A failure to run is never a pass.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

#  nm type letters that denote a strong definition. W/V (weak) and C (common)
#  are deliberately absent - see the module docstring.
STRONG_TYPES = set("TDBRAGS")


def enumerate_definitions(nm: Path, objects: list[Path]) -> dict[str, set[str]]:
    """Map symbol -> set of object file names defining it strongly.

    A single nm failure aborts: letting one through would turn an earlier
    failure into a reassuring "0 duplicates".
    """
    definitions: dict[str, set[str]] = {}
    for obj in objects:
        result = subprocess.run([str(nm), "-g", "--defined-only", str(obj)],
                                text=True, capture_output=True)
        if result.returncode != 0:
            print(f"duplicate-symbol audit: nm failed on {obj.name}",
                  file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            raise SystemExit(2)
        for line in result.stdout.splitlines():
            fields = line.split()
            #  "<addr> <type> <name>", or "<type> <name>" when the address is
            #  absent.
            if len(fields) < 2:
                continue
            symbol_type, name = fields[-2], fields[-1]
            if len(symbol_type) == 1 and symbol_type in STRONG_TYPES:
                definitions.setdefault(name, set()).add(obj.name)
    return definitions


def read_allowlist(path: Path | None) -> set[str]:
    """One symbol per line; '#' starts a comment.

    Returns an empty set when no file is given. An empty allowlist must behave
    as "allow nothing" - the bash original had a bug here where an empty list
    made every duplicate look permitted and the audit always passed, so this is
    kept deliberately explicit.
    """
    if path is None:
        return set()
    if not path.is_file():
        print(f"duplicate-symbol audit: allowlist not found: {path}",
              file=sys.stderr)
        raise SystemExit(2)
    allowed: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            allowed.add(line.split()[0])
    return allowed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Find duplicate strong global definitions in a stage.")
    parser.add_argument("--nm", required=True,
                        help="the target nm (xtensa-esp32s3-elf-nm)")
    parser.add_argument("--objects", required=True,
                        help="directory of staged .o files")
    parser.add_argument("--allow", default="",
                        help="allowlist file; every entry needs a reason")
    parser.add_argument("--label", default="",
                        help="profile name, for the report line")
    args = parser.parse_args()

    nm = Path(args.nm)
    if not nm.is_file():
        print(f"duplicate-symbol audit: nm not found: {nm}", file=sys.stderr)
        return 2
    objects_dir = Path(args.objects)
    if not objects_dir.is_dir():
        print(f"duplicate-symbol audit: no such directory: {objects_dir}",
              file=sys.stderr)
        return 2
    objects = sorted(objects_dir.glob("*.o"))
    if not objects:
        #  Zero inputs is not a pass.
        print(f"duplicate-symbol audit: no .o files in {objects_dir}",
              file=sys.stderr)
        return 2

    definitions = enumerate_definitions(nm, objects)
    duplicates = {name: files for name, files in definitions.items()
                  if len(files) > 1}
    allowed = read_allowlist(Path(args.allow) if args.allow else None)
    remaining = {name: files for name, files in duplicates.items()
                 if name not in allowed}

    label = f"[{args.label}] " if args.label else ""
    print(f"  {label}duplicate-symbol audit: {len(objects)} objects / "
          f"{len(definitions)} strong definitions / {len(duplicates)} duplicated"
          f" / {len(allowed)} allowed")
    if remaining:
        print(f"  {label}duplicate-symbol audit FAILED - duplicates that are "
              f"not in the allowlist:", file=sys.stderr)
        for name in sorted(remaining):
            files = " ".join(sorted(remaining[name]))
            print(f"      {name}   defined by: {files}", file=sys.stderr)
        print("  Which definition survives is decided by object file name "
              "order, not by design.", file=sys.stderr)
        print("  Either guard the duplicate out of one side, or add it to the "
              "allowlist WITH a reason.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
