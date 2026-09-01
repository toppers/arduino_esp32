/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2016 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
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

/*
 * タイマドライバ（ESP32-S3用、Xtensaコア内蔵CCOUNT/CCOMPARE0）
 *
 * 本作業単位は単一コアのみを対象とする（SMP/cross-core interruptは
 * 別作業単位）。rp2350_pico2_gcc/target_timer.cのPRC1/PRC2分岐は削除。
 */

#include "kernel_impl.h"
#include "time_event.h"
#include "target_timer.h"
#include "chip_ipi.h"
#include "diag_recorder.h"
#include <sil.h>

/*
 * HRT値の一時的な凍結（宣言はtarget_timer.h参照）。既定は無効（false/0）。
 */
volatile bool_t   _kernel_hrt_frozen = false;
volatile HRTCNT   _kernel_hrt_frozen_val = 0U;

/*
 * [追加] 2026-07-07: TTSP3のgain_tick（凍結中に1 tick分だけ強制的に
 * tick割込みを発生させ、また凍結し直す機構）向けのワンショット完了通知
 * フラグ（宣言はtarget_timer.h参照）。
 *
 * 当初gain_tick側は「enable_intしてCCOMPARE0の値が変化するまでポーリング
 * し、変化を検知したらdisable_int」という設計だったが、QEMU上ではCCOUNTの
 * 進み方が命令数に対して線形でなく（TCGブロック境界等で数百サイクル単位の
 * 飛びが起こり得る、GDB実測でxtensa_set_ccompare0からenable_int完了までの
 * 数命令の間に239サイクルもの経過を確認）、「CCOMPARE0=実CCOUNT+1」という
 * タイトな設定・小さいマージンでは容易に取りこぼされ、逆にマージンを
 * 増やすと1回のgain_tick呼び出し中に2回目の一致が生じてしまい
 * _kernel_l1int_entryのネスト検出パニックを引き起こす、という板挟みになり
 * ポーリング方式では確実に「ちょうど1回だけ」発火させることができなかった。
 *
 * 対策：ポーリングでの間接検出をやめ、tick割込みハンドラ自身
 * （_kernel_l1int_dispatch）に「1回限りモード」の完了通知フックを持たせる。
 * gain_tickはenable_int前にこのフラグを立てておき、_kernel_l1int_dispatch
 * はsignal_time()呼び出し直後にこのフラグを見て、立っていれば即座に
 * disable_intしてフラグを下ろす。gain_tick側はこのフラグが下りるのを
 * 待つだけでよく、タスクコンテキストでのポーリングタイミングに依存せず
 * 「ハンドラ実行後、確実に1回でマスクされる」ことが保証される。
 */
volatile bool_t   _kernel_hrt_gain_tick_pending = false;

/*
 * [追加] 2026-07-09: target_hrt_get_current()のソフトウェア64bit拡張用
 * per-core状態（宣言・詳細コメントはtarget_timer.h参照。JTAG_DEBUG.md
 * 追記51/52）。
 *
 * 2026-07-17改訂（Bug A の修正＝F1のLX6への移植）：
 *   旧コメントはここに「BSS上でゼロ初期化されるため，明示的な初期化コードは
 *   不要（…従来どおり『コアリセットからの経過』を表す値になる）」と書いて
 *   いたが、これは**設計上の欠陥**だった。「コアリセットからの経過」は
 *   **コアごとに時刻原点が違う**ことを意味する。無印ESP32(LX6)のCPU1
 *   （APP_CPU）もS3と同様にチップリセットから大幅に遅れて解除されるため、
 *   CPU0とCPU1のCCOUNTには**定数オフセットΔ**が乗る。
 *
 *   一方FMP3カーネルは current_evttim / current_hrtcnt を**全プロセッサ
 *   共有のグローバルスカラ**として持ち（kernel/time_event.c:127,132）、
 *   update_current_evttim() が
 *       hrtcnt_advance = target_hrt_get_current() - current_hrtcnt
 *   というグローバル差分演算を行う（kernel/time_event.c:389-399）。すなわち
 *   target_hrt_get_current() は「**どのコアで呼んでも同じ絶対時刻を返す
 *   単一の時刻源**」であることを要求する（他ポートはいずれもコア間共有の
 *   ハードタイマを使う：arch/arm_gcc/common/mpcore_timer.h=MPCore Global
 *   Timer、arch/riscv_gcc/common/mtimer.h=mtime）。per-coreの原点はこの
 *   契約に違反しており、コア跨ぎのタイムイベント（msta_alm等）で
 *   current_evttim が ±Δ 逆行/早送りする。
 *
 *   LX6での実測（JTAG、ESP-WROVER-KIT 84:0d:8e:18:83:7c、修正前）：
 *       B_0 == B_1 == 0（＝両コアの累積器が生CCOUNT＝原点はコア毎のリセット）
 *       Δ = acc[0] - acc[1] = 14,041,078 / 14,248,950 cycles（seam、2ブート）
 *         → Δ_HRT = 58,504 / 59,370 HRT-us
 *       Δ = 1,550,366 cycles（Direct Boot）→ Δ_HRT = 6,459 HRT-us
 *     機序の中核 `no time event is processed in hrt interrupt on PRC2.` が
 *     seam で7回・Direct Boot でも出現＝**逆行が実機で実際に起きていた**。
 *   ただし test_malarm1 は LX6 では PASS していた（判定閾値 200,000 に対し
 *     100,000+58,504=158,504 で余裕約17%）。**テストが通ることは健全性の
 *     根拠にならない**（偽陰性）。S3 は TCYC_PER_HRT が小さくΔ_HRTが2倍に
 *     膨らむため同じ欠陥が FAIL として露見した、という違いに過ぎない。
 *   詳細：非公開作業記録/20260717-malarm1-alarm-skew/evidence-04-lx6-same-defect-measured.txt
 *
 *   対策：起動時に一度だけCPU1の累積器をCPU0（PRC1）の時間軸へ同期する
 *   （target_hrt_sync_origin()。両コアは同一CPUクロックで駆動されΔは
 *   ドリフトしないため、一度きりの同期で恒久的に十分）。同期後は
 *   acc[]/last_ccount[] は従来どおり各コアが自分の要素のみを読み書きする
 *   （ラップ対策・排他制御の資産＝追記51/52 はそのまま活きる）。
 */
uint32_t _kernel_hrt_last_ccount[TNUM_PRCID];
uint64_t _kernel_hrt_acc_cycles[TNUM_PRCID];

#if TNUM_PRCID >= 2

/*
 * [追加] 2026-07-17: HRT時刻原点のコア間同期（Bug A の真因修正）
 *
 * 本関数は target/esp32s3_devkitc_gcc/target_timer.c の
 *   target_hrt_sync_origin()（コミット e2cb08e、S3のF1）の移植である。
 *   両ターゲットの target_timer.c は元々ほぼ同一であり、同期の機序は
 *   CPUクロック定数（TCYC_PER_HRT）にも チップ固有レジスタにも依存しない
 *   ため、**アルゴリズムは1行も変えずに移植できる**（呼出し位置のみLX6の
 *   VECBASE事情に合わせる。target_hrt_initialize() のコメント参照）。
 *   導出の全文・記号の定義は S3 側の同名関数のコメントを正本とする。
 *
 * ■なぜ「起動時一度きり」で十分か
 *   両コアは同一のCPUクロック（同一PLL/XTAL由来）で駆動される。CCOUNTは
 *   そのクロックで1サイクル1カウント進むだけのカウンタなので、両コアの
 *   CCOUNTの差は**リセット解除時刻の差**そのもの＝**定数**であり、周波数差に
 *   由来するドリフトは原理的に生じない。よって一度合わせれば恒久的に一致
 *   し続ける。実際 LX6 の実測でも acc[i]==last_ccount[i] が両コアで成立し、
 *   両者の差が一定であることが確認されている（evidence-04）。
 *
 * ■実施箇所と起動順序の根拠
 *   (1) 両コアは sta_ker（fmp3_trunk/kernel/startup.c）で
 *         … barrier_sync(4) → call_inirtn(自プロセッサのリスト)
 *         → barrier_sync(5) → [TMASTER] current_hrtcnt = target_hrt_get_current()
 *         → barrier_sync(6) → set_hrt_event → start_dispatch
 *       と進む。
 *   (2) 本関数は target_hrt_initialize() から呼ばれる。target_hrt_initialize は
 *       target_timer.cfg の CLASS(CLS_PRC1)/CLASS(CLS_PRC2) 両方に ATT_INI
 *       されており（LX6の target_timer.cfg は S3 と同一内容）、**両コアの
 *       プロセッサ別inirtnリストの先頭**に来る。
 *       → 両コアは barrier_sync(4) を出た直後に、ほぼ何も挟まずに本関数へ
 *         到達する。したがって
 *           ・両コアが確実に生きて到達する（＝ランデブが成立しデッドロック
 *             しない。先行する初期化ルーチンが無いので、片コアが別の
 *             待ちに捕まる経路が存在しない）
 *           ・到達時刻の差が最小＝往復時間 rtt が小さく、同期誤差 rtt/2 が小さい
 *   (3) 順序上の必須要件：同期は「グローバル時刻がHRT値を最初に取り込む
 *       時点」＝(1)の barrier_sync(5) 直後（startup.c:215）より**前**でなければ
 *       ならない。本関数は barrier_sync(4)〜(5) の区間で走るため要件を満たす。
 *       それ以前に current_hrtcnt は0（BSS）であり、ディスパッチもtick割込みも
 *       未開始のため、同期による累積器の書き換えがカーネルのグローバル時刻へ
 *       波及することはない。
 *   (4) 本関数は tick(INT6)のenable_intより前に呼ぶ（呼出し位置参照）。
 *       ハンドシェイク中にtickが割り込む余地を作らない。
 *
 * ■同期の方式（NTP流の往復中点補償）
 *   CPU1はCPU0のCCOUNTを直接読めないため、共有SRAM経由でランデブする。
 *   単純な「CPU0が公開→CPU1が読む」では公開から読取りまでの遅延がそのまま
 *   誤差になるので、CPU1側で往復時間を測り中点を採る：
 *       t1 = CPU1のCCOUNT（要求発行の直前）
 *       CPU0は要求を見て自分の累積器を最新化し acc[0] を公開して応答
 *       t2 = CPU1のCCOUNT（応答を観測した直後）
 *   CPU0の採取時刻はCPU1の時間軸で [t1, t2] の間にあり、中点 (t1+t2)/2 で
 *   近似できる。誤差は往復の非対称分のみで、上限は rtt/2。
 *
 *   同期の式：core i の get_current が返す値を
 *       HRT_i(t) = (acc[i] + (ccount_i(t) - last_ccount[i])) / TCYC_PER_HRT
 *   と書き、バイアス B_i = acc[i] - last_ccount[i] を定義すると
 *       HRT_i(t) = (B_i + ccount_i(t)) / TCYC_PER_HRT
 *   HRT_1 == HRT_0 の必要十分条件は B_1 = B_0 + (ccount_0 - ccount_1) = B_0 + Δ。
 *   CPU0は公開直前に自分の累積器を最新化するので last_ccount[0] == 採取した
 *   ccount_0 となり B_0 = acc0_pub - ccount_0_sample。CPU1が
 *       last_ccount[1] = t2
 *   と置けば
 *       acc[1] = B_1 + last_ccount[1] = B_0 + Δ + t2
 *              = (acc0_pub - ccount_0_sample) + (ccount_0_sample - (t1+t2)/2) + t2
 *              = acc0_pub + (t2 - t1)/2
 *   となる。すなわち **acc[1] = 公開されたacc[0] + rtt/2、last_ccount[1] = t2**。
 *   （B_0 を消去して acc0_pub だけで書けるのがこの式の要点。CPU0の累積器が
 *     ゼロ原点である等の前提を一切置かない。）
 *
 * ■メモリ順序
 *   CPU0: acc0 を書く → memw → ack=1 を書く
 *   CPU1: ack==1 を観測 → memw → acc0 を読む
 *   の release/acquire 対で64bit値のtearingを防ぐ（内蔵SRAMはコア間
 *   コヒーレント。非公開作業記録/20260706-arch-bringup/design.md §0）。
 */
static volatile uint32_t hrt_sync_req;    /* CPU1 → CPU0：採取要求 */
static volatile uint32_t hrt_sync_ack;    /* CPU0 → CPU1：採取完了 */
static volatile uint64_t hrt_sync_acc0;   /* CPU0 → CPU1：採取時点の acc[0] */

/*
 * 同期の実測値（診断・証跡採取用。JTAGのmdwやsyslogで読める）。
 *   _kernel_hrt_sync_rtt   : CPU1が測った往復サイクル数（同期誤差の上限は
 *                            この1/2）
 *   _kernel_hrt_sync_delta : 適用したΔ = ccount_0 - ccount_1（サイクル）。
 *                            ＝CPU1のリセット解除がCPU0より何サイクル
 *                              遅かったか。
 *   _kernel_hrt_sync_done  : 同期完了フラグ
 */
uint32_t _kernel_hrt_sync_rtt;
uint32_t _kernel_hrt_sync_delta;
volatile bool_t _kernel_hrt_sync_done;

static void
target_hrt_sync_origin(void)
{
	SIL_PRE_LOC;

	if (xtensa_get_prcidx() == 0U) {
		/*
		 *  マスタ（PRC1/CPU0）側：CPU1の要求を待ち、自分の累積器を
		 *  最新化して acc[0] を公開する。
		 */
		while (hrt_sync_req == 0U) {
			/* CPU1が本関数へ到達するのを待つ（上記(2)によりランデブは保証） */
		}
		Asm("memw" ::: "memory");

		SIL_LOC_INT();
		{
			uint32_t raw0, delta0;

			/*
			 *  累積器の最新化（target_hrt_get_current()と同じRMW）。
			 *  同関数を呼ばずに書き下すのは、(a)凍結中
			 *  （_kernel_hrt_frozen）は固定値を返し累積器を更新しない
			 *  ため同期に使えない、(b)必要なのはus単位の戻り値ではなく
			 *  64bitのacc値そのもの、という2点による。
			 *  この最新化により last_ccount[0] == raw0 となり、上記の
			 *  導出における B_0 = acc0_pub - ccount_0_sample が成立する。
			 */
			raw0 = xtensa_get_ccount();
			delta0 = raw0 - _kernel_hrt_last_ccount[0];
			_kernel_hrt_last_ccount[0] = raw0;
			_kernel_hrt_acc_cycles[0] += (uint64_t) delta0;
			hrt_sync_acc0 = _kernel_hrt_acc_cycles[0];
		}
		SIL_UNL_INT();

		/* release：acc0 の書き込みを ack より先に可視化する */
		Asm("memw" ::: "memory");
		hrt_sync_ack = 1U;
		Asm("memw" ::: "memory");
	}
	else {
		/*
		 *  スレーブ（PRC2/CPU1）側：往復を測りつつCPU0の acc[0] を取得し、
		 *  自分の累積器をCPU0の時間軸へ載せ替える。
		 */
		uint32_t t1, t2, rtt;
		uint64_t acc0;

		SIL_LOC_INT();

		t1 = xtensa_get_ccount();
		Asm("memw" ::: "memory");
		hrt_sync_req = 1U;
		Asm("memw" ::: "memory");

		while (hrt_sync_ack == 0U) {
			/* CPU0の応答を待つ */
		}
		/* acquire：ack の観測後に acc0 を読む */
		Asm("memw" ::: "memory");
		t2 = xtensa_get_ccount();
		acc0 = hrt_sync_acc0;

		/*
		 *  rtt は32bit符号なし減算でラップ安全。rtt/2 がCPU0の採取時刻から
		 *  t2 までの経過サイクルの推定値。
		 */
		rtt = t2 - t1;

		_kernel_hrt_last_ccount[1] = t2;
		_kernel_hrt_acc_cycles[1] = acc0 + (uint64_t)(rtt / 2U);

		/* 診断用（適用したΔ＝CPU1解除の遅れ。32bit減算でラップ安全） */
		_kernel_hrt_sync_rtt = rtt;
		_kernel_hrt_sync_delta = (uint32_t)(acc0 + (uint64_t)(rtt / 2U)) - t2;
		_kernel_hrt_sync_done = true;

		SIL_UNL_INT();
	}
}

#endif /* TNUM_PRCID >= 2 */

/*
 * タイマの起動処理
 *
 * VECBASEを変更せず，ROM提供のXTOS APIでCCOMPARE0割込みのハンドラを
 * 登録し，有効化する。実際の初回イベント設定（CCOMPARE0への書き込み）
 * はカーネル本体（kernel/startup.c、sta_ker）がset_hrt_event経由で
 * 行うため，ここでは登録・有効化のみ行う（rp2350_pico2_gcc実装と
 * 同じ役割分担）。
 */
void target_hrt_initialize(intptr_t exinf)
{
    /*
     * [2026-07-14 W0/自前VECBASE化] 自前ベクタ表 _kernel_vectors
     * （arch/xtensa_gcc/esp32/chip_vectors.S）へ VECBASE を設定する。
     * 本inirtnは CLS_PRC1/CLS_PRC2 の両方（target_timer.cfg）で走るため、
     * PRO_CPU・APP_CPU の両コアが各々（自コアのVECBASE特殊レジスタに）設定する。
     * target_kernel.cfg は target_timer.cfg を target_ipi.cfg より先に
     * INCLUDE しており、本inirtnは ipi_initialize より先に走る＝IPI(INT13)
     * 許可より前に VECBASE が確定する。tick(INT6)許可も本関数末尾のため
     * VECBASE 設定後になる。
     *
     * これ以前は「ROM VECBASE ＋ xtos_exc_handler_table[EXCCAUSE=4] へ
     * トランポリン(chip_l1int_entry_trampoline)を登録」する方式だったが、
     * classic ESP32のROM _UserExceptionVector が APP_CPU では表方式に
     * 協力せず _xtos_l1int_handler→break→0x40000288 スピンへ落ちる不具合
     * （実機JTAG確定、W0ブロッカー）があった。自前VECBASE化でROM表・
     * _xtos_l1int_handler 依存を断ち、APP_CPU でも Level-1割込みが自前
     * _kernel_l1int_entry へ確実に到達する。
     *
     * 自前 UserExceptionVector(chip_vectors.S) が ROM の a2/a3/a4 退避を
     * 肩代わりし、EXCCAUSE で demux する（=4→_kernel_l1int_entry、
     * その他→core_exc_entry）。Level-3割込みも自前ベクタ(0x1C0)が
     * _kernel_l3int_entry へ直接分岐するため、旧来の EXCSAVE3 事前設定
     * （ROM _Level3Vector の jx 間接分岐用）は不要になった（自前L3ベクタが
     * 入口で EXCSAVE3 へ中断時a2 を退避する）。
     */
    chip_set_vecbase();

#if TNUM_PRCID >= 2
    /*
     * [追加] 2026-07-17: HRT時刻原点のコア間同期（Bug A の真因修正。S3のF1
     * ＝コミット e2cb08e の移植）。
     *
     * tick(INT6)のenable_int（本関数末尾）より前に行う：ハンドシェイク中に
     *   tick割込みが割り込む余地を作らないため。またハンドシェイクは両コアの
     *   ランデブ（＝相互待ち合わせ）なので、両コアが barrier_sync(4) を出た
     *   直後のこの位置で行うことで往復時間rtt＝同期誤差(rtt/2)が最小になる。
     *
     * S3との呼出し位置の違い（意図的）：S3は本関数の**先頭**（＝自前
     *   ベクタ登録より前）で呼ぶが、LX6では chip_set_vecbase() の**直後**に
     *   置く。理由は、同期のマスタ側（CPU0）が hrt_sync_req を割込み許可
     *   状態でスピン待ちするためである。LX6はROMの _UserExceptionVector が
     *   APP_CPU で表方式に協力せず _xtos_l1int_handler→break→スピンへ落ちる
     *   既知のROM不具合があり（W0ブロッカー、実機JTAG確定。上の
     *   chip_set_vecbase() のコメント参照）、自前VECBASE確定**前**に割込み
     *   許可状態のコードを走らせたくない。S3はROM表方式のまま運用しており
     *   この制約が無いため先頭で足りる。
     *   chip_set_vecbase() は自コアのVECBASE特殊レジスタを書くだけの
     *   非ブロッキングかつ両コア対称な処理なので、この位置の差は
     *   (a) ランデブ成立性（デッドロック無し）に影響せず、
     *   (b) 両コアに同量の遅延しか与えないためコア間スキュー＝rttも増やさない。
     */
    target_hrt_sync_origin();
#endif /* TNUM_PRCID >= 2 */

    /*
     * tick(INT6)の許可はenable_intで行う（VECBASE設定後）。自コアのソフト
     * ウェア許可マスク_kernel_intenable_maskとハードのINTENABLEを対で更新
     * する。int6は本プロジェクト唯一のLevel-1割込み源。ROMの_xtos_ints_onは
     * 内部の_xtos_enabledでINTENABLEを上書きし、enable_intやCFG_INTで許可した
     * 割込み（例：INT7）のビットを消してしまうため使わない。
     */
    enable_int(XT_TIMER_INTNUM);
}

/*
 * タイマの停止処理
 */
void target_hrt_terminate(intptr_t exinf)
{
    /* CCOMPARE0を十分先の値にして実質的に停止する */
    target_hrt_clear_event(PRC1);
}

/*
 *  タイマ割込みハンドラ
 *
 *  CCOMPARE一致による割込みは，WSR.CCOMPARE0への書き込み（次回イベント
 *  設定）自体が割込み要因のクリアを兼ねる（NVICのような明示的な
 *  ペンディングビットクリアは不要）。次回のCCOMPARE0設定はsignal_time()
 *  経由でカーネルのtime_event処理がset_hrt_eventを呼ぶことで行われる。
 */
void target_hrt_handler_prc1(void)
{
    /*
     * 非タスクコンテキストの設定（i_begin_int/i_end_int＝割込みネスト
     * カウンタの増減）は、自前の割込みエントリ_kernel_l1int_entry
     * （core_support.S）が一元管理する（asmがintnestの唯一の管理者）。
     * ここでは呼ばない（二重カウント防止、design.md §7）。エントリが
     * intnestを+1した状態で呼ばれるため、signal_time()内の
     * assert(sense_context())は満たされる。CCOMPARE0の次回イベント設定・
     * 割込み要因クリアはsignal_time()→set_hrt_event()経由で行われる。
     */
    signal_time();

    /*
     * [追記56] 本関数は実際には呼ばれない（デッドコード）。
     * _kernel_l1int_entry（core_support.S）が擬似call4で呼ぶのは
     * _kernel_l1int_dispatch（下記）であり、tick(INT6)のsignal_time()呼出しは
     * そちらのif分岐内で行われる。本関数はarch-bringup初期の設計（VECBASE
     * 経由でtarget_hrt_handler_prc1を直接ディスパッチする案）の名残で、
     * Level-1割込みをtick/IPIでdemuxする現行方式（_kernel_l1int_dispatch）に
     * 移行した際に置き換え忘れたまま残っていた。診断ハートビート
     * （diag_heartbeat()）は_kernel_l1int_dispatch側のtick分岐へ移設済み。
     */
}

/*
 *  Level-1割込みの多重化（demux）
 *
 *  _kernel_l1int_entry（core_support.S）から擬似call4で呼ばれる。ESP32-S3の
 *  Level-1割込み源はEXCCAUSE=4で単一の入口に集約されるため、INTERRUPT
 *  レジスタのペンディングビットを見てtick(INT6)とIPI(INT13)を振り分ける。
 *  IPIを先に処理する（他コアからのディスパッチ要求を早く反映）。単一コア
 *  ではtickのみ。
 */
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
/*
 *  BT-4診断計装（既定OFF）：Level-1割込みディスパッチの所要時間計測。
 *  _kernel_l1int_entry（core_support.S）はディスパッチ中INTENABLE=0で
 *  走るため、この区間はrsil>=3マスク窓と同様にBTのLevel-3割込みを
 *  ブロックする（tick/UART TX等のLevel-1処理が長いとBLE接続イベントの
 *  RX窓を外す）。カウンタ実体はesp/shim/esp_shim.c。
 */
extern volatile uint32_t l3ld_l1d_max;
extern volatile uint32_t l3ld_l1d_sum;
extern volatile uint32_t l3ld_l1d_cnt;
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */

void
_kernel_l1int_dispatch(void)
{
    uint32_t pend, active;
    uint_t n;
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
    uint32_t l3ld_t0;
    Asm("rsr.ccount %0" : "=a"(l3ld_t0));
#endif

    /*
     *  _kernel_l1int_entryはディスパッチ中INTENABLEを0にする（再入防止）ため、
     *  ここでハードのINTENABLEを読むと0になる。許可状態の判定には自コアの
     *  ソフトウェア許可マスク_kernel_intenable_maskを用いる（dis_int中の
     *  割込みのISRを呼ばないため。test_int1のdis_int→ras_int→ISR非発火）。
     */
    Asm("rsr.interrupt %0" : "=a"(pend));
    active = pend & _kernel_intenable_mask[get_my_prcidx()];
#if TNUM_PRCID >= 2
    if ((active & INT_IPI_MASK) != 0U) {
        _kernel_ipi_irq_handler();
    }
#endif /* TNUM_PRCID >= 2 */
    if ((active & (1U << XT_TIMER_INTNUM)) != 0U) {
        signal_time();

        /*
         * [追記56] 生存監視の軽量ハートビート（常設recorder基盤、
         * diag_recorder.h）。自コアの「最終生存tick」を診断領域へ1ストア
         * するだけ（printf/syslog/カーネルAPI非依存、IRAM/SRAM常駐）。
         * 本関数（_kernel_l1int_dispatch）は各コアが自コアのtick(INT6)で
         * 呼ぶため、PRC1/PRC2両方のハートビートがここで更新される
         * （旧target_hrt_handler_prc1は実際には呼ばれないデッドコードの
         * ため、そちらから移設した）。
         */
        diag_heartbeat();

        if (_kernel_hrt_gain_tick_pending) {
            /*
             *  TTSP3のgain_tick向けワンショット処理（詳細は
             *  _kernel_hrt_gain_tick_pending宣言部のコメント参照）。
             *  タスクコンテキストのポーリングに頼らず，ハンドラ自身が
             *  直ちにdisable_intすることで，次の一致が起きる前に確実に
             *  マスクする。
             */
            _kernel_hrt_gain_tick_pending = false;
            disable_int(XT_TIMER_INTNUM);
        }
    }
    /*
     *  CFG_INT/CRE_ISRで登録されたLevel-1割込みのディスパッチ。
     *  tick(INT6)/IPI(INT13)は上で個別処理済み（ハンドラ表には未登録）。
     *  割込みネストは_kernel_l1int_entry（asm）が管理済みで、ここは既に
     *  非タスクコンテキスト。各ISRは要因を自身でクリアする（clr_int等）。
     *
     *  XCHAL_INTLEVEL1_MASKでハードウェア的にレベル1の割込み番号だけに
     *  絞り込む（BT-4調査でLevel-3対応を追加したため、_kernel_inh_handler_tbl[]
     *  にはレベル3の割込み番号（23/27等）も登録されうる。rsr.interruptは
     *  レベルを問わず全pendingビットを返すため、絞り込み無しではLevel-1
     *  文脈からLevel-3ハンドラを誤って呼んでしまう。
     *  .claude/plans/sparkling-forging-taco.md参照）。
     */
    active &= XCHAL_INTLEVEL1_MASK;
    /*
     *  2026-07-17 test_mtrans2のIPI receive-livelock対策：
     *  「割込み番号0〜31の全ビット位置を無条件に走査」から「activeに
     *  セットされているビットだけを昇順に処理」へ変更した。呼ばれる
     *  ハンドラの集合・順序・回数は完全に同一で（下記の等価性1〜4）、
     *  変わるのは「空振りする反復を踏まない」ことだけである。
     *
     *  ■動機（実測。非公開作業記録/20260716-mtrans2/evidence-22）
     *  この32回固定の走査は、IPI受信経路の**tail**（＝FROM_CPUトリガを
     *  クリアしてからrfeで中断タスクへ戻るまでの区間）の中にあった。
     *  実機CCOUNT実測で
     *      tail = 573.7cyc、うち本ループ = 358.6cyc（**62.5%**）
     *  であり、tail の最大の構成要素だった。
     *
     *  前進の必要条件は tail < gap（gap＝送出側が次にトリガを立てるまでの
     *  沈黙時間。evidence-19 §3）。rfeの瞬間に次のIPIが既にペンディング
     *  していると、中断タスクは1命令もretireできずに再捕捉される
     *  （＝古典的なreceive livelock）。実測 gap は送出ループ693.4cyc、
     *  victimがRUNNABLEな窓（rsm_tsk〜次のsus_tsk）は約347cyc。
     *  律速は後者である：evidence-19が「tail(730)>gap(693)」と書いたのは
     *  見積り730cycがhead込みだったための誤りで、真のtail 573.7は693を
     *  下回る。それでも前進しなかった事実が、693ではなく347cyc側が
     *  予算であることを示す。
     *  本ループを削ると tail は **実測 239.8cyc**（同一計装での前後比較。
     *  削減 333.9cyc）となり347cycを下回る。実機 test_mtrans2 は
     *  恒久ライブロック（INCOMPLETE）から 0.301s で DONE へ変わった
     *  （evidence-23。同一コード配置での位相掃引12点でも変更前5/12→
     *  変更後10/12と厳密に優越し、sweep=0＝実構成で×→○）。
     *
     *  限界（将来の担当者へ）：本変更は tail を予算内に収める
     *  「余裕の改善」であって「前進保証」ではない。人工的な位相掃引では
     *  変更後も通らない点が残る（evidence-23 §4.3）。上記の 347cyc モデルも
     *  近似で、その残存点を説明しない。真の前進保証は evidence-17 案D
     *  （IPI処理後にINT13を一時的にINTENABLEから落とし、victimをN cycle
     *  走らせてから再許可＝割込み駆動→ポーリング切替）であり、別課題として
     *  残っている。tail をこれ以上縮めるなら、次に大きいのは F1→G の
     *  158.7cyc（asm exit＋窓正規化＋dispatcher_1を毎回1周）で、
     *  FreeRTOS/Zephyrはここに no-switch 高速路を持つ（が、FMP3では
     *  core_support.S:745-749 の窓二重化回避が障壁。分岐1本では済まない）。
     *
     *  なぜ「窓スピル削減」でも「受信側backoff」でもないのか：
     *  どちらも**クリアより手前（head）**にあり、tail に現れないため
     *  前進可否を1ミリも動かさない。実際、
     *   ・受信側escalating backoffは設計どおり割込みを5.7倍削減しても
     *     前進ゼロ（ON/OFFともN=5で0/5。evidence-19）
     *   ・core_support.S:691の毎回無条件の全窓スピルは実測**53.0cyc**で
     *     あり（流通していた「1,104cyc」は誤り＝約21倍の過大）、しかも
     *     head側なので削っても無効（evidence-22）
     *  tail を縮めることだけが効く。
     *
     *  ■等価性の根拠（この変更が「静かに壊れない」理由）
     *   1. ループ本体は (active & (1<<n)) != 0 のときしか実行されない。
     *      セットされていないビット位置の反復は元々**完全な空振り**で
     *      あり、踏まなくても副作用は無い。
     *   2. 最下位セットビットから消費するのでnは**昇順**＝元のforと同順
     *      （優先順位を持つ割込み源の処理順序を変えない）。
     *   3. activeはローカルのスナップショット。元のforも毎反復この同じ
     *      スナップショットを参照しており、ループ後にactiveを読む箇所は
     *      無いため、破壊的に消費してよい。
     *   4. activeはXCHAL_INTLEVEL1_MASK済みの32bit値。取り出すnは必ず
     *      0..31＝元の走査範囲 0..TMAX_INTNO(=31) と同一で、
     *      _kernel_inh_handler_tbl[]（要素数TMAX_INTNO+1）の範囲外を
     *      触ることはない。
     *
     *  LX6移植（本ファイル。手本は target/esp32s3_devkitc_gcc/
     *  target_timer.c の同関数、commit 93644a0）。demuxループの実コードは
     *  S3と1バイト差なく同一（while(active!=0)＋__builtin_clz）。無印
     *  ESP32（Xtensa LX6）もNSAU命令を持つため__builtin_clzは1命令に
     *  展開される（ライブラリ__clzsi2呼出しにならないことを逆アセンブルで
     *  確認済み。非公開作業記録/20260718-mtrans2-fix-lx6/disasm 参照）。
     */
    while (active != 0U) {
        uint32_t    lsb = active & (~active + 1U);   /* 最下位のセットビット */

        n = 31U - (uint_t) __builtin_clz(lsb);       /* lsb!=0 なので未定義動作にならない */
        active &= ~lsb;
        if (_kernel_inh_handler_tbl[n] != NULL) {
            _kernel_inh_handler_tbl[n]();
        }
    }
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
    if (get_my_prcidx() == 0U) {
        uint32_t l3ld_t1, l3ld_d;
        Asm("rsr.ccount %0" : "=a"(l3ld_t1));
        l3ld_d = l3ld_t1 - l3ld_t0;
        l3ld_l1d_sum += l3ld_d;
        l3ld_l1d_cnt++;
        if (l3ld_d > l3ld_l1d_max) {
            l3ld_l1d_max = l3ld_d;
        }
    }
#endif
}

/*
 *  Level-3割込みの多重化（demux）
 *
 *  _kernel_l3int_entry（core_support.S）から擬似call4で呼ばれる
 *  （INTLEVEL=3のまま。core_asm.inc PS_L3INT_PSEUDO_CALL4参照）。
 *  INTERRUPTレジスタのpendingビットをXCHAL_INTLEVEL3_MASKで絞り込み、
 *  _kernel_l1int_dispatchと共有の_kernel_inh_handler_tbl[]へ
 *  ディスパッチする。tick/IPIのような特別扱いは不要（Level-3専用
 *  ソースのみを扱うため）。
 */
void
_kernel_l3int_dispatch(void)
{
    uint32_t pend, active;
    uint_t n;

    Asm("rsr.interrupt %0" : "=a"(pend));
    active = pend & _kernel_intenable_mask[get_my_prcidx()] & XCHAL_INTLEVEL3_MASK;
    for (n = 0; n <= (uint_t) TMAX_INTNO; n++) {
        if ((active & (1U << n)) != 0U && _kernel_inh_handler_tbl[n] != NULL) {
            _kernel_inh_handler_tbl[n]();
        }
    }
}
