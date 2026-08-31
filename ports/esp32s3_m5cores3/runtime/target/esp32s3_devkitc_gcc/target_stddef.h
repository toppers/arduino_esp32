/*
 *  t_stddef.h のターゲット依存部（ESP32-S3-DevKitC-1 / FMP3 用）
 */

#ifndef TOPPERS_TARGET_STDDEF_H
#define TOPPERS_TARGET_STDDEF_H

/*
 *  ターゲットを識別するためのマクロの定義
 */
#define TOPPERS_ESP32S3_DEVKITC        /* システム略称 */

/*
 *  開発環境で共通な定義
 */
#ifndef TOPPERS_MACRO_ONLY
#include <stdint.h>
#endif /* TOPPERS_MACRO_ONLY */
#include "tool_stddef.h"

/*
 *  チップで共通な定義
 */
#include <chip_stddef.h>

/*
 *  アサーションの失敗時の実行中断処理
 */
#ifndef TOPPERS_MACRO_ONLY
Inline void
TOPPERS_assert_abort(void)
{
	while (1) ;
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_TARGET_STDDEF_H */
