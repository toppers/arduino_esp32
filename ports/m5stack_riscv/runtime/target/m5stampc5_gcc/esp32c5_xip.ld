/*
 *  ESP32-C5用リンカスクリプト（M5Stamp-C5/FMP3/seam 用）
 *
 *  出典: fmp3/target/m5nanoc6_gcc/esp32c6_xip.ld（C6 段2-4）の複製。設計根拠
 *  （.flash_rodata_dummy 方式、.flash.appdesc、LMA==VMA の .data、クラス別
 *  セクション、IRAM 群の RAM 常駐）はそのファイルの冒頭コメントを参照。
 *  C5 で変えた点:
 *   (1) RAM LENGTH = 0x4084E5A0 - 0x40800000 = 320,928 B。C5 の HP SRAM は
 *       384 KiB（0x40800000..0x40860000、soc.h:158-159）で、seam では bootloader の
 *       iram_loader_seg 開始 0x4084E5A0（bootloader/subproject/main/ld/esp32c5/
 *       bootloader.ld の ASSERT、esp_system/ld/esp32c5/memory.ld.in の SRAM_SEG_END）
 *       までしか静的に使えない。LENGTH をここで切るので超えれば ld が落ちる。
 *   (2) FLASH 窓は C5 の SOC_IROM_LOW..SOC_IROM_HIGH = 0x42000000..0x44000000
 *       （32 MiB 固定。soc.h:148-149。C6 は MMU 256 ページ = 16 MiB）。text は
 *       IDF と同じく +0x20 から。DROM==IROM（soc.h:152）なので単一 FLASH 領域。
 *   (3) MMU ページは 64 KiB（soc/esp32c5/include/soc/ext_mem_defs.h:19、M5Stack
 *       core の sdkconfig CONFIG_MMU_PAGE_SIZE_64KB=y）。C6 と同じ 0x10000。
 *   (4) ENTRY は toppers_start のまま（段1 と同じ）。seam（段2、-DA1_C5_SEAM=ON）は
 *       cmake/a1_c5_stage1.cmake が **-Wl,-e,seam_c5_entry** を渡して上書きする
 *       （GNU ld はコマンドラインの -e を ENTRY() より優先する）。ld を seam 用に
 *       分けなかった理由: 非 seam（段1、test_int1 等）の ELF を 1 バイトも変えない
 *       ため（ENTRY を seam_c5_entry にすると非 seam では未定義シンボルの警告 +
 *       e_entry が変わる。PROVIDE で別名を作ってもシンボル表が変わる）。
 *       .iram_text（seam エントリ）の KEEP は入力が無ければ空なので残す。
 *   (5) INCLUDE は esp32c5.peripherals.ld（-L components/soc/esp32c5/ld）。
 *  .flash.appdesc の KEEP(*(.appdesc)) は非 seam では入力が無く空（seam では
 *  esp/boot/seam_c5_appdesc.c が供給し、esptool がこの名前のセクションを
 *  segment #0 に置く）。
 */

OUTPUT_ARCH(riscv)

/* esp-halのペリフェラル構造体インスタンス（SYSTIMER等．-Lで検索．段1と同じ） */
INCLUDE esp32c5.peripherals.ld

MEMORY
{
    /* C5: IROM/DROM は同一空間 0x42000000..0x44000000（32 MiB 固定窓）。
       IDF と同じく text を +0x20 から。 */
    FLASH (rx) : ORIGIN = 0x42000020, LENGTH = 0x2000000 - 0x20
    /* RAM: IDF memory.ld.in の SRAM_SEG_START..SRAM_SEG_END（bootloader の
       iram_loader_seg 開始 0x4084E5A0 = 320,928 B）。LENGTH をここで切るので、
       超えれば ld が落ちる（bootloader を上書きしない）。 */
    RAM (rwx)  : ORIGIN = 0x40800000, LENGTH = 0x4084E5A0 - 0x40800000
}

MMU_PAGE = 0x10000;

ENTRY(toppers_start)   /* seam は -Wl,-e,seam_c5_entry で上書き（冒頭 (4)） */

SECTIONS
{
    /* ---- IROM: text（IDF と同じ並び。text を低位に置く）---- */
    .text : {
        KEEP(*(.iram_text))            /* seam_c5_entry（esp/boot/seam_c5_entry.S。先頭。entry が IROM 先頭付近） */
        . = ALIGN(256);                /* trap_vector_tableのアライン（段1 と同じ） */
        *(.text .text.*)
        /*
         *  段4 Task 3（2026-09-14）: ここに在った `*(.iram1 .iram1.*)` は
         *  下の .data（RAM）へ移した（計画 F4。IRAM 群を RAM 常駐にする）。
         */
        . = ALIGN(16);
    } > FLASH

    /*
     *  ---- DROM: MMU ページ境界 + 0x20 へ進め、app_desc を先頭に置いて
     *  rodata を続ける（esp-idf sections.ld.in:317-323 の .flash_rodata_dummy
     *  と同じ役割。単一の FLASH 領域なので「.」がそのまま .text の直後から
     *  続く -- IDF のような irom_seg/drom_seg 別名トリックは不要）----
     */
    .flash_rodata_dummy (NOLOAD) : {
        . = ALIGN(MMU_PAGE) + 0x20;
    } > FLASH

    .flash.appdesc : ALIGN(0x10) {
        KEEP(*(.appdesc))              /* esp/boot/seam_c5_appdesc.c: magic 0xABCD5432 が segment #0 の先頭 */
    } > FLASH

    .flash.rodata : ALIGN(0x10) {
        /*
         *  cfg が生成する `_kernel_*` の読み取り専用データ（アラーム/周期
         *  ハンドラの評価順テーブル等）を丸ごと守る。P4 の esp32p4_xip.ld と
         *  同じ理由: `-ffunction-sections -fdata-sections` かつ
         *  `--gc-sections`（target.cmake）の下では、値が定数な
         *  `intptr_t *const p_evar` は参照側で直接アドレスに畳まれ、
         *  参照する者が誰もいなくなったセクションが gc で落ちる
         *  （test_notify1 の `_kernel_almhdr_ALM3_p_evar` で実測）。
         *  `--undefined=_kernel_*_table` は名前を先に知っている表にしか
         *  効かず、アプリ依存の名前（ALM3 等）は列挙できないので KEEP で防ぐ。
         */
        KEEP(*(.rodata._kernel_* .srodata._kernel_*))
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.rodata_wlog_*)              /* ログ文字列圧縮用セクション（段1 と同じ） */

        /*
         *  C++ static constructor / destructor tables (arduino_esp32 port,
         *  C6 S1-5, same addition as target/m5nanoc6_gcc/esp32c6_xip.ld).
         *  Not in the dev linker script: the dev C5 images are C only.
         *  The Arduino bridge (src/bridge/ArduinoSketchBridge.cpp) calls
         *  esp_run_init_array() (runtime/seam/init_array.cpp), which walks
         *  __init_array_start..__init_array_end and __ctors_start..__ctors_end,
         *  so a sketch's global C++ objects need these symbols. The tables are
         *  read-only function pointers and go into DROM with the rodata.
         *
         *  Kept INSIDE .flash.rodata rather than as output sections of their
         *  own: the bootloader accepts exactly two flash-mapped segments
         *  (.text and this one; check C-1 in scripts/fmp3_link.py), and the
         *  size regex install_platform.py writes for this board only knows
         *  .text / .flash.appdesc / .flash.rodata. KEEP is required: nothing
         *  references these entries, so --gc-sections would drop them.
         *  riscv32-esp-elf gcc emits .init_array; .ctors is accepted as well,
         *  the same way the Xtensa esp32s3_xip_m5.ld does.
         */
        . = ALIGN(4);
        __init_array_start = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = ABSOLUTE(.);
        . = ALIGN(4);
        __fini_array_start = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = ABSOLUTE(.);
        . = ALIGN(4);
        __ctors_start = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        __ctors_end = ABSOLUTE(.);
        . = ALIGN(4);
        __dtors_start = ABSOLUTE(.);
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
        __dtors_end = ABSOLUTE(.);
        /*
         *  Unwind tables (arduino_esp32 port, same reason as the tables
         *  above). The sketch objects are C++ built with -fexceptions (the
         *  M5Stack core's cpp_flags), so they carry .eh_frame and
         *  .gcc_except_table. Without an input rule the linker places them
         *  as orphan output sections after this one; giving them a home here
         *  keeps the flash layout explicit. The Xtensa esp32s3_xip_m5.ld
         *  collects .eh_frame into its rodata the same way.
         */
        . = ALIGN(4);
        *(.eh_frame .eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table .gcc_except_table.*)
        . = ALIGN(16);
    } > FLASH

    /*
     *  ---- RAM: 初期値つきデータ ----
     *  LMA==VMA（AT() を付けない＝bootloader がイメージから直接 RAM へ
     *  置く。上記コメント参照）。入力規則は C6 の esp32c6_xip.ld（元は C6 段1 の
     *  esp32c6.ld）と同一。C5 に Direct Boot 用の esp32c5.ld は無い。
     */
    .data : ALIGN(8) {
        __data_start = .;
        LONG(0)     /* 番兵（.dataが空だとldがLMA計算を崩すのを防ぐ。C6 と同じ） */
        *(.data*)
        /*
         *  FMP3 のクラス別セクション。入力規則が無いと orphan になり，
         *  .data のコピー範囲（__idata_*）にも .bss のゼロ初期化にも入らない
         *  （P4 の esp32p4_xip.ld:232-268 / C6 段1 の esp32c6.ld が踏んだ型）。
         */
        *(.kernel_data_CLS_*)
        *(.dram1 .dram1.*)
        /*
         *  段4 Task 3（2026-09-14/計画 F4）: Wi-Fi/coex blob と esp/shim の
         *  IRAM 群（IRAM_ATTR = .iram1、blob の .wifi*iram / .coexiram）を
         *  RAM 常駐にする。S3 と同型（cache 無効中や高頻度 ISR での実行に備える。
         *  C6 では asp3 C6 が FLASH(XIP) のまま動かした実績を退避路にした。
         *  asp3 C5 の IRAM 群の配置は本段では確認していない = 段4 で
         *  esp/c5/esp32c5_seam.ld を読んで決める）。
         *  **.data の中に置く**のは、seam 画像検査 C-6（cmake/a1_c6_seam_image.sh）
         *  が「RAM セグメント全部を ELF の .data と比較する」形だからで、
         *  別出力セクションを作ると C-6 の拡張が要る（AC-3e）。bootloader は
         *  .data を 1 本の RAM セグメントとしてイメージから memcpy する。
         *  RAM は rwx（MEMORY）なので実行可能。入力セクション名の出典は
         *  esp-idf の linker fragment（esp_wifi/linker.lf: .wifi0iram/.wifirxiram/
         *  .wifislpiram/.wifiorslpiram/.wifislprxiram/.wifiextrairam、
         *  esp_coex/linker.lf: .coexiram/.coexsleepiram、esp_phy/linker.lf:
         *  .phyiram）と esp_attr.h の IRAM_ATTR（.iram1）。IDF は
         *  ESP_WIFI_IRAM_OPT 等の Kconfig でこれらを iram0_text へ写す。
         */
        . = ALIGN(4);
        *(.iram1 .iram1.*)
        *(.wifi0iram .wifi0iram.*)
        *(.wifirxiram .wifirxiram.*)
        *(.wifiextrairam .wifiextrairam.*)
        *(.wifiorslpiram .wifiorslpiram.*)
        *(.wifislpiram .wifislpiram.*)
        *(.wifislprxiram .wifislprxiram.*)
        *(.coexiram .coexiram.*)
        *(.coexsleepiram .coexsleepiram.*)
        *(.phyiram .phyiram.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(8);
        _thread_local_data_start = .;
        *(.tdata .tdata.*)
        _thread_local_data_end = .;
        . = ALIGN(4);
    } > RAM
    /*
     *  LMA==VMA なので __idata_start は __data_start と同じ番地になり，
     *  chip_start.S の memcpy ループは自己コピー（無害）になる
     *  （esp32p4_xip.ld と同じ扱い。冒頭コメント参照）。
     */
    __idata_start = LOADADDR(.data);
    __idata_end = __idata_start + SIZEOF(.data);

    /* MAIN BSS SECTION（段1 と同じ。NOLOAD なのでイメージには含まれない） */
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        __sbss_start = .;
        __sbss_end = .;
        *(.sbss .sbss.*)
        *(.bss*)
        *(COMMON)
        /*
         *  FMP3 のクラス別スタック（段1 と同じ理由で .bss へ明示的に集める）。
         */
        *(.stack_CLS_*)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    .tbss (NOLOAD) : ALIGN(8) {
        _thread_local_bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        _thread_local_bss_end = .;
    } > RAM
}
