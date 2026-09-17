/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  SDIO スレーブ（ESP32-C6）のボード側電源・リセット制御
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
 *  なぜこのファイルが要るか（移植元との差 D-8）
 *  ============================================================================
 *  移植元（P4 repo `wifi_p4_module/sdio_host/fmp3_sdmmc.c:792-814`）は
 *  「GPIO42 を叩けば C6 が起きる」だけの世界だった（AddOn C6 は常時給電）。
 *  Tab5 は違う——**C6 の電源が I2C IO エキスパンダ（0x44）の配下**にあり、
 *  電源を入れないと SDIO は 1 ビットも動かない。
 *  ⇒ 電源経路をボード層として分離し、ドライバ本体をボード非依存に保つ。
 *
 *  ============================================================================
 *  Tab5 の一次情報（2026-08-16）
 *  ============================================================================
 *  回路図 `Tab5_Schematics_PDF.pdf` p.1（U7 = PI4IOE5V6408 @0x44）:
 *      P0=`WLAN_PWR_EN` / P1,P2=NC / P3=`USB5V_EN` / **P4=`PWROFF_PLUSE`** /
 *      P5=`nCHG_QC_EN` / P6=`CHG_STAT` / P7=`CHG_EN`
 *  公式ファクトリデモ `M5Tab5-UserDemo`（HEAD 68b19d37）
 *  `platforms/tab5/components/m5stack_tab5/m5stack_tab5.c`:
 *      :297-299 `IO_DIR(0x03)   = 0b10111001`（P0/P3/P4/P5/P7 を出力）
 *      :300-303 `OUT_H_IM(0x07) = 0b00000110`（P1,P2 だけ Hi-Z＝P0 は駆動）
 *      :319-323 `OUT_SET(0x05)  = 0b00001001` かつコメントに
 *               `Output Port Register P0(WLAN_PWR_EN), P3(USB5V_EN), P7(CHG_EN)`
 *      :482-505 `bsp_set_wifi_power_enable(bool)` が `OUT_SET` の **bit0** を RMW
 *
 *  ⇒ **C6 の電源 = 0x44 の Output(0x05) bit0**。これは 2 つの一次情報が
 *     一致して指している。
 *
 *  ============================================================================
 *  安全（段 7a AC §5。**0x44 へ書くのは本 repo で初めて**）
 *  ============================================================================
 *  段v-2（表示）は 0x43 の bit4 だけを触り、0x44 は**読むだけ**にした。
 *  本ファイルはその 0x44 へ書く。触るのは以下の 3 ビットだけである:
 *      0x07 High-Z  bit0 = 0   （P0 を駆動する）
 *      0x03 Dir     bit0 = 1   （P0 を出力に）
 *      0x05 Out     bit0 = 1   （WLAN_PWR_EN を on）
 *  すべて read-modify-write。**それ以外は 1 ビットも触らない。**
 *
 *  触ってはならないもの（`s_exp_write()` が機械で拒否する）:
 *      0x44 reg 0x05 の **bit4 = `PWROFF_PLUSE`** … 立てると**板の電源が落ちる**
 *          （`bsp_generate_poweroff_signal()` `m5stack_tab5.c:402-423` が
 *            まさにこのビットを 3 回パルスさせて電源断を作っている）
 *      0x44 reg 0x01 … デバイス ID 兼**ソフトウェアリセット**
 *      0x43 の全レジスタ … 表示段の領分（本ファイルは 0x43 へ書かない）
 */

#include <kernel.h>
#include <sil.h>
#include <string.h>
#include "p4sdio_board.h"
#include "p4sdio_pins.h"

#if P4SDIO_PWR_KIND == P4SDIO_PWR_EXPANDER
#include "p4disp_i2c.h"		/* 段v-2 の GPIO ビットバン I2C を再利用 */
#endif

/*  p4sdio_host.c の同期出力と GPIO ヘルパ（両バンク対応）  */
extern void		p4sdio_puts(const char *s);
extern void		p4sdio_put_kv(const char *k, uint32_t v);
extern void		p4sdio_gpio_out_init(uint32_t gpio);
extern void		p4sdio_gpio_out_set(uint32_t gpio, bool level);

volatile uint32_t	p4sdio_board_n_write;
volatile uint32_t	p4sdio_board_n_guard_hit;
volatile uint32_t	p4sdio_board_n_other_dev;
volatile uint32_t	p4sdio_exp_before[8];
volatile uint32_t	p4sdio_exp_after[8];

#if P4SDIO_PWR_KIND == P4SDIO_PWR_EXPANDER

/*  PI4IOE5V6408 のレジスタ  */
#define PI4_REG_ID			0x01	/* Device ID / Software Reset */
#define PI4_REG_DIR			0x03	/* Direction: 1=output */
#define PI4_REG_OUT			0x05	/* Output Port */
#define PI4_REG_HIZ			0x07	/* Output High-Impedance: 1=Hi-Z */
#define PI4_REG_INDEF		0x09
#define PI4_REG_PULLEN		0x0B
#define PI4_REG_PULLSEL		0x0D
#define PI4_REG_IN			0x0F

static const uint8_t	s_snap_regs[8] = {
	PI4_REG_ID, PI4_REG_DIR, PI4_REG_OUT, PI4_REG_HIZ,
	PI4_REG_INDEF, PI4_REG_PULLEN, PI4_REG_PULLSEL, PI4_REG_IN
};

/*  【禁止】0x05 で立ててはならないビット（P4 = PWROFF_PLUSE）  */
#define PI4_FORBIDDEN_OUT_BITS	0x10U

static bool		s_i2c_ready;

static void
s_i2c_setup(void)
{
	if (!s_i2c_ready) {
		p4disp_i2c_init(P4SDIO_PWR_I2C_SDA, P4SDIO_PWR_I2C_SCL);
		s_i2c_ready = true;
	}
}

/*
 *  拡張器への 1 バイト書込み（**fail-closed ガード付き**）
 *    - 想定外のデバイスアドレスなら書かない
 *    - reg 0x01（ソフトウェアリセット）へは書かない
 *    - reg 0x05 で PWROFF_PLUSE を立てる値なら書かない
 */
static bool
s_exp_write(uint8_t addr7, uint8_t reg, uint8_t val)
{
	if (addr7 != (uint8_t) P4SDIO_PWR_ADDR) {
		p4sdio_board_n_other_dev++;
		p4sdio_board_n_guard_hit++;
		p4sdio_puts("P4SDIO NG: 想定外の I2C デバイスへの書込みを拒否した");
		return(false);
	}
	if (reg == PI4_REG_ID) {
		p4sdio_board_n_guard_hit++;
		p4sdio_puts("P4SDIO NG: 拡張器 reg0x01（ソフトリセット）への書込みを拒否した");
		return(false);
	}
	if (reg == PI4_REG_OUT && (val & PI4_FORBIDDEN_OUT_BITS) != 0U) {
		p4sdio_board_n_guard_hit++;
		p4sdio_puts("P4SDIO NG: PWROFF_PLUSE(bit4) を立てる書込みを拒否した");
		return(false);
	}
	if (!p4disp_i2c_write_reg(addr7, reg, val)) {
		return(false);
	}
	p4sdio_board_n_write++;
	return(true);
}

/*
 *  read-modify-write（1 ビットだけ）
 */
static bool
s_exp_rmw(uint8_t addr7, uint8_t reg, uint8_t mask, bool set)
{
	uint8_t v;

	if (!p4disp_i2c_read_reg(addr7, reg, &v)) {
		return(false);
	}
	if (set) {
		v = (uint8_t)(v | mask);
	}
	else {
		v = (uint8_t)(v & (uint8_t) ~mask);
	}
	return(s_exp_write(addr7, reg, v));
}

static void
s_snapshot(volatile uint32_t *dst)
{
	int i;
	uint8_t v;

	for (i = 0; i < 8; i++) {
		dst[i] = p4disp_i2c_read_reg((uint8_t) P4SDIO_PWR_ADDR, s_snap_regs[i], &v)
					? (uint32_t) v : 0xFFFFFFFFU;
	}
}

int
p4sdio_board_c6_power(bool on)
{
	s_i2c_setup();

	if (on) {
		s_snapshot(p4sdio_exp_before);
	}

	/*  1. High-Z 解除（P0 を駆動する）  */
	if (!s_exp_rmw((uint8_t) P4SDIO_PWR_ADDR, PI4_REG_HIZ,
				   (uint8_t) P4SDIO_PWR_BIT, false)) {
		return(-1);
	}
	/*  2. 出力方向へ  */
	if (!s_exp_rmw((uint8_t) P4SDIO_PWR_ADDR, PI4_REG_DIR,
				   (uint8_t) P4SDIO_PWR_BIT, true)) {
		return(-2);
	}
	/*  3. WLAN_PWR_EN  */
	if (!s_exp_rmw((uint8_t) P4SDIO_PWR_ADDR, PI4_REG_OUT,
				   (uint8_t) P4SDIO_PWR_BIT, on)) {
		return(-3);
	}

	/*  電源の立上り待ち（C6 モジュールの 3V3 が安定するまで）  */
	dly_tsk(100U * 1000U);		/* 100ms（RELTIM はマイクロ秒単位） */

	s_snapshot(p4sdio_exp_after);
	return(0);
}

void
p4sdio_board_report(void)
{
	int i;

	p4sdio_put_kv("exp_addr", (uint32_t) P4SDIO_PWR_ADDR);
	for (i = 0; i < 8; i++) {
		p4sdio_put_kv("exp_before", p4sdio_exp_before[i]);
	}
	for (i = 0; i < 8; i++) {
		p4sdio_put_kv("exp_after", p4sdio_exp_after[i]);
	}
	p4sdio_put_kv("i2c_n_write", p4disp_i2c_n_write);
	p4sdio_put_kv("i2c_n_read", p4disp_i2c_n_read);
	p4sdio_put_kv("i2c_n_nack", p4disp_i2c_n_nack);
	p4sdio_put_kv("board_n_write", p4sdio_board_n_write);
	p4sdio_put_kv("board_n_guard_hit", p4sdio_board_n_guard_hit);
	p4sdio_put_kv("board_n_other_dev", p4sdio_board_n_other_dev);
}

bool
p4sdio_board_guard_selftest(void)
{
	uint32_t before = p4sdio_board_n_guard_hit;
	uint32_t writes_before = p4sdio_board_n_write;
	bool rejected;

	s_i2c_setup();
	/*
	 *  **positive control**: PWROFF_PLUSE を立てる書込みを意図的に要求する。
	 *  ガードが働けば false が返り、書込み回数は増えない。
	 */
	rejected = !s_exp_write((uint8_t) P4SDIO_PWR_ADDR, PI4_REG_OUT,
							(uint8_t) PI4_FORBIDDEN_OUT_BITS);
	return(rejected && (p4sdio_board_n_guard_hit == before + 1U) &&
		   (p4sdio_board_n_write == writes_before));
}

#else /* P4SDIO_PWR_KIND == P4SDIO_PWR_EXPANDER */

int
p4sdio_board_c6_power(bool on)
{
	(void) on;
	return(0);					/* 常時給電のボード: 何もしない */
}

void
p4sdio_board_report(void)
{
	p4sdio_puts("P4SDIO: 電源制御なし（常時給電のボード）");
}

bool
p4sdio_board_guard_selftest(void)
{
	/*  ガードの対象が無い構成。**「合格」とは言わない**（実演していない）。  */
	return(false);
}

#endif /* P4SDIO_PWR_KIND == P4SDIO_PWR_EXPANDER */

/*
 *  スレーブのハードリセット（active low）
 *    移植元 `fmp3_sdmmc.c:792-814` と同じ手順（esp-hosted の
 *    `transport_gpio_reset` に倣ったもの）:
 *      HIGH 10ms → LOW 10ms（リセット）→ HIGH → 1500ms（slave ブート待ち）
 *    Tab5 も active low（`M5Tab5-UserDemo` `sdkconfig:2881`
 *    `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_LOW=y`）なので同一手順で足りる。
 *
 *    **電源が入ってから呼ぶこと。** 給電前に EN を叩かない。
 */
void
p4sdio_board_slave_reset(void)
{
	p4sdio_gpio_out_init(P4SDIO_SLAVE_RST_GPIO);

	p4sdio_gpio_out_set(P4SDIO_SLAVE_RST_GPIO, true);	/* HIGH */
	dly_tsk(10U * 1000U);
	p4sdio_gpio_out_set(P4SDIO_SLAVE_RST_GPIO, false);	/* LOW = リセット */
	dly_tsk(10U * 1000U);
	p4sdio_gpio_out_set(P4SDIO_SLAVE_RST_GPIO, true);	/* HIGH = 解放 */
	dly_tsk(1500U * 1000U);								/* slave ブート待ち */
}
