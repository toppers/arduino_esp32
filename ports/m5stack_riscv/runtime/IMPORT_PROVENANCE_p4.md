# `ports/m5stack_riscv` の ESP32-P4（M5Stamp-P4）層の出典と改変

- 取込み 2026-09-17（StampP4 計画 段A1、dev `.steering/20260917-stampp4-arduino-plan/PLAN.md`）。
- 出典: dev `fmp3_esp_idf_dev` **`a745318658e2ef77cd94c126c2d0ce942f92925a`**（main。
  `packaging/release-allowlist.json` の `portBaseCommitP4` と同じ）。P4 層そのものの最終変更は
  2026-08（dev 段1-6 / 段7 系）で、`seam-p4-smp` preset は **M5Stamp ESP32P4 rev v1.3 の実機で
  起動済み**（dev `.steering/20260814-p4-stage2/`、`20260815-p4-stage6/`）。
- dev 側の chip 層は fmp3_core（submodule、両 repo とも `685b36a9`）の
  `arch/riscv_gcc/esp32p4` のバイト同一複製（dev `chip.cmake` 冒頭の注記）で、arduino 側でも
  fmp3_core は無改変で `add_subdirectory` する。共通 arch（`arch/riscv_gcc/common`: start.S、
  clic_kernel_impl.c、msi_ipi.c、mtimer.c）は fmp3_core 側から読む。
- **持ち込まなかったもの**: `chip_kernel.trb` / `target_*.trb`（cfg_py 一本）、`Makefile.chip` /
  `MANIFEST` / 設計文書 `*.md`（chip_design / esp32p4_hw_reference / fmp3_port_mapping /
  idf_riscv_intr_impl / mtrans2_lost_wakeup_analysis / pie_hwlp_design / target_user）、
  `gdb.ini` / `esp32p4_openocd_jtag.md` / `idf_image_integration.md`、`esp32p4_fmp.ld`
  （方式(a) 用。seam 一本）、`tools/`（fmp_app / fmp_loader）、`esp/boot/seam_p4_trace.c`
  （診断専用）、`esp/boot/seam_p4/`（dev 製 bootloader。stock bootloader を使う: 計画 P1）。
- 監査: pristine 監査台本は置かない（C6 / C5 段1 と同じ判断）。下表の sha256 と
  「identical」列が照合の根拠。照合コマンド:
  `cmp <dev>/fmp3/arch/riscv_gcc/esp32p4/<f> ports/m5stack_riscv/runtime/arch/riscv_gcc/esp32p4/<f>`

## 改変（1 ファイルのみ）

| ファイル | 出典との差 | 理由 |
|---|---|---|
| `target/m5stamp_esp32p4_gcc/esp32p4_xip.ld` | `.flash_rodata` の末尾に C++ の init/fini/ctors/dtors 表と `.eh_frame` を **追加**（C5 の `esp32c5_xip.ld`、C6 の `esp32c6_xip.ld` と同じ追加） | Arduino bridge の `esp_run_init_array()` が `__init_array_start` 等を要求する。出力セクションを増やさず `.flash_rodata` の中に置くのは、bootloader が flash セグメント**ちょうど 2 本**を課すため（同ファイル冒頭 (1)、driver C-1） |

上の表のほかは **すべてバイト同一**（下表 identical=yes）。`target.cmake` / `chip.cmake` も
無改変——dev の `A1_P4_LDSCRIPT` / `FMP3_BOARD` の口を arduino 側の `CMakeLists.txt` が
そのまま使う（`A1_${TAG}_LDSCRIPT` with TAG=P4）。

## arduino 側で新規に書いたもの（出典なし）

| ファイル | 内容 |
|---|---|
| `cmake/prebuilt_stage_p4.cmake` | `prebuilt_stage_c5.cmake` の写し + ROM_LDS 空を許容 + XIP ld の `INCLUDE` 断片を stage 側の写しへインライン + flash 16MB/qio |
| `cmake/toolchain-riscv-esp32p4.cmake` | `toolchain-riscv-esp32c5.cmake` の写し（esp-14.2.0_20260121 に fail-closed 固定。dev の P4 は 20241119 名指しだが、この機械にも M5Stack core にも無い） |
| `app/phase3_p4/phase3_arduino_app.{c,h,cfg}` | `app/phase3/` の写し + PRC2 の `[P4-CORE2] alive N` 周期タスク（`CLS_PRC2`） |

## 段A1 で確定した数値（P4 固有）

| 項目 | 値 | 出典 |
|---|---|---|
| seam 静的 RAM 上限 | 0x4FF2CBD0（183,248 B） | `esp32p4_xip.ld` MEMORY、esp32p4_es-libs 3.3.8 `bootloader_qio_80m.elf` の `.iram_loader.text` @0x4ff2cbd0 |
| HP SRAM | 0x4FF00000..0x4FFC0000 | soc.h:167-170 |
| flash 窓 | 0x40000000..0x44000000（D/I 共有） | soc.h:152-155 |
| MMU ページ | 64 KiB | esp32p4_es-libs sdkconfig |
| chip_id / rev | 0x0012 / v1.3 = 103 | esp_app_format.h:27、dev 段2 |
| Blink の像 | 88,880 B（mapped 2 = DROM 4,896 + IROM 23,052、RAM 2 = .iram_text 796 + .data 30,384）、C-1..C-9 満足 | driver の報告（段A2） |

## ファイル表

| arduino_esp32 file | dev source (a7453186) | identical | sha256 (arduino copy) |
|---|---|---|---|
| `arch/riscv_gcc/esp32p4/chip_asm.inc` | `fmp3/arch/riscv_gcc/esp32p4/chip_asm.inc` | yes | `1c15557839fee0a8…` |
| `arch/riscv_gcc/esp32p4/chip.cmake` | `fmp3/arch/riscv_gcc/esp32p4/chip.cmake` | yes | `6d565ed31a2ec005…` |
| `arch/riscv_gcc/esp32p4/chip_kernel.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_kernel.h` | yes | `0b4f9b3e1b5a5e26…` |
| `arch/riscv_gcc/esp32p4/chip_kernel_impl.c` | `fmp3/arch/riscv_gcc/esp32p4/chip_kernel_impl.c` | yes | `63cbd0026fcfe00e…` |
| `arch/riscv_gcc/esp32p4/chip_kernel_impl.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_kernel_impl.h` | yes | `f26f37e790f06652…` |
| `arch/riscv_gcc/esp32p4/chip_kernel.py` | `fmp3/arch/riscv_gcc/esp32p4/chip_kernel.py` | yes | `cd2b55b1d02f083c…` |
| `arch/riscv_gcc/esp32p4/chip_rename.def` | `fmp3/arch/riscv_gcc/esp32p4/chip_rename.def` | yes | `4fdbd73c5a9f80d0…` |
| `arch/riscv_gcc/esp32p4/chip_rename.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_rename.h` | yes | `b9d8afc2412e360f…` |
| `arch/riscv_gcc/esp32p4/chip_serial.c` | `fmp3/arch/riscv_gcc/esp32p4/chip_serial.c` | yes | `19d32b6d6aed9288…` |
| `arch/riscv_gcc/esp32p4/chip_serial.cfg` | `fmp3/arch/riscv_gcc/esp32p4/chip_serial.cfg` | yes | `0e96061b4f699a64…` |
| `arch/riscv_gcc/esp32p4/chip_serial.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_serial.h` | yes | `9e862df597228ad4…` |
| `arch/riscv_gcc/esp32p4/chip_sil.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_sil.h` | yes | `0a3c8cc70ebe9248…` |
| `arch/riscv_gcc/esp32p4/chip_stddef.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_stddef.h` | yes | `487dc5b588f0eb14…` |
| `arch/riscv_gcc/esp32p4/chip_support.S` | `fmp3/arch/riscv_gcc/esp32p4/chip_support.S` | yes | `9cbcf75fc63e6324…` |
| `arch/riscv_gcc/esp32p4/chip_timer.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_timer.h` | yes | `e14e4d72c5e0316e…` |
| `arch/riscv_gcc/esp32p4/chip_unrename.h` | `fmp3/arch/riscv_gcc/esp32p4/chip_unrename.h` | yes | `4b7aef17a2999376…` |
| `arch/riscv_gcc/esp32p4/clic_kernel.py` | `fmp3/arch/riscv_gcc/esp32p4/clic_kernel.py` | yes | `4887fe931bdcbbf1…` |
| `arch/riscv_gcc/esp32p4/clint_ipi.h` | `fmp3/arch/riscv_gcc/esp32p4/clint_ipi.h` | yes | `8b626043fde28fc4…` |
| `arch/riscv_gcc/esp32p4/esp32p4.h` | `fmp3/arch/riscv_gcc/esp32p4/esp32p4.h` | yes | `7c50277952228ca0…` |
| `target/m5stamp_esp32p4_gcc/esp32p4_xip.ld` | `fmp3/target/m5stamp_esp32p4_gcc/esp32p4_xip.ld` | NO | `03df5cdd265d8ecf…` |
| `target/m5stamp_esp32p4_gcc/m5stamp_esp32p4_kit.h` | `fmp3/target/m5stamp_esp32p4_gcc/m5stamp_esp32p4_kit.h` | yes | `917eae7d28f42375…` |
| `target/m5stamp_esp32p4_gcc/p4_bss_high.ld` | `fmp3/target/m5stamp_esp32p4_gcc/p4_bss_high.ld` | yes | `41811d21fd515b8d…` |
| `target/m5stamp_esp32p4_gcc/p4_dram_extra.ld` | `fmp3/target/m5stamp_esp32p4_gcc/p4_dram_extra.ld` | yes | `3d4b7db654b6c664…` |
| `target/m5stamp_esp32p4_gcc/p4_iram_extra.ld` | `fmp3/target/m5stamp_esp32p4_gcc/p4_iram_extra.ld` | yes | `53456f210e747f1a…` |
| `target/m5stamp_esp32p4_gcc/p4_mem_high.ld` | `fmp3/target/m5stamp_esp32p4_gcc/p4_mem_high.ld` | yes | `2258fdf04d11312c…` |
| `target/m5stamp_esp32p4_gcc/tab5_kit.h` | `fmp3/target/m5stamp_esp32p4_gcc/tab5_kit.h` | yes | `ba74c3a1ba78957d…` |
| `target/m5stamp_esp32p4_gcc/target_asm.inc` | `fmp3/target/m5stamp_esp32p4_gcc/target_asm.inc` | yes | `4ea33109ab2b3aaf…` |
| `target/m5stamp_esp32p4_gcc/target_cfg1_out.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_cfg1_out.h` | yes | `5253560d243bd997…` |
| `target/m5stamp_esp32p4_gcc/target_check.py` | `fmp3/target/m5stamp_esp32p4_gcc/target_check.py` | yes | `92fb265640281a08…` |
| `target/m5stamp_esp32p4_gcc/target_class.py` | `fmp3/target/m5stamp_esp32p4_gcc/target_class.py` | yes | `c281eca33a3d7269…` |
| `target/m5stamp_esp32p4_gcc/target.cmake` | `fmp3/target/m5stamp_esp32p4_gcc/target.cmake` | yes | `13f5fecd10474ed1…` |
| `target/m5stamp_esp32p4_gcc/target_ipi.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_ipi.h` | yes | `59028a74b95d2889…` |
| `target/m5stamp_esp32p4_gcc/target_kernel.cfg` | `fmp3/target/m5stamp_esp32p4_gcc/target_kernel.cfg` | yes | `80df31563ee9757d…` |
| `target/m5stamp_esp32p4_gcc/target_kernel.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_kernel.h` | yes | `ec5d34951c611a96…` |
| `target/m5stamp_esp32p4_gcc/target_kernel_impl.c` | `fmp3/target/m5stamp_esp32p4_gcc/target_kernel_impl.c` | yes | `95a92d3685059206…` |
| `target/m5stamp_esp32p4_gcc/target_kernel_impl.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_kernel_impl.h` | yes | `9e9d23e35a47634c…` |
| `target/m5stamp_esp32p4_gcc/target_kernel.py` | `fmp3/target/m5stamp_esp32p4_gcc/target_kernel.py` | yes | `67ffc11e2c714c87…` |
| `target/m5stamp_esp32p4_gcc/target_rename.def` | `fmp3/target/m5stamp_esp32p4_gcc/target_rename.def` | yes | `0708b753ccd551f8…` |
| `target/m5stamp_esp32p4_gcc/target_rename.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_rename.h` | yes | `7551fd665e9b9df9…` |
| `target/m5stamp_esp32p4_gcc/target_serial.cfg` | `fmp3/target/m5stamp_esp32p4_gcc/target_serial.cfg` | yes | `9d4cffb458e804ae…` |
| `target/m5stamp_esp32p4_gcc/target_serial.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_serial.h` | yes | `8b88eca3c5100126…` |
| `target/m5stamp_esp32p4_gcc/target_sil.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_sil.h` | yes | `3242c3fe97a449a4…` |
| `target/m5stamp_esp32p4_gcc/target_stddef.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_stddef.h` | yes | `621aa0a98ae8fd61…` |
| `target/m5stamp_esp32p4_gcc/target_syssvc.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_syssvc.h` | yes | `4662bf48f6201419…` |
| `target/m5stamp_esp32p4_gcc/target_test.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_test.h` | yes | `2a1210f2bccec66b…` |
| `target/m5stamp_esp32p4_gcc/target_timer.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_timer.h` | yes | `972925ce8c94e889…` |
| `target/m5stamp_esp32p4_gcc/target_unrename.h` | `fmp3/target/m5stamp_esp32p4_gcc/target_unrename.h` | yes | `4c0089c84c690eea…` |
| `target/m5stamp_esp32p4_gcc/unit_poe_p4_kit.h` | `fmp3/target/m5stamp_esp32p4_gcc/unit_poe_p4_kit.h` | yes | `b0dd5374c38a0992…` |
| `seam/seam_p4_appdesc.c` | `esp/boot/seam_p4_appdesc.c` | yes | `2f8a91c8b3e8fcc3…` |
| `seam/seam_p4_entry.S` | `esp/boot/seam_p4_entry.S` | yes | `4af97aad54ff6be2…` |
| `seam/seam_p4_clk.c` | `esp/boot/seam_p4_clk.c` | yes | `b194d62b8ec144ba…` |
| `seam/seam_p4_core1.c` | `esp/boot/seam_p4_core1.c` | yes | `7761e8609eec7106…` |
| `seam/seam_p4_core1_entry.S` | `esp/boot/seam_p4_core1_entry.S` | yes | `602145f15d58a20c…` |
