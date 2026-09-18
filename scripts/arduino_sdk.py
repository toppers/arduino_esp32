#!/usr/bin/env python3
"""Locate and validate a chip's SDK bundled with the M5Stack Arduino core.

A port of Resolve-ArduinoEsp32S3Sdk.ps1, which every build
script goes through and which was the single thing tying them to Windows: it
took the Arduino data directory to be %LOCALAPPDATA%\\Arduino15 and nothing
else. That location is per-OS, so no amount of fixing the callers would have
helped while the resolver itself only knew one of the three.

The cross-platform parts of this repository are Python already - the link
driver, the package index, the verifier, the duplicate-symbol audit - so this
follows them rather than adding a PowerShell 7 dependency for developers.

Used as a module by the Python build scripts, and as a command by the
PowerShell ones:

    python scripts/arduino_sdk.py --as-json

Exit status is 0 when the SDK is complete, 1 when something it needs is
missing, naming the item.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

DEFAULT_CORE_VERSION = "3.3.9"

#  The one header that differs by architecture, keyed by chip: the item name
#  the error message uses, and its path below <sdk>/include/. The Xtensa rows
#  are the path resolve() used to build from the chip name; the RISC-V SDK has
#  no include/xtensa at all, so the C6 and C5 rows name the header that
#  proves their own arch tree is there (riscv/include/riscv/csr.h, checked
#  against esp32c6-libs / esp32c5-libs 3.3.9).
ARCH_HEADERS = {
    "esp32s3": ("xtensaCoreIsa",
                ("xtensa", "esp32s3", "include", "xtensa", "config",
                 "core-isa.h")),
    "esp32": ("xtensaCoreIsa",
              ("xtensa", "esp32", "include", "xtensa", "config",
               "core-isa.h")),
    "esp32c6": ("riscvCsr", ("riscv", "include", "riscv", "csr.h")),
    #  Same layout as the C6 (checked against esp32c5-libs 3.3.9).
    "esp32c5": ("riscvCsr", ("riscv", "include", "riscv", "csr.h")),
    #  ESP32-P4 (RISC-V, dual core). Same include layout (checked against
    #  esp32p4_es-libs 3.3.9).
    "esp32p4": ("riscvCsr", ("riscv", "include", "riscv", "csr.h")),
}

#  The M5Stack core's SDK tool directory of a chip, below
#  <arduino data>/packages/m5stack/tools/. For every chip so far it was
#  "<chip>-libs", which is what resolve() used to build from the chip name.
#  The ESP32-P4 is the first chip whose SDK is not named after the chip:
#  the core ships two P4 SDKs, esp32p4_es-libs for silicon below revision
#  v3 (boards.txt 3.3.9 m5stack_stamp_p4.build.chip_variant=esp32p4_es,
#  the default; the ChipVariant menu offers esp32p4 for v3 and later), and
#  the stages are built against the one the board's default selects
#  (StampP4 plan P2). A chip absent here uses "<chip>-libs" as before.
SDK_TOOL_NAMES = {
    "esp32p4": "esp32p4_es-libs",
}

#  Archives resolve() requires beyond the chip-neutral set, keyed by chip;
#  (item name, path below <sdk>/lib or, for libphy.a, <sdk>/ld). The set
#  below is the one resolve() required for every chip before the P4 row
#  existed, so the other chips' checks are unchanged. The ESP32-P4 has no
#  radio: its SDK has no libphy.a and no libcoexist.a (the Wi-Fi it links is
#  esp_wifi_remote over esp_hosted), so its row leaves the two out.
_RADIO_ARCHIVES = (
    ("wifiArchive", ("lib", "libesp_wifi.a")),
    ("coexistArchive", ("lib", "libcoexist.a")),
    ("phyArchive", ("ld", "libphy.a")),
)
CHIP_ARCHIVES = {
    "esp32s3": _RADIO_ARCHIVES,
    "esp32": _RADIO_ARCHIVES,
    "esp32c6": _RADIO_ARCHIVES,
    "esp32c5": _RADIO_ARCHIVES,
    "esp32p4": (
        ("wifiArchive", ("lib", "libesp_wifi.a")),
    ),
}


def sdk_tool_name(chip: str) -> str:
    """The SDK tool directory name of a chip (see SDK_TOOL_NAMES)."""
    return SDK_TOOL_NAMES.get(chip, f"{chip}-libs")


class SdkError(RuntimeError):
    """The SDK is absent or incomplete; the message names what is missing."""


def default_arduino_data() -> Path:
    """Where arduino-cli keeps packages/, per OS.

    Windows uses %LOCALAPPDATA%\\Arduino15, macOS ~/Library/Arduino15 and Linux
    ~/.arduino15. Hardcoding the Windows one is what made every build script
    Windows-only.
    """
    override = os.environ.get("ARDUINO_DIRECTORIES_DATA")
    if override:
        return Path(override)
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA")
        if not local:
            raise SdkError("LOCALAPPDATA is unavailable; pass --arduino-data")
        return Path(local) / "Arduino15"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Arduino15"
    return Path.home() / ".arduino15"


def resolve(arduino_data: Path | None = None,
            core_version: str = DEFAULT_CORE_VERSION,
            chip: str = "esp32s3") -> dict:
    """Return the SDK layout, having checked every part the build needs.

    The M5Stack core ships one of these trees per chip, laid out identically
    and named for the chip both in the tool directory and in the linker
    scripts inside it, so the chip is the only thing that varies here - apart
    from the architecture's own include tree, which ARCH_HEADERS names.
    """
    if chip not in ARCH_HEADERS:
        raise SdkError(f"unknown chip {chip!r}; known: "
                       + ", ".join(ARCH_HEADERS))
    arch_item, arch_header = ARCH_HEADERS[chip]
    data = Path(arduino_data) if arduino_data else default_arduino_data()

    package_root = data / "packages" / "m5stack"
    core_root = package_root / "hardware" / "esp32" / core_version
    sdk_root = package_root / "tools" / sdk_tool_name(chip) / core_version
    include_root = sdk_root / "include"
    library_root = sdk_root / "lib"
    linker_root = sdk_root / "ld"
    versions_file = sdk_root / "versions.txt"

    #  The same set the PowerShell resolver checked. Checking them here rather
    #  than letting the compiler fail later is deliberate: a missing archive
    #  surfaces as an undefined reference hundreds of lines into a link.
    required = {
        "core": core_root,
        "sdk": sdk_root,
        "versions": versions_file,
        "idfVersionHeader":
            include_root / "esp_common" / "include" / "esp_idf_version.h",
        arch_item: include_root.joinpath(*arch_header),
        "peripheralLinkerScript": linker_root / f"{chip}.peripherals.ld",
        "romLinkerScript": linker_root / f"{chip}.rom.ld",
        "socArchive": library_root / "libsoc.a",
        "lwipArchive": library_root / "liblwip.a",
        "mbedtlsArchive": library_root / "libmbedtls.a",
    }
    for name, (subdir, file_name) in CHIP_ARCHIVES[chip]:
        required[name] = sdk_root / subdir / file_name
    for name, path in required.items():
        if not path.exists():
            raise SdkError(
                f"M5Stack Arduino SDK item is missing ({name}): {path}")

    text = versions_file.read_text(encoding="utf-8", errors="replace")
    idf_line = next((line for line in text.splitlines()
                     if re.match(r"^esp-idf:\s+", line)), None)
    if idf_line is None:
        raise SdkError(f"ESP-IDF version was not found in {versions_file}")
    match = re.search(r"v\d+\.\d+\.\d+", idf_line)
    if not match:
        raise SdkError(f"Could not parse ESP-IDF version from: {idf_line}")

    #  Key names match the PowerShell resolver's output so that callers - which
    #  consume it as JSON - do not care which one produced it.
    return {
        "arduinoData": str(data.resolve()),
        "package": "m5stack:esp32",
        "packageRoot": str(package_root.resolve()),
        "coreVersion": core_version,
        "coreRoot": str(core_root.resolve()),
        "sdkRoot": str(sdk_root.resolve()),
        "espIdfVersion": match.group(0),
        "includeRoot": str(include_root.resolve()),
        "libraryRoot": str(library_root.resolve()),
        "linkerScriptRoot": str(linker_root.resolve()),
    }


def tool_executable(package_root: Path, tool: str, name: str) -> Path:
    """Find a bundled tool, taking the newest version directory.

    The .exe suffix is added only on Windows; the PowerShell helpers this
    replaces assumed it everywhere.
    """
    root = Path(package_root) / "tools" / tool
    if not root.is_dir():
        raise SdkError(f"tool directory is missing: {root}")
    suffix = ".exe" if sys.platform == "win32" else ""
    #  Highest-sorting version directory wins, matching Find-ToolFile.
    for version in sorted((d for d in root.iterdir() if d.is_dir()),
                          key=lambda d: d.name, reverse=True):
        for candidate in version.rglob(name + suffix):
            if candidate.is_file():
                return candidate
    raise SdkError(f"{name} was not found under {root}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arduino-data", default="",
                        help="Arduino data directory holding packages/ "
                             "(default: this OS's location)")
    parser.add_argument("--core-version", default=DEFAULT_CORE_VERSION)
    parser.add_argument("--chip", choices=list(ARCH_HEADERS),
                        default="esp32s3")
    parser.add_argument("--as-json", action="store_true",
                        help="print the layout as JSON")
    args = parser.parse_args(argv)

    try:
        result = resolve(Path(args.arduino_data) if args.arduino_data else None,
                         args.core_version, args.chip)
    except SdkError as error:
        print(f"arduino_sdk: {error}", file=sys.stderr)
        return 1

    if args.as_json:
        print(json.dumps(result, indent=2))
    else:
        for key, value in result.items():
            print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
