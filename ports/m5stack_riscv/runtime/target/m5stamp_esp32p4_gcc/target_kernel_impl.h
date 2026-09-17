/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
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
 *  $Id: target_kernel_impl.h 343 2023-04-19 13:51:34Z ertl-honda $
 */

/*
 *    カーネルのターゲット依存部に関する定義（M5Stamp ESP32-P4 用）
 *
 *  カーネルのターゲット依存部のヘッダファイル．kernel_impl.hのターゲッ
 *  ト依存部の位置付けとなる．
 */

#ifndef TOPPERS_TARGET_KERNEL_IMPL_H
#define TOPPERS_TARGET_KERNEL_IMPL_H

/*
 *  ターゲットのハードウェア資源の定義
 *
 *  ボード差は kit.h を並べて吸収する（段E・2026-08-15。「1 ターゲット +
 *  ボードマクロ」＝ボード計画 判断点 iii-b-1）。各 kit.h は自分のボードマクロ
 *  （M5STAMP_ESP32P4 / UNIT_POE_P4 / M5STACK_TAB5）で全体を #ifdef している
 *  ので、選ばれなかった側は**空に展開される**＝既定（M5Stamp）の前処理結果は不変。
 */
#include "m5stamp_esp32p4_kit.h"
#include "unit_poe_p4_kit.h"
#include "tab5_kit.h"

/*
 *  チップ依存部（ESP32-P4 用）
 */
#include "chip_kernel_impl.h"

#ifndef TOPPERS_MACRO_ONLY

/*
 *  ターゲットシステム依存の初期化（マスタのみ）
 */
extern void	target_mprc_initialize(void);

/*
 *  ターゲットシステム依存の初期化
 */
extern void	target_initialize(PCB *p_my_pcb);

/*
 *  ターゲットシステムの終了
 *
 *  システムを終了する時に使う．
 */
extern void	target_exit(void) NoReturn;

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_TARGET_KERNEL_IMPL_H */
