/*
 *  ESP32-P4 Wi-Fi モジュール対応 - lwIP sys_arch FMP3 実装
 *  静的プール公開ヘッダ
 *
 *  lwIP の sys_arch 契約（sys_sem_xxx, sys_mbox_xxx, sys_mutex_xxx, sys_thread_new）は
 *  実行時にオブジェクトを動的生成する（sys_sem_new/sys_mbox_new/sys_mutex_new/
 *  sys_thread_new はいずれも呼び出し側が任意のタイミングで呼ぶ）。しかし FMP3 は
 *  完全な静的コンフィギュレーションのカーネルであり、acre_sem のような実行時
 *  オブジェクト生成 API を持たない。
 *
 *  本モジュールは、姉妹実装 `wifi_p4_module/os_adapter/fmp3_hosted_pools.c`
 *  （esp-hosted host 用の同種の橋渡し）と同じ「静的プール払い出し方式」を踏襲する:
 *    - `fmp3_lwip_pools.cfg` で セマフォ/ミューテックス/メールボックス(dtq)/タスク
 *      を「あらかじめ決めた個数 N」だけ静的に CRE_ してプールを作る。
 *    - `fmp3_lwip_pool_*_new()` は空きプール要素を1つ確保し、不透明ハンドル
 *      （`void *`、実体はプール配列要素へのポインタ）を返す。
 *    - `fmp3_lwip_pool_*_free()` は要素をプールへ返却する。
 *    - 確保/返却は `FMP3_LWIP_POOL_META_MTX`（1本の管理用ミューテックス）で保護する
 *      （sys_sem_new 等は lwIP 内部でも通常タスク文脈からのみ呼ばれる）。
 *
 *  【os_adapter との違い（重要・独立性）】
 *  esp-hosted の動的キューは要素サイズ可変のため os_adapter は dtq を2本
 *  （空き/格納済みスロット番号）+ ヒープリングバッファで実現していたが、lwIP の
 *  メールボックスは「void* 1個」しか運ばない契約なので、FMP3 の dtq
 *  （intptr_t 1個を運ぶ）に直接 1:1 で対応できる。ゆえに本モジュールの mbox は
 *  dtq 1本のみで実現し、ヒープ確保も不要（os_adapter より単純）。
 *  また、本モジュールは os_adapter のファイルを一切 include/リンクしない
 *  独立モジュールである（S3 等と将来共有する共通層になるため）。
 *
 *  【時間単位】
 *  FMP3 の RELTIM/TMO はマイクロ秒単位（`t_stddef.h` の
 *  `TMAX_RELTIM = 4000000000U` ≒ 66分40秒であることから確認済み。os_adapter
 *  実装時に確認済みの事実を踏襲）。本ヘッダの各関数の `timeout_ms` 引数は
 *  「lwIP 契約どおりミリ秒」であり、内部で us へ変換する
 *  （変換規則は `fmp3_lwip_pools.c` の `fmp3_lwip_ms_to_tmo()` 参照）。
 *  **lwIP 独自の規約**: sys_arch の timeout は「0 = 無限待ち」であり、
 *  os_adapter (`hosted_pool_map_timeout_ms`) の「0 = ポーリング」とは逆である
 *  ことに注意（両モジュールは独立しているため規約が異なっても問題ないが、
 *  読み違えやすいのでここに明記する）。
 *
 *  【ISR 文脈からの利用】
 *  FMP3 kernel.h で ISR 文脈から呼べることが確認済み（＝i系サービスコールの
 *  単純な #define 別名がある）のは `isig_sem`(=sig_sem) と
 *  `ipsnd_dtq`/`ifsnd_dtq`(=psnd_dtq/fsnd_dtq) のみであり、`twai_sem`/`tloc_mtx`/
 *  `trcv_dtq`/`tsnd_dtq`（タイムアウト付き、TMO_POL 指定時も含む）には i系別名が
 *  存在しない。したがって:
 *    - `sys_sem_signal`（→ sig_sem）は ISR から呼んでよい。
 *    - `sys_mbox_trypost_fromisr`（lwIP 契約上 ISR 専用）は `ipsnd_dtq` を使う。
 *    - `sys_mbox_trypost`（タスク文脈想定）は `tsnd_dtq(...,TMO_POL)` を使う
 *      （ISR から呼ばないこと。lwIP 契約上も trypost は ISR 保証が無い）。
 *  詳細は `mapping.md` を参照。
 */
#ifndef FMP3_LWIP_POOLS_H
#define FMP3_LWIP_POOLS_H

#include <kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  =====================================================================
 *  静的プールのサイズ（集中管理）
 *  =====================================================================
 *  初期値は暫定。lwIP の実際のオブジェクト生成数
 *  （tcpip_thread 用 mbox/sem、各 netconn の mbox、TCP/UDP PCB 用途の内部
 *  セマフォ等）を統合時に確認し、過不足があれば調整すること（TODO）。
 *
 *  【命名に関する注意】ここで定義するマクロは意図的に "LWIP_" ではなく
 *  "FMP3_LWIP_" を接頭辞にしている。"LWIP_" は lwIP 本体（lwipopts.h 等）が
 *  使う名前空間であり、統合時に実 lwIP ヘッダと同居させたときの衝突を避ける
 *  ため。
 */
#ifndef FMP3_LWIP_POOL_NUM_SEM
#define FMP3_LWIP_POOL_NUM_SEM		16	/* sys_sem_new() の静的プール数 */
#endif
#ifndef FMP3_LWIP_POOL_NUM_MUTEX
#define FMP3_LWIP_POOL_NUM_MUTEX	8	/* sys_mutex_new() の静的プール数 */
#endif
#ifndef FMP3_LWIP_POOL_NUM_MBOX
/*  socket/netconn 有効化(2026-07-10 第二段階)に伴い 8→12 へ増量。
 *  内訳の見積: tcpip_thread 受信箱(1) + 各 netconn の recvmbox/acceptmbox
 *  (MEMP_NUM_NETCONN=4 の netconn が最大 recv+accept で 2 本ずつ) + 余裕。
 *  S3 の実績値 SYS_ARCH_NUM_MBOX=10 に余裕を足した値。 */
#define FMP3_LWIP_POOL_NUM_MBOX		12	/* sys_mbox_new() の静的プール数(dtq 1本/mbox) */
#endif
#ifndef FMP3_LWIP_POOL_NUM_THREAD
#define FMP3_LWIP_POOL_NUM_THREAD	8	/* sys_thread_new() の静的プール数 */
#endif

/*  メールボックス1本あたりの最大要素数（dtq の dtqcnt として固定）。
 *  sys_mbox_new(size) の size 要求がこれを超える場合はこの容量へクランプする
 *  （TODO: 統合時に tcpip_thread 等の実際の要求サイズ(TCPIP_MBOX_SIZE 等)を
 *  確認して調整）。 */
#ifndef FMP3_LWIP_MBOX_MAX_ELEMS
#define FMP3_LWIP_MBOX_MAX_ELEMS	32
#endif

/*  セマフォプールの静的 maxsem。sys_sem_new() の count 引数は u8_t（最大255）
 *  なので、CRE_SEM の maxsem をちょうど255にしておけばクランプ処理が一切
 *  不要になる（os_adapter は esp-hosted 側の maxCount(int) が理論上255を
 *  超え得たため 1024 でクランプしていたが、lwIP は u8_t 契約なので単純化できる）。*/
#ifndef FMP3_LWIP_SEM_STATIC_MAXCNT
#define FMP3_LWIP_SEM_STATIC_MAXCNT	255
#endif

/*  タスクプールの既定スタックサイズ・優先度。
 *  【設計判断】os_adapter は esp-hosted（FreeRTOS流: 値が大きいほど高優先度）の
 *  tprio を FMP3 の PRI へ線形写像＋反転していたが、本モジュールの
 *  sys_thread_new(prio) は **lwipopts.h 側（統合時にこのリポジトリ外で書く）が
 *  最初から FMP3 の PRI 値（TMIN_TPRI=1(最高)～TMAX_TPRI=16(最低)）を直接
 *  指定する運用にする**（lwIP 自体は prio の意味を規定しておらず、
 *  各ポートが独自解釈してよい契約のため；os_adapterと異なり呼び出し元
 *  (lwipopts.h)を本統合で自由に書けるので、変換テーブルを持たずに済む）。
 *  そのため本プールは prio を範囲チェック（クランプ）するだけで、写像は行わない。
 *  TODO(統合時): 実際に esp_netif 等が TCPIP_THREAD_PRIO 等に FreeRTOS 流の値
 *  （configMAX_PRIORITIES 系）をハードコードして渡してくる場合は、ここに
 *  os_adapter 同様の変換関数を追加すること。 */
#ifndef FMP3_LWIP_THREAD_STACK_SIZE
#define FMP3_LWIP_THREAD_STACK_SIZE	4096
#endif
#ifndef FMP3_LWIP_THREAD_PRI_DEFAULT
#define FMP3_LWIP_THREAD_PRI_DEFAULT	8	/* prio 範囲外時の既定優先度 */
#endif
#ifndef FMP3_LWIP_THREAD_NAME_MAX
#define FMP3_LWIP_THREAD_NAME_MAX	16	/* デバッグ表示用の名前保持長 */
#endif

/*
 *  =====================================================================
 *  プール操作の戻り値
 *  =====================================================================
 *  ER をそのまま漏らさず、本モジュール専有の小さな状態コードに丸める
 *  （sys_arch.c 側は lwIP の err_t/SYS_ARCH_TIMEOUT へさらに変換する）。
 */
enum {
	FMP3_LWIP_POOL_OK      = 0,
	FMP3_LWIP_POOL_TIMEOUT = -1,	/* タイムアウト（E_TMOUT 相当） */
	FMP3_LWIP_POOL_ERROR   = -2,	/* その他の失敗 */
	FMP3_LWIP_POOL_INVALID = -3		/* 引数(ハンドル)不正 */
};

/*
 *  =====================================================================
 *  セマフォプール
 *  =====================================================================
 */
/* count: 生成直後の初期資源数（lwIP契約: 0 または 1 だが u8_t を素直に受ける） */
extern void	*fmp3_lwip_pool_sem_new(uint8_t count);
extern void	 fmp3_lwip_pool_sem_free(void *sem_handle);
extern void	 fmp3_lwip_pool_sem_signal(void *sem_handle);	/* ISR文脈から呼んでよい(isig_sem=sig_sem) */
extern int	 fmp3_lwip_pool_sem_wait(void *sem_handle, uint32_t timeout_ms);

/*
 *  =====================================================================
 *  ミューテックスプール
 *  =====================================================================
 *  lwIP の sys_mutex_lock() はタイムアウト無し（無限待ち）契約なので、
 *  タイムアウト付き版は用意しない（loc_mtx を直接使う）。
 */
extern void	*fmp3_lwip_pool_mutex_new(void);
extern void	 fmp3_lwip_pool_mutex_free(void *mutex_handle);
extern int	 fmp3_lwip_pool_mutex_lock(void *mutex_handle);
extern int	 fmp3_lwip_pool_mutex_unlock(void *mutex_handle);

/*
 *  =====================================================================
 *  メールボックスプール（FMP3 dtq 1本 = mbox 1個）
 *  =====================================================================
 *  msg は void* をそのまま intptr_t にキャストして dtq で運ぶ（NULL も可）。
 */
extern void	*fmp3_lwip_pool_mbox_new(int size_hint);
extern void	 fmp3_lwip_pool_mbox_free(void *mbox_handle);
extern int	 fmp3_lwip_pool_mbox_post(void *mbox_handle, void *msg);				/* 無限待ち送信 */
extern int	 fmp3_lwip_pool_mbox_trypost(void *mbox_handle, void *msg);			/* タスク文脈のみ */
extern int	 fmp3_lwip_pool_mbox_trypost_fromisr(void *mbox_handle, void *msg);	/* ISR文脈から呼んでよい(ipsnd_dtq) */
extern int	 fmp3_lwip_pool_mbox_fetch(void *mbox_handle, void **p_msg, uint32_t timeout_ms);
extern int	 fmp3_lwip_pool_mbox_tryfetch(void *mbox_handle, void **p_msg);		/* タスク文脈のみ、非ブロッキング */

/*
 *  =====================================================================
 *  タスク（スレッド）プール
 *  =====================================================================
 *  tstack_size は静的 CRE_TSK のスタック領域（FMP3_LWIP_THREAD_STACK_SIZE、
 *  cfg で固定）に対して無視する（os_adapter と同様の制約。TODO(統合時):
 *  実際のスレッドの要求スタックサイズを確認し調整すること）。
 */
extern void	*fmp3_lwip_pool_thread_new(const char *name, void (*thread_fn)(void *arg),
		void *arg, int tstack_size, int prio);

/*
 *  =====================================================================
 *  初期化・トランポリン
 *  =====================================================================
 */
/* sys_init() から一度だけ呼ぶ。プールオブジェクトは cfg の CRE_* で既に
 * 生成済みなので、ここでは "used" フラグの防御的な再初期化のみ行う。 */
extern void fmp3_lwip_pools_init(void);

/* 【診断 2026-07-10】sys_mbox_trypost(=psnd_dtq) の統計を返す（TCP 受信不達の
 * 原因段特定用）。last_ercd: E_TMOUT=満杯 / E_ID・E_PAR=ハンドル不正 / E_CTX=非タスク。 */
extern void fmp3_lwip_pool_mbox_get_trypost_stats(uint32_t *total, uint32_t *fail,
		int *last_ercd);

/* fmp3_lwip_pools.cfg の CRE_TSK から exinf=プール添字(0起点) で呼ばれる。
 * アプリからは通常呼ばない。 */
extern void fmp3_lwip_pool_task_trampoline(EXINF exinf);

#ifdef __cplusplus
}
#endif

#endif /* FMP3_LWIP_POOLS_H */
