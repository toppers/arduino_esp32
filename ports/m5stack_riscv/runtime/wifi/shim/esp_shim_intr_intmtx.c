/*
 *  TOPPERS/FMP3 ESP32-C6 移植 --
 *  INTMTX 版 ESP-IDF 割込み確保シム（esp_intr_alloc 系）と、
 *  Wi-Fi/BT blob 用 CPU 割込み線 1..15 の使用中管理・割込み入口
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
 *  位置づけ（2026-09-14・段4 Task 3）
 *  ============================================================================
 *  型は P4 の esp/shim/esp_shim_intr_clic.c（CLIC 版）。C6 との違い:
 *
 *   (1) 線の集合が「blob が自分で番号を決める線」と共用である。
 *       Wi-Fi blob は osi 表の _set_intr(cpu, source, intno, prio) で
 *       **線番号を指定して**配線を要求し（asp3 C6 実測: src=0(WIFI_MAC)/
 *       src=2(WIFI_PWR) -> 線 1）、_set_isr(intno, fn, arg) でハンドラを
 *       esp_shim.c の shim_isr_tbl[] へ登録する。一方 esp_intr_alloc() は
 *       空いている線を**シムが選んで**払い出す。両者が同じ線を取り合わない
 *       ように、本ファイルが 1..15 の帳簿（owner = blob / alloc）を 1 つ持つ。
 *
 *   (2) 配線は kernel の esp32c6_intmtx_route()（chip_kernel_impl.c。
 *       kernel_rename で _kernel_esp32c6_intmtx_route）へ委譲する。
 *       INTMTX の MAP レジスタへ書くだけでなく、prb_int が読む
 *       intmtx_srcmask[] を更新するため。P4 版は MAP を直接書いていた
 *       （P4 の CLIC は prb_int を別経路で実現している）。
 *
 *   (3) 割込み入口は DEF_INH（esp_shim_intr_intmtx.cfg）。P4 版の CRE_ISR
 *       （exinf にスロット番号）ではなく、S3/LX6 の esp_shim.cfg
 *       （DEF_INH(n, esp_shim_inthdr_n)）と同じ形にした。理由: blob の線は
 *       esp_shim.c の shim_isr_tbl[] 経由で呼ぶ必要があり、S3/LX6 の
 *       esp_shim_inthdr_n と同じ経路（shim_int_dispatch）を通すことで
 *       esp_shim_int_count[] 等の既存の診断がそのまま使えるため。
 *
 *  【C6 Wi-Fi 構成での実利用】esp_intr_alloc() は誰も呼ばない（ヘッダの
 *  コメント参照）。実際に動くのは esp_shim_intmtx_route()（_set_intr 経由）
 *  と esp_shim_intmtx_inthdr_n（線 1）である。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <stddef.h>
#include <stdint.h>

#include "esp32c6.h"					/* ESP32C6_INTMTX_BASE / PLIC_MX_BASE / TNUM_INTSRC */
#include "esp_shim_intr_intmtx.h"
#include "esp_shim_intr_intmtx_lines.h"

/*
 *  kernel 側の配線関数（fmp3/arch/riscv_gcc/esp32c6/chip_kernel_impl.c）。
 *  kernel_rename.h の対象（chip_rename.def）なので、kernel 外の本 TU からは
 *  改名後の名前で呼ぶ。シグネチャは intmtx_kernel_impl.h の
 *  `extern void esp32c6_intmtx_route(uint_t intsrc, INTNO intno);` と同一。
 */
extern void _kernel_esp32c6_intmtx_route(uint_t intsrc, INTNO intno);

/*
 *  blob が _set_isr で登録したハンドラを呼ぶ esp_shim.c 側の入口
 *  （esp_shim.c の shim_int_dispatch() の C6 用ラッパ。
 *  `#if defined(TOPPERS_ESP32C6)` の内側で定義）。
 */
extern void esp_shim_wifi_int_dispatch(int intno);

/*
 *  PLIC_MX のレジスタ（intmtx_kernel_impl.h と同じオフセット。あちらは
 *  kernel 内部ヘッダなので、ここでは esp32c6.h のベースから直接組む）。
 */
#define INTMTX_MAP_REG(src)		((void *)(ESP32C6_INTMTX_BASE + (uint32_t)(src) * 4U))
#define PLICMX_ENABLE_REG		((void *)(ESP32C6_PLIC_MX_BASE + 0x000U))
#define PLICMX_TYPE_REG			((void *)(ESP32C6_PLIC_MX_BASE + 0x004U))
#define PLICMX_PRI_REG(n)		((void *)(ESP32C6_PLIC_MX_BASE + 0x010U + (uint32_t)(n) * 4U))

/*
 *  スロットの所有者
 */
#define OWNER_NONE	0
#define OWNER_BLOB	1				/* _set_intr（esp_shim_intmtx_route）で配線された */
#define OWNER_ALLOC	2				/* esp_intr_alloc() で払い出した */

/*
 *  IDF の intr_handle_t は struct intr_handle_data_t *。実体はここで定義する。
 */
struct intr_handle_data_t {
	uint32_t	idx;				/* スロット番号 */
	uint32_t	valid;				/* free 後の handle を弾くため */
};

struct esp_shim_intmtx_intr_slot {
	uint32_t					owner;
	uint32_t					enabled;	/* alloc スロットのみ意味を持つ */
	int							source;		/* 最後に配線した source（診断用） */
	void						(*handler)(void *);
	void						*arg;
	volatile uint32_t			*status_reg;
	uint32_t					status_mask;
	struct intr_handle_data_t	handle;
};

static struct esp_shim_intmtx_intr_slot	esp_shim_intmtx_intr_slot[ESP_SHIM_INTMTX_INTR_NSLOT];
static volatile uint32_t				esp_shim_intmtx_intr_n_isr[ESP_SHIM_INTMTX_INTR_NSLOT];

volatile uint32_t	esp_shim_intmtx_intr_n_alloc;
volatile uint32_t	esp_shim_intmtx_intr_n_alloc_fail;
volatile uint32_t	esp_shim_intmtx_intr_n_free;
volatile uint32_t	esp_shim_intmtx_intr_n_route;
volatile uint32_t	esp_shim_intmtx_intr_n_route_fail;
volatile uint32_t	esp_shim_intmtx_intr_n_flag_iram;
volatile uint32_t	esp_shim_intmtx_intr_n_flag_shared;
volatile uint32_t	esp_shim_intmtx_intr_n_flag_level;
volatile uint32_t	esp_shim_intmtx_intr_n_flag_edge;
volatile uint32_t	esp_shim_intmtx_intr_n_ena_fail;
volatile uint32_t	esp_shim_intmtx_intr_n_dis_fail;
volatile uint32_t	esp_shim_intmtx_intr_n_unowned_isr;

/*
 *  ============================================================================
 *  内部ヘルパ
 *  ============================================================================
 */

static int
line_in_range(int line)
{
	return((line >= ESP_SHIM_INTMTX_LINE_MIN) && (line <= ESP_SHIM_INTMTX_LINE_MAX));
}

static int
source_in_range(int source)
{
	return((source >= 0) && (source < ESP32C6_TNUM_INTSRC));
}

/*
 *  ena_int()/dis_int() の失敗を捨てない（P4 版と同じ）。
 *  ena_int は check_intno_cfg() が偽なら E_OBJ を返す。線 1..15 は
 *  esp_shim_intr_intmtx.cfg が CFG_INT しているので通るはずで、
 *  通らなければ cfg がリンクされていない（cmake の FMP3_CFG_FILES 漏れ）。
 */
static void
ena_line(int line)
{
	ER	ercd;

	ercd = ena_int((INTNO) line);
	if (ercd != E_OK) {
		esp_shim_intmtx_intr_n_ena_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: ena_int(line=%d) failed ercd=%d"
				 " (line not in cfg? check esp_shim_intr_intmtx.cfg in FMP3_CFG_FILES)",
				 line, (int) ercd);
	}
}

static void
dis_line(int line)
{
	ER	ercd;

	ercd = dis_int((INTNO) line);
	if (ercd != E_OK) {
		esp_shim_intmtx_intr_n_dis_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: dis_int(line=%d) failed ercd=%d",
				 line, (int) ercd);
	}
}

/*
 *  ============================================================================
 *  blob の _set_intr / _clear_intr 用（esp_wifi_adapter.c の C6 分岐が呼ぶ）
 *  ============================================================================
 */
int
esp_shim_intmtx_route(int source, int line)
{
	struct esp_shim_intmtx_intr_slot	*p;

	if (!source_in_range(source) || !line_in_range(line)) {
		esp_shim_intmtx_intr_n_route_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: route source=%d line=%d out of range"
				 " (source 0..76, line 1..15) -> NOT routed",
				 source, line);
		return(-1);
	}
	p = &esp_shim_intmtx_intr_slot[ESP_SHIM_INTMTX_SLOT_OF_LINE(line)];
	if (p->owner == OWNER_ALLOC) {
		esp_shim_intmtx_intr_n_route_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: route source=%d line=%d refused"
				 " (line already handed out by esp_intr_alloc)",
				 source, line);
		return(-1);
	}
	p->owner  = OWNER_BLOB;
	p->source = source;
	_kernel_esp32c6_intmtx_route((uint_t) source, (INTNO) line);
	esp_shim_intmtx_intr_n_route++;
	return(0);
}

int
esp_shim_intmtx_unroute(int source, int line)
{
	if (!source_in_range(source)) {
		esp_shim_intmtx_intr_n_route_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: unroute source=%d line=%d out of range -> ignored",
				 source, line);
		return(-1);
	}
	/*
	 *  MAP を 0 へ（線 0 = 未接続。intmtx_initialize() が起動時に全ソースへ
	 *  書く値と同じ）。帳簿は変えない（ヘッダのコメント参照）。
	 */
	sil_wrw_mem(INTMTX_MAP_REG(source), 0U);
	(void) line;
	return(0);
}

/*
 *  ============================================================================
 *  割込み入口（cfg の DEF_INH から）
 *  ============================================================================
 *  alloc 払い出し済みの線ならそのハンドラを、そうでなければ esp_shim.c の
 *  shim_isr_tbl[]（blob の _set_isr 登録先）を呼ぶ。どちらでもなければ
 *  数えて戻る（黙って落とさない。level 割込みなら storm になるので、
 *  カウンタが伸びていたら「配線したのにハンドラが無い」を疑うこと）。
 */
static void
intmtx_dispatch(int line)
{
	struct esp_shim_intmtx_intr_slot	*p;
	uint32_t							idx = (uint32_t) ESP_SHIM_INTMTX_SLOT_OF_LINE(line);

	p = &esp_shim_intmtx_intr_slot[idx];
	esp_shim_intmtx_intr_n_isr[idx]++;

	if (p->owner == OWNER_ALLOC) {
		if ((p->enabled == 0U) || (p->handler == NULL)) {
			return;
		}
		if (p->status_reg != NULL) {
			if ((*(p->status_reg) & p->status_mask) == 0U) {
				return;				/* 自分宛ではない */
			}
		}
		(*(p->handler))(p->arg);
		return;
	}
	if (p->owner == OWNER_BLOB) {
		esp_shim_wifi_int_dispatch(line);
		return;
	}
	esp_shim_intmtx_intr_n_unowned_isr++;
}

void esp_shim_intmtx_inthdr_1(void)  { intmtx_dispatch(1); }
void esp_shim_intmtx_inthdr_2(void)  { intmtx_dispatch(2); }
void esp_shim_intmtx_inthdr_3(void)  { intmtx_dispatch(3); }
void esp_shim_intmtx_inthdr_4(void)  { intmtx_dispatch(4); }
void esp_shim_intmtx_inthdr_5(void)  { intmtx_dispatch(5); }
void esp_shim_intmtx_inthdr_6(void)  { intmtx_dispatch(6); }
void esp_shim_intmtx_inthdr_7(void)  { intmtx_dispatch(7); }
void esp_shim_intmtx_inthdr_8(void)  { intmtx_dispatch(8); }
void esp_shim_intmtx_inthdr_9(void)  { intmtx_dispatch(9); }
void esp_shim_intmtx_inthdr_10(void) { intmtx_dispatch(10); }
void esp_shim_intmtx_inthdr_11(void) { intmtx_dispatch(11); }
void esp_shim_intmtx_inthdr_12(void) { intmtx_dispatch(12); }
void esp_shim_intmtx_inthdr_13(void) { intmtx_dispatch(13); }
void esp_shim_intmtx_inthdr_14(void) { intmtx_dispatch(14); }
void esp_shim_intmtx_inthdr_15(void) { intmtx_dispatch(15); }

/*
 *  ============================================================================
 *  ESP-IDF 互換 API
 *  ============================================================================
 */
esp_err_t
esp_intr_alloc_intrstatus(int source, int flags,
						  uint32_t intrstatusreg, uint32_t intrstatusmask,
						  void (*handler)(void *), void *arg,
						  struct intr_handle_data_t **ret_handle)
{
	uint32_t							idx;
	uint32_t							found = (uint32_t) ESP_SHIM_INTMTX_INTR_NSLOT;
	struct esp_shim_intmtx_intr_slot	*p;
	int									line;

	/*
	 *  未対応のフラグを黙って捨てない（P4 版と同じ）。
	 *   - LEVELn/NMI/EDGE: CFG_INT が静的なので実行時には効かせられない
	 *     （全線 ESP_SHIM_INTMTX_INTR_INTPRI、level 型）。
	 *   - IRAM: cache 無効期間の扱いは未対応。
	 *   - SHARED: 1 線 1 ソース。
	 */
	if ((flags & ESP_INTR_FLAG_IRAM) != 0) {
		esp_shim_intmtx_intr_n_flag_iram++;
	}
	if ((flags & ESP_INTR_FLAG_SHARED) != 0) {
		esp_shim_intmtx_intr_n_flag_shared++;
	}
	if ((flags & (ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_LEVEL3
				  | ESP_INTR_FLAG_LEVEL4 | ESP_INTR_FLAG_LEVEL5 | ESP_INTR_FLAG_LEVEL6
				  | ESP_INTR_FLAG_NMI)) != 0) {
		esp_shim_intmtx_intr_n_flag_level++;
	}
	if ((flags & ESP_INTR_FLAG_EDGE) != 0) {
		esp_shim_intmtx_intr_n_flag_edge++;
	}

	if (!source_in_range(source)) {
		syslog_1(LOG_ERROR, "esp_shim_intr_intmtx: source=%d out of range", source);
		return(ESP_ERR_INVALID_ARG);
	}
	if (handler == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}

	/*  同じソースの二重確保は fail-closed で拒否（MAP は 1 ソース 1 行き先）。  */
	for (idx = 0U; idx < (uint32_t) ESP_SHIM_INTMTX_INTR_NSLOT; idx++) {
		if ((esp_shim_intmtx_intr_slot[idx].owner != OWNER_NONE)
			&& (esp_shim_intmtx_intr_slot[idx].source == source)) {
			syslog_2(LOG_ERROR,
					 "esp_shim_intr_intmtx: source=%d already routed to line %d"
					 " (double alloc refused)",
					 source, ESP_SHIM_INTMTX_LINE_OF_SLOT((int) idx));
			return(ESP_ERR_INVALID_STATE);
		}
	}

	/*
	 *  空き線は**大きい番号から**選ぶ。blob は小さい番号（実測 1）を自分で
	 *  選んで _set_intr してくるので、alloc 側が下から埋めると blob の
	 *  set_intr が「alloc 済み」で拒否される事故になる。
	 */
	for (idx = (uint32_t) ESP_SHIM_INTMTX_INTR_NSLOT; idx > 0U; idx--) {
		if (esp_shim_intmtx_intr_slot[idx - 1U].owner == OWNER_NONE) {
			found = idx - 1U;
			break;
		}
	}
	if (found >= (uint32_t) ESP_SHIM_INTMTX_INTR_NSLOT) {
		esp_shim_intmtx_intr_n_alloc_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_intmtx: no free line (source=%d, NSLOT=%d)",
				 source, ESP_SHIM_INTMTX_INTR_NSLOT);
		return(ESP_ERR_NO_MEM);
	}
	idx  = found;
	line = ESP_SHIM_INTMTX_LINE_OF_SLOT((int) idx);

	p = &esp_shim_intmtx_intr_slot[idx];
	p->owner       = OWNER_ALLOC;
	p->source      = source;
	p->status_reg  = (intrstatusreg != 0U)
					 ? (volatile uint32_t *)(uintptr_t) intrstatusreg : NULL;
	p->status_mask = intrstatusmask;
	p->handler     = handler;
	p->arg         = arg;
	p->enabled     = 1U;
	p->handle.idx  = idx;
	p->handle.valid = 1U;

	/*
	 *  配線して、優先度（内部表現 2 = cfg の -2 と同じ）と level 型を書き、
	 *  線を開ける。順序は blob の set_intr（esp_wifi_adapter.c）と同じ。
	 */
	_kernel_esp32c6_intmtx_route((uint_t) source, (INTNO) line);
	sil_wrw_mem(PLICMX_PRI_REG(line), 2U);
	sil_wrw_mem(PLICMX_TYPE_REG,
				sil_rew_mem(PLICMX_TYPE_REG) & ~(1UL << (uint32_t) line));
	ena_line(line);

	esp_shim_intmtx_intr_n_alloc++;
	syslog_3(LOG_NOTICE, "esp_shim_intr_intmtx: source=%d -> line=%d (slot=%d)",
			 source, line, (int) idx);

	if (ret_handle != NULL) {
		*ret_handle = &p->handle;
	}
	return(ESP_OK);
}

esp_err_t
esp_intr_alloc(int source, int flags,
			   void (*handler)(void *), void *arg,
			   struct intr_handle_data_t **ret_handle)
{
	return(esp_intr_alloc_intrstatus(source, flags, 0U, 0U, handler, arg, ret_handle));
}

static struct esp_shim_intmtx_intr_slot *
slot_of_handle(struct intr_handle_data_t *handle)
{
	if ((handle == NULL) || (handle->valid == 0U)
		|| (handle->idx >= (uint32_t) ESP_SHIM_INTMTX_INTR_NSLOT)) {
		return(NULL);
	}
	if (esp_shim_intmtx_intr_slot[handle->idx].owner != OWNER_ALLOC) {
		return(NULL);
	}
	return(&esp_shim_intmtx_intr_slot[handle->idx]);
}

esp_err_t
esp_intr_enable(struct intr_handle_data_t *handle)
{
	struct esp_shim_intmtx_intr_slot	*p = slot_of_handle(handle);

	if (p == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}
	p->enabled = 1U;
	ena_line(ESP_SHIM_INTMTX_LINE_OF_SLOT((int) handle->idx));
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(struct intr_handle_data_t *handle)
{
	struct esp_shim_intmtx_intr_slot	*p = slot_of_handle(handle);

	if (p == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}
	dis_line(ESP_SHIM_INTMTX_LINE_OF_SLOT((int) handle->idx));
	p->enabled = 0U;
	return(ESP_OK);
}

esp_err_t
esp_intr_free(struct intr_handle_data_t *handle)
{
	struct esp_shim_intmtx_intr_slot	*p = slot_of_handle(handle);
	int									line;

	if (p == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}
	line = ESP_SHIM_INTMTX_LINE_OF_SLOT((int) handle->idx);
	dis_line(line);
	sil_wrw_mem(INTMTX_MAP_REG(p->source), 0U);
	p->enabled     = 0U;
	p->handler     = NULL;
	p->arg         = NULL;
	p->status_reg  = NULL;
	p->status_mask = 0U;
	p->source      = -1;
	p->owner       = OWNER_NONE;
	p->handle.valid = 0U;
	esp_shim_intmtx_intr_n_free++;
	return(ESP_OK);
}

/*
 *  ============================================================================
 *  診断
 *  ============================================================================
 */
int
esp_shim_intmtx_intr_nslot(void)
{
	return(ESP_SHIM_INTMTX_INTR_NSLOT);
}

int
esp_shim_intmtx_intr_slot_info(int idx, int *owner, int *source,
							   int *line, uint32_t *n_isr)
{
	struct esp_shim_intmtx_intr_slot	*p;

	if ((idx < 0) || (idx >= ESP_SHIM_INTMTX_INTR_NSLOT)) {
		return(0);
	}
	p = &esp_shim_intmtx_intr_slot[idx];
	if (owner != NULL)  { *owner  = (int) p->owner; }
	if (source != NULL) { *source = p->source; }
	if (line != NULL)   { *line   = ESP_SHIM_INTMTX_LINE_OF_SLOT(idx); }
	if (n_isr != NULL)  { *n_isr  = esp_shim_intmtx_intr_n_isr[idx]; }
	return((p->owner != OWNER_NONE) ? 1 : 0);
}

uint32_t
esp_shim_intmtx_intr_read_map(int source)
{
	if (!source_in_range(source)) {
		return(0xFFFFFFFFU);
	}
	return(sil_rew_mem(INTMTX_MAP_REG(source)));
}

uint32_t
esp_shim_intmtx_intr_read_plic_enable(void)
{
	return(sil_rew_mem(PLICMX_ENABLE_REG));
}

uint32_t
esp_shim_intmtx_intr_read_plic_type(void)
{
	return(sil_rew_mem(PLICMX_TYPE_REG));
}

uint32_t
esp_shim_intmtx_intr_read_plic_pri(int line)
{
	if ((line < 0) || (line > 31)) {
		return(0xFFFFFFFFU);
	}
	return(sil_rew_mem(PLICMX_PRI_REG(line)));
}

#if defined(A1_C6_WIFI_DIAG)
/*
 *  段4 最終レビュー是正（2026-09-15）: 上の診断 API の呼び手。cmake の
 *  A1_C6_WIFI_DIAG（既定 OFF）のときだけリンクされ、esp/app/wifi_sta.c の
 *  [C6-SHIM] 報告（WIFI_STA_C6_DIAG_DUMP）から呼ばれる。使用中のスロット
 *  ごとに、帳簿（owner/source/n_isr）と、レジスタから読み返した INTMTX の
 *  MAP（source -> line）・PLIC_MX の ENABLE/TYPE ビットと PRI を並べる
 *  （書いた側の主張とレジスタの実値を同じ行で突き合わせるため）。
 *  syslog の引数は 5 個まで（TNUM_LOGPAR=6）なので 2 行に分ける。
 */
void
esp_shim_intmtx_diag_dump(void)
{
	int			idx;
	int			owner, source, line;
	uint32_t	n_isr;
	uint32_t	ena = esp_shim_intmtx_intr_read_plic_enable();
	uint32_t	typ = esp_shim_intmtx_intr_read_plic_type();

	syslog(LOG_NOTICE,
		   "[C6-INTMTX] nslot=%d n_route=%u n_route_fail=%u n_alloc=%u n_alloc_fail=%u",
		   esp_shim_intmtx_intr_nslot(), (uint_t) esp_shim_intmtx_intr_n_route,
		   (uint_t) esp_shim_intmtx_intr_n_route_fail, (uint_t) esp_shim_intmtx_intr_n_alloc,
		   (uint_t) esp_shim_intmtx_intr_n_alloc_fail);
	syslog(LOG_NOTICE,
		   "[C6-INTMTX] n_free=%u n_unowned_isr=%u plic_enable=0x%08x plic_type=0x%08x",
		   (uint_t) esp_shim_intmtx_intr_n_free, (uint_t) esp_shim_intmtx_intr_n_unowned_isr,
		   (uint_t) ena, (uint_t) typ);
	for (idx = 0; idx < esp_shim_intmtx_intr_nslot(); idx++) {
		if (esp_shim_intmtx_intr_slot_info(idx, &owner, &source, &line, &n_isr) == 0) {
			continue;
		}
		syslog(LOG_NOTICE,
			   "[C6-INTMTX] slot[%d] line=%d owner=%d source=%d n_isr=%u",
			   idx, line, owner, source, (uint_t) n_isr);
		syslog(LOG_NOTICE,
			   "[C6-INTMTX] slot[%d] map[source]=0x%08x ena_bit=%u type_bit=%u pri=0x%08x",
			   idx, (uint_t) esp_shim_intmtx_intr_read_map(source),
			   (uint_t) ((ena >> line) & 1U), (uint_t) ((typ >> line) & 1U),
			   (uint_t) esp_shim_intmtx_intr_read_plic_pri(line));
	}
}
#endif /* A1_C6_WIFI_DIAG */
