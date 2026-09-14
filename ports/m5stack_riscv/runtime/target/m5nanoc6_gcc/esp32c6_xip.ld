/*
 *  ESP32-C6用リンカスクリプト（M5NanoC6/FMP3/seam=段2 用）
 *
 *  出典/設計根拠（3つの合成）:
 *    (1) 段1 の esp32c6.ld（Direct Boot/RAM側の .data/.bss 入力規則、
 *        クラス別セクション `.kernel_data_CLS_*` と `.stack_CLS_*` を含む）。
 *    (2) esp-idf v5.5.4 components/esp_system/ld/esp32c6/sections.ld.in の
 *        .flash_rodata_dummy 方式（317-323行） -- C6 は SOC_DROM_LOW==SOC_IROM_LOW
 *        （同一アドレス空間）なので、text の直後を MMU ページ境界+0x20 へ進めて
 *        rodata を別ページに置く（text と rodata が同じページに同居すると
 *        MMU の書換え粒度がページ単位のため区別できなくなる）。
 *    (3) fmp3/target/m5stamp_esp32p4_gcc/esp32p4_xip.ld の教訓（冒頭コメント
 *        (2)(6)） -- (2) esptool は `.flash.appdesc` という**名前**のセクションを
 *        segment #0 の先頭へ特別扱いする（bin_image.py 741-759 相当）ので、
 *        DROM 側の先頭にこの名前で app_desc を置く。(6) text を低位/rodata を
 *        高位に置く「IDF と同じ並び」を踏襲する（P4 では逆にして実機で起動しな
 *        かった実績があるため、そもそも逆にしない）。
 *
 *  ---- .data の LMA/VMA について（段1 から意図的に変えた点）----
 *  段1（Direct Boot XIP）は「.data の初期値を FLASH 上の LMA（.text 直後）に
 *  焼き、start.S 自身が __idata_start..__idata_end を __data_start へ手動で
 *  memcpy する」設計だった -- Direct Boot には ELF/esp_image のような「ロー
 *  ダーが RAM セグメントを解釈して配置する」機構が無く、フラットな1枚の
 *  flash 生バイナリを objcopy が LMA 基準で敷き詰めるだけだからである。
 *
 *  seam（本ファイル）は esp_image 形式（esptool elf2image）を経由する。
 *  esptool の既定（--use_segments 無指定）は **ELF セクションを VMA
 *  （section header の sh_addr。esptool 内部ではこれを紛らわしく "lma" と
 *  呼ぶ変数名にしているが実体は sh_addr＝VMA）で拾う**
 *  （esptool/bin_image.py:1185 `image.segments = e.segments if args.use_segments
 *  else e.sections`、既定 False＝e.sections 側。e.sections は _read_sections()
 *  が section header の第4フィールド＝sh_addr をそのまま使う。実機ソース
 *  確認済み: python_env/idf5.5_py3.12_env の esptool 本体）。
 *  つまり **.data に AT() で FLASH 側の LMA を明示しても、esptool はそれを
 *  一切参照しない** -- .data の VMA（RAM）だけを見て「RAM セグメント」として
 *  扱い、実際の初期値バイト列（ELF ファイル中の本物の内容）をそのセグメント
 *  に詰めて、bootloader がそれを直接 RAM へ memcpy する。段1 のような
 *  「FLASH 上の LMA に本当にバイト列が置かれ、後で自前で読み出す」という
 *  前提は seam では成立しない（LMA に AT() で FLASH 上の番地を指定しても、
 *  その番地には対応するセクション実体が無く、esp_image にも焼かれない --
 *  読めば mapped DROM セグメントの終端付近の無関係な flash 生データを
 *  拾うだけになる。P4 xip ld が「LMA==VMA なら自己コピーで無害」と書いて
 *  いる（esp32p4_xip.ld:226-230）のと表裏の関係で、**LMA!=VMA だと実害が
 *  出る**）。
 *  ⇒ 本ファイルは .data に AT() を付けない（LMA==VMA＝RAM）。P4 xip ld と
 *  同じく、fmp3_core 共通 chip_start.S の __idata_start..__idata_end
 *  copy loop は「自己コピー」になり無害（__idata_start が __data_start と
 *  同じ番地になるため）。RAM 側の入力規則（*(.data*)/*(.kernel_data_CLS_*)
 *  等）自体は段1 の esp32c6.ld から変更していない。
 *
 *  MMU ページサイズ 0x10000 は Task 2 で読んだ bootloader の
 *  CONFIG_MMU_PAGE_SIZE の実測値（esp-idf sdkconfig）。FLASH 領域の長さは
 *  IDF の IDRAM0_2_SEG_SIZE = (CONFIG_MMU_PAGE_SIZE << 8) と同じ計算
 *  （256ページ分=16MB。M5NanoC6 の実 flash は 4MB だが、MMU 窓の宣言は
 *  IDF と同じ仮想空間サイズを踏襲する -- 実 flash サイズの制約は
 *  esptool の --flash_size で別途課される）。
 */

OUTPUT_ARCH(riscv)

/* esp-halのペリフェラル構造体インスタンス（SYSTIMER等．-Lで検索．段1と同じ） */
INCLUDE esp32c6.peripherals.ld

MEMORY
{
    /* C6: IROM/DROM は同一空間 0x42000000。IDF と同じく text を +0x20 から。
       LENGTH は MMU 256 ページ分（CONFIG_MMU_PAGE_SIZE << 8）から 0x20 を引く。 */
    FLASH (rx) : ORIGIN = 0x42000020, LENGTH = 0x1000000 - 0x20
    /* RAM: IDF memory.ld.in の SRAM_SEG_START..SRAM_SEG_END（bootloader の
       iram_loader_seg 開始）。LENGTH をここで切るので、超えれば ld が落ちる
       （bootloader を上書きしない）。 */
    RAM (rwx)  : ORIGIN = 0x40800000, LENGTH = 0x4086E610 - 0x40800000
}

MMU_PAGE = 0x10000;

ENTRY(seam_c6_entry)

SECTIONS
{
    /* ---- IROM: text（IDF と同じ並び。text を低位に置く）---- */
    .text : {
        KEEP(*(.iram_text))            /* seam_c6_entry（先頭。entry が IROM 先頭付近） */
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
        KEEP(*(.appdesc))              /* seam_c6_appdesc.c: magic 0xABCD5432 が segment #0 の先頭 */
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
         *  S1-5). Not in the dev linker script: the dev C6 images are C only.
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
     *  置く。上記コメント参照）。入力規則は段1 の esp32c6.ld と同一。
     */
    .data : ALIGN(8) {
        __data_start = .;
        LONG(0)     /* 番兵（.dataが空だとldがLMA計算を崩すのを防ぐ。段1と同じ） */
        *(.data*)
        /*
         *  FMP3 のクラス別セクション。入力規則が無いと orphan になり，
         *  .data のコピー範囲（__idata_*）にも .bss のゼロ初期化にも入らない
         *  （P4 の esp32p4_xip.ld:232-268/段1 esp32c6.ld が実機で踏んだ型）。
         */
        *(.kernel_data_CLS_*)
        *(.dram1 .dram1.*)
        /*
         *  段4 Task 3（2026-09-14・計画 F4）: Wi-Fi/coex blob と esp/shim の
         *  IRAM 群（IRAM_ATTR = .iram1、blob の .wifi*iram / .coexiram）を
         *  RAM 常駐にする。S3 と同型（cache 無効中や高頻度 ISR での実行に備える。
         *  asp3 C6 は FLASH(XIP) のまま動かしているので、RAM が足りなければ
         *  asp3 の配置が実証済みの退避路）。
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
