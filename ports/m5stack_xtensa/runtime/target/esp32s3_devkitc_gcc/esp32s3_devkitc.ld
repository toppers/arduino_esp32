/*
 *  リンカスクリプト（ESP32-S3-DevKitC-1 / FMP3 用）
 *
 *  ESP32-S3のメモリマップ（一次情報：ESP-IDFの
 *  components/esp_system/ld/esp32s3/memory.ld.in）：
 *    IRAM: SRAM_IRAM_START=0x40370000 〜 SRAM_IRAM_END=0x403CB700
 *          （0x403B9000以降はESP-IDFの2nd stage bootloader起動処理が
 *          使う領域とコメントされているため避ける）
 *    DRAM: SRAM_DRAM_START=0x3FC88000〜
 *
 *  本作業単位はESP-IDFのフラッシュキャッシュ・MMU設定を一切行わない
 *  （flash直接実行=XIPは使わない）ため、ICACHE/DCACHE予約領域の厳密な
 *  計算は不要。念のためIRAM/DRAMの両端から離れた保守的な範囲のみを
 *  使用する（実機検証で要再確認）。
 *
 *  VECBASEは変更しない方針のため（design.md参照）、RP2350版のような
 *  ベクタ表(.vector)をリンカスクリプト内で特別に先頭配置する必要はない。
 *  イメージ変換（esptool.py elf2image）はELFのe_entry（ENTRY()指定）を
 *  そのままエントリポイントとして使う。
 */

OUTPUT_ARCH(xtensa)

/*
 *  【重要・実機必須】IRAMとDRAMは物理的に非重複でなければならない（eb9cbda）。
 *  ESP32-S3のSRAM1はデュアルマップで、同一物理バイトがIRAMビュー(命令)と
 *  DRAMビュー(データ)の両方に現れる：iram_addr = dram_addr + 0x6F0000
 *  （SRAM1 IRAM先頭0x40378000 − DRAM先頭0x3FC88000）。IRAM=0x40380000と
 *  DRAM=0x3FC90000は 0x3FC90000+0x6F0000=0x40380000 で同一物理SRAMを指し、
 *  コード領域とデータ(割込みスタック等)が物理的に重なる。実機ではコード領域は
 *  命令用に構成されデータ書込みがStoreProhibited例外になる（かつ書けてもコードを
 *  破壊する）。これが実機の非決定的クラッシュ・ブート不成立の真因（QEMUは
 *  デュアルマップを厳密に模さないため露見しない）。
 *
 *  別PCのTTSP3 API自動生成テスト用のIRAM/DRAM拡張
 *  （IRAM224k/DRAM280kを同一origin=物理重複配置）はQEMU（IRAM/DRAMが別メモリ）
 *  では動くが実機では上記の物理重複で破綻する。実機必須の非重複を維持しつつ、
 *  使用可能な物理SRAM [0x3FC90000, 0x3FCDB700]（IRAMビュー上限0x403B9000＝物理
 *  0x3FCC9000も考慮）をIRAM/DRAMで分割して両者をできる限り拡張する：
 *    IRAM(160k)=物理0x3FC90000〜0x3FCB8000、DRAM=物理0x3FCB8000〜、非重複。
 *  ※.textが160KB超・.dataが140KB超になる最大級のTTSP3 APIテストは、内蔵SRAM
 *  （非重複で~300KB）に収まらず実機SRAM単独では不可（QEMU、またはflash XIPが必要）。
 */
MEMORY
{
    IRAM(rx)  : ORIGIN = 0x40380000, LENGTH = 160k
    DRAM(rw)  : ORIGIN = 0x3FCB8000, LENGTH = 140k
}

ENTRY(_start)

/*
 * ROM常駐のXTOS API（VECBASE非変更で割込みハンドラを登録するための
 * 唯一の手段。design.md、target_timer.h参照）。
 * 一次情報：esp-idf/components/esp_rom/esp32s3/ld/esp32s3.rom.ld
 */
_xtos_ints_on                    = 0x40001bfc;
_xtos_set_interrupt_handler      = 0x40001c20;
_xtos_set_interrupt_handler_arg  = 0x40001c2c;
_xtos_set_exception_handler      = 0x40001c14;

/*
 * ROM常駐のXTOS汎用Level1割込みディスパッチャ本体（EXCCAUSE_LEVEL1_
 * INTERRUPTスロットへ_xtos_set_exception_handlerで登録する対象）。
 * ESP-IDFのesp32s3.rom.ldには公式シンボル定義が無いため、QEMUのROM ELF
 * （esp-rom-elfs/20241011/esp32s3_rev0_rom.elf）をnmで実測したアドレス。
 * target_timer.hのコメント参照。
 */
_xtos_l1int_handler               = 0x40034eb8;

/*
 * ROMのEXCCAUSEディスパッチテーブル本体（RAM常駐、_UserExceptionVector
 * がEXCCAUSEでインデックスして飛ぶ関数ポインタ配列）。
 * _xtos_set_exception_handler()はこのテーブルの該当スロットに必ず
 * xtos_c_wrapper_handler（Cの例外ハンドラ用ラッパ）を書き込むため、
 * Level1割込みの生ハンドラ_xtos_l1int_handlerを登録する用途には使えない
 * （ラッパ経由になり割込みがディスパッチされない。実測で判明）。
 * このテーブルへ直接_xtos_l1int_handlerを書き込むために実体アドレスを
 * 結び付ける（QEMUのROM ELFをnmで実測：xtos_exc_handler_table）。
 */
xtos_exc_handler_table            = 0x3fcedf68;

/*
 * ROM常駐のレジスタウィンドウ全スピルルーチン（call0規約・自身は
 * ウィンドウ例外を起こさない）。コンテキストスイッチ時に手放すタスクの
 * 全ライブウィンドウを自スタックへ退避するために使う（core_support.S）。
 * QEMUのROM ELFを
 * nmで実測したアドレス。
 */
xthal_window_spill_nw             = 0x40053d60;

SECTIONS
{
    /*
     * 自前ベクタテーブル（core_vectors.S）。VECBASEは1KB境界必須のため
     * 1024整列でIRAM先頭付近に配置する。start.SでVECBASEを_kernel_vectorsへ
     * 設定する（実機のROMウィンドウハンドラ非決定性への対処。Phase2）。
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

        /* --- C++ グローバル/静的コンストラクタ表（.init_array + .ctors）---
         * XIP版(esp32s3_xip.ld)の .flash_rodata と同じ役割。こちらは
         * QEMU 機能テスト・C++ ランタイム smoke テスト（SRAM plain ELF）で
         * コンストラクタ表を配置・走査可能にするため。★本ツールチェーンは
         * コンストラクタを .ctors に出力する（esp32s3_xip.ld のコメント参照）。
         * C のみの既存テスト51件は入力が無く空（start==end）＝非回帰。
         * 走査は m5/compat/cxx_runtime.cpp の esp_run_init_array()
         * （.init_array=前方／.ctors=後方）。 */
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        . = ALIGN(4);
        __ctors_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        __ctors_end = .;

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
