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

The second half covers manifest schema 2 and paddrMode "fixed-vma" (the
RISC-V stages): that a schema 1 manifest still produces the schema 1 link
command literally, that schema 2 takes its flags from the manifest, and that
the image checks C-1 to C-8 each fail on an image built to break exactly one
of them - a check that cannot be shown failing is not evidence of anything.
The images and the ELF are synthetic; esptool is not involved.
"""

import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fmp3_link import (APP_DESC_MAGIC, FIXED_VMA_LAYOUTS,  # noqa: E402
                       ImageLayout, LinkError, app_partition_length,
                       build_link_command, check_fixed_vma_image,
                       collect_arduino_objects, expander,
                       parse_image_segments, validate_manifest)


PROJECT = "LibraryInfo.ino"

#  The three profiles ship this manifest entry; wifi-connect adds
#  ToppersFMP3_WiFi.cpp.o to it.
MANIFEST = {"requiredArduinoObjects": ["<sketch>.cpp.o",
                                       "ArduinoSketchBridge.cpp.o"]}

#  What the Arduino builder leaves behind for a sketch of two files that
#  includes the bundled library.
SKETCH_OBJECTS = [f"sketch/{PROJECT}.cpp.o", "sketch/helper.cpp.o"]
LIBRARY_OBJECTS = [
    "libraries/ToppersFMP3-M5Stack/ArduinoSketchBridge.cpp.o",
    "libraries/ToppersFMP3-M5Stack/ToppersFMP3_M5CoreS3.cpp.o",
    "libraries/ToppersFMP3-M5Stack/ToppersFMP3_WiFi.cpp.o",
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


def expect_error(failures: list, case: str, action, fragment: str) -> None:
    """The action must raise LinkError whose message names the condition."""
    try:
        action()
    except LinkError as error:
        if fragment not in str(error):
            failures.append(f"{case}: raised LinkError without {fragment!r}: "
                            f"{error}")
    else:
        failures.append(f"{case}: no LinkError was raised")


#
#  Manifests. SCHEMA1 is the shape the Xtensa stages carry (values from
#  build/prebuilt/esp32s3/minimal); SCHEMA2 adds the keys a RISC-V stage
#  writes.
#
SCHEMA1 = {
    "schema": 1, "profile": "minimal", "chip": "esp32s3",
    "paddrMode": "runtime-mmu", "xipLinkerScript": "ld/esp32s3_xip_m5.ld",
    "romLinkerScripts": ["esp32s3.rom.ld", "esp32s3.rom.api.ld"],
    "extraLinkerScripts": [], "linkUFlags": [], "linkLibGroup": ["-lstdc++", "-lm"],
    "flashMode": "dio", "flashFreq": "80m", "flashSize": "16MB",
    "objectCount": 0, "objectOrder": [], "requiredArduinoObjects": [],
}
SCHEMA2_RISCV = dict(SCHEMA1, **{
    "schema": 2, "chip": "esp32c6", "paddrMode": "fixed-vma",
    "xipLinkerScript": "ld/esp32c6_xip.ld",
    "romLinkerScripts": ["esp32c6.rom.ld", "esp32c6.rom.api.ld"],
    "linkBaseFlags": ["-march=rv32imac_zicsr_zifencei", "-mabi=ilp32",
                      "-nostdlib"],
    "linkTailFlags": ["-lc", "-lgcc"],
    "flashSize": "4MB",
})

FAKE_SDK = {"gcc": Path("gcc"), "sdk_ld": Path("sdk-ld"),
            "sdk_lib": Path("sdk-lib")}


def link_command(manifest: dict) -> list:
    stage = Path("stage")
    return build_link_command(manifest, stage, FAKE_SDK,
                              expander(stage, FAKE_SDK, manifest["chip"]),
                              "libarduino.a")


def manifest_cases(failures: list) -> None:
    #  Schema 1 is read as driver 2 read it, message included.
    validate_manifest(dict(SCHEMA1))
    expect_error(failures, "schema 1 with fixed-vma is refused",
                 lambda: validate_manifest(dict(SCHEMA1, paddrMode="fixed-vma")),
                 "requires runtime PADDR resolution")
    expect_error(failures, "an unknown schema is refused",
                 lambda: validate_manifest(dict(SCHEMA1, schema=3)),
                 "Unsupported manifest schema")
    expect_error(failures, "schema 2 with an unknown paddrMode is refused",
                 lambda: validate_manifest(dict(SCHEMA2_RISCV, paddrMode="esptool")),
                 "Unsupported paddrMode")
    #  Schema 2 needs linkBaseFlags; a stage that forgot them would
    #  otherwise link with the Xtensa literals on a RISC-V toolchain.
    missing = dict(SCHEMA2_RISCV)
    del missing["linkBaseFlags"]
    expect_error(failures, "schema 2 without linkBaseFlags is refused",
                 lambda: validate_manifest(missing), "linkBaseFlags")
    expect_error(failures, "schema 2 with linkBaseFlags as a string is refused",
                 lambda: validate_manifest(dict(SCHEMA2_RISCV, linkBaseFlags="-nostdlib")),
                 "linkBaseFlags")
    validate_manifest(dict(SCHEMA2_RISCV))
    without_tail = dict(SCHEMA2_RISCV)
    del without_tail["linkTailFlags"]
    validate_manifest(without_tail)
    validate_manifest(dict(SCHEMA2_RISCV, paddrMode="runtime-mmu"))

    #  The schema 1 command, literally: -nostdlib -mlongcalls first, -lgcc
    #  -lc last. This is the line the Xtensa boards link with.
    command = link_command(SCHEMA1)
    check(failures, "schema 1 links with the literal head flags",
          command[1:3] == ["-nostdlib", "-mlongcalls"], f"command={command}")
    check(failures, "schema 1 links with the literal tail flags",
          command[-2:] == ["-lgcc", "-lc"], f"command={command}")
    check(failures, "schema 1 puts the link group before the tail",
          command[-4:-2] == ["-lstdc++", "-lm"], f"command={command}")

    #  A schema 2 manifest that spells the Xtensa literals links identically.
    xtensa2 = dict(SCHEMA1, schema=2,
                   linkBaseFlags=["-nostdlib", "-mlongcalls"])
    check(failures, "schema 2 with the Xtensa literals gives the schema 1 command",
          link_command(xtensa2) == command,
          f"schema1={command}\nschema2={link_command(xtensa2)}")

    #  A RISC-V manifest: its own flags in the same positions, no
    #  -mlongcalls anywhere.
    riscv = link_command(SCHEMA2_RISCV)
    check(failures, "schema 2 takes the head flags from linkBaseFlags",
          riscv[1:4] == SCHEMA2_RISCV["linkBaseFlags"], f"command={riscv}")
    check(failures, "schema 2 takes the tail flags from linkTailFlags",
          riscv[-2:] == ["-lc", "-lgcc"], f"command={riscv}")
    check(failures, "schema 2 does not carry the Xtensa literals",
          "-mlongcalls" not in riscv, f"command={riscv}")
    check(failures, "schema 2 keeps the fixed flags after the head",
          riscv[4:6] == ["-Wl,--gc-sections", "-Wl,--allow-multiple-definition"],
          f"command={riscv}")
    check(failures, "schema 2 without linkTailFlags falls back to -lgcc -lc",
          link_command(without_tail)[-2:] == ["-lgcc", "-lc"])


#
#  Synthetic images. The layout is the driver's esp32c6 row, and the image
#  is built the way esptool lays one out: 24-byte header, then segments of
#  (load, length, data). A RAM segment carries bytes from the synthetic
#  ELF's .data at the matching address.
#
LAYOUT = FIXED_VMA_LAYOUTS["esp32c6"]
DATA_VMA = 0x40800000
DATA_BYTES = bytes(range(256)) * 4          # 1 KB of .data
APP_LENGTH = 0x140000                       # default.csv app0

#  Mapped segments: text at page 0, rodata at page 1, congruent by
#  construction (file offsets are chosen so that offset % page == vaddr %
#  page). Segment #0 starts with the app descriptor magic.
TEXT_VADDR = 0x42000020
RODATA_VADDR = 0x42010000 + 0x20
ENTRY = TEXT_VADDR + 0x40


def synthetic_elf(data_vma: int = DATA_VMA, data: bytes = DATA_BYTES,
                  with_data: bool = True) -> bytes:
    """A 32-bit little-endian ELF with .data (PROGBITS) and .shstrtab."""
    shstrtab = b"\0.data\0.shstrtab\0"
    ehdr_size, shdr_size = 52, 40
    data_off = ehdr_size
    str_off = data_off + len(data)
    shoff = str_off + len(shstrtab)
    names = {"null": 0, ".data": 1, ".shstrtab": 7}
    sections = [(0, 0, 0, 0, 0, 0)]
    if with_data:
        sections.append((names[".data"], 1, 3, data_vma, data_off, len(data)))
    sections.append((names[".shstrtab"], 3, 0, 0, str_off, len(shstrtab)))
    shstrndx = len(sections) - 1
    ehdr = (b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8
            + struct.pack("<HHIIIIIHHHHHH", 2, 243, 1, ENTRY, 0, shoff, 0,
                          ehdr_size, 0, 0, shdr_size, len(sections),
                          shstrndx))
    body = ehdr + data + shstrtab
    for name, kind, flags, addr, offset, size in sections:
        body += struct.pack("<IIIIIIIIII", name, kind, flags, addr, offset,
                            size, 0, 0, 1, 0)
    return body


def synthetic_image(segments, entry: int = ENTRY) -> bytes:
    """esp_image bytes: header, then (load, data) segments in order."""
    header = struct.pack("<BBBBI", 0xE9, len(segments), 0, 0, entry)
    header += b"\0" * (24 - len(header))
    body = bytearray(header)
    for load, data in segments:
        body += struct.pack("<II", load, len(data)) + data
    return bytes(body)


def congruent_segments(text: bytes, rodata: bytes, ram: bytes,
                       ram_load: int = DATA_VMA) -> list:
    """Segments whose mapped members satisfy C-3 by padding, as esptool does.

    The first mapped segment goes first (C-2), then the second mapped one,
    then the RAM segment: text, rodata, ram. See layout_segments().
    """
    return layout_segments([(TEXT_VADDR, text), (RODATA_VADDR, rodata),
                            (ram_load, ram)])


def layout_segments(segments: list) -> list:
    """Lay (load, data) segments out in the given order, keeping every
    flash-mapped one page-congruent (C-3) by inserting a pad segment (load 0)
    in front of it when needed, as esptool does.

    The order is the caller's, so an image that puts a RAM segment first, or
    carries a third mapped segment, still satisfies C-3 - which is what lets
    a test break C-2 or C-1 without also breaking C-3.
    """
    page = LAYOUT.page
    laid = []
    offset = 24                                # where the next header starts
    for load, data in segments:
        mapped = LAYOUT.drom[0] <= load < LAYOUT.drom[1]
        if mapped and (offset + 8) % page != load % page:
            #  With a pad segment in between, this segment's data begins at
            #  offset + 8 (pad header) + pad + 8 (its own header).
            pad = (load - offset - 16) % page or page
            laid.append((0, b"\0" * pad))
            offset += 8 + pad
        laid.append((load, data))
        offset += 8 + len(data)
    return laid


def good_image() -> bytes:
    #  Segment #0's first bytes must be the app descriptor; the text segment
    #  also has to contain ENTRY (C-4).
    text = struct.pack("<I", APP_DESC_MAGIC) + b"T" * 0x200
    #  The header of segment #0 sits at file offset 24, its data at 32;
    #  32 % page == TEXT_VADDR % page == 0x20, so C-3 holds for it.
    segments = congruent_segments(text, b"R" * 0x100, DATA_BYTES)
    return synthetic_image(segments)


def image_cases(failures: list) -> None:
    elf = synthetic_elf()
    image = good_image()
    try:
        lines = check_fixed_vma_image(image, elf, LAYOUT, APP_LENGTH)
    except LinkError as error:
        failures.append(f"the well-formed image should pass: {error}")
        return
    check(failures, "the well-formed image reports C-8 with the partition length",
          any("C-8 OK" in line and f"/{APP_LENGTH}" in line for line in lines),
          f"lines={lines}")

    #  Each condition broken on its own. The good image is the control, and
    #  for the images whose shape could break a second condition as a side
    #  effect (a third mapped segment, a RAM segment in front, a RAM segment
    #  outside SRAM), that second condition is asserted to still hold, so
    #  the check named in the label is the only one that can fail.
    text = image[32:32 + 0x204]
    three_mapped = synthetic_image(layout_segments(
        [(TEXT_VADDR, text), (RODATA_VADDR, b"R" * 0x100),
         (DATA_VMA, DATA_BYTES), (0x42030000, b"X" * 16)]))
    check(failures, "C-1 image: every mapped segment is page-congruent (C-3 holds)",
          all(seg.file_offset % LAYOUT.page == seg.load % LAYOUT.page
              for seg in parse_image_segments(three_mapped, LAYOUT).mapped))
    expect_error(failures, "C-1: three mapped segments",
                 lambda: check_fixed_vma_image(three_mapped, elf, LAYOUT, APP_LENGTH),
                 "C-1")
    bad_magic = bytearray(image)
    bad_magic[32:36] = b"\0\0\0\0"
    expect_error(failures, "C-2: segment #0 without the app descriptor",
                 lambda: check_fixed_vma_image(bytes(bad_magic), elf, LAYOUT, APP_LENGTH),
                 "C-2")
    ram_first = synthetic_image(layout_segments(
        [(DATA_VMA, DATA_BYTES), (TEXT_VADDR, text),
         (RODATA_VADDR, b"R" * 0x100)]))
    check(failures, "C-2 image: every mapped segment is page-congruent (C-3 holds)",
          all(seg.file_offset % LAYOUT.page == seg.load % LAYOUT.page
              for seg in parse_image_segments(ram_first, LAYOUT).mapped))
    expect_error(failures, "C-2: a RAM segment before the mapped ones",
                 lambda: check_fixed_vma_image(ram_first, elf, LAYOUT, APP_LENGTH),
                 "C-2")
    #  Drop the pad segment: the rodata segment's offset is then not
    #  congruent with its vaddr.
    no_pad = synthetic_image([seg for seg in
                              congruent_segments(text, b"R" * 0x100, DATA_BYTES)
                              if seg[0] != 0])
    expect_error(failures, "C-3: a mapped segment that is not page-congruent",
                 lambda: check_fixed_vma_image(no_pad, elf, LAYOUT, APP_LENGTH),
                 "C-3")
    stray_entry = synthetic_image(
        congruent_segments(text, b"R" * 0x100, DATA_BYTES), entry=0x42090000)
    expect_error(failures, "C-4: an entry in no segment",
                 lambda: check_fixed_vma_image(stray_entry, elf, LAYOUT, APP_LENGTH),
                 "C-4")
    into_loader = synthetic_image(congruent_segments(
        text, b"R" * 0x100, DATA_BYTES, ram_load=LAYOUT.loader_seg - 0x100))
    loader_elf = synthetic_elf(data_vma=LAYOUT.loader_seg - 0x100)
    expect_error(failures, "C-5: a RAM segment reaching iram_loader_seg",
                 lambda: check_fixed_vma_image(into_loader, loader_elf, LAYOUT, APP_LENGTH),
                 "C-5")
    #  Below SRAM, so that the segment's end is also below iram_loader_seg:
    #  only the range half of C-5 is broken. (A load above SRAM would end
    #  above iram_loader_seg too, and the other half would catch it as
    #  well.) The ELF's .data is moved along so C-6 holds.
    below_sram = 0x3FC00000
    outside_sram = synthetic_image(congruent_segments(
        text, b"R" * 0x100, DATA_BYTES, ram_load=below_sram))
    check(failures, "C-5 range image: the RAM segment ends below iram_loader_seg",
          below_sram + len(DATA_BYTES) < LAYOUT.loader_seg)
    expect_error(failures, "C-5: a RAM segment outside SRAM",
                 lambda: check_fixed_vma_image(outside_sram, synthetic_elf(data_vma=below_sram), LAYOUT, APP_LENGTH),
                 "C-5")
    corrupted = bytearray(image)
    corrupted[-1] ^= 0xFF                     # last byte of the RAM segment
    expect_error(failures, "C-6: RAM bytes that differ from the ELF .data",
                 lambda: check_fixed_vma_image(bytes(corrupted), elf, LAYOUT, APP_LENGTH),
                 "C-6")
    expect_error(failures, "C-6: an ELF without .data",
                 lambda: check_fixed_vma_image(image, synthetic_elf(with_data=False), LAYOUT, APP_LENGTH),
                 "C-6")
    expect_error(failures, "C-6: an ELF with empty .data",
                 lambda: check_fixed_vma_image(image, synthetic_elf(data=b""), LAYOUT, APP_LENGTH),
                 "C-6")
    no_ram = synthetic_image(
        congruent_segments(text, b"R" * 0x100, b"")[:-1])
    expect_error(failures, "C-6: an image with no RAM segment",
                 lambda: check_fixed_vma_image(no_ram, elf, LAYOUT, APP_LENGTH),
                 "C-6")
    #  A rodata segment that starts on the text segment's page: C-3 still
    #  holds (the pad keeps it congruent) but the page ranges overlap.
    page = LAYOUT.page
    overlapping = list(congruent_segments(text, b"R" * 0x100, DATA_BYTES))
    #  Rewrite the second mapped segment to text's page + a congruent offset.
    overlap_vaddr = (TEXT_VADDR & ~(page - 1)) + (overlapping[2][0] % page)
    overlapping[2] = (overlap_vaddr, overlapping[2][1])
    overlap_image = synthetic_image(overlapping)
    expect_error(failures, "C-7: two mapped segments on the same page",
                 lambda: check_fixed_vma_image(overlap_image, elf, LAYOUT, APP_LENGTH),
                 "C-7")
    expect_error(failures, "C-8: an image larger than the app partition",
                 lambda: check_fixed_vma_image(image, elf, LAYOUT, len(image) - 1),
                 "C-8")
    #  Exactly the partition size still fits.
    check_fixed_vma_image(image, elf, LAYOUT, len(image))

    #  A layout whose page does not divide the flash offset cannot be
    #  checked at all (C-3's premise).
    odd = ImageLayout(page=0x8000 * 3, drom=LAYOUT.drom, iram=LAYOUT.iram,
                      loader_seg=LAYOUT.loader_seg)
    expect_error(failures, "C-3: a page that does not divide the flash offset",
                 lambda: check_fixed_vma_image(image, elf, odd, APP_LENGTH),
                 "C-3")


#  The M5Stack core's tools/partitions/default.csv, which the C6 boards use.
DEFAULT_CSV = """\
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
spiffs,   data, spiffs,  0x290000,0x160000,
coredump, data, coredump,0x3F0000,0x10000,
"""


def partition_cases(failures: list) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        csv = Path(temporary) / "partitions.csv"
        csv.write_text(DEFAULT_CSV, encoding="utf-8")
        check(failures, "the app partition at 0x10000 gives its size",
              app_partition_length(csv) == 0x140000)
        #  An implicit offset (the app after otadata lands on 0x10000 by
        #  alignment) is resolved the way gen_esp32part resolves it.
        csv.write_text(DEFAULT_CSV.replace("app0,     app,  ota_0,   0x10000,",
                                           "app0,     app,  ota_0,   ,"),
                       encoding="utf-8")
        check(failures, "an implicit app offset resolves to 0x10000",
              app_partition_length(csv) == 0x140000)
        csv.write_text("nvs, data, nvs, 0x9000, 0x5000,\n"
                       "app0, app, ota_0, 0x20000, 0x100000,\n",
                       encoding="utf-8")
        expect_error(failures, "no app partition at the flash offset",
                     lambda: app_partition_length(csv), "C-8")
        expect_error(failures, "no partitions.csv at all",
                     lambda: app_partition_length(Path(temporary) / "none.csv"),
                     "C-8")


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

    manifest_cases(failures)
    image_cases(failures)
    partition_cases(failures)

    if failures:
        print(f"FAILED, {len(failures)} case(s):")
        for failure in failures:
            print("  " + failure)
        return 1
    print("PASSED: every case behaves as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
