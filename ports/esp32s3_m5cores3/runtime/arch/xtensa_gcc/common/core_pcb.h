/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2009-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)～(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．（条件略）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 *		pcb.hのコア依存部（Xtensa用）
 *
 *  ARM-M版と異なり、CPUロック状態はソフトウェアフラグではなく
 *  PS.INTLEVELレジスタで直接表現する（core_kernel_impl.hのlock_cpu/
 *  unlock_cpu参照）ため、TPCBにロック関連フィールドは持たない。
 */

#ifndef TOPPERS_CORE_PCB_H
#define TOPPERS_CORE_PCB_H

#ifndef TOPPERS_MACRO_ONLY

/*
 *  Xtensa依存プロセッサコントロールブロック
 *
 *  アイドル処理用のスタック（コンテキストスイッチ実装時に使用予定）
 */
typedef struct target_processor_control_block {
	STK_T* idstkpt;
	STK_T* idstktop;
	volatile bool_t lock_flag; /* cfgツールの汎用オフセット診断テーブル
	                            * 生成のため必要。現時点では未使用
	                            * （CPUロック状態はPS.INTLEVELで直接表現） */
} TPCB;

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CORE_PCB_H */
