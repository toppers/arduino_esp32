/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  CLIC 版 ESP-IDF 割込み確保シムが使う CLIC 割込み線の単一真実源
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef ESP_SHIM_INTR_CLIC_LINES_H
#define ESP_SHIM_INTR_CLIC_LINES_H

/*
 *  ============================================================================
 *  なぜこのヘッダが在るか（2026-08-15・C-1）
 *  ============================================================================
 *  `esp/shim/esp_shim_intr_clic.c` の線表と、`esp/shim/esp_shim_intr_clic.cfg` の
 *  `CFG_INT`/`CRE_ISR` は同じ線番号でなければならない。Xtensa 側は
 *  **実際にこれが破れて**（`seam-s3-m5-factory` でシム=線8／cfg=線13＝IPI）、
 *  その反省から `esp/shim/esp_shim_intr_lines.h` という単一真実源が生まれた。
 *  P4 版も最初からその形で始める。
 *
 *  cfg 側からこのヘッダを読めることの根拠は Xtensa 版ヘッダ冒頭に書いた通りで、
 *  P4 でも同じである（cfg_py の pass1 は `#include` を解釈せず `cfg1_out.c` へ
 *  そのまま吐き、静的APIの引数は C コンパイラが評価する）。実在の前例:
 *  `fmp3/arch/riscv_gcc/esp32p4/chip_serial.cfg` の
 *  `CFG_INT(INTNO_SIO, { INTATR_SIO, INTPRI_SIO });`。
 *
 *  ============================================================================
 *  方式の選定: 「固定スロット式（cfg 事前宣言）＋プールとして払い出す」
 *  ============================================================================
 *  【純粋な動的プールは構造的に不可能である（事実）】
 *  FMP3 の `CFG_INT` は**静的 API のみ**であり、`acre_int` に相当するものが無い。
 *  さらに `ena_int()` は `check_intno_cfg()` を通り、それは
 *      p_intcfg_table[prcidx][INTNO_MASK(intno)] != 0U
 *  を見る（`fmp3/arch/riscv_gcc/esp32p4/chip_kernel_impl.h`。表は cfg が生成する）。
 *  ⇒ **cfg に `CFG_INT` の無い線は、実行時にどうやっても開けられない。**
 *  したがって「線そのものを実行時に生やす」形のプールは選択肢に存在しない。
 *  実行時に選べるのは「事前宣言済みの線のうちどれを、どのソースに割り当てるか」
 *  だけである。本ファイルはその**事前宣言済みの集合**を定義する。
 *
 *  【スロット数を「見積り」で決めない（BL-H-9 の教訓）】
 *  2026-08-15 の BL-H-9 は、LX6 のスロット数を「SD-over-SPI が 1 本使う」という
 *  **数え落とし**で 1 に決めた結果、golden 構成（`seam-lx6-m5-factory`）の SD が
 *  丸ごと立たなくなっていた（LCD が先に唯一のスロットを取り、SD が
 *  `ESP_ERR_NO_MEM` を食っていた。`.steering/20260815-blh9-lx6-intr-slots/`）。
 *  ⇒ **利用者を列挙し、各行に「同時に何本要るか」と出典を書く。**下表がそれである。
 *
 *  【P4 では「余裕を持って取る」ことのコストが小さい（Xtensa との差）】
 *  Xtensa(S3/LX6) で線が窮屈なのは、`EXTERN_LEVEL` かつレベル1 の線が **12 本**しか
 *  無く、その大半を Wi-Fi/BT blob が占めるからである（`esp_shim_intr_lines.h`）。
 *  P4 は事情が違う（いずれも実測）:
 *    - 外部割込みを載せられる CLIC 線は **16..47 の 32 本**
 *      （`CLIC_TNUM_INTNO=48`・`CLIC_EXT_OFFSET=16`。`esp32p4.h`）。
 *    - **tick と IPI は内部線**（mtime=7 / msip=3。`esp32p4.h`）であり、
 *      割込みマトリクスからは駆動できない ⇒ **ペリフェラル配線と構造的に衝突しない**。
 *      Xtensa 側で検査(A)(B) が必要だったのはここが逆だったからである。
 *    - 本 repo の P4 が現に使っている外部線は **16/17/19/45/46/47 の 6 本だけ**
 *      （下の「予約線」参照）。
 *  ⇒ 窮屈さは制約ではない。**残るリスクは「足りなくて実機で初めて分かる」側だけ**
 *    なので、列挙した必要数に**明示的な余裕**を足して取る。
 *
 *  【利用者の列挙（この表が NSLOT の根拠である）】
 *
 *  | 利用者                     | 同時本数 | 出典（一次情報）                                     |
 *  |----------------------------|---------|------------------------------------------------------|
 *  | EMAC（Ethernet）           | 1       | `esp/eth/os/eth_os_fmp3.c` が捕捉するのは             |
 *  |                            |         | `ETS_ETH_MAC_INTR_SOURCE` の 1 本のみ（方式(a) 実績） |
 *  | MIPI-DSI ブリッジ（Tab5）  | 1       | `esp-idf/components/esp_lcd/dsi/esp_lcd_panel_dpi.c`  |
 *  |                            |         | が `esp_intr_alloc(soc_mipi_dsi_signals[..].brg_irq_id`|
 *  |                            |         | ..)` を 1 パネルにつき 1 回（src=86）                  |
 *  | **DW-GDMA（DSI の給餌）**  | **1**   | **2026-08-16 追加（段v-2 フェーズB の実測）**。        |
 *  |                            |         | `esp_hw_support/dma/dw_gdma.c:638-646` が              |
 *  |                            |         | `esp_intr_alloc_intrstatus(ETS_DW_GDMA_INTR_SOURCE`    |
 *  |                            |         | `=24, ... ESP_INTR_FLAG_SHARED, ...)` をチャネル毎に。 |
 *  |                            |         | DPI パネルは DMA リンクを**無条件に**作るので、        |
 *  |                            |         | **DSI は 1 本ではなく 2 本要る**。実機で確認済み       |
 *  |                            |         | （src=86→線30 / src=24→線31。MAP 読み返し）。         |
 *  |                            |         | しかも**フレーム完了は DW-GDMA 側から来る**——        |
 *  |                            |         | rev v1.3 は VSYNC イベントを持たない                   |
 *  |                            |         | （`mipi_dsi_brg_ll.h:20-24`）ため、ブリッジ側の        |
 *  |                            |         | `n_isr` は 0 のままだった                              |
 *  | GPIO（タッチ等）           | 1       | `esp-idf/components/hal/esp32p4/include/hal/gpio_ll.h`|
 *  |                            |         | が P4 では `ETS_GPIO_INTR0_SOURCE` しか使わない       |
 *  |                            |         | （`gpio_ll_intr_enable_on_core` が core_id を無視）    |
 *  | SDMMC                      | 1       | 方式(a) の実績（`esp/eth/os/fmp3_eth_pools.h` の記録） |
 *  | **明示的な余裕**           | **1**   | BL-H-9 は「列挙し忘れた 1 本」で golden 構成が死んだ。 |
 *  |                            |         | P4 では余裕のコストが小さい（上記）ので先に積む。     |
 *  |                            |         | **2026-08-16: DW-GDMA を表に足したぶん余裕は 2→1**    |
 *  |                            |         | （NSLOT は 6 のまま。**まさに BL-H-9 の型の            |
 *  |                            |         | 数え落としが 1 件実在した**という記録）               |
 *  | **合計 = ESP_SHIM_CLIC_INTR_NSLOT** | **6** |                                            |
 *
 *  ⇒ 余裕を使い切ったら**黙って足さないこと**。表に行を足し、出典を書き、
 *    `cmake/a1_p4_intno_audit.sh` を通してから NSLOT を上げる。
 *
 *  【動的 ISR（`acre_isr`/`ENA_DYNISR`）は使わない（選定の記録）】
 *  fmp3_core の `acre_isr` は P4 でもリンクされている（ALLFUNC）。しかし本シムは
 *  **静的 `CRE_ISR` ＋ `exinf` にスロット番号**という形を採る。理由:
 *    (a) `acre_isr` を使っても `CFG_INT` と `ENA_DYNISR(line)` は結局静的に要る
 *        （`ena_int` の `check_intno_cfg` は変わらない）ので、**静的宣言は減らない**。
 *    (b) ハンドラの差し替えはスロット表の関数ポインタで既に実現できる。
 *        `acre_isr` を挟むと「ISR 生成の失敗」という新しい失敗モードが増えるだけ。
 *    (c) 同じ形の前例が 2 つあり、どちらも実機で通っている——
 *        Xtensa の `esp/shim/esp_shim_intr.c`、および P4 方式(a) の
 *        `esp/eth/os/eth_os_fmp3.c` の `fmp3_emac_isr_trampoline`。
 *  必要になったら (b) の判断だけを覆せばよい（線の事前宣言は共通のまま使える）。
 *
 *  ============================================================================
 *  線 17 の食い違い（段5/段6 の残件）の決着（2026-08-15・AC X-1）
 *  ============================================================================
 *  `esp/eth/os/fmp3_eth_pools.h` は「プロジェクト内の使用線」として
 *  **17(crosscore)** を挙げ、`fmp3/target/m5stamp_esp32p4_gcc/target_test.h` は
 *  **`INTNO1 = 17`** と定義している。段5 はこれを「未解決の食い違い」として記録した。
 *
 *  **結論: 両方とも、それぞれの世界では正しい。矛盾ではなく、射程の書き漏れである。**
 *
 *  根拠（一次情報）:
 *   - 「17=crosscore」は**方式(a)（IDF をリンクする世界）**の観測である。
 *     `esp_netif_init()` が起こす crosscore ソフト割込み
 *     （`ETS_FROM_CPU_INTR0_SOURCE`＝ソース 79。本 repo の
 *      `esp-idf/components/soc/esp32p4/include/soc/interrupts.h` の enum から機械導出）
 *     を **IDF の動的アロケータ**が CLIC 線 17 へ割り当てた、という実機 JTAG での
 *     確定結果である（移植元 `~/TOPPERS/ESP32/esp32_p4/HANDOFF.md:744-757`。
 *     SDMMC を同じ 17 に置いていたため秒間 82 万回の疑似ストームになった事故の記録）。
 *   - seam（本 preset `seam-p4-smp`）には **IDF の動的アロケータも `esp_netif` も
 *     存在しない**。`FROM_CPU_*` を使う主体も無い（本 repo の P4 側で `FROM_CPU` を
 *     参照するコードは 0 件＝実測。FMP3 の IPI は CLINT msip＝内部線 3 である）。
 *     ⇒ seam では線 17 は空いており、`INTNO1 = 17` は正しい。
 *
 *  ⇒ **どちらの世界でも、本シムは線 17 を使わない。**（seam では `INTNO1` が、
 *    方式(a) では IDF の crosscore が居るため。）これは下の検査 (E) で
 *    ビルド時に保証する（AC X-3）。
 *
 *  取込み層（`esp/eth/os/fmp3_eth_pools.h` は P4 repo からの取込み・
 *  `esp/eth/IMPORT_PROVENANCE.md` で「乖離: 無」）は**変更していない**。
 *  コメント 1 行のために取込みの byte 同一性を捨てると、以後の再取込みが高くつく。
 *  代わりに決着を「将来この線を選ぶ人が必ず読む場所」＝本ヘッダと
 *  `cmake/a1_p4_intno_audit.sh` に置いた（AC X-2）。
 */

/*
 *  参照する側の定義（線の衝突検査に使う）。
 *   - `CLIC_INTNO_MSIP`   … IPI が使う内部線（`fmp3/arch/riscv_gcc/esp32p4/esp32p4.h`）
 *   - `CLIC_INTNO_MTIMER` … tick が使う内部線（同上）
 *   - `CLIC_TNUM_INTNO`   … 線の本数（有効な線番号は 0..CLIC_TNUM_INTNO-1）
 *   - `CLIC_EXT_OFFSET`   … 外部割込みの線番号オフセット（16）
 *  tick/IPI は `CFG_INT` を通らず `chip_kernel_impl.c` が直接 CTL を設定するので、
 *  cfg の重複検査（`intno N is duplicated`）では検出できない。⇒ ここでやるしかない。
 *
 *   - `INTNO_SIO`         … コンソールが使う線（`target_syssvc.h`）
 *  こちらは `chip_serial.cfg` が `CFG_INT(INTNO_SIO,…)` を出すので cfg でも捕まるが、
 *  エラー文は「どの線が誰と重なったか」を言わない。BL-H-9 と同じ理由で先に落とす。
 */
#include "esp32p4.h"
#include "target_syssvc.h"

/*
 *  ============================================================================
 *  スロット数と、各スロットの CLIC 割込み線
 *  ============================================================================
 *  【予約線（この 12 本は使えない）】いずれも出典つき。
 *    3      IPI（CLINT msip・内部線）           `esp32p4.h`
 *    7      tick（CLINT mtime・内部線）         `esp32p4.h`
 *    16     `INTNO_SIO`（USB-Serial/JTAG）      `target_syssvc.h`
 *    17     `INTNO1`（テスト）                  `target_test.h`
 *           ＝方式(a) では IDF crosscore（上記「線17 の決着」）
 *    18     `INTNO_UNOPTED`（`INTNO1 + 1`）     `fmp3_core/test/test_dcre5.h`
 *           テストが「有効範囲内だが**未登録**」として使う＝登録してはいけない
 *    19     `INTNO3`（テスト）                  `target_test.h`
 *    22     IDF が割込みマトリクスの切断先に使う `idf_riscv_intr_impl.md`
 *    40..44 IDF の `vectors_clic.S` が予約       同上
 *           （T1WDT/CACHEERR/MEMPROT/ASSIST_DEBUG/IPC_ISR。**外部番号 24..28 に
 *             +16 したもの**であることを `esp-idf/components/riscv/vectors_clic.S`
 *             の表本体で確認済み）
 *    45     `INTNO2`（テスト）                  `target_test.h`
 *    46     `INTNO_EMAC`（Ethernet・段E）       `esp/eth/os/fmp3_eth_pools.h`
 *    47     SDMMC（慣行）                        同上
 *
 *  【空いている外部線】20, 21, 23..39（＝21 本）。
 *
 *  【30..35 を選んだ理由】
 *  低い側の塊（16..22）と高い側の塊（40..47）の**どちらからも離れた**中央を取る。
 *  こうすると、将来どちらの側が伸びても（テスト線が増える／IDF 予約が増える）
 *  本シムの 6 本を動かさずに済み、20/21/23..29 と 36..39 の**両側に**余裕が残る。
 *  連番にしてあるのは、実機ログで「スロット i は線 30+i」と即座に読めるようにするため。
 */
#define ESP_SHIM_CLIC_INTR_NSLOT	6
#define ESP_SHIM_CLIC_INTR_LINE0	30
#define ESP_SHIM_CLIC_INTR_LINE1	31
#define ESP_SHIM_CLIC_INTR_LINE2	32
#define ESP_SHIM_CLIC_INTR_LINE3	33
#define ESP_SHIM_CLIC_INTR_LINE4	34
#define ESP_SHIM_CLIC_INTR_LINE5	35

/*
 *  全スロット共通の FMP3 割込み優先度（内部表現ではなく `CFG_INT` に書く外部表現）。
 *
 *  【なぜ 1 つに固定するか】`CFG_INT` は静的であり、`ESP_INTR_FLAG_LEVELn` を
 *  実行時に受け取っても CLIC の CTL を動的に書き換える口を本シムは持たない
 *  （書き換えると、その線に載っている他の利用者の前提を壊す）。
 *  ⇒ **固定し、フラグは受けた回数を数えるだけにする**（黙って無視しない）。
 *
 *  【なぜ -4 か】P4 側の既存の実績と揃える:
 *    `INTPRI_SIO  = -4`（`target_syssvc.h`）
 *    `INTPRI_EMAC = -4`（`esp/eth/os/fmp3_eth_pools.h`）
 *    テスト線 `INTNO1/2/3_INTPRI = -4`（`target_test.h`）
 *
 *  【FMP3 intpri と CLIC level の対応（AC D-3a）】
 *    `INT_IPM(ipm) = ((uint_t)(-(ipm))) << 5`
 *    `EXT_IPM(pri) = -(PRI)((pri) >> 5)`      （`chip_kernel_impl.h`）
 *  CLIC は NLBITS=3 で、level は CTL バイトの上位 3 ビット [7:5] にある。
 *  ⇒ **CLIC level = -(FMP3 intpri)**。intpri -4 は CLIC level 4。
 *    最高が level 7（FMP3 の `TMIN_INTPRI = -7`）、最低が level 1（`TMAX_INTPRI = -1`）。
 *
 *  【`chg_ipm` との整合（AC D-3b）】
 *  CLIC のマスクは **inclusive**（閾値**以下**の level を全部止める。
 *  `idf_riscv_intr_impl.md` §1.5）。`chg_ipm(-N)` は
 *  `clic_set_context_priority(cidx, INT_IPM(-N))` ＝閾値 level N になるので、
 *  **level N 以下＝FMP3 の優先度 -N 以下**が止まる。これは FMP3 の
 *  「`chg_ipm(-N)` は優先度 -N 以下の割込みを禁止する」という意味論と一致する。
 *  ⇒ 本シムのスロット（level 4）は `chg_ipm(-4)` 以下で止まる。
 *  なお `test_ipmmask` が P4 で SKIP である事実は本段では変えていない（C-3 の別項）。
 */
#define ESP_SHIM_CLIC_INTR_INTPRI	(-4)

/*
 *  ISR の ISRPRI（同一線に複数 ISR が載ったときの呼出し順）。
 *  本シムは 1 線 1 ISR なので順序は問題にならないが、値は必要である。
 */
#define ESP_SHIM_CLIC_INTR_ISRPRI	1

/*
 *  ============================================================================
 *  ビルド時の機械照合
 *  ============================================================================
 *  Xtensa 版の反省（旧版は `#if defined(TOPPERS_ESP32_LX6)` の内側にしか検査が
 *  無く、S3 が線13 を使う場合を 1 件も捕まえられなかった）を踏まえ、
 *  **条件で分岐せず、線の値そのものを見る**。
 */

/*
 *  (0) 参照する定義が見えていること。
 *  見えていないと `#if X == Y` は 0==0＝真 や 0==30＝偽 に化けて、検査が
 *  素通りするか誤爆する（＝検査が存在しないのと同じ）。先に `#error` で落とす。
 */
#if !defined(CLIC_INTNO_MSIP)
#error "CLIC_INTNO_MSIP が見えていない（esp32p4.h）。線の衝突検査が素通りする"
#endif
#if !defined(CLIC_INTNO_MTIMER)
#error "CLIC_INTNO_MTIMER が見えていない（esp32p4.h）。線の衝突検査が素通りする"
#endif
#if !defined(CLIC_TNUM_INTNO)
#error "CLIC_TNUM_INTNO が見えていない（esp32p4.h）。範囲検査が素通りする"
#endif
#if !defined(CLIC_EXT_OFFSET)
#error "CLIC_EXT_OFFSET が見えていない（esp32p4.h）。外部線判定が素通りする"
#endif
#if !defined(INTNO_SIO)
#error "INTNO_SIO が見えていない（target_syssvc.h）。コンソール線の衝突検査が素通りする"
#endif

/*
 *  検査を 6 スロット分そのまま並べると読めなくなるので、1 スロットぶんの
 *  検査をマクロにまとめ、`#if` の条件式として使う。
 *  （`#if` の中では関数呼出しは使えないが、オブジェクト形式マクロの展開は使える。）
 */

/*
 *  (A) tick / IPI の内部線との衝突は常に禁止。
 *  P4 では両者が内部線（3 と 7）なので、外部線（16 以上）を選んでいる限り
 *  **構造的に**起こらない——が、「起こらないはずだから検査しない」をやると、
 *  誰かが 16 未満を書いた瞬間に静かに壊れる。検査 (C) と二重に張る。
 */
#if (ESP_SHIM_CLIC_INTR_LINE0 == CLIC_INTNO_MTIMER) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == CLIC_INTNO_MTIMER) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == CLIC_INTNO_MTIMER) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == CLIC_INTNO_MTIMER) \
 || (ESP_SHIM_CLIC_INTR_LINE4 == CLIC_INTNO_MTIMER) \
 || (ESP_SHIM_CLIC_INTR_LINE5 == CLIC_INTNO_MTIMER)
#error "esp_shim_intr_clic: CLIC線が tick(CLIC_INTNO_MTIMER) と衝突している。別の空き線へ移すこと"
#endif

#if (ESP_SHIM_CLIC_INTR_LINE0 == CLIC_INTNO_MSIP) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == CLIC_INTNO_MSIP) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == CLIC_INTNO_MSIP) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == CLIC_INTNO_MSIP) \
 || (ESP_SHIM_CLIC_INTR_LINE4 == CLIC_INTNO_MSIP) \
 || (ESP_SHIM_CLIC_INTR_LINE5 == CLIC_INTNO_MSIP)
#error "esp_shim_intr_clic: CLIC線が IPI(CLIC_INTNO_MSIP) と衝突している。別の空き線へ移すこと"
#endif

/*
 *  (B) コンソール線（`INTNO_SIO`）との衝突は常に禁止。
 *  重なると、シムの `dis_int()`（`esp_intr_disable`/`esp_intr_free`）が
 *  **コンソールの線を止める**。これは「ログが出なくなる」＝診断そのものを失う形で
 *  出るので、実機で最も気づきにくい（BL-H-9 で Xtensa 側に入れたのと同じ検査）。
 */
#if (ESP_SHIM_CLIC_INTR_LINE0 == INTNO_SIO) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == INTNO_SIO) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == INTNO_SIO) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == INTNO_SIO) \
 || (ESP_SHIM_CLIC_INTR_LINE4 == INTNO_SIO) \
 || (ESP_SHIM_CLIC_INTR_LINE5 == INTNO_SIO)
#error "esp_shim_intr_clic: CLIC線が コンソール(INTNO_SIO) と衝突している。別の空き線へ移すこと"
#endif

/*
 *  (C) 有効範囲と「外部線であること」。
 *  下限は `CLIC_EXT_OFFSET`（16）——**割込みマトリクスから駆動できるのは外部線だけ**
 *  なので、内部線（0..15）を選ぶと「配線したのに永久に来ない」割込みができる。
 *  上限は `CLIC_TNUM_INTNO - 1`（47）。範囲外は `CLIC_INT_CTRL()` の
 *  範囲外 MMIO 書込みになる（chip 層が 2026 に同型の off-by-one を実際に踏んでいる）。
 */
#if (ESP_SHIM_CLIC_INTR_LINE0 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE0 > (CLIC_TNUM_INTNO - 1)) \
 || (ESP_SHIM_CLIC_INTR_LINE1 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE1 > (CLIC_TNUM_INTNO - 1)) \
 || (ESP_SHIM_CLIC_INTR_LINE2 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE2 > (CLIC_TNUM_INTNO - 1)) \
 || (ESP_SHIM_CLIC_INTR_LINE3 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE3 > (CLIC_TNUM_INTNO - 1)) \
 || (ESP_SHIM_CLIC_INTR_LINE4 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE4 > (CLIC_TNUM_INTNO - 1)) \
 || (ESP_SHIM_CLIC_INTR_LINE5 < CLIC_EXT_OFFSET) || (ESP_SHIM_CLIC_INTR_LINE5 > (CLIC_TNUM_INTNO - 1))
#error "esp_shim_intr_clic: CLIC線が外部線の範囲(CLIC_EXT_OFFSET .. CLIC_TNUM_INTNO-1)の外にある"
#endif

/*
 *  (D) スロット同士の重複禁止。
 *  cfg 側の `intno N is duplicated` でも捕まるが、`.h` を編集した段階で落ちる方が早い。
 */
#if (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_LINE1) \
 || (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_LINE2) \
 || (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_LINE3) \
 || (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_LINE4) \
 || (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_LINE5) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == ESP_SHIM_CLIC_INTR_LINE2) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == ESP_SHIM_CLIC_INTR_LINE3) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == ESP_SHIM_CLIC_INTR_LINE4) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == ESP_SHIM_CLIC_INTR_LINE5) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == ESP_SHIM_CLIC_INTR_LINE3) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == ESP_SHIM_CLIC_INTR_LINE4) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == ESP_SHIM_CLIC_INTR_LINE5) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == ESP_SHIM_CLIC_INTR_LINE4) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == ESP_SHIM_CLIC_INTR_LINE5) \
 || (ESP_SHIM_CLIC_INTR_LINE4 == ESP_SHIM_CLIC_INTR_LINE5)
#error "esp_shim_intr_clic: スロット同士で CLIC線が重複している"
#endif

/*
 *  (E) 「線 17 は使わない」を機械で保証する（AC X-3）。
 *  17 は seam では `INTNO1`、方式(a) では IDF crosscore である（冒頭の決着参照）。
 *  `INTNO1` は `target_test.h` にあり、テスト以外のビルドでは見えないので
 *  **値を直接見る**（見えていないマクロと比べて素通りする事故を避けるため）。
 */
#define ESP_SHIM_CLIC_INTR_FORBIDDEN_17	17
#if (ESP_SHIM_CLIC_INTR_LINE0 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17) \
 || (ESP_SHIM_CLIC_INTR_LINE1 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17) \
 || (ESP_SHIM_CLIC_INTR_LINE2 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17) \
 || (ESP_SHIM_CLIC_INTR_LINE3 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17) \
 || (ESP_SHIM_CLIC_INTR_LINE4 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17) \
 || (ESP_SHIM_CLIC_INTR_LINE5 == ESP_SHIM_CLIC_INTR_FORBIDDEN_17)
#error "esp_shim_intr_clic: CLIC線17 は使えない（seam=INTNO1 / 方式(a)=IDF crosscore。ヘッダ冒頭の決着を読むこと）"
#endif

/*
 *  (F) スロット数と線の定義本数が食い違っていないこと。
 *  NSLOT を上げて LINE を足し忘れる（あるいはその逆）を捕まえる。
 *  本ヘッダは LINE0..5 を無条件に定義するので、NSLOT はちょうど 6 でなければならない。
 *  増やすときは冒頭の利用者表に行を足してから、ここも一緒に直す。
 */
#if (ESP_SHIM_CLIC_INTR_NSLOT != 6)
#error "esp_shim_intr_clic: NSLOT と LINE0..LINE5 の本数が食い違っている（表と一緒に直すこと）"
#endif

#endif /* ESP_SHIM_INTR_CLIC_LINES_H */
