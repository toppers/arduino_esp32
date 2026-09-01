#!/usr/bin/env python3
"""Locate and validate the ESP32-S3 SDK bundled with the M5Stack Arduino core.

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

DEFAULT_CORE_VERSION = "3.3.8"


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
    scripts inside it, so the chip is the only thing that varies here.
    """
    data = Path(arduino_data) if arduino_data else default_arduino_data()

    package_root = data / "packages" / "m5stack"
    core_root = package_root / "hardware" / "esp32" / core_version
    sdk_root = package_root / "tools" / f"{chip}-libs" / core_version
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
        "xtensaCoreIsa":
            include_root / "xtensa" / chip / "include" / "xtensa"
            / "config" / "core-isa.h",
        "peripheralLinkerScript": linker_root / f"{chip}.peripherals.ld",
        "romLinkerScript": linker_root / f"{chip}.rom.ld",
        "socArchive": library_root / "libsoc.a",
        "wifiArchive": library_root / "libesp_wifi.a",
        "coexistArchive": library_root / "libcoexist.a",
        "phyArchive": linker_root / "libphy.a",
        "lwipArchive": library_root / "liblwip.a",
        "mbedtlsArchive": library_root / "libmbedtls.a",
    }
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
    parser.add_argument("--as-json", action="store_true",
                        help="print the layout as JSON")
    args = parser.parse_args(argv)

    try:
        result = resolve(Path(args.arduino_data) if args.arduino_data else None,
                         args.core_version)
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
