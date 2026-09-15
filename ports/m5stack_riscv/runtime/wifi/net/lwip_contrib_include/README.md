# lwIP contrib app headers (ESP32-C6 wifi-connect profile)

`ping.h`, `tcpecho_raw.h`, and `udpecho_raw.h` are the public headers for the
three lwIP contrib apps that `../netif_esp32s3.c` includes
(`#include "ping.h"` etc.). Their compiled objects (`lw_ping.o`,
`lw_tcpecho_raw.o`, `lw_udpecho_raw.o`) already live inside
`../../prebuilt/lwip/esp32c6/liblwip.a`; only the headers are missing from
the M5Stack Arduino core 3.3.8 SDK (`esp32c6-libs/3.3.8/include/lwip/` does
not ship `contrib/apps/`), so only the headers are vendored here.

Provenance:

- Source: ESP-IDF v5.5.4 (`735507283d`) `components/lwip/lwip`, a nested Git
  submodule pinned to commit `fd432e4ee2`, path
  `contrib/apps/{ping,tcpecho_raw,udpecho_raw}/{ping,tcpecho_raw,udpecho_raw}.h`
- Taken from the development repository
  `https://github.com/exshonda/fmp3_esp_idf_dev.git` at commit
  `c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe` (same base as the rest of
  `ports/m5stack_riscv/runtime/wifi/`); content is unmodified (byte-identical
  to the lwIP source)
- License: lwIP, BSD-3-Clause

This is part of the D8 deviation from `BUILDING.md`'s "do not duplicate
ESP-IDF" rule (esp-idf-derived sources vendored because the SDK does not
carry them); see `docs/c6-port.md` (D8) and
`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md` for the full record and
re-evaluation plan (stage 5).
