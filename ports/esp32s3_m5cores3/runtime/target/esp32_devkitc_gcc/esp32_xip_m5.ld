/*
 *  無印ESP32(Xtensa LX6) flash-XIP リンカ（M5 変種：.ctors 集約版）
 *
 *  esp32_xip.ld（W1 Wi-Fi/W2 BLE 用）を土台に、.flash_rodata へ
 *  C++ グローバル/静的コンストラクタ表（.init_array／.ctors）の KEEP 集約を
 *  追加しただけのもの。それ以外（メモリマップ・start.o 配置・.kernel_bss等の
 *  レイアウト・EXCLUDE_FILE(*kernel_cfg.o)によるSRAM常駐分離）は無改変。
 *  Phase H（M0C・非公開作業記録/20260710-m5-arduino-idf-mode/tasklist.md）。
 *
 *  S3側の esp32s3_xip_m5.ld を単純に写さないこと：あちらは
 *  EXCLUDE_FILE(*kernel_cfg.o) による .sram_rodata 分離を**持たない**（S3の
 *  seam起動はbootloaderからICache/DCache有効のまま制御を受け取るため、
 *  start.SがSP初期化でDROMを読んでも安全＝分離が不要）。LX6はESP-IDFの
 *  2nd-stage bootloaderからではなくROM Direct Bootから起動し、start.Sが
 *  flash_xip_map()（flash cacheのMMUマップ）より**前**に_kernel_istkpt_table
 *  からSPを読む（esp32_xip.ld:193-198のコメント参照）。この時点でDROMは
 *  未マップなので、kernel_cfg.oのrodataをDROMに置くとブートローダの
 *  キャッシュ残渣依存の不定読み→不正SP→ハングになる。したがって本ファイルは
 *  esp32_xip.ldの分離パターンを**そのまま維持する**（S3のM5変種とは
 *  ここだけ構造が異なる）。
 *
 *  start.oのバイト化け既知問題（0C-3a参照）：本ファイルはesp32_xip.ldと
 *  同じALIGN(4)対策のみを踏襲する。これはW1(wifi_sta)・W3(bt_classic_ctrl_smoke)
 *  規模では実機検証済みだが、より大きい構成（ble_hs_smoke規模、および本ファイルを
 *  使うM5Unified/M5GFXビルド）では対策が不十分な可能性がある未解決の懸念として
 *  残る。0C-5のAC-7判定時に、リンク成功だけでなく`_start`が指すコードが実際に
 *  妥当な命令列であることをobjdump等で確認すること（README/tasklist参照）。
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
/*  ets_update_cpu_frequency は無印ROMに _rom サフィックス付きで存在
 *  (esp32.rom.ld: ets_update_cpu_frequency_rom=0x40008550)。blob/periph_ctrlが
 *  サフィックス無しで参照するためエイリアスを供給。 */
PROVIDE ( ets_update_cpu_frequency = 0x40008550 );

MEMORY
{
  /* Flash XIP 窓（64KB MMUページ、cache_flash_mmu_set でマップ）。
   * IROM: 命令キャッシュ窓 0x400D0000〜0x40400000（約3MB）。
   * DROM: データキャッシュ窓 0x3F400000〜0x3F800000（4MB）。 */
  IROM (rx) : ORIGIN = 0x400D0000, LENGTH = 0x2F0000   /* flash instr XIP */
  DROM (r)  : ORIGIN = 0x3F400000, LENGTH = 0x400000   /* flash data XIP */
  /* SRAM: IRAM(SRAM0) 0x40080000〜0x400A0000(128KB)。起動コード+M5 iram。 */
  IRAM (rwx): ORIGIN = 0x40080000, LENGTH = 0x20000
  /* DRAM: 0x3FFBE000〜0x3FFF8000(232KB)。esp32_xip.ldと同じくBT EM/BSS/MISC
   * 領域(0x3FFB0000〜0x3FFBDB5C、約56KB)をROM予約として除外する
   * （M5変種はBT blobをリンクしないが、レイアウトはesp32_xip.ldと合わせ、
   * 将来M5+BT合成構成を作る際の互換性を保つ）。 */
  DRAM (rw) : ORIGIN = 0x3FFBE000, LENGTH = 0x3A000
}
ENTRY(_start)

SECTIONS
{
  /* --- SRAM: 起動コード（ROM Direct Bootがロード、フラッシュマップ前に実行）--- */
  .iram_boot : ALIGN(1024)
  {
    KEEP(*(.kernel_vectors))
    KEEP(*(.entry.text))
    *(.iram_boot .iram_boot.*)
    /*  esp32_xip.ldと同じ理由でstart.oをKEEP化する（--gc-sectionsによる
     *  誤破棄防止）。ALIGN(4)対策の詳細・start.oバイト化け既知問題の全文は
     *  esp32_xip.ld:75-126のコメント参照（重複記載しない）。 */
    . = ALIGN(4);
    KEEP(*start.o(.literal .literal.*))
    _fmp_reset_entry = ABSOLUTE(.);
    KEEP(*start.o(.text .text.*))
    *flash_cache_init.o(.literal .text .literal.* .text.*)
    /* 割込み/例外エントリとコンテキストスイッチ核はSRAMに置く
     * （esp32_xip.ldと同じ理由：ベクタ表からのj命令の到達範囲・
     * cache-off文脈での実行が必須なため）。 */
    *core_support.o(.literal .text .literal.* .text.*)
    *chip_l1int_entry.o(.literal .text .literal.* .text.*)
    *(.iram1 .iram1.*)
    . = ALIGN(4);
  } > IRAM

  /*  esp32_xip.ldと同じ_start確定パターン（ROM側PROVIDEに対する通常代入での
   *  優先確定）。詳細はesp32_xip.ld:158-175のコメント参照。 */
  _start = _fmp_reset_entry;

  /* --- Flash IROM: 本体コード（XIP、MMUマップ後にアクセス）--- */
  .flash_text : ALIGN(0x10000)
  {
    _flash_text_start = ABSOLUTE(.);
    *(.literal .text .literal.* .text.*)
    *(.init.literal) *(.init) *(.fini.literal) *(.fini)
    . = ALIGN(4);
    _flash_text_end = ABSOLUTE(.);
  } > IROM
  _flash_text_size = _flash_text_end - _flash_text_start;

  /* --- Flash DROM: rodata（XIP）--- */
  .flash_rodata : ALIGN(0x10000)
  {
    _flash_rodata_start = ABSOLUTE(.);
    /* kernel_cfg.o の .rodata は SRAM常駐(.sram_rodata)へ分離するため除外。
     * 理由はファイル冒頭コメント参照（S3のM5変種と違いLX6はこの分離が必須）。 */
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

    /* --- C++ グローバル/静的コンストラクタ表シンボル（.init_array）---
     * S3のesp32s3_xip_m5.ldと同じ役割・同じ理由（design.md §7 変更点②の
     * LX6版）。走査・呼出しはm5/compat/cxx_runtime.cppのesp_run_init_array()
     * （アプリmain_task先頭で1回呼ぶ）。golden保護構成（seam-lx6-smp/wifi）は
     * このファイルを一切参照しないため無関係・不変。 */
    __init_array_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = ABSOLUTE(.);
    /* --- .ctors 集約（M5 グローバルコンストラクタをesp_run_init_array()が走査）---
     * ツールチェーンesp-14.2.0はC++ ctorを.init_arrayではなく.ctorsへ出力する
     * （S3側1-3実測と同じツールチェーン挙動。xtensa-esp-elf共通）。
     * A1_VARIANT=mはBT/Wi-Fi blobを一切リンクしないため、ここで復活する
     * blob .ctorsは存在せず、単純KEEPで安全（S3のesp32s3_xip_m5.ldと同じ
     * 前提）。将来M5+blob合成構成を作る場合はEXCLUDE_FILEでblobオブジェクトの
     * .ctorsを除外する行を追加すること。 */
    __ctors_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors))
    __ctors_end = ABSOLUTE(.);

    . = ALIGN(4);
    _flash_rodata_end = ABSOLUTE(.);
  } > DROM
  _flash_rodata_size = _flash_rodata_end - _flash_rodata_start;

  /* --- SRAM DRAM: data/bss（LMA=VMA、ROM Direct Bootが初期値をSRAMへ配置）--- */
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

  /* SRAM常駐 rodata：kernel_cfg.o の const テーブル（_kernel_istkpt_table 等）。
   * esp32_xip.ld:227-236と同じ理由・同じ配置。 */
  .sram_rodata : ALIGN(4)
  {
    *kernel_cfg.o(.rodata .rodata.*)
    . = ALIGN(4);
  } > DRAM

  /* .kernel_bss を.bssより先の低位DRAMへ配置する（esp32_xip.ld:238-249と
   * 同じ理由：ROM予約SRAM1(0x3FFE0000〜)からTCB/スタック等を守るため）。 */
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

  /DISCARD/ : { *(.comment) *(.note.*) }
}
