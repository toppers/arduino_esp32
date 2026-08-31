#!/usr/bin/env python3
"""Find build-machine paths that leaked into something being distributed.

This replaces a blocklist of specific known paths, one set per developer,
which could only ever catch the developer it was written for. It matches by
SHAPE instead - anything that looks like an absolute path on the machine that
built the artifact - so it works for whoever runs it, including people who have
not joined the project yet.

The blocklist was not merely incomplete. It missed a live leak: the 0.3.0
package embedded the packager's directory in 15 of its 47 objects and in every
final image, because the check built its patterns with backslashes and with
JSON-escaped backslashes but never with forward slashes, and CMake writes
forward slashes on Windows. Matching on shape has no such gap.

    python scripts/check_host_paths.py build/package
    python scripts/check_host_paths.py dist/toppers-esp32-0.3.0.zip

Directories and zip archives are both accepted. Exit status is 0 when nothing
leaked, 1 when something did, and 2 when the check could not run.
"""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path

#  A drive letter must not be preceded by an alphanumeric, or "https://" and
#  every other URL scheme matches - which is how the first version of this
#  reported eight false positives and no real ones.
HOST_PATH = re.compile(
    rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]{1,2}[A-Za-z0-9_. -]{2,}"
    rb"|/home/[a-z0-9_-]{2,}/"
    rb"|/Users/[A-Za-z0-9_. -]{2,}/"
    rb"|/root/[A-Za-z0-9_.-]")

#  Placeholders are how documentation writes a path without naming a machine.
#  A line carrying one is describing a path, not leaking one.
PLACEHOLDER = re.compile(rb"<[A-Za-z_-]{2,}>|\{[A-Za-z0-9_.]+\}|%[A-Z_]+%|\$[A-Za-z{]"
                         rb"|[Pp]ath.[Tt]o.|/\.\.\./")

#  Nothing in these is text, and scanning them produces noise rather than
#  findings. Objects are deliberately NOT here: they are where the leak was.
SKIP_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".pdf", ".zip", ".gz", ".bz2"}


def contexts(data: bytes) -> list[str]:
    """Every host-path match with a little text around it, placeholders aside."""
    found = []
    for match in HOST_PATH.finditer(data):
        line_start = data.rfind(b"\n", 0, match.start()) + 1
        line_end = data.find(b"\n", match.end())
        line = data[line_start:line_end if line_end != -1 else match.end() + 60]
        if PLACEHOLDER.search(line[:400]):
            continue
        text = data[match.start():match.end() + 50]
        text = text.split(b"\x00")[0]
        found.append(text.decode("latin1", errors="replace").strip())
    return found


def scan_bytes(name: str, data: bytes, problems: list[str]) -> None:
    if Path(name).suffix.lower() in SKIP_SUFFIXES:
        return
    for hit in dict.fromkeys(contexts(data)):
        problems.append(f"{name}: {hit}")


def scan(target: Path, problems: list[str]) -> int:
    """Returns how many members were examined."""
    examined = 0
    if target.is_dir():
        for path in sorted(target.rglob("*")):
            #  third_party is a submodule: upstream's files, carrying
            #  upstream's author's paths, which we cannot edit without
            #  forking. Scanning it buries our own findings - 294 of the 305
            #  matches on the publication tree came from one upstream
            #  document. What ships to a user is the platform archive, and
            #  that is scanned as an archive, where this does not apply.
            if "third_party" in path.parts:
                continue
            if path.is_file():
                examined += 1
                scan_bytes(str(path.relative_to(target)),
                           path.read_bytes(), problems)
    elif zipfile.is_zipfile(target):
        with zipfile.ZipFile(target) as archive:
            for member in archive.namelist():
                if member.endswith("/"):
                    continue
                examined += 1
                scan_bytes(member, archive.read(member), problems)
    elif target.is_file():
        examined = 1
        scan_bytes(target.name, target.read_bytes(), problems)
    else:
        raise FileNotFoundError(target)
    return examined


def main(argv: list[str] | None = None) -> int:
    #  A finding quotes the surrounding text, and this repository's comments
    #  are Japanese. On a Windows console stdout is cp932, so printing a
    #  finding raised UnicodeEncodeError and the check DIED instead of
    #  reporting - the one outcome a release gate must never have, because the
    #  findings are lost and the traceback looks like a broken tool rather
    #  than a leak. Degrade the characters, never the report.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="backslashreplace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("targets", nargs="+",
                        help="directories, zip archives or files to scan")
    parser.add_argument("--quiet", action="store_true",
                        help="print only the findings")
    args = parser.parse_args(argv)

    problems: list[str] = []
    total = 0
    for name in args.targets:
        target = Path(name)
        try:
            examined = scan(target, problems)
        except FileNotFoundError:
            print(f"check_host_paths: no such target: {target}", file=sys.stderr)
            return 2
        total += examined
        if not args.quiet:
            print(f"  scanned {examined:>4} file(s) in {target}")

    if problems:
        print(f"\nFAILED: {len(problems)} build-machine path(s) in the "
              "distributed files")
        for problem in problems[:40]:
            print(f"  - {problem}")
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more")
        print("\nA path here ships to every user. If it comes from __FILE__ or "
              "debug info,\nthe build needs -ffile-prefix-map; see "
              "ports/esp32s3_m5cores3/runtime/CMakeLists.txt.")
        return 1

    if not args.quiet:
        print(f"\nPASSED: no build-machine paths in {total} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
