# BT Classic SPP for M5Stack Basic

Design for exposing Bluetooth Classic SPP to Arduino sketches on the
M5Stack Basic (ESP32, Xtensa LX6) board of this package.

Status: implemented and verified on hardware, 2026-09-02.

One decision changed on the way. This document assumed the M5Stack core's
prebuilt libbt.a would be linked ("The archives exist in the core we already
depend on"). It was, and it does not work: that archive is compiled with
CONFIG_BTDM_CTRL_HLI=y and FMP3's vector table has no level-4 interrupt.
BlueDroid is built from source instead - third_party/bluedroid, the same
ESP-IDF commit the core was built from - which is what upstream does and what
had already been proven on this board. See third_party/bluedroid/README.vendored.md.

## Scope

A sketch can start an SPP server, wait for a phone or PC to connect, and
exchange bytes with it.

Not in scope, and each for a reason:

* **The CoreS3.** ESP32-S3 has no Bluetooth Classic at all - only BLE.
  Upstream refuses the combination outright (`A1_VARIANT=btclassic` raises
  `FATAL_ERROR` unless `A1_CHIP=esp32`), and so will this. That makes
  `bt-classic` the first profile that exists on one board and not the other.
* **Client (master) role.** Upstream has no working example of it, so it would
  be new ground on top of new ground.
* **Device discovery (inquiry).** A "scan for nearby devices" API is a second
  GAP surface; the server path does not need it.
* **BLE.** Different stack, different question.
* **Combining BT with M5Unified or Wi-Fi.** Upstream needed separate linker
  scripts for the combinations; BT stands alone here to begin with.

## What the investigation established

These are facts checked in the trees and headers on 2026-09-02, not
assumptions. They shape the design, so they are recorded with their evidence.

### There is no Arduino-facing Bluetooth anywhere upstream

Searching `arduino/` in both the public snapshot and the development tree for
anything Bluetooth returns nothing. Upstream's BT Classic exists as standalone
FMP3 applications - `esp/app/bt_classic_spp_smoke.c` and friends - that call
the ESP-IDF APIs directly from a `main_task`.

This is the difference from the Wi-Fi work. There, the layer that makes the
capability usable from a sketch already existed in this repository:
`wifi/adapter/*` is 944 lines, `src/ToppersFMP3_WiFi.{h,cpp}` another 122, and
the examples 85. The equivalent for BT has to be written.

### This port has no Stream and no Print

The M5Stack core's `core.a` is not linked, so `Serial`, `Print`, `Stream` and
`delay()` do not exist here - a fact this repository already documents and
that broke the LibraryInfo example once. A `BluetoothSerial` that inherits
`Stream`, which is what an Arduino user would expect, cannot be built. The API
therefore mirrors the Wi-Fi one: C functions with a thin C++ wrapper whose
methods are named like Stream's without being one.

### The interrupt lines do not collide

The BT controller blob takes CPU interrupt lines 5, 7 and 8 dynamically. On
LX6 the console sits on line 18 and the Arduino GPIO interrupt on 17.
`target_serial.h` records why: the console was moved off line 5 on 2026-07-15
to avoid exactly this, and the move was checked on hardware as far as
`esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)`.

`arduino_interrupt.c` already fails the build if its line collides with the
tick, the IPI, the console or the Wi-Fi shim's lines. That list will need the
BT lines added when this profile exists.

### The BT profile must not use the full-DRAM linker script

The controller needs its exchange-memory region, 0x3FFB0000 upward, about
56KB. Every profile today uses `esp32_xip_m5_wifi.ld`, which starts DRAM at
0x3FFB0000 precisely because nothing here uses BT. `bt-classic` branches to
`esp32_xip_btc.ld`, the BlueDroid-capable script already carried in the target
directory and referenced by nothing.

### The archives exist in the core we already depend on

`libbt.a` in `lib/` and `libbtdm_app.a` in `ld/` of `esp32-libs/3.3.8`. No new
download, and no equivalent of the WPA supplicant problem where the shipped
archive had to be rebuilt.

## Architecture

Four layers, bottom to top. The bottom one is imported; the rest is new.

| Layer | Contents | Origin |
| --- | --- | --- |
| `bt/` | controller HAL, BlueDroid glue, shim, its own sdkconfig override, libc overrides | imported from upstream `esp/bt/`, 35 files / 7,418 lines |
| `bt/adapter/toppers_bt_spp.c` | bring-up sequence, GAP and SPP callbacks, receive ring, connection state | new |
| `src/ToppersFMP3_BT.{h,cpp}` | the sketch-facing wrapper | new |
| `examples/BluetoothSPP/` | echo example | new |

The imported layer is taken from the public repository
`toppers/fmp3_esp_idf`, the same provenance the rest of the port records, and
gets the same treatment the LX6 core import got: comments referring to
unpublished working notes are trimmed, and files that name the wrong chip are
corrected.

## The sketch-facing API

```c
/*  ToppersFMP3_BT.h  */
bool     toppersBtBegin(const char *device_name);
void     toppersBtEnd(void);
bool     toppersBtConnected(void);
int      toppersBtAvailable(void);
int      toppersBtRead(void);                       /* -1 when empty */
size_t   toppersBtReadBytes(uint8_t *buf, size_t len);
size_t   toppersBtWrite(const uint8_t *buf, size_t len);
```

and a wrapper object with `begin`, `end`, `connected`, `available`, `read`,
`readBytes` and `write`, so a sketch reads the way an Arduino sketch does
without claiming to be a `Stream`.

`begin()` returns false rather than blocking forever if the controller or
BlueDroid does not come up; the reason goes to the kernel log.

## Concurrency

SPP callbacks run in the shim's task context. The sketch runs in the Arduino
task. Received bytes therefore go into a ring buffer written by the callback
and read by the sketch, guarded the way the Wi-Fi adapter guards its own
state - `esp_shim_int_disable()` / `esp_shim_int_restore()` around the short
critical sections.

`write()` calls `esp_spp_write()` from the sketch's context and reports how
many bytes it accepted. `ESP_SPP_CONG_EVT` sets a congestion flag that makes
`write()` return 0 rather than queue without bound.

The ring is fixed size, allocated statically. A full ring drops the newest
bytes and counts the drops; the count is readable, so a sketch that is too
slow can find out rather than silently lose data.

## Pairing

The upstream policy, unchanged: Secure Simple Pairing confirmations are
accepted automatically, and legacy pairing replies with PIN `1234`. This is
the behaviour that has been exercised on hardware.

**It is not secure. Any device in range can pair and connect.** The README,
the release README and the example all have to say so. Making the policy a
sketch's choice is a later question; it changes the API and the verification,
and doing it now would mean designing it before the plain path is known to
work.

## Profile and packaging

* `build_prebuilt_stages.py`: `bt-classic` added to the profile table, with
  its application, and refused for any chip but `esp32`.
* Linker script: `esp32_xip_btc.ld`, branched in `CMakeLists.txt` where the
  comment already anticipates it.
* Link group: `-lbt` from `lib/` and `libbtdm_app.a` from `ld/`.
* `install_platform.py`: one entry added to `MENU_ENTRIES`. Nothing else -
  the board lines already emit only the entries whose stage exists, so the
  menu item appears on the M5Core board and not on the CoreS3.
  `EXPECTED_PROFILES["esp32"]` gains it.
* `verify_package.py`: its profile matrix is shared by both boards today and
  has to become per-board, since one profile now exists on one board only.

## How it will be verified

1. The stage builds for `esp32`, and building it for `esp32s3` is refused.
2. On the board: the controller initialises, BlueDroid enables, the SPP server
   starts, and the device becomes discoverable under the name the sketch gave.
3. **A phone or PC pairs with it and exchanges bytes with the echo example.**
4. The CoreS3 stages stay byte-identical to the released package apart from
   the differences already recorded, and the CoreS3 board still shows no
   Bluetooth menu entry.
5. `verify_package.py` builds every profile on the board that offers it.

Step 3 cannot be done from this machine. Pairing and sending bytes needs a
Bluetooth host in the room, so "it is discoverable", "it connected" and "the
echo came back" have to be confirmed by a person with the board in front of
them.

## Risks

* **BlueDroid is big.** Flash and DRAM have to be measured before anything is
  claimed. If it does not fit alongside the FMP3 runtime in the BT-only
  profile, the design changes rather than the numbers.
* **Upstream's hardware evidence stops earlier than this design needs.** The
  controller reaching `esp_bt_controller_enable` was checked on an M5Stick.
  Whether the SPP application has ever run on an M5Stack Basic is not recorded
  anywhere found.
* **The libc overrides under `classic/libc_override/` may collide** with the
  stub headers this port already carries for the Wi-Fi shim. They are on the
  include path only for BT translation units, but that has to be arranged
  rather than assumed.
* **The callback context is not documented upstream.** If SPP callbacks turn
  out to run somewhere that cannot take the shim's lock, the ring buffer
  design changes.

## Milestones

1. Import `bt/`, add the profile, the stage builds. Nothing runs yet.
2. An equivalent of the upstream smoke path runs on the board: controller,
   BlueDroid, SPP server, discoverable.
3. The adapter, the wrapper and the example; echo works from a phone.
4. Packaging: menu entry on the one board, per-board verification, README and
   release README, including what pairing does not protect.
