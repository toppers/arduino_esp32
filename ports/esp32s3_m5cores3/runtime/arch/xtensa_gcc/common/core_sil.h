/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
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
 *  @(#) $Id: core_sil.h 289 2021-08-05 14:44:10Z ertl-komori $
 */

/*
 *   sil.hのコア依存部（Xtensa用）
 *
 *  本ファイルはFMP3のarm_m_gcc（Cortex-M）移植を土台に、esp32_s3プロジェクト
 *  。
 *
 *  Xtensaの割込みロックは`RSIL`命令（read-and-set PS.INTLEVEL、アトミック）
 *  で行う。全割込み禁止＝PS.INTLEVELを15（最高レベル）に設定する。Xtensaの
 *  NMI（レベル7の"non-maskable"扱いとなる実装が多い。ESP32-S3のNMI相当は
 *  Debug例外等、通常のINTLEVELマスクの対象外）はこの操作でも禁止されない
 *  ため、ARM-Mの「NMIを除くすべての割込みの禁止」と同じ意味になる。
 *  参考：ESP-IDFの`xtruntime.h`の`XTOS_SET_INTLEVEL`/`XTOS_DISABLE_ALL_INTERRUPTS`
 *  マクロと同じ`rsil`/`wsr.ps`+`rsync`命令列（要ではなく、独自に最小実装する
 *  ことでESP-IDFヘッダへの依存を避ける）。
 */
#ifndef TOPPERS_CORE_SIL_H
#define TOPPERS_CORE_SIL_H

#ifndef TOPPERS_MACRO_ONLY

#ifndef TECSGEN

#ifdef TOPPERS_S3_BT_L3LAT_DIAG
/*
 *  ★BT-4診断計装（既定OFF）：rsil>=3マスク窓の計測フック。
 *  実体はesp/shim/esp_shim.c（core_kernel_impl.hのlock_cpu/unlock_cpu
 *  にも同じフックあり。設計メモはesp_shim.c冒頭の計装コメント参照）。
 */
extern void l3ld_lock_hook(uint32_t oldps, uint32_t ra);
extern void l3ld_unlock_hook(uint32_t newps);
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */

/*
 *  すべての割込みの禁止（PS.INTLEVELを15に設定し，元の値を返す）
 */
Inline uint32_t
TOPPERS_disint(void)
{
	uint32_t val;

	Asm("rsil  %0, 15" : "=a"(val) :: "memory");
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_lock_hook(val, (uint32_t) __builtin_return_address(0));
#endif
	return (val);
}

/*
 *  割込みロック状態の復帰（PS.INTLEVELを指定値に戻す）
 */
Inline void
TOPPERS_enaint(uint32_t oldps)
{
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_unlock_hook(oldps);
#endif
	Asm("wsr.ps  %0 ; rsync" :: "a"(oldps) : "memory");
}

#endif

/*
 *  全割込みロック状態の制御
 */
#define SIL_PRE_LOC      uint32_t  TOPPERS_locked
#define SIL_LOC_INT()    ((void)(TOPPERS_locked = TOPPERS_disint()))
#define SIL_UNL_INT()    (TOPPERS_enaint(TOPPERS_locked))

#define TOPPERS_SIL_WRITE_SYNC()	Asm("memw")

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CORE_SIL_H */
