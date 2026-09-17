/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  SDMMC ホストドライバ（外付け ESP32-C6 / esp-hosted スレーブ向け）実装
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ============================================================================
 *  出典と改変（要旨。全数は `esp/p4sdio/IMPORT_PROVENANCE.md`）
 *  ============================================================================
 *  出典: P4 repo `~/TOPPERS/ESP32/esp32_p4`（**読み取り専用**）
 *        `wifi_p4_module/sdio_host/fmp3_sdmmc.c`（1249 行）
 *
 *  移植元の設計（そのまま引き継ぐ）:
 *    - **実装の唯一の参照**は同 repo `sdio_host/sdmmc_ll_crib_sheet.md`
 *      （IDF v5.5 実ソースからの抽出）。IDF は**ヘッダのみ**利用する
 *      （`hal/sdmmc_ll.h` の inline LL 関数・レジスタ構造体・SD プロトコル定数）。
 *      **IDF の .c / FreeRTOS / ROM 関数は使わない。**
 *    - ISR は MINTSTS/IDSTS を W1C し、ビットを単調 OR で蓄積して
 *      `P4SDIO_SEM_EVT` に `isig_sem` する（蓄積は次トランザクション開始まで
 *      不変なのでロックフリーで安全）。
 *    - SDIO カード割込み(IO_SLOT1)は自己マスクして `P4SDIO_SEM_IO` に通知。
 *    - トランザクションは `P4SDIO_MTX` で直列化。
 *
 *  本 repo での改変（**申告制**。これ以外は移植元と同じ振る舞いを意図する）:
 *    D-1  ピン/スロットを `p4sdio_pins.h` へ外出し（＝本段の主目的・申し送り iii-c）
 *    D-2  **GPIO 32 未満に対応**。移植元は `enable1_w1ts` / `1<<(gpio-32)` を
 *         直書きしており **GPIO>=32 でしか動かない**。Tab5 は G8-G15 なので
 *         そのままでは**別の pad を叩く**（静かに壊れる型の欠陥）。
 *         下の `gpio_out_enable()` / `gpio_out_set()` が両バンクを扱う。
 *    D-3  割込み確保を **C-1 CLIC シムの `esp_intr_alloc()`** へ変更。
 *         移植元は割込みマトリクスへ直書きし、自前の `CFG_INT(47)`/`CRE_ISR` を
 *         持っていた（IDF の動的アロケータと線 17 を取り合った経緯の産物）。
 *         seam には IDF のアロケータが居らず、線の管理は
 *         `esp/shim/esp_shim_intr_clic_lines.h` の利用者表が正本である
 *         （SDMMC の行は既に 1 本計上されている）。⇒ **表に従う**。
 *         これに伴い `esp_intr_reserve()` の 2 回の呼出しは**落とした**
 *         （IDF のアロケータが居ないので予約する相手が居ない）。
 *    D-4  カーネルオブジェクト ID を `SDMMC_*` → `P4SDIO_*` へ改名
 *         （本 repo の `esp/p4sdio/p4sdio.cfg` が生成する）。
 *    D-5  パッドのドライブ強度（IO_MUX FUN_DRV）を構成値化（Tab5 は 0）。
 *    D-6  デバッグ出力を `syslog()` から **同期出力（target_fput_log 直呼び）** へ。
 *         実機診断で `syslog` を根拠にしない（logtask が回らないと出ない）。
 *    D-7  `SDMMC_ISR_DIAG` ブロック（移植元で `0` に固定された死にコード。
 *         ISR 内で USB-CDC をポーリングする危険な診断）を**丸ごと落とした**。
 *    D-8  スレーブのリセット/電源は `p4sdio_board.c` へ分離
 *         （ボードごとに電源経路が違うため。移植元は GPIO 直叩き 1 種類だけ）。
 *    D-9  診断カウンタ（`p4sdio_n_isr` 等）を追加。
 *    D-14 **CMD52/CMD53 の R5 応答フラグ（エラービット）を数える**
 *         （`p4sdio_n_r5_err` / `p4sdio_last_r5_flags`）。移植元も本 repo も
 *         R5 のフラグを一度も見ておらず、カードが `FUNCTION_NUMBER` /
 *         `OUT_OF_RANGE` / `COM_CRC_ERROR` 等を立てても**ホストは E_OK を返す**
 *         （段 7a §6-4 が「潜在バグ」として記録）。**本 D-n は直さない。数えるだけ。**
 *         **戻り値は変えない**（変えると 7a〜7g の全判定の意味が変わる）。
 *    D-15 `p4sdio_wait_int()` の §31 先読み fast path を**無効化するフック**
 *         （`P4SDIO_NO_INT_FASTPATH`）。**既定 OFF**——定義しなければ
 *         プリプロセッサだけの追加であり、生成コードは変わらない。
 *         H3-c（lost-wakeup の positive control）を撃つためだけに在る。
 *         **ON にした版は「壊した版」であって移植元の振る舞いではない。**
 */

#include <kernel.h>
#include <sil.h>
#include <string.h>
#include "kernel_cfg.h"		/* P4SDIO_SEM_EVT/P4SDIO_SEM_IO/P4SDIO_MTX */
#include "p4sdio_host.h"
#include "p4sdio_pins.h"
#include "esp_shim_intr_clic.h"	/* esp_intr_alloc（C-1） */

/*
 *  IDF ヘッダ（ヘッダオンリー利用。**.c は 1 本も引かない**）
 */
#include "hal/sdmmc_ll.h"
#include "hal/sd_types.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_sig_map.h"
#include "soc/cache_struct.h"
#include "soc/interrupts.h"
#include "sd_protocol_defs.h"

/*
 *  D-6: bring-up 用ステップログ。**同期出力**（`target_fput_log()` 直呼び）。
 *  `syslog()` は logtask 経由の非同期なので、停止・ストームの直前の行を
 *  取りこぼす。実機診断ではこれを根拠にしない（本プロジェクトの既知の教訓）。
 *
 *  `p4shim_puts()` を使わないのは、そのためだけに `esp/p4shim` の一式
 *  （`software_init_hook` や heap_caps を含む）をリンクさせたくないため。
 *  SDIO は PSRAM も表示も要らない構成でも建てられるべきである。
 */
extern void		target_fput_log(char c);

void
p4sdio_puts(const char *s)
{
	if (s == NULL) {
		return;
	}
	(void) loc_cpu();
	while (*s != '\0') {
		target_fput_log(*s++);
	}
	target_fput_log('\n');
	(void) unl_cpu();
}

void
p4sdio_put_kv(const char *k, uint32_t v)
{
	char		buf[12];
	int			i = 0;

	(void) loc_cpu();
	while (*k != '\0') {
		target_fput_log(*k++);
	}
	target_fput_log('=');
	if (v == 0U) {
		target_fput_log('0');
	}
	else {
		while ((v != 0U) && (i < 11)) {
			buf[i++] = (char)('0' + (v % 10U));
			v /= 10U;
		}
		while (i > 0) {
			target_fput_log(buf[--i]);
		}
	}
	target_fput_log('\n');
	(void) unl_cpu();
}

#define P4SDIO_DEBUG	1
#if P4SDIO_DEBUG
#define DBG(s)			p4sdio_puts(s)
#define DBGKV(k, v)		p4sdio_put_kv((k), (uint32_t)(v))
#else /* P4SDIO_DEBUG */
#define DBG(s)			((void) 0)
#define DBGKV(k, v)		((void) 0)
#endif /* P4SDIO_DEBUG */

/*
 *  周辺レジスタへのアクセス
 *    IDF のグローバル構造体シンボル（SDMMC/GPIO/HP_SYS_CLKRST 等）はリンカ
 *    スクリプトの PROVIDE 供給で FMP3 単体リンクには無く、RISC-V は絶対
 *    アドレスの直接シンボル解決もできないため、ポインタキャストで参照する。
 *    グローバル構造体を参照する RCC 系 LL 関数だけは同一内容をローカル
 *    再実装する（`p4_rcc_*` 群）。
 */
#define HPCLKRST	((hp_sys_clkrst_dev_t *) 0x500E6000UL)
#define LPCLKRST	((lp_clkrst_dev_t *) 0x50111000UL)
#define GPIOX		((gpio_dev_t *) 0x500E0000UL)
#define CACHEX		((cache_dev_t *) 0x3FF10000UL)

/*  IO_MUX pad レジスタ: base 0x500E1000 + 4*(n+1)  */
#define P4_IOMUX_PAD(n)		((uint32_t *)(0x500E1000UL + ((n) + 1U) * 4UL))
#define IOMUX_FUN_IE		(1UL << 9)
#define IOMUX_FUN_WPD		(1UL << 7)
#define IOMUX_FUN_WPU		(1UL << 8)
#define IOMUX_FUN_DRV_S		10
#define IOMUX_FUN_DRV_M		(0x3UL << IOMUX_FUN_DRV_S)
#define IOMUX_MCU_SEL_S		12
#define IOMUX_MCU_SEL_M		(0x7UL << IOMUX_MCU_SEL_S)

/*  GPIO マトリクス定数入力  */
#define GPIO_MATRIX_CONST_ZERO	0x3EU
#define GPIO_MATRIX_CONST_ONE	0x3FU

/*  エラーマスク（crib sheet §4-5）  */
#define CMD_ERR_MASK	(SDMMC_INTMASK_RTO | SDMMC_INTMASK_RCRC | SDMMC_INTMASK_RESP_ERR)
#define DATA_ERR_MASK	(SDMMC_INTMASK_DTO | SDMMC_INTMASK_DCRC | SDMMC_INTMASK_HTO | \
						 SDMMC_INTMASK_SBE | SDMMC_INTMASK_EBE)
#define DMA_DONE_MASK	(SDMMC_IDMAC_INTMASK_TI | SDMMC_IDMAC_INTMASK_RI | SDMMC_IDMAC_INTMASK_NI)

/*  DMA ディスクリプタ（一括チェーン・ISR 補充なし）  */
#define NDESC			8U			/* 最大 8*4096 = 32KB/転送 */
#define DESC_MAX_SIZE	4096U

/*  ブロックモードの実効上限（CMD53 の 9bit=511 ではない。移植元の指摘⑥）  */
#define SDIO_RW_BLOCKS_MAX_NBLK	((NDESC * DESC_MAX_SIZE) / P4SDIO_BLOCK_SIZE)

/*  タイムアウト（FMP3 の TMO/RELTIM はマイクロ秒単位）  */
#define CMD_TAKEN_TIMEOUT_US	1000000U
#define EVENT_TIMEOUT_US		(1000U * 1000U)

/*  キャッシュ同期ポーリングの上限（移植元の指摘⑤）  */
#define CACHE_OP_INV		(1UL << 0)
#define CACHE_OP_WB			(1UL << 2)
#define CACHE_SYNC_TIMEOUT_LOOPS	1000000U

/*
 *  ============================================================================
 *  RCC 系 LL 関数のローカル再実装
 *  ============================================================================
 *  `sdmmc_ll.h` の同名関数と同一内容。グローバル構造体 HP_SYS_CLKRST /
 *  LP_AON_CLKRST を HPCLKRST/LPCLKRST に置換しただけ。
 */
static void
p4_rcc_enable_bus_clock(bool en)
{
	HPCLKRST->soc_clk_ctrl1.reg_sdmmc_sys_clk_en = en;
}

static void
p4_rcc_reset_register(void)
{
	LPCLKRST->hp_sdmmc_emac_rst_ctrl.rst_en_sdmmc = 1;
	LPCLKRST->hp_sdmmc_emac_rst_ctrl.rst_en_sdmmc = 0;
}

static void
p4_rcc_select_clk_source(void)
{
	/*  PLL_F160M（=クロック値0）固定  */
	HPCLKRST->peri_clk_ctrl01.reg_sdio_ls_clk_src_sel = 0;
	HPCLKRST->peri_clk_ctrl01.reg_sdio_ls_clk_en = 1;
}

static void
p4_rcc_set_clock_div(uint32_t div)
{
	if (div > 1U) {
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_h = div / 2U - 1U;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_n = div - 1U;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_l = div - 1U;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_cfg_update = 1;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_cfg_update = 0;
	}
	else {
		HPCLKRST->peri_clk_ctrl01.reg_sdio_hs_mode = 1;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_h = 0;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_n = 0;
		HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_l = 0;
	}
}

static void
p4_rcc_init_phase_delay(void)
{
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_drv_clk_en = 1;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_sam_clk_en = 1;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_slf_clk_en = 1;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_drv_clk_edge_sel = 1;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_sam_clk_edge_sel = 0;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_slf_clk_edge_sel = 0;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_cfg_update = 1;
	HPCLKRST->peri_clk_ctrl02.reg_sdio_ls_clk_edge_cfg_update = 0;
}

/*
 *  ドライバ状態
 */
static sdmmc_dev_t *const	s_hw = (sdmmc_dev_t *) 0x50083000UL;
static volatile uint32_t	s_ev_sd;		/* 蓄積 MINTSTS（ISR が OR）*/
static volatile uint32_t	s_ev_dma;		/* 蓄積 IDSTS */
static uint32_t				s_last_resp;
static uint32_t				s_last_err;
static uint16_t				s_rca;
static uint32_t				s_r4;			/* 直近の CMD5 R4（IO 機能数・C ビットの根拠） */
static bool					s_wide;

volatile uint32_t	p4sdio_n_isr;
volatile uint32_t	p4sdio_n_isr_io;
volatile uint32_t	p4sdio_n_cmd;
volatile uint32_t	p4sdio_n_cmd_err;
int					p4sdio_clic_line = -1;

/*
 *  D-14: R5 応答フラグの計数
 *
 *  R5（CMD52/CMD53 の応答）は 48 bit で、コントローラの RESP0 に入るのは
 *  bit39..8＝`stuff(16) | response flags(8) | data(8)` である。
 *  ⇒ フラグは `(resp >> 8) & 0xFF`、データは `resp & 0xFF`（既存コードと同じ）。
 *
 *  フラグのビット（SDIO 簡易仕様 R5 の定義）:
 *    b7 COM_CRC_ERROR / b6 ILLEGAL_COMMAND / b5..4 IO_CURRENT_STATE /
 *    b3 ERROR / b2 RFU / b1 FUNCTION_NUMBER / b0 OUT_OF_RANGE
 *  ⇒ **エラーは b7,b6,b3,b1,b0＝0xCB**。b5..4 は状態であってエラーではない。
 */
#define R5_FLAG_ERR_MASK	0xCBU

volatile uint32_t	p4sdio_n_r5;			/* R5 応答を見た回数 */
volatile uint32_t	p4sdio_n_r5_err;		/* うちエラービットが立っていた回数 */
volatile uint32_t	p4sdio_last_r5_flags;	/* 最後に見たフラグ 8bit（0 のときも記録） */
volatile uint32_t	p4sdio_r5_err_or;		/* 立ったエラービットの累積 OR */

/*
 *  D-14: **数えるだけ。戻り値も呼出し側の制御流も変えない。**
 *  「直す」（エラーで E_SYS を返す）のは別の判断であって、まず数字が要る。
 */
static void
r5_count(uint32_t resp)
{
	uint32_t	flags = (resp >> 8) & 0xFFU;

	p4sdio_n_r5++;
	p4sdio_last_r5_flags = flags;
	if ((flags & R5_FLAG_ERR_MASK) != 0U) {
		p4sdio_n_r5_err++;
		p4sdio_r5_err_or |= (flags & R5_FLAG_ERR_MASK);
	}
}

static sdmmc_desc_t	s_desc[NDESC] __attribute__((aligned(64)));
static uint8_t		s_bounce[1536 + 64] __attribute__((aligned(64)));

/*
 *  P4 L1D+L2 キャッシュの範囲 writeback / invalidate（CACHE レジスタ直叩き）
 *    sync_map: [4]=L1-DCache, [5]=L2-Cache
 */
static ER
cache_sync_range(const void *addr, uint32_t size, uint32_t opbit)
{
	uint32_t start = (uint32_t) addr & ~63U;
	uint32_t end   = ((uint32_t) addr + size + 63U) & ~63U;
	uint32_t i;

	CACHEX->sync_map.sync_map = 0x30U;			/* L1D + L2 */
	CACHEX->sync_addr.sync_addr = start;
	CACHEX->sync_size.sync_size = end - start;
	CACHEX->sync_ctrl.val = opbit;
	for (i = CACHE_SYNC_TIMEOUT_LOOPS; i > 0U; i--) {
		if ((CACHEX->sync_ctrl.val & opbit) == 0U) {
			break;
		}
	}
	if (i == 0U) {
		s_last_err = 0xCCCCCCCCU;
		return(E_TMOUT);
	}
	for (i = CACHE_SYNC_TIMEOUT_LOOPS; i > 0U; i--) {
		if (CACHEX->sync_ctrl.sync_done != 0U) {
			break;
		}
	}
	if (i == 0U) {
		s_last_err = 0xCCCCCCCCU;
		return(E_TMOUT);
	}
	return(E_OK);
}

/*
 *  µs 単位のビジーウェイト
 */
static void
delay_us(uint32_t us)
{
	while (us-- > 0U) {
		sil_dly_nse(1000U);
	}
}

/*
 *  ============================================================================
 *  ISR
 *  ============================================================================
 *  D-3: C-1 シムは `void (*)(void *)` で呼ぶ（IDF の `intr_handler_t` 互換）。
 */
void
p4sdio_isr(void *arg)
{
	uint32_t pend = sdmmc_ll_get_intr_status(s_hw);
	uint32_t dmap = sdmmc_ll_get_idsts_interrupt_raw(s_hw);
	bool     wake = false;

	(void) arg;
	p4sdio_n_isr++;

	if ((pend & SDMMC_INTMASK_IO_SLOT1) != 0U) {
		/*  SDIO カード割込み: 自己マスクして通知（再イネーブルは wait_int 側）  */
		sdmmc_ll_enable_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1, false);
		sdmmc_ll_clear_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1);
		p4sdio_n_isr_io++;
		(void) isig_sem(P4SDIO_SEM_IO);			/* E_QOVR は無視 */
		pend &= ~(uint32_t) SDMMC_INTMASK_IO_SLOT1;
	}
	if (pend != 0U) {
		sdmmc_ll_clear_interrupt(s_hw, pend);
		s_ev_sd |= pend;
		wake = true;
	}
	if (dmap != 0U) {
		/*  NI はスティッキー: raw を全クリア（補充なし方式なので単純クリアで可）  */
		sdmmc_ll_clear_idsts_interrupt(s_hw, dmap);
		s_ev_dma |= dmap;
		wake = true;
	}
	if (wake) {
		(void) isig_sem(P4SDIO_SEM_EVT);		/* E_QOVR は無視 */
	}
}

/*
 *  start_command の受理（自己クリア）待ち
 */
static ER
wait_cmd_taken(void)
{
	uint32_t i;

	for (i = 0U; i < CMD_TAKEN_TIMEOUT_US; i++) {
		if (sdmmc_ll_is_command_taken(s_hw)) {
			return(E_OK);
		}
		sil_dly_nse(1000U);
	}
	s_last_err = 0xFFFFFFFFU;
	return(E_TMOUT);
}

/*
 *  HW コマンド書込み（受理待ち→arg→cmd→受理待ち）
 */
static ER
issue_hw_cmd(sdmmc_hw_cmd_t hc, uint32_t arg)
{
	ER ercd;

	hc.use_hold_reg = 1;						/* 全コマンドで必須（罠#4）*/
	hc.card_num = P4SDIO_SLOT;
	hc.start_command = 1;

	ercd = wait_cmd_taken();
	if (ercd != E_OK) {
		return(ercd);
	}
	sdmmc_ll_set_command_arg(s_hw, arg);
	sdmmc_ll_set_command(s_hw, hc);
	return(wait_cmd_taken());
}

/*
 *  クロック更新コマンド（分周変更のたびに必要）
 */
static ER
clock_update_command(void)
{
	sdmmc_hw_cmd_t hc;

	memset(&hc, 0, sizeof(hc));
	hc.update_clk_reg = 1;
	hc.wait_complete = 1;
	return(issue_hw_cmd(hc, 0U));
}

/*
 *  カードクロック設定（crib sheet §1-3/1-4）
 *    固定表: 400kHz→(10,20) / 20MHz→(8,0) / 40MHz→(4,0)
 */
static ER
set_card_clock(uint32_t freq_khz)
{
	uint32_t host_div, card_div;
	ER ercd;

	if (freq_khz >= 40000U) {
		host_div = 4U;  card_div = 0U;
	}
	else if (freq_khz >= 20000U) {
		host_div = 8U;  card_div = 0U;
	}
	else {
		host_div = 10U;
		card_div = 160000U / (host_div * 2U * freq_khz);		/* 400kHz → 20 */
	}

	sdmmc_ll_enable_card_clock(s_hw, P4SDIO_SLOT, false);
	ercd = clock_update_command();
	if (ercd != E_OK) {
		return(ercd);
	}

	sdmmc_ll_set_card_clock_div(s_hw, P4SDIO_SLOT, card_div);
	p4_rcc_set_clock_div(host_div);
	p4_rcc_select_clk_source();
	p4_rcc_init_phase_delay();
	delay_us(10U);

	ercd = clock_update_command();
	if (ercd != E_OK) {
		return(ercd);
	}

	sdmmc_ll_enable_card_clock(s_hw, P4SDIO_SLOT, true);
	sdmmc_ll_enable_card_clock_low_power(s_hw, P4SDIO_SLOT, true);
	ercd = clock_update_command();
	if (ercd != E_OK) {
		return(ercd);
	}

	sdmmc_ll_set_data_timeout(s_hw, 100U * freq_khz);		/* 100ms 相当 */
	sdmmc_ll_set_response_timeout(s_hw, 255U);
	return(E_OK);
}

/*
 *  イベント待ち: s_ev_sd に want が全て立つ（または err のどれかが立つ）まで
 */
static ER
wait_sd_events(uint32_t want, uint32_t err_mask, bool want_dma)
{
	ER ercd;

	for (;;) {
		uint32_t sd  = s_ev_sd;
		uint32_t dma = s_ev_dma;

		if ((sd & err_mask) != 0U) {
			s_last_err = sd;
			return((sd & SDMMC_INTMASK_RTO) != 0U ? E_TMOUT : E_OBJ);
		}
		if ((sd & want) == want && (!want_dma || (dma & DMA_DONE_MASK) != 0U)) {
			return(E_OK);
		}
		ercd = twai_sem(P4SDIO_SEM_EVT, EVENT_TIMEOUT_US);
		if (ercd == E_TMOUT) {
			s_last_err = 0xEEEEEEEEU;
			return(E_TMOUT);
		}
		if (ercd != E_OK) {
			return(ercd);
		}
	}
}

/*
 *  レスポンス種別
 */
typedef enum {
	RESP_NONE,			/* R0 */
	RESP_SHORT,			/* R1/R5/R6 (CRC チェックあり) */
	RESP_SHORT_NOCRC,	/* R3/R4 (CRC チェック無効: 罠#14) */
} resp_kind_t;

/*
 *  データ無しコマンドの実行（ロック保持前提）
 */
static ER
send_cmd_nodata(uint_t idx, uint32_t arg, resp_kind_t rk, bool send_init,
				uint32_t *p_resp)
{
	sdmmc_hw_cmd_t hc;
	ER ercd;

	memset(&hc, 0, sizeof(hc));
	hc.cmd_index = idx & 0x3FU;
	if (rk != RESP_NONE) {
		hc.response_expect = 1;
		hc.check_response_crc = (rk == RESP_SHORT) ? 1 : 0;
	}
	if (send_init) {
		hc.send_init = 1;
	}

	s_ev_sd = 0U;
	s_ev_dma = 0U;
	s_last_err = 0U;
	p4sdio_n_cmd++;

	ercd = issue_hw_cmd(hc, arg);
	if (ercd != E_OK) {
		p4sdio_n_cmd_err++;
		return(ercd);
	}
	ercd = wait_sd_events(SDMMC_INTMASK_CMD_DONE, CMD_ERR_MASK, false);
	if (ercd != E_OK) {
		p4sdio_n_cmd_err++;
		return(ercd);
	}
	if (rk == RESP_NONE) {
		return(E_OK);
	}
	s_last_resp = s_hw->resp[0];
	if (p_resp != NULL) {
		*p_resp = s_last_resp;
	}
	return(E_OK);
}

/*
 *  データ付きコマンド（CMD53）の実行（ロック保持前提）
 */
static ER
send_cmd_data(uint32_t arg, bool wr, void *buf, uint32_t len, uint32_t blksiz)
{
	sdmmc_hw_cmd_t hc;
	uint32_t len4 = (len + 3U) & ~3U;
	uint32_t ndesc = (len4 + DESC_MAX_SIZE - 1U) / DESC_MAX_SIZE;
	uint32_t i, rem;
	uint8_t *p = buf;
	ER ercd;

	if (ndesc == 0U || ndesc > NDESC) {
		return(E_PAR);
	}

	/*  DMA ディスクリプタ構築（一括チェーン）  */
	memset(s_desc, 0, sizeof(s_desc[0]) * ndesc);
	rem = len4;
	for (i = 0U; i < ndesc; i++) {
		uint32_t sz = (rem > DESC_MAX_SIZE) ? DESC_MAX_SIZE : rem;
		s_desc[i].second_address_chained = 1;
		s_desc[i].buffer1_size = sz;
		s_desc[i].buffer1_ptr = &p[len4 - rem];
		s_desc[i].next_desc_ptr = (i + 1U < ndesc) ? (void *) &s_desc[i + 1U] : NULL;
		s_desc[i].owned_by_idmac = 1;
		rem -= sz;
	}
	s_desc[0].first_descriptor = 1;
	s_desc[ndesc - 1U].last_descriptor = 1;

	/*  キャッシュ同期: desc とバッファを writeback（RX も dirty 追出し防止で WB）  */
	ercd = cache_sync_range(s_desc, sizeof(s_desc[0]) * ndesc, CACHE_OP_WB);
	if (ercd == E_OK) {
		ercd = cache_sync_range(buf, len4, CACHE_OP_WB);
	}
	if (ercd != E_OK) {
		DBG("P4SDIO NG: cache_sync_range(WB) timeout");
		return(ercd);
	}

	s_ev_sd = 0U;
	s_ev_dma = 0U;
	s_last_err = 0U;
	p4sdio_n_cmd++;

	/*  DMA 準備（必ずコマンド発行前）  */
	sdmmc_ll_set_data_transfer_len(s_hw, len4);
	sdmmc_ll_set_block_size(s_hw, blksiz);
	sdmmc_ll_set_desc_addr(s_hw, (uint32_t) &s_desc[0]);
	sdmmc_ll_enable_dma(s_hw, true);
	sdmmc_ll_poll_demand(s_hw);

	memset(&hc, 0, sizeof(hc));
	hc.cmd_index = SD_IO_RW_EXTENDED;
	hc.response_expect = 1;
	hc.check_response_crc = 1;
	hc.data_expected = 1;
	hc.rw = wr ? 1 : 0;

	ercd = issue_hw_cmd(hc, arg);
	if (ercd == E_OK) {
		ercd = wait_sd_events(SDMMC_INTMASK_CMD_DONE, CMD_ERR_MASK, false);
	}
	if (ercd == E_OK) {
		/*  DATA_OVER(bit3) + DMA 完了の両方を待つ  */
		ercd = wait_sd_events(SDMMC_INTMASK_DATA_OVER, DATA_ERR_MASK, true);
	}
	if (ercd != E_OK) {
		p4sdio_n_cmd_err++;
		DBG("P4SDIO NG: cmd53 fail");
		DBGKV("  ev_sd", s_ev_sd);
		DBGKV("  ev_dma", s_ev_dma);
		DBGKV("  resp0", s_hw->resp[0]);
		/*  エラー時は DMA 停止 + FIFO リセット（罠#9）  */
		sdmmc_ll_stop_dma(s_hw);
		s_hw->ctrl.fifo_reset = 1;
		return(ercd);
	}
	s_last_resp = s_hw->resp[0];
	r5_count(s_last_resp);					/* D-14: CMD53 の R5 フラグを数える */

	if (!wr) {
		ercd = cache_sync_range(buf, len4, CACHE_OP_INV);
		if (ercd != E_OK) {
			DBG("P4SDIO NG: cache_sync_range(INV) timeout");
			return(ercd);
		}
	}
	return(E_OK);
}

/*
 *  CMD52（ロック保持前提）
 *    arg = rw<<31 | func<<28 | RAW<<27 | (reg&0x1FFFF)<<9 | data
 */
static ER
cmd52(bool wr, uint_t func, uint32_t reg, uint8_t in, uint8_t *p_out)
{
	uint32_t arg = ((wr ? 1UL : 0UL) << 31) | (((uint32_t) func & 7U) << 28) |
				   ((reg & 0x1FFFFU) << 9) | in;
	uint32_t resp;
	ER ercd;

	ercd = send_cmd_nodata(SD_IO_RW_DIRECT, arg, RESP_SHORT, false, &resp);
	if (ercd == E_OK) {
		r5_count(resp);						/* D-14: CMD52 の R5 フラグを数える */
	}
	if (ercd == E_OK && p_out != NULL) {
		*p_out = (uint8_t)(resp & 0xFFU);
	}
	return(ercd);
}

/*
 *  CMD53 引数の構築
 */
static uint32_t
cmd53_arg(bool wr, uint_t func, uint32_t addr, bool block, bool incr, uint32_t count)
{
	return(((wr ? 1UL : 0UL) << 31) | (((uint32_t) func & 7U) << 28) |
		   ((block ? 1UL : 0UL) << 27) | ((incr ? 1UL : 0UL) << 26) |
		   ((addr & 0x1FFFFU) << 9) | (count & 0x1FFU));
}

/*
 *  ============================================================================
 *  GPIO ヘルパ（D-2: **両バンク対応**）
 *  ============================================================================
 *  移植元は GPIO>=32 だけを想定して `enable1_w1ts` / `1<<(gpio-32)` を
 *  直書きしていた。Tab5 の SDIO は G8-G15 なので、そのまま持ってくると
 *  **`1<<(8-32)` という未定義シフト**になり、たまたま別の pad を叩く。
 *  「起動はするが静かに壊れる」型なので、ここで構造的に潰す。
 */
static void
gpio_out_enable(uint32_t gpio)
{
	if (gpio < 32U) {
		GPIOX->enable_w1ts.enable_w1ts = 1UL << gpio;
	}
	else {
		GPIOX->enable1_w1ts.enable1_w1ts = 1UL << (gpio - 32U);
	}
}

void
p4sdio_gpio_out_set(uint32_t gpio, bool level)
{
	if (gpio < 32U) {
		if (level) {
			GPIOX->out_w1ts.out_w1ts = 1UL << gpio;
		}
		else {
			GPIOX->out_w1tc.out_w1tc = 1UL << gpio;
		}
	}
	else {
		if (level) {
			GPIOX->out1_w1ts.out1_w1ts = 1UL << (gpio - 32U);
		}
		else {
			GPIOX->out1_w1tc.out1_w1tc = 1UL << (gpio - 32U);
		}
	}
}

static void
gpio_route_out(uint32_t gpio, uint32_t sig)
{
	GPIOX->func_out_sel_cfg[gpio].out_sel = sig;
	GPIOX->func_out_sel_cfg[gpio].out_inv_sel = 0;
	GPIOX->func_out_sel_cfg[gpio].oen_sel = 0;		/* 周辺回路が OE 制御（罠#11）*/
	GPIOX->func_out_sel_cfg[gpio].oen_inv_sel = 0;
	gpio_out_enable(gpio);
}

static void
gpio_route_in(uint32_t sig, uint32_t gpio_or_const)
{
	GPIOX->func_in_sel_cfg[sig].in_sel = gpio_or_const;
	GPIOX->func_in_sel_cfg[sig].in_inv_sel = 0;
	GPIOX->func_in_sel_cfg[sig].sig_in_sel = 1;		/* マトリクス経由 */
}

static void
iomux_config(uint32_t gpio, bool input_en)
{
	uint32_t v = sil_rew_mem(P4_IOMUX_PAD(gpio));

	v &= ~(IOMUX_MCU_SEL_M | IOMUX_FUN_WPD);
	v |= (1UL << IOMUX_MCU_SEL_S);				/* PIN_FUNC_GPIO=1 */
	v |= IOMUX_FUN_WPU;							/* 内部プルアップ */
	if (input_en) {
		v |= IOMUX_FUN_IE;
	}
#if defined(P4SDIO_PAD_DRV)
	/*  D-5: ドライブ強度（Tab5 は公式デモに合わせて 0＝最弱）  */
	v &= ~IOMUX_FUN_DRV_M;
	v |= ((uint32_t)(P4SDIO_PAD_DRV) << IOMUX_FUN_DRV_S) & IOMUX_FUN_DRV_M;
#endif
	sil_wrw_mem(P4_IOMUX_PAD(gpio), v);
}

/*
 *  D-8: リセット線を GPIO 出力として構成する（実際のパルスは p4sdio_board.c）
 */
void
p4sdio_gpio_out_init(uint32_t gpio)
{
	uint32_t v = sil_rew_mem(P4_IOMUX_PAD(gpio));

	v &= ~IOMUX_MCU_SEL_M;
	v |= (1UL << IOMUX_MCU_SEL_S);
	sil_wrw_mem(P4_IOMUX_PAD(gpio), v);
	GPIOX->func_out_sel_cfg[gpio].out_sel = 256U;	/* GPIO_OUT_REG */
	GPIOX->func_out_sel_cfg[gpio].oen_sel = 0;
	gpio_out_enable(gpio);
}

static void
configure_pins(void)
{
	/*  CLK: 出力のみ  */
	gpio_route_out(P4SDIO_GPIO_CLK, SD_CARD_CCLK_2_PAD_OUT_IDX);
	iomux_config(P4SDIO_GPIO_CLK, false);

	/*  CMD/D0-D3: 双方向（out+in）  */
	gpio_route_out(P4SDIO_GPIO_CMD, SD_CARD_CCMD_2_PAD_OUT_IDX);
	gpio_route_in(SD_CARD_CCMD_2_PAD_IN_IDX, P4SDIO_GPIO_CMD);
	iomux_config(P4SDIO_GPIO_CMD, true);

	gpio_route_out(P4SDIO_GPIO_D0, SD_CARD_CDATA0_2_PAD_OUT_IDX);
	gpio_route_in(SD_CARD_CDATA0_2_PAD_IN_IDX, P4SDIO_GPIO_D0);
	iomux_config(P4SDIO_GPIO_D0, true);

	gpio_route_out(P4SDIO_GPIO_D1, SD_CARD_CDATA1_2_PAD_OUT_IDX);
	gpio_route_in(SD_CARD_CDATA1_2_PAD_IN_IDX, P4SDIO_GPIO_D1);
	iomux_config(P4SDIO_GPIO_D1, true);

	gpio_route_out(P4SDIO_GPIO_D2, SD_CARD_CDATA2_2_PAD_OUT_IDX);
	gpio_route_in(SD_CARD_CDATA2_2_PAD_IN_IDX, P4SDIO_GPIO_D2);
	iomux_config(P4SDIO_GPIO_D2, true);

	gpio_route_out(P4SDIO_GPIO_D3, SD_CARD_CDATA3_2_PAD_OUT_IDX);
	gpio_route_in(SD_CARD_CDATA3_2_PAD_IN_IDX, P4SDIO_GPIO_D3);
	iomux_config(P4SDIO_GPIO_D3, true);

	/*
	 *  CD/WP/card_int の定数入力バイパス（罠#10）:
	 *    CD=定数0(カードあり)。WP は HW が active-high(1=保護) を期待し、IDF は
	 *    「定数1を反転接続」= 実効0 なので、ここでは定数0を直結(=保護なし)。
	 *    card_int_n=定数1(SDIO 割込みのエッジ検出条件)。
	 */
	gpio_route_in(SD_CARD_DETECT_N_2_PAD_IN_IDX, GPIO_MATRIX_CONST_ZERO);
	gpio_route_in(SD_CARD_WRITE_PRT_2_PAD_IN_IDX, GPIO_MATRIX_CONST_ZERO);
	gpio_route_in(SD_CARD_INT_N_2_PAD_IN_IDX, GPIO_MATRIX_CONST_ONE);
}

/*
 *  ============================================================================
 *  ホストコントローラ初期化
 *  ============================================================================
 */
ER
p4sdio_host_init(void)
{
	uint32_t i;
	esp_err_t eerr;
	ER ercd;

	/*  バスクロック有効化 + モジュールリセット  */
	p4_rcc_enable_bus_clock(true);
	p4_rcc_reset_register();

	/*  クロック通電（div=2）  */
	p4_rcc_set_clock_div(2U);
	p4_rcc_select_clk_source();
	p4_rcc_init_phase_delay();
	delay_us(10U);

	/*  コントローラ/DMA/FIFO リセット（自己クリア待ち）  */
	sdmmc_ll_reset_controller(s_hw);
	sdmmc_ll_reset_dma(s_hw);
	sdmmc_ll_reset_fifo(s_hw);
	for (i = 0U; i < 100000U; i++) {
		if (sdmmc_ll_is_controller_reset_done(s_hw) &&
			sdmmc_ll_is_dma_reset_done(s_hw) &&
			sdmmc_ll_is_fifo_reset_done(s_hw)) {
			break;
		}
		sil_dly_nse(1000U);
	}
	if (i >= 100000U) {
		s_last_err = 0xDDDDDDDDU;
		return(E_TMOUT);
	}

	/*  割込み: 全クリア→全マスク→既定イベントのみ許可→グローバル許可  */
	sdmmc_ll_clear_interrupt(s_hw, 0xFFFFFFFFU);
	sdmmc_ll_enable_interrupt(s_hw, 0xFFFFFFFFU, false);
	sdmmc_ll_enable_global_interrupt(s_hw, false);
	sdmmc_ll_enable_interrupt(s_hw, SDMMC_LL_EVENT_DEFAULT, true);
	sdmmc_ll_enable_global_interrupt(s_hw, true);
	sdmmc_ll_enable_busy_clear_interrupt(s_hw, false);

	/*  IDMAC 初期化  */
	sdmmc_ll_init_dma(s_hw);

	/*  ピン配線（GPIO マトリクス）と CD/WP バイパス  */
	configure_pins();

	/*
	 *  D-3: 割込みの確保は C-1 CLIC シムへ委ねる（線の管理は
	 *  `esp_shim_intr_clic_lines.h` の利用者表が正本。SDMMC の行は計上済み）。
	 *  ソース番号 23 = `ETS_SDIO_HOST_INTR_SOURCE`（`soc/interrupts.h`）。
	 */
	eerr = esp_intr_alloc((int) ETS_SDIO_HOST_INTR_SOURCE, 0,
						  p4sdio_isr, NULL, NULL);
	if (eerr != ESP_OK) {
		DBGKV("P4SDIO NG: esp_intr_alloc rc", (uint32_t) eerr);
		return(E_SYS);
	}
	{
		int slot = esp_shim_clic_intr_slot_of_source((int) ETS_SDIO_HOST_INTR_SOURCE);
		p4sdio_clic_line = (slot >= 0) ? esp_shim_clic_intr_line_of(slot) : -1;
	}

	/*  カードクロック 400kHz（プローブ用）  */
	ercd = set_card_clock(400U);
	if (ercd != E_OK) {
		return(ercd);
	}

	/*  1bit 幅から開始  */
	sdmmc_ll_set_card_width(s_hw, P4SDIO_SLOT, SD_BUS_WIDTH_1_BIT);
	s_wide = false;
	return(E_OK);
}

/*
 *  ============================================================================
 *  SDIO カード（esp-hosted slave）初期化
 *  ============================================================================
 */
ER
p4sdio_card_init(uint32_t freq_khz)
{
	uint32_t resp, ocr;
	uint8_t v;
	int_t i;
	ER ercd;

	ercd = loc_mtx(P4SDIO_MTX);
	if (ercd != E_OK) {
		return(ercd);
	}

	/*  1. IO リセット（CCCR 0x06 <- RES=BIT3）。エラーは無視して良い  */
	ercd = cmd52(true, 0, SD_IO_CCCR_CTL, CCCR_CTL_RES, NULL);
	DBGKV("P4SDIO io_reset er", (uint32_t) ercd);
	dly_tsk(20U * 1000U);				/* 20ms（RELTIM はマイクロ秒単位） */

	/*  2. CMD0（send_init=80 クロック付き）  */
	ercd = send_cmd_nodata(MMC_GO_IDLE_STATE, 0U, RESP_NONE, true, NULL);
	DBGKV("P4SDIO cmd0 er", (uint32_t) ercd);
	if (ercd != E_OK) {
		goto out;
	}
	dly_tsk(20U * 1000U);

	/*  3. CMD5 arg=0 で OCR 読出し（R4: CRC チェック無効）  */
	ercd = send_cmd_nodata(SD_IO_SEND_OP_COND, 0U, RESP_SHORT_NOCRC, false, &resp);
	DBGKV("P4SDIO cmd5a er", (uint32_t) ercd);
	DBGKV("P4SDIO cmd5a resp", resp);
	if (ercd != E_OK) {
		goto out;
	}
	ocr = resp & 0xFFFFFFU;

	/*  4. CMD5 arg=OCR を ready(BIT31) まで（最大 100 回 × 10ms）  */
	for (i = 0; i < 100; i++) {
		ercd = send_cmd_nodata(SD_IO_SEND_OP_COND, ocr & 0xFF8000U,
							   RESP_SHORT_NOCRC, false, &resp);
		if (ercd != E_OK) {
			goto out;
		}
		if ((resp & 0x80000000U) != 0U) {
			break;
		}
		dly_tsk(10U * 1000U);			/* 10ms */
	}
	s_r4 = resp;
	DBGKV("P4SDIO cmd5b resp", resp);
	if ((resp & 0x80000000U) == 0U) {
		ercd = E_TMOUT;
		goto out;
	}

	/*  5. CMD3: RCA 取得（R6）  */
	ercd = send_cmd_nodata(SD_SEND_RELATIVE_ADDR, 0U, RESP_SHORT, false, &resp);
	if (ercd != E_OK) {
		goto out;
	}
	s_rca = (uint16_t)(resp >> 16);
	DBGKV("P4SDIO rca", (uint32_t) s_rca);

	/*  6. CMD7: カード選択（R1）  */
	ercd = send_cmd_nodata(MMC_SELECT_CARD, ((uint32_t) s_rca) << 16,
						   RESP_SHORT, false, &resp);
	if (ercd != E_OK) {
		goto out;
	}

	/*  7. 4bit 化（CCCR 0x07 の [1:0]=2）+ ホスト側 CTYPE  */
	ercd = cmd52(false, 0, SD_IO_CCCR_BUS_WIDTH, 0U, &v);
	if (ercd != E_OK) {
		goto out;
	}
	v = (uint8_t)((v & ~3U) | CCCR_BUS_WIDTH_4);
	ercd = cmd52(true, 0, SD_IO_CCCR_BUS_WIDTH, v, NULL);
	if (ercd != E_OK) {
		goto out;
	}
	sdmmc_ll_set_card_width(s_hw, P4SDIO_SLOT, SD_BUS_WIDTH_4_BIT);
	s_wide = true;

	/*  8. FN1 有効化 → ready 待ち → 割込み許可  */
	ercd = cmd52(false, 0, SD_IO_CCCR_FN_ENABLE, 0U, &v);
	if (ercd != E_OK) {
		goto out;
	}
	ercd = cmd52(true, 0, SD_IO_CCCR_FN_ENABLE, (uint8_t)(v | (1U << 1)), NULL);
	if (ercd != E_OK) {
		goto out;
	}
	{
		bool fn_ready = false;

		for (i = 0; i < 10; i++) {
			ercd = cmd52(false, 0, SD_IO_CCCR_FN_READY, 0U, &v);
			if (ercd == E_OK && (v & (1U << 1)) != 0U) {
				fn_ready = true;
				break;
			}
			dly_tsk(10U * 1000U);		/* 10ms */
		}
		if (!fn_ready) {
			if (ercd == E_OK) {
				ercd = E_TMOUT;
			}
			goto out;
		}
	}
	ercd = cmd52(false, 0, SD_IO_CCCR_INT_ENABLE, 0U, &v);
	if (ercd != E_OK) {
		goto out;
	}
	ercd = cmd52(true, 0, SD_IO_CCCR_INT_ENABLE, (uint8_t)(v | 0x03U), NULL);
	if (ercd != E_OK) {
		goto out;
	}

	/*  9. FN0/FN1 ブロックサイズ = 512  */
	ercd = cmd52(true, 0, SD_IO_CCCR_BLKSIZEL, 0x00U, NULL);
	if (ercd == E_OK) {
		ercd = cmd52(true, 0, SD_IO_CCCR_BLKSIZEH, 0x02U, NULL);
	}
	if (ercd == E_OK) {
		ercd = cmd52(true, 0, SD_IO_FBR_START + SD_IO_CCCR_BLKSIZEL, 0x00U, NULL);
	}
	if (ercd == E_OK) {
		ercd = cmd52(true, 0, SD_IO_FBR_START + SD_IO_CCCR_BLKSIZEH, 0x02U, NULL);
	}
	if (ercd != E_OK) {
		goto out;
	}

	/*  10. クロック引上げ  */
	ercd = set_card_clock(freq_khz);

  out:
	(void) unl_mtx(P4SDIO_MTX);
	return(ercd);
}

/*
 *  公開 API: CMD52
 */
ER
p4sdio_read_reg(uint_t func, uint32_t reg, uint8_t *p_val)
{
	ER ercd = loc_mtx(P4SDIO_MTX);

	if (ercd != E_OK) {
		return(ercd);
	}
	ercd = cmd52(false, func, reg, 0U, p_val);
	(void) unl_mtx(P4SDIO_MTX);
	return(ercd);
}

ER
p4sdio_write_reg(uint_t func, uint32_t reg, uint8_t val)
{
	ER ercd = loc_mtx(P4SDIO_MTX);

	if (ercd != E_OK) {
		return(ercd);
	}
	ercd = cmd52(true, func, reg, val, NULL);
	(void) unl_mtx(P4SDIO_MTX);
	return(ercd);
}

/*
 *  公開 API: CMD53 バイトモード
 */
static ER
rw_bytes(bool wr, uint_t func, uint32_t addr, void *buf, uint32_t len, bool addr_fixed)
{
	uint32_t len4 = (len + 3U) & ~3U;
	bool aligned = (((uint32_t) buf & 63U) == 0U) && (len4 == len);
	void *dma_buf = buf;
	uint32_t count;
	ER ercd;

	if (len == 0U || len > 512U) {
		return(E_PAR);							/* バイトモードは 512 まで */
	}
	if (!aligned) {
		if (len4 > sizeof(s_bounce)) {
			return(E_PAR);
		}
		dma_buf = s_bounce;
		if (wr) {
			memcpy(s_bounce, buf, len);
			memset(&s_bounce[len], 0, len4 - len);
		}
	}
	count = (len4 == 512U) ? 0U : len4;			/* 512→0 エンコード（罠#13）*/

	ercd = loc_mtx(P4SDIO_MTX);
	if (ercd != E_OK) {
		return(ercd);
	}
	ercd = send_cmd_data(cmd53_arg(wr, func, addr, false, !addr_fixed, count),
						 wr, dma_buf, len4, len4);
	/*
	 *  バウンスバッファからの読出しコピーはミューテックス保持中に行う
	 *  （unl_mtx 後だと別タスクが s_bounce を上書きする競合窓ができる）。
	 */
	if (ercd == E_OK && !wr && !aligned) {
		memcpy(buf, s_bounce, len);
	}
	(void) unl_mtx(P4SDIO_MTX);

	return(ercd);
}

ER
p4sdio_read_bytes(uint_t func, uint32_t addr, void *dst, uint32_t len, bool addr_fixed)
{
	return(rw_bytes(false, func, addr, dst, len, addr_fixed));
}

ER
p4sdio_write_bytes(uint_t func, uint32_t addr, const void *src, uint32_t len, bool addr_fixed)
{
	return(rw_bytes(true, func, addr, (void *) src, len, addr_fixed));
}

/*
 *  公開 API: CMD53 ブロックモード（buf は 64B 整列必須）
 */
static ER
rw_blocks(bool wr, uint_t func, uint32_t addr, void *buf, uint32_t nblk, bool addr_fixed)
{
	ER ercd;

	if (nblk == 0U || nblk > SDIO_RW_BLOCKS_MAX_NBLK || ((uint32_t) buf & 63U) != 0U) {
		return(E_PAR);
	}
	ercd = loc_mtx(P4SDIO_MTX);
	if (ercd != E_OK) {
		return(ercd);
	}
	ercd = send_cmd_data(cmd53_arg(wr, func, addr, true, !addr_fixed, nblk),
						 wr, buf, nblk * P4SDIO_BLOCK_SIZE, P4SDIO_BLOCK_SIZE);
	(void) unl_mtx(P4SDIO_MTX);
	return(ercd);
}

ER
p4sdio_read_blocks(uint_t func, uint32_t addr, void *dst, uint32_t nblk, bool addr_fixed)
{
	return(rw_blocks(false, func, addr, dst, nblk, addr_fixed));
}

ER
p4sdio_write_blocks(uint_t func, uint32_t addr, const void *src, uint32_t nblk, bool addr_fixed)
{
	return(rw_blocks(true, func, addr, (void *) src, nblk, addr_fixed));
}

/*
 *  公開 API: スレーブ(カード)割込み待ち
 *
 *  移植元 §31 の根治をそのまま引き継ぐ: 待ちに入る前に RINTSTS.IO_SLOT1 を
 *  無条件クリアすると、RX 処理中に latch された次パケットの兆候を捨ててしまう
 *  （DW コントローラは 4-bit アイドル中に DAT1 を必ずしも再サンプルしない）。
 *  ⇒「クリアしてから待つ」ではなく「latch 済みなら消費して即 E_OK」。
 */
ER
p4sdio_wait_int(uint32_t tmout_ms)
{
	TMO tmo;
	SIL_PRE_LOC;

	if (tmout_ms == P4SDIO_WAIT_FOREVER) {
		tmo = TMO_FEVR;
	}
	else {
		uint32_t ms = tmout_ms;

		if (ms > TMAX_RELTIM / 1000U) {
			ms = TMAX_RELTIM / 1000U;
		}
		tmo = (TMO) (ms * 1000U);
	}

#ifdef P4SDIO_NO_INT_FASTPATH
	/*
	 *  D-15（**既定 OFF。ON にした版は「壊した版」である**）:
	 *  移植元 §31 の修正**前**の振る舞いをわざと再現する——待ちに入る前に
	 *  RINTSTS.IO_SLOT1 を**無条件クリア**し、latch 済みの兆候を捨てる。
	 *  これで lost-wakeup（出典 HANDOFF.md:1095-1106 の「TCP エコーだけが
	 *  12 秒滞留する」）が再現するはずである、というのが H3-c の主張であり、
	 *  そのフックがこれである。**主張が正しいかは実機で測って初めて言える。**
	 */
	SIL_LOC_INT();
	sdmmc_ll_clear_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1);
	sdmmc_ll_enable_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1, true);
	SIL_UNL_INT();

	return(twai_sem(P4SDIO_SEM_IO, tmo));
#else
	SIL_LOC_INT();
	if ((sdmmc_ll_get_interrupt_raw(s_hw) & SDMMC_INTMASK_IO_SLOT1) != 0U) {
		sdmmc_ll_clear_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1);
		SIL_UNL_INT();
		(void) pol_sem(P4SDIO_SEM_IO);
		return(E_OK);
	}
	sdmmc_ll_enable_interrupt(s_hw, SDMMC_INTMASK_IO_SLOT1, true);
	SIL_UNL_INT();

	return(twai_sem(P4SDIO_SEM_IO, tmo));
#endif
}

uint32_t
p4sdio_last_resp(void)
{
	return(s_last_resp);
}

uint32_t
p4sdio_last_err(void)
{
	return(s_last_err);
}

uint16_t
p4sdio_rca(void)
{
	return(s_rca);
}

uint32_t
p4sdio_r4(void)
{
	return(s_r4);
}
