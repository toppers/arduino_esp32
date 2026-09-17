/*
 *  ESP32-P4 / FMP3 seam（実 ESP-IDF 2nd-stage bootloader 起動）用リンカスクリプト
 *
 *  段4（2026-08-14）。正本の計画 .steering/20260814-p4-integration-plan/PLAN.md §3 段4、
 *  判定基準 .steering/20260814-p4-stage4/AC.md、実施記録 .steering/20260814-p4-stage4/README.md。
 *
 *  本ファイルは esp32p4_fmp.ld（全 text を L2MEM へ置く方式(a)/単独リンク検証用）の
 *  **複製ベースの別ファイル**である。esp32p4_fmp.ld は 1 バイトも変更していない
 *  （方式(a)＝段2/段3 の経路はこのファイルを一切読まない）。
 *
 *  ============================================================================
 *  なぜこの形でなければならないか（bootloader 側の要件・すべて一次資料）
 *  ============================================================================
 *  (1) **flash マップされるセグメントがちょうど 2 本**でなければならない。
 *      esp-idf v5.5.4 の bootloader_utility.c:805-853（`#if SOC_MMU_DI_VADDR_SHARED`
 *      の枝。esp32p4 だけが soc_caps.h:420 でこれを定義する）が
 *        assert(rom_index < 2);   （:836）
 *        assert(rom_index == 2);  （:843）
 *      を課す。rom_index は「load_addr が [SOC_DROM_LOW, SOC_DROM_HIGH)
 *      ＝[0x4000_0000, 0x4400_0000) か PSRAM 窓 [0x4800_0000, 0x4c00_0000) に
 *      入るイメージセグメントの本数」である（同 :820-838）。
 *      P4 は D/I の vaddr 窓が同一（soc.h:152-155）なので、**同じ 1 本の窓の中に
 *      rodata と text を別ページで置く**ことで 2 本にする。
 *      実測: 実 ESP-IDF アプリ（tools/fmp_loader）も DROM 1 本＋IROM 1 本の
 *      ちょうど 2 本である（段4 README §1）。
 *
 *  (2) **セグメント #0 の先頭に有効な esp_app_desc が要る**。
 *      esp_image_format.c:767-777 が segment==0 の先頭を esp_app_desc_t として読み、
 *      min/max_efuse_blk_rev_full で efuse ブロックリビジョンを検証する
 *      （`#if !CONFIG_IDF_TARGET_ESP32`＝P4 も対象）。
 *      esptool は flash セグメントを load_addr の昇順に並べたうえで、
 *      **`.flash.appdesc` という名前の ELF セクションを先頭へ移す**
 *      （esptool 4.12 bin_image.py:741-759。この特別扱いは C6/P4 のような
 *        D/I 共有 vaddr のチップのために入っている）。
 *      本スクリプトは IDF と同じくその名前を使う——app_desc を独立した出力セクション
 *      `.flash.appdesc` に置き、直後に `.flash_rodata` を隙間なく続ける
 *      （隣接しているので esptool が 1 セグメントへ併合し、名前は先頭側＝
 *        `.flash.appdesc` が残る）。
 *      併合や移動が崩れたら **cmake/a1_p4_seam_image.sh の C-1/C-2 がビルドを止める**
 *      （セグメント本数と segment #0 先頭の magic を直接見ているので、
 *        esptool 側の挙動が変わっても黙って通ることはない）。
 *
 *  (3) **マップされるセグメントは (file_offs % 64KB) == (vaddr % 64KB) を満たす**
 *      必要がある（esp_image_format.c:873-880）。イメージ先頭ヘッダ 0x18 ＋
 *      セグメントヘッダ 0x08 ＝ 0x20 なので、vaddr を「64KB 境界＋0x20」に置くと
 *      先頭セグメントが 1 ページ分の padding 無しに収まる（IDF の memory.ld.in が
 *      irom_seg/drom_seg を 0x4000_0020 にしているのと同じ理由・同ファイル :57-65）。
 *      2 本目以降は esptool が padding を入れて整合させる。
 *
 *  (4) **RAM セグメントの置き場所**。bootloader 自身が動いている領域を上書きしては
 *      いけない。IDF の memory.ld.in（CONFIG_ESP32P4_SELECTS_REV_LESS_V3 の枝・
 *      本 seam の sdkconfig もこれ）は アプリの sram_low を
 *        0x4FF0_0000 〜 0x4FF2_CBD0（= 2nd-stage bootloader の iram_loader_seg 先頭）
 *      と定めている。0x4FF2_CBD0 は bootloader.ld が
 *        ASSERT((bootloader_iram_loader_seg_start == 0x4ff2cbd0), ...)
 *      で固定している定数である（実測: 本 repo で建てた bootloader.map:3175）。
 *      ⇒ RAM 領域は [0x4FF0_0000, 0x4FF2_CBD0) の 0x2CBD0 バイトとする。
 *      （0x4FF4_0000 以降の sram_high も使えるが、現状の FMP3 は 50KB 程度しか
 *        使わないので分割しない。ROM 予約域 0x4FF2_CBD0〜0x4FF4_0000 には
 *        触れない＝LX6 で踏んだ「ROM 予約と .bss の重なり」の同型事故を構造的に避ける。）
 *
 *  (5) **P4 では FMP3 側の flash MMU 設定コード（S3 の flash_cache_init.c 相当）が
 *      要らない**。bootloader の set_cache_and_start_app（bootloader_utility.c:1150-）が
 *      cache を disable → mmu_hal_unmap_all → 2 本を mmu_hal_map_region →
 *      cache_ll_l1_enable_bus（core0/1 両方）→ cache_hal_enable してから
 *      entry へ jump する。**制御を受け取った時点で cache は有効・マッピング済み**。
 *      S3 seam が flash_xip_map() を自前で持つのは Direct Boot 由来の経緯である。
 *
 *  (6) **text を低い番地・rodata を高い番地に置く**（IDF と同じ並び）。
 *      これは趣味ではなく実測による——**逆にすると起動しない**。
 *      当初 DROM を 0x4000_0020、IROM を 0x4010_0020 に置いた版は、
 *      bootloader の `Loaded app from partition` の後、FMP3 が 1 文字も出さずに黙った。
 *      JTAG（openocd-esp32・USB-JTAG、**シリアル番号でこの板に固定**）で単一命令実行
 *      した結果:
 *        start.S:78 の `my_istkpt sp` ＝ `lw sp, 0(_kernel_istkpt_table)`
 *        （vaddr 0x40000e28 の**データ読み**）が 0x4ff005e0（イメージ内の正しい値）
 *        ではなく **0x0fe03717** を返した。この 32bit は `.flash_text` の
 *        0x40100e28 にある命令語 `auipc a4,0xfe03` そのもの
 *        ＝ **DROM 窓の読みが IROM 側の物理ページの中身を返していた**。
 *        その結果 sp が壊れ、次の `sw ra,12(sp)` が Store access fault(mcause=7) を出し、
 *        まだ FMP3 の mtvec が入っていない（chip_initialize 前）ため ROM の
 *        トラップハンドラへ落ちて無限ループしていた（PC=0x4FC00C50 で停止を実測）。
 *      **なぜそうなるかの機序は完全には特定していない（未確認）**。有力な読みは
 *        「bootloader 自身が flash を読むときの mmap 窓が `MMU_BLOCK0_VADDR =
 *         SOC_DROM_LOW = 0x4000_0000`（bootloader_flash.c:175,185）＝**まさにこの番地**で、
 *         そこに残ったデータ側のキャッシュ行を、MMU 再設定後の読みが拾っている」
 *        というもの。ESP-IDF アプリはこの番地に .text（命令側）を置くので当たらない。
 *      ⇒ **実 IDF アプリと同じ並びに合わせる**（この並びは同じ石・同じ bootloader で
 *        動くことが tools/fmp_loader で実証済み）。
 *
 *  ============================================================================
 *  IRAM に置くもの（RAM 常駐コード）——**現状は「無い」**
 *  ============================================================================
 *  当初は起動・トラップ入口・文脈切替（start.S / core_support.S / chip_support.S）を
 *  RAM 常駐にする設計にした（IDF が vectors_clic.S を .iram0.text へ置くのと同じ考え）。
 *  **これは成立しない**——実測で判明した理由:
 *
 *      start.S:150 の `jal _kernel_target_mprc_initialize` は**素の jal**（±1MB）で、
 *      RISC-V の call 疑似命令（auipc+jalr, ±2GB）ではない。start.S を RAM
 *      (0x4FF0_0000) に、カーネル本体を flash IROM (0x4010_0000) に置くと距離が
 *      約 254MB になり
 *        relocation truncated to fit: R_RISCV_JAL against `_kernel_target_mprc_initialize'
 *      でリンクが落ちる（cfg1_out・fmp とも）。fmp3_core は submodule（無改変の約束）
 *      なので start.S 側を call へ書き換える選択肢は採らない。
 *
 *  ⇒ **text は全部 flash IROM に置く**（＝ 完全 XIP）。bootloader は cache を
 *     有効化してから entry へ jump するので、エントリもトラップ入口も flash で問題ない。
 *     .iram_text は将来 RAM 常駐が要るコード（flash 書込み中に走る等）のための
 *     受け口として**空のまま残す**。使うときは、その関数群を RAM 側へまとめて
 *     置き、飛び先の距離（jal かどうか）を確かめること。
 */

OUTPUT_ARCH("riscv")
ENTRY(seam_p4_entry)

MEMORY
{
    /*
     *  flash XIP（bootloader が MMU でマップする。D/I 窓は同一）。
     *
     *  **並び（text が先・rodata が後）は ESP-IDF アプリと同じにしてある。
     *    逆にすると実機で起動しない**（下の (6) に実測の経緯）。
     */
    IROM (rx)  : ORIGIN = 0x40000020, LENGTH = 1M - 0x20
    DROM (r)   : ORIGIN = 0x40100020, LENGTH = 3M - 0x20
    /*  内蔵 SRAM（bootloader がイメージから memcpy でロードする）  */
    RAM  (rwx) : ORIGIN = 0x4FF00000, LENGTH = 0x2CBD0
    /*
     *  ---- 追加のメモリ領域（構成ごとに変わる）----
     *  seam-eth 構成だけが sram_high（0x4FF4_0000〜）を要る。**ここで直接
     *  宣言しないのは、空の領域や空の出力セクションでも ELF のセクション表と
     *  シンボル表が変わり、esptool が `app_desc.app_elf_sha256` へ書く値が
     *  動いて `seam-p4-smp` の golden が変わってしまうからである**
     *  （実測: 空の `.bss_high` を足しただけで `88b4d8fc…` → `9734e348…`。
     *   段E §6-1 で踏んだのと同じ経路）。
     *
     *  ⇒ **中身を差し替えられる INCLUDE にする**。既定は
     *    `fmp3/target/m5stamp_esp32p4_gcc/p4_mem_high.ld`（**コメントだけ**）で、
     *    seam-eth 構成のときだけ `esp/p4shim/p4_mem_high.ld`（実体）が
     *    `-L` の探索順で先に見つかる。既定側は 1 バイトも変わらない。
     *    ld は INCLUDE を「カレントディレクトリ → -L の順」で探す。
     */
    INCLUDE p4_mem_high.ld
}

SECTIONS
{
    /*
     *  ---- IRAM（RAM 常駐コード）----
     *  **.flash_text より前に宣言すること**。入力セクションは先に宣言された
     *  出力セクションに取られるので、宣言順が selection の優先順になる。
     *  （中身は現状 seam のエントリスタブだけ。理由は下の「IRAM に置くもの」）
     */
    .iram_text : ALIGN(64)
    {
        _iram_text_start = ABSOLUTE(.);
        KEEP(*(.iram_text .iram_text.*))
        /*
         *  構成ごとに「IRAM 常駐が要るオブジェクト」を足す口（段v-2・2026-08-16）。
         *  既定は同ディレクトリの p4_iram_extra.ld（**コメントだけ**）なので、
         *  既存構成の生成物は不変。PSRAM 構成のときだけ
         *  esp/p4shim/p4_iram_extra.ld（実体）が読まれる。
         *  どちらを読むかは CMake 変数 A1_P4_LDFRAG_DIR 1 個で決まる。
         */
        INCLUDE p4_iram_extra.ld
        . = ALIGN(4);
        _iram_text_end = ABSOLUTE(.);
    } > RAM

    /*
     *  ---- IROM: 本体コード（低い番地・IDF と同じ並び。上記 (6)）----
     */
    .flash_text : ALIGN(4)
    {
        _flash_text_start = ABSOLUTE(.);
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
        _flash_text_end = ABSOLUTE(.);
    } > IROM
    _flash_text_size = _flash_text_end - _flash_text_start;

    /*
     *  ---- DROM: app_desc ＋ rodata ----
     *  `.flash.appdesc` という**名前**が効く（上記 (2)）。直後に .flash_rodata を
     *  隙間なく続けて 1 セグメントに併合させる。
     */
    .flash.appdesc : ALIGN(0x10)
    {
        KEEP(*(.appdesc))
    } > DROM

    .flash_rodata : ALIGN(0x10)
    {
        _flash_rodata_start = ABSOLUTE(.);
        /*
         *  ---- cfg pass3(check) が引くシンボルを --gc-sections から守る ----
         *  段5（2026-08-15）に足した。実測した壊れ方:
         *      test_notify1 を seam で建てると
         *        error: E_SYS: symbol '_kernel_almhdr_ALM3_p_evar' not found
         *      で pass3 が落ちる。obj には在る（`R _kernel_almhdr_ALM3_p_evar`）が、
         *      リンクで消えている。
         *  機序: seam は `-ffunction-sections -fdata-sections` ＋ `--gc-sections`
         *      （chip.cmake:57-58・target.cmake:140）。`intptr_t *const p_evar` は
         *      値が定数なので、参照側（同一 TU の almhdr）が直接アドレスへ畳まれ、
         *      **セクションが誰からも参照されなくなって gc で落ちる**。
         *      方式(a) は `-fno-function-sections`（＝-fdata-sections も付かない）で
         *      kernel_cfg.c の rodata が 1 本にまとまるため、たまたま生き残っていた。
         *  なぜ KEEP か: target.cmake は同じ問題に `-Wl,--undefined=<表名>` で
         *      対処しているが（段1）、それは**名前を先に知っている表**にしか使えない。
         *      `_kernel_almhdr_ALM3_p_evar` のような **アプリ依存の名前**は列挙できない。
         *      cfg が生成する `_kernel_*` の読み取り専用データを丸ごと守るのが正しい。
         *  代償: gc が効かなくなる分だけ .rodata が増える（実測 +数百バイト）。
         */
        KEEP(*(.rodata._kernel_* .srodata._kernel_*))
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        *(.srodata .srodata.*)
        *(.sdata2 .sdata2.*)
        *(.gcc_except_table .gcc_except_table.*)

        /*
         *  C++ static constructor / destructor tables (arduino_esp32 port,
         *  StampP4 plan stage A1; the same addition as target/m5stampc5_gcc/
         *  esp32c5_xip.ld and target/m5nanoc6_gcc/esp32c6_xip.ld). Not in
         *  the dev linker script: the dev P4 images are C only. The Arduino
         *  bridge (src/bridge/ArduinoSketchBridge.cpp) calls
         *  esp_run_init_array() (runtime/seam/init_array.cpp), which walks
         *  __init_array_start..__init_array_end and __ctors_start..__ctors_end,
         *  so a sketch's global C++ objects need these symbols. The tables
         *  are read-only function pointers and go into DROM with the rodata.
         *
         *  Kept INSIDE .flash_rodata rather than as output sections of their
         *  own: the bootloader accepts exactly two flash-mapped segments
         *  (requirement (1) at the top of this file; check C-1 in
         *  scripts/fmp3_link.py). KEEP is required: nothing references these
         *  entries, so --gc-sections would drop them. riscv32-esp-elf gcc
         *  emits .init_array; .ctors is accepted as well. The unwind tables
         *  (.eh_frame) the M5Stack core's -fexceptions sketches carry are
         *  given a home here too rather than left as orphans.
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
        . = ALIGN(4);
        *(.eh_frame .eh_frame.*)
        *(.eh_frame_hdr)
        . = ALIGN(4);
        _flash_rodata_end = ABSOLUTE(.);
    } > DROM
    _flash_rodata_size = _flash_rodata_end - _flash_rodata_start;

    /*
     *  ---- RAM: 初期値つきデータ ----
     *
     *  LMA==VMA（bootloader がイメージから直接 RAM へ置く）。start.S の
     *  __idata→__data コピーは自己コピーになるだけで無害（esp32p4_fmp.ld と同じ）。
     *
     *  **.kernel_data_CLS_* / .stack_CLS_* を明示的にここへ集める**。
     *  これらは cfg が生成する kernel_cfg.c が
     *    __attribute__((section(".kernel_data_CLS_PRC1"),nocommon))
     *  で作る PROGBITS セクションで、esp32p4_fmp.ld では orphan として
     *  リンカ任せに配置されていた（RAM しか無かったので当たっていた）。
     *  本スクリプトは領域が 3 つあるので、明示しないと RW の orphan が
     *  **flash 領域に置かれかねない**（＝書けないメモリ上のカーネル制御ブロック）。
     *  上流 svn の方式(a) も build_fmp3_lib.sh が同じ 2 群を .data.* へ改名して
     *  IDF の DRAM へ入れており、扱いは同じである。
     */
    .data : ALIGN(8)
    {
        __data_start = .;
        /*
         *  構成ごとに「RAM 常駐が要るデータ」を足す口（段v-2・2026-08-16）。
         *  既定は同ディレクトリの p4_dram_extra.ld（**コメントだけ**）。
         *  IDF の DRAM_ATTR は .dram1.N という孤児セクションを作るので、
         *  PSRAM 構成ではここで拾う（flash へ流すとキャッシュ停止中に読めない）。
         *
         *  **必ず `__data_start` の「後」に置くこと**（実機で踏んだ）。
         *  前に置くと `__idata_start`(=LOADADDR(.data)) と `__data_start` が
         *  この断片の長さぶんズレ、start.S の
         *      __idata_start..__idata_end → __data_start へコピー
         *  が**ズレたまま長さそのまま**走る。結果、コピー先が .data の末尾を
         *  越えて `.sbss` の先頭を上書きし、**「.bss が消えていない」ように見える**
         *  （実際には消した後に上書きされている）。実測では .sbss 先頭に
         *  `"%s: tuning fail, best phase id is ..."` という .dram1 の文字列が
         *  現れ、カウンタが全部ゴミになった（run4-run6）。
         *  ——「起動はするが、静かに壊れている」型の欠陥である。
         */
        INCLUDE p4_dram_extra.ld
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        *(.sdata .sdata.*)
        *(.gnu.linkonce.s.*)
        *(.kernel_data_CLS_*)
        *(.stack_CLS_*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM AT> RAM
    __idata_start = LOADADDR(.data);
    __idata_end   = __idata_start + SIZEOF(.data);

    /*
     *  グローバルポインタ（gp 相対アクセスの基準）。
     *  **必ず RAM の .data 先頭基準で置く**——esp32p4_fmp.ld のように
     *  「素の `.` ＋0x800」で書くと、直前の出力セクションが flash 側だった場合に
     *  gp が flash 番地になる（本スクリプトでは .flash_text の直後になってしまう）。
     */
    PROVIDE(__global_pointer$ = ADDR(.data) + 0x800);

    /*
     *  TLS テンプレート境界（chip_asm.inc の start_r フックが参照）。
     */
    .tdata : ALIGN(8)
    {
        _thread_local_data_start = .;
        *(.tdata .tdata.*)
        _thread_local_data_end = .;
    } > RAM

    .tbss (NOLOAD) : ALIGN(8)
    {
        _thread_local_bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        _thread_local_bss_end = .;
    } > RAM

    /*
     *  ---- 追加の出力セクション（構成ごとに変わる）----
     *  **`.sbss`/`.bss` より前**に置くこと。入力セクションは先に宣言された
     *  出力セクションに取られるので、後ろに置くと `*(.bss .bss.*)` に全部
     *  奪われる（このファイルの (2) と同じ規則）。
     *
     *  既定は `fmp3/target/m5stamp_esp32p4_gcc/p4_bss_high.ld`（**コメントだけ**）。
     *  seam-eth 構成のときだけ `esp/p4shim/p4_bss_high.ld` が使われ、
     *  `liblwip.a` / `libeth.a` の .bss（実測 97,664 B）を sram_high へ逃がす。
     *  なぜ「空のセクションを常に置く」ではいけないのかは MEMORY 側の
     *  コメント（golden が動く）を参照。
     */
    INCLUDE p4_bss_high.ld

    .sbss (NOLOAD) : ALIGN(8)
    {
        __sbss_start = .;
        *(.sbss .sbss.*)
        *(.scommon)
        __sbss_end = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(8)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    . = ALIGN(16);
    PROVIDE(__heap_start = .);
    PROVIDE(_end = .);
    PROVIDE(end = .);

    /DISCARD/ : { *(.eh_frame) *(.eh_frame.*) *(.comment) *(.riscv.attributes) *(.note.*) }
}
