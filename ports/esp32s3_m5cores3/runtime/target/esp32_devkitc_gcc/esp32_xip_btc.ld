/*
 *  無印ESP32(Xtensa LX6) flash-XIP リンカ（BT Classic/BlueDroid対応・W3派生）
 *
 *  esp32_xip.ld（W1 Wi-Fi/W2 BLE用）からのBT Classic専用フォーク。
 *  唯一の差：DRAM領域の先頭を CONFIG_BTDM_RESERVE_DRAM(0xdb5c、ESP-IDF
 *  v5.5 hal/nuttx/esp32/include/sdkconfig.h実測値)ぶんだけ後退させる。
 *
 *  理由（実機クラッシュから特定）：classic ESP32のBTコントローラ
 *  （bt/controller/esp32/bt.c、libbtdm_app.a）は「exchange memory (EM)」
 *  と呼ばれる内部作業領域を、リンカが割り付ける通常のC変数としてでは
 *  なく、DRAM先頭0x3ffb0000から固定ハードコードされた領域として直接
 *  読み書きする（esp_bt.hのSOC_MEM_BT_DATA_START=0x3ffae6e0/
 *  SOC_MEM_BT_EM_START=0x3ffb0000等の固定アドレス定義が根拠）。
 *  ESP-IDF本家はesp_system/ld/esp32/memory.ld.inで
 *  `dram0_0_seg: org = 0x3FFB0000 + CONFIG_BTDM_RESERVE_DRAM`として
 *  この領域を素通しで確保している。
 *
 *  本ポートのesp32_xip.ld(W1/W2共用)はDRAM ORIGINを素の0x3FFB0000から
 *  始めていたため、BTコントローラのEM領域と本ポートの.data/.bss/
 *  kernel_bss(TCB等)が物理的に重なる恐れがあった。bt_classic_ctrl_smoke
 *  でesp_bt_controller_enable(CLASSIC_BT)直後にFatal exception(0)
 *  IllegalInstruction(epc1=0x400b260c、IRAM別名窓経由でDRAM上のデータを
 *  命令として誤フェッチ)が再現一貫して発生し、本ファイルで切り分け中。
 *  W1のWi-Fiは固定EM領域を使わないため同一レイアウトでも無症状だった、
 *  という説明と整合する。
 *
 *  W1(Wi-Fi)/W2(BLE)の既存ビルド・非回帰には影響しない独立ファイル
 *  （esp32_xip.ldは無変更のまま）。BT Classic/BlueDroid系ビルド
 *  （build_bt_classic_*_esp32.sh）のみが本ファイルを使う。
 */

/*
 *  無印ESP32(Xtensa LX6) flash-XIP リンカ（WiFi対応・W1、BT Classic用
 *  フォーク元。以下の説明文はフォーク元のまま残す）
 *  esp32s3_xip.ld を無印ESP32のメモリマップへ読み替え：
 *    .text/.rodata はフラッシュ(IROM 0x400D0000 / DROM 0x3F400000)へXIP配置、
 *    起動コード(.iram_boot=start.S+flash_cache_init)と.data/.bssはSRAMへ。
 *  一次情報：ESP-IDF v5.5 components/soc/esp32/include/soc/ext_mem_defs.h
 *    （SOC_IRAM0_CACHE_ADDRESS_LOW=0x400D0000, DROM=0x3F400000, 64KBページ）、
 *    components/esp_system/ld/esp32/memory.ld.in
 *    （IRAM iram0_0=0x40080000 len 0x20000、DRAM dram0_0=0x3FFB0000）。
 *  S3との差：IROM/DROM/IRAM/DRAMの全ベースが異なる。無印はDMA可能な
 *    内蔵SRAMが 0x3FFB0000〜0x3FFFFFFF に限られ、WiFi DMAバッファ/ヒープは
 *    この範囲に置く必要がある（PSRAM経由DMA不可）。
 *  ROM窓ハンドラ等の絶対シンボル：esp32_devkitc.ld と同じ実測値を流用。
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
  /* SRAM: IRAM(SRAM0) 0x40080000〜0x400A0000(128KB)。起動コード+WiFi iram。 */
  IRAM (rwx): ORIGIN = 0x40080000, LENGTH = 0x20000
  /* DRAM: 0x3FFB0000〜0x3FFF8000(288KB、DMA可能領域)。SRAM1上部
   * (0x3FFE0000〜0x40000000 は IRAM別名 0x400A0000〜0x400C0000)を
   * DRAM側で使う（本ポートのIRAMはSRAM0=0x40080000〜0x400A0000のみ使用のため
   * SRAM1をIRAMに使わず、DRAMへ回せる）。最上部32KBはROM Direct Boot時の
   * ROMスタック/データ退避のため残す。WiFi .bss(実測でDMA可SRAM逼迫、W1で
   * 66KB超過だった)はここで吸収し、なお足りなければesp_shimヒーププールを縮める
   * （recipe #8：無印は使用可SRAMがS3より小さい）。 */
  /*  BT Classic派生の核心：CONFIG_BTDM_RESERVE_DRAM(0xdb5c)ぶんだけ
   *  ORIGINを後退・LENGTHを短縮し、BTコントローラのEM固定領域
   *  （0x3ffb0000〜0x3ffbdb5c相当）を我々の.data/.bss/kernel_bssの
   *  配置対象から除外する。 */
  DRAM (rw) : ORIGIN = 0x3FFB0000 + 0xdb5c, LENGTH = 0x48000 - 0xdb5c
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
    /*  W3(BT Classic)チェックポイントで発見・修正：start.oの_start
     *  （ENTRY(_start)のリセットエントリ本体）がKEEP()無しだと
     *  --gc-sections で「未参照」と判定され破棄される実例を確認した
     *  （bt_classic_ctrl_smokeビルドでfmp_xip.mapの"Discarded input
     *  sections"にobjs/start.o(.text)が列挙され、esp32.rom.ldの
     *  PROVIDE(_start = 0x40000704)ヘフォールバックしてしまい、ROM
     *  ブートローダがROM内部アドレスへジャンプ→即RTCWDT_RTC_RESETの
     *  高速リブートループになっていた＝ENTRY()指定だけではgc-sections
     *  からエントリシンボルの属するセクションが保護される保証が無い
     *  （少なくとも本ツールチェーンのld挙動では）。KEEP()で明示的に
     *  保護する。W1/W2ビルドで顕在化しなかったのはSOBJS構成の違いで
     *  たまたま他の生存パスからstart.oが間接参照されていた可能性が
     *  あるが、確実性のため全ビルド共通でKEEP化する（W1/W2にも影響
     *  するが安全側の修正＝壊れていたリスクを塞ぐのみで退行させない）。 */
    /*  [2026-07-15 W3継続セッション追記・真因特定] 上記KEEP自体は必須で
     *  正しいが、KEEP追加後もM5Stick実機で起動直後にIllegalInstruction
     *  (epc1=0x400b2604、未マップSRAM1域への暴走call4)でクラッシュする
     *  別の不具合が残っていた。fmp_xip.mapを直接確認して真因を特定：
     *  start.oの.literal（GAS/ldのxtensa link-time relaxationにより実際は
     *  出力上".iram_boot.literal"というセクション名で合成され、上のより
     *  早い行の汎用ワイルドカード`*(.iram_boot .iram_boot.*)`に先に飲み
     *  込まれる）はstart.oの.textより前方（flash_xip_mapのさらに前）に
     *  配置される一方、`_fmp_reset_entry = ABSOLUTE(.)`をKEEP(*start.o
     *  (.text...))の直前で評価していたため、.textセクションの整列要求
     *  （start.SのSTART: `.align 4`）を満たすための2バイトのfill（詰め物）
     *  が_fmp_reset_entry評価の「後」に挿入されていた。結果、
     *  _fmp_reset_entry（＝_start＝ROMのジャンプ先）が実際のコード開始
     *  より2バイト手前を指し、CPUが整列パディング境界からずれた位置から
     *  命令フェッチを開始→以降のバイト列がバイトシフトして誤デコードされ、
     *  未マップアドレスへのcall4として暴走していた（GDB `disassemble`の
     *  アドレス範囲表示とfmp_xip.mapの実配置アドレスを突き合わせて特定）。
     *  対策：`_fmp_reset_entry`評価の直前で明示的に4バイト整列する
     *  （`. = ALIGN(4);`）。これによりfillは境界確定前に消化され、
     *  マーカーの値が実際の.text開始アドレスと一致する。 */
    . = ALIGN(4);
    KEEP(*start.o(.literal .literal.*))
    _fmp_reset_entry = ABSOLUTE(.);
    KEEP(*start.o(.text .text.*))
    *flash_cache_init.o(.literal .text .literal.* .text.*)
    /* 割込み/例外エントリ（_kernel_l1int_entry/_kernel_l3int_entry/
     * _kernel_core_exc_entry, core_support.o）とチップL1エントリ・
     * コンテキストスイッチ核はSRAMに置く：(1)ベクタ表(.kernel_vectors)からの
     * 直接j命令が±128KB範囲でしか届かず、flash(.flash_text≒320KB先)へ置くと
     * 「dangerous relocation: cannot encode: j」になる。(2)割込みエントリは
     * cache-off文脈でも走るためSRAM必須（機能的にも正しい）。 */
    *core_support.o(.literal .text .literal.* .text.*)
    *chip_l1int_entry.o(.literal .text .literal.* .text.*)
    *(.iram1 .iram1.*)
    /* WiFi blobのIRAM_ATTRコード（割込み・cache-off文脈で走るためSRAM必須） */
    *(.wifi0iram .wifi0iram.*)
    *(.wifirxiram .wifirxiram.*)
    *(.wifislpiram .wifislpiram.*)
    *(.wifislprxiram .wifislprxiram.*)
    *(.wifiextrairam .wifiextrairam.*)
    *(.wifiorslpiram .wifiorslpiram.*)
    *(.coexiram .coexiram.*)
    *(.coexsleepiram .coexsleepiram.*)
    /* 無印ESP32のlibphyはPHY較正コードを.phyiramに置く（cache-off文脈で走るため
     * SRAM必須）。S3版libphyには無い節。ここに置かないとflash(.flash_text直後)へ
     * 落ち、①独立IROMセグメント化してROMローダがcache未設定のflash窓へロード→fault、
     * ②実行時cache-offでPHY較正ハング、の二重障害になる。 */
    *(.phyiram .phyiram.*)
    . = ALIGN(4);
  } > IRAM

  /*  KEEP()だけでは不十分だったため追加した本修正の核心：esp32.rom.ld
   *  （後段で-Tされる、ROM自身のリンク時参照用スクリプト）はROMの
   *  コールドブートエントリを指す `PROVIDE ( _start = 0x40000704 );` を
   *  持つ。start.o の `_start`（本ポートのリセットエントリ）はオブジェクト
   *  ファイル由来の“本物の”定義のはずでPROVIDEはそれに譲るはずだが、
   *  実測では--gc-sections+--allow-multiple-definition下で本ポートの
   *  _start定義が採用されず0x40000704（ROM側)が勝ってしまう事象を
   *  確認した（nmでKEEP後もEntry point/シンボル値が0x40000704のまま）。
   *  ここで esp32_xip.ld 側（-Tの中で最初に処理される＝esp32.rom.ldより
   *  前）で _start を「通常の（PROVIDEでない）代入」として確定させる。
   *  これによりesp32.rom.ld側のPROVIDEは「既に定義済み」を検出して
   *  no-opになる（PROVIDEの定義どおりの挙動）。 */
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
     * 理由：start.S が SP初期化で _kernel_istkpt_table を flash_xip_map() の
     * 「前」に読む（l32i a1）。DROM(flash XIP)はこの時点で未マップのため、
     * ここに置くとブートローダのキャッシュ残渣に依存した不定読み→不正SP→
     * flash_xip_map への callx4 で無出力ハングになる（.rodata レイアウトの
     * 僅かな変化でアドレスがブートローダ残渣域を外れて顕在化した）。 */
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
    . = ALIGN(4);
    _flash_rodata_end = ABSOLUTE(.);
  } > DROM
  _flash_rodata_size = _flash_rodata_end - _flash_rodata_start;

  /* --- SRAM DRAM: data/bss（LMA=VMA、ROM Direct Bootが初期値をSRAMへ配置）--- */
  .data : ALIGN(4)
  {
    __data_start = .;
    *(.data .data.*)
    *(.dram1 .dram1.*)          /* WiFi DRAM_ATTR初期化データ */
    *(.sdata .sdata.*)
    . = ALIGN(4);
  } > DRAM
  __idata_start = ADDR(.data);
  __idata_end = __idata_start + SIZEOF(.data);

  /* SRAM常駐 rodata：kernel_cfg.o の const テーブル（_kernel_istkpt_table 等）。
   * ROM Direct Boot がロードする PROGBITS セクション（DRAM）。start.S が
   * flash_xip_map() 前に SP を _kernel_istkpt_table から読むため、DROM(flash)
   * ではなくここ(SRAM)へ常駐させる必要がある。elf2image が sram.elf の
   * ロード可能セグメントとして自動的に取り込む。 */
  .sram_rodata : ALIGN(4)
  {
    *kernel_cfg.o(.rodata .rodata.*)
    . = ALIGN(4);
  } > DRAM

  /* レイアウト重要修正（freeze根因）：カーネル重要データ(.kernel_bss ＝
   * TCB/セマフォ/タスクスタック/割込み・例外テーブル等)を、.bssより「先」に
   * 低位の安全DRAM(SRAM2, 0x3FFB0000〜0x3FFE0000)へ配置する。
   *  classic ESP32の SRAM1(0x3FFE0000〜) は ROM 予約領域で、先頭に
   *  xtos_exc_handler_table(0x3FFE0000) 等が居る。PHY/blob が ROM 関数を呼ぶと
   *  ROM がその領域へ書き込むため、そこに置かれた TCB(SHIM_TSK6〜LOGTASK等)が
   *  破壊され raster ビットが立ち、優先度2の timer タスクが E_RASTER で暴走して
   *  全体 freeze していた。旧レイアウトは .bss(blob) を先に置いたため .kernel_bss が
   *  高位(SRAM1)へ押し出されていた。順序を入替え、.kernel_bss を必ず ROM 予約より
   *  下に固定する。SRAM1 へは非クリティカルな blob バッファ(.bss)のみがはみ出す。
   *  __bss_start は先頭の .kernel_bss、__bss_end は末尾の .bss（start.S の
   *  ゼロクリアが両方を覆う）。 */
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

  /* .kernel_bss がROM予約(0x3FFE0000)へ食い込まないことをリンク時に保証する。 */
  ASSERT(_kernel_isolated_end <= 0x3FFE0000,
         "FATAL: .kernel_bss(TCB/stacks) overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")

  .bss (NOLOAD) : ALIGN(4)
  {
    _bt_bss_start = ABSOLUTE(.); _bt_bss_end = ABSOLUTE(.);
    _bt_controller_bss_start = ABSOLUTE(.); _bt_controller_bss_end = ABSOLUTE(.);
    _bt_data_start = ABSOLUTE(.); _bt_data_end = ABSOLUTE(.);
    _bt_controller_data_start = ABSOLUTE(.); _bt_controller_data_end = ABSOLUTE(.);
    /* カーネル重要データはEXCLUDE_FILEで除外済み（上の.kernel_bssへ配置） */
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

  /*  無印ESP32 BT blob(libbtdm_app.a: arch_main.o r_get_stack_usage)が単一
   *  アンダースコア名 _bss_start/_bss_end を参照するため別名で供給する。 */
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

  /*  レビュー指摘#4：.kernel_bss用ASSERT（上）はTCB/stack等カーネル重要
   *  データがROM予約SRAM1(0x3FFE0000〜)へ侵入しないことしか保証せず、
   *  後続の.bss/.diag_noinit/.noinitは無検出だった。
   *  ただし.bss自体の終端に同型ASSERTを機械的に追加することはできない：
   *  無印ESP32の大容量WiFi/BTブロブバッファ(.bss)は本ファイル上の
   *  .kernel_bssコメントが説明する設計どおり、非クリティカルなblob
   *  バッファに限りSRAM1(ROM予約域)へ意図的にはみ出す前提でDRAM
   *  LENGTHが組まれている（実機WiFi/BT実証済み）。2026-07-16実測：
   *  本ファイルでリンクしたbt_classic_ctrl_smoke_esp32で
   *  __bss_end=0x3fff249c、ROM予約境界0x3FFE0000を0x1249c(約75KB)超過
   *  （esp32_xip.ld/esp32_xip_wifi.ldでもそれぞれ約96KB/71KB超過を
   *  実測）。これらは全て実機検証済みビルドであり、.bss終端への
   *  無条件ASSERTは正規のビルドをリンクエラーにしてしまうため追加しない。
   *  代わりに、.diag_noinit/.noinitに「実際の中身」がある場合のみROM
   *  予約域侵入を検出するASSERTを追加する（サイズ0ならアドレスが
   *  ROM予約域に位置していても実害が無いため許容）。無印ESP32では
   *  diag_recorderがno-opスタブ(diag_recorder.c)のため現状は両方とも
   *  常にサイズ0で通過するが、将来classicにdiag recorder実装や
   *  __NOINIT_ATTR変数を追加した際の回帰検出として機能する。 */
  ASSERT(SIZEOF(.diag_noinit) == 0 || _diag_noinit_end <= 0x3FFE0000,
         "FATAL: .diag_noinit(diag recorder) overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")
  ASSERT(SIZEOF(.noinit) == 0 || (ADDR(.noinit) + SIZEOF(.noinit)) <= 0x3FFE0000,
         "FATAL: .noinit overlaps ROM-reserved SRAM1 (>=0x3FFE0000)")

  /DISCARD/ : { *(.comment) *(.note.*) }
}
