#!/usr/bin/env python3
"""Build the Boards Manager package: a platform archive and a package index.

Installing the board currently means running a PowerShell
script by hand, which is Windows-only. With a package index the user adds one URL
to Boards Manager and installs, updates and removes the board from the IDE, on
any OS.

Distribution happens from a repository that is not decided yet, so every part of
the URL is an argument. The archives are content-addressed by their checksums, so
pointing the index at a different host later only means regenerating the index -
the archives do not have to be rebuilt.

What it produces under --output-dir:

    <packager>-esp32-<version>.zip      the platform
    package_<packager>_index.json       the index describing it

The platform archive carries boards.txt, platform.txt, programmers.txt, the
partition data, and the prebuilt FMP3 stages. platform.txt is rewritten so the
link driver comes from a tool rather than an absolute interpreter path; pass the
frozen driver archives with --driver so the index declares them.

Note on the M5Stack dependency: the boards use Arduino's core reference
(build.core=m5stack:esp32), and toolsDependencies pull the M5Stack toolchain,
esptool and SDK. Boards Manager does not install a *referenced platform*, so the
M5Stack ESP32 core still has to be installed by the user. That belongs in the
install instructions.

Verify locally without a server by pointing Boards Manager at the generated
index with a file:// URL.

Usage:
    python scripts/make_package_index.py --version 0.3.0 \
        --platform-dir <installed platform> --output-dir build/package \
        [--owner toppers] [--repo arduino_esp32] [--tag v0.3.0] \
        [--driver x86_64-mingw32=path/to/fmp3-link-x86_64-mingw32.zip ...]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

#  Versions of the M5Stack tools the platform is built against. These are the
#  versions the stages were linked with; changing either means relinking.
M5STACK_TOOL_DEPENDENCIES = [
    ("m5stack", "esp-x32", "2601"),
    ("m5stack", "esptool_py", "5.2.0"),
    ("m5stack", "esp32s3-libs", "3.3.8"),
    #  ★The M5Core board links against the ESP32 (LX6) SDK, which is a
    #  separate tool from the S3 one. It was missing while the platform held
    #  only the CoreS3, and stayed missing when the second board was added -
    #  it went unnoticed because this machine already had the tool installed
    #  for other work, so the platform installed and built anyway. On a clean
    #  machine the M5Core builds would have failed at the first include.
    ("m5stack", "esp32-libs", "3.3.8"),
]

DRIVER_TOOL_NAME = "fmp3-link"

#  Files and directories that are machine-specific or not needed by a user.
EXCLUDED_FROM_ARCHIVE = {".toppers-fmp3-platform.json", "boards.txt.bak"}


def sha256_and_size(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest(), path.stat().st_size


def rewrite_platform_txt(text: str) -> str:
    """Point the link recipes at the driver tool instead of a local interpreter.

    The development install bakes an absolute path to python and the driver
    script. In a package neither exists: the driver arrives as a tool, so it is
    addressed through {runtime.tools.<name>.path} like any other tool.
    """
    driver = ('"{runtime.tools.' + DRIVER_TOOL_NAME + '.path}/'
              '{tools.' + DRIVER_TOOL_NAME + '.cmd}"')
    lines = []
    injected = False
    for line in text.splitlines():
        if line.startswith("recipe.c.combine.pattern=") or \
                line.startswith("recipe.objcopy.bin.pattern=") or \
                line.startswith("recipe.objcopy.partitions.bin.pattern="):
            key, _, value = line.partition("=")
            #  Replace everything up to the first driver argument, which is
            #  where the interpreter and the script path sit.
            match = re.search(r"(--(?:stage|partitions)\b.*)$", value)
            #  The partitions recipe is only ours to rewrite once the installer
            #  has pointed it at the driver. The inherited gen_esp32part form
            #  has no such argument, so leave that one alone.
            if not match and line.startswith(
                    "recipe.objcopy.partitions.bin.pattern="):
                lines.append(line)
                continue
            if not match:
                raise SystemExit(f"cannot rewrite recipe line: {key}")
            line = f"{key}={driver} {match.group(1)}"
        lines.append(line)
        if not injected and line.startswith("name="):
            lines.append("")
            lines.append("#  Link driver, shipped as a tool (frozen per host).")
            lines.append("tools." + DRIVER_TOOL_NAME + ".cmd=" + DRIVER_TOOL_NAME)
            lines.append("tools." + DRIVER_TOOL_NAME + ".cmd.windows="
                         + DRIVER_TOOL_NAME + ".exe")
            injected = True
    if not injected:
        raise SystemExit("platform.txt has no name= line to anchor tool defs")
    return "\n".join(lines) + "\n"


#  A platform can ship libraries of its own, and Arduino picks them up from
#  <platform>/libraries. Bundling the library there is what removes the manual
#  "add .ZIP library" step.
LIBRARY_ITEMS = ("library.properties", "src", "keywords.txt",
                 "LICENSE", "THIRD_PARTY_NOTICES.md")

#  packaging/release-allowlist.json is the canonical definition of what gets
#  distributed. Every example is copied under the destination it names there.
#  An example stays out only by saying so - boardsManager false, with the
#  reason beside it (DualCore, M5UnifiedLink) - because allowlisted_examples
#  rejects an example that is in the repository and in no entry at all: an
#  omission cannot be told apart from forgetting.
ALLOWLIST = "packaging/release-allowlist.json"


def allowlisted_examples(library_root: Path) -> list[tuple[Path, str]]:
    allowlist = library_root / ALLOWLIST
    if not allowlist.is_file():
        raise SystemExit(f"the release allowlist is missing: {allowlist}")
    entries = json.loads(allowlist.read_text(encoding="utf-8"))["entries"]
    #  An entry can opt out of this package while staying in the ZIP release:
    #  the library ships inside the platform here, so its examples only surface
    #  when the FMP3 board is selected.
    examples = [(library_root / entry["source"], entry["destination"])
                for entry in entries
                if str(entry["source"]).startswith("examples/")
                and entry.get("boardsManager", True)]
    if not examples:
        raise SystemExit(f"{ALLOWLIST} lists no examples")
    missing = [str(source) for source, _ in examples if not source.is_file()]
    if missing:
        raise SystemExit("the allowlist points at missing examples: "
                         + ", ".join(missing))

    #  And the other direction, which nothing checked: an example in the
    #  repository that the allowlist does not mention at all.
    #
    #  examples/BluetoothSPP was absent for a whole release cycle while
    #  packaging/README.release.md told users, by name, that it was the example
    #  for the Bluetooth Classic (SPP) runtime. Nothing caught it:
    #  verify_package.py builds examples from the repository rather than from
    #  the package, so an example only has to exist to pass, and the check
    #  above only looks at what the allowlist already names.
    #
    #  Excluding one is fine - that is what boardsManager: false is for - but
    #  it has to be said out loud in the allowlist rather than by omission,
    #  because omission is indistinguishable from forgetting.
    listed = {str(entry["source"]) for entry in entries
              if str(entry["source"]).startswith("examples/")}
    on_disk = {f"examples/{sketch.parent.name}/{sketch.name}"
               for sketch in sorted((library_root / "examples").glob("*/*.ino"))}
    unlisted = sorted(on_disk - listed)
    if unlisted:
        raise SystemExit(
            f"these examples are in the repository but not in {ALLOWLIST}: "
            + ", ".join(unlisted)
            + ". Add them, or add them with \"boardsManager\": false to ship "
              "them in the ZIP only, or say why in a _comment.")
    return examples


def bundle_library(library_root: Path, platform_root: Path) -> str:
    properties = library_root / "library.properties"
    if not properties.is_file():
        raise SystemExit(f"not a library directory: {library_root}")
    name = ""
    for line in properties.read_text(encoding="utf-8").splitlines():
        if line.startswith("name="):
            name = line.partition("=")[2].strip()
    if not name:
        raise SystemExit("library.properties has no name=")
    destination = platform_root / "libraries" / name
    destination.mkdir(parents=True)
    for item in LIBRARY_ITEMS:
        source = library_root / item
        if not source.exists():
            continue
        if source.is_dir():
            #  arduino-cli writes its output into <sketch>/build when a sketch
            #  is compiled in place, and that would otherwise be distributed
            #  inside the examples.
            shutil.copytree(source, destination / item,
                            ignore=shutil.ignore_patterns("build",
                                                          "__pycache__"))
        else:
            shutil.copyfile(source, destination / item)

    #  README.md in the platform is the release document, not the repository one.
    release_readme = library_root / "packaging" / "README.release.md"
    if release_readme.is_file():
        shutil.copyfile(release_readme, destination / "README.md")

    for source, relative in allowlisted_examples(library_root):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)

    #  Fail closed rather than ship build output again.
    leaked = sorted(path.relative_to(destination).as_posix()
                    for path in destination.rglob("*")
                    if path.suffix.lower() in (".bin", ".elf", ".o", ".a",
                                               ".hex", ".map"))
    if leaked:
        raise SystemExit("the library bundle contains build output: "
                         + ", ".join(leaked[:5]))
    return name


def stage_platform(platform_dir: Path, staging: Path, root_name: str) -> Path:
    root = staging / root_name
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    for entry in sorted(platform_dir.iterdir()):
        if entry.name in EXCLUDED_FROM_ARCHIVE:
            continue
        if entry.is_dir():
            #  fmp3-tools holds the .py driver, which the package replaces with
            #  the frozen tool.
            if entry.name == "fmp3-tools":
                continue
            shutil.copytree(entry, root / entry.name)
        else:
            shutil.copyfile(entry, root / entry.name)
    platform_txt = root / "platform.txt"
    platform_txt.write_text(
        rewrite_platform_txt(platform_txt.read_text(encoding="utf-8")),
        encoding="utf-8", newline="\n")
    return root


def zip_directory(root: Path, archive: Path) -> None:
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as handle:
        for path in sorted(root.rglob("*")):
            handle.write(path, path.relative_to(root.parent).as_posix())


def board_names(boards_txt: Path) -> list[str]:
    names = []
    for line in boards_txt.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([A-Za-z0-9_]+)\.name=(.+)$", line)
        if match:
            names.append(match.group(2).strip())
    return names


def _project_website(owner: str, repo: str) -> str:
    """The project URL for websiteURL / help.online.

    --owner and --repo are required unless --base-url is given, so the release
    path always has them.  The --base-url path (verify_package.py serving over
    loopback) does not, and joining two empty strings produced the malformed
    "https://github.com//".  Fall back to library.properties, which is the same
    URL and is already the file check_release_artifacts.py holds the index
    against.  Returning "" is better than a URL that goes nowhere.
    """
    if owner and repo:
        return f"https://github.com/{owner}/{repo}"

    props = Path(__file__).resolve().parent.parent / "library.properties"
    try:
        text = props.read_text(encoding="utf-8")
    except OSError:
        return ""
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep and key.strip() == "url":
            return value.strip()
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the package index.")
    parser.add_argument("--version", required=True, help="platform version")
    parser.add_argument("--platform-dir", required=True,
                        help="platform to package (as produced by the installer)")
    parser.add_argument("--output-dir", default="build/package")
    parser.add_argument("--packager", default="toppers")
    parser.add_argument("--architecture", default="esp32")
    #  REQUIRED, deliberately. These build the URLs Boards Manager fetches the
    #  archives from, so a wrong value produces an index that installs nothing -
    #  and it fails at the user, not here. There is no default because a default
    #  is only correct while the release stays in one place, and it goes stale
    #  silently: the index still generates, still validates, and still points at
    #  a repository that does not serve these archives. Make the release say
    #  where it is publishing.
    #  Required unless --base-url is given, which replaces the GitHub URL
    #  outright (that is how verify_package.py serves over loopback).
    parser.add_argument("--owner", default="",
                        help="GitHub owner hosting the release, e.g. toppers; "
                             "required unless --base-url is given")
    parser.add_argument("--repo", default="",
                        help="repository hosting the release; "
                             "required unless --base-url is given")
    parser.add_argument("--tag", default="",
                        help="release tag (default: v<version>)")
    parser.add_argument("--base-url", default="",
                        help="overrides the GitHub release URL entirely")
    parser.add_argument("--maintainer", default="TOPPERS Project")
    parser.add_argument("--website", default="")
    parser.add_argument("--email", default="")
    parser.add_argument("--platform-name", default="TOPPERS/FMP3 M5Stack boards")
    parser.add_argument("--driver", action="append", default=[],
                        metavar="HOST=ARCHIVE",
                        help="frozen driver archive for an Arduino host triplet; "
                             "repeatable")
    parser.add_argument("--library-dir", default=".",
                        help="library to bundle into the platform's libraries/; "
                             "pass an empty string to skip it")
    parser.add_argument("--driver-version", default="",
                        help="version of the driver tool (default: --version)")
    parser.add_argument("--merge-into", default="",
                        help="an already published index whose versions must "
                             "be kept; the file served from the raw URL. "
                             "Without it the new index lists only this "
                             "version and every earlier one disappears from "
                             "Boards Manager")
    parser.add_argument("--require-merge-target", action="store_true",
                        help="fail if --merge-into names no file, instead of "
                             "starting a fresh index; use for every release "
                             "after the first")
    args = parser.parse_args()

    if not args.base_url and not (args.owner and args.repo):
        raise SystemExit(
            "make_package_index: give --owner and --repo (where the release "
            "assets will live), or --base-url to override the URL entirely. "
            "There is no default: a wrong one produces an index that installs "
            "nothing, and it fails at the user rather than here.")

    tag = args.tag or f"v{args.version}"
    base_url = args.base_url or (
        f"https://github.com/{args.owner}/{args.repo}/releases/download/{tag}")
    base_url = base_url.rstrip("/")
    website = args.website or _project_website(args.owner, args.repo)
    driver_version = args.driver_version or args.version

    platform_dir = Path(args.platform_dir)
    if not (platform_dir / "boards.txt").is_file():
        raise SystemExit(f"not a platform directory: {platform_dir}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    staging = output_dir / "staging"
    staging.mkdir(exist_ok=True)

    root_name = f"{args.packager}-{args.architecture}-{args.version}"
    root = stage_platform(platform_dir, staging, root_name)
    bundled_library = ""
    if args.library_dir:
        bundled_library = bundle_library(Path(args.library_dir), root)
    archive = output_dir / f"{root_name}.zip"
    zip_directory(root, archive)
    checksum, size = sha256_and_size(archive)

    tools = []
    driver_systems = []
    for entry in args.driver:
        host, _, path_text = entry.partition("=")
        if not path_text:
            raise SystemExit(f"--driver needs HOST=ARCHIVE, got: {entry}")
        path = Path(path_text)
        if not path.is_file():
            raise SystemExit(f"driver archive not found: {path}")
        driver_checksum, driver_size = sha256_and_size(path)
        destination = output_dir / path.name
        if path.resolve() != destination.resolve():
            shutil.copyfile(path, destination)
        driver_systems.append({
            "host": host,
            "url": f"{base_url}/{path.name}",
            "archiveFileName": path.name,
            "checksum": f"SHA-256:{driver_checksum}",
            "size": str(driver_size),
        })
    if driver_systems:
        tools.append({
            "name": DRIVER_TOOL_NAME,
            "version": driver_version,
            "systems": driver_systems,
        })

    tool_dependencies = [
        {"packager": packager, "name": name, "version": version}
        for packager, name, version in M5STACK_TOOL_DEPENDENCIES
    ]
    if driver_systems:
        tool_dependencies.append({
            "packager": args.packager,
            "name": DRIVER_TOOL_NAME,
            "version": driver_version,
        })

    #
    #  Keep the versions already published.
    #
    #  The index is served from a stable raw URL on the default branch, so each
    #  release REPLACES the file. Generated fresh it names one platform version,
    #  which means publishing 0.4.0 would silently remove 0.3.0 from Boards
    #  Manager - nobody could install or roll back to it, and nothing would say
    #  so. Merging the previous index keeps every published version listed.
    #
    #  Re-releasing the same version replaces its entry rather than duplicating
    #  it, so a corrected build of an existing tag is expressible.
    #
    previous_platforms: list = []
    previous_tools: list = []
    if args.merge_into:
        merge_path = Path(args.merge_into)
        if merge_path.is_file():
            existing = json.loads(merge_path.read_text(encoding="utf-8"))
            for package in existing.get("packages", []):
                if package.get("name") != args.packager:
                    continue
                previous_platforms = [
                    entry for entry in package.get("platforms", [])
                    if not (entry.get("architecture") == args.architecture
                            and entry.get("version") == args.version)]
                previous_tools = [
                    entry for entry in package.get("tools", [])
                    if not (entry.get("name") == DRIVER_TOOL_NAME
                            and entry.get("version") == driver_version)]
        elif args.require_merge_target:
            raise SystemExit(
                f"--merge-into names no file: {merge_path}. Publishing without "
                "the previous index would drop every version already released.")

    index = {
        "packages": [{
            "name": args.packager,
            "maintainer": args.maintainer,
            "websiteURL": website,
            "email": args.email,
            "help": {"online": website},
            "platforms": previous_platforms + [{
                "name": args.platform_name,
                "architecture": args.architecture,
                "version": args.version,
                "category": "Contributed",
                "help": {"online": website},
                "url": f"{base_url}/{archive.name}",
                "archiveFileName": archive.name,
                "checksum": f"SHA-256:{checksum}",
                "size": str(size),
                "boards": [{"name": name} for name in
                           board_names(platform_dir / "boards.txt")],
                "toolsDependencies": tool_dependencies,
            }],
            "tools": previous_tools + tools,
        }]
    }

    index_path = output_dir / f"package_{args.packager}_index.json"
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8",
                         newline="\n")

    print(f"platform archive : {archive} ({size / 1048576:.1f} MB)")
    print(f"  SHA-256        : {checksum}")
    print(f"  library        : "
          f"{bundled_library or 'not bundled (--library-dir was empty)'}")
    print(f"index            : {index_path}")
    print(f"  url base       : {base_url}")
    if not driver_systems:
        print("  note           : no --driver given, so the index has no link "
              "driver tool; a package without it cannot build.")
    #
    #  The one URL users keep. Printed here because it is not written into the
    #  index and so has no other place to be checked against reality.
    #
    if args.owner and args.repo:
        print()
        print("Publish this file as a release asset. Users add:")
        print(f"  https://github.com/{args.owner}/{args.repo}"
              f"/releases/latest/download/{index_path.name}")
        print("  ^ 'latest' resolves to the newest release that is NOT marked")
        print("    pre-release. Publishing this release as a pre-release makes")
        print("    that URL 404 for everyone. Publish it as a full release.")
    print()
    print("Try it locally:")
    print(f"  arduino-cli core update-index --additional-urls "
          f"file:///{index_path.resolve().as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
