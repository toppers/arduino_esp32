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
 *  $Id: chip_kernel_impl.c 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    カーネルのチップ依存部（ESP32-P4 用）
 */

#include "kernel_impl.h"
#include "mtimer.h"
#if defined(TOPPERS_PIE_LAZY) || defined(TOPPERS_SUPPORT_HWLP)
#include "task.h"		/* lazy PIE: TCB(p_tinib->stk) / HWLP: p_runtsk 比較 の完全型が必要 */
#endif /* TOPPERS_PIE_LAZY || TOPPERS_SUPPORT_HWLP */

/*
 *  per-core storm 連続検出カウンタ（dispatch-IPI storm livelock breaker の状態）．
 *
 *  ESP32-P4 の CLIC + msip 構成では，他コアの sus_tsk/rsm_tsk 連打が割込み対象コアへ
 *  dispatch IPI(msip)を高頻度で送り，割込み対象コアが割込み入口/出口処理に飽和して，
 *  割込まれたタスクが割込みの合間に1命令も retire できない位相共振 livelock に
 *  陥る(実機 test_mtrans2 で実証．PolarFire 等の割込みコストが低い構成では発生
 *  しないため，本対策は ESP32-P4 チップ依存部に置く)．
 *  index は INDEX_PRC(prcid)(= mhartid)．BSS 非初期化環境のため明示ゼロ化する．
 */
volatile uint32_t clic_storm_cnt[TNUM_PRCID];
volatile uint32_t clic_storm_last[TNUM_PRCID];

/*
 *  IPI 受信入口フック irc_begin_ipi の ESP32-P4 実装＝dispatch-IPI storm livelock
 *  breaker．汎用 msi_handler(common/msi_ipi.c)の入口(clear_msip の前)から呼ばれる．
 *
 *  検出: 連続する msi_handler 入口の mcycle 間隔が CLIC_STORM_PERIOD_CYC 未満＝
 *  割込みが連続（近接）＝storm．storm 連続数 clic_storm_cnt を「storm で++/非storm
 *  で 0 リセット」し，連続数に比例した有界 backoff(上限 COUNT_CAP*UNIT)を回して
 *  受信側の割込み受理位相を他コアの raise cadence からずらし，割込み対象タスクの前進を
 *  保証する．
 *
 *  なぜ escalating(連続数比例)か: 固定 backoff は timing 敏感で脆い(デバッグ計装の
 *  数十 cyc が消えただけで効かなくなる Heisenbug を実測)．連続数比例なら任意の
 *  共振点で backoff が伸びて必ず割込み対象タスクの前進窓に入り，前進で storm が止み
 *  cnt=0 に戻る self-tuning．COUNT_CAP で最悪レイテンシを bound．
 *
 *  なぜ clear の前か: clear 前に遅延すると backoff 中に他コアが立てる msip は既に
 *  pending の同一線へ合体(coalesce)し，割込み対象コアが取る割込み回数自体が減る．
 *
 *  安全性: giant lock 非保持(msi_handler は lock 外)で，スケジューラ状態も msip の
 *  クリア/マスク論理も変えない．msip は level-pending のまま保持され，復帰後に asm
 *  ディスパッチ経路が p_schedtsk を再評価して必ずディスパッチ＝lost-wakeup 無し．
 *  通常時は割込み間隔が広く cnt=0・backoff 0，追加コストは mcycle 1読み(~数cyc)のみ．
 */
#define CLIC_STORM_PERIOD_CYC		4000U	/* 連続割込み間隔がこの cycle 未満なら storm
											 * (通常の dispatch IPI/tick は ms オーダ＝
											 * ≫4000cyc で誤検出しない．360MHz で ≈11us) */
#define CLIC_STORM_BACKOFF_UNIT		8U		/* escalate 1段あたり backoff ≈110cyc */
#define CLIC_STORM_COUNT_CAP		64U		/* 連続 storm 上限．backoff は CAP*UNIT=512
											 * (≈7us@360MHz)で頭打ち＝最悪レイテンシ bound */

void
irc_begin_ipi(PCB *p_my_pcb)
{
    uint_t idx = INDEX_PRC(p_my_pcb->prcid);
    uint32_t now, dt, loops;

    __asm__ volatile ("csrr %0, 0xB00" : "=r"(now));	/* mcycle 下位32bit */
    dt = now - clic_storm_last[idx];					/* 32bit wrap 安全(modular) */
    clic_storm_last[idx] = now;

    if (dt < CLIC_STORM_PERIOD_CYC) {
        /* storm 継続: 連続数を escalate(上限 COUNT_CAP で頭打ち) */
        if (clic_storm_cnt[idx] < CLIC_STORM_COUNT_CAP) {
            clic_storm_cnt[idx]++;
        }
    }
    else {
        /* 割込み間隔が広い=storm でない(割込み対象タスクが前進/storm 終了) */
        clic_storm_cnt[idx] = 0U;
    }

    /* 連続 storm 数に比例した有界 backoff を clear の前に費やす */
    loops = clic_storm_cnt[idx] * CLIC_STORM_BACKOFF_UNIT;
    {
        volatile uint32_t n;
        for (n = 0U; n < loops; n++) {
            /* 有界 backoff．volatile で最適化除去を防ぐ． */
        }
    }
}

/*
 *  チップ依存の初期化（マスタプロセッサ用）
 */
void
chip_mprc_initialize(void)
{
    uint_t i;

    /*  storm breaker 状態の明示ゼロ化（BSS 非初期化環境のため）  */
    for (i = 0; i < TNUM_PRCID; i++) {
        clic_storm_cnt[i]  = 0U;
        clic_storm_last[i] = 0U;
    }

    clic_global_initialize();
}

/*
 *  チップ依存の初期化
 */
void
chip_initialize(PCB *p_my_pcb)
{
    extern void *trap_vector_table;

    /*
     *  Machine Trap-Vector Base の設定
     *    ESP32-P4 は CLIC モード固定(mtvec.MODE=3)．非ベクタ(SHV=0)+ソフト
     *    ディスパッチのため mtvec に trap_vector_table の先頭を設定する．
     *    mtvt(CSR0x307)はハードウェアベクタリング用だが念のため同じ表を設定．
     */
    riscv_write_mtvec((ulong_t)&trap_vector_table | 0x3UL);
    __asm__ volatile ("csrw %0, %1" :: "i"(CSR_MTVT), "r"(&trap_vector_table));

    /*
     *  CLINT mtime の有効化
     *    ESP32-P4 の mtime は mtimectl の MTIME_EN で明示有効化が要る
     *    （PolarFire は常時稼働なので common の mtimer.c は有効化しない）．
     *    これが無いと mtime が進まず get_utm/task_loop 校正・周期処理が止まる．
     */
    sil_swrw_mem(MTIMER_MTIMECTL, MTIMER_MTIME_EN);

    /*
     *  Machine Timer の設定（mtimecmp を遠方にしタイマ割込み要求をクリア）
     */
    target_hrt_initialize(0);

    /*
     *  Machine External Interrupt の設定
     */
    clic_context_initialize(p_my_pcb);

    /*
     *  タイマ tick 割込み(CLIC 内部線7=mtimer)の有効化
     *    CLIC は割込みレベル = CTL>>(8-NLBITS) = CTL>>5 (NLBITS=3)．確実に配信
     *    させるため最大レベル7 (CTL=0xE0) とし，IE=1 を設定．閾値 THRESH は
     *    clic_context_initialize で 0(全通過)．mstatus.MIE は sta_ker の
     *    ディスパッチで立ち，以降 tick が発生する．level-trigger のため
     *    ハンドラが mtimecmp を更新すれば IP は自動クリア．
     *
     *    重要: CLIC_INT_CTRL レジスタの byte2=clicintattr には MODE(bit7:6)が
     *    あり，ROM 既定では 0xC0(MODE=M-mode)．全ワード書込みで attr を 0 に
     *    潰すと M-mode へ配信されない(実機で IP は立つが割込みが入らない不具合)．
     *    そこで read-modify-write で attr を保持し，ctl と ie のみ設定する．
     */
    {
        uint32_t v = sil_rew_mem(CLIC_INT_CTRL(CLIC_INTNO_MTIMER));
        v &= ~(0xFFUL << CLIC_INT_CTL_SHIFT);      /* ctl(レベル)をクリア */
        v |= (0xE0UL << CLIC_INT_CTL_SHIFT);       /* レベル7 */
        v |= CLIC_INT_IE_BIT;                       /* 許可 */
        sil_swrw_mem(CLIC_INT_CTRL(CLIC_INTNO_MTIMER), v);
    }

    /*
     *  プロセッサ間割込み(CLIC 内部線3=msip)の有効化
     *    コア間 IPI(raise_msip→他コアの CLINT_MSIP set→当該コアの CLIC線3)．
     *    線7(mtimer)と同様 read-modify-write で attr(MODE=M)を保持し ctl(レベル)/IE を設定．
     *    msi_handler が入口で clear_msip するため level-trigger で問題ない．
     */
    {
        uint32_t v = sil_rew_mem(CLIC_INT_CTRL(CLIC_INTNO_MSIP));
        v &= ~(0xFFUL << CLIC_INT_CTL_SHIFT);      /* ctl(レベル)をクリア */
        v |= (0xE0UL << CLIC_INT_CTL_SHIFT);       /* レベル7 */
        v |= CLIC_INT_IE_BIT;                       /* 許可 */
        sil_swrw_mem(CLIC_INT_CTRL(CLIC_INTNO_MSIP), v);
    }
    /*
     *  注: mintthresh(CSR 0x347)は ESP32-P4 に存在せず csrw で不正命令例外に
     *  なる(実機確認)．閾値はメモリマップド CLIC_INT_THRESH(0x2080_0008)のみ．
     */

    /*
     *  割込みの許可
     *    CLIC モードでは mie/mip は無効．割込み許可は mstatus.MIE と CLIC 個別 IE
     *    で行う．MSI/MTI/MEI の mie ビット設定は CLIC では不要なため削除．
     *    注: 周辺(外部)割込みの優先度→CLICレベル変換(<<5)は未対応(別TODO)．
     *        本コミットは first light のタイマ tick を優先．
     */

    /*
     *  コア依存の初期化
     */
    core_initialize(p_my_pcb);
}

/*
 *  チップ依存の終了処理
 */
void
chip_terminate(void)
{
    /*
     *  Machine Timer の終了処理
     */
    target_hrt_terminate(0);
    
    /*
     *  コア依存の終了処理
     */
    core_terminate();
}

/*
 *  割込み管理機能の初期化
 */
void
initialize_interrupt(PCB *p_my_pcb)
{
    clic_initialize_interrupt(p_my_pcb);
}

#ifdef TOPPERS_PIE_LAZY
/*
 *  lazy PIE: PIE コプロセッサコンテキストの遅延オーナ管理
 *    設計: pie_hwlp_design.md §9.2
 *
 *  オーナ = その PE の物理 PIE レジスタに生値が載るタスク．切替時は退避せず
 *  オーナを据え置き(ディスパッチ先のタスクの復元で CSR_PIE_STATE_REG のみ管理 = chip_asm.inc pie_lazy_restore)，
 *  非オーナの初回 PIE 命令を illegal トラップさせて本ファイルのハンドラで
 *  オンデマンドに退避/復元する．移行(mig_tsk)時は save_context フック
 *  (=chip_pie_save_context)で物理状態を保存域へ flush する．
 */

/*  PIE 退避/復元(物理レジスタ↔保存域)．chip_support.S の global asm 関数  */
extern void pie_save_regs(void *frame);
extern void pie_restore_regs(void *frame);

/*
 *  各 PE の現 PIE オーナ．NULL=無し．index は get_my_prcidx()(=mhartid)．
 *  chip_asm.inc の pie_lazy_restore が asm から 'pie_owner' を直接参照する．
 *  BSS 非初期化環境のため明示ゼロ化する．
 */
TCB *pie_owner[TNUM_PRCID] = { NULL };

/*  EXT_ILL CSR(0x7F0) の理由ビット: PIE=bit2  */
#define EXT_ILL_RSN_PIE		0x4U

/*
 *  保存域(タスクスタック底 224B)の「有効」マーカ．
 *
 *  非オーナの初回 PIE トラップで未初期化(garbage)の保存域を復元すると，garbage の
 *  mode レジスタ等が初回 PIE 演算を壊す(実機: 初回 compute 結果が 0 になる)．そこで
 *  保存域がまだ一度も flush されていない=未初期化なら復元せず物理状態を継承する
 *  (eager の start_r が初回に物理を継承するのと同義で，定義済みの状態になる)．
 *  「初期化済み」判定は保存域末尾(pie_save_regs が書く 204B の外，予約 224B 内の
 *  オフセット PIE_AREA_FLAG_OFF)にマジック値を置いて行う(per-task の別表を持たず，
 *  boot 時のタスク表走査も不要)．garbage が偶然マジックに一致する確率は 2^-32．
 */
#define PIE_AREA_MAGIC		0x50494531U		/* "PIE1" */
#define PIE_AREA_FLAG_OFF	208U			/* 16B 整列・[204,224) の空き領域内 */

Inline uint32_t *
pie_area_flag(TCB *p_tcb)
{
    return (uint32_t *)((uint8_t *) p_tcb->p_tinib->stk + PIE_AREA_FLAG_OFF);
}

/*  CSR_PIE_STATE_REG(PIE state): enable=1 / OFF=0  */
Inline void
pie_state_enable(void)
{
    Asm("csrwi 0x7f2, 1" ::: "memory");
}

Inline void
pie_state_off(void)
{
    Asm("csrwi 0x7f2, 0" ::: "memory");
}

/*
 *  マイグレーション時の追加コンテキスト保存(共通部 save_context フックの実体)．
 *
 *  p_tcb がこの PE の現オーナなら，物理 PIE を p_tcb の保存域(スタック底 = stk)へ
 *  flush し owner=NULL にする(非オーナなら何もしない=冪等)．添字は p_tcb->p_pcb
 *  ではなく「実行中の自 PE」を使う(mig_tsk は p_tcb->p_pcb を移行先へ更新済の
 *  ことがあるが，物理レジスタは常に自 PE 上にあるため)．caller は非オーナで
 *  CSR_PIE_STATE_REG OFF ゆえ，flush 用 esp.* の前後で enable/OFF を括る．
 */
void
chip_pie_save_context(TCB *p_tcb)
{
    uint_t pe = get_my_prcidx();

    if (pie_owner[pe] == p_tcb) {
        pie_state_enable();
        pie_save_regs(p_tcb->p_tinib->stk);
        pie_owner[pe] = NULL;
        pie_state_off();
    }
}

/*  タスク終了時の解放処理は chip_release_context(本ファイル末尾, HWLP と共通)へ  */

/*
 *  PIE オーナのオンデマンド入替え本体(EXT_ILL は呼出側でクリア済み前提)．
 *  IDF(components/freertos/.../portasm.S の rtos_save_pie_coproc)と同一アルゴリズム．
 *
 *  旧オーナを保存域へ flush → 自分の保存域から復元(初回のみ復元せず物理継承) →
 *  owner 更新．呼出後は cur がオーナ・CSR_PIE_STATE_REG enable のまま復帰し，PC 不変ゆえ
 *  faulting な esp.* を再実行する．
 *
 *  重要な順序(IDF 準拠):
 *   - CSR_PIE_STATE_REG enable は **先頭**で行う．直後に esp.*(pie_save_regs)を出すと csrwi の
 *     効果が間に合わず再トラップし保存域を破壊する．enable と最初の esp.* の間に
 *     get_my_prcidx/get_my_pcb 等の命令を挟んで enable を確定させる．
 *   - **初回使用時(保存域が未 flush)は復元しない**(物理状態を継承)．有効性は保存域末尾の
 *     マジック(PIE_AREA_MAGIC)で判定．
 *
 *  この関数は2経路から呼ばれる:
 *   (1) ファストトラップ pie_fast_trap(chip_support.S): トラップベクタで PIE 不正命令を横取りし，
 *       OS の例外入口/出口を介さず**割込み禁止のまま atomic** に本関数を呼ぶ(既定の lazy 経路)．
 *   (2) DEF_EXC 経由の pie_exc_handler(下記): ファストトラップ未使用時のフォールバック．
 */
void
chip_pie_lazy_swap(void)
{
    uint_t   pe;
    TCB      *cur, *old;
    uint32_t *flag;

    /* enable を先頭で.以降の get_my_* が enable 確定までの遅延を与える(IDF と同様) */
    pie_state_enable();

    pe  = get_my_prcidx();
    cur = get_my_pcb()->p_runtsk;
    old = pie_owner[pe];

    /*
     *  防御(規約違反時の墜落防止): 非タスクコンテキストでの PIE 命令使用は禁止規約
     *  だが，アイドル中(p_runtsk==NULL)の ISR が esp.* を踏むと cur==NULL のまま
     *  進み pie_area_flag(NULL) 書込み(0x0 近傍)→MIE=0 中の入れ子例外で解析困難な
     *  ハングになる．cur==NULL ならオーナ状態だけ保全(flush+解放)して enable のまま
     *  復帰する(以後の esp.* は物理レジスタ上で走り，タスクの保存状態は壊さない)．
     *  なお ISR(p_runtsk!=NULL)での使用は本ガードでは検出せず規約に委ねる
     *  (fast-trap 経路と DEF_EXC 経路で exncnt の基準値が異なり安価に判別できない)．
     */
    if (cur == NULL) {
        if (old != NULL) {
            pie_save_regs(old->p_tinib->stk);
            pie_owner[pe] = NULL;
        }
        return;
    }

    if (old != cur) {
        if (old != NULL) {
            pie_save_regs(old->p_tinib->stk);   /* 旧オーナを保存域へ flush */
        }
        pie_owner[pe] = cur;

        flag = pie_area_flag(cur);
        if (*flag == PIE_AREA_MAGIC) {
            pie_restore_regs(cur->p_tinib->stk); /* flush 済の有効な保存域 → 復元 */
        }
        else {
            *flag = PIE_AREA_MAGIC;              /* 初回: 復元せず物理を継承(IDF norestore) */
        }
    }
}

/*
 *  PIE トラップハンドラ(DEF_EXC(EXCNO_IINST, ...) 経由のフォールバック)．
 *
 *  既定では PIE 不正命令はファストトラップ pie_fast_trap がベクタで横取りするため本関数は
 *  PIE では起動しない(到達するのは非PIE の不正命令で，default_exc_handler へ委譲)．
 *  ファストトラップを無効化した構成でも正しく動くよう従来の振る舞いを残す．
 */
void
pie_exc_handler(void *p_excinf)
{
    uint32_t extill;

    /* illegal の理由を読み出しクリア．PIE 以外は default_exc_handler へ委譲 */
    Asm("csrrw %0, 0x7f0, zero" : "=r"(extill) : : "memory");
    if ((extill & EXT_ILL_RSN_PIE) == 0U) {
        default_exc_handler(p_excinf, EXCNO_IINST);
        return;
    }
    chip_pie_lazy_swap();
}
#endif /* TOPPERS_PIE_LAZY */

#if defined(TOPPERS_SUPPORT_HWLP) || defined(TOPPERS_PIE_LAZY)
/*
 *  タスク終了時の追加コンテキスト解放(共通部 release_context フックの実体)．
 *
 *  [HWLP] ext_tsk(exit_and_dispatch/exit_and_migrate)経路は追加退避マクロ
 *  (hwlp_push の退避後ゼロ化)を通らないため，hwloop 実行中に自タスクが終了すると
 *  ループ CSR(count≠0)が物理に残留する．残留 CSR は同タスクの再起動時(自身の旧
 *  LOOP_END で偽ループ発火)に限らず，**その PE で次に start_r 起動される任意の
 *  新規タスク**(restore=hwlp_pop を通らない)にも継承され得る．終了タスクが実行中
 *  (=物理ループ CSR が当該タスクの状態)のときのみゼロ化する(非実行タスクの終了
 *  [ras_ter/ter_tsk]では物理 CSR は現走行タスクの状態であり，対象のループ状態は
 *  破棄されるスタック上にあるため触らない)．
 *
 *  [lazy PIE] 終了するタスクのコンテキストは捨てられるため，save_context と違い
 *  物理 PIE の保存は不要．p_tcb がこの PE の現オーナなら owner=NULL +
 *  CSR_PIE_STATE_REG OFF にするだけで済む(pie_save_regs の 224B コピーが不要＝軽い)．
 *  これが無いと: 終了タスクがオーナのまま CSR_PIE_STATE_REG=ON を残し，直後に新規
 *  タスクが start_r(switch-in の CSR_PIE_STATE_REG 管理を通らない)で起動すると ON を
 *  継承して「非オーナなのに有効」な幽霊オーナとなり，後続のプリエンプトで生 PIE 状態を
 *  死んだタスクの保存域へ取り違えて flush し計算を壊す(pie_hwlp_design.md §9.2)．
 *  あわせて保存域のマジックを無効化し，同 TCB が再起動したとき旧 incarnation の
 *  保存状態を復元せず物理継承(初回扱い)にする．
 *
 *  添字/判定は実行中の自 PE を使う(全終了経路は同一 PE のタスクが対象．
 *  ras_ter/ter_tsk は他 PE 対象を E_OBJ で拒否する)．
 */
void
chip_release_context(TCB *p_tcb)
{
#ifdef TOPPERS_PIE_LAZY
    uint_t pe = get_my_prcidx();
#endif /* TOPPERS_PIE_LAZY */

#ifdef TOPPERS_SUPPORT_HWLP
    if (p_tcb == get_my_pcb()->p_runtsk) {
        Asm("csrwi 0x7c6, 0  \n\t"   /* LOOP0_START_ADDR */
            "csrwi 0x7c7, 0  \n\t"   /* LOOP0_END_ADDR   */
            "csrwi 0x7c8, 0  \n\t"   /* LOOP0_COUNT      */
            "csrwi 0x7c9, 0  \n\t"   /* LOOP1_START_ADDR */
            "csrwi 0x7ca, 0  \n\t"   /* LOOP1_END_ADDR   */
            "csrwi 0x7cb, 0  \n\t"   /* LOOP1_COUNT      */
            ::: "memory");
    }
#endif /* TOPPERS_SUPPORT_HWLP */

#ifdef TOPPERS_PIE_LAZY
    if (pie_owner[pe] == p_tcb) {
        pie_owner[pe] = NULL;
        pie_state_off();
    }
    *pie_area_flag(p_tcb) = 0U;
#endif /* TOPPERS_PIE_LAZY */
}
#endif /* TOPPERS_SUPPORT_HWLP || TOPPERS_PIE_LAZY */
