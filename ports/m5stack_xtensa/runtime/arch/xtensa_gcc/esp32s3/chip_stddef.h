/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2007,2011,2015 by Embedded and Real-Time Systems Laboratory
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
 * t_stddef.hのターゲット依存部（ESP32-S3用）
 */

#ifndef TOPPERS_CHIP_STDDEF_H
#define TOPPERS_CHIP_STDDEF_H

/*
 * チップを識別するためのマクロの定義
 */
#define TOPPERS_ESP32S3 /* システム略称 */

/*
 * ROM newlib（syscall stub table）とstruct _reentのレイアウトを一致させる
 * ためのマクロ（chip_rom_libc.c参照）
 *
 * ESP32-S3のROM newlib（rand/strtol等）は struct _reent の _r48 が
 * **オフセット56**にあることを決め打ちでコンパイルされている。この配置に
 * なるのは _REENT_BACKWARD_BINARY_COMPAT が有効な場合（_reserved_0/
 * _reserved_1 の2ワードが _emergency の直後に入る）で、無効だと _r48 は
 * 48に来てROMが _cvtlen/_cvtbuf を _r48 と誤認して破壊する（無音の
 * メモリ破壊になる）。ESP-IDFはこれを components/newlib/platform_include/
 * sys/reent.h で <sys/reent.h> のinclude前に定義することで担保している。
 *
 * 本ツールチェーンは _REENT_BACKWARD_BINARY_COMPAT を**既定で有効化済み**で、
 *   定義の有無に関わらず offsetof(struct _reent, _r48)==56 になる。したがって
 *   下記は現状では冗長だが、★**ツールチェーン更新で既定が変わった場合に
 *   無音でメモリ破壊へ退行するのを防ぐ保険**として明示する
 *   （#ifndefガードによりnewlib側定義と衝突しない）。
 *
 * ★本ヘッダはt_stddef.hからtarget_stddef.h経由で最初期に読まれるため、
 *   ビルド内のどの <sys/reent.h> のincludeよりも前に評価される。
 *   <sys/reent.h> を直接includeする箇所は必ず本ヘッダの後に置くこと。
 *
 * _REENT_SDIDINITはnewlib側に定義が無いため本体をここで与える。
 * ESP-IDF（platform_include/sys/reent.h:12）と同じく _reserved_0 を使う。
 */
#ifndef _REENT_BACKWARD_BINARY_COMPAT
#define _REENT_BACKWARD_BINARY_COMPAT
#endif /* _REENT_BACKWARD_BINARY_COMPAT */
#ifndef _REENT_SDIDINIT
#define _REENT_SDIDINIT(_ptr)   ((_ptr)->_reserved_0)
#endif /* _REENT_SDIDINIT */

/*
 * コアで共通な定義
 */
#include <core_stddef.h>

#endif /* TOPPERS_CHIP_STDDEF_H */
