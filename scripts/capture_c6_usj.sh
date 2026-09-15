#!/usr/bin/env bash
#
#  scripts/capture_c6_usj.sh -- gated flash-and-capture for M5NanoC6 (ESP32-C6)
#  ===========================================================================
#
#  Writes the four images an `arduino-cli compile` run produces for the
#  toppers:esp32:m5nanoc6_fmp3 board to the M5NanoC6 over its USB Serial/JTAG
#  (USJ) port, then records the same port for CAPTURE_SEC seconds and counts
#  the markers the FMP3 runtime and the Arduino bridge print. Verdicts are the
#  caller's: the script only prints the counts.
#
#  This is the arduino_esp32 port of the development-side
#  esp/boot/flash_and_capture_c6_usj.sh (fmp3_esp_idf_dev). What changed:
#    - the images come from an arduino-cli build directory (SKETCH_BUILD),
#      plus the core's boot_app0.bin, at the addresses the M5Stack core's
#      platform.txt upload recipe uses: bootloader 0x0 / partition table
#      0x8000 / boot_app0 0xe000 / app 0x10000. BOOTLOADER= / PTABLE= override
#      single images so one axis at a time can be swapped (stage 2 arms B/C).
#    - esptool is the M5Stack core's bundled esptool_py (5.x, dash-style
#      subcommands: write-flash, flash-id). The write-flash arguments are the
#      upload recipe's (-z, --flash-mode/--flash-freq/--flash-size keep).
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
#    3. the capture prerequisites exist (esp_idf_monitor python, port node).
#    4. esptool flash-id (read-only, ROM only, --after no-reset) reports
#       BASE MAC == DUT_MAC, chip type == DUT_CHIP, detected flash size 4MB.
#
#  Usage:
#    SKETCH_BUILD=<dir> bash scripts/capture_c6_usj.sh
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
#    ESPTOOL       esptool executable. Default: newest
#                  <arduino data>/packages/m5stack/tools/esptool_py/*/esptool.
#    ARDUINO_DIRECTORIES_DATA  arduino-cli data directory (default ~/.arduino15,
#                  the same rule scripts/arduino_sdk.py applies).
#    BAUD          esptool baud (default 921600 = the board's UploadSpeed
#                  default; USJ ignores it).
#    DUT_MAC       expected BASE MAC (default 9c:13:9e:d3:62:18, the M5NanoC6).
#    DUT_CHIP      expected chip type string (default "ESP32-C6FH4 (QFN32)").
#    DUT_PORT      serial device. Default: /dev/serial/by-id name derived
#                  from DUT_MAC (Espressif USB JTAG/serial debug unit).
#    OUT           capture log path (default LOG_DIR/c6-capture-<stamp>.log).
#                  Sidecars are written next to it: .ident.log (flash-id),
#                  .flash.log (write-flash), .cold.txt (COLD=1 by-id timeline),
#                  .sha.txt (sha256 of what this run wrote).
#    LOG_DIR       default directory for OUT
#                  (default $HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/.steering/
#                   20260915-c6-arduino-plan/stage2/logs).
#    CAPTURE_SEC   capture length in seconds (default 60; digits only).
#    MARKERS       ERE; when it matches the capture, stop early (default empty
#                  = always capture the full CAPTURE_SEC).
#    DRYRUN=1      identify the DUT read-only, print what WOULD be written and
#                  the exact esptool command line, write nothing, capture
#                  nothing. NOTE: the identification enters the ROM download
#                  mode (esptool --before default-reset) and, with
#                  --after no-reset, LEAVES THE BOARD THERE; the next write
#                  (or a power cycle / hard reset) recovers it.
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
#    WIFI_CREDS    credentials shell file for the redact needles (default
#                  $HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/esp/boot/
#                  wifi_credentials.sh; absent = no needles).
#    IDF_PYTHON    python of an ESP-IDF python env that has esp_idf_monitor
#                  (default: newest $HOME/tools/espressif/python_env/
#                  idf*_py3.*_env or $HOME/.espressif/python_env/... by sort -V).
#    C6_MASK_SELFTEST=1  run the redact/mask self-test on fixtures and exit.
#                  Touches no hardware.
#
#  Markers counted at the end (strings from src/bridge/ArduinoSketchBridge.cpp,
#  third_party/fmp3_core/syssvc/banner.c and arch/riscv_gcc/common):
#    banner     "TOPPERS/FMP3 Kernel Release"
#    setup      "[Arduino] setup complete"
#    heartbeat  "[Arduino] loop heartbeat"      (once per 1000 loop() calls)
#    unexpected "## Unexpected" / "## Assertion" / "## Internal" /
#               "Unregistered exception|interrupt" / "mcause="
#    smark      a line that is just "S" (SEAM_C6_ENTRY_MARK, printed by the
#               seam entry before the FMP3 banner)
#    blink      "[Blink] ON" / "[Blink] OFF"    (examples/Blink only)
#
#  Exit status: 0 when the capture ran (or DRYRUN completed); non-zero when
#  the gate refused, a tool failed, or the redact stage could not prove the
#  logs clean (then the files are renamed *.UNREDACTED; do not commit them).
#
set -euo pipefail

say() { echo "[C6] $*"; }
die() { echo "" >&2; echo "ABORT: $*" >&2; exit 1; }

#  Use the real grep: on some hosts `grep` is a function that delegates to a
#  different tool (ugrep) with different exit-code and .gitignore behaviour.
GREP=/usr/bin/grep

#  ---------------------------------------------------------------- DUT identity
#  Exactly one C6 board exists (M5NanoC6). A typo here would write to somebody
#  else's board, so the default is a literal and the FORBIDDEN list is the
#  development-side one (the ESP32-P4 boards plus the asp3 C6 devkit).
DUT_MAC="${DUT_MAC:-9c:13:9e:d3:62:18}"
DUT_CHIP="${DUT_CHIP:-ESP32-C6FH4 (QFN32)}"
FORBIDDEN="60:55:f9:57:c2:60 d0:cf:13:f0:a7:44 d0:cf:13:f0:c8:94 30:76:f5:ed:86:a8
14:c1:9f:e0:61:b0 30:ed:a0:f3:f1:64 44:1b:f6:e2:73:84 78:21:84:a6:5c:64 f4:12:fa:5b:4a:58"

dut_lc="$(printf '%s' "$DUT_MAC" | tr 'A-Z' 'a-z')"
dut_uc="$(printf '%s' "$DUT_MAC" | tr 'a-z' 'A-Z')"
printf '%s' "$dut_lc" | /usr/bin/grep -qE '^[0-9a-f]{2}(:[0-9a-f]{2}){5}$' \
    || die "DUT_MAC must look like hh:hh:hh:hh:hh:hh (given: $DUT_MAC)"
DUT_PORT="${DUT_PORT:-/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${dut_uc}-if00}"

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
#  The transformer is sed; the checker is a separate implementation (normalize
#  then fixed-string / grep -E count). Both are exercised by
#  C6_MASK_SELFTEST=1. The checker's silence is never taken as success: if a
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
c6_load_needles() {
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
c6_redact_transform() {
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
    local lc eui lcU euiU
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    lcU="$(printf '%s' "$lc" | tr 'a-z' 'A-Z')"; euiU="$(printf '%s' "$eui" | tr 'a-z' 'A-Z')"
    sed -i -E \
        -e "s/${eui:-__none__}/__C6_DUT_EUI_LC__/g" -e "s/${euiU:-__none__}/__C6_DUT_EUI_UC__/g" \
        -e "s/${lc:-__none__}/__C6_DUT_MAC_LC__/g"  -e "s/${lcU:-__none__}/__C6_DUT_MAC_UC__/g" \
        -e 's/([0-9A-Fa-f]{2}:){7}[0-9A-Fa-f]{2}/<PEER-MAC>/g' \
        -e 's/[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}/<PEER-MAC>/g' \
        -e 's/TAHI:0x[0-9A-Fa-f]+/TAHI:<PEER-MAC>/gI' -e 's/TALO:0x[0-9A-Fa-f]+/TALO:<PEER-MAC>/gI' \
        -e 's/\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b/<IPv4>/g' \
        -e "s/__C6_DUT_EUI_LC__/${eui:-}/g" -e "s/__C6_DUT_EUI_UC__/${euiU:-}/g" \
        -e "s/__C6_DUT_MAC_LC__/${lc:-}/g"  -e "s/__C6_DUT_MAC_UC__/${lcU:-}/g" "$f" || return 1
    return 0
}

#  Checker (separate implementation). Prints ONE integer: residue lines
#  (peer MAC / TAHI-TALO / IPv4, DUT spellings removed first) plus the number
#  of needles still found (normalized, fixed-string). Prints nothing and
#  returns non-zero if any stage fails -- the caller reads "nothing" as residue.
_c6_peer_re='[0-9a-f][0-9a-f](:[0-9a-f][0-9a-f]){5}|ta(hi|lo):0x[0-9a-f]|\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b'
c6_residue_count() {
    local f="$1" lc eui out i needle n=0 found
    lc="$(_dut_forms_lc | sed -n 1p)"; eui="$(_dut_forms_lc | sed -n 2p)"
    out="$( set -o pipefail
            tr 'A-Z' 'a-z' < "$f" | sed -e "s/${eui:-__none__}//g" -e "s/${lc:-__none__}//g" \
              | { $GREP -acE "$_c6_peer_re" || [ $? -eq 1 ]; } )" || return 1
    case "$out" in ''|*[!0-9]*) return 1 ;; esac
    n=$out
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
c6_redact_check() {
    local f="$1" n
    n="$(c6_residue_count "$f")" || n=""
    case "$n" in ''|*[!0-9]*) n="?" ;; esac
    if [ "$n" = "?" ] || [ "$n" -ne 0 ]; then
        echo "## redact: residue count $n in $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    return 0
}

c6_redact_file() {
    local f="$1"
    [ -f "$f" ] || return 0
    if ! c6_redact_transform "$f"; then
        echo "## redact: transformer failed on $f -- quarantined as $f.UNREDACTED; do not commit it" >&2
        mv -f -- "$f" "$f.UNREDACTED"
        return 1
    fi
    c6_redact_check "$f"
}

#  Files this run wrote (OUT and its sidecars); filled in as they are created.
C6_FILES=()
c6_redact_on_exit() {
    local rc0=$? f bad=0 n=0
    for f in ${C6_FILES[@]+"${C6_FILES[@]}"}; do
        [ -f "$f" ] || continue
        n=$((n + 1))
        c6_redact_file "$f" || bad=1
    done
    if [ "$n" -eq 0 ]; then
        echo "[C6] redact: nothing was written by this run; nothing to redact" >&2
    elif [ "$bad" -eq 0 ]; then
        echo "[C6] redact: $n file(s) masked and checked clean" >&2
    fi
    if [ "$bad" -ne 0 ]; then
        [ "$rc0" -ne 0 ] && exit "$rc0"
        exit 93
    fi
    return "$rc0"
}

#  ---------------------------------------------------------------- self-test
if [ "${C6_MASK_SELFTEST:-0}" = "1" ]; then
    #  Positive controls on fixtures. Touches no hardware, writes only to a
    #  temporary directory.
    _sd="$(mktemp -d)"; _st="$_sd/fixture.log"
    _fail() { echo "selftest FAIL: $*" >&2; chmod -R u+w "$_sd" 2>/dev/null; rm -rf "$_sd"; exit 1; }
    _count() { $GREP -o "$1" "$2" | wc -l; }
    _fix_mask() {
        printf 'port _%s-if00\nBASE MAC: %s\nMAC: %s\nCCMP mgmt frame from 12:34:56:ab:cd:ef used\n<ba-add> TAHI:0xabcd, TALO:0x12345678, x\ngot ip 192.168.4.23 mask 255.255.255.0\npeer eui64 12:34:56:ff:fe:ab:cd:ef seen\n' \
            "$dut_uc" "$dut_lc" "${dut_lc:0:8}:ff:fe:${dut_lc:9}" > "$_st"
    }
    #  --- (1)-(6): no needles (credentials file absent) ---
    WIFI_CREDS="$_sd/no-such-creds.sh"
    c6_load_needles >/dev/null
    [ "${#NEEDLE_VALUES[@]}" -eq 0 ] || _fail "(0) needles loaded from a missing file"
    _fix_mask
    _pre="$(c6_residue_count "$_st")" || _fail "(1) checker returned non-zero on the fixture"
    [ "$_pre" = "4" ] || _fail "(1) residue before masking is not 4 lines ($_pre)"
    c6_redact_check "$_st" 2>/dev/null && _fail "(2) quarantine branch returned zero"
    [ ! -e "$_st" ] && [ -f "$_st.UNREDACTED" ] || _fail "(2) fixture was not renamed to .UNREDACTED"
    mv -f -- "$_st.UNREDACTED" "$_st"
    #  (3) transformer cannot write (read-only directory) -> on_exit exits 93
    _ro="$_sd/ro"; mkdir -p "$_ro"; cp -f "$_st" "$_ro/fixture.log"; chmod 555 "$_ro"
    ( C6_FILES=("$_ro/fixture.log"); c6_redact_on_exit 2>/dev/null ) && _rc=0 || _rc=$?
    chmod 755 "$_ro"
    [ "$_rc" -eq 93 ] || _fail "(3) c6_redact_on_exit rc is not 93 ($_rc)"
    #  (4) transform: residue 0, DUT spellings kept, 4 peer masks (one of them
    #  a whole 8-octet EUI-64, no ":hh:hh" tail left), 2 IPv4 masks
    c6_redact_file "$_st" || _fail "(4) residue after transform"
    $GREP -qF "_${dut_uc}-if00" "$_st" || _fail "(4) DUT MAC (upper case) was masked"
    $GREP -qF "BASE MAC: ${dut_lc}" "$_st" || _fail "(4) DUT MAC (lower case) was masked"
    $GREP -qF "MAC: ${dut_lc:0:8}:ff:fe:${dut_lc:9}" "$_st" || _fail "(4) DUT EUI-64 spelling was masked"
    [ "$(_count '<PEER-MAC>' "$_st")" -eq 4 ] || _fail "(4) peer masks are not 4 ($(_count '<PEER-MAC>' "$_st"))"
    [ "$(_count '<PEER-MAC>:[0-9A-Fa-f]' "$_st")" -eq 0 ] || _fail "(4) a peer EUI-64 left a tail after <PEER-MAC>"
    $GREP -qF "peer eui64 <PEER-MAC> seen" "$_st" || _fail "(4) the peer EUI-64 line is not masked whole"
    [ "$(_count '<IPv4>' "$_st")" -eq 2 ] || _fail "(4) IPv4 masks are not 2"
    #  (5) DUT_MAC unset: everything MAC-shaped is masked, still clean
    _fix_mask
    ( DUT_MAC=""; c6_redact_file "$_st" ) || _fail "(5) transform with DUT_MAC unset failed"
    [ "$(_count '<PEER-MAC>' "$_st")" -eq 7 ] || _fail "(5) peer masks with DUT_MAC unset are not 7 ($(_count '<PEER-MAC>' "$_st"))"
    [ "$(_count '<PEER-MAC>:[0-9A-Fa-f]' "$_st")" -eq 0 ] || _fail "(5) the DUT EUI-64 left a tail after <PEER-MAC>"
    #  (6) checker stage failure -> empty output and non-zero (never "0")
    _n="$(c6_residue_count "$_sd/missing.log" 2>/dev/null)" && _fail "(6) checker returned zero on a missing file"
    [ -z "$_n" ] || _fail "(6) checker printed something on a missing file ($_n)"
    #  --- (7)-(8): needles from a synthetic credentials file ---
    WIFI_CREDS="$_sd/creds.sh"
    printf '#!/bin/sh\nWIFI_STA_SSID="SelfTestNet-42"\nexport WIFI_STA_PASS='"'"'SelfTestPass9'"'"'\nWIFI_STA_BSSID=de:ad:be:ef:00:01\n' > "$WIFI_CREDS"
    c6_load_needles >/dev/null
    [ "${#NEEDLE_VALUES[@]}" -eq 7 ] || _fail "(7) needle count is not 7 (3 values + 2 hex spellings x 2 text values): ${#NEEDLE_VALUES[@]}"
    printf 'wifi: ssid=selftestnet-42 pass=SELFTESTPASS9\nbssid DE-AD-BE-EF-00-01 connected\nhex 53 65 6c 66 54 65 73 74 4e 65 74 2d 34 32 dump\nlink 10.0.0.7 up\n' > "$_st"
    _pre="$(c6_residue_count "$_st")" || _fail "(7) checker returned non-zero on the needle fixture"
    [ "$_pre" = "5" ] || _fail "(7) residue before redact is not 5 (4 needles + 1 IPv4 line): $_pre"
    c6_redact_file "$_st" || _fail "(8) residue after needle redact"
    [ "$(_count '<REDACTED_SSID>' "$_st")" -eq 2 ] || _fail "(8) SSID tokens are not 2 (plain + hex)"
    [ "$(_count '<REDACTED_PASS>' "$_st")" -eq 1 ] || _fail "(8) PASS token is not 1"
    [ "$(_count '<REDACTED_BSSID>' "$_st")" -eq 1 ] || _fail "(8) BSSID token is not 1 (dash spelling)"
    [ "$(_count '<IPv4>' "$_st")" -eq 1 ] || _fail "(8) IPv4 token is not 1"
    #  (9) negative control: a needle the transformer was NOT given stays
    #  visible to the checker (the checker is not the transformer's echo)
    NEEDLE_VALUES+=("SelfTestExtra"); NEEDLE_TOKENS+=("<REDACTED_X>"); NEEDLE_KINDS+=("text")
    printf 'x SELFTESTEXTRA y\n' > "$_st"
    _pre="$(c6_residue_count "$_st")" || _fail "(9) checker failed"
    [ "$_pre" = "1" ] || _fail "(9) checker does not see the extra needle ($_pre)"
    rm -rf "$_sd"
    echo "c6 redact selftest PASS: (1) residue 4 (2) quarantine rc!=0 + .UNREDACTED (3) transformer failure -> rc 93 (4) masked: peer 4 (EUI-64 whole) / IPv4 2 / DUT kept (5) DUT_MAC unset -> 7 masks, no tails (6) checker failure -> empty (7) creds needles 7, residue 5 (8) tokens SSID 2 / PASS 1 / BSSID 1 / IPv4 1 (9) checker sees an untransformed needle"
    exit 0
fi

trap 'c6_redact_on_exit' EXIT

#  ---------------------------------------------------------------- modes
DRYRUN="${DRYRUN:-0}"; NOFLASH="${NOFLASH:-0}"; COLD="${COLD:-0}"; NORESET="${NORESET:-0}"
for _v_name in DRYRUN NOFLASH COLD NORESET; do
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

c6_load_needles

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
    [ -n "$ESPTOOL" ] && [ -x "$ESPTOOL" ] || die "esptool not found (ESPTOOL=$ESPTOOL)"
fi

#  The write list, in the upload recipe's order and addresses.
WRITE_ARGS=(0x0 "$IMG_BL" 0x8000 "$IMG_PT")
[ "$BOOT_APP0" = "none" ] || WRITE_ARGS+=(0xe000 "$BOOT_APP0")
WRITE_ARGS+=(0x10000 "$IMG_APP")
N_IMAGES=$(( ${#WRITE_ARGS[@]} / 2 ))
FLASH_CMD=("$ESPTOOL" --chip esp32c6 --port "$DUT_PORT" --baud "$BAUD"
           --before default-reset --after hard-reset write-flash -z
           --flash-mode keep --flash-freq keep --flash-size keep "${WRITE_ARGS[@]}")

print_write_plan() {
    local i
    say "write plan (address  sha256  size  file):"
    for ((i = 0; i < ${#WRITE_ARGS[@]}; i += 2)); do
        printf '    %-8s %s  %8d  %s\n' "${WRITE_ARGS[$i]}" \
            "$(sha256sum "${WRITE_ARGS[$((i+1))]}" | cut -c1-64)" \
            "$(stat -c %s "${WRITE_ARGS[$((i+1))]}")" "${WRITE_ARGS[$((i+1))]}"
    done
    [ "$BOOT_APP0" = "none" ] && say "    WARNING: BOOT_APP0=none -- 0xe000 (otadata/boot_app0) is NOT written. The stage 2 plan forbids this except as a recorded deviation."
    say "esptool command line:"
    printf '    '; printf '%q ' "${FLASH_CMD[@]}"; printf '\n'
}

#  ---------------------------------------------------------------- 2. output paths
LOG_DIR="${LOG_DIR:-$HOME/TOPPERS/ESP32/fmp3_esp_idf_dev/.steering/20260915-c6-arduino-plan/stage2/logs}"
OUT="${OUT:-$LOG_DIR/c6-capture-$(date +%Y%m%d-%H%M%S).log}"
BASE="${OUT%.log}"
IDENT_LOG="$BASE.ident.log"; FLASH_LOG="$BASE.flash.log"; COLD_TXT="$BASE.cold.txt"; SHA_TXT="$BASE.sha.txt"

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
#  (no-reset) when a write follows (write-flash resets it again anyway) or in
#  DRYRUN; when nothing will be written and the monitor will not reset either
#  (NOFLASH=1 NORESET=1), hard-reset so the monitor listens to a running
#  board rather than to one this script parked in the download mode.
IDENT_AFTER=no-reset
if [ "$NOFLASH" = "1" ] && [ "$NORESET" = "1" ] && [ "$DRYRUN" != "1" ]; then IDENT_AFTER=hard-reset; fi
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
    C6_FILES+=("$IDENT_LOG")
    if ! "$ESPTOOL" --chip esp32c6 --port "$DUT_PORT" --no-stub --after "$IDENT_AFTER" flash-id >"$IDENT_LOG" 2>&1; then
        sed 's/^/    /' "$IDENT_LOG" >&2
        die "flash-id failed (the DUT did not answer); see $IDENT_LOG"
    fi
    { $GREP -E "Chip type:|BASE MAC:|Manufacturer|Device:|Detected flash size|Crystal" "$IDENT_LOG" || true; } | sed 's/^/    /'
    #  BASE MAC is the 48-bit one; the "MAC:" line on C6 is the EUI-64 form.
    $GREP -qiE "^BASE MAC: *${dut_lc}[[:space:]]*$" "$IDENT_LOG" \
        || die "BASE MAC does not match. Nothing written.
  expected $DUT_MAC (is a different board on this port?)"
    #  Full chip string, fixed-string: "ESP32-C6" alone would also accept ESP32-C61.
    $GREP -E "^Chip type:" "$IDENT_LOG" | $GREP -qF "$DUT_CHIP" \
        || die "chip type is not \"$DUT_CHIP\". Nothing written."
    $GREP -q "^Detected flash size: 4MB" "$IDENT_LOG" \
        || die "detected flash size is not 4MB. Nothing written."
    say "gate OK: BASE MAC $DUT_MAC / $DUT_CHIP / 4MB"
    if [ "$DRYRUN" = "1" ]; then
        print_write_plan
        say "DRYRUN=1: stopping here (nothing written, nothing captured). The DUT is LEFT IN THE ROM DOWNLOAD MODE (esptool --after no-reset); the next write, a hard reset or a power cycle recovers it."
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
C6_FILES+=("$SHA_TXT")
{
    echo "# $(date '+%F %T')  DRYRUN=$DRYRUN NOFLASH=$NOFLASH COLD=$COLD NORESET=$NORESET  DUT_MAC=$DUT_MAC"
} > "$SHA_TXT"
if [ "$NOFLASH" != "1" ]; then
    say "==== 2. write (bootloader 0x0 / ptable 0x8000 / boot_app0 0xe000 / app 0x10000) ===="
    print_write_plan
    C6_FILES+=("$FLASH_LOG")
    if ! "${FLASH_CMD[@]}" >"$FLASH_LOG" 2>&1; then
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
    {
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
    C6_FILES+=("$COLD_TXT")
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
    _deadline=$(( $(date +%s) + COLD_WAIT_SEC ))
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
C6_FILES+=("$OUT")
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

#  ---------------------------------------------------------------- 7. markers
_cnt() { $GREP -acE "$1" "$OUT" || true; }
n_banner="$(_cnt 'TOPPERS/FMP3 Kernel Release')"
n_setup="$(_cnt '\[Arduino\] setup complete')"
n_heart="$(_cnt '\[Arduino\] loop heartbeat')"
n_unexp="$(_cnt '## Unexpected|## Assertion|## Internal|Unregistered (exception|interrupt)|mcause ?=')"
n_smark="$(_cnt '^S'$'\r''*$')"
n_blink="$(_cnt '\[Blink\] (ON|OFF)')"
say "markers: banner=$n_banner setup=$n_setup heartbeat=$n_heart unexpected=$n_unexp smark=$n_smark blink=$n_blink"
echo "markers: banner=$n_banner setup=$n_setup heartbeat=$n_heart unexpected=$n_unexp smark=$n_smark blink=$n_blink" >> "$SHA_TXT"
say "record -> $SHA_TXT"
exit 0
