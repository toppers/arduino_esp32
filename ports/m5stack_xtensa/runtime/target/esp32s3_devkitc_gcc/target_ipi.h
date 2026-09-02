/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  プロセッサ間割込み（IPI）のターゲット依存部（ESP32-S3-DevKitC-1用）
 *  
 */

#ifndef TOPPERS_TARGET_IPI_H
#define TOPPERS_TARGET_IPI_H

#include "chip_ipi.h"

/*
 *  IPIの割込み優先度（カーネル管理割込みの最低優先度の一つ上）
 */
#define INTPRI_IPI (TMAX_INTPRI - 1)

#ifndef TOPPERS_MACRO_ONLY

/*
 *  他プロセッサへのディスパッチ／カーネル終了／HRTイベント設定要求。
 *  単一コア（TNUM_PRCID<2）では他コアが無いので何もしない。
 */
Inline void
request_dispatch_prc(uint_t prcid)
{
#if TNUM_PRCID >= 2
	chip_ipi_send(prcid, IPI_DISPATCH);
#endif /* TNUM_PRCID >= 2 */
}

Inline void
request_ext_ker(uint_t prcid)
{
#if TNUM_PRCID >= 2
	chip_ipi_send(prcid, IPI_EXT_KER);
#endif /* TNUM_PRCID >= 2 */
}

Inline void
request_set_hrt_event(uint_t prcid)
{
#if TNUM_PRCID >= 2
	chip_ipi_send(prcid, IPI_SET_HRT);
#endif /* TNUM_PRCID >= 2 */
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_TARGET_IPI_H */
