/*
 *  無印ESP32(Xtensa LX6) flash-XIP リンカ（M5＋Wi-Fi合成構成専用・.ctors集約版）
 *
 *  2026-08-01（design.md §10.5/§10.6、LX6+M5+wifi合成構成トライ）。
 *  `esp32_xip_m5.ld`（M5単体・eval/demo用）との唯一の違いはDRAM MEMORY行の
 *  ORIGIN/LENGTHのみ。`esp32_xip_m5.ld`はBTコントローラのハードウェアEM
 *  (Exchange Memory)領域(0x3FFB0000〜0x3FFBDB5C、約56KB)をROM予約として
 *  除外していた（「将来M5+BT合成構成を作る際の互換性」を見込んだ設計）が、
 *  本ファイルが対象とするのはM5+**Wi-Fi**合成構成でありBTは一切enableしない
 *  （`esp_bt_controller_init()`を呼ばない）。BT EM領域を予約する理由が無い
 *  ため、`esp32_xip_wifi.ld`（W1・Wi-Fi単独用）と同じ「全288KB版」を使う。
 *
 *  実測動機：`esp32_xip_m5.ld`のまま（DRAM 232KB）でM5+Wi-Fi合成をリンクした
 *  ところ、DRAM領域が52,256バイトオーバーフローした（M5Unified/M5GFX一式
 *  ＋esp_shim.o（127,920B）＋kernel_cfg.o（91,452B）＋Wi-Fiスタックの
 *  合計がDRAM 232KBに収まらない）。BT予約領域(57,344B)を回収することで
 *  解消する（esp32_xip_wifi.ld冒頭コメントの経緯と同型の問題・同じ解決策）。
 *
 *  将来M5＋Wi-Fi＋BT（3者合成）を作る場合は、本ファイルではなく
 *  `esp32_xip_m5.ld`（BT予約あり版）をベースにする必要がある。
 *
 *  それ以外（メモリマップの他行・start.o配置・.kernel_bss等のレイアウト・
 *  EXCLUDE_FILE(*kernel_cfg.o)によるSRAM常駐分離・.ctors/.init_array集約）は
 *  `esp32_xip_m5.ld`と完全に同一（コメントも含め重複記載しない。詳細は
 *  そちらのファイル冒頭コメント参照）。
 */
/*  無印ESP32(LX6) の ROM 例外ハンドラ表 `_xtos_exc_handler_table` の実体番地。
 *  2026-08-14 訂正(A-2): 旧値 0x3ffe0000 は誤りだった。ROM の実測値は
 *  0x3ffe0448（esp-idf/components/esp_rom/esp32/ld/esp32.rom.ld:1422 の
 *  `PROVIDE(_xtos_exc_handler_table = 0x3ffe0448)`、esp-rom-elfs/20241011/
 *  esp32_rev0_rom.elf の同名シンボルも同値。esp32_devkitc.ld:59 は当初から
 *  0x3ffe0448 であり、XIP 系の ld 群だけがズレていた。
 *  旧値 0x3ffe0000 は ROM 予約SRAM1とアプリ .bss の重なり域の先頭であり、
 *  wifi/m5 系の LX6 構成では生きているアプリ変数が載っている
 *  （実測 seam-lx6-m5-wifi: netif_esp32s3.o の .bss の中）。
 *  なお LX6 は自前VECBASE化（2026-07-14）以降 ROM のこの表を一切引かない
 *  （chip_vectors.S が EXCCAUSE を自前で demux）。core_kernel_impl.c の
 *  define_exc() も 2026-08-14 以降 LX6 ではこの表へ書かないので、
 *  本行は「万一参照されたときに .bss を壊さない」ための保険である。
 *  詳細: 非公開作業記録/20260814-lx6-rom-deps/README.md */
xtos_exc_handler_table = 0x3ffe0448;   /* 無印ESP32: _xtos_exc_handler_table（ROM実測。要target_timer.h整合） */
PROVIDE ( ets_update_cpu_frequency = 0x40008550 );

MEMORY
{
  IROM (rx) : ORIGIN = 0x400D0000, LENGTH = 0x2F0000   /* flash instr XIP */
  DROM (r)  : ORIGIN = 0x3F400000, LENGTH = 0x400000   /* flash data XIP */
  IRAM (rwx): ORIGIN = 0x40080000, LENGTH = 0x20000
  /* DRAM: 0x3FFB0000〜0x3FFF8000(288KB)。BTを一切enableしない合成構成
   * なのでBT EM領域予約は不要——esp32_xip_wifi.ldと同じ全DRAMを使う
   * （ファイル冒頭コメント参照）。 */
  DRAM (rw) : ORIGIN = 0x3FFB0000, LENGTH = 0x48000
}
ENTRY(_start)

SECTIONS
{
  .iram_boot : ALIGN(1024)
  {
    KEEP(*(.kernel_vectors))
    KEEP(*(.entry.text))
    *(.iram_boot .iram_boot.*)
    . = ALIGN(4);
    KEEP(*start.o(.literal .literal.*))
    _fmp_reset_entry = ABSOLUTE(.);
    KEEP(*start.o(.text .text.*))
    *flash_cache_init.o(.literal .text .literal.* .text.*)
    *core_support.o(.literal .text .literal.* .text.*)
    *chip_l1int_entry.o(.literal .text .literal.* .text.*)
    *(.iram1 .iram1.*)
    /*  ★Wi-Fi blob の IRAM_ATTR コード（割込み・cache-off 文脈で走るため
     *  SRAM 必須）。集める記述が無いと孤児セクションとして .flash_text の
     *  **後ろ**へ流され、_flash_text_end の外に出る。flash_xip_map() は
     *  _flash_text_size の分しか MMU へ張らないので、そこを実行した瞬間に
     *  マップ外の命令フェッチになる（2026-09-02、M5Stack Basic 実機で
     *  esp_wifi_start 中の i2c_master_reset=0x401402b8 が
     *  _flash_text_end=0x4013690c を 0x2b8 超えて EXCCAUSE=0）。
     *  S3 の esp32s3_xip_m5.ld には最初から入っている。 */
    *(.wifi0iram .wifi0iram.*)
    *(.wifirxiram .wifirxiram.*)
    *(.wifislpiram .wifislpiram.*)
    *(.wifislprxiram .wifislprxiram.*)
    *(.wifiextrairam .wifiextrairam.*)
    *(.wifiorslpiram .wifiorslpiram.*)
    *(.coexiram .coexiram.*)
    . = ALIGN(4);
  } > IRAM

  _start = _fmp_reset_entry;

  .flash_text : ALIGN(0x10000)
  {
    _flash_text_start = ABSOLUTE(.);
    *(.literal .text .literal.* .text.*)
    *(.init.literal) *(.init) *(.fini.literal) *(.fini)
    . = ALIGN(4);
    _flash_text_end = ABSOLUTE(.);
  } > IROM
  _flash_text_size = _flash_text_end - _flash_text_start;

  .flash_rodata : ALIGN(0x10000)
  {
    _flash_rodata_start = ABSOLUTE(.);
    *(EXCLUDE_FILE(*kernel_cfg.o) .rodata)
    *(EXCLUDE_FILE(*kernel_cfg.o) .rodata.*)
    *(.constdata .constdata.*)
    *(.rodata_wlog_error .rodata_wlog_error.*)
    *(.rodata_wlog_warning .rodata_wlog_warning.*)
    *(.rodata_wlog_info .rodata_wlog_info.*)
    *(.rodata_wlog_debug .rodata_wlog_debug.*)
    *(.rodata_wlog_verbose .rodata_wlog_verbose.*)
    *(.xt_except_table .gcc_except_table*)
    *(.eh_frame .eh_frame.* .eh_frame_hdr)
    *(.xt.lit .xt.lit.* .xt.prop .xt.prop.*)

    /*  ★4 バイト境界へ揃える。esp_run_init_array() は表を 32bit の関数ポインタ
     *  配列として読むので、境界に載っていないと**別物を関数ポインタとして呼ぶ**。
     *  2026-09-02、M5Stack Basic 実機で __ctors_start=0x3f412fb2 /
     *  __ctors_end=0x3f412fb8（6 バイト・非整列）になり、
     *  EXCCAUSE=20 InstrFetchProhibited PC=0x00000074 で落ちた。
     *  S3 側の同じスクリプトには入っている。 */
    . = ALIGN(4);
    __init_array_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = ABSOLUTE(.);
    . = ALIGN(4);
    __ctors_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors))
    __ctors_end = ABSOLUTE(.);

    . = ALIGN(4);
    _flash_rodata_end = ABSOLUTE(.);
  } > DROM
  _flash_rodata_size = _flash_rodata_end - _flash_rodata_start;

  .data : ALIGN(4)
  {
    __data_start = .;
    *(.data .data.*)
    *(.dram1 .dram1.*)
    *(.sdata .sdata.*)
    . = ALIGN(4);
  } > DRAM
  __idata_start = ADDR(.data);
  __idata_end = __idata_start + SIZEOF(.data);

  .sram_rodata : ALIGN(4)
  {
    *kernel_cfg.o(.rodata .rodata.*)
    . = ALIGN(4);
  } > DRAM

  .kernel_bss (NOLOAD) : ALIGN(4)
  {
    __bss_start = .;
    _kernel_isolated_start = ABSOLUTE(.);
    *kernel_cfg.o(.bss .bss.* COMMON)
    *core_kernel_impl.o(.bss .bss.* COMMON)
    *chip_kernel_impl.o(.bss .bss.* COMMON)
    *chip_ipi.o(.bss .bss.* COMMON)
    *target_timer.o(.bss .bss.* COMMON)
    *taskhook.o(.bss .bss.* COMMON)
    *time_event.o(.bss .bss.* COMMON)
    . = ALIGN(4);
    _kernel_isolated_end = ABSOLUTE(.);
  } > DRAM

  ASSERT(_kernel_isolated_end <= 0x3FFE0000,
         "FATAL: .kernel_bss(TCB/stacks) overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")

  /*  2026-08-02: .diag_noinit/.noinit を .bss より前（.kernel_bss 直後）に配置する。
   *  この構成は diag_recorder が no-op スタブでなく実体を持つため
   *  SIZEOF(.diag_noinit)==0 が成立せず、アドレスが実際に効く。ヒープ
   *  （esp_shim.o の heap_area、.bss 末尾）を124KB既定へ戻すとROM予約
   *  (0x3FFE0000)へ意図的にはみ出す設計（esp32_xip_wifi.ldと同型、非
   *  クリティカルなblobバッファのみ許容）になるため、.bssより後ろに
   *  diag_noinit/noinitを置くとその境界チェックが即座に壊れる。 */
  .diag_noinit (NOLOAD) : ALIGN(4)
  {
    _diag_noinit_start = ABSOLUTE(.);
    *(.diag_noinit .diag_noinit.*)
    . = ALIGN(4);
    _diag_noinit_end = ABSOLUTE(.);
  } > DRAM

  .noinit (NOLOAD) : ALIGN(4) { *(.noinit*) } > DRAM

  ASSERT(SIZEOF(.diag_noinit) == 0 || _diag_noinit_end <= 0x3FFE0000,
         "FATAL: .diag_noinit(diag recorder) overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")
  ASSERT(SIZEOF(.noinit) == 0 || (ADDR(.noinit) + SIZEOF(.noinit)) <= 0x3FFE0000,
         "FATAL: .noinit overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")

  .bss (NOLOAD) : ALIGN(4)
  {
    _bt_bss_start = ABSOLUTE(.); _bt_bss_end = ABSOLUTE(.);
    _bt_controller_bss_start = ABSOLUTE(.); _bt_controller_bss_end = ABSOLUTE(.);
    _bt_data_start = ABSOLUTE(.); _bt_data_end = ABSOLUTE(.);
    _bt_controller_data_start = ABSOLUTE(.); _bt_controller_data_end = ABSOLUTE(.);
    *(EXCLUDE_FILE(
        *kernel_cfg.o *core_kernel_impl.o *chip_kernel_impl.o *chip_ipi.o
        *target_timer.o *taskhook.o *time_event.o
      ) .bss .bss.*)
    *(EXCLUDE_FILE(
        *kernel_cfg.o *core_kernel_impl.o *chip_kernel_impl.o *chip_ipi.o
        *target_timer.o *taskhook.o *time_event.o
      ) .dram0.bss)
    *(EXCLUDE_FILE(
        *kernel_cfg.o *core_kernel_impl.o *chip_kernel_impl.o *chip_ipi.o
        *target_timer.o *taskhook.o *time_event.o
      ) .sbss .sbss.*)
    *(EXCLUDE_FILE(
        *kernel_cfg.o *core_kernel_impl.o *chip_kernel_impl.o *chip_ipi.o
        *target_timer.o *taskhook.o *time_event.o
      ) COMMON)
    . = ALIGN(4);
  } > DRAM

  __bss_end = ADDR(.bss) + SIZEOF(.bss);

  PROVIDE ( _bss_start = __bss_start );
  PROVIDE ( _bss_end = __bss_end );

  /DISCARD/ : { *(.comment) *(.note.*) }
}
