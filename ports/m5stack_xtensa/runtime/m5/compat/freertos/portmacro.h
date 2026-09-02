/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ用 FreeRTOS portmacro.h 互換ヘッダ
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  一部の ESP-IDF/M5 コードは freertos/portmacro.h を直接 include する。
 *  クリティカルセクション・portMUX_TYPE・型は FreeRTOS.h に集約済みなので
 *  それを取り込むだけ。加えて xPortGetCoreID を提供する（design.md §2）。
 */

#ifndef TOPPERS_M5_FREERTOS_PORTMACRO_H
#define TOPPERS_M5_FREERTOS_PORTMACRO_H

#include "freertos/FreeRTOS.h"

/*
 *  実行中コアの ID（0 起点）。esp_shim_get_core_id（→kernel.h の get_pid，
 *  1起点PRCIDを0起点へ変換）へ写像する（kernel.h由来のID/get_pidをC++
 *  翻訳単位から直接使うとalignof/offsetofマクロ汚染が起きるため、esp_shim
 *  経由に変更。esp_shim_public.h参照）。
 *  ★get_my_prcidx()（アーキ層のstatic inline関数）を使う実装を最初に
 *    試したが，blob構成（S3-BLE等）のesp_shim.oリンクでundefined
 *    referenceになったため get_pid()（通常のexternカーネルサービス
 *    コール）へ変更（詳細は esp_shim.c の esp_shim_get_core_id コメント）。
 *  ★M5 は原則コア0固定（SHIM_LOCK 規約）のため通常は 0 を返す。M5GFX 単体
 *    経路では未使用（M0 棚卸し）。
 */
static inline BaseType_t
xPortGetCoreID(void)
{
	return (BaseType_t) esp_shim_get_core_id();
}


/*  ★`portENTER_CRITICAL_SAFE` / `portEXIT_CRITICAL_SAFE`
 *  （ESP-IDF の I2S ドライバが使う。実測で判明）。
 *  ★IDF 版は「タスク文脈なら通常版・ISR 文脈なら ISR 版」を選ぶが、
 *  本ポートは**どちらも CPU ロック**で足りる（`loc_cpu`/`unl_cpu` はネスト可）。
 *  ★spinlock 引数は無視する（SMP の相互排除は本ポートの m5 構成では不要
 *  ——m5 は単一コアで動く。★これが変わったら見直すこと）。 */
#ifndef portENTER_CRITICAL_SAFE
#define portENTER_CRITICAL_SAFE(mux)	do { (void)(mux); loc_cpu(); } while (0)
#define portEXIT_CRITICAL_SAFE(mux)		do { (void)(mux); unl_cpu(); } while (0)
#endif


/*
 *  ★`portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR`。
 *  ★ESP-IDF の `spi_master.c` が ISR 文脈の排他に使う。
 *  ★本ポートは PRC1 単一コアで、ISR は既に割込み禁止レベルで走っているので
 *  ★**割込みレベルの上げ下げだけ**にする（spinlock は持たない）。
 *  ★`portENTER_CRITICAL_SAFE` と同じ流儀（同ファイル内の既存実装に合わせた）。
 */
#ifndef portENTER_CRITICAL_ISR
#define portENTER_CRITICAL_ISR(mux)   do { (void)(mux); portENTER_CRITICAL_SAFE(mux); } while (0)
#endif
#ifndef portEXIT_CRITICAL_ISR
#define portEXIT_CRITICAL_ISR(mux)    do { (void)(mux); portEXIT_CRITICAL_SAFE(mux); } while (0)
#endif

#endif /* TOPPERS_M5_FREERTOS_PORTMACRO_H */
