/*
 *    t_stddef.hのターゲット依存部（Plarfire SoC Kit用）
 *
 *  このヘッダファイルは，t_stddef.hの先頭でインクルードされる．他のファ
 *  イルからは直接インクルードすることはない．他のヘッダファイルに先立っ
 *  て処理されるため，他のヘッダファイルに依存してはならない．
 * 
 *  $Id: target_stddef.h 166 2019-08-28 07:54:41Z ertl-honda $
 */

#ifndef TOPPERS_TARGET_STDDEF_H
#define TOPPERS_TARGET_STDDEF_H

/*
 *  ターゲットを識別するためのマクロの定義
 */
#define TOPPERS_M5STAMP_ESP32P4_KIT    /* システム略称 */

/*
 *  チッブで共通な定義
 */
#include "chip_stddef.h"

/*
 *  アサーションの失敗時の実行中断処理
 */
#ifndef TOPPERS_MACRO_ONLY

Inline void
TOPPERS_assert_abort(void)
{
    while(1);    /* trueの定義前なので，1と記述する */
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_TARGET_STDDEF_H */
