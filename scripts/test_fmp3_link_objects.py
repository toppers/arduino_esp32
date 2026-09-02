#!/usr/bin/env python3
"""Regression test for which Arduino objects fmp3_link hands to the linker.

    python scripts/test_fmp3_link_objects.py

The driver used to link the sketch object plus a fixed allowlist
(``requiredArduinoObjects``) and nothing else. Everything else the Arduino
builder had compiled was dropped without a word, which meant:

  - the bundled LibraryInfo example did not link on any profile, because
    ToppersFMP3_M5CoreS3.cpp.o - which defines the only function the example
    calls - was on no profile's allowlist;
  - a sketch made of more than one file did not link either, because only
    <project>.cpp.o was taken out of build/sketch;
  - any library with its own .cpp would have failed the same way.

The allowlist was not pointless, though, and this is why the fix is not
"link everything". Force-linking every library object would put
ToppersFMP3_WiFi.cpp.o into the minimal profile, where the Wi-Fi symbols it
calls do not exist, and Blink would stop linking. So objects the manifest does
not name are offered to the linker in an archive: pulled in when a sketch
refers to them, absent when it does not. The cases below pin both halves of
that - what must be linked, and what must merely be available.

No toolchain is needed; the object files here are empty placeholders, because
what is under test is the selection, not the link.
"""

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fmp3_link import LinkError, collect_arduino_objects  # noqa: E402


PROJECT = "LibraryInfo.ino"

#  The three profiles ship this manifest entry; wifi-connect adds
#  ToppersFMP3_WiFi.cpp.o to it.
MANIFEST = {"requiredArduinoObjects": ["<sketch>.cpp.o",
                                       "ArduinoSketchBridge.cpp.o"]}

#  What the Arduino builder leaves behind for a sketch of two files that
#  includes the bundled library.
SKETCH_OBJECTS = [f"sketch/{PROJECT}.cpp.o", "sketch/helper.cpp.o"]
LIBRARY_OBJECTS = [
    "libraries/ToppersFMP3-M5CoreS3/ArduinoSketchBridge.cpp.o",
    "libraries/ToppersFMP3-M5CoreS3/ToppersFMP3_M5CoreS3.cpp.o",
    "libraries/ToppersFMP3-M5CoreS3/ToppersFMP3_WiFi.cpp.o",
    "libraries/SomeThirdPartyLib/src/Widget.cpp.o",
]


def build_tree(root: Path, relative_paths) -> None:
    for relative in relative_paths:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"")


def names(paths) -> set:
    return {path.name for path in paths}


def check(failures: list, case: str, condition: bool, detail: str = "") -> None:
    if not condition:
        failures.append(f"{case}: {detail}" if detail else case)


def main() -> int:
    failures: list = []

    with tempfile.TemporaryDirectory() as temporary:
        build_path = Path(temporary)
        build_tree(build_path, SKETCH_OBJECTS + LIBRARY_OBJECTS)
        objects = collect_arduino_objects(MANIFEST, build_path, PROJECT)
        linked = names(objects.linked)
        archived = names(objects.archived)

        check(failures, "the sketch's own object is linked",
              f"{PROJECT}.cpp.o" in linked, f"linked={sorted(linked)}")
        check(failures, "a second file of the same sketch is linked",
              "helper.cpp.o" in linked, f"linked={sorted(linked)}")
        check(failures, "a library object the manifest names is linked",
              "ArduinoSketchBridge.cpp.o" in linked, f"linked={sorted(linked)}")

        check(failures, "the bundled library's own object is available",
              "ToppersFMP3_M5CoreS3.cpp.o" in archived,
              f"archived={sorted(archived)}")
        check(failures, "a third-party library object is available",
              "Widget.cpp.o" in archived, f"archived={sorted(archived)}")

        #  The reason the fix is an archive rather than "link everything":
        #  this object calls Wi-Fi symbols that the minimal profile does not
        #  have, so forcing it in would break every minimal build.
        check(failures,
              "an object the manifest does not name is not force-linked",
              "ToppersFMP3_WiFi.cpp.o" not in linked,
              f"linked={sorted(linked)}")
        check(failures, "no object is both linked and archived",
              not (linked & archived), f"both={sorted(linked & archived)}")

    #  A stage that names an object the build does not have is a broken stage,
    #  and that has to stay loud rather than become a link error later.
    with tempfile.TemporaryDirectory() as temporary:
        build_path = Path(temporary)
        build_tree(build_path, SKETCH_OBJECTS)
        try:
            collect_arduino_objects(MANIFEST, build_path, PROJECT)
        except LinkError:
            pass
        else:
            failures.append("a missing required object should raise LinkError")

    with tempfile.TemporaryDirectory() as temporary:
        build_path = Path(temporary)
        build_tree(build_path, LIBRARY_OBJECTS)
        try:
            collect_arduino_objects(MANIFEST, build_path, PROJECT)
        except LinkError:
            pass
        else:
            failures.append("a missing sketch object should raise LinkError")

    if failures:
        print(f"FAILED, {len(failures)} case(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("PASSED: every case behaves as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
