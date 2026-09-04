#!/usr/bin/env python3
"""Assemble the TOPPERS/FMP3 Arduino board platform.

Produces the platform
directory that Boards Manager later packages: boards.txt, platform.txt, the
partition tools, the prebuilt stages and the link driver.

One platform can hold both boards. build_prebuilt_stages.py writes
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
    "m5core_fmp3": ("esp32", "m5stack_core",
                    "M5Core (TOPPERS/FMP3)", "m5stack_core"),
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

    lines = board + [
        #  Which chip's stages this board links against; the layout is
        #  fmp3-prebuilt/<chip>/<profile>.
        f"{prefix}build.toppers_chip={chip}",
    ]
    entries = list(MENU_ENTRIES)
    if (stage_root / EXPERIMENTAL_ENTRY[2]).is_dir():
        entries.append(EXPERIMENTAL_ENTRY)
    entries.extend(CHIP_ONLY_ENTRIES.get(chip, []))
    for key, label, profile in entries:
        if not (stage_root / profile).is_dir():
            continue
        lines.append(f"{prefix}menu.FMP3Runtime.{key}={label}")
        lines.append(f"{prefix}menu.FMP3Runtime.{key}"
                     f".build.toppers_profile={profile}")
    return menus, lines


def platform_lines(source: Path, link: str, objcopy: str,
                   partitions: str) -> list[str]:
    out = []
    for line in source.read_text(encoding="utf-8").splitlines():
        if line.startswith("name="):
            out.append("name=M5Stack Arduino with TOPPERS/FMP3")
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
    parser.add_argument("--core-version", default="3.3.8")
    parser.add_argument("--prebuilt-stage-root", default="",
                        help="stages from build_prebuilt_stages.py; required")
    parser.add_argument("--python-executable", default="",
                        help="interpreter for the driver recipe; a frozen "
                             "per-OS build replaces it in the released package")
    #  Repeatable. Without it, a stage root laid out per chip installs every
    #  chip it holds; a single chip's stage root installs esp32s3.
    parser.add_argument("--chip", action="append",
                        choices=["esp32s3", "esp32"], default=None)
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

    (platform_root / "platform.txt").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\r\n")
    shutil.copy2(source_platform / "programmers.txt", platform_root)

    (platform_root / MARKER).write_text(json.dumps({
        "package": "ToppersFMP3-M5CoreS3",
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
