/*
 *  ESP32-P4 + M5Stack Stamp-AddOn C6 (ESP-Hosted) 移植
 *  os_adapter 静的プール実装
 *
 *  FMP3 は完全な静的コンフィギュレーションであり acre_tsk/acre_sem のような
 *  実行時オブジェクト生成 API を持たない。本ファイルは、`p4hosted.cfg` で
 *  静的に CRE_ 済みのタスク/セマフォ/ミューテックス/データキュー/アラームの
 *  「プール」から空き要素を確保・返却することで、esp-hosted host が要求する
 *  動的 `_h_create_*`/`_h_destroy_*` セマンティクスを模倣する。
 *
 *  各プールの排他は 1 本の管理用ミューテックス `HOSTED_POOL_META_MTX` で保護する。
 *  `_h_create_*`/`_h_destroy_*` は esp-hosted 側でもタスク文脈からのみ呼ばれる
 *  （ISR から動的生成/破棄を行う設計にはなっていない）ため、通常の loc_mtx/unl_mtx
 *  で問題ない。
 *
 *  データキューの実現方式（重要）:
 *    FMP3 のデータキュー(dtq)は intptr_t 1 個しか運べない（ポインタ幅）。
 *    esp-hosted の queue は要素サイズが可変（ポインタより大きいことがある）。
 *    そこで、1 つの「動的キュー」につき
 *      - リングバッファ（qitem_size バイト × HOSTED_QUEUE_MAX_ELEMS 個、ヒープ確保）
 *      - dtq_free   : 空きスロット番号(0..HOSTED_QUEUE_MAX_ELEMS-1)を保持する dtq
 *      - dtq_filled : データ格納済みスロット番号を保持する dtq
 *    の 3 点セットで実現する。
 *      _h_queue_item   : dtq_free からスロット番号を1つ確保(タイムアウト付き) →
 *                        リングバッファへ memcpy → dtq_filled へスロット番号を送信
 *      _h_dequeue_item : dtq_filled からスロット番号を1つ受信(タイムアウト付き) →
 *                        リングバッファから memcpy → dtq_free へスロット番号を返却
 *    この方式なら「in-flight な要素数は常に dtq_free/dtq_filled の合計
 *    (=HOSTED_QUEUE_MAX_ELEMS個)を超えない」ことが保証され、リングバッファの
 *    上書き事故が起きない（dtq 自体の容量とリングバッファの容量が一致している
 *    ため、両者の容量不一致によるオーバーラン問題を回避できる）。
 *
 *  esp-hosted 側が要求する qnum_elem が HOSTED_QUEUE_MAX_ELEMS を超える場合は
 *  クランプする（TODO: 実機で実際の要求サイズを確認し、必要なら
 *  HOSTED_QUEUE_MAX_ELEMS ないし HOSTED_POOL_NUM_QUEUE を調整すること）。
 */
#include "p4hosted_osi.h"
#include <string.h>
#include <stdlib.h>

/*
 *  =====================================================================
 *  cfg 生成 ID のフォールバック前方宣言
 *  =====================================================================
 *  実ビルドでは configure.rb が p4hosted.cfg 中の CRE_TSK/CRE_SEM/...
 *  の名前から kernel_id.h を自動生成し、これらのマクロを本物の ID 定数として
 *  定義する。本ファイル単体の構文チェック（configure.rb を通さない）のためだけに、
 *  未生成時は仮の ID 値を与える。
 *
 *  【重要】プールサイズ（HOSTED_POOL_NUM_*）を変更する場合は、この ID 一覧と
 *  p4hosted.cfg 側の CRE_ 一覧の「両方」を手動で追加/削除し、個数を
 *  一致させること（動的生成ではないため自動追随しない）。
 */
/*
 *  2026-07-10バグ修正(HOSTED_TMR_TSK/DTQ発見に続く全面是正): 本ファイルは
 *  kernel_cfg.h を一切 #include しない設計のため（実機確認済み、上記の
 *  「未生成時は仮のID値を与える」という説明とは異なり、実際には常にこの
 *  フォールバック値がそのままコンパイルされ使用される）、以下の値は
 *  kernel_cfg.h に実際に生成されるIDと一致していなければならない。
 *  従来の値は全面的に不一致だった（旧値はgit履歴参照）。fmp_wifi_scan_probe
 *  で esp_netif_init() 抜きでも "No element in any queue found" →
 *  cmd53失敗という新規の壁に到達した際に発覚。当該.cfg
 *  （p4hosted.cfg、LOGTASK等の外部宣言を含む）で cfg.rb が実際に
 *  割り当てる値に修正する（値が変わればこの.cfgの構造も変える必要あり、
 *  その際はkernel_cfg.hを再確認してこの一覧も追従させること）。
 */
#ifndef HOSTED_TSK1
#define HOSTED_TSK1		2
#define HOSTED_TSK2		3
#define HOSTED_TSK3		4
#define HOSTED_TSK4		5
#define HOSTED_TSK5		6
#define HOSTED_TSK6		7
#define HOSTED_TSK7		8
#define HOSTED_TSK8		9
#endif

#ifndef HOSTED_SEM1
#define HOSTED_SEM1		5
#define HOSTED_SEM2		6
#define HOSTED_SEM3		7
#define HOSTED_SEM4		8
#define HOSTED_SEM5		9
#define HOSTED_SEM6		10
#define HOSTED_SEM7		11
#define HOSTED_SEM8		12
#define HOSTED_SEM9		13
#define HOSTED_SEM10	14
#define HOSTED_SEM11	15
#define HOSTED_SEM12	16
#define HOSTED_SEM13	17
#define HOSTED_SEM14	18
#define HOSTED_SEM15	19
#define HOSTED_SEM16	20
#endif

#ifndef HOSTED_MTX1
#define HOSTED_MTX1		2
#define HOSTED_MTX2		3
#define HOSTED_MTX3		4
#define HOSTED_MTX4		5
#define HOSTED_MTX5		6
#define HOSTED_MTX6		7
#define HOSTED_MTX7		8
#define HOSTED_MTX8		9
#endif

#ifndef HOSTED_POOL_META_MTX
#define HOSTED_POOL_META_MTX	10
#endif

#ifndef HOSTED_DTQ_FREE1
#define HOSTED_DTQ_FREE1	1
#define HOSTED_DTQ_FREE2	3
#define HOSTED_DTQ_FREE3	5
#define HOSTED_DTQ_FREE4	7
#define HOSTED_DTQ_FREE5	9
#define HOSTED_DTQ_FREE6	11
#define HOSTED_DTQ_FREE7	13
#define HOSTED_DTQ_FREE8	15
#define HOSTED_DTQ_FILLED1	2
#define HOSTED_DTQ_FILLED2	4
#define HOSTED_DTQ_FILLED3	6
#define HOSTED_DTQ_FILLED4	8
#define HOSTED_DTQ_FILLED5	10
#define HOSTED_DTQ_FILLED6	12
#define HOSTED_DTQ_FILLED7	14
#define HOSTED_DTQ_FILLED8	16
#define HOSTED_DTQ_FREE9	17
#define HOSTED_DTQ_FREE10	19
#define HOSTED_DTQ_FREE11	21
#define HOSTED_DTQ_FREE12	23
#define HOSTED_DTQ_FILLED9	18
#define HOSTED_DTQ_FILLED10	20
#define HOSTED_DTQ_FILLED11	22
#define HOSTED_DTQ_FILLED12	24
#endif

#ifndef HOSTED_ALM1
#define HOSTED_ALM1		1
#define HOSTED_ALM2		2
#define HOSTED_ALM3		3
#define HOSTED_ALM4		4
#define HOSTED_ALM5		5
#define HOSTED_ALM6		6
#define HOSTED_ALM7		7
#define HOSTED_ALM8		8
#endif

/*
 *  2026-07-10バグ修正: フォールバック値がこの.cfgの実際のcfg.rb割当て
 *  （kernel_cfg.h参照）と食い違っていた（91/92は総オブジェクト数を大幅に
 *  超える範囲外のID）。本ファイルは他の全マクロ同様 kernel_cfg.h を
 *  #include しない（実機で確認: 常にこのフォールバック値が使われる）ため、
 *  実際にcfg.rbが割り当てる値(10/25)に修正する。範囲外IDで rcv_dtq() を
 *  呼ぶと即座にエラー復帰するため、下記 hosted_pool_timer_dispatch_task()
 *  の for(;;){ if(rcv_dtq(...)!=E_OK) continue; } が優先度6のビジーループ
 *  となり、他の全タスク（WIFI_SCAN_PROBE_TASK優先度10等）を完全に飢餓
 *  させていた（JTAGで実機確認: tskctxbが起動直後の状態から一切進行せず）。
 */
#ifndef HOSTED_TMR_TSK
#define HOSTED_TMR_TSK	10	/* タイマコールバックディスパッチ専用タスク */
#endif
#ifndef HOSTED_TMR_DTQ
#define HOSTED_TMR_DTQ	25	/* アラームハンドラ→ディスパッチタスクの通知 dtq */
#endif

/*
 *  =====================================================================
 *  destroy 系の安全化方針（C-1/M-1 対策, 2026-07-16 コードレビュー対応）
 *  =====================================================================
 *  静的プールは「スロット再利用（ABA）」の構造そのものが、destroy 時に
 *  そのオブジェクトへブロック中のタスクが残っていると、再利用後の
 *  create が誤って旧待機者を起床させてしまう危険（C-1）を内包している。
 *  queue が最悪で、待機中タスクの受信バッファサイズと新キューの
 *  qitem_size が食い違うとバッファオーバーランになり得る。
 *
 *  正攻法は timer プール（本ファイル下部）が既に採用している「世代
 *  カウンタ (gen)」方式（HOSTED_TMR_PACK(idx, gen) / used&&gen一致のみ
 *  有効、のパターン）を task/sem/mutex/queue 全プールへ広げることだが、
 *  本対応では見送る（動いているホットパス（特に semaphore destroy は
 *  同期RPC毎に呼ばれる）への大改造になり、C-1 の到達経路自体は現状
 *  デッドコード（esp_hosted_deinit を呼んでいない）なのでリスクに
 *  見合わない）。世代カウンタ方式へ広げる場合は timer プールの実装を
 *  雛形にすること。
 *
 *  代わりに、各 destroy 関数の冒頭で FMP3 の ref_sem/ref_dtq/ref_mtx に
 *  よって待機者（および mutex はロック保持者）の有無を検査し、いれば
 *  「silent なメモリ破壊」ではなく「syslog(LOG_ERROR) を出して destroy を
 *  拒否（スロットは used=true のままリーク）」という loud な失敗に倒す。
 *  ref_* がエラーを返した場合（E_ID 等、通常は起こらないはず）も同様に
 *  安全側（拒否）に倒す。正常系（待機者なし）では従来どおり成功する。
 *
 *  M-1（hosted_pool_thread_cancel の ter_tsk 失敗握り潰し）も同じ考え方
 *  で、ter_tsk が失敗した場合はスロットを used=true のまま維持する
 *  （後述）。生きたタスクが destroy 済みオブジェクトを掴んだまま
 *  放置されると C-1 の引き金になるため。
 *
 *  =====================================================================
 *  タスクプール
 *  =====================================================================
 */
/*
 *  =====================================================================
 *  【改変 D-9（本 repo・段7d）】失敗を見えるようにするための最小の道具立て
 *  =====================================================================
 *  出典は `_h_thread_create` の `act_tsk` の戻り値を捨てていた。
 *  ここでは (1) 計数、(2) **同期出力**（`syslog` ではない。logtask 経由の
 *  非同期出力はハング直前の行を落とすので、実機診断の根拠に使わない
 *  ——本 repo の規律。改変 D-3 と同じ理由）を足す。
 *
 *  `p4sdio_puts` は 7a の `esp/p4sdio/p4sdio_host.c` が提供する同期出力である
 *  （改変 D-3 で `_h_printf` の出力先に選んだものと同じ）。ここでヘッダを
 *  include せず extern 宣言だけ置くのは、出典の「本ファイルは kernel_cfg.h も
 *  他モジュールのヘッダも引かない」という構成を崩さないためである。
 *
 *  **0 でも印字できるように計数は常に公開する**（「数えていない」と
 *  「0 だった」は違う）。
 */
extern void	p4sdio_puts(const char *s);

volatile uint32_t	p4hosted_n_thread_act_ok;		/* act_tsk が E_OK だった回数 */
volatile uint32_t	p4hosted_n_thread_act_fail;		/* act_tsk が失敗した回数 */
volatile uint32_t	p4hosted_n_thread_pool_full;	/* スロット枯渇で NULL を返した回数 */

static void
p4hosted_pools_puts(const char *s)
{
	p4sdio_puts(s);
}

static void
p4hosted_pools_putd(int_t v)
{
	char	buf[12];
	int		i = (int) sizeof(buf) - 1;
	uint32_t u;

	buf[i] = '\0';
	if (v < 0) {
		u = (uint32_t) (-v);
	} else {
		u = (uint32_t) v;
	}
	do {
		buf[--i] = (char) ('0' + (u % 10U));
		u /= 10U;
	} while (u != 0U && i > 1);
	if (v < 0) {
		buf[--i] = '-';
	}
	p4sdio_puts(&buf[i]);
}

typedef struct {
	bool_t	used;				/* 使用中か */
	ID		tskid;				/* 対応する静的 CRE_TSK の ID（固定） */
	void	(*start_routine)(void const *);
	void	*arg;
	PRI		pri_mapped;			/* tprio から写像した FMP3 優先度 */
	char	name[HOSTED_THREAD_NAME_MAX];
} hosted_task_slot_t;

static hosted_task_slot_t g_hosted_task_pool[HOSTED_POOL_NUM_TASK] = {
	{ false, HOSTED_TSK1, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK2, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK3, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK4, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK5, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK6, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK7, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
	{ false, HOSTED_TSK8, NULL, NULL, HOSTED_POOL_TASK_PRI_DEFAULT, { 0 } },
};

static void
hosted_copy_name(char *dst, const char *src)
{
	size_t i;

	for (i = 0; i + 1 < HOSTED_THREAD_NAME_MAX && src != NULL && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static PRI
hosted_pool_map_prio(uint32_t tprio)
{
	/*
	 *  esp-hosted（FreeRTOS流）は「値が大きいほど高優先度」、FMP3 は
	 *  「値が小さいほど高優先度」（TMIN_TPRI=1(最高)〜TMAX_TPRI=16(最低)）。
	 *  esp-hosted 側の実際の tprio 分布を未確認のため、ここでは 0-31 の範囲を
	 *  仮定した単純な線形写像＋反転にとどめる。
	 *  TODO(phase1): 実機で esp-hosted から渡される実際の tprio 値をログ出力し、
	 *  写像の妥当性（極端な偏りが無いか）を確認・調整すること。
	 */
	uint32_t clamped = (tprio > 31U) ? 31U : tprio;
	int32_t  span    = (int32_t) TMAX_TPRI - (int32_t) TMIN_TPRI;
	PRI      pri     = (PRI) ((int32_t) TMAX_TPRI - (int32_t) ((clamped * (uint32_t) span) / 31U));

	if (pri < TMIN_TPRI) {
		pri = TMIN_TPRI;
	}
	if (pri > TMAX_TPRI) {
		pri = TMAX_TPRI;
	}
	return pri;
}

static TMO
hosted_pool_map_timeout_ms(int timeout_ms)
{
	/*
	 *  FMP3 の TMO はマイクロ秒単位（t_stddef.h の TMAX_RELTIM=4000000000U ≒
	 *  66分40秒 ＝ 4,000,000,000us であることから確認済み）。
	 *
	 *  規約（本アダプタで統一する解釈。TODO(phase1/2): esp-hosted の実際の
	 *  呼び出し箇所を確認して検証すること）:
	 *    timeout_ms == 0  : ノンブロッキング（ポーリング）
	 *    timeout_ms <  0  : 永久待ち
	 *    timeout_ms >  0  : ミリ秒指定（ミューテックス/セマフォの
	 *                       upstream リファレンス実装の timeout_ms 命名と
	 *                       同じ解釈）。
	 *  【注意】upstream(FreeRTOS版) の hosted_dequeue_item は正の timeout を
	 *  「秒」として扱っているように見える(SEC_TO_MILLISEC(timeout))箇所があり、
	 *  hosted_queue_item は逆に RTOS tick をそのまま渡している等、queue 系の
	 *  timeout 単位は upstream 内でも一貫していない疑いがある。本アダプタは
	 *  queue/mutex/semaphore の全てで "ミリ秒" に統一しており、esp-hosted の
	 *  実呼び出し値と整合するかは実機で要検証。
	 */
	if (timeout_ms == 0) {
		return TMO_POL;
	} else if (timeout_ms < 0) {
		return TMO_FEVR;
	} else {
		/*  ×1000 変換の uint32 ラップ（timeout_ms > 4,294,967）と
		 *  TMAX_RELTIM 超過（twai_sem/tloc_mtx 等が E_PAR で即時失敗＝
		 *  「長く待つ」つもりが「全く待たない」）を防ぐため、µs 換算で
		 *  TMAX_RELTIM に収まる値へクランプする（2026-07-10 レビュー指摘）。 */
		uint32_t ms = (uint32_t) timeout_ms;

		if (ms > TMAX_RELTIM / 1000U) {
			ms = TMAX_RELTIM / 1000U;
		}
		return (TMO) (ms * 1000U);
	}
}

void
hosted_pool_task_trampoline(EXINF exinf)
{
	uint_t idx = (uint_t) exinf;
	hosted_task_slot_t *slot = &g_hosted_task_pool[idx];
	void (*routine)(void const *);
	void *arg;

	/*
	 *  自タスクの優先度を、_h_thread_create 時に要求された tprio の写像値へ
	 *  変更する。act_tsk 直後に create 側で chg_pri するとタスク起動と
	 *  chg_pri の間にわずかな競合窓が生じる（起動直後に一瞬既定優先度で走る）
	 *  ため、ここ（タスク自身の先頭）で行うことで競合を避けている。
	 */
	(void) chg_pri(TSK_SELF, slot->pri_mapped);

	routine = slot->start_routine;
	arg = slot->arg;

	if (routine != NULL) {
		routine(arg);
	}

	/*
	 *  esp-hosted の tx/rx/control task 等は通常無限ループで戻らないが、
	 *  万一 start_routine が戻ってきた場合はプールへ返却しダーマント状態へ
	 *  戻る（ext_tsk）。ext_tsk 後は次の act_tsk で本トランポリンの先頭から
	 *  再実行される。
	 */
	loc_mtx(HOSTED_POOL_META_MTX);
	slot->used = false;
	unl_mtx(HOSTED_POOL_META_MTX);

	(void) ext_tsk();
	/* ext_tsk は戻らない */
}

void *
hosted_pool_thread_create(const char *tname, uint32_t tprio, uint32_t tstack_size,
		void (*start_routine)(void const *), void *sr_arg)
{
	int i;
	hosted_task_slot_t *slot = NULL;

	if (start_routine == NULL) {
		return NULL;
	}

	/*
	 *  tstack_size は静的 CRE_TSK のスタック領域（HOSTED_POOL_TASK_STACK_SIZE，
	 *  cfg で固定）に対して無視する。要求サイズがこれを超える場合にスタックが
	 *  不足する恐れがあるため、TODO(phase1): esp-hosted の各スレッドの要求
	 *  スタックサイズを実機ログで確認し、HOSTED_POOL_TASK_STACK_SIZE を
	 *  十分な値に調整すること。
	 */
	(void) tstack_size;

	loc_mtx(HOSTED_POOL_META_MTX);
	for (i = 0; i < HOSTED_POOL_NUM_TASK; i++) {
		if (!g_hosted_task_pool[i].used) {
			g_hosted_task_pool[i].used = true;
			slot = &g_hosted_task_pool[i];
			slot->start_routine = start_routine;
			slot->arg = sr_arg;
			slot->pri_mapped = hosted_pool_map_prio(tprio);
			hosted_copy_name(slot->name, tname);
			break;
		}
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	if (slot == NULL) {
		/* プール枯渇。HOSTED_POOL_NUM_TASK を調整するか、呼び出し元の
		 * スレッド生成数を見直すこと(TODO)。 */
		/*  改変 D-9（本 repo・段7d）: 枯渇も**黙って NULL** ではなく数える。 */
		p4hosted_n_thread_pool_full++;
		p4hosted_pools_puts("hosted_pool: D-9 thread_create: プール枯渇（NULL を返す）\n");
		return NULL;
	}

	/*
	 *  【改変 D-9（本 repo・段7d）】出典は `(void) act_tsk(slot->tskid);` と
	 *  **戻り値を捨てていた**。段 7c で「スロットは返るがスレッドが走らない」
	 *  現象に当たったとき、外から失敗が見えず原因の切り分けに時間を要した。
	 *
	 *  【正直に書く】この改変は **7c の現象の原因ではなかった**。
	 *  真の原因は「`HOSTED_TSK*` が cfg 生成値ではなくフォールバック値で
	 *  コンパイルされていて、**隣のタスク**を起こしていた」ことであり、
	 *  そのとき `act_tsk` は（実在する DORMANT のタスクを起こすので）
	 *  **`E_OK` を返す**。つまり戻り値を見ても捕まらなかった。
	 *  根治は CMake 側の `-include kernel_cfg.h`（`cmake/a1_p4_stage1.cmake`）＋
	 *  実測監査 `cmake/a1_p4_hosted_cfgid_audit.sh` である。
	 *
	 *  それでもここを直すのは、**別の失敗**（プールの ID が範囲外＝`E_ID`、
	 *  既に起動中＝`E_QOVR` 等）が起きたときに、
	 *  「起きなかった」ことに気づけない構造を残さないためである。
	 *  失敗したらスロットを返却して **NULL を返す**（＝呼出し側の契約どおり
	 *  「作れなかった」と伝える。旧実装は非 NULL を返すので、呼出し側は
	 *  作れたと信じて先へ進んだ）。
	 */
	{
		ER	ercd = act_tsk(slot->tskid);

		if (ercd != E_OK) {
			p4hosted_n_thread_act_fail++;
			p4hosted_pools_puts("hosted_pool: D-9 act_tsk failed tskid=");
			p4hosted_pools_putd((int_t) slot->tskid);
			p4hosted_pools_puts(" ercd=");
			p4hosted_pools_putd((int_t) ercd);
			p4hosted_pools_puts(" (スレッドは走らない。NULL を返す)\n");

			loc_mtx(HOSTED_POOL_META_MTX);
			slot->used = false;
			slot->start_routine = NULL;
			slot->arg = NULL;
			unl_mtx(HOSTED_POOL_META_MTX);
			return NULL;
		}
		p4hosted_n_thread_act_ok++;
	}

	return (void *) slot;
}

int
hosted_pool_thread_cancel(void *thread_handle)
{
	hosted_task_slot_t *slot = (hosted_task_slot_t *) thread_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  【2026-07-10 修正】used の検査とプール返却を meta ミューテックス下で
	 *  ter_tsk と同一クリティカルセクションにまとめる。旧実装は used を
	 *  見ずに ter_tsk していたため、start_routine が自然終了して slot が
	 *  返却→再払い出しされた後に古いハンドルで cancel されると、新しい
	 *  利用者のタスクを誤って強制終了する ABA バグがあった（レビュー指摘）。
	 *  used=false（終了済み/未使用）の古いハンドルは何もせず成功扱いで返す。
	 *
	 *  【2026-07-16 M-1 修正】ter_tsk の戻り値を見ずに used=false へ倒す
	 *  実装だったため、ter_tsk が失敗（対象タスクが実際には終了していない）
	 *  した場合でもスロットがプールへ返却されてしまっていた。これを
	 *  再利用すると (a) 新しい論理スレッドの start_routine が一度も起動
	 *  されない「ゴースト・スレッド」になり、かつ (b) 生きたままの旧タスクが
	 *  destroy 済みの queue/sem/mutex を掴み続け、それが再利用されたときに
	 *  C-1（待機者/所有者の誤配線）の引き金になる。そのため ter_tsk 失敗時は
	 *  used=true のまま維持してスロットをリークさせる（安全側）。
	 *
	 *  TODO(重要・phase1): FMP3 の ter_tsk はタスクを強制終了するサービスコール
	 *  であり、対象タスクがロックを保持中に呼ぶとシステムが不整合になり得る
	 *  （itron系カーネル一般の注意点。この点は本修正でも未解決）。esp-hosted の
	 *  スレッドは通常無限ループで自発的に終了しないため、_h_thread_cancel が
	 *  実際に呼ばれる頻度は低いと想定されるが、将来的には「協調的な終了
	 *  フラグ＋タスク自身の ext_tsk」方式へ置き換えることを検討すること。
	 */
	loc_mtx(HOSTED_POOL_META_MTX);
	if (!slot->used) {
		unl_mtx(HOSTED_POOL_META_MTX);
		return RET_OK;
	}
	ercd = ter_tsk(slot->tskid);
	if (ercd == E_OK) {
		slot->used = false;
	} else {
		syslog(LOG_ERROR,
				"hosted_pool: M-1 ter_tsk failed (tskid=%d ercd=%d); "
				"leaking slot instead of reuse",
				(int_t) slot->tskid, (int_t) ercd);
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

/*
 *  =====================================================================
 *  セマフォプール
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		semid;
	int		max_count;			/* _h_create_semaphore の要求 maxCount */
} hosted_sem_slot_t;

static hosted_sem_slot_t g_hosted_sem_pool[HOSTED_POOL_NUM_SEM] = {
	{ false, HOSTED_SEM1, 0 },  { false, HOSTED_SEM2, 0 },  { false, HOSTED_SEM3, 0 },
	{ false, HOSTED_SEM4, 0 },  { false, HOSTED_SEM5, 0 },  { false, HOSTED_SEM6, 0 },
	{ false, HOSTED_SEM7, 0 },  { false, HOSTED_SEM8, 0 },  { false, HOSTED_SEM9, 0 },
	{ false, HOSTED_SEM10, 0 }, { false, HOSTED_SEM11, 0 }, { false, HOSTED_SEM12, 0 },
	{ false, HOSTED_SEM13, 0 }, { false, HOSTED_SEM14, 0 }, { false, HOSTED_SEM15, 0 },
	{ false, HOSTED_SEM16, 0 },
};

void *
hosted_pool_create_semaphore(int maxCount)
{
	int i;
	hosted_sem_slot_t *slot = NULL;

	/*
	 *  静的 CRE_SEM の maxsem(=HOSTED_SEM_STATIC_MAXCNT) を超える要求は
	 *  静的上限へクランプする。それ以下の要求（典型: maxCount=1 の
	 *  バイナリセマフォ）は slot->max_count に記録し、post 側で飽和を
	 *  エミュレートする（旧実装はコメントで「クランプ」と言いつつ何も
	 *  しておらず、バイナリセマフォが上限1024のカウンティングとして
	 *  振る舞っていた。2026-07-10 レビュー指摘）。
	 */
	if (maxCount <= 0 || maxCount > HOSTED_SEM_STATIC_MAXCNT) {
		maxCount = HOSTED_SEM_STATIC_MAXCNT;
	}

	loc_mtx(HOSTED_POOL_META_MTX);
	for (i = 0; i < HOSTED_POOL_NUM_SEM; i++) {
		if (!g_hosted_sem_pool[i].used) {
			g_hosted_sem_pool[i].used = true;
			g_hosted_sem_pool[i].max_count = maxCount;
			slot = &g_hosted_sem_pool[i];
			break;
		}
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	if (slot == NULL) {
		return NULL;
	}

	/*
	 *  upstream(FreeRTOS版参照実装)は生成直後に1回シグナルしてカウントを1に
	 *  している（"生成直後は利用可能" を期待する呼び出し側がある）。
	 *  プール返却時(destroy)に資源数を0まで空読みしているため、ここでの
	 *  sig_sem は常にカウント0→1になる想定。
	 */
	(void) sig_sem(slot->semid);

	return (void *) slot;
}

int
hosted_pool_destroy_semaphore(void *semaphore_handle)
{
	hosted_sem_slot_t *slot = (hosted_sem_slot_t *) semaphore_handle;
	T_RSEM rsem;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  C-1 対策: twai_sem でブロック中のタスク（待ち行列の先頭 = wtskid）が
	 *  いる状態で destroy すると、後続の create が同じスロットを再取得した
	 *  際に旧待機者が新しいセマフォへ誤って起床させられる恐れがある。
	 *  ref_sem() は1回だけ呼ぶ（この関数はホットパス:
	 *  rpc_core.c の set_sync_resp_sem() から同期RPCリクエスト毎に呼ばれる）。
	 *  正常系（待機者なし）では ercd==E_OK && wtskid==TSK_NONE となり、
	 *  従来どおり drain して即成功する。
	 *  ref_sem がエラーを返した場合（E_ID 等、通常は起こらないはず）も、
	 *  待機者の有無を確認できない以上、安全側に倒して destroy を拒否する。
	 */
	ercd = ref_sem(slot->semid, &rsem);
	if (ercd != E_OK || rsem.wtskid != TSK_NONE) {
		syslog(LOG_ERROR,
				"hosted_pool: C-1 destroy_semaphore rejected "
				"(semid=%d ercd=%d wtskid=%d)",
				(int_t) slot->semid, (int_t) ercd,
				(ercd == E_OK) ? (int_t) rsem.wtskid : (int_t) 0);
		return RET_FAIL;
	}

	/* 次回利用者に前回の残数を持ち越さないよう、0まで空読みする。 */
	while (pol_sem(slot->semid) == E_OK) {
		/* drain */
	}

	loc_mtx(HOSTED_POOL_META_MTX);
	slot->used = false;
	unl_mtx(HOSTED_POOL_META_MTX);

	return RET_OK;
}

int
hosted_pool_post_semaphore(void *semaphore_handle)
{
	hosted_sem_slot_t *slot = (hosted_sem_slot_t *) semaphore_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  _h_post_semaphore_from_isr もこの関数を共用する（p4hosted_osi.c 参照）。
	 *  FMP3 の kernel.h は `#define isig_sem(semid) sig_sem(semid)` としており、
	 *  非タスク文脈(割込み等)専用の別実装を持たない。つまりタスク文脈/非タスク
	 *  文脈で sig_sem の呼び分けが不要という前提（本移植で確認済み）。
	 *
	 *  maxCount の飽和エミュレーション: 要求 maxCount が静的 maxsem より
	 *  小さい場合（典型: バイナリセマフォ）、FreeRTOS 同様に上限で飽和させる
	 *  （超過 post は成功扱いで無視）。ref_sem→sig_sem は非アトミックだが、
	 *  同時 post の競合で高々数カウント超過し得るのみで、上限1024まで
	 *  蓄積し得た旧実装より大幅に安全側。ref_sem はタスク文脈専用のため、
	 *  非タスク文脈からの post では飽和チェックを行わない（安全側）。
	 */
	if (slot->max_count < HOSTED_SEM_STATIC_MAXCNT && !sns_ctx()) {
		T_RSEM rsem;

		if (ref_sem(slot->semid, &rsem) == E_OK
				&& rsem.semcnt >= (uint_t) slot->max_count) {
			return RET_OK;		/* 飽和: FreeRTOS の xSemaphoreGive 同様に上限維持 */
		}
	}
	ercd = sig_sem(slot->semid);

	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

int
hosted_pool_get_semaphore(void *semaphore_handle, int timeout_ms)
{
	hosted_sem_slot_t *slot = (hosted_sem_slot_t *) semaphore_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	ercd = twai_sem(slot->semid, hosted_pool_map_timeout_ms(timeout_ms));
	if (ercd == E_OK) {
		return RET_OK;
	} else if (ercd == E_TMOUT) {
		return RET_FAIL_TIMEOUT;
	}
	return RET_FAIL;
}

/*
 *  =====================================================================
 *  ミューテックスプール
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		mtxid;
} hosted_mutex_slot_t;

static hosted_mutex_slot_t g_hosted_mutex_pool[HOSTED_POOL_NUM_MUTEX] = {
	{ false, HOSTED_MTX1 }, { false, HOSTED_MTX2 }, { false, HOSTED_MTX3 },
	{ false, HOSTED_MTX4 }, { false, HOSTED_MTX5 }, { false, HOSTED_MTX6 },
	{ false, HOSTED_MTX7 }, { false, HOSTED_MTX8 },
};

void *
hosted_pool_create_mutex(void)
{
	int i;
	hosted_mutex_slot_t *slot = NULL;

	loc_mtx(HOSTED_POOL_META_MTX);
	for (i = 0; i < HOSTED_POOL_NUM_MUTEX; i++) {
		if (!g_hosted_mutex_pool[i].used) {
			g_hosted_mutex_pool[i].used = true;
			slot = &g_hosted_mutex_pool[i];
			break;
		}
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	return (void *) slot;
}

int
hosted_pool_destroy_mutex(void *mutex_handle)
{
	hosted_mutex_slot_t *slot = (hosted_mutex_slot_t *) mutex_handle;
	T_RMTX rmtx;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  C-1 対策: ロック保持中（htskid != TSK_NONE）または tloc_mtx で
	 *  ブロック中のタスクがいる（wtskid != TSK_NONE）状態で destroy すると、
	 *  同じスロットの再利用時に旧保持者/旧待機者が新しいミューテックスへ
	 *  誤って絡む恐れがある（queue/semaphore と同型のリスク）。
	 *  旧実装は「呼び出し元が自分でロックしたまま destroy した」ケースを
	 *  想定し防御的に unl_mtx() して黙って解放していたが、それは
	 *  「保持中の破棄」を隠蔽するだけで C-1 と同じ危険を抱えていたため廃止し、
	 *  loud な拒否に変える。
	 *  ref_mtx がエラーを返した場合（E_ID 等）も安全側に倒して拒否する。
	 */
	ercd = ref_mtx(slot->mtxid, &rmtx);
	if (ercd != E_OK || rmtx.htskid != TSK_NONE || rmtx.wtskid != TSK_NONE) {
		syslog(LOG_ERROR,
				"hosted_pool: C-1 destroy_mutex rejected "
				"(mtxid=%d ercd=%d htskid=%d wtskid=%d)",
				(int_t) slot->mtxid, (int_t) ercd,
				(ercd == E_OK) ? (int_t) rmtx.htskid : (int_t) 0,
				(ercd == E_OK) ? (int_t) rmtx.wtskid : (int_t) 0);
		return RET_FAIL;
	}

	loc_mtx(HOSTED_POOL_META_MTX);
	slot->used = false;
	unl_mtx(HOSTED_POOL_META_MTX);

	return RET_OK;
}

int
hosted_pool_lock_mutex(void *mutex_handle, int timeout_ms)
{
	hosted_mutex_slot_t *slot = (hosted_mutex_slot_t *) mutex_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	ercd = tloc_mtx(slot->mtxid, hosted_pool_map_timeout_ms(timeout_ms));
	if (ercd == E_OK) {
		return RET_OK;
	} else if (ercd == E_TMOUT) {
		return RET_FAIL_TIMEOUT;
	}
	return RET_FAIL;
}

int
hosted_pool_unlock_mutex(void *mutex_handle)
{
	hosted_mutex_slot_t *slot = (hosted_mutex_slot_t *) mutex_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	ercd = unl_mtx(slot->mtxid);
	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

/*
 *  =====================================================================
 *  データキュープール
 *  =====================================================================
 */
typedef struct {
	bool_t		used;
	ID			dtq_free;		/* 空きスロット番号を保持する dtq */
	ID			dtq_filled;		/* 格納済みスロット番号を保持する dtq */
	uint32_t	qnum_elem;		/* 要求要素数(<=HOSTED_QUEUE_MAX_ELEMS にクランプ済み) */
	uint32_t	qitem_size;		/* 1要素のバイト数 */
	uint8_t		*ring;			/* qitem_size * HOSTED_QUEUE_MAX_ELEMS のヒープ領域 */
} hosted_queue_slot_t;

static hosted_queue_slot_t g_hosted_queue_pool[HOSTED_POOL_NUM_QUEUE] = {
	{ false, HOSTED_DTQ_FREE1, HOSTED_DTQ_FILLED1, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE2, HOSTED_DTQ_FILLED2, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE3, HOSTED_DTQ_FILLED3, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE4, HOSTED_DTQ_FILLED4, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE5, HOSTED_DTQ_FILLED5, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE6, HOSTED_DTQ_FILLED6, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE7, HOSTED_DTQ_FILLED7, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE8, HOSTED_DTQ_FILLED8, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE9, HOSTED_DTQ_FILLED9, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE10, HOSTED_DTQ_FILLED10, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE11, HOSTED_DTQ_FILLED11, 0, 0, NULL },
	{ false, HOSTED_DTQ_FREE12, HOSTED_DTQ_FILLED12, 0, 0, NULL },
};

void *
hosted_pool_create_queue(uint32_t qnum_elem, uint32_t qitem_size)
{
	int i;
	hosted_queue_slot_t *slot = NULL;
	uint32_t elems;
	uint8_t *ring;
	intptr_t idx;

	if (qitem_size == 0) {
		return NULL;
	}

	elems = qnum_elem;
	if (elems == 0) {
		elems = 1;
	}
	if (elems > HOSTED_QUEUE_MAX_ELEMS) {
		/* HOSTED_QUEUE_MAX_ELEMS へクランプする。TODO(phase1): 実際の
		 * qnum_elem 要求を確認し、必要なら HOSTED_QUEUE_MAX_ELEMS を
		 * 引き上げること。 */
		elems = HOSTED_QUEUE_MAX_ELEMS;
	}

	/* リングバッファは常に HOSTED_QUEUE_MAX_ELEMS 個ぶん確保する
	 * （dtq_free/dtq_filled の容量(=HOSTED_QUEUE_MAX_ELEMS)と一致させ、
	 * 容量不一致によるオーバーラン事故を避けるため）。 */
	ring = (uint8_t *) malloc((size_t) qitem_size * HOSTED_QUEUE_MAX_ELEMS);
	if (ring == NULL) {
		return NULL;
	}

	loc_mtx(HOSTED_POOL_META_MTX);
	for (i = 0; i < HOSTED_POOL_NUM_QUEUE; i++) {
		if (!g_hosted_queue_pool[i].used) {
			g_hosted_queue_pool[i].used = true;
			slot = &g_hosted_queue_pool[i];
			slot->qnum_elem = elems;
			slot->qitem_size = qitem_size;
			slot->ring = ring;
			break;
		}
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	if (slot == NULL) {
		free(ring);
		return NULL;
	}

	/* dtq_free を elems（要求要素数）個の空きスロット番号で満たす。
	 * 生成直後の dtq は空(sdtqcnt=0)のはずなので、tsnd_dtq はポーリングで
	 * 即座に成功するはず。
	 * 【2026-07-10 修正】旧実装は常に HOSTED_QUEUE_MAX_ELEMS(32) 個を投入して
	 * おり、qnum_elem=5 で作られたキューも 32 要素まで受け付けていた。
	 * FreeRTOS では「キュー満杯」がフロー制御（バックプレッシャ）として
	 * 機能する箇所で閾値が 6 倍以上緩み、メモリ滞留・遅延増の原因になる
	 * （レビュー指摘）。リングバッファ自体は MAX_ELEMS 分確保したままなので
	 * 安全側は不変。 */
	for (idx = 0; idx < (intptr_t) slot->qnum_elem; idx++) {
		ER ercd = tsnd_dtq(slot->dtq_free, idx, TMO_POL);
		if (ercd != E_OK) {
			/* 想定外の内部エラー。ここまでに投入した分は次回 destroy で
			 * drain されるので致命的ではないが、ログに残す価値がある
			 * (TODO: syslog 連携)。 */
			break;
		}
	}

	return (void *) slot;
}

int
hosted_pool_destroy_queue(void *queue_handle)
{
	hosted_queue_slot_t *slot = (hosted_queue_slot_t *) queue_handle;
	intptr_t dummy;
	T_RDTQ rdtq_free, rdtq_filled;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  C-1 対策（本ファイル中で最悪のシナリオ）: dtq_free/dtq_filled の
	 *  いずれかに送信待ち(stskid)/受信待ち(rtskid)のタスクが残っている
	 *  状態で destroy すると、slot->ring が free() された後に別スレッドが
	 *  同じスロットを異なる qitem_size で create し、そこへ queue_item()
	 *  すると、ブロック中だった旧タスクの trcv_dtq がそのまま起床する。
	 *  旧タスクは新しい(大きい) qitem_size で memcpy するため、旧(小さい)
	 *  サイズの受信バッファへオーバーランし得る（レビュー指摘の core
	 *  シナリオ）。そのため両方の dtq に待機者がいないことを確認できるまで
	 *  destroy を拒否する。
	 *  ref_dtq がエラーを返した場合（E_ID 等）も安全側に倒して拒否する。
	 */
	ercd = ref_dtq(slot->dtq_free, &rdtq_free);
	if (ercd != E_OK || rdtq_free.stskid != TSK_NONE || rdtq_free.rtskid != TSK_NONE) {
		syslog(LOG_ERROR,
				"hosted_pool: C-1 destroy_queue rejected "
				"(dtq_free=%d ercd=%d stskid=%d rtskid=%d)",
				(int_t) slot->dtq_free, (int_t) ercd,
				(ercd == E_OK) ? (int_t) rdtq_free.stskid : (int_t) 0,
				(ercd == E_OK) ? (int_t) rdtq_free.rtskid : (int_t) 0);
		return RET_FAIL;
	}
	ercd = ref_dtq(slot->dtq_filled, &rdtq_filled);
	if (ercd != E_OK || rdtq_filled.stskid != TSK_NONE || rdtq_filled.rtskid != TSK_NONE) {
		syslog(LOG_ERROR,
				"hosted_pool: C-1 destroy_queue rejected "
				"(dtq_filled=%d ercd=%d stskid=%d rtskid=%d)",
				(int_t) slot->dtq_filled, (int_t) ercd,
				(ercd == E_OK) ? (int_t) rdtq_filled.stskid : (int_t) 0,
				(ercd == E_OK) ? (int_t) rdtq_filled.rtskid : (int_t) 0);
		return RET_FAIL;
	}

	/* dtq_filled/dtq_free を完全に空にしてから返却する
	 * (次回利用時に前回の残留スロット番号が混ざらないようにするため)。 */
	while (trcv_dtq(slot->dtq_filled, &dummy, TMO_POL) == E_OK) {
		/* drain */
	}
	while (trcv_dtq(slot->dtq_free, &dummy, TMO_POL) == E_OK) {
		/* drain */
	}

	free(slot->ring);

	loc_mtx(HOSTED_POOL_META_MTX);
	slot->ring = NULL;
	slot->qnum_elem = 0;
	slot->qitem_size = 0;
	slot->used = false;
	unl_mtx(HOSTED_POOL_META_MTX);

	return RET_OK;
}

int
hosted_pool_queue_item(void *queue_handle, void *item, int timeout)
{
	hosted_queue_slot_t *slot = (hosted_queue_slot_t *) queue_handle;
	intptr_t idx;
	ER ercd;

	if (slot == NULL || item == NULL) {
		return RET_INVALID;
	}

	/* 空きスロットを1つ確保する。 */
	ercd = trcv_dtq(slot->dtq_free, &idx, hosted_pool_map_timeout_ms(timeout));
	if (ercd != E_OK) {
		return (ercd == E_TMOUT) ? RET_FAIL_TIMEOUT : RET_FAIL;
	}

	memcpy(slot->ring + (size_t) idx * slot->qitem_size, item, slot->qitem_size);

	/* 空きスロット確保で容量は保証済みなので、ここは即座に成功するはず。 */
	ercd = tsnd_dtq(slot->dtq_filled, idx, TMO_POL);
	if (ercd != E_OK) {
		/* 想定外。確保したスロットを空きへ戻して整合性を保つ。 */
		(void) tsnd_dtq(slot->dtq_free, idx, TMO_POL);
		return RET_FAIL;
	}

	return RET_OK;
}

int
hosted_pool_dequeue_item(void *queue_handle, void *item, int timeout)
{
	hosted_queue_slot_t *slot = (hosted_queue_slot_t *) queue_handle;
	intptr_t idx;
	ER ercd;

	if (slot == NULL || item == NULL) {
		return RET_INVALID;
	}

	ercd = trcv_dtq(slot->dtq_filled, &idx, hosted_pool_map_timeout_ms(timeout));
	if (ercd != E_OK) {
		return (ercd == E_TMOUT) ? RET_FAIL_TIMEOUT : RET_FAIL;
	}

	memcpy(item, slot->ring + (size_t) idx * slot->qitem_size, slot->qitem_size);

	(void) tsnd_dtq(slot->dtq_free, idx, TMO_POL);

	return RET_OK;
}

int
hosted_pool_queue_msg_waiting(void *queue_handle)
{
	hosted_queue_slot_t *slot = (hosted_queue_slot_t *) queue_handle;
	T_RDTQ rdtq;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	ercd = ref_dtq(slot->dtq_filled, &rdtq);
	if (ercd != E_OK) {
		return RET_FAIL;
	}

	return (int) rdtq.sdtqcnt;
}

int
hosted_pool_reset_queue(void *queue_handle)
{
	hosted_queue_slot_t *slot = (hosted_queue_slot_t *) queue_handle;
	intptr_t idx;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/* dtq_filled に溜まっている分を全て dtq_free へ戻す（＝キューを空にする）。 */
	while (trcv_dtq(slot->dtq_filled, &idx, TMO_POL) == E_OK) {
		(void) tsnd_dtq(slot->dtq_free, idx, TMO_POL);
	}

	return RET_OK;
}

/*
 *  =====================================================================
 *  タイマ(アラーム)プール
 *  =====================================================================
 *  【2026-07-10 全面改修（コードレビュー指摘への対応）】
 *  旧実装はアラームハンドラ（FMP3 の非タスク文脈）からコールバックを直接
 *  呼んでいたが、upstream(FreeRTOS/esp_timer) ではタイマコールバックは
 *  タスク文脈で走る契約であり、esp-hosted のコールバックはミューテックス
 *  取得・キュー送受信・セマフォ待ち等のタスク文脈専用 API を呼び得る。
 *  非タスク文脈からでは tloc_mtx/twai_sem/trcv_dtq 等が E_CTX で全滅し、
 *  さらにコールバックが _h_timer_start/_h_create_* を呼ぶと
 *  loc_mtx(HOSTED_POOL_META_MTX) が E_CTX で失敗したまま（戻り値未チェック）
 *  プールの used ビットマップを排他なしで触る事故に化ける。
 *
 *  対策: アラームハンドラは「スロット添字＋世代カウンタ」を専用 dtq
 *  （HOSTED_TMR_DTQ）へ ipsnd_dtq するだけにし、コールバック本体は専用の
 *  ディスパッチタスク（HOSTED_TMR_TSK, hosted_pool_timer_dispatch_task）が
 *  タスク文脈で呼ぶ。世代カウンタ(gen)は stop 後に飛行中の通知が届いた場合や
 *  スロット再利用後に旧通知が届いた場合（ABA）に、それを無効として棄却する
 *  ためのもの（stop/start のたびに ++）。
 *
 *  【前提】アラームハンドラとプールタスク群は cfg で同一 PE（CLS_PRC1）に
 *  固定されているため、ハンドラはタスクをプリエンプトする形でのみ走り、
 *  ハンドラからの slot フィールド読出し（used/gen/periodic/period_us）は
 *  タスク側の meta ミューテックス下の更新と PE 内で直列化される。
 */
typedef struct {
	bool_t		used;
	ID			almid;
	void		(*timeout_handler)(void *);
	void		*arg;
	uint32_t	period_us;
	bool_t		periodic;
	uint32_t	gen;			/* 世代カウンタ（start/stop ごとに ++） */
} hosted_timer_slot_t;

static hosted_timer_slot_t g_hosted_timer_pool[HOSTED_POOL_NUM_TIMER] = {
	{ false, HOSTED_ALM1, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM2, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM3, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM4, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM5, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM6, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM7, NULL, NULL, 0, false, 0 },
	{ false, HOSTED_ALM8, NULL, NULL, 0, false, 0 },
};

/*  dtq メッセージのパック形式: 下位8bit=スロット添字, 上位=世代カウンタ  */
#define HOSTED_TMR_PACK(idx, gen)	((intptr_t) ((((uintptr_t) (gen)) << 8) | ((uintptr_t) (idx) & 0xFFU)))
#define HOSTED_TMR_UNPACK_IDX(m)	((uint_t) ((uintptr_t) (m) & 0xFFU))
#define HOSTED_TMR_UNPACK_GEN(m)	((uint32_t) (((uintptr_t) (m)) >> 8))

void
hosted_pool_alarm_trampoline(EXINF exinf)
{
	uint_t idx = (uint_t) exinf;
	hosted_timer_slot_t *slot = &g_hosted_timer_pool[idx];

	if (!slot->used || slot->timeout_handler == NULL) {
		return;
	}

	/*
	 *  コールバック本体は呼ばず、ディスパッチタスクへ通知するだけ。
	 *  dtq 満杯（ディスパッチタスクが長時間走れていない異常時）は通知を
	 *  落とす（非タスク文脈からブロックはできない。周期タイマなら次周期で
	 *  回復する）。
	 */
	(void) ipsnd_dtq(HOSTED_TMR_DTQ, HOSTED_TMR_PACK(idx, slot->gen));

	if (slot->periodic) {
		/*
		 *  満了ハンドラ内で再武装する（設計方針どおり）。アラームハンドラの
		 *  実行文脈は非タスク文脈相当のため、i 系サービスコールの別名
		 *  (ista_alm。kernel.h では sta_alm の単純なマクロ別名)を用いる。
		 */
		(void) ista_alm(slot->almid, (RELTIM) slot->period_us);
	}
}

/*
 *  タイマコールバックディスパッチタスク（cfg の CRE_TSK(HOSTED_TMR_TSK) が
 *  TA_ACT で生成・起動する）。通知を受けるたびに、meta ミューテックス下で
 *  スロットの有効性（used かつ世代一致）を再確認してからコールバックを
 *  タスク文脈で呼ぶ。検証→呼出しの間にも理論上の窓は残るが、
 *  (a) コールバックがタスク文脈で走ること、(b) stop/再利用後の旧通知が
 *  世代不一致で棄却されること、の2点で旧実装の実害（E_CTX 全滅・停止後
 *  誤発火・再利用スロットへの誤発火）は解消される。
 */
void
hosted_pool_timer_dispatch_task(EXINF exinf)
{
	intptr_t msg;
	uint_t idx;
	uint32_t gen;
	hosted_timer_slot_t *slot;
	void (*handler)(void *);
	void *arg;

	(void) exinf;

	for (;;) {
		if (rcv_dtq(HOSTED_TMR_DTQ, &msg) != E_OK) {
			continue;
		}
		idx = HOSTED_TMR_UNPACK_IDX(msg);
		gen = HOSTED_TMR_UNPACK_GEN(msg);
		if (idx >= (uint_t) HOSTED_POOL_NUM_TIMER) {
			continue;
		}
		slot = &g_hosted_timer_pool[idx];

		handler = NULL;
		arg = NULL;
		loc_mtx(HOSTED_POOL_META_MTX);
		if (slot->used && slot->gen == gen) {
			handler = slot->timeout_handler;
			arg = slot->arg;
		}
		unl_mtx(HOSTED_POOL_META_MTX);

		if (handler != NULL) {
			handler(arg);
		}
	}
}

void *
hosted_pool_timer_start(const char *name, int duration_ms, int type,
		void (*timeout_handler)(void *), void *arg)
{
	int i;
	hosted_timer_slot_t *slot = NULL;
	uint32_t period_ms;

	if (timeout_handler == NULL || duration_ms <= 0) {
		return NULL;
	}

	/*  ×1000 変換のラップ/TMAX_RELTIM 超過を防ぐクランプ
	 *  （hosted_pool_map_timeout_ms と同じ理由） */
	period_ms = (uint32_t) duration_ms;
	if (period_ms > TMAX_RELTIM / 1000U) {
		period_ms = TMAX_RELTIM / 1000U;
	}

	loc_mtx(HOSTED_POOL_META_MTX);
	for (i = 0; i < HOSTED_POOL_NUM_TIMER; i++) {
		if (!g_hosted_timer_pool[i].used) {
			g_hosted_timer_pool[i].used = true;
			slot = &g_hosted_timer_pool[i];
			slot->timeout_handler = timeout_handler;
			slot->arg = arg;
			slot->periodic = (type == H_TIMER_TYPE_PERIODIC);
			slot->period_us = period_ms * 1000U;
			slot->gen++;		/* 新しい貸出し世代（旧通知の棄却用） */
			break;
		}
	}
	unl_mtx(HOSTED_POOL_META_MTX);

	if (slot == NULL) {
		return NULL;
	}

	(void) name; /* デバッグ名は現状未使用。TODO: syslog へ出す等 */

	(void) sta_alm(slot->almid, (RELTIM) slot->period_us);

	return (void *) slot;
}

int
hosted_pool_timer_stop(void *timer_handle)
{
	hosted_timer_slot_t *slot = (hosted_timer_slot_t *) timer_handle;
	ER ercd;

	if (slot == NULL) {
		return RET_INVALID;
	}

	/*
	 *  先に meta ミューテックス下で「論理的に停止」させてから stp_alm する。
	 *  順序が重要: used=false と gen++ を先に立てておけば、
	 *    - この後にアラームハンドラが走っても used=false で即 return
	 *      （再武装もしない）
	 *    - 既に dtq へ投入済みの飛行中通知はディスパッチタスクが世代不一致で
	 *      棄却する
	 *  ため、「stop 後の誤発火」「再利用スロットへの旧周期での誤発火」の
	 *  両方が防がれる（旧実装のレビュー指摘）。
	 */
	loc_mtx(HOSTED_POOL_META_MTX);
	if (!slot->used) {
		unl_mtx(HOSTED_POOL_META_MTX);
		return RET_OK;			/* 二重 stop は成功扱い */
	}
	slot->used = false;
	slot->gen++;
	slot->periodic = false;
	slot->timeout_handler = NULL;
	slot->arg = NULL;
	unl_mtx(HOSTED_POOL_META_MTX);

	ercd = stp_alm(slot->almid);

	return (ercd == E_OK) ? RET_OK : RET_FAIL;
}

#ifdef H_USE_MEMPOOL
/*
 *  =====================================================================
 *  mempool ロック（既定では H_USE_MEMPOOL 未定義のため vtable には組み込まれない。
 *  フェーズ2以降で esp-hosted の nw_mp（ネットワークバッファ用メモリプール）を
 *  使う構成にする場合に有効化する想定）
 *  =====================================================================
 *  esp-hosted の mempool ロックは通常ごく短いクリティカルセクション
 *  （固定長バッファの free-list 操作）1本を保護できれば足りるため、専用の
 *  ミューテックスを1本だけ用意し、_h_create_lock_mempool() は常に同じ
 *  ハンドル（このモジュール内の静的変数のアドレス）を返す。
 *  TODO(phase2): esp-hosted が複数個の独立した mempool ロックを要求する場合は、
 *  他プール同様の「静的プール払い出し」方式へ拡張すること。
 */
/*  2026-07-10バグ修正: 71は範囲外だった。実際のcfg.rb割当て値(11)に修正。 */
#ifndef HOSTED_MEMPOOL_LOCK_MTX
#define HOSTED_MEMPOOL_LOCK_MTX		11
#endif

static int g_hosted_mempool_lock_token;

void *
hosted_pool_create_lock_mempool(void)
{
	return (void *) &g_hosted_mempool_lock_token;
}

void
hosted_pool_lock_mempool(void *lock_handle)
{
	(void) lock_handle;
	loc_mtx(HOSTED_MEMPOOL_LOCK_MTX);
}

void
hosted_pool_unlock_mempool(void *lock_handle)
{
	(void) lock_handle;
	unl_mtx(HOSTED_MEMPOOL_LOCK_MTX);
}

void
hosted_pool_destroy_lock_mempool(void *lock_handle)
{
	/* 静的オブジェクトなので解放処理は不要。 */
	(void) lock_handle;
}
#endif /* H_USE_MEMPOOL */
