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

## BlueDroid (ESP-IDF Bluetooth host stack)

`third_party/bluedroid/` — Espressif Systems, Apache License 2.0.
ESP-IDF commit `735507283d5b2f9fb363a1901172dbd9e847945d` (v5.5.4) の
`components/bt/` からの無改変コピー。由来と経緯は
[`third_party/bluedroid/README.vendored.md`](third_party/bluedroid/README.vendored.md)。

