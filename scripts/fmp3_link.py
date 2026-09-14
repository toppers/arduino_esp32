#!/usr/bin/env python3
"""Link a prebuilt FMP3 stage with Arduino objects into the XIP image.

The final image is produced with nothing but the
toolchain and esptool that ship with the M5Stack Arduino core, so a sketch build
needs neither CMake, Ninja nor a cfg generator on the user's machine.

The input is a stage produced by ports/m5stack_xtensa/runtime/cmake/
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

Manifest schemas
----------------

The driver accepts schema 1 and schema 2 manifests. Schema 1 is what the
Xtensa stages carry and it is read exactly as it always was; schema 2 is
schema 1 plus the keys below, and is what a stage writes when it needs them.

Schema 1 keys (unchanged): ``schema``, ``profile``, ``chip``, ``paddrMode``
(must be ``"runtime-mmu"``), ``debugInfo``, ``xipLinkerScript``,
``romLinkerScripts``, ``extraLinkerScripts``, ``linkUFlags``,
``linkLibGroup``, ``flashMode``, ``flashFreq``, ``flashSize``,
``objectCount``, ``objectOrder``, ``requiredArduinoObjects``.

Schema 2 adds:

* ``paddrMode``: ``"runtime-mmu"`` or ``"fixed-vma"``. ``runtime-mmu`` is
  the Xtensa arrangement (the image resolves its own flash mapping at boot).
  ``fixed-vma`` is the RISC-V arrangement: the ELF is linked at the virtual
  addresses the bootloader's MMU mapping gives it, esptool lays the segments
  out from the section headers, and no flash_cache_init object is involved.
  After elf2image the driver checks such an image against what the
  bootloader will accept (check_fixed_vma_image, conditions C-1 to C-8).
* ``linkBaseFlags`` (required, list of strings): the flags placed right after
  the compiler driver, where the schema 1 link places the literal
  ``-nostdlib -mlongcalls``. A schema 2 manifest for an Xtensa stage would
  write exactly ``["-nostdlib", "-mlongcalls"]`` to link identically.
* ``linkTailFlags`` (optional, list of strings): the flags placed at the very
  end of the link, where the schema 1 link places the literal ``-lgcc -lc``.
  Omitted, that literal is used.

The command a schema 2 manifest produces is therefore
``<gcc> <linkBaseFlags> -Wl,--gc-sections -Wl,--allow-multiple-definition
-Wl,-T,<xipLinkerScript> -L<sdk ld> -Wl,-T,<romLinkerScripts>...
<extraLinkerScripts> <linkUFlags> -Wl,-Map=... -o fmp_xip.elf @objects.rsp
[libarduino.a] <linkLibGroup> <linkTailFlags>``.
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
from typing import NamedTuple


#  The frozen driver is shipped as a versioned Arduino tool, and a
#  package whose tool version has drifted from the script it was frozen from is
#  a real hazard: the manifest schema the driver understands is what decides
#  whether a stage links at all. --version lets the release check compare the
#  binary it is about to publish against this source.
DRIVER_VERSION = "3"

#  The schema a stage written for this driver carries, and every schema this
#  driver still reads. A schema 1 manifest links exactly as it did under
#  driver 2; see "Manifest schemas" above for what schema 2 adds.
MANIFEST_SCHEMA = 2
SUPPORTED_MANIFEST_SCHEMAS = (1, 2)

PADDR_RUNTIME_MMU = "runtime-mmu"
PADDR_FIXED_VMA = "fixed-vma"
#  Schema 1 knows runtime-mmu only; fixed-vma needs the schema 2 keys.
PADDR_MODES = {1: (PADDR_RUNTIME_MMU,),
               2: (PADDR_RUNTIME_MMU, PADDR_FIXED_VMA)}

#  What a schema 1 manifest links with, literally, in the two places a schema
#  2 manifest fills from linkBaseFlags and linkTailFlags. These do not move
#  into the Xtensa stages' manifests: the stages on disk are schema 1 and are
#  compared byte for byte against a baseline.
SCHEMA1_LINK_BASE_FLAGS = ["-nostdlib", "-mlongcalls"]
SCHEMA1_LINK_TAIL_FLAGS = ["-lgcc", "-lc"]

#  Where the M5Stack platform's upload and merge-bin recipes put
#  {build.project_name}.bin, for every board (platform.txt: "0x10000
#  {build.path}/{build.project_name}.bin"). The fixed-vma checks need it to
#  find the app partition the image has to fit in.
APP_FLASH_OFFSET = 0x10000
#  Holds the Arduino objects the manifest does not name; see stage_archive.
ARCHIVE_NAME = "libarduino.a"
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


def tool_beside(gcc: Path, name: str) -> Path:
    """A binutils tool of the same toolchain as the given gcc.

    The Arduino recipe passes {compiler.c.cmd} but has no placeholder that
    names ar for this chip, so it is derived rather than passed: the tools
    live in the same directory and share the name up to the trailing "gcc".
    """
    stem = gcc.name[:-4] if gcc.name.lower().endswith(".exe") else gcc.name
    if not stem.endswith("gcc"):
        raise LinkError(f"Cannot find the {name} that goes with {gcc}")
    return existing_program(gcc.with_name(stem[:-3] + name), name)


def ar_beside(gcc: Path) -> Path:
    """The ar of the same toolchain as the given gcc."""
    return tool_beside(gcc, "ar")


#  Where the driver looks when the recipe does not pass the tools: keyed by
#  the stage's chip. The esp32s3 row is what the fallback always was; the
#  recipe passes every one of these, so a sketch build never comes here.
#
#    chip: (tool directory, compiler driver, SDK tool directory)
#
SDK_FALLBACKS = {
    "esp32s3": ("esp-x32", "xtensa-esp32s3-elf-gcc", "esp32s3-libs"),
    "esp32": ("esp-x32", "xtensa-esp32-elf-gcc", "esp32-libs"),
    "esp32c6": ("esp-rv32", "riscv32-esp-elf-gcc", "esp32c6-libs"),
}


def resolve_sdk(args: argparse.Namespace,
                chip: str = "esp32s3") -> dict[str, Path]:
    """Fill in whatever the caller did not pass explicitly."""
    resolved: dict[str, Path] = {}
    package_root = None
    if not (args.gcc and args.esptool and args.sdk_ld and args.sdk_lib):
        data = Path(args.arduino_data) if args.arduino_data else arduino_data_default()
        package_root = data / "packages" / "m5stack"
        if not package_root.is_dir():
            raise LinkError(f"M5Stack package directory is missing: {package_root}")
        if chip not in SDK_FALLBACKS:
            raise LinkError(f"No default toolchain is known for {chip}; pass "
                            "--gcc, --esptool, --sdk-ld and --sdk-lib")
    tool_dir, gcc_name, sdk_dir = SDK_FALLBACKS.get(chip, SDK_FALLBACKS["esp32s3"])

    if args.gcc:
        resolved["gcc"] = Path(args.gcc)
    else:
        resolved["gcc"] = newest(
            str(package_root / "tools" / tool_dir / "*" / "bin"
                / f"{gcc_name}{EXE}"),
            gcc_name)
    if args.esptool:
        resolved["esptool"] = Path(args.esptool)
    else:
        resolved["esptool"] = newest(
            str(package_root / "tools" / "esptool_py" / "*" / f"esptool{EXE}"),
            "esptool")
    if args.sdk_ld:
        resolved["sdk_ld"] = Path(args.sdk_ld)
    else:
        resolved["sdk_ld"] = (package_root / "tools" / sdk_dir
                              / args.core_version / "ld")
    if args.sdk_lib:
        resolved["sdk_lib"] = Path(args.sdk_lib)
    else:
        resolved["sdk_lib"] = (package_root / "tools" / sdk_dir
                               / args.core_version / "lib")

    for key in ("gcc", "esptool"):
        resolved[key] = existing_program(resolved[key], key)
    resolved["ar"] = ar_beside(resolved["gcc"])
    for key in ("sdk_ld", "sdk_lib"):
        if not resolved[key].is_dir():
            raise LinkError(f"{key} does not exist: {resolved[key]}")
    return resolved


def validate_manifest(manifest: dict) -> dict:
    """Reject what this driver cannot link; see "Manifest schemas" above.

    Split from load_manifest so the schema rules can be tested without a
    stage on disk.
    """
    schema = int(manifest.get("schema", 0))
    if schema not in SUPPORTED_MANIFEST_SCHEMAS:
        raise LinkError(f"Unsupported manifest schema: {manifest.get('schema')}")
    mode = manifest.get("paddrMode")
    if mode not in PADDR_MODES[schema]:
        if schema == 1:
            #  The message driver 2 gave, for the manifests driver 2 read.
            raise LinkError(
                "This driver requires runtime PADDR resolution "
                f"(manifest={mode})")
        raise LinkError(
            f"Unsupported paddrMode {mode!r} for manifest schema {schema} "
            f"(known: {', '.join(PADDR_MODES[schema])})")
    if schema >= 2:
        for key, required in (("linkBaseFlags", True),
                              ("linkTailFlags", False)):
            value = manifest.get(key)
            if value is None and not required:
                continue
            if not isinstance(value, list) or \
                    not all(isinstance(flag, str) for flag in value):
                raise LinkError(
                    f"Manifest schema {schema} needs {key} as a list of "
                    f"strings (manifest={value!r})")
    return manifest


def load_manifest(stage: Path) -> dict:
    manifest_path = stage / "link-manifest.json"
    if not manifest_path.is_file():
        raise LinkError(f"Prebuilt stage is incomplete: {manifest_path}")
    return validate_manifest(
        json.loads(manifest_path.read_text(encoding="utf-8")))


def link_base_flags(manifest: dict) -> list[str]:
    """What goes right after the compiler driver."""
    if int(manifest.get("schema", 0)) >= 2:
        return list(manifest["linkBaseFlags"])
    return list(SCHEMA1_LINK_BASE_FLAGS)


def link_tail_flags(manifest: dict) -> list[str]:
    """What goes at the very end of the link."""
    if int(manifest.get("schema", 0)) >= 2 and \
            manifest.get("linkTailFlags") is not None:
        return list(manifest["linkTailFlags"])
    return list(SCHEMA1_LINK_TAIL_FLAGS)


class ArduinoObjects(NamedTuple):
    """What the Arduino builder produced, split by how it reaches the linker.

    ``linked`` is force-linked, the way the Arduino builder links a sketch:
    every translation unit of the sketch itself, plus the library objects the
    stage cannot work without (``requiredArduinoObjects``).

    ``archived`` is everything else the builder compiled - the bundled
    library's other sources, and any library the sketch pulls in. It goes into
    an archive so that the linker takes a member only when the sketch actually
    refers to it. Force-linking these instead would break the profiles they
    were not built for: ToppersFMP3_WiFi.cpp.o calls Wi-Fi symbols that exist
    only in the wifi-connect stage, so a minimal build would stop linking.
    """

    linked: list[Path]
    archived: list[Path]


def missing_object_message(name: str, hits: list[Path],
                           library_root: Path) -> str:
    """Say why a required object is absent, in terms of what the user did.

    "Expected exactly one ArduinoSketchBridge.cpp.o, found 0" named a file
    nobody writes by hand and gave no way to act. The realistic cause is that
    the sketch includes no header of the bundled library, so arduino-cli never
    compiled it: the builder compiles a library only when the sketch reaches
    for one of its headers, and every one of these objects comes from that
    library. Look at what is in build/libraries and say which case this is.
    """
    if len(hits) > 1:
        found = ", ".join(str(path) for path in hits)
        return (f"Expected exactly one {name}, found {len(hits)}: {found}. "
                "Two copies of the ToppersFMP3 library are being compiled; "
                "remove the one in the sketchbook's libraries/ folder, which "
                "shadows the copy that ships with the board.")

    compiled = sorted(path.name for path in library_root.glob("*")
                      if path.is_dir())
    if not compiled:
        return (f"{name} was not built, because this sketch includes no header "
                "of the board's bundled library, so the Arduino builder never "
                "compiled it. Add this line at the top of the sketch:\n"
                "    #include <ToppersFMP3_ArduinoBridge.h>\n"
                f"({name} carries the FMP3 task that calls setup() and loop(), "
                "so every sketch needs it, whichever runtime is selected.)")

    return (f"{name} was not built, although these libraries were compiled: "
            f"{', '.join(compiled)}. The board's bundled library is expected "
            "to supply it; a stale copy of that library in the sketchbook's "
            "libraries/ folder is the usual cause, because it shadows the one "
            "that ships with the board.")


def collect_arduino_objects(manifest: dict, build_path: Path,
                            project_name: str) -> ArduinoObjects:
    sketch_object = build_path / "sketch" / f"{project_name}.cpp.o"
    if not sketch_object.is_file():
        raise LinkError(f"Arduino sketch object was not found: {sketch_object}")
    #  Not just the .ino: a sketch folder may hold further .cpp/.c files, and
    #  the builder compiles each of them into build/sketch.
    linked = sorted((build_path / "sketch").rglob("*.o"))

    required: list[Path] = []
    library_root = build_path / "libraries"
    for name in manifest.get("requiredArduinoObjects", []):
        if name == "<sketch>.cpp.o":
            continue
        hits = list(library_root.rglob(name))
        if len(hits) != 1:
            raise LinkError(missing_object_message(name, hits, library_root))
        required.append(hits[0])

    archived = [path for path in sorted((build_path / "libraries").rglob("*.o"))
                if path not in set(required)]
    return ArduinoObjects(linked + required, archived)


def stage_objects(stage: Path, work: Path,
                  arduino_objects: ArduinoObjects) -> list[str]:
    """Copy the stage and the force-linked Arduino objects into one directory.

    Basename collisions inside the stage were already rejected when it was
    produced; a collision here can only come from an Arduino object.
    """
    objs = work / "objs"
    if work.exists():
        shutil.rmtree(work)
    objs.mkdir(parents=True)
    for source in sorted((stage / "objs").glob("*.o")):
        shutil.copyfile(source, objs / source.name)
    for source in arduino_objects.linked:
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


def stage_archive(work: Path, arduino_objects: ArduinoObjects,
                  ar: Path) -> str | None:
    """Put the on-demand objects into an archive beside objects.rsp.

    Returns the archive's name for the link command, or None when there is
    nothing to offer. ``D`` keeps the archive itself reproducible; duplicate
    member basenames are allowed by ar and resolved through the symbol index,
    so two libraries may both contain a util.cpp.o.
    """
    if not arduino_objects.archived:
        return None
    run([str(ar), "rcsD", ARCHIVE_NAME]
        + [str(path) for path in arduino_objects.archived],
        work, "Archiving the Arduino objects")
    return ARCHIVE_NAME


def expander(stage: Path, sdk: dict[str, Path], chip: str):
    #  The peripherals script is named for the chip, and the M5Stack core ships
    #  one tree per chip. Taking the name from the manifest rather than fixing
    #  it here is what lets a second board's stage link at all: with esp32s3
    #  baked in, an esp32 stage asks for a file that is not in its SDK.
    replacements = {
        "@STAGE@": str(stage.resolve()),
        "@SDK_LD_ROOT@": str(sdk["sdk_ld"]),
        "@SDK_LIBRARY_ROOT@": str(sdk["sdk_lib"]),
        "@SDK_PERIPHERALS_LD@": str(sdk["sdk_ld"] / f"{chip}.peripherals.ld"),
    }

    def expand(value: str) -> str:
        for placeholder, actual in replacements.items():
            value = value.replace(placeholder, actual)
        return value

    return expand


def build_link_command(manifest: dict, stage: Path, sdk: dict[str, Path],
                       expand, archive: str | None = None) -> list[str]:
    command = [
        str(sdk["gcc"]),
        *link_base_flags(manifest),
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
    #  After the objects, so a member is taken only for a symbol still
    #  undefined at this point, and before the SDK libraries it may itself
    #  need.
    if archive:
        command += [archive]
    command += [expand(flag) for flag in manifest.get("linkLibGroup", [])]
    command += link_tail_flags(manifest)
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


#
#  fixed-vma image checks (C-1 .. C-8).
#
#  A runtime-mmu (Xtensa) image maps itself at boot, so a mistake in its
#  layout shows up as its own boot code failing. A fixed-vma image is laid
#  out by esptool from the ELF section headers and mapped by the ESP-IDF
#  second-stage bootloader, which asserts its own conditions on the segment
#  list - and a bootloader assert on a board with no console attached looks
#  like a board that does nothing. So the conditions are checked here, on the
#  bytes that would be flashed, and a violation stops the build with the
#  condition named. Each condition cites the bootloader source it comes from
#  (ESP-IDF v5.5.4, the version the M5Stack core 3.3.8 bundles):
#
#    C-1  exactly two segments are mapped from flash
#         (bootloader_utility.c:836,843: assert(rom_index < 2) while
#          collecting, assert(rom_index == 2) after)
#    C-2  segment #0 begins with ESP_APP_DESC_MAGIC_WORD, 0xABCD5432
#         (esp_image_format.c:767-777 reads segment #0 as esp_app_desc_t)
#    C-3  every mapped segment has file_offset % page == vaddr % page
#         (esp_image_format.c:873-880; the app is flashed at APP_FLASH_OFFSET,
#          a multiple of the page, so the offset inside the image is what
#          the offset in flash is, modulo the page)
#    C-4  the entry point lies in a segment the image actually carries
#    C-5  no RAM segment reaches the bootloader's own iram_loader_seg
#         (loading over it kills the loader; bootloader.ld asserts the
#          address)
#    C-6  every RAM segment's bytes equal the ELF .data bytes at that address
#         (esptool builds segments from section headers; this is the direct
#          check that .data went in with its contents, and it fails closed
#          when either .data or the RAM segment list is empty)
#    C-7  the two mapped segments' MMU page ranges do not overlap
#    C-8  the image fits the app partition it is flashed to
#         (the length comes from the build's partitions.csv, not a constant:
#          a changed partition scheme must move this check with it)
#
#  Unit tests build synthetic images that break one condition at a time and
#  confirm the check names it; a check that cannot be made to fail is not one.
#

class ImageLayout(NamedTuple):
    """Where a chip's bootloader expects a fixed-vma image to sit."""

    #  CONFIG_MMU_PAGE_SIZE of the bootloader the M5Stack core ships.
    page: int
    #  [low, high) of the flash-mapped (IROM = DROM) window.
    drom: tuple[int, int]
    #  [low, high) of internal SRAM.
    iram: tuple[int, int]
    #  Start of the bootloader's iram_loader_seg; the app's RAM must end
    #  below it.
    loader_seg: int


#  esp32c6: soc.h:154-165 (SOC_IROM_LOW 0x42000000, 256 pages of 64 KB;
#  SOC_IRAM_LOW/HIGH 0x40800000/0x40880000), bootloader.ld:48
#  (bootloader_iram_loader_seg_start == 0x4086E610), sdkconfig of
#  esp32c6-libs 3.3.8 (CONFIG_MMU_PAGE_SIZE=0x10000).
FIXED_VMA_LAYOUTS = {
    "esp32c6": ImageLayout(page=0x10000,
                           drom=(0x42000000, 0x43000000),
                           iram=(0x40800000, 0x40880000),
                           loader_seg=0x4086E610),
}

IMAGE_MAGIC = 0xE9
IMAGE_HEADER_SIZE = 24          # 8-byte header plus the 16-byte extended one
APP_DESC_MAGIC = 0xABCD5432


def fixed_vma_layout(chip: str) -> ImageLayout:
    if chip not in FIXED_VMA_LAYOUTS:
        raise LinkError(
            f"paddrMode fixed-vma is not supported for {chip}: this driver "
            "knows the bootloader layout of "
            + ", ".join(FIXED_VMA_LAYOUTS) + " only")
    return FIXED_VMA_LAYOUTS[chip]


class ImageSegment(NamedTuple):
    index: int
    load: int
    file_offset: int
    length: int


class ImageSegments(NamedTuple):
    entry: int
    mapped: list
    ram: list
    pad: list


def parse_image_segments(image: bytes, layout: ImageLayout) -> ImageSegments:
    """Split an esp_image into its segments by where each one loads."""
    if len(image) < IMAGE_HEADER_SIZE or image[0] != IMAGE_MAGIC:
        raise LinkError("C-0: the image does not start with the esp_image "
                        f"magic 0x{IMAGE_MAGIC:02x}")
    _, count, _, _, entry = struct.unpack("<BBBBI", image[:8])
    offset = IMAGE_HEADER_SIZE
    mapped, ram, pad = [], [], []
    for index in range(count):
        if offset + 8 > len(image):
            raise LinkError(f"C-0: segment {index} header runs past the image")
        load, length = struct.unpack("<II", image[offset:offset + 8])
        data_offset = offset + 8
        if data_offset + length > len(image):
            raise LinkError(f"C-0: segment {index} data runs past the image")
        segment = ImageSegment(index, load, data_offset, length)
        if layout.drom[0] <= load < layout.drom[1]:
            mapped.append(segment)
        elif load < 0x10000000:
            #  esptool inserts these (load address 0) to keep the mapped
            #  segments page-congruent; the bootloader skips them.
            pad.append(segment)
        else:
            ram.append(segment)
        offset = data_offset + length
    return ImageSegments(entry, mapped, ram, pad)


def elf_section(elf: bytes, name: str) -> tuple[int, bytes] | None:
    """(sh_addr, bytes) of a PROGBITS section of a 32-bit little-endian ELF.

    Read from the section headers directly rather than through objcopy and
    readelf, so the check needs no tool beyond the driver itself and can be
    exercised on a synthetic ELF.
    """
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise LinkError("C-6: the ELF is not a 32-bit little-endian ELF")
    (shoff, shentsize, shnum,
     shstrndx) = struct.unpack("<I", elf[32:36]) + struct.unpack(
        "<HHH", elf[46:52])
    if shnum == 0 or shstrndx >= shnum:
        raise LinkError("C-6: the ELF has no section headers")

    def header(index: int) -> tuple:
        start = shoff + index * shentsize
        return struct.unpack("<IIIIIIIIII", elf[start:start + 40])

    _, _, _, _, str_offset, str_size, _, _, _, _ = header(shstrndx)
    strings = elf[str_offset:str_offset + str_size]
    for index in range(shnum):
        sh_name, sh_type, _, sh_addr, sh_offset, sh_size, *_ = header(index)
        end = strings.find(b"\0", sh_name)
        if strings[sh_name:end].decode("ascii", "replace") != name:
            continue
        if sh_type != 1:        # SHT_PROGBITS
            continue
        return sh_addr, elf[sh_offset:sh_offset + sh_size]
    return None


def app_partition_length(csv_path: Path,
                         flash_offset: int = APP_FLASH_OFFSET) -> int:
    """Size of the app partition the image is flashed to.

    The M5Stack recipes write the application at flash_offset regardless of
    the partition scheme, so the partition is found by that offset. No
    partition there, or no partitions.csv, is a failure: C-8 cannot be
    checked and must not be reported as passed.
    """
    if not csv_path.is_file():
        raise LinkError(f"C-8: partitions.csv was not found: {csv_path}")
    entries = partition_table_from_csv(csv_path.read_text(encoding="utf-8"))
    for entry in entries:
        if entry.type == PARTITION_APP_TYPE and entry.offset == flash_offset:
            return entry.size
    raise LinkError(
        f"C-8: {csv_path} has no app partition at 0x{flash_offset:x}, where "
        "the image is flashed")


def check_fixed_vma_image(image: bytes, elf: bytes, layout: ImageLayout,
                          app_length: int) -> list[str]:
    """Check an esp_image against the bootloader's conditions C-1 to C-8.

    Returns the lines to report on success; raises LinkError naming the
    condition on the first failure.
    """
    page = layout.page
    if APP_FLASH_OFFSET % page:
        raise LinkError(
            f"C-3: the app flash offset 0x{APP_FLASH_OFFSET:x} is not a "
            f"multiple of the MMU page 0x{page:x}, so offsets inside the "
            "image do not stand for offsets in flash")
    segments = parse_image_segments(image, layout)
    entry, mapped, ram, pad = segments
    lines = [f"fixed-vma image: segments={len(mapped) + len(ram) + len(pad)} "
             f"entry=0x{entry:08x} mapped={len(mapped)} ram={len(ram)} "
             f"pad={len(pad)}"]
    for kind, group in (("mapped", mapped), ("ram", ram), ("pad", pad)):
        for seg in group:
            lines.append(f"  {kind:<6} seg{seg.index} load=0x{seg.load:08x} "
                         f"file_offset=0x{seg.file_offset:06x} "
                         f"len={seg.length}")

    #  C-1
    if len(mapped) != 2:
        raise LinkError(
            f"C-1: {len(mapped)} segment(s) map from flash; the bootloader "
            "asserts exactly 2 (bootloader_utility.c: rom_index == 2)")
    #  C-2
    if mapped[0].index != 0:
        raise LinkError("C-2: segment #0 is not a flash-mapped segment, but "
                        "the app descriptor is read from segment #0")
    word = struct.unpack("<I", image[mapped[0].file_offset:
                                     mapped[0].file_offset + 4])[0]
    if word != APP_DESC_MAGIC:
        raise LinkError(
            f"C-2: segment #0 starts with 0x{word:08x}, not the app "
            f"descriptor magic 0x{APP_DESC_MAGIC:08x}")
    #  C-3
    for seg in mapped:
        if seg.file_offset % page != seg.load % page:
            raise LinkError(
                f"C-3: seg{seg.index} is not page-congruent: file_offset % "
                f"page = 0x{seg.file_offset % page:x}, vaddr % page = "
                f"0x{seg.load % page:x} (page 0x{page:x})")
    #  C-4
    if not any(seg.load <= entry < seg.load + seg.length
               for seg in mapped + ram):
        raise LinkError(
            f"C-4: entry 0x{entry:08x} lies in no segment the image carries")
    #  C-5
    for seg in ram:
        if not (layout.iram[0] <= seg.load < layout.iram[1]):
            raise LinkError(
                f"C-5: ram seg{seg.index} loads at 0x{seg.load:08x}, outside "
                "internal SRAM")
        end = seg.load + seg.length
        if end > layout.loader_seg:
            raise LinkError(
                f"C-5: ram seg{seg.index} ends at 0x{end:08x}, inside the "
                f"bootloader's iram_loader_seg (0x{layout.loader_seg:08x})")
    #  C-6
    data = elf_section(elf, ".data")
    if data is None or not data[1]:
        raise LinkError(
            "C-6: the ELF has no non-empty .data section; the linker script "
            "is expected to place the kernel's initialized data there, so "
            "an empty one means the check cannot be made, not that it passed")
    data_vma, data_bytes = data
    if not ram:
        raise LinkError(
            "C-6: the image carries no RAM segment although the ELF .data is "
            f"{len(data_bytes)} bytes; esptool dropped it")
    for seg in ram:
        offset = seg.load - data_vma
        if offset < 0 or offset + seg.length > len(data_bytes):
            raise LinkError(
                f"C-6: ram seg{seg.index} [0x{seg.load:08x},+{seg.length}) "
                f"is not within the ELF .data [0x{data_vma:08x},"
                f"+{len(data_bytes)})")
        expected = data_bytes[offset:offset + seg.length]
        actual = image[seg.file_offset:seg.file_offset + seg.length]
        if actual != expected:
            raise LinkError(
                f"C-6: ram seg{seg.index} differs from the ELF .data bytes "
                f"at 0x{seg.load:08x} ({seg.length} bytes)")
        lines.append(f"  C-6 OK: ram seg{seg.index} equals ELF "
                     f".data[0x{offset:x}:0x{offset + seg.length:x}]")
    #  C-7
    def page_range(seg: ImageSegment) -> tuple[int, int]:
        return (seg.load & ~(page - 1),
                (seg.load + seg.length - 1) & ~(page - 1))

    (lo0, hi0), (lo1, hi1) = page_range(mapped[0]), page_range(mapped[1])
    if not (hi0 < lo1 or hi1 < lo0):
        raise LinkError(
            f"C-7: seg{mapped[0].index} pages [0x{lo0:08x},0x{hi0:08x}] and "
            f"seg{mapped[1].index} pages [0x{lo1:08x},0x{hi1:08x}] overlap")
    lines.append(f"  C-7 OK: seg{mapped[0].index} pages [0x{lo0:08x},"
                 f"0x{hi0:08x}] and seg{mapped[1].index} pages "
                 f"[0x{lo1:08x},0x{hi1:08x}] are disjoint")
    #  C-8
    if len(image) > app_length:
        raise LinkError(
            f"C-8: the image is {len(image)} bytes, over the app partition's "
            f"{app_length} bytes by {len(image) - app_length}")
    lines.append(f"  C-8 OK: image {len(image)}/{app_length} bytes "
                 f"({app_length - len(image)} free)")
    lines.append("fixed-vma image: C-1 to C-8 satisfied")
    return lines


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
    parser.add_argument("--gcc", default="", help="the stage chip's gcc driver")
    parser.add_argument("--esptool", default="", help="esptool executable")
    parser.add_argument("--sdk-ld", default="", help="SDK ld directory")
    parser.add_argument("--sdk-lib", default="", help="SDK lib directory")
    #  ★The flash size is a property of the board, not of the stage.
    #  Two boards can share one chip's stages and not share a flash part: the
    #  M5CoreS3 has 16MB and the M5StickS3 8MB, and both link the esp32s3
    #  stages. The manifest can only carry one value, so the board's own
    #  build.flash_size wins when the recipe passes it.
    parser.add_argument("--flash-size", default="",
                        help="board flash size; overrides the manifest")
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
    sdk = resolve_sdk(args, manifest["chip"])

    work = Path(args.output_directory) if args.output_directory else (
        build_path / "fmp3-prebuilt-link")
    work = work / "link"

    arduino_objects = collect_arduino_objects(manifest, build_path,
                                              args.project_name)
    names = stage_objects(stage, work, arduino_objects)
    archive = stage_archive(work, arduino_objects, sdk["ar"])

    expand = expander(stage, sdk, manifest["chip"])
    run(build_link_command(manifest, stage, sdk, expand, archive), work,
        "Linking")
    run([str(sdk["esptool"]), "--chip", manifest["chip"], "elf2image",
         "--flash-mode", manifest["flashMode"],
         "--flash-freq", manifest["flashFreq"],
         "--flash-size", args.flash_size or manifest["flashSize"],
         "-o", "app_xip.bin", "fmp_xip.elf"], work, "elf2image")

    if manifest["paddrMode"] == PADDR_FIXED_VMA:
        #  The image goes to flash unchanged, so what the bootloader will
        #  make of it is decided here. The app partition it must fit in
        #  comes from the same partitions.csv the partition recipe converts.
        layout = fixed_vma_layout(manifest["chip"])
        app_length = app_partition_length(build_path / "partitions.csv")
        for line in check_fixed_vma_image(
                (work / "app_xip.bin").read_bytes(),
                (work / "fmp_xip.elf").read_bytes(), layout, app_length):
            print(line)

    destination_elf = build_path / f"{args.project_name}.elf"
    destination_bin = build_path / f"{args.project_name}.bin"
    shutil.copyfile(work / "fmp_xip.elf", destination_elf)
    shutil.copyfile(work / "app_xip.bin", destination_bin)

    digest = hashlib.sha256(destination_bin.read_bytes()).hexdigest().upper()
    print()
    print(f"Linked the {manifest['profile']} profile from the prebuilt stage.")
    print(f"  objects:  {len(names)} "
          f"(prebuilt {manifest['objectCount']} + "
          f"Arduino {len(arduino_objects.linked)})"
          + (f", plus {len(arduino_objects.archived)} on demand"
             if arduino_objects.archived else ""))
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
