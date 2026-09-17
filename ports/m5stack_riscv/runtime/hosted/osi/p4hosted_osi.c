/*
 *  ESP32-P4 + M5Stack Stamp-AddOn C6 (ESP-Hosted) 移植
 *  os_adapter 本体: hosted_osi_funcs_t の FMP3 実装
 *
 *  esp-hosted host の OS 依存を集約した vtable `hosted_osi_funcs_t` を埋める。
 *  メモリ/スレッド/キュー/ミューテックス/セマフォ/タイマ/sleep/printf 等の
 *  「OS プリミティブ系」は本実装（多くは p4hosted_pools.c の静的プール経由）。
 *  GPIO/SDIO/bus/event/power-save/restart 等の「ドライバ・IDF依存系」は、
 *  実機・SDIOドライバ・esp_event移植が揃うフェーズ1/2まではスタブとする。
 *
 *  参照:
 *    docs/esp32p4_wifi_module_plan.md       … 設計計画（§4 vtable対応表, §8リスク）
 *    docs/esp32p4_wifi_module_bringup_checklist.md … ブリングアップ手順
 *    wifi_p4_module/os_adapter/mapping.md   … 本ファイルの実装/スタブ一覧
 */
#include "p4hosted_osi.h"
#include <sil.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

/*  P4 SDMMC ホストドライバ本体（wifi_p4_module/sdio_host/, 実機検証済み。
 *  docs/research/esp_hosted_init_event_recipe.md 参照）。 */
#include "p4sdio_host.h"
#include "p4sdio_pins.h"			/* D-6: P4SDIO_FREQ_KHZ（構成値は 1 箇所） */
/*  D-1 の帰結: 出典の `fmp3_sdio_slave_reset()` は本 repo では
 *  `p4sdio_board_slave_reset()`（ボード層）にあたるので、その宣言を引く。 */
#include "p4sdio_board.h"

/*
 *  【追加 D-7（本 repo・段7b）】vtable の契約を**コンパイル時に**照合する。
 *
 *  この生成ヘッダは上流ヘッダ（`upstream/esp_hosted_os_abstraction.h`）から
 *  `cmake/a1_p4_hosted_vtable_check.py --emit` が機械生成したもので、
 *  全スロットの `offsetof` と**型**の `_Static_assert` が入っている。
 *  それを「適合層ヘッダ `p4hosted_osi.h` を見る TU」＝**このファイル**で
 *  コンパイルすることで、2 つの独立宣言がずれたらビルドが落ちる。
 *
 *  2026-07-09 に実機で起きた事故（`H_USE_MEMPOOL` の定義有無が片側だけ違い、
 *  `_h_hosted_init_hook()` の呼出しが `_h_sdio_write_reg` 相当のスロットへ
 *  落ちてクラッシュ）を、コンパイル時に落とすためのものである。
 */
#include "generated/hosted_vtable_contract.h"

struct hosted_config_t g_h = HOSTED_CONFIG_INIT_DEFAULT();

/*
 *  =====================================================================
 *  メモリ
 *  =====================================================================
 */
static void *
hosted_osi_memcpy(void *dest, const void *src, uint32_t size)
{
	return memcpy(dest, src, size);
}

static void *
hosted_osi_memset(void *buf, int val, size_t len)
{
	return memset(buf, val, len);
}

static void *
hosted_osi_malloc(size_t size)
{
	return malloc(size);
}

static void *
hosted_osi_calloc(size_t blk_no, size_t size)
{
	return calloc(blk_no, size);
}

static void
hosted_osi_free(void *ptr)
{
	free(ptr);
}

static void *
hosted_osi_realloc(void *mem, size_t newsize)
{
	if (newsize == 0) {
		free(mem);
		return NULL;
	}
	return realloc(mem, newsize);
}

/*
 *  malloc_align/free_align: 整列確保。
 *
 *  【重要・2026-07-11 修正（BLE HCI TX クラッシュの根治）】
 *  esp-hosted の契約は「_h_malloc_align の返り値は _h_free（＝素の free）でも
 *  解放できる」こと。実際 vhci_drv.c の ble_transport_to_ll_cmd_impl は
 *  _h_malloc_align で確保したバッファを esp_hosted_tx(..., H_DEFLT_FREE_FUNC)
 *  へ渡し、送信後に H_DEFLT_FREE_FUNC(=g_h.funcs->_h_free=素の free) で解放する。
 *  旧実装は「malloc + 整列オフセット + 直前に元ポインタ埋め込み」方式で、
 *  返り値が malloc ベースと異なるため素の free では tlsf ヒープを破壊し、
 *  最初の HCI コマンド(Reset)送信直後に PRC1 Store/AMO fault(tlsf_free 内)で
 *  クラッシュしていた。
 *  → free 互換の整列確保 aligned_alloc(C11。IDF/newlib のヒープが提供、free で
 *  解放可能)を使う。size は align の倍数へ切り上げる(aligned_alloc の要件)。
 *  これで _h_free / _h_free_align のどちらでも正しく解放できる。
 *  64B 整列は SDIO DMA/キャッシュライン整合のため必要なので維持する。
 */
static void *
hosted_osi_malloc_align(size_t size, size_t align)
{
	size_t asize;

	if (align == 0) {
		align = sizeof(void *);
	}
	/*
	 *  【レビュー指摘④】ビットマスク丸め (size+align-1)&~(align-1) と
	 *  C11 aligned_alloc() はいずれも align が2冪であることが前提
	 *  （非2冪では未定義動作）。呼出し元(esp-hosted)は常に
	 *  HOSTED_MEM_ALIGNMENT_64(=64)等の2冪しか渡さない契約だが、無検証
	 *  のままだと契約違反時にヒープ破壊等の未定義動作へ落ちる。
	 *  malloc のホットパスなので検証は「1回の分岐」に留める
	 *  （align & (align-1) は2冪判定の定番の安価な式）。
	 *  違反時は安全側の NULL 返却とする（呼出し元は全て _h_malloc_align の
	 *  戻り値を NULL チェックする契約。transport_util.h の MEMPOOL_ALLOC
	 *  等、実際に `if (ptr && ...)` の形でNULLを扱っている）。
	 */
	if ((align & (align - 1U)) != 0U) {
		syslog(LOG_ERROR,
				"hosted_osi_malloc_align: align=%u is not power-of-2, refused",
				(unsigned int) align);
		return NULL;
	}
	/*  aligned_alloc は size が align の倍数であることを要求する（C11）。 */
	asize = (size + align - 1U) & ~(align - 1U);

	return aligned_alloc(align, asize);
}

static void
hosted_osi_free_align(void *ptr)
{
	/*  aligned_alloc の返り値は素の free で解放可能（上記コメント参照）。 */
	free(ptr);
}

/*
 *  =====================================================================
 *  スレッド
 *  =====================================================================
 */
static void
hosted_osi_thread_yield(void)
{
	/* dly_tsk(0): 同一/より高い優先度の他タスクへ実行機会を譲る。
	 * rot_rdq(自優先度) でも良いが、自タスクの現在優先度取得(get_pri)が
	 * 追加で必要になるため、より簡潔な dly_tsk(0) を採用する。 */
	(void) dly_tsk(0);
}

/*
 *  =====================================================================
 *  sleep / 遅延
 *  =====================================================================
 */

/*
 *  µs 単位のビジーウェイト。
 *
 *  【2026-07-10 修正】旧実装は mcycle（CSR 0xB00/0xB80）直読みだったが、
 *  mcycle はハート（コア）ごとに独立したカウンタであり、プールタスクは
 *  マイグレーション可能クラスに置かれ得るため、busy-wait 中（最大
 *  HOSTED_USLEEP_DLY_THRESHOLD_US ≒ 2ms、tick を跨ぐ）にプリエンプト→
 *  他コアへ移動すると経過計算が破綻し「待たずに即抜ける/過大待ち」に
 *  なり得た（コードレビュー指摘）。コア非依存の sil_dly_nse（SIL の
 *  較正済み遅延、どの PE でも同じ実時間）へ置き換える。
 */
static void
hosted_osi_busy_wait_us(unsigned int useconds)
{
	while (useconds >= 1000U) {
		sil_dly_nse(1000U * 1000U);
		useconds -= 1000U;
	}
	if (useconds > 0U) {
		sil_dly_nse(useconds * 1000U);
	}
}

static unsigned int
hosted_osi_msleep(unsigned int mseconds)
{
	/* RELTIM/TMO はマイクロ秒単位（t_stddef.h の TMAX_RELTIM=4000000000U ≒
	 * 66分40秒であることから確認済み）。ms→us変換して dly_tsk する。
	 * 変換オーバーフロー（mseconds > 4,294,967 で uint32 ラップ）と
	 * TMAX_RELTIM 超過（dly_tsk が E_PAR で即時失敗＝「全く待たない」）を
	 * 防ぐため、TMAX_RELTIM/1000 ms ごとに分割して待つ。 */
	while (mseconds > 0U) {
		unsigned int chunk = mseconds;

		if (chunk > TMAX_RELTIM / 1000U) {
			chunk = TMAX_RELTIM / 1000U;
		}
		(void) dly_tsk((RELTIM) chunk * 1000U);
		mseconds -= chunk;
	}
	return 0;
}

static unsigned int
hosted_osi_usleep(unsigned int useconds)
{
	/*
	 *  実際のタイマ分解能(tick周期。本ターゲットは概ね1ms)より短い要求は
	 *  dly_tsk で待つと大幅に伸びてしまう(次tickまで丸め上げられる)ため、
	 *  閾値未満は mcycle 直読みのビジーウェイトにする。
	 *  TODO(phase1): 実機で実際の tick 周期を確認し、
	 *  HOSTED_USLEEP_DLY_THRESHOLD_US を調整すること。
	 */
	if (useconds < HOSTED_USLEEP_DLY_THRESHOLD_US) {
		hosted_osi_busy_wait_us(useconds);
	} else {
		(void) dly_tsk((RELTIM) useconds);
	}
	return 0;
}

static unsigned int
hosted_osi_sleep(unsigned int seconds)
{
	/* seconds * 1000 の uint32 ラップ（seconds > 4,294,967）を防ぐため分割。
	 * msleep 側で TMAX_RELTIM 分割も行われる。 */
	while (seconds > 0U) {
		unsigned int chunk = (seconds > 3600U) ? 3600U : seconds;

		(void) hosted_osi_msleep(chunk * 1000U);
		seconds -= chunk;
	}
	return 0;
}

static unsigned int
hosted_osi_blocking_delay(unsigned int number)
{
	/*
	 *  upstream(FreeRTOS参照実装)の hosted_for_loop_delay は
	 *  `for (idx = 0; idx < 100*number; idx++);` という単位不定の
	 *  実装依存ビジーループであり、真の時間単位を持たない。
	 *  本移植では設計方針(§8)どおり mcycle 直読みのビジーウェイトに
	 *  置き換え、"number" をマイクロ秒とみなして待つ（暫定解釈）。
	 *  TODO(phase1): esp-hosted 側の実際の呼び出し意図（本当にus換算で
	 *  良いか）を実機で確認すること。
	 */
	hosted_osi_busy_wait_us(number);
	return 0;
}

static uint64_t
hosted_osi_get_time_ms(void)
{
	/*
	 *  【2026-07-10 修正】旧実装は mcycle 直読みだったが、mcycle は per-hart
	 *  カウンタで両コアの値は起動タイミング分ずれるため、呼出しごとに実行
	 *  コアが変わると時刻が前後にジャンプし、esp-hosted 側の経過時間計算
	 *  （タイムアウト判定等）を狂わせ得た。コア間で共有される FMP3 システム
	 *  時刻（get_tim の SYSTIM、マイクロ秒単位）へ置き換える。
	 */
	SYSTIM systim = 0;

	(void) get_tim(&systim);
	return (uint64_t) systim / 1000U;
}

/*
 *  =====================================================================
 *  GPIO（スタブ: フェーズ1で P4 GPIO ドライバ実装後に接続）
 *  =====================================================================
 */
static int
hosted_osi_config_gpio(void *gpio_port, uint32_t gpio_num, uint32_t mode)
{
	(void) gpio_port; (void) gpio_num; (void) mode;
	/* TODO(phase1): P4 GPIO ドライバ実装後に接続する。 */
	return RET_FAIL;
}

static int
hosted_osi_config_gpio_as_interrupt(void *gpio_port, uint32_t gpio_num, uint32_t intr_type,
		void (*gpio_isr_handler)(void *arg), void *arg)
{
	(void) gpio_port; (void) gpio_num; (void) intr_type; (void) gpio_isr_handler; (void) arg;
	/* TODO(phase1): CLIC/割込みマトリクス経由での GPIO 割込み登録
	 * （データレディ割込み等）を実装する。ISR からは isig_sem 等の
	 * i系サービスコールで待ちタスクを起こす設計（§8参照）。 */
	return RET_FAIL;
}

static int
hosted_osi_teardown_gpio_interrupt(void *gpio_port, uint32_t gpio_num)
{
	(void) gpio_port; (void) gpio_num;
	/* TODO(phase1) */
	return RET_FAIL;
}

static int
hosted_osi_read_gpio(void *gpio_port, uint32_t gpio_num)
{
	(void) gpio_port; (void) gpio_num;
	/* TODO(phase1) */
	return RET_FAIL;
}

static int
hosted_osi_write_gpio(void *gpio_port, uint32_t gpio_num, uint32_t value)
{
	(void) gpio_port; (void) gpio_num; (void) value;
	/* TODO(phase1) */
	return RET_FAIL;
}

static int
hosted_osi_pull_gpio(void *gpio_port, uint32_t gpio_num, uint32_t pull_value, uint32_t enable)
{
	(void) gpio_port; (void) gpio_num; (void) pull_value; (void) enable;
	/* TODO(phase1) */
	return RET_FAIL;
}

static int
hosted_osi_hold_gpio(void *gpio_port, uint32_t gpio_num, uint32_t hold_value)
{
	(void) gpio_port; (void) gpio_num; (void) hold_value;
	/* TODO(phase1) */
	return RET_FAIL;
}

static int
hosted_osi_get_host_wakeup_or_reboot_reason(void)
{
	/* TODO(phase1): P4 のリセット要因レジスタ取得実装後に接続する。
	 * 現状は「通常電源投入」を安全側の既定値として返す。 */
	return HOSTED_WAKEUP_NORMAL_REBOOT;
}

/*
 *  =====================================================================
 *  バス初期化（P4 SDMMC ホストドライバ = wifi_p4_module/sdio_host/ を接続）
 *  =====================================================================
 *  p4sdio_host.c はシングルトン実装（静的グローバル状態、インスタンスを
 *  複数持たない）のため，ctx には実データではなくダミーの非NULLポインタを
 *  返す（esp-hosted 側は ctx を単に他の _h_sdio_* 呼出しへ素通しするだけで
 *  中身は見ない．port_esp_hosted_host_sdio.c の sdmmc_context_t 相当）。
 */
static int s_sdio_dummy_ctx;   /* アドレスのみ使う非NULLダミー */

static void *
hosted_osi_bus_init(void)
{
	ER ercd = p4sdio_host_init();

	if (ercd != E_OK) {
		return NULL;
	}
	return &s_sdio_dummy_ctx;
}

static int
hosted_osi_bus_deinit(void *ctx)
{
	(void) ctx;
	/* p4sdio_host.c はホストコントローラの deinit を提供しない
	 * （esp-hosted 側も通常プロセス寿命中は呼ばない想定）。 */
	return RET_OK;
}

/*
 *  =====================================================================
 *  イベント / ログ / フック
 *  =====================================================================
 */

/*
 *  esp_event 最小shim（`wifi_p4_module/esp_event_port/`）との配線。
 *
 *  os_adapter と esp_event シムは互いに不要な依存をしない独立モジュール
 *  方針（両者の mapping.md 参照）のため、`esp_event_shim.h` を直接
 *  #include せず、ここに必要最小限の extern 宣言だけを置く「薄い糊」方式に
 *  する。`esp_err_t`/`TickType_t` は `esp_event_port/include/
 *  _esp_event_min_shim.h` と同じガード名（`ESP_ERR_T_DECLARED`/
 *  `TICK_TYPE_T_DECLARED`）を使うことで、将来どちらのヘッダを先に
 *  includeしても再定義エラーにならないようにしてある（実体はどちらも
 *  同じ int32_t/uint32_t なので型としても衝突しない）。
 *
 *  WIFI_EVENT（esp_event_shim.c で定義される暫定base文字列）も同様に
 *  extern 宣言のみで参照する。
 */
/*
 *  【改変 D-2（本 repo・段7b）】esp_event への配線を外した。
 *
 *  出典は `wifi_p4_module/esp_event_port/`（esp_event 最小 shim）へ橋渡ししていたが、
 *  本 repo は段7b でそのモジュールを**取り込まない**（7c 以降）。
 *
 *  **黙って RET_OK を返さない**——「投げたことにする」と、呼出し側は
 *  「イベントは配送された」と信じて先へ進み、後段で理由の分からない不整合になる。
 *  `RET_FAIL` を返し、**呼ばれた回数を数えて公開する**（実際に呼ばれるなら
 *  それは 7c の入口条件が 1 つ増えたということなので、数字で見えるようにする）。
 */
volatile uint32_t	p4hosted_n_event_post_unwired;

static int
hosted_osi_event_wifi_post(int32_t event_id, void *event_data, size_t event_data_size,
		uint32_t ticks_to_wait)
{
	(void) event_id; (void) event_data; (void) event_data_size; (void) ticks_to_wait;
	p4hosted_n_event_post_unwired++;
	return RET_FAIL;
}

static int
hosted_osi_event_post(esp_event_base_t event_base, int32_t event_id, void *event_data,
		size_t event_data_size, uint32_t ticks_to_wait)
{
	(void) event_base; (void) event_id; (void) event_data;
	(void) event_data_size; (void) ticks_to_wait;
	p4hosted_n_event_post_unwired++;
	return RET_FAIL;
}

/*
 *  esp-hosted の `_h_printf(level, tag, format, ...)` を FMP3 syslog へ橋渡しする。
 *
 *  【2026-07-10 修正・重要】TOPPERS の syslog 機構は書式文字列・ポインタ引数を
 *  「ポインタのまま」ログレコードに保存し、実際の整形は logtask が後で非同期に
 *  行う（だからこそ TOPPERS では書式は静的文字列リテラルが前提）。旧実装は
 *  スタック上の fmtbuf を tt_syslog へ渡して即 return していたため、logtask が
 *  整形する時点でスタックは解放・上書き済みで、出力の文字化け・引数解釈の
 *  ずれを招いていた（esp-hosted 側が渡す %s の動的文字列引数も同罪）。
 *
 *  対策: 静的リングバッファへ vsnprintf で【完全に整形してから】格納し、
 *  syslog へは静的リテラル "%s" とリングスロットへのポインタだけを渡す。
 *  スロットは以後 HOSTED_PRINTF_RING_SLOTS 回の _h_printf 呼出しまで安定
 *  なので、logtask の遅延整形にも耐える（それ以上遅延した場合は古いログが
 *  新しい内容で上書きされるだけで、メモリ安全性は保たれる）。
 */
#define HOSTED_PRINTF_RING_SLOTS	8
#define HOSTED_PRINTF_RING_WIDTH	128

static char hosted_printf_ring[HOSTED_PRINTF_RING_SLOTS][HOSTED_PRINTF_RING_WIDTH];
static uint_t hosted_printf_ring_next;

static void
hosted_osi_printf(int level, const char *tag, const char *format, ...)
{
	va_list ap;
	uint_t prio;
	uint_t slot;
	char *buf;
	int off;
	SIL_PRE_LOC;

	/*
	 *  esp_log_level_t 相当のレベル値を FMP3 の LOG_* へ写像する
	 *  （ESP_LOG_NONE=0,ERROR=1,WARN=2,INFO=3,DEBUG=4,VERBOSE=5 を想定。
	 *  upstream ヘッダ未取得のため値は ESP-IDF の一般的な既定に基づく仮定。
	 *  TODO: 統合時に esp_log_level_t の実値と突き合わせて確認すること）。
	 */
	switch (level) {
	case 1:  prio = LOG_ERROR;   break;
	case 2:  prio = LOG_WARNING; break;
	case 3:  prio = LOG_INFO;    break;
	case 4:  /* fall through */
	case 5:  prio = LOG_DEBUG;   break;
	default: prio = LOG_NOTICE;  break;
	}

	/*  リングスロットの払い出し（割込み禁止で index を進めるだけの短い区間） */
	SIL_LOC_INT();
	slot = hosted_printf_ring_next;
	hosted_printf_ring_next = (slot + 1U) % HOSTED_PRINTF_RING_SLOTS;
	SIL_UNL_INT();
	buf = hosted_printf_ring[slot];

	off = snprintf(buf, HOSTED_PRINTF_RING_WIDTH, "[%s] ",
			(tag != NULL) ? tag : "?");
	if (off < 0) {
		off = 0;
	}
	if ((size_t) off < HOSTED_PRINTF_RING_WIDTH) {
		va_start(ap, format);
		(void) vsnprintf(buf + off, HOSTED_PRINTF_RING_WIDTH - (size_t) off,
				(format != NULL) ? format : "", ap);
		va_end(ap);
	}

	/*
	 *  【改変 D-3（本 repo・段7b）】`syslog` ではなく**同期出力**へ出す。
	 *
	 *  本 repo の規律: **実機診断の根拠に `syslog`（logtask 経由の非同期出力）を
	 *  使わない**——ハング/リセットの直前の行がそのまま失われるため、
	 *  「出なかった」のが「起きなかった」なのか「出力が間に合わなかった」なのかを
	 *  区別できなくなる。`p4sdio_puts()` は `target_fput_log()` 直呼びである。
	 *
	 *  出典の「静的リングへ完全整形してからポインタだけ渡す」工夫は、非同期
	 *  出力に耐えるためのものだった。同期出力なら整形直後に出し切るので
	 *  リング自体は不要だが、**バッファの寿命に関する出典の判断を消さない**ため
	 *  リングは残す（可変長引数の整形先として使う）。
	 */
	(void) prio;
	p4sdio_puts(buf);
	p4sdio_puts("\n");
}

static void
hosted_osi_hosted_init_hook(void)
{
	/* upstream 同様、ポート固有の初期化フックだが本移植では現状不要
	 * （フェーズ1でSDIO/GPIO初期化を追加する際にここへ接続する想定）。 */
}

/*
 *  =====================================================================
 *  SDIO トランスポート（wifi_p4_module/sdio_host/p4sdio_host.c を接続）
 *  =====================================================================
 *  esp-hosted 側の契約（port_esp_hosted_host_sdio.c 実装を精読して確認済み。
 *  docs/research/esp_hosted_host_setup_findings.md 参照）:
 *    - func は常に SDIO_FUNC_1 固定（呼出し引数に func は無い）
 *    - reg には ESP_ADDRESS_MASK(0x3FF) 適用前の生アドレスが渡ることがある
 *      ため、こちらでもマスクしてから fmp3_sdmmc API へ渡す
 *    - read/write_reg も read/write_block も size<=1 は CMD52、size>1 は
 *      CMD53（インクリメントモード）で同じ実装（呼分けは size のみ）
 */
#define SDIO_OSI_ADDR_MASK	0x3FFU
#define SDIO_OSI_FUNC		1U

/*  【改変 D-8】待ちの計数（下の `_h_sdio_wait_slave_intr` のコメント参照）  */
volatile uint32_t	p4hosted_n_wait_call;
volatile uint32_t	p4hosted_n_wait_ok;
volatile uint32_t	p4hosted_n_wait_tmout;
volatile uint32_t	p4hosted_n_wait_err;
volatile uint32_t	p4hosted_n_wait_capped;

static int
hosted_osi_sdio_card_init(void *ctx, bool show_config)
{
	ER ercd;

	(void) ctx; (void) show_config;

	/*  fmp_sdio_probe での実機検証と同じ手順: ハードリセット→カード初期化．
	 *  esp-hosted 標準は起動時クロック 400kHz→運用クロックへ引上げだが，
	 *  実機検証済みの 20MHz を初期値として使う（動作確認後 40MHz へ引上げ
	 *  可、esp32p4_sdmmc_host_findings.md 参照）。
	 *
	 *  【改変 D-6（本 repo・段7b）】出典は `20000U` を直書きしていたが、
	 *  本 repo は 7a で周波数を構成値化した（`p4sdio_pins.h` の
	 *  `P4SDIO_FREQ_KHZ`。**定義は 1 箇所**が 7a の AC-C3）。ここで数字を
	 *  写すと、7a が潰した「値が 2 箇所にあって食い違う」型の欠陥が戻る。 */
	p4sdio_board_slave_reset();
	ercd = p4sdio_card_init(P4SDIO_FREQ_KHZ);
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

static int
hosted_osi_sdio_card_deinit(void *ctx)
{
	(void) ctx;
	/* p4sdio_host.c はカード deinit を提供しない（esp-hosted 側も通常
	 * プロセス寿命中は呼ばない想定。他ドライバ実装も大半が no-op）。 */
	return RET_OK;
}

/*
 *  CMD53 転送のチャンク分割:
 *  fmp3_sdio_read/write_bytes（バイトモード）は 512B が上限（p4sdio_host.c
 *  の rw_bytes 参照）のため，512B 超は 512 の倍数分をブロックモード
 *  （fmp3_sdio_read/write_blocks）＋端数をバイトモードに分けて転送する
 *  （esp-hosted 参照実装 sdio_read_fromio/sdio_write_toio と同じ方針）。
 *  reg はインクリメントアドレスとして進める（呼出し元がスレーブ側の
 *  アドレス計算・ロールオーバーを担う。ここは純粋な転送分割のみ）。
 */
static ER
sdio_osi_transfer(bool wr, uint32_t reg, uint8_t *data, uint32_t size)
{
	uint32_t done = 0;
	ER ercd = E_OK;

	while (done < size) {
		uint32_t remain = size - done;

		if (remain >= P4SDIO_BLOCK_SIZE) {
			uint32_t nblk = remain / P4SDIO_BLOCK_SIZE;

			if (nblk > 8U) {
				nblk = 8U;   /* p4sdio_host.c の NDESC(8) に合わせて分割 */
			}
			ercd = wr ?
				p4sdio_write_blocks(SDIO_OSI_FUNC, reg + done, data + done, nblk, false) :
				p4sdio_read_blocks(SDIO_OSI_FUNC, reg + done, data + done, nblk, false);
			done += nblk * P4SDIO_BLOCK_SIZE;
		}
		else {
			ercd = wr ?
				p4sdio_write_bytes(SDIO_OSI_FUNC, reg + done, data + done, remain, false) :
				p4sdio_read_bytes(SDIO_OSI_FUNC, reg + done, data + done, remain, false);
			done += remain;
		}
		if (ercd != E_OK) {
			break;
		}
	}
	return ercd;
}

static int
hosted_osi_sdio_read_reg(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required)
{
	ER ercd;

	(void) ctx; (void) lock_required;   /* p4sdio_host.c 内部で P4SDIO_MTX 排他済み */
	reg &= SDIO_OSI_ADDR_MASK;

	if (size <= 1U) {
		ercd = p4sdio_read_reg(SDIO_OSI_FUNC, reg, data);
	}
	else {
		ercd = sdio_osi_transfer(false, reg, data, size);
	}
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

static int
hosted_osi_sdio_write_reg(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required)
{
	ER ercd;

	(void) ctx; (void) lock_required;
	reg &= SDIO_OSI_ADDR_MASK;

	if (size <= 1U) {
		ercd = p4sdio_write_reg(SDIO_OSI_FUNC, reg, *data);
	}
	else {
		ercd = sdio_osi_transfer(true, reg, data, size);
	}
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

/*
 *  read_block/write_block は read_reg/write_reg と異なり ESP_ADDRESS_MASK
 *  を適用しない（esp-hosted 参照実装 hosted_sdio_read_block/write_block を
 *  確認済み。docs/research/esp_hosted_host_setup_findings.md 参照）。
 *  read/write_reg は小さな FN1 レジスタ番地(<0x400)専用でマスクにより
 *  安全化するが，read/write_block は 0x1F800 減算スキームの大きな実データ
 *  アドレスを直接使うため，マスクすると番地が破壊される（実際に発生した
 *  バグ: addr=0x1F7B8 が 0x3FF マスクで 0x3B8 に化けて空データを読んだ）。
 */
static int
hosted_osi_sdio_read_block(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required)
{
	ER ercd;

	(void) ctx; (void) lock_required;
	if (size <= 1U) {
		ercd = p4sdio_read_reg(SDIO_OSI_FUNC, reg & SDIO_OSI_ADDR_MASK, data);
	}
	else {
		ercd = sdio_osi_transfer(false, reg, data, size);
	}
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

static int
hosted_osi_sdio_write_block(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required)
{
	ER ercd;

	(void) ctx; (void) lock_required;
	if (size <= 1U) {
		ercd = p4sdio_write_reg(SDIO_OSI_FUNC, reg & SDIO_OSI_ADDR_MASK, *data);
	}
	else {
		ercd = sdio_osi_transfer(true, reg, data, size);
	}
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

static int
hosted_osi_sdio_wait_slave_intr(void *ctx, uint32_t ticks_to_wait)
{
	ER ercd;

	(void) ctx;
	/*
	 *  実 D1 線ハード割込み（p4sdio_host.c の p4sdio_wait_int）に接続．
	 *  RINTSTS.IO_SLOT1 のスティッキー残留による偽陽性は修正済み
	 *  （docs/research/esp_hosted_init_event_recipe.md 参照）。
	 *  ticks_to_wait は esp-hosted 側 RTOS tick 相当。IDF既定
	 *  (portTICK_PERIOD_MS=1) に倣い 1 tick=1ms とみなす。
	 *  p4sdio_wait_int は【ミリ秒】契約（2026-07-10 に ms 契約へ統一。
	 *  旧実装は ms 値を TMO（マイクロ秒）へ無変換で渡しており、意図の
	 *  1/1000 のタイムアウトでビジーポーリング化していた）。
	 *  portMAX_DELAY(0xFFFFFFFF) は P4SDIO_WAIT_FOREVER と同値で永久待ち。
	 */
	/*
	 *  【2026-07-10 修正・ポーリングフォールバック（RX ロストウェイクアップの安全網）】
	 *  DW-SDMMC は SD 4-bit モードのアイドル中に DAT1（スレーブのデータレディ）を
	 *  必ずしも再サンプルしないため、割込みを取り逃すと次の SDIO トランザクションまで
	 *  RX が停止する（真因と実測は p4sdio_host.c p4sdio_wait_int の修正コメント参照。
	 *  真因側は同関数の raw 先読みで対処済み）。上流 sdio_drv.c の RX ループは本関数が
	 *  RET_OK を返すたびに必ずスレーブのレジスタ（INT_RAW/PACKET_LEN）を読んで滞留
	 *  パケットを全処理するので、永久待ちを 100ms 上限に切り、タイムアウトも RET_OK と
	 *  して返すことで「最悪 100ms 遅延のポーリング」に落ちる保険を掛ける（空振り時の
	 *  コストはレジスタ読み 1 回/100ms のみ）。
	 */
	/*
	 *  【改変 D-8（本 repo・段7b）】**計数を足した**（挙動は変えていない）。
	 *
	 *  BL-H-7 の教訓: 「長く待つつもりが、変換の折り返しやクランプで全く待たない
	 *  （あるいは即時エラーで返る）」が**黙って**起きると、症状は
	 *  「別のところが遅い/固まる」として現れ、原因に辿り着けない。
	 *  ⇒ (a) 上限に当てた回数、(b) 待ちが E_OK でも E_TMOUT でもない値を
	 *  返した回数、(c) 先読み fast path で即時成立した回数——を数えて公開する。
	 *  **0 でも印字する**（「数えていない」と「0 だった」は違う）。
	 *
	 *  なお `ticks_to_wait` は uint32 のまま扱い、**掛け算をここでしない**
	 *  （µs 換算は `p4sdio_wait_int()` 側。ここで ms*1000 を 32bit で書くと
	 *  BL-H-7 と同型の折り返しを新しく作ることになる）。
	 */
	{
		uint32_t ms = ticks_to_wait;

		if (ms == 0xFFFFFFFFU || ms > P4HOSTED_SDIO_WAIT_CAP_MS) {
			ms = P4HOSTED_SDIO_WAIT_CAP_MS;
			p4hosted_n_wait_capped++;
		}
		p4hosted_n_wait_call++;
		ercd = p4sdio_wait_int(ms);
	}
	if (ercd == E_OK) {
		p4hosted_n_wait_ok++;
		return RET_OK;
	}
	if (ercd == E_TMOUT) {
		p4hosted_n_wait_tmout++;
		return RET_OK;			/* タイムアウト＝ポーリング契機（上流がレジスタ再読みで吸収） */
	}
	p4hosted_n_wait_err++;
	return RET_FAIL;
}

/*
 *  =====================================================================
 *  再起動 / パワーセーブ（スタブ）
 *  =====================================================================
 */
static int
hosted_osi_restart_host(void)
{
	/* TODO(phase1以降): ESP-IDF側の esp_restart 相当（またはウォッチドッグ
	 * リセット等）と接続する。現状は安全側として何もせず失敗を返す。 */
	return RET_FAIL;
}

static int
hosted_osi_config_host_power_save(uint32_t power_save_type, void *gpio_port, uint32_t gpio_num,
		int level)
{
	(void) power_save_type; (void) gpio_port; (void) gpio_num; (void) level;
	/* パワーセーブは初期スコープ外（§3方針決定: Wi-Fi STA+DHCP+TCP/UDP echo が
	 * first light 目標）。TODO: 必要になった時点で実装。 */
	return RET_FAIL;
}

static int
hosted_osi_start_host_power_save(uint32_t power_save_type)
{
	(void) power_save_type;
	/* TODO: 上記と同様、初期スコープ外。 */
	return RET_FAIL;
}

/*
 *  =====================================================================
 *  vtable 本体
 *  =====================================================================
 *  シグネチャが一致する項目は p4hosted_pools.c の関数をそのまま代入している
 *  （ラッパを介さないことで、hosted_osi_funcs_t 側とプール実装側の間に余計な
 *  間接層を作らない）。
 */
hosted_osi_funcs_t g_hosted_osi_funcs = {
	._h_memcpy                      = hosted_osi_memcpy,
	._h_memset                      = hosted_osi_memset,
	._h_malloc                      = hosted_osi_malloc,
	._h_calloc                      = hosted_osi_calloc,
	._h_free                        = hosted_osi_free,
	._h_realloc                     = hosted_osi_realloc,
	._h_malloc_align                = hosted_osi_malloc_align,
	._h_free_align                  = hosted_osi_free_align,

	._h_thread_create                = hosted_pool_thread_create,
	._h_thread_cancel                = hosted_pool_thread_cancel,
	._h_thread_yield                 = hosted_osi_thread_yield,

	._h_msleep                       = hosted_osi_msleep,
	._h_usleep                       = hosted_osi_usleep,
	._h_sleep                        = hosted_osi_sleep,

	._h_blocking_delay                = hosted_osi_blocking_delay,

	._h_queue_item                    = hosted_pool_queue_item,
	._h_create_queue                  = hosted_pool_create_queue,
	._h_dequeue_item                  = hosted_pool_dequeue_item,
	._h_queue_msg_waiting             = hosted_pool_queue_msg_waiting,
	._h_destroy_queue                 = hosted_pool_destroy_queue,
	._h_reset_queue                   = hosted_pool_reset_queue,

	._h_unlock_mutex                  = hosted_pool_unlock_mutex,
	._h_create_mutex                  = hosted_pool_create_mutex,
	._h_lock_mutex                    = hosted_pool_lock_mutex,
	._h_destroy_mutex                 = hosted_pool_destroy_mutex,

	._h_post_semaphore                = hosted_pool_post_semaphore,
	/* isig_sem は kernel.h で sig_sem の単純な別名(#define)であり、FMP3 には
	 * タスク文脈/非タスク文脈で実装が分かれる "ISR専用版" が無い（本移植で
	 * 確認済み）。よって _h_post_semaphore と同じ関数でよい。 */
	._h_post_semaphore_from_isr       = hosted_pool_post_semaphore,
	._h_create_semaphore              = hosted_pool_create_semaphore,
	._h_get_semaphore                 = hosted_pool_get_semaphore,
	._h_destroy_semaphore             = hosted_pool_destroy_semaphore,

	._h_timer_stop                    = hosted_pool_timer_stop,
	._h_timer_start                   = hosted_pool_timer_start,
	._h_get_time_ms                   = hosted_osi_get_time_ms,

#ifdef H_USE_MEMPOOL
	._h_create_lock_mempool           = hosted_pool_create_lock_mempool,
	._h_lock_mempool                  = hosted_pool_lock_mempool,
	._h_unlock_mempool                = hosted_pool_unlock_mempool,
	._h_destroy_lock_mempool          = hosted_pool_destroy_lock_mempool,
#endif

	._h_config_gpio                   = hosted_osi_config_gpio,
	._h_config_gpio_as_interrupt      = hosted_osi_config_gpio_as_interrupt,
	._h_teardown_gpio_interrupt       = hosted_osi_teardown_gpio_interrupt,
	._h_read_gpio                     = hosted_osi_read_gpio,
	._h_write_gpio                    = hosted_osi_write_gpio,
	._h_pull_gpio                     = hosted_osi_pull_gpio,
	._h_hold_gpio                     = hosted_osi_hold_gpio,
	._h_get_host_wakeup_or_reboot_reason = hosted_osi_get_host_wakeup_or_reboot_reason,

	._h_bus_init                      = hosted_osi_bus_init,
	._h_bus_deinit                    = hosted_osi_bus_deinit,

	._h_event_wifi_post               = hosted_osi_event_wifi_post,
	._h_printf                        = hosted_osi_printf,
	._h_hosted_init_hook              = hosted_osi_hosted_init_hook,

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
	._h_sdio_card_init                = hosted_osi_sdio_card_init,
	._h_sdio_card_deinit              = hosted_osi_sdio_card_deinit,
	._h_sdio_read_reg                 = hosted_osi_sdio_read_reg,
	._h_sdio_write_reg                = hosted_osi_sdio_write_reg,
	._h_sdio_read_block               = hosted_osi_sdio_read_block,
	._h_sdio_write_block              = hosted_osi_sdio_write_block,
	._h_sdio_wait_slave_intr          = hosted_osi_sdio_wait_slave_intr,
#endif

	._h_restart_host                  = hosted_osi_restart_host,

	._h_config_host_power_save_hal_impl = hosted_osi_config_host_power_save,
	._h_start_host_power_save_hal_impl   = hosted_osi_start_host_power_save,
	._h_event_post                       = hosted_osi_event_post,
};

/*
 *  【改変 D-4（本 repo・段7b）】ESP-IDF crosscore 割込み（絶対 CLIC 線 17）の
 *  引取り ISR を**削除した**。
 *
 *  出典がこれを持っていた理由は「IDF の FreeRTOS-SMP が `xTaskCreate()` の中で
 *  `esp_crosscore_int_send_yield()` を撃ち、その要求が消えないと `vPortYield()`
 *  のビジーウェイトが永久に抜けなくなる」ことだった（出典ヘッダの長いコメント）。
 *
 *  本段は **FreeRTOS を 1 記号もリンクしない**（AC-I4。`nm` で実測する）ので、
 *  crosscore の yield 要求を出す主体がそもそも居ない。
 *  さらに本 repo では**割込み線は C-1 CLIC シムの利用者表が唯一の正本**であり、
 *  線 17 をここから直接掴むと 7a と同じ「利用者ごとに線を写して食い違う」型の
 *  欠陥を新しく作る。⇒ 必要になったら**表に行を足してから**復活させる。
 */

/*
 *  =====================================================================
 *  初期化関数
 *  =====================================================================
 */
void
p4hosted_osi_init(void)
{
	g_h.funcs = &g_hosted_osi_funcs;

	syslog(LOG_NOTICE,
			"wifi_p4_module: fmp3_hosted_osi initialized "
			"(task_pool=%d sem_pool=%d queue_pool=%d mutex_pool=%d timer_pool=%d)",
			(int_t) HOSTED_POOL_NUM_TASK, (int_t) HOSTED_POOL_NUM_SEM,
			(int_t) HOSTED_POOL_NUM_QUEUE, (int_t) HOSTED_POOL_NUM_MUTEX,
			(int_t) HOSTED_POOL_NUM_TIMER);
}
