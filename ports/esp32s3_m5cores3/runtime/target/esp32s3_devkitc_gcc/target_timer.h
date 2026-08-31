/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2007,2011,2015 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)～(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 */
#ifndef TOPPERS_TARGET_TIMER_H
#define TOPPERS_TARGET_TIMER_H

#include <sil.h>

/*
 * 高分解能タイマ（Xtensaコア内蔵CCOUNT/CCOMPARE0）
 *
 * ESP32-S3はSoCペリフェラルのSYSTIMERではなく、Xtensaコア内蔵の
 * CCOUNT（フリーランカウンタ）とCCOMPARE0（比較レジスタ、一致で
 * 割込み発生）を使う。VECBASEは変更せず、ROM提供のXTOS API
 * （_xtos_set_interrupt_handler/_xtos_ints_on）で割込みハンドラを
 * 登録する。
 *
 * 一次情報：
 *  - CCOMPARE0の割込み番号・レベル：
 *    esp-idf/components/xtensa/esp32s3/include/xtensa/config/core-isa.h
 *    468行 #define XCHAL_TIMER0_INTERRUPT 6 （CCOMPARE0）
 *    389行 #define XCHAL_INT6_LEVEL 1        （XCHAL_EXCM_LEVEL=3以下、RTOS tick用に適格）
 *  - XTOS API（ROM常駐、VECBASE非変更で割込みハンドラを登録できる）：
 *    ROMアドレス：esp32s3.rom.ld（_xtos_set_interrupt_handler=0x40001c20、
 *    _xtos_set_exception_handler=0x40001c14）
 *    宣言：ESP-IDF components/xtensa/include/xtensa/xtruntime.h
 *    （本ファイルではESP-IDFヘッダに依存せず、同じ関数シグネチャを
 *    独自に宣言し、リンカスクリプトでROM固定アドレスに結び付ける）
 *
 *  【重要・実機/QEMU実測で判明した必須の追加登録】
 *  ROMの`_UserExceptionVector`はEXCCAUSEでインデックスした関数ポインタ
 *  テーブル（QEMU実測：ベースアドレス0x4000016c）経由でディスパッチする。
 *  このテーブルの EXCCAUSE_LEVEL1_INTERRUPT(=4) スロットは**ROM起動直後は
 *  NULL**（QEMUのメモリダンプで実測確認済み、2026-07-06）。すなわち
 *  `_xtos_set_interrupt_handler()`で割込み番号ごとのハンドラを登録して
 *  `_xtos_ints_on()`で有効化しただけでは、Level1割込み発生時にROMの
 *  ディスパッチがそもそも起動せず（NULLへjxしてクラッシュする）、
 *  ハンドラは一切呼ばれない。
 *  解決策：`_xtos_set_exception_handler(EXCCAUSE_LEVEL1_INTERRUPT,
 *  _xtos_l1int_handler)` を**先に**呼び、ROM常駐のXTOS汎用Level1割込み
 *  ディスパッチャ自体をEXCCAUSEテーブルへ登録する必要がある
 *  （`_xtos_l1int_handler`はROM常駐、ESP-IDFのld scriptには公式シンボル
 *  定義が無いため、QEMUのROM ELFをnmで実測したアドレスを直接使う）。
 *  この2段階登録の存在は、TOPPERS/ATK2-SC1のXtensa(xcc)移植コード
 *  （atk2-sc1/arch/xtensa_xcc/prc_support.S、ユーザ提供の参考実装）で
 *  「Level1割込みは_UserExceptionVector→xtos例外ハンドラ経由で
 *  _xtos_l1int_handlerへ」という記述から着想を得て確認した。
 */
#define XT_TIMER_INTNUM   6     /* CCOMPARE0の割込み番号（XCHAL_TIMER0_INTERRUPT） */
#define XT_TIMER_INTLEVEL 1     /* 割込みレベル（XCHAL_INT6_LEVEL） */
#define XT_EXCCAUSE_LEVEL1_INTERRUPT 4  /* ESP-IDF corebits.hのEXCCAUSE_LEVEL1_INTERRUPT */

/*
 *  各割込み番号(0〜31)のハードウェア固定レベルによるビットマスク
 *  （ESP32-S3固有core-isa.hの値。ヘッダ依存を避けるため自前定義。
 *  .claude/plans/sparkling-forging-taco.md参照）。
 *  _kernel_l1int_dispatch/_kernel_l3int_dispatchが、rsr.interruptで読んだ
 *  pendingビットを「自レベルのハードウェア割込み番号」だけに絞り込み、
 *  同一の_kernel_inh_handler_tbl[]を共有していても異なるレベルの
 *  ハンドラを誤って呼ばないようにする。
 */
#define XCHAL_INTLEVEL1_MASK  0x000637FFUL
#define XCHAL_INTLEVEL3_MASK  0x28C08800UL

/*
 * 割込みタイミングに指定する最大値
 */
#define HRTCNT_BOUND 0x7FFFFFFFU

#ifndef TOPPERS_MACRO_ONLY

/*
 * XTOS API（ROM常駐。実体はリンカスクリプトでROM固定アドレスに結合）
 */
typedef void (_xtos_handler_func)(void);
typedef _xtos_handler_func *_xtos_handler;

extern _xtos_handler _xtos_set_interrupt_handler(int n, _xtos_handler f);
extern _xtos_handler _xtos_set_exception_handler(int n, _xtos_handler f);
extern unsigned int   _xtos_ints_on(unsigned int mask);
extern void _xtos_l1int_handler(void); /* ROM常駐のXTOS汎用Level1割込みディスパッチャ実体 */

/*
 * ROMのEXCCAUSEディスパッチテーブル本体（RAM常駐、リンカスクリプトで
 * ROM RAM上の実体アドレスに結合）。_UserExceptionVectorがEXCCAUSEで
 * インデックスして飛ぶ。Level1割込み(EXCCAUSE=4)スロットへ
 * _xtos_l1int_handlerを直接書き込むために使う（詳細はtarget_timer.c）。
 */
extern _xtos_handler xtos_exc_handler_table[];

/*
 * Level-3割込みエントリ（core_support.S、アセンブリ実装）。
 * ROMの_Level3Vector(0x400001c0)は`xsr.excsave3 a2; jx a2`のみを行い
 * EXCSAVE3レジスタの内容へ間接ジャンプする設計のため、VECBASEを変更せず
 * 起動時にEXCSAVE3へこの関数のアドレスを書き込むだけでLevel-3割込みを
 * フックできる（xtos_exc_handler_table[EXCCAUSE]と同じ発想。
 * .claude/plans/sparkling-forging-taco.md参照）。
 */
extern void _kernel_l3int_entry(void);

/*
 * Level-3割込みの多重化（demux、target_timer.c）。_kernel_l3int_entryから
 * 擬似call4で呼ばれる。
 */
extern void _kernel_l3int_dispatch(void);

/*
 * CCOUNT/CCOMPARE0への読み書き
 */
Inline uint32_t xtensa_get_ccount(void)
{
    uint32_t val;
    Asm("rsr.ccount  %0" : "=a"(val));
    return val;
}

Inline void xtensa_set_ccompare0(uint32_t val)
{
    Asm("wsr.ccompare0  %0 ; esync" :: "a"(val) : "memory");
}

#ifdef TOPPERS_M5_TICK_DIAG
/*
 * ★M5 専用 tick 診断カウンタ（TOPPERS_M5_TICK_DIAG、CMakeLists の
 *   A1_VARIANT=m5 分岐でのみ -D される。golden 5 構成では未定義＝
 *   このブロックは 1 命令も生成されない）。
 *
 * 目的：「tick が二度と来ない」の機序を実機 1 回で確定する。
 *   CCOMPARE0 は**厳密一致**でしか割込みを出さないため、CCOMPARE0 を
 *   「既に過ぎた値」に置いた瞬間、次の一致は CCOUNT が一周するまで
 *   （2^32 サイクル＝160MHz で 26.8 秒）来ない。本カウンタは
 *   「書いた値」と「書いた直後の CCOUNT」を毎回記録し、過去に置いた
 *   回数（MISS）を数える。読み取り専用の観測であり制御は一切変えない。
 *
 * 実体は m5/app/m5_display_smoke/m5_diag.c（アプリ側 TU）に置く。
 * カーネル／arch 側にはシンボル定義を持ち込まない。
 */
#define M5_TDIAG_TICK       0U  /* tick(INT6) が signal_time を呼んだ回数     */
#define M5_TDIAG_SET        1U  /* target_hrt_set_event 呼出し回数            */
#define M5_TDIAG_SET_MISS   2U  /* うち「書いた直後に既に過ぎていた」回数     */
#define M5_TDIAG_RESCUE     3U  /* set_event 内の救済 raise_event 発動回数     */
#define M5_TDIAG_RAISE      4U  /* target_hrt_raise_event 総呼出し回数         */
#define M5_TDIAG_RAISE_MISS 5U  /* うち取りこぼした（＝ほぼ常に）回数          */
#define M5_TDIAG_CLEAR      6U  /* target_hrt_clear_event 呼出し回数           */
#define M5_TDIAG_LAST_HRT   7U  /* 最後に set_event へ渡された hrtcnt(us)     */
#define M5_TDIAG_LAST_CMP   8U  /* 最後に CCOMPARE0 へ書いた値                */
#define M5_TDIAG_LAST_CC    9U  /* その書込み直後の CCOUNT                    */
#define M5_TDIAG_LAST_SITE 10U  /* 最後の書込み元 1=set 2=raise 3=clear       */
#define M5_TDIAG_NUM       11U

extern volatile uint32_t _m5_tdiag[M5_TDIAG_NUM];

/*
 * CCOMPARE0 へ書いた直後に「もう過ぎているか」を判定して記録する。
 * (int32_t)(written - ccount) <= 0 なら過去＝この一致は永久に来ない。
 */
Inline void _m5_tdiag_note(uint32_t written, uint32_t site, uint32_t miss_idx)
{
    uint32_t now = xtensa_get_ccount();

    _m5_tdiag[M5_TDIAG_LAST_CMP]  = written;
    _m5_tdiag[M5_TDIAG_LAST_CC]   = now;
    _m5_tdiag[M5_TDIAG_LAST_SITE] = site;
    if ((int32_t)(written - now) <= 0) {
        _m5_tdiag[miss_idx]++;
    }
}
#endif /* TOPPERS_M5_TICK_DIAG */

/*
 * 自コアインデックス（0/1）の読出し（PRIDレジスタのbit13）。
 *
 * 本来はchip_kernel_impl.hのget_my_prcidx()と全く同じ処理だが、
 * target_timer.hは<kernel.h>のみをインクルードした翻訳単位（例：
 * esp/shim/esp_shim.c）からも直接インクルードされ、そこでは
 * kernel_impl.h経由でしか見えないget_my_prcidx()は参照できない。
 * diag_recorder.c（diag_get_core）と同じ理由・同じ手法でここに複製し、
 * target_timer.hをkernel_impl.hに依存させないようにする。
 */
Inline uint_t xtensa_get_prcidx(void)
{
    uint32_t id;
    Asm("rsr.prid %0 \n\t"
        "extui %0, %0, 13, 1" : "=a"(id));
    return (uint_t) id;
}

/*
 * 高分解能タイマの起動処理
 */
extern void target_hrt_initialize(intptr_t exinf);

/*
 * 高分解能タイマの停止処理
 */
extern void target_hrt_terminate(intptr_t exinf);

/*
 * ★CPUクロック周波数の単一の正本（TOPPERS_S3_CPU_FREQ_MHZ）
 *
 * ■ このマクロが意味するもの
 *   「FMP3のカーネルtickが走り出した後、定常状態で実際にCPUが回っている
 *   周波数(MHz)」。CCOUNTはCPUクロックで1サイクル1カウント進むため、
 *   この値がそのまま「1usあたりのCCOUNTサイクル数」＝TCYC_PER_HRT であり、
 *   同時に sil_dly_nse（arch/xtensa_gcc/common/core_kernel_impl.c）の
 *   cycles_per_usec でもある（同ファイルが本ヘッダのTCYC_PER_HRTを参照する）。
 *   ★**本マクロが単一の正本であり、他は全てここから導出する。**
 *
 * ■ 起動経路ごとに「正しい値」が違う（★これが単純な定数変更で済まない理由）
 *   起動経路はビルド時マクロで一意に判別できるため、コンパイル時に分岐する
 *   （実行時取得は不要。TCYC_PER_HRTは除算・乗算にインライン展開される定数で
 *   あり、変数化はABI・全参照箇所に波及するため採らない）。
 *
 *   | 経路              | 判別マクロ                    | ROM/bootloaderが渡す状態      | 本ポートの扱い |
 *   |-------------------|-------------------------------|-------------------------------|----------------|
 *   | QEMU              | TOPPERS_QEMU                  | CPU=XTAL 40MHz（CCOUNTも      | 40（据置き）   |
 *   |                   |                               | ~40MHz固定＝QEMU仮想タイマ）  |                |
 *   | Direct Boot(実機) | (どちらも未定義)              | ROM 1st-stageがCPU=XTAL 40MHz | 40（据置き）   |
 *   | seam              | TOPPERS_BOOT_SEAM               | IDF 2nd-stage bootloaderが    | ★昇圧(既定160)|
 *   |                   |                               | CPU=80MHz(PLL 480M/6)         |                |
 *   | WiFi/BT(XIP等)    | TOPPERS_ESP_WIFI_SYSCLK_PLL   | 経路による（下記）            | ★昇圧(既定160)|
 *
 *   - **QEMU / Direct Boot は 40MHz のまま**（昇圧しない）。Direct BootはROMが
 *     CPU=XTAL=40MHzで渡す。QEMUのCCOUNTは
 *     仮想タイマ由来で~40MHz固定であり、そもそも実PLLを持たない。
 *     ★Direct Bootは退役方向だが、**QEMU回帰が同じ40MHzの前提を
 *       共有している**ため、40MHz分岐は「Direct Bootのため」ではなく
 *       「QEMUのため」に残す必要がある。よって40/240の両立は必須であり、
 *       「seam専用に80固定」や「一律240」という単純化は採れない。
 *   - **seam は昇圧する**（target_kernel_impl.c の esp32s3_cpu_clock_boost_pll()
 *     が software_init_hook＝tick開始前に一度だけ実行）。IDF 2nd-stage
 *     bootloaderが CPU_CLK_FREQ_MHZ_BTLD=80 を適用して80MHzで渡してくるが
 *     （esp-idf/components/soc/esp32s3/include/soc/soc.h:154 →
 *      bootloader_clock_init.c:40）、FMP3は esp_clk_init() を呼ばない。
 *     ⇒ **昇圧しなければ 80MHz のまま走り、40MHz前提の時間定数と2倍ずれる。**
 *
 * ■ 許容値と選択方法（★クロックは「振れる」ことに価値がある）
 *   40（XTAL直結。QEMU/Direct Boot専用）または 80/160/240（PLL 480M/6, /3, /2）。
 *   ビルド時に明示上書きできる。★ただし CMake フロー（classic 全廃後の唯一の正準）には
 *   現状 CPU クロックの外部 -D 注入口が無い（classic 時代の `EXTRA_OFLAGS=` 汎用注入は
 *   全廃済み。first-class な preset／`-DA1_...` キャッシュ変数化は統合レビュー B-2／P-2 で
 *   方針決定待ち）。当面の上書き手段は本マクロの既定値を直接編集するか、CMakeLists.txt の
 *   `A1_EXTRA_DEFS` に `TOPPERS_S3_CPU_FREQ_MHZ=240` 等を足す（全 .o へ -D 注入される）。
 *   TCYC_PER_HRT も sil_dly_nse の cycles_per_usec も本マクロから導出されるため、
 *   **1箇所変えるだけで時間定数が丸ごと追随する**（＝80/160/240 を振って
 *   「症状がクロックに比例するか」をA/Bで切り分ける実験の道具になる）。
 *   APBはPLL選択時どれでも80MHz固定（UARTボーレート等は不変）。
 *
 * ■ 既定値＝ESP-IDFの既定に合わせる（★240ではない）
 *   ESP-IDF v5.5 の既定CPUクロックは **160MHz** である：
 *     esp-idf/components/esp_system/port/soc/esp32s3/Kconfig.cpu:4
 *       choice ESP_DEFAULT_CPU_FREQ_MHZ
 *           default ESP_DEFAULT_CPU_FREQ_MHZ_160     ← ★これが既定
 *   （選択肢は 80/160/240。40はIDF_ENV_FPGA限定。無印ESP32も同じく既定160＝
 *    esp-idf/components/esp_system/port/soc/esp32/Kconfig.cpu:4）
 *   ★「240MHz＝ESP-IDF標準」は誤解である。240はチップの**最大**動作周波数で
 *     あり（esptoolが表示する "240MHz" はチップの**能力**であって設定値ではない）、
 *     IDFが既定で設定する値は160MHzである。240を使いたい場合は明示的に選ぶこと。
 */
#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#if defined(TOPPERS_BOOT_SEAM) || defined(TOPPERS_ESP_WIFI_SYSCLK_PLL)
#define TOPPERS_S3_CPU_FREQ_MHZ  160	/* ESP-IDF既定(Kconfig.cpu:4)に一致 */
#else
#define TOPPERS_S3_CPU_FREQ_MHZ  40	/* QEMU / Direct Boot: CPU=XTAL 40MHz */
#endif
#endif /* TOPPERS_S3_CPU_FREQ_MHZ */

#if TOPPERS_S3_CPU_FREQ_MHZ != 40 && TOPPERS_S3_CPU_FREQ_MHZ != 80 \
	&& TOPPERS_S3_CPU_FREQ_MHZ != 160 && TOPPERS_S3_CPU_FREQ_MHZ != 240
#error "TOPPERS_S3_CPU_FREQ_MHZ must be 40, 80, 160, or 240"
#endif
#if TOPPERS_S3_CPU_FREQ_MHZ == 40 && defined(TOPPERS_BOOT_SEAM)
#error "seam boot leaves CPU on PLL; 40MHz (XTAL) is not reachable without a PLL->XTAL switch"
#endif

/*
 * CCOUNT（CPUクロック）とHRT単位（1us、TSTEP_HRTCNT=1）の換算係数。
 *
 * CCOUNTはCPUクロックで進むため、1us = TOPPERS_S3_CPU_FREQ_MHZ サイクル。
 * HRTは1us分解能（target_kernel.h TSTEP_HRTCNT=1）なので、get_currentは
 * CCOUNTをこの値で割ってus単位に、set_eventはus単位のhrtcntをこの値倍して
 * CCOMPARE0（サイクル単位）に変換する。
 *
 * ★CCOUNTの一周（2^32サイクル）は 40MHz=107.4s / 80MHz=53.7s / 240MHz=17.9s。
 *   240MHz化でこれが約1/3に短くなるが、以下のとおり既存機構が追随する：
 *   (a) target_hrt_get_current() は生CCOUNTの32bit差分を64bit累積器へ積んで
 *       から割る。ラップ耐性は「2^32サイクルに
 *       最低1回は呼ばれること」に依存し、tick(既定1ms周期)がこれを満たす。
 *       17.9sでも余裕は4桁ある。
 *   (b) target_hrt_set_event() のオーバーフロー・クランプ閾値は
 *       0x7FFFFFFFU / TCYC_PER_HRT と TCYC_PER_HRT から導出されるため
 *       自動追随する（240MHzでは約8.95s。クランプ後もCCOMPARE0が
 *       「過去の値」にならないことは40MHz時と同じ理屈で成立）。
 */
#define TCYC_PER_HRT  ((uint32_t) TOPPERS_S3_CPU_FREQ_MHZ)

/*
 * HRT値の一時的な凍結（テストスイート用途。通常のシステム動作では
 * 使用しない・既定は無効）。
 *
 * XtensaのCCOUNTはフリーランカウンタでハードウェア的に停止する手段が
 * 無い（ARM等の一部ターゲットが持つ「タイマ全体停止ビット」に相当する
 * ものが無い）。FMP3のtime_event.c（update_current_evttim）はHRT絶対値
 * （target_hrt_get_current）を直接参照して時刻を進めるため、TTSP3の
 * ttsp_target_stop_tick相当（「時間を完全に止める」）を実現するには、
 * 割込みマスクだけでは不十分で、HRT参照点そのものを固定する必要がある
 * 。
 * _kernel_hrt_frozenが真の間、target_hrt_get_currentは実際のCCOUNTでは
 * なく_kernel_hrt_frozen_valを返す。
 */
extern volatile bool_t   _kernel_hrt_frozen;
extern volatile HRTCNT   _kernel_hrt_frozen_val;

/*
 * TTSP3のgain_tick向けワンショット完了通知フラグ（定義・詳細コメントは
 * target_timer.c参照）。gain_tick側がenable_int前に立て、
 * _kernel_l1int_dispatchがtick処理直後にこれを見てdisable_intし下ろす。
 */
extern volatile bool_t   _kernel_hrt_gain_tick_pending;

/*
 * target_hrt_get_current()のソフトウェア64bit拡張用のper-core状態。
 *
 * CCOUNTは32bitのフリーランカウンタなので，これをTCYC_PER_HRT（40or80）
 * で割ったHRTCNT（us単位）は，80MHz構成では約2^32/80,000,000≈53.687秒
 * ごとに自然にラップする。ところがtarget_kernel.h（TCYC_HRTCNT未定義）は
 * 「HRTCNTは2^32usのフルサイクルでラップする」という前提でカーネルの
 * update_current_evttim()のラップ補正を無効化しているため，
 * 「割った後の値」が53.687秒ごとにラップすると，補正されないまま
 * current_evttimが巨大なゴミ値へ壊れる。
 *
 * 対策：CCOUNTの生サイクル値を，TCYC_PER_HRTで割る前に32bit符号なし
 * 減算で差分を取り（これはCCOUNT自身の2^32サイクル周期に対して常に
 * 正しくラップ対応する），64bit累積カウンタへ積算してからus単位へ
 * 換算する。返り値（HRTCNT，32bit）はその結果として2^32usのフルサイクル
 * （約71.6分）で自然にラップするようになり，target_kernel.hが
 * TCYC_HRTCNTを未定義のままにしている前提（フルサイクルでのラップ）と
 * 整合する。
 *
 * per-coreで独立させる理由：ESP32-S3のCCOUNTは各コア固有のレジスタで
 * あり，各コアは自分のCCOUNTしか読めない。よって配列を
 * get_my_prcidx()相当のインデックス（xtensa_get_prcidx()）で分離し，
 * 自コアの状態のみを読み書きする（他コアの状態には触れない）。
 *
 * ★**コア間の原点は一致していなければならない。** CPU1のリセット解除は
 * CPU0より大幅に遅く、これは無視できない定数オフセットΔになる。FMP3の
 * グローバルなcurrent_evttimはコア間で一貫した単一時刻源を要求するため、
 * Δを放置するとコア跨ぎのタイムイベント（msta_alm等）が直接壊れる。
 * → 起動時に target_hrt_sync_origin()（target_timer.c）がCPU1の累積器を
 *   CPU0の時間軸へ同期する。両コアは同一CPUクロック駆動でΔはドリフト
 *   しないため、起動時の一度きりの同期で恒久的に十分。
 *
 * 排他制御について：本関数は状態を持つread-modify-writeであるため，
 * 同一コア内での再入（タスクコンテキスト実行中にtick割込みが割り込む，
 * またはプリエンプションで別タスクの呼出しが割り込む）から保護しないと
 * 状態が壊れる。特に「raw=CCOUNT読出し」と「last_ccount参照」の間に
 * 別の呼出しが割り込んでlast_ccountを進めてしまうと，
 * delta = raw - last が32bit符号なし演算でほぼ2^32（＝約+53.687秒相当の
 * サイクル数）の巨大値になり，acc_cyclesが一気に1ラップ分飛ぶ。
 * カーネル内の呼出し元（time_event.c/startup.c）はCPUロック下だが，
 * fch_hrt()（kernel/time_manage.c）はSIL_LOC_INTのみで呼び，さらに
 * WiFi OSAシム（esp/shim/esp_shim.c のOSA時刻取得）は**ロック無し**で
 * 本関数を直接呼ぶため，呼出し側の規約には頼れない。
 * よって関数自身がSIL全割込みロック（rsil 15。ネスト安全）でRMW区間全体を
 * 保護する。区間は10数命令であり割込みレイテンシへの影響は無視できる。
 * 他コアは自分の配列要素しか触らないため，コア間の排他は不要
 * （RMWの単位は常に単一コア内で閉じる）。
 */
extern uint32_t _kernel_hrt_last_ccount[TNUM_PRCID];
extern uint64_t _kernel_hrt_acc_cycles[TNUM_PRCID];

#if TNUM_PRCID >= 2
/*
 * HRT時刻原点のコア間同期（F1）の実測値。
 * 定義・機序・導出は target_timer.c の target_hrt_sync_origin() を参照。
 * 診断・証跡採取専用（カーネルの動作には関与しない）。
 *   _kernel_hrt_sync_rtt   : CPU1が測った往復サイクル数（同期誤差の上限＝rtt/2）
 *   _kernel_hrt_sync_delta : 適用したΔ＝ccount_0 - ccount_1（サイクル）
 *                            ＝CPU1のリセット解除がCPU0より遅れたサイクル数
 *   _kernel_hrt_sync_done  : 同期完了フラグ
 */
extern uint32_t _kernel_hrt_sync_rtt;
extern uint32_t _kernel_hrt_sync_delta;
extern volatile bool_t _kernel_hrt_sync_done;
#endif /* TNUM_PRCID >= 2 */

/*
 * 高分解能タイマの現在のカウント値の読出し（us単位）
 */
Inline HRTCNT target_hrt_get_current(void)
{
    uint_t   prcidx;
    uint32_t raw, delta;
    uint64_t acc;
    SIL_PRE_LOC;

    if (_kernel_hrt_frozen) {
        return _kernel_hrt_frozen_val;
    }

    /*
     * RMW区間全体（CCOUNT読出し〜acc_cycles更新）を全割込みロックで
     * 保護する（上の「排他制御について」参照。CCOUNT読出し自体も区間内で
     * 行わないと，割込まれた場合にdeltaがほぼ2^32の巨大値になる）。
     * 64bit除算（libgcc呼出し）はローカルコピーに対してロック外で行い，
     * ロック区間を最短にする。
     */
    SIL_LOC_INT();
    prcidx = xtensa_get_prcidx();
    raw = xtensa_get_ccount();

    /*
     * 生サイクル値の32bit符号なし減算を先に行ってから累積する
     * （TCYC_PER_HRTで割った後の値でラップ判定すると53.687秒周期の
     * 誤ったラップ検出になる。。
     */
    delta = raw - _kernel_hrt_last_ccount[prcidx];
    _kernel_hrt_last_ccount[prcidx] = raw;
    _kernel_hrt_acc_cycles[prcidx] += (uint64_t) delta;
    acc = _kernel_hrt_acc_cycles[prcidx];
    SIL_UNL_INT();

    return (HRTCNT) (acc / TCYC_PER_HRT);
}

/*
 * 高分解能タイマ割込みの要求
 *
 * XtensaのCCOMPARE一致は「厳密な一致」でのみ発生するため（一度ラップ
 * すると再び一致するまで約2^32カウントかかる）、ARM-M/RP2350のような
 * 「割込みペンディングビットを直接立てる」手段がハードウェアタイマ
 * 割込みには存在しない。CCOMPARE0をCCOUNT+1に設定し直すことで、
 * ほぼ即座に一致・割込み発生させる（Xtensaでの標準的な手法）。
 */
Inline void target_hrt_raise_event(ID prcid)
{
#ifdef TOPPERS_M5_TICK_DIAG
    uint32_t _m5_w = xtensa_get_ccount() + 1U;

    _m5_tdiag[M5_TDIAG_RAISE]++;
    xtensa_set_ccompare0(_m5_w);
    _m5_tdiag_note(_m5_w, 2U, M5_TDIAG_RAISE_MISS);
#else /* TOPPERS_M5_TICK_DIAG */
    xtensa_set_ccompare0(xtensa_get_ccount() + 1U);
#endif /* TOPPERS_M5_TICK_DIAG */
}

/*
 * 高分解能タイマへの割込みタイミングの設定
 *
 * 高分解能タイマを，hrtcntで指定した値カウントアップしたら割込みを発
 * 生させるように設定する．
 */
Inline void target_hrt_set_event(ID prcid, HRTCNT hrtcnt)
{
    /*
     * 現在のカウント値を読み，hrtcnt後に割込みが発生するように設定する．
     *
     * ★取りこぼし判定は必ず実際のCCOUNT（xtensa_get_ccount()、
     * サイクル単位）で行う。target_hrt_get_current()（HRT単位）
     * はHRT凍結中（_kernel_hrt_frozen）は固定値を返すため、これで判定
     * すると凍結中は「実際に経過したか」を検知できず、CCOMPARE0が既に
     * 過去の値になっていても救済されない。CCOMPARE0の一致は
     * 常にハードウェアのCCOUNTと比較されるため、取りこぼし判定も凍結の
     * 有無に関わらず実際のCCOUNTで行う必要がある。
 */
    const uint32_t start_ccount = xtensa_get_ccount();
    uint32_t hrtcnt_cycles;

    /*
     * (1回目): hrtcnt*TCYC_PER_HRT（40倍）の32bit乗算
     * オーバーフロー対策。set_hrt_event（kernel/time_event.c）はタイム
     * イベントが無い場合hrtcnt=HRTCNT_BOUND(0x7FFFFFFF)を渡す。これを
     * 40倍すると0x7FFFFFFF*40=85,899,345,880で32bit(uint32_t)を大きく
     * 超え、ラップアラウンドして「start_ccount-40」相当の**過去の値**に
     * なってしまい、CCOMPARE0が既に一致済み（次に一致するのは約2^32
     * カウント後＝実質発火しない）という重大な不具合があった。
     *
     * (2回目・重要): 1回目の修正でクランプ先を
     * 0xFFFFFFFFU（=-1相当）にしていたが、これも誤りだった。
     * `start_ccount + 0xFFFFFFFFU`は32bit演算で`start_ccount - 1`に
     * ラップし、依然として**過去の値**になってしまう（GDB実測：
     * check_library/timerのgain_tick 2回目呼び出しでカーネルパニック
     * LoadProhibited発生。1回目のtick処理でHRTCNT_BOUND経由の
     * set_hrt_eventがCCOMPARE0を「1サイクル前」に設定してしまい、直後に
     * 誤って即発火→2回目のtick割込みが1回目の出口処理の僅かな窓
     * （INTENABLE復元からintnest--までの間）で重なりネスト割込みとして
     * パニックした）。正しいクランプ先は32bit加算で健全にオーバーフロー
     * しない値（2^31-1程度、約53秒@40MHz）にする必要がある。
     */
    if (hrtcnt > (0x7FFFFFFFU / TCYC_PER_HRT)) {
        hrtcnt_cycles = 0x7FFFFFFFU;
    }
    else {
        hrtcnt_cycles = hrtcnt * TCYC_PER_HRT;
    }

    /* CCOMPARE0はサイクル単位。hrtcnt(us)を40倍してサイクルに換算する */
    xtensa_set_ccompare0(start_ccount + hrtcnt_cycles);

#ifdef TOPPERS_M5_TICK_DIAG
    _m5_tdiag[M5_TDIAG_SET]++;
    _m5_tdiag[M5_TDIAG_LAST_HRT] = (uint32_t) hrtcnt;
    _m5_tdiag_note(start_ccount + hrtcnt_cycles, 1U, M5_TDIAG_SET_MISS);
#endif /* TOPPERS_M5_TICK_DIAG */

    /*
     * 上で現在のカウント値を読んで以降に，hrtcnt以上カウントアップしてい
     * た場合には，割込みを発生させる（CCOMPARE一致を取りこぼした場合の
     * 救済。rp2350_pico2_gcc/target_timer.hの同名関数と同じ考え方）．
     */
    if ((xtensa_get_ccount() - start_ccount) / TCYC_PER_HRT >= hrtcnt) {
#ifdef TOPPERS_M5_TICK_DIAG
        _m5_tdiag[M5_TDIAG_RESCUE]++;
#endif /* TOPPERS_M5_TICK_DIAG */
        target_hrt_raise_event(prcid);
    }
}

/*
 * 高分解能タイマへの割込みタイミングのクリア
 *
 * CCOMPAREは「一致」でのみ発生するため，NVICのペンディングビットの
 * ような明示クリア操作はない。十分先の値を設定し，実質的に「次に
 * 予定されているイベントを無効化」する。
 */
Inline void target_hrt_clear_event(ID prcid)
{
    xtensa_set_ccompare0(xtensa_get_ccount() + HRTCNT_BOUND);
#ifdef TOPPERS_M5_TICK_DIAG
    _m5_tdiag[M5_TDIAG_CLEAR]++;
    _m5_tdiag[M5_TDIAG_LAST_SITE] = 3U;
#endif /* TOPPERS_M5_TICK_DIAG */
}

/*
 * 高分解能タイマ割込みハンドラ
 */
extern void target_hrt_handler_prc1(void);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_TARGET_TIMER_H */
