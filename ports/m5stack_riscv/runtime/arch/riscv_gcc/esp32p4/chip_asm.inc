/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
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
 *  $Id: core_asm.inc 64 2018-08-20 06:57:46Z ertl-honda $
 */

/*
 *    アセンブリ言語用マクロ定義（ESP32-P4 GNU開発環境用）
 */

#ifndef TOPPERS_CHIP_ASM_INC
#define TOPPERS_CHIP_ASM_INC

/*
 *  自プロセッサのプロセッサIDの取得
 */
.macro my_pid reg
    /*
     *  プロセッサID(prcid, 1オリジン)を返す．
     *  ESP32-P4: PE は hart0/hart1 → prcid = mhartid + 1 (hart0=PRC1=マスタ)．
     *  PolarFire(PE=hart1..4, prcid==hartid)と異なり +1 のオフセットが要る．
     *  これで start.S の master 判定(my_pid==TOPPERS_MASTER_PRCID=PRC1)が
     *  hart0 で成立する．プロセッサINDEX(0オリジン)は my_pidx を使うこと．
     */
    csrr \reg, mhartid
    addi \reg, \reg, 1
.endm

/*
 *  自プロセッサのプロセッサINDEXの取得
 *    ESP32-P4: core0=hart0, core1=hart1 → pidx = mhartid（master=0, 0オリジン）
 */
.macro my_pidx reg
    csrr \reg, mhartid
.endm

/*
 *  自プロセッサのコンテキストINDEXの取得
 *    CLIC の割込み優先度マスクは自コアのメモリマップドレジスタのため
 *    コンテキストINDEXは実質未使用．I/F互換のため mhartid を返す．
 */
.macro my_cidx reg
    csrr \reg, mhartid
.endm

/*
 *  ESP32-P4 コプロセッサ制御 CSR の定義
 *    CSR_PIE_STATE_REG (0x7F2): PIE コプロセッサの状態/有効化レジスタ．値が
 *      0=OFF のとき esp.* 命令は illegal 命令例外を起こし，非0=有効のとき esp.* 命令を
 *      実行できる．mstatus.FS 等と同様のコプロセッサ有効ビットで，PIE コンテキストの管理
 *      (特に lazy 方式でのオーナ判定)に用いる．
 */
#define CSR_PIE_STATE_REG	0x7f2

/*
 *  HWLP コプロセッサコンテキスト管理 (設計: pie_hwlp_design.md)．
 *  eager 無条件方式: タスクディスパッチ時にループ CSR(0x7C6-0x7CB, 6word=24B)をスタックに
 *  保存復帰する．状態ビット非依存で P4 rev<=1 のシリコンバグに構造的免疫．単一スイッチ
 *  TOPPERS_SUPPORT_HWLP(Makefile.chip)で有効化．hardware_init_hook(target, 全 PE)が
 *  0x7F1=CLEAN で常時有効化＋ループ CSR を初期ゼロ化し，以後は hwlp_push(退避側)が
 *  ディスパッチ毎に退避後ゼロ化するため，新規タスク(start_r 経由で restore を通らない)も
 *  count=0 を見る．これで共通部(start.S/start_r)・kernel を一切変更せずチップ/ターゲット
 *  依存部のみで完結．t0 はこれらフック時点で dead(自発: s退避前 / 非自発: フレーム退避済)．
 */
#ifdef TOPPERS_SUPPORT_HWLP
.macro hwlp_push
    addi sp, sp, -32             /* 24B 使用, 16B 境界維持で 32 確保 */
    csrr t0, 0x7C6; sw t0,  0(sp)   /* LOOP0_START_ADDR */
    csrr t0, 0x7C7; sw t0,  4(sp)   /* LOOP0_END_ADDR   */
    csrr t0, 0x7C8; sw t0,  8(sp)   /* LOOP0_COUNT      */
    csrr t0, 0x7C9; sw t0, 12(sp)   /* LOOP1_START_ADDR */
    csrr t0, 0x7CA; sw t0, 16(sp)   /* LOOP1_END_ADDR   */
    csrr t0, 0x7CB; sw t0, 20(sp)   /* LOOP1_COUNT      */
    /* 退避後にループ CSR をゼロ化．次に走るのが restore を通らない新規タスク
     * (start_r 経由)でも count=0 を見る → 残留 count による偽ループを防止(設計メモ #1)．
     * 復帰タスクは hwlp_pop で自分の値を上書きするため無害． */
    csrwi 0x7C6, 0; csrwi 0x7C7, 0; csrwi 0x7C8, 0
    csrwi 0x7C9, 0; csrwi 0x7CA, 0; csrwi 0x7CB, 0
.endm
.macro hwlp_pop
#ifdef HWLP_NO_RESTORE
    /* 負対照(検証用): sp 整合のみ行い CSR を復帰しない．これを有効化すると
     * プリエンプトされた hwloop のループ CSR が復帰されず結果が壊れる(errA>0)はず． */
    addi sp, sp, 32
#else /* HWLP_NO_RESTORE */
    lw t0,  0(sp); csrw 0x7C6, t0
    lw t0,  4(sp); csrw 0x7C7, t0
    lw t0,  8(sp); csrw 0x7C8, t0
    lw t0, 12(sp); csrw 0x7C9, t0
    lw t0, 16(sp); csrw 0x7CA, t0
    lw t0, 20(sp); csrw 0x7CB, t0
    addi sp, sp, 32
#endif /* HWLP_NO_RESTORE */
.endm
#endif /* TOPPERS_SUPPORT_HWLP */

/*
 *  PIE コプロセッサコンテキスト管理．既定は eager 無条件方式．
 *  タスクディスパッチ時に PIE 状態(q0-q7[q3はHW仕様で除外], QACC_L/H, UA_STATE, XACC,
 *  SAR系 misc)を 216B(16B整列)でスタックに保存復帰する．命令列は ESP-IDF portasm.S
 *  (pie_save_regs/pie_restore_regs)を踏襲し，スクラッチを a0/a1/a2 → t0/t1/t2 に機械置換
 *  した(int_disp フックでは a1=p_runtsk が live のため a 系を避ける．t0/t1/t2 は全フック点で
 *  dead)．hardware_init_hook(target, 全PE)が CSR_PIE_STATE_REG を非OFF(enable)にし esp.*
 *  命令を合法化する．新規タスク(start_r)は restore を通らず PIE レジスタを継承するが，PIE は
 *  単なるデータで制御に影響しないためゼロ化は不要(使うタスクは使用前に自分で設定する)．
 */
#ifdef TOPPERS_SUPPORT_PIE
#ifndef TOPPERS_PIE_LAZY
/*  --- eager(既定): タスクディスパッチ時に 216B をスタックに退避/復帰する ---  */
.macro pie_push
    /* esp.* は base/scratch を x8-x15 でしか符号化できない(t0/t1 不可)．
     * a2/a3/a4 を使う(全 4 フック点で安全: a1=p_runtsk は int_disp で live なので避け，
     * s0/s1 はフレーム退避前後で live なので避ける．a2-a5 は割込み入口でフレーム退避済 /
     * 自発時は caller-saved でスクラッチ可)． */
    addi sp, sp, -224               /* RvPIESaveArea(216B) を 16B 整列で確保 */
    mv   a2, sp
    esp.vst.128.ip  q0, a2, 16
    esp.vst.128.ip  q1, a2, 16
    esp.vst.128.ip  q2, a2, 16
    esp.vst.128.ip  q4, a2, 16      /* q3 は HW 仕様で退避列から除外(IDF 準拠) */
    esp.vst.128.ip  q5, a2, 16
    esp.vst.128.ip  q6, a2, 16
    esp.vst.128.ip  q7, a2, 16
    esp.st.qacc.l.l.128.ip a2, 16
    esp.st.qacc.l.h.128.ip a2, 16
    esp.st.qacc.h.l.128.ip a2, 16
    esp.st.qacc.h.h.128.ip a2, 16
    esp.st.ua.state.ip a2, 16
    esp.st.u.xacc.ip a2, 8
    esp.movx.r.sar a3
    slli a4, a3, 8
    esp.movx.r.sar.bytes a3
    slli a3, a3, 4
    or   a4, a4, a3
    esp.movx.r.fft.bit.width a3
    or   a4, a4, a3
    sw   a4, (a2)
.endm
.macro pie_pop
#ifdef PIE_NO_RESTORE
    /* 負対照(検証用): sp 整合のみで PIE を復帰しない．プリエンプトされた PIE 演算の
     * q レジスタ等が復帰されず結果が壊れる(errA>0)はず． */
    addi sp, sp, 224
#else /* PIE_NO_RESTORE */
    mv   a2, sp                     /* base/scratch は x8-x15 のみ可 → a2/a3/a4(pie_push と同) */
    esp.vld.128.ip  q0, a2, 16
    esp.vld.128.ip  q1, a2, 16
    esp.vld.128.ip  q2, a2, 16
    esp.vld.128.ip  q4, a2, 16
    esp.vld.128.ip  q5, a2, 16
    esp.vld.128.ip  q6, a2, 16
    esp.vld.128.ip  q7, a2, 16
    esp.ld.qacc.l.l.128.ip a2, 16
    esp.ld.qacc.l.h.128.ip a2, 16
    esp.ld.qacc.h.l.128.ip a2, 16
    esp.ld.qacc.h.h.128.ip a2, 16
    esp.ld.ua.state.ip a2, 16
    esp.ld.xacc.ip a2, 8
    lw   a4, (a2)
    andi a3, a4, 0xf
    esp.movx.w.sar a3
    srli a4, a4, 4
    andi a3, a4, 0xf
    esp.movx.w.sar.bytes a3
    srli a4, a4, 4
    andi a3, a4, 0x3f
    esp.movx.w.fft.bit.width a3
    addi sp, sp, 224
#endif /* PIE_NO_RESTORE */
.endm
#else /* TOPPERS_PIE_LAZY */
/*
 *  --- lazy: ディスパッチ元のタスクの退避はせず(オーナ据置)，ディスパッチ先のタスクで
 *      CSR_PIE_STATE_REG を管理する ---
 *  pie_push : ディスパッチ元のタスクの退避時に CSR_PIE_STATE_REG を OFF にする(物理レジスタ
 *    =データは据え置く)．これは新規タスク(start_r 経由で restore を通らない)にも OFF を継承
 *    させるためのもの．OFF を継承すれば新規タスクは非オーナとして初回 PIE 命令で必ずトラップ
 *    し，lazy の不変条件「CSR_PIE_STATE_REG が非OFF ⟺ 走行中タスク==オーナ」を満たす．これが
 *    無いと新規タスクが前タスクの非OFF を継承して「非オーナなのに有効」で走り，トラップせず
 *    物理状態を破壊する(実機: 起動時 LOW が q0/q1 ロードを trap せず VADD で trap し結果不整合)．
 *    正規のディスパッチでは直後にディスパッチ先のタスクの復元 pie_lazy_restore がオーナには
 *    非OFF を再設定する．
 *  pie_lazy_restore tcbreg : ディスパッチ先のタスク(tcbreg=p_runtsk)==pie_owner[PE] なら
 *    CSR_PIE_STATE_REG=enable，否なら OFF(以後の esp.* が illegal トラップ→pie_exc_handler で
 *    オンデマンド退避/復元)．
 *  PE 添字=mhartid．pie_owner は chip_kernel_impl.c の TCB *pie_owner[TNUM_PRCID]．
 *  scratch t0/t1/t2 は両復元点(dispatch_r / ret_int_r)で dead，ディスパッチ先のタスクは a0 で live．
 */
.macro pie_push
    csrwi CSR_PIE_STATE_REG, 0   /* ディスパッチ元のタスクの退避: OFF(新規タスクへ OFF を継承) */
.endm
.macro pie_lazy_restore tcbreg
    csrr  t0, mhartid
    la    t1, pie_owner
    slli  t2, t0, 2                 /* index * sizeof(TCB*) = 4 (RV32) */
    add   t1, t1, t2
    lw    t1, 0(t1)                 /* t1 = pie_owner[PE] */
    beq   \tcbreg, t1, .Lpie_own\@  /* ディスパッチ先のタスク==オーナ → enable */
    csrwi CSR_PIE_STATE_REG, 0     /* 非オーナ → OFF(esp.* はトラップ) */
    j     .Lpie_end\@
.Lpie_own\@:
    csrwi CSR_PIE_STATE_REG, 1     /* オーナ → enable */
.Lpie_end\@:
.endm
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_SUPPORT_PIE */

/*
 *  ディスパッチ時の追加コンテキストの保存
 *    保存順は hwlp → pie．復帰はその逆順(pie → hwlp)で LIFO 整合させる．
 */
.macro save_additional_regs_disp
#ifdef TOPPERS_SUPPORT_HWLP
    hwlp_push
#endif /* TOPPERS_SUPPORT_HWLP */
#ifdef TOPPERS_SUPPORT_PIE
    pie_push
#endif /* TOPPERS_SUPPORT_PIE */
.endm

/*
 *  ディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_disp
#ifdef TOPPERS_SUPPORT_PIE
#ifdef TOPPERS_PIE_LAZY
    pie_lazy_restore a0             /* dispatch_r: a0 = incoming p_runtsk */
#else /* TOPPERS_PIE_LAZY */
    pie_pop
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_SUPPORT_PIE */
#ifdef TOPPERS_SUPPORT_HWLP
    hwlp_pop
#endif /* TOPPERS_SUPPORT_HWLP */
.endm

/*
 *  タスク実行開始時(start_r)のTLS(スレッドローカルストレージ)初期化．
 *  ESP32-S31 チップ依存部で発見・実装した修正を移植(2026-07-09)．設計・検証の
 *  詳細は esp32_s31 リポジトリ docs/esp32s31_bringup_checklist.md 参照．
 *  ESP-IDF v6.1 picolibc の rand() 等 _Thread_local 変数は tp(スレッドポインタ)
 *  相対でアクセスされるが，FMP3 はタスク毎に tp を設定しないため tp=0 のまま
 *  残り，TLS 依存 libc 関数呼出しで tp+offset(≈0)への Load access fault になる
 *  (S31 実機の test_mtrans2 の rand() 呼出しで実証．P4 でも同じ欠落があるが
 *  未検証＝IDF v5.5 でも同一 TLS 実装の rand() の存在は確認済み)．
 *
 *  ESP-IDF FreeRTOS ポート(uxInitialiseStackTLS)と同じ方式: リンカ提供の TLS
 *  初期化イメージ(.tdata の値 + .tbss のゼロ)を，タスクスタック最下位
 *  (p_tinib->stk)へコピーし，その先頭を tp とする．新規タスク開始時点
 *  (activate_context: sp=stk+stksz)ではスタックは丸ごと未使用のため安全．
 *  .tbss は NOLOAD のため _thread_local_bss_start からは読まずサイズ計算のみ
 *  に用いる．TOPPERS_PIE_LAZY 有効時は stk 先頭224Bを PIE 保存域(pie_area_flag
 *  等)が使うため+224オフセット(P4 既定は eager のため通常は該当しない)．
 *
 *  レジスタ: \tcb_reg(a0=p_runtsk)は読むだけで不変．t0-t3/tp を破壊．
 */
#ifdef TOPPERS_SUPPORT_TLS
.macro init_additional_regs_start_r tcb_reg
    lw    t0, TCB_p_tinib(\tcb_reg)        /* t0 = p_tinib(RV32固定のため lw．lx は
                                             * core_asm.inc が本ファイル末尾の
                                             * #include より後で #define するため
                                             * ここではまだ未定義) */
    lw    tp, TINIB_stk(t0)                /* tp = p_tinib->stk(基準点，以後不変) */
#ifdef TOPPERS_PIE_LAZY
    addi  tp, tp, 224
#endif /* TOPPERS_PIE_LAZY */
    mv    t0, tp                           /* コピー先カーソル */
    la    t1, _thread_local_data_start     /* .tdata コピー元 */
    la    t2, _thread_local_data_end
.Ltls_data_loop\@:
    beq   t1, t2, .Ltls_bss_start\@
    lb    t3, 0(t1)
    sb    t3, 0(t0)
    addi  t1, t1, 1
    addi  t0, t0, 1
    j     .Ltls_data_loop\@
.Ltls_bss_start\@:
    la    t1, _thread_local_bss_start      /* .tbss はサイズ計算のみ(読まない) */
    la    t2, _thread_local_bss_end
.Ltls_bss_loop\@:
    beq   t1, t2, .Ltls_done\@
    sb    zero, 0(t0)
    addi  t0, t0, 1
    addi  t1, t1, 1
    j     .Ltls_bss_loop\@
.Ltls_done\@:
.endm
#else /* TOPPERS_SUPPORT_TLS */
.macro init_additional_regs_start_r tcb_reg
.endm
#endif /* TOPPERS_SUPPORT_TLS */

/*
 *  割込み・例外ハンドラ入口処理での追加コンテキストの保存
 */
.macro save_additional_regs_int
.endm

/*
 *  割込み・例外ハンドラ入口処理で保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int
.endm

/*
 *  割込み・例外ハンドラからのディスパッチ時の追加コンテキストの保存
 */
.macro save_additional_regs_int_disp
#ifdef TOPPERS_SUPPORT_HWLP
    hwlp_push
#endif /* TOPPERS_SUPPORT_HWLP */
#ifdef TOPPERS_SUPPORT_PIE
    pie_push
#endif /* TOPPERS_SUPPORT_PIE */
.endm
	
/*
 *  割込み・例外ハンドラからのディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int_disp
#ifdef TOPPERS_SUPPORT_PIE
#ifdef TOPPERS_PIE_LAZY
    pie_lazy_restore a0             /* ret_int_r: a0 = incoming p_runtsk */
#else /* TOPPERS_PIE_LAZY */
    pie_pop
#endif /* TOPPERS_PIE_LAZY */
#endif /* TOPPERS_SUPPORT_PIE */
#ifdef TOPPERS_SUPPORT_HWLP
    hwlp_pop
#endif /* TOPPERS_SUPPORT_HWLP */
.endm

/*
 *  CPU例外番号(exccode)を mcause から reg に取り出す
 *    CLIC モードでは mcause 上位に mpp/mpie/mpil 等が同時にセットされるため，
 *    下位12bit(exccode)のみ残す(slli/srli 20 は XLEN=32 前提)．reg のみ破壊．
 *    core_support.S の CPU例外入口で用いる(関数フックでなくマクロでインライン展開
 *    し，jal/ra 退避を不要にする)．
 */
.macro core_get_exccode_asm reg
    csrr  \reg, mcause
    slli  \reg, \reg, 20
    srli  \reg, \reg, 20
.endm

/*
 *  コア依存部で共通な定義
 */
#include "core_asm.inc"

#endif /* TOPPERS_CORE_ASM_INC */


