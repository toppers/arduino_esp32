/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *    カーネルのチップ依存部のアセンブリ言語部（ESP32-C6用）
 *
 *  ESP32-C3依存部をESP32-C6用に置き換えたもの．操作対象レジスタは
 *  PLICMX_BASE（Espressif呼称"PLIC"．実体はC3のCPU側INTMTXと同じ方式．
 *  esp32c6.h・intmtx_kernel_impl.h冒頭コメント参照）：
 *    - vectoredモードのmtvecにより mcause&0x1f がそのままCPU割込み線
 *      番号（＝ASP3のINTNO，1〜31）になるため，割込み番号の取得は
 *      mcauseのマスクだけでよい（claim／meinext相当の操作は不要）．
 *    - ハードウェアによる優先度の自動昇格を持たないため，入口で
 *      THRESHレジスタを「受け付けた割込みの優先度+1」へソフトウェアで
 *      昇格し（irc_begin_int），出口で割込み前の値へ復元する
 *      （irc_end_int）．C3と同じESP-IDFのvectors.S由来のパターン．
 *    - 全割込みソースをlevel型で使用するため，CLEARの操作は不要
 *      （ソース側のクリアで割込み線が落ちる）．
 */

#define TOPPERS_MACRO_ONLY
#define TOPPERS_ASM_MACRO
#define UINT_C(val)     (val) /* uint_t型の定数を作るマクロ */
#define ULONG_C(val)    (val) /* ulong_t型の定数を作るマクロ */
#define CAST(type, val) (val) /* 型キャストを行うマクロ */

#include "kernel_impl.h"
#include "offset.h"
#include "target_asm.inc"

/*
 *  トラップベクタテーブル(256byteアライン)
 *
 *  vectoredモードでは，例外はエントリ0，割込みは
 *  (mtvec & ~0xff) + (mcause & 0x1f) * 4 へ分岐する（ESP32-C6のCPUも
 *  C3と同じくmtvecの上位24ビットのみを使用するため256byteアラインが
 *  必須）．CPU割込み線は1〜31の31本．
 */
    AO_PUSH    /* 設定の保存 */
    AO_NORVC
    ABALIGN(256)
    AGLOBAL(trap_vector_table)
ALABEL(trap_vector_table)
    j core_exc_entry  /*  0:Synchronous Exception */
    .rept 31          /*  1〜31:CPU割込み線 */
    j core_int_entry
    .endr
    AO_POP     /* 設定の復帰 */

/*
 *  割込み入口処理での割込みコントローラ操作
 *
 *  受け付けた割込みに応じて割込み優先度マスクをセットする
 *  以下の内容をレジスタに入れてリターンする
 *   a0:割込み番号（0ならSpurious割込み．本チップでは0にならない）
 *   a1:割込み前の優先度＝割込み前のTHRESHレジスタの生値
 *   a2:割込みハンドラアドレス
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_int)
ALABEL(irc_begin_int)
    /*
     *  割込み番号の取得（vectoredモードのためmcauseの下位5ビットが
     *  CPU割込み線番号）
     */
    csrr t0, mcause
    andi a0, t0, 0x1f

    /*
     *  割込み前のTHRESHを保存（a1．irc_end_intで書き戻す）し，
     *  受け付けた割込みの優先度+1へ昇格する（優先度が同じか低い
     *  割込みをブロック．この後の共通部でMIEが再許可される）
     */
    li   t1, PLICMX_BASE
    lw   a1, PLICMX_THRESH_OFF(t1)
    slli t2, a0, 2
    add  t2, t1, t2
    lw   t0, PLICMX_PRI_BASEOFF(t2)  /* 受け付けた割込みの優先度 */
    addi t0, t0, 1
    sw   t0, PLICMX_THRESH_OFF(t1)
    fence                            /* THRESH反映をMIE再許可前に保証 */

    /*
     *  割込みハンドラアドレスの取得（自プロセッサの inh_table を PCB 経由で引く）
     */
    my_pcb t1, t2                  /* t1 = p_my_pcb */
    lx     t1, PCB_p_inh_tbl(t1)   /* 割込みハンドラテーブルの読込み */
    slli   t2, a0, 2               /* RV32: 4 バイト/エントリ */
    add    t2, t1, t2
    lx     a2, 0(t2)               /* 割込みハンドラの番地 */
    jr     ra

/*
 *  割込み出口処理での割込みコントローラ操作
 *
 *  a0:割込み番号
 *  a1:割込み前の優先度＝割込み前のTHRESHレジスタの生値
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_int)
ALABEL(irc_end_int)
    /*
     *  割込み前の優先度マスク（THRESH）を復元する
     */
    li   t0, PLICMX_BASE
    sw   a1, PLICMX_THRESH_OFF(t0)
    fence
    jr   ra

/*
 *  CPU例外発生前の割込み優先度の取得
 *
 *  CPU例外の発生で割込み優先度が変わることはないため，現在の割込み優先
 *  度を外部表現（負値）に変換して返す．内部表現 = THRESH - 1．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_get_intpri)
ALABEL(irc_get_intpri)
    li   t0, PLICMX_BASE
    lw   t0, PLICMX_THRESH_OFF(t0)
    addi t0, t0, -1                /* 内部表現に変換 */
    neg  a0, t0                    /* 外部表現に変換 */
    jr   ra

/*
 *  CPU例外ハンドラ呼出し前の割込みコントローラ操作
 *
 *  irc_end_excで用いる情報（CPU例外発生前のTHRESHの生値）を返す．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_exc)
ALABEL(irc_begin_exc)
    li   t0, PLICMX_BASE
    lw   a0, PLICMX_THRESH_OFF(t0)
    jr   ra

/*
 *  CPU例外ハンドラ呼出し後の割込みコントローラ操作
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_exc)
ALABEL(irc_end_exc)
    li   t0, PLICMX_BASE
    sw   a0, PLICMX_THRESH_OFF(t0)
    fence
    jr   ra
