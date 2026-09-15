/*
 *  seam-C5: CPU クロックの昇圧（80MHz -> 240MHz、分周 3 -> 1）結果の受け渡し
 *  ==========================================================================
 *  ESP32-C5 統合 計画 3 段4 Task 1（2026-09-16）。型は esp/boot/seam_c6_clk.h
 *  （C6 段4 Task 1）。実体、一次資料は esp/boot/seam_c5_clk.c。
 *
 *  C6 との違い（本ヘッダの範囲）:
 *   - C6 は 80/160 どちらのビルドでも本構造体を fmp_app が読む（80 では
 *     rc=NOT_RUN が対照）。C5 は **240 のビルドだけ**が seam_c5_clk.c を
 *     リンクし、結果は esp/boot/seam_c5_clk.cfg の報告タスク
 *     （seam_c5_clk_report_task）が syslog に出す。80 のビルドは本ヘッダも
 *     seam_c5_clk.c も見ない（段3 の像を 1 バイトも変えないため。AC-1a/1e）。
 *   - ahb_freq_conf と bus_clk_update の待ち回数も記録する（C5 は分周の
 *     書換え後に PCR_BUS_CLK_UPDATE のパルスが要る。seam_c5_clk.c 参照）。
 *
 *  構造体は「entry から toppers_start より前に書き込まれ、カーネル起動後の
 *  タスクから読まれる」寿命を持つので **.data に明示配置する**（.bss だと
 *  start.S の .bss クリアで消える。理由は seam_c6_clk.c 冒頭と同じ）。
 */
#ifndef SEAM_C5_CLK_H
#define SEAM_C5_CLK_H

#include <stdint.h>

/*
 *  結果コード（g_seam_c5_clk_result.rc）
 *    0 = NOT_RUN         seam_c5_clk_set() が呼ばれていない（.data 初期値）
 *    1 = OK              分周を書き、bus_clk_update が自己クリアし、読み戻しが一致
 *    2 = SKIP_NOT_PLL240 PCR_SYSCLK_CONF の SOC_CLK_SEL が PLL_F240M(3) でない
 *                        ので何も書かずに戻った（bootloader が渡す前提が
 *                        崩れている場合の安全側）
 *    3 = VERIFY_NG       書いた値の読み戻しが一致しなかった
 *    4 = BUSUPD_TIMEOUT  PCR_BUS_CLK_UPDATE bit0 が上限回数内に 0 へ戻らなかった
 *                        （分周は書いたまま。ticks_per_us は更新しない）
 */
#define SEAM_C5_CLK_RC_NOT_RUN          0U
#define SEAM_C5_CLK_RC_OK               1U
#define SEAM_C5_CLK_RC_SKIP_NOT_PLL240  2U
#define SEAM_C5_CLK_RC_VERIFY_NG        3U
#define SEAM_C5_CLK_RC_BUSUPD_TIMEOUT   4U

typedef struct {
	uint32_t sysclk_conf_before;
	uint32_t cpu_freq_conf_before;
	uint32_t ahb_freq_conf_before;
	uint32_t sysclk_conf_after;
	uint32_t cpu_freq_conf_after;
	uint32_t ahb_freq_conf_after;
	uint32_t busupd_wait;		/* bus_clk_update の自己クリアを待ったループ回数 */
	uint32_t rc;
} seam_c5_clk_result_t;

extern seam_c5_clk_result_t g_seam_c5_clk_result;

/*
 *  toppers_start より前（.bss/.data 初期化より前）に entry から呼ぶ。
 *  グローバル変数の読み書きは g_seam_c5_clk_result（.data 配置）のみ。
 */
extern void seam_c5_clk_set(void);

/*
 *  報告タスク（esp/boot/seam_c5_clk.cfg が CRE_TSK する。tick タスクより低い
 *  優先度で 1 回だけ syslog して終わる）。
 */
#define SEAM_C5_CLK_REPORT_PRIORITY   11		/* fmp_app の TICK_PRIORITY(10) の次 */
#define SEAM_C5_CLK_REPORT_STACK_SIZE 1024
#ifndef TOPPERS_MACRO_ONLY
extern void seam_c5_clk_report_task(intptr_t exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* SEAM_C5_CLK_H */
