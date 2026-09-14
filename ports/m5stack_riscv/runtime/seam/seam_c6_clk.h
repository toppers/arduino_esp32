/*
 *  seam-C6: CPU クロックの昇圧（80MHz -> 160MHz）結果を fmp_app へ渡す構造体
 *  ==========================================================================
 *  ESP32-C6 統合 段4 Task 1（2026-09-14）。実体、一次資料は esp/boot/seam_c6_clk.c。
 *
 *  この構造体は「entry.S から toppers_start より前に書き込まれ、カーネル
 *  起動後の fmp_app タスクから読まれる」という寿命を持つ。**.bss ではなく
 *  .data に明示的に配置する**理由は seam_c6_clk.c 冒頭コメント参照
 *  （fmp3_core/arch/riscv_gcc/common/start.S の .bss クリアが
 *  hardware_init_hook の**後**、書込みの**後**に走るため、.bss に置くと
 *  ここで書いた値が toppers_start の直後にゼロクリアされて消える）。
 */
#ifndef SEAM_C6_CLK_H
#define SEAM_C6_CLK_H

#include <stdint.h>

/*
 *  結果コード（g_seam_c6_clk_result.rc）
 *    0 = NOT_RUN     seam_c6_clk_set() が呼ばれていない（80MHz ビルド、
 *                     SEAM_C6_CLK_BOOST 未定義。既定の .data 初期値でもある）
 *    1 = OK           昇圧を書き込み、読み戻しで確認できた
 *    2 = SKIP_NOT_PLL PCR_SYSCLK_CONF の SOC_CLK_SEL が SPLL でなかったので
 *                      何も書かずに戻った（ブートローダが PLL を残す前提が
 *                      崩れている場合の安全側）
 *    3 = VERIFY_NG     書き込んだのに読み戻しが期待値と一致しなかった
 */
#define SEAM_C6_CLK_RC_NOT_RUN      0U
#define SEAM_C6_CLK_RC_OK           1U
#define SEAM_C6_CLK_RC_SKIP_NOT_PLL 2U
#define SEAM_C6_CLK_RC_VERIFY_NG    3U

typedef struct {
	uint32_t sysclk_conf_before;
	uint32_t cpu_freq_conf_before;
	uint32_t sysclk_conf_after;
	uint32_t cpu_freq_conf_after;
	uint32_t rc;
} seam_c6_clk_result_t;

/*
 *  .data 常駐（NOT .bss。理由は本ファイル冒頭コメント参照）。
 *  既定値はすべて 0（= NOT_RUN）。entry.S から SEAM_C6_CLK_BOOST 定義時のみ
 *  seam_c6_clk_set() が呼ばれてここへ実測値を書く。
 */
extern seam_c6_clk_result_t g_seam_c6_clk_result;

/*
 *  toppers_start より前（.bss/.data 初期化より前）に entry.S から呼ぶ。
 *  グローバル変数の読み書きは g_seam_c6_clk_result（.data 配置）のみに限定する
 *  （他のグローバルはこの時点でまだ正しく初期化されていない）。
 */
extern void seam_c6_clk_set(void);

#endif /* SEAM_C6_CLK_H */
