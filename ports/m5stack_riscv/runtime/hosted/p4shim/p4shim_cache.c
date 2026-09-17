/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: キャッシュ同期（`esp_cache_msync`）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  ここは stub にできない（実測に基づく・最重要）
 *  ============================================================================
 *  ESP32-P4 は **内部 SRAM（L2MEM）も L1 データキャッシュ経由**で見える
 *  （`soc_caps.h`: `SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE = 1`,
 *   `SOC_CACHE_WRITEBACK_SUPPORTED = 1`）。seam の EMAC DMA バッファは
 *  静的ヒープ（`target_kernel_impl.c` の `_sbrk`）＝L2MEM に載るので、
 *  **CPU が書いた記述子が DMA から見えない／DMA が書いた受信データが CPU から
 *  見えない**という形で必ず壊れる。「内部 SRAM だから同期不要」ではない。
 *
 *  呼び手（`esp_eth_mac_esp_dma.c:24-40`、P4 では有効）は
 *      #define DMA_CACHE_WB(a,s)         ... assert(esp_cache_msync(a,s,C2M) == ESP_OK)
 *      #define DMA_CACHE_INVALIDATE(a,s) ... assert(esp_cache_msync(a,s,M2C) == ESP_OK)
 *  で、**エラーを許容しない**（assert だけ）。⇒ 実際に呼ばれる範囲では
 *  必ず `ESP_OK` を返さなければならない。
 *
 *  ============================================================================
 *  整列の前提（なぜ広げても安全か）
 *  ============================================================================
 *  L1 D-cache のラインは **64 バイト**（`cache_ll_l1_dcache_get_line_size()`）。
 *  呼び手が渡す範囲は 2 種類しかない:
 *    (1) DMA 記述子 1 個ぶん … `EMAC_HAL_DMA_DESC_SIZE`。**P4 では 64**
 *        （`hal/emac_hal.h:28-33`。「Descriptor must be 64B aligned for
 *          ESP32P4 due to cache arrangement」というコメントつきで、
 *          構造体に `CacheAlign[]` の詰め物まで入っている）
 *    (2) データバッファ 1 個ぶん … `CONFIG_ETH_DMA_BUFFER_SIZE` = **512**
 *  どちらも 64 の倍数で、確保元は本 repo の `heap_caps_aligned_calloc`
 *  （`p4shim_misc.c`）が **`MALLOC_CAP_DMA` のとき 64 バイト境界へ切り上げて
 *  確保し、長さも 64 の倍数へ丸める**。⇒ **範囲をライン境界へ広げても、
 *  必ず同じ確保の内側に収まる**（隣の確保の dirty な行を巻き添えにしない）。
 *
 *  IDF 本体は整列していない要求を `ESP_ERR_INVALID_ARG` で撥ねるが、
 *  本シムは **撥ねずに広げて実施する**。理由: 撥ねると呼び手の assert が
 *  落ちて「Ethernet が動かない」になるだけで、診断の役に立たない。
 *  代わりに **広げた回数を数える**（`p4shim_n_cache_badarg`）——
 *  上の前提（64 の倍数で来る）が崩れたことが後から分かる。
 */

#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "p4shim.h"

#include "soc/soc.h"
#include "soc/soc_caps.h"
/*
 *  **IDF の公開ヘッダを include してプロトタイプの照合をコンパイラにさせる。**
 *  怠ると「リンクは通り、ビルドも通り、実機でだけ壊れる」型の失敗になる
 *  ——run1 で実際に踏んだ（`gpio_func_sel` を void で書き、呼び手が
 *  戻り値を見ていたため a0 のゴミが非 0 と読まれ、SMI 初期化が失敗した）。
 */
#include "esp_cache.h"

/*
 *  ROM のキャッシュ操作（`esp_rom/esp32p4/include/esp32p4/rom/cache.h`）。
 *  実体は ROM に在り、`esp32p4.rom.ld` が番地を PROVIDE する。
 *  `hal/cache_ll.h` は全部 static inline だが最終的にこの 2 本を呼ぶだけなので、
 *  `cache_hal.c`（非 inline・`esp_mm` の mutex/分割機構つき）は引かない。
 */
extern int Cache_WriteBack_Addr(uint32_t map, uint32_t addr, uint32_t size);
extern int Cache_Invalidate_Addr(uint32_t map, uint32_t addr, uint32_t size);

#define P4SHIM_CACHE_MAP_L1_DCACHE		(1U << 4)	/* CACHE_MAP_L1_DCACHE */
#define P4SHIM_CACHE_MAP_L2_CACHE		(1U << 5)	/* CACHE_MAP_L2_CACHE  */
#define P4SHIM_DCACHE_LINE				64U

/*
 *  **外部メモリ（PSRAM/flash）は L2 も通る**（段v-2・2026-08-16 に是正）。
 *  P4 は `SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE`（`soc_caps.h:175`）＝
 *  内蔵 RAM は L1、外部メモリは L2 が担当する。IDF の
 *  `cache_ll_writeback_addr()`（`hal/esp32p4/include/hal/cache_ll.h:678-693`）は
 *  level==2 のとき **L1 D$ と L2 の両方**を書き戻す。
 *
 *  本シムは当初 L1 の 1 ビットしか渡していなかった。Ethernet の DMA バッファは
 *  内蔵 RAM なのでそれで正しく動いていた（**現行バグではなかった**）が、
 *  PSRAM 上にフレームバッファを置いた瞬間に
 *  「書き戻しが L2 に届かない／invalidate が L2 の古い行を残す」＝
 *  **静かにデータが壊れる**。番地で level を選ぶ。
 *
 *  ライン長は L1/L2 とも 64B（実 IDF ビルドの
 *  `CONFIG_CACHE_L1_CACHE_LINE_SIZE` / `CONFIG_CACHE_L2_CACHE_LINE_SIZE` 実測）。
 */
static uint32_t
p4shim_cache_map_of(uint32_t vaddr)
{
	if (vaddr >= SOC_EXTRAM_LOW && vaddr < SOC_EXTRAM_HIGH) {
		return P4SHIM_CACHE_MAP_L1_DCACHE | P4SHIM_CACHE_MAP_L2_CACHE;
	}
	return P4SHIM_CACHE_MAP_L1_DCACHE;
}

/*  `esp_mm/include/esp_cache.h` の flags（値は現物どおり）  */
#define P4SHIM_MSYNC_FLAG_INVALIDATE	(1 << 0)
#define P4SHIM_MSYNC_FLAG_UNALIGNED		(1 << 1)
#define P4SHIM_MSYNC_FLAG_DIR_C2M		(1 << 2)
#define P4SHIM_MSYNC_FLAG_DIR_M2C		(1 << 3)
#define P4SHIM_MSYNC_FLAG_TYPE_INST		(1 << 5)

/*
 *  キャッシュが載っている番地か。P4 の内部 SRAM（L2MEM）は
 *  `SOC_IRAM_LOW..SOC_IRAM_HIGH`（`soc/soc.h`）で、そこが L1 経由になる。
 *  範囲外（レジスタ空間等）は IDF 本体と同じく `ESP_ERR_INVALID_ARG`。
 */
static bool
p4shim_cache_mapped(uint32_t vaddr, uint32_t end)
{
	return (vaddr >= SOC_IRAM_LOW && end <= SOC_IRAM_HIGH)
		|| (vaddr >= SOC_DRAM_LOW && end <= SOC_DRAM_HIGH)
		|| (vaddr >= SOC_EXTRAM_LOW && end <= SOC_EXTRAM_HIGH);
}

esp_err_t
esp_cache_msync(void *addr, size_t size, int flags)
{
	uint32_t	vaddr = (uint32_t) addr;
	uint32_t	end;
	uint32_t	a_lo, a_hi;
	uint32_t	map;

	if (addr == NULL || size == 0U) {
		p4shim_n_cache_badarg++;
		return ESP_ERR_INVALID_ARG;
	}
	end = vaddr + (uint32_t) size;
	if (end < vaddr) {					/* 桁溢れ */
		p4shim_n_cache_badarg++;
		return ESP_ERR_INVALID_ARG;
	}
	if ((flags & P4SHIM_MSYNC_FLAG_TYPE_INST) != 0) {
		/*  命令キャッシュは本シムの利用者に居ない。黙って成功にしない。 */
		p4shim_n_cache_badarg++;
		return ESP_ERR_INVALID_ARG;
	}
	if (!p4shim_cache_mapped(vaddr, end)) {
		p4shim_n_cache_badarg++;
		return ESP_ERR_INVALID_ARG;
	}

	/*  ライン境界へ広げる（広げたら数える。冒頭のコメント参照）  */
	a_lo = vaddr & ~(P4SHIM_DCACHE_LINE - 1U);
	a_hi = (end + P4SHIM_DCACHE_LINE - 1U) & ~(P4SHIM_DCACHE_LINE - 1U);
	if (a_lo != vaddr || a_hi != end) {
		p4shim_n_cache_badarg++;
	}

	/*
	 *  ISR 文脈からも呼ばれ得る（EMAC の受信処理は ISR で起こされた RX タスク
	 *  だが、`emac_esp_dma_*` は割込み禁止区間からも来る）。`loc_cpu` は
	 *  タスク文脈専用ではないので、CPU ロックで囲って ROM を叩く。
	 */
	map = p4shim_cache_map_of(vaddr);
	(void) loc_cpu();
	if ((flags & P4SHIM_MSYNC_FLAG_DIR_M2C) != 0) {
		(void) Cache_Invalidate_Addr(map, a_lo, a_hi - a_lo);
	}
	else {
		/*  既定は C2M（write-back）。`INVALIDATE` の併用も IDF と同じに扱う。 */
		(void) Cache_WriteBack_Addr(map, a_lo, a_hi - a_lo);
		if ((flags & P4SHIM_MSYNC_FLAG_INVALIDATE) != 0) {
			(void) Cache_Invalidate_Addr(map, a_lo, a_hi - a_lo);
		}
	}
	(void) unl_cpu();
	p4shim_n_cache_sync++;
	return ESP_OK;
}
