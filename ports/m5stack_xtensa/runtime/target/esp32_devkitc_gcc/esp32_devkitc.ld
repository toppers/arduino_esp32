/*
 *  リンカスクリプト（無印ESP32(Xtensa LX6) QEMU PoC用）
 *
 *  [2026-07-13新規] esp32s3_devkitc_gcc/esp32s3_devkitc.ldを雛形に作成。
 *  本PoCの範囲（vector table初期化＋UARTポーリング出力のみ、
 *  tasklist.md T1・非公開作業記録/20260713-esp32-classic-poc/design.md参照）は
 *  QEMUのみでの検証であり、実機の物理SRAM重複問題（S3版の本コメント
 *  参照）の検証は対象外。
 *
 *  無印ESP32のメモリマップ（一次情報：ESP-IDFの
 *  components/esp_system/ld/esp32/memory.ld.in）：
 *    IRAM: iram0_0_seg  org=0x40080000  len=0x20000（128KB、SRAM1拡張分は
 *          CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM未設定時は含めない）
 *    DRAM: dram0_0_seg   org=0x3FFB0000  len≈0x2c200（既定、CONFIG_BTDM_
 *          RESERVE_DRAM=0時）
 *  無印ESP32(LX6)はESP32-S3(LX7)のような単純なIRAM/DRAM同一物理デュアル
 *  マップではなく、SRAM0/SRAM1/SRAM2に分かれた複雑なバンク構成
 *  （要実機検証、design.md §5「実機の挙動は一切分からない」参照）。
 *  本PoCはQEMU専用のため、上記2領域から十分に離れた保守的な範囲のみを
 *  使用し、この複雑さの解決は次フェーズへ委ねる。
 */

OUTPUT_ARCH(xtensa)

MEMORY
{
    IRAM(rx)  : ORIGIN = 0x40080000, LENGTH = 64k
    DRAM(rw)  : ORIGIN = 0x3FFB0000, LENGTH = 64k
}

ENTRY(_start)

/*
 * ROM常駐のXTOS API（VECBASE非変更で割込みハンドラを登録するための
 * 唯一の手段。design.md、target_timer.h参照）。
 * 一次情報：esp-rom-elfs/20241011/esp32_rev0_rom.elf（esp32s3.rom.ld相当の
 * 公式ldスクリプトが無印ESP32には存在しないため、nmで実測。
 * design.md §6参照）。
 *
 * 命名差異：無印ESP32のROMは_xtos_exc_handler_table（先頭アンダースコア
 * 有り）。S3は xtos_exc_handler_table（アンダースコア無し）。本ファイルは
 * アドレスを直接埋め込むため、この命名差異はcore_kernel_impl.c側の
 * 参照名（xtos_exc_handler_table、アンダースコア無し）とは無関係に解決
 * できる。
 */
_xtos_ints_on                    = 0x4000bf88;
_xtos_set_interrupt_handler      = 0x4000bf78;
_xtos_set_interrupt_handler_arg  = 0x4000bf34;
_xtos_set_exception_handler      = 0x4000074c;

/*
 * ROM常駐のXTOS汎用Level1割込みディスパッチャ本体。design.md §6参照。
 */
_xtos_l1int_handler               = 0x40000814;

/*
 * ROMのEXCCAUSEディスパッチテーブル本体（RAM常駐）。design.md §6参照。
 */
xtos_exc_handler_table            = 0x3ffe0448;

/*
 * CPUクロックPLL昇圧（target_kernel_impl.c esp32_cpu_clock_init_pll）が使う
 * ROM関数。一次情報：ESP-IDF v5.5 esp_rom/esp32/ld/esp32.rom.ld。
 *  - rom_i2c_writeReg：BBPLL regi2cレジスタ書込み（block,host_id,reg,data）
 *  - ets_delay_us：g_ticks_per_us基準のマイクロ秒ビジー遅延
 *  - ets_update_cpu_frequency_rom：g_ticks_per_us更新（自コアのみ）
 */
rom_i2c_writeReg                  = 0x400041a4;
ets_delay_us                      = 0x40008534;
ets_update_cpu_frequency_rom      = 0x40008550;

/*
 * 【本PoCでは未使用・未到達】ROM常駐のレジスタウィンドウ全スピル
 * ルーチン。無印ESP32(LX6)のROM・トゥールチェーン静的ライブラリの
 * どちらにも存在しないことを確認済み（design.md §6）。本PoCは
 * dispatch()/migrate()を実行しないため未定義のままでよいが、
 * core_support.Sのcall0参照を解決するため、chip層のプレースホルダ
 * スタブ（chip_window_spill_stub.S）側でシンボルを定義する
 * （本リンカスクリプトでは定義しない）。
 */

SECTIONS
{
    /*
     * 自前ベクタテーブル（core_vectors.S）。VECBASEは1KB境界必須のため
     * 1024整列でIRAM先頭付近に配置する。start.SでVECBASEを_kernel_vectorsへ
     * 設定する。
     */
    .kernel_vectors : ALIGN(1024)
    {
        KEEP(*(.kernel_vectors))
        . = ALIGN(4);
    } > IRAM

    .text : ALIGN(4)
    {
        *(.entry.text)
        *(.init.literal)
        *(.init)
        *(.literal .text .literal.* .text.*)
        . = ALIGN(4);
    } > IRAM

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.* .constdata .constdata.*)
        . = ALIGN(4);
    } > DRAM

    __idata_start = .;
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
    } > DRAM
    __idata_end = __idata_start + SIZEOF(.data);

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DRAM

    .noinit (NOLOAD) : ALIGN(4)
    {
        __noinit_start = .;
        *(.noinit*)
        . = ALIGN(4);
        __noinit_end = .;
    } > DRAM
}
