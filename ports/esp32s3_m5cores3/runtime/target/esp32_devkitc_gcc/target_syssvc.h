/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2007-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．（条件略）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 * システムサービスのターゲット依存部（RP2350用）
 */

#ifndef TOPPERS_TARGET_SYSSVC_H
#define TOPPERS_TARGET_SYSSVC_H

#ifdef TOPPERS_OMIT_TECS

/*
 * 起動メッセージのターゲットシステム名
 */
#define TARGET_NAME "ESP32-DevKitC (Xtensa LX6)"

/*
 * シリアルインタフェースドライバを実行するクラス
 */
#define CLS_SERIAL CLS_PRC1

/*
 * システムログの低レベル出力のための文字出力
 */
extern void target_fput_log(char c);

/*
 * 低レベル出力で使用するSIOポート
 */
#define SIOPID_FPUT 1

#endif /* TOPPERS_OMIT_TECS */

/*
 * チップ共有のハードウェア資源の読み込み
 */
#include "chip_syssvc.h"

#endif /* TOPPERS_TARGET_SYSSVC_H */
