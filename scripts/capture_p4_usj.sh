#!/bin/bash
#
#  scripts/capture_p4_usj.sh -- gated flash-and-capture for M5Stamp-P4 (ESP32-P4)
#  =============================================================================
#
#  Development-side script (not distributed). Writes a sketch built for the
#  toppers:esp32:m5stampp4_fmp3 board to the M5Stamp-P4 over its USB
#  Serial/JTAG port, captures the console, and counts the lines stage A4 of
#  the StampP4 plan looks for. The board this was written for is on ANOTHER
#  machine (dev .steering/20260917-stampp4-arduino-plan/PLAN.md P12), so
#  unlike scripts/capture_c5_usj.sh this one has no default DUT: DUT_MAC is
#  required, and the script writes nothing until esptool has read that MAC
#  back from the port it is about to write to. What it needs on the host:
#  bash, the M5Stack core 3.3.8 in the Arduino data directory (its esptool
#  binary), stty and cat (the capture is a plain read of the tty; no
#  pyserial, no esp_idf_monitor), and a sketch built with arduino-cli
#  (--output-dir <dir> gives the three images).
#
#  Why so much smaller than capture_c5_usj.sh: the C5 script's redact stage,
#  JTAG liveness probe and PCR decoding were written for Wi-Fi runs and for
#  a board on the same desk. The P4 minimal stage has no Wi-Fi and no
#  credentials, and a JTAG probe cannot be tuned for a board that is not
#  here. What is kept is every gate that decides whether flash is written
#  at all, the bootloader-offset check against boards.txt, the readback of
#  the bootloader region, and a marker counter with a selftest.
#
#  Usage (on the machine with the board):
#
#    arduino-cli compile --fqbn toppers:esp32:m5stampp4_fmp3:FMP3Runtime=minimal \
#        --output-dir /tmp/p4-blink <path>/examples/Blink
#    DUT_MAC=30:ed:a0:xx:xx:xx OUT_DIR=/tmp/p4-blink bash scripts/capture_p4_usj.sh
#
#  Environment:
#    DUT_MAC       REQUIRED. The board's MAC as esptool prints it (lower or
#                  upper case). Read it once with
#                    esptool --chip esp32p4 --port <port> --no-stub flash-id
#                  and paste it; the script refuses to guess.
#    DUT_PORT      the serial port (default: the by-id node the P4's built-in
#                  USB Serial/JTAG creates from DUT_MAC, which is what the
#                  dev repository used for the M5Stamp ESP32P4).
#    OUT_DIR       arduino-cli --output-dir of the build to flash (REQUIRED
#                  unless NOFLASH=1): <sketch>.ino.bin, .bootloader.bin,
#                  .partitions.bin are taken from there.
#    LOG_DIR       where the capture goes (default build/p4-capture).
#    CAPTURE_SEC   seconds to capture (default 30).
#    RESET_MODE    esptool --after mode used to start the board before the
#                  capture: hard-reset (default) or watchdog-reset (esptool 5 spellings). The dev
#                  repository found the Tab5 (another P4 board) drops into
#                  download mode after an RTS hard reset and stays silent;
#                  the M5Stamp ESP32P4 did not. If the capture is empty and
#                  the ident step worked, try RESET_MODE=watchdog-reset.
#    DRYRUN=1      identify the board and print the write plan; write nothing.
#    NOFLASH=1     skip the write; reset and capture what is on the board.
#    SELFTEST=1    run the marker-counter and boards.txt-gate selftests and
#                  exit (no board, no esptool needed).
#
#  Expected console lines (StampP4 plan, stage A4, examples/Blink):
#    "[Arduino] loop heartbeat N"  once a second from the sketch task (PRC1)
#    "[P4-CORE2] alive N"          once a second from the PRC2 task
#                                   (app/phase3_p4): core1 runs the kernel
#    "Processor 2 start."           the kernel's own line when PRC2 joins
#  and, before any of them, the raw 'S' (seam entry, core0) and 'C' (core1
#  entry) bytes the seam writes to the USB Serial/JTAG FIFO.
#
set -u

GREP=/usr/bin/grep
say() { echo "[P4] $*"; }
die() { echo "" >&2; echo "ABORT: $*" >&2; exit 1; }

#  ---------------------------------------------------------------- pure helpers
#  Marker counts over a captured log (pure; selftested). One line of
#  key=value pairs.
p4_count_markers() {   # file
    local f="$1"
    local hb alive p2 banner setup unexpected
    hb="$($GREP -acE 'loop heartbeat [0-9]+' "$f" || true)"
    alive="$($GREP -acE '\[P4-CORE2\] alive [0-9]+' "$f" || true)"
    p2="$($GREP -acE 'Processor 2 start\.' "$f" || true)"
    banner="$($GREP -acE 'TOPPERS/FMP3 Kernel Release' "$f" || true)"
    setup="$($GREP -acE '\[Arduino\] setup\(\)' "$f" || true)"
    unexpected="$($GREP -acE '## (Unexpected|Assertion|Internal)|Guru Meditation|abort\(\)' "$f" || true)"
    echo "heartbeat=${hb:-0} core2_alive=${alive:-0} prc2_start=${p2:-0} banner=${banner:-0} setup=${setup:-0} unexpected=${unexpected:-0}"
}

#  The bootloader offset the board's boards.txt line states. The ESP32-P4
#  ROM loads the 2nd-stage bootloader from 0x2000 (like the C5; the C6's
#  0x0 and the Xtensa 0x1000 would brick nothing but boot nothing). Read
#  from the installed platform, never a literal here; refused unless it is
#  exactly one line saying 0x2000 (pure over a file; selftested).
p4_bootloader_addr() {   # boards.txt -> prints the address or a reason on stderr with rc 1
    local f="$1" key='m5stampp4_fmp3.build.bootloader_addr' lines v
    [ -f "$f" ] || { echo "boards.txt not found: $f" >&2; return 1; }
    lines="$($GREP -aE "^m5stampp4_fmp3\.build\.bootloader_addr=" "$f" | tr -d '\r' || true)"
    [ -n "$lines" ] || { echo "key $key is missing from $f" >&2; return 1; }
    [ "$(printf '%s\n' "$lines" | wc -l)" -eq 1 ] || { echo "key $key appears more than once in $f" >&2; return 1; }
    v="${lines#*=}"
    [ "$v" = "0x2000" ] || { echo "key $key is '$v' in $f, expected 0x2000 (the ESP32-P4 bootloader offset); refusing" >&2; return 1; }
    echo "$v"
}

#  ---------------------------------------------------------------- selftest
if [ "${SELFTEST:-0}" = "1" ]; then
    _sd="$(mktemp -d)"
    _fail() { echo "selftest FAIL: $*" >&2; rm -rf "$_sd"; exit 1; }
    #  (1) marker counter over a fixture with known counts
    printf '%s\n' 'S' 'C' 'TOPPERS/FMP3 Kernel Release 3.4.0 for M5Stamp ESP32P4 <HP RV32IMAFC, RISC-V> (Sep 17 2026, 00:00:00)' \
        'Processor 2 start.' '[Arduino] setup() called' '[Arduino] loop heartbeat 1' '[P4-CORE2] alive 1' \
        '[Arduino] loop heartbeat 2' '[P4-CORE2] alive 2' 'noise heartbeat' '[P4-CORE2] alive x' > "$_sd/a.log"
    _m="$(p4_count_markers "$_sd/a.log")"
    [ "$_m" = "heartbeat=2 core2_alive=2 prc2_start=1 banner=1 setup=1 unexpected=0" ] || _fail "(1) markers: $_m"
    #  (2) the failure detectors
    printf '%s\n' '## Unexpected exception' 'Guru Meditation Error' '[Arduino] loop heartbeat 9' > "$_sd/b.log"
    _m="$(p4_count_markers "$_sd/b.log")"
    [ "$_m" = "heartbeat=1 core2_alive=0 prc2_start=0 banner=0 setup=0 unexpected=2" ] || _fail "(2) unexpected: $_m"
    #  (3) an empty capture counts zero everywhere (silence is not success)
    : > "$_sd/c.log"
    _m="$(p4_count_markers "$_sd/c.log")"
    [ "$_m" = "heartbeat=0 core2_alive=0 prc2_start=0 banner=0 setup=0 unexpected=0" ] || _fail "(3) empty: $_m"
    #  (4) boards.txt gate: accepted / refused shapes
    printf 'x.build.bootloader_addr=0x1000\nm5stampp4_fmp3.build.bootloader_addr=0x2000\n' > "$_sd/ok.txt"
    [ "$(p4_bootloader_addr "$_sd/ok.txt" 2>/dev/null)" = "0x2000" ] || _fail "(4) 0x2000 not accepted"
    printf 'm5stampp4_fmp3.build.bootloader_addr=0x2000\r\n' > "$_sd/crlf.txt"
    [ "$(p4_bootloader_addr "$_sd/crlf.txt" 2>/dev/null)" = "0x2000" ] || _fail "(4) CRLF 0x2000 not accepted"
    printf 'm5stampp4_fmp3.build.bootloader_addr=0x0\n' > "$_sd/bad1.txt"
    p4_bootloader_addr "$_sd/bad1.txt" >/dev/null 2>&1 && _fail "(4) 0x0 accepted"
    printf 'm5stampp4_fmp3.build.bootloader_addr=0x1000\n' > "$_sd/bad2.txt"
    p4_bootloader_addr "$_sd/bad2.txt" >/dev/null 2>&1 && _fail "(4) 0x1000 accepted"
    printf 'm5stampp4Xfmp3.build.bootloader_addr=0x2000\n' > "$_sd/bad3.txt"
    p4_bootloader_addr "$_sd/bad3.txt" >/dev/null 2>&1 && _fail "(4) a key with a different character accepted (dots as any-char)"
    printf 'm5stampp4_fmp3.build.bootloader_addr=0x2000\nm5stampp4_fmp3.build.bootloader_addr=0x2000\n' > "$_sd/bad4.txt"
    p4_bootloader_addr "$_sd/bad4.txt" >/dev/null 2>&1 && _fail "(4) duplicate key accepted"
    p4_bootloader_addr "$_sd/missing.txt" >/dev/null 2>&1 && _fail "(4) missing file accepted"
    rm -rf "$_sd"
    echo "p4 capture selftest PASS: (1) markers 2/2/1/1/1/0 (2) unexpected 2 (3) empty -> zeros (4) bootloader gate: 0x2000 LF/CRLF accepted; 0x0, 0x1000, other key, duplicate, missing refused"
    exit 0
fi

#  ---------------------------------------------------------------- DUT identity
#  No default: the board is on another machine (see the header). The
#  FORBIDDEN list is the development machine's other boards (the same list
#  as capture_c5_usj.sh plus the M5Stamp-C5 and the AtomS3 Lite); a DUT_MAC
#  in it stops the script before it touches anything.
DUT_MAC="${DUT_MAC:-}"
[ -n "$DUT_MAC" ] || die "DUT_MAC is required (the board's MAC as esptool prints it). Read it with: esptool --chip esp32p4 --port <port> --no-stub flash-id"
dut_lc="$(printf '%s' "$DUT_MAC" | tr 'A-Z' 'a-z')"
dut_uc="$(printf '%s' "$DUT_MAC" | tr 'a-z' 'A-Z')"
printf '%s' "$dut_lc" | $GREP -qE '^([0-9a-f]{2}:){5}[0-9a-f]{2}$' || die "DUT_MAC is not a MAC address: $DUT_MAC"
FORBIDDEN="60:55:f9:57:c2:60 d0:cf:13:f0:a7:44 d0:cf:13:f0:c8:94 30:76:f5:ed:86:a8
14:c1:9f:e0:61:b0 30:ed:a0:f3:f1:64 44:1b:f6:e2:73:84 78:21:84:a6:5c:64 f4:12:fa:5b:4a:58
9c:13:9e:d3:62:18 3c:dc:75:8d:ed:20 34:b7:da:5e:95:1c"
for m in $FORBIDDEN; do
    [ "$dut_lc" = "$m" ] && die "DUT_MAC is a board on the FORBIDDEN list: $DUT_MAC"
done
DUT_PORT="${DUT_PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${dut_uc}-if00}"

#  ---------------------------------------------------------------- modes and paths
DRYRUN="${DRYRUN:-0}"; NOFLASH="${NOFLASH:-0}"
CAPTURE_SEC="${CAPTURE_SEC:-30}"
RESET_MODE="${RESET_MODE:-hard-reset}"
case "$RESET_MODE" in hard-reset|watchdog-reset) ;; *) die "RESET_MODE must be hard-reset or watchdog-reset (got '$RESET_MODE')" ;; esac
LOG_DIR="${LOG_DIR:-$(cd "$(dirname "$0")/.." && pwd -P)/build/p4-capture}"
mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$LOG_DIR/p4-capture-$STAMP.log"
IDENT_LOG="$LOG_DIR/p4-capture-$STAMP.ident.log"
FLASH_LOG="$LOG_DIR/p4-capture-$STAMP.flash.log"
READBACK_BIN="$LOG_DIR/p4-capture-$STAMP.readback.bin"

ARDUINO_DATA="${ARDUINO_DIRECTORIES_DATA:-$HOME/.arduino15}"
_newest_dir() { ls -d "$1"/*/ 2>/dev/null | sort -V | tail -1; }
ESPTOOL="${ESPTOOL:-}"
if [ -z "$ESPTOOL" ]; then
    _et="$(_newest_dir "$ARDUINO_DATA/packages/m5stack/tools/esptool_py")"
    [ -n "$_et" ] && ESPTOOL="$(find "$_et" -maxdepth 2 -type f -name 'esptool' | head -1)"
fi
[ -n "$ESPTOOL" ] && [ -x "$ESPTOOL" ] || die "esptool was not found under $ARDUINO_DATA/packages/m5stack/tools/esptool_py (set ESPTOOL=<path>)"
BOARDS_TXT="${BOARDS_TXT:-$HOME/Arduino/hardware/toppers/esp32/boards.txt}"
[ -f "$BOARDS_TXT" ] || BOARDS_TXT="$(ls "$ARDUINO_DATA"/packages/toppers/hardware/esp32/*/boards.txt 2>/dev/null | sort -V | tail -1)"
BL_ADDR="$(p4_bootloader_addr "$BOARDS_TXT")" || die "bootloader address gate failed (boards.txt: $BOARDS_TXT)"

#  ---------------------------------------------------------------- images
IMG_APP=""; IMG_BL=""; IMG_PT=""
if [ "$NOFLASH" != "1" ]; then
    OUT_DIR="${OUT_DIR:-}"
    [ -n "$OUT_DIR" ] && [ -d "$OUT_DIR" ] || die "OUT_DIR (arduino-cli --output-dir of the build) is required unless NOFLASH=1"
    _pick() {   # suffix -> exactly one file
        local hits; hits="$(ls "$OUT_DIR"/*"$1" 2>/dev/null || true)"
        [ "$(printf '%s\n' "$hits" | $GREP -c . )" -eq 1 ] || die "exactly one *$1 expected in $OUT_DIR, found: ${hits:-none}"
        printf '%s' "$hits"
    }
    IMG_BL="$(_pick .ino.bootloader.bin)"; IMG_PT="$(_pick .ino.partitions.bin)"
    IMG_APP="$(ls "$OUT_DIR"/*.ino.bin 2>/dev/null | $GREP -vE '\.(bootloader|partitions|merged)\.bin$' | head -1)"
    [ -n "$IMG_APP" ] && [ -f "$IMG_APP" ] || die "no <sketch>.ino.bin in $OUT_DIR"
fi

#  ---------------------------------------------------------------- 1. identify (read-only)
say "==== 1. identify (read-only) ===="
say "  DUT_MAC=$DUT_MAC  port=$DUT_PORT  esptool=$ESPTOOL"
[ -e "$DUT_PORT" ] || die "DUT port is absent: $DUT_PORT (DUT_PORT=... to override; is the board plugged in?)"
if ! "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --no-stub --after no-reset flash-id > "$IDENT_LOG" 2>&1; then
    sed 's/^/    /' "$IDENT_LOG" >&2; die "flash-id failed (the board did not answer); nothing written"
fi
$GREP -iE 'Chip (is|type)|MAC:|Manufacturer|Device:|Detected flash size|Crystal|revision' "$IDENT_LOG" | sed 's/^/    /'
$GREP -qi "MAC: *$dut_lc" "$IDENT_LOG" || $GREP -qi "MAC: *$dut_uc" "$IDENT_LOG" \
    || die "MAC mismatch: expected $DUT_MAC (another board on this port?); nothing written"
$GREP -q "ESP32-P4" "$IDENT_LOG" || die "the chip is not an ESP32-P4; nothing written"
$GREP -qiE 'Detected flash size: *16MB' "$IDENT_LOG" || say "WARNING: flash size is not 16MB as boards.txt assumes (see ident log)"
say "identity OK: $DUT_MAC / ESP32-P4"

if [ "$NOFLASH" != "1" ]; then
    say "==== write plan: $BL_ADDR $(basename "$IMG_BL") / 0x8000 $(basename "$IMG_PT") / 0x10000 $(basename "$IMG_APP") (qio 80m 16MB) ===="
    sha256sum "$IMG_BL" "$IMG_PT" "$IMG_APP" | sed 's/^/    /'
fi
if [ "$DRYRUN" = "1" ]; then say "DRYRUN=1: stopping here; nothing written"; exit 0; fi

#  ---------------------------------------------------------------- 2. write + readback
if [ "$NOFLASH" != "1" ]; then
    say "==== 2. write ===="
    if ! "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --baud 921600 --before default-reset --after no-reset \
            write-flash --flash-mode qio --flash-freq 80m --flash-size 16MB \
            "$BL_ADDR" "$IMG_BL" 0x8000 "$IMG_PT" 0x10000 "$IMG_APP" > "$FLASH_LOG" 2>&1; then
        tail -30 "$FLASH_LOG" | sed 's/^/    /' >&2; die "write-flash failed ($FLASH_LOG)"
    fi
    N="$($GREP -c 'Hash of data verified' "$FLASH_LOG" || true)"
    [ "${N:-0}" -ge 3 ] || die "'Hash of data verified' seen $N time(s), expected 3 ($FLASH_LOG)"
    #  Read the bootloader region back and compare with what was written:
    #  the direct evidence that the 2nd-stage bootloader is where the ROM
    #  looks for it.
    BL_LEN="$(stat -c %s "$IMG_BL")"
    "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --baud 921600 --before default-reset --after no-reset \
        read-flash "$BL_ADDR" "$BL_LEN" "$READBACK_BIN" >> "$FLASH_LOG" 2>&1 || die "read-flash of the bootloader region failed ($FLASH_LOG)"
    cmp -s "$IMG_BL" "$READBACK_BIN" || die "bootloader readback at $BL_ADDR differs from $IMG_BL"
    say "write OK (Hash of data verified x$N; bootloader readback at $BL_ADDR identical)"
fi

#  ---------------------------------------------------------------- 3. reset + capture
say "==== 3. reset ($RESET_MODE) and capture ${CAPTURE_SEC}s ===="
#  Open the tty first so the seam's first bytes ('S', 'C') are not lost,
#  then reset through esptool. The USB Serial/JTAG ignores the baud rate.
stty -F "$DUT_PORT" 115200 raw -echo -echoe -echok 2>/dev/null || die "stty on $DUT_PORT failed (permissions? dialout group?)"
: > "$OUT"
( timeout "$CAPTURE_SEC" cat "$DUT_PORT" > "$OUT" 2>/dev/null ) &
CATPID=$!
sleep 0.3
"$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --before default-reset --after "$RESET_MODE" chip-id >> "$FLASH_LOG" 2>&1 \
    || say "WARNING: the reset command returned non-zero (see $FLASH_LOG); capturing anyway"
wait "$CATPID" 2>/dev/null || true
#  Strip ANSI colour and NUL so the counts see plain text.
sed -i 's/\x1b\[[0-9;]*m//g; s/\x00//g' "$OUT" 2>/dev/null || true
say "captured $(wc -l < "$OUT") lines -> $OUT"

#  ---------------------------------------------------------------- 4. markers
MARKER_LINE="$(p4_count_markers "$OUT")"
say "$MARKER_LINE"
FIRST="$(head -c 8 "$OUT" | od -An -c | tr -s ' ' | head -1)"
say "first bytes:$FIRST   (expect 'S' from the seam entry on core0 and 'C' from core1)"
if $GREP -qE 'unexpected=0' <<< "$MARKER_LINE" && $GREP -qE 'heartbeat=[1-9]' <<< "$MARKER_LINE" \
        && $GREP -qE 'core2_alive=[1-9]' <<< "$MARKER_LINE"; then
    say "VERDICT PASS (heartbeat and core2_alive both counted, no failure marker)"
    exit 0
fi
say "VERDICT FAIL (see $OUT; if it is empty and step 1 passed, try RESET_MODE=watchdog-reset)"
exit 2
