/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2007,2011,2015 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．（条件略）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 * sil.hのチップ依存部（ESP32-S3用）
 *
 * 本作業単位は単一コアのみ対象。RP2350版にあったSIOハードウェア
 * スピンロック関連（TNUM_PRCID>=2分岐）はSMP対応の別作業単位で
 * cross-core interrupt方式に合わせて再実装する。
 */

#ifndef TOPPERS_CHIP_SIL_H
#define TOPPERS_CHIP_SIL_H

/*
 * プロセッサのインディアン定義（Xtensa/ESP32-S3はリトルエンディアン）
 */
#define SIL_ENDIAN_LITTLE

/*
 * プロセッサで共通な定義
 */
#include <core_sil.h>

/*
 * 一般共通レジスタ操作関数（read-modify-writeで実現。
 * RP2350のようなアトミックエイリアスはESP32-S3には無い）
 */
#define sil_orb( mem, val )  sil_wrb_mem(mem, sil_reb_mem(mem) | (val))
#define sil_andb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & (val))
#define sil_clrb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & ~(val))
#define sil_orh( mem, val )  sil_wrh_mem(mem, sil_reh_mem(mem) | (val))
#define sil_andh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & (val))
#define sil_clrh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & ~(val))
#define sil_orw( mem, val )  sil_wrw_mem(mem, sil_rew_mem(mem) | (val))
#define sil_andw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & (val))
#define sil_clrw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & ~(val))

#ifndef TOPPERS_MACRO_ONLY

/*
 * プロセッサIDの取得（1オリジン）。単一コアのため常に1。
 */
Inline void
sil_get_pid(ID *p_prcid)
{
	/*
	 * 自プロセッサのPRCID（1オリジン：PRC1/PRC2）を返す。XtensaのPRID
	 * bit13でコア識別（core0=0→PRC1, core1=1→PRC2）。syslog等がログを
	 * 記録したコアのタグ付けに使う（マルチコアで必須）。PRIDを直接読み
	 * include順に依存しない。単一コアでも常にPRC1(=1)を返し従来と等価。
	 */
	uint32_t id;
	Asm("rsr.prid %0 \n\t"
	    "extui %0, %0, 13, 1" : "=a"(id));
	*p_prcid = (ID)(id + 1);
}

/*
 * SIL スピンロック
 *
 *  - 単一プロセッサ(TNUM_PRCID<2)：スピンロックは割込みロックと等価。
 *  - マルチプロセッサ(TNUM_PRCID>=2)：割込みロックに加え、専用のS32C1I
 *    スピンロック sil_spn_lock（chip_kernel_impl.c、内蔵SRAM）を取得して
 *    SIL臨界区間をコア間排他する（syslog等の低レベルUART出力がコア間で
 *    混ざるのを防ぐ）。acquire_lock/sil_spn_lockは使用地点(kernel の *.c)で可視。
 */
#if TNUM_PRCID >= 2
/*
 * SILスピンロックはsyssvc（syslog.c等、chip_kernel_impl.hを取り込まない
 * ファイル）からも使われるため、ここで自己完結的にS32C1Iで実装する
 * （カーネル内部のacquire_lock等には依存しない）。実体はchip_kernel_impl.c。
 */
extern volatile uint32_t sil_spn_lock;	/* 0=空き（内蔵SRAM） */

/*
 * 2026-08-06（フェーズ1 1-3）: CAS実体を `xtensa_cas.h` の
 * `xtensa_core_cas()` へ1本化（詳細・不変条件は同ヘッダ）。
 */
#include <xtensa_cas.h>

Inline void
TOPPERS_sil_acq_spn(void)
{
	uint32_t id;
	Asm("rsr.prid %0" : "=a"(id));
	while (!xtensa_core_cas(&sil_spn_lock, 0U, id)) {
		/*  他コアが保持中。呼出し元はSIL_LOC_INT()で自コアの割込みを
		 *  既に禁止済み（SIL_LOC_SPN()マクロ経由）なので待ちは有界。 */
	}
	Asm("memw" ::: "memory");
}

Inline void
TOPPERS_sil_rel_spn(void)
{
	Asm("memw" ::: "memory");
	sil_spn_lock = 0U;
}

#define SIL_LOC_SPN()  do { SIL_LOC_INT(); TOPPERS_sil_acq_spn(); } while (0)
#define SIL_UNL_SPN()  do { TOPPERS_sil_rel_spn(); SIL_UNL_INT(); } while (0)
#else /* TNUM_PRCID >= 2 */
#define SIL_LOC_SPN()  SIL_LOC_INT()
#define SIL_UNL_SPN()  SIL_UNL_INT()
#endif /* TNUM_PRCID >= 2 */

/*
 * SIL スピンロックの強制解放（単一プロセッサでは何もしない）
 */
Inline void
TOPPERS_sil_force_unl_spn(void)
{
}

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CHIP_SIL_H */
