/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス．無保証．
 */

/*
 *    カーネルのチップ依存部のアセンブリ言語部（ESP32-C5 / CLIC 用）
 *
 *  出典: fmp3/arch/riscv_gcc/esp32p4/chip_support.S（P4 CLIC 層。fmp3_core
 *  pristine の複製）を単一コア向けに縮めたもの。
 *  差分: (1) CLINT 内部線（msip=3 / mtimer=7）の分岐（do_msi / do_mti）を削除
 *  （C5 層は CLINT を使わない。全割込みが外部線として inh_table を引く）、
 *  (2) lazy PIE のファストトラップ / pie_save_regs 等を削除（コプロセッサ無し）、
 *  (3) 割込み優先度マスク（閾値）の read/write を CSR mintthresh（0x347）
 *  既定 / メモリマップド CLIC_INT_THRESH_REG（0x20800008）option の 2 経路に
 *  した（TOPPERS_ESP32C5_CLIC_THRESH_MMIO。esp32c5_clic_kernel_impl.h と対）。
 *  P4 の出口正規化（irc_end_int の mcause.mpil=0）と D3（irc_get_intpri の
 *  srli 5）はそのまま残す。
 *
 *  ESP32-C5 は標準 CLIC（mtvec.MODE=3）。非ベクタ(SHV=0)+ソフトディスパッチとし，
 *  USE_RISCV_DIRECT_TRAP により core_int_entry が mcause を見て割込み/例外を
 *  振り分ける（共通部 core_support.S）。
 *
 *  「内部表現の割込み優先度」= CLIC_INT_CTRL[31:24] / 閾値の 8bit 値。
 *  CSR 版の mintthresh はその 8bit 値を [7:0] にそのまま持つ（IDF
 *  interrupt_clic.h の INTTHRESH_STANDARD、asp3 C5 と同じ）。MMIO 版は
 *  [31:24]（P4 と同じ）。
 *
 *  段3 Task 4（2026-09-16）で実機確定: **効くのは CSR mintthresh**。
 *  fmp3/test_ovr/test_ipmmask（chg_ipm(-1) で INTNO1 がマスクされる）は CSR 版で
 *  PASS（4 check point）、MMIO 版（TOPPERS_ESP32C5_CLIC_THRESH_MMIO）では
 *  `isr_count == 0` の assert で FAIL（.steering/20260915-c5-plan/stage3/README.md
 *  4 節）。既定は CSR のまま。MMIO 分岐は「起動は壊さないが閾値として効かない」
 *  実測記録として残す。未確認のまま: mintthresh の read-back の幅、mcause の
 *  exccode 幅（12bit マスクは P4 と同じ）。
 *
 *  fence について（レビュー F6）: asp3 C5 の chip_support.S は csrw 0x347 の直後に
 *  fence を置く（「mintthresh 反映を MIE 再許可前に保証」）。IDF v5.5.4 の CLIC コード
 *  （interrupt_clic.h の rv_utils_restore_intlevel_regval、INTTHRESH_STANDARD 側）は
 *  csrw のみで fence を置かず、P4 の MMIO 版は lw による read-back で代替している。
 *  同一 hart の CSR 書込みは後続命令に対して順序付けられるため、本層は IDF に
 *  合わせて fence を置かない。段2 で優先度マスクが効かない徴候が出たら最初に
 *  試す 1 軸（asp3 に fence を要した記録は無い）。
 */

#define TOPPERS_MACRO_ONLY
#define TOPPERS_ASM_MACRO
#define UINT_C(val)     (val)
#define ULONG_C(val)    (val)
#define CAST(type, val) (val)

/*  アセンブラから使う CLIC レジスタの生アドレス / CSR 番号（C キャスト無し）  */
#define CLIC_THRESH_ADDR  0x20800008
#define CLIC_CTRL_ADDR    0x20801000
#define CSR_MINTTHRESH_N  0x347

#include "kernel_impl.h"
#include "offset.h"
#include "target_asm.inc"

/*
 *  トラップベクタ（CLIC 非ベクタ）
 *
 *  mtvec はこの先頭を指す（chip_initialize で mtvec=trap_vector_table|3）．
 *  USE_RISCV_DIRECT_TRAP 有効時，core_int_entry が mcause で例外/割込みを
 *  振り分けるため，ここは core_int_entry へジャンプするだけでよい．
 *  非ベクタの分岐先は (mtvec & ~0x3f) なので 64 バイト整列が要る（asp3 C5 の
 *  注記）．P4 と同じ 256 整列で満たす．
 */
    AO_PUSH
    AO_NORVC
    ABALIGN(256)
    AGLOBAL(trap_vector_table)
ALABEL(trap_vector_table)
    j core_int_entry
    AO_POP

/*
 *  割込み入口処理での割込みコントローラ操作
 *
 *  受け付けた割込みに応じて割込み優先度マスクをセットし，以下を返す:
 *   a0:割込み番号  a1:割込み前の優先度(内部表現)  a2:割込みハンドラアドレス
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_int)
ALABEL(irc_begin_int)
    /*
     *  CLIC: mcause の exccode が割込み線番号
     */
    csrr t0, mcause
    li   t1, 0xfff
    and  t0, t0, t1               /* t0 = 割込み線番号(id) */

    /*  割込み番号 a0 = (プロセッサID << 16) | id  */
    my_pid t1
    slli   t1, t1, 16
    add    a0, t0, t1

    /*
     *  割込みハンドラテーブルから番地，CLIC_INT_CTRL から優先度
     *  （C5 は CLINT 内部線を使わないので P4 の do_msi/do_mti 分岐は無い）
     */
    my_pcb   t1, t2
    lx       t1, PCB_p_inh_tbl(t1)    /* 割込みハンドラテーブル */
    slli     t2, t0, 2                /* RV32: 4 バイト/エントリ */
    add      t2, t1, t2
    lx       a2, 0(t2)                /* ハンドラ番地 */

    li       t3, CLIC_CTRL_ADDR       /* 優先度 = CLIC_INT_CTRL(id)[31:24] */
    slli     t2, t0, 2
    add      t3, t3, t2
    lw       a3, 0(t3)
    srli     a3, a3, 24

    /*
     *  割込み優先度マスクの設定
     *   a3 : 設定する割込み優先度(内部表現, 8bit 値)
     *   a1 : 割込み前の優先度マスク(内部表現)
     */
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    li      t0, CLIC_THRESH_ADDR
    lw      t1, 0(t0)
    srli    a1, t1, 24
    slli    t2, a3, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    csrrw   a1, CSR_MINTTHRESH_N, a3 /* a1 = 旧 mintthresh, mintthresh = a3 */
    andi    a1, a1, 0xff
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    jr      ra

/*
 *  割込み出口処理での割込みコントローラ操作
 *   a0:割込み番号  a1:割込み前の優先度(内部表現)
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_int)
ALABEL(irc_end_int)
    /*
     *  CLIC の in-service level(mil)を 0 へ戻す準備（P4 の出口正規化）．
     *  本割込み出口に続く mret(通常復帰/ネスト復帰/共通部のidle復帰/
     *  割込み文脈dispatchリダイレクト)が mcause.mpil から mil を復元するため，
     *  ここで mcause.mpil(bits23:16)=0 を強制する．これにより mil 固着
     *  (asp3 が C5 で踏んだ全割込み凍結: docs/c5-clic-exit-fix-review.md)を
     *  全出口で一律に断つ．本移植は割込み優先度マスクを閾値で行うため，mil=0
     *  にしても閾値が保護を担い preemption 意味論は不変．0xFF00FFFF は RV32 専用．
     */
    csrr    t0, mcause
    li      t1, 0xFF00FFFF
    and     t0, t0, t1
    csrw    mcause, t0

    beqz    a0, irc_end_int_2        /* Spurious は何もしない */

    /*
     *  割込み優先度マスクを割込み前に戻す
     *  CLIC はハンドラ起動時にペンディングを自動クリアする想定のため
     *  ここでの明示的な complete 操作は行わない．
     */
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    li      t0, CLIC_THRESH_ADDR
    slli    t2, a1, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    csrw    CSR_MINTTHRESH_N, a1
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */

ALABEL(irc_end_int_2)
    jr ra

/*
 *  CPU例外発生前の割込み優先度の取得（外部表現で返す）
 *
 *  内部表現は閾値の 8bit 値であり，レベルは上位3ビット[7:5]に在る（NLBITS=3）．
 *  よって外部表現へ戻すにはバイトを取り出した後にさらに >>5 が要る
 *  （chip_kernel_impl.h の EXT_IPM(pri) = -(pri>>5) と同じ．P4 の D3）．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_get_intpri)
ALABEL(irc_get_intpri)
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    li      t0, CLIC_THRESH_ADDR
    lw      a1, 0(t0)
    srli    a1, a1, 24               /* 内部表現(8bit)を取り出す */
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    csrr    a1, CSR_MINTTHRESH_N
    andi    a1, a1, 0xff             /* 内部表現(8bit)を取り出す */
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    srli    a1, a1, 5                /* レベル(上位3ビット)を右詰めする */
    neg     a0, a1                   /* 外部表現に変換 */
    jr      ra

/*
 *  CPU例外ハンドラ呼出し前の割込みコントローラ操作
 *   irc_end_exc で用いる情報（例外発生前の割込み優先度マスク, 内部表現）を返す
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_exc)
ALABEL(irc_begin_exc)
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    li      t0, CLIC_THRESH_ADDR
    lw      a0, 0(t0)
    srli    a0, a0, 24
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    csrr    a0, CSR_MINTTHRESH_N
    andi    a0, a0, 0xff
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    jr      ra

/*
 *  CPU例外ハンドラ呼出し後の割込みコントローラ操作
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_exc)
ALABEL(irc_end_exc)
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    li      t0, CLIC_THRESH_ADDR
    slli    t2, a0, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    csrw    CSR_MINTTHRESH_N, a0
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    jr      ra
