/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: クロック面（periph_rcc / clk_tree / MPLL）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  RMII の 50MHz がどこから来るか（IDF v5.5.4 の現物・一次資料）
 *  ============================================================================
 *  `esp_eth_mac_esp.c::emac_config_pll_clock()`（:499-531）の P4 の枝:
 *
 *      periph_rtc_mpll_acquire();
 *      ret = periph_rtc_mpll_freq_set(50MHz * 2, &real_freq);   // 100MHz を要求
 *      if (ret == ESP_ERR_INVALID_STATE) { ...「既に占有されている」…  }
 *      if (real_freq > 50MHz) {
 *          div = real_freq / 50MHz;
 *          clk_ll_pll_f50m_set_divider(div);
 *          real_freq /= div;
 *      }
 *      esp_clk_tree_enable_src(SOC_MOD_CLK_PLL_F50M, true);
 *      // 許容: abs(real_freq - 50MHz) <= 2500  (＝ 50MHz で 50ppm)
 *
 *  経路は **XTAL(40MHz) → MPLL → /div → PLL_F50M ゲート → RMII** で、
 *  途中に源の選択は無い（F50M は無条件に MPLL 由来）。
 *
 *  ============================================================================
 *  本シムの方針: **MPLL は設定せず、現在値を読んで返す**
 *  ============================================================================
 *  seam は実 ESP-IDF 2nd-stage bootloader から制御を受け取る。bootloader は
 *  MSPI（flash/PSRAM）のために MPLL を既に構成しており、**その上で XIP 実行
 *  している**——ここで MPLL を勝手に張り替えると、自分が実行している
 *  flash の読み出しごと壊す。
 *
 *  ⇒ `periph_rtc_mpll_freq_set()` は **`clk_ll_mpll_get_freq_mhz()` で
 *    現在の MPLL 周波数を読み、`ESP_ERR_INVALID_STATE` を返す**。
 *    これは IDF 本体の「他に参照者が居て別の周波数を要求された」枝と
 *    **同じ戻り値・同じ意味（`*real_freq` は現に出ている値）**であり、
 *    呼び手 `emac_config_pll_clock()` はその枝を正しく処理する
 *    （警告を 1 行出して、実測値から分周比を決める）。
 *
 *  **未確認だったこと**: bootloader が MPLL を何 MHz で回しているか。
 *  読んだ値は `p4shim_mpll_freq_hz` に記録し、実機ログへ出す（AC §9 M-1）。
 *  50MHz の整数倍でなければ `abs(real-50M) <= 2500` の検査に落ちて
 *  `esp_eth_driver_install` が失敗する＝**黙って劣化しない**。
 */

#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4shim.h"

#include "soc/soc_caps.h"
#include "soc/clk_tree_defs.h"
#include "hal/clk_tree_ll.h"
#include "hal/clk_gate_ll.h"
#include "esp_clk_tree.h"
/*
 *  **IDF の公開ヘッダを include してプロトタイプの照合をコンパイラにさせる。**
 *  怠ると「リンクは通り、ビルドも通り、実機でだけ壊れる」型の失敗になる
 *  ——run1 で実際に踏んだ（`gpio_func_sel` を void で書き、呼び手が
 *  戻り値を見ていたため a0 のゴミが非 0 と読まれ、SMI 初期化が失敗した）。
 */
#include "esp_private/periph_ctrl.h"
#include "clk_ctrl_os.h"

#ifndef MHZ
#define MHZ		(1000000)
#endif

/*
 *  ============================================================================
 *  `periph_rcc_enter` / `periph_rcc_exit`
 *  ============================================================================
 *  IDF 本体（`esp_hw_support/periph_ctrl.c`）は
 *  `portENTER_CRITICAL_SAFE(&periph_spinlock)` の対である。P4 の RISC-V SMP
 *  FreeRTOS ポートでは `portENTER_CRITICAL_SAFE` が**引数のロックを捨てて**
 *  自コアの割込み閾値マスクと非 atomic な入れ子カウンタになるので、
 *  上流でもコア間の相互排除にはなっていない。
 *
 *  本シムは `loc_cpu`/`unl_cpu`（自コア割込み禁止）で置き換える。
 *  **入れ子に耐える**必要がある（`PERIPH_RCC_ATOMIC()` は入れ子で使われ得る）
 *  ので、深さを数えて最外だけが解除する。
 *
 *  【射程を書いておく】これは**自コア内の排他**であって、コア間ではない。
 *  `HP_SYS_CLKRST` は SHA/MIPI-CSI 等と共有するレジスタなので、PRC2 から
 *  クロックゲートを触る利用者が現れたら、`esp/shim/esp_shim_xcore_crit.h`
 *  （CAS ベース mux）へ載せ替えること。現状の利用者は EMAC の init/deinit
 *  だけで、それは PRC1 のタスク文脈からしか呼ばれない（C-1 と同じ制約）。
 */
static uint32_t	p4shim_rcc_nest;

void
periph_rcc_enter(void)
{
	if (p4shim_rcc_nest == 0U) {
		(void) loc_cpu();
	}
	p4shim_rcc_nest++;
}

void
periph_rcc_exit(void)
{
	if (p4shim_rcc_nest > 0U) {
		p4shim_rcc_nest--;
		if (p4shim_rcc_nest == 0U) {
			(void) unl_cpu();
		}
	}
}

/*
 *  ============================================================================
 *  `regi2c_ctrl_read_reg_mask` / `efuse_hal_chip_revision`
 *  ============================================================================
 *  この 2 本は**自分で呼びたくて置いたのではない**。`clk_ll_cpll_get_freq_mhz()`
 *  と `clk_ll_mpll_get_freq_mhz()`（どちらも static inline）が PLL の分周値を
 *  **アナログ側のレジスタ（regi2c）**から読み、その番地がチップリビジョンで
 *  分かれる（`hal/esp32p4/include/hal/clk_tree_ll.h:351,386`）ためである。
 *  ⇒ **リンクの穴は「呼びたい関数」ではなく inline の内側から開く。**
 *  実測（初回リンク）で出た未解決はこの 2 本だけだった。
 *
 *  `regi2c_ctrl_read_reg_mask` の本体（`esp_hw_support/regi2c_ctrl.c:31-39`）は
 *      REGI2C_CLOCK_ENABLE(); portENTER_CRITICAL_SAFE(&mux);
 *      v = regi2c_read_reg_mask_raw(...);
 *      portEXIT_CRITICAL_SAFE(&mux); REGI2C_CLOCK_DISABLE();
 *  で、`regi2c_read_reg_mask_raw` は `esp_rom_regi2c_read_mask`（P4 では ROM
 *  ではなく `esp_rom/patches/esp_rom_regi2c_esp32p4.c` の実装。未定義は
 *  `__assert_func` と `LPPERI` だけ＝**自己完結**なので、そのファイルだけ
 *  リンクする）。臨界区間は `loc_cpu`/`unl_cpu` へ置き換える。
 *
 *  **クロックゲートを落とさない**: 上流は読み終わりに `REGI2C_CLOCK_DISABLE()`
 *  するが、本シムは**有効化したまま**にする。seam では bootloader が既に
 *  アナログ側を使っており、我々が「最後の利用者」かどうかを知る術が無い
 *  （上流はカウンタを持つ）。落として他所を壊すより、点けたままにする方が安全側。
 *  そう判断したことをここに書いておく。
 */
#include "hal/regi2c_ctrl_ll.h"
#include "hal/efuse_ll.h"

extern uint8_t esp_rom_regi2c_read_mask(uint8_t block, uint8_t host_id,
										uint8_t reg_add, uint8_t msb, uint8_t lsb);

uint8_t
regi2c_ctrl_read_reg_mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
						  uint8_t msb, uint8_t lsb)
{
	uint8_t	v;

	(void) loc_cpu();
	if (!regi2c_ctrl_ll_master_is_clock_enabled()) {
		_regi2c_ctrl_ll_master_enable_clock(true);
	}
	v = esp_rom_regi2c_read_mask(block, host_id, reg_add, msb, lsb);
	(void) unl_cpu();
	return v;
}

uint32_t
efuse_hal_chip_revision(void)
{
	return efuse_ll_get_chip_wafer_version_major() * 100U
			+ efuse_ll_get_chip_wafer_version_minor();
}

/*
 *  ============================================================================
 *  `esp_clk_tree_enable_src`
 *  ============================================================================
 *  IDF 本体（`port/esp32p4/esp_clk_tree.c:99-131`）は `enable == false` を
 *  **現状 no-op にしている**（参照カウンタ未実装という TODO つき）。
 *  ここも同じにする——**上流と違う挙動を黙って入れない**。
 *
 *  Ethernet が使うのは `SOC_MOD_CLK_PLL_F50M` だけ。他は「黙って成功に
 *  しない」ため `ESP_ERR_NOT_SUPPORTED` を返して数える。
 *
 *  `clk_gate_ll_ref_50m_clk_en()` はマクロで `__DECLARE_RCC_ATOMIC_ENV` を
 *  要求する（`PERIPH_RCC_ATOMIC()` の外で呼ぶとコンパイルが通らない仕掛け）。
 *  本シムは `periph_rcc_enter/exit` で同じ保護を自前で掛けるので、
 *  **下線つきの実体 `_clk_gate_ll_ref_50m_clk_en()` を直接呼ぶ**。
 */
esp_err_t
esp_clk_tree_enable_src(soc_module_clk_t clk_src, bool enable)
{
	if (!enable) {
		return ESP_OK;			/* 上流と同じ（参照カウンタ未実装） */
	}
	/*
	 *  **段v-2（2026-08-16）で 2 源を追加**した。MIPI-DSI が要求する:
	 *    PLL_F20M  … PHY PLL の基準（rev v1.3 は `..._DEFAULT_LEGACY`＝F20M。
	 *                 XTAL 直結は rev >= 3.0 の話。`clk_tree_defs.h:449-461`）
	 *    PLL_F240M … DPI ピクセルクロックの源（同 :433）
	 *  どちらも SPLL(480MHz) 由来で、**MPLL（PSRAM）とも CPLL（CPU）とも独立**。
	 */
	switch ((int) clk_src) {
	case SOC_MOD_CLK_PLL_F50M:
		periph_rcc_enter();
		_clk_gate_ll_ref_50m_clk_en(true);
		periph_rcc_exit();
		return ESP_OK;
	case SOC_MOD_CLK_PLL_F20M:
		periph_rcc_enter();
		_clk_gate_ll_ref_20m_clk_en(true);
		periph_rcc_exit();
		return ESP_OK;
	case SOC_MOD_CLK_PLL_F240M:
		periph_rcc_enter();
		_clk_gate_ll_ref_240m_clk_en(true);
		periph_rcc_exit();
		return ESP_OK;
	default:
		p4shim_n_clk_unsupported++;
		return ESP_ERR_NOT_SUPPORTED;
	}
}

/*
 *  ============================================================================
 *  `esp_clk_tree_src_get_freq_hz`
 *  ============================================================================
 *  Ethernet が呼ぶのは `SOC_MOD_CLK_SYS` 1 通りだけ（`emac_ll_get_csr_clk_src()`
 *  が P4 では SYS を返す。`hal/esp32p4/include/hal/emac_ll.h:819-823`）。
 *  値は MDC（SMI クロック）の分周比の決定に使われる
 *  （`emac_hal_set_csr_clock_range`）。**大きく外すと SMI が通らない**ので、
 *  定数で持たずに**レジスタから計算する**。
 *
 *  計算式は `hal/esp32p4/clk_tree_hal.c` の
 *      CPU  = root_freq(src) * denominator / (integer*denominator + numerator)
 *      MEM  = CPU / clk_ll_mem_get_divider()
 *      SYS  = MEM / clk_ll_sys_get_divider()
 *  をそのまま写したもの（使う LL はすべて static inline）。
 *  こうしておくと、seam の 360MHz 昇圧（`esp/boot/seam_p4_clk.c`）を後から
 *  変えても**自動で追随する**（定数で持つと黙ってずれる）。
 */
static uint32_t
p4shim_root_freq_mhz(soc_cpu_clk_src_t src)
{
	switch (src) {
	case SOC_CPU_CLK_SRC_XTAL:
		return clk_ll_xtal_load_freq_mhz();
	case SOC_CPU_CLK_SRC_CPLL:
		return clk_ll_cpll_get_freq_mhz(clk_ll_xtal_load_freq_mhz());
	case SOC_CPU_CLK_SRC_RC_FAST:
		return SOC_CLK_RC_FAST_FREQ_APPROX / MHZ;
	default:
		return 0U;
	}
}

static uint32_t
p4shim_cpu_freq_hz(void)
{
	soc_cpu_clk_src_t	src = clk_ll_cpu_get_src();
	uint32_t			integer, numerator, denominator;
	uint32_t			root_mhz = p4shim_root_freq_mhz(src);

	if (root_mhz == 0U) {
		return 0U;
	}
	clk_ll_cpu_get_divider(&integer, &numerator, &denominator);
	if (denominator == 0U) {
		denominator = 1U;
		numerator = 0U;
	}
	if ((integer * denominator + numerator) == 0U) {
		return 0U;
	}
	return (uint32_t) (((uint64_t) root_mhz * MHZ * denominator)
					   / (integer * denominator + numerator));
}

esp_err_t
esp_clk_tree_src_get_freq_hz(soc_module_clk_t clk_src,
							 esp_clk_tree_src_freq_precision_t precision,
							 uint32_t *out_freq_hz)
{
	uint32_t	cpu_hz;
	uint32_t	mem_div;
	uint32_t	sys_div;

	(void) precision;			/* SYS は常に実測計算（上流も同じ） */
	if (out_freq_hz == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	/*
	 *  段v-2 追加: DSI は PHY 基準（F20M）と DPI 源（F240M）の**周波数**を聞く。
	 *  どちらも SPLL(480MHz) を固定比で割ったもので、名前が値そのものである
	 *  （`clk_tree_defs.h` の SOC_CLK_* 定義）。SYS のようにレジスタから
	 *  計算する必要は無い——**分周器が可変なのは SYS 系だけ**である。
	 */
	if (clk_src == SOC_MOD_CLK_PLL_F20M) {
		*out_freq_hz = 20U * MHZ;
		return ESP_OK;
	}
	if (clk_src == SOC_MOD_CLK_PLL_F240M) {
		*out_freq_hz = 240U * MHZ;
		return ESP_OK;
	}
	if (clk_src != SOC_MOD_CLK_SYS) {
		p4shim_n_clk_unsupported++;
		return ESP_ERR_NOT_SUPPORTED;
	}
	cpu_hz = p4shim_cpu_freq_hz();
	mem_div = clk_ll_mem_get_divider();
	sys_div = clk_ll_sys_get_divider();
	if (cpu_hz == 0U || mem_div == 0U || sys_div == 0U) {
		p4shim_n_clk_unsupported++;
		return ESP_FAIL;
	}
	*out_freq_hz = cpu_hz / mem_div / sys_div;
	p4shim_sys_freq_hz = *out_freq_hz;
	return ESP_OK;
}

/*
 *  ============================================================================
 *  MPLL（本ファイル冒頭の方針どおり「読むだけ」）
 *  ============================================================================
 *  **例外（段v-2・2026-08-16）**: `A1_P4_PSRAM=ON`（＝`P4SHIM_PSRAM` が定義
 *  される）ビルドでは、PSRAM が MPLL を **実際に 400MHz へ設定する**必要が
 *  あるので、この 3 本は `esp/p4shim/p4shim_psram.c` の実装に譲る。
 *  ここで両方が定義されると多重定義になるため `#if` で落とす。
 *  「なぜ張り替えてよいのか」（flash は SPLL・CPU は CPLL 由来）は
 *  `p4shim_psram.c` 冒頭に一次情報つきで書いた。
 */
#if !defined(P4SHIM_PSRAM)
esp_err_t
periph_rtc_mpll_acquire(void)
{
	return ESP_OK;
}

void
periph_rtc_mpll_release(void)
{
	/*  取得していないので、返すものが無い。  */
}

/*
 *  `expt_freq_hz` は **Hz**（`clk_ctrl_os.h:77-99`。MHz ではない）。
 *  本シムは設定せず、現在の MPLL 周波数を `*real_freq_hz` へ書いて
 *  `ESP_ERR_INVALID_STATE`（＝「既に占有されている」）を返す。
 */
esp_err_t
periph_rtc_mpll_freq_set(uint32_t expt_freq_hz, uint32_t *real_freq_hz)
{
	uint32_t	mpll_mhz;

	(void) expt_freq_hz;
	mpll_mhz = clk_ll_mpll_get_freq_mhz(clk_ll_xtal_load_freq_mhz());
	p4shim_mpll_freq_hz = mpll_mhz * MHZ;
	p4shim_n_mpll_notouch++;
	if (real_freq_hz != NULL) {
		*real_freq_hz = p4shim_mpll_freq_hz;
	}
	return ESP_ERR_INVALID_STATE;
}
#endif /* !defined(P4SHIM_PSRAM) */

/*
 *  ----------------------------------------------------------------------------
 *  RMII の分周結果を記録するための小さな包み
 *  ----------------------------------------------------------------------------
 *  `emac_config_pll_clock()` は `clk_ll_pll_f50m_set_divider()`（static inline）を
 *  直接呼ぶので、本シムからは見えない。**分周比を後から確かめられるように**、
 *  レジスタを読み返す口をここに置く（`p4shim_report()` が使う）。
 *  「書けた」を書いた側の主張ではなくレジスタから読む、という C-1 の作法。
 */
void
p4shim_clk_snapshot_f50m(void)
{
	uint32_t	div;

	/*  `clk_ll_pll_f50m_set_divider` が書く先: ref_clk_ctrl0.reg_ref_50m_clk_div_num
	 *  （書く値は divider-1）。読み返して +1 したものが実効分周比。  */
	div = HP_SYS_CLKRST.ref_clk_ctrl0.reg_ref_50m_clk_div_num + 1U;
	p4shim_f50m_div = div;
	if (p4shim_mpll_freq_hz != 0U && div != 0U) {
		p4shim_f50m_freq_hz = p4shim_mpll_freq_hz / div;
	}
}
