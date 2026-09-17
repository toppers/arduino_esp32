/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: `__real_*` の fail-closed スタブ ＋ 小物
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜスタブが要るのか（`--wrap` の仕組みと、seam での意味）
 *  ============================================================================
 *  `esp/eth/os/eth_os_fmp3.c` の `__wrap_X` は
 *      if (!g_fmp3_active) { return __real_X(...); }   // IDF 起動中の枝
 *      ... FMP3 ネイティブ実装 ...                      // FMP3 掌握後の枝
 *  という形をしている。方式(a)（IDF アプリを殻にする）では前者が実 FreeRTOS へ
 *  行くが、**seam には「IDF が起動中」という期間が無い**（実 ESP-IDF 2nd-stage
 *  bootloader から FMP3 のエントリへ直行する）。アプリは `esp_eth` を触る前に
 *  `fmp3_eth_os_mark_active()` を呼ぶので、**この枝は 1 回も通らない**。
 *
 *  しかしリンクは通らなければならない。GNU ld の `--wrap=X` は
 *  「`__real_X` への未定義参照を `X` へ解決する」ので、`X` の定義がどこかに要る。
 *  ⇒ **ここで `X` を定義する。** ただし
 *
 *      **黙って成功にしない。呼ばれたら数え、同期出力で報告し、失敗値を返す。**
 *
 *  これは「通らないはず」を主張するための計測器である（AC I-6）。
 *  カウンタが常に 0 を返す壊れた計測器でないことは、スタブを 1 回だけ意図的に
 *  呼ぶ版で 1 になることを実演して示す（positive control）。
 *
 *  ============================================================================
 *  `esp_intr_*` だけ形が違う理由（AC §1-3）
 *  ============================================================================
 *  seam の `--wrap` は **13 本**で、`esp_intr_alloc`/`esp_intr_enable`/
 *  `esp_intr_free` の 3 本は **含めない**。含めないので `libeth.a` の参照は
 *  素の `esp_intr_alloc` へ向き、**C-1 の `esp/shim/esp_shim_intr_clic.c` が受ける**
 *  ——これが本段の目的の一つ（CLIC シムを実利用者に繋ぐ）である。
 *
 *  その帰結として `eth_os_fmp3.c` の `__wrap_esp_intr_alloc` は
 *  **定義されるが呼ばれない**死んだ枝になり、そこにある `__real_esp_intr_alloc`
 *  への参照は「`--wrap` されていない素の名前」として残る。
 *  ⇒ こちらは `__real_` を**名前にそのまま含めて**定義する（下記）。
 *  `X` の側（＝`esp_intr_alloc`）を定義してはならない——C-1 シムと多重定義になる。
 */

#include <kernel.h>
#include <stdint.h>
#include <stddef.h>

#include "p4shim.h"

volatile uint32_t	p4shim_n_real_stub;

extern void target_fput_log(char c);

/*
 *  スタブが呼ばれたことの報告。1 行の間だけ CPU ロックする
 *  （`target_fput_log()` は上限つきスピンのポーリング出力なのでハングしない。
 *  C-1 §5-4 で「非同期出力に判定行を食われた」実例があるため、
 *  本層の出力はすべて同期にしてある）。
 */
static void
p4shim_stub_hit(const char *name)
{
	const char *p;

	p4shim_n_real_stub++;
	(void) loc_cpu();
	for (p = "P4SHIM STUB CALLED: "; *p != '\0'; p++) {
		target_fput_log(*p);
	}
	for (p = name; *p != '\0'; p++) {
		target_fput_log(*p);
	}
	target_fput_log('\n');
	(void) unl_cpu();
}

/*
 *  ----------------------------------------------------------------------------
 *  `--wrap` 済み 9 本の real 名（FreeRTOS）
 *  ----------------------------------------------------------------------------
 *  引数は受け取らない形で定義する。C は名前をマングルしないので、リンクは
 *  名前だけで成立する。**実際に呼ばれることを想定していない**ので、
 *  呼ばれたら報告して失敗値を返す（引数を読まないことが安全側に働く）。
 *
 *  返り値は「失敗」に倒す:
 *    - `xTaskCreatePinnedToCore` は pdFAIL(0)
 *    - `xQueueCreateMutex` は NULL
 *    - `xQueueSemaphoreTake` / `xQueueGenericSend` は pdFALSE(0)
 *    - `ulTaskGenericNotifyTake` は 0（通知なし）
 *  こうしておくと、万一通ってしまっても「動いているように見える」ことがない。
 */
int
xTaskCreatePinnedToCore(void)
{
	p4shim_stub_hit("xTaskCreatePinnedToCore");
	return 0;						/* pdFAIL */
}

void
vTaskDelay(void)
{
	p4shim_stub_hit("vTaskDelay");
}

void
vTaskDelete(void)
{
	p4shim_stub_hit("vTaskDelete");
}

void
vTaskGenericNotifyGiveFromISR(void)
{
	/*  ISR 文脈から呼ばれ得るので出力はしない（数えるだけ）。  */
	p4shim_n_real_stub++;
}

uint32_t
ulTaskGenericNotifyTake(void)
{
	p4shim_stub_hit("ulTaskGenericNotifyTake");
	return 0U;
}

void *
xQueueCreateMutex(void)
{
	p4shim_stub_hit("xQueueCreateMutex");
	return NULL;
}

int
xQueueSemaphoreTake(void)
{
	p4shim_stub_hit("xQueueSemaphoreTake");
	return 0;						/* pdFALSE */
}

int
xQueueGenericSend(void)
{
	p4shim_stub_hit("xQueueGenericSend");
	return 0;						/* pdFALSE */
}

void
vQueueDelete(void)
{
	p4shim_stub_hit("vQueueDelete");
}

/*
 *  ----------------------------------------------------------------------------
 *  `--wrap` していない 3 本（`__real_` を名前に含めて定義する）
 *  ----------------------------------------------------------------------------
 *  上の 9 本と違い、こちらは `--wrap` の対象外なので `__real_X` という綴りの
 *  シンボルがそのまま未定義参照として残る。**`X` の側を定義してはならない**
 *  （C-1 の `esp_shim_intr_clic.c` が `esp_intr_alloc` 等を定義しており、
 *  多重定義になる）。
 */
esp_err_t
__real_esp_intr_alloc(void)
{
	p4shim_stub_hit("__real_esp_intr_alloc");
	return ESP_FAIL;
}

esp_err_t
__real_esp_intr_enable(void)
{
	p4shim_stub_hit("__real_esp_intr_enable");
	return ESP_FAIL;
}

esp_err_t
__real_esp_intr_free(void)
{
	p4shim_stub_hit("__real_esp_intr_free");
	return ESP_FAIL;
}

/*
 *  ----------------------------------------------------------------------------
 *  `esp_intr_reserve`
 *  ----------------------------------------------------------------------------
 *  `eth_os_fmp3.c` の `__wrap_esp_intr_alloc`（seam では死んだ枝）が呼ぶ。
 *  IDF 本体では「動的割込みアロケータにこの線を使わせない」宣言だが、
 *  seam には動的アロケータが無い（C-1 は固定スロット式）ので**やることが無い**。
 *  ⇒ no-op。ただし**呼ばれたことは数える**（呼ばれたら、死んだはずの枝が
 *  生きていたということなので、I-6 と合わせて診断の手掛かりになる）。
 */
esp_err_t
esp_intr_reserve(int intno, int cpu)
{
	(void) intno;
	(void) cpu;
	p4shim_n_intr_reserve++;
	return ESP_OK;
}
