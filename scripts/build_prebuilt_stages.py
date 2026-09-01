#!/usr/bin/env python3
"""Build the sketch-independent FMP3 stage for each runtime profile.

This is the release-time half
of the split: CMake, Ninja and Python run here, on a developer machine, and
produce stages that a sketch build can link against with nothing but the
toolchain and esptool that ship with the M5Stack Arduino core.

One directory per profile appears under --output-directory, each holding objs/,
ld/, optionally lib/, link-manifest.json and objects.rsp. See
ports/esp32s3_m5cores3/runtime/cmake/prebuilt_stage.cmake.

The stages are sketch-independent because cfg is fixed per profile; only the
final link depends on the sketch.

This replaces New-Fmp3PrebuiltStages.ps1, which was the only reason the CI
package job had to run on a Windows runner - everything it drives (CMake, Ninja,
the Xtensa toolchain) is cross-platform already. The PowerShell script remains
for now; both produce the same stage.

    python scripts/build_prebuilt_stages.py --cmake <path> --ninja <path>
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from arduino_sdk import SdkError, resolve, tool_executable  # noqa: E402

SHIPPED_PROFILES = ["minimal", "m5-unified", "wifi-connect"]
#  'all-in-one' is EXPERIMENTAL: M5Unified + SMP + Wi-Fi in one
#  runtime. Not in the default set and not shipped.
ALL_PROFILES = SHIPPED_PROFILES + ["all-in-one"]

#  profile -> (application name, application directory, is it outside ports/)
APPLICATIONS = {
    "minimal": ("phase3_arduino_app", "phase3", False),
    "m5-unified": ("phase5_m5_app", "phase5", True),
    "wifi-connect": ("phase9_wifi_connect_app", "wifi_connect", False),
    "all-in-one": ("allinone_app", "allinone", True),
}
#  Only m5-unified has a self-test application; the others build the same thing
#  either way. The self-test adds a monitor task that prints PASS or FAILED, and
#  belongs to the test suite rather than the product - stages that go into the
#  Boards Manager package are built WITHOUT it.
SELF_TEST_APPLICATIONS = {"m5-unified": "phase5_m5_selftest"}

NEEDS_SDK_HEADERS = {"m5-unified", "wifi-connect", "all-in-one"}
NEEDS_M5_SOURCES = {"m5-unified", "all-in-one"}


def sketchbook_libraries() -> Path:
    """arduino-cli's default user directory, per OS.

    Windows and macOS put it under Documents; Linux does not. Assuming
    Documents everywhere is the kind of thing that made these scripts
    Windows-only in the first place.
    """
    if sys.platform.startswith("linux"):
        return Path.home() / "Arduino" / "libraries"
    return Path.home() / "Documents" / "Arduino" / "libraries"


def resolve_program(name: str, explicit: str) -> str:
    if explicit:
        if not Path(explicit).exists():
            raise SystemExit(f"{name} was not found: {explicit}")
        return str(Path(explicit).resolve())
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"{name} was not found. Install it or pass its path.")


def run(program: str, arguments: list[str], what: str, env: dict) -> None:
    completed = subprocess.run([program, *arguments], env=env)
    if completed.returncode != 0:
        raise SystemExit(f"{what} failed (exit={completed.returncode}).")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", nargs="+", choices=ALL_PROFILES,
                        default=SHIPPED_PROFILES)
    parser.add_argument("--library-root", default="")
    parser.add_argument("--output-directory", default="")
    parser.add_argument("--work-directory", default="")
    parser.add_argument("--cmake", default="")
    parser.add_argument("--ninja", default="")
    parser.add_argument("--arduino-data", default="")
    parser.add_argument("--core-version", default="3.3.8")
    parser.add_argument("--m5gfx-source", default="")
    parser.add_argument("--m5unified-source", default="")
    #  Stages are laid out per chip, because a second board (plain M5Core, LX6)
    #  will need its own set.
    parser.add_argument("--chip", choices=["esp32s3", "esp32"],
                        default="esp32s3")
    parser.add_argument("--self-test", action="store_true",
                        help="build the self-test flavour; use a separate "
                             "--output-directory so the two sets do not "
                             "overwrite each other")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)

    library_root = Path(args.library_root).resolve() if args.library_root \
        else Path(__file__).resolve().parent.parent
    output_directory = Path(args.output_directory).resolve() \
        if args.output_directory \
        else library_root / "build" / "prebuilt" / args.chip
    work_directory = Path(args.work_directory).resolve() \
        if args.work_directory \
        else library_root / "build" / "prebuilt-work" / args.chip

    cmake = resolve_program("cmake", args.cmake)
    ninja = resolve_program("ninja", args.ninja)

    try:
        sdk = resolve(Path(args.arduino_data) if args.arduino_data else None,
                      args.core_version, args.chip)
        package_root = Path(sdk["packageRoot"])
        #  One multi-target driver serves both, but the per-chip alias is what
        #  target.cmake matches on: -dumpmachine says "xtensa-esp-elf" for
        #  either, so the driver name is the only thing that can catch
        #  building one chip with the other's settings.
        compiler = tool_executable(package_root, "esp-x32",
                                   f"xtensa-{args.chip}-elf-gcc")
        esptool = tool_executable(package_root, "esptool_py", "esptool")
    except SdkError as error:
        raise SystemExit(str(error))

    m5gfx = Path(args.m5gfx_source) if args.m5gfx_source else None
    m5unified = Path(args.m5unified_source) if args.m5unified_source else None
    if NEEDS_M5_SOURCES.intersection(args.profiles):
        #  m5-unified compiles M5GFX and M5Unified on the CMake side, so the
        #  stage has to be built against the same sources the sketch build uses.
        libraries = sketchbook_libraries()
        if m5gfx is None:
            m5gfx = libraries / "M5GFX" / "src"
        if m5unified is None:
            m5unified = libraries / "M5Unified" / "src"
        for required in (m5gfx, m5unified):
            if not required.exists():
                raise SystemExit(
                    f"m5-unified needs the library sources: {required}")

    runtime = library_root / "ports" / "esp32s3_m5cores3" / "runtime"
    fmp3_core = library_root / "third_party" / "fmp3_core"
    if not (fmp3_core / "CMakeLists.txt").is_file():
        raise SystemExit(
            f"fmp3_core submodule is not checked out: {fmp3_core}")

    if args.clean:
        #  Only the profiles being built. Cleaning the whole output directory
        #  would delete the stages of profiles not named, which is how an
        #  install once ended up with one stage instead of the full set.
        for name in args.profiles:
            for stale in (output_directory / name, work_directory / name):
                if stale.exists():
                    shutil.rmtree(stale)
    output_directory.mkdir(parents=True, exist_ok=True)

    #  The toolchain has to be first on PATH for CMake's compiler probe.
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")

    results = []
    for name in args.profiles:
        application_name, directory_name, outside_ports = APPLICATIONS[name]
        if args.self_test and name in SELF_TEST_APPLICATIONS:
            application_name = SELF_TEST_APPLICATIONS[name]
        #  The m5-unified application lives outside ports/ in the development
        #  tree, the same split Invoke-PortableFmp3Recipe.ps1 uses.
        application = (library_root / "fmp_app" / directory_name
                       if outside_ports else
                       library_root / "ports" / "esp32s3_m5cores3" / "app"
                       / directory_name)

        build = work_directory / name
        stage = output_directory / name

        configure = [
            "-S", str(runtime),
            "-B", str(build),
            "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_TOOLCHAIN_FILE="
            f"{runtime / 'cmake' / f'toolchain-xtensa-{args.chip}.cmake'}",
            f"-DFMP3_CORE_ROOT={fmp3_core}",
            f"-DFMP3_APPLICATION_DIR={application}",
            f"-DFMP3_APPLICATION_NAME={application_name}",
            f"-DFMP3_RUNTIME_PROFILE={name}",
            f"-DA1_CHIP={args.chip}",
            f"-DARDUINO_SDK_LD_ROOT={sdk['linkerScriptRoot']}",
            f"-DA1_ESPTOOL_EXECUTABLE={esptool}",
        ]
        if name in NEEDS_SDK_HEADERS:
            configure += [
                f"-DARDUINO_SDK_INCLUDE_ROOT={sdk['includeRoot']}",
                f"-DARDUINO_SDK_LIBRARY_ROOT={sdk['libraryRoot']}",
            ]
        if name in NEEDS_M5_SOURCES:
            configure += [
                f"-DM5GFX_SOURCE_ROOT={m5gfx}",
                f"-DM5UNIFIED_SOURCE_ROOT={m5unified}",
                f"-DTOPPERS_LIBRARY_SOURCE_ROOT={library_root / 'src'}",
            ]

        print(f"\n=== staging {name} ===", flush=True)
        run(cmake, configure, f"Configuring {name}", env)
        run(cmake, ["--build", str(build), "--target", "fmp3_prebuilt",
                    "--parallel"], f"Staging {name}", env)

        produced = build / "prebuilt"
        if not (produced / "link-manifest.json").is_file():
            raise SystemExit(f"Stage was not produced for {name}.")
        if stage.exists():
            shutil.rmtree(stage)
        shutil.copytree(produced, stage)

        manifest = json.loads(
            (stage / "link-manifest.json").read_text(encoding="utf-8"))
        total = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file())
        results.append((name, manifest.get("objectCount", "?"),
                        round(total / (1024 * 1024), 1), stage))

    print("\nPrebuilt FMP3 stages\n")
    print(f"  {'Profile':<14}{'Objects':>8}{'SizeMB':>9}  Stage")
    for name, objects, size, stage in results:
        print(f"  {name:<14}{objects:>8}{size:>9.1f}  {stage}")
    print(f"\nOutput: {output_directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
