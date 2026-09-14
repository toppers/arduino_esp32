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
 *    カーネルのチップ依存部（ESP32-C6用）
 *
 *  ESP32-C3依存部（割込みマトリクス／INTMTX）をESP32-C6用に置き換えた
 *  もの．C3との相違はソースが77本（C3は62本）のため生ステータスが
 *  3ワード（STATUS0/1/2）である点のみ．高分解能タイマはSYSTIMER
 *  （ターゲット依存部のtarget_timer.[ch]）を使用するため，本ファイル
 *  ではタイマの初期化を行わない。
 */

#include "kernel_impl.h"
#include "interrupt.h"
#include <sil.h>

/*
 *  各CPU割込み線に割り当てたソースのビットマスクとFROM_CPUソース番号
 *  （esp32c6_intmtx_routeが更新する）
 */
uint32_t intmtx_srcmask[32][3];
uint8_t intmtx_from_cpu[32];

/*
 *  ペリフェラル割込みソースのCPU割込み線への割り当て
 */
void
esp32c6_intmtx_route(uint_t intsrc, INTNO intno)
{
	sil_wrw_mem((void *)(INTMTX_BASE + intsrc * 4U), (uint32_t)intno);
	intmtx_srcmask[intno][intsrc / 32U] |= 1U << (intsrc % 32U);
	if (ESP32C6_INTSRC_FROM_CPU_0 <= intsrc
			&& intsrc <= ESP32C6_INTSRC_FROM_CPU_3) {
		intmtx_from_cpu[intno] = (uint8_t)(intsrc
										- ESP32C6_INTSRC_FROM_CPU_0);
	}
}

/*
 *  割込みマトリクスの初期化
 */
void
intmtx_initialize(void)
{
	uint_t i;

	/*
	 *  全ペリフェラルソースの割り当てを解除する（CPU割込み線0へ
	 *  マップ＝線0は許可しないため実質無効）．ROMブートローダが
	 *  設定した割り当てをクリアする意味もある．
	 */
	for (i = 0U; i < ESP32C6_TNUM_INTSRC; i++) {
		sil_wrw_mem((void *)(INTMTX_BASE + i * 4U), 0U);
	}

	/*
	 *  ソース割り当ての管理テーブルの初期化
	 */
	for (i = 0U; i < 32U; i++) {
		intmtx_srcmask[i][0] = 0U;
		intmtx_srcmask[i][1] = 0U;
		intmtx_srcmask[i][2] = 0U;
		intmtx_from_cpu[i] = 0xFFU;
	}

	/*
	 *  全CPU割込み線の禁止・タイプをlevelに設定・edgeペンディングの
	 *  クリア
	 */
	sil_wrw_mem((void *)(PLICMX_BASE + PLICMX_ENABLE_OFF), 0U);
	sil_wrw_mem((void *)(PLICMX_BASE + PLICMX_TYPE_OFF), 0U);
	sil_wrw_mem((void *)(PLICMX_BASE + PLICMX_CLEAR_OFF), ~0U);
	sil_wrw_mem((void *)(PLICMX_BASE + PLICMX_CLEAR_OFF), 0U);

	/*
	 *  すべてのCPU割込み線を優先度0（決して受け付けられない）に設定
	 */
	for (i = 0U; i <= INTMTX_TNUM_INT; i++) {
		sil_wrw_mem((void *)(PLICMX_BASE + PLICMX_PRI_BASEOFF + i * 4U), 0U);
	}

	/*
	 *  割込み優先度マスクを全解除（内部表現0＝THRESH 1）に設定
	 */
	intmtx_set_thresh(0U);
}

/*
 *  チップ依存の初期化（マスタプロセッサ用）
 *  ESP32-C6 は単一コア．割込みマトリクスの初期化は chip_initialize で行う．
 */
void
chip_mprc_initialize(void)
{
}

/*
 *  チップ依存の初期化
 */
void
chip_initialize(PCB *p_my_pcb)
{
	extern void *trap_vector_table;

	/*
	 *  Machine Trap-Vector Base の設定（VECTOREDモード．
	 *  mcause&0x1fがCPU割込み線番号としてテーブルを索引する．
	 *  C3と同じ標準RISC-VベクタドモードでMTVEC_MODE_CSR=1．
	 *  esp-hal-3rdpartyのriscv/interrupt_plic.cで確認済み）
	 */
	riscv_write_mtvec((ulong_t)&trap_vector_table | MTVEC_MODE_VECTORD);

	/*
	 *  割込みマトリクスの初期化
	 */
	intmtx_initialize();

	/*
	 *  mie CSRのCPU割込み線ビットをすべて許可する
	 *
	 *  【重要な訂正】esp32c6.h冒頭のコメント（C3からの類推）は「mie/mip
	 *  へのアクセスは不正命令になる」としていたが，これは実機未検証の
	 *  誤った仮定だった。実機（ESP32-C6FH4 rev v0.2）で`csrr mie`を
	 *  実際に発行して確認したところ，不正命令例外にはならず正常に
	 *  読み出せる（値は0＝リセット直後は全ビット無効）。この結果，
	 *  PLIC_MX側（ENABLE／PRI／THRESH／EIP）をどれだけ正しく設定しても，
	 *  標準RISC-VのCSRであるmie側が全ビット0のままではCPUは一切
	 *  外部割込みトラップを起こさない，というのが
	 *  「TIMER・SIOとも実機でCPUへの割込み配送が一度も成立しない」
	 *  現象の真因だった（詳細はdocs/dev/esp32c6-target.md参照）。
	 *  QEMU限定だった従来のコードを実機でも有効にする。
	 */
	Asm("csrw mie, %0" : : "r"(~0U));

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
	 *  コア依存の終了処理
	 */
	core_terminate();
}

/*
 *  割込み要求ラインの属性の設定
 */
Inline void
intmtx_config_int(INTNO intno, ATR intatr, PRI intpri)
{
	assert(TMIN_INTNO <= intno && intno <= TMAX_INTNO);
	assert(TMIN_INTPRI <= intpri && intpri <= TMAX_INTPRI);

	/*
	 *  割込み優先度を設定
	 */
	intmtx_set_priority(intno, INT_IPM(intpri));

	/*
	 *  割込みを許可
	 */
	if ((intatr & TA_ENAINT) != 0U) {
		intmtx_enable_int(intno);
	}
}

/*
 *  割込み管理機能の初期化
 */
void
initialize_interrupt(PCB *p_my_pcb)
{
	uint_t			i;
	const INTINIB	*p_intinib;

	for (i = 0U; i < tnum_cfg_intno; i++) {
		p_intinib = &(intinib_table[i]);
		if (p_intinib->iprcid == p_my_pcb->prcid) {
			intmtx_config_int(INTNO_MASK(p_intinib->intno),
							  p_intinib->intatr, p_intinib->intpri);
		}
	}
}
