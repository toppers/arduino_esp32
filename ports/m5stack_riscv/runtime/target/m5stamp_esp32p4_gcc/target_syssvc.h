/*
 *    システムサービスのターゲット依存部（Plarfire SoC Kit用）
 *
 *  システムサービスのターゲット依存部のヘッダファイル．システムサービ
 *  スのターゲット依存の設定は，できる限りコンポーネント記述ファイルで
 *  記述し，このファイルに記述するものは最小限とする．
 * 
 *  $Id: target_syssvc.h 334 2023-04-14 07:39:43Z ertl-honda $
 */

#ifndef TOPPERS_TARGET_SYSSVC_H
#define TOPPERS_TARGET_SYSSVC_H

#ifdef TOPPERS_OMIT_TECS

/*  ボード差の吸収は target_kernel_impl.h 冒頭のコメント参照（段E・2026-08-15）  */
#include "m5stamp_esp32p4_kit.h"
#include "unit_poe_p4_kit.h"
#include "tab5_kit.h"

/*
 *  シリアルインタフェースドライバを実行するクラスの定義
 */
#define CLS_SERIAL  CLS_PRC1

/*
 *  システムログの低レベル出力のための文字出力
 *
 *  ターゲット依存の方法で，文字cを表示/出力/保存する．
 */
extern void target_fput_log(char c);

/*
 *  サポートするシリアルポートの数
 */
#define TNUM_PORT  1

/*
 *  ESP32-P4 はコンソールに UART0 を使用（U0TXD=GPIO37/U0RXD=GPIO38）。
 *  Step3 初手はポーリング送信のみのため SIO 割込みは登録しない。
 *  下記 INTNO_SIO/INTPRI_SIO 等は割込み駆動化（後続作業）まで未使用。
 */
#define SIO0_BASE   UART0_BASE

/*
 *  SIO割込みを登録するための定義（割込み駆動化時に使用, 暫定値）
 *  INTNO_SIO は CLIC の有効な線番号(1..47)に収める必要がある。
 */
#define INTNO_SIO   16                /* SIO割込み番号(CLIC外部線16=最初の外部線, USB src22を割付) */
#define ISR_SIO     1                 /* SIO ISR の ID */
#define ISRPRI_SIO  1                 /* SIO ISR優先度 */
#define INTPRI_SIO  (-4)             /* SIO割込み優先度(内部表現, CLIC level4) */
#define INTATR_SIO  TA_ENAINT        /* SIO割込み属性(起動時に許可) */

/*
 *  低レベル出力で使用するSIOポート
 */
#define SIOPID_FPUT  1

/*
 *  トレースログに関する設定
 */
#ifdef TOPPERS_ENABLE_TRACE
#include "arch/tracelog/trace_log.h"
#endif /* TOPPERS_ENABLE_TRACE */

#endif /* TOPPERS_OMIT_TECS */

/*
 *  コアで共通な定義（チップ依存部は飛ばす）
 */
#include "core_syssvc.h"

/*
 *  ESP32-P4：実行時間分布集計（histogram）のサイクル取得を mcycle へ上書き
 *
 *  core_syssvc.h の既定 HIST_GET_TIM は riscv_get_pmcnt＝rdcycle/rdcycleh
 *  （ユーザサイクル CSR 0xC00/0xC80）を読む．ESP32-P4 の HP コアでは IDF も含め
 *  機械サイクル mcycle（0xB00）を用いており，ユーザサイクル（0xC00）の M モード
 *  可用性が不確実なため，確実な mcycle を直読みする（cycle と mcycle は同一の物理
 *  サイクルカウンタで値は等価）．上位は mcycleh（0xB80）で読み，RV32 の桁上がりを
 *  跨ぐ読みを wrap-safe にする．時間変換 HIST_CONV_TIM＝count*10/CORE_CLK_MHZ
 *  （=360）は既定のまま（0.1us 単位）．サイクルカウンタのみで perf 計測に十分．
 */
#ifndef TOPPERS_MACRO_ONLY
Inline void
esp32p4_hist_get_mcycle(PMCNT *p_count)
{
    uint32_t lo, hi, hi2;

    do {
        Asm("csrr %0, 0xB80" : "=r"(hi));   /* mcycleh（上位32bit）*/
        Asm("csrr %0, 0xB00" : "=r"(lo));   /* mcycle （下位32bit）*/
        Asm("csrr %0, 0xB80" : "=r"(hi2));  /* mcycleh（再読み）   */
    } while (hi != hi2);
    *p_count = (((PMCNT) hi) << 32) | (PMCNT) lo;
}
#endif /* TOPPERS_MACRO_ONLY */

#undef  HIST_GET_TIM
#define HIST_GET_TIM(p_time)    esp32p4_hist_get_mcycle(p_time)

#endif /* TOPPERS_TARGET_SYSSVC_H */
