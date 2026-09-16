# FMP3-built lwIP archive for the ESP32-C6 (M5NanoC6) and ESP32-C5 (M5Stamp-C5)

`esp32c6/liblwip.a` is the lwIP TCP/IP stack the C6 `wifi-connect` runtime
profile links; `esp32c5/liblwip.a` is the C5 profile's (the C5 section is at
the end of this file). The C6 port takes the dev repository's lwIP arrangement (D7 in
`docs/c6-port.md`): this archive plus the glue compiled into the stage
(`../../net/netif_esp32s3.c`, `../../net/port/sys_arch.c`, options in
`../../net/port/include/lwipopts.h`). The Xtensa boards use the M5Stack core's
`liblwip.a` with a different glue (`ports/m5stack_xtensa/runtime/wifi/net/`);
the two are not mixed.

The archive holds the lwIP core (IPv4, TCP, UDP, DHCP client, ARP, ICMP, raw
API, netconn/sockets) and three lwIP contrib apps (`lw_ping.o`,
`lw_tcpecho_raw.o`, `lw_udpecho_raw.o`) that `netif_esp32s3.c` starts. Their
headers are vendored beside the glue (`../../net/lwip_contrib_include/`).
`LWIP_DNS` is 1 in this repository's `lwipopts.h` (stage 4 Task 0,
2026-09-15): the archive carries the resolver (`dns_gethostbyname`,
`netconn_gethostbyname`, `lwip_getaddrinfo`, `dns_setserver`, `dns_tmr`), and
`dhcp.o` requests DHCP option 6 and registers the servers itself
(`LWIP_DHCP_PROVIDE_DNS_SERVERS`). This is the one point where the archive
differs from the dev repository's golden archive
(`esp/lib/lwip_esp32c6_espidf/liblwip.a`, `LWIP_DNS 0`, sha256
`85859F70...`): it is built by the same script from the same sources and
toolchain, with `PORT_EXTRA` pointing at this repository's
`../../net/port/include` so that its `lwipopts.h` (`LWIP_DNS 1`,
`MEMP_NUM_SYS_TIMEOUT 9` for the added `dns_tmr`, `ERANGE` for `netdb.c`)
overrides the dev one. The stage TUs include that same `lwipopts.h`, so the
archive and the code compiled against it agree (the R12 exception is recorded
in `../../IMPORT_PROVENANCE.md`). The dev golden is untouched: the build
went to a scratch `OUT_DIR`, never to the dev `esp/lib`.

Provenance:

- Source repository: `https://github.com/exshonda/fmp3_esp_idf_dev.git`
  (development repository; same base as `../wpa2/README.md`)
- Source base commit: `5bdac26e` (dev tree at the time of the rebuild,
  2026-09-15; the script, the lwIP sources and the port files it reads are
  unchanged since `c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe`, the commit the
  previous `LWIP_DNS 0` archive was taken from). The script is
  deterministic: run with its defaults into a scratch `OUT_DIR` on the same
  day it reproduced the dev golden archive byte for byte (sha256
  `85859F70...`, 450340 bytes).
- Build recipe (arduino DNS variant), run from the dev repository root:
  ```
  PORT_EXTRA=<arduino_esp32>/ports/m5stack_riscv/runtime/wifi/net/port/include \
  OUT_DIR=<scratch dir> \
  bash esp/boot/build_lwip_lib_espidf_esp32c6.sh
  ```
  (42 sources compiled, none failed; `PORT_EXTRA` is searched before the
  dev `esp/wifi/net/port/include`, whose `arch/cc.h` and `arch/sys_arch.h`
  are byte-identical to the copies beside our `lwipopts.h`, so only
  `lwipopts.h` differs). Toolchain `riscv32-esp-elf` esp-14.2.0_20260121
- lwIP source: ESP-IDF v5.5.4 (`735507283d`) `components/lwip/lwip`
  (submodule `fd432e4ee2`)
- Target: RISC-V RV32IMAC (`-march=rv32imac_zicsr_zifencei -mabi=ilp32`),
  TOPPERS/FMP3 `sys_arch` port

Archive checksum (SHA-256) and size:

- `esp32c6/liblwip.a` (473586 bytes, `LWIP_DNS 1`, 2026-09-15):
  `5BFBC3EF56D795A9B13C101FCB9511944596D3328ADCBDCB4CFF2BCC9EA9D5F1`
- previous (`LWIP_DNS 0`, stage 3, = dev golden, 450340 bytes):
  `85859F7049A33B3F7EB889C411C84608CA605E3830B62A2AB588FE864BB01681`

`python3 scripts/check_host_paths.py` passes on it.

lwIP is BSD-3-Clause (`LWIP_COPYING.txt`, reproduced from the lwIP tree; the
ESP-IDF fork carries `ESP_IDF_LICENSE.txt`, Apache-2.0, for its additions).
Update the archive only together with this file.

## ESP32-C5 (M5Stamp-C5): `esp32c5/liblwip.a`

The C5 `wifi-connect` profile links `esp32c5/liblwip.a`, built the same way
(C5 plan decision A7 = the C6 D7 + stage 4 Task 0 arrangement): the dev
script for the C5, `esp/boot/build_lwip_lib_espidf_esp32c5.sh` (the C6 script
with the chip name changed; it has the same `OUT_DIR` / `PORT_EXTRA`
overrides), run with `PORT_EXTRA` pointing at this repository's
`../../net/port/include` (`LWIP_DNS 1`, `MEMP_NUM_SYS_TIMEOUT 9`, `ERANGE`)
and `OUT_DIR` a scratch directory. The dev golden `esp/lib/lwip_esp32c5_espidf/
liblwip.a` (`LWIP_DNS 0`, sha256 `85859F70...`, 450340 bytes) is untouched;
on the same day the script with its defaults into another scratch `OUT_DIR`
reproduced that golden byte for byte (the control for the script's
determinism, dev-side log `stage3/logs/task1-build-lwip-dns.txt`).

The result is byte-identical to `esp32c6/liblwip.a`: lwIP itself has no chip
code, both chips are RV32IMAC with the same `-march`, and both builds read
the same `lwipopts.h`. It is kept as a separate file so that each chip
directory is complete on its own and the stage's `lib/` is copied from one
place per chip (`runtime/CMakeLists.txt`, `LWIP_PREBUILT_ROOT`).

Provenance:

- Source repository: as above (the development repository; the C5 base is
  `packaging/release-allowlist.json` `portBaseRepositoryC5` /
  `portBaseCommitC5`)
- Source base commit: `1d96bcba32a043eb7066126550b0dbe598e4aad6` (2026-09-16;
  the C5 script was added at dev C5 stage 4 Task 2, and the lwIP sources
  and port files it reads are the same as the C6 ones)
- Build recipe (arduino DNS variant), run from the dev repository root
  (2026-09-16, 42 sources compiled, none failed):
  ```
  PORT_EXTRA=<arduino_esp32>/ports/m5stack_riscv/runtime/wifi/net/port/include \
  OUT_DIR=<scratch dir> \
  bash esp/boot/build_lwip_lib_espidf_esp32c5.sh
  ```
  Toolchain `riscv32-esp-elf` esp-14.2.0_20260121
- lwIP source and target: as above (ESP-IDF v5.5.4 `735507283d`
  `components/lwip/lwip`, submodule `fd432e4ee2`; RV32IMAC, TOPPERS/FMP3
  `sys_arch` port)

Archive checksum (SHA-256) and size:

- `esp32c5/liblwip.a` (473586 bytes, `LWIP_DNS 1`, 2026-09-16,
  = `esp32c6/liblwip.a`):
  `5BFBC3EF56D795A9B13C101FCB9511944596D3328ADCBDCB4CFF2BCC9EA9D5F1`
- dev golden it deviates from (`LWIP_DNS 0`, 450340 bytes, not shipped):
  `85859F7049A33B3F7EB889C411C84608CA605E3830B62A2AB588FE864BB01681`

`python3 scripts/check_host_paths.py` passes on it. Same licenses as above.
Update the archive only together with this file and `lwipopts.h`.

### Why the two chips keep their own copy (stage 5 decision S5-4, 2026-09-16)

`esp32c5/liblwip.a` and `esp32c6/liblwip.a` are byte-identical, so the released
platform archive carries the same 473,586 bytes twice, once per chip.  Stage 5
decided to keep it that way rather than share one file:

- The stage build reads exactly one directory per chip
  (`runtime/CMakeLists.txt`, `LWIP_PREBUILT_ROOT` = `prebuilt/lwip/<chip>`), so
  sharing would mean either a symlink (not preserved by every checkout, and not
  by the Git-for-Windows default) or a chip-selection branch in the build,
  which is the kind of coupling the chip branch exists to avoid.
- They are identical today by coincidence of inputs, not by construction: lwIP
  has no chip code and both chips are RV32IMAC with the same `-march` and the
  same `lwipopts.h`.  Any of those three can change for one chip alone (a
  different `-march`, a chip-specific option), and a shared file would then have
  to be un-shared under time pressure.
- The cost is bounded and known: 473,586 bytes of a platform archive whose C5
  stage pair already contributes several MB, and the archive is fetched once by
  Boards Manager.

Re-open this only if a third RISC-V chip is added, when the cost stops being a
constant.
