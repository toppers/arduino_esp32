#!/usr/bin/env python3
"""Generate the sketch-facing M5GFX font table from M5GFX's own font list.

M5GFX selects fonts by pointer: M5.Display.setFont(&fonts::FreeSans12pt7b).
A sketch cannot do that here, because the sketch and the FMP3 runtime disagree
on the M5GFX class layout: Arduino compiles the sketch with -DARDUINO, so
LGFXBase derives from Arduino's Print, while the runtime compiles the same
headers without it and gets M5GFX's internal Print. The boundary between the
two is therefore plain C, and a font has to be named by an id instead.

This script parses M5GFX's lgfx_fonts.hpp and writes:

  src/ToppersFMP3_M5Fonts.h            the ids, for sketches
  ports/esp32s3_m5cores3/runtime/m5/adapter/m5_arduino_fonts.inc
                                       the id -> &fonts::X mapping, for the
                                       adapter that runs on the runtime side

Both outputs are generated together and committed, so they cannot drift apart;
regenerate them when M5GFX is updated.

U8g2font fonts are excluded on purpose. Those are the CJK sets, and one of them
is larger than the whole app partition: lgfx_efont_ja is 8.8 MB of .rodata,
lgfx_efont_tw 11.6 MB, and the partition is 3 MB. Selecting a font by id means
the mapping references every selectable font, which defeats --gc-sections, so
only fonts that can actually fit belong here.

Usage:
    python scripts/gen_m5_fonts.py [--m5gfx-src <path>] [--check]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

#  Excluded because a single one does not fit in the app partition (see above).
EXCLUDED_TYPES = {"U8g2font"}

DECLARATION = re.compile(
    r"^\s*extern\s+const\s+lgfx::(?P<type>[A-Za-z0-9_]+)\s+(?P<name>[A-Za-z0-9_]+)\s*;")


def default_m5gfx_src() -> Path:
    if sys.platform == "win32":
        base = Path.home() / "Documents" / "Arduino" / "libraries"
    elif sys.platform == "darwin":
        base = Path.home() / "Documents" / "Arduino" / "libraries"
    else:
        base = Path.home() / "Arduino" / "libraries"
    return base / "M5GFX" / "src"


def parse_fonts(header: Path) -> list[tuple[str, str]]:
    fonts: list[tuple[str, str]] = []
    for line in header.read_text(encoding="utf-8", errors="replace").splitlines():
        match = DECLARATION.match(line)
        if not match:
            continue
        font_type = match.group("type")
        name = match.group("name")
        if font_type in EXCLUDED_TYPES:
            continue
        fonts.append((font_type, name))
    return fonts


def render_header(fonts: list[tuple[str, str]], m5gfx_version: str) -> str:
    lines = [
        "#ifndef TOPPERS_FMP3_M5FONTS_H",
        "#define TOPPERS_FMP3_M5FONTS_H",
        "",
        "/*",
        " *  M5GFX のフォントをスケッチから選ぶためのID。",
        " *  scripts/gen_m5_fonts.py が M5GFX の lgfx_fonts.hpp から生成する。手で編集しない。",
        f" *  生成元: M5GFX {m5gfx_version}",
        " *",
        " *  M5GFX 本体は setFont(&fonts::名前) とポインタで指定するが、スケッチ側と",
        " *  ランタイム側で M5GFX のクラスレイアウトが一致しない（スケッチは -DARDUINO",
        " *  付きで Arduino の Print を、ランタイムは M5GFX 内蔵の Print を基底にする）。",
        " *  そのため境界は C に限り、フォントはIDで指定する。",
        " *",
        " *  CJK フォント（U8g2font）は含まない。1つで app パーティション(3MB)を超える",
        " *  （lgfx_efont_ja が 8.8MB、lgfx_efont_tw が 11.6MB）。",
        " *",
        " *  使い方:",
        " *      toppers_m5_set_font(TOPPERS_M5_FONT_FreeSans12pt7b);",
        " */",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "enum {",
    ]
    for index, (font_type, name) in enumerate(fonts):
        lines.append(f"    TOPPERS_M5_FONT_{name} = {index},"
                     f"  /* lgfx::{font_type} */")
    lines += [
        f"    TOPPERS_M5_FONT_COUNT = {len(fonts)}",
        "};",
        "",
        "/*",
        " *  フォントを選ぶ。成功で 0、未知のIDなら -1。",
        " *  ヘッダと同じリリースのランタイムに対してのみ有効（IDは生成時に決まる）。",
        " */",
        "int32_t toppers_m5_set_font(int32_t font_id);",
        "",
        "/*  ランタイムが持っているフォント数。ヘッダとの一致確認に使える。 */",
        "int32_t toppers_m5_font_count(void);",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif  /* TOPPERS_FMP3_M5FONTS_H */",
        "",
    ]
    return "\n".join(lines)


def render_include(fonts: list[tuple[str, str]], m5gfx_version: str) -> str:
    lines = [
        "/*",
        " *  ID から M5GFX のフォントへの対応表。",
        " *  scripts/gen_m5_fonts.py が生成する。手で編集しない。",
        f" *  生成元: M5GFX {m5gfx_version}",
        " *",
        " *  ここで参照したフォントはすべてイメージに載る（--gc-sections が効かない）。",
        " *  フォントを増やすときはサイズを確認すること。",
        " */",
        "",
        "static const lgfx::v1::IFont *toppers_m5_font_table[] = {",
    ]
    for font_type, name in fonts:
        lines.append(f"    &fonts::{name},")
    lines += [
        "};",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m5gfx-src", default="",
                        help="M5GFX src directory")
    parser.add_argument("--library-root", default="",
                        help="this library's root (default: parent of scripts/)")
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed output is stale")
    args = parser.parse_args()

    m5gfx_src = Path(args.m5gfx_src) if args.m5gfx_src else default_m5gfx_src()
    header = m5gfx_src / "lgfx" / "v1" / "lgfx_fonts.hpp"
    if not header.is_file():
        print(f"gen_m5_fonts: not found: {header}", file=sys.stderr)
        return 1

    properties = m5gfx_src.parent / "library.properties"
    version = "unknown"
    if properties.is_file():
        for line in properties.read_text(encoding="utf-8").splitlines():
            if line.startswith("version="):
                version = line.split("=", 1)[1].strip()
                break

    fonts = parse_fonts(header)
    if not fonts:
        print("gen_m5_fonts: no fonts parsed", file=sys.stderr)
        return 1

    root = Path(args.library_root) if args.library_root else Path(__file__).resolve().parent.parent
    outputs = {
        root / "src" / "ToppersFMP3_M5Fonts.h": render_header(fonts, version),
        root / "ports" / "esp32s3_m5cores3" / "runtime" / "m5" / "adapter"
            / "m5_arduino_fonts.inc":
            render_include(fonts, version),
    }

    stale = []
    for path, content in outputs.items():
        current = path.read_text(encoding="utf-8") if path.is_file() else ""
        if current == content:
            continue
        if args.check:
            stale.append(path)
        else:
            path.write_text(content, encoding="utf-8", newline="\n")

    if args.check:
        if stale:
            for path in stale:
                print(f"gen_m5_fonts: stale: {path}", file=sys.stderr)
            return 1
        print(f"gen_m5_fonts: up to date ({len(fonts)} fonts, M5GFX {version})")
        return 0

    print(f"gen_m5_fonts: {len(fonts)} fonts from M5GFX {version}")
    for path in outputs:
        print(f"  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
