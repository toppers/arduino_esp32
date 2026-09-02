# FMP3-compatible WPA2 archives

This directory contains only the WPA supplicant and software-cryptography
archives needed by the `wifi-connect` runtime profile, one set per chip:
`esp32s3/` for CoreS3 and `esp32/` for the plain-ESP32 (Xtensa LX6) board.
The archives are not interchangeable - each is compiled for its own core -
while the license texts beside them apply to both. Wi-Fi driver blobs,
PHY support, and lwIP continue to come from the M5Stack Arduino ESP32-S3 SDK.

The corresponding archives bundled with Arduino core 3.3.8 cannot be linked
directly into this runtime: its `libmbedcrypto.a` enables ESP-IDF hardware
crypto and consequently depends on FreeRTOS queues, interrupts, GDMA, and
other ESP-IDF runtime services that are not present under TOPPERS/FMP3.

Provenance:

- Source repository: `https://github.com/exshonda/fmp3_esp_idf_dev.git`
- Source base commit: `1683046b0de6b0361e047a4f09add39a3df10b29`
- Build recipe: `esp/boot/build_wpa_libs_espidf_esp32s3.sh` (`..._esp32.sh` for LX6)
- ESP-IDF source version: v5.5.4
- Configuration: software cryptography; ESP hardware AES/SHA/MPI disabled
- Target: Xtensa ESP32-S3 and Xtensa ESP32 (LX6), TOPPERS/FMP3 compatibility stubs

Archive checksums (SHA-256):

- `esp32s3/libsupplicant.a`:
  `212FAAFE03512E07DE7ED67EFC49E65AE6F25370361CD4D3B02B52AFB1C4F173`
- `esp32s3/libmbedcrypto.a`:
  `3242C8FA215A4F9E38EE0B3EEADA88D26012B1DB4DA08B4F0ED9E443CCA760F7`
- `esp32/libsupplicant.a`:
  `7DF594DC7E1AE2EC7B6A4B00F0506DA2AB241BF1F2F28123FD77FD306D899CF2`
- `esp32/libmbedcrypto.a`:
  `2E46F5641FF9C88072B292E4CFC36787D6749235D5FF3945000FB8A916FD1915`

The LX6 pair was built on 2026-09-02 from the same ESP-IDF v5.5.4 submodule
with `build_wpa_libs_espidf_esp32.sh`: 147 sources compiled, none failed, and
the hardware-crypto check the recipe applies (no lsi/ssi instructions in
`libmbedcrypto.a`) came out at zero, which is what "software cryptography"
means here.

These archives contain code from ESP-IDF's WPA supplicant and mbedTLS source
trees. The accompanying `WPA_SUPPLICANT_COPYING.txt`, `MBEDTLS_LICENSE.txt`, and
`ESP_IDF_LICENSE.txt` files reproduce the applicable upstream license texts.
Those notices remain authoritative; this README does not assert a single license
for every object in the archives.
