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
 *    カーネルのチップ依存部のアセンブリ言語部（ESP32-P4 / CLIC 用）
 *
 *  PolarFire(PLIC vectored)版を CLIC モード向けに書き換え．
 *  ESP32-P4 は mtvec.MODE=3(CLIC) ハードワイヤ．非ベクタ(SHV=0)+ソフト
 *  ディスパッチとし，USE_RISCV_DIRECT_TRAP により core_int_entry が
 *  mcause を見て割込み/例外を振り分ける（共通部 core_support.S）．
 *
 *  割込み優先度マスクは CLIC_INT_THRESH(0x2080_0008)の[31:24]，
 *  割込み優先度は CLIC_INT_CTRL(0x2080_1000 + id*4)の[31:24]を用いる．
 *  「内部表現の割込み優先度」= これら [31:24] フィールドの 8bit 値とする．
 *
 *  TODO(要HW確認):
 *   - mcause の exccode 幅（ここでは12bitマスク）
 *   - レベルトリガ割込みのペンディングクリア（irc_end_int）
 *   - 外部割込み(線16-47)と割込みマトリクスの対応
 */

#define TOPPERS_MACRO_ONLY
#define TOPPERS_ASM_MACRO
#define UINT_C(val)     (val)
#define ULONG_C(val)    (val)
#define CAST(type, val) (val)

/*  アセンブラから使う CLIC レジスタの生アドレス（C キャスト無し）  */
#define CLIC_THRESH_ADDR  0x20800008
#define CLIC_CTRL_ADDR    0x20801000

#include "kernel_impl.h"
#include "offset.h"
#include "target_asm.inc"

/*
 *  トラップベクタ（CLIC 非ベクタ）
 *
 *  mtvec はこの先頭を指す（chip_initialize で mtvec=trap_vector_table|3）．
 *  USE_RISCV_DIRECT_TRAP 有効時，core_int_entry が mcause で例外/割込みを
 *  振り分けるため，ここは core_int_entry へジャンプするだけでよい．
 */
    AO_PUSH
    AO_NORVC
    ABALIGN(256)
    AGLOBAL(trap_vector_table)
ALABEL(trap_vector_table)
#if defined(TOPPERS_SUPPORT_PIE) && defined(TOPPERS_PIE_LAZY)
    /*
     *  lazy PIE ファストトラップ・プリフィルタ
     *
     *  PIE コプロセッサの「非オーナ初回 PIE 命令」による不正命令例外を，OS の例外
     *  入口/出口(core_exc_entry: 文脈退避・割込み許可・DEF_EXC テーブル・復帰時
     *  ディスパッチ)を一切介さず，**割込み禁止(トラップで MIE=0)のまま atomic** に
     *  処理して mret する(IDF FreeRTOS の vectors.S→rtos_save_pie_coproc と同方式)．
     *  これにより「割込み許可窓での非 atomic な esp.* coproc 退避」に起因する密 PIE
     *  ピンポン時のハング(pie_hwlp_design.md §9.2.2)を根治し，かつ高速化する．
     *  非PIE のトラップ(割込み/他の例外)は従来どおり core_int_entry へ素通しする．
     *
     *  mscratch は task 実行中フリー(core_int_entry も先頭で無退避に上書きする)なので
     *  t0 の一時退避に使える．判定はこの t0 一本で完結する．
     */
    csrw    mscratch, t0
    csrr    t0, mcause
    bltz    t0, 1f                  /* mcause[31]=1 → 割込み → 通常路 */
    slli    t0, t0, 20              /* exccode 抽出(CLIC 下位12bit; core_get_exccode と同手法) */
    srli    t0, t0, 20
    addi    t0, t0, -2              /* EXCNO_IINST(=2: 不正命令)か? */
    bnez    t0, 1f                  /* 不正命令以外の例外 → 通常路 */
    csrr    t0, 0x7f0               /* EXT_ILL を非破壊 read で覗く */
    andi    t0, t0, 0x4             /* EXT_ILL_RSN_PIE? */
    beqz    t0, 1f                  /* 非PIE の不正命令 → 通常路(EXT_ILL 温存) */
    csrr    t0, mscratch            /* task の t0 を復元 */
    j       pie_fast_trap           /* ← 割込み禁止のまま atomic 処理して mret */
1:
    csrr    t0, mscratch            /* task の t0 を復元 */
#endif /* TOPPERS_SUPPORT_PIE && TOPPERS_PIE_LAZY */
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

    /*  内部割込み(msip=3, mtimer=7)の分岐  */
    li  t1, MCAUSE_MSI
    beq t0, t1, do_msi
    li  t1, MCAUSE_MTI
    beq t0, t1, do_mti

    /*
     *  外部割込み: 割込みハンドラテーブルから番地，CLIC_INT_CTRL から優先度
     */
    my_pcb   t1, t2
    lx       t1, PCB_p_inh_tbl(t1)    /* 割込みハンドラテーブル */
#if __riscv_xlen == 64
    slli     t2, t0, 3
#elif __riscv_xlen == 32
    slli     t2, t0, 2
#endif
    add      t2, t1, t2
    lx       a2, 0(t2)                /* ハンドラ番地 */

    li       t3, CLIC_CTRL_ADDR       /* 優先度 = CLIC_INT_CTRL(id)[31:24] */
    slli     t2, t0, 2
    add      t3, t3, t2
    lw       a3, 0(t3)
    srli     a3, a3, 24
    j        irc_begin_int_1

ALABEL(do_msi)
    la     a2, msi_handler
    li     a3, 1
    j      irc_begin_int_1

ALABEL(do_mti)
    la     a2, target_hrt_handler
    li     a3, 1
    j      irc_begin_int_1

    /*
     *  割込み優先度マスクの設定
     *   a3 : 設定する割込み優先度(内部表現, [31:24] の 8bit 値)
     */
ALABEL(irc_begin_int_1)
    li      t0, CLIC_THRESH_ADDR
    lw      t1, 0(t0)
    srli    a1, t1, 24               /* 割込み前の優先度マスク(内部表現) */
    slli    t2, a3, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */
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
     *  CLIC の in-service level(mil)を 0 へ戻す準備．本割込み出口に続く mret
     *  (通常復帰/ネスト復帰/共通部のidle復帰・割込み文脈dispatchリダイレクト)が
     *  mcause.mpil から mil を復元するため，ここで mcause.mpil(bits23:16)=0 を
     *  強制する．これにより mil 固着(lost wakeup/livelock)を全出口で一律に断つ．
     *  本移植は割込み優先度マスクを CLIC_INT_THRESH(threshold)で行うため，mil=0
     *  にしても threshold が保護を担い preemption 意味論は不変(ネスト時も外側の
     *  threshold を下で復元)．0xFF00FFFF は RV32 専用(本チップは RV32)．t0/t1 は
     *  caller-saved のため破壊してよい．
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
     *  TODO(要HW確認): レベルトリガ源のペンディングクリアが必要なら追加．
     */
    li      t0, CLIC_THRESH_ADDR
    slli    t2, a1, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */

ALABEL(irc_end_int_2)
    jr ra

/*
 *  CPU例外発生前の割込み優先度の取得（外部表現で返す）
 *
 *  内部表現は CLIC_INT_THRESH[31:24] の 8bit 値であり，レベルは上位3ビット
 *  [7:5] に在る（NLBITS=3）．よって外部表現へ戻すには **バイトを取り出した後に
 *  さらに >>5 が要る**（chip_kernel_impl.h の EXT_IPM(pri) = -(pri>>5) と同じ）．
 *  この srli を欠くと，例えば IPM=-1（内部表現 0x3f）のとき -63 を返す．
 *  PLIC 版（polarfire_soc）は EXT_IPM(pri) = -pri でシフトが無いので，そこから
 *  持ってきた形になっていた（推測）．
 *  実害: T_EXCINF.intpri を通じて exc_get_intpri()/xsns_dpn() が壊れる．
 *  とくに INT_IPM が下位5ビットを1詰めするようになった後は，割込み優先度マスク
 *  全解除（TIPM_ENAALL）でも内部表現が 0x1f になるため，シフト無しでは -31 を返し
 *  **exc_sense_intmask() が「全解除」を認識できなくなる**（実機で -31 を実測．
 *  2026-08-16・ESP32-P4 M5Stamp rev v1.3）．INT_IPM の修正と本修正は**セットで
 *  入れること**（片方だけでは上記のとおり別の壊れ方をする）．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_get_intpri)
ALABEL(irc_get_intpri)
    li      t0, CLIC_THRESH_ADDR
    lw      a1, 0(t0)
    srli    a1, a1, 24               /* 内部表現(CTLバイト)を取り出す */
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
    li      t0, CLIC_THRESH_ADDR
    lw      a0, 0(t0)
    srli    a0, a0, 24
    jr      ra

/*
 *  CPU例外ハンドラ呼出し後の割込みコントローラ操作
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_exc)
ALABEL(irc_end_exc)
    li      t0, CLIC_THRESH_ADDR
    slli    t2, a0, 24
    sw      t2, 0(t0)
    lw      zero, 0(t0)              /* read-back */
    jr      ra

#if defined(TOPPERS_SUPPORT_PIE) && defined(TOPPERS_PIE_LAZY)
/*
 *  lazy PIE: 物理 PIE レジスタ群を frame(=a0) へ退避/復元する C 呼出可能関数
 *    void pie_save_regs(void *frame);     a0 = 保存域先頭(216B, 16B 整列)
 *    void pie_restore_regs(void *frame);  a0 = 保存域先頭
 *
 *  命令列は chip_asm.inc の eager pie_push/pie_pop と同一(IDF portasm.S 準拠)．
 *  base/scratch は esp.* の x8-15 制約を満たす a0(x10)/a1(x11)/a2(x12) を使用．
 *  呼出側で 0x7F2 を enable してから呼ぶこと(esp.* の合法化)．q3 は HW 仕様で除外．
 *  caller-saved の a0-a2 のみ破壊(ra は不変)．
 */
    ATEXT
    AALIGN(2)
    AGLOBAL(pie_save_regs)
ALABEL(pie_save_regs)
    esp.vst.128.ip  q0, a0, 16
    esp.vst.128.ip  q1, a0, 16
    esp.vst.128.ip  q2, a0, 16
    esp.vst.128.ip  q4, a0, 16      /* q3 は退避列から除外(IDF/HW 仕様) */
    esp.vst.128.ip  q5, a0, 16
    esp.vst.128.ip  q6, a0, 16
    esp.vst.128.ip  q7, a0, 16
    esp.st.qacc.l.l.128.ip a0, 16
    esp.st.qacc.l.h.128.ip a0, 16
    esp.st.qacc.h.l.128.ip a0, 16
    esp.st.qacc.h.h.128.ip a0, 16
    esp.st.ua.state.ip a0, 16
    esp.st.u.xacc.ip a0, 8
    esp.movx.r.sar a1
    slli a2, a1, 8
    esp.movx.r.sar.bytes a1
    slli a1, a1, 4
    or   a2, a2, a1
    esp.movx.r.fft.bit.width a1
    or   a2, a2, a1
    sw   a2, 0(a0)
    jr   ra

    ATEXT
    AALIGN(2)
    AGLOBAL(pie_restore_regs)
ALABEL(pie_restore_regs)
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vld.128.ip  q2, a0, 16
    esp.vld.128.ip  q4, a0, 16
    esp.vld.128.ip  q5, a0, 16
    esp.vld.128.ip  q6, a0, 16
    esp.vld.128.ip  q7, a0, 16
    esp.ld.qacc.l.l.128.ip a0, 16
    esp.ld.qacc.l.h.128.ip a0, 16
    esp.ld.qacc.h.l.128.ip a0, 16
    esp.ld.qacc.h.h.128.ip a0, 16
    esp.ld.ua.state.ip a0, 16
    esp.ld.xacc.ip a0, 8
    lw   a2, 0(a0)
    andi a1, a2, 0xf
    esp.movx.w.sar a1
    srli a2, a2, 4
    andi a1, a2, 0xf
    esp.movx.w.sar.bytes a1
    srli a2, a2, 4
    andi a1, a2, 0x3f
    esp.movx.w.fft.bit.width a1
    jr   ra

/*
 *  lazy PIE ファストトラップ本体(trap_vector_table のプリフィルタから飛来)．
 *
 *  到達時の状態: PIE 不正命令例外確定，MIE=0(トラップで割込み禁止), 全 GP レジスタは
 *  被トラップタスクの値(ベクタで t0 も mscratch から復元済), sp=有効なタスクスタック,
 *  mepc=faulting な esp.* 命令．OS の文脈退避を経ていないので，呼ぶ C 関数
 *  chip_pie_lazy_swap が破壊し得る caller-saved 整数レジスタ(ra, t0-t6, a0-a7)のみ
 *  タスクスタックへ退避→呼出→復元し，mret する(PC 不変ゆえ faulting 命令を 0x7F2=ON で
 *  再実行)．callee-saved(s0-s11)/sp/gp/tp は C ABI が保つ．float は本経路で未使用ゆえ
 *  退避不要．割込みは最後まで禁止のまま＝**esp.* coproc 退避が atomic**(本方式の要点)．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(pie_fast_trap)
ALABEL(pie_fast_trap)
    addi sp, sp, -(16 * XREG_SIZE)
    sx   ra,   0*XREG_SIZE(sp)
    sx   t0,   1*XREG_SIZE(sp)
    sx   t1,   2*XREG_SIZE(sp)
    sx   t2,   3*XREG_SIZE(sp)
    sx   t3,   4*XREG_SIZE(sp)
    sx   t4,   5*XREG_SIZE(sp)
    sx   t5,   6*XREG_SIZE(sp)
    sx   t6,   7*XREG_SIZE(sp)
    sx   a0,   8*XREG_SIZE(sp)
    sx   a1,   9*XREG_SIZE(sp)
    sx   a2,  10*XREG_SIZE(sp)
    sx   a3,  11*XREG_SIZE(sp)
    sx   a4,  12*XREG_SIZE(sp)
    sx   a5,  13*XREG_SIZE(sp)
    sx   a6,  14*XREG_SIZE(sp)
    sx   a7,  15*XREG_SIZE(sp)

    csrrw zero, 0x7f0, zero      /* EXT_ILL をクリア(再アーム)．ベクタの覗きは非破壊 */
    jal  chip_pie_lazy_swap      /* enable + オーナ入替え(esp.* save/restore) */

    lx   ra,   0*XREG_SIZE(sp)
    lx   t0,   1*XREG_SIZE(sp)
    lx   t1,   2*XREG_SIZE(sp)
    lx   t2,   3*XREG_SIZE(sp)
    lx   t3,   4*XREG_SIZE(sp)
    lx   t4,   5*XREG_SIZE(sp)
    lx   t5,   6*XREG_SIZE(sp)
    lx   t6,   7*XREG_SIZE(sp)
    lx   a0,   8*XREG_SIZE(sp)
    lx   a1,   9*XREG_SIZE(sp)
    lx   a2,  10*XREG_SIZE(sp)
    lx   a3,  11*XREG_SIZE(sp)
    lx   a4,  12*XREG_SIZE(sp)
    lx   a5,  13*XREG_SIZE(sp)
    lx   a6,  14*XREG_SIZE(sp)
    lx   a7,  15*XREG_SIZE(sp)
    addi sp, sp, (16 * XREG_SIZE)
    mret
#endif /* TOPPERS_SUPPORT_PIE && TOPPERS_PIE_LAZY */
