#!/usr/bin/env python3
"""Regression test for the host-path pattern in check_host_paths.py.

    python scripts/test_check_host_paths.py

The pattern has been wrong three times, each time in a way that looked fine:

  - it matched every URL scheme, because "https://" ends in a letter, a colon
    and a slash;
  - it missed the leak it was written for, because it never built the
    forward-slash form CMake writes on Windows;
  - it matched compressed bytes inside the frozen driver binaries, because two
    path-safe characters after a stray drive letter were enough.

and the placeholder rule silenced a real finding because a placeholder
appeared elsewhere on the same line.

Every MUST_MATCH below is a path this project actually shipped or nearly
shipped. Every MUST_NOT_MATCH is something the check reported that was not a
leak. Both lists are assembled from fragments rather than written out, so that
this file - a file full of host paths - does not itself trip the check.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_host_paths import HOST_PATH, contexts  # noqa: E402

BS = chr(92)


def win(*parts: str) -> str:
    """A Windows path, joined here so no literal one appears in this file."""
    return BS.join(parts)


def nix(*parts: str) -> str:
    return "/".join(parts)


MUST_MATCH = [
    #  The build-path leak that reached 15 of 47 objects and every image. It
    #  survived because only the backslash forms were ever tested.
    nix("H:", "NagoyaUniv", "M5StackArduino", "port"),
    win("H:", "NagoyaUniv", "M5StackArduino"),
    #  A drive plus one component, with nothing after it.
    nix("H:", "NagoyaUniv"),
    #  Earlier developers' paths, found in comments before publishing.
    win("C:", "Users", "someone"),
    win("D:", "GitHub", "M5Stack", "FirstProj", "esp32_s3"),
    win("D:", "TOPPERS", "Arduino"),
    #  CI runner paths: one was in a generated recipe, one in a comment.
    win("C:", "hostedtoolcache", "windows", "Python"),
    win("C:", "Users", "runneradmin", "Documents"),
    win("C:", "Program Files", "Python312", "python.exe"),
    #  The JSON-escaped form, and the one an IDE writes.
    win("H:", "", "NagoyaUniv", "", "M5StackArduino"),
    nix("C:", "Users", "someone", "Documents", "Arduino"),
    #  Unix homes, including the runners'.
    nix("", "home", "someone", "tools", "esp-idf"),
    nix("", "home", "runner", "work", "arduino_esp32"),
    nix("", "Users", "runner", "hostedtoolcache"),
    #  System prefixes a build runs out of. Home directories alone missed the
    #  one that mattered: a hosted runner keeps its interpreter under the
    #  tool-cache prefix, install_platform.py writes that into platform.txt,
    #  and the check looked straight past it. Spelled from fragments below,
    #  like everything else here, so this file does not report itself.
    nix("", "opt", "hostedtoolcache", "Python", "3.12.10", "x64", "bin", ""),
    nix("", "opt", "homebrew", "bin", ""),
    nix("", "usr", "local", "bin", ""),
    nix("", "tmp", "build-a1b2c3", ""),
    nix("", "github", "workspace"),
    #  As it really appears in a compile command. Rejecting anything preceded
    #  by a letter would have silenced the URL false positives AND this.
    "-I" + nix("", "opt", "hostedtoolcache", "Python", "include", ""),
]

MUST_NOT_MATCH = [
    #  URL schemes - the first version's eight false positives.
    "https://github.com/toppers/arduino_esp32",
    "http://proxy.example.com:8888",
    "file:///" + nix("H:", ""),
    #  Compressed bytes from the frozen drivers: a stray drive letter, two
    #  characters, then bytes that are not path characters at all.
    "l:/dX",
    "h:/L7",
    "o:/wC",
    win("l:", "zz"),
    win("P:", "8S"),
    #  Prose and code that merely contains a colon.
    "note: see the manifest",
    "case 3: fall through",
    "Exit status is 0 when nothing leaked",
    #  Shebangs. Every script in this tree starts with one.
    "#!" + nix("", "usr", "bin", "env") + " python3",
    nix("", "usr", "bin", "ld"),
    #  Too short to be a directory name.
    nix("", "opt", ""),
    nix("", "tmp", "x", ""),
]

#  URLs whose path happens to contain a system prefix. HOST_PATH matches these
#  on purpose - rejecting them by a lookbehind would also reject -I/opt/... -
#  so contexts() filters them by the scheme separator instead. Checked through
#  contexts(), not HOST_PATH.
URL_MUST_BE_QUIET = [
    "https://cdn.example.com" + nix("", "opt", "downloads", "x.zip"),
    "https://github.com/toppers/arduino_esp32" + nix("", "tmp", "whatever", ""),
    "http://host" + nix("", "usr", "local", "guide", ""),
]


def main() -> int:
    failures = []
    for text in MUST_MATCH:
        if not HOST_PATH.search(text.encode("utf-8")):
            failures.append(f"MISSED a real leak: {text!r}")
    for text in MUST_NOT_MATCH:
        match = HOST_PATH.search(text.encode("utf-8"))
        if match:
            failures.append(
                f"FALSE POSITIVE on {text!r} (matched {match.group(0)!r})")
    for text in URL_MUST_BE_QUIET:
        reported = contexts(text.encode("utf-8"))
        if reported:
            failures.append(
                f"URL REPORTED AS A HOST PATH: {text!r} -> {reported[0]!r}")

    print(f"must match    : {len(MUST_MATCH)}")
    print(f"must not match: {len(MUST_NOT_MATCH)}")
    print(f"urls kept quiet: {len(URL_MUST_BE_QUIET)}")
    if failures:
        print(f"\nFAILED, {len(failures)} case(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("\nPASSED: every case behaves as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
