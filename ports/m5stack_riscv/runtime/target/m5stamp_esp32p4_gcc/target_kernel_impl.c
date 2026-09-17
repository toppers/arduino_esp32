/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: target_kernel_impl.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    カーネルのターゲット依存部（Plarfire SoC Kit用）
 */

#include "kernel_impl.h"
#include <sil.h>
#include "riscv.h"

/*
 *  カーネル動作時のメモリマップと関連する定義(ToDo)
 */

/*
 *  システムログの低レベル出力のための初期化
 */
#ifndef TOPPERS_OMIT_TECS

/*
 *  セルタイプtPutLogSIOPort内に実装されている関数を直接呼び出す．
 */
extern void tPutLogSIOPort_initialize(void);

#else /* TOPPERS_OMIT_TECS */

extern void sio_initialize(EXINF exinf);
extern void target_fput_initialize(void);

#endif /* TOPPERS_OMIT_TECS */

/*
 *  entry point (start.S)
 */
extern void start(void);

#ifdef A1_P4_DISABLE_BOOT_WDT
/*
 *  【本 repo による意図的な乖離】M5Stamp-P4 Arduino 統合 段B4（2026-09-18）
 *  出典と乖離の申告: ../../IMPORT_PROVENANCE_p4.md §2
 *
 *  開発リポジトリの P4 は**自前の 2nd-stage bootloader**（esp/boot/seam_p4、
 *  その sdkconfig は `# CONFIG_BOOTLOADER_WDT_ENABLE is not set`）から起動する
 *  ので、FMP3 側がウォッチドッグに触る必要が無かった。本 repo は C6 / C5 と
 *  同じく **M5Stack の SDK が配る bootloader** をそのまま使うため、その
 *  bootloader が仕掛けたウォッチドッグが生きたまま FMP3 に渡ってくる。
 *  ESP-IDF のアプリはこれを起動処理で止めるが、FMP3 は止めない。
 *
 *  実測（M5Stamp-P4 rev v1.3・2026-09-18）: Blink は正しく動き、
 *  `[Arduino] loop heartbeat` と `[P4-CORE2] alive` を約 7〜9 秒出したあと
 *  `rst:0x7 (HP_SYS_HP_WDT_RESET)` で再起動する、という**起動ループ**になる。
 *  0x07 は `RESET_REASON_CORE_MWDT`（SDK の soc/reset_reasons.h）＝タイマ
 *  グループの WDT である。
 *
 *  ⇒ C6 / C5 の target（同じ理由で同じことをしている）と同じ形で、
 *    ここで止める。TIMG1 には触らない——**この構成で動いているのは TIMG0**
 *    であり（それが今リセットを掛けてきた）、クロックゲートされたままの
 *    ペリフェラルへ書くとバスストールになるためである。RTC/super WDT も
 *    触らない: リセット要因が 0x09 / 0x0D ではないので、**効いている証拠の
 *    無いものを同時に変えない**（効いたかどうかの帰属が取れなくなる）。
 */
#define A1_P4_TIMG0_BASE		0x500C2000U
#define A1_P4_TIMG_WDTCONFIG0	0x48U
#define A1_P4_TIMG_WDTWPROTECT	0x64U
#define A1_P4_TIMG_WDT_WKEY		0x50D83AA1U	/* soc/wdt_periph.h */

/*
 *  LP_WDT（RWDT・super WDT）。always-on ドメインなのでクロックゲートの心配は
 *  無い。番地は SDK の soc/esp32p4/register/hw_ver1/soc/{reg_base,lp_wdt_reg}.h
 *  （DR_REG_LPAON_BASE 0x50110000 + 0x6000）、鍵は hal/esp32p4/lpwdt_ll.h の
 *  LP_WDT_WKEY_VALUE / LP_WDT_SWD_WKEY_VALUE（どちらも 0x50D83AA1）。
 *
 *  **これが実際に効いた方**である（2026-09-18 の実測）。ESP-IDF の
 *  bootloader_config_wdt()（bootloader_init.c）は
 *    - RWDT を CONFIG_BOOTLOADER_WDT_TIME_MS（既定 9000 ms）で**有効にし**、
 *    - MWDT0 は flashboot 保護を外すだけ
 *  という順に書く。したがって FMP3 へ渡ってくる生きた番犬は RWDT である。
 *  ROM が印字するリセット要因の綴りは `rst:0x7 (HP_SYS_HP_WDT_RESET)` で、
 *  SDK の soc/reset_reasons.h は 0x07 を `RESET_REASON_CORE_MWDT` と呼ぶ
 *  ——**名前は MWDT を指しているが、実際に止めて効いたのは RWDT** という
 *  食い違いがあるので、名前から犯人を決めないこと。
 */
#define A1_P4_LP_WDT_BASE		0x50116000U
#define A1_P4_LP_WDT_CONFIG0	0x00U
#define A1_P4_LP_WDT_WPROTECT	0x18U
#define A1_P4_LP_WDT_SWD_CONFIG	0x1CU
#define A1_P4_LP_WDT_SWD_WPROT	0x20U
#define A1_P4_LP_WDT_WKEY		0x50D83AA1U
#define A1_P4_LP_WDT_SWD_AUTO_FEED_EN	(1U << 18)	/* lp_wdt_reg.h */
#define A1_P4_LP_WDT_SWD_DISABLE		(1U << 30)	/* lp_wdt_reg.h */

static void
a1_p4_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)(timg_base + A1_P4_TIMG_WDTWPROTECT),
				A1_P4_TIMG_WDT_WKEY);			/* 書込み保護の解除 */
	sil_wrw_mem((void *)(timg_base + A1_P4_TIMG_WDTCONFIG0), 0U);
	sil_wrw_mem((void *)(timg_base + A1_P4_TIMG_WDTWPROTECT), 0U);
}

static void
a1_p4_disable_rwdt(void)
{
	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_WPROTECT),
				A1_P4_LP_WDT_WKEY);
	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_CONFIG0), 0U);
	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_WPROTECT), 0U);

	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_SWD_WPROT),
				A1_P4_LP_WDT_WKEY);
	/*  read-modify-write。P4 の chip 層には C6 の sil_orw が無いので直に書く。 */
	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_SWD_CONFIG),
				sil_rew_mem((uint32_t *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_SWD_CONFIG))
				| A1_P4_LP_WDT_SWD_AUTO_FEED_EN | A1_P4_LP_WDT_SWD_DISABLE);
	sil_wrw_mem((void *)(A1_P4_LP_WDT_BASE + A1_P4_LP_WDT_SWD_WPROT), 0U);
}
#endif /* A1_P4_DISABLE_BOOT_WDT */

/*
 *  ハードウェアの初期化
 */
void
hardware_init_hook(void)
{
#ifdef A1_P4_DISABLE_BOOT_WDT
	/*  SDK の bootloader が仕掛けた番犬を止める（上のコメント参照）  */
	a1_p4_disable_mwdt(A1_P4_TIMG0_BASE);
	a1_p4_disable_rwdt();
#endif /* A1_P4_DISABLE_BOOT_WDT */

#ifdef TOPPERS_SUPPORT_HWLP
    /*
     *  HWLP コプロセッサコンテキスト管理(eager)の起動時初期化．
     *  本フックは start.S から master/slave 分岐前に呼ばれる＝全 PE で実行される．
     *  - 0x7F1(CSR_HWLP_STATE_REG)=CLEAN(2) で HWLP を常時有効化(ループ CSR を常時アクセス可)．
     *  - ループ CSR(0x7C6-0x7CB)を 0 初期化(boot 時の残留 count による偽ループを防止)．
     *  以後の新規タスクは hwlp_push(save 側)が切替毎に CSR をゼロ化するため count=0 を見る
     *  (共通部 start.S/start_r を変更せずに設計メモ #1 を満たす)．
     */
    Asm("csrwi 0x7f1, 2  \n\t"   /* HWLP enable: CLEAN          */
        "csrwi 0x7c6, 0  \n\t"   /* LOOP0_START_ADDR            */
        "csrwi 0x7c7, 0  \n\t"   /* LOOP0_END_ADDR              */
        "csrwi 0x7c8, 0  \n\t"   /* LOOP0_COUNT                 */
        "csrwi 0x7c9, 0  \n\t"   /* LOOP1_START_ADDR            */
        "csrwi 0x7ca, 0  \n\t"   /* LOOP1_END_ADDR              */
        "csrwi 0x7cb, 0  \n\t"   /* LOOP1_COUNT                 */
        ::: "memory");
#endif /* TOPPERS_SUPPORT_HWLP */

#ifdef TOPPERS_SUPPORT_PIE
#ifndef TOPPERS_PIE_LAZY
    /*
     *  PIE コプロセッサコンテキスト管理(eager)の起動時初期化．
     *  全PEで CSR_PIE_STATE_REG(0x7F2) = ON(1)にし，以後の pie_push/pie_pop の
     *  esp.* 命令を合法化する．レジスタ値は初期化不要(eager で毎切替 保存復帰し，
     *  PIE は制御に影響しないデータのみ．使うタスクは使用前に自分で設定する)．
     */
    Asm("csrwi 0x7f2, 1  \n\t"   /* PIE enable (INITIAL; IDF pie_enable と同値) */
        ::: "memory");
#else /* TOPPERS_PIE_LAZY */
    /*
     *  lazy: 起動時は CSR_PIE_STATE_REG(0x7f2)=OFF(0) にする．オーナ(pie_owner[PE])は起動時 NULL ゆえ
     *  どのタスクも非オーナで，初回 PIE 命令を illegal トラップさせて pie_exc_handler で
     *  オンデマンドに退避/復元する(ディスパッチ先のタスクの復元で chip_asm.inc
     *  pie_lazy_restore がオーナのみ enable に戻す)．
     */
    Asm("csrwi 0x7f2, 0  \n\t"   /* PIE OFF (lazy: 非オーナはトラップ) */
        ::: "memory");
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_SUPPORT_PIE */
}

/*
 *  Master processor initialization before str_ker().
 */
void
target_mprc_initialize(void)
{
    chip_mprc_initialize();

#ifdef TOPPERS_SEAM_P4_CORE1
    /*
     *  【本 repo による意図的な乖離】ESP32-P4 統合 段6（2026-08-15）
     *  出典と乖離の申告: ./IMPORT_PROVENANCE.md §2
     *
     *  seam（実 ESP-IDF 2nd-stage bootloader から FMP3 へ直行する起動方式）では、
     *  方式(a) の IDF ローダ殻（fmp_loader.c:38-41）が居ないため **core1 を起こす
     *  主体が存在しない**。ここで起こす。実体と一次資料は
     *  esp/boot/seam_p4_core1.c（本 repo 産）。
     *
     *  この位置である理由: start.S:174 から呼ばれる＝**.bss クリア／.data コピーの
     *  後・master のみ・start_sync へ MAGIC を書く直前**。core1 が slave_wait で
     *  読む start_sync[] は .bss にあるので、これより前（seam のエントリ等）で
     *  起こすと未初期化の値を読む窓ができる。ESP32-S3 の seam も同じ関数で
     *  APP_CPU を起こしている（fmp3/target/esp32s3_devkitc_gcc/target_kernel_impl.c）。
     *
     *  TOPPERS_SEAM_P4_CORE1 は seam かつ A1_P4_SEAM_CORE1=ON のときだけ定義される
     *  （cmake/a1_p4_stage1.cmake）。**方式(a) では前処理でこのブロックごと消える**。
     */
    {
        extern uint32_t seam_p4_core1_start(void);
        extern void     seam_p4_core1_report(uint32_t rc);

        seam_p4_core1_report(seam_p4_core1_start());
    }
#endif /* TOPPERS_SEAM_P4_CORE1 */
}

/*
 *  ターゲット依存の初期化
 */
void
target_initialize(PCB *p_my_pcb)
{    
    /*
     *  チップ依存の初期化
     */
    chip_initialize(p_my_pcb);

    /*
     *  SIOを初期化
     *    ESP32-P4 では UART0 のクロック有効化/リセット解除/ピン設定は
     *    ESP-IDF ブートローダが実施済みのため，ここでは行わない．
     *    (方式(a): IDF ローダ殻に乗せて起動)
     */
    sio_initialize(0);
    target_fput_initialize();
}

/*
 *  デフォルトのsoftware_term_hook（weak定義）
 */
__attribute__((weak))
void software_term_hook(void)
{
}

/*
 *  ターゲット依存の終了処理
 */
void
target_exit(void)
{
    /*
     *  software_term_hookの呼出し
     */
    software_term_hook();

    /*
     *  チップ依存の終了処理
     */
    chip_terminate();

    while (true) ;
}


/*
 *  システムログの低レベル出力（本来は別のファイルにすべき）
 */
#include "target_syssvc.h"
#include "target_serial.h"

/*
 *  低レベル出力用のSIOポート管理ブロック
 */
static SIOPCB  *p_siopcb_target_fput;

/*
 *  SIOポートの初期化
 */
void
target_fput_initialize(void)
{
    p_siopcb_target_fput = sio_opn_por(SIOPID_FPUT, 0);
}

/*
 *  ============================================================================
 *  SIOポートへのポーリング出力
 *  ============================================================================
 *  [改変] 2026-08-17・段P4TS — **待ち時間を有界かつ小さくし，ホスト不在を
 *  ラッチする**ようにした（`.steering/20260817-p4-target-uart-spin/`）．
 *
 *  改変前は「上限 100,000 回 x sil_dly_nse(100)」でハングは防いでいたが，
 *  **十分ではなかった**．上限到達までに **1 文字あたり実測 24.8 ms** かかり，
 *  かつ本関数を **CPU ロック(MIE=0)中に呼ぶ経路が多数ある**
 *  （1 行を loc_cpu() で囲う同期出力．P4 側だけで 14 ファイル）ため，
 *  ホストが USB CDC を drain していないときは
 *  **1 行が数百 ms〜1 秒の割込み禁止区間**になっていた．
 *
 *  これが M5Stack Tab5 の「表示が青い画面と交互になる」の真因である
 *  （段BLUE `.steering/20260817-tab5-blue-alternation/` で実測）．
 *  IDF の DPI パネルは num_fbs=1 でも **1 フレームごとに DW-GDMA 完了 ISR が
 *  ソフトウェアで DMA を貼り直す**ので，割込みを止めるとフレーム送出が
 *  **1 枚で止まる**（loc_cpu() 2000 ms 保持 -> 完了フレーム 1 枚．
 *  割込みが生きていれば 114 枚）．
 *
 *  直し方（2 段構え）:
 *    (1) 上限を実測で数 ms 台へ下げる（TARGET_FPUT_TXWAIT_SPIN）．
 *    (2) 一度でも上限に達したら「ホストは居ない」と**ラッチ**し，以後は
 *        待たずに捨てる．1 文字でも送信に成功した瞬間に忘れる．
 *  ⇒ ホスト不在時の割込み禁止区間は「**ラッチが立つ 1 回だけ上限ぶん**」で，
 *    以後は 1 文字あたりレジスタ読み 1 回になる．
 *
 *  **失うもの（正直に書く）**: ホストが drain していないときのログは落ちる
 *  （落とした文字数は target_fput_n_dropped に残る）．改変前も上限到達時は
 *  同じく捨てていたので，欠落そのものは新しい性質ではない．
 *
 *  段BLUE の `esp/p4shim/p4shim_misc.c` の直し方（CPU ロックの外で待ち，
 *  ロック中はすぐ書ける文字だけ出す）と**違える**理由: あれは呼び手側でしか
 *  できない形で，target 層は呼び手のロック状態を選べない．両者は排他ではなく
 *  重ねて効く（p4shim 経路は待ちが 0，それ以外の経路は一発だけ有界）．
 */

/*
 *  空きを待つ上限（回数）．
 *    段BLUE の実測で改変前の 100,000 回が約 24.8 ms ＝ 1 回あたり約 248 ns．
 *    20,000 回 ≒ 5 ms を採る．USB FS のフレーム(1 ms)数回分あるので，
 *    ホストが drain している限りここへは届かない
 *    （届いていないことは target_fput_n_dropped == 0 で確認する）．
 */
#ifndef TARGET_FPUT_TXWAIT_SPIN
#define TARGET_FPUT_TXWAIT_SPIN  20000U
#endif /* TARGET_FPUT_TXWAIT_SPIN */

/*
 *  「ホストが居ない」ことの記憶（ラッチ）と，落とした文字数（観測用）．
 *  SMP で複数コアから同時に触れうるが，競合しても「余分に 1 回待つ」か
 *  「余分に 1 文字落とす」だけで，壊れる状態は無い．
 */
volatile bool_t   target_fput_host_gone;
volatile uint32_t target_fput_n_dropped;

/*
 *  **negative control 用の旧実装**（既定では 1 行もコンパイルされない）．
 *    `-DTARGET_FPUT_LEGACY_SPIN=1` を付けたビルドだけが改変前の挙動になる．
 *    「直したから直った」を主張するには，**直す前が壊れていることを
 *    同じ台本で実演できる**必要がある．そのための口をここに残す
 *    （`.steering/20260817-p4-target-uart-spin/` V1-b）．
 *    既定ビルド(`seam-p4-smp` 等)の生成コードには一切影響しない．
 */
#if defined(TARGET_FPUT_LEGACY_SPIN) && (TARGET_FPUT_LEGACY_SPIN != 0)

Inline void
polafire_soc_kit_uart_fput(char c)
{
    uint32_t spin = 0U;
    while (!(sio_snd_chr(p_siopcb_target_fput, c))) {
        if (++spin >= 100000U) {
            break;
        }
        sil_dly_nse(100);
    }
}

#else /* TARGET_FPUT_LEGACY_SPIN */

Inline void
polafire_soc_kit_uart_fput(char c)
{
    uint32_t spin;

    if (target_fput_host_gone) {
        /*
         *  ホスト不在と覚えている間は待たない．sio_snd_chr は「試行 1 回」
         *  なので，成功したらその文字は送れており，同時にホスト復帰が分かる．
         */
        if (!(sio_snd_chr(p_siopcb_target_fput, c))) {
            target_fput_n_dropped++;
            return;
        }
        target_fput_host_gone = false;
        return;
    }
    for (spin = 0U; !(sio_snd_chr(p_siopcb_target_fput, c)); spin++) {
        if (spin >= TARGET_FPUT_TXWAIT_SPIN) {
            target_fput_host_gone = true;
            target_fput_n_dropped++;
            return;             /* ホスト不在．捨てて先へ進む */
        }
        sil_dly_nse(100);
    }
}

#endif /* TARGET_FPUT_LEGACY_SPIN */

/*
 *  SIOポートへの文字出力
 */
void
target_fput_log(char c)
{
    if (c == '\n') {
        polafire_soc_kit_uart_fput('\r');
    }
    polafire_soc_kit_uart_fput(c);
}

/*
 *  _sbrk（newlib のヒープ確保）
 *
 *  newlib の malloc/printf 系（_sbrk_r 経由）がヒープを要求するため，自己完結
 *  の静的ヒープを用いた最小限の _sbrk を提供する．SDK の newlib_stubs.c は
 *  _write 等が FMP3 のシリアル出力と競合するため取り込まず，本実装を用いる．
 */
#include <sys/types.h>

#ifndef TARGET_HEAP_SIZE
#define TARGET_HEAP_SIZE  (64 * 1024)
#endif /* TARGET_HEAP_SIZE */

static char target_heap[TARGET_HEAP_SIZE];

caddr_t
_sbrk(int incr)
{
    static char *heap_end = target_heap;
    char        *prev_heap_end = heap_end;

    if ((heap_end + incr) > (target_heap + TARGET_HEAP_SIZE)
            || (heap_end + incr) < target_heap) {
        return (caddr_t) -1;            /* ヒープ不足 */
    }
    heap_end += incr;
    return (caddr_t) prev_heap_end;
}












/*
 *  newlib 再入(__getreent): rand/assert/malloc 等が要求する．
 *  単一の _impure_ptr を返す(シングル _reent)．TTSP3 の rand/assert 用途では十分．
 */
#include <reent.h>
/*  IDF リンク時は freertos が strong な __getreent を，newlib が _exit を提供するため
 *  これらは weak にして衝突を避ける．スタンドアロン FMP3 リンク(IDF 無し)ではこちらが使われる． */
__attribute__((weak)) struct _reent *
__getreent(void)
{
    return _impure_ptr;
}

/*
 *  newlib syscall スタブ群．
 *  __getreent を提供すると newlib の reent 層(closer/readr/writer/...)が
 *  これらの下位 syscall を要求するため，最小スタブを置く．
 *  TTSP3 ではファイル I/O は使わない(出力は sio 経由)ので失敗を返すだけでよい．
 */
#include <sys/stat.h>
#include <errno.h>

int   _close(int fd)                       { (void)fd; return -1; }
int   _fstat(int fd, struct stat *st)      { (void)fd; if (st) st->st_mode = S_IFCHR; return 0; }
int   _isatty(int fd)                      { (void)fd; return 1; }
int   _lseek(int fd, int off, int wh)      { (void)fd; (void)off; (void)wh; return 0; }
int   _read(int fd, char *buf, int len)    { (void)fd; (void)buf; (void)len; return 0; }
int   _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int   _getpid(void)                        { return 1; }
int   _kill(int pid, int sig)              { (void)pid; (void)sig; errno = EINVAL; return -1; }
__attribute__((weak)) void _exit(int code) { (void)code; target_exit(); for (;;) ; }
