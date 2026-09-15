# `ports/m5stack_riscv/runtime/` の出典と改変（ESP32-C6 / M5NanoC6）

`THIRD_PARTY_NOTICES.md`「取り込むときはヘッダ保持・正確な source commit を記録・
改変境界を記す」に従う記録。`packaging/release-allowlist.json` の
`portBaseRepositoryC6` / `portBaseCommitC6` と同じ出典である。

- 出典: `https://github.com/exshonda/fmp3_esp_idf_dev.git`（非公開の開発リポジトリ）
  commit **`c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe`**（2026-09-15）。公開スナップショット
  `toppers/fmp3_esp_idf` `fdd89f8` には C6 が含まれないため、Xtensa 側の
  `wifi/prebuilt/wpa2` と同じく開発リポジトリを直接の出典として記録する。
- 取込み: 2026-09-15（arduino_esp32 段1 Task 2）。`git archive c7fef18 <path>` で
  当該 commit の内容をそのまま写した（作業ツリーではなく commit の内容）。
- 出典側との照合コマンド（改変「なし」の行はこれで 0 差分になる）:
  `git -C <dev repo> show c7fef18:<dev path> | cmp - <arduino path>`
- 出典側にある `arch/riscv_gcc/esp32c6/IMPORT_PROVENANCE.md` と
  `target/m5nanoc6_gcc/IMPORT_PROVENANCE.md` は、さらに上流（asp3_esp_idf）から
  dev への出典と改変の記録で、そのまま同梱している（本ファイルは dev から
  arduino への段だけを扱う）。
- fmp3_core は arduino_esp32 の既存 submodule `685b36a`（dev と同じ commit）を使い、
  無改変。chip.cmake が共通 `arch/riscv_gcc/common/start.S` を `chip_start.S` へ
  差し替える仕組みもそのまま。

## 改変の方針

改変は次の 5 種（段1）＋ 段3 の 2 種 ＋ 段4 の 1 種 ＋ 段5 の 1 種 ＋ C5 段3 の 1 種だけで、
いずれも下表に理由を書く（3 と 5 は段1 Task 2 の逸脱として reviewer が受理したもの。
`docs/c6-port.md`「逸脱の受理」。6 と 7 は段3 Task 1 の逸脱で、`docs/c6-port.md` 段3 節に記録する。
8 は段4 Task 0、9 は段5 Task 2 の R12 例外。10 は C5 計画 段3 Task 1 の 9 の C5 版）。

1. **SDK パスの写像**（`target.cmake`）: dev は esp-idf submodule の
   `components/<comp>/...` を include / link するが、arduino_esp32 は「ESP-IDF を
   複製しない」（`BUILDING.md`）ので、M5Stack core の `esp32c6-libs/3.3.8/include/<comp>/...`
   と `ld/` へ 1 対 1 で写す（同じ ESP-IDF v5.5.4）。
2. **C++ 静的初期化の表**（`esp32c6_xip.ld`）: Arduino の橋
   （`src/bridge/ArduinoSketchBridge.cpp`）が `esp_run_init_array()` を呼ぶため、
   dev の ld に無い `__init_array_*` / `__ctors_*`（と `__fini_array_*` / `__dtors_*`）を
   足す（計画 S1-5）。
3. **unwind 表の入力規則**（`esp32c6_xip.ld`）: スケッチ側 .o は M5Stack core の
   `cpp_flags`（`-fexceptions`）で建つため `.eh_frame` / `.eh_frame_hdr` /
   `.gcc_except_table` を持ち込む。入力規則が無いと orphan 出力セクションになるので
   `.flash.rodata` 内に置く（Xtensa の `esp32s3_xip_m5.ld` と同型）。
4. **`TA_FPU` の除去**（`app/phase3/*.cfg`、Xtensa 側アプリからの派生）: riscv の
   fmp3_core に `TA_FPU` の定義が無い（計画 S1-4）。
5. **`TOPPERS_XIP_PADDR_PROBE` ブロックの削除**（`app/phase3/phase3_arduino_app.c`、
   Xtensa 側アプリからの派生）: Xtensa の `flash_cache_init.c` の診断で、固定 VMA の
   C6 には対象が無い（死コード）。
6. **esp-idf 原本の同梱（D8）**（`runtime/wifi/idf_src/`、`runtime/wifi/net/lwip_contrib_include/`）:
   `BUILDING.md`「ESP-IDF を複製しない」からの逸脱。dev の C6 Wi-Fi 構成が esp-idf submodule
   から直接コンパイルする 6 本（`.c`）と、`netif_esp32s3.c` が include する lwIP contrib の
   ヘッダ 3 本（M5Stack core の SDK には含まれない）。内容は無改変、Apache-2.0 / BSD-3 の
   ヘッダを保持。**理由**: M5Stack core の `esp32c6-libs` が持つ `.a`（`libesp_hw_support.a`／
   `libhal.a`）のこの 6 本に対応するメンバは `vPortEnterCritical`／`vPortExitCritical`／
   `xPortInIsrContext`（FreeRTOS のクリティカルセクション API）を未解決参照として要求するが、
   FMP3 はこれらを提供しない。そのため `.a` のメンバをそのままリンクする経路は使えず、
   dev はこの 6 本をソースから FreeRTOS スタブ（`esp/bt/stub/include`）に対してリンクしている
   （出典: 開発リポジトリ `.steering/20260915-c6-arduino-plan/INVESTIGATION.md` 2-3 節）。
   **段5 の判断 S5-2（2026-09-15）: 維持** -- core の `.a` メンバ + `vPort*` シム案は未検証で、
   段4 の実機実績は vendored 版のもの。**再評価の条件**は、(1) 「core の `.a` メンバ + `vPort*`
   シム」経路を実際に検証する、または (2) 固定中の M5Stack core バージョンが動く、のどちらか
   （`BUILDING.md` の例外条項）。なお 9（下記）の既定 OFF では lwIP contrib ヘッダ 3 本は
   どの TU からも include されない（ON のときだけ使う）が、同梱は維持する。
7. **ファイル名の変更のみ**（`idf_src/efuse_hal_esp32c6.c`）: esp-idf の `hal/efuse_hal.c` と
   `hal/esp32c6/efuse_hal.c` は同名で、stage は全オブジェクトを 1 つのディレクトリに
   basename で置く（`prebuilt_stage_c6.cmake` が衝突を fatal にする）ため、チップ側を改名した。
   中身はバイト同一。
8. **`lwipopts.h` の DNS 化（段4 Task 0、2026-09-15。R12 の例外）**
   （`runtime/wifi/net/port/include/lwipopts.h`、`runtime/wifi/prebuilt/lwip/esp32c6/liblwip.a`）:
   dev 由来ファイルはバイト同一で持ち込むという方針（R12）からの逸脱。`LWIP_DNS 0 -> 1`
   （名前解決 = `hostByName` のため）、それに伴い `MEMP_NUM_SYS_TIMEOUT 8 -> 9`（`dns_tmr` が
   周期タイマを 1 本足す。`lwip_num_cyclic_timers` 6 -> 7 を実測。8 のままだと ping 鎖と合わせて
   プールが満杯 = 次の `sys_timeout()` が assert で tcpip_thread を止める）、`#ifndef ERANGE`
   ガードつきの `ERANGE 34`（`LWIP_DNS 1` で初めてコンパイルされる `netdb.c` の
   `lwip_gethostbyname_r()` が参照するが、hal_stub の flat `errno.h` に無い。dev の台本の include
   path は本リポジトリから変えられないので、両方が読む唯一のファイルから供給する）。それ以外は
   dev と同一。`liblwip.a` は dev の台本 `build_lwip_lib_espidf_esp32c6.sh` を **`PORT_EXTRA`=
   本リポジトリの `net/port/include`、`OUT_DIR`=scratch** で走らせた生成物（dev の
   `esp/lib/` と golden は不変。同日、既定引数の生成物が dev golden とバイト同一であることを
   sha256 で確認）。`lwipopts.h` は `liblwip.a` と stage の TU の両方を決めるので、
   **`lwipopts.h`・`liblwip.a`・`prebuilt/lwip/README.md` は同じ commit で動かす**
   （`docs/c6-port.md` 段3「段4 の入口条件」DNS）。
9. **dev 診断フックの `TOPPERS_C6_NET_DIAG` 化（段5 Task 2、2026-09-15、判断 S5-3。R12 の例外）**
   （`runtime/wifi/net/netif_esp32s3.c`）: dev のデモ用フック -- DHCP bound 直後のゲートウェイ
   ping 鎖（`netif_esp32s3_ping_gateway` / `ping_init`、`net_ping_result`、`ping_stop` の no-op）、
   `tcpip_init_done` の TCP/UDP echo サーバ（ポート 7、`tcpecho_raw_init` / `udpecho_raw_init`）、
   `net: DHCP bound ip=%s gw=%s` 行のアドレス部 -- を `#if TOPPERS_C6_NET_DIAG` で包んだ
   （contrib ヘッダ 3 本の `#include` も同じ条件）。OFF（既定）では `net: DHCP bound` だけを
   出す（`scripts/capture_c6_usj.sh` の `dhcp` マーカーは接頭辞一致なので両方に一致する）。
   理由: 出荷するランタイムが利用者に無断で待受けソケットを開き ICMP を送ってはならない。
   呼出し口を adapter 側で選べる形（`netif_esp32s3_start()` の引数等）は vendored ファイルに
   無く（ping と echo は `netif_status_cb` / `tcpip_init_done` の内部で無条件）、最小改変は
   ファイル側の `#if` になった。`netif_esp32s3.h` の `netif_esp32s3_ping_gateway` 宣言は残す
   （OFF で呼べばリンクエラー = fail-closed）。それ以外の行は dev と同一。CMake option は
   `runtime/CMakeLists.txt` の `TOPPERS_C6_NET_DIAG`（ON で `-DTOPPERS_C6_NET_DIAG=1`）。
   段4 の実機記録（`ping gateway -> OK` の回数等）は ON 相当で採ったもの。
10. **9 の C5 版: `TOPPERS_C5_NET_DIAG` の alias（C5 計画 段3 Task 1、2026-09-16、A11）**
   （`runtime/wifi/net/netif_esp32s3.c`）: 9 のブロックが読む `TOPPERS_C6_NET_DIAG` に、C5 の
   option 名 `TOPPERS_C5_NET_DIAG` を `#if defined(TOPPERS_ESP32C5) && defined(TOPPERS_C5_NET_DIAG)
   && !defined(TOPPERS_C6_NET_DIAG)` で定義する 3 行（+ コメント）を 9 の既定値定義の前に足した。
   9 のブロックはそのまま（C5 のためにブロックの条件を書き直さない = C6 の前処理結果が不変で
   X-check MATCH）。CMake option は `runtime/CMakeLists.txt` の `TOPPERS_C5_NET_DIAG`（既定 OFF、
   ON で `-DTOPPERS_C5_NET_DIAG=1`）。

## ファイル一覧

### chip 層 `runtime/arch/riscv_gcc/esp32c6/`（dev `fmp3/arch/riscv_gcc/esp32c6/`、23 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `fmp3/arch/riscv_gcc/esp32c6/IMPORT_PROVENANCE.md` | `runtime/arch/riscv_gcc/esp32c6/IMPORT_PROVENANCE.md` | なし | asp3 -> dev の記録。同梱 |
| `fmp3/arch/riscv_gcc/esp32c6/chip.cmake` | 同名 | なし | `-march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany --specs=nano.specs`、`chip_start.S` 差替え、コンソール選択（usbjtag）はそのまま |
| `chip_asm.inc` `chip_kernel.h` `chip_kernel.py` `chip_kernel_impl.c` `chip_kernel_impl.h` `chip_rename.def` `chip_rename.h` `chip_serial.c` `chip_serial.cfg` `chip_serial.h` `chip_sil.h` `chip_start.S` `chip_stddef.h` `chip_support.S` `chip_unrename.h` `esp32c6.h` `esp32c6_uart.c` `esp32c6_uart.h` `esp32c6_usbjtag.c` `esp32c6_usbjtag.h` `intmtx_kernel_impl.h`（計 21 本） | 同名 | なし | バイト同一（`cmp` で確認） |

### target 層 `runtime/target/m5nanoc6_gcc/`（dev `fmp3/target/m5nanoc6_gcc/` から `app/` を除く 30 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `fmp3/target/m5nanoc6_gcc/target.cmake` | `runtime/target/m5nanoc6_gcc/target.cmake` | **あり** | (a) `ESP_SUP_DIR`（esp-idf submodule）の参照を `ARDUINO_SDK_INCLUDE_ROOT` / `ARDUINO_SDK_LD_ROOT`（M5Stack core の `esp32c6-libs/3.3.8/{include,ld}`）へ写像: include 10 本は `components/X` -> `include/X`、`-L components/soc/esp32c6/ld` -> `-L <ld>`、ROM ld 2 本は `<ld>/esp32c6.rom.ld` / `esp32c6.rom.api.ld`。(b) esp-idf submodule の存在検査を SDK の存在検査（`hal/esp32c6/include/hal/systimer_ll.h`、`esp32c6.rom.ld`）に置換。(c) `esp/config/esp32c6` の参照を本 runtime の `config/esp32c6` へ。(d) 冒頭に改変の説明を英語で追記。`FMP3_TARGET_C_FILES`・cfg/py の列挙・`FMP3_LINK_OPTIONS`（`--gc-sections`、`--build-id=none`、`--undefined=_kernel_*_table` 4 本）・`FMP3_CFG1_OUT_LINK_OPTIONS` は不変 |
| `fmp3/target/m5nanoc6_gcc/esp32c6_xip.ld` | `runtime/target/m5nanoc6_gcc/esp32c6_xip.ld` | **あり** | `.flash.rodata` 出力セクションの末尾に (a) `__init_array_start/end`・`__fini_array_start/end`・`__ctors_start/end`・`__dtors_start/end` と対応する `KEEP(*(...))` 入力規則、(b) `.eh_frame` / `.eh_frame_hdr` / `.gcc_except_table` の入力規則、を追加（計画 S1-5）。(a) は `src/bridge/ArduinoSketchBridge.cpp` -> `seam/init_array.cpp` が走査する表で、dev の C6 像は C のみのため存在しなかった。(b) はスケッチ側 .o が M5Stack core の `cpp_flags`（`-fexceptions`）で建つため持ち込む unwind 表で、入力規則が無いと orphan 出力セクションになる。**別の出力セクションを作らず `.flash.rodata` の中に置く**のは、bootloader が受理する flash 写像セグメントをちょうど 2 本に保ち（ドライバの検査 C-1）、`install_platform.py` の size regex が知る `.text` / `.flash.appdesc` / `.flash.rodata` の名前を増やさないため。MEMORY・`.text`・`.flash_rodata_dummy`・`.flash.appdesc`・`.data`・`.bss`・`.tbss` は不変。実測: ドライバでスケッチ 1 本を smoke リンクして `mapped=2 ram=1 pad=1`、C-1..C-8 すべて成立（`.steering/20260915-c6-arduino-plan/stage1/logs/task2-smoke-link.txt`） |
| `IMPORT_PROVENANCE.md` `diag_recorder.c` `diag_recorder.h` `esp32c6.ld` `esp32c6_usbjtag_hal.c` `target_asm.inc` `target_cfg1_out.h` `target_check.py` `target_class.py` `target_hrt64.c` `target_ipi.h` `target_kernel.cfg` `target_kernel.h` `target_kernel_impl.c` `target_kernel_impl.h` `target_kernel.py` `target_rename.def` `target_rename.h` `target_serial.cfg` `target_serial.h` `target_sil.h` `target_stddef.h` `target_syssvc.h` `target_test.h` `target_timer.c` `target_timer.cfg` `target_timer.h` `target_unrename.h`（計 28 本） | 同名 | なし | バイト同一。`esp32c6.ld`（Direct Boot 用）・`diag_recorder.*`・`target_hrt64.c` は minimal では使わない（後 2 者は dev の Wi-Fi 構成が `fmp` へ足すもので、段3 の wifi-connect で使う）が、target 層を丸ごと写す方針で同梱 |
| `fmp3/target/m5nanoc6_gcc/app/**`（`fmp_app`、`usj_probe`） | （持ち込まない） | - | dev の hello / 計測用アプリ。arduino のアプリは `ports/m5stack_riscv/app/phase3` |

### seam `runtime/seam/`（dev `esp/boot/`、4 本 + arduino Xtensa port から 1 本 + 新規 1 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/boot/seam_c6_appdesc.c` | `runtime/seam/seam_c6_appdesc.c` | なし | `.appdesc` セクションの esp_app_desc スタブ |
| `esp/boot/seam_c6_entry.S` | `runtime/seam/seam_c6_entry.S` | なし | 像のエントリ（`SEAM_C6_ENTRY_MARK`、`SEAM_C6_CLK_BOOST` は CMake が渡す） |
| `esp/boot/seam_c6_clk.c` | `runtime/seam/seam_c6_clk.c` | なし | 80 -> 160 MHz 昇圧 |
| `esp/boot/seam_c6_clk.h` | `runtime/seam/seam_c6_clk.h` | なし | 同上のヘッダ |
| （arduino_esp32 `ports/m5stack_xtensa/runtime/seam/init_array.cpp`） | `runtime/seam/init_array.cpp` | なし（複製） | dev 由来ではない。`esp_run_init_array()` の実体でチップ非依存。Xtensa port のファイルを参照せず複製したのは、Xtensa 側の変更が C6 の stage を黙って変えないようにするため |
| （新規） | `runtime/seam/newlib_syscalls.c` | -（新規） | dev 由来ではない。newlib-nano の `__stack_chk_fail` 経路が要求する `_exit` / `_kill` / `_getpid` / `_write` / `__getreent` の実体。M5Stack core はスケッチを `-fstack-protector` で建てるため、ローカル配列を持つスケッチはこの 5 本が無いとリンクできない（段1 Task 3 で実測、Blink / LibraryInfo / TwoFileSketch は保護フレームを持たず無くてもリンクする）。型は Xtensa port の `arch/xtensa_gcc/esp32s3/chip_rom_libc.c` の `_exit` / `_kill` / `_getpid`（2026-08-22 に同じ経路で追加）。ファイルを共有しないのは `init_array.cpp` と同じ理由。fix wave 1: `_write` は fd 1/2 以外で `errno = EBADF` を立てて `-1` |

### config `runtime/config/esp32c6/`（dev `esp/config/esp32c6/`、2 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/config/esp32c6/sdkconfig.h` | `runtime/config/esp32c6/sdkconfig.h` | なし | hal ヘッダが読む `CONFIG_*`（Espressif Apache-2.0 ヘッダ保持） |
| `esp/config/esp32c6/hal_stub_include/nuttx/config.h` | `runtime/config/esp32c6/hal_stub_include/nuttx/config.h` | なし | `sdkconfig.h` が無条件に include する最小スタブ |

### アプリ `ports/m5stack_riscv/app/phase3/`（arduino Xtensa port からの派生、dev 由来ではない）

| 元 | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `ports/m5stack_xtensa/app/phase3/phase3_arduino_app.cfg` | `app/phase3/phase3_arduino_app.cfg` | **あり** | `TA_ACT \| TA_FPU` -> `TA_ACT`（riscv の fmp3_core に `TA_FPU` は無い、S1-4）。`CLASS(CLS_PRC1)` は `target_class.py` の単一コアのクラスで不変。`#ifdef` で切り替えず別ファイルにする（`BUILDING.md`） |
| `ports/m5stack_xtensa/app/phase3/phase3_arduino_app.c` | `app/phase3/phase3_arduino_app.c` | **あり** | `TOPPERS_XIP_PADDR_PROBE` ブロック（Xtensa の `flash_cache_init.c` が SRAM へ退避した PADDR を印字する診断）を削除。C6 は固定 VMA で PADDR 解決を持たないため対象が無い |
| `ports/m5stack_xtensa/app/phase3/phase3_arduino_app.h` | `app/phase3/phase3_arduino_app.h` | なし | バイト同一 |

### 本 port で新規に書いたもの（出典なし）

- `runtime/CMakeLists.txt` -- dev `cmake/a1_c6_stage1.cmake` の seam-c6-min 相当の消費側部分を、
  Xtensa の `ports/m5stack_xtensa/runtime/CMakeLists.txt` の型（`FMP3_LIBRARY_ONLY=ON`、
  `seam_objects` / `seam_start` / `seam_cxx`、`fmp3_prebuilt`）で書き直したもの。
  dev から持ち込んだ build 事実: `CORE_CLK_MHZ`（既定 160 = S1-6）、`SEAM_C6_CLK_BOOST`、
  `SEAM_C6_ENTRY_MARK=0x53`、`FMP3_PRC_NUM=1`、cfg1_out に xip ld を使う（`A1_C6_LDSCRIPT`）、
  ROM ld は profile 別の表 `A1_ROM_LDS_<profile>` で、minimal は target.cmake と同じ 2 本
  （`esp32c6.rom.ld` / `esp32c6.rom.api.ld`）。Task 2/3 では `a1_c6_stage1.cmake` の Wi-Fi 構成の
  11 本も足した 13 本だったが、`rom.libc` / `rom.newlib` 等の素の代入が `libc_nano.a` に黙って
  勝ち、ROM の newlib が未初期化の `syscall_table_ptr` / `_global_impure_ptr` を辿る経路を
  作るため fix wave 1 で 2 本に戻した（`docs/c6-port.md`「ROM linker script の勝者」）。
  wifi-connect の表は段3 で libc 供給の決定と一緒に書く。
- `runtime/cmake/toolchain-riscv-esp32c6.cmake` -- dev `cmake/toolchain-riscv-esp32c6.cmake` と
  同じ形（`riscv32-esp-elf-` 接頭辞、`esp-14.2.0_20260121` の版固定）だが、
  ツールチェーンの場所は書かない（`build_prebuilt_stages.py` が M5Stack core の
  `esp-rv32/2601` を PATH 先頭に置く）。
- `runtime/cmake/prebuilt_stage_c6.cmake` -- Xtensa の `prebuilt_stage.cmake` の C6 版
  （`flash_cache_init.o` 無し、manifest schema 2 / `paddrMode fixed-vma`、`flashSize 4MB`）。

## 段3 Task 1（2026-09-15）: `runtime/wifi/` -- wifi-connect の shim / hal / stub / net / config / prebuilt

dev `c7fef18` の C6 Wi-Fi 構成（`cmake/a1_c6_stage1.cmake` の `if(A1_C6_WIFI)` ブロック）が
`fmp` にリンクする集合を、dev `build/c6-wifi/build.ninja`（コンパイルされる 31 TU）と
`ninja -t deps`（各 TU が実際に include したリポジトリ内ヘッダ）から機械列挙して写した。
**esp/shim は Xtensa port（`ports/m5stack_xtensa/runtime/wifi/shim`、公開版由来の fork）とは
別系統の第 2 コピー**で、Xtensa 側は無改変（R12。以後の shim 修正は 2 系統に分かれる）。
dev 由来 82 本（ソース・ヘッダ 78 + `.a` 4）はすべてバイト同一
（照合: 上記 `git show c7fef18:<dev path> | cmp - <arduino path>`、`.a` は dev 作業ツリーの
`esp/lib/` と `cmp`、sha256 は dev `.steering/20260913-c6-stage4/README.md` の fix round 後の表と一致）。

### shim `runtime/wifi/shim/`（dev `esp/shim/`、26 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/shim/esp_shim.c` `esp_shim_mtx.c` `esp_shim_sem.c` `esp_shim_tsk.c` `esp_wifi_adapter.c` `esp_shim_libc.c` `esp_shim_blobglue.c` `esp_event_shim.c` `esp_timer_shim.c` `wifi_stubs.c` `esp_coex_adapter.c` `esp_shim_isr_ctx.c` `esp_shim_ring.c` `esp_shim_intr_intmtx.c` `esp_shim_bt_crit_wifi.c`（15 本、dev が `fmp` へ `target_sources` する集合そのまま） | `runtime/wifi/shim/` 同名 | なし | `wifi_objects`（`runtime/CMakeLists.txt`）がこの順でコンパイルする。C6 分岐は `#if defined(TOPPERS_ESP32C6)`（`chip_stddef.h` が定義） |
| `esp/shim/esp_shim.cfg` `esp_shim_intr_intmtx.cfg` | 同名 | なし | 前者はアプリ cfg が `INCLUDE("esp_shim.cfg")` する（`#ifndef TOPPERS_ESP32C6` で S3/LX6 の線 0-3/23/27 を外す。BUILDING.md の「cfg を `#ifdef` で切らない」との関係は段3 R5 で実証する）。後者は `FMP3_CFG_FILES` に CMake が足す（線 1..15 の `CFG_INT`/`DEF_INH`） |
| `esp/shim/esp_shim.h` `esp_shim_cfg.h` `esp_shim_isr_ctx.h` `esp_shim_ring.h` `esp_shim_intr_intmtx.h` `esp_shim_intr_intmtx_lines.h` | 同名 | なし | dev の 31 TU が実際に include する 6 本（deps で確認） |
| `esp/shim/esp_shim_public.h` `esp_shim_xcore_crit.h` | 同名 | なし | C6 の TU は include しない（前者は外部コンシューマ向け集約ヘッダで Task 2 の adapter が使う候補、後者は `esp_shim_bt_crit_wifi.c` の `TNUM_PRCID >= 2` 分岐だけが include）。テキスト上の参照先を欠かさないために同梱 |
| `esp/shim/IMPORT_PROVENANCE_c6.md` | 同名 | なし | asp3_esp_idf -> dev の C6 分岐の出典と改変の記録。同梱 |
| `esp/shim/esp_shim_apll_stub_lx6.c` `esp_shim_intr.c` `esp_shim_intr_clic.*` `esp_shim_intr_lines.h` `esp_shim_audio_names.h` `m5_idf_containerof.h` `app/**` | （持ち込まない） | - | Xtensa（LX6/S3）・P4・M5 専用で C6 の build.ninja に無い |

### hal `runtime/wifi/hal_src/`（dev `esp/wifi/hal_src/`、6 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/wifi/hal_src/phy_common.c` `phy_init.c` `esp_wifi_regulatory.c` `wifi_init.c` `wifi_lib_printf.c` `phy_lib_printf.c` | 同名 | なし | dev の C6 構成が使う 6 本 |
| `esp/wifi/hal_src/periph_ctrl.c` `phy_init_data.c` `esp_sha_hwcrypto_glue.c` | （持ち込まない） | - | 前 2 者は S3/LX6 改造版で C6 は esp-idf 原本を使う（下記 idf_src）。後者は S3 の HW crypto 検証用 |

### esp-idf 原本 `runtime/wifi/idf_src/`（D8 の逸脱。esp-idf v5.5.4 `735507283d`、6 本、Apache-2.0）

dev はこれらを esp-idf submodule から直接 `target_sources` する。本リポジトリは submodule を
持たず「ESP-IDF を複製しない」（`BUILDING.md`）が原則なので、**逸脱として**同梱する（D8、段5 で
再評価）。出典は dev の submodule（`git -C esp-idf show HEAD:<path>` = v5.5.4 タグ）、内容は
無改変（`efuse_hal_esp32c6.c` はファイル名のみ変更、改変方針 7）。

| esp-idf のパス（`components/`） | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp_hw_support/periph_ctrl.c` | `runtime/wifi/idf_src/periph_ctrl.c` | なし | `periph_module_enable` 等。`shared_periph_module_t=periph_module_t` の `-D` を要する |
| `esp_hw_support/modem_clock.c` | `runtime/wifi/idf_src/modem_clock.c` | なし | modem クロック（`freertos/FreeRTOS.h` スタブの `portENTER_CRITICAL_SAFE` を使う） |
| `hal/esp32c6/modem_clock_hal.c` | `runtime/wifi/idf_src/modem_clock_hal.c` | なし | 同上の hal |
| `hal/efuse_hal.c` | `runtime/wifi/idf_src/efuse_hal.c` | なし | efuse 読出し（MAC） |
| `hal/esp32c6/efuse_hal.c` | `runtime/wifi/idf_src/efuse_hal_esp32c6.c` | **ファイル名のみ** | 上と basename が衝突するため改名（改変方針 7）。中身はバイト同一 |
| `esp_phy/esp32c6/phy_init_data.c` | `runtime/wifi/idf_src/phy_init_data.c` | なし | C6 の PHY 初期化データ（dev 段4 Task 5 で S3 値の `hal_src/phy_init_data.c` から切替え） |

### lwIP contrib ヘッダ `runtime/wifi/net/lwip_contrib_include/`（D8 の逸脱の追加分。lwIP `fd432e4ee2`、3 本、BSD-3）

`netif_esp32s3.c` が `#include "ping.h"` / `"tcpecho_raw.h"` / `"udpecho_raw.h"` する。dev では
`-I esp-idf/components/lwip/lwip/contrib/apps/{ping,tcpecho_raw,udpecho_raw}` で解決するが、
M5Stack core の SDK（`esp32c6-libs/3.3.8/include/lwip/`）は contrib apps を含まない
（`-I` の写像で存在しない 3 ディレクトリ）。実体（`lw_ping.o` 等）は `prebuilt/lwip/esp32c6/liblwip.a`
の中にあるので、ヘッダだけを同梱する。出典は dev の esp-idf submodule の nested submodule
`components/lwip/lwip`（`git -C components/lwip/lwip show HEAD:contrib/apps/<app>/<h>`）。
**Task 1 の brief に無い追加であり、reviewer の受理待ち**（`task-1-report.md`）。

| lwIP のパス | arduino のパス | 改変 |
|---|---|---|
| `contrib/apps/ping/ping.h` | `runtime/wifi/net/lwip_contrib_include/ping.h` | なし |
| `contrib/apps/tcpecho_raw/tcpecho_raw.h` | `runtime/wifi/net/lwip_contrib_include/tcpecho_raw.h` | なし |
| `contrib/apps/udpecho_raw/udpecho_raw.h` | `runtime/wifi/net/lwip_contrib_include/udpecho_raw.h` | なし |

### FreeRTOS スタブ `runtime/wifi/freertos_stub/`（dev `esp/bt/stub/include/`、14 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/bt/stub/include/freertos/{FreeRTOS,queue,semphr,task}.h` | `runtime/wifi/freertos_stub/freertos/` 同名 | なし | C6 の TU（esp-idf 原本 `modem_clock.c`、hal、shim の 8 TU）が実際に include する 4 本 |
| `esp/bt/stub/include/freertos/{FreeRTOSConfig,event_groups,portable,portmacro,ringbuf,timers}.h`、`esp/bt/stub/include/{bt_nimble_config,esp_partition,esp_vfs,esp_vfs_dev}.h` | 同名 | なし | C6 の TU は include しないが、ディレクトリを丸ごと写す方針（段1 の target 層と同じ。Xtensa port の `wifi/freertos_stub` も同じ 14 本） |

### net `runtime/wifi/net/`（dev `esp/wifi/net/`、8 本。D7 = dev 型）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/wifi/net/netif_esp32s3.c` | 同名 | **あり（段5 Task 2、方針 9）** | Wi-Fi driver と lwIP を結ぶ netif（名前は S3 由来だがチップ共通）。段4 まではバイト同一。2026-09-15 に ping / port-7 echo / `ip=/gw=` を `#if TOPPERS_C6_NET_DIAG`（既定 OFF）で包んだ |
| `esp/wifi/net/netif_esp32s3.h` | 同名 | なし | 同上のヘッダ（`netif_esp32s3_ping_gateway` の宣言は残す） |
| `esp/wifi/net/net.cfg` `net_cfg.h` | 同名 | なし | `NET_SEM1..8` / `NET_MBOX1..10` / `NET_TSK`。アプリ cfg が `INCLUDE("net.cfg")` する（Task 2） |
| `esp/wifi/net/port/sys_arch.c` `port/include/arch/cc.h` `port/include/arch/sys_arch.h` | 同名 | なし | lwIP の OS ポート |
| `esp/wifi/net/port/include/lwipopts.h` | 同名 | **あり（段4 Task 0、方針 8）** | 段3 時点はバイト同一（`LWIP_DNS 0`）。2026-09-15 に `LWIP_DNS 1` / `MEMP_NUM_SYS_TIMEOUT 9` / `ERANGE` を加えた。`liblwip.a` を建てたときの物と同一でなければならない（本リポジトリの `liblwip.a` はこのファイルで建てた） |
| `esp/wifi/net/https_client.c` `mbedtls_user_config.h` | （持ち込まない） | - | dev のデモ（TLS クライアント）と `.a` ビルド時の mbedTLS 設定。`.a` の再生成は dev の台本で行う（`prebuilt/wpa2/README.md`） |

### libc スタブヘッダ `runtime/wifi/config/hal_stub_include/`（dev `esp/config/esp32/hal_stub_include/`、24 本）

dev の `build_incflags_esp32c6_espidf.txt` は `-I esp/config/esp32/hal_stub_include`
（名前は LX6 だがチップ共有、同梱の `README.vendored.txt` 参照）を全 TU に渡し、`string.h` /
`stdio.h` / `stdlib.h` / `assert.h` / `errno.h` / `inttypes.h` / `sys/*.h` / `platform/os.h` /
`esp_netif.h` / `nvs*.h` / `driver/gpio.h` / `esp_timer.h` の**最小スタブが newlib のヘッダより
先に見える**。段1 の `config/esp32c6/`（`sdkconfig.h`、`nuttx/config.h`）とは別のディレクトリで、
wifi-connect だけが include path に加える（minimal は不変）。

| dev のパス | arduino のパス | 改変 |
|---|---|---|
| `esp/config/esp32/hal_stub_include/README.vendored.txt` と 23 ヘッダ（`assert.h` `driver/gpio.h` `endian.h` `errno.h` `esp_netif.h` `esp_timer.h` `inttypes.h` `machine/endian.h` `nuttx/config.h` `nvs.h` `nvs_flash.h` `platform/os.h` `stdio.h` `stdlib.h` `string.h` `sys/cdefs.h` `sys/lock.h` `sys/param.h` `sys/queue.h` `sys/time.h` `sys/types.h` `time.h` `unistd.h`） | `runtime/wifi/config/hal_stub_include/` 同名 | なし |

**`runtime/wifi/config/hal_stub_include/nuttx/config.h` は shadow 化されている**（Task 1 レビュー
確認済み、dev 側にも同じ shadow がある）: include 順で `config/esp32c6/hal_stub_include/`
（段1、上記）が先に来るため、この第2コピーはどの TU からも解決されない。編集するのは
`runtime/config/esp32c6/hal_stub_include/nuttx/config.h` 側のみでよい。

### prebuilt `runtime/wifi/prebuilt/`（dev `esp/lib/`、`.a` 4 本 + README 2 本 + ライセンス 5 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/lib/wpa_esp32c6_espidf/libsupplicant.a` `libmbedtls.a` `libmbedcrypto.a` | `runtime/wifi/prebuilt/wpa2/esp32c6/` 同名 | なし（バイト同一、sha256 は README） | dev 台本 `build_{wpa_libs,mbedtls_tls}_espidf_esp32c6.sh` の生成物。Git 管理対象（`BUILDING.md`） |
| `esp/lib/lwip_esp32c6_espidf/liblwip.a` | `runtime/wifi/prebuilt/lwip/esp32c6/liblwip.a` | **あり（段4 Task 0、方針 8）** | 段3 時点はバイト同一（sha256 `85859F70...`）。2026-09-15 から dev 台本 `build_lwip_lib_espidf_esp32c6.sh` を `PORT_EXTRA`=本リポジトリの `lwipopts.h`（`LWIP_DNS 1`）・`OUT_DIR`=scratch で走らせた生成物（sha256 `5BFBC3EF...`、`prebuilt/lwip/README.md`）。dev golden との差は `lwipopts.h` の差だけ |
| （esp-idf `components/wpa_supplicant/COPYING`、`components/mbedtls/mbedtls/LICENSE`、`LICENSE`、`components/lwip/lwip/COPYING`） | `prebuilt/wpa2/{WPA_SUPPLICANT_COPYING,MBEDTLS_LICENSE,ESP_IDF_LICENSE}.txt`、`prebuilt/lwip/{LWIP_COPYING,ESP_IDF_LICENSE}.txt` | なし | 上流のライセンス本文（Xtensa の `wifi/prebuilt/wpa2/` と同じ 3 本 + lwIP） |
| （新規） | `prebuilt/wpa2/README.md` `prebuilt/lwip/README.md` | -（新規） | 由来 commit・台本・sha256・ライセンス |

### 持ち込まなかった dev の入力（wifi-connect の CMake が写像で置き換えたもの）

- `esp/boot/build_incflags_esp32c6_espidf.txt`: `-I@ESPIDF@/components/X/...` 37 本を
  `ARDUINO_SDK_INCLUDE_ROOT/X/...` に写像した表を `runtime/CMakeLists.txt` の
  `WIFI_SDK_INCLUDE_DIRS` に持つ（configure 時に全ディレクトリの存在を検査）。SDK に無い
  `esp_hw_support/port/include` と `esp_wifi/wifi_apps/roaming_app/include` は、dev の deps に
  よればどの TU も include しないので落とした。`lwip/lwip/contrib/apps/*` 3 本は上記ヘッダの同梱で置換。
  `-D` は `TOPPERS_ESP32C6` を除いてそのまま（dev も落とす）。
- `cmake/a1_creds.cmake` の `-include a1_wifi_credentials.h`: 資格情報はスケッチが持つ（stage は creds を
  持たない）。vendoring した TU のうち `WIFI_STA_*` を参照するものは無い（コメントを除く）。
- dev のデモアプリ `esp/app/wifi_sta.{c,cfg,h}` と、それだけが読む `-D`（`A1_C6_WIFI_SCAN`、
  `TOPPERS_APP_HEAP_REPORT`、`A1_C6_WIFI_BOOT_DELAY_MS`、`A1_C6_USJ_REARM_PROBE`）、および診断用
  `A1_C6_BOOT_TRACE`: 持ち込まない。arduino のアプリは Task 2 の `app/wifi_connect/`。
- `fmp3/target/m5nanoc6_gcc/diag_recorder.c` `target_hrt64.c`: 段1 で target 層ごと同梱済み。
  wifi-connect の `wifi_objects` がコンパイルする（dev と同じ）。


## 段3 Task 2（2026-09-15）: adapter の C6 版・`attachInterrupt`・`app/wifi_connect/`（arduino Xtensa port からの派生、dev 由来ではない）

いずれも vendoring ではなく、`ports/m5stack_xtensa` の同名ファイルを写して C6 分岐にしたもの
（コメントは英語に書き直した）。設計判断は `docs/c6-port.md` の D6 / D7 / S3-1..S3-6。

| 元（arduino Xtensa port） | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `runtime/wifi/adapter/toppers_wifi_core.{h,c}` | `runtime/wifi/adapter/toppers_wifi_core.{h,c}` | **あり** | D6: `g_ic+0x1b4` の WPA コールバック表・`__real_esp_supplicant_init` 経由の auth backend 選択・OPEN 用の `wpa_crypto_funcs` ゼロ化を**持たない**（supplicant は `esp_wifi_init` に任せる。Open AP は段4 まで未検証と明記）。初期化順は dev `esp/app/wifi_sta.c` の C6 経路（`esp_shim_initialize` -> handler -> `esp_shim_coex_adapter_register` -> `wifi_module_enable` -> `esp_wifi_init`）。`sar_periph_ctrl_init` / `esp_bbpll_enable_480m` / `esp_wifi_clock_init_pll`（S3/LX6 専用）を呼ばない。`toppers_wifi_core_has_supplicant()` は定数 true |
| `runtime/wifi/adapter/toppers_wifi_connect.c` | `runtime/wifi/adapter/toppers_wifi_connect.c` | **あり** | D6: `__wrap_esp_supplicant_init` を持たない。D7: `toppers_netif.h`（Xtensa 型）ではなく dev 型 `netif_esp32s3.h`（`netif_esp32s3_start` / `_notify_link` / `_get_ipaddr`）へ接続。gateway / netmask は lwIP の `netif_default` から読む。`toppers_fmp3_wifi_host_by_name` は段3 では `lwip_getaddrinfo` が無い（`liblwip.a` は `LWIP_DNS 0`）ため数値表記だけ `ip4addr_aton` で解決し名前は失敗値 0 を返していた（Low#1）が、段4 Task 0（方針 8）で `tcpip_callback` + `dns_gethostbyname`（tcpip_thread 文脈）+ 5 秒の有限待ちに置き換えた。`SO_RCVTIMEO` は `LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1`（int ms）で渡す |
| `runtime/wifi/adapter/toppers_wifi_scan.c` | `runtime/wifi/adapter/toppers_wifi_scan.c` | コメントのみ | scan の手順は dev `wifi_sta_c6_scan_run` と同じ。C6 では backend 選択が無いことをコメントに |
| `runtime/wifi/adapter/toppers_wifi_optional_stubs.c` | `runtime/wifi/adapter/toppers_wifi_optional_stubs.c` | コメントのみ（英訳） | weak な失敗値スタブ（`BUILDING.md`）。記号・戻り値は同一 |
| `runtime/arduino/arduino_interrupt.{c,cfg,h}` | `runtime/arduino/arduino_interrupt.{c,cfg,h}` | **あり** | S3-4: 線 19、GPIO ソース `ETS_GPIO_INTR_SOURCE`（30）を kernel の `_kernel_esp32c6_intmtx_route` で配線（MAP レジスタ直書きではない）。`hal/gpio_ll.h` の esp32c6 版（`status_high` 系は無い = 32 本未満）。衝突検査は `INTNO_TIMER` / `INTNO_SIO` / `esp_shim_intr_intmtx_lines.h` の範囲 / 18,20,21。未対応モードは失敗（WARNING + attach しない）。段3 はリンクまで、動作は段6 |
| `app/wifi_connect/phase9_wifi_connect_app.{c,cfg,h}` | `app/wifi_connect/phase9_wifi_connect_app.{c,cfg,h}` | **あり** | `TA_FPU` 除去、`CLASS(CLS_PRC1)`、`INCLUDE("net.cfg")` を追加（dev 型 lwIP の `NET_TSK` / sys_arch プール）。`esp_shim_intr_intmtx.cfg` は CMake が cfg ファイルとして先に渡す |
| （新規） | `runtime/CMakeLists.txt` の追記 | - | `wifi_objects` に上記 5 TU を追加、`FMP3_INCLUDE_DIRS` に `wifi/adapter` と `arduino`（cfg の INCLUDE 解決） |

## ESP32-C5（M5Stamp-C5、C5 計画 段1、2026-09-16）: chip 分岐として並置した C5 の層

`docs/c5-port.md` の A2（`ports/m5stack_riscv` を chip 分岐にし、C5 の層を C6 の隣に並置する）に
従って足した分。`packaging/release-allowlist.json` の `portBaseRepositoryC5` / `portBaseCommitC5`
と同じ出典である。**C6 の層（上記）は 1 バイトも触っていない**（C5 計画 段0 の baseline に対する
X-check 9/9 MATCH。`runtime/CMakeLists.txt` の chip 表化は構造の変更で、C6 の値は同じ）。

- 出典: `https://github.com/exshonda/fmp3_esp_idf_dev.git`（非公開の開発リポジトリ）
  commit **`1d96bcba32a043eb7066126550b0dbe598e4aad6`**（2026-09-16、C5 計画 3 完了 commit）。
- 取込み: 2026-09-16（arduino_esp32 C5 計画 段1 Task 3）。`git archive 1d96bcba <path>` で当該
  commit の内容をそのまま写した。
- 出典側との照合コマンド（改変「なし」の行はこれで 0 差分になる）:
  `git -C <dev repo> show 1d96bcba:<dev path> | cmp - <arduino path>`
- 出典側にある `arch/riscv_gcc/esp32c5/IMPORT_PROVENANCE.md` と
  `target/m5stampc5_gcc/IMPORT_PROVENANCE.md` は、さらに上流（asp3_esp_idf / P4 / C6）から dev への
  出典と改変の記録で、そのまま同梱している。
- fmp3_core は C6 と同じ既存 submodule `685b36a` を使い、無改変。`chip.cmake` が共通
  `arch/riscv_gcc/common/start.S` を `chip_start.S` へ差し替える仕組み、共通 `clic_kernel_impl.c`
  を積む仕組みもそのまま。

### 改変の方針（C5）

C6 の改変方針 1（SDK パスの写像）、2（C++ 静的初期化の表）、3（unwind 表の入力規則）を **同じ
内容で** C5 の `target.cmake` / `esp32c5_xip.ld` に施した。4（`TA_FPU` 除去）と 5（`PADDR_PROBE`
削除）は C6 のアプリ `app/phase3/` を共有するので C5 で新たに行うことは無い。6-9（wifi-connect
の逸脱）は段3。C5 固有の改変は無い。

### chip 層 `runtime/arch/riscv_gcc/esp32c5/`（dev `fmp3/arch/riscv_gcc/esp32c5/`、23 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `fmp3/arch/riscv_gcc/esp32c5/IMPORT_PROVENANCE.md` | `runtime/arch/riscv_gcc/esp32c5/IMPORT_PROVENANCE.md` | なし | P4 / C6 -> dev の記録。同梱 |
| `fmp3/arch/riscv_gcc/esp32c5/chip.cmake` | 同名 | なし | `-march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany --specs=nano.specs`、`USE_RISCV_DIRECT_TRAP`（CLIC 非ベクタ）、共通 `clic_kernel_impl.c` の追加、`chip_start.S` 差替え、コンソール選択（usbjtag）はそのまま |
| `chip_asm.inc` `chip_kernel.h` `chip_kernel.py` `chip_kernel_impl.c` `chip_kernel_impl.h` `chip_rename.def` `chip_rename.h` `chip_serial.c` `chip_serial.cfg` `chip_serial.h` `chip_sil.h` `chip_start.S` `chip_stddef.h` `chip_support.S` `chip_unrename.h` `clic_kernel.py` `esp32c5_clic_kernel_impl.h` `esp32c5.h` `esp32c5_uart.c` `esp32c5_uart.h` `esp32c5_usbjtag.h`（計 21 本） | 同名 | なし | バイト同一（`cmp` で確認）。C6 に無い `clic_kernel.py` / `esp32c5_clic_kernel_impl.h` は CLIC（P4 型）の分、C6 の `intmtx_kernel_impl.h` / `esp32c6_usbjtag.c` に当たるものは C5 では無い（USJ の実体は target 側 `esp32c5_usbjtag_hal.c` のみ） |

### target 層 `runtime/target/m5stampc5_gcc/`（dev `fmp3/target/m5stampc5_gcc/` から `app/` を除く 29 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `fmp3/target/m5stampc5_gcc/target.cmake` | `runtime/target/m5stampc5_gcc/target.cmake` | **あり** | C6 の `target.cmake` と同じ写像: (a) `ESP_SUP_DIR`（esp-idf submodule）の参照を `ARDUINO_SDK_INCLUDE_ROOT` / `ARDUINO_SDK_LD_ROOT`（M5Stack core の `esp32c5-libs/3.3.8/{include,ld}`）へ: include 10 本は `components/X` -> `include/X`、`-L components/soc/esp32c5/ld` -> `-L <ld>`、ROM ld 2 本は `<ld>/esp32c5.rom.ld` / `esp32c5.rom.api.ld`。(b) esp-idf submodule の存在検査を SDK の存在検査（`hal/esp32c5/include/hal/systimer_ll.h`、`esp32c5.rom.ld`）に置換。(c) `esp/config/esp32c5` の参照を本 runtime の `config/esp32c5` へ。(d) 冒頭に改変の説明を英語で追記。`FMP3_TARGET_C_FILES`・cfg/py の列挙・`FMP3_LINK_OPTIONS`（`--gc-sections`、`--build-id=none`、`--undefined=_kernel_*_table` 4 本）・`FMP3_CFG1_OUT_LINK_OPTIONS` は不変 |
| `fmp3/target/m5stampc5_gcc/esp32c5_xip.ld` | `runtime/target/m5stampc5_gcc/esp32c5_xip.ld` | **あり** | C6 の `esp32c6_xip.ld` と同じ追加: `.flash.rodata` 出力セクションの末尾に (a) `__init_array_start/end`・`__fini_array_start/end`・`__ctors_start/end`・`__dtors_start/end` と対応する `KEEP(*(...))` 入力規則、(b) `.eh_frame` / `.eh_frame_hdr` / `.gcc_except_table` の入力規則。MEMORY（RAM LENGTH = `0x4084E5A0 - 0x40800000`）・`ENTRY(toppers_start)`（seam のエントリは manifest の `linkBaseFlags` の `-Wl,-e,seam_c5_entry_boost` で上書き = dev の `-Wl,-e` と同じ機構）・`.text`・`.flash_rodata_dummy`・`.flash.appdesc`・`.data`（dev が段4 で IRAM 群を移した形のまま）・`.bss`・`.tbss` は不変。実測: 3 例題とも `mapped=2 ram=1 pad=1`、C-1..C-9 成立（`.steering/20260916-c5-arduino-plan/stage1/logs/task4-compile-c5-*.txt`） |
| `IMPORT_PROVENANCE.md` `diag_recorder.c` `diag_recorder.h` `esp32c5_usbjtag_hal.c` `target_asm.inc` `target_cfg1_out.h` `target_check.py` `target_class.py` `target_hrt64.c` `target_ipi.h` `target_kernel.cfg` `target_kernel.h` `target_kernel_impl.c` `target_kernel_impl.h` `target_kernel.py` `target_rename.def` `target_rename.h` `target_serial.cfg` `target_serial.h` `target_sil.h` `target_stddef.h` `target_syssvc.h` `target_test.h` `target_timer.c` `target_timer.cfg` `target_timer.h` `target_unrename.h`（計 27 本） | 同名 | なし | バイト同一。`diag_recorder.*`・`target_hrt64.c` は minimal では使わない（段3 の wifi-connect で使う）が、target 層を丸ごと写す方針で同梱。C6 の Direct Boot 用 `esp32c6.ld` に当たるものは C5 には無い（dev D2） |
| `fmp3/target/m5stampc5_gcc/app/**`（`fmp_app`、`usj_probe`） | （持ち込まない） | - | dev の hello / 計測用アプリ。arduino のアプリは C6 と共有の `ports/m5stack_riscv/app/phase3`（chip 非依存。`TA_FPU` 無し、`CLS_PRC1`。C5 の `target_class.py` も `CLS_PRC1 = 1` / `CLS_ALL_PRC1 = 2`） |

### seam `runtime/seam/`（dev `esp/boot/`、4 本。`init_array.cpp` / `newlib_syscalls.c` は C6 と共有）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/boot/seam_c5_appdesc.c` | `runtime/seam/seam_c5_appdesc.c` | なし | `.appdesc` セクションの esp_app_desc スタブ（`mmu_page_size` は C5 では読まれない） |
| `esp/boot/seam_c5_entry.S` | `runtime/seam/seam_c5_entry.S` | なし | 像のエントリ 2 本: `seam_c5_entry`（`SEAM_C5_ENTRY_MARK`、80 MHz 用）と `seam_c5_entry_boost`（`SEAM_C5_CLK_BOOST` のときだけ組まれる 240 MHz 用。gp/sp を用意して `seam_c5_clk_set()` を呼び `seam_c5_entry` へ）。どちらを ELF のエントリにするかは `runtime/CMakeLists.txt` が `A1_C5_CPU_FREQ_MHZ` から決めて manifest の `linkBaseFlags` に `-Wl,-e,...` で入れる（既定 240 = `seam_c5_entry_boost`） |
| `esp/boot/seam_c5_clk.c` | `runtime/seam/seam_c5_clk.c` | なし | 80 -> 240 MHz（PCR の CPU_DIV_NUM 2 -> 0 + `bus_clk_update`。ソース切替はしない = dev D5）。結果は `.data` の `g_seam_c5_clk_result`（3 例題とも `0x40800004`） |
| `esp/boot/seam_c5_clk.h` | `runtime/seam/seam_c5_clk.h` | なし | 同上のヘッダ |
| `esp/boot/seam_c5_clk.cfg` | （持ち込まない） | - | dev の 240 MHz 証跡用の報告タスク（`CRE_TSK(SEAM_C5_CLK_REPORT_TASK, ...)`、優先度 11）。arduino の stage には配布物としてタスクを 1 本足すことになるため持ち込まない。段2 で 240 MHz の証跡が要るときは、スケッチから `extern "C" seam_c5_clk_result_t g_seam_c5_clk_result` を読む（判断 S1-C5-3、`docs/c5-port.md`） |

### config `runtime/config/esp32c5/`（dev `esp/config/esp32c5/`、1 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/config/esp32c5/sdkconfig.h` | `runtime/config/esp32c5/sdkconfig.h` | なし | hal ヘッダが読む `CONFIG_*`（asp3 の手書きスタブ。`nuttx/config.h` を include しないので C6 の `hal_stub_include/nuttx/config.h` に当たるものは無い = dev `target.cmake` 冒頭 (2)） |

### 本 port で新規に書いたもの（C5、出典なし）

- `runtime/CMakeLists.txt` の esp32c5 行（chip 表）-- dev `cmake/a1_c5_stage1.cmake` の seam-c5-min 相当:
  `CORE_CLK_MHZ`（既定 240 = A6、`SEAM_C5_CLK_BOOST=1`、80 は `--cmake-define A1_C5_CPU_FREQ_MHZ=80`）、
  `SEAM_C5_ENTRY_MARK=0x53`、`FMP3_PRC_NUM=1`、cfg1_out に xip ld を使う（`A1_C5_LDSCRIPT`）、ROM ld は
  minimal で dev と同じ 2 本（`esp32c5.rom.ld` / `esp32c5.rom.api.ld`）。wifi-connect の 13 本
  （eco3 無し）は段3。dev の `A1_C5_SEAM=1` define は arduino の C5 ソースのどれも読まないので渡さない。
- `runtime/cmake/toolchain-riscv-esp32c5.cmake` -- `toolchain-riscv-esp32c6.cmake` の c6 -> c5 写し
  （dev `cmake/toolchain-riscv-esp32c5.cmake` と同じ形。版固定は同じ `esp-14.2.0_20260121`）。
- `runtime/cmake/prebuilt_stage_c5.cmake` -- `prebuilt_stage_c6.cmake` の写し（`A1_CHIP` の検査を esp32c5 に、
  flash 4MB は M5Stamp-C5 の `boards.txt`）。C-9 は staging ではなくドライバ側
  （`scripts/fmp3_link.py` の `FIXED_VMA_LAYOUTS["esp32c5"]`: `chip_id=0x0017`、`board_rev_full=100`）。
- `runtime/arduino/arduino_interrupt_c5.{c,cfg,h}` / `runtime/arduino/arduino_gpio_c5.c` -- C6 の
  `arduino_interrupt.{c,cfg,h}` / `arduino_gpio.c` の C5 版（A3。CLIC 線 23、`_kernel_esp32c5_intmtx_route`、
  `ETS_GPIO_INTR_SOURCE == 31`、USB 13/14、MSPI 15-22、GPIO 0..28。`arduino_gpio.h` は共有）。段1 では
  どの stage にも組み込まれず、stage の compile 行で `-fsyntax-only` を通しただけ
  （`stage1/logs/task3-syntax-check-arduino-c5.txt`。`esp_shim_intr_c5_lines.h` は段3 で vendoring）。

## ESP32-C5 段3 Task 1（2026-09-16）: `runtime/wifi/` の C5 分岐 -- shim の dev 差分、esp-idf 原本、prebuilt `.a`

dev `1d96bcba` の C5 Wi-Fi 構成（`cmake/a1_c5_stage1.cmake` の `if(A1_C5_WIFI)` ブロック）が
`fmp` にリンクする集合を dev `build/c5-wifi/build.ninja`（32 TU）から機械列挙して写した。
C6 の集合（段3 Task 1）との差は `esp_shim_intr_intmtx.c` -> `esp_shim_intr_c5.c`、esp-idf 原本 3 本の
C5 版、`.a` 4 本の C5 版、`-DCONFIG_IDF_TARGET_ESP32C5=1` と include の順序（`hal/esp32c5/include` が
`hal/include` より前）のみ。**C6 の stage は 1 バイトも動いていない**（X-check 9/9 MATCH、
`.steering/20260916-c5-arduino-plan/stage3/logs/task1-xcheck.txt`）。

- 出典: 上記と同じ dev commit **`1d96bcba32a043eb7066126550b0dbe598e4aad6`**。
- 取込みの方法（shim）: **dev の差分 `git diff c7fef18 1d96bcba -- esp/shim`（+ `esp/bt/stub/include/
  freertos/FreeRTOS.h` の 1 行）を、パス接頭辞を `ports/m5stack_riscv/runtime/wifi/shim/`
  （`wifi/freertos_stub/freertos/`）に書き換えただけで `git apply` した**（hunk の失敗 0、手編集なし。
  `stage3/logs/task1-apply-shim-diff.txt`）。適用後の shim 34 本すべてが dev `1d96bcba` の `esp/shim/` と
  `cmp` で同一。dev 側はこの差分を「同じ行数での `#if` 条件の書換え・空行 1 本の `#include` 置換・EOF 追記」
  の 3 形に限っており（dev `.steering/20260915-c5-plan/stage4/README.md` 0-3 節、seam-c6-wifi golden の
  DWARF 感受性のため）、本リポジトリでも同じ差分を当てた結果 C6 の objs が不変であることを X-check で示した。
- 照合コマンド: `git -C <dev repo> show 1d96bcba:<dev path> | cmp - <arduino path>`。

### shim `runtime/wifi/shim/`（dev `esp/shim/`、変更 6 本 + 新規 7 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/shim/esp_shim.c` `esp_shim.cfg` `esp_shim.h` `esp_shim_blobglue.c` `esp_shim_libc.c` `esp_wifi_adapter.c` | 同名 | なし（dev `1d96bcba` と同一。`c7fef18` からの差分 = dev の C5 分岐） | C5 分岐の形: `#if defined(TOPPERS_ESP32C6)` -> `|| defined(TOPPERS_ESP32C5)`（同じ行数）、S3 既定の `#else` -> `#elif !defined(TOPPERS_ESP32C5)`、`esp_wifi_adapter.c:90` / `esp_shim_blobglue.c:174` の空行 1 本を `#include "*_c5.inc"` に置換、`esp_shim.h` 末尾に RNG / eFuse 番地（C6 / C5）を追記。`esp_shim.cfg` は `#ifndef TOPPERS_ESP32C6` -> `#if !defined(TOPPERS_ESP32C6) && !defined(TOPPERS_ESP32C5)`（S3/LX6 の線 0-3/23/27 を C5 でも外す） |
| `esp/shim/esp_wifi_adapter_c5.inc` | 同名 | なし（新規） | `esp_wifi_adapter.c` の C5 分岐本体: `set_intr/clear_intr/set_isr/ints_on/ints_off`（`esp_shim_intr_c5` 経由）、`phy_enable` / `wifi_clock_enable`（modem クロック読み戻し + **APM/TEE 解除 `c5_apm_unblock`**: `A1_C5_APM_UNBLOCK` の下に `A1_C5_APM_FUNC_CTRL`（HP_APM / LP_APM0 / LP_APM / CPU_APM の FUNC_CTRL=0）と `A1_C5_APM_TEE`（TEE master 0..31 を mode 0）。`runtime/CMakeLists.txt` の option `TOPPERS_C5_APM_UNBLOCK`（既定 ON）が 3 つとも定義する = A11）、`esp_wifi_adapter_c5_apm_readback`（adapter の scan 後読み戻しが呼ぶ）、`esp_wifi_adapter_c5_diag_dump`。共通側の同名 3 関数を関数形式マクロで `c5_unused_*` へ逃がし `__attribute__((noipa, error))` で迷い呼出しをエラーにする仕掛けは dev のまま |
| `esp/shim/esp_shim_blobglue_c5.inc` | 同名 | なし（新規） | `esp_shim_blobglue.c` の C5 分岐本体: `rtc_clk_xtal_freq_get`（48 MHz）、`esp_clk_tree_enable_src`（Low#3）、`putchar`（Low#2、-1）、`__errno`（`libm_nano.a` の `log10` 経路が要求）、`vPortEnterCritical/ExitCritical`、`esp_sleep_*`（Low#1、`ESP_ERR_NOT_SUPPORTED`） |
| `esp/shim/esp_shim_intr_c5.c` `esp_shim_intr_c5.h` `esp_shim_intr_c5.cfg` `esp_shim_intr_c5_lines.h` | 同名 | なし（新規） | blob の `intr_num` 1..15 を CLIC 線 25..39（BASE 24）へ写像する線シム（`esp_shim_c5_wifi_route/unroute/set_isr/ints_on/ints_off`、DEF_INH 15 本 -> `esp_shim_wifi_int_dispatch(n)`）。`.cfg` は `runtime/CMakeLists.txt` が `FMP3_CFG_FILES` に足す（C6 の `esp_shim_intr_intmtx.cfg` と同じ位置）。`_lines.h` の予約線検査（16/17、18-22、40-44）を `arduino_interrupt_c5.c` も読む |
| `esp/shim/IMPORT_PROVENANCE_c5.md` | 同名 | なし（新規） | asp3 / C6 -> dev の C5 分岐の出典と改変の記録（各ブロックの出典行、Low#1-6）。同梱 |
| `esp/bt/stub/include/freertos/FreeRTOS.h` | `runtime/wifi/freertos_stub/freertos/FreeRTOS.h` | なし（dev `1d96bcba` と同一） | `vPortEnterCritical/ExitCritical` の extern 宣言を C5 でも見せる 1 行（同じ行数） |

`esp_shim_intr_intmtx.*`（C6）と `esp_shim_intr_c5.*`（C5）は並置し、`runtime/CMakeLists.txt` の chip 表
（`A1_CHIP_WIFI_INTR_CFG` / `A1_CHIP_WIFI_INTR_SOURCE`）が chip ごとに片方だけを組む（A2）。

### hal / net / config / FreeRTOS スタブ / libc スタブヘッダ（C5 で新たに持ち込んだ物は無い）

`hal_src/` 6 本、`net/` 8 本、`freertos_stub/` 14 本、`wifi/config/hal_stub_include/` 24 本は
C6 と共有（dev でも S3 / C6 / C5 で同じファイル）。`config/esp32c5/sdkconfig.h` は段1 で vendoring 済み
（`CONFIG_SOC_WIFI_SUPPORT_5G 1`（:120）、`CONFIG_ESP32C5_REV_MIN_FULL 100` / `REV_MAX_FULL 199`（:189-192）、
`CONFIG_IDF_TARGET "esp32c5"`。dev `1d96bcba` と `cmp` 同一）。C6 の `config/esp32c6/hal_stub_include/`
に当たる C5 専用スタブは無く、dev と同じく chip 共用の `wifi/config/hal_stub_include/` だけを include path に
置く（`runtime/CMakeLists.txt` `A1_CHIP_WIFI_CONFIG_DIRS`）。

`net/netif_esp32s3.c` は改変方針 9（`TOPPERS_C6_NET_DIAG`）の上に、C5 の option 名
`TOPPERS_C5_NET_DIAG` を同じマクロへ alias する 3 行（+ コメント）を足した（**改変方針 10**、C5 計画 A11:
「`TOPPERS_C5_NET_DIAG`（既定 OFF）を C6 と対称に」。`#if defined(TOPPERS_ESP32C5) &&
defined(TOPPERS_C5_NET_DIAG) && !defined(TOPPERS_C6_NET_DIAG)` で `TOPPERS_C6_NET_DIAG` を
`TOPPERS_C5_NET_DIAG` に定義する。C6 ビルドでは条件が偽で前処理結果が不変 = X-check MATCH）。

### esp-idf 原本 `runtime/wifi/idf_src/`（A8 = D8 の逸脱。esp-idf v5.5.4 `735507283d`、C5 の同名原本 3 本、Apache-2.0）

| esp-idf のパス（`components/`） | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp_phy/esp32c5/phy_init_data.c` | `runtime/wifi/idf_src/phy_init_data_esp32c5.c` | なし（ファイル名のみ chip 接尾辞、方針 7） | C5 の PHY 初期化データ（2.4 / 5 GHz、345 行）。dev AC-3i はこの原本の単独コンパイル 256 B と ELF の `phy_init_data` を `cmp` で同一と実測 |
| `hal/esp32c5/modem_clock_hal.c` | `runtime/wifi/idf_src/modem_clock_hal_esp32c5.c` | なし（同上） | C5 の modem クロック hal（C6 版と byte 39 から異なる別物） |
| `hal/esp32c5/efuse_hal.c` | `runtime/wifi/idf_src/efuse_hal_esp32c5.c` | なし（同上） | C5 の chip 別 eFuse hal（`CONFIG_ESP_REV_MIN_FULL` を読む） |
| `esp_hw_support/periph_ctrl.c` `esp_hw_support/modem_clock.c` `hal/efuse_hal.c` | （C6 と共有、段3 Task 1 の 3 本） | なし | chip 非依存（esp-idf でも 1 本）。`periph_ctrl.c` の `wifi_module_enable()` は `PERIPH_WIFI_MODULE`（C5 も `soc/periph_defs.h` の enum）で C5 の modem を有効化する |

`runtime/CMakeLists.txt` の chip 表 `A1_CHIP_WIFI_PHY_INIT_DATA` / `A1_CHIP_WIFI_MODEM_CLOCK_HAL` /
`A1_CHIP_WIFI_EFUSE_HAL` が chip ごとに 1 本ずつ選ぶ（dev の `target_sources` と同じ位置）。
段5 で再評価する方針（D8 / A8）は C6 と同じ。

### prebuilt `runtime/wifi/prebuilt/{wpa2,lwip}/esp32c5/`（dev `esp/lib/`、`.a` 4 本）

| dev のパス | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `esp/lib/wpa_esp32c5_espidf/libsupplicant.a` `libmbedtls.a` `libmbedcrypto.a` | `runtime/wifi/prebuilt/wpa2/esp32c5/` 同名 | なし（バイト同一。sha256 は `prebuilt/wpa2/README.md` の C5 表 = dev `.steering/20260915-c5-plan/stage4/README.md` Task 2 の表と一致） | dev 台本 `build_{wpa_libs,mbedtls_tls}_espidf_esp32c5.sh` の生成物。`libmbedcrypto.a` / `libmbedtls.a` は C6 版とバイト同一、`libsupplicant.a` は 3 .o が違う（`esp_wpa_main` / `esp_wps` / `ieee802_11_common`: C5 の sdkconfig / 5G / HE）ので C6 の物は流用しない |
| `esp/lib/lwip_esp32c5_espidf/liblwip.a` | `runtime/wifi/prebuilt/lwip/esp32c5/liblwip.a` | **あり（方針 8 の C5 版 = A7）** | dev 台本 `build_lwip_lib_espidf_esp32c5.sh` を `PORT_EXTRA`=本リポジトリの `wifi/net/port/include`（`LWIP_DNS 1`）・`OUT_DIR`=scratch で走らせた生成物（sha256 `5bfbc3ef...`、473,586 B。`nm` に `dns_gethostbyname` / `lwip_getaddrinfo`）。dev golden（`LWIP_DNS 0`、`85859f70...`）は無改変（同じ台本を既定引数で scratch へ走らせると golden と同一の sha を再現することも同時に実測 = 台本の決定性の対照）。**C6 の DNS 版 `liblwip.a` とバイト同一**（lwIP 本体は chip 非依存、同じ `-march`・同じ `lwipopts.h`）。`stage3/logs/task1-build-lwip-dns.txt` |

### 持ち込まなかった dev の入力（C5）

- `esp/boot/build_incflags_esp32c5_espidf.txt`: C6 と同じ写像を `runtime/CMakeLists.txt` の chip 表
  `A1_CHIP_WIFI_SDK_INCLUDE_DIRS`（esp32c5 行）に持つ。SDK に無い 2 本（`esp_hw_support/port/include`、
  `esp_wifi/wifi_apps/roaming_app/include`）は C6 と同じ理由で落とした。`-DTOPPERS_ESP32C5` は落とし
  （`chip_stddef.h` が定義）、`-DCONFIG_IDF_TARGET_ESP32C5=1` は残す（`A1_CHIP_WIFI_COMPILE_OPTIONS`）。
- `cmake/a1_creds.cmake`、`esp/app/wifi_sta.{c,cfg,h}` + `wifi_sta_c5.inc`、それだけが読む `-D`
  （`A1_C5_WIFI_SCAN`、`TOPPERS_APP_HEAP_REPORT`）: C6 と同じ理由で持ち込まない。
- `esp/boot/seam_c5_clk.cfg`、`fmp3/target/m5stampc5_gcc/app/**`: 段1 と同じ。
- `esp32c5.rom.eco3.ld`: dev D14（M5Stamp-C5 は rev v1.0 = 100。eco3 を入れた asp3 は ROM の PHY 関数が
  blob の RAM 版を上書きする store fault を踏んだ）。ROM ld は dev と同じ 13 本。

## ESP32-C5 段3 Task 2（2026-09-16）: adapter の C5 分岐・`app/wifi_connect_c5/`・scripts の表（arduino 側の派生、dev 由来ではない）

| 元 | arduino のパス | 改変 | 理由 |
|---|---|---|---|
| `runtime/wifi/adapter/toppers_wifi_core.{h,c}` `toppers_wifi_connect.c` `toppers_wifi_scan.c`（C6 版、段3 Task 2） | 同名（C6 / C5 共用） | **あり（C5 分岐）** | `#if !defined(TOPPERS_ESP32C6)` の `#error` を `&& !defined(TOPPERS_ESP32C5)` に。`toppers_wifi_scan.c` は APM 読み戻しを `#if defined(TOPPERS_ESP32C5)` で `esp_wifi_adapter_c5_apm_readback` に切替え（`toppers_wifi_apm_readback` マクロ）、C5 だけ scan 後に `[WiFiScan] bands: 2.4GHz=N 5GHz=M (of K listed)` を 1 行出す（ch > 14 を 5 GHz と数える dev の規則、S3-6: 記録のみ）。初期化順・D6・D7 は C6 と同一（dev の C5 経路 `wifi_sta.c` + `wifi_sta_c5.inc` も同じ順）。C6 の objs は不変（X-check） |
| `app/wifi_connect/phase9_wifi_connect_app.{c,cfg,h}`（C6） | `app/wifi_connect_c5/phase9_wifi_connect_app.{c,cfg,h}` | **あり（cfg の INCLUDE 1 行）** | `INCLUDE("arduino_interrupt.cfg")` -> `INCLUDE("arduino_interrupt_c5.cfg")`（線 23）。`.c` / `.h` は C6 の写し。cfg は `#ifdef` で切らない（`BUILDING.md`）ので別ディレクトリにし、`scripts/build_prebuilt_stages.py` の `CHIP_APPLICATIONS[("esp32c5", "wifi-connect")]` が選ぶ |
| `scripts/build_prebuilt_stages.py` `install_platform.py` `verify_package.py` `test_check_release_artifacts.py` `packaging/release-allowlist.json` `.github/workflows/verify-package.yml` | 同名 | 表の行 | esp32c5 の profile を `minimal` + `wifi-connect` に（6 表）。drift test に `verify_package.BOARD_PROFILES` の写像（段1 レビュー F1）。`--list-builds` 62 -> 68 |
| `examples/GpioInterrupt/GpioInterrupt.ino` | 同名 | `#elif defined(ARDUINO_M5STACK_STAMP_C5)` の 3 行 | 試験ピン G1（A10。リンクのみ、実機は段4） |
