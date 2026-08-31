#!/usr/bin/env python3
"""Link a prebuilt FMP3 stage with Arduino objects into the XIP image.

The final image is produced with nothing but the
toolchain and esptool that ship with the M5Stack Arduino core, so a sketch build
needs neither CMake, Ninja nor a cfg generator on the user's machine.

The input is a stage produced by ports/esp32s3_m5cores3/runtime/cmake/
prebuilt_stage.cmake: objs/, ld/, optionally lib/, and link-manifest.json.

Replaces the PowerShell prototype this grew out of, because the recipe has to
run on Windows, macOS and Linux. Verified byte identical against the CMake path
for all five profiles.

Two rules the manifest exists to pin down:

* Link order is ordinal (byte-wise, case sensitive), matching CMake's
  list(SORT). Sorting the same names with a locale-aware or case-insensitive
  comparison yields a different image.
* Machine-specific paths are placeholders expanded here, not baked into the
  distributed stage.

Paths to the toolchain and the SDK are normally passed in, because the Arduino
recipe already knows them. When they are omitted the script falls back to
locating the M5Stack package under the Arduino data directory, which is what
the local regression scripts do.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path


#  The frozen driver is shipped as a versioned Arduino tool, and a
#  package whose tool version has drifted from the script it was frozen from is
#  a real hazard: the manifest schema the driver understands is what decides
#  whether a stage links at all. --version lets the release check compare the
#  binary it is about to publish against this source.
DRIVER_VERSION = "1"

MANIFEST_SCHEMA = 1
EXE = ".exe" if os.name == "nt" else ""


class LinkError(Exception):
    """Anything that should stop the link with a readable message."""


def arduino_data_default() -> Path:
    """Default Arduino data directory (where packages/ lives)."""
    if os.name == "nt":
        local = os.environ.get("LOCALAPPDATA")
        if not local:
            raise LinkError("LOCALAPPDATA is unset; pass --arduino-data")
        return Path(local) / "Arduino15"
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Arduino15"
    return Path.home() / ".arduino15"


def newest(pattern: str, what: str) -> Path:
    """Highest-sorting match, so a newer tool version wins."""
    hits = sorted(glob.glob(pattern))
    if not hits:
        raise LinkError(f"{what} was not found: {pattern}")
    return Path(hits[-1])


def existing_program(path: Path, what: str) -> Path:
    """Accept a program path with or without the Windows .exe suffix.

    Arduino hands out {compiler.c.cmd} without an extension even on Windows
    (unlike {tools.esptool_py.cmd}), because the OS appends it when executing.
    """
    candidates = [path]
    if os.name == "nt" and path.suffix.lower() != ".exe":
        candidates.append(path.with_name(path.name + ".exe"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise LinkError(f"{what} does not exist: {path}")


def resolve_sdk(args: argparse.Namespace) -> dict[str, Path]:
    """Fill in whatever the caller did not pass explicitly."""
    resolved: dict[str, Path] = {}
    package_root = None
    if not (args.gcc and args.esptool and args.sdk_ld and args.sdk_lib):
        data = Path(args.arduino_data) if args.arduino_data else arduino_data_default()
        package_root = data / "packages" / "m5stack"
        if not package_root.is_dir():
            raise LinkError(f"M5Stack package directory is missing: {package_root}")

    if args.gcc:
        resolved["gcc"] = Path(args.gcc)
    else:
        resolved["gcc"] = newest(
            str(package_root / "tools" / "esp-x32" / "*" / "bin"
                / f"xtensa-esp32s3-elf-gcc{EXE}"),
            "xtensa-esp32s3-elf-gcc")
    if args.esptool:
        resolved["esptool"] = Path(args.esptool)
    else:
        resolved["esptool"] = newest(
            str(package_root / "tools" / "esptool_py" / "*" / f"esptool{EXE}"),
            "esptool")
    if args.sdk_ld:
        resolved["sdk_ld"] = Path(args.sdk_ld)
    else:
        resolved["sdk_ld"] = (package_root / "tools" / "esp32s3-libs"
                              / args.core_version / "ld")
    if args.sdk_lib:
        resolved["sdk_lib"] = Path(args.sdk_lib)
    else:
        resolved["sdk_lib"] = (package_root / "tools" / "esp32s3-libs"
                               / args.core_version / "lib")

    for key in ("gcc", "esptool"):
        resolved[key] = existing_program(resolved[key], key)
    for key in ("sdk_ld", "sdk_lib"):
        if not resolved[key].is_dir():
            raise LinkError(f"{key} does not exist: {resolved[key]}")
    return resolved


def load_manifest(stage: Path) -> dict:
    manifest_path = stage / "link-manifest.json"
    if not manifest_path.is_file():
        raise LinkError(f"Prebuilt stage is incomplete: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if int(manifest.get("schema", 0)) != MANIFEST_SCHEMA:
        raise LinkError(f"Unsupported manifest schema: {manifest.get('schema')}")
    if manifest.get("paddrMode") != "runtime-mmu":
        raise LinkError(
            "This driver requires runtime PADDR resolution "
            f"(manifest={manifest.get('paddrMode')})")
    return manifest


def collect_arduino_objects(manifest: dict, build_path: Path,
                            project_name: str) -> list[Path]:
    sketch_object = build_path / "sketch" / f"{project_name}.cpp.o"
    if not sketch_object.is_file():
        raise LinkError(f"Arduino sketch object was not found: {sketch_object}")
    objects = [sketch_object]
    for name in manifest.get("requiredArduinoObjects", []):
        if name == "<sketch>.cpp.o":
            continue
        hits = list((build_path / "libraries").rglob(name))
        if len(hits) != 1:
            raise LinkError(
                f"Expected exactly one {name}, found {len(hits)}.")
        objects.append(hits[0])
    return objects


def stage_objects(stage: Path, work: Path,
                  arduino_objects: list[Path]) -> list[str]:
    """Copy the stage and the Arduino objects into one directory.

    Basename collisions inside the stage were already rejected when it was
    produced; a collision here can only come from an Arduino object.
    """
    objs = work / "objs"
    if work.exists():
        shutil.rmtree(work)
    objs.mkdir(parents=True)
    for source in sorted((stage / "objs").glob("*.o")):
        shutil.copyfile(source, objs / source.name)
    for source in arduino_objects:
        staged = re.sub(r"\.(c|cpp|S)\.o$", ".o", source.name)
        if staged == source.name:
            raise LinkError(f"Unexpected Arduino object name: {source.name}")
        destination = objs / staged
        if destination.exists():
            raise LinkError(f"Basename collision while staging: {staged}")
        shutil.copyfile(source, destination)

    #  Ordinal order. Plain sorted() on str is exactly that.
    names = sorted(path.name for path in objs.glob("*.o"))
    (work / "objects.rsp").write_text(
        "".join(f"objs/{name}\n" for name in names), encoding="ascii")
    return names


def expander(stage: Path, sdk: dict[str, Path]):
    replacements = {
        "@STAGE@": str(stage.resolve()),
        "@SDK_LD_ROOT@": str(sdk["sdk_ld"]),
        "@SDK_LIBRARY_ROOT@": str(sdk["sdk_lib"]),
        "@SDK_PERIPHERALS_LD@": str(sdk["sdk_ld"] / "esp32s3.peripherals.ld"),
    }

    def expand(value: str) -> str:
        for placeholder, actual in replacements.items():
            value = value.replace(placeholder, actual)
        return value

    return expand


def build_link_command(manifest: dict, stage: Path, sdk: dict[str, Path],
                       expand) -> list[str]:
    command = [
        str(sdk["gcc"]),
        "-nostdlib",
        "-mlongcalls",
        "-Wl,--gc-sections",
        "-Wl,--allow-multiple-definition",
        "-Wl,-T," + str(stage / manifest["xipLinkerScript"]),
        "-L" + str(sdk["sdk_ld"]),
    ]
    command += [f"-Wl,-T,{name}" for name in manifest["romLinkerScripts"]]
    command += [f"-Wl,-T,{expand(name)}"
                for name in manifest.get("extraLinkerScripts", [])]
    command += [expand(flag) for flag in manifest.get("linkUFlags", [])]
    command += ["-Wl,-Map=fmp_xip.map", "-o", "fmp_xip.elf", "@objects.rsp"]
    command += [expand(flag) for flag in manifest.get("linkLibGroup", [])]
    command += ["-lgcc", "-lc"]
    return command


def run(command: list[str], cwd: Path, what: str) -> None:
    completed = subprocess.run(command, cwd=str(cwd), text=True,
                               capture_output=True)
    if completed.returncode != 0:
        raise LinkError(
            f"{what} failed (exit={completed.returncode})\n"
            f"--- stdout ---\n{completed.stdout}\n"
            f"--- stderr ---\n{completed.stderr}")


#
#  Partition table generation (partitions.csv -> partitions.bin).
#
#  The platform inherits tools.gen_esp32part.cmd from the M5Stack core, and on
#  everything except Windows that runs "python3 gen_esp32part.py". Freezing this
#  driver took Python out of the link, but that recipe put it straight back on
#  macOS and Linux, where python3 is not always installed - recent macOS does
#  not ship it. So the conversion lives here and the recipe points at this
#  driver on every host.
#
#  Byte-compatible with gen_esp32part.py as the Arduino recipe invokes it: the
#  only flag passed is -q, so the md5 entry is on, the table sits at 0x8000 and
#  verification is on. Checked against gen_esp32part.exe for every partition CSV
#  the platform ships; see scripts/Test-PartitionTable.ps1.
#
PARTITION_MAGIC = b"\xaa\x50"
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0x1000
PARTITION_MAX_LENGTH = 0xC00
PARTITION_MD5_PREFIX = b"\xeb\xeb" + b"\xff" * 14
PARTITION_ENTRY_FORMAT = "<2sBBLL16sL"

PARTITION_APP_TYPE = 0x00
PARTITION_DATA_TYPE = 0x01
PARTITION_TYPES = {
    "app": PARTITION_APP_TYPE,
    "data": PARTITION_DATA_TYPE,
    "bootloader": 0x02,
    "partition_table": 0x03,
}
#  Keep in sync with esp_partition_subtype_t, as gen_esp32part.py does.
PARTITION_SUBTYPES = {
    PARTITION_APP_TYPE: dict(
        {"factory": 0x00, "test": 0x20},
        **{"ota_%d" % slot: 0x10 + slot for slot in range(16)},
        **{"tee_%d" % slot: 0x30 + slot for slot in range(2)}),
    PARTITION_DATA_TYPE: {
        "ota": 0x00, "phy": 0x01, "nvs": 0x02, "coredump": 0x03,
        "nvs_keys": 0x04, "efuse": 0x05, "undefined": 0x06,
        "esphttpd": 0x80, "fat": 0x81, "spiffs": 0x82, "littlefs": 0x83,
        "tee_ota": 0x90,
    },
}
PARTITION_ALIGNMENT = {PARTITION_APP_TYPE: 0x10000, PARTITION_DATA_TYPE: 0x1000}
PARTITION_FLAGS = {"encrypted": 0, "readonly": 1}
#  A read/write NVS partition smaller than this cannot work, so gen_esp32part
#  rejects it unless it is flagged read-only.
PARTITION_NVS_RW_MIN_SIZE = 0x3000


class _Partition:
    def __init__(self) -> None:
        self.name = ""
        self.type = 0
        self.subtype = 0
        self.offset: int | None = None
        self.size = 0
        self.flags = 0
        self.line_no = 0

    def to_binary(self) -> bytes:
        #  16s truncates a longer name, which is what gen_esp32part does too.
        return struct.pack(PARTITION_ENTRY_FORMAT, PARTITION_MAGIC, self.type,
                           self.subtype, self.offset, self.size,
                           self.name.encode("utf-8"), self.flags)


def partition_int(text: str, keywords: dict | None = None) -> int:
    """int(x, 0) with k/m suffixes and keyword lookup, as gen_esp32part has."""
    keywords = keywords or {}
    try:
        for letter, multiplier in (("k", 1024), ("m", 1024 * 1024)):
            if text.lower().endswith(letter):
                return partition_int(text[:-1], keywords) * multiplier
        return int(text, 0)
    except ValueError:
        try:
            return keywords[text.lower()]
        except KeyError:
            known = ", ".join(sorted(keywords))
            raise LinkError(
                f"partitions.csv: {text!r} is not a valid value"
                + (f" (known: {known})" if known else ""))


def partition_from_csv_line(line: str, line_no: int) -> _Partition:
    #  Appending the empty fields is how gen_esp32part supports defaults.
    fields = [field.strip() for field in (line + ",,,,").split(",")]
    entry = _Partition()
    entry.line_no = line_no
    entry.name = fields[0]

    if not fields[1]:
        raise LinkError(f"partitions.csv line {line_no}: type must not be empty")
    entry.type = partition_int(fields[1], PARTITION_TYPES)
    if entry.type in (PARTITION_TYPES["bootloader"],
                      PARTITION_TYPES["partition_table"]):
        #  These take their offset and size from command line options the
        #  Arduino recipe does not pass, so gen_esp32part would fail here too.
        raise LinkError(
            f"partitions.csv line {line_no}: partitions of type {fields[1]!r} "
            "need a bootloader offset that the Arduino build does not provide")

    if not fields[2]:
        if entry.type == PARTITION_APP_TYPE:
            raise LinkError(f"partitions.csv line {line_no}: "
                            "an app partition cannot have an empty subtype")
        entry.subtype = PARTITION_SUBTYPES[PARTITION_DATA_TYPE]["undefined"]
    else:
        entry.subtype = partition_int(
            fields[2], PARTITION_SUBTYPES.get(entry.type, {}))

    entry.offset = partition_int(fields[3]) if fields[3] else None
    if not fields[4]:
        raise LinkError(f"partitions.csv line {line_no}: size must not be empty")
    entry.size = partition_int(fields[4])

    for flag in fields[5].split(":"):
        if flag in PARTITION_FLAGS:
            entry.flags |= 1 << PARTITION_FLAGS[flag]
        elif flag:
            raise LinkError(
                f"partitions.csv line {line_no}: unknown flag {flag!r}")
    return entry


def partition_table_from_csv(text: str) -> list:
    entries = []
    for line_no, raw in enumerate(text.splitlines(), start=1):
        line = os.path.expandvars(raw).strip()
        if not line or line.startswith("#"):
            continue
        entries.append(partition_from_csv_line(line, line_no))

    #  Fill in missing offsets and resolve negative sizes.
    last_end = PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE
    for entry in entries:
        if entry.offset is not None and entry.offset < last_end:
            raise LinkError(
                f"partitions.csv line {entry.line_no}: partitions overlap; "
                f"offset 0x{entry.offset:x} but the previous one ends at "
                f"0x{last_end:x}")
        if entry.offset is None:
            align = PARTITION_ALIGNMENT.get(
                entry.type, PARTITION_ALIGNMENT[PARTITION_DATA_TYPE])
            if last_end % align:
                last_end += align - (last_end % align)
            entry.offset = last_end
        if entry.size < 0:
            entry.size = -entry.size - entry.offset
        last_end = entry.offset + entry.size
    return entries


def verify_partition_table(entries: list) -> None:
    """The checks gen_esp32part runs by default (--no-verify is not passed)."""
    names = [entry.name for entry in entries]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise LinkError("partitions.csv: partition names must be unique: "
                        + ", ".join(duplicates))

    table_end = PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE
    for entry in entries:
        align = PARTITION_ALIGNMENT.get(
            entry.type, PARTITION_ALIGNMENT[PARTITION_DATA_TYPE])
        if entry.offset % align:
            raise LinkError(
                f"partitions.csv: {entry.name!r} offset 0x{entry.offset:x} is "
                f"not aligned to 0x{align:x}")
        #  Without secure boot an app partition only needs 4K size alignment.
        if entry.type == PARTITION_APP_TYPE and entry.size % 0x1000:
            raise LinkError(
                f"partitions.csv: {entry.name!r} size 0x{entry.size:x} is not "
                "aligned to 0x1000")
        if entry.offset < table_end:
            raise LinkError(
                f"partitions.csv: {entry.name!r} offset 0x{entry.offset:x} is "
                f"below 0x{table_end:x}")
        readonly = bool(entry.flags & (1 << PARTITION_FLAGS["readonly"]))
        nvs = PARTITION_SUBTYPES[PARTITION_DATA_TYPE]["nvs"]
        if (entry.type == PARTITION_DATA_TYPE and entry.subtype == nvs
                and entry.size < PARTITION_NVS_RW_MIN_SIZE and not readonly):
            raise LinkError(
                f"partitions.csv: read/write nvs partition {entry.name!r} is "
                f"0x{entry.size:x}, below 0x{PARTITION_NVS_RW_MIN_SIZE:x}")

    previous = None
    for entry in sorted(entries, key=lambda item: item.offset):
        if previous is not None and entry.offset < previous.offset + previous.size:
            raise LinkError(
                f"partitions.csv: {entry.name!r} at 0x{entry.offset:x} overlaps "
                f"{previous.name!r} at 0x{previous.offset:x}")
        previous = entry

    ota = PARTITION_SUBTYPES[PARTITION_DATA_TYPE]["ota"]
    otadata = [entry for entry in entries
               if entry.type == PARTITION_DATA_TYPE and entry.subtype == ota]
    if len(otadata) > 1:
        raise LinkError("partitions.csv: only one otadata partition is allowed")


def write_partition_table(csv_path: Path, bin_path: Path) -> None:
    if not csv_path.is_file():
        raise LinkError(f"partitions.csv was not found: {csv_path}")
    raw = csv_path.read_bytes()
    if raw[:2] == PARTITION_MAGIC:
        raise LinkError(f"expected a CSV but {csv_path} is a binary table")

    entries = partition_table_from_csv(raw.decode("utf-8"))
    if not entries:
        raise LinkError(f"partitions.csv has no partitions: {csv_path}")
    verify_partition_table(entries)

    table = b"".join(entry.to_binary() for entry in entries)
    table += PARTITION_MD5_PREFIX + hashlib.md5(table).digest()
    if len(table) >= PARTITION_MAX_LENGTH:
        raise LinkError(f"the partition table is too long ({len(table)} bytes)")
    table += b"\xff" * (PARTITION_MAX_LENGTH - len(table))

    bin_path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(table)
    print(f"Wrote {len(entries)} partitions to {bin_path}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Link a prebuilt FMP3 stage into the XIP image.")
    parser.add_argument("--stage", default="",
                        help="directory produced by prebuilt_stage.cmake")
    parser.add_argument("--build-path", default="",
                        help="Arduino builder output directory ({build.path})")
    parser.add_argument("--project-name", default="",
                        help="{build.project_name}, e.g. Fmp3Minimal.ino")
    parser.add_argument("--output-directory", default="",
                        help="work directory (default: <build-path>/fmp3-prebuilt-link)")
    parser.add_argument("--gcc", default="", help="xtensa-esp32s3-elf-gcc")
    parser.add_argument("--esptool", default="", help="esptool executable")
    parser.add_argument("--sdk-ld", default="", help="SDK ld directory")
    parser.add_argument("--sdk-lib", default="", help="SDK lib directory")
    parser.add_argument("--arduino-data", default="",
                        help="Arduino data directory holding packages/")
    parser.add_argument("--core-version", default="3.3.8")
    parser.add_argument("--keep-work", action="store_true",
                        help="keep the staged objects for inspection")
    parser.add_argument("--partitions", nargs=2, metavar=("CSV", "BIN"),
                        help="convert an Arduino partitions.csv into a binary "
                             "partition table and exit; replaces the inherited "
                             "gen_esp32part recipe, which needs python3 on "
                             "macOS and Linux")
    parser.add_argument("--version", action="store_true",
                        help="print the driver version understood by this "
                             "build and exit; the release check compares it "
                             "with what the package index declares")
    parser.add_argument("--check-only", action="store_true",
                        help="only assert the image is present; for the Arduino "
                             "objcopy recipe, which must not overwrite it")
    args = parser.parse_args(argv)

    if args.version:
        print(f"fmp3-link {DRIVER_VERSION}")
        return 0

    if args.partitions:
        write_partition_table(Path(args.partitions[0]),
                              Path(args.partitions[1]))
        return 0

    for name in ("stage", "build_path", "project_name"):
        if not getattr(args, name):
            raise LinkError(f"--{name.replace('_', '-')} is required")

    stage = Path(args.stage)
    build_path = Path(args.build_path)

    if args.check_only:
        for required in (build_path / f"{args.project_name}.elf",
                         build_path / f"{args.project_name}.bin"):
            if not required.is_file():
                raise LinkError(f"FMP3 artifact was not found: {required}")
        print("Preserved the TOPPERS/FMP3 application image.")
        return 0

    manifest = load_manifest(stage)
    sdk = resolve_sdk(args)

    work = Path(args.output_directory) if args.output_directory else (
        build_path / "fmp3-prebuilt-link")
    work = work / "link"

    arduino_objects = collect_arduino_objects(manifest, build_path,
                                              args.project_name)
    names = stage_objects(stage, work, arduino_objects)

    expand = expander(stage, sdk)
    run(build_link_command(manifest, stage, sdk, expand), work, "Linking")
    run([str(sdk["esptool"]), "--chip", manifest["chip"], "elf2image",
         "--flash-mode", manifest["flashMode"],
         "--flash-freq", manifest["flashFreq"],
         "--flash-size", manifest["flashSize"],
         "-o", "app_xip.bin", "fmp_xip.elf"], work, "elf2image")

    destination_elf = build_path / f"{args.project_name}.elf"
    destination_bin = build_path / f"{args.project_name}.bin"
    shutil.copyfile(work / "fmp_xip.elf", destination_elf)
    shutil.copyfile(work / "app_xip.bin", destination_bin)

    digest = hashlib.sha256(destination_bin.read_bytes()).hexdigest().upper()
    print()
    print(f"Linked the {manifest['profile']} profile from the prebuilt stage.")
    print(f"  objects:  {len(names)} "
          f"(prebuilt {manifest['objectCount']} + Arduino {len(arduino_objects)})")
    print(f"  ELF:      {destination_elf}")
    print(f"  BIN:      {destination_bin}")
    print(f"  SHA-256:  {digest}")

    if not args.keep_work:
        shutil.rmtree(work / "objs", ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except LinkError as error:
        print(f"fmp3_link: {error}", file=sys.stderr)
        sys.exit(1)
