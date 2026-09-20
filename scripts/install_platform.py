#!/usr/bin/env python3
"""Assemble the TOPPERS/FMP3 Arduino board platform.

Produces the platform
directory that Boards Manager later packages: boards.txt, platform.txt, the
partition tools, the prebuilt stages and the link driver.

One platform can hold every board whose chip has stages (the ESP32-S3,
ESP32 and ESP32-C6 boards side by side). build_prebuilt_stages.py writes
build/prebuilt/<chip>, so pointing at the parent installs every chip built
there:

    python scripts/install_platform.py --prebuilt-stage-root build/prebuilt

Pointing at one chip's own directory installs just that board, and --chip
picks a subset out of a multi-chip root:

    python scripts/install_platform.py --chip esp32s3 \
        --prebuilt-stage-root build/prebuilt/esp32s3

Together with build_prebuilt_stages.py this is everything the CI package job
does, so that job no longer needs a Windows runner.

There used to be a second way to install: without prebuilt stages, so that a
sketch build compiled the whole FMP3 runtime through
Invoke-PortableFmp3Recipe.ps1. This script refused to implement it rather than
half-support it, because that recipe was PowerShell and a platform written for
it worked on Windows only. Both are gone now, so there is one way to install
and stages are not optional.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from arduino_sdk import SdkError, resolve  # noqa: E402

MARKER = ".toppers-fmp3-platform.json"

#  The runtime profiles the board offers. Retired option
#  keys are unchanged so sketches with a saved board selection keep working, and
#  'dual' and 'wifi' are not reused - an old selection fails to resolve rather
#  than silently building something different.
#  Our board per chip, and the M5Stack board it is derived from.
#
#    chip: (source board id in the M5Stack boards.txt, our board id,
#           our display name, the variant to reference)
#
#  The variant is named explicitly rather than taken from the source line,
#  because it has to be rewritten into the m5stack: namespace anyway.
#  ★Keyed by our board id, not by chip. Two boards can share a chip - the
#  M5CoreS3 and the M5StickS3 are both ESP32-S3 and link the same stages - so
#  a chip cannot name a board. Each entry carries the chip it needs stages for.
#
#    our board id: (chip, source board id in the M5Stack boards.txt,
#                   display name, variant)
#
#  The variant is named explicitly rather than taken from the source line,
#  because it has to be rewritten into the m5stack: namespace anyway.
BOARDS = {
    "m5cores3_fmp3": ("esp32s3", "m5stack_cores3",
                      "M5CoreS3 (TOPPERS/FMP3)", "m5stack_cores3"),
    "m5sticks3_fmp3": ("esp32s3", "m5stack_sticks3",
                       "M5StickS3 (TOPPERS/FMP3)", "m5stack_sticks3"),
    #  ESP32-S3, no display, no PSRAM. The M5Stack core 3.3.9 has no
    #  m5stack_atoms3lite board at all (only m5stack_atoms3 and
    #  m5stack_atoms3r), so this derives from m5stack_atoms3: same chip, same
    #  8MB flash, same qio_qspi memory type and the same upload sizes as the
    #  M5StickS3 row above - the two differ only in build.board, which
    #  BOARD_BUILD_OVERRIDES rewrites below. Its variant is m5stack_atoms3
    #  too; the variant supplies pin names, not a display.
    "m5atoms3lite_fmp3": ("esp32s3", "m5stack_atoms3",
                          "M5AtomS3Lite (TOPPERS/FMP3)", "m5stack_atoms3"),
    "m5core_fmp3": ("esp32", "m5stack_core",
                    "M5Core (TOPPERS/FMP3)", "m5stack_core"),
    #  ESP32-PICO-D4 (LX6), no display, no PSRAM, 4MB flash. Derived from
    #  the M5Stack core's m5stack_atom, which is this very board (upstream
    #  keeps one row for the ATOM Lite, Matrix and Echo), so unlike the
    #  M5AtomS3Lite above nothing has to be rewritten: build.board stays
    #  M5STACK_ATOM and the sketches guard on ARDUINO_M5STACK_ATOM. The row
    #  differs from m5stack_core in name, variant, build.board and
    #  build.partitions=huge_app (app0 3MB; upload.maximum_size comes from
    #  the PartitionScheme menu as it does upstream) - same 4MB flash, dio,
    #  80m, bootloader at 0x1000. Its stages are the esp32 ones the M5Core
    #  links; see BOARD_SKIP_ENTRIES for the one it does not offer.
    "m5atomlite_fmp3": ("esp32", "m5stack_atom",
                        "M5AtomLite (TOPPERS/FMP3)", "m5stack_atom"),
    #  ESP32-C6 (RISC-V). Derived from the M5Stack core's m5stack_nano_c6
    #  (boards.txt 3.3.9: tarch=riscv32, mcu=esp32c6, 4MB flash), which is
    #  what makes {compiler.path} and {compiler.sdk.path} resolve to the
    #  RISC-V toolchain and esp32c6-libs without anything chip-specific in
    #  platform.txt. Installed only when esp32c6 stages are present, like
    #  every other board.
    "m5nanoc6_fmp3": ("esp32c6", "m5stack_nano_c6",
                      "M5NanoC6 (TOPPERS/FMP3)", "m5stack_nano_c6"),
    #  ESP32-C5 (RISC-V). Derived from the M5Stack core's m5stack_stamp_c5
    #  (boards.txt 3.3.9: tarch=riscv32, mcu=esp32c5, 4MB flash,
    #  bootloader_addr=0x2000 - the C5's bootloader offset, inherited as is;
    #  f_cpu=240000000L, the clock the stage is built for). C5 plan A9.
    "m5stampc5_fmp3": ("esp32c5", "m5stack_stamp_c5",
                       "M5StampC5 (TOPPERS/FMP3)", "m5stack_stamp_c5"),
    #  ESP32-P4 (RISC-V, dual core). Derived from the M5Stack core's
    #  m5stack_stamp_p4 (boards.txt 3.3.9: tarch=riscv32, mcu=esp32p4,
    #  chip_variant=esp32p4_es, 16MB qio flash, bootloader_addr=0x2000,
    #  f_cpu=360000000L - the clock the stage is built for). The
    #  ChipVariant menu the row inherits is dropped (BOARD_DROP_MENUS) and
    #  build.chip_variant pinned (BOARD_BUILD_OVERRIDES): the stage is built
    #  against esp32p4_es-libs and its ROM linker scripts. StampP4 plan
    #  P3 / P8. Installed only when esp32p4 stages are present.
    "m5stampp4_fmp3": ("esp32p4", "m5stack_stamp_p4",
                       "M5StampP4 (TOPPERS/FMP3)", "m5stack_stamp_p4"),
}

#  Board-level build properties that must NOT be inherited from the board we
#  derive from, keyed by our board id. The M5AtomS3Lite derives from the
#  M5Stack core's m5stack_atoms3 (there is no Lite row upstream), and that
#  row sets build.board=M5STACK_ATOMS3, which reaches a sketch as
#  ARDUINO_M5STACK_ATOMS3 - the macro a sketch would use to tell the two
#  apart. The AtomS3 has an LCD and no RGB LED; the Lite is the opposite, so
#  examples/AtomS3LiteRgb must not compile its real body on an AtomS3.
#  Rewriting the macro is safe because M5Unified and M5GFX decide the board
#  at run time (autodetect); neither library reads ARDUINO_M5STACK_ATOMS3*
#  anywhere (grep, 3.3.9-era checkouts).
BOARD_BUILD_OVERRIDES = {
    "m5atoms3lite_fmp3": {"build.board": "M5STACK_ATOMS3LITE"},
    #  The M5StampP4 keeps its inherited build.board; what it pins is the
    #  SDK variant. Upstream sets build.chip_variant=esp32p4_es on the row
    #  and offers a ChipVariant menu that can switch it to esp32p4 (v3
    #  silicon: other ROM, other bootloader). The FMP3 stage is one build
    #  against esp32p4_es-libs, so the menu is dropped (BOARD_DROP_MENUS)
    #  and the value restated here - the same key as upstream, replaced in
    #  place, so the pin is visible next to the other build.* lines.
    "m5stampp4_fmp3": {
        "build.chip_variant": "esp32p4_es",
        #  The M5Stack core 3.3.8 did not compile for its own
        #  m5stack_stamp_p4 board: cores/esp32/esp32-hal-spi.c:299 read
        #  BOARD_SDMMC_POWER_CHANNEL under SOC_SDMMC_IO_POWER_EXTERNAL (P4)
        #  and the variant's pins_arduino.h does not define it (the Tab5's
        #  does: 4), so an empty sketch failed with "'BOARD_SDMMC_POWER_CHANNEL'
        #  undeclared". This board therefore restated build.extra_flags.esp32p4
        #  with -DBOARD_SDMMC_POWER_CHANNEL=4 added.
        #
        #  3.3.9 fixes it upstream: the identifier no longer appears anywhere
        #  under cores/ (SD_MMC.cpp still reads it, but under
        #  `#if defined(...)`), and `arduino-cli compile -b
        #  m5stack:esp32:m5stack_stamp_p4` builds an empty sketch (measured
        #  2026-09-18, 314144 bytes). The override is removed: the value 4 was
        #  the Tab5's LDO channel and is not known to be the StampP4's, so
        #  leaving it defined would hand SD_MMC a possibly wrong channel now
        #  that the guard no longer discards it. The board inherits the
        #  platform's build.extra_flags.esp32p4 unchanged.
    },
}

#  Inherited menus a board must not offer, keyed by our board id. A menu
#  the source board declares is copied line by line with the board; for the
#  M5StampP4 the ChipVariant menu would let the user switch
#  build.chip_variant to esp32p4 (the v3-silicon SDK), under which the
#  sketch would link against another ROM linker script set than the one
#  the stage was built with - silently, since the stage carries no image.
#  The menu's board lines are dropped; the platform-wide `menu.ChipVariant=`
#  declaration stays (other boards of the same platform are unaffected by
#  a declared menu no board line uses).
BOARD_DROP_MENUS = {
    "m5stampp4_fmp3": {"ChipVariant"},
}

#  Menu entries a board does not offer although its chip ships the stage,
#  keyed by our board id. Until now every board offered exactly what its chip
#  shipped, because the display-less boards (M5NanoC6, M5StampC5) are the only
#  boards of chips that ship no m5-unified stage at all. The M5AtomS3Lite is
#  the first board that has to say no by itself: it is an ESP32-S3, and the
#  ESP32-S3 ships m5-unified for the M5CoreS3 and the M5StickS3. The AtomS3
#  Lite has no display, and M5Unified decides the board at run time, so
#  whether the m5 option is usable there is a hardware question (AtomS3 Lite
#  plan, stage 4) - until it is answered the board does not offer the entry.
#  verify_package.BOARD_PROFILES and the drift test
#  (scripts/test_check_release_artifacts.py) subtract the same set.
#  The M5AtomLite is the same case on the ESP32: no display, while the chip
#  ships m5-unified for the M5Core. Here the answer is by analogy, not
#  measured (the board was added without hardware at hand, ATOM Lite plan
#  B-2): the m5 runtime's adapter needs the LCD's SPI bus
#  (m5_arduino_adapter.cpp, the AtomS3 Lite failed exactly there), and
#  M5Unified's PICO-D4 autodetect uses delay()/taskENTER_CRITICAL and the
#  touch sensor on G27, none of which this port has. bt-classic IS offered:
#  the PICO-D4 has BR/EDR and the stage is board-independent (M5Core-proven),
#  link-verified only until the board is run.
#  The experimental all-in-one entry follows m5 exactly: it CONTAINS the
#  m5-unified runtime (M5GFX and M5Unified compiled into the same stage), so
#  a board that cannot use m5 cannot use aio either. Spelled out per board
#  rather than derived from the m5 row, because the reason is the board's
#  hardware and a future board could skip one without the other.
BOARD_SKIP_ENTRIES = {
    "m5atoms3lite_fmp3": {"m5", "aio"},
    "m5atomlite_fmp3": {"m5", "aio"},
}


def boards_for_chips(chips) -> list[str]:
    """Our board ids for these chips, in BOARDS order."""
    return [board for board, entry in BOARDS.items() if entry[0] in chips]

MENU_ENTRIES = [
    ("minimal", "Minimal", "minimal"),
    ("m5", "M5Unified + Dual Core", "m5-unified"),
    ("wificonnect", "WiFi", "wifi-connect"),
]
#  EXPERIMENTAL: M5Unified + SMP + Wi-Fi in one runtime. Offered
#  only when its stage is present, so a normal install of the three shipped
#  profiles does not show a menu entry that cannot build.
EXPERIMENTAL_ENTRY = ("aio", "All-in-one (experimental)", "all-in-one")

#  Entries that exist on one chip only. Bluetooth Classic is the first: the
#  ESP32-S3 has no BR/EDR radio at all, so the CoreS3 board must not offer an
#  option it could never build. Keyed by chip rather than filtered by "is the
#  stage on disk", so a stray stage directory cannot put the entry on the wrong
#  board.
CHIP_ONLY_ENTRIES = {
    "esp32": [("btclassic", "Bluetooth Classic (SPP)", "bt-classic")],
}


def profile_macro(profile: str) -> str:
    """The define that tells a sketch which runtime it is being built for.

    Picking the wrong Tools > FMP3 Runtime option used to surface as a page of
    undefined references from libarduino.a: the library object that wraps the
    runtime (ToppersFMP3_BT.cpp.o, ToppersFMP3_WiFi.cpp.o) compiles against
    every profile, but the symbols behind it exist in one stage only. Nothing
    in the build carried the selection, so an example could not check it and
    the message named linker symbols rather than the menu. With this define an
    example states the profile it needs and the build stops at the #error.
    """
    return "TOPPERS_FMP3_RUNTIME_" + profile.upper().replace("-", "_")

#  What a complete board for this chip offers. Installing with a stage missing
#  is a real mistake - every board ships all three - so that stays an error.
#
#  This comment used to say the LX6 board ran the minimal profile only, because
#  the m5/ and wifi/ shims had not been ported to the chip. They have been:
#  9d0b8d2 ran m5-unified on the M5Stack Basic and ac7b5ac ran wifi-connect,
#  and both profiles pass on that board in the Windows suite as well
#  (docs/windows-tests/backlog.md, B-1). The table below has required all three
#  for both chips since then; it was the comment that was left behind, which is
#  worth saying because the next reader would otherwise doubt the table.
EXPECTED_PROFILES = {
    "esp32s3": {"minimal", "m5-unified", "wifi-connect"},
    "esp32": {"minimal", "m5-unified", "wifi-connect"},
    #  The C6 port offers minimal and wifi-connect (docs/c6-port.md, D11).
    #  m5-unified never - the M5NanoC6 has no display.
    "esp32c6": {"minimal", "wifi-connect"},
    #  The C5 port offers the same pair as the C6 (docs/c5-port.md A10;
    #  wifi-connect since C5 plan stage 3). m5-unified never - the
    #  M5Stamp-C5 has no display. The drift test holds this row against
    #  build_prebuilt_stages.CHIPS and the release allowlist.
    "esp32c5": {"minimal", "wifi-connect"},
    #  The P4 port offers the same pair as the C6 and C5 since StampP4 plan
    #  stage B, but its wifi-connect is hosted: no blob and no PHY on this
    #  chip - the radio is a companion ESP32-C6 on the Stamp add-on, reached
    #  over SDIO (esp_hosted). m5-unified never (the M5Stamp-P4 has no
    #  display) and no bt-classic (no BR/EDR radio on the P4 itself).
    "esp32p4": {"minimal", "wifi-connect"},
}

#  recipe.size.regex per chip, for a chip whose linker script does not use
#  the section names platform_lines() writes into platform.txt. Emitted as
#  board-level overrides in boards.txt, so platform.txt keeps the Xtensa
#  values unchanged. Section names from the C6 port's esp32c6_xip.ld: .text,
#  .flash.appdesc, .flash.rodata in flash; .data, .bss, .tbss in RAM.
#
#  A board-level recipe.size.regex does take precedence over the platform
#  one: arduino-cli 1.5.2 `compile --show-properties` reports these values
#  for m5nanoc6_fmp3 and the platform.txt values for the Xtensa boards, and
#  the size line of a C6 Blink build (.data + .bss = 17760 bytes) shows the
#  RAM regex leaving out .flash_rodata_dummy, the NOBITS gap the linker
#  script keeps between .text and the DROM page (size -B counts it as bss).
#  Adding C6 names to the platform regex, the fallback S1-7 allowed, was
#  therefore not needed.
SIZE_REGEX_OVERRIDES = {
    "esp32c6": (r"^(?:\.text|\.flash\.appdesc|\.flash\.rodata)\s+([0-9]+).*",
                r"^(?:\.data|\.bss|\.tbss)\s+([0-9]+).*"),
    #  The C5 port's esp32c5_xip.ld is the C6 script's copy with the same
    #  output section names (docs/c5-port.md, stage 1).
    "esp32c5": (r"^(?:\.text|\.flash\.appdesc|\.flash\.rodata)\s+([0-9]+).*",
                r"^(?:\.data|\.bss|\.tbss)\s+([0-9]+).*"),
    #  The P4 port's esp32p4_xip.ld (the dev seam script): .flash_text,
    #  .flash.appdesc, .flash_rodata in flash; .iram_text (the seam entry,
    #  RAM-resident), .data, .sbss, .bss in RAM (its .kernel_data_CLS_* /
    #  .stack_CLS_* input sections are collected into .data / .bss; .sbss is
    #  an output section of its own in this script, unlike the C6 / C5).
    "esp32p4": (r"^(?:\.flash_text|\.flash\.appdesc|\.flash_rodata)\s+([0-9]+).*",
                r"^(?:\.iram_text|\.data|\.sbss|\.bss|\.tbss)\s+([0-9]+).*"),
}

#  upload.maximum_size / upload.maximum_data_size per chip: the denominators
#  of the IDE's "Sketch uses N bytes (P%)" lines. The inherited board lines
#  carry the M5Stack core's values, which describe an ESP-IDF/FreeRTOS image
#  and not what the FMP3 linker script allows, so the percentage misleads.
#  Rewritten in place on the board's own lines (the key is the same one the
#  source board sets), so a chip without a row here keeps the inherited
#  values byte for byte; the Xtensa boards have no row.
#
#  esp32c6 (stage 5, S5-1): the C6 port's esp32c6_xip.ld gives RAM
#  LENGTH = 0x4086E610 - 0x40800000 = 0x6E610 = 452112 bytes, against the
#  inherited 327680 (with which a Blink build read "91% used" while the
#  linker had 26 KB more than that in hand - docs/c6-port.md, stage 3/4).
#  upload.maximum_size stays the stock app0 partition, 0x140000 = 1310720,
#  which is what the board's default partition scheme gives the image;
#  stated here so the pair is explicit rather than half inherited.
UPLOAD_SIZE_OVERRIDES = {
    "esp32c6": {"upload.maximum_size": "1310720",
                "upload.maximum_data_size": "452112"},
    #  esp32c5 (C5 plan A9): the C5 port's esp32c5_xip.ld gives RAM
    #  LENGTH = 0x4084E5A0 - 0x40800000 = 0x4E5A0 = 320928 bytes (the
    #  bootloader's iram_loader_seg starts at 0x4084E5A0 on the C5, lower
    #  than the C6's 0x4086E610), against the inherited 327680 - here the
    #  inherited value is LARGER than what the linker allows, so without
    #  the override a sketch could read "95% used" and still fail to link.
    #  upload.maximum_size stays the stock app0 partition, 0x140000 =
    #  1310720 (the M5Stamp-C5's default partition scheme, same as the C6).
    "esp32c5": {"upload.maximum_size": "1310720",
                "upload.maximum_data_size": "320928"},
    #  esp32p4 (StampP4 plan P8): the P4 port's esp32p4_xip.ld gives RAM
    #  LENGTH = 0x4FF2CBD0 - 0x4FF00000 = 0x2CBD0 = 183,248 bytes (the
    #  bootloader's iram_loader_seg starts at 0x4ff2cbd0; the script keeps
    #  the app below it and does not use the sram_high region), against the
    #  inherited 327680 - larger than what the linker allows, the C5 case
    #  again. upload.maximum_size is the stock default_16MB scheme's app0,
    #  0x640000 = 6553600 (the M5Stamp-P4's default partition scheme).
    "esp32p4": {"upload.maximum_size": "6553600",
                "upload.maximum_data_size": "183248"},
}


def warn_if_boards_manager_shadowed(arduino_data: Path) -> None:
    """Say so when a Boards Manager copy occupies the same packager:arch.

    ★While a sketchbook platform exists at hardware/toppers/esp32, arduino-cli
    will not manage toppers:esp32 through Boards Manager at all: install,
    uninstall and search all behave as if the platform did not exist, and the
    error they give ("Platform 'toppers:esp32@x' not found") points at nothing.
    The reverse also bites - a Boards Manager copy wins over this one, so a
    freshly assembled platform is quietly not the one that gets built against.

    Both directions cost real time to work out from the symptom, so say it
    here rather than let the next person rediscover it.
    """
    installed = arduino_data / "packages" / "toppers" / "hardware" / "esp32"
    #  Only a directory that actually holds a platform counts. A leftover or
    #  renamed folder beside it is not one, and a warning that fires either
    #  way says nothing.
    versions = sorted(entry.name for entry in installed.glob("*")
                      if (entry / "boards.txt").is_file()) \
        if installed.is_dir() else []
    if not versions:
        return
    print()
    print("  Note: Boards Manager already has toppers:esp32 "
          + ", ".join(versions))
    print("  Both live under the same packager:architecture, and while this")
    print("  sketchbook platform exists arduino-cli refuses to install,")
    print("  uninstall or find the Boards Manager one - it reports the")
    print("  platform as not found. The IDE will build against THIS copy.")
    print("  To go back to the released package, remove "
          "<sketchbook>/hardware/toppers")

def default_sketchbook() -> Path:
    """arduino-cli's default user directory. Linux does not use Documents."""
    if sys.platform.startswith("linux"):
        return Path.home() / "Arduino"
    return Path.home() / "Documents" / "Arduino"


def remove_installed_platform(platform_root: Path) -> None:
    """Delete a previous install, but only one this script recognises."""
    if not platform_root.exists():
        return
    if not (platform_root / MARKER).is_file():
        raise SystemExit(
            f"Refusing to remove an unrecognized platform directory: "
            f"{platform_root}")
    #  Junctions are no longer created, but an install from an
    #  earlier version may still have them. Detaching first keeps the recursive
    #  delete from following the link into the M5Stack platform.
    for name in ("cores", "libraries", "tools", "variants"):
        legacy = platform_root / name
        if legacy.is_symlink() or (legacy.exists() and os.path.islink(legacy)):
            legacy.unlink()
            continue
        if legacy.is_dir() and sys.platform == "win32":
            #  A Windows junction is a directory reparse point; rmdir detaches
            #  it without touching the target.
            if os.readlink.__module__ and _is_reparse_point(legacy):
                os.rmdir(legacy)
    shutil.rmtree(platform_root)


def _is_reparse_point(path: Path) -> bool:
    FILE_ATTRIBUTE_REPARSE_POINT = 0x400
    try:
        return bool(path.lstat().st_file_attributes  # type: ignore[attr-defined]
                    & FILE_ATTRIBUTE_REPARSE_POINT)
    except (AttributeError, OSError):
        return False


def board_lines(source_boards: Path, board_id: str,
                stage_root: Path) -> tuple[list[str], list[str]]:
    """Derive our board definition from the M5Stack one.

    Returns the menu declarations (shared by every board in a platform, so the
    caller emits them once) and the lines for this one board.
    """
    chip, source_id, display_name, variant = BOARDS[board_id]
    source_prefix = source_id + "."
    prefix = board_id + "."

    kept = []
    for line in source_boards.read_text(encoding="utf-8").splitlines():
        if line.startswith("menu."):
            kept.append(line)
        elif line.startswith(source_prefix):
            kept.append(prefix + line[len(source_prefix):])

    adjusted = []
    for line in kept:
        if line.startswith(prefix + "name="):
            adjusted.append(f"{prefix}name={display_name}")
        #  Arduino's core reference, so build.core.path and build.variant.path
        #  resolve into the M5Stack platform while runtime.platform.path stays
        #  ours - which is what the fmp3-tools and fmp3-prebuilt references
        #  rely on. This replaced NTFS junctions, which exist only on Windows
        #  and which a Boards Manager package cannot create at all.
        elif line.startswith(prefix + "build.core="):
            adjusted.append(f"{prefix}build.core=m5stack:esp32")
        elif line.startswith(prefix + "build.variant="):
            adjusted.append(f"{prefix}build.variant=m5stack:{variant}")
        else:
            adjusted.append(line)

    menus = [line for line in adjusted if line.startswith("menu.")]
    board = [line for line in adjusted if line.startswith(prefix)]
    if not board:
        raise SystemExit(
            f"{source_boards} has no board '{source_id}' to derive from")

    #  Menus this board must not offer (BOARD_DROP_MENUS): every
    #  <board>.menu.<name>.* line goes; a name that matches no line is an
    #  error, so a renamed upstream menu cannot leave the dangerous entries
    #  in silently.
    for menu_name in sorted(BOARD_DROP_MENUS.get(board_id, set())):
        marker = f"{prefix}menu.{menu_name}."
        kept_lines = [line for line in board if not line.startswith(marker)]
        if len(kept_lines) == len(board):
            raise SystemExit(
                f"{source_boards}: '{source_id}' has no menu '{menu_name}' "
                f"to drop for {board_id}")
        board = kept_lines

    #  Board-level overrides: replace the inherited line of the same key, in
    #  place, so the board keeps one definition per key and the
    #  menu.PartitionScheme lines that follow still override
    #  upload.maximum_size for the schemes that set it, as they do upstream.
    #  A key the source board does not set is appended; a key it does set
    #  must be found exactly once, or the override would silently not apply.
    #  Two sources of these: the chip's size denominators
    #  (UPLOAD_SIZE_OVERRIDES) and this board's own properties
    #  (BOARD_BUILD_OVERRIDES).
    overrides = dict(UPLOAD_SIZE_OVERRIDES.get(chip, {}))
    overrides.update(BOARD_BUILD_OVERRIDES.get(board_id, {}))
    for key, value in overrides.items():
        marker = f"{prefix}{key}="
        hits = [i for i, line in enumerate(board) if line.startswith(marker)]
        if len(hits) > 1:
            raise SystemExit(
                f"{source_boards} sets {marker} {len(hits)} times for "
                f"'{source_id}'; cannot tell which line to override")
        if hits:
            board[hits[0]] = f"{marker}{value}"
        else:
            board.append(f"{marker}{value}")
    lines = board + [
        #  Which chip's stages this board links against; the layout is
        #  fmp3-prebuilt/<chip>/<profile>.
        f"{prefix}build.toppers_chip={chip}",
    ]
    if chip in SIZE_REGEX_OVERRIDES:
        text_regex, data_regex = SIZE_REGEX_OVERRIDES[chip]
        lines.append(f"{prefix}recipe.size.regex={text_regex}")
        lines.append(f"{prefix}recipe.size.regex.data={data_regex}")
    entries = list(MENU_ENTRIES)
    if (stage_root / EXPERIMENTAL_ENTRY[2]).is_dir():
        entries.append(EXPERIMENTAL_ENTRY)
    entries.extend(CHIP_ONLY_ENTRIES.get(chip, []))
    skip = BOARD_SKIP_ENTRIES.get(board_id, set())
    for key, label, profile in entries:
        if key in skip:
            continue
        if not (stage_root / profile).is_dir():
            continue
        lines.append(f"{prefix}menu.FMP3Runtime.{key}={label}")
        lines.append(f"{prefix}menu.FMP3Runtime.{key}"
                     f".build.toppers_profile={profile}")
        #  A separate key, not build.defines: the PSRAM menu already sets
        #  build.defines, and two menus writing one key lose each other.
        lines.append(f"{prefix}menu.FMP3Runtime.{key}"
                     f".build.toppers_profile_macro={profile_macro(profile)}")
    return menus, lines


#  M5Stack core 3.3.9 routes every upload through tools/flasher.{py,exe},
#  an esptool wrapper that reuses <name>_flashed.bin reference images to
#  write only the changed flash ranges. The wrapper is reached through
#  {runtime.platform.path}, which for a board of THIS platform is this
#  platform - and the wrapper lives in the M5Stack core, so the upload
#  fails with "flasher.exe is missing" the first time a user presses
#  Upload. Shipping a copy is not an option either: the non-Windows line
#  runs it with python3, and requiring Python on the user's machine is the
#  one thing the prebuilt stages exist to avoid. So these three patterns
#  call esptool directly, the way the core did before 3.3.9. What is lost
#  is the reflash speed-up, not correctness: the wrapper's only effect is
#  which ranges esptool skips. --build-dir goes with it (a flasher
#  argument, not an esptool one).
UPLOAD_PATTERNS_WITHOUT_FLASHER = {
    "tools.esptool_py.upload.pattern":
        '"{path}/{cmd}" {upload.pattern_args}',
    "tools.esptool_py.program.pattern":
        '"{path}/{cmd}" {program.pattern_args}',
    "tools.esptool_py_app_only.upload.pattern":
        '"{path}/{cmd}" {tools.esptool_py_app_only.upload.pattern_args}',
}


#  Reached through {runtime.platform.path} by a line this platform keeps, but
#  deliberately not shipped, because the feature behind it is not offered.
#  Each entry is a path relative to the platform root, truncated at its first
#  {placeholder} segment the way unshipped_platform_references() truncates.
#  A reference NOT listed here and NOT present is what the guard exists to
#  catch: that is how the 3.3.9 flasher would have been found at install time
#  instead of when a user first pressed Upload.
KNOWN_UNSHIPPED = {
    #  OTA upload. Shipping espota would not make OTA work: it pushes to an
    #  ArduinoOTA responder running on the board, and there is none. The
    #  port exposes no listening socket (the Wi-Fi API stops at one
    #  client request), no mDNS, and no esp_ota_* / libapp_update.a in any
    #  stage; the core's ArduinoOTA cannot be borrowed because the core
    #  runtime is not linked. The partition table has ota_0/ota_1/otadata,
    #  which is the container and nothing else. README.md says so under
    #  its constraints - not under "unverified", which it used to, and
    #  which reads as "might work if you try it".
    "tools/espota.py",
    "tools/espota.exe",
    #  ESP Insights: no recipe of this platform produces its input.
    "tools/gen_insights_package.exe",
    #  The Windows-only partition tool the driver replaces on every host;
    #  the recipe that used it is overridden and the guard above proves it.
    "tools/gen_esp32part.exe",
    #  SVD files for the IDE debugger, which this platform does not support.
    "tools/ide-debug/svd",
}

PLATFORM_PATH_REFERENCE = re.compile(
    r"\{runtime\.platform\.path\}([\\/][^\"'\s]*)?")


def unshipped_platform_references(lines: list[str],
                                  platform_root: Path) -> set[str]:
    """Paths reached through {runtime.platform.path} that are not there.

    A reference may end in a {placeholder} segment ({build.mcu}.svd, the
    stage directory named by the menu), so the check stops at the first
    segment that holds one and asks for the directory above it. That is
    enough: a missing parent is the failure being looked for, and the
    placeholder's own values are checked where they are produced.
    """
    missing = set()
    for line in lines:
        for match in PLATFORM_PATH_REFERENCE.finditer(line):
            tail = (match.group(1) or "").strip("\\/")
            if not tail:
                continue
            segments = tail.replace("\\", "/").split("/")
            kept = []
            for segment in segments:
                if "{" in segment:
                    break
                kept.append(segment)
            if not kept:
                continue
            reference = "/".join(kept)
            if reference in KNOWN_UNSHIPPED:
                continue
            if not (platform_root / reference).exists():
                missing.add(reference)
    return missing


def platform_lines(source: Path, link: str, objcopy: str,
                   partitions: str) -> list[str]:
    out = []
    for line in source.read_text(encoding="utf-8").splitlines():
        #  Keyed on the whole "<key>=" so that upload.pattern_args, which is
        #  a different property and is kept as it is, does not match.
        replacement = next(
            (value for key, value in UPLOAD_PATTERNS_WITHOUT_FLASHER.items()
             if line.startswith(key + "=")), None)
        if replacement is not None:
            out.append(f"{line.split('=', 1)[0]}={replacement}")
        elif line.startswith("tools.flasher."):
            #  Dropped, not kept-but-unused: the definitions name paths under
            #  {runtime.platform.path} that are not here, and leaving them
            #  would make the reference guard below report files no line
            #  actually runs. A pattern that starts using the wrapper again
            #  is caught by the {tools.flasher.cmd} check instead.
            continue
        elif line.startswith("name="):
            out.append("name=M5Stack Arduino with TOPPERS/FMP3")
        elif line.startswith("build.extra_flags="):
            #  Only the unsuffixed key: build.extra_flags.<mcu> is a different
            #  property and the trailing '=' keeps it out. TOPPERS_FMP3_RUNTIME
            #  _SELECTED says the mechanism is present at all, so an example
            #  built against a platform older than this one is not rejected by
            #  a guard the platform cannot answer.
            out.append(line + " -D{build.toppers_profile_macro}"
                              " -DTOPPERS_FMP3_RUNTIME_SELECTED=1")
        elif line.startswith("recipe.c.combine.pattern="):
            out.append(f"recipe.c.combine.pattern={link}")
        elif line.startswith("recipe.objcopy.bin.pattern="):
            out.append(f"recipe.objcopy.bin.pattern={objcopy}")
        elif line.startswith("recipe.objcopy.partitions.bin.pattern="):
            out.append(f"recipe.objcopy.partitions.bin.pattern={partitions}")
        elif line.startswith("recipe.size.regex="):
            #  The inherited regex matches ESP-IDF section names, which the FMP3
            #  link does not produce, so the IDE reported 0 bytes used. These are
            #  the sections of ports/.../runtime/ld.
            out.append(r"recipe.size.regex=^(?:\.iram_boot|\.flash_text"
                       r"|\.flash_rodata)\s+([0-9]+).*")
        elif line.startswith("recipe.size.regex.data="):
            out.append(r"recipe.size.regex.data=^(?:\.data|\.bss|\.kernel_bss"
                       r"|\.diag_noinit)\s+([0-9]+).*")
        else:
            out.append(line)
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library-root", default="")
    parser.add_argument("--sketchbook", default="")
    parser.add_argument("--arduino-data", default="")
    parser.add_argument("--core-version", default="3.3.9")
    parser.add_argument("--prebuilt-stage-root", default="",
                        help="stages from build_prebuilt_stages.py; required")
    parser.add_argument("--python-executable", default="",
                        help="interpreter for the driver recipe; a frozen "
                             "per-OS build replaces it in the released package")
    #  Repeatable. Without it, a stage root laid out per chip installs every
    #  chip it holds; a single chip's stage root installs esp32s3.
    parser.add_argument("--chip", action="append",
                        choices=list(dict.fromkeys(
                            entry[0] for entry in BOARDS.values())),
                        default=None)
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args(argv)
    args.chip_given = args.chip is not None
    if not args.chip_given:
        args.chip = ["esp32s3"]

    library_root = Path(args.library_root).resolve() if args.library_root \
        else Path(__file__).resolve().parent.parent
    if not (library_root / "library.properties").is_file():
        raise SystemExit(
            f"library.properties was not found under {library_root}")

    sketchbook = Path(args.sketchbook).resolve() if args.sketchbook \
        else default_sketchbook().resolve()
    platform_root = (sketchbook / "hardware" / "toppers" / "esp32").resolve()
    allowed = (sketchbook / "hardware" / "toppers").resolve()
    if not str(platform_root).lower().startswith(str(allowed).lower() + os.sep):
        raise SystemExit(
            f"Platform target escapes the expected sketchbook location: "
            f"{platform_root}")

    if args.uninstall:
        remove_installed_platform(platform_root)
        print(f"Removed TOPPERS/FMP3 Arduino board platform: {platform_root}")
        return 0

    if not args.prebuilt_stage_root:
        raise SystemExit(
            "--prebuilt-stage-root is required. Build the stages first with "
            "scripts/build_prebuilt_stages.py.")
    stage_root = Path(args.prebuilt_stage_root).resolve()
    if not stage_root.is_dir():
        raise SystemExit(f"Prebuilt stage root was not found: {stage_root}")

    #
    #  Which chips to install, and where each one's stages are.
    #
    #  build_prebuilt_stages.py writes build/prebuilt/<chip>/<profile>, so
    #  pointing at build/prebuilt installs every chip built there - both boards
    #  in one platform, which is how a release should look. Pointing at one
    #  chip's own directory still installs just that board, which is what the
    #  older invocations do.
    #
    chips_with_boards = {entry[0] for entry in BOARDS.values()}
    per_chip = {name: stage_root / name
                for name in sorted(chips_with_boards)
                if (stage_root / name).is_dir()}
    if per_chip:
        if args.chip_given:
            per_chip = {c: r for c, r in per_chip.items() if c in args.chip}
            missing_chip = sorted(set(args.chip) - set(per_chip))
            if missing_chip:
                raise SystemExit(
                    "no stages for " + ", ".join(missing_chip)
                    + f" below {stage_root}")
    else:
        #  A single chip's stage root, named by --chip (default esp32s3).
        per_chip = {args.chip[0]: stage_root}
    chips = sorted(per_chip)

    python_executable = args.python_executable or sys.executable
    if not Path(python_executable).exists():
        raise SystemExit(f"Python was not found: {python_executable}")

    driver_source = library_root / "scripts" / "fmp3_link.py"
    if not driver_source.is_file():
        driver_source = library_root / "extras" / "tools" / "fmp3_link.py"
    if not driver_source.is_file():
        raise SystemExit("fmp3_link.py was not found in the library.")

    try:
        sdk = resolve(Path(args.arduino_data) if args.arduino_data else None,
                      args.core_version)
    except SdkError as error:
        raise SystemExit(str(error))
    source_platform = Path(sdk["coreRoot"])
    source_boards = source_platform / "boards.txt"
    source_platform_file = source_platform / "platform.txt"
    #  Validate everything that can fail BEFORE the existing platform is
    #  removed. Removing first meant a bad argument left a half-deleted
    #  platform behind, and because the marker went with it the next run
    #  refused to continue.
    for required in (source_boards, source_platform_file,
                     source_platform / "programmers.txt"):
        if not required.is_file():
            raise SystemExit(
                f"M5Stack platform input was not found: {required}")

    remove_installed_platform(platform_root)
    platform_root.mkdir(parents=True, exist_ok=True)
    #  Claim the directory before anything is written into it. Not everything
    #  that can fail is checkable up front - the reference guard below needs
    #  the assembled platform to check against - and a failure after this
    #  point used to leave a directory with no marker, which the NEXT run
    #  then refused to remove. The final marker overwrites this one.
    (platform_root / MARKER).write_text(
        json.dumps({"package": "ToppersFMP3-M5Stack",
                    "state": "incomplete"}, indent=2) + "\n",
        encoding="utf-8")

    #  Only tools/ has to exist here, because it is reached through
    #  runtime.platform.path. gen_esp32part.exe is NOT copied: with prebuilt
    #  stages the partition recipe goes through the driver on every host, and it
    #  was a Windows-only binary in a package whose point is running everywhere.
    tools_destination = platform_root / "tools"
    tools_destination.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_platform / "tools" / "partitions",
                    tools_destination / "partitions")
    source_tool = source_platform / "tools" / "gen_esp32part.py"
    if source_tool.is_file():
        shutil.copy2(source_tool, tools_destination)

    #  The menu.* declarations are platform-wide, so they are written once and
    #  each board's own lines follow. Two boards that both offer FMP3Runtime
    #  would otherwise declare the menu twice.
    menu_lines: list[str] = []
    board_blocks: list[str] = []
    #  Boards, not chips: two boards can share one chip's stages.
    for board_id in boards_for_chips(chips):
        menus, board = board_lines(source_boards, board_id,
                                   per_chip[BOARDS[board_id][0]])
        for line in menus:
            if line not in menu_lines:
                menu_lines.append(line)
        board_blocks.extend(board)
    (platform_root / "boards.txt").write_text(
        "\n".join(menu_lines + ["menu.FMP3Runtime=FMP3 Runtime"] + board_blocks)
        + "\n", encoding="utf-8", newline="\r\n")

    platform_tools = platform_root / "fmp3-tools"
    platform_tools.mkdir(parents=True, exist_ok=True)
    shutil.copy2(driver_source, platform_tools)
    #  Copy the stages the board menu names, and only those. Taking whatever
    #  directory happens to be under the stage root ships anything left there:
    #  build_prebuilt_stages.py does not clean its output, so the retired
    #  wifi-scan profile was still on disk and went into the platform - 658 KB
    #  of a profile no menu entry can select, packaged and distributed. What a
    #  release contains has to follow from what the board offers, not from what
    #  a build directory still holds.
    staged = 0
    skipped = []
    for chip in chips:
        chip_root = per_chip[chip]
        offered = {profile for _, _, profile in MENU_ENTRIES}
        if (chip_root / EXPERIMENTAL_ENTRY[2]).is_dir():
            offered.add(EXPERIMENTAL_ENTRY[2])
        offered.update(profile for _, _, profile in CHIP_ONLY_ENTRIES.get(chip, []))
        present = {p.name for p in chip_root.iterdir() if p.is_dir()}
        for stage in sorted(p for p in chip_root.iterdir() if p.is_dir()):
            if not (stage / "link-manifest.json").is_file():
                continue
            if stage.name not in offered:
                skipped.append(f"{chip}/{stage.name}")
                continue
            shutil.copytree(
                stage, platform_root / "fmp3-prebuilt" / chip / stage.name)
            staged += 1
        missing = sorted(EXPECTED_PROFILES[chip] - present)
        if missing:
            raise SystemExit(
                f"the {chip} board offers profiles with no stage built: "
                + ", ".join(missing)
                + "\nbuild them first: build_prebuilt_stages.py"
                + f" --chip {chip} --profiles {' '.join(missing)}")
    if staged == 0:
        raise SystemExit(f"No prebuilt stage was found below {stage_root}")
    for name in skipped:
        print(f"  skipped stage {name}: no menu entry selects it")

    #  Everything the recipe needs is inside the platform, so the recipe can be
    #  written with Arduino variables only - which is what makes it portable.
    driver_prefix = (f'"{python_executable}" '
                     '"{runtime.platform.path}/fmp3-tools/fmp3_link.py"')
    recipe_base = (
        f"{driver_prefix} "
        '--stage "{runtime.platform.path}/fmp3-prebuilt/'
        '{build.toppers_chip}/{build.toppers_profile}" '
        '--build-path "{build.path}" '
        '--project-name "{build.project_name}" '
        '--gcc "{compiler.path}{compiler.c.cmd}" '
        '--esptool "{tools.esptool_py.path}/{tools.esptool_py.cmd}" '
        '--sdk-ld "{compiler.sdk.path}/ld" '
        '--sdk-lib "{compiler.sdk.path}/lib" '
        #  The board's own flash size, not the stage's; see fmp3_link.py.
        '--flash-size "{build.flash_size}"')
    #  The inherited partition recipe runs "python3 gen_esp32part.py" on every
    #  host except Windows, which would put a Python requirement back on
    #  macOS and Linux after the driver was frozen to remove it.
    partitions_recipe = (f"{driver_prefix} --partitions "
                         '"{build.path}/partitions.csv" '
                         '"{build.path}/{build.project_name}.partitions.bin"')

    lines = platform_lines(source_platform_file, recipe_base,
                           f"{recipe_base} --check-only", partitions_recipe)
    #  gen_esp32part.exe is not shipped, so no recipe may depend on it. This
    #  catches the override silently ceasing to apply, which would otherwise
    #  surface as a missing file during a sketch build on Windows.
    dangling = [line for line in lines
                if line.startswith("recipe.")
                and "{tools.gen_esp32part.cmd}" in line]
    if dangling:
        raise SystemExit(
            "A recipe still uses gen_esp32part, which this platform does not "
            "ship: " + "; ".join(dangling))
    still_wrapped = [line for line in lines
                     if "{tools.flasher.cmd}" in line]
    if still_wrapped:
        raise SystemExit(
            "A pattern still goes through the M5Stack flasher wrapper, which "
            "this platform does not ship. Add its key to "
            "UPLOAD_PATTERNS_WITHOUT_FLASHER: " + "; ".join(still_wrapped))
    missing = unshipped_platform_references(lines, platform_root)
    if missing:
        raise SystemExit(
            "The platform.txt inherited from M5Stack core "
            f"{args.core_version} reaches files through "
            "{runtime.platform.path} that this platform does not ship. Either "
            "ship them, rewrite the line, or - if the feature is one this "
            "platform does not offer - name them in KNOWN_UNSHIPPED:\n  "
            + "\n  ".join(sorted(missing)))

    (platform_root / "platform.txt").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\r\n")
    shutil.copy2(source_platform / "programmers.txt", platform_root)

    (platform_root / MARKER).write_text(json.dumps({
        "package": "ToppersFMP3-M5Stack",
        "installedAt": datetime.datetime.now(
            datetime.timezone.utc).isoformat(),
        "libraryRoot": str(library_root),
        "sourcePlatform": str(source_platform),
        "coreVersion": args.core_version,
        "chips": chips,
    }, indent=2) + "\n", encoding="utf-8")

    #  <data>/packages/m5stack, so two levels up is the data directory.
    warn_if_boards_manager_shadowed(
        Path(sdk['packageRoot']).parent.parent)

    print("\nTOPPERS/FMP3 Arduino board platform installed.")
    print(f"  Platform: {platform_root}")
    for board_id in boards_for_chips(chips):
        chip, _, display_name, _ = BOARDS[board_id]
        print(f"  Board:    {display_name}  ({chip})")
    print(f"  Stages:   {staged}")
    print("Restart Arduino IDE before selecting the board.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
