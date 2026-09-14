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

改変は次の 3 種だけで、いずれも下表に理由を書く。

1. **SDK パスの写像**（`target.cmake`）: dev は esp-idf submodule の
   `components/<comp>/...` を include / link するが、arduino_esp32 は「ESP-IDF を
   複製しない」（`BUILDING.md`）ので、M5Stack core の `esp32c6-libs/3.3.8/include/<comp>/...`
   と `ld/` へ 1 対 1 で写す（同じ ESP-IDF v5.5.4）。
2. **C++ 静的初期化の表**（`esp32c6_xip.ld`）: Arduino の橋
   （`src/bridge/ArduinoSketchBridge.cpp`）が `esp_run_init_array()` を呼ぶため、
   dev の ld に無い `__init_array_*` / `__ctors_*`（と `__fini_array_*` / `__dtors_*`）を
   足す（計画 S1-5）。
3. **`TA_FPU` の除去**（`app/phase3/*.cfg`、Xtensa 側アプリからの派生）: riscv の
   fmp3_core に `TA_FPU` の定義が無い（計画 S1-4）。

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
| （新規） | `runtime/seam/newlib_syscalls.c` | -（新規） | dev 由来ではない。newlib-nano の `__stack_chk_fail` 経路が要求する `_exit` / `_kill` / `_getpid` / `_write` / `__getreent` の実体。M5Stack core はスケッチを `-fstack-protector` で建てるため、ローカル配列を持つスケッチはこの 5 本が無いとリンクできない（段1 Task 3 で実測、Blink / LibraryInfo / TwoFileSketch は保護フレームを持たず無くてもリンクする）。型は Xtensa port の `arch/xtensa_gcc/esp32s3/chip_rom_libc.c` の `_exit` / `_kill` / `_getpid`（2026-08-22 に同じ経路で追加）。ファイルを共有しないのは `init_array.cpp` と同じ理由 |

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
  ROM ld 13 本（target.cmake の 2 本 + `a1_c6_stage1.cmake` の Wi-Fi 構成の 11 本）。
- `runtime/cmake/toolchain-riscv-esp32c6.cmake` -- dev `cmake/toolchain-riscv-esp32c6.cmake` と
  同じ形（`riscv32-esp-elf-` 接頭辞、`esp-14.2.0_20260121` の版固定）だが、
  ツールチェーンの場所は書かない（`build_prebuilt_stages.py` が M5Stack core の
  `esp-rv32/2601` を PATH 先頭に置く）。
- `runtime/cmake/prebuilt_stage_c6.cmake` -- Xtensa の `prebuilt_stage.cmake` の C6 版
  （`flash_cache_init.o` 無し、manifest schema 2 / `paddrMode fixed-vma`、`flashSize 4MB`）。
