# Third-party notices

This repository does not apply one license to every file. Each imported or derived file
must retain its original copyright and license header. Component-level notices will be
expanded as code is incorporated.

The parts written for this repository are under the TOPPERS License, whose full text is
in `LICENSE`. Their file headers carry the short form, which says the conditions are in
another source file's header; `LICENSE` is where those conditions now are.

## Current project contents

The Arduino library, the build integration scripts, and the sketch bridge were newly
written for this repository. They do not copy Arduino-ESP32, M5Unified, or M5GFX
implementation source.

`third_party/fmp3_core` is a Git submodule of
`https://github.com/toppers/fmp3_core.git`, pinned to commit
`685b36a98175e3093c0931b713729b9030774e16` on branch `fix/p4-clic-ipm`. Its files
remain governed by their original file-level copyright and TOPPERS license
notices.

`ports/m5stack_xtensa/runtime` contains the Xtensa LX7 common layer,
ESP32-S3 chip layer, CoreS3 target layer, XIP linker, and host build support
selected from that `esp32_s3` repository. Original file headers are retained.
Arduino Release packages expand the pinned `fmp3_core` sources and this selected
port under `extras/runtime`; they do not include the `esp32_s3/esp-idf` source tree.

The Wi-Fi profiles additionally contain the selected ESP shim, Wi-Fi HAL
integration sources, configuration headers, and compatibility headers under
`ports/m5stack_xtensa/runtime/wifi`, selected from the same `esp32_s3` source
commit. Original Espressif and project file headers are retained. At build time they
link Wi-Fi driver, coexistence, PHY, lwIP, and related binary archives supplied by
M5Stack Arduino core 3.3.8; those archives are not copied into the Release ZIP.

The WPA2 Connect profile includes `libsupplicant.a` and `libmbedcrypto.a` under
`ports/m5stack_xtensa/runtime/wifi/prebuilt/wpa2`. They were built from ESP-IDF
v5.5.4 by `esp/boot/build_wpa_libs_espidf_esp32s3.sh` in the same source repository
at commit `1683046b0de6b0361e047a4f09add39a3df10b29`, with hardware crypto disabled
for TOPPERS/FMP3 compatibility. Their ESP-IDF WPA supplicant and mbedTLS license
notices remain authoritative; their license texts, provenance, and SHA-256 values
are included beside the archives.

The M5Unified profile contains 25 selected FMP3 compatibility, C/C++ runtime,
I2C/SPI/GPIO shim, and diagnostic support files under
`ports/m5stack_xtensa/runtime/m5`, copied from the same `esp32_s3` commit
`1683046b0de6b0361e047a4f09add39a3df10b29`. Existing file headers are retained.
This provenance record does not infer one repository-wide license for files that do
not carry a standalone license statement. M5GFX and M5Unified implementation sources
are not included in the Release ZIP; the build uses the versions selected by Arduino.

## Reference sources

The following sources inform the design but are not otherwise vendored into this
repository:

- the `ToppersASP-renesas_uno` Arduino library
- the remaining, non-vendored parts of the `esp32_s3` port
  (`https://github.com/toppers/fmp3_esp_idf.git`)
- `h7ga40/Arduino_TOPPERS_ASP_FreeRTOS_API` (TOPPERS license in file headers)
- M5Stack Arduino-ESP32 core 3.3.8
- M5Unified and M5GFX submodules referenced by `esp32_s3`

When source code is imported or adapted, preserve its file header, record the exact
source commit, and document the modification boundary here.

## ESP32-C6 (M5NanoC6) port

`ports/m5stack_riscv/runtime` contains the RISC-V chip layer (`arch/riscv_gcc/esp32c6`),
the M5NanoC6 target layer (`target/m5nanoc6_gcc`), the seam boot glue, and the
`esp32c6`/`hal_stub_include` configuration headers, selected from the development
repository `https://github.com/exshonda/fmp3_esp_idf_dev.git` at commit
`c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe` (2026-09-15). That repository is not yet
public; this follows the same direct-provenance precedent already used for
`ports/m5stack_xtensa/runtime/wifi/prebuilt/wpa2`. Its files remain governed by their
original TOPPERS / project license headers; the vendored `sdkconfig.h` header carries
Espressif's Apache License 2.0 header, which is retained unchanged.

The full file-by-file provenance, the exact source path for every file, and the
boundary and rationale of every modification (five kinds: SDK path remapping,
`.init_array`/`.ctors` linker input, `.eh_frame`/`.gcc_except_table` linker input,
the `TA_FPU` removal, and the removal of the `TOPPERS_XIP_PADDR_PROBE` block) are
recorded in
[`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`](ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md);
this notice does not restate that list. `ports/m5stack_riscv/app/phase3` and
`ports/m5stack_riscv/runtime/seam/newlib_syscalls.c` were newly written for this
repository (the former derived from `ports/m5stack_xtensa/app/phase3`), not taken from
the development repository; see the same provenance file for details. At build time
the port compiles and links against the RISC-V toolchain (`esp-rv32`) and the ESP32-C6
SDK headers, linker scripts and libraries (`esp32c6-libs`) that the M5Stack Arduino
core 3.3.8 installs; none of those files are copied into this repository or into the
Release ZIP.

### 段3: wifi-connect profile の追加出典（2026-09-15）

`ports/m5stack_riscv/runtime/wifi/` の Wi-Fi shim は、`ports/m5stack_xtensa/runtime/wifi/shim`
（公開版由来の fork）とは独立した**第2コピー**であり、開発リポジトリ `c7fef18`（上記と同じ出典）の
`esp/shim/` をそのまま vendoring したもの（Xtensa 側は無改変のまま）。1 本ごとの出自・改変の
有無・改変境界は
[`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`](ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md)
「段3 Task 1」「段3 Task 2」節を正本とし、本notice では再掲しない。

esp-idf 原本の同梱（D8 の逸脱）は、この段でさらに 3 本増えた: `netif_esp32s3.c` が
`#include` する lwIP contrib のヘッダ 3 本（`ping.h` `tcpecho_raw.h` `udpecho_raw.h`、
`runtime/wifi/net/lwip_contrib_include/`）。出典は ESP-IDF v5.5.4 submodule に入れ子になっている
lwIP submodule（`fd432e4ee2`）の `contrib/apps/{ping,tcpecho_raw,udpecho_raw}/`、
BSD-3-Clause。M5Stack Arduino core 3.3.8 の SDK は lwIP contrib apps のヘッダを含まないため
（実体は `liblwip.a` の中にある）、ヘッダだけを補って同梱している。D8 の逸脱
（`BUILDING.md`「ESP-IDF を複製しない」からの逸脱）は、esp-idf 原本 6 本（Apache-2.0:
`periph_ctrl.c` `modem_clock.c` `modem_clock_hal.c` `efuse_hal.c` `efuse_hal_esp32c6.c`
`phy_init_data.c`）+ 上記 lwIP contrib ヘッダ 3 本（BSD-3-Clause）の計 9 本。段5 で core の
`.a` メンバ + `vPort*` シム案へ再評価する方針（`docs/c6-port.md` D8）は不変。

`ports/m5stack_riscv/runtime/wifi/prebuilt/` には ESP-IDF v5.5.4（`735507283d`）由来の
プリビルトアーカイブ 4 本を同梱する（いずれも開発リポジトリ `c7fef18` 時点で 1 回建てた物）:

- `wpa2/esp32c6/libsupplicant.a` `libmbedcrypto.a` `libmbedtls.a` -- 開発リポジトリの
  `esp/boot/build_wpa_libs_espidf_esp32c6.sh` / `build_mbedtls_tls_espidf_esp32c6.sh` で
  ソフトウェア暗号のみで建てた物。WPA supplicant は BSD-3-Clause、mbedTLS は 3.6.5
  （`components/mbedtls/mbedtls` `ffb280bb63`）で Apache-2.0 OR GPL-2.0-or-later。
  `libmbedtls.a` は Xtensa 側の同名ディレクトリには無い C6 固有の追加で、supplicant の
  EAP-TLS 経路が要求する（WPA2-PSK 経路では未使用と実測済み）。
- `lwip/esp32c6/liblwip.a` -- 開発リポジトリの `build_lwip_lib_espidf_esp32c6.sh` を
  `PORT_EXTRA`=本リポジトリの vendored `lwipopts.h`（`LWIP_DNS 1`）・`OUT_DIR`=別出力で
  建てた ESP-IDF の lwIP、BSD-3-Clause。段4 Task 0（2026-09-15）で `LWIP_DNS 1` へ
  再生成した（sha256 `5bfbc3ef...`、473,586 B）。dev golden の `.a`（`LWIP_DNS 0`、
  sha256 `85859f70...`）とは別物で、差分は `lwipopts.h` のみ（R12 の逸脱として
  `IMPORT_PROVENANCE.md` 方針 8 に記録）。

sha256・生成台本・上流ライセンス本文は `wifi/prebuilt/{wpa2,lwip}/README.md` に記録済み
（`BUILDING.md` の要求どおり、アーカイブと README は同じコミットで更新する）。

### 段5: `netif_esp32s3.c` の in-file 改変（R12 例外 9、2026-09-15）

段3 で vendoring した `runtime/wifi/net/netif_esp32s3.c`（上記「段3」節の
第2コピー）は、段5 の判断 S5-3（dev 由来の診断フック--port 7 の TCP/UDP
echo サーバ、DHCP直後のgateway ping 1回、`ip=`/`gw=`付きログ行--を出荷物では
既定OFFにする）に伴い、初めて内容を改変した。呼出し元
（`toppers_wifi_connect.c`）側に切替口が無く、`tcpip_init_done`／
`netif_status_cb` の内部で無条件に呼ばれていたため、該当する各ブロックを
`#if TOPPERS_C6_NET_DIAG`（既定 0）で囲む最小改変とした（+38/-1行、リンク
される記号・文字列そのものはOFFビルドで一切残らないことを`nm`/`strings`で
確認済み）。`netif_esp32s3.h`は無改変（宣言は残るため、ONを意図せず呼ぶと
リンクエラーで気づける）。ライセンス・著作権表示・出自は変更していない
（元のESP-IDF由来コードの表示のまま）。詳細は
`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`「改変の方針」9番。

## BlueDroid (ESP-IDF Bluetooth host stack)

`third_party/bluedroid/` — Espressif Systems, Apache License 2.0.
ESP-IDF commit `735507283d5b2f9fb363a1901172dbd9e847945d` (v5.5.4) の
`components/bt/` からの無改変コピー。由来と経緯は
[`third_party/bluedroid/README.vendored.md`](third_party/bluedroid/README.vendored.md)。

