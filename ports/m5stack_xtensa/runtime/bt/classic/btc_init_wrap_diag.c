/*
 *  btc_init_wrap_diag — BT Classic SPP実機検証で発見した
 *  assert(list != NULL)（esp-idf/components/bt/common/osi/list.c:262）の
 *  原因切り分け用診断（2026-08-08）。
 *
 *  訂正（第2版）: 当初 list.c:262 を list_foreach() 内の assert と誤認し
 *    btm_bda_to_acl() を疑って wrap したが、第1版の実機検証で
 *    __wrap_btm_init/__wrap_btm_bda_to_acl とも一度も呼ばれていないことが
 *    判明した（診断メッセージが1件も出なかった）。list.c を読み直すと
 *    line 262 は実際には list_begin() 内の assert(list != NULL) だった
 *    （list_foreach の assert は line 247）。
 *
 *  訂正（第4版・根本原因）: 第2版・第3版（list_begin()をwrapしsyslog()で
 *    ログ）はいずれも実機で1件もログが出なかった。当初「wrapが実行時に
 *    通っていない」と疑ったが、esp/app/bt_classic_spp_smoke.c の
 *    dbg_mark()コメント（同ファイル38-40行）に先例があった: syslog()は
 *    logtaskが非同期に drain するキューを経由するため、logtask starvation
 *    やリングオーバーフローで**メッセージが実際には出力されずに消える**。
 *    本診断のsyslog()呼び出しも同じ罠に落ちていた可能性が高い
 *    （esp_bluedroid_init()直後は大量のsyslog呼び出しが短時間に発生する
 *    局面で、まさにこの罠が起きやすい）。target_fput_log()（ポーリング
 *    UART0の同期直接出力、dbg_markと同じ経路）へ切り替えて再検証する。
 *
 *  vendored（esp-idf submodule）は一切改変しない（--wrap、
 *  esp/bt/bt_hci_wrap_diag.c と同じ手法）。診断ビルドでしかリンクしない
 *  （A1_BTCLASSIC_INIT_DIAG）。
 */
#include <kernel.h>

typedef struct list_t list_t;
typedef struct list_node_t list_node_t;
extern list_node_t*	__real_list_begin(const list_t *list);
extern void		target_fput_log(char c);

static void
diag_puts(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

static void
diag_put_hex32(uint32_t v)
{
	static const char	hexdigits[] = "0123456789abcdef";
	int_t			i;

	for (i = 28; i >= 0; i -= 4) {
		target_fput_log(hexdigits[(v >> i) & 0xFU]);
	}
}

volatile uint32_t	btc_diag_list_begin_n;

list_node_t*
__wrap_list_begin(const list_t *list)
{
	btc_diag_list_begin_n++;
	diag_puts("\r\n<<BTCDIAG list_begin n=");
	diag_put_hex32(btc_diag_list_begin_n);
	diag_puts(" list=");
	diag_put_hex32((uint32_t) (uintptr_t) list);
	diag_puts(" caller=");
	diag_put_hex32((uint32_t) (uintptr_t) __builtin_return_address(0));
	diag_puts(">>\r\n");
	return __real_list_begin(list);
}

/*
 *  第5版: caller=l2c_free_p_lcb_pool()（stack/l2cap/l2c_main.c:925、
 *  l2c_free()経由）と判明——l2c_init()が一度も呼ばれる前に l2c_free() が
 *  走っている。呼び出し経路は btu_free_core() <- btu_task_shut_down()
 *  <- BTU_ShutDown()（BTU_StartUp()のerror_exit分岐、またはbtu_task受信の
 *  SIG_BTU_SHUT_DOWN）の2系統。BTU_StartUp()（vendored,
 *  stack/btu/btu_init.c）内の4箇所のgoto error_exit（hash_map_new x3・
 *  osi_thread_create）のどれが原因かを切り分けるため、
 *  osi_thread_create()自体（osi/thread.c、vendored）の戻り値を
 *  --wrapで直接観測する。
 */
typedef struct osi_thread osi_thread_t;
typedef int osi_thread_core_t;
extern osi_thread_t*	__real_osi_thread_create(const char *name, size_t stack_size,
	int priority, osi_thread_core_t core, unsigned char work_queue_num,
	const size_t work_queue_len[]);

/*
 *  第6版: osi_thread_create("BTU_TASK",...) が NULL を返すことは第5版で
 *  確定したが、esp_shim.c 側のタスク生成失敗経路（スタックサイズ拒否／
 *  スロット枯渇／acre_tsk失敗／mact_tsk失敗、いずれも同期マーカ追加済み）
 *  はどれも発火していない。⇒ osi_thread_create() 自身の中
 *  （esp_shim_task_create_pinned に到達する前）で失敗している。候補は
 *  ガード節／osi_calloc x2／osi_work_queue_create／osi_sem_new x3。
 *  「AID_SEM(28)が足りない」は seam_shim.cfg のコメントにある**過去の別事象**
 *  （esp_spp_start_srv()到達時点、RFCOMM確立後）からの類推であり、まだ
 *  実測していない――ここで esp_shim_sem_acre_fail / last_ercd を直接読んで
 *  確かめる（推測で終わらせない）。
 */
extern volatile uint32_t	esp_shim_sem_acre_fail;
extern volatile int32_t	esp_shim_sem_acre_last_ercd;
extern volatile uint32_t	esp_shim_ring_n_create_fail;
/*  2026-08-15（BL-G-3 段3）: `esp_shim_dtq_acre_fail` / `_last_ercd` の参照は
 *  削除した。DTQ 実装（`esp_shim_queue_*`）とその生成口（`esp_shim_dtq.c`）を
 *  撤去したためである。キューの生成失敗はリング側の
 *  `esp_shim_ring_n_create_fail`（上）に出る。 */

osi_thread_t*
__wrap_osi_thread_create(const char *name, size_t stack_size, int priority,
	osi_thread_core_t core, unsigned char work_queue_num, const size_t work_queue_len[])
{
	osi_thread_t	*ret;

	ret = __real_osi_thread_create(name, stack_size, priority, core, work_queue_num, work_queue_len);
	diag_puts("\r\n<<BTCDIAG osi_thread_create name=");
	diag_puts(name);
	diag_puts(" ret=");
	diag_put_hex32((uint32_t) (uintptr_t) ret);
	diag_puts(">>\r\n");
	if (ret == NULL) {
		diag_puts("<<BTCDIAG fail-detail stack_size=");
		diag_put_hex32((uint32_t) stack_size);
		diag_puts(" core=");
		diag_put_hex32((uint32_t) core);
		diag_puts(" wqnum=");
		diag_put_hex32((uint32_t) work_queue_num);
		diag_puts(" wqlen0=");
		diag_put_hex32((uint32_t) work_queue_len[0]);
		diag_puts(">>\r\n");
		diag_puts("<<BTCDIAG sem_acre_fail=");
		diag_put_hex32(esp_shim_sem_acre_fail);
		diag_puts(" sem_last_ercd=");
		diag_put_hex32((uint32_t) esp_shim_sem_acre_last_ercd);
		diag_puts(" ring_fail=");
		diag_put_hex32(esp_shim_ring_n_create_fail);
		diag_puts(">>\r\n");
	}
	return ret;
}
