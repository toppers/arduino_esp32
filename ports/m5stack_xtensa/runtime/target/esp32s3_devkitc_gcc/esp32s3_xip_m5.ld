/*
 *  ESP32-S3 flash-XIP リンカ（★M5 変種：.ctors 集約版）
 *  .text/.rodata はフラッシュ(IROM 0x42000000 / DROM 0x3C000000)へXIP配置、
 *  起動コード(.iram_boot=start.S+flash_cache_init)と.data/.bssはSRAMへ。
 *  ROMはSRAMセグメントのみロード、start.SがROM関数でフラッシュをMMUマップ後、
 *  フラッシュ上の.textへ進む。IROM/DROMは64KB(0x10000)境界整列必須(MMUページ)。
 *
 *  ★★このファイルは esp32s3_xip.ld（golden 5 構成が依存する共有スクリプト）の
 *    複製に、.flash_rodata 内の C++ グローバルコンストラクタ表 .ctors を
 *    KEEP 集約する変更だけを加えたものである（下の __ctors_start/end 参照）。
 *    esp32s3_xip.ld 本体は不変。この変種を使うのは A1_VARIANT=m5 のみで、
 *    golden 5 構成（smp/wifi/ble/lx6）は従来どおり esp32s3_xip.ld を使う。
 *
 *  ★blob 衝突について（1-3 引き継ぎ）：esp32s3_xip.ld で .ctors を集約すると
 *    BT/Wi-Fi blob（bt_rtc_time.o 等）の休眠 .ctors が --gc-sections から復活し
 *    seam-s3-ble/wifi のバイトが変わる（機能不変・バイトのみ変化。復活した
 *    blob ctor の実挙動は未知）。しかし本変種を使う A1_VARIANT=m5 は
 *    **blob を一切リンクしない**（A1_HAS_BLOB は wifi/ble のみ ON）。したがって
 *    ここで .ctors を KEEP しても復活する blob .ctors は存在せず、衝突は起きない。
 *    ★将来 M5＋blob 構成を作る場合は、下の .ctors KEEP に EXCLUDE_FILE を足して
 *      blob（bt_rtc_time.o 等）の休眠 .ctors を除外し byte-identity を保つこと
 *      （＝blob 復活問題は未解決のまま。M5×blob 統合時に方針を別途決める）。
 */
xtos_exc_handler_table = 0x3fcedf68;
xthal_window_spill_nw  = 0x40053d60;

MEMORY
{
  IROM (rx) : ORIGIN = 0x42000000, LENGTH = 4096k   /* flash instr XIP */
  DROM (r)  : ORIGIN = 0x3C100000, LENGTH = 4096k   /* flash data XIP(entry16, IROMに16ページ確保) */
  /* SRAM: IRAM(boot)物理0x3FC88000〜, DRAM物理0x3FC90000〜 で非重複 */
  IRAM (rwx): ORIGIN = 0x40378000, LENGTH = 64k     /* phys 0x3FC88000-0x3FC98000。WiFi *iramを収容 */
  /* DRAM: 実機検証済みの物理上限0x3FCDB700まで使用（旧268k=0x3FCDB000から+0x700拡張）。
   * 拡張自体はカーネル重要データ隔離領域（追記47）確保のため。前後ガードギャップ
   * (各2KB)は[追記56]で撤去済み（blob OOB説消滅、DRAM 4KB回収のため）。 */
  DRAM (rw) : ORIGIN = 0x3FC98000, LENGTH = 0x43700 /* phys 0x3FC98000-0x3FCDB700 */
}
ENTRY(_start)

SECTIONS
{
  /* --- SRAM: 起動コード（ROMがロード、フラッシュマップ前に実行）---
   * start.o と flash_cache_init.o はフラッシュマップ前に走るためSRAMに置く。 */
  .iram_boot : ALIGN(1024)
  {
    KEEP(*(.kernel_vectors))
    KEEP(*(.entry.text))
    *(.iram_boot .iram_boot.*)
    *start.o(.literal .text .literal.* .text.*)
    *flash_cache_init.o(.literal .text .literal.* .text.*)
    *(.iram1 .iram1.*)
    /* WiFi blobのIRAM_ATTRコード（割込み・cache-off文脈で走るためSRAM必須） */
    *(.wifi0iram .wifi0iram.*)
    *(.wifirxiram .wifirxiram.*)
    *(.wifislpiram .wifislpiram.*)
    *(.wifislprxiram .wifislprxiram.*)
    *(.wifiextrairam .wifiextrairam.*)
    *(.wifiorslpiram .wifiorslpiram.*)
    *(.coexiram .coexiram.*)

    /*
     *  ★★PSRAM（A1_M5_PSRAM=ON）: **フラッシュのクロックを変える関数は IRAM に置く**。
     *
     *  【なぜ必須か・実機 p71 で確かめた】`esp_psram_impl_enable()` は S3 で無条件に
     *  `mspi_timing_enter_low_speed_mode(true)` を呼び、★**MSPI(=フラッシュ)のコア
     *  クロックを変更する**。フラッシュから命令フェッチしながらこれを実行すると
     *  ★**クロックが変わった瞬間に次の命令が取れない** ⇒ LoadProhibited で即死する
     *  （実機 p71: `mspi_timing_config_set_flash_clock` で crash）。
     *
     *  ★**IDF 自身が同じ指定をしている**（`esp_psram/linker.lf`）:
     *      if SPIRAM_MODE_QUAD = y: … `esp_psram_impl_ap_quad (noflash)`
     *  `noflash` ＝ IRAM 配置。★我々はこれを**移植し忘れていた**。
     *
     *  ★サイズは実測で ~2KB（impl_quad 1451 + mspi 3 本 584）。
     *  IRAM は 64KB あり、現状の使用は 796B なので余裕は十分。
     *  ★`esp_psram.c` 本体は**入れない**——クロックを触るのは impl 側だけで、
     *  低速モードのままフラッシュを読むこと自体は動く（IDF もそうしている）。
     */
    *libpsram.a:ps_impl_quad.o(.literal .literal.* .text .text.*)
    *libpsram.a:ps_mspi_tuning.o(.literal .literal.* .text .text.*)
    *libpsram.a:ps_mspi_cfg.o(.literal .literal.* .text .text.*)
    *libpsram.a:ps_mspi_delay.o(.literal .literal.* .text .text.*)
    . = ALIGN(4);
  } > IRAM

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
    *(.rodata .rodata.* .constdata .constdata.*)
    *(.rodata_wlog_error .rodata_wlog_error.*)
    *(.rodata_wlog_warning .rodata_wlog_warning.*)
    *(.rodata_wlog_info .rodata_wlog_info.*)
    *(.rodata_wlog_debug .rodata_wlog_debug.*)
    *(.rodata_wlog_verbose .rodata_wlog_verbose.*)
    *(.xt_except_table .gcc_except_table*)
    *(.eh_frame .eh_frame.* .eh_frame_hdr)
    *(.xt.lit .xt.lit.* .xt.prop .xt.prop.*)

    /* --- C++ グローバル/静的コンストラクタ表シンボル（.init_array）---
     *  §7 変更点②。
     * M5ライブラリ（C++）のグローバルオブジェクト初期化に必要。読み取り
     * 専用の関数ポインタ表なので DROM(rodata)側へ置く。走査・呼出しは
     * m5/compat/cxx_runtime.cpp の esp_run_init_array()（アプリ main_task
     * 先頭で1回呼ぶ。FMP3の共有起動シーケンスからは呼ばない。design.md §3）。
     *
     * ★golden バイト不変性：C のみの既存 5 seam 構成には .init_array 入力が
     *   存在せず空（start==end，0バイト）。5構成すべてで golden sha256 一致を
     *   実測確認済み（seam-s3-smp/ble は完全一致，Wi-Fi 2 構成の差は本変更とは
     *   無関係の既存環境差＝リンカスクリプト非改変の clean HEAD でも同値を再現）。
     *
     * ★★重要な未完事項（1-4 へ引き継ぎ）：本プロジェクトのツールチェーン
     *   （xtensa-esp-elf esp-14.2.0）は --enable-initfini-array 無効ビルドのため
     *   グローバル C++ コンストラクタを .init_array ではなく **.ctors** に出力する
     *   （-fno-exceptions 等では切替不可＝ツールチェーン既定）。
     *   よって実 M5 XIP ビルドでは .ctors の集約が必須になる。ところが BT ライブラリ
     *   （esp/lib/bt_esp32s3_espidf/obj/bt_rtc_time.o）等の blob が .ctors を持ち，
     *   現状 --gc-sections で除去されている。ここで KEEP(*(.ctors)) を足すとそれを
     *   復活・再配置して seam-s3-ble/wifi の app_xip.bin が変化する。seam
     *   起動は .ctors を走査しないため機能不変だが golden バイトは変わる。したがって
     *   .ctors の KEEP 集約は，M5 XIP 構成を新設する 1-4 で
     *     (a) EXCLUDE_FILE で blob の .ctors を除外して byte-identity を保つ，または
     *     (b) golden を更新する
     *   のいずれかをユーザー判断で行う。
     *   ★本 M5 変種（1-4）は (a) を採る＝下で .ctors を KEEP 集約する。ただし
     *     A1_VARIANT=m5 は blob を一切リンクしないため復活対象の blob .ctors は
     *     存在せず、EXCLUDE_FILE は現段階では実質 no-op の前方ガードである。
     *   ※検証用 SRAM リンカ esp32s3_devkitc.ld も同様に .ctors を KEEP 集約する。 */
    . = ALIGN(4);
    __init_array_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = ABSOLUTE(.);
    /* --- .ctors 集約（M5 グローバルコンストラクタを esp_run_init_array() が走査）---
     * ツールチェーン esp-14.2.0 は C++ ctor を .ctors へ出力する（1-3 実測）。
     * A1_VARIANT=m5 は blob を一切リンクしないため、ここで復活する blob .ctors は
     * 存在せず、単純 KEEP で安全（検証用 esp32s3_devkitc.ld と同一パターン）。
     * ★将来 M5＋blob 構成を作る場合は、ここに EXCLUDE_FILE で blob オブジェクト
     *   （bt_rtc_time.o 等）の .ctors を除外する行を追加して byte-identity を保つこと。
     *   EXCLUDE_FILE は SORT_BY_INIT_PRIORITY とは併用できず、また
     *   `*(EXCLUDE_FILE(file...) .ctors)` の形（プレーンなセクション名のみ）で書く。 */
    . = ALIGN(4);
    __ctors_start = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors))
    __ctors_end = ABSOLUTE(.);

    . = ALIGN(4);
    _flash_rodata_end = ABSOLUTE(.);
  } > DROM
  _flash_rodata_size = _flash_rodata_end - _flash_rodata_start;

  /*
   *  ★★ESP-IDF の MMU 管理（esp_mm/esp_mmu_map.c）へ「ここは既に flash が使っている」
   *  と教えるための予約シンボル。
   *
   *  【なぜ要るか】PSRAM の仮想窓は `SOC_EXTRAM_DATA_LOW/HIGH = 0x3C000000-0x3E000000`
   *  で、★**我々の DROM(0x3C100000) はその中にある**。S3 では flash の rodata と PSRAM が
   *  同じ DBUS 番地空間を MMU エントリ単位で分け合う。
   *  IDF は `s_reserve_irom_region()` / `s_reserve_drom_region()` でこの 4 本を読み、
   *  ★**その範囲を PSRAM 用の空き番地から除外する**。
   *  ⇒ ★**定義しないと、IDF は 0x3C100000 付近を空きと見なして PSRAM を割り当て、
   *  `mmu_hal_map_region()` が我々の rodata マッピングを上書きして即クラッシュする。**
   *
   *  ★シンボルを足すだけで、セクションもバイトも増えない（app_xip.bin は不変）。
   *  ★IDF 側は `mmu_ll_vaddr_to_laddr(end) - mmu_ll_vaddr_to_laddr(start) == 長さ`
   *  を assert するので、**同じ窓の中の実アドレス**でなければならない。
   */
  _instruction_reserved_start = _flash_text_start;
  _instruction_reserved_end   = _flash_text_end;
  _rodata_reserved_start      = _flash_rodata_start;
  _rodata_reserved_end        = _flash_rodata_end;

  /* --- SRAM DRAM: data/bss ---
   * .dataはSRAM直接ロード(LMA=VMA)。ROMがセグメントロードで初期値をSRAMへ置くため
   * start.Sの__idata→__dataコピーは不要(S3はOMIT_DATA_INIT)。DROMへ載せると
   * esptoolがPADDINGセグメント(load 0x0)を挿入しROMロードが破綻するため避ける。 */
  .data : ALIGN(4)
  {
    __data_start = .;
    /* seam-S3 LX7: esp32s3 bootloaderが segment#0 先頭を esp_app_desc として
     * 読むため、.appdesc(seam_s3_appdesc.o)を .data の先頭へKEEP配置する。
     * seam以外(Direct Boot)は .appdesc 提供obj無し=何も配置されず非回帰。 */
    KEEP(*(.appdesc))
    *(.data .data.*)
    *(.dram1 .dram1.*)          /* WiFi DRAM_ATTR初期化データ */
    *(.sdata .sdata.*)
    . = ALIGN(4);
  } > DRAM
  __idata_start = ADDR(.data);
  __idata_end = __idata_start + SIZEOF(.data);

  .bss (NOLOAD) : ALIGN(4)
  {
    __bss_start = .;
    /*  BT(BLE)コントローラのメモリ解放境界シンボル。bt.cの
     *  esp_bt_controller_mem_release()が参照する（bt_smokeでは未使用だが
     *  -ffunction-sections無しのbt.oではgc-sectionsで除去されず参照が残る）。
     *  blob(libbtdm_app.a)の.bss/.data実体は下の*(.bss)/.dataに通常配置され
     *  機能する。ここは全シンボルを同一位置＝ゼロ長領域とし、mem_release時に
     *  0バイト解放（無害）にする。BLEブリングアップ段階ではこれで十分。 */
    _bt_bss_start = ABSOLUTE(.); _bt_bss_end = ABSOLUTE(.);
    _bt_controller_bss_start = ABSOLUTE(.); _bt_controller_bss_end = ABSOLUTE(.);
    _bt_data_start = ABSOLUTE(.); _bt_data_end = ABSOLUTE(.);
    _bt_controller_data_start = ABSOLUTE(.); _bt_controller_data_end = ABSOLUTE(.);
    /* カーネル重要データ(下記KERNEL_ISOLATE_OBJSに列挙するオブジェクト)は、ここでは
     * EXCLUDE_FILEで除外し、隔離専用セクション(.kernel_bss、本ファイル末尾)へ回す。
     * 追記47参照。 */
    *(EXCLUDE_FILE(
        *kernel_cfg.o
        *core_kernel_impl.o
        *chip_kernel_impl.o
        *chip_ipi.o
        *target_timer.o
        *taskhook.o
        *time_event.o
      ) .bss .bss.*)
    *(EXCLUDE_FILE(
        *kernel_cfg.o
        *core_kernel_impl.o
        *chip_kernel_impl.o
        *chip_ipi.o
        *target_timer.o
        *taskhook.o
        *time_event.o
      ) .dram0.bss)
    *(EXCLUDE_FILE(
        *kernel_cfg.o
        *core_kernel_impl.o
        *chip_kernel_impl.o
        *chip_ipi.o
        *target_timer.o
        *taskhook.o
        *time_event.o
      ) .sbss .sbss.*)
    *(EXCLUDE_FILE(
        *kernel_cfg.o
        *core_kernel_impl.o
        *chip_kernel_impl.o
        *chip_ipi.o
        *target_timer.o
        *taskhook.o
        *time_event.o
      ) COMMON)
    . = ALIGN(4);
    /* __bss_end はここでは確定させない。start.S の BSSゼロクリアループ
     * （arch/xtensa_gcc/common/start.S、core_asm.inc の BSS_START/BSS_END）が
     * [__bss_start, __bss_end) を単純に0埋めするため、下記.kernel_bss（隔離
     * 領域）もこの範囲に含めないとTCB/PCB等が未初期化のまま起動し即ハングする
     * （本ファイル変更時に実機で実測・特定した不具合。追記47参照）。__bss_end の
     * 実体値は本ファイル末尾で.kernel_bssの終端まで含めて定義する。 */
  } > DRAM

  /* ============================================================
   *  カーネル重要データの隔離領域
   * ============================================================
   *  背景：導入当初（追記47）は、WiFiバイナリblob（libpp.a等、ソース非公開・
   *  パッチ不可）にカーネル状態（_kernel_pcb_prc1近傍）へ着弾する境界外書込み
   *  バグがあるとの仮説（追記37/42）に基づき、FMP3カーネル本体が静的に保持
   *  する全状態（TCB/PCB/セマフォ/ミューテックス/データキュー制御ブロック・
   *  タスクスタック・割込みスタック・アイドルスタック・SMPギャントロック・
   *  クロスコアIPI状態・割込み/例外ハンドラテーブル等。実体は下記
   *  KERNEL_ISOLATE_OBJS＝kernel_cfg.o他）を、WiFiタスクスタック・netバッファ・
   *  シム(esp_shim等)のデータから物理的に引き離した専用DRAM領域へ移し、前後に
   *  ガードギャップ（各2KB、未使用番地、NOLOAD）を置いていた。
   *
   *  [追記56] ガードギャップは撤去済み。WiFi安定性問題の真因はその後の実機
   *  JTAG解析で完全に別物と確定した（HRTのCCOUNTラップ不整合＝00829ef、
   *  Window Overflow/Underflow例外ハンドラでのZOL(LBEG/LEND/LCOUNT)未退避＝
   *  20f8a23）。blobによるカーネル領域への境界外
   *  書込み説はこの過程で否定され、ガードギャップの保護価値は無くなった。
   *  一方でDRAM残量逼迫（-DWIFI_THROUGHPUT構成が診断recorder等の追加で
   *  リンク不能になる）が実害として発生したため、ガード2KB×2=4KBを回収する。
   *  .kernel_bssによるオブジェクトグルーピング自体（隔離領域の"置き場所を
   *  1箇所にまとめる"効果）は無害なので維持する。
   * ============================================================ */
  .kernel_bss (NOLOAD) : ALIGN(4)
  {
    _kernel_isolated_start = ABSOLUTE(.);
    /* KERNEL_ISOLATE_OBJS: FMP3カーネル本体＋SMP/割込み基盤の静的状態を持つ
     * オブジェクト一覧。TCB/PCB本体はkernel_cfg.o（FMP3コンフィギュレータ生成、
     * タスクスタック・セマフォ/ミューテックス/データキュー制御ブロックも同居）。
     * 残り6つは割込み/例外ハンドラテーブル・SMPギャントロック・クロスコアIPI・
     * hrt/tick状態・taskhook・time_eventの各小状態（core_kernel_impl.o /
     * chip_kernel_impl.o / chip_ipi.o / target_timer.o / taskhook.o /
     * time_event.o）。 */
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

  /* __bss_end の確定値：start.S の単純0クリアループ対象を
   * [__bss_start, __bss_end) = 通常.bss〜隔離カーネル領域(.kernel_bss)
   * まで拡張する。[追記56] ガードギャップ撤去に伴い、確定基準を
   * .kernel_guard_postから.kernel_bssへ変更（意味は同じ：隔離カーネル
   * 領域の終端まででBSSクリアを終える）。.diag_noinitは意図的にこの
   * 範囲の外（後方）に置き、従来通り0クリア対象外を維持する。 */
  __bss_end = ADDR(.kernel_bss) + SIZEOF(.kernel_bss);

  /* ============================================================
   *  クラッシュ/ハング診断recorder用noinit領域（常設recorder基盤）
   * ============================================================
   *  target/esp32s3_devkitc_gcc/diag_recorder.cのdiag_region（イベント
   *  リング・per-coreハートビート・パニックスナップショット）を置く。
   *  .kernel_bss（カーネル本体）とは別の場所（直後）に置き、リセット/
   *  再起動を跨いでも内容が消えないNOLOADとする。__bss_end確定より後に
   *  置くことで、start.SのBSSゼロクリアループ（[__bss_start,__bss_end)）
   *  の対象外になる（.noinitと同じ扱い）。[追記56] 従来はこの位置に
   *  .kernel_guard_post（2KB）が挟まっていたが撤去済み。
   *
   *  DRAM残量はこの時点で約1272バイト（実機map実測、追記47）しか無い
   *  ため、diag_region本体はヘッダ16B+heartbeat8B+ring_head4B+
   *  ring(64*16B=1024B)+snapshot60B=1112Bに収め、後続の.noinit用に
   *  約160Bの余裕を残す（要求仕様の「64〜128エントリ」の下限側を採用。
   *  詳細な内訳はdiag_recorder.cのコメント参照）。
   */
  .diag_noinit (NOLOAD) : ALIGN(4)
  {
    _diag_noinit_start = ABSOLUTE(.);
    *(.diag_noinit .diag_noinit.*)
    . = ALIGN(4);
    _diag_noinit_end = ABSOLUTE(.);
  } > DRAM

  .noinit (NOLOAD) : ALIGN(4) { *(.noinit*) } > DRAM

  /* デバッグ情報は非ロード（イメージに含めない） */
  /DISCARD/ : { *(.comment) *(.note.*) }
}
