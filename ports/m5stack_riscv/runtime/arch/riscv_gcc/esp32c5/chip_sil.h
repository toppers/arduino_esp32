/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 * 
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: core_sil.h 178 2019-10-08 13:55:00Z ertl-honda $
 */

/*
 *      sil.hのチップ依存部（ESP32-C5 用）
 *
 *  出典: fmp3/arch/riscv_gcc/esp32p4/chip_sil.h（P4）。sil_get_pid（mhartid+1）は
 *  同じ。C6 の chip_sil.h に在るビット操作マクロ（sil_orw 等。target 側の
 *  WDT 無効化が使う）と esp32c5.h の include を足した。
 *
 *  このヘッダファイルは，target_sil.h（または，そこからインクルードさ
 *  れるファイル）のみからインクルードされる．他のファイルから直接イン
 *  クルードしてはならない．
 */

#ifndef TOPPERS_CHIP_SIL_H
#define TOPPERS_CHIP_SIL_H

#include <t_stddef.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 *  プロセッサIDの取得
 */
Inline void
sil_get_pid(ID *p_prcid)
{
    ulong_t val;
    Asm("csrr %0, mhartid" : "=r"(val));
    /*
     *  FMP3 の prcid は 1 オリジン(PRC1=1)．ESP32-C5 は hart0 のみ = PRC1．
     *  asm の my_pid とも一致．
     */
    *p_prcid = (ID)val + 1;
}

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  コア依存部で共通な定義
 */
#include "core_sil.h"

/*
 *  ビット操作（read-modify-write。C6 と同じ）
 */
#define sil_mskb( mem, val, msk ) sil_wrb_mem(mem, (sil_reb_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orb( mem, val )  sil_wrb_mem(mem, sil_reb_mem(mem) | (val))
#define sil_andb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & (val))
#define sil_clrb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & ~(val))
#define sil_mskh( mem, val, msk ) sil_wrh_mem(mem, (sil_reh_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orh( mem, val )  sil_wrh_mem(mem, sil_reh_mem(mem) | (val))
#define sil_andh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & (val))
#define sil_clrh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & ~(val))
#define sil_mskw( mem, val, msk ) sil_wrw_mem(mem, (sil_rew_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orw( mem, val )  sil_wrw_mem(mem, sil_rew_mem(mem) | (val))
#define sil_andw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & (val))
#define sil_clrw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & ~(val))

#include "esp32c5.h"

#endif /* TOPPERS_CHIP_SIL_H */
