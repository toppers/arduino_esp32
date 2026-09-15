/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
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
 *    カーネルのチップ依存部（ESP32-C5 用）
 *
 *  出典: fmp3/arch/riscv_gcc/esp32p4/chip_kernel_impl.c（P4 CLIC 層）を
 *  単一コア向けに縮めたもの。落としたもの: irc_begin_ipi（dispatch-IPI storm
 *  breaker、CLINT msip 用）、mtimer の有効化と CLIC 内部線 3/7（msip/mtimer）
 *  の設定、lazy PIE / HWLP（コプロセッサ無し）。足したもの:
 *  esp32c5_intmtx_route（C6 の型。MAP 値は CLIC 線番号そのもの）、
 *  chip_initialize での割込みマトリクス全解除と CLIC 外部線の ATTR.MODE=machine
 *  復元（下記）。
 */

#include "kernel_impl.h"

/*
 *  ペリフェラル割込みソースの CLIC 線への割付け（割込みマトリクス）
 *
 *  MAP レジスタへ書く値は割付先の CLIC 線番号そのもの（16..47）。P4 の
 *  chip_serial.c（INTNO_SIO=16 を直接書く）と同じ意味で、asp3 が C5 実機で
 *  「MAP 値は CLIC 内部番号（+16 込み）」と確定した（docs/c5-bringup.md 実施02:
 *  USB src54 を 17 で配線すると CLIC 17 が pending のまま未配送、33 へ変えると配送）。
 *  16 未満（内部線）を書くのは誤りなので assert で止める。
 */
void
esp32c5_intmtx_route(uint_t intsrc, INTNO intno)
{
    assert(intsrc < ESP32C5_TNUM_INTSRC);
    assert(CLIC_EXT_OFFSET <= INTNO_MASK(intno) && INTNO_MASK(intno) <= TMAX_INTNO);
    sil_wrw_mem(INTMTX_MAP(intsrc), (uint32_t) INTNO_MASK(intno));
}

/*
 *  チップ依存の初期化（マスタプロセッサ用）
 */
void
chip_mprc_initialize(void)
{
    clic_global_initialize();
}

/*
 *  チップ依存の初期化
 */
void
chip_initialize(PCB *p_my_pcb)
{
    extern void *trap_vector_table;
    uint_t i;

    /*
     *  Machine Trap-Vector Base の設定
     *    ESP32-C5 は CLIC モード（mtvec.MODE=3）．非ベクタ(SHV=0)+ソフト
     *    ディスパッチのため mtvec に trap_vector_table の先頭を設定する．
     *    mtvt(CSR 0x307)はハードウェアベクタリング用だが念のため同じ表を設定
     *    （P4 / asp3 C5 と同じ）．
     */
    riscv_write_mtvec((ulong_t)&trap_vector_table | 0x3UL);
    __asm__ volatile ("csrw %0, %1" :: "i"(CSR_MTVT), "r"(&trap_vector_table));

    /*
     *  割込みマトリクスの全解除（全ソースを CLIC 線 0 = 内部線/IE=0 へ）．
     *  ROM/bootloader が設定した割付け（USB-Serial/JTAG 等）を消す意味もある
     *  （C6 の intmtx_initialize / asp3 C5 の clic_initialize と同じ）．
     */
    for (i = 0U; i < ESP32C5_TNUM_INTSRC; i++) {
        sil_wrw_mem(INTMTX_MAP(i), 0U);
    }

    /*
     *  CLIC の初期化（閾値 0=全通過，全線 IE=0/IP=0/CTL=0）．共通部の
     *  clic_context_initialize は CLIC_INT_CTRL を全ワード 0 で書くため
     *  ATTR バイト（MODE[7:6]）も 0 になる．clic_reg.h の ATTR_MODE default は
     *  2'b11（machine）で，asp3 は C5 実機で ATTR=0xC0（machine, level, SHV=0）を
     *  明示して動かした（asp3 clic_initialize）．IDF は MODE を書かない（RMW）．
     *  MODE=0（user）のまま配信されるか，WARL で 3 に固定されるかは**未確認**
     *  （P4 は共通部のまま動いている＝P4 では固定されている推定）なので，
     *  安全側に倒して全線の ATTR バイトへ 0xC0 を書き戻す（TA_EDGE の線は
     *  後続の clic_config_int が RMW で TRIG ビットを立てる）．
     */
    clic_context_initialize(p_my_pcb);
    for (i = 0U; i < CLIC_TNUM_INTNO; i++) {
        sil_wrb_mem(CLIC_INT_CTRL_ATTR(i), (uint8_t) ESP32C5_CLIC_ATTR_MODE_BYTE_M);
    }

    /*
     *  高分解能タイマ（target 側 systimer）の初期化は target_timer.cfg の
     *  ATT_INI（target_hrt_initialize）が行う（C6 と同じ）．P4 の mtimer/
     *  msip（CLIC 内部線 7/3）の設定は C5 では行わない．
     *
     *  割込みの許可: CLIC モードでは mie/mip は無効．割込み許可は mstatus.MIE と
     *  CLIC 個別 IE で行う（P4 と同じ．asp3 C5 の csrw mie,~0 は踏襲しない＝
     *  効くかどうかは段2 で観測．未確認）．
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
     *  コア依存の終了処理（HRT の停止は target_timer.cfg の ATT_TER）
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
