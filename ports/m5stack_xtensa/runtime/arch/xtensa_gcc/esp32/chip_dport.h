/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  DPORTレジスタSMP安全読み出し（無印ESP32 / Xtensa LX6用）
 *  （非公開作業記録/20260714-esp32-classic-wifi-bt/ W0-4）
 *
 *  ■DPORT読みエラッタ（ESP32 ECO / silicon rev v0.0/v1.0で顕著）
 *  無印ESP32(classic)のDPORTペリフェラル（0x3FF00000基点）を両コアが同時に
 *  読むと不正値（他方のコアのAPBアクセスと衝突しランダムに誤読）を返す。
 *  シングルコア運用中は他コアが停止しているため回避できていたが、W0で
 *  デュアルコアSMP化すると顕在化しうる。
 *
 *  ■対策（ESP-IDF esp_dport_access_reg_read 相当、pre-read APB方式）
 *  対象DPORT読みの直前に「既知のAPBレジスタ（UART0 = 0x3FF40078）を1回読む」
 *  ことで両コアのアクセスを同期させ、誤読を防ぐ。読み出しシーケンス中は
 *  rsilで割込みをマスクし、APB pre-read と DPORT read の間に他割込みが
 *  割り込まないようにする（ESP-IDF components/soc/esp32/dport_access.c の
 *  esp_dport_access_reg_read をそのまま移植）。
 *
 *  ■W0での適用範囲（重要）
 *  W0のSMP経路（chip_ipi.c／per-core tick／割込みディスパッチ）は
 *   - IPI(FROM_CPU)レジスタは**書きのみ**（DPORT書きはエラッタ対象外）
 *   - コア識別はPRID特殊レジスタ（rsr.prid、内蔵・DPORT非経由）
 *   - 割込みpendingはINTERRUPT特殊レジスタ（rsr.interrupt、内蔵・DPORT非経由）
 *  であり、実行時に**両コア同時のDPORT読みは発生しない**（本ファイル冒頭の
 *  実装ノート・design.md参照）。クロック昇圧（software_init_hook）のDPORT
 *  読み書きはAPP_CPU起動前＝シングルコア文脈のため対象外。
 *  本ヘルパは、W1以降（Wi-Fi）でSMP動作中にDPORTレジスタを読む必要が生じた
 *  箇所すべてから使うための、W0で導入する本格対策の共通機構である。
 */

#ifndef TOPPERS_CHIP_DPORT_H
#define TOPPERS_CHIP_DPORT_H

#ifndef TOPPERS_MACRO_ONLY

#include <t_stddef.h>

/*
 *  DPORT読みエラッタ回避レベル。ESP-IDF SOC_DPORT_WORKAROUND_DIS_INTERRUPT_LVL
 *  相当（=カーネル管理割込みを一時マスクできる十分高いレベル）。本ポートは
 *  Level-1(tick/IPI)・Level-3(BT)を使うため、シーケンス中は両方をマスクする
 *  レベル(=5)を下のrsilに直書きしている（NMI(Level-6/7)はマスクしない）。
 *  rsilは即値レベルしか取れず、本ツリーは他所でもrsilにリテラルを直書き
 *  している（start.S / core_sil.h の rsil a2,15）ため、それに倣う。
 */

/*
 *  DPORTレジスタのSMP安全1ワード読み出し。
 *  regはDPORT空間(0x3FF00000〜)のレジスタアドレス。
 */
Inline uint32_t
chip_dport_read(uint32_t reg)
{
#if TNUM_PRCID >= 2
	uint32_t apb;
	uint32_t lvl;

	__asm__ __volatile__ (
		"rsil  %[LVL], 5\n"		/* Level-1/3割込みをマスク */
		"movi  %[APB], 0x3ff40078\n"	/* 既知のAPBレジスタ(UART0)をpre-read */
		"l32i  %[APB], %[APB], 0\n"
		"l32i  %[REG], %[REG], 0\n"	/* 対象DPORTレジスタを読む */
		"wsr   %[LVL], ps\n"
		"rsync\n"
		: [APB] "=&a" (apb), [REG] "+a" (reg), [LVL] "=&a" (lvl)
		:
		: "memory"
	);
	return reg;
#else /* TNUM_PRCID >= 2 */
	/* シングルコアではエラッタ非該当。素の読み出し */
	return *((volatile uint32_t *) (uintptr_t) reg);
#endif /* TNUM_PRCID >= 2 */
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CHIP_DPORT_H */
