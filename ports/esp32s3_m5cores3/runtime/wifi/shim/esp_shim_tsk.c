/*
 *  TOPPERS/FMP3 ESP32-S3 / ESP32(LX6) port
 *
 *  シムの blob 用タスクプール — **動的生成（`acre_tsk`/`del_tsk`）へ写像する**
 *
 *  ========================================================================
 *  方式
 *  ========================================================================
 *  カーネルの動的生成サービスコール `acre_tsk` / `mact_tsk` / `del_tsk` を
 *  直接使う。
 *
 *  ★**静的版と動的版の分岐は作らない**（`#ifdef` も実行時分岐も）。無条件に
 *    動的生成。写像先を構成で切り替える分岐は、過去に不具合を再発させた形である。
 *    下にある `#if defined(TOPPERS_BT_HOST_NIMBLE) …` は**静的/動的の分岐ではない**——
 *    旧 cfg の `CRE_TSK(SHIM_TSK1, …)` が持っていた**構成ごとの優先度**を
 *    そのまま写したものである（§「優先度」参照）。
 *
 *  ========================================================================
 *  スタックは**シムが供給する**（案B）。これが段1〜段3 との本質的な差
 *  ========================================================================
 *  `acre_tsk` は `stk == NULL` なら `aligned_alloc_mpk()` で**カーネルメモリプール
 *  （`DEF_MPK`）からスタックを取る**（`fmp3_core/kernel/task_manage.c`）。
 *  そのプールは **bump アロケータ**で、`free_mpk` は割当カウントを減らすだけ
 *  ——brk が戻るのは**カウント 0 の瞬間だけ**（`fmp3_core/docs/dynamic-creation.md` §6）。
 *  **タスクの生成/削除サイクルは実在する**（発生源は M5Unified `Speaker`/`Mic` の
 *  `begin()`/`end()`）。**回数の上限が静的に決まらない**ので、`stk == NULL` では
 *  **足りる `DEF_MPK` の値が存在しない**。
 *
 *  ⇒ **本 TU は `stk` を必ず非 NULL で渡す。**その結果:
 *     ・`TA_MEMALLOC` は立たない ＝ **`del_tsk` はカーネルメモリを 1 バイトも解放しない**。
 *     ・**`DEF_MPK` は要らない**（`_kernel_mpksz = 0` のままでよい）。
 *
 *  ------------------------------------------------------------------------
 *  供給源に何を選んだか＝**シムが持つ静的スタックスロット配列**。理由と却下理由
 *  ------------------------------------------------------------------------
 *  **供給源＝シムが持つ静的スタックスロット配列**
 *    `shim_tsk_stack[ESP_SHIM_NUM_TSK][ESP_SHIM_TSK_STKSZ]`（16 整列）。
 *    ⇒ **DRAM 予算が静的に決まる性質を保つ。**
 *
 *  ★**シムヒープ（`esp_shim_malloc`）へ移してはならない（今は）。**
 *    スタックは 4,096〜8,192 B と大きく、ヒープの高水位を測っていない。
 *    静的に証明できる予算を実行時の予算へ変えると **Wi-Fi blob の作業メモリと
 *    競合**し、足りるかは実機でしか分からない。またシムヒープの払い出しは
 *    8 整列だが `CHECK_STACK_ALIGN` は 16 を要求する。
 *    ⇒ 却下の理由は「ヒープが悪い」ではなく**「測っていないものへ移さない」**。
 *      移せば `.bss` が 24〜64 KB 減る利得は実在する。
 *
 *  ★**`stk == NULL` ＋ `DEF_MPK` も採れない。** bump アロケータで単調に痩せ、
 *    足りる容量が存在しない。
 *
 *  **メモリの量と置き場所の性質は旧構造と同じ**である。ここで無くしたのは
 *  「個数を 3 箇所に書く」構造（cfg の `CRE_TSK` 8 行・C 配列 `tskid_tbl[]`・
 *  マクロ `ESP_SHIM_NUM_TSK`）であって、DRAM の使い方ではない。
 *
 *
 *  ------------------------------------------------------------------------
 *  整列とサイズの検査を満たす根拠（コメントではなく構造で示す）
 *  ------------------------------------------------------------------------
 *  `acre_tsk` は `stk != NULL` のとき次を通す（`fmp3_core/kernel/task_manage.c`）:
 *    ・`CHECK_PAR(stksz >= TARGET_MIN_STKSZ)`
 *    ・`CHECK_PAR(STKSZ_ALIGN(stksz))`  … Xtensa は `CHECK_STKSZ_ALIGN 16`
 *    ・`CHECK_PAR(STACK_ALIGN(stk))`    … Xtensa は `CHECK_STACK_ALIGN 16`
 *      （`fmp3/arch/xtensa_gcc/common/core_kernel_impl.h:95,98`）
 *  **`stk != NULL` のとき `stksz` は `ROUND_STK_T` で丸められない**——渡す側の責任である。
 *  ⇒ 本 TU は:
 *    ・配列に `__attribute__((aligned(16)))` を付ける（先頭が 16 整列）。
 *    ・1 スロット分の長さを `SHIM_TSK_STKSZ`＝`ROUND16(ESP_SHIM_TSK_STKSZ)` にする
 *      （**要素長が 16 の倍数**なので 2 本目以降の先頭も 16 整列）。
 *    ・`_Static_assert` で「16 の倍数であること」と「0 でないこと」を**コンパイル時に**落とす。
 *  それでも実行時に整列を確かめて fail-closed する（写しが腐る備えはコメントではなく検査。
 *    段2／段3 と同じ作法）。
 *
 *  ========================================================================
 *  自己削除と回収
 *  ========================================================================
 *  `del_tsk` は**休止状態のタスクしか消せない**（`fmp3_core/kernel/task_manage.c:262-263`。
 *  非休止／静的タスクは `E_OBJ`）。`ext_tsk` は資源を解放しない。
 *  ⇒ **走行中の自タスクは自分の ID を返せない。**回収は移植側の責任である
 *    （`fmp3_core/docs/qa-esp32s3-20260804.md` R-2 で確定。カーネルは `ext_tsk` で解放しない）。
 *
 *  **選んだ回収機構＝「次回 create の先頭で、休止した pend_del スロットを掃除する」。**
 *   ・自己終了したタスクは**スロットを保持したまま** `pend_del` を立てて `ext_tsk` する。
 *   ・次に誰かが `esp_shim_task_create()` を呼ぶと、その先頭で
 *     `get_tst()` が `TTS_DMT` を返すスロットだけ `del_tsk` して解放する。
 *   ・⇒ **容量は減らない**（掃除は選択の**前**に走るので、
 *     「掃除すれば空くのに枯渇した」という状態が構造的に起こらない）。
 *
 *  **却下した(i): reaper タスク**。常駐タスクが 1 本増える＝**スタックがもう 1 本要る**
 *    （本段が消そうとしている当のコスト）。さらに reaper の起床契機（周期／セマフォ）を
 *    新設することになり、**回収が時間に依存する**。
 *  **却下した(ii): 自己削除の直前にスロットだけ返す（旧実装のまま）**。
 *    静的プールならこれで足りた（ID がスロットに固定されていたから）。動的では
 *    **ID が回収されず `AID_TSK` の予約が単調に減る**——サイクルの上限が静的に決まらない以上、
 *    **いつか必ず `E_NOID` になる。**
 *  **却下した(iii): `ext_tsk` の代わりに「削除係へ委譲してから寝る」**。
 *    自タスクのスタック上で走り続ける以上、他タスクが `del_tsk` できる状態にならない。
 *
 *  **この機構が持つ性質を正直に書く**:
 *   ・**回収は「次の create」まで遅れる。**create が二度と来なければ ID は返らない。
 *     実害は無い（枠を占めるだけで、その枠を必要とする者が居ないということだから）。
 *   ・**`get_tst` が `TTS_DMT` を返した瞬間、そのタスクはまだ数命令だけ
 *     自分のスタック上で走っている**（`fmp3_core/docs/dynamic-creation.md` §7.1(1) の
 *     受容済みウィンドウ）。本 TU はスタックを**解放しない**（静的スロットのまま）ので、
 *     **この窓でスタックが別用途に化けることは起きない。**
 *     スロットの再利用（＝`acre_tsk` で同じスタックを渡す）は起こり得るが、
 *     それは**単一コア（`TNUM_PRCID == 1`）では起こらない**——掃除を走らせるタスクは、
 *     終了中のタスクが `exit_and_dispatch()` を終えるまで走れないからである。
 *     **`TNUM_PRCID >= 2` の構成では窓が実在する。**本段の 10 preset で
 *     `esp_shim.c` をリンクする 7 構成はすべて `TNUM_PRCID == 1`だが、
 *     **SMP のシムを作る人はここを読むこと。**
 *
 *  ========================================================================
 *  コア固定
 *  ========================================================================
 *  `acre_tsk` にコアを指定する引数は無い。生成直後は必ず **PRC1 割付け・affinity 全コア**
 *  （`fmp3_core/docs/dynamic-creation.md` §5.1）。⇒ **`TA_ACT` を付けずに生成し、
 *  `mact_tsk(tskid, prcid)` で起動する**（§3-1 パターン1・QEMU 実証済み）。
 *  `TA_ACT` を付けると即座に PRC1 で起動してコア選択の機会が無い。
 *
 *  **「どちらでもよい」を「どちらかに決める」ポリシー**:
 *   ・`core_id < TNUM_PRCID` … `prcid = core_id + 1`（FreeRTOS 0 起点 / FMP3 1 起点）。
 *   ・`core_id == ESP_SHIM_TASK_NO_AFFINITY`（= `CONFIG_FREERTOS_NO_AFFINITY` = 0x7FFFFFFF）
 *     … **PRC1 を選ぶ。**理由: 時刻管理プロセッサであり、**現行のシムタスクが
 *     全部そこで走っている**（＝非退行）。ラウンドロビンは負荷分散の設計であって
 *     本段の主題ではなく、実機で確かめられない。
 *   ・`core_id >= TNUM_PRCID`（存在しないコアを名指しされた）
 *     … **PRC1 へ落として数える＋診断を出す。**fail-closed にしない——
 *     現行は `(void) core_id` で**捨てて**おり、全部 PRC1 で動いている。ここで断ると
 *     `TNUM_PRCID == 1` の構成で blob のタスク生成が丸ごと失敗する＝**退行**である。
 *     ⇒ **「捨てている」から「見た上で、記録した上で、無視している」へ変えた。**
 *       カウンタ `esp_shim_tsk_core_clamped` が 0 でないことが、
 *       **blob が実際にコア固定を要求した実行時の証拠**になる。
 *
 *  `mact_tsk` は 2 本目のサービスコールなので、**失敗したら `del_tsk` で巻き戻す**
 *  （`esp-idf/components/esp_driver_spi/src/gpspi/spi_master.c` の `nomem:` と同じ形）。
 *  `acre_tsk` が成功して返した ID にしか `mact_tsk` を撃たない（
 *   未生成スロット ID への `mact_tsk` は `E_PAR` になる既知の穴がある＝§7-2）。
 *
 *  ========================================================================
 *  `TA_FPU`（このリポジトリでは前提が成り立たない）
 *  ========================================================================
 *  §7-7 は「FreeRTOS API に FPU 属性が無いので**全動的タスクに `TA_FPU` を付けるしかない**」
 *  と書いているが、**現行の静的定義はそうなっていない**:
 *      `esp_shim.cfg`: `CRE_TSK(SHIM_TSKn, { SHIM_TA_FPU, … })`
 *      `esp_shim_cfg.h`: `SHIM_TA_FPU` = **`TA_FPU`（無印ESP32/LX6）／`TA_NULL`（S3）**
 *  属性は FreeRTOS API から来ていない——**チップごとのビルド時定数**としてシムが既に持っている。
 *  ⇒ **同じマクロを実行時に `acre_tsk` へ渡すだけでよい。**
 *    `acre_tsk` は `CHECK_VALIDATR(tskatr, TA_ACT|TA_NOACTQUE|TARGET_TSKATR)` で
 *    `TARGET_TSKATR == TA_FPU`（`core_kernel_impl.h:85`）を許す。
 *  ⇒ **属性は旧構造とビット単位で同一＝コストの増減はゼロ。**
 *    「全タスクに `TA_FPU` を付けるコスト」は**発生しない**（付けないから）。
 *
 *  ========================================================================
 *  文脈の制約
 *  ========================================================================
 *  `acre_tsk`/`mact_tsk`/`del_tsk`/`get_tst` は `CHECK_TSKCTX_UNL`＝
 *  **タスク文脈かつ CPU ロック外**でしか成功しない。
 *  ⇒ **本 TU はサービスコールを `SHIM_LOCK` の外で呼ぶ**（そもそも本 TU は `SHIM_LOCK` を
 *   取らない。呼出し側 `esp_shim.c` がスロット表を触る区間とは**重ならない**並びにしてある）。
 *  ⇒ 禁止文脈から呼ばれた場合は `E_CTX` になる。**畳まず診断に残す**。
 *
 *  ========================================================================
 *  エラー値の扱い（**4 つを畳まない**）
 *  ========================================================================
 *   (a) **`E_NOID`** … `AID_TSK` の予約が尽きた（カーネルの ID 枯渇）。→ `acre_fail`
 *   (b) **`E_NOMEM`** … **本 TU では原理的に起きない**（`stk` を必ず渡すので
 *       `aligned_alloc_mpk` を通らない）。起きたら**それ自体が異常**なので生値で残す。
 *   (c) **`mact_tsk` の失敗**（`E_ID`＝存在しないコア／`E_OBJ`／`E_CTX`）。→ `mact_fail`
 *   (d) **シムのスロット枯渇**（"task pool exhausted"）… `esp_shim.c` 側の事象で、
 *       本 TU は関与しない。
 *  **値を意味へ翻訳した分岐表は作らない**（pin 更新で腐る）。**値をそのまま診断へ通す。**
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "kernel_cfg.h"			/* TNUM_TSKID（cfg 生成。静的＋AID_TSK の総数） */
#include "esp_shim.h"
#include "esp_shim_cfg.h"

/*
 *  ID の下限。カーネルの `TMIN_TSKID` は内部ヘッダにあり、シム層から
 *  include してはいけない。値は FMP3 の全オブジェクト種で 1 固定である。
 *  写しが腐ることへの備えは**実行時の範囲検査**（下記）。段2／段3 と同じ作法。
 */
#define SHIM_TSK_TMIN_ID	1

/*  整列要求（`CHECK_STACK_ALIGN` / `CHECK_STKSZ_ALIGN`）。カーネル内部ヘッダを
 *  読めないので写すが、**写した値より厳しい整列しか渡さない**ことは構造で保証する。 */
#define SHIM_TSK_STK_ALIGN	16U

/*  1 スロット分のスタック長（16 の倍数へ切り上げる。`stk != NULL` のとき
 *  カーネルは `ROUND_STK_T` を通さないので、渡す側が丸める責任を負う）。 */
#define SHIM_TSK_STKSZ \
	((((size_t) ESP_SHIM_TSK_STKSZ) + (SHIM_TSK_STK_ALIGN - 1U)) \
	 & ~((size_t)(SHIM_TSK_STK_ALIGN - 1U)))

/*  コンパイル時に落とす（実行時の検査だけに頼らない）。 */
_Static_assert((SHIM_TSK_STKSZ % SHIM_TSK_STK_ALIGN) == 0U,
			   "shim task stack size must be a multiple of 16");
_Static_assert(SHIM_TSK_STKSZ >= 1024U,
			   "shim task stack size looks too small");
_Static_assert((SHIM_TSK_STKSZ % sizeof(STK_T)) == 0U,
			   "shim task stack size must be a whole number of STK_T");

/*
 *  スタックの供給源（案B）。旧 cfg の `_kernel_stack_SHIM_TSK1..8` と
 *  **同じ本数・同じサイズ**をシム側で持つ。詳細と却下した選択肢は本ファイル冒頭。
 *
 *  置き場所が変わることを隠さない: 旧 `_kernel_stack_SHIM_TSK*` は
 *  `kernel_cfg.o` の `.bss` ＝ リンカスクリプトの**隔離領域 `.kernel_bss`** に居た。
 *  本配列は `esp_shim_tsk.o` の `.bss` ＝ 通常 `.bss` に居る。
 *  **両者は同じ `> DRAM` 領域に連続配置される**ので**総量は変わらない**
 *  （`fmp3/target/esp32s3_devkitc_gcc/esp32s3_xip.ld`）。
 *  隔離の保護価値は 2026 年の実機 JTAG 解析で否定済み（同 ld の [追記56]。
 *   真因は HRT の CCOUNT ラップ不整合と ZOL 未退避であってメモリ配置ではなかった）。
 */
static STK_T shim_tsk_stack[ESP_SHIM_NUM_TSK][SHIM_TSK_STKSZ / sizeof(STK_T)]
		__attribute__((aligned(SHIM_TSK_STK_ALIGN)));

/*
 *  診断カウンタ（JTAG／将来のダンプから読めるように公開する）。
 *  `ercd` は**翻訳せずそのまま**残す（E_NOID=-34 / E_CTX=-25 / E_ID=-18 など）。
 */
volatile uint32_t	esp_shim_tsk_acre_fail;			/* acre_tsk が失敗した回数      */
volatile int32_t	esp_shim_tsk_acre_last_ercd;	/* 直近の acre_tsk のエラー値   */
volatile uint32_t	esp_shim_tsk_mact_fail;			/* mact_tsk が失敗した回数      */
volatile int32_t	esp_shim_tsk_mact_last_ercd;	/* 直近の mact_tsk のエラー値   */
volatile uint32_t	esp_shim_tsk_del_fail;			/* del_tsk が失敗した回数       */
volatile int32_t	esp_shim_tsk_del_last_ercd;		/* 直近の del_tsk のエラー値    */
volatile uint32_t	esp_shim_tsk_live;				/* 生きている本数（+1/-1）      */
volatile uint32_t	esp_shim_tsk_reaped;			/* 回収した本数                 */
/*  blob が「存在しないコア」を名指しした回数。0 でないことが
 *  「blob は実際にコア固定を要求している」実行時の証拠になる（本ファイル冒頭）。 */
volatile uint32_t	esp_shim_tsk_core_clamped;

/*
 *  診断（沈黙させない）。氾濫を避けるため最初の 4 回とその後 1024 回ごとに出す。
 *  「出す回数を絞る」ことと「出さない」ことは違う——カウンタは常に進む
 *  （`esp_shim_sem.c`・`esp_shim_dtq.c`・`esp_shim_isr_ctx.c` と同じ作法）。
 */
static void
shim_tsk_report(const char *what, ER ercd, volatile uint32_t *p_cnt)
{
	uint32_t	n;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	n = *p_cnt + 1U;
	*p_cnt = n;
	SIL_UNL_INT();

	if ((n <= 4U) || ((n % 1024U) == 0U)) {
		syslog(LOG_ERROR,
			   "esp_shim: %s failed ercd=%d (ctx=%d lock=%d) n=%d",
			   what, (int_t) ercd,
			   (int_t) (sns_ctx() ? 1 : 0), (int_t) (sns_loc() ? 1 : 0),
			   (int_t) n);
	}
}

static void
shim_tsk_count(volatile uint32_t *p_cnt, int32_t delta)
{
	SIL_PRE_LOC;

	SIL_LOC_INT();
	*p_cnt = (uint32_t)((int32_t) *p_cnt + delta);
	SIL_UNL_INT();
}

/*
 *  コア指定の写像（ポリシーは本ファイル冒頭「コア固定」）。
 *  「捨てている」から「見た上で無視している」へ変えた実体がこれである。
 */
static ID
shim_tsk_prcid(uint32_t core_id)
{
#ifdef W1N3_PROBE
	/*
	 *  N-3: blob が渡した
	 *  core_id の**生値**を出す。opt-in。既定では 1 バイトも増えない。
	 *  ここで見えるのは「要求が写像関数まで届いた」ことだけである——
	 *  TNUM_PRCID==1 では「配置に反映される」ことは観測できない（AC 3-N-c）。
	 *  先頭 8 回だけ出す（BT は起動時に数本しか作らない。溢れたら数だけ増える）。
	 */
	{
		static uint32_t	w1n3_n;

		w1n3_n++;
		if (w1n3_n <= 8U) {
			syslog_2(LOG_NOTICE, "N3 prcid core_id=%d (raw) call=%d",
					 (int_t) core_id, (int_t) w1n3_n);
		}
	}
#endif /* W1N3_PROBE */
	if (core_id == (uint32_t) ESP_SHIM_TASK_NO_AFFINITY) {
		/*  「どちらでもよい」⇒ PRC1 に決める（現行と同じ場所＝非退行）。 */
		return((ID) 1);
	}
	if (core_id < (uint32_t) TNUM_PRCID) {
		return((ID)(core_id + 1U));
	}
	/*  存在しないコアを名指しされた。落として**数える**（黙らない）。
	 *  fail-closed にしない理由は本ファイル冒頭（断ると TNUM_PRCID==1 で退行する）。 */
	shim_tsk_count(&esp_shim_tsk_core_clamped, 1);
	if ((esp_shim_tsk_core_clamped <= 4U)
			|| ((esp_shim_tsk_core_clamped % 1024U) == 0U)) {
		syslog(LOG_NOTICE,
			   "esp_shim: core_id %u >= TNUM_PRCID %d -> PRC1 で起動する n=%d",
			   (uint_t) core_id, (int_t) TNUM_PRCID,
			   (int_t) esp_shim_tsk_core_clamped);
	}
	return((ID) 1);
}

/*
 *  タスクを 1 本生成する（休止状態のまま返す。起動は `esp_shim_tsk_activate()`）。
 *
 *  `slot`    … スタックスロット番号（`0 <= slot < ESP_SHIM_NUM_TSK`）。
 *  `task`    … 入口。`exinf` … 入口へ渡す値（呼出し側はスロット番号を使う）。
 *  `itskpri` … 初期優先度。
 *
 *  戻り値 … タスク ID（成功）／**0（失敗。診断は本 TU が出す）**。
 *
 *  **CPU ロックの外・タスク文脈で呼ぶこと**（本ファイル冒頭「文脈の制約」）。
 */
ID
esp_shim_tsk_create(uint_t slot, TASK task, EXINF exinf, PRI itskpri)
{
	T_CTSK		ctsk;
	ER_ID		ercd;

	if (slot >= (uint_t) ESP_SHIM_NUM_TSK) {
		/*  起こり得ない（呼出し側がスロット表から採る）。起きたら渡さない。 */
		shim_tsk_report("stack slot range", E_PAR, &esp_shim_tsk_acre_fail);
		return(0);
	}
	if ((((uintptr_t) &shim_tsk_stack[slot][0])
			& (uintptr_t)(SHIM_TSK_STK_ALIGN - 1U)) != 0U) {
		/*  起こり得ない（冒頭「整列」の根拠＋`_Static_assert`）。起きたら黙って渡さない。 */
		shim_tsk_report("stack align", E_PAR, &esp_shim_tsk_acre_fail);
		return(0);
	}

	/*  属性は静的生成時（`CRE_TSK(…, { SHIM_TA_FPU, … })`）と**ビット単位で同じ**。
	 *  `TA_ACT` は**付けない**——付けると即座に PRC1 で起動してコア選択の機会が消える
	 *  。起動は `mact_tsk` で行う。 */
	ctsk.tskatr = SHIM_TA_FPU;
	ctsk.exinf = exinf;
	ctsk.task = task;
	ctsk.itskpri = itskpri;
	ctsk.stksz = SHIM_TSK_STKSZ;
	ctsk.stk = &shim_tsk_stack[slot][0];	/* 非 NULL ＝ TA_MEMALLOC を立てさせない */

	ercd = acre_tsk(&ctsk);
	if (ercd <= 0) {
		/*  (a) `E_NOID`（予約枯渇）／`E_CTX`（文脈）／`E_RSATR`／`E_PAR`。
		 *  (b) `E_NOMEM` は原理的に来ない（`stk` を渡している）。来たら異常。
		 *  **翻訳も畳みもせず生値で**残す。 */
		esp_shim_tsk_acre_last_ercd = (int32_t) ercd;
		shim_tsk_report("acre_tsk", (ER) ercd, &esp_shim_tsk_acre_fail);
		return(0);
	}
	if ((ercd < SHIM_TSK_TMIN_ID)
		|| (ercd >= (SHIM_TSK_TMIN_ID + (ID) TNUM_TSKID))) {
		/*  起こり得ないが、起きたら黙って配るより落とす方がよい。 */
		esp_shim_tsk_acre_last_ercd = (int32_t) ercd;
		shim_tsk_report("acre_tsk(range)", E_SYS, &esp_shim_tsk_acre_fail);
		(void) del_tsk((ID) ercd);
		return(0);
	}

	shim_tsk_count(&esp_shim_tsk_live, 1);
	return((ID) ercd);
}

#ifdef M5_SHIM_AUDIO
/*
 *  ========================================================================
 *  音声プール（M5Unified Speaker/Mic）の生成
 *  ========================================================================
 *  **blob プールと別の関数にした理由**（＝`esp_shim_tsk_create()` を一般化しなかった理由）:
 *   1. **スタック長が違う**（音声 `SHIM_AUDIO_TSK_STKSZ`=4096 ／
 *      blob `ESP_SHIM_TSK_STKSZ`=6656 or 8192）。同じ配列に載せられない。
 *   2. **属性が違う**——音声は**両チップとも `TA_FPU`**（浮動小数点を使う）。
 *      blob は `SHIM_TA_FPU`（LX6=TA_FPU／**S3=TA_NULL**）。
 *      ここを取り違えると S3 で音声が FPU 無しでスイッチされる（＝旧 cfg が
 *      わざわざ `TA_FPU` を直書きしていた理由。`esp_shim_cfg.h` の同箇所）。
 *   3. **一般化すると `esp_shim_tsk_create()` の署名が変わり、
 *      `M5_SHIM_AUDIO` を定義しない 9 構成のコードまで動く**。
 *      ⇒ **「変更が届く構成」を 1 本に閉じ込めるため**に、丸ごと `#ifdef` の中へ置く。
 *      これは**静的版／動的版の分岐ではない**（H4 の型ではない）——
 *      **音声プールが在るか無いか**の分岐であって、在るときは**必ず動的**である。
 *
 *  整列とサイズの根拠は本ファイル冒頭「整列」と同じ（16 整列・16 の倍数長）。
 *  回収（`del_tsk`）と起動（`mact_tsk`）は **blob と同じ関数を使う**
 *  （`esp_shim_tsk_activate` / `esp_shim_tsk_reap` / `esp_shim_tsk_terminate` は
 *   `tskid` しか受け取らない＝プールに依存しない）。⇒ **回収機構は 1 つだけである。**
 */
#define SHIM_AUDIO_STKSZ \
	((((size_t) SHIM_AUDIO_TSK_STKSZ) + (SHIM_TSK_STK_ALIGN - 1U)) \
	 & ~((size_t)(SHIM_TSK_STK_ALIGN - 1U)))

_Static_assert((SHIM_AUDIO_STKSZ % SHIM_TSK_STK_ALIGN) == 0U,
			   "audio task stack size must be a multiple of 16");
_Static_assert(SHIM_AUDIO_STKSZ >= 1024U,
			   "audio task stack size looks too small");
_Static_assert((SHIM_AUDIO_STKSZ % sizeof(STK_T)) == 0U,
			   "audio task stack size must be a whole number of STK_T");

/*
 *  旧 cfg の `_kernel_stack_SHIM_AUDIO_TSK1/2` と**同じ本数・同じサイズ**。
 *  置き場所は `kernel_cfg.o` の `.kernel_bss` から `esp_shim_tsk.o` の通常 `.bss` へ
 *  移るが、**同じ `> DRAM` 領域に連続配置される**ので総量は変わらない
 *  （blob 側と同じ。本ファイル冒頭の該当節）。
 */
static STK_T shim_audio_stack[SHIM_AUDIO_NTSK][SHIM_AUDIO_STKSZ / sizeof(STK_T)]
		__attribute__((aligned(SHIM_TSK_STK_ALIGN)));

/*
 *  音声タスクを 1 本生成する（休止状態のまま返す。起動は `esp_shim_tsk_activate()`）。
 *  戻り値 … タスク ID（成功）／**0（失敗。診断は本 TU が出す）**。
 *  **CPU ロックの外・タスク文脈で呼ぶこと**。
 */
ID
esp_shim_audio_tsk_create(uint_t slot, TASK task, EXINF exinf, PRI itskpri)
{
	T_CTSK		ctsk;
	ER_ID		ercd;

	if (slot >= (uint_t) SHIM_AUDIO_NTSK) {
		shim_tsk_report("audio stack slot range", E_PAR, &esp_shim_tsk_acre_fail);
		return(0);
	}
	if ((((uintptr_t) &shim_audio_stack[slot][0])
			& (uintptr_t)(SHIM_TSK_STK_ALIGN - 1U)) != 0U) {
		shim_tsk_report("audio stack align", E_PAR, &esp_shim_tsk_acre_fail);
		return(0);
	}

	/*  属性は旧 `CRE_TSK(SHIM_AUDIO_TSK1, { TA_FPU, … })` と**ビット単位で同じ**。
	 *  `TA_ACT` は付けない（起動は `mact_tsk`。blob と同じ理由）。 */
	ctsk.tskatr = TA_FPU;
	ctsk.exinf = exinf;
	ctsk.task = task;
	ctsk.itskpri = itskpri;
	ctsk.stksz = SHIM_AUDIO_STKSZ;
	ctsk.stk = &shim_audio_stack[slot][0];	/* 非 NULL ＝ TA_MEMALLOC を立てさせない */

	ercd = acre_tsk(&ctsk);
	if (ercd <= 0) {
		/*  `E_NOID`／`E_CTX`／`E_RSATR`／`E_PAR`。翻訳も畳みもせず生値で残す。 */
		esp_shim_tsk_acre_last_ercd = (int32_t) ercd;
		shim_tsk_report("acre_tsk(audio)", (ER) ercd, &esp_shim_tsk_acre_fail);
		return(0);
	}
	if ((ercd < SHIM_TSK_TMIN_ID)
		|| (ercd >= (SHIM_TSK_TMIN_ID + (ID) TNUM_TSKID))) {
		esp_shim_tsk_acre_last_ercd = (int32_t) ercd;
		shim_tsk_report("acre_tsk(audio range)", E_SYS, &esp_shim_tsk_acre_fail);
		(void) del_tsk((ID) ercd);
		return(0);
	}

	shim_tsk_count(&esp_shim_tsk_live, 1);
	return((ID) ercd);
}
#endif /* M5_SHIM_AUDIO */

/*
 *  生成済み（休止中）のタスクを、指定されたコアで起動する。
 *
 *  **失敗したら `del_tsk` で巻き戻す**（リーク防止）。
 *  呼出し側は「成功したら ID が生きている／失敗したら ID は無い」だけを見ればよい。
 *
 *  戻り値 … `true`（起動した）／`false`（失敗。ID は削除済み）。
 */
bool_t
esp_shim_tsk_activate(ID tskid, uint32_t core_id)
{
	ER		ercd;

	if ((tskid < SHIM_TSK_TMIN_ID)
		|| (tskid >= (SHIM_TSK_TMIN_ID + (ID) TNUM_TSKID))) {
		/*  `acre_tsk` が成功して返した ID にしか撃たない（§3-2(d)）。 */
		shim_tsk_report("mact_tsk(range)", E_ID, &esp_shim_tsk_mact_fail);
		return(false);
	}

#ifdef W1N3_PROBE
	{
		/*
		 *  BL-B-1 / LM-N-3: これまでの判定 STALE の根拠は
		 *  「要求が写像関数まで届いた」までであった（shim_tsk_prcid の probe）。
		 *  TNUM_PRCID>=2 の構成では、その先の
		 *  「カーネルが実際にそのプロセッサへ割り付けたか」を ref_tsk で読める。
		 *  ref_tsk の prcid は割付け先であって実行実績ではない——
		 *  ここで言えるのは割付けまでである。
		 *  opt-in。既定では 1 バイトも増えない。先頭 8 本だけ出す。
		 */
		static uint32_t	n3act_n;
		ID				want = shim_tsk_prcid(core_id);
		ER				e2;
		T_RTSK			rtsk;

		n3act_n++;
		e2 = mact_tsk(tskid, want);
		if (n3act_n <= 8U) {
			ID		got = 0;
			ER		e3;

			e3 = ref_tsk(tskid, &rtsk);
			if (e3 == E_OK) {
				got = rtsk.prcid;
			}
			syslog(LOG_NOTICE,
				   "N3 act tsk=%d want_prc=%d mact=%d ref_prc=%d nprc=%d",
				   (int_t) tskid, (int_t) want, (int_t) e2,
				   (int_t) got, (int_t) TNUM_PRCID);
		}
		ercd = e2;
	}
#else /* W1N3_PROBE */
	ercd = mact_tsk(tskid, shim_tsk_prcid(core_id));
#endif /* W1N3_PROBE */
	if (ercd != E_OK) {
		/*  (c) `E_ID`（存在しないコア。`E_PAR` ではない＝§3-2(b)）／`E_OBJ`／`E_CTX`。
		 *  **翻訳せず生値で**残す。 */
		esp_shim_tsk_mact_last_ercd = (int32_t) ercd;
		shim_tsk_report("mact_tsk", ercd, &esp_shim_tsk_mact_fail);
		/*  巻き戻す。生成直後は休止状態なので `del_tsk` は通る。 */
		ercd = del_tsk(tskid);
		if (ercd != E_OK) {
			esp_shim_tsk_del_last_ercd = (int32_t) ercd;
			shim_tsk_report("del_tsk(rollback)", ercd, &esp_shim_tsk_del_fail);
		}
		else {
			shim_tsk_count(&esp_shim_tsk_live, -1);
		}
		return(false);
	}
	return(true);
}

/*
 *  休止していれば削除して ID を返す（回収機構の実体。本ファイル冒頭「自己削除と回収」）。
 *
 *  戻り値 … `true`（削除した＝スロットを解放してよい）／`false`（まだ休止していない）。
 *
 *  `get_tst` が `TTS_DMT` を返す瞬間、そのタスクはまだ数命令だけ自分のスタック上で
 *  走っている（`fmp3_core/docs/dynamic-creation.md` §7.1(1)）。本 TU はスタックを
 *  **解放しない**（静的スロット）ので、その窓でスタックが別用途に化けることは無い。
 */
bool_t
esp_shim_tsk_reap(ID tskid)
{
	STAT	tskstat;
	ER		ercd;

	if ((tskid < SHIM_TSK_TMIN_ID)
		|| (tskid >= (SHIM_TSK_TMIN_ID + (ID) TNUM_TSKID))) {
		return(false);
	}

	ercd = get_tst(tskid, &tskstat);
	if (ercd != E_OK) {
		/*  `E_NOEXS`（既に消えている）／`E_CTX`（禁止文脈）。生値で残す。 */
		esp_shim_tsk_del_last_ercd = (int32_t) ercd;
		shim_tsk_report("get_tst", ercd, &esp_shim_tsk_del_fail);
		return(false);
	}
	if (tskstat != TTS_DMT) {
		return(false);			/* まだ走っている。次の機会に回す（黙る＝正常） */
	}

	ercd = del_tsk(tskid);
	if (ercd != E_OK) {
		/*  静的タスクを渡されると `E_OBJ`、既に消えていれば `E_NOEXS`。生値で残す。 */
		esp_shim_tsk_del_last_ercd = (int32_t) ercd;
		shim_tsk_report("del_tsk", ercd, &esp_shim_tsk_del_fail);
		return(false);
	}
	shim_tsk_count(&esp_shim_tsk_live, -1);
	shim_tsk_count(&esp_shim_tsk_reaped, 1);
	return(true);
}

/*
 *  他タスクを強制終了してから削除する（`vTaskDelete(handle != NULL)` 相当）。
 *
 *  `ter_tsk` は**同期的に**休止状態へ落とす（自タスクには使えないが、ここは他タスク）。
 *  ⇒ そのまま `del_tsk` できる＝**この経路は回収を遅らせない**。
 *
 *  戻り値 … `true`（削除した）／`false`（ID がまだ生きている＝スロットを解放してはいけない）。
 */
bool_t
esp_shim_tsk_terminate(ID tskid)
{
	ER		ercd;

	if ((tskid < SHIM_TSK_TMIN_ID)
		|| (tskid >= (SHIM_TSK_TMIN_ID + (ID) TNUM_TSKID))) {
		return(false);
	}

	ercd = ter_tsk(tskid);
	if ((ercd != E_OK) && (ercd != E_OBJ)) {
		/*  `E_OBJ` は「既に休止」＝正常な通り道なので数えない。
		 *  それ以外（`E_CTX`／`E_NOEXS`／`E_ILUSE`）は残す。 */
		esp_shim_tsk_del_last_ercd = (int32_t) ercd;
		shim_tsk_report("ter_tsk", ercd, &esp_shim_tsk_del_fail);
	}
	return(esp_shim_tsk_reap(tskid));
}
