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
#  Why smaller than capture_c5_usj.sh: that script's JTAG liveness probe and
#  PCR decoding were written for a board on the same desk, and a JTAG probe
#  cannot be tuned for a board that is not here. What IS here is every gate
#  that decides whether flash is written at all, the bootloader-offset check
#  against boards.txt, the readback of the bootloader region, a marker
#  counter with a selftest, and - since StampP4 plan stage B put hosted
#  Wi-Fi on this board - the same redact/mask layer as the S3 and C5
#  scripts (credential needles, peer MAC, IPv4, the scan lines' SSID
#  column), fail-closed: a file the checker cannot prove clean is renamed
#  *.UNREDACTED and the run exits non-zero.
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
#    BOOT_APP0     the otadata image written at 0xe000 (default: the
#                  installed platform's tools/partitions/boot_app0.bin, the
#                  same file its upload recipe writes). `none` skips it,
#                  which leaves whatever the flash already holds deciding
#                  which app slot boots - see the note at the images step.
#    LOG_DIR       where the capture goes (default build/p4-capture).
#    CAPTURE_SEC   seconds to capture (default 30).
#    RESET_MODE    esptool --after mode used to start the board before the
#                  capture: watchdog-reset (default) or hard-reset (esptool 5
#                  spellings). The default is NOT the obvious one, and the
#                  reason is the console: this board's console is its
#                  built-in USB Serial/JTAG, and a hard reset takes that USB
#                  device down and brings it back, so the whole early boot -
#                  ROM banner, the bootloader's load lines, the seam's 'S'
#                  and core1's 'C', the kernel banner, "Processor 2 start."
#                  - is printed while no host is attached and is simply
#                  gone. A watchdog reset does not drop USB, and the same
#                  run then captures all of it (measured 2026-09-18:
#                  hard-reset gave banner=0 prc2_start=0, watchdog-reset
#                  gave banner=1 prc2_start=1 on the same image).
#    DRYRUN=1      identify the board and print the write plan; write nothing.
#    NOFLASH=1     skip the write; reset and capture what is on the board.
#    NORESET=1     skip the write AND the reset; only read. The true-cold
#                  shape: cut the board's power yourself (uhubctl on its own
#                  port), bring it back, then run this. Implies NOFLASH=1.
#    EXTRA_MARKERS `|`-separated FIXED strings (grep -F, not regex; default
#                  empty), counted in the capture and printed as
#                  "extra: <string>=<n> ...". Brackets are literal, e.g.
#                  EXTRA_MARKERS='[GPIO-INTR] VERDICT PASS|[WiFiHosted] companion ready'
#    WIFI_CREDS    credentials shell file the redact needles are read from
#                  (default the development repository's
#                  esp/boot/wifi_credentials.sh). Only counts are printed,
#                  never values; with no such file the mask still runs
#                  (peer MAC, IPv4, SSID column).
#    P4_REDACT_ONLY=1  mask the files named as arguments and exit. Touches no
#                  hardware and needs no DUT_MAC. For logs captured before a
#                  mask rule existed. rc 93 (and *.UNREDACTED) on residue.
#    P4_REDACT_ANYWHERE=1  let P4_REDACT_ONLY touch a file outside LOG_DIR.
#                  A git-tracked file is refused either way: a masked source
#                  is still source.
#    SELFTEST=1    run the marker-counter, redact/mask, extra-marker and
#                  boards.txt-gate selftests and exit (no board, no esptool
#                  needed).
#
#  Expected console lines of the hosted Wi-Fi runs (StampP4 plan, stage B4):
#    "[WiFiHosted] companion INIT chip_id=0x0d caps=0x0d"  the add-on C6
#                                   answered the SDIO handshake (B-10: the
#                                   first line that separates "the add-on is
#                                   not there" from everything else)
#    "[WiFiHosted] companion ready (STA)"                  bring-up done
#    "[WiFiScan] AP[i] ... SSID=<SSID-redacted>"           masked by this script
#    "[WiFiConnect] connected and DHCP completed"
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
    #  The strings are the ones the code actually prints, checked against
    #  the sources rather than guessed: ArduinoSketchBridge.cpp writes
    #  "[Arduino] loop heartbeat" with NO number and "[Arduino] setup
    #  complete" (the first version of this script looked for
    #  "loop heartbeat <N>" and "setup()" and counted zero on a board that
    #  was running perfectly - measured 2026-09-18).
    hb="$($GREP -acE '\[A?rduino\] loop heartbeat' "$f" || true)"
    #  Anchored on the TAIL of the tag, and the first letter of the other
    #  tags is optional, because this console LOSES THE START OF LINES. It
    #  is usually one character - "[P4-CORE2] alive 2" arrives as
    #  "[4-CORE2] alive 2", "[WiFiScan] found 14 APs" as "[iFiScan] found
    #  14 APs" - but not always: "[WiFiHosted] companion ready (STA)"
    #  arrived once as "on ready (STA)", 13 characters gone (all measured
    #  on this board 2026-09-18; the same F-1 defect the S3 and LX6 scripts
    #  record, but here it is the rule rather than the exception - 52 of 60
    #  alive lines in one capture). Counting the strict spelling would
    #  report a working board as broken. core2_full below counts the WHOLE
    #  spelling, so the loss stays measured instead of papered over.
    alive="$($GREP -acE 'CORE2\] alive [0-9]+' "$f" || true)"
    alive_full="$($GREP -acE '\[P4-CORE2\] alive [0-9]+' "$f" || true)"
    p2="$($GREP -acE 'P?rocessor 2 start\.' "$f" || true)"
    banner="$($GREP -acE 'T?OPPERS/FMP3 Kernel Release' "$f" || true)"
    setup="$($GREP -acE '\[A?rduino\] setup complete' "$f" || true)"
    unexpected="$($GREP -acE '## (Unexpected|Assertion|Internal)|Guru Meditation|abort\(\)' "$f" || true)"
    #  Every ROM banner after the first is a reset: the board rebooted
    #  during the capture. A boot loop otherwise reads as "it printed
    #  something", which is how the watchdog defect first looked.
    reboot="$($GREP -acE '^ESP-ROM:' "$f" || true)"
    echo "heartbeat=${hb:-0} core2_alive=${alive:-0} core2_full=${alive_full:-0} prc2_start=${p2:-0} banner=${banner:-0} setup=${setup:-0} unexpected=${unexpected:-0} romboot=${reboot:-0}"
}

#  Hosted Wi-Fi markers (stage B). One line of key=value pairs, counts only;
#  verdicts are the caller's. The bring-up line is what StampP4 plan B-10
#  asks for first: the companion's own chip_id, read from the INIT event, is
#  the one-line answer to "is the add-on there and talking".
#    companion  the "[WiFiHosted] companion INIT chip_id=.." line
#    ready      "[WiFiHosted] companion ready (STA)"
#    hosted_err any [WiFiHosted] warning (bring-up or a step failing)
#    scan       the last "found N APs" value (-1 = the line never appeared)
#    scanap     scan result lines
#    ssidraw    scan lines whose SSID column is NOT a placeholder; must be 0
#               (the transform masks them; a file that still has one is
#               quarantined by the residue check, not stored)
#    connected/dhcpdone/dnsok/dnsfail/tcp  the WiFiConnect sketch's lines
#    disc       the sketch's failure lines (disconnect / timeout / TCP failed)
p4_count_wifi() {   # file
    local f="$1"
    _wcnt() { $GREP -acE "$1" "$f" || true; }
    local n_comp n_ready n_err n_scan n_scanap n_ssidraw n_conn n_dhcpdone n_dnsok n_dnsfail n_tcp n_disc
    n_comp="$(_wcnt '\[W?iFiHosted\] companion INIT')"
    #  Tail only: this line is measured lost in bulk, not by one
    #  character (see p4_count_markers' note).
    n_ready="$(_wcnt 'ready \(STA\)')"
    n_err="$(_wcnt '\[W?iFiHosted\] (companion bring-up failed|no INIT event|slave_config failed|wifi_init failed|set_mode|wifi_start failed|scan_[a-z_]* failed|set_sta_config failed|connect request refused)')"
    n_scan="$($GREP -aoE '\[W?iFiScan\] found [0-9]+ APs' "$f" | tail -1 | $GREP -oE '[0-9]+' || true)"
    n_scan="${n_scan:--1}"
    n_scanap="$(_wcnt '\[W?iFiScan\] AP\[[0-9]+\]')"
    n_ssidraw="$({ $GREP -aE 'SSID=' "$f" || true; } | { $GREP -avcE 'SSID=(<SSID-[0-9]+>|<SSID-redacted>)' || true; })"
    n_conn="$(_wcnt '\[W?iFiConnect\] connected and DHCP completed')"
    n_dhcpdone="$(_wcnt '\[W?iFiConnect\] DHCP completed|connected and DHCP completed')"
    #  The strings are the sketch's, checked against examples/WiFiConnect
    #  rather than guessed: it prints "DNS completed" / "DNS failed" and
    #  "TCP request completed" / "TCP request failed". The runtime's own
    #  "[WiFiConnect] TCP received=N" is counted separately as tcp.
    n_dnsok="$(_wcnt '\[W?iFiConnect\] DNS completed')"
    n_dnsfail="$(_wcnt '\[W?iFiConnect\] DNS failed')"
    n_tcp="$(_wcnt '\[W?iFiConnect\] TCP received=')"
    n_disc="$(_wcnt '\[W?iFiConnect\] (disconnected reason=|connection timeout|TCP request failed)')"
    echo "wifi: companion=$n_comp ready=$n_ready hosted_err=$n_err scan=$n_scan scanap=$n_scanap ssidraw=$n_ssidraw connected=$n_conn dhcpdone=$n_dhcpdone dnsok=$n_dnsok dnsfail=$n_dnsfail tcp=$n_tcp disc=$n_disc"
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
#  P4_MASK_SELFTEST=1. The checker's silence is never taken as success: if a
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
p4_load_needles() {
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
p4_redact_transform() {
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
    #  The pattern deliberately matches the TAIL of the tag and does NOT
    #  require the space before SSID=: this console drops characters (the
    #  F-1 defect), and a line that arrived as "...ch=10SSID=NeighbourNet"
    #  once slipped past a stricter pattern and was quarantined by the
    #  residue check below (measured on the ATOM Lite, 2026-09-17).
    local lc eui lcU euiU
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    lcU="$(printf '%s' "$lc" | tr 'a-z' 'A-Z')"; euiU="$(printf '%s' "$eui" | tr 'a-z' 'A-Z')"
    sed -i -E \
        -e "s/${eui:-__none__}/__P4_DUT_EUI_LC__/g" -e "s/${euiU:-__none__}/__P4_DUT_EUI_UC__/g" \
        -e "s/${lc:-__none__}/__P4_DUT_MAC_LC__/g"  -e "s/${lcU:-__none__}/__P4_DUT_MAC_UC__/g" \
        -e 's/([0-9A-Fa-f]{2}:){7}[0-9A-Fa-f]{2}/<PEER-MAC>/g' \
        -e 's/[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}/<PEER-MAC>/g' \
        -e 's/TAHI:0x[0-9A-Fa-f]+/TAHI:<PEER-MAC>/gI' -e 's/TALO:0x[0-9A-Fa-f]+/TALO:<PEER-MAC>/gI' \
        -e 's/\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b/<IPv4>/g' \
        -e 's/address=0x[0-9A-Fa-f]{8}/address=<HEX32>/g' \
        -e '/Scan\] AP\[[0-9]+\].*SSID=<SSID-[0-9]+>/! s/(Scan\] AP\[[0-9]+\].*SSID=).*$/\1<SSID-redacted>/' \
        -e "s/__P4_DUT_EUI_LC__/${eui:-}/g" -e "s/__P4_DUT_EUI_UC__/${euiU:-}/g" \
        -e "s/__P4_DUT_MAC_LC__/${lc:-}/g"  -e "s/__P4_DUT_MAC_UC__/${lcU:-}/g" "$f" || return 1
    return 0
}

#  Checker (separate implementation). Prints ONE integer: residue lines
#  (peer MAC / TAHI-TALO / IPv4 / address=0x<8 hex>, DUT spellings removed
#  first) plus the number
#  of needles still found (normalized, fixed-string). Prints nothing and
#  returns non-zero if any stage fails -- the caller reads "nothing" as residue.
_p4_peer_re='[0-9a-f][0-9a-f](:[0-9a-f][0-9a-f]){5}|ta(hi|lo):0x[0-9a-f]|\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b|address=0x[0-9a-f]{8}'
p4_residue_count() {
    local f="$1" lc eui out i needle n=0 found
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    out="$( set -o pipefail
            tr 'A-Z' 'a-z' < "$f" | sed -e "s/${eui:-__none__}//g" -e "s/${lc:-__none__}//g" \
              | { $GREP -acE "$_p4_peer_re" || [ $? -eq 1 ]; } )" || return 1
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
p4_redact_check() {
    local f="$1" n
    n="$(p4_residue_count "$f")" || n=""
    case "$n" in ''|*[!0-9]*) n="?" ;; esac
    if [ "$n" = "?" ] || [ "$n" -ne 0 ]; then
        echo "## redact: residue count $n in $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    return 0
}

p4_redact_file() {
    local f="$1"
    [ -f "$f" ] || return 0
    if ! p4_redact_transform "$f"; then
        echo "## redact: transformer failed on $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    p4_redact_check "$f"
}

#  Files this run wrote (OUT and its sidecars); filled in as they are created.
P4_FILES=()
p4_redact_on_exit() {
    local rc0=$? f bad=0 n=0
    for f in ${P4_FILES[@]+"${P4_FILES[@]}"}; do
        [ -f "$f" ] || continue
        n=$((n + 1))
        p4_redact_file "$f" || bad=1
    done
    if [ "$n" -eq 0 ]; then
        echo "[P4] redact: nothing was written by this run; nothing to redact" >&2
    elif [ "$bad" -eq 0 ]; then
        echo "[P4] redact: $n file(s) masked and checked clean" >&2
    fi
    if [ "$bad" -ne 0 ]; then
        [ "$rc0" -ne 0 ] && exit "$rc0"
        exit 93
    fi
    return "$rc0"
}


#  ---------------------------------------------------------------- extra markers
#  EXTRA_MARKERS: `|`-separated FIXED strings, each counted with grep -F
#  ("[S3-INTR] VERDICT PASS" is a literal, not a bracket expression). Prints
#    extra: <string1>=<n> <string2>=<n> ...
#  or nothing when EXTRA_MARKERS is empty. Exercised by P4_MASK_SELFTEST=1.
EXTRA_MARKERS="${EXTRA_MARKERS:-}"
p4_count_extra() {
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

#  ---------------------------------------------------------------- selftest
if [ "${SELFTEST:-0}" = "1" ]; then
    _sd="$(mktemp -d)"
    _fail() { echo "selftest FAIL: $*" >&2; rm -rf "$_sd"; exit 1; }
    #  (1) marker counter over a fixture with known counts
    printf '%s\n' 'S' 'C' 'TOPPERS/FMP3 Kernel Release 3.4.0 for M5Stamp ESP32P4 <HP RV32IMAFC, RISC-V> (Sep 17 2026, 00:00:00)' \
        'Processor 2 start.' '[Arduino] setup complete' '[Arduino] loop heartbeat' '[P4-CORE2] alive 1' \
        '[Arduino] loop heartbeat' '[4-CORE2] alive 2' 'noise heartbeat' '[P4-CORE2] alive x' > "$_sd/a.log"
    _m="$(p4_count_markers "$_sd/a.log")"
    #  core2_alive 2 counts both the whole and the mangled line; core2_full 1
    #  counts only the whole one, so the difference IS the character loss.
    [ "$_m" = "heartbeat=2 core2_alive=2 core2_full=1 prc2_start=1 banner=1 setup=1 unexpected=0 romboot=0" ] || _fail "(1) markers: $_m"
    #  (2) the failure detectors, and the reboot counter
    printf '%s\n' '## Unexpected exception' 'Guru Meditation Error' '[Arduino] loop heartbeat' \
        'ESP-ROM:esp32p4-eco2-20240710' 'ESP-ROM:esp32p4-eco2-20240710' > "$_sd/b.log"
    _m="$(p4_count_markers "$_sd/b.log")"
    [ "$_m" = "heartbeat=1 core2_alive=0 core2_full=0 prc2_start=0 banner=0 setup=0 unexpected=2 romboot=2" ] || _fail "(2) unexpected: $_m"
    #  (3) an empty capture counts zero everywhere (silence is not success)
    : > "$_sd/c.log"
    _m="$(p4_count_markers "$_sd/c.log")"
    [ "$_m" = "heartbeat=0 core2_alive=0 core2_full=0 prc2_start=0 banner=0 setup=0 unexpected=0 romboot=0" ] || _fail "(3) empty: $_m"
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
    #  (5) Wi-Fi markers over a fixture with known counts. The scan count is
    #      the LAST "found N APs" value, not the number of such lines.
    printf '%s\n' \
        '[WiFiHosted] companion INIT chip_id=0x0d caps=0x0d' \
        '[WiFiHosted] companion ready (STA)' \
        '[WiFiScan] found 3 APs' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=<SSID-redacted>' \
        '[WiFiScan] AP[1] rssi=-70 ch=6 SSID=<SSID-redacted>' \
        '[WiFiScan] found 5 APs' \
        '[WiFiConnect] connected and DHCP completed' \
        '[WiFiConnect] DNS completed' \
        '[WiFiConnect] TCP received=344' > "$_sd/w.log"
    _m="$(p4_count_wifi "$_sd/w.log")"
    [ "$_m" = "wifi: companion=1 ready=1 hosted_err=0 scan=5 scanap=2 ssidraw=0 connected=1 dhcpdone=1 dnsok=1 dnsfail=0 tcp=1 disc=0" ] \
        || _fail "(5) wifi markers: $_m"
    #  (6) an unmasked SSID column counts as ssidraw, and a bring-up failure
    #      counts as hosted_err; a capture that never mentions Wi-Fi is all
    #      zeros with scan=-1 (silence is not success).
    printf '%s\n' '[WiFiHosted] no INIT event from the companion' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=NeighbourNet' > "$_sd/w2.log"
    _m="$(p4_count_wifi "$_sd/w2.log")"
    [ "$_m" = "wifi: companion=0 ready=0 hosted_err=1 scan=-1 scanap=1 ssidraw=1 connected=0 dhcpdone=0 dnsok=0 dnsfail=0 tcp=0 disc=0" ] \
        || _fail "(6) wifi failure markers: $_m"
    : > "$_sd/w3.log"
    _m="$(p4_count_wifi "$_sd/w3.log")"
    [ "$_m" = "wifi: companion=0 ready=0 hosted_err=0 scan=-1 scanap=0 ssidraw=0 connected=0 dhcpdone=0 dnsok=0 dnsfail=0 tcp=0 disc=0" ] \
        || _fail "(6) empty wifi: $_m"
    #  ---- redact/mask ----
    #  The DUT spellings the mask must keep. Set here because this block runs
    #  before the DUT identity gate (a selftest needs no board).
    DUT_MAC="${DUT_MAC:-30:ed:a0:ea:98:0e}"
    NEEDLE_VALUES=(); NEEDLE_TOKENS=(); NEEDLE_KINDS=()
    #  (7) the mask itself: peer MACs (6- and 8-octet), IPv4, address=0x,
    #      the SSID column; the DUT's own MAC and its EUI-64 spelling stay.
    printf '%s\n' \
        'MAC: 30:ed:a0:ea:98:0e' \
        'peer 44:1b:f6:e2:73:84 seen' \
        'eui 44:1b:f6:ff:fe:e2:73:84 seen' \
        'ip 192.168.1.23 gw 192.168.1.1' \
        'address=0xc0a80117' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=NeighbourNet' \
        '[WiFiScan] AP[1] rssi=-70 ch=6SSID=OtherNet' \
        '[WiFiScan] AP[2] rssi=-80 ch=11 authmode=3 SSID=<SSID-2>' > "$_sd/r.log"
    p4_redact_transform "$_sd/r.log" || _fail "(7) transformer failed"
    $GREP -q '30:ed:a0:ea:98:0e' "$_sd/r.log" || _fail "(7) the DUT's own MAC was masked"
    [ "$($GREP -ac '<PEER-MAC>' "$_sd/r.log")" -eq 2 ] || _fail "(7) peer MACs: $(cat "$_sd/r.log")"
    [ "$($GREP -ac '<IPv4>' "$_sd/r.log")" -eq 1 ] || _fail "(7) IPv4 line"
    $GREP -q 'address=<HEX32>' "$_sd/r.log" || _fail "(7) address=0x not masked"
    [ "$($GREP -ac 'SSID=<SSID-redacted>' "$_sd/r.log")" -eq 2 ] || _fail "(7) SSID column: $(cat "$_sd/r.log")"
    #  The second scan line is the F-1 shape (a dropped space before SSID=);
    #  masking it must not need that space.
    $GREP -qF 'ch=6SSID=<SSID-redacted>' "$_sd/r.log" || _fail "(7) a mangled scan line was not masked"
    $GREP -q 'rssi=-40 ch=1' "$_sd/r.log" || _fail "(7) the rest of the scan line was damaged"
    #  A line that already carries the runtime's own "<SSID-N>" placeholder
    #  keeps it: the index is evidence, and re-masking it would throw the
    #  index away for nothing (the placeholder is not a network name).
    $GREP -qF 'SSID=<SSID-2>' "$_sd/r.log" || _fail "(7) the runtime's placeholder was overwritten"
    [ "$(p4_residue_count "$_sd/r.log")" = "0" ] || _fail "(7) the checker still sees residue"
    #  (8) positive control: the checker must SEE an unmasked file, and
    #      p4_redact_file must quarantine it. Without this, "residue 0"
    #      would also be what a broken checker prints.
    printf '%s\n' 'peer 44:1b:f6:e2:73:84' 'ip 10.0.0.5' 'address=0xdeadbeef' \
        '[WiFiScan] AP[0] rssi=-40 ch=1 SSID=NeighbourNet' > "$_sd/q.log"
    _n="$(p4_residue_count "$_sd/q.log")"
    [ "$_n" = "4" ] || _fail "(8) residue count on an unmasked file: $_n (expected 4)"
    printf '%s\n' 'peer 44:1b:f6:e2:73:84' > "$_sd/q2.log"
    NEEDLE_VALUES=("SecretNet"); NEEDLE_TOKENS=("<REDACTED_SSID>"); NEEDLE_KINDS=("text")
    printf '%s\n' 'ssid SecretNet' > "$_sd/q3.log"
    p4_redact_file "$_sd/q3.log" >/dev/null 2>&1 || _fail "(8) a needle was not masked (the file was quarantined)"
    $GREP -q '<REDACTED_SSID>' "$_sd/q3.log" || _fail "(8) the needle token is missing"
    NEEDLE_VALUES=(); NEEDLE_TOKENS=(); NEEDLE_KINDS=()
    #  (9) quarantine: an unmaskable line leaves *.UNREDACTED and rc 1. The
    #      transform cannot fix a bare IPv4-looking token the checker counts,
    #      so drive it through the checker directly.
    printf '%s\n' 'peer 44:1b:f6:e2:73:84' > "$_sd/u.log"
    if p4_redact_check "$_sd/u.log" >/dev/null 2>&1; then _fail "(9) an unmasked file passed the check"; fi
    [ -f "$_sd/u.log.UNREDACTED" ] || _fail "(9) the file was not quarantined"
    [ ! -f "$_sd/u.log" ] || _fail "(9) the original was left in place"
    #  (10) EXTRA_MARKERS: FIXED strings, so '[' is a literal. An ERE would
    #       make '[GPIO-INTR]' a bracket expression and count every line
    #       holding one of those characters.
    printf '%s\n' '[GPIO-INTR] VERDICT PASS' 'GPIO' 'x' '[GPIO-INTR] VERDICT PASS' > "$_sd/e.log"
    EXTRA_MARKERS='[GPIO-INTR] VERDICT PASS|GPIO'
    _m="$(p4_count_extra "$_sd/e.log")"
    [ "$_m" = "extra: [GPIO-INTR] VERDICT PASS=2 GPIO=3" ] || _fail "(10) extra markers: $_m"
    EXTRA_MARKERS=''
    [ -z "$(p4_count_extra "$_sd/e.log")" ] || _fail "(10) empty EXTRA_MARKERS printed something"
    rm -rf "$_sd"
    echo "p4 capture selftest PASS: (1) markers 2/2/1/1/1/0 (2) unexpected 2 (3) empty -> zeros (4) bootloader gate: 0x2000 LF/CRLF accepted; 0x0, 0x1000, other key, duplicate, missing refused (5) wifi markers: scan = last N, scanap 2, ssidraw 0 (6) hosted_err 1 / ssidraw 1 / empty -> zeros with scan -1 (7) mask: peer 2 (EUI-64 whole) / IPv4 1 / HEX32 / SSID column 2 incl. the dropped-space shape / DUT MAC kept / residue 0 (8) positive control: checker sees 4 on an unmasked file, needle masked to its token (9) quarantine: rc!=0 + .UNREDACTED, original gone (10) EXTRA_MARKERS fixed strings 2/3, empty -> nothing"
    exit 0
fi

#  ---------------------------------------------------------------- redact-only
#  LOG_DIR is resolved here rather than with the other output paths below,
#  and the redact-only mode sits BEFORE the DUT identity gate: masking a log
#  that was captured earlier needs no board and no DUT_MAC (the mask keeps
#  the DUT's own MAC only when DUT_MAC happens to be set; with it unset,
#  every MAC in the file is masked, which is the safe direction).
LOG_DIR="${LOG_DIR:-$(cd "$(dirname "$0")/.." && pwd -P)/build/p4-capture}"

#  The M-6 guard. Rewriting in place is fine for a capture this script
#  wrote; it is not fine for anything else, and the only difference between
#  the two on the command line is where the file lives. Two rules, checked
#  before anything is touched: a file git tracks is never masked (masked
#  source is still source, and would be committed as such), and a file
#  outside LOG_DIR is masked only with P4_REDACT_ANYWHERE=1 said out loud.
p4_redact_guard() {
    local f="$1" abs dir_abs
    if git -C "$(dirname -- "$f")" ls-files --error-unmatch -- "$(basename -- "$f")" >/dev/null 2>&1; then
        die "P4_REDACT_ONLY refuses a git-tracked file: $f (a masked source or document is not a redacted log)"
    fi
    abs="$(realpath -m -- "$f")"; dir_abs="$(realpath -m -- "$LOG_DIR")"
    case "$abs" in
        "$dir_abs"/*) ;;
        *) [ "${P4_REDACT_ANYWHERE:-0}" = "1" ] \
               || die "P4_REDACT_ONLY refuses a file outside LOG_DIR ($LOG_DIR): $f (set P4_REDACT_ANYWHERE=1 for a log kept elsewhere)" ;;
    esac
}
if [ "${P4_REDACT_ONLY:-0}" = "1" ]; then
    [ "$#" -ge 1 ] || die "P4_REDACT_ONLY=1 needs the files to redact as arguments"
    for f in "$@"; do [ -f "$f" ] || die "not a file: $f"; done
    for f in "$@"; do p4_redact_guard "$f"; done
    p4_load_needles
    P4_FILES=("$@")
    p4_redact_on_exit
    exit "$?"
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

#  From here on every file this run writes is masked and checked on the way
#  out - including on an abort, which is when a half-written log is most
#  likely to hold something. p4_load_needles() only prints counts.
p4_load_needles
trap 'p4_redact_on_exit' EXIT

#  ---------------------------------------------------------------- modes and paths
DRYRUN="${DRYRUN:-0}"; NOFLASH="${NOFLASH:-0}"; NORESET="${NORESET:-0}"
#  NORESET=1 is the true-cold shape: the operator cuts the board's power
#  (uhubctl on its own port) and brings it back, and this script only reads.
#  Resetting here would turn a cold boot into a warm one and the run would
#  answer a question nobody asked.
[ "$NORESET" = "1" ] && NOFLASH=1
CAPTURE_SEC="${CAPTURE_SEC:-30}"
RESET_MODE="${RESET_MODE:-watchdog-reset}"
case "$RESET_MODE" in hard-reset|watchdog-reset) ;; *) die "RESET_MODE must be hard-reset or watchdog-reset (got '$RESET_MODE')" ;; esac
#  LOG_DIR was resolved in the redact-only section above.
mkdir -p "$LOG_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$LOG_DIR/p4-capture-$STAMP.log"
IDENT_LOG="$LOG_DIR/p4-capture-$STAMP.ident.log"
FLASH_LOG="$LOG_DIR/p4-capture-$STAMP.flash.log"
READBACK_BIN="$LOG_DIR/p4-capture-$STAMP.readback.bin"
#  The three text files go through the mask on exit. The readback is a
#  flash image, not text: masking it would corrupt the very bytes the
#  write check compares, and it holds no credentials (bootloader region).
P4_FILES=("$OUT" "$IDENT_LOG" "$FLASH_LOG")

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
    #  boot_app0 at 0xe000. NOT optional in practice: the partition table
    #  this board uses (default, 16MB) has otadata there and no factory
    #  partition, so whatever the flash already holds at 0xe000 decides
    #  which app the bootloader starts. A board that came with other
    #  firmware can therefore boot a stale, now-erased slot and say nothing
    #  at all - measured on this board, 2026-09-18: the first run wrote
    #  three images, the ROM banner appeared, and the console stayed silent.
    #  The platform's own upload recipe writes it
    #  ({runtime.platform.path}/tools/partitions/boot_app0.bin), so this
    #  script writes the same file from the same place.
    if [ "${BOOT_APP0:-}" = "" ]; then
        BOOT_APP0="$(dirname -- "$BOARDS_TXT")/tools/partitions/boot_app0.bin"
    fi
    if [ "$BOOT_APP0" != "none" ]; then
        [ -f "$BOOT_APP0" ] || die "boot_app0.bin not found: $BOOT_APP0 (set BOOT_APP0=<path>, or BOOT_APP0=none to skip it - see the note above)"
        [ "$(stat -c %s "$BOOT_APP0")" -eq 8192 ] || die "boot_app0.bin is not 8192 bytes: $BOOT_APP0"
    fi
fi

#  ---------------------------------------------------------------- 1. identify (read-only)
say "==== 1. identify (read-only) ===="
say "  DUT_MAC=$DUT_MAC  port=$DUT_PORT  esptool=$ESPTOOL"
[ -e "$DUT_PORT" ] || die "DUT port is absent: $DUT_PORT (DUT_PORT=... to override; is the board plugged in?)"
if [ "$NORESET" = "1" ]; then
    #  esptool is NOT called in this mode. Its connect asserts the CDC reset
    #  lines and parks the chip in the ROM download mode, which destroys the
    #  very thing a true-cold run is there to observe - measured 2026-09-18:
    #  five power cycles in a row captured zero bytes because the gate had
    #  reset the board into download mode before the reader opened.
    #  The identity still holds: the P4's built-in USB Serial/JTAG puts its
    #  MAC in the device's serial number, so the by-id node this script
    #  derived from DUT_MAC cannot resolve to another board.
    say "NORESET=1: esptool is not called (its connect would reset the board and overwrite the cold boot)"
    say "identity: by-id node only, which carries the MAC ($DUT_MAC)"
else
    if ! "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --no-stub --after no-reset flash-id > "$IDENT_LOG" 2>&1; then
        sed 's/^/    /' "$IDENT_LOG" >&2; die "flash-id failed (the board did not answer); nothing written"
    fi
    $GREP -iE 'Chip (is|type)|MAC:|Manufacturer|Device:|Detected flash size|Crystal|revision' "$IDENT_LOG" | sed 's/^/    /'
    $GREP -qi "MAC: *$dut_lc" "$IDENT_LOG" || $GREP -qi "MAC: *$dut_uc" "$IDENT_LOG" \
        || die "MAC mismatch: expected $DUT_MAC (another board on this port?); nothing written"
    $GREP -q "ESP32-P4" "$IDENT_LOG" || die "the chip is not an ESP32-P4; nothing written"
    $GREP -qiE 'Detected flash size: *16MB' "$IDENT_LOG" || say "WARNING: flash size is not 16MB as boards.txt assumes (see ident log)"
    say "identity OK: $DUT_MAC / ESP32-P4"
fi

if [ "$NOFLASH" != "1" ]; then
    say "==== write plan: $BL_ADDR $(basename "$IMG_BL") / 0x8000 $(basename "$IMG_PT") / 0xe000 ${BOOT_APP0##*/} / 0x10000 $(basename "$IMG_APP") (flash mode/freq/size: keep, as the platform's upload recipe does) ===="
    sha256sum "$IMG_BL" "$IMG_PT" "$IMG_APP" | sed 's/^/    /'
fi
if [ "$DRYRUN" = "1" ]; then say "DRYRUN=1: stopping here; nothing written"; exit 0; fi

#  ---------------------------------------------------------------- 2. write + readback
if [ "$NOFLASH" != "1" ]; then
    say "==== 2. write ===="
    #  `keep` for all three, which is what the platform's own upload recipe
    #  passes (platform.txt tools.esptool_py.upload.pattern_args). It matters
    #  twice. (a) Faithfulness: a run that writes the images differently from
    #  the way a user's IDE writes them is measuring something the user never
    #  gets. (b) The readback below can then be a plain `cmp`: with an
    #  explicit --flash-mode, esptool patches the image header in flight
    #  (byte 2 = SPI mode) and recomputes the image's trailing SHA-256, so
    #  33 bytes of a correct write differ from the file - measured on this
    #  board, 2026-09-18, when the check fired on a write that was fine.
    WRITE_ARGS=("$BL_ADDR" "$IMG_BL" 0x8000 "$IMG_PT" 0x10000 "$IMG_APP")
    N_IMAGES=3
    if [ "$BOOT_APP0" != "none" ]; then
        WRITE_ARGS+=(0xe000 "$BOOT_APP0")
        N_IMAGES=4
    else
        say "WARNING: BOOT_APP0=none - 0xe000 (otadata) is NOT written; the flash decides which app slot boots"
    fi
    if ! "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --baud 921600 --before default-reset --after no-reset \
            write-flash -z --flash-mode keep --flash-freq keep --flash-size keep \
            "${WRITE_ARGS[@]}" > "$FLASH_LOG" 2>&1; then
        tail -30 "$FLASH_LOG" | sed 's/^/    /' >&2; die "write-flash failed ($FLASH_LOG)"
    fi
    N="$($GREP -c 'Hash of data verified' "$FLASH_LOG" || true)"
    [ "${N:-0}" -ge "$N_IMAGES" ] || die "'Hash of data verified' seen $N time(s), expected $N_IMAGES ($FLASH_LOG)"
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
say "==== 3. $([ "$NORESET" = "1" ] && echo "no reset" || echo "reset ($RESET_MODE)") and capture ${CAPTURE_SEC}s ===="
#  Open the tty first so the seam's first bytes ('S', 'C') are not lost,
#  then reset through esptool. The USB Serial/JTAG ignores the baud rate.
: > "$OUT"
#  The reader is a small python3 loop, for two reasons that both showed up
#  on this board (2026-09-18) and neither of which a `cat` can handle.
#
#  (1) It REOPENS the port. The P4's console is its built-in USB
#      Serial/JTAG, so a chip reset takes the USB device down and brings it
#      back up: an open fd on /dev/ttyACMn dies there and a plain `cat`
#      returns after the two ROM banner lines, whatever the board does
#      afterwards. That is not a cosmetic difference - it is exactly the
#      shape a boot loop has, so a running board and a reboot loop produce
#      the same two-line capture.
#  (2) It opens with O_NONBLOCK and sets CLOCAL. A CDC-ACM port that has
#      just come up from a POWER CYCLE has no carrier asserted, and a
#      blocking open() - which is what `cat` and `stty -F` both do - waits
#      for it forever. The true-cold loop hung there, in stty, before a
#      single byte was read.
#  python3 is already a hard dependency of this platform (the link driver).
_p4_capture() {   # seconds
    local secs="$1"
    PORT="$DUT_PORT" OUTFILE="$OUT" SECS="$secs" python3 - <<'PYEOF'
import os, select, sys, termios, time

port = os.environ["PORT"]
out_path = os.environ["OUTFILE"]
deadline = time.monotonic() + float(os.environ["SECS"])

def open_port():
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        #  raw, no echo, CLOCAL (do not wait for carrier), 115200 both ways
        attrs[0] = 0                      # iflag
        attrs[1] = 0                      # oflag
        attrs[3] = 0                      # lflag
        attrs[2] = (attrs[2] | termios.CLOCAL | termios.CREAD) & ~termios.CRTSCTS
        attrs[4] = termios.B115200
        attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass                              # a USJ CDC ignores the line settings
    return fd

with open(out_path, "ab", buffering=0) as out:
    fd = None
    while time.monotonic() < deadline:
        if fd is None:
            if not os.path.exists(port):
                time.sleep(0.1)
                continue
            try:
                fd = open_port()
            except OSError:
                time.sleep(0.1)
                continue
        try:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                data = os.read(fd, 4096)
                if data:
                    out.write(data)
                else:
                    raise OSError("eof")
        except OSError:
            try:
                os.close(fd)
            except OSError:
                pass
            fd = None
            time.sleep(0.1)
    if fd is not None:
        os.close(fd)
PYEOF
}
#  Reset FIRST, then read. The other order is the natural one (open the tty
#  so the seam's first bytes are not lost) and it is wrong here: the reader
#  and esptool would share one USB CDC endpoint, the reader would swallow
#  esptool's replies, and esptool fails with "Serial data stream stopped:
#  Possible serial noise or corruption" leaving the board in download mode -
#  measured 2026-09-18. Nothing is lost by waiting: the port disappears
#  across the reset anyway, so no reader can hold the first bytes.
if [ "$NORESET" = "1" ]; then
    say "NORESET=1: not resetting; reading whatever the board is doing (true-cold shape)"
else
    "$ESPTOOL" --chip esp32p4 --port "$DUT_PORT" --before default-reset --after "$RESET_MODE" chip-id >> "$FLASH_LOG" 2>&1 \
        || say "WARNING: the reset command returned non-zero (see $FLASH_LOG); capturing anyway"
fi
_p4_capture "$CAPTURE_SEC"
#  Strip ANSI colour and NUL so the counts see plain text.
sed -i 's/\x1b\[[0-9;]*m//g; s/\x00//g' "$OUT" 2>/dev/null || true
say "captured $(wc -l < "$OUT") lines -> $OUT"

#  ---------------------------------------------------------------- 4. markers
MARKER_LINE="$(p4_count_markers "$OUT")"
say "$MARKER_LINE"
WIFI_LINE="$(p4_count_wifi "$OUT")"
say "$WIFI_LINE"
EXTRA_LINE="$(p4_count_extra "$OUT")"
[ -z "$EXTRA_LINE" ] || say "$EXTRA_LINE"
FIRST="$(head -c 8 "$OUT" | od -An -c | tr -s ' ' | head -1)"
say "first bytes:$FIRST   (expect 'S' from the seam entry on core0 and 'C' from core1)"
if $GREP -qE 'unexpected=0' <<< "$MARKER_LINE" && $GREP -qE 'heartbeat=[1-9]' <<< "$MARKER_LINE" \
        && $GREP -qE 'core2_alive=[1-9]' <<< "$MARKER_LINE"; then
    say "VERDICT PASS (heartbeat and core2_alive both counted, no failure marker)"
    exit 0
fi
say "VERDICT FAIL (see $OUT; if it is empty and step 1 passed, try RESET_MODE=hard-reset)"
exit 2
