# FMP3-compatible WPA2 archives for the ESP32-C6 (M5NanoC6)

This directory holds the WPA supplicant and the software-cryptography mbedTLS
pair the C6 `wifi-connect` runtime profile links, under `esp32c6/`. They are
the RISC-V (RV32IMAC) counterparts of `ports/m5stack_xtensa/runtime/wifi/
prebuilt/wpa2/{esp32s3,esp32}/` and are not interchangeable with them. The
Wi-Fi driver blobs (`libnet80211` `libpp` `libcore` `libcoexist` `libmesh`)
and `libphy` continue to come from the M5Stack Arduino core (esp32c6-libs
3.3.8); lwIP is the FMP3-built archive in `../lwip/esp32c6/` (D7, not the
core's).

The corresponding archives bundled with Arduino core 3.3.8 cannot be linked
into this runtime for the same reason as on the Xtensa boards: its
`libmbedcrypto.a` is built for the ESP-IDF runtime (FreeRTOS, esp_timer,
hardware SHA/AES/MPI drivers) which is not present under TOPPERS/FMP3. The C6
has no FPU and no hardware-crypto path in this port, so the set is
software cryptography only.

Unlike the Xtensa set, `libmbedtls.a` (the TLS layer) is included: the dev C6
Wi-Fi build links it in the same `--start-group` (`cmake/a1_c6_stage1.cmake`),
and the supplicant's EAP-TLS objects reference `mbedtls_ssl_*` from it. In a
WPA2-PSK station link no member of it is taken (measured on the stage 3
Task 1 smoke link); the stage links what the dev image links.

Provenance:

- Source repository: `https://github.com/exshonda/fmp3_esp_idf_dev.git`
  (the development repository, not a public snapshot; same base as
  `packaging/release-allowlist.json` `portBaseRepositoryC6` /
  `portBaseCommitC6`)
- Source base commit: `c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe` (2026-09-15).
  The archives are the files `esp/lib/wpa_esp32c6_espidf/{libsupplicant,
  libmbedcrypto,libmbedtls}.a` of that tree, built on 2026-09-14 by the
  recipes below (the "fix round 1" build of the dev stage-4 Task 2; the
  checksums below equal the ones recorded there).
- Build recipes: `esp/boot/build_wpa_libs_espidf_esp32c6.sh`
  (`libsupplicant.a` + `libmbedcrypto.a`, 147 sources compiled, none failed)
  and `esp/boot/build_mbedtls_tls_espidf_esp32c6.sh` (`libmbedtls.a`,
  9 sources). Toolchain `riscv32-esp-elf` esp-14.2.0_20260121 (the M5Stack
  core's `esp-rv32/2601` is the same release).
- ESP-IDF source version: v5.5.4 (`735507283d`), mbedTLS 3.6.5
  (`components/mbedtls/mbedtls` at `ffb280bb63`)
- Configuration: `mbedtls/esp_config.h` + the dev
  `esp/wifi/net/mbedtls_user_config.h` (no filesystem, no PSA storage, no
  server side, no time); software cryptography, ESP hardware AES/SHA/MPI
  drivers not compiled
- Target: RISC-V RV32IMAC (`-march=rv32imac_zicsr_zifencei -mabi=ilp32`),
  TOPPERS/FMP3 compatibility stubs (`../../config/hal_stub_include`)

Archive checksums (SHA-256) and sizes:

- `esp32c6/libsupplicant.a` (551514 bytes):
  `DEEA3B3515B3780CCB48A4469097B271E896C318460E3494EE3F0F33DC88BF4B`
- `esp32c6/libmbedcrypto.a` (879498 bytes):
  `AB0B175A32745B3B71A09F5D7640B5B4F25D3C7366EADF4F6D040E3456AE05D3`
- `esp32c6/libmbedtls.a` (204834 bytes):
  `993C0C95D8CDD2C7E9F03F7519228CF82D6A0B6FE79989F1EA1491E3E5C0836A`

`python3 scripts/check_host_paths.py` passes on all three (no build-machine
paths inside).

These archives contain code from ESP-IDF's WPA supplicant (BSD-3-Clause,
`WPA_SUPPLICANT_COPYING.txt`), ESP-IDF's mbedTLS port and mbedTLS itself
(`MBEDTLS_LICENSE.txt`: Apache-2.0 OR GPL-2.0-or-later; `ESP_IDF_LICENSE.txt`:
Apache-2.0). Those notices remain authoritative; this README does not assert a
single license for every object in the archives. Update the archives only
together with this file (source commit, recipe, checksums, licenses), as
`BUILDING.md` requires for `wifi/prebuilt`.
