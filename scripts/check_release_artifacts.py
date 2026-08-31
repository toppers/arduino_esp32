#!/usr/bin/env python3
"""Check an assembled release before it is published.

A release is three kinds of artifact - the platform archive,
one frozen link driver per host, and the index that ties them together - and the
index repeats each archive's SHA-256 and size. Boards Manager enforces those
checksums, so a mismatch is not a warning: the install fails at the user, with a
message that says nothing about which of our steps went wrong.

The realistic failure is not a corrupt file. It is a release assembled from
parts that do not belong together - an index regenerated after a zip was
replaced, a driver artifact from an older CI run, a host quietly missing because
its runner never picked the job up. Every one of those produces a plausible
looking directory. This reads the index, compares it against what is actually on
disk, and against what packaging/release-allowlist.json says a release must
carry.

    python scripts/check_release_artifacts.py --release-dir build/package

Exit status is 0 when everything checks out, 1 when something is wrong, and 2
when the check could not run at all (no index, no allowlist) - so a failure to
check never reads as a pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path

ALLOWLIST = "packaging/release-allowlist.json"
INDEX_NAME = "package_toppers_index.json"


def sha256_and_size(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def declared_checksum(entry: dict) -> str:
    """The index writes "SHA-256:<hex>"; compare on the hex alone."""
    raw = str(entry.get("checksum", ""))
    prefix, _, value = raw.partition(":")
    if prefix.upper().replace("-", "") != "SHA256":
        raise ValueError(f"unexpected checksum algorithm: {raw!r}")
    return value.lower()


def host_triplet() -> str:
    """Arduino's name for this host, matching what the index declares."""
    machine = platform.machine().lower()
    if sys.platform == "win32":
        return "x86_64-mingw32"
    if sys.platform == "darwin":
        return "arm64-apple-darwin" if machine in ("arm64", "aarch64") \
            else "x86_64-apple-darwin"
    if machine in ("aarch64", "arm64"):
        return "aarch64-linux-gnu"
    return "x86_64-pc-linux-gnu"


def check_archive(problems: list[str], label: str, release: Path,
                  entry: dict) -> None:
    """One index entry against the file it names."""
    name = entry.get("archiveFileName", "")
    if not name:
        problems.append(f"{label}: the index entry has no archiveFileName")
        return
    path = release / name
    if not path.is_file():
        problems.append(f"{label}: {name} is named by the index but not present")
        return
    actual_digest, actual_size = sha256_and_size(path)
    try:
        expected = declared_checksum(entry)
    except ValueError as error:
        problems.append(f"{label}: {error}")
        return
    if actual_digest != expected:
        problems.append(
            f"{label}: {name} does not match the index checksum\n"
            f"       index: {expected}\n"
            f"       file : {actual_digest}\n"
            "       (regenerate the index from the archives you are shipping; "
            "this is what an index built against an older zip looks like)")
    declared_size = str(entry.get("size", ""))
    if declared_size and declared_size != str(actual_size):
        problems.append(
            f"{label}: {name} is {actual_size} bytes, index says {declared_size}")
    if actual_digest == expected and (not declared_size
                                      or declared_size == str(actual_size)):
        print(f"  ok   {label:<34} {name}")


def check_driver_version(problems: list[str], release: Path, tool: dict,
                         repository: Path) -> None:
    """Compare the frozen driver with the source it was frozen from.

    Only the driver for THIS host can be executed, so the others are reported
    as unchecked rather than passed - the point of this file is that a check
    which did not run must not look like one that did.
    """
    source = repository / "scripts" / "fmp3_link.py"
    if not source.is_file():
        problems.append(f"the driver source is missing: {source}")
        return
    try:
        expected = subprocess.run(
            [sys.executable, str(source), "--version"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, OSError) as error:
        problems.append(f"could not run the driver source for --version: {error}")
        return

    this_host = host_triplet()
    for system in tool.get("systems", []):
        if system.get("host") != this_host:
            print(f"  --   driver {system.get('host'):<27} "
                  "not runnable here, version unchecked")
            continue
        archive = release / system.get("archiveFileName", "")
        if not archive.is_file():
            continue
        print(f"  ..   driver {this_host:<27} unpacking to check --version")
        import tempfile
        import zipfile
        with tempfile.TemporaryDirectory() as work:
            with zipfile.ZipFile(archive) as handle:
                handle.extractall(work)
            binaries = [p for p in Path(work).rglob("fmp3-link*")
                        if p.is_file() and p.suffix.lower() in ("", ".exe")]
            if not binaries:
                problems.append(
                    f"{archive.name} holds no fmp3-link binary")
                return
            binary = binaries[0]
            binary.chmod(0o755)
            try:
                actual = subprocess.run([str(binary), "--version"],
                                        capture_output=True, text=True,
                                        check=True).stdout.strip()
            except (subprocess.CalledProcessError, OSError) as error:
                problems.append(
                    f"the frozen driver for {this_host} would not run: {error}")
                return
        if actual != expected:
            problems.append(
                f"the frozen driver for {this_host} reports {actual!r}, but "
                f"scripts/fmp3_link.py is {expected!r} - the package would ship "
                "a driver built from different source than this tree")
        else:
            print(f"  ok   driver {this_host:<27} {actual}")


def check_version_agreement(problems: list[str], repository: Path,
                            platforms: list[dict]) -> None:
    """The three places a version appears must say the same thing.

    library.properties is what Arduino reads, libraryInfo() is what a sketch
    can print, and the index version is what Boards Manager shows. Nothing
    tied them together, and the C++ literal sat a whole release behind while
    the package shipped as 0.3.0 - a user printing the version got 0.2.0 and
    no build ever complained.
    """
    properties = repository / "library.properties"
    source = repository / "src" / "ToppersFMP3_M5CoreS3.cpp"
    if not properties.is_file() or not source.is_file():
        problems.append("cannot check the version: library.properties or "
                        "src/ToppersFMP3_M5CoreS3.cpp is missing")
        return

    declared = ""
    for line in properties.read_text(encoding="utf-8").splitlines():
        if line.startswith("version="):
            declared = line[len("version="):].strip()
            break
    if not declared:
        problems.append("library.properties has no version=")
        return

    #  Match on the literal's SHAPE, not its position. Counting literals from
    #  the start of the file picks up the #include, and would go on picking up
    #  whatever literal is added above it next.
    reported_all = re.findall(r'"(\d+\.\d+\.\d+[^"]*)"',
                              source.read_text(encoding="utf-8"))
    if len(reported_all) != 1:
        problems.append(
            f"expected exactly one version literal in {source.name}, found "
            f"{len(reported_all)}: {reported_all}. The check cannot tell which "
            "one libraryInfo() returns.")
        return
    reported = reported_all[0]
    if reported != declared:
        problems.append(
            f"libraryInfo() reports version {reported!r} but "
            f"library.properties says {declared!r}. Sketches print the former; "
            "update src/ToppersFMP3_M5CoreS3.cpp.")
    else:
        print(f"  ok   version                          {declared} in "
              "library.properties and libraryInfo()")

    for entry in platforms:
        packaged = entry.get("version", "")
        if packaged != declared:
            problems.append(
                f"the index publishes platform {packaged!r} but the library it "
                f"bundles is {declared!r}. Boards Manager shows the platform "
                "version, so the two must agree; pass --version to "
                "make_package_index.py to match library.properties.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-dir", required=True,
                        help="directory holding the index and the archives")
    parser.add_argument("--repository", default=".",
                        help="repository root, for the allowlist and the "
                             "driver source")
    parser.add_argument("--skip-driver-version", action="store_true",
                        help="do not execute the frozen driver")
    args = parser.parse_args(argv)

    release = Path(args.release_dir).resolve()
    repository = Path(args.repository).resolve()

    index_path = release / INDEX_NAME
    if not index_path.is_file():
        print(f"check_release_artifacts: no {INDEX_NAME} in {release}",
              file=sys.stderr)
        return 2
    allowlist_path = repository / ALLOWLIST
    if not allowlist_path.is_file():
        print(f"check_release_artifacts: no allowlist at {allowlist_path}",
              file=sys.stderr)
        return 2

    index = json.loads(index_path.read_text(encoding="utf-8"))
    allowlist = json.loads(allowlist_path.read_text(encoding="utf-8"))
    wanted = allowlist.get("releaseArtifacts")
    if not wanted:
        print("check_release_artifacts: the allowlist has no releaseArtifacts "
              "section to check against", file=sys.stderr)
        return 2

    problems: list[str] = []
    packages = index.get("packages", [])
    if len(packages) != 1:
        print(f"check_release_artifacts: expected one package, found "
              f"{len(packages)}", file=sys.stderr)
        return 2
    package = packages[0]

    print(f"release  : {release}")
    print(f"index    : {INDEX_NAME}")

    platforms = package.get("platforms", [])
    if not platforms:
        problems.append("the index declares no platform")
    for entry in platforms:
        check_archive(problems, f"platform {entry.get('version', '?')}",
                      release, entry)
    check_version_agreement(problems, repository, platforms)

    tools = {tool.get("name"): tool for tool in package.get("tools", [])}
    driver_name = wanted["linkDriver"]["toolName"]
    driver = tools.get(driver_name)
    if driver is None:
        problems.append(
            f"the index declares no {driver_name} tool. A package without it "
            "installs and then fails to build on every host - see "
            "make_package_index.py --driver.")
    else:
        present = {system.get("host") for system in driver.get("systems", [])}
        for host in wanted["linkDriver"]["hosts"]:
            if host not in present:
                problems.append(
                    f"no {driver_name} driver for {host}, which "
                    f"{ALLOWLIST} lists as required. Users on that host cannot "
                    "build. Either take the artifact from CI, or remove the "
                    "host from the allowlist and say so in the release notes.")
        for extra in sorted(present - set(wanted["linkDriver"]["hosts"])):
            print(f"  note driver for {extra} is present but not required")
        for system in driver.get("systems", []):
            check_archive(problems, f"driver {system.get('host', '?')}",
                          release, system)
        if not args.skip_driver_version:
            check_driver_version(problems, release, driver, repository)

    #  Whatever assembled this release, it must not carry the
    #  builder's directory: a path here ships to every user, and the 0.3.0
    #  package did exactly that until -ffile-prefix-map was added.
    sys.path.insert(0, str(repository / "scripts"))
    try:
        from check_host_paths import scan as scan_host_paths
    except ImportError as error:
        problems.append(f"could not load the host-path check: {error}")
    else:
        for entry in platforms:
            archive = release / entry.get("archiveFileName", "")
            if archive.is_file():
                leaks: list[str] = []
                scan_host_paths(archive, leaks)
                for leak in leaks[:10]:
                    problems.append(f"build-machine path in the package: {leak}")
                if len(leaks) > 10:
                    problems.append(
                        f"... and {len(leaks) - 10} more build-machine paths")
                if not leaks:
                    print(f"  ok   host paths                        "
                          f"none in {archive.name}")

    if problems:
        print("\nFAILED")
        for problem in problems:
            print(f"  - {problem}")
        return 1
    print("\nPASSED: the release is internally consistent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
