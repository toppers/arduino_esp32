/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス（plic_kernel_impl.h と同一）．無保証．
 */

/*
 *     カーネルの割込みコントローラ依存部（CLIC 用/ESP32-C5 の chip 側複製）
 *
 *  出典: fmp3_core（submodule/pristine）685b36a9
 *        arch/riscv_gcc/common/clic_kernel_impl.h の複製。
 *  fmp3_core は改変しない方針のため、C5 固有の差分はこちら（chip 層）に置く
 *  （C6 の chip_start.S と同じ作法）。chip_kernel_impl.h は共通版ではなく
 *  本ファイルを include する。共通の clic_kernel_impl.c（clic_ipm_shadow の実体、
 *  clic_context_initialize / clic_global_initialize / clic_initialize_interrupt）は
 *  無改変のまま chip.cmake がリンクし、kernel_impl.h 経由で本ファイルの
 *  Inline 関数を使う。
 *
 *  【差分】割込み優先度マスク（閾値）の read/write だけを差し替えた:
 *   - 共通版は P4 のメモリマップド CLIC_INT_THRESH（0x20800008）へ
 *     sil_wrw_mem する決め打ち（P4 rev<3 は mintthresh CSR を持たず不正命令）。
 *   - C5 は IDF v5.5.4 が標準 CLIC の CSR mintthresh（0x347）を使う
 *     （components/riscv/include/esp_private/interrupt_clic.h の
 *      INTTHRESH_STANDARD=1: 8bit の閾値バイトを CSR の [7:0] にそのまま書く。
 *      MMIO 版の [31:24] とは位置が違う）。asp3 も C5 実機で CSR 版を使った
 *     （asp3/arch/riscv_gcc/esp32c5/clic_kernel_impl.h）。
 *   - メモリマップド CLIC_INT_THRESH_REG も clic_reg.h に在るが、C5 で閾値として
 *     効くかは**未確認**（asp3 は実測 0 で未使用）。
 *   => 既定は CSR 版。TOPPERS_ESP32C5_CLIC_THRESH_MMIO（cmake の
 *      A1_C5_CLIC_THRESH_MMIO=ON）で共通版と同じ MMIO 経路に切り替えられる
 *      （段2 で 1 軸実験。効けば共通版へ戻して本複製を減らせる）。
 *  他の関数（clic_disable_int 等、CLIC_INT_CTRL の RMW）は共通版と同一。
 *  chip_support.S の irc_begin_int / irc_end_int / irc_get_intpri /
 *  irc_begin_exc / irc_end_exc も同じマクロで同じ 2 経路を持つ。
 *
 *  注: FMP3 の INTNO は CLIC 線番号と同一に扱う（esp32c5.h 参照）。
 *      割込み優先度の内部表現 pri は CTL/THRESH の 8bit 値
 *      （レベル/優先度の割付は NLBITS=3 に従う。chip_kernel_impl.h の INT_IPM）。
 */

#ifndef TOPPERS_ESP32C5_CLIC_KERNEL_IMPL_H
#define TOPPERS_ESP32C5_CLIC_KERNEL_IMPL_H

#include <sil.h>
#include "riscv.h"

/*
 *  RISC-Vコアで共通な定義
 */
#include "core_kernel_impl.h"

#ifndef TOPPERS_MACRO_ONLY

/*
 *  割込みターゲットテーブル（kernel_cfg.c, clic_kernel.py が生成）
 *    割込み要求ラインの割込み先コンテキストINDEX．未割付は0xff．
 */
extern const uint8_t clic_target_cidx_table[CLIC_TNUM_INTNO + 1];

/*
 *  IPM ソフトウェアシャドウ（共通 clic_kernel_impl.c で定義, コア別内部表現8bit）
 *    閾値レジスタの read-back が保証されない実装に備え，IPM の真値はこちらで
 *    保持する（HW 非依存の防御的設計）．
 */
extern volatile uint8_t clic_ipm_shadow[TNUM_PRCID];

/*
 *  割込み優先度マスクの設定（priは内部表現 8bit）
 *    cidx は使用しない（自コアの閾値のみ）．
 *    実マスク用に閾値へ書込みつつ，論理 IPM をシャドウへ記録する．
 */
Inline void
clic_set_context_priority(uint_t cidx, uint_t pri)
{
    (void) cidx;
#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
    sil_wrw_mem(CLIC_INT_THRESH, (pri & 0xFFU) << CLIC_INT_THRESH_SHIFT);
    (void) sil_rew_mem(CLIC_INT_THRESH);   /* read-back */
#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    Asm("csrw 0x347, %0" : : "r"(pri & 0xFFU) : "memory");   /* mintthresh */
#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
    clic_ipm_shadow[get_my_prcidx()] = (uint8_t)(pri & 0xFFU);
}

/*
 *  割込み優先度マスクの取得（内部表現で返す）
 *    閾値レジスタの read-back に依存せず，シャドウを真値として返す．
 */
Inline uint_t
clic_get_context_priority(uint_t cidx)
{
    (void) cidx;
    return((uint_t) clic_ipm_shadow[get_my_prcidx()]);
}

/*
 *  割込み禁止（CLIC_INT_CTRL の IE クリア）
 */
Inline void
clic_disable_int(INTNO intno)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val &= ~CLIC_INT_IE_BIT;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  割込み許可（CLIC_INT_CTRL の IE セット）
 */
Inline void
clic_enable_int(INTNO intno)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val |= CLIC_INT_IE_BIT;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  割込みペンディングのチェック（CLIC_INT_CTRL の IP）
 */
Inline bool_t
clic_probe_pending(INTNO intno)
{
    return((sil_rew_mem(CLIC_INT_CTRL(intno)) & CLIC_INT_IP_BIT) != 0U);
}

/*
 *  割込み要求ラインに対する割込み優先度の設定（priは内部表現, CTL[31:24]）
 */
Inline void
clic_set_priority(INTNO intno, uint_t pri)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val &= ~(0xFFUL << CLIC_INT_CTL_SHIFT);
    val |= (pri & 0xFFU) << CLIC_INT_CTL_SHIFT;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  割込み要求(ペンディング)のセット（CLIC_INT_CTRL の IP）
 *    software による割込み発生(ras_int)．IP を software で立てるには対象線が
 *    エッジtrigである必要があるため，エッジ設定とセットで使う(clic_set_edge)．
 */
Inline void
clic_raise_pending(INTNO intno)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val |= CLIC_INT_IP_BIT;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  割込み要求(ペンディング)のクリア（CLIC_INT_CTRL の IP）
 */
Inline void
clic_clear_pending(INTNO intno)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val &= ~CLIC_INT_IP_BIT;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  割込み線のトリガをエッジに設定（software で IP を立てて発生させるため）
 */
Inline void
clic_set_edge(INTNO intno)
{
    uint32_t val = sil_rew_mem(CLIC_INT_CTRL(intno));
    val |= CLIC_INT_TRIG_EDGE;
    sil_swrw_mem(CLIC_INT_CTRL(intno), val);
}

/*
 *  CLIC のコンテキスト単位の初期化（共通 clic_kernel_impl.c）
 */
extern void clic_context_initialize(PCB *p_my_pcb);

/*
 *  CLIC のグローバルな初期化（共通 clic_kernel_impl.c）
 */
extern void clic_global_initialize(void);

/*
 *  割込み管理機能の初期化（共通 clic_kernel_impl.c）
 */
extern void clic_initialize_interrupt(PCB *p_my_pcb);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_ESP32C5_CLIC_KERNEL_IMPL_H */
