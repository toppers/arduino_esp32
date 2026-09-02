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
