/*
 *  TOPPERS/FMP3 ESP32-C5 移植 --
 *  Wi-Fi blob 用 CLIC 割込み線シム（計画 3 段4 Task 3、2026-09-16）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  何をするか（esp_shim_intr_c5.h も参照）:
 *   - blob は wifi_osi_funcs_t の _set_intr(cpu, src, intr_num, prio) で
 *     「ソース src を CPU 割込み intr_num へ」と要求する（intr_num は blob が
 *     自分で選ぶ小さな番号。C6 の実測は intr_num=1 に src 0(WIFI_MAC)/2(WIFI_PWR)）。
 *     FMP3 C5 の INTNO は CLIC 線番号そのものなので、intr_num をそのまま線に
 *     すると内部線（0..15、割込みマトリクスから駆動できない）になる。
 *     ESP_SHIM_C5_WIFI_LINE(intr_num) = 24 + intr_num（25..39）へ写像し、
 *     割込みマトリクス MAP へは kernel の esp32c5_intmtx_route()（MAP 値 = CLIC
 *     線番号、asp3 実施02/04 で確定）で書く。
 *   - 線 25..39 は esp_shim_intr_c5.cfg が CFG_INT/DEF_INH で静的に宣言する
 *     （CFG_INT は静的 API のみ。ena_int は cfg に無い線を受け付けない）。
 *     DEF_INH の入口 esp_shim_c5_wifi_inthdr_n は esp_shim.c の
 *     esp_shim_wifi_int_dispatch(n)（shim_isr_tbl[n] = blob が _set_isr で
 *     登録したハンドラ）へ戻す。**shim_isr_tbl の添字は blob の intr_num（1..15）
 *     のまま**（esp_shim_set_isr(n, ...) が S3/LX6/C6 と同じ添字で登録する）。
 *   - _ints_on/_ints_off(mask) は bit n ごとに線 24+n の CLIC IE バイトを書く
 *     （asp3 esp/c5/wifi_v8/esp_wifi_adapter.c:210-240 と同じ意味。kernel の
 *     ena_int/dis_int が触るのと同じビットなので割込み禁止下で）。
 *   - 優先度は cfg の -2 に固定（blob の prio は無視。S3/C6/asp3 と同じ）。
 *     ATTR（level/machine/SHV=0）は chip 層が設定済み（chip_kernel_impl.c）。
 *
 *  出典: asp3 esp/c5/wifi_v8/esp_wifi_adapter.c:130-240（CLIC_LINE / CTL / IE）、
 *  dev esp/shim/esp_shim_intr_intmtx.c（帳簿、dispatch、診断の型）。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_shim_intr_c5.h"

/*
 *  kernel の esp32c5_intmtx_route()（fmp3/arch/riscv_gcc/esp32c5/chip_kernel_impl.c）。
 *  kernel_rename で _kernel_esp32c5_intmtx_route になる（chip_rename.def）。
 *  kernel_impl.h を shim TU から include しない = C6 の esp_shim_intr_intmtx.c と同じ形。
 */
extern void _kernel_esp32c5_intmtx_route(uint_t intsrc, INTNO intno);

extern void esp_shim_wifi_int_dispatch(int intno);		/* esp_shim.c（C6/C5 分岐） */

/*  帳簿（診断用。線 n（1..15）に最後に配線したソース。-1 = 未配線。.data 初期化）  */
static int		c5wifi_src_of[ESP_SHIM_C5_WIFI_INTR_NUM_MAX + 1] = {
	[0 ... ESP_SHIM_C5_WIFI_INTR_NUM_MAX] = -1
};
static uint32_t	c5wifi_n_route, c5wifi_n_unroute, c5wifi_n_reject, c5wifi_n_set_isr;
static uint32_t	c5wifi_n_ints_on, c5wifi_n_ints_off, c5wifi_n_stray;

static bool_t
c5wifi_num_ok(int n)
{
	return(n >= ESP_SHIM_C5_WIFI_INTR_NUM_MIN && n <= ESP_SHIM_C5_WIFI_INTR_NUM_MAX);
}

int
esp_shim_c5_wifi_route(int intr_source, int intr_num)
{
	if (!c5wifi_num_ok(intr_num) || intr_source < 0 || intr_source >= (int) ESP32C5_TNUM_INTSRC) {
		c5wifi_n_reject++;
		syslog(LOG_NOTICE, "esp_shim_intr_c5: route src=%d intno=%d rejected (intno 1..15, src < %d)",
			   (int_t) intr_source, (int_t) intr_num, (int_t) ESP32C5_TNUM_INTSRC);
		return(-1);
	}
	_kernel_esp32c5_intmtx_route((uint_t) intr_source, (INTNO) ESP_SHIM_C5_WIFI_LINE(intr_num));
	c5wifi_src_of[intr_num] = intr_source;
	c5wifi_n_route++;
	return(0);
}

int
esp_shim_c5_wifi_unroute(int intr_source, int intr_num)
{
	if (intr_source < 0 || intr_source >= (int) ESP32C5_TNUM_INTSRC) {
		c5wifi_n_reject++;
		return(-1);
	}
	/*  切断 = MAP を 0（CLIC 線 0 = 内部線、IE=0）へ。chip_initialize と同じ扱い。
	 *  kernel に unroute の口は無いので MAP を直接書く（esp32c5.h INTMTX_MAP）。  */
	sil_wrw_mem(INTMTX_MAP(intr_source), 0U);
	if (c5wifi_num_ok(intr_num) && c5wifi_src_of[intr_num] == intr_source) {
		c5wifi_src_of[intr_num] = -1;
	}
	c5wifi_n_unroute++;
	return(0);
}

int
esp_shim_c5_wifi_set_isr(int32_t n, void *f, void *arg)
{
	ER	ercd;

	c5wifi_n_set_isr++;
	if (!c5wifi_num_ok((int) n)) {
		syslog(LOG_NOTICE, "esp_shim_intr_c5: set_isr intno=%d outside 1..15 (not enabled)", (int_t) n);
		return((int) E_PAR);
	}
	esp_shim_set_isr(n, f, arg);		/* shim_isr_tbl[n]（添字は blob の番号） */
	if (f == NULL) {
		return((int) E_OK);
	}
	ercd = ena_int((INTNO) ESP_SHIM_C5_WIFI_LINE(n));
	if (ercd != E_OK) {
		syslog(LOG_NOTICE, "esp_shim_intr_c5: ena_int line=%d (intno %d) -> %d (FAILED)",
			   (int_t) ESP_SHIM_C5_WIFI_LINE(n), (int_t) n, (int_t) ercd);
	}
	return((int) ercd);
}

/*
 *  CLIC_INT_CTRL(line) の IE バイト（bit 8 = +1 バイト目）。esp32c5.h の
 *  CLIC_INT_IE_BIT と同じビット。asp3 の CLIC_IE_OFF(line) と同じ番地。
 */
static void
c5wifi_set_ie(uint32_t mask, uint8_t val)
{
	uint32_t	lock = esp_shim_int_disable();
	int			n;

	for (n = ESP_SHIM_C5_WIFI_INTR_NUM_MIN; n <= ESP_SHIM_C5_WIFI_INTR_NUM_MAX; n++) {
		if ((mask & (1UL << n)) != 0U) {
			sil_wrb_mem((uint8_t *) CLIC_INT_CTRL(ESP_SHIM_C5_WIFI_LINE(n)) + 1, val);
		}
	}
	esp_shim_int_restore(lock);
}

void
esp_shim_c5_wifi_ints_on(uint32_t mask)
{
	c5wifi_n_ints_on++;
	c5wifi_set_ie(mask, 1U);
}

void
esp_shim_c5_wifi_ints_off(uint32_t mask)
{
	c5wifi_n_ints_off++;
	c5wifi_set_ie(mask, 0U);
}

void
esp_shim_c5_wifi_diag_dump(void)
{
#if defined(A1_C5_WIFI_DIAG)
	int	n;

	syslog(LOG_NOTICE, "[C5-INTR] route=%u unroute=%u reject=%u set_isr=%u",
		   (uint_t) c5wifi_n_route, (uint_t) c5wifi_n_unroute,
		   (uint_t) c5wifi_n_reject, (uint_t) c5wifi_n_set_isr);
	syslog(LOG_NOTICE, "[C5-INTR] ints_on=%u ints_off=%u stray=%u",
		   (uint_t) c5wifi_n_ints_on, (uint_t) c5wifi_n_ints_off, (uint_t) c5wifi_n_stray);
	for (n = ESP_SHIM_C5_WIFI_INTR_NUM_MIN; n <= ESP_SHIM_C5_WIFI_INTR_NUM_MAX; n++) {
		if (c5wifi_src_of[n] >= 0) {
			syslog(LOG_NOTICE, "[C5-INTR] intno %d -> line %d src=%d map=0x%08x ctrl=0x%08x",
				   (int_t) n, (int_t) ESP_SHIM_C5_WIFI_LINE(n), (int_t) c5wifi_src_of[n],
				   (uint_t) sil_rew_mem(INTMTX_MAP(c5wifi_src_of[n])),
				   (uint_t) sil_rew_mem(CLIC_INT_CTRL(ESP_SHIM_C5_WIFI_LINE(n))));
		}
	}
#endif /* A1_C5_WIFI_DIAG */
}

/*
 *  DEF_INH の入口。線 24+n の割込みは blob の intr_num n として
 *  shim_isr_tbl[n] へ配送する（esp_shim.c の shim_int_dispatch は
 *  esp_shim_int_count[n] 等の既存の診断をそのまま使う）。
 */
static void
c5wifi_dispatch(int n)
{
	if (c5wifi_src_of[n] < 0) {
		c5wifi_n_stray++;		/* 配線していない線の発火（数えるだけ） */
	}
	esp_shim_wifi_int_dispatch(n);
}

void esp_shim_c5_wifi_inthdr_1(void)  { c5wifi_dispatch(1); }
void esp_shim_c5_wifi_inthdr_2(void)  { c5wifi_dispatch(2); }
void esp_shim_c5_wifi_inthdr_3(void)  { c5wifi_dispatch(3); }
void esp_shim_c5_wifi_inthdr_4(void)  { c5wifi_dispatch(4); }
void esp_shim_c5_wifi_inthdr_5(void)  { c5wifi_dispatch(5); }
void esp_shim_c5_wifi_inthdr_6(void)  { c5wifi_dispatch(6); }
void esp_shim_c5_wifi_inthdr_7(void)  { c5wifi_dispatch(7); }
void esp_shim_c5_wifi_inthdr_8(void)  { c5wifi_dispatch(8); }
void esp_shim_c5_wifi_inthdr_9(void)  { c5wifi_dispatch(9); }
void esp_shim_c5_wifi_inthdr_10(void) { c5wifi_dispatch(10); }
void esp_shim_c5_wifi_inthdr_11(void) { c5wifi_dispatch(11); }
void esp_shim_c5_wifi_inthdr_12(void) { c5wifi_dispatch(12); }
void esp_shim_c5_wifi_inthdr_13(void) { c5wifi_dispatch(13); }
void esp_shim_c5_wifi_inthdr_14(void) { c5wifi_dispatch(14); }
void esp_shim_c5_wifi_inthdr_15(void) { c5wifi_dispatch(15); }
