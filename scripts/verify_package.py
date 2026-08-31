#!/usr/bin/env python3
"""Install the Boards Manager package on this machine and build every profile.

This is the check that the package works on an OS other than
the one it was built on. It runs the same path a user would: a package index, an
install through Boards Manager, then a sketch build - nothing reaches into the
repository except the examples.

Boards Manager will not fetch an archive over file://, only the index, so the
package is served over loopback HTTP while the install runs.

What it needs on the machine:

* arduino-cli (or pass --arduino-cli)
* the M5Stack ESP32 core, which it installs; Arduino cannot declare a
  dependency on a referenced platform, so this is a genuine prerequisite
* PyInstaller, unless a frozen driver is passed with --driver

What it does not need: CMake, Ninja, or a Python interpreter at sketch build
time. That is the point of the exercise.

Usage:
    python scripts/verify_package.py --platform-dir <platform>

The platform directory is what scripts/Install-ArduinoIdeIntegration.ps1
produces, or the artifact the verify-package workflow uploads. Copy it to the
machine under test; it is host-independent.
"""

from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import os
import platform
import shutil
import subprocess
import sys
import threading
import zipfile
from pathlib import Path

M5STACK_INDEX = ("https://static-cdn.m5stack.com/resource/arduino/"
                 "package_m5stack_index.json")
CORE_VERSION = "3.3.8"
#  Pinned: the m5-unified stage is compiled against these sources, and a later
#  version can call into the SDK in ways the stage does not resolve.
LIBRARY_VERSIONS = {"M5GFX": "0.2.27", "M5Unified": "0.2.20"}

#  menu option -> example sketches built against it
#
#  Steps D-4c and D-4e cut five profiles to three: dual-core folded into
#  m5-unified, wifi-scan into wifi-connect.
#
#  Each profile is checked with the examples that exercise it AND with a plain
#  sketch, because "any sketch builds on any profile" is a property worth
#  failing on: it was not true before D-4, and nothing else here would notice
#  if it broke again.
PROFILES = {
    "minimal": ["Blink"],
    "m5": ["M5Unified", "DualCore", "Blink"],
    "wificonnect": ["WiFiConnect", "WiFiScan", "Blink"],
}

BOARD = "toppers:esp32:m5cores3_fmp3"


class VerifyError(Exception):
    """Anything that should stop the run with a readable message."""


def host_triplet() -> str:
    """The Arduino host name for this machine, as a package index spells it."""
    machine = platform.machine().lower()
    system = platform.system()
    if system == "Windows":
        return "i686-mingw32" if machine in ("x86", "i386") else "x86_64-mingw32"
    if system == "Darwin":
        return "arm64-apple-darwin" if machine == "arm64" else "x86_64-apple-darwin"
    if system == "Linux":
        if machine in ("aarch64", "arm64"):
            return "aarch64-linux-gnu"
        if machine.startswith("arm"):
            return "arm-linux-gnueabihf"
        if machine in ("i386", "i686"):
            return "i686-pc-linux-gnu"
        return "x86_64-pc-linux-gnu"
    raise VerifyError(f"unsupported system: {system}")


def explain_index_failure(output: str) -> None:
    """Say why `core update-index` failed, in terms of what to change.

    A failed refresh commits nothing - not even the URLs that did download - so
    the install that follows reports only "Platform not found", which points at
    nothing. What distinguishes the two real causes is WHICH url arduino-cli
    could not fetch, and only arduino-cli's own output knows that: probing from
    here would always succeed for the loopback server this script started.
    """
    failed = [line.strip() for line in output.splitlines()
              if "Download failed" in line or "could not be updated" in line]
    ours = [line for line in failed if "package_toppers_index.json" in line]
    theirs = [line for line in failed if "package_toppers_index.json" not in line
              and "Download failed" in line]

    print("  -- why the refresh failed")
    for line in failed:
        print(f"     {line}")
    if ours and not theirs:
        print("  Only the index this script serves failed. A proxy is "
              "swallowing the loopback URL:")
        print("  ARDUINO_NETWORK_PROXY applies to every request and has no "
              "bypass list, so pointing")
        print("  arduino-cli at a proxy that cannot reach 127.0.0.1 breaks "
              "exactly this fetch.")
    elif theirs and not ours:
        print("  Only the public indexes failed. Give arduino-cli a route to "
              "the internet with")
        print("  ARDUINO_NETWORK_PROXY=http://<proxy>:<port> - it ignores "
              "HTTP_PROXY/HTTPS_PROXY.")
    else:
        print("  Both the public indexes and the local one failed; check the "
              "lines above.")
    if ours or theirs:
        print("  Note the two needs can be irreconcilable: arduino-cli has one "
              "global proxy, so a")
        print("  network whose proxy reaches the internet but not loopback "
              "cannot satisfy both. Run")
        print("  this check where that holds - CI does, in "
              ".github/workflows/verify-package.yml.")
    print("  --")


def run(command: list[str], what: str, cwd: Path | None = None,
        capture: bool = False) -> str:
    print(f"  $ {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd, text=True,
                            capture_output=capture)
    if result.returncode != 0:
        if capture:
            print(result.stdout)
            print(result.stderr, file=sys.stderr)
        raise VerifyError(f"{what} failed (exit {result.returncode})")
    return result.stdout if capture else ""


def freeze_driver(repository: Path, work: Path, host: str) -> Path:
    """Build the one-file driver for this host and zip it as a tool."""
    print("Freezing the link driver")
    distribution = work / "dist"
    run([sys.executable, "-m", "PyInstaller", "--onefile",
         "--name", "fmp3-link", "--distpath", str(distribution),
         "--workpath", str(work / "pyinstaller"),
         "--specpath", str(work),
         str(repository / "scripts" / "fmp3_link.py")],
        "PyInstaller", capture=True)

    binary = distribution / ("fmp3-link.exe" if os.name == "nt" else "fmp3-link")
    if not binary.is_file():
        raise VerifyError(f"PyInstaller produced no binary at {binary}")

    #  The driver must fail with its own message rather than a traceback when
    #  the stage is missing; that also proves the frozen binary starts at all.
    probe = subprocess.run([str(binary), "--stage", "does-not-exist",
                            "--build-path", ".", "--project-name", "x.ino"],
                           text=True, capture_output=True)
    if probe.returncode == 0 or "Prebuilt stage is incomplete" not in (
            probe.stdout + probe.stderr):
        raise VerifyError("the frozen driver did not report a missing stage: "
                          + (probe.stdout + probe.stderr).strip())

    archive = work / f"fmp3-link-{host}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as handle:
        entry = zipfile.ZipInfo("fmp3-link/" + binary.name)
        entry.compress_type = zipfile.ZIP_DEFLATED
        #  0o755: the executable bit has to survive on macOS and Linux.
        entry.external_attr = (0o100755 << 16)
        handle.writestr(entry, binary.read_bytes())
    print(f"  driver: {archive} ({archive.stat().st_size // 1024} KB)")
    return archive


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args, **kwargs) -> None:
        pass


def serve(directory: Path, port: int) -> http.server.ThreadingHTTPServer:
    handler = functools.partial(_QuietHandler, directory=str(directory))
    #  Loopback only: this exists to satisfy Boards Manager, not to publish.
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Serving the package on http://127.0.0.1:{port}")
    return server


def main() -> int:
    repository = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Verify the Boards Manager package on this machine.")
    parser.add_argument("--platform-dir", required=True,
                        help="platform to package and install")
    parser.add_argument("--version", default="0.3.0")
    parser.add_argument("--arduino-cli", default="arduino-cli")
    parser.add_argument("--driver", default="",
                        help="frozen driver zip for this host; built with "
                             "PyInstaller when omitted")
    parser.add_argument("--work-dir", default="",
                        help="default: <platform-dir>/../verify-package")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--profiles", nargs="*", choices=sorted(PROFILES),
                        default=sorted(PROFILES))
    parser.add_argument("--skip-core", action="store_true",
                        help="the M5Stack core is already installed")
    parser.add_argument("--skip-libraries", action="store_true",
                        help="M5GFX and M5Unified are already available")
    parser.add_argument("--summary", default="",
                        help="write 'profile size sha256' lines here, for "
                             "comparing hosts")
    parser.add_argument("--config-file", default="",
                        help="arduino-cli configuration, for verifying against "
                             "an isolated data directory instead of the one the "
                             "IDE uses")
    args = parser.parse_args()

    platform_dir = Path(args.platform_dir).resolve()
    if not (platform_dir / "boards.txt").is_file():
        raise VerifyError(f"not a platform directory: {platform_dir}")

    work = Path(args.work_dir).resolve() if args.work_dir else (
        platform_dir.parent / "verify-package")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    host = host_triplet()
    print(f"Host: {host}")

    driver = Path(args.driver).resolve() if args.driver else freeze_driver(
        repository, work, host)

    print("Building the package and the index")
    package = work / "pkg"
    run([sys.executable, str(repository / "scripts" / "make_package_index.py"),
         "--version", args.version,
         "--platform-dir", str(platform_dir),
         "--output-dir", str(package),
         "--driver", f"{host}={driver}",
         "--base-url", f"http://127.0.0.1:{args.port}"],
        "make_package_index.py")

    server = serve(package, args.port)
    try:
        index = f"http://127.0.0.1:{args.port}/package_toppers_index.json"
        urls = f"{M5STACK_INDEX},{index}"
        cli = [args.arduino_cli]
        if args.config_file:
            cli += ["--config-file", args.config_file]

        print("Installing through Boards Manager")
        #  update-index fails if ANY configured index cannot be refreshed, and
        #  a failed run commits none of them - not even the ones that did
        #  download. The install below then reports "Platform not found", which
        #  says nothing about the real cause, so diagnose it here instead.
        refresh = subprocess.run(
            cli + ["core", "update-index", "--additional-urls", urls],
            text=True, capture_output=True)
        print(f"  $ {' '.join(cli)} core update-index ...")
        if refresh.returncode != 0:
            print(f"  warning: core update-index failed "
                  f"(exit {refresh.returncode})")
            explain_index_failure(refresh.stdout + refresh.stderr)
        if not args.skip_core:
            #  Arduino cannot declare a dependency on a referenced platform, so
            #  the M5Stack core is a prerequisite the user installs first.
            run(cli + ["core", "install", f"m5stack:esp32@{CORE_VERSION}",
                       "--additional-urls", urls],
                "installing the M5Stack core")
        run(cli + ["core", "install", f"toppers:esp32@{args.version}",
                   "--additional-urls", urls], "installing the FMP3 platform")
        if not args.skip_libraries:
            #  --no-deps is REQUIRED, not tidiness. M5Unified 0.2.20 declares
            #  a bare "depends=M5GFX" with no version constraint, so dependency
            #  resolution installs the newest M5GFX and REPLACES the pinned one
            #  that was requested on the same command line. The first Linux run
            #  (2026-08-29) showed it happening:
            #
            #      Installing M5GFX@0.2.27...
            #      Installed M5GFX@0.2.27
            #      ...
            #      Replacing M5GFX@0.2.27 with M5GFX@0.2.28...
            #
            #  The build then used 0.2.28, which defeats the pin and makes
            #  cross-host hash comparison meaningless. Our own library.properties
            #  pin does not help: it constrains what a sketch resolves, not what
            #  `lib install` does. Both libraries are named explicitly here, so
            #  there is nothing for the resolver to add anyway.
            run(cli + ["lib", "install", "--no-deps"]
                + [f"{name}@{version}"
                   for name, version in LIBRARY_VERSIONS.items()],
                "installing the M5 libraries")

        print("Building every runtime profile")
        results = []
        failures = 0
        for option in args.profiles:
            for example in PROFILES[option]:
                label = f"{option}/{example}"
                output = work / f"out-{option}-{example}"
                print(f"  {label}")
                try:
                    #  No library path: the library is bundled in the platform.
                    run(cli + ["compile", "-b",
                               f"{BOARD}:FMP3Runtime={option}",
                               "--build-path",
                               str(work / f"bp-{option}-{example}"),
                               "--output-dir", str(output),
                               str(repository / "examples" / example)],
                        f"building {label}", capture=True)
                except VerifyError as error:
                    print(f"    {error}")
                    results.append((label, "BUILD FAILED", ""))
                    failures += 1
                    continue
                image = output / f"{example}.ino.bin"
                if not image.is_file() or image.stat().st_size < 4096:
                    results.append((label, "NO IMAGE", ""))
                    failures += 1
                    continue
                digest = hashlib.sha256(image.read_bytes()).hexdigest()
                results.append((label, f"{image.stat().st_size} bytes",
                                digest))
    finally:
        server.shutdown()

    print()
    print(f"{'profile/sketch':<30}{'image':<16}sha256")
    for label, size, digest in results:
        print(f"{label:<30}{size:<16}{digest[:32]}")
    if args.summary:
        rows = [f"{label} {size.split()[0]} {digest}"
                for label, size, digest in results if digest]
        Path(args.summary).write_text("\n".join(rows) + "\n",
                                      encoding="utf-8", newline="\n")
        print(f"summary: {args.summary}")
    print()
    if failures:
        print(f"FAILED: {failures} of {len(results)} builds")
        return 1
    print(f"PASSED: {len(results)} builds from the installed package "
          f"on {host}")
    print("Compare the hashes with a run on another host; they are expected to "
          "match.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except VerifyError as error:
        print(f"verify_package: {error}", file=sys.stderr)
        sys.exit(1)
