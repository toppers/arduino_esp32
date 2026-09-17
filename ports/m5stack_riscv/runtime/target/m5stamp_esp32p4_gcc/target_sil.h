/*
 *    sil.hのターゲット依存部（Plarfire SoC Kit用）
 *
 *  このヘッダファイルは，sil.hからインクルードされる．他のファイルから
 *  直接インクルードすることはない．このファイルをインクルードする前に，
 *  t_stddef.hがインクルードされるので，それに依存してもよい．
 * 
 *  $Id: target_sil.h 166 2019-08-28 07:54:41Z ertl-honda $
 */

#ifndef TOPPERS_TARGET_SIL_H
#define TOPPERS_TARGET_SIL_H

/*
 *  プロセッサのエンディアン
 */
#define SIL_ENDIAN_LITTLE

/*
 *  チップ依存部で共通な定義
 */
#include "chip_sil.h"

#endif /* TOPPERS_TARGET_SIL_H */
