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
 * 登録する（詳細は非公開作業記録/20260706-arch-bringup/design.md参照）。
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

/*
 * target_hrt_raise_event() が CCOMPARE0 へ置く「ごく近い未来」のマージン
 * （サイクル単位。詳細は target_hrt_raise_event() のコメント。
 *  esp32s3_devkitc_gcc/target_timer.h と同一の定義）。
 *
 *  INIT … 初回に試すマージン。1us 相当（TCYC_PER_HRT サイクル）。
 *  MAX  … リトライで拡大するときの上限。256us 相当（契約＝「即座に発生」を
 *          損なわない範囲で飽和させ、以後は同じマージンで再試行する）。
 */
#define HRT_RAISE_MARGIN_INIT   (TCYC_PER_HRT)
#define HRT_RAISE_MARGIN_MAX    (TCYC_PER_HRT * 256U)

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
 * CCOUNT（CPUクロック）とHRT単位（1us、TSTEP_HRTCNT=1）の換算係数。
 *
 * CCOUNTはCPUクロックで進むため、1us = CPU周波数(MHz) CCOUNTサイクル。
 * HRTは1us分解能（target_kernel.h TSTEP_HRTCNT=1）なので、get_currentは
 * CCOUNTをTCYC_PER_HRTで割ってus単位に、set_eventはus単位のhrtcntを
 * TCYC_PER_HRT倍してCCOMPARE0（サイクル単位）に変換する。これを行わないと
 * HRTが速く進み、アラーム/周期ハンドラが所定時刻より早く発火する
 * （test_sem2等のタイミング依存テストが誤動作）。
 *
 * CPUクロック周波数のビルド時選択（TOPPERS_ESP32_CPU_FREQ_MHZ）。
 *   許容値 40 / 80 / 160 / 240（MHz）、既定は160。
 *
 *   2026-07-17 既定値を 240 → 160 へ変更（ユーザー指示「ディフォルトを
 *     ESP-IDFと一緒にして」）。ESP-IDF v5.5 の無印ESP32の既定CPUクロックは
 *     **160MHz** である：
 *       esp-idf/components/esp_system/port/soc/esp32/Kconfig.cpu:4
 *         choice ESP_DEFAULT_CPU_FREQ_MHZ
 *             default ESP_DEFAULT_CPU_FREQ_MHZ_160    ← これが既定
 *     （選択肢は 80/160/240。40は IDF_ENV_FPGA 限定。ESP32-S3も同じく既定160＝
 *      esp-idf/components/esp_system/port/soc/esp32s3/Kconfig.cpu:4。
 *      よってS3側 TOPPERS_S3_CPU_FREQ_MHZ の既定160と本マクロの既定160が揃う）
 *     「240MHz＝ESP-IDF標準」は誤解である。240はチップの**最大**動作周波数で
 *       あり（esptoolが表示する "240MHz" はチップの**能力**であって設定値では
 *       ない）、IDFが既定で設定する値は160MHzである。
 *     2026-07-14に「240MHz化」というユーザー指示で既定240としていた経緯が
 *       あるが、2026-07-17の「ESP-IDFの既定に合わせる」→「160で」という
 *       ユーザー決定が後から出たため160へ変更した（**2026-07-14の指示を
 *       今回の決定で上書き。ユーザー承認済み**）。240で走らせたい場合は
 *       下記の -D で明示的に選ぶこと。
 *     **240が壊れたから下げたのではない**。実機WROVER-KITで240動作を実測済み
 *       （246.423MHz。非公開作業記録/20260717-seam-cpu-80mhz/evidence-01 §4）。
 *       **単にESP-IDFの既定に揃えただけ**である。
 *     **240へ戻すなら下の #define を 240 にする1行で戻せる**が、戻す前に必ず
 *       非公開作業記録/20260717-seam-cpu-80mhz/README.md の「決定事項」節
 *       （なぜ160なのか・esptoolの"240MHz"は能力文字列であって設定値ではない、
 *        という誤解の説明）を読むこと。**経緯を知らずに戻すと同じ議論が再発する。**
 *   - 40 = PLLを使わずXTAL 40MHz直結のまま（従来動作。PLL立上げに問題が
 *     出たrev/個体のデバッグ用フォールバック。target_kernel_impl.cの
 *     esp32_cpu_clock_init_pll()はこの値のとき何もしない）。
 *   - 80/160/240 = software_init_hook（カーネルtick開始前）でBBPLL
 *     （80/160→320M, 240→480M）を立ち上げ、CPUクロック源をPLLへ切替える
 *     （esp32_cpu_clock_init_pll。classic ESP32はS3と異なりPLL構成が
 *     320/480M）。APBはPLL選択時どれも80MHz固定（S3同様）だが、classic
 *     ESP32ではブート時APB=XTAL 40MHzのため、切替でAPBが40→80MHzへ倍増
 *     する。UART0のボーレート分周（APBクロック由来）が化けないよう
 *     esp32_cpu_clock_init_pll内で×2再設定する（S3はAPBが元々80MHzで
 *     不変のため再設定不要だった点がclassicとの相違）。
 *   使い方：configure.rb -o "-DTOPPERS_ESP32_CPU_FREQ_MHZ=160" 等。
 *
 * TCYC_PER_HRTは選択したCPU周波数(MHz)に一致させる（40なら従来どおり40）。
 * テストは<10sで、CCOUNTは2^32（240MHzでも~17.9s）に達しないため、us値の
 * 非2冪ラップは実害がない（>10s動作時はtarget_timer.hの64bit積算が対応）。
 */
#ifndef TOPPERS_ESP32_CPU_FREQ_MHZ
#define TOPPERS_ESP32_CPU_FREQ_MHZ  160	/* ESP-IDF既定(Kconfig.cpu:4)に一致 */
#endif
#if TOPPERS_ESP32_CPU_FREQ_MHZ != 40 && TOPPERS_ESP32_CPU_FREQ_MHZ != 80 \
	&& TOPPERS_ESP32_CPU_FREQ_MHZ != 160 && TOPPERS_ESP32_CPU_FREQ_MHZ != 240
#error "TOPPERS_ESP32_CPU_FREQ_MHZ must be 40, 80, 160, or 240"
#endif
/*
 * 40（XTALフォールバック）は Direct Boot 専用である。
 *   40のとき esp32_cpu_clock_init_pll() は何もしない＝「ROM/bootloaderが渡した
 *   クロックのまま走る」という意味になる。Direct BootはROMがCPU=XTAL 40MHzで
 *   渡すのでこれは正しいが、**seamでは IDF 2nd-stage bootloader が CPU を
 *   80MHz にして渡す**ため（esp-idf/components/soc/esp32/include/soc/soc.h:158
 *   CPU_CLK_FREQ_MHZ_BTLD=80）、40のままだと**実CPU 80MHz に対し
 *   TCYC_PER_HRT=40 ＝ 2倍ずれたまま黙って走る**（これはS3 seamで実際に起きて
 *   いた欠陥そのもの＝非公開作業記録/20260717-seam-cpu-80mhz/）。
 *   同じ罠を踏まないようコンパイル時に弾く。seamで低クロックにしたい場合は
 *   80 を選ぶこと（PLL経由で明示的に80MHzへ設定される）。
 */
#if TOPPERS_ESP32_CPU_FREQ_MHZ == 40 && defined(TOPPERS_BOOT_SEAM)
#error "TOPPERS_ESP32_CPU_FREQ_MHZ=40 (XTAL fallback) is Direct-Boot only; seam boot is left at 80MHz by the IDF bootloader, so TCYC_PER_HRT=40 would be 2x off. Use 80 instead."
#endif
/* 1usあたりのCCOUNTサイクル数 = CPU周波数(MHz) */
#define TCYC_PER_HRT  ((uint32_t) TOPPERS_ESP32_CPU_FREQ_MHZ)

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
 * （2026-07-07、TTSP3 check_library/timer移植で判明）。
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
 * [追加] 2026-07-09（JTAG_DEBUG.md 追記51/52の真因修正）：
 * target_hrt_get_current()のソフトウェア64bit拡張用のper-core状態。
 *
 * CCOUNTは32bitのフリーランカウンタなので，これをTCYC_PER_HRT（40or80）
 * で割ったHRTCNT（us単位）は，80MHz構成では約2^32/80,000,000≈53.687秒
 * ごとに自然にラップする。ところがtarget_kernel.h（TCYC_HRTCNT未定義）は
 * 「HRTCNTは2^32usのフルサイクルでラップする」という前提でカーネルの
 * update_current_evttim()のラップ補正を無効化しているため，
 * 「割った後の値」が53.687秒ごとにラップすると，補正されないまま
 * current_evttimが巨大なゴミ値へ壊れていた（実機で53〜56秒ごとに
 * タイマ駆動タスクが永眠するフリーズとして観測。追記51で機序を確定）。
 *
 * 対策：CCOUNTの生サイクル値を，TCYC_PER_HRTで割る前に32bit符号なし
 * 減算で差分を取り（これはCCOUNT自身の2^32サイクル周期に対して常に
 * 正しくラップ対応する），64bit累積カウンタへ積算してからus単位へ
 * 換算する。返り値（HRTCNT，32bit）はその結果として2^32usのフルサイクル
 * （約71.6分）で自然にラップするようになり，target_kernel.hが
 * TCYC_HRTCNTを未定義のままにしている前提（フルサイクルでのラップ）と
 * 整合する。
 *
 * per-coreで独立させる理由：CCOUNTは各コア固有のレジスタであり，
 * 各コアは自分のCCOUNTしか読めない。よって配列を
 * get_my_prcidx()相当のインデックス（xtensa_get_prcidx()）で分離し，
 * 自コアの状態のみを読み書きする（他コアの状態には触れない）。
 *
 * 2026-07-17訂正（重要）：旧コメントはここに「コア間で同期していない
 * （起動タイミングの違いで**数%程度の**オフセットが乗る）」と書き，
 * これを**良性と誤判定**していた。実際にはCPU1（APP_CPU）のリセット解除は
 * CPU0より大幅に遅く（LX6 seam起動のJTAG実測でΔ=14,041,078サイクル
 * ＝58,504 HRT-us，Direct Bootでも1,550,366サイクル＝6,459 HRT-us），
 * これは「数%の相対誤差」ではなく**無視できない定数オフセットΔ**である。
 * FMP3のグローバルなcurrent_evttimはコア間で一貫した単一時刻源を要求する
 * ため（kernel/time_event.c:127,132・389-399），このΔはコア跨ぎの
 * タイムイベント（msta_alm等）を直接壊す。
 * → 現在は起動時に target_hrt_sync_origin()（target_timer.c）がCPU1の
 *   累積器をCPU0の時間軸へ同期するため，**両コアの原点は一致している**。
 *   両コアは同一CPUクロック駆動でΔはドリフトしないため，起動時の一度きりの
 *   同期で恒久的に十分。
 * この欠陥はLX6実機で作動していることが実測済みだが，test_malarm1 は
 *   閾値に対し余裕約17%で**PASSしてしまう**（偽陰性）。テストのPASSを
 *   健全性の根拠にしてはならない。
 * 詳細：非公開作業記録/20260717-malarm1-alarm-skew/evidence-04-lx6-same-defect-measured.txt
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
 * 本関数を直接呼ぶため，呼出し側の規約には頼れない。実際，実機検証の
 * trial3でこの競合により「LOGTASK/MAIN_TASKのタイムイベントだけが
 * 約53秒ずれて発火する」異常を観測した（JTAG_DEBUG.md追記52）。
 * よって関数自身がSIL全割込みロック（rsil 15。ネスト安全）でRMW区間全体を
 * 保護する。区間は10数命令であり割込みレイテンシへの影響は無視できる。
 * 他コアは自分の配列要素しか触らないため，コア間の排他は不要
 * （RMWの単位は常に単一コア内で閉じる）。
 */
extern uint32_t _kernel_hrt_last_ccount[TNUM_PRCID];
extern uint64_t _kernel_hrt_acc_cycles[TNUM_PRCID];

#if TNUM_PRCID >= 2
/*
 * [追加] 2026-07-17: HRT時刻原点のコア間同期（S3のF1＝コミット e2cb08e の
 * 移植）の実測値。
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
     * 誤ったラップ検出になる。JTAG_DEBUG.md追記51参照）。
     */
    delta = raw - _kernel_hrt_last_ccount[prcidx];
    _kernel_hrt_last_ccount[prcidx] = raw;
    _kernel_hrt_acc_cycles[prcidx] += (uint64_t) delta;
    acc = _kernel_hrt_acc_cycles[prcidx];
    SIL_UNL_INT();

    return (HRTCNT) (acc / TCYC_PER_HRT);
}

/*
 * 高分解能タイマの現在のカウント値の読出し（us単位・64bit）
 *
 * [追加] 2026-07-27（レビュー指摘 esp-1。S3版 target_timer.h と同一）：
 * target_hrt_get_current() は FMP3 カーネルの HRTCNT 契約（32bit・
 * 2^32us≈71.6分でラップ）に従うため，戻り値を 32bit へ切り詰めている。
 * ところが ESP-IDF の OS 抽象化シム（esp/shim/esp_shim.c の
 * esp_shim_time_us()＝osi_funcs の esp_timer_get_time 相当）は
 * **単調増加する 64bit マイクロ秒**を要求する契約であり，そこへ
 * 32bit 値を int64 へゼロ拡張して渡すと 71.6 分ごとに時刻が逆行する
 * （one-shot タイマが最大 71 分遅延する／lwIP の sys_now() が巻き戻る）。
 *
 * 累積器 _kernel_hrt_acc_cycles[] は元々 64bit なので，割った結果を
 * 切り詰めずに返すだけで 64bit 連続の時刻源になる。
 *
 * 実装は target_hrt_get_current() と**同一の RMW**（同じ
 *   _kernel_hrt_last_ccount[]/_kernel_hrt_acc_cycles[] を同じ手順で
 *   更新する）である。したがって 32bit 版と 64bit 版のどちらを呼んでも
 *   累積器は正しく進み，混在して呼んでも整合する。上の
 *   target_hrt_get_current() のコメント（ラップ処理・排他制御・
 *   コア間同期）はすべて本関数にもそのまま当てはまる。
 *
 * HRT凍結中（_kernel_hrt_frozen）の制限：32bit 版と同様に
 *   _kernel_hrt_frozen_val（32bit）をそのまま返すため，**凍結を跨ぐと
 *   64bit の連続性は崩れる**（凍結はテストスイート／デバッグ専用の
 *   機構であり，Wi-Fi/BT シムが動く通常構成では使用しない）。
 */
Inline int64_t target_hrt_get_current64(void)
{
    uint_t   prcidx;
    uint32_t raw, delta;
    uint64_t acc;
    SIL_PRE_LOC;

    if (_kernel_hrt_frozen) {
        return (int64_t) _kernel_hrt_frozen_val;
    }

    /*
     * RMW区間全体（CCOUNT読出し〜acc_cycles更新）を全割込みロックで
     * 保護する（target_hrt_get_current() と同一）。
     */
    SIL_LOC_INT();
    prcidx = xtensa_get_prcidx();
    raw = xtensa_get_ccount();

    /*
     * 生サイクル値の32bit符号なし減算を先に行ってから累積する
     * （target_hrt_get_current() と同一）。
     */
    delta = raw - _kernel_hrt_last_ccount[prcidx];
    _kernel_hrt_last_ccount[prcidx] = raw;
    _kernel_hrt_acc_cycles[prcidx] += (uint64_t) delta;
    acc = _kernel_hrt_acc_cycles[prcidx];
    SIL_UNL_INT();

    /* 32bit版と違い，ここで HRTCNT へ切り詰めない */
    return (int64_t) (acc / TCYC_PER_HRT);
}

/*
 * 高分解能タイマ割込みの要求
 *
 * esp32s3_devkitc_gcc/target_timer.h の同名関数と**完全に同一**の実装。
 *   契約・旧欠陥の機序・案の比較の詳細はそちらのコメント、および
 *   非公開作業記録/20260728-hrt-raise-event-fix/design.md を参照。
 *
 * ■ 契約：prcid のタイマ割込みを（呼出し後ほどなく）**確実に**発生させる。
 *   呼出し元 set_hrt_event()（kernel/time_event.c）は「発生時刻を既に過ぎた
 *   タイムイベント」の処理をこの関数だけに託しており、取りこぼしたときの
 *   救済経路は存在しない（後続の登録は index == ROOT_INDEX でゲートされる）。
 *
 * ■ 旧実装 `xtensa_set_ccompare0(xtensa_get_ccount() + 1U)` は
 *   **構造的に必ず取りこぼす**（Xtensa の CCOMPARE は厳密一致でのみ発火し、
 *   rsr→wsr の間に CCOUNT が N+2 以上へ進むため N+1 は書いた瞬間に過去）。
 *   結果、CCOUNT が一周する 2^32 サイクル（LX6 の既定 160MHz で 26.8 秒、
 *   80MHz で 53.7 秒）タイムイベントが全停止していた。
 *
 * ■ 新実装：マージン付きの未来値を書き、**書いた結果を読み直して
 *   まだ未来であることを確認できるまで**マージンを 4 倍しつつ再試行する。
 *   成功が確認できるまで return しない。
 *
 * ■ prcid の前提（2026-07-29 明記。riscv_gcc/common/mtimer.h:144 と同水準）
 *   本ポートは **TOPPERS_SUPPORT_CONTROL_OTHER_HRT を定義していない**ため、
 *   prcid は**必ず自プロセッサ**である。⇒ 本関数は引数 prcid を使わず、
 *   常に**自 PE の** CCOMPARE0（rsr/wsr は自コアのレジスタ）を触る。
 *   他 PE 分の要求はカーネルが IPI へ迂回させるので、ここへは来ない。
 *   この前提が崩れる変更をした瞬間、本関数は**黙って別 PE のタイマを
 *     設定し損ねる**。前提を変えるなら必ず本関数も直すこと。
 */
Inline void target_hrt_raise_event(ID prcid)
{
    uint32_t margin = HRT_RAISE_MARGIN_INIT;
    uint32_t w, now;

    for (;;) {
        w = xtensa_get_ccount() + margin;
        xtensa_set_ccompare0(w);
        now = xtensa_get_ccount();
        if ((int32_t)(w - now) > 0) {
            /* まだ未来＝CCOUNT は必ず w を通過する＝一致は確実に起きる */
            break;
        }
        if (margin < HRT_RAISE_MARGIN_MAX) {
            margin <<= 2;
        }
    }
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
     * [改変] 2026-07-07: 取りこぼし判定は必ず実際のCCOUNT（xtensa_get_
     * ccount()、サイクル単位）で行う。target_hrt_get_current()（HRT単位）
     * はHRT凍結中（_kernel_hrt_frozen）は固定値を返すため、これで判定
     * すると凍結中は「実際に経過したか」を検知できず、CCOMPARE0が既に
     * 過去の値になっていても救済されない不具合があった（TTSP3のAPIテスト
     * でmsta_alm等が凍結中に呼ばれた際、CCOMPARE0が取りこぼされたまま
     * 放置されアラームが二度と発火しなくなっていた）。CCOMPARE0の一致は
     * 常にハードウェアのCCOUNTと比較されるため、取りこぼし判定も凍結の
     * 有無に関わらず実際のCCOUNTで行う必要がある。
     */
    const uint32_t start_ccount = xtensa_get_ccount();
    uint32_t hrtcnt_cycles;

    /*
     * [改変] 2026-07-07 (1回目): hrtcnt*TCYC_PER_HRT（40倍）の32bit乗算
     * オーバーフロー対策。set_hrt_event（kernel/time_event.c）はタイム
     * イベントが無い場合hrtcnt=HRTCNT_BOUND(0x7FFFFFFF)を渡す。これを
     * 40倍すると0x7FFFFFFF*40=85,899,345,880で32bit(uint32_t)を大きく
     * 超え、ラップアラウンドして「start_ccount-40」相当の**過去の値**に
     * なってしまい、CCOMPARE0が既に一致済み（次に一致するのは約2^32
     * カウント後＝実質発火しない）という重大な不具合があった。
     *
     * [改変] 2026-07-07 (2回目・重要): 1回目の修正でクランプ先を
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

    /*
     * 上で現在のカウント値を読んで以降に，hrtcnt以上カウントアップしてい
     * た場合には，割込みを発生させる（CCOMPARE一致を取りこぼした場合の
     * 救済。rp2350_pico2_gcc/target_timer.hの同名関数と同じ考え方）．
     */
    if ((xtensa_get_ccount() - start_ccount) / TCYC_PER_HRT >= hrtcnt) {
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
}

/*
 * 高分解能タイマ割込みハンドラ
 */
extern void target_hrt_handler_prc1(void);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_TARGET_TIMER_H */
