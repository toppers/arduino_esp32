#!/usr/bin/env bash
#
#  scripts/capture_s3_usj.sh -- gated flash-and-capture for M5AtomS3 Lite (ESP32-S3)
#  ===========================================================================
#
#  Writes the four images an `arduino-cli compile` run produces for the
#  toppers:esp32:m5atoms3lite_fmp3 board to the M5AtomS3 Lite over its USB
#  Serial/JTAG (USJ) port, then records the same port for CAPTURE_SEC seconds
#  and counts the markers the FMP3 runtime and the Arduino bridge print.
#  Verdicts are the caller's: the script only prints the counts.
#
#  This is a copy of scripts/capture_c5_usj.sh (the M5Stamp-C5 script; that
#  file and the M5NanoC6 one are untouched) with the ESP32-S3 specifics
#  folded in. What differs from the C5 script:
#    - the DUT is the M5AtomS3 Lite (MAC 34:b7:da:5e:95:1c, chip string
#      "ESP32-S3 (QFN56) (revision v0.2)", 8MB embedded flash) on hub 2-3.3
#      port 1, and the FORBIDDEN list holds BOTH RISC-V DUTs on the
#      neighbouring ports (M5NanoC6 port 3, M5Stamp-C5 port 4): this script
#      must never write to either.
#    - esptool prints one "MAC:" line for this chip, not the C5's pair of
#      "BASE MAC:" (48-bit) and "MAC:" (EUI-64), so the gate reads "MAC:".
#      DUT_CHIP is matched as a fixed substring of the "Chip type:" line and
#      defaults to "ESP32-S3" alone: package and revision are not pinned
#      (the MAC gate pins the individual board).
#    - the bootloader address is NOT a literal: it is read from the installed
#      platform's boards.txt (BOARDS_TXT, key
#      m5atoms3lite_fmp3.build.bootloader_addr) and MUST be 0x0 (the ESP32-S3
#      ROM loads the 2nd-stage bootloader from 0x0; the C5 uses 0x2000 and
#      the ESP32 0x1000). Any other value, or a missing key, is a gate
#      failure.
#    - there is NO Direct Boot erase. The C5 script erases flash 0x0-0x1FFF
#      before every write because a leftover asp3 Direct Boot magic there
#      outranks that chip's bootloader at 0x2000. This chip's bootloader IS
#      at 0x0, so the erase would only delete what the write puts back.
#      Instead the read-back is a positive check: after the write, flash
#      0x0-0x0FFF is read and must equal the first 4096 bytes of the
#      bootloader image that was just written. The factory backup of the
#      whole 8MB flash lives in build/atoms3lite-backup/ (git-ignored); this
#      script never restores it.
#    - the esptool calls of one writing run are chained in the ROM download
#      mode (flash-id --after no-reset, then write-flash with --before
#      no-reset --after no-reset, then read-flash --before no-reset --after
#      hard-reset); esptool 5.2.0 re-uploads its stub per call and nothing
#      resets in between.
#    - the JTAG liveness probe uses board/esp32s3-builtin.cfg and reads the
#      ESP32-S3's USB_SERIAL_JTAG registers (EP1_CONF 0x60038004, INT_RAW
#      0x60038008; the RISC-V chips have the same block at 0x6000F000). The
#      C5 script's PCR clock evidence and g_seam_c5_clk_result reads are
#      gone: they are C5-only.
#    - the APM counters are gone (the ESP32-S3 has no APM to unblock, so
#      there is no "wifi_adapter(c5): apm " line to count).
#    - the images come from an arduino-cli build directory (SKETCH_BUILD),
#      plus the core's boot_app0.bin. BOOTLOADER= / PTABLE= override single
#      images so one axis at a time can be swapped.
#    - esptool is the M5Stack core's bundled esptool_py (5.x, dash-style
#      subcommands: write-flash, flash-id, read-flash). The write-flash
#      arguments are the upload recipe's (-z, --flash-mode/--flash-freq/
#      --flash-size keep).
#    - the redact stage works with NO credentials file (peer-MAC mask and
#      IPv4 mask only). If WIFI_CREDS names a readable shell file with
#      WIFI_STA_SSID / WIFI_STA_PASS / WIFI_STA_BSSID assignments, its values
#      become needles as well. Values are never printed.
#
#  The gate (fail-closed, nothing is written unless all pass):
#    1. DUT_MAC has the hh:hh:hh:hh:hh:hh shape and is not in the FORBIDDEN
#       list (string compare, no hardware).
#    2. the images are plausible: none empty, bootloader and app start with
#       the ESP image magic 0xE9, the partition table is exactly 3072 bytes,
#       boot_app0 exactly 8192 bytes.
#    2b. BOARDS_TXT exists, holds m5atoms3lite_fmp3.build.bootloader_addr,
#       and the value is 0x0.
#    3. the capture prerequisites exist (esp_idf_monitor python, port node).
#    4. esptool flash-id (read-only, ROM only, --after no-reset) reports
#       MAC == DUT_MAC, chip type contains DUT_CHIP, detected flash size
#       == DUT_FLASH (8MB).
#  After a write: flash 0x0-0x0FFF read back must equal the first 4096 bytes
#  of the bootloader image, and write-flash must have printed "Hash of data
#  verified" once per image; otherwise the run fails and .sha.txt says the
#  flash contents are suspect.
#
#  Usage:
#    SKETCH_BUILD=<dir> bash scripts/capture_s3_usj.sh
#
#  Inputs (environment):
#    SKETCH_BUILD  arduino-cli --build-path directory holding
#                  <sketch>.ino.bootloader.bin / .partitions.bin / .bin
#                  (required to flash; optional for NOFLASH=1 / COLD=1).
#    PROJECT       sketch project name (e.g. Blink.ino) when SKETCH_BUILD holds
#                  more than one set of images. Default: the only one found.
#    BOOTLOADER    explicit bootloader image (overrides SKETCH_BUILD's).
#    PTABLE        explicit partition table image (overrides SKETCH_BUILD's).
#    BOOT_APP0     boot_app0 image. Default: the M5Stack core's
#                  tools/partitions/boot_app0.bin. `none` skips it. WARNING:
#                  the stage 2 plan forbids a run without boot_app0 except as
#                  a recorded deviation (the development-side ptable puts nvs
#                  at 0xe000, which is the only reason `none` exists).
#    APP           explicit application image (overrides SKETCH_BUILD's).
#    BOARDS_TXT    the installed platform's boards.txt the bootloader address
#                  is read from (default $HOME/Arduino/hardware/toppers/esp32/
#                  boards.txt = what scripts/install_platform.py installs).
#                  The key m5atoms3lite_fmp3.build.bootloader_addr must exist
#                  and be 0x0; there is no override variable on purpose.
#    ESPTOOL       esptool executable. Default: newest
#                  <arduino data>/packages/m5stack/tools/esptool_py/*/esptool.
#    ARDUINO_DIRECTORIES_DATA  arduino-cli data directory (default ~/.arduino15,
#                  the same rule scripts/arduino_sdk.py applies).
#    BAUD          esptool baud (default 921600 = the board's UploadSpeed
#                  default; USJ ignores it).
#    DUT_MAC       expected MAC (default 34:b7:da:5e:95:1c, the M5AtomS3 Lite).
#    DUT_CHIP      expected chip type substring (default "ESP32-S3"; the full
#                  line reads "ESP32-S3 (QFN56) (revision v0.2)" on this
#                  board. Package and revision are deliberately not pinned).
#    DUT_FLASH     expected "Detected flash size" (default 8MB).
#    DUT_PORT      serial device. Default: /dev/serial/by-id name derived
#                  from DUT_MAC (Espressif USB JTAG/serial debug unit).
#    OUT           capture log path (default LOG_DIR/s3-capture-<stamp>.log).
#                  Sidecars are written next to it: .ident.log (flash-id),
#                  .flash.log (write-flash + read-flash),
#                  .cold.txt (COLD=1 by-id timeline),
#                  .journal.txt (COLD=1: the kernel journal's USB lines for the
#                  capture window, starting JOURNAL_LEAD_SEC = 30 s before the
#                  by-id wait so the power-off line is inside it, so the
#                  power-cycle evidence is self-contained), .sha.txt (sha256
#                  of what this run wrote), .jtag.log / .jtag.txt (the JTAG
#                  liveness probe's raw openocd output and its parsed
#                  summary; only when the probe ran, see S3_JTAG_ON_SILENT).
#    LOG_DIR       default directory for OUT
#                  (default $HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/.steering/
#                   20260917-atoms3lite-plan/stage2/logs).
#    CAPTURE_SEC   capture length in seconds (default 60; digits only).
#    MARKERS       ERE; when it matches the capture, stop early (default empty
#                  = always capture the full CAPTURE_SEC).
#    EXTRA_MARKERS `|`-separated FIXED strings (grep -F, not regex; default
#                  empty). Each is counted (matching lines) in the capture and
#                  one line "extra: <s1>=<n> <s2>=<n> ..." is printed after
#                  the standard "markers:" line and appended to .sha.txt;
#                  empty prints nothing. Meant for an example's own lines,
#                  e.g. EXTRA_MARKERS='[S3-INTR] VERDICT PASS|[S3-RGB] tx_done'
#                  (the brackets are literal, which is why it is -F).
#    DRYRUN=1      identify the DUT read-only, print what WOULD be done (the
#                  four writes with sha256 and address, and the read-back)
#                  and the exact esptool command lines, write
#                  nothing, capture nothing. The identification enters the
#                  ROM download mode (esptool --before default-reset) and
#                  ends with --after hard-reset, so the board runs again
#                  afterwards (unlike the C6 script, which parks it).
#    NOFLASH=1     do not write; capture only. The monitor hard-resets the
#                  chip when it opens the port (warm-boot capture).
#    NORESET=1     open the monitor with --no-reset. With NOFLASH=1 (and not
#                  COLD) the identification uses --after hard-reset instead of
#                  --after no-reset, so the board is running again when the
#                  monitor opens; the head of the boot (ROM banner, 'S', and
#                  usually the banner) is lost during the reconnect.
#    COLD=1        true cold boot. Implies NOFLASH=1 and NORESET=1, and calls
#                  esptool NOT AT ALL (esptool's connect performs a reset that
#                  would overwrite the cold boot). The power cycle is done
#                  outside this script (uhubctl or by hand); the script waits
#                  for the by-id node to disappear and reappear (COLD_WAIT_SEC,
#                  default 120, digits only), records both instants, then
#                  opens the port.
#    S3_JTAG_ON_SILENT  (default 1) after the marker count, when COLD=1 AND
#                  heartbeat=0 (a silent cold run: no output at all), run the
#                  JTAG liveness probe (below). 0 disables it.
#    S3_JTAG_FORCE (default 0) 1 runs the probe regardless of the counts --
#                  the positive control on a healthy warm run (a probe that
#                  only ever runs on a silent board has never been seen to
#                  say "alive").
#    OPENOCD       openocd-esp32 binary for the probe (default
#                  $HOME/.arduino15/packages/m5stack/tools/openocd-esp32/
#                  v0.12.0-esp32-20251215/bin/openocd, under
#                  ARDUINO_DIRECTORIES_DATA; its scripts directory is
#                  <bin>/../share/openocd/scripts, passed with -s). When the
#                  binary is missing the probe writes one line "# not run:
#                  openocd not found at <path>" to .jtag.txt and nothing
#                  else; PATH is never searched for another openocd.
#    ELF           the linked ELF the probe reads the address of
#                  toppers_arduino_loop_calls from (default
#                  $SKETCH_BUILD/fmp3-prebuilt-link/link/fmp_xip.elf when
#                  SKETCH_BUILD is set -- fmp3_link.py links there and copies
#                  it to <sketch>.elf; empty otherwise). nm is the M5Stack
#                  core's riscv32-esp-elf-nm (newest tools/esp-rv32/<ver>/bin,
#                  the same rule ESPTOOL uses). Without the ELF, the tool or
#                  the symbol the probe still reads PC/EP1_CONF/INT_RAW and
#                  records "loop_calls: symbol unavailable (<why>)".
#    WIFI_CREDS    credentials shell file for the redact needles (default
#                  $HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/esp/boot/
#                  wifi_credentials.sh; absent = no needles).
#    IDF_PYTHON    python of an ESP-IDF python env that has esp_idf_monitor
#                  (default: newest $HOME/tools/espressif/python_env/
#                  idf*_py3.*_env or $HOME/.espressif/python_env/... by sort -V).
#    S3_MASK_SELFTEST=1  run the redact/mask self-test, the marker-count
#                  self-test (EXTRA_MARKERS included) and the JTAG probe's
#                  decision/parser self-test on fixtures and exit. Touches no
#                  hardware (openocd is never started).
#    S3_REDACT_ONLY=1  `S3_REDACT_ONLY=1 bash capture_s3_usj.sh <file>...`
#                  runs the redact stage (needles from WIFI_CREDS, peer-MAC,
#                  IPv4, hex address) over existing files in place and checks
#                  them, exactly as the EXIT trap does after a capture. For
#                  logs captured before a mask rule existed. Touches no
#                  hardware; rc 93 (and *.UNREDACTED) on residue.
#                  Guarded (stage 4 review M-6): it rewrites files in place,
#                  so it refuses a file that git tracks or that lies outside
#                  LOG_DIR - a source file or a document handed to it by
#                  mistake would be masked into nonsense with no way back.
#                  S3_REDACT_ANYWHERE=1 lifts the LOG_DIR bound (never the
#                  git-tracked one) for a log kept elsewhere.
#
#  JTAG liveness probe (stage 6 task 2). Two cold runs (stages 2 and 4)
#  produced NO output at all (heartbeat=0) and came back on the next warm
#  reset; the capture cannot tell "alive with the USB Serial/JTAG console
#  stuck" from "hung". The chip's USB-JTAG is a second interface of the same
#  USB device (303a:1001) and openocd reads memory through it WITHOUT a reset
#  (the development side did this 6/6 on this board). So, AFTER the monitor
#  is killed (the CDC-ACM interface is free) and only when wanted
#  (s3_jtag_wanted: DRYRUN never; S3_JTAG_FORCE=1 always; else COLD=1 and
#  heartbeat=0 with S3_JTAG_ON_SILENT=1), the script runs, under a 30 s
#  timeout, capturing stdout+stderr raw to .jtag.log:
#    openocd -s <scripts> -c "adapter serial <DUT_MAC upper-case>" \
#            -f board/esp32s3-builtin.cfg -c init -c halt -c "reg pc" \
#            -c "mdw 0x60038004" -c "mdw 0x60038008" [-c "mdw <loop_calls>"] \
#            -c resume -c "sleep 2000" -c halt -c "reg pc" [-c "mdw <loop_calls>"] \
#            -c resume -c shutdown
#  i.e. halt only as long as two reads take, never reset, never flash.
#  Board pinning is mandatory and fail-closed: `adapter serial` is the FIRST
#  -c (the USB-JTAG serial of the chip is its base MAC, derived from
#  DUT_MAC), and afterwards the output MUST contain
#  "esp_usb_jtag: serial (<that serial>)" -- the development side once had
#  openocd grab a DIFFERENT board because no serial was given. Without that
#  line .jtag.txt gets "# REFUSED: openocd did not report the pinned serial",
#  no value is trusted, and a [S3] warning is printed. When openocd prints
#  NO "serial (" line at all and exits non-zero (no device found, or the
#  adapter failed before init) that is not a grabbed-wrong-board event: the
#  sidecar says "verdict: not-run (openocd rc=N, no serial line)" instead,
#  again with no value printed. Registers:
#  0x60038004 = USB_SERIAL_JTAG_EP1_CONF (bit0 WR_DONE, bit1
#  SERIAL_IN_EP_DATA_FREE = the "data_free" printed, bit2
#  SERIAL_OUT_EP_DATA_AVAIL), 0x60038008 = USB_SERIAL_JTAG_INT_RAW (soc/
#  usb_serial_jtag_reg.h, esp32s3). EP1_CONF and INT_RAW are RECORDED RAW
#  for later comparison with a silent run; they are NOT a health criterion.
#  In particular data_free is NOT expected to be 1 on a healthy board: the
#  probe runs after the monitor has been killed, so no host reader drains
#  the IN endpoint, the application keeps writing, and bit1 reads 0 with
#  the sketch demonstrably running (stage 6 task 3, forced probe on a warm
#  run: loop_calls 40528 -> 42283 delta=1755, verdict alive, data_free=0).
#  The verdict is decided by loop_calls (two reads 2 s apart) alone.
#  The parsed sidecar .jtag.txt:
#    # jtag probe <date> reason=<silent-cold|forced> serial=<pinned> elf=<path or none>
#    pc1=0x........ pc2=0x........
#    ep1_conf=0x........ int_raw=0x........   (data_free=<0|1>; raw evidence, not a criterion)
#    loop_calls: <a> -> <b> delta=<b-a>        (or "symbol unavailable (...)")
#    verdict: alive | not-advancing | refused | not-run
#  alive = loop_calls delta > 0 with b >= a; not-advancing = delta == 0, or
#  (symbol unknown) pc1 == pc2; refused = openocd identified a device but
#  not the pinned one (a serial line is present and differs), or exited 0
#  without any serial line; not-run = no verdict (openocd missing, no
#  output, non-zero exit with no serial line at all, fewer than two reads,
#  symbol unknown and pc moved -- inconclusive, or the counter went
#  backwards: b < a is printed as "delta=<(b-a) mod 2^32> (wrapped)" and is
#  NOT called alive, because it can only come from a 32-bit wrap -- about
#  49 days at the measured ~1 loop() per ms, unreachable inside a capture --
#  or from the counter being re-zeroed by a reset between the two reads,
#  and neither shows the instance under test advancing; the qualifier in
#  parentheses says which). The verdict and loop_calls lines go to .sha.txt
#  under "# jtag:". The probe is evidence, not a gate: it never changes the
#  exit status.
#  Caveat (halt left behind): if openocd stops between halt and resume --
#  the 30 s timeout (recorded as "openocd rc=124") OR a command error that
#  ends the -c chain early (a refused mdw, a target error; recorded as the
#  non-zero rc) -- the chip stays halted until the next reset or power
#  cycle. A capture that follows without a reset is then silent for that
#  reason, not for the one under investigation.
#
#  Console note (measured on this board, 2026-09-17): the ESP32-S3 USB
#  Serial/JTAG console loses the first character or two of SOME lines the
#  FMP3 log task writes while the Arduino task is also printing
#  ("[Arduino] task start" arrived as "rduino] task start", "[WiFiScan]
#  found 17 APs" as "[iFiScan] found 17 APs", "[S3-RGB] tx_done=1" as
#  "[3-RGB] tx_done=1"); the lines a sketch writes itself through
#  target_fput_log() are intact. That is a defect of this port's console,
#  not of this script. Until it is fixed the patterns below deliberately do
#  NOT anchor on the leading "[" of a bracketed tag, so a lost bracket does
#  not silently turn a passing run into a zero count - they match the TAIL
#  of a tag, two characters in ("Scan] found", not "[WiFiScan] found"),
#  because the losses seen so far eat one or two characters and can eat the
#  tag's own first letter ("[WiFiScan] found 16 APs" arrived as
#  "[iFiScan] found 16 APs"). EXTRA_MARKERS is
#  fixed-string matching and cannot do that for you: give it the tail of a
#  tag (e.g. "RGB] tx_done", not "[S3-RGB] tx_done").
#
#  Markers counted at the end (strings from src/bridge/ArduinoSketchBridge.cpp,
#  third_party/fmp3_core/syssvc/banner.c and arch/riscv_gcc/common):
#    banner     "TOPPERS/FMP3 Kernel Release"
#    setup      "[Arduino] setup complete"
#    heartbeat  "[Arduino] loop heartbeat"      (once per 1000 loop() calls)
#    unexpected "## Unexpected" / "## Assertion" / "## Internal" /
#               "Unregistered exception|interrupt" / "mcause=" /
#               "[LWIP-ASSERT]" (wifi/net/port/sys_arch.c, the tcpip thread
#               parks) / "abort() called" (wifi/shim/esp_shim_libc.c) /
#               "ROM newlib abort()" (wifi/shim/wifi_stubs.c) /
#               "libc: _exit(" and "libc: _kill(" (seam/newlib_syscalls.c)
#    smark      a line that is just "S". The RISC-V seam entry prints this
#               before the FMP3 banner (SEAM_<CHIP>_ENTRY_MARK); the Xtensa
#               seam has no such mark, so 0 is the expected count here. The
#               counter is kept so one capture format serves every board.
#    blink      "[Blink] ON" / "[Blink] OFF"    (examples/Blink only)
#  Wi-Fi markers (strings from ports/m5stack_riscv/runtime/wifi/adapter/
#  toppers_wifi_scan.c, toppers_wifi_connect.c, wifi/net/netif_esp32s3.c and
#  wifi/shim/esp_wifi_adapter.c; the wifi-connect runtime only):
#    scan       "[WiFiScan] found N APs"; the value printed is the LAST N
#               seen (-1 when the line never appeared)
#    scanap     "[WiFiScan] AP[i] ..." lines (one per neighbour listed)
#    ssidraw    scan lines whose SSID column is neither the RISC-V runtime's
#               "<SSID-i>" placeholder nor this script's "<SSID-redacted>"
#               mask; any other spelling is a neighbour's SSID left in the
#               log -- must be 0 (the Xtensa runtime prints the real name,
#               so it is the mask above that makes this 0)
#    connected  "[WiFiConnect] connected authmode="
#    dhcp       "net: DHCP bound"   (the RISC-V net layer's line; the Xtensa
#               runtime has no such line, so 0 is expected here)
#    dhcpdone   "[WiFiConnect] connected and DHCP completed"
#               (examples/WiFiConnect.ino, so every board prints it)
#    ping       "net: ping gateway -> OK"   (the RISC-V net layer's, only
#               with its NET_DIAG build; 0 is expected here)
#    dnsok      "[WiFiConnect] DNS completed"   (examples/WiFiConnect.ino)
#    dnsfail    "[WiFiConnect] DNS failed"      (examples/WiFiConnect.ino)
#    tcp        "[WiFiConnect] TCP request completed"
#    disc       "[WiFiConnect] disconnected reason="
#    beginrej   "[WiFiConnect] begin: rejected"
#
#  Exit status: 0 when the capture ran (or DRYRUN completed); non-zero when
#  the gate refused, a tool failed, or the redact stage could not prove the
#  logs clean (then the files are renamed *.UNREDACTED; do not commit them).
#
set -euo pipefail

say() { echo "[S3] $*"; }
die() { echo "" >&2; echo "ABORT: $*" >&2; exit 1; }

#  Use the real grep: on some hosts `grep` is a function that delegates to a
#  different tool (ugrep) with different exit-code and .gitignore behaviour.
GREP=/usr/bin/grep

#  ---------------------------------------------------------------- DUT identity
#  The DUT is the M5AtomS3 Lite. A typo here would write to somebody else's
#  board, so the default is a literal and the FORBIDDEN list is the
#  development-side one (the ESP32-P4 boards plus the asp3 C6 devkit) PLUS the
#  two RISC-V DUTs on the neighbouring ports of the same hub: the M5NanoC6
#  (9c:13:9e:d3:62:18, hub 2-3.3 port 3) and the M5Stamp-C5
#  (3c:dc:75:8d:ed:20, port 4). This board is on port 1.
#  DUT_CHIP is compared against the start of esptool's "Chip type:" line,
#  which on this board reads "ESP32-S3 (QFN56) (revision v0.2)"; requiring
#  only "ESP32-S3" keeps a board of another package or revision acceptable
#  while the MAC gate pins the individual board.
DUT_MAC="${DUT_MAC:-34:b7:da:5e:95:1c}"
DUT_CHIP="${DUT_CHIP:-ESP32-S3}"
#  The M5AtomS3 Lite has 8MB of embedded flash (GD, quad, 3.3V).
DUT_FLASH="${DUT_FLASH:-8MB}"
FORBIDDEN="60:55:f9:57:c2:60 d0:cf:13:f0:a7:44 d0:cf:13:f0:c8:94 30:76:f5:ed:86:a8
14:c1:9f:e0:61:b0 30:ed:a0:f3:f1:64 44:1b:f6:e2:73:84 78:21:84:a6:5c:64 f4:12:fa:5b:4a:58
9c:13:9e:d3:62:18 3c:dc:75:8d:ed:20"

dut_lc="$(printf '%s' "$DUT_MAC" | tr 'A-Z' 'a-z')"
dut_uc="$(printf '%s' "$DUT_MAC" | tr 'a-z' 'A-Z')"
printf '%s' "$dut_lc" | /usr/bin/grep -qE '^[0-9a-f]{2}(:[0-9a-f]{2}){5}$' \
    || die "DUT_MAC must look like hh:hh:hh:hh:hh:hh (given: $DUT_MAC)"
DUT_PORT="${DUT_PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${dut_uc}-if00}"

#  ---------------------------------------------------------------- bootloader address
#  The bootloader address: read from BOARDS_TXT (the installed platform's
#  boards.txt), key BOARDS_KEY, and required to be exactly 0x0 (the ESP32-S3
#  ROM's 2nd-stage bootloader offset; the C5's 0x2000 and the ESP32's 0x1000
#  would put the bootloader where this ROM never looks). Prints the value on
#  success; prints the reason and returns 1 otherwise (the caller dies).
#  Pure over the file; exercised by S3_MASK_SELFTEST=1.
BOARDS_TXT="${BOARDS_TXT:-$HOME/Arduino/hardware/toppers/esp32/boards.txt}"
BOARDS_KEY="m5atoms3lite_fmp3.build.bootloader_addr"
BOARDS_KEY_RE="$(printf '%s' "$BOARDS_KEY" | sed 's/\./\\./g')"   # literal dots in the ERE
S3_BL_ADDR_EXPECTED=0x0
s3_bootloader_addr() {   # <boards.txt> -> "0x0" or a reason (rc 1)
    local f="$1" v n
    if [ ! -f "$f" ]; then echo "boards.txt not found: $f (BOARDS_TXT=; run scripts/install_platform.py first)"; return 1; fi
    n="$($GREP -cE "^${BOARDS_KEY_RE}=" "$f" || true)"
    if [ "${n:-0}" -eq 0 ]; then echo "key $BOARDS_KEY not in $f"; return 1; fi
    if [ "$n" -ne 1 ]; then echo "key $BOARDS_KEY appears $n times in $f"; return 1; fi
    v="$($GREP -E "^${BOARDS_KEY_RE}=" "$f" | sed -E 's/^[^=]*=//; s/[[:space:]\r]+$//')"
    case "$v" in
        0x*|0X*) [ -n "${v#0[xX]}" ] && printf '%s' "${v#0[xX]}" | $GREP -qE '^[0-9A-Fa-f]+$' \
                     || { echo "key $BOARDS_KEY has a non-hex value '$v' in $f"; return 1; } ;;
        *) echo "key $BOARDS_KEY has a non-hex value '$v' in $f"; return 1 ;;
    esac
    if [ "$((v))" -ne "$((S3_BL_ADDR_EXPECTED))" ]; then
        echo "key $BOARDS_KEY is $v in $f, expected $S3_BL_ADDR_EXPECTED (the ESP32-S3 bootloader offset); refusing"
        return 1
    fi
    printf '0x%x\n' "$((v))"
}


#  ---------------------------------------------------------------- redact stage
#
#  Three layers, applied in this order to every file this run wrote:
#    (a) needles  -- values from WIFI_CREDS (SSID/PASS/BSSID and, for text
#                    values, their hex-dump spellings), case-insensitive, MAC
#                    values with any of `-:.` as separator -> <REDACTED_xxx>
#    (b) peer MAC -- every hh:hh:hh:hh:hh:hh and TAHI:/TALO: hex word ->
#                    <PEER-MAC>, except the DUT's own MAC (and its EUI-64
#                    spelling hh:hh:hh:ff:fe:hh:hh:hh, which esptool prints)
#    (c) IPv4     -- every dotted quad -> <IPv4>
#    (d) hex word -- "address=0x" + 8 hex digits -> address=<HEX32> (the
#                    adapter prints the DHCP lease and DNS answers as
#                    "address=0x%08x"; a LAN address in hex is still an
#                    address. Found by the Task 2 review: 9 unmasked lines)
#  The transformer is sed; the checker is a separate implementation (normalize
#  then fixed-string / grep -E count). Both are exercised by
#  S3_MASK_SELFTEST=1. The checker's silence is never taken as success: if a
#  pipeline stage fails it prints nothing, and the caller treats "nothing" as
#  residue (fail-closed). On residue the file is renamed *.UNREDACTED.
#
WIFI_CREDS="${WIFI_CREDS:-$HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/esp/boot/wifi_credentials.sh}"
NEEDLE_VALUES=(); NEEDLE_TOKENS=(); NEEDLE_KINDS=()

_needle_add() {   # value token ; kind decided by the SHAPE of the value
    local v="$1" t="$2"
    NEEDLE_VALUES+=("$v"); NEEDLE_TOKENS+=("$t")
    if printf '%s' "$v" | $GREP -qE '^[0-9A-Fa-f]{2}([-:.][0-9A-Fa-f]{2}){5}$'; then
        NEEDLE_KINDS+=("mac")
    else
        NEEDLE_KINDS+=("text")
    fi
}

#  Loads needles from WIFI_CREDS. Prints only counts, never values.
s3_load_needles() {
    NEEDLE_VALUES=(); NEEDLE_TOKENS=(); NEEDLE_KINDS=()
    if [ ! -r "$WIFI_CREDS" ]; then
        say "redact: no credentials file (needles=0; peer-MAC mask + IPv4 mask only)"
        return 0
    fi
    local v val n0 i hex spaced
    for v in WIFI_STA_SSID WIFI_STA_PASS WIFI_STA_BSSID; do
        val="$($GREP -oE "^[[:space:]]*(export[[:space:]]+)?${v}=[\"']?[^\"']*" "$WIFI_CREDS" 2>/dev/null \
               | head -1 | sed -E "s/^[[:space:]]*(export[[:space:]]+)?${v}=[\"']?//")" || val=""
        [ -n "$val" ] || continue
        _needle_add "$val" "<REDACTED_${v#WIFI_STA_}>"
    done
    #  Hex-dump spellings of text values (a probe once dumped a frame as
    #  "66 6d 70 33 ..." and the plain-text needles missed it).
    n0=${#NEEDLE_VALUES[@]}
    for ((i = 0; i < n0; i++)); do
        [ "${NEEDLE_KINDS[$i]}" = "text" ] || continue
        [ "${#NEEDLE_VALUES[$i]}" -ge 4 ] || continue
        hex="$(printf '%s' "${NEEDLE_VALUES[$i]}" | od -An -tx1 -v | tr -d ' \n')"
        spaced="$(printf '%s' "$hex" | sed -e 's/../& /g' -e 's/ $//')"
        NEEDLE_VALUES+=("$spaced"); NEEDLE_TOKENS+=("${NEEDLE_TOKENS[$i]}"); NEEDLE_KINDS+=("text")
        NEEDLE_VALUES+=("$hex");    NEEDLE_TOKENS+=("${NEEDLE_TOKENS[$i]}"); NEEDLE_KINDS+=("text")
    done
    say "redact: credentials file present, needles=${#NEEDLE_VALUES[@]} (values not shown)"
}

_dut_forms_lc() {   # the DUT spellings the mask must keep (lower case)
    local m="${DUT_MAC:-}"
    [ -n "$m" ] || return 0
    m="$(printf '%s' "$m" | tr 'A-Z' 'a-z')"
    printf '%s\n' "$m" "${m:0:8}:ff:fe:${m:9}"
}

#  Transformer (sed). Returns non-zero when sed cannot write the file.
s3_redact_transform() {
    local f="$1" i esc pat
    local -a args=()
    for i in "${!NEEDLE_VALUES[@]}"; do
        if [ "${NEEDLE_KINDS[$i]}" = "mac" ]; then
            pat="$(printf '%s' "${NEEDLE_VALUES[$i]}" | tr -d '\-:.' \
                   | sed -e 's/../&[-:.]\\{0,1\\}/g' -e 's/\[-:\.\]\\{0,1\\}$//')"
            args+=(-e "s/${pat}/${NEEDLE_TOKENS[$i]}/gI")
        else
            esc="$(printf '%s' "${NEEDLE_VALUES[$i]}" | sed -e 's/[][\.*^$/&]/\\&/g')"
            args+=(-e "s/${esc}/${NEEDLE_TOKENS[$i]}/gI")
        fi
    done
    if [ "${#args[@]}" -gt 0 ]; then
        sed -i "${args[@]}" "$f" || return 1
    fi
    #  Peer-MAC mask: park the DUT spellings, mask 8-octet EUI-64 forms
    #  first (else a peer EUI-64 would leave "<PEER-MAC>:hh:hh" behind), then
    #  6-octet forms, then TAHI/TALO and IPv4, then restore the DUT spellings.
    #  SSID column: unlike the RISC-V runtime, which prints "<SSID-i>"
    #  placeholders, ports/m5stack_xtensa/runtime/wifi/adapter/
    #  toppers_wifi_scan.c prints the neighbours' real SSIDs
    #  ("[WiFiScan] AP[i] rssi=.. ch=.. SSID=<the real name>"), so this
    #  script masks that column itself - the rest of the line is kept.
    local lc eui lcU euiU
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    lcU="$(printf '%s' "$lc" | tr 'a-z' 'A-Z')"; euiU="$(printf '%s' "$eui" | tr 'a-z' 'A-Z')"
    sed -i -E \
        -e "s/${eui:-__none__}/__S3_DUT_EUI_LC__/g" -e "s/${euiU:-__none__}/__S3_DUT_EUI_UC__/g" \
        -e "s/${lc:-__none__}/__S3_DUT_MAC_LC__/g"  -e "s/${lcU:-__none__}/__S3_DUT_MAC_UC__/g" \
        -e 's/([0-9A-Fa-f]{2}:){7}[0-9A-Fa-f]{2}/<PEER-MAC>/g' \
        -e 's/[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}/<PEER-MAC>/g' \
        -e 's/TAHI:0x[0-9A-Fa-f]+/TAHI:<PEER-MAC>/gI' -e 's/TALO:0x[0-9A-Fa-f]+/TALO:<PEER-MAC>/gI' \
        -e 's/\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b/<IPv4>/g' \
        -e 's/address=0x[0-9A-Fa-f]{8}/address=<HEX32>/g' \
        -e 's/(\[WiFiScan\] AP\[[0-9]+\].* SSID=).*$/\1<SSID-redacted>/' \
        -e "s/__S3_DUT_EUI_LC__/${eui:-}/g" -e "s/__S3_DUT_EUI_UC__/${euiU:-}/g" \
        -e "s/__S3_DUT_MAC_LC__/${lc:-}/g"  -e "s/__S3_DUT_MAC_UC__/${lcU:-}/g" "$f" || return 1
    return 0
}

#  Checker (separate implementation). Prints ONE integer: residue lines
#  (peer MAC / TAHI-TALO / IPv4 / address=0x<8 hex>, DUT spellings removed
#  first) plus the number
#  of needles still found (normalized, fixed-string). Prints nothing and
#  returns non-zero if any stage fails -- the caller reads "nothing" as residue.
_s3_peer_re='[0-9a-f][0-9a-f](:[0-9a-f][0-9a-f]){5}|ta(hi|lo):0x[0-9a-f]|\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b|address=0x[0-9a-f]{8}'
s3_residue_count() {
    local f="$1" lc eui out i needle n=0 found
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    out="$( set -o pipefail
            tr 'A-Z' 'a-z' < "$f" | sed -e "s/${eui:-__none__}//g" -e "s/${lc:-__none__}//g" \
              | { $GREP -acE "$_s3_peer_re" || [ $? -eq 1 ]; } )" || return 1
    case "$out" in ''|*[!0-9]*) return 1 ;; esac
    n=$out
    #  A scan line whose SSID column is still a real name. This port's
    #  runtime prints the neighbours' SSIDs (the RISC-V one prints
    #  "<SSID-i>" placeholders), so the transform above masks them; a file
    #  that reaches here with one left is quarantined, not stored.
    found="$( set -o pipefail
              { $GREP -aE 'SSID=' "$f" || [ $? -eq 1 ]; } \
                | { $GREP -acvE 'SSID=(<SSID-[0-9]+>|<SSID-redacted>)' || [ $? -eq 1 ]; } )" || return 1
    case "$found" in ''|*[!0-9]*) return 1 ;; esac
    n=$((n + found))
    for i in "${!NEEDLE_VALUES[@]}"; do
        if [ "${NEEDLE_KINDS[$i]}" = "mac" ]; then
            needle="$(printf '%s' "${NEEDLE_VALUES[$i]}" | tr 'A-Z' 'a-z' | tr -d '\-:.')"
            found="$( set -o pipefail; tr 'A-Z' 'a-z' < "$f" | tr -d '\-:.' \
                      | { $GREP -acF -- "$needle" || [ $? -eq 1 ]; } )" || return 1
        else
            needle="$(printf '%s' "${NEEDLE_VALUES[$i]}" | tr 'A-Z' 'a-z')"
            found="$( set -o pipefail; tr 'A-Z' 'a-z' < "$f" \
                      | { $GREP -acF -- "$needle" || [ $? -eq 1 ]; } )" || return 1
        fi
        case "$found" in ''|*[!0-9]*) return 1 ;; esac
        [ "$found" -eq 0 ] || n=$((n + 1))
    done
    printf '%s\n' "$n"
}

#  Check + quarantine. Returns 1 (after renaming to .UNREDACTED) on residue or
#  when the checker could not run.
s3_redact_check() {
    local f="$1" n
    n="$(s3_residue_count "$f")" || n=""
    case "$n" in ''|*[!0-9]*) n="?" ;; esac
    if [ "$n" = "?" ] || [ "$n" -ne 0 ]; then
        echo "## redact: residue count $n in $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    return 0
}

s3_redact_file() {
    local f="$1"
    [ -f "$f" ] || return 0
    if ! s3_redact_transform "$f"; then
        echo "## redact: transformer failed on $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    s3_redact_check "$f"
}

#  Files this run wrote (OUT and its sidecars); filled in as they are created.
S3_FILES=()
s3_redact_on_exit() {
    local rc0=$? f bad=0 n=0
    for f in ${S3_FILES[@]+"${S3_FILES[@]}"}; do
        [ -f "$f" ] || continue
        n=$((n + 1))
        s3_redact_file "$f" || bad=1
    done
    if [ "$n" -eq 0 ]; then
        echo "[S3] redact: nothing was written by this run; nothing to redact" >&2
    elif [ "$bad" -eq 0 ]; then
        echo "[S3] redact: $n file(s) masked and checked clean" >&2
    fi
    if [ "$bad" -ne 0 ]; then
        [ "$rc0" -ne 0 ] && exit "$rc0"
        exit 93
    fi
    return "$rc0"
}

#  ---------------------------------------------------------------- markers
#  Counts the markers in one capture file and prints two lines:
#    markers: banner=.. setup=.. heartbeat=.. unexpected=.. smark=.. blink=..
#    wifi: scan=<N> scanap=.. ssidraw=.. connected=.. dhcp=.. dhcpdone=.. ping=..
#          dnsok=.. dnsfail=.. tcp=.. disc=.. beginrej=..
#  Counts only; verdicts are the caller's. Exercised by S3_MASK_SELFTEST=1.
S3_UNEXPECTED_RE='## Unexpected|## Assertion|## Internal|Unregistered (exception|interrupt)|mcause ?=|\[LWIP-ASSERT\]|abort\(\) called|ROM newlib abort\(\)|libc: _(exit|kill)\('
s3_count_markers() {
    local f="$1"
    _cnt() { $GREP -acE "$1" "$f" || true; }
    local n_banner n_setup n_heart n_unexp n_smark n_blink
    n_banner="$(_cnt 'TOPPERS/FMP3 Kernel Release')"
    n_setup="$(_cnt 'duino\] setup complete')"
    n_heart="$(_cnt 'duino\] loop heartbeat')"
    n_unexp="$(_cnt "$S3_UNEXPECTED_RE")"
    n_smark="$(_cnt '^S'$'\r''*$')"
    n_blink="$(_cnt 'ink\] (ON|OFF)')"
    echo "markers: banner=$n_banner setup=$n_setup heartbeat=$n_heart unexpected=$n_unexp smark=$n_smark blink=$n_blink"
    #  Wi-Fi: the scan count is the last "found N APs" value (-1 = never seen).
    local n_scan n_scanap n_ssidraw n_conn n_dhcp n_dhcpdone n_ping n_dnsok n_dnsfail n_tcp n_disc n_beginrej
    n_scan="$($GREP -aoE 'Scan\] found [0-9]+ APs' "$f" | tail -1 | $GREP -oE '[0-9]+' || true)"
    n_scan="${n_scan:--1}"
    n_scanap="$(_cnt 'Scan\] AP\[[0-9]+\]')"
    #  A scan line is leak-free only when its SSID column is the placeholder.
    n_ssidraw="$({ $GREP -aE 'SSID=' "$f" || true; } | { $GREP -avcE 'SSID=(<SSID-[0-9]+>|<SSID-redacted>)' || true; })"
    n_conn="$(_cnt 'Connect\] connected authmode=')"
    n_dhcp="$(_cnt 'net: DHCP bound')"
    n_dhcpdone="$(_cnt 'Connect\] connected and DHCP completed')"
    n_ping="$(_cnt 'net: ping gateway -> OK')"
    n_dnsok="$(_cnt 'Connect\] DNS completed')"
    n_dnsfail="$(_cnt 'Connect\] DNS failed')"
    n_tcp="$(_cnt 'Connect\] TCP request completed')"
    n_disc="$(_cnt 'Connect\] disconnected reason=')"
    n_beginrej="$(_cnt 'Connect\] begin: rejected')"
    echo "wifi: scan=$n_scan scanap=$n_scanap ssidraw=$n_ssidraw connected=$n_conn dhcp=$n_dhcp dhcpdone=$n_dhcpdone ping=$n_ping dnsok=$n_dnsok dnsfail=$n_dnsfail tcp=$n_tcp disc=$n_disc beginrej=$n_beginrej"
}

#  ---------------------------------------------------------------- extra markers
#  EXTRA_MARKERS: `|`-separated FIXED strings, each counted with grep -F
#  ("[S3-INTR] VERDICT PASS" is a literal, not a bracket expression). Prints
#    extra: <string1>=<n> <string2>=<n> ...
#  or nothing when EXTRA_MARKERS is empty. Exercised by S3_MASK_SELFTEST=1.
EXTRA_MARKERS="${EXTRA_MARKERS:-}"
s3_count_extra() {
    local f="$1" s n out=""
    local -a list=()
    [ -n "$EXTRA_MARKERS" ] || return 0
    IFS='|' read -ra list <<< "$EXTRA_MARKERS"
    for s in ${list[@]+"${list[@]}"}; do
        [ -n "$s" ] || continue
        n="$($GREP -acF -- "$s" "$f" || true)"
        out="$out $s=$n"
    done
    [ -z "$out" ] || echo "extra:$out"
    return 0
}

#  ---------------------------------------------------------------- jtag probe
#  Decision (pure; exercised by the selftest):
#    s3_jtag_wanted <cold> <heartbeat> <force> <dryrun> [<on_silent>=1]
#  prints yes/no. DRYRUN wins over everything; then S3_JTAG_FORCE; then the
#  silent-cold rule (COLD=1, heartbeat=0, S3_JTAG_ON_SILENT=1).
s3_jtag_wanted() {
    local cold="$1" hb="$2" force="$3" dryrun="$4" on_silent="${5:-1}"
    if [ "$dryrun" = "1" ]; then echo no; return 0; fi
    if [ "$force" = "1" ]; then echo yes; return 0; fi
    if [ "$on_silent" = "1" ] && [ "$cold" = "1" ] && [ "${hb:-0}" = "0" ]; then echo yes; return 0; fi
    echo no
}

#  Parser (pure over a file; exercised by the selftest):
#    s3_jtag_parse <raw openocd log> <loop_calls addr hex or ""> [<why unavailable>] [<openocd rc>]
#  prints the .jtag.txt body: pc1/pc2, ep1_conf/int_raw (data_free, raw
#  evidence only), the loop_calls line and the verdict line. The pinned
#  serial is $dut_uc. What it reads (openocd prints command output without
#  the "Info :" prefix):
#    Info : esp_usb_jtag: serial (9C:13:9E:D3:62:18)   <- the pin check
#    pc (/32): 0x40800abc                              <- reg pc
#    0x60038004: 00000002                              <- mdw (8 hex digits)
#  Verdicts: alive (delta > 0 and b >= a) / not-advancing (delta == 0, or
#  symbol unknown and pc1 == pc2) / refused (a serial line is present but
#  it is another board's, or openocd exited 0 with no serial line at all:
#  NOTHING from the run is trusted, no values are printed) / not-run (no
#  verdict: no output, non-zero rc with no serial line at all -- openocd
#  found no device --, fewer than two reads, symbol unknown and pc moved,
#  or the counter went backwards (b < a, "(wrapped)") -- all inconclusive).
#  A missing second read is never "alive"; a backwards counter is never
#  "alive" (see the header: a wrap is unreachable inside a capture and a
#  reset between the reads means the instance under test is gone).
#  The verdict never depends on ep1_conf/int_raw (stage 6 ruling R7).
s3_jtag_parse() {
    local raw="$1" addr="${2:-}" why="${3:-no ELF}" orc="${4:-}"
    local pc1 pc2 ep1 intraw a b free delta n
    if [ ! -s "$raw" ]; then
        echo "# no openocd output"
        echo "loop_calls: not read (no openocd output)"
        echo "verdict: not-run (no openocd output)"
        return 0
    fi
    if ! $GREP -aqF "esp_usb_jtag: serial ($dut_uc)" "$raw"; then
        if ! $GREP -aqF 'esp_usb_jtag: serial (' "$raw" && [ -n "$orc" ] && [ "$orc" != "0" ]; then
            #  No device was identified at all and openocd failed: nothing
            #  was probed. Not a grabbed-wrong-board event, so not "refused".
            echo "# not run: openocd exited $orc without reporting any serial (no device found, or the adapter failed before init)"
            echo "loop_calls: not read (no serial line)"
            echo "verdict: not-run (openocd rc=$orc, no serial line)"
            return 0
        fi
        echo "# REFUSED: openocd did not report the pinned serial ($dut_uc); no value from this run is trusted"
        { $GREP -aF 'esp_usb_jtag: serial (' "$raw" || true; } | sed 's/^/#   seen: /'
        echo "loop_calls: not trusted (refused)"
        echo "verdict: refused"
        return 0
    fi
    _nth_hex() {   # <n> <ERE with the value as the last hex run> -> the value or ""
        $GREP -aoiE "$2" "$raw" | sed -n "${1}p" | $GREP -oiE '[0-9a-f]+$' || true
    }
    pc1="$(_nth_hex 1 'pc \(/[0-9]+\): 0x[0-9a-f]+')"
    pc2="$(_nth_hex 2 'pc \(/[0-9]+\): 0x[0-9a-f]+')"
    ep1="$(_nth_hex 1 '0x60038004: [0-9a-f]{8}')"
    intraw="$(_nth_hex 1 '0x60038008: [0-9a-f]{8}')"
    echo "pc1=${pc1:+0x}${pc1:-?} pc2=${pc2:+0x}${pc2:-?}"
    if [ -n "$ep1" ]; then
        free=$(( (16#$ep1 >> 1) & 1 ))
        echo "ep1_conf=0x$ep1 int_raw=${intraw:+0x}${intraw:-?}   (data_free=$free; raw evidence, not a criterion)"
    else
        echo "ep1_conf=? int_raw=${intraw:+0x}${intraw:-?}   (data_free=?; raw evidence, not a criterion)"
    fi
    echo "# ep1_conf/int_raw are recorded for comparison with a silent run; the verdict below is decided by loop_calls only"
    if [ -n "$addr" ]; then
        #  openocd prints the address as 0x%08x, lower case, whatever spelling
        #  the mdw command was given.
        n="$(printf '%08x' "$((16#${addr#0x}))")"
        a="$(_nth_hex 1 "0x${n}: [0-9a-f]{8}")"
        b="$(_nth_hex 2 "0x${n}: [0-9a-f]{8}")"
        if [ -z "$a" ] || [ -z "$b" ]; then
            echo "loop_calls: incomplete (read 1: ${a:-missing}, read 2: ${b:-missing})"
            echo "verdict: not-run (incomplete: $([ -n "$a" ] && echo second || echo first) read missing)"
            return 0
        fi
        a=$((16#$a)); b=$((16#$b)); delta=$(( (b - a) & 0xFFFFFFFF ))
        if [ "$b" -lt "$a" ]; then
            #  Backwards: a 32-bit wrap (unreachable inside a capture at the
            #  measured ~1 loop() per ms) or a reset between the two reads
            #  (the counter lives in .bss). Either way the pair does not show
            #  the instance under test advancing: inconclusive, never alive.
            echo "loop_calls: $a -> $b delta=$delta (wrapped)"
            echo "verdict: not-run (counter went backwards: $a -> $b; a reset between the reads or a 32-bit wrap -- inconclusive)"
            return 0
        fi
        echo "loop_calls: $a -> $b delta=$delta"
        if [ "$delta" -gt 0 ]; then echo "verdict: alive"; else echo "verdict: not-advancing (loop_calls delta=0)"; fi
        return 0
    fi
    echo "loop_calls: symbol unavailable ($why)"
    if [ -z "$pc1" ] || [ -z "$pc2" ]; then
        echo "verdict: not-run (incomplete: $([ -n "$pc1" ] && echo second || echo first) pc read missing)"
    elif [ "$pc1" = "$pc2" ]; then
        echo "verdict: not-advancing (loop_calls unknown; pc1 == pc2)"
    else
        echo "verdict: not-run (loop_calls unknown; pc moved -- inconclusive)"
    fi
    return 0
}

#  Runner (hardware: the USB-JTAG interface, through openocd only). Writes
#  $JTAG_LOG (raw) and $JTAG_TXT (parsed), both redacted on exit; appends the
#  verdict and loop_calls lines to $SHA_TXT under "# jtag:". Never changes
#  the exit status; never resets, never flashes; never searches PATH.
s3_jtag_probe() {   # <reason: silent-cold|forced>
    local reason="$1" elf="${ELF:-}" addr="" why="no ELF" nm="" d scripts rc=0 v
    local -a cmd=()
    mkdir -p "$(dirname "$JTAG_TXT")"
    S3_FILES+=("$JTAG_TXT")
    if [ ! -x "$OPENOCD" ]; then
        echo "# not run: openocd not found at $OPENOCD" > "$JTAG_TXT"
        { echo "# jtag: reason=$reason not run (openocd not found at $OPENOCD)"; echo "verdict: not-run (openocd not found)"; } >> "$SHA_TXT"
        say "jtag: not run: openocd not found at $OPENOCD (OPENOCD= to point at openocd-esp32; PATH is not searched)"
        return 0
    fi
    #  The symbol address, from the ELF with the core's riscv32-esp-elf-nm.
    if [ -z "$elf" ] && [ -n "${SKETCH_BUILD:-}" ]; then
        elf="$SKETCH_BUILD/fmp3-prebuilt-link/link/fmp_xip.elf"
    fi
    if [ -n "$elf" ]; then
        if [ ! -f "$elf" ]; then
            why="ELF missing: $elf"
        else
            d="$(_newest_dir "$ARDUINO_DATA/packages/m5stack/tools/esp-rv32")"
            nm="${d:+$d/bin/riscv32-esp-elf-nm}"
            if [ -z "$nm" ] || [ ! -x "$nm" ]; then
                why="riscv32-esp-elf-nm not found under $ARDUINO_DATA/packages/m5stack/tools/esp-rv32"
            else
                addr="$("$nm" "$elf" 2>/dev/null | awk '$3=="toppers_arduino_loop_calls"{print $1; exit}' || true)"
                case "$addr" in
                    ''|*[!0-9a-fA-F]*) addr=""; why="symbol toppers_arduino_loop_calls not in $elf" ;;
                esac
            fi
        fi
    fi
    scripts="$(dirname "$OPENOCD")/../share/openocd/scripts"
    #  `adapter serial` FIRST, before the board cfg and init; halt only for
    #  the reads; resume in between; never reset.
    cmd=("$OPENOCD" -s "$scripts" -c "adapter serial $dut_uc" -f board/esp32s3-builtin.cfg
         -c init -c halt -c 'echo {== read 1 ==}' -c 'reg pc' -c 'mdw 0x60038004' -c 'mdw 0x60038008')
    [ -z "$addr" ] || cmd+=(-c "mdw 0x$addr")
    cmd+=(-c resume -c 'sleep 2000' -c halt -c 'echo {== read 2 ==}' -c 'reg pc')
    [ -z "$addr" ] || cmd+=(-c "mdw 0x$addr")
    cmd+=(-c resume -c shutdown)
    say "==== 4. jtag liveness probe (reason=$reason, serial pinned to $dut_uc, halt/read/resume, no reset) ===="
    say "  openocd: $OPENOCD"
    say "  elf: ${elf:-none}  loop_calls: $([ -n "$addr" ] && echo "0x$addr" || echo "unavailable ($why)")"
    {
        echo "# jtag probe $(date '+%F %T') reason=$reason serial=$dut_uc elf=${elf:-none}"
        echo "# openocd: $OPENOCD"
        printf '# command: '; printf '%q ' "${cmd[@]}"; printf '\n'
    } > "$JTAG_TXT"
    S3_FILES+=("$JTAG_LOG")
    timeout 30 "${cmd[@]}" > "$JTAG_LOG" 2>&1 || rc=$?
    echo "# openocd rc=$rc$([ "$rc" -eq 124 ] && echo ' (killed by the 30 s timeout; the chip may be left halted -- reset or power-cycle it)')$([ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] && { if $GREP -q 'serial (' "$JTAG_LOG" 2>/dev/null; then echo ' (non-zero: if the -c chain stopped between halt and resume the chip may be left halted -- reset or power-cycle it)'; else echo ' (non-zero before any adapter was opened: nothing was halted)'; fi; })" >> "$JTAG_TXT"
    s3_jtag_parse "$JTAG_LOG" "$addr" "$why" "$rc" >> "$JTAG_TXT"
    {
        echo "# jtag: reason=$reason serial=$dut_uc openocd rc=$rc"
        $GREP -aE '^(verdict|loop_calls):' "$JTAG_TXT" || true
    } >> "$SHA_TXT"
    v="$($GREP -aE '^verdict:' "$JTAG_TXT" || true)"
    case "$v" in
        "verdict: refused"*) say "WARNING: jtag: openocd did not report the pinned serial ($dut_uc); the probe's values are NOT trusted (see $JTAG_LOG)" ;;
    esac
    { $GREP -aE '^(pc1=|ep1_conf=|loop_calls:|verdict:)' "$JTAG_TXT" || true; } | sed 's/^/[S3] jtag: /'
    say "jtag: raw -> $JTAG_LOG  parsed -> $JTAG_TXT"
    return 0
}

#  ---------------------------------------------------------------- self-test
if [ "${S3_MASK_SELFTEST:-0}" = "1" ]; then
    #  Positive controls on fixtures. Touches no hardware, writes only to a
    #  temporary directory.
    _sd="$(mktemp -d)"; _st="$_sd/fixture.log"
    _fail() { echo "selftest FAIL: $*" >&2; chmod -R u+w "$_sd" 2>/dev/null; rm -rf "$_sd"; exit 1; }
    _count() { $GREP -o "$1" "$2" | wc -l; }
    _fix_mask() {
        printf 'port _%s-if00\nBASE MAC: %s\nMAC: %s\nCCMP mgmt frame from 12:34:56:ab:cd:ef used\n<ba-add> TAHI:0xabcd, TALO:0x12345678, x\ngot ip 192.168.4.23 mask 255.255.255.0\npeer eui64 12:34:56:ff:fe:ab:cd:ef seen\n[WiFiConnect] DHCP address=0x0A00020F then DNS resolved host=x address=0x5db8d822\n' \
            "$dut_uc" "$dut_lc" "${dut_lc:0:8}:ff:fe:${dut_lc:9}" > "$_st"
    }
    #  --- (1)-(6): no needles (credentials file absent) ---
    WIFI_CREDS="$_sd/no-such-creds.sh"
    s3_load_needles >/dev/null
    [ "${#NEEDLE_VALUES[@]}" -eq 0 ] || _fail "(0) needles loaded from a missing file"
    _fix_mask
    _pre="$(s3_residue_count "$_st")" || _fail "(1) checker returned non-zero on the fixture"
    [ "$_pre" = "5" ] || _fail "(1) residue before masking is not 5 lines ($_pre)"
    s3_redact_check "$_st" 2>/dev/null && _fail "(2) quarantine branch returned zero"
    [ ! -e "$_st" ] && [ -f "$_st.UNREDACTED" ] || _fail "(2) fixture was not renamed to .UNREDACTED"
    mv -f -- "$_st.UNREDACTED" "$_st"
    #  (3) transformer cannot write (read-only directory) -> on_exit exits 93
    _ro="$_sd/ro"; mkdir -p "$_ro"; cp -f "$_st" "$_ro/fixture.log"; chmod 555 "$_ro"
    ( S3_FILES=("$_ro/fixture.log"); s3_redact_on_exit 2>/dev/null ) && _rc=0 || _rc=$?
    chmod 755 "$_ro"
    [ "$_rc" -eq 93 ] || _fail "(3) s3_redact_on_exit rc is not 93 ($_rc)"
    #  (4) transform: residue 0, DUT spellings kept, 4 peer masks (one of them
    #  a whole 8-octet EUI-64, no ":hh:hh" tail left), 2 IPv4 masks, 2 hex
    #  address masks (upper and lower case digits, two on one line)
    s3_redact_file "$_st" || _fail "(4) residue after transform"
    $GREP -qF "_${dut_uc}-if00" "$_st" || _fail "(4) DUT MAC (upper case) was masked"
    $GREP -qF "BASE MAC: ${dut_lc}" "$_st" || _fail "(4) DUT MAC (lower case) was masked"
    $GREP -qF "MAC: ${dut_lc:0:8}:ff:fe:${dut_lc:9}" "$_st" || _fail "(4) DUT EUI-64 spelling was masked"
    [ "$(_count '<PEER-MAC>' "$_st")" -eq 4 ] || _fail "(4) peer masks are not 4 ($(_count '<PEER-MAC>' "$_st"))"
    [ "$(_count '<PEER-MAC>:[0-9A-Fa-f]' "$_st")" -eq 0 ] || _fail "(4) a peer EUI-64 left a tail after <PEER-MAC>"
    $GREP -qF "peer eui64 <PEER-MAC> seen" "$_st" || _fail "(4) the peer EUI-64 line is not masked whole"
    [ "$(_count '<IPv4>' "$_st")" -eq 2 ] || _fail "(4) IPv4 masks are not 2"
    [ "$(_count 'address=<HEX32>' "$_st")" -eq 2 ] || _fail "(4) hex address masks are not 2 ($(_count 'address=<HEX32>' "$_st"))"
    [ "$(_count 'address=0x' "$_st")" -eq 0 ] || _fail "(4) an address=0x word survived the transform"
    #  (5) DUT_MAC unset: everything MAC-shaped is masked, still clean
    _fix_mask
    ( DUT_MAC=""; s3_redact_file "$_st" ) || _fail "(5) transform with DUT_MAC unset failed"
    [ "$(_count '<PEER-MAC>' "$_st")" -eq 7 ] || _fail "(5) peer masks with DUT_MAC unset are not 7 ($(_count '<PEER-MAC>' "$_st"))"
    [ "$(_count '<PEER-MAC>:[0-9A-Fa-f]' "$_st")" -eq 0 ] || _fail "(5) the DUT EUI-64 left a tail after <PEER-MAC>"
    #  (6) checker stage failure -> empty output and non-zero (never "0")
    _n="$(s3_residue_count "$_sd/missing.log" 2>/dev/null)" && _fail "(6) checker returned zero on a missing file"
    [ -z "$_n" ] || _fail "(6) checker printed something on a missing file ($_n)"
    #  --- (7)-(8): needles from a synthetic credentials file ---
    WIFI_CREDS="$_sd/creds.sh"
    printf '#!/bin/sh\nWIFI_STA_SSID="SelfTestNet-42"\nexport WIFI_STA_PASS='"'"'SelfTestPass9'"'"'\nWIFI_STA_BSSID=de:ad:be:ef:00:01\n' > "$WIFI_CREDS"
    s3_load_needles >/dev/null
    [ "${#NEEDLE_VALUES[@]}" -eq 7 ] || _fail "(7) needle count is not 7 (3 values + 2 hex spellings x 2 text values): ${#NEEDLE_VALUES[@]}"
    printf 'wifi: ssid=selftestnet-42 pass=SELFTESTPASS9\nbssid DE-AD-BE-EF-00-01 connected\nhex 53 65 6c 66 54 65 73 74 4e 65 74 2d 34 32 dump\nlink 10.0.0.7 up\n' > "$_st"
    _pre="$(s3_residue_count "$_st")" || _fail "(7) checker returned non-zero on the needle fixture"
    [ "$_pre" = "5" ] || _fail "(7) residue before redact is not 5 (4 needles + 1 IPv4 line): $_pre"
    s3_redact_file "$_st" || _fail "(8) residue after needle redact"
    [ "$(_count '<REDACTED_SSID>' "$_st")" -eq 2 ] || _fail "(8) SSID tokens are not 2 (plain + hex)"
    [ "$(_count '<REDACTED_PASS>' "$_st")" -eq 1 ] || _fail "(8) PASS token is not 1"
    [ "$(_count '<REDACTED_BSSID>' "$_st")" -eq 1 ] || _fail "(8) BSSID token is not 1 (dash spelling)"
    [ "$(_count '<IPv4>' "$_st")" -eq 1 ] || _fail "(8) IPv4 token is not 1"
    #  (9) negative control: a needle the transformer was NOT given stays
    #  visible to the checker (the checker is not the transformer's echo)
    NEEDLE_VALUES+=("SelfTestExtra"); NEEDLE_TOKENS+=("<REDACTED_X>"); NEEDLE_KINDS+=("text")
    printf 'x SELFTESTEXTRA y\n' > "$_st"
    _pre="$(s3_residue_count "$_st")" || _fail "(9) checker failed"
    [ "$_pre" = "1" ] || _fail "(9) checker does not see the extra needle ($_pre)"
    #  (9b) mutation control for the hex-address rule: a file that only the
    #  transformer's address rule would clean must be counted as residue by
    #  the checker (1 line), be quarantined by s3_redact_check when NOT
    #  transformed, and be clean (0) once transformed. A 7-digit hex word is
    #  not an address and must be left alone (the rule is exactly 8 digits).
    NEEDLE_VALUES=(); NEEDLE_TOKENS=(); NEEDLE_KINDS=()
    printf 'DHCP address=0xC0A80105 lease\nreg address=0x1234567 short\n' > "$_st"
    _pre="$(s3_residue_count "$_st")" || _fail "(9b) checker failed on the hex-address fixture"
    [ "$_pre" = "1" ] || _fail "(9b) checker does not count an unmasked address=0x<8 hex> as residue ($_pre)"
    s3_redact_check "$_st" 2>/dev/null && _fail "(9b) an unmasked address=0x<8 hex> was not quarantined"
    mv -f -- "$_st.UNREDACTED" "$_st"
    s3_redact_file "$_st" || _fail "(9b) residue after the hex-address transform"
    [ "$(_count 'address=<HEX32>' "$_st")" -eq 1 ] || _fail "(9b) hex address mask is not 1"
    $GREP -qF 'address=0x1234567 short' "$_st" || _fail "(9b) the 7-digit hex word was altered"
    #  --- (10)-(12): marker counting on a fixture ---
    #  (10) a Wi-Fi capture with placeholders only: every counter non-zero
    #  where the fixture has the line, ssidraw 0, unexpected 0, scan = LAST N.
    #  The DHCP line appears in both spellings, the DIAG-ON one with
    #  " ip=.. gw=.." and the shipped stage's bare one (S5-3), so dhcp = 2:
    #  the counter is a prefix match and must take both.
    _mk="$_sd/markers.log"
    printf '%s\n' \
        'S' 'TOPPERS/FMP3 Kernel Release 3.2.1' '[Arduino] setup complete' \
        '[WiFiScan] found 3 APs' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=<SSID-0>' \
        '[WiFiScan] AP[1] rssi=-60 ch=6 SSID=<SSID-1>' \
        '[WiFiScan] AP[2] rssi=-70 ch=11 SSID=<SSID-2>' \
        '[WiFiScan] found 12 APs' \
        '[WiFiConnect] connected authmode=3 channel=6' \
        'net: DHCP bound ip=<IPv4> gw=<IPv4>' 'net: DHCP bound' \
        '[WiFiConnect] connected and DHCP completed' \
        'net: ping gateway -> OK' 'net: ping gateway -> timeout' \
        '[WiFiConnect] DNS completed host=example.com address=0x12345678' \
        '[WiFiConnect] DNS failed host=example.invalid error=-1 (unresolved)' \
        '[WiFiConnect] TCP request completed bytes=42' \
        '[WiFiConnect] disconnected reason=201 (NO_AP_FOUND) rssi=0' \
        '[WiFiConnect] begin: rejected or initialization failed' \
        '[Arduino] loop heartbeat 1000' '[Blink] ON' '[Blink] OFF' > "$_mk"
    _ml="$(s3_count_markers "$_mk")" || _fail "(10) s3_count_markers returned non-zero"
    _exp1='markers: banner=1 setup=1 heartbeat=1 unexpected=0 smark=1 blink=2'
    _exp2='wifi: scan=12 scanap=3 ssidraw=0 connected=1 dhcp=2 dhcpdone=1 ping=1 dnsok=1 dnsfail=1 tcp=1 disc=1 beginrej=1'
    [ "$(printf '%s\n' "$_ml" | sed -n 1p)" = "$_exp1" ] || _fail "(10) markers line: $(printf '%s\n' "$_ml" | sed -n 1p)"
    [ "$(printf '%s\n' "$_ml" | sed -n 2p)" = "$_exp2" ] || _fail "(10) wifi line: $(printf '%s\n' "$_ml" | sed -n 2p)"
    #  (11) positive controls for the new detectors: a leaked SSID column is
    #  counted (ssidraw 1 per such line, placeholders excluded), and each new
    #  unexpected pattern counts once.
    printf '%s\n' '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=SelfTestLeak' \
        '[WiFiScan] AP[1] rssi=-40 ch=1 SSID=<SSID-1>' \
        '[WiFiScan] AP[2] rssi=-40 ch=1 SSID=' \
        '[LWIP-ASSERT] "x" at file.c:1' 'abort() called' 'ROM newlib abort()' \
        'libc: _exit(1)' 'libc: _kill(sig=6)' '## Unexpected exception' > "$_mk"
    _ml="$(s3_count_markers "$_mk")" || _fail "(11) s3_count_markers returned non-zero"
    printf '%s\n' "$_ml" | sed -n 1p | $GREP -q ' unexpected=6 ' || _fail "(11) unexpected is not 6: $(printf '%s\n' "$_ml" | sed -n 1p)"
    printf '%s\n' "$_ml" | sed -n 2p | $GREP -q ' ssidraw=2 ' || _fail "(11) ssidraw is not 2: $(printf '%s\n' "$_ml" | sed -n 2p)"
    printf '%s\n' "$_ml" | sed -n 2p | $GREP -q '^wifi: scan=-1 scanap=3 ' || _fail "(11) scan/scanap without a found line: $(printf '%s\n' "$_ml" | sed -n 2p)"
    #  (12) an empty file: every count 0, scan -1 (the counters must not
    #  fail-open into a non-number when grep matches nothing).
    : > "$_mk"
    _ml="$(s3_count_markers "$_mk")" || _fail "(12) s3_count_markers returned non-zero on an empty file"
    [ "$(printf '%s\n' "$_ml" | sed -n 1p)" = 'markers: banner=0 setup=0 heartbeat=0 unexpected=0 smark=0 blink=0' ] || _fail "(12) markers line on empty: $(printf '%s\n' "$_ml" | sed -n 1p)"
    [ "$(printf '%s\n' "$_ml" | sed -n 2p)" = 'wifi: scan=-1 scanap=0 ssidraw=0 connected=0 dhcp=0 dhcpdone=0 ping=0 dnsok=0 dnsfail=0 tcp=0 disc=0 beginrej=0' ] || _fail "(12) wifi line on empty: $(printf '%s\n' "$_ml" | sed -n 2p)"
    #  (13)-(15) the redact-only guard (M-6), through the script itself so
    #  the entry path is what is tested. (13) a git-tracked file (this
    #  script) is refused and untouched; (14) a file outside LOG_DIR is
    #  refused and untouched; (15) the same file with S3_REDACT_ANYWHERE=1,
    #  and a file inside LOG_DIR without it, are masked (positive control:
    #  the refusals are not a broken entry path).
    _self="$0"
    git -C "$(dirname -- "$_self")" ls-files --error-unmatch -- "$(basename -- "$_self")" >/dev/null 2>&1 \
        || _fail "(13) selftest needs \$0 to be a git-tracked file ($_self)"
    _sha_before="$(sha256sum -- "$_self" | cut -c1-64)"
    ( S3_MASK_SELFTEST=0 S3_REDACT_ONLY=1 LOG_DIR="$_sd/logs" bash "$_self" "$_self" >/dev/null 2>"$_sd/g13.err" ) && _fail "(13) a git-tracked file was accepted"
    $GREP -q 'refuses a git-tracked file' "$_sd/g13.err" || _fail "(13) refusal reason not stated: $(cat "$_sd/g13.err")"
    [ "$(sha256sum -- "$_self" | cut -c1-64)" = "$_sha_before" ] || _fail "(13) the tracked file was modified"
    #  the git-tracked rule is not lifted by S3_REDACT_ANYWHERE
    ( S3_MASK_SELFTEST=0 S3_REDACT_ONLY=1 S3_REDACT_ANYWHERE=1 LOG_DIR="$_sd/logs" bash "$_self" "$_self" >/dev/null 2>&1 ) && _fail "(13b) S3_REDACT_ANYWHERE lifted the git-tracked rule"
    [ "$(sha256sum -- "$_self" | cut -c1-64)" = "$_sha_before" ] || _fail "(13b) the tracked file was modified"
    mkdir -p "$_sd/logs" "$_sd/elsewhere"
    printf 'got ip 192.168.4.23\n' > "$_sd/elsewhere/x.log"
    ( S3_MASK_SELFTEST=0 S3_REDACT_ONLY=1 LOG_DIR="$_sd/logs" WIFI_CREDS="$_sd/no-such-creds.sh" bash "$_self" "$_sd/elsewhere/x.log" >/dev/null 2>"$_sd/g14.err" ) && _fail "(14) a file outside LOG_DIR was accepted"
    $GREP -q 'refuses a file outside LOG_DIR' "$_sd/g14.err" || _fail "(14) refusal reason not stated: $(cat "$_sd/g14.err")"
    $GREP -q '192.168.4.23' "$_sd/elsewhere/x.log" || _fail "(14) the refused file was modified"
    ( S3_MASK_SELFTEST=0 S3_REDACT_ONLY=1 S3_REDACT_ANYWHERE=1 LOG_DIR="$_sd/logs" WIFI_CREDS="$_sd/no-such-creds.sh" bash "$_self" "$_sd/elsewhere/x.log" >/dev/null 2>&1 ) || _fail "(15) S3_REDACT_ANYWHERE=1 did not accept a file outside LOG_DIR"
    [ "$(cat "$_sd/elsewhere/x.log")" = 'got ip <IPv4>' ] || _fail "(15) the file was not masked: $(cat "$_sd/elsewhere/x.log")"
    printf 'got ip 192.168.4.23\n' > "$_sd/logs/y.log"
    ( S3_MASK_SELFTEST=0 S3_REDACT_ONLY=1 LOG_DIR="$_sd/logs" WIFI_CREDS="$_sd/no-such-creds.sh" bash "$_self" "$_sd/logs/y.log" >/dev/null 2>&1 ) || _fail "(15b) a file inside LOG_DIR was refused"
    [ "$(cat "$_sd/logs/y.log")" = 'got ip <IPv4>' ] || _fail "(15b) the file inside LOG_DIR was not masked: $(cat "$_sd/logs/y.log")"
    #  --- (16) EXTRA_MARKERS: fixed strings, counted literally ---
    _ex="$_sd/extra.log"
    printf '%s\n' '[S3-INTR] VERDICT PASS edges=10' 'noise S3' '[S3-RGB] tx_done=3' \
        '[S3-INTR] VERDICT PASS edges=11' '[S3-INTR] VERDICT FAIL' 'S' '3' '[S3] tag' > "$_ex"
    _el="$(EXTRA_MARKERS='[S3-INTR] VERDICT PASS|[S3-RGB] tx_done' s3_count_extra "$_ex")" || _fail "(16) s3_count_extra returned non-zero"
    [ "$_el" = 'extra: [S3-INTR] VERDICT PASS=2 [S3-RGB] tx_done=1' ] || _fail "(16) extra line: $_el"
    #  regex metacharacters are literal: as an ERE "[S3]" matches every line
    #  holding a C or a 5 (all 8 -- checked here so the discriminator is
    #  known to discriminate); as a fixed string exactly the one "[S3] tag"
    [ "$($GREP -acE '[S3]' "$_ex")" -eq 8 ] || _fail "(16) the ERE control does not match all 8 lines"
    _el="$(EXTRA_MARKERS='[S3]' s3_count_extra "$_ex")" || _fail "(16) s3_count_extra returned non-zero on [S3]"
    [ "$_el" = 'extra: [S3]=1' ] || _fail "(16) '[S3]' was not counted as a fixed string: $_el"
    _el="$(EXTRA_MARKERS='' s3_count_extra "$_ex")" || _fail "(16) s3_count_extra returned non-zero on an empty list"
    [ -z "$_el" ] || _fail "(16) empty EXTRA_MARKERS printed something: $_el"
    _el="$(EXTRA_MARKERS='absent-string' s3_count_extra "$_ex")" || _fail "(16) s3_count_extra returned non-zero on an absent string"
    [ "$_el" = 'extra: absent-string=0' ] || _fail "(16) an absent string is not 0: $_el"
    #  --- (17) probe decision: pure function on (cold heartbeat force dryrun) ---
    _jw() { [ "$(s3_jtag_wanted "$1" "$2" "$3" "$4" ${5:+"$5"})" = "$6" ] || _fail "(17) s3_jtag_wanted $1 $2 $3 $4${5:+ $5} is not $6"; }
    _jw 1 0 0 0 '' yes; _jw 1 5 0 0 '' no; _jw 0 0 0 0 '' no; _jw 0 5 1 0 '' yes; _jw 1 0 0 1 '' no; _jw 1 0 1 1 '' no
    _jw 1 0 0 0 0 no       # S3_JTAG_ON_SILENT=0 disables the silent-cold rule
    _jw 1 0 1 0 0 yes      # ... but not the forced mode
    #  --- (18) probe parser on canned openocd output ---
    _jl="$_sd/jtag.log"
    _jfix() {   # <serial line or ""> <pc1> <pc2> <read-1 value> <read-2 value or ""> -> fixture
        {
            echo 'Open On-Chip Debugger v0.12.0-esp32-20251215 (2025-12-15-18:17)'
            [ -z "$1" ] || echo "$1"
            echo 'Info : esp_usb_jtag: Device found. Base speed 40000KHz, div range 1 to 255'
            echo 'Info : [esp32s3] Target halted, pc=0x40800abc'
            echo '== read 1 =='
            echo "pc (/32): $2"
            echo '0x60038004: 00000002 '
            echo '0x60038008: 00000000 '
            echo '0x60096110: b0030200 '
            echo '0x60096118: 00000000 '
            echo '0x40800004: b0030200 00000002 00000000 b0030200 00000000 00000000 00000003 00000001 '
            echo "0x40800123: $4 "
            echo '== read 2 =='
            echo "pc (/32): $3"
            [ -z "$5" ] || echo "0x40800123: $5 "
            echo 'shutdown command invoked'
        } > "$_jl"
    }
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800def 000003e8 000007d0
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) s3_jtag_parse returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'loop_calls: 1000 -> 2000 delta=1000' || _fail "(18) loop_calls line: $_jp"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: alive' || _fail "(18) verdict is not alive: $_jp"
    printf '%s\n' "$_jp" | $GREP -qxF 'pc1=0x40800abc pc2=0x40800def' || _fail "(18) pc line: $_jp"
    printf '%s\n' "$_jp" | $GREP -qxF 'ep1_conf=0x00000002 int_raw=0x00000000   (data_free=1; raw evidence, not a criterion)' || _fail "(18) ep1_conf line: $_jp"
    printf '%s\n' "$_jp" | $GREP -q '^# ep1_conf/int_raw are recorded for comparison' || _fail "(18) raw-evidence note missing: $_jp"
    #  the address spelling is normalized (upper case, no leading zeros)
    _jp="$(s3_jtag_parse "$_jl" 40800123)" || _fail "(18) s3_jtag_parse (bare addr) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: alive' || _fail "(18) bare-address spelling not accepted: $_jp"
    #  equal values -> not-advancing (the pc differing must not rescue it)
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800def 000003e8 000003e8
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (equal) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'loop_calls: 1000 -> 1000 delta=0' || _fail "(18) equal loop_calls line: $_jp"
    printf '%s\n' "$_jp" | $GREP -q '^verdict: not-advancing' || _fail "(18) equal values are not not-advancing: $_jp"
    #  no serial line, rc 0 (or unknown) -> refused, and no value is printed at all
    _jfix "" 0x40800abc 0x40800def 000003e8 000007d0
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (no serial) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: refused' || _fail "(18) missing serial is not refused: $_jp"
    printf '%s\n' "$_jp" | $GREP -q '^# REFUSED: openocd did not report the pinned serial' || _fail "(18) REFUSED line missing: $_jp"
    printf '%s\n' "$_jp" | $GREP -qE '^(pc1=|ep1_conf=)|delta=' && _fail "(18) a value was printed although refused: $_jp"
    _jp="$(s3_jtag_parse "$_jl" 0x40800123 "no ELF" 0)" || _fail "(18) parse (no serial, rc 0) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: refused' || _fail "(18) missing serial with rc 0 is not refused: $_jp"
    #  no serial line at all AND openocd failed (no device found) -> not-run, not refused, no values
    _jp="$(s3_jtag_parse "$_jl" 0x40800123 "no ELF" 1)" || _fail "(18) parse (no serial, rc 1) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: not-run (openocd rc=1, no serial line)' || _fail "(18) no device + rc 1 is not not-run: $_jp"
    printf '%s\n' "$_jp" | $GREP -q 'refused\|REFUSED' && _fail "(18) no device + rc 1 was labelled refused: $_jp"
    printf '%s\n' "$_jp" | $GREP -qE '^(pc1=|ep1_conf=)|delta=' && _fail "(18) a value was printed although not-run: $_jp"
    #  a DIFFERENT board's serial (the recorded accident) -> refused too,
    #  whatever the rc (the not-run sub-case needs NO serial line at all)
    _jfix "Info : esp_usb_jtag: serial (F4:12:FA:5B:4A:58)" 0x40800abc 0x40800def 000003e8 000007d0
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (other serial) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: refused' || _fail "(18) another board's serial is not refused: $_jp"
    _jp="$(s3_jtag_parse "$_jl" 0x40800123 "no ELF" 1)" || _fail "(18) parse (other serial, rc 1) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: refused' || _fail "(18) another board's serial with rc 1 is not refused: $_jp"
    #  counter went backwards (b < a) -> "(wrapped)" on the loop_calls line, not-run, never alive
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800def 000007d0 000003e8
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (backwards) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'loop_calls: 2000 -> 1000 delta=4294966296 (wrapped)' || _fail "(18) backwards loop_calls line: $_jp"
    printf '%s\n' "$_jp" | $GREP -q '^verdict: not-run (counter went backwards' || _fail "(18) backwards counter is not not-run: $_jp"
    printf '%s\n' "$_jp" | $GREP -q 'alive' && _fail "(18) backwards counter reported alive: $_jp"
    #  mutation control: second read missing -> not-run, never alive
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800def 000003e8 ""
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (second read missing) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -q '^verdict: not-run (incomplete: second read missing)' || _fail "(18) missing second read is not not-run: $_jp"
    printf '%s\n' "$_jp" | $GREP -q 'alive' && _fail "(18) missing second read reported alive: $_jp"
    #  symbol unknown: pc equal -> not-advancing; pc moved -> not-run (inconclusive)
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800abc 000003e8 000007d0
    _jp="$(s3_jtag_parse "$_jl" "" "no ELF")" || _fail "(18) parse (no symbol) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -qxF 'loop_calls: symbol unavailable (no ELF)' || _fail "(18) symbol-unavailable line: $_jp"
    printf '%s\n' "$_jp" | $GREP -qxF 'verdict: not-advancing (loop_calls unknown; pc1 == pc2)' || _fail "(18) no symbol + equal pc: $_jp"
    _jfix "Info : esp_usb_jtag: serial ($dut_uc)" 0x40800abc 0x40800def 000003e8 000007d0
    _jp="$(s3_jtag_parse "$_jl" "")" || _fail "(18) parse (no symbol, pc moved) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -q '^verdict: not-run (loop_calls unknown; pc moved' || _fail "(18) no symbol + moved pc: $_jp"
    printf '%s\n' "$_jp" | $GREP -q 'alive' && _fail "(18) no symbol + moved pc reported alive: $_jp"
    #  no output at all (openocd died before printing) -> not-run
    : > "$_jl"
    _jp="$(s3_jtag_parse "$_jl" 0x40800123)" || _fail "(18) parse (empty) returned non-zero"
    printf '%s\n' "$_jp" | $GREP -q '^verdict: not-run (no openocd output)' || _fail "(18) empty output is not not-run: $_jp"
    #  --- (19) the bootloader address gate: 0x0 from boards.txt, everything else refused ---
    _bt="$_sd/boards.txt"
    printf 'm5stampc5_fmp3.build.bootloader_addr=0x2000\nm5atoms3lite_fmp3.name=X\nm5atoms3lite_fmp3.build.bootloader_addr=0x0\n' > "$_bt"
    _ba="$(s3_bootloader_addr "$_bt")" || _fail "(19) 0x0 was refused: $_ba"
    [ "$_ba" = "0x0" ] || _fail "(19) value is not 0x0: $_ba"
    printf 'm5atoms3lite_fmp3.build.bootloader_addr=0x0\r\n' > "$_bt"
    _ba="$(s3_bootloader_addr "$_bt")" || _fail "(19) CRLF line was refused: $_ba"
    [ "$_ba" = "0x0" ] || _fail "(19) CRLF value is not 0x0: $_ba"
    _bneg() {   # <label> <boards.txt content or MISSING> <reason substring>
        local out
        if [ "$2" = "MISSING" ]; then rm -f "$_bt"; else printf '%b' "$2" > "$_bt"; fi
        out="$(s3_bootloader_addr "$_bt")" && _fail "(19) $1 was accepted: $out"
        printf '%s' "$out" | $GREP -qF "$3" || _fail "(19) $1: reason not stated ($3): $out"
    }
    _bneg "missing file" MISSING "not found"
    _bneg "missing key" 'm5nanoc6_fmp3.build.bootloader_addr=0x0\n' "not in"
    _bneg "key with the dots replaced (the ERE must treat them literally)" 'm5atoms3lite_fmp3Xbuild_bootloader_addr=0x0\n' "not in"
    _bneg "0x2000 (the C5 address)" 'm5atoms3lite_fmp3.build.bootloader_addr=0x2000\n' "expected 0x0"
    _bneg "0x1000 (the ESP32 address)" 'm5atoms3lite_fmp3.build.bootloader_addr=0x1000\n' "expected 0x0"
    _bneg "decimal 0" 'm5atoms3lite_fmp3.build.bootloader_addr=0\n' "non-hex"
    _bneg "empty value" 'm5atoms3lite_fmp3.build.bootloader_addr=\n' "non-hex"
    _bneg "duplicate key" 'm5atoms3lite_fmp3.build.bootloader_addr=0x0\nm5atoms3lite_fmp3.build.bootloader_addr=0x0\n' "appears 2 times"
    #  --- (21) the SSID column mask: this port's scan prints the real names ---
    _ss="$_sd/ssid.log"
    printf '%s\n' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=NeighbourNet' \
        '[WiFiScan] AP[1] rssi=-60 ch=6 SSID=Another Net With Spaces' \
        '[WiFiScan] found 2 APs' > "$_ss"
    s3_redact_file "$_ss" || _fail "(21) redact failed on the SSID fixture"
    [ "$($GREP -ac 'SSID=<SSID-redacted>' "$_ss")" -eq 2 ] || _fail "(21) SSID column not masked: $(cat "$_ss")"
    if $GREP -aqE 'NeighbourNet|Another Net' "$_ss"; then _fail "(21) a neighbour SSID survived the mask"; fi
    if ! $GREP -aqxF '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=<SSID-redacted>' "$_ss"; then _fail "(21) the rest of the line was damaged: $(sed -n 1p "$_ss")"; fi
    _sl="$(s3_count_markers "$_ss" | sed -n 2p)"
    case "$_sl" in *' ssidraw=0 '*) : ;; *) _fail "(21) ssidraw is not 0 after the mask: $_sl" ;; esac
    #  negative control: without the transform the same fixture counts 2
    printf '%s\n' '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=NeighbourNet' \
        '[WiFiScan] AP[1] rssi=-60 ch=6 SSID=Another Net With Spaces' > "$_ss"
    _sl="$(s3_count_markers "$_ss" | sed -n 2p)"
    case "$_sl" in *' ssidraw=2 '*) : ;; *) _fail "(21) the unmasked control does not count 2: $_sl" ;; esac
    #  and the checker alone (no transform) quarantines such a file: an
    #  unmasked SSID column counts as residue, so a log that skipped the
    #  transform can never be stored.
    _pre="$(s3_residue_count "$_ss")" || _fail "(21) checker failed on the unmasked fixture"
    [ "$_pre" -ge 2 ] || _fail "(21) unmasked SSIDs are not counted as residue ($_pre)"
    s3_redact_check "$_ss" 2>/dev/null && _fail "(21) an unmasked SSID column was not quarantined"
    [ -f "$_ss.UNREDACTED" ] || _fail "(21) no .UNREDACTED after the SSID quarantine"
    [ ! -f "$_ss" ] || _fail "(21) the quarantined file was left in place"
    rm -f "$_ss.UNREDACTED"
    rm -rf "$_sd"
    echo "s3 redact selftest PASS: (1) residue 5 (2) quarantine rc!=0 + .UNREDACTED (3) transformer failure -> rc 93 (4) masked: peer 4 (EUI-64 whole) / IPv4 2 / HEX32 2 / DUT kept (5) DUT_MAC unset -> 7 masks, no tails (6) checker failure -> empty (7) creds needles 7, residue 5 (8) tokens SSID 2 / PASS 1 / BSSID 1 / IPv4 1 (9) checker sees an untransformed needle (9b) address=0x<8 hex>: residue 1 / quarantined / masked 1 / 7-digit left (10) markers: fixture counts exact (dhcp 2 = both spellings), scan = last N (11) ssidraw 2 / unexpected 6 (12) empty file -> zeros, scan -1 (13) redact-only refuses a git-tracked file, also with S3_REDACT_ANYWHERE (14) refuses outside LOG_DIR, file untouched (15) S3_REDACT_ANYWHERE=1 / inside LOG_DIR -> masked (16) EXTRA_MARKERS: fixed strings counted (2/1), '[S3]' literal = 1 (ERE would be 8), empty -> nothing, absent -> 0 (17) s3_jtag_wanted: 6 asserted tuples + on_silent=0 (18) s3_jtag_parse: alive (1000 -> 2000, data_free=1 raw only) / not-advancing / refused (no serial with rc 0, other serial with rc 0 and 1: no values) / no serial + rc 1 -> not-run (no device), no values / backwards 2000 -> 1000 -> (wrapped), not-run, never alive / second read missing -> not-run, never alive / no symbol: pc equal -> not-advancing, pc moved -> not-run / empty -> not-run (19) bootloader address: 0x0 (LF and CRLF) accepted; missing file / missing key / dots-as-any-char / 0x2000 / 0x1000 / decimal / empty / duplicate key refused with a reason (21) SSID column: 2 masked to <SSID-redacted>, neighbour names gone, rest of the line intact, ssidraw 0 (unmasked control 2, which the checker quarantines)"
    exit 0
fi

#  ---------------------------------------------------------------- redact-only
#  LOG_DIR is set here rather than with the other output paths below because
#  the redact-only guard needs it; the value is the same.
LOG_DIR="${LOG_DIR:-$HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/.steering/20260917-atoms3lite-plan/stage2/logs}"

#  The M-6 guard. Rewriting in place is fine for a capture this script
#  wrote; it is not fine for anything else, and the only difference between
#  the two on the command line is where the file lives. Two rules, checked
#  before anything is touched: a file git tracks is never masked (masked
#  source is still source, and would be committed as such), and a file
#  outside LOG_DIR is masked only with S3_REDACT_ANYWHERE=1 said out loud.
s3_redact_guard() {
    local f="$1" abs dir_abs
    if git -C "$(dirname -- "$f")" ls-files --error-unmatch -- "$(basename -- "$f")" >/dev/null 2>&1; then
        die "S3_REDACT_ONLY refuses a git-tracked file: $f (a masked source or document is not a redacted log)"
    fi
    abs="$(realpath -m -- "$f")"; dir_abs="$(realpath -m -- "$LOG_DIR")"
    case "$abs" in
        "$dir_abs"/*) ;;
        *) [ "${S3_REDACT_ANYWHERE:-0}" = "1" ] \
               || die "S3_REDACT_ONLY refuses a file outside LOG_DIR ($LOG_DIR): $f (set S3_REDACT_ANYWHERE=1 for a log kept elsewhere)" ;;
    esac
}
if [ "${S3_REDACT_ONLY:-0}" = "1" ]; then
    [ "$#" -ge 1 ] || die "S3_REDACT_ONLY=1 needs the files to redact as arguments"
    for f in "$@"; do [ -f "$f" ] || die "not a file: $f"; done
    for f in "$@"; do s3_redact_guard "$f"; done
    s3_load_needles
    S3_FILES=("$@")
    s3_redact_on_exit
    exit "$?"
fi

trap 's3_redact_on_exit' EXIT

#  ---------------------------------------------------------------- modes
DRYRUN="${DRYRUN:-0}"; NOFLASH="${NOFLASH:-0}"; COLD="${COLD:-0}"; NORESET="${NORESET:-0}"
S3_JTAG_ON_SILENT="${S3_JTAG_ON_SILENT:-1}"; S3_JTAG_FORCE="${S3_JTAG_FORCE:-0}"
for _v_name in DRYRUN NOFLASH COLD NORESET S3_JTAG_ON_SILENT S3_JTAG_FORCE; do
    eval "_v_val=\$$_v_name"
    case "$_v_val" in
        0|1) ;;
        *) die "$_v_name must be 0 or 1 (given: $_v_val)" ;;
    esac
done
unset _v_name _v_val
if [ "$COLD" = "1" ]; then NOFLASH=1; NORESET=1; fi
CAPTURE_SEC="${CAPTURE_SEC:-60}"
COLD_WAIT_SEC="${COLD_WAIT_SEC:-120}"
for _v_name in CAPTURE_SEC COLD_WAIT_SEC; do
    eval "_v_val=\$$_v_name"
    case "$_v_val" in
        ''|*[!0-9]*) die "$_v_name must be a non-negative integer (given: $_v_val)" ;;
    esac
done
unset _v_name _v_val
MARKERS="${MARKERS:-}"
BAUD="${BAUD:-921600}"

#  ---------------------------------------------------------------- 0. gate part 1
#  String compare only -- runs before anything touches hardware, in every mode.
for m in $FORBIDDEN; do
    [ "$dut_lc" = "$m" ] && die "DUT_MAC $DUT_MAC is in the FORBIDDEN list; nothing done"
done

s3_load_needles

#  ---------------------------------------------------------------- 1. inputs
ARDUINO_DATA="${ARDUINO_DIRECTORIES_DATA:-$HOME/.arduino15}"
_newest_dir() {   # newest version directory under $1 by version sort
    ls -d "$1"/*/ 2>/dev/null | sed 's#/$##' | sort -V | tail -1 || true
}
if [ -z "${ESPTOOL:-}" ]; then
    _d="$(_newest_dir "$ARDUINO_DATA/packages/m5stack/tools/esptool_py")"
    ESPTOOL="${_d:+$_d/esptool}"
fi
if [ -z "${BOOT_APP0:-}" ]; then
    _d="$(_newest_dir "$ARDUINO_DATA/packages/m5stack/hardware/esp32")"
    BOOT_APP0="${_d:+$_d/tools/partitions/boot_app0.bin}"
fi
#  The probe's openocd is pinned to the version the core ships (not
#  _newest_dir: a different openocd-esp32 has not been run against this
#  board). PATH is never searched.
OPENOCD="${OPENOCD:-$ARDUINO_DATA/packages/m5stack/tools/openocd-esp32/v0.12.0-esp32-20251215/bin/openocd}"

#  Pick the sketch images from SKETCH_BUILD (one project unless PROJECT= says).
_pick_image() {   # suffix -> path (exactly one match required)
    local suffix="$1" n
    local -a c=()
    if [ -n "${PROJECT:-}" ]; then
        c=("$SKETCH_BUILD/${PROJECT}${suffix}")
    else
        while IFS= read -r -d '' f; do c+=("$f"); done < <(
            find "$SKETCH_BUILD" -maxdepth 1 -type f -name "*.ino${suffix}" -print0 | sort -z)
    fi
    n=${#c[@]}
    [ "$n" -eq 1 ] || { echo "expected exactly one *.ino${suffix} in $SKETCH_BUILD, found $n (set PROJECT=)" >&2; return 1; }
    [ -f "${c[0]}" ] || { echo "missing: ${c[0]}" >&2; return 1; }
    printf '%s\n' "${c[0]}"
}
IMG_BL="${BOOTLOADER:-}"; IMG_PT="${PTABLE:-}"; IMG_APP="${APP:-}"
BL_ADDR=""   # set ONLY by the boards.txt gate below; never a literal
if [ -n "${SKETCH_BUILD:-}" ]; then
    [ -d "$SKETCH_BUILD" ] || die "SKETCH_BUILD is not a directory: $SKETCH_BUILD"
    [ -n "$IMG_BL" ]  || IMG_BL="$(_pick_image .bootloader.bin)"  || die "bootloader image not found"
    [ -n "$IMG_PT" ]  || IMG_PT="$(_pick_image .partitions.bin)"  || die "partition table image not found"
    [ -n "$IMG_APP" ] || IMG_APP="$(_pick_image .bin)"            || die "app image not found"
fi
if [ "$COLD" != "1" ] && { [ "$NOFLASH" != "1" ] || [ "$DRYRUN" = "1" ]; }; then
    [ -n "$IMG_BL" ] && [ -n "$IMG_PT" ] && [ -n "$IMG_APP" ] \
        || die "set SKETCH_BUILD=<arduino-cli --build-path dir> (or BOOTLOADER=, PTABLE= and APP=)"
    for f in "$IMG_BL" "$IMG_PT" "$IMG_APP"; do [ -f "$f" ] || die "image missing: $f"; done
    if [ "$BOOT_APP0" != "none" ]; then
        [ -n "$BOOT_APP0" ] && [ -f "$BOOT_APP0" ] || die "boot_app0.bin not found (BOOT_APP0=$BOOT_APP0; set BOOT_APP0=<path> or =none)"
    fi
    #  Plausibility of what would be written (an empty or truncated file, or a
    #  file that is not an ESP image, is refused before esptool is even found).
    _first_byte() { od -An -tx1 -N1 "$1" | tr -d ' \n'; }
    _seg_count()  { od -An -tu1 -j1 -N1 "$1" | tr -d ' \n'; }
    _check_image() {   # label path kind(esp|size:<n>)
        local label="$1" f="$2" kind="$3" sz
        [ -s "$f" ] || die "$label image is empty (0 bytes): $f"
        sz="$(stat -c %s "$f")"
        case "$kind" in
            #  ESP image: magic 0xE9, a 24-byte header plus at least one 8-byte
            #  segment header, and a segment count in 1..16 (esp_image_header_t).
            esp)  [ "$(_first_byte "$f")" = "e9" ] \
                      || die "$label image does not start with the ESP image magic 0xE9 (first byte 0x$(_first_byte "$f")): $f"
                  [ "$sz" -ge 32 ] \
                      || die "$label image is shorter than an ESP image header + one segment header (32 bytes; is $sz): $f"
                  [ "$(_seg_count "$f")" -ge 1 ] && [ "$(_seg_count "$f")" -le 16 ] \
                      || die "$label image segment count byte is not in 1..16 (is $(_seg_count "$f")): $f" ;;
            size:*) [ "$sz" -eq "${kind#size:}" ] \
                      || die "$label image must be exactly ${kind#size:} bytes (is $sz): $f" ;;
        esac
    }
    _check_image bootloader "$IMG_BL" esp
    _check_image "partition table" "$IMG_PT" size:3072
    [ "$BOOT_APP0" = "none" ] || _check_image boot_app0 "$BOOT_APP0" size:8192
    _check_image app "$IMG_APP" esp
    say "images plausible: bootloader/app are ESP images (0xE9, header, 1..16 segments), partition table 3072 B$([ "$BOOT_APP0" = "none" ] || echo ", boot_app0 8192 B")"
    #  The bootloader address comes from the installed platform, never from a
    #  literal here, and must be the S3's 0x0 (gate 2b).
    BL_ADDR="$(s3_bootloader_addr "$BOARDS_TXT")" || die "bootloader address: $BL_ADDR"
    say "bootloader address $BL_ADDR from $BOARDS_TXT ($BOARDS_KEY)"
    [ -n "$ESPTOOL" ] && [ -x "$ESPTOOL" ] || die "esptool not found (ESPTOOL=$ESPTOOL)"
fi

#  The write list, in the upload recipe's order and addresses (bootloader at
#  BL_ADDR = 0x0 from boards.txt, gate 2b).
#  The C5 script erases flash 0x0-0x1FFF before every write (a leftover
#  Direct Boot magic there outranks the bootloader at 0x2000 on that chip).
#  This chip's bootloader IS at 0x0, so there is nothing to erase and an
#  erase would only delete what the write puts back. The read-back is a
#  positive check instead: the first sector of flash must come back equal to
#  the first sector of the bootloader image that was just written.
RB_START=0x0; RB_LEN=0x1000          # flash 0x0-0x0FFF: the bootloader's first sector
#  BL_ADDR is empty in the modes that never write (NOFLASH=1 / COLD=1); the
#  commands below are then assembled but never run. Any mode that can write
#  must have passed the boards.txt gate, so an empty BL_ADDR there is a bug
#  in this script, not a case to paper over with a literal.
if [ "$COLD" != "1" ] && { [ "$NOFLASH" != "1" ] || [ "$DRYRUN" = "1" ]; }; then
    [ -n "$BL_ADDR" ] || die "internal: bootloader address unset on a writing path (the boards.txt gate did not run)"
fi
WRITE_ARGS=("$BL_ADDR" "$IMG_BL" 0x8000 "$IMG_PT")
[ "$BOOT_APP0" = "none" ] || WRITE_ARGS+=(0xe000 "$BOOT_APP0")
WRITE_ARGS+=(0x10000 "$IMG_APP")
N_IMAGES=$(( ${#WRITE_ARGS[@]} / 2 ))
#  One writing run = two esptool calls chained in the ROM download mode that
#  the identification (flash-id --after no-reset) left the chip in:
#  write-flash, then read-flash (the last one wakes the board with
#  --after hard-reset). esptool 5.2.0 re-uploads its stub per call and no
#  reset happens in between.
FLASH_CMD=("$ESPTOOL" --chip esp32s3 --port "$DUT_PORT" --baud "$BAUD"
           --before no-reset --after no-reset write-flash -z
           --flash-mode keep --flash-freq keep --flash-size keep "${WRITE_ARGS[@]}")
READBACK_CMD_PREFIX=("$ESPTOOL" --chip esp32s3 --port "$DUT_PORT" --baud "$BAUD"
           --before no-reset --after hard-reset read-flash "$RB_START" "$RB_LEN")

print_write_plan() {
    local i
    say "write plan, step 1: write-flash (address  sha256  size  file):"
    for ((i = 0; i < ${#WRITE_ARGS[@]}; i += 2)); do
        printf '    %-8s %s  %8d  %s\n' "${WRITE_ARGS[$i]}" \
            "$(sha256sum "${WRITE_ARGS[$((i+1))]}" | cut -c1-64)" \
            "$(stat -c %s "${WRITE_ARGS[$((i+1))]}")" "${WRITE_ARGS[$((i+1))]}"
    done
    [ "$BOOT_APP0" = "none" ] && say "    WARNING: BOOT_APP0=none -- 0xe000 (otadata/boot_app0) is NOT written. The stage 2 plan forbids this except as a recorded deviation."
    printf '    '; printf '%q ' "${FLASH_CMD[@]}"; printf '\n'
    say "write plan, step 2: read-flash $RB_START $RB_LEN -> must equal the first $((RB_LEN)) bytes of the bootloader image; --after hard-reset wakes the board"
    printf '    '; printf '%q ' "${READBACK_CMD_PREFIX[@]}" '<tmpfile>'; printf '\n'
}

#  ---------------------------------------------------------------- 2. output paths
#  LOG_DIR: set above the redact-only block.
OUT="${OUT:-$LOG_DIR/s3-capture-$(date +%Y%m%d-%H%M%S).log}"
BASE="${OUT%.log}"
IDENT_LOG="$BASE.ident.log"; FLASH_LOG="$BASE.flash.log"; COLD_TXT="$BASE.cold.txt"; SHA_TXT="$BASE.sha.txt"
JOURNAL_TXT="$BASE.journal.txt"
JTAG_LOG="$BASE.jtag.log"; JTAG_TXT="$BASE.jtag.txt"

#  ---------------------------------------------------------------- 3. capture prerequisites
#  Checked BEFORE anything touches the board: a write followed by a monitor
#  that cannot start would leave the board's new state unobserved.
#  esp_idf_monitor (the same reader the development-side script uses): it
#  follows the USB re-enumeration that a reset of a USJ-console chip causes,
#  which a plain pyserial reader would lose.
if [ -z "${IDF_PYTHON:-}" ]; then
    IDF_PYTHON="$(ls -d "$HOME"/tools/espressif/python_env/idf*_py3.*_env/bin/python 2>/dev/null | sort -V | tail -1 || true)"
    [ -n "$IDF_PYTHON" ] || IDF_PYTHON="$(ls -d "$HOME"/.espressif/python_env/idf*_py3.*_env/bin/python 2>/dev/null | sort -V | tail -1 || true)"
fi
[ -n "$IDF_PYTHON" ] && [ -x "$IDF_PYTHON" ] || die "no ESP-IDF python env with esp_idf_monitor (set IDF_PYTHON=)"
"$IDF_PYTHON" -c 'import esp_idf_monitor' 2>/dev/null || die "esp_idf_monitor is not importable by $IDF_PYTHON"
command -v script >/dev/null 2>&1 || die "the 'script' utility (util-linux) is required for the capture"
if [ "$COLD" != "1" ]; then
    [ -e "$DUT_PORT" ] || die "DUT by-id node is absent: $DUT_PORT"
fi
say "capture prerequisites OK: monitor python $IDF_PYTHON"

#  ---------------------------------------------------------------- 4. gate part 2 (hardware, read-only)
#  --after for the identification: the board is left in the ROM download mode
#  (no-reset) when a write follows (the erase/write/read-back chain continues
#  there with --before no-reset); hard-reset when nothing will be written and
#  the monitor will not reset either (NOFLASH=1 NORESET=1), so the monitor
#  listens to a running board, and in DRYRUN (override below), so the board
#  is not parked in the download mode by a run that does nothing else.
IDENT_AFTER=no-reset
if [ "$NOFLASH" = "1" ] && [ "$NORESET" = "1" ] && [ "$DRYRUN" != "1" ]; then IDENT_AFTER=hard-reset; fi
#  DRYRUN: nothing follows the identification, so wake the board instead of
#  parking it in the download mode (the development-side script does the same).
if [ "$DRYRUN" = "1" ]; then IDENT_AFTER=hard-reset; fi
if [ "$COLD" = "1" ]; then
    say "COLD=1: esptool is not called at all (its connect would reset the chip and overwrite the cold boot)"
    say "  expected MAC=$DUT_MAC (by-id derivation only)  port=$DUT_PORT"
    if [ "$DRYRUN" = "1" ]; then say "DRYRUN=1: stopping here (nothing done)"; exit 0; fi
else
    [ -n "$ESPTOOL" ] && [ -x "$ESPTOOL" ] || die "esptool not found (ESPTOOL=$ESPTOOL)"
    say "==== 1. identify the DUT before anything is written (read-only, ROM only, --after $IDENT_AFTER) ===="
    say "  expected MAC=$DUT_MAC  chip=\"$DUT_CHIP\"  port=$DUT_PORT"
    say "  esptool: $ESPTOOL ($("$ESPTOOL" version 2>/dev/null | tail -1))"
    mkdir -p "$(dirname "$OUT")"
    S3_FILES+=("$IDENT_LOG")
    if ! "$ESPTOOL" --chip esp32s3 --port "$DUT_PORT" --no-stub --after "$IDENT_AFTER" flash-id >"$IDENT_LOG" 2>&1; then
        sed 's/^/    /' "$IDENT_LOG" >&2
        die "flash-id failed (the DUT did not answer); see $IDENT_LOG"
    fi
    { $GREP -E "Chip type:|MAC:|Manufacturer|Device:|Detected flash size|Crystal" "$IDENT_LOG" || true; } | sed 's/^/    /'
    #  esptool prints one "MAC:" line for this chip (the C5 prints a 48-bit
    #  "BASE MAC:" and an EUI-64 "MAC:"; the S3 has only the 48-bit one).
    $GREP -qiE "^MAC: *${dut_lc}[[:space:]]*$" "$IDENT_LOG" \
        || die "MAC does not match. Nothing written.
  expected $DUT_MAC (is a different board on this port?)"
    #  Fixed string on the "Chip type:" line. DUT_CHIP is "ESP32-S3", which
    #  the full line ("ESP32-S3 (QFN56) (revision v0.2)") contains; the MAC
    #  gate above is what pins the individual board.
    $GREP -E "^Chip type:" "$IDENT_LOG" | $GREP -qF "$DUT_CHIP" \
        || die "chip type is not \"$DUT_CHIP\". Nothing written."
    $GREP -q "^Detected flash size: $DUT_FLASH" "$IDENT_LOG" \
        || die "detected flash size is not $DUT_FLASH. Nothing written."
    say "gate OK: MAC $DUT_MAC / $DUT_CHIP / $DUT_FLASH"
    if [ "$DRYRUN" = "1" ]; then
        print_write_plan
        say "DRYRUN=1: stopping here (nothing written, nothing captured). The identification ended with --after hard-reset, so the DUT is running whatever the flash holds."
        exit 0
    fi
    if [ "$IDENT_AFTER" = "hard-reset" ]; then
        say "NOFLASH=1 NORESET=1: the identification hard-reset the board; the monitor will open with --no-reset, so the head of this boot is lost during the reconnect"
    fi
fi

#  ---------------------------------------------------------------- 5. write
#  The .sha.txt "written by this run" record is made right after a successful
#  write-flash, before the monitor starts: a capture failure must never leave
#  the board's new contents unrecorded.
mkdir -p "$(dirname "$OUT")"
S3_FILES+=("$SHA_TXT")
{
    echo "# $(date '+%F %T')  DRYRUN=$DRYRUN NOFLASH=$NOFLASH COLD=$COLD NORESET=$NORESET  DUT_MAC=$DUT_MAC"
} > "$SHA_TXT"
if [ "$NOFLASH" != "1" ]; then
    [ -n "$BL_ADDR" ] || die "internal: bootloader address unset right before the write"
    say "==== 2. write (bootloader $BL_ADDR / ptable 0x8000 / boot_app0 0xe000 / app 0x10000) ===="
    print_write_plan
    S3_FILES+=("$FLASH_LOG")
    : > "$FLASH_LOG"
    #  2a. the four images. The chip is in the ROM download mode since the
    #  identification; --before no-reset keeps it there.
    echo "## 2a. write-flash" >> "$FLASH_LOG"
    if ! "${FLASH_CMD[@]}" >>"$FLASH_LOG" 2>&1; then
        tail -30 "$FLASH_LOG" | sed 's/^/    /' >&2
        echo "# write-flash FAILED (rc!=0); flash contents are UNDEFINED (partially written?). See $FLASH_LOG" >> "$SHA_TXT"
        die "write-flash failed; see $FLASH_LOG"
    fi
    N="$($GREP -c "Hash of data verified" "$FLASH_LOG" || true)"
    if [ "${N:-0}" -lt "$N_IMAGES" ]; then
        echo "# write-flash rc=0 but \"Hash of data verified\" seen $N times, expected $N_IMAGES; flash contents are SUSPECT. See $FLASH_LOG" >> "$SHA_TXT"
        die "\"Hash of data verified\" seen $N times, expected $N_IMAGES; see $FLASH_LOG"
    fi
    say "write OK (Hash of data verified x$N)"
    #  2b. read the bootloader's first sector back and require it to equal
    #  what was just written there. The last esptool call, so --after
    #  hard-reset wakes the board (the monitor resets it once more).
    RB_DIR="$(mktemp -d)"; RB="$RB_DIR/sector0.bin"
    echo "## 2b. read-flash $RB_START $RB_LEN (read-back of the bootloader's first sector)" >> "$FLASH_LOG"
    if ! "${READBACK_CMD_PREFIX[@]}" "$RB" >>"$FLASH_LOG" 2>&1; then
        tail -20 "$FLASH_LOG" | sed 's/^/    /' >&2
        echo "# read-flash $RB_START $RB_LEN FAILED (rc!=0) after the write; the bootloader is UNVERIFIED (images written, Hash of data verified x$N). See $FLASH_LOG" >> "$SHA_TXT"
        rm -rf "$RB_DIR"
        die "read-flash (bootloader read-back) failed; see $FLASH_LOG"
    fi
    RB_SIZE="$(stat -c %s "$RB")"
    HEAD16="$(od -An -tx1 -N16 "$RB" | tr -s ' ' | sed 's/^ //')"
    RB_SHA="$(sha256sum "$RB" | cut -c1-64)"
    WANT_SHA="$(head -c "$((RB_LEN))" "$IMG_BL" | sha256sum | cut -c1-64)"
    rm -rf "$RB_DIR"
    if [ "$RB_SIZE" -ne "$((RB_LEN))" ] || [ "$RB_SHA" != "$WANT_SHA" ]; then
        echo "# bootloader read-back FAILED: read $RB_SIZE bytes (expected $((RB_LEN))), sha $RB_SHA, expected $WANT_SHA, first 16: $HEAD16 -- flash $RB_START does not hold the bootloader that was written; images written (Hash of data verified x$N). See $FLASH_LOG" >> "$SHA_TXT"
        die "bootloader read-back: flash $RB_START does not match the image (read sha $RB_SHA, expected $WANT_SHA, size $RB_SIZE, first 16: $HEAD16); see $FLASH_LOG"
    fi
    say "bootloader read-back OK ($RB_START +$RB_LEN equals the image, sha $RB_SHA, first 16: $HEAD16)"
    {
        echo "# read back by this run: $RB_START +$RB_LEN equals the first $((RB_LEN)) bytes of the bootloader image (sha $RB_SHA, first 16: $HEAD16)"
        echo "# written by this run (address  sha256  size  file), Hash of data verified x$N:"
        for ((i = 0; i < ${#WRITE_ARGS[@]}; i += 2)); do
            printf '%-8s %s  %8d  %s\n' "${WRITE_ARGS[$i]}" "$(sha256sum "${WRITE_ARGS[$((i+1))]}" | cut -c1-64)" \
                "$(stat -c %s "${WRITE_ARGS[$((i+1))]}")" "${WRITE_ARGS[$((i+1))]}"
        done
        [ "$BOOT_APP0" = "none" ] && echo "# WARNING: BOOT_APP0=none -- 0xe000 was NOT written (recorded deviation from the stage 2 plan)"
    } >> "$SHA_TXT"
    say "written images recorded -> $SHA_TXT"
else
    {
        echo "# NOT written by this run (NOFLASH=$NOFLASH COLD=$COLD). Flash holds whatever the previous writing run wrote."
        if [ -n "$IMG_APP" ] && [ -f "$IMG_APP" ]; then
            echo "# app image on disk at capture time (informational only):"
            sha256sum "$IMG_APP"
        fi
    } >> "$SHA_TXT"
fi

#  ---------------------------------------------------------------- 6. capture
say "==== 3. capture (up to ${CAPTURE_SEC}s${MARKERS:+, early stop on MARKERS}) ===="
if [ "$COLD" = "1" ]; then
    #  Record the by-id timeline: the node must go away (power off) and come
    #  back (power on) within COLD_WAIT_SEC. The power cycle is external.
    S3_FILES+=("$COLD_TXT")
    _ts() { date '+%F %T.%N' | cut -c1-23; }
    {
        echo "# COLD=1 by-id timeline for $DUT_PORT"
        if [ -e "$DUT_PORT" ]; then
            echo "$(_ts)  present at start; waiting for it to disappear (power off now, up to ${COLD_WAIT_SEC}s)"
        else
            echo "$(_ts)  absent at start; waiting for it to appear (power on, up to ${COLD_WAIT_SEC}s)"
        fi
    } > "$COLD_TXT"
    say "COLD=1: waiting for $DUT_PORT to disappear and reappear (power-cycle the DUT now; up to ${COLD_WAIT_SEC}s)"
    COLD_T0="$(date +%s)"
    _deadline=$(( COLD_T0 + COLD_WAIT_SEC ))
    if [ -e "$DUT_PORT" ]; then
        while [ -e "$DUT_PORT" ]; do
            [ "$(date +%s)" -lt "$_deadline" ] || die "COLD: by-id did not disappear within ${COLD_WAIT_SEC}s (no power cycle observed); nothing captured"
            sleep 0.1
        done
        echo "$(_ts)  disappeared" >> "$COLD_TXT"
    fi
    while [ ! -e "$DUT_PORT" ]; do
        [ "$(date +%s)" -lt "$_deadline" ] || die "COLD: by-id did not reappear within ${COLD_WAIT_SEC}s; nothing captured"
        sleep 0.1
    done
    echo "$(_ts)  appeared; opening with --no-reset" >> "$COLD_TXT"
    say "COLD=1: by-id timeline -> $COLD_TXT"
fi
[ -e "$DUT_PORT" ] || die "DUT by-id node is absent: $DUT_PORT"

_mon_opt=()
[ "$NORESET" = "1" ] && _mon_opt+=(--no-reset)
: > "$OUT"
S3_FILES+=("$OUT")
( script -qfc "$IDF_PYTHON -m esp_idf_monitor ${_mon_opt[*]:-} --port $DUT_PORT" /dev/null ) > "$OUT" 2>&1 &
SPID=$!
for ((i = 0; i < CAPTURE_SEC; i++)); do
    sleep 1
    if [ -n "$MARKERS" ] && $GREP -qaE "$MARKERS" "$OUT" 2>/dev/null; then break; fi
    kill -0 "$SPID" 2>/dev/null || break
done
kill "$SPID" 2>/dev/null || true
#  Only the monitor that holds THIS port; never every monitor on the host.
pkill -f "esp_idf_monitor.*${DUT_PORT}" 2>/dev/null || true
sleep 1

#  Strip ANSI colour so the counts below see plain text.
sed -i 's/\x1b\[[0-9;]*m//g' "$OUT" 2>/dev/null || true
say "captured $(wc -l < "$OUT") lines -> $OUT"

#  COLD=1: the kernel journal's USB lines for the window, so the power-cycle
#  evidence (disconnect, one enumeration, no re-enumeration during the
#  capture) is next to the log instead of in a journal that rotates. The
#  window opens JOURNAL_LEAD_SEC before the by-id wait began, not at it
#  (stage 4 review M-7): the operator pulls the power at about the moment
#  the script says to, and a disconnect a second or two BEFORE COLD_T0 was
#  outside the old window, which then showed the enumeration and not the
#  power-off it is meant to prove. Passes through the redact stage like
#  every sidecar (the DUT's own serial spelling is kept, peers masked).
JOURNAL_LEAD_SEC=30
if [ "$COLD" = "1" ]; then
    S3_FILES+=("$JOURNAL_TXT")
    _journal_since=$(( COLD_T0 - JOURNAL_LEAD_SEC ))
    {
        echo "# journalctl -k --since @$_journal_since (USB lines only) for $DUT_PORT; window start = the by-id wait (@$COLD_T0) - ${JOURNAL_LEAD_SEC}s"
        if ! journalctl -k --since "@$_journal_since" --no-pager -o short-precise 2>&1 | $GREP -iE 'usb|cdc_acm|ttyACM'; then
            echo "# (no USB lines, or journalctl not readable by this user -- the by-id timeline in .cold.txt is the only evidence)"
        fi
    } > "$JOURNAL_TXT"
    say "COLD=1: kernel journal USB lines -> $JOURNAL_TXT"
fi

#  ---------------------------------------------------------------- 7. markers
MARKER_LINES="$(s3_count_markers "$OUT")"
printf '%s\n' "$MARKER_LINES" | sed 's/^/[S3] /'
printf '%s\n' "$MARKER_LINES" >> "$SHA_TXT"
#  EXTRA_MARKERS (fixed strings; nothing when unset).
EXTRA_LINE="$(s3_count_extra "$OUT")"
if [ -n "$EXTRA_LINE" ]; then
    say "$EXTRA_LINE"
    printf '%s\n' "$EXTRA_LINE" >> "$SHA_TXT"
fi
say "record -> $SHA_TXT"

#  ---------------------------------------------------------------- 8. jtag liveness probe
#  After the monitor is gone (the CDC-ACM interface is free). Evidence only:
#  the exit status is not changed by anything the probe finds.
HEARTBEAT="$(printf '%s\n' "$MARKER_LINES" | sed -n 1p | $GREP -oE 'heartbeat=[0-9]+' | cut -d= -f2 || true)"
HEARTBEAT="${HEARTBEAT:-0}"
if [ "$(s3_jtag_wanted "$COLD" "$HEARTBEAT" "$S3_JTAG_FORCE" "$DRYRUN" "$S3_JTAG_ON_SILENT")" = "yes" ]; then
    _reason=silent-cold
    [ "$S3_JTAG_FORCE" = "1" ] && _reason=forced
    s3_jtag_probe "$_reason"
else
    say "jtag: probe not run (COLD=$COLD heartbeat=$HEARTBEAT S3_JTAG_FORCE=$S3_JTAG_FORCE S3_JTAG_ON_SILENT=$S3_JTAG_ON_SILENT); it runs on a silent cold run or with S3_JTAG_FORCE=1"
fi
exit 0
