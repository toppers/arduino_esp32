# FMP3-built lwIP archive for the ESP32-C6 (M5NanoC6)

`esp32c6/liblwip.a` is the lwIP TCP/IP stack the C6 `wifi-connect` runtime
profile links. The C6 port takes the dev repository's lwIP arrangement (D7 in
`docs/c6-port.md`): this archive plus the glue compiled into the stage
(`../../net/netif_esp32s3.c`, `../../net/port/sys_arch.c`, options in
`../../net/port/include/lwipopts.h`). The Xtensa boards use the M5Stack core's
`liblwip.a` with a different glue (`ports/m5stack_xtensa/runtime/wifi/net/`);
the two are not mixed.

The archive holds the lwIP core (IPv4, TCP, UDP, DHCP client, ARP, ICMP, raw
API, netconn/sockets) and three lwIP contrib apps (`lw_ping.o`,
`lw_tcpecho_raw.o`, `lw_udpecho_raw.o`) that `netif_esp32s3.c` starts. Their
headers are vendored beside the glue (`../../net/lwip_contrib_include/`).
`LWIP_DNS` is 0 in `lwipopts.h`: the archive has no resolver
(`lwip_getaddrinfo` / `dns_gethostbyname` are absent), which stage 3 Task 2 /
stage 4 have to decide on (`docs/c6-port.md`).

Provenance:

- Source repository: `https://github.com/exshonda/fmp3_esp_idf_dev.git`
  (development repository; same base as `../wpa2/README.md`)
- Source base commit: `c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe` (2026-09-15).
  The archive is `esp/lib/lwip_esp32c6_espidf/liblwip.a` of that tree, built
  on 2026-09-14 (dev stage-4 Task 2 "fix round 1": rebuilt with the
  `hal_stub_include` on the include path so it uses the flat `errno` the
  shim supplies instead of newlib's `__errno`; the checksum below is the one
  recorded there after that rebuild)
- Build recipe: `esp/boot/build_lwip_lib_espidf_esp32c6.sh` (42 sources
  compiled, none failed). Toolchain `riscv32-esp-elf` esp-14.2.0_20260121
- lwIP source: ESP-IDF v5.5.4 (`735507283d`) `components/lwip/lwip`
  (submodule `fd432e4ee2`)
- Target: RISC-V RV32IMAC (`-march=rv32imac_zicsr_zifencei -mabi=ilp32`),
  TOPPERS/FMP3 `sys_arch` port

Archive checksum (SHA-256) and size:

- `esp32c6/liblwip.a` (450340 bytes):
  `85859F7049A33B3F7EB889C411C84608CA605E3830B62A2AB588FE864BB01681`

`python3 scripts/check_host_paths.py` passes on it.

lwIP is BSD-3-Clause (`LWIP_COPYING.txt`, reproduced from the lwIP tree; the
ESP-IDF fork carries `ESP_IDF_LICENSE.txt`, Apache-2.0, for its additions).
Update the archive only together with this file.
