/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  Wi-Fi os_adapter shimの基盤プリミティブ実装（ASP3用）
 *
 *  設計はdocs/wifi-shim.md．FreeRTOS流の動的生成要求を，ASP3の静的
 *  生成オブジェクトのプール（esp_shim.cfg）＋shim実装で提供する：
 *    - セマフォ／ミューテックス：CRE_SEM／CRE_MTXプールから割当て
 *    - キュー：CRE_DTQプール＋ヒープ確保item（正しいブロッキングと
 *      非タスク文脈送信（psnd_dtq）のためDTQを使う．itemはヒープに
 *      コピーしポインタを流す）
 *    - タスク：CRE_TSKプール（共通エントリ＋関数ポインタ渡し）
 *    - ets_timer：shim専用タイマタスク＋期限ソートリスト
 *    - ヒープ：静的配列上のfirst-fit（境界タグ・前方結合）
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <sil.h>
#include <stdarg.h>
#include <stdio.h>
#include "kernel_cfg.h"
#include "esp_shim.h"
#include "esp_shim_cfg.h"
#include "target_timer.h"		/* esp32c3_systimer_read */
#include "diag_recorder.h"		/* 常設recorder基盤（クラッシュ/ハング診断） */
#ifdef TOPPERS_ESP_WIFI_WPA2
/* mbedtls構成ファイルをソース内で定義（make版のCOPTS経由だと <> がシェル
 * リダイレクトと衝突するため、-D でなくここで定義する。libmbedcrypto.a側と同一） */
#ifndef MBEDTLS_CONFIG_FILE
#define MBEDTLS_CONFIG_FILE <mbedtls/esp_config.h>
#endif
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP_WIFI_WPA2 */

#ifdef TOPPERS_S3_BT_L3LAT_DIAG
/*
 *  ★L3遅延の診断計装（TOPPERS_S3_BT_L3LAT_DIAG、既定OFF）：
 *  「rsil>=3マスク窓（PS.INTLEVEL>=3でLevel-3割込みが遅延する区間）」
 *  の滞在時間をCCOUNTで測る。
 *
 *  計測点（全てl3ld_lock_hook/l3ld_unlock_hookへ集約）：
 *    - esp_shim_int_disable/restore（SHIM_LOCK、BT_LOCK、
 *      esp_shim_bt_enter/exit_critical＝portENTER_CRITICAL系の全て）
 *    - カーネルのlock_cpu/unlock_cpu（core_kernel_impl.h。giant lock
 *      スピン待ちを含むサービスコール臨界区間）
 *    - SIL（core_sil.h TOPPERS_disint/enaint。syslogバッファ書込み等）
 *  加えてLevel-1割込みディスパッチ（INTENABLE=0で走るためLevel-3を
 *  同様にブロックする）の所要時間をtarget_timer.cで、BTのL3 ISR自体の
 *  実行時間・発火間隔をshim_int_dispatch()で計測する。
 *
 *  設計メモ：
 *    - 窓変数はコア0専用（BT関連は全てCLS_PRC1＝コア0固定）。arm/record
 *      は割込み禁止下でのみ走るため排他不要。
 *    - arm（lock側）は旧INTLEVEL<3のときのみ。既にarm済みで再armは
 *      起きない（INTLEVEL>=3中のlockは旧値>=3のため）。ret_int経路の
 *      復帰などCフックを通らずにマスクが解ける経路でstaleなarmが残る
 *      ことがあるが、次のlock（旧値<3）で必ず上書きされてから
 *      unlockが来るため、誤計上にはならない（自己修復）。
 *    - ディスパッチャのアイドルループ（core_support.S dispatcher_idle）は
 *      rsil 0で割込みを開けるため、直前にl3ld_win_startをクリアする
 *      （クリアしないとアイドル滞在時間が窓として誤計上される）。
 *    - RA（呼出し元PC）はwindowed ABIのa0上位2bitを0b01へ正規化して記録。
 */
volatile uint32_t l3ld_win_start;		/* 窓開始CCOUNT|1（0=窓なし） */
volatile uint32_t l3ld_win_ra;			/* 窓を開けた呼出し元PC */
volatile uint32_t l3ld_win_max;			/* 秒次最大（読み手がリセット） */
volatile uint32_t l3ld_win_max_ra;
volatile uint32_t l3ld_win_gmax;		/* 全期間最大 */
volatile uint32_t l3ld_win_gmax_ra;
volatile uint32_t l3ld_win_cnt;			/* 記録した窓の総数 */
volatile uint32_t l3ld_win_hist[6];		/* <10us,<30us,<100us,<150us,<600us,>=600us */

/*  us→CCOUNTサイクル換算（CPUクロック依存。TOPPERS_S3_CPU_FREQ_MHZは
 *  ビルド時選択80/160/240、未定義時160。periph_ctrl.c参照。フォール
 *  バック値はperiph_ctrl.c/target_timer.hの既定と一致させること） */
#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#define TOPPERS_S3_CPU_FREQ_MHZ  160
#endif
#define L3LD_US(us)  ((uint32_t)(us) * (uint32_t)TOPPERS_S3_CPU_FREQ_MHZ)

/*  L3 ISR（線23/27）実行時間・発火間隔（[0]=線23/source8、[1]=線27/source5） */
volatile uint32_t l3ld_isr_dur_max[2];	/* 秒次最大（読み手がリセット） */
volatile uint32_t l3ld_isr_dur_sum[2];
volatile uint32_t l3ld_isr_gap_max[2];	/* 秒次最大発火間隔 */
volatile uint32_t l3ld_isr_last[2];

/*  Level-1割込みディスパッチ所要（コア0のみ。INTENABLE=0区間の近似） */
volatile uint32_t l3ld_l1d_max;			/* 秒次最大（読み手がリセット） */
volatile uint32_t l3ld_l1d_sum;
volatile uint32_t l3ld_l1d_cnt;

static inline uint32_t
l3ld_ccount(void)
{
	uint32_t c;
	Asm("rsr.ccount %0" : "=a"(c));
	return(c);
}

static inline uint32_t
l3ld_is_core0(void)
{
	uint32_t id;
	Asm("rsr.prid %0; extui %0, %0, 13, 1" : "=a"(id));
	return(id == 0U);
}

void
l3ld_lock_hook(uint32_t oldps, uint32_t ra)
{
	if ((oldps & 0xFU) < 3U && l3ld_is_core0()) {
		l3ld_win_ra = (ra & 0x3fffffffU) | 0x40000000U;
		l3ld_win_start = l3ld_ccount() | 1U;	/* 非0保証（誤差<=1cycle） */
	}
}

void
l3ld_unlock_hook(uint32_t newps)
{
	if ((newps & 0xFU) < 3U && l3ld_is_core0() && l3ld_win_start != 0U) {
		uint32_t d = l3ld_ccount() - l3ld_win_start;
		l3ld_win_start = 0U;
		l3ld_win_cnt++;
		if (d > l3ld_win_max) {
			l3ld_win_max = d;
			l3ld_win_max_ra = l3ld_win_ra;
		}
		if (d > l3ld_win_gmax) {
			l3ld_win_gmax = d;
			l3ld_win_gmax_ra = l3ld_win_ra;
		}
		l3ld_win_hist[(d < L3LD_US(10)) ? 0 : (d < L3LD_US(30)) ? 1 :
					  (d < L3LD_US(100)) ? 2 : (d < L3LD_US(150)) ? 3 :
					  (d < L3LD_US(600)) ? 4 : 5]++;
	}
}
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */

/*
 *  クリティカルセクション（mstatus.MIEの退避・復元＝ネスト対応）
 */
uint32_t
esp_shim_int_disable(void)
{
	uint32_t state;

	/* S3(Xtensa)：PS.INTLEVELを15へ上げ、旧PSを返す（C3のmstatus.MIEに相当） */
	Asm("rsil %0, 15" : "=r"(state) :: "memory");
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_lock_hook(state, (uint32_t) __builtin_return_address(0));
#endif
	return(state);
}

void
esp_shim_int_restore(uint32_t state)
{
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_unlock_hook(state);
#endif
	/* 旧PSを復元（INTLEVELを元に戻す） */
	Asm("wsr.ps %0; rsync" :: "r"(state) : "memory");
}

/*
 *  ★SMP注意：SHIM_LOCK/UNLOCKは自コアのPS.INTLEVELマスクのみで、
 *  コア間の相互排他は一切提供しない（esp_shim_malloc/free等のヒープ
 *  臨界区間はコア間非安全）。現状は「BT/WiFi/esp_shim関連タスクは
 *  全てCLS_ALL_PRC1/CLS_PRC1（コア0）に固定する」という運用規約
 *  （esp/bt/bt.cfg・esp_shim.cfg・各アプリcfg参照）でのみ安全性が
 *  成立している——コード上で強制されていない前提であり、将来この
 *  規約を破ってesp_shim_malloc/free呼び出し元をコア1へ動かす場合は
 *  クロスコアスピンロックの追加が必須（P4側`fmp3_newlib_locks.c`の
 *  AMOスピンロック実装参照）。
 */
#define SHIM_LOCK()		uint32_t shim_lock_ = esp_shim_int_disable()
#define SHIM_UNLOCK()	esp_shim_int_restore(shim_lock_)

/*
 *  tick（1ms）→ASP3タイムアウト（μs）変換
 */
TMO
esp_shim_tick_to_tmo(uint32_t tick)
{
	if (tick == ESP_SHIM_BLOCK_FOREVER) {
		return(TMO_FEVR);
	}
	if (tick == 0U) {
		return(TMO_POL);
	}
	if (tick > 2000000U) {		/* TMO（μs・32bit）のオーバフロー回避 */
		tick = 2000000U;
	}
	return((TMO)(tick * 1000U));
}

/*
 *  時刻・乱数
 */
int64_t
esp_shim_time_us(void)
{
	/* S3移植：ESP32-S3はXtensa CCOUNTベースのHRT(us)を使う */
	return((int64_t) target_hrt_get_current());
}

uint32_t
esp_shim_random(void)
{
	/*
	 *  HW乱数生成器（WDEV_RND_REG）．無線が有効になるとRFノイズ由来の
	 *  真性乱数になる（無効時はエントロピー低）．
	 *
	 *  ★ESP32-S3の正しいアドレスは WDEV_RND_REG = 0x6003507C
	 *  （hal/components/soc/esp32s3/include/soc/wdev_reg.h）．
	 *  以前は0x600260B0（C3のSYSCON_RND_DATA_REGアドレスを流用）を
	 *  読んでいたが，S3ではこれは常に定数0x00003fffを返す別レジスタ
	 *  だった．0x3fffは非ゼロのためWPA2 4-wayハンドシェイクは（SNonce
	 *  が非ゼロなので）成功していたが，乱数が定数のためlwIPの
	 *  tcp_init()が tcp_port=TCP_ENSURE_LOCAL_PORT_RANGE(0x3fff)=0xffff
	 *  に固定され，最初のtcp_new_port()で tcp_port++ がu16_tオーバー
	 *  フローして0（無効ポート）を返し，外向きTCP connectが
	 *  ERR_BUF(ENOBUFS/errno=105)で失敗する原因だった．実機で
	 *  esp_shim_random()の戻り値が常時0x3fffと確認して修正．
	 *
	 *  ★チップ差：WDEV_RND_REGのアドレスがS3と無印ESP32で異なる．
	 *   - ESP32-S3 : 0x6003507C（soc/esp32s3/wdev_reg.h）
	 *   - 無印ESP32 : 0x60035144（soc/esp32/wdev_reg.h．AHBバス表現．
	 *     DPORTバス別名では0x3FF75144＝同一レジスタ．esp_random()が読む値）
	 *  誤ると常時0/定数→SNonce不正でWPA2 4-way失敗（reason=15）．
	 */
	/* TOPPERS_ESP32_LX6/ESP32S3 は chip_stddef.h（kernel.h経由で必ず可視）で定義．
	 * esp_shim.c は sdkconfig.h を include しないため CONFIG_IDF_TARGET_* は使えない． */
#if defined(TOPPERS_ESP32_LX6)
	return(sil_rew_mem((void *)0x60035144U));	/* WDEV_RND_REG (無印ESP32) */
#else
	return(sil_rew_mem((void *)0x6003507CU));	/* WDEV_RND_REG (ESP32-S3) */
#endif
}

/*
 *  ログ（blobの_log_write系・lwIPのLWIP_PLATFORM_DIAG/ASSERT等，
 *  printf系を持たない呼出し元の共通折返し先）
 */
void
esp_shim_log_write(const char *format, ...)
{
	va_list	args;
	extern void esp_shim_syslog_vprintf(const char *, va_list);

	va_start(args, format);
	esp_shim_syslog_vprintf(format, args);
	va_end(args);
}

/*
 *  esp_shim_log_emerg / esp_shim_get_core_id の実装は
 *  m5/compat/esp_shim_m5_ext.c にある（本ファイルではない）。
 *
 *  理由（★重要）：本ファイル esp_shim.c は SEAM_OBJS に直接コンパイルされ，
 *  このビルドは未使用シンボルを dead-code-elimination で除去しない
 *  （--gc-sections 非適用）。⇒ **本ファイルへ関数を1つ追加するだけで，
 *  その関数が実際には呼ばれなくても app_xip.bin のバイト同一性が壊れる。**
 *  M5専用の折返し関数はSEAM_OBJSに含まれない別ファイルへ置き，宣言だけを
 *  esp_shim.h/esp_shim_public.h に足す（宣言はコードを生成しないため
 *  イメージに影響しない）。
 */

/*
 *		ヒープ（静的配列上のfirst-fit・境界タグ）
 */
typedef struct heap_block {
	size_t				size;		/* ヘッダ込みサイズ（最下位bit=使用中） */
	struct heap_block	*next;		/* アドレス順の次ブロック */
} HEAP_BLOCK;

#define HB_USED			0x1U
#define HB_SIZE(b)		((b)->size & ~(size_t)HB_USED)
#define HB_IS_USED(b)	(((b)->size & HB_USED) != 0U)
#define HB_ALIGN(sz)	(((sz) + 7U) & ~(size_t)7U)

static uint64_t heap_area[ESP_SHIM_HEAP_SIZE / sizeof(uint64_t)];
static HEAP_BLOCK *heap_top;
static size_t heap_free_total;

/*
 *  ヒープ初期化（冪等）．
 *
 *  ★結合profile（all-in-one）では M5Unified が先に走り，Wi-Fi 起動前に
 *  esp_shim_malloc() を呼ぶ（M5 側の 16KB 専用アリーナは M5_USE_ESP_SHIM で
 *  落ちて本ヒープを共用するため）．⇒ **esp_shim_initialize() を待たずに
 *  初期化できなければならない．**
 *
 *  そこで esp_shim_malloc() から遅延初期化できるようにし，かつ後から来る
 *  esp_shim_initialize() の呼び出しでヒープを作り直して M5 の確保済み領域を
 *  破棄してしまわないよう，**初期化済みなら何もしない**．heap_top は初期化後は
 *  必ず heap_area を指すので，NULL は「未初期化」の確実な印になる．
 */
static void
heap_initialize(void)
{
	if (heap_top != NULL) {
		return;
	}
	heap_top = (HEAP_BLOCK *)heap_area;
	heap_top->size = sizeof(heap_area);
	heap_top->next = NULL;
	heap_free_total = sizeof(heap_area);
}

/*
 *  ★W3④診断計装（同期マーカ、CLASSIC限定）：malloc失敗/タスク生成を
 *  target_fput_log（ポーリングUART0、logtaskを経由しない同期出力）で
 *  出力する。BTU_TASK未生成の直接原因（osi_thread_createのosi_malloc失敗
 *  か task pool枯渇か）を、logtask取りこぼしに影響されず確定するため。
 *  #ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC でCLASSICビルドのみ有効化
 *  （共有ファイルだがS3/W1/W2ビルドは本マクロ未定義＝無効＝非回帰）。
 */
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
extern void target_fput_log(char c);
static size_t	shim_heap_min_free = (size_t)-1;	/* 空き最小（高水位） */

static void
shim_dbg_str(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

static void
shim_dbg_u(uint_t v)
{
	char	buf[12];
	int_t	i = 0;

	if (v == 0U) {
		target_fput_log('0');
		return;
	}
	while (v != 0U) {
		buf[i++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (i > 0) {
		target_fput_log(buf[--i]);
	}
}
static void
shim_dbg_hex(uint32_t v)
{
	int_t	i;
	static const char	hx[] = "0123456789abcdef";
	for (i = 28; i >= 0; i -= 4) {
		target_fput_log(hx[(v >> i) & 0xFU]);
	}
}
/*  コントローラ(HAL bt.c)のグローバル：env構造体ポインタ。offset28がVHCIサブ環境。 */
extern void	*btdm_env_p;
/*  現在のbtdm_env_pとbtdm_env+28(vhci_env)を同期ダンプ（clobberタイミング特定用）。 */
static void
shim_dbg_btenv(void)
{
	shim_dbg_str(" btdm_env_p=");
	shim_dbg_hex((uint32_t) btdm_env_p);
	if (btdm_env_p != NULL) {
		shim_dbg_str(" +28=");
		shim_dbg_hex(*(uint32_t *)((char *) btdm_env_p + 28));
	}
}
#define SHIM_DBG_STR(s)		shim_dbg_str(s)
#define SHIM_DBG_U(v)		shim_dbg_u((uint_t)(v))
#define SHIM_DBG_BTENV()	shim_dbg_btenv()
#else /* TOPPERS_ESP32_BT_BLUEDROID_CLASSIC */
#define SHIM_DBG_BTENV()	((void)0)
#define SHIM_DBG_STR(s)		((void)0)
#define SHIM_DBG_U(v)		((void)0)
#endif /* TOPPERS_ESP32_BT_BLUEDROID_CLASSIC */

void *
esp_shim_malloc(size_t size)
{
	HEAP_BLOCK	*b;
	size_t		need;
	void		*ret = NULL;

	if (size == 0U) {
		size = 1U;
	}
	need = HB_ALIGN(size) + sizeof(HEAP_BLOCK);

	SHIM_LOCK();
	/*  esp_shim_initialize() より先に呼ばれ得る（heap_initialize()の注釈）．  */
	heap_initialize();
	for (b = heap_top; b != NULL; b = b->next) {
		if (!HB_IS_USED(b) && HB_SIZE(b) >= need) {
			if (HB_SIZE(b) >= need + sizeof(HEAP_BLOCK) + 16U) {
				/* 分割 */
				HEAP_BLOCK *rest = (HEAP_BLOCK *)((char *)b + need);
				rest->size = HB_SIZE(b) - need;
				rest->next = b->next;
				b->size = need;
				b->next = rest;
			}
			b->size |= HB_USED;
			heap_free_total -= HB_SIZE(b);
			ret = (void *)(b + 1);
			break;
		}
	}
	SHIM_UNLOCK();

	if (ret == NULL) {
		syslog(LOG_ERROR, "esp_shim: malloc(%u) failed (free=%u)",
			   (uint_t)size, (uint_t)heap_free_total);
		SHIM_DBG_STR("\r\n<<SHIM malloc FAIL req=");
		SHIM_DBG_U(size);
		SHIM_DBG_STR(" free=");
		SHIM_DBG_U(heap_free_total);
		SHIM_DBG_STR(">>\r\n");
	}
/*
 *  ★★2026-07-26: 高水位の記録が **BT Classic のときしか動いていなかった**。
 *  BT Classic は 2026-07-20 に全廃されたので、★**現在の木では一度も記録されない**。
 *  しかも `shim_heap_min_free` を**外へ出す口が無い**（誰もログに出していない）。
 *  ⇒ ★**`ESP_SHIM_HEAP_SIZE = 124KB` は、現在の木では実測の裏づけを持たない数字**である。
 *
 *  【なぜ今これが要るか】CoreS3 で無線を動かすには DRAM が **252,839 バイト足りず**
 *  、`heap_area`(126,976) は
 *  その最大の削り代である。★削って良いかは**使用量を測らなければ決められない**。
 *
 *  ★既定は OFF にしてある——golden 5 構成（wifi×2・ble）を**バイト単位で変えない**ため。
 *  測るときだけ `-DESP_SHIM_HEAP_STATS` を付ける。
 */
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC) || defined(ESP_SHIM_HEAP_STATS)
	else if (heap_free_total < shim_heap_min_free) {
		shim_heap_min_free = heap_free_total;
	}
#endif
	return(ret);
}

void
esp_shim_free(void *ptr)
{
	HEAP_BLOCK	*b;

	if (ptr == NULL) {
		return;
	}
	b = ((HEAP_BLOCK *)ptr) - 1;

	SHIM_LOCK();
	b->size &= ~(size_t)HB_USED;
	heap_free_total += HB_SIZE(b);
	/* 前方（アドレス順の次）との結合 */
	while (b->next != NULL && !HB_IS_USED(b->next)
		   && (char *)b + HB_SIZE(b) == (char *)b->next) {
		b->size = HB_SIZE(b) + HB_SIZE(b->next);
		b->next = b->next->next;
	}
	SHIM_UNLOCK();
}

void *
esp_shim_calloc(size_t n, size_t size)
{
	void	*p;

	/* CWE-190対策：n*sizeの乗算オーバーフロー検査。オーバーフローする
	 * 場合は（意図した確保サイズより小さいバッファを誤って返すのを防ぐため）
	 * NULLを返す。 */
	if (n != 0U && size > ((size_t)-1) / n) {
		return(NULL);
	}
	p = esp_shim_malloc(n * size);
	if (p != NULL) {
		memset(p, 0, n * size);
	}
	return(p);
}

void *
esp_shim_realloc(void *ptr, size_t size)
{
	void		*np;
	HEAP_BLOCK	*b;
	size_t		old;

	if (ptr == NULL) {
		return(esp_shim_malloc(size));
	}
	if (size == 0U) {
		esp_shim_free(ptr);
		return(NULL);
	}
	b = ((HEAP_BLOCK *)ptr) - 1;
	old = HB_SIZE(b) - sizeof(HEAP_BLOCK);
	if (old >= size) {
		return(ptr);
	}
	np = esp_shim_malloc(size);
	if (np != NULL) {
		memcpy(np, ptr, old);
		esp_shim_free(ptr);
	}
	return(np);
}

size_t
esp_shim_heap_free_size(void)
{
	return(heap_free_total);
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/allocator.cのログ出力
 *  （heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)）が要求する。
 *  最大の未使用ブロックサイズ（ヘッダ分を除いたユーザ領域サイズ）を
 *  空きリストを走査して求める。esp_shim_heap_free_size同様，
 *  診断/ログ用途のみで送受信ホットパスからは呼ばれない。
 */
size_t
esp_shim_heap_largest_free_block(void)
{
	HEAP_BLOCK	*b;
	size_t		best = 0U;

	SHIM_LOCK();
	for (b = heap_top; b != NULL; b = b->next) {
		if (!HB_IS_USED(b)) {
			size_t	usable = HB_SIZE(b) - sizeof(HEAP_BLOCK);

			if (usable > best) {
				best = usable;
			}
		}
	}
	SHIM_UNLOCK();
	return(best);
}

/*
 *		セマフォ生成・削除 — 動的生成へ移した
 *
 *  実体は wifi/shim/esp_shim_sem.c（acre_sem / del_sem）。ここに在った
 *  静的プール（CRE_SEM(SHIM_SEM1..100) と shim_sem_id[] / shim_sem_used[]）は
 *  廃止した。上流 fmp3_esp_idf の esp/shim/esp_shim_sem.c をそのまま取り込む。
 *
 *  ハンドル表現は変わらない（セマフォIDを void* へ入れる）ので、下の
 *  take / give / get_count はそのまま使える。
 *
 *  再利用時のカウントクリア（旧 create の pol_sem ループ）は要らなくなった。
 *  動的生成は毎回新しいオブジェクトで、初期値を isemcnt へ直接渡す。
 */

int32_t
esp_shim_sem_take(void *sem, uint32_t block_time_tick)
{
	ER		er;
	int32_t	ret;


	er = twai_sem((ID)(intptr_t)sem, esp_shim_tick_to_tmo(block_time_tick));
	if (er == E_CTX) {
		/*
		 *  twai_semは「待ちに入り得る」サービスコールのため，ディスパッチ
		 *  保留状態では空きトークンがあってもE_CTXを返す（NGKI0181，
		 *  BT-4調査 steering §13のE_CTX問題と同種）。E_CTX時はpol_sem
		 *  （待ちに入らない取得，NGKI0157）へフォールバックし，該当文脈
		 *  では非ブロッキング取得として振る舞わせる。ただし本ポートの
		 *  sense_lock()はPS.INTLEVEL!=0で真になるため，CPUロック相当の
		 *  文脈（rsil保持中・割込みハンドラ内）ではpol_semもE_CTXになり
		 *  救済できない（その場合は従来通り0を返す）。
		 */
		er = pol_sem((ID)(intptr_t)sem);
	}
	ret = (er == E_OK ? 1 : 0);
	diag_event(DIAG_EV_SEM_TAKE, (uint32_t)(intptr_t)sem, (uint32_t)ret);
	return(ret);
}

int32_t
esp_shim_sem_give(void *sem)
{
	int32_t	ret = (sig_sem((ID)(intptr_t)sem) == E_OK ? 1 : 0);
	diag_event(DIAG_EV_SEM_GIVE, (uint32_t)(intptr_t)sem, (uint32_t)ret);
	return(ret);
}

/*
 *  現在のセマフォ資源数（FreeRTOS uxSemaphoreGetCount相当．NimBLE NPL用）
 */
uint32_t
esp_shim_sem_get_count(void *sem)
{
	T_RSEM		rsem;

	if (ref_sem((ID)(intptr_t)sem, &rsem) != E_OK) {
		return(0U);
	}
	return((uint32_t)rsem.semcnt);
}

/*
 *		ミューテックス — 動的生成へ移した
 *
 *  実体は wifi/shim/esp_shim_mtx.c（acre_mtx / del_mtx）。ここに在った
 *  静的プール（CRE_MTX ×20 と shim_mtx_id[] / shim_mtx[]）は廃止した。
 *  上流 fmp3_esp_idf の esp/shim/esp_shim_mtx.c をそのまま取り込んでいる。
 *
 *  静的プールには「shim_mtx_id[] が ESP_SHIM_NUM_MTX より短い構成だと
 *  create() が同じスロットを非NULLで返す」というエイリアス欠陥が
 *  コメントで記録されていた。動的生成ではこの種の欠陥は起こらない。
 */


/*
 *		イベントフラグプール（W3④ SPP：btc_spp.cのtx_event_group専用）
 *
 *  FreeRTOS EventGroupHandle_t（xEventGroupCreate/SetBits/ClearBits/
 *  WaitBits/vEventGroupDelete）をASP3のイベントフラグ（CRE_FLG）へ
 *  写像する。FreeRTOSのxEventGroupWaitBitsは「待った特定ビットのみ」を
 *  xClearOnExit時にクリアする仕様だが，ASP3のTA_CLR属性は「待ち解除時に
 *  全ビットを0クリア」なので意味が異なる。そのためTA_CLRは使わず，
 *  twai_flg()成功後に明示的にclr_flg(id, ~bits_to_wait_for)で該当ビット
 *  のみを落とす（他ビットは温存）ことで正しい選択的クリアを実現する。
 *  本プールはTOPPERS_ESP32_BT_BLUEDROID_CLASSIC限定（ESP_SHIM_NUM_FLGが
 *  未定義のビルド＝W1 Wi-Fi/W2 BLE/NimBLEでは本ブロック自体を
 *  コンパイルしない。freertos/event_groups.hはbtc_spp.c以外から
 *  includeされないため未定義でも実害無し）。
 */
#ifdef ESP_SHIM_NUM_FLG
static const ID shim_flg_id[ESP_SHIM_NUM_FLG] = {
	SHIM_FLG1, SHIM_FLG2,
};
static bool_t shim_flg_used[ESP_SHIM_NUM_FLG];

void *
esp_shim_flag_create(void)
{
	uint_t	i;
	ID		flgid = 0;

	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_FLG; i++) {
		if (!shim_flg_used[i]) {
			shim_flg_used[i] = true;
			flgid = shim_flg_id[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (flgid == 0) {
		syslog(LOG_ERROR, "esp_shim: event flag pool exhausted");
		return(NULL);
	}
	(void) clr_flg(flgid, 0U);		/* 再利用時のビットパターンクリア */
	return((void *)(intptr_t)flgid);
}

void
esp_shim_flag_delete(void *flg)
{
	ID		flgid = (ID)(intptr_t)flg;
	uint_t	i;

	if (flgid == 0) {
		return;
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_FLG; i++) {
		if (shim_flg_id[i] == flgid) {
			shim_flg_used[i] = false;
			break;
		}
	}
	SHIM_UNLOCK();
}

uint32_t
esp_shim_flag_set_bits(void *flg, uint32_t bits_to_set)
{
	ID		flgid = (ID)(intptr_t)flg;
	T_RFLG	rflg;

	if (flgid == 0) {
		return(0U);
	}
	(void) set_flg(flgid, (FLGPTN) bits_to_set);
	if (ref_flg(flgid, &rflg) != E_OK) {
		return(0U);
	}
	return((uint32_t) rflg.flgptn);
}

uint32_t
esp_shim_flag_clear_bits(void *flg, uint32_t bits_to_clear)
{
	ID		flgid = (ID)(intptr_t)flg;
	T_RFLG	rflg;
	uint32_t	prev = 0U;

	if (flgid == 0) {
		return(0U);
	}
	if (ref_flg(flgid, &rflg) == E_OK) {
		prev = (uint32_t) rflg.flgptn;
	}
	(void) clr_flg(flgid, (FLGPTN) ~bits_to_clear);
	return(prev);
}

uint32_t
esp_shim_flag_wait_bits(void *flg, uint32_t bits_to_wait_for, bool_t clear_on_exit,
						 bool_t wait_for_all, uint32_t block_time_tick)
{
	ID		flgid = (ID)(intptr_t)flg;
	FLGPTN	ptn = 0U;
	MODE	wfmode = wait_for_all ? TWF_ANDW : TWF_ORW;
	ER		er;
	T_RFLG	rflg;

	if (flgid == 0) {
		return(0U);
	}
	er = twai_flg(flgid, (FLGPTN) bits_to_wait_for, wfmode, &ptn,
				  esp_shim_tick_to_tmo(block_time_tick));
	if (er == E_CTX) {
		/*  E_CTX時のpol_flgフォールバックはesp_shim_sem_takeと同型
		 *  （ディスパッチ保留状態での待ち系サービスコール制約，NGKI0181）。 */
		er = pol_flg(flgid, (FLGPTN) bits_to_wait_for, wfmode, &ptn);
	}
	if (er == E_OK) {
		if (clear_on_exit) {
			(void) clr_flg(flgid, (FLGPTN) ~bits_to_wait_for);
		}
		return((uint32_t) ptn);
	}
	/*  タイムアウト等：現在値を読み戻す（本ポートの実使用はxClearOnExit
	 *  運用のため通常0のはず。呼び出し側はtx_event_group_val==0を
	 *  タイムアウト判定に使う，btc_spp.c参照）。 */
	if (ref_flg(flgid, &rflg) == E_OK) {
		return((uint32_t) rflg.flgptn);
	}
	return(0U);
}
#endif /* ESP_SHIM_NUM_FLG */

/*
 *		キュー（DTQプール＋ヒープ確保item）
 */
typedef struct {
	ID			dtqid;		/* 0なら未使用スロット */
	ID			semid;		/* 空きスロット数を表すカウンティングセマフォ
							 * （shim_qsem_idからdtqidと同じindexで1:1対応）。
							 * esp_shim_queue_send()のブロッキング待ちに使う
							 * （BT-4調査 §8.5/§10のバグ修正，下記関数群参照）。 */
	uint32_t	item_size;
	uint8_t		*pool;		/* depth*item_size。生成時に1回だけ確保 */
	uint16_t	*free_stk;	/* 空きスロット番号スタック（LIFO） */
	uint32_t	depth;
	volatile uint32_t free_top;	/* 空きスロット数 */
	/*
	 *  ★E_CTX文脈（CPUロック状態＝PS.INTLEVEL!=0）からの送信用の
	 *  「保留リング」（BT-4調査 steering §13）。本ポートのsense_lock()は
	 *  PS.INTLEVEL!=0で真になるため（core_kernel_impl.h），BTクリティカル
	 *  セクション（esp_shim_bt_enter_critical=rsil15保持）内では
	 *  twai_sem/pol_sem/psnd_dtq/sig_semを含む全サービスコールがE_CTXに
	 *  なる。そこでこの文脈では：スロットを直接確保（カーネル呼出し無し）
	 *  →itemをコピー→スロット番号をpend_ringへ積んで成功を返し，
	 *  INTLEVEL=0へ戻った時点（esp_shim_bt_exit_criticalの最外解除，
	 *  または次のqueue_send/recv冒頭）でpsnd_dtqへ流し込む（flush）。
	 *  sem_debtは「トークン（q->semid）を消費せずに確保したスロット数」で，
	 *  解放時（shim_que_slot_free_notify）にsig_semを1回スキップして返済
	 *  する（トークンは不可分＝どの解放で返済しても会計は一致する）。
	 */
	uint16_t	*pend_ring;	/* 保留スロット番号リング（容量depth） */
	uint32_t	pend_rd;	/* リング読み出しindex（SHIM_LOCK下で更新） */
	uint32_t	pend_wr;	/* リング書き込みindex（SHIM_LOCK下で更新） */
	volatile uint32_t	pend_cnt;	/* 保留数 */
	volatile uint32_t	sem_debt;	/* 未返済トークン数 */
} SHIM_QUE;

/*  全キュー合計の保留数（exit_critical側の高速チェック用。SHIM_LOCK下で
 *  更新，読み出しはロック無し＝0/非0の判定にのみ使う）。 */
static volatile uint32_t	shim_que_pend_total;

/*
 *  キューは「生成時に固定プールを1回確保し、送受信ではmallocしない」方式。
 *  旧実装はメッセージ毎に esp_shim_malloc/free していたため、
 *   (1) ISR文脈(queue_send_from_isr)のmalloc失敗でtx-done完了通知を取りこぼし
 *       → WiFi動的TXバッファが永久未回収 → 送信自己ロック(両コアフリーズ)、
 *   (2) 1.6KB級alloc/freeのchurnで124KBシムヒープが断片化、
 *  という持続高レートTXのハングを招いていた。
 *  固定プール化でmallocを送受信経路から排除し、DTQはスロット番号のみ運ぶ。
 */

static const ID shim_dtq_id[ESP_SHIM_NUM_DTQ] = {
	SHIM_DTQ1, SHIM_DTQ2, SHIM_DTQ3, SHIM_DTQ4
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
	, SHIM_DTQ5, SHIM_DTQ6, SHIM_DTQ7, SHIM_DTQ8
#endif
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	, SHIM_DTQ9, SHIM_DTQ10, SHIM_DTQ11, SHIM_DTQ12
	, SHIM_DTQ13, SHIM_DTQ14, SHIM_DTQ15, SHIM_DTQ16
#endif
};
/*  shim_dtq_idと同じindexで1:1対応する「空きスロット数」セマフォ
 *  （esp_shim.cfg参照，BT-4調査 §8.5/§10のバグ修正）。 */
static const ID shim_qsem_id[ESP_SHIM_NUM_DTQ] = {
	SHIM_QSEM1, SHIM_QSEM2, SHIM_QSEM3, SHIM_QSEM4
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
	, SHIM_QSEM5, SHIM_QSEM6, SHIM_QSEM7, SHIM_QSEM8
#endif
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	, SHIM_QSEM9, SHIM_QSEM10, SHIM_QSEM11, SHIM_QSEM12
	, SHIM_QSEM13, SHIM_QSEM14, SHIM_QSEM15, SHIM_QSEM16
#endif
};
static SHIM_QUE shim_que[ESP_SHIM_NUM_DTQ];

void *
esp_shim_queue_create(uint32_t len, uint32_t item_size)
{
	uint_t		i;
	uint32_t	k, depth = len;
	SHIM_QUE	*q = NULL;
	uint8_t		*pool;
	uint16_t	*stk;
	uint16_t	*pring;

	if (depth > ESP_SHIM_DTQ_CNT) {
		syslog(LOG_NOTICE, "esp_shim: queue len %u > pool depth %u",
			   (uint_t)len, (uint_t)ESP_SHIM_DTQ_CNT);
		depth = ESP_SHIM_DTQ_CNT;
	}
	/*  プールと空きスタックを生成時に1回だけ確保（以後mallocしない）。 */
	pool = (uint8_t *) esp_shim_malloc((size_t)depth * item_size);
	stk = (uint16_t *) esp_shim_malloc((size_t)depth * sizeof(uint16_t));
	pring = (uint16_t *) esp_shim_malloc((size_t)depth * sizeof(uint16_t));
	syslog(LOG_NOTICE, "esp_shim: queue create depth=%u item=%u pool=%uB",
		   (uint_t)depth, (uint_t)item_size, (uint_t)(depth * item_size));
	if (pool == NULL || stk == NULL || pring == NULL) {
		esp_shim_free(pool);
		esp_shim_free(stk);
		esp_shim_free(pring);
		syslog(LOG_ERROR, "esp_shim: queue pool alloc失敗");
		return(NULL);
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_DTQ; i++) {
		if (shim_que[i].dtqid == 0) {
			q = &shim_que[i];
			q->dtqid = shim_dtq_id[i];
			q->semid = shim_qsem_id[i];
			q->item_size = item_size;
			q->pool = pool;
			q->free_stk = stk;
			q->depth = depth;
			for (k = 0U; k < depth; k++) {
				stk[k] = (uint16_t)k;
			}
			q->free_top = depth;
			q->pend_ring = pring;
			q->pend_rd = 0U;
			q->pend_wr = 0U;
			q->pend_cnt = 0U;
			q->sem_debt = 0U;
			break;
		}
	}
	SHIM_UNLOCK();

	if (q == NULL) {
		esp_shim_free(pool);
		esp_shim_free(stk);
		esp_shim_free(pring);
		syslog(LOG_ERROR, "esp_shim: queue pool exhausted");
	} else {
		/*
		 *  空きスロット数セマフォ（q->semid）の初期値をdepthに合わせる。
		 *  スロット再利用（旧queue_deleteでdrain済み）に備えてまず0まで
		 *  読み捨ててから，depth回sig_semしてカウントを積む。
		 */
		while (pol_sem(q->semid) == E_OK) {
			;	/* 前回利用分の残トークンを捨てる */
		}
		for (k = 0U; k < depth; k++) {
			(void) sig_sem(q->semid);
		}
	}
	return((void *)q);
}

void
esp_shim_queue_delete(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;

	if (q != NULL) {
		while (prcv_dtq(q->dtqid, &data) == E_OK) {
			;	/* スロットはプール管理のため個別freeしない */
		}
		while (pol_sem(q->semid) == E_OK) {
			;	/* 空きスロット数セマフォも0まで読み捨てる（再利用に備える） */
		}
		SHIM_LOCK();
		shim_que_pend_total -= q->pend_cnt;	/* 保留リング残も破棄 */
		q->pend_cnt = 0U;
		SHIM_UNLOCK();
		esp_shim_free(q->pool);
		esp_shim_free(q->free_stk);
		esp_shim_free(q->pend_ring);
		q->pool = NULL;
		q->free_stk = NULL;
		q->pend_ring = NULL;
		q->dtqid = 0;
	}
}

/*  空きスロットを1つ取得（無ければ0xFFFFFFFF）。SHIM_LOCK下で呼ぶこと。 */
static uint32_t
shim_que_slot_alloc(SHIM_QUE *q)
{
	if (q->free_top == 0U) {
		return(0xFFFFFFFFU);
	}
	q->free_top--;
	return((uint32_t)q->free_stk[q->free_top]);
}

/*  スロットを返却。SHIM_LOCK下で呼ぶこと。 */
static void
shim_que_slot_free(SHIM_QUE *q, uint32_t slot)
{
	q->free_stk[q->free_top] = (uint16_t)slot;
	q->free_top++;
}

/*
 *  スロットを1つ解放し，対応する空きスロット数セマフォへトークンを1つ
 *  返却する（esp_shim_queue_send()のブロッキング待ちを解除するため）。
 *  SHIM_LOCK外から呼ぶこと（sig_semはSHIM_LOCK内で呼ぶべきでないため。
 *  sig_semはisig_semと同一実装＝ISR文脈からも呼出し可）。
 *
 *  sem_debt（トークンを消費せずに確保されたスロット数＝E_CTX保留送信分）
 *  が残っている場合は，sig_semを1回スキップして返済する。トークンは
 *  不可分なので，どのスロットの解放で返済しても総会計は一致する。
 */
static void
shim_que_slot_free_notify(SHIM_QUE *q, uint32_t slot)
{
	bool_t	repay = false;

	SHIM_LOCK();
	shim_que_slot_free(q, slot);
	if (q->sem_debt > 0U) {
		q->sem_debt--;
		repay = true;
	}
	SHIM_UNLOCK();
	if (!repay) {
		(void) sig_sem(q->semid);
	}
}

/*
 *  トークン（q->semid）を消費せずにスロットを確保してitemをコピーする．
 *  E_CTX文脈（pol_sem不可）からの送信の共通前段：sem_debt++で会計を
 *  保存し（解放時にsig_semを1回スキップして返済），コピーまで済ませた
 *  スロット番号を返す。スロット枯渇（真の満杯）なら0xFFFFFFFF。
 *  itemはポインタ級の小サイズのため，memcpyまで単一のSHIM_LOCK区間で
 *  行う（未コピーのスロットが後続の公開経路から見えないことを保証）。
 */
static uint32_t
shim_que_slot_alloc_debt_copy(SHIM_QUE *q, const void *item)
{
	uint32_t	slot;

	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	if (slot != 0xFFFFFFFFU) {
		q->sem_debt++;	/* トークン未消費で確保＝解放時にsig_semを1回スキップ */
		memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	}
	SHIM_UNLOCK();
	return(slot);
}

/*  保留経路の利用実績カウンタ（検証・将来デバッグ用。初回のみsyslogに
 *  痕跡を残す＝E_CTXフォールバックが実際に発動した証跡）。 */
static volatile uint32_t	shim_que_pend_used;

/*
 *  確保・コピー済みのスロット番号を保留リングへ公開する（SHIM_QUE定義部
 *  のコメント参照，BT-4調査 steering §13）。CPUロック状態（PS.INTLEVEL!=0，
 *  BTクリティカルセクション等）＝psnd_dtqすら発行できない文脈向けの最終
 *  手段で，INTLEVEL=0復帰後のflushでDTQへ流し込まれる。
 */
static void
shim_que_pend_push_slot(SHIM_QUE *q, uint32_t slot)
{
	SHIM_LOCK();
	q->pend_ring[q->pend_wr] = (uint16_t)slot;
	q->pend_wr = (q->pend_wr + 1U >= q->depth) ? 0U : q->pend_wr + 1U;
	q->pend_cnt++;		/* スロット数で上限が押さえられておりdepthを超えない */
	shim_que_pend_total++;
	shim_que_pend_used++;
	SHIM_UNLOCK();
	if (shim_que_pend_used == 1U) {
		/*  初回のみ痕跡を残す（E_CTXフォールバック発動の実機証跡。
		 *  syslogは本ポートではSILベースでこの文脈からも呼出し可）。 */
		syslog(LOG_NOTICE, "esp_shim: pend path engaged (dtqid=%d)",
			   (int)q->dtqid);
	}
}

/*
 *  E_CTX文脈（CPUロック状態）からの送信の実体：カーネル呼出しを一切
 *  使わずスロットを確保してitemをコピーし，スロット番号を保留リングへ
 *  積む。成功なら1，スロット枯渇（真の満杯）なら0を返す。
 */
static int32_t
shim_que_pend_push(SHIM_QUE *q, const void *item)
{
	uint32_t	slot = shim_que_slot_alloc_debt_copy(q, item);

	if (slot == 0xFFFFFFFFU) {
		return(0);	/* 真の満杯 */
	}
	shim_que_pend_push_slot(q, slot);
	return(1);
}

/*
 *  保留リングのflush：INTLEVEL=0（サービスコール発行可能な文脈）で呼び，
 *  保留中のスロット番号をpsnd_dtqでDTQへ流し込む。呼出し文脈は
 *  タスク・非タスクいずれでも良い（psnd_dtqは待ちに入らない送信で
 *  非タスク文脈からも呼出し可）。呼出し点：
 *    - esp_shim_bt_exit_critical()の最外解除直後（bt_shim.c）
 *    - esp_shim_queue_send()/esp_shim_queue_recv()の冒頭（機会的）
 *  psnd_dtqの失敗は原理的に起きない（DTQ容量=プールdepth，未解放スロット
 *  総数<=depthのためDTQには必ず空きがある）が，万一失敗した場合は
 *  スロットを解放して取りこぼしをDIAGに残す。
 */
void
esp_shim_queue_flush_pending(void)
{
	uint_t		i;
	uint32_t	slot;
	SHIM_QUE	*q;
	ER			er;

	if (shim_que_pend_total == 0U) {
		return;		/* 高速パス（ロック無し読み） */
	}
	for (i = 0U; i < ESP_SHIM_NUM_DTQ; i++) {
		q = &shim_que[i];
		while (q->dtqid != 0 && q->pend_cnt > 0U) {
			SHIM_LOCK();
			if (q->pend_cnt == 0U) {
				SHIM_UNLOCK();
				break;
			}
			slot = (uint32_t)q->pend_ring[q->pend_rd];
			q->pend_rd = (q->pend_rd + 1U >= q->depth) ? 0U : q->pend_rd + 1U;
			q->pend_cnt--;
			shim_que_pend_total--;
			SHIM_UNLOCK();
			er = psnd_dtq(q->dtqid, (intptr_t)slot);
			if (er != E_OK) {
				syslog(LOG_ERROR, "esp_shim: DIAG flush psnd_dtq er=%d q=%d",
					   (int)er, (int)q->dtqid);
				shim_que_slot_free_notify(q, slot);
			}
		}
	}
}

/*
 *  esp_shim_bt_crit_nest_get()のweakデフォルト実装（Wi-Fi単体ビルド用）．
 *  強シンボルの本実装はesp/bt/bt_shim.c（BLEビルドのみリンク対象）に
 *  あり，BLEビルドではそちらが優先される．Wi-FiビルドはBTクリティカル
 *  セクション機構を持たないため常に0（非ネスト）を返す．
 */
uint32_t esp_shim_bt_crit_nest_get(void) __attribute__((weak));

uint32_t
esp_shim_bt_crit_nest_get(void)
{
	return(0U);
}

/*
 *  esp_shim_queue_send()：タスク文脈からのキュー送信．
 *
 *  ★**呼出し元の block_time_tick を守ること。** 固定サイズスロットプールが
 *  枯渇したときに portMAX_DELAY を無視して即座に失敗を返すと，呼出し元は
 *  この契約違反を検出できずタスクを永久停止させ得る。
 *
 *  方式：キュー毎に「空きスロット数」をそのまま値とするカウンティング
 *  セマフォ（q->semid，esp_shim_queue_create()生成時にdepthへ初期化）を
 *  用意し，twai_semで資源トークンを1つ確保できてから初めて
 *  shim_que_slot_allocを呼ぶ。これによりtwai_semのTMO_FEVR/有限tick/
 *  TMO_POLの意味論がそのままesp_shim_queue_send()のブロッキング契約
 *  になる（ASP3標準プリミティブが提供するタイムアウト機構をそのまま
 *  流用するため，独自のポーリング・時刻計算は不要）。トークンを確保した
 *  時点でfree_top>=1が保証されるため，直後のshim_que_slot_allocは
 *  原理的に失敗しない（0xFFFFFFFFはSHIM_LOCK内の防御的チェックのみ）。
 *  また，DTQ容量（ESP_SHIM_DTQ_CNT）はプールdepthと同一なので，トークン
 *  確保後のtsnd_dtqは実際には待たない（空きが必ずある）。
 *  固定プール自体の設計（送受信経路からmallocを排除する方針）は維持する。
 */
int32_t
esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick,
					bool_t to_front)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	uint32_t	slot;
	TMO			tmo;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	esp_shim_queue_flush_pending();	/* 機会的flush（保留残の滞留防止） */
	tmo = esp_shim_tick_to_tmo(block_time_tick);
	er = twai_sem(q->semid, tmo);
	if (er == E_CTX) {
		/*
		 *  ★BT-4調査 §10.5仮説B（実機DIAGで確定，steering §13）への対処：
		 *  NimBLEポーティング層はBTクリティカルセクション
		 *  （esp_shim_bt_enter_critical=rsil15保持）のまま
		 *  ble_npl_eventq_put→本関数(portMAX_DELAY)を呼ぶことがある。
		 *  実機捕捉値：er=-25(E_CTX) ctx=0 dsp=0 loc=1 critnest=1。
		 *  TOPPERS仕様上twai_semは「待ちに入り得る」サービスコールなので，
		 *  ディスパッチ保留状態では空きトークンがあっても無条件にE_CTXを
		 *  返す（NGKI0181）。ここで失敗を返すとNimBLEのassertion
		 *  （ret==pdPASS）が発火し，接続後のイベント処理が沈黙する
		 *  （BT-4の接続直後assertion→ATT無応答バグ）。
		 *
		 *  まずpol_sem（待ちに入らない取得，NGKI0157）を試す：ディスパッチ
		 *  保留状態（dis_dsp中・IPMがENAALL以外等）ならこれが合法で，
		 *  非ブロッキング取得として成立する。ただし本ポートのsense_lock()は
		 *  PS.INTLEVEL!=0で真になるため（core_kernel_impl.h），BTクリティカル
		 *  セクション内ではpol_semも
		 *  CHECK_TSKCTX_UNLでE_CTXになる（実機再確認済み：pol_semフォール
		 *  バック単独では同一DIAGが再発した）。この場合はカーネル呼出しを
		 *  一切使わない保留リング（shim_que_pend_push，INTLEVEL復帰時に
		 *  psnd_dtqへflush）で送信を成立させる。FreeRTOS意味論
		 *  （portENTER_CRITICAL内のxQueueSendは満杯でない限り成功）と一致し，
		 *  真の満杯時のみ0を返す。
		 */
		er = pol_sem(q->semid);
		if (er == E_CTX) {
			int32_t	ok = shim_que_pend_push(q, item);

			if (ok == 0) {
				/* 真の満杯のみここに来る（原因確定済みのためDIAGは満杯検出用） */
				syslog(LOG_ERROR, "esp_shim: DIAG2 semid=%d er=%d ctx=%d dsp=%d",
					   (int)q->semid, (int)er, (int)sns_ctx(), (int)sns_dsp());
				syslog(LOG_ERROR, "esp_shim: DIAG3 loc=%d full free_top=%u",
					   (int)sns_loc(), (uint_t)q->free_top);
			}
			diag_event(DIAG_EV_Q_SEND, (uint32_t)(intptr_t)q, (uint32_t)ok);
			return(ok);
		}
	}
	if (er != E_OK) {
		extern uint32_t esp_shim_bt_crit_nest_get(void);
		/*
		 *  タイムアウト（有限tick）・ノンブロッキング要求・pol_sem
		 *  フォールバックのいずれでも取得できなかった＝真の満杯。
		 *  （E_CTX起因は上の保留リングで解消済みのため，ここに来るのは
		 *  本当にスロットが無い場合のみ。満杯検出用にDIAGは残す。）
		 */
		syslog(LOG_ERROR, "esp_shim: DIAG2 semid=%d er=%d ctx=%d dsp=%d",
			   (int)q->semid, (int)er, (int)sns_ctx(), (int)sns_dsp());
		syslog(LOG_ERROR, "esp_shim: DIAG3 loc=%d", (int)sns_loc());
		syslog(LOG_ERROR, "esp_shim: DIAG4 critnest=%d",
			   (int)esp_shim_bt_crit_nest_get());
		diag_event(DIAG_EV_Q_SEND, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	SHIM_UNLOCK();
	if (slot == 0xFFFFFFFFU) {
		/* 到達しないはずだが，念のため（セマフォとプールの不整合防御） */
		syslog(LOG_ERROR,
			   "esp_shim: DIAG q=%p slot_alloc failed after token free_top=%u",
			   (void *)q, (uint_t)q->free_top);
		(void) sig_sem(q->semid);
		diag_event(DIAG_EV_Q_SEND, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	/*
	 *  [レビュー指摘#14：制限事項の明記、意図的に未実装]
	 *  to_front（xQueueSendToFront/_queue_send_to_front委譲、NimBLE NPLの
	 *  eventq_put_to_front用）は非対応で，常に通常送信（末尾追加）として
	 *  扱う。FMP3のtsnd_dtq（データキュー，esp/bt/stub/include/
	 *  freertos/queue.hのuxQueueSpacesAvailable近傍コメント参照）はFIFO
	 *  専用のリングバッファ実装で先頭挿入APIを持たず，本shimの固定プール
	 *  設計（送受信経路からmallocを排除する方針，esp_shim_queue_send()の
	 *  冒頭コメント参照）とも相容れないため，優先度キュー的な並べ替えを
	 *  実装するにはdtqを別のデータ構造（連結リスト等）へ置き換える設計
	 *  変更が必要で，本ポートのスコープでは非現実的と判断した。
	 *  NimBLEのeventq_put_to_frontは稀用途で順序が厳密に要る箇所が無い
	 *  ことを確認済みのため，通常送信で代用する（挙動上の制限として
	 *  文書化するに留め，実装はしない）。
	 */
	(void) to_front;
	er = tsnd_dtq(q->dtqid, (intptr_t)slot, tmo);
	if (er == E_CTX) {
		/*
		 *  tsnd_dtqも「待ちに入り得る」サービスコールなので，twai_semと
		 *  同様にクリティカルセクション内（ディスパッチ保留状態）では
		 *  空きがあってもE_CTXを返す。DTQ容量はプールdepthと同一で，
		 *  トークン確保後はDTQに必ず空きがある設計のため，psnd_dtq
		 *  （待ちに入らない送信，ISR経路と同じ）へフォールバックする。
		 */
		er = psnd_dtq(q->dtqid, (intptr_t)slot);
	}
	if (er != E_OK) {
		syslog(LOG_ERROR, "esp_shim: DIAG q=%p tsnd_dtq_er=%d", (void *)q, (int)er);
		shim_que_slot_free_notify(q, slot);
		diag_event(DIAG_EV_Q_SEND, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	diag_event(DIAG_EV_Q_SEND, (uint32_t)(intptr_t)q, 1U);
	return(1);
}

/*
 *  esp_shim_queue_send_from_isr()：ISR文脈からのキュー送信．
 *
 *  ISR文脈はブロッキングできないため，非ブロッキング仕様のまま
 *  （真の満杯＝スロット枯渇なら即座に0を返す）。
 *
 *  ★FMP3の文脈チェック一次確認（fmp3_trunk/kernel/，2026-07-10）：
 *    - pol_sem   ：CHECK_TSKCTX_UNL（semaphore.c，NGKI1513/1514）＝
 *                  タスク文脈専用。割込みハンドラ（非タスク文脈）からは
 *                  空きの有無に関係なく常にE_CTX。
 *    - psnd_dtq  ：CHECK_UNL_MYSTATE（dataqueue.c）＝CPUロック状態で
 *                  なければ非タスク文脈から合法（第3世代のiXXX統合）。
 *    - sig_sem   ：CHECK_UNL_MYSTATE（semaphore.c，NGKI1501）＝同上。
 *  本ポートのLevel-1割込みCハンドラはINTLEVEL=0・INTENABLEマスクで走る
 *  （core_support.S）ため，Wi-Fi ISRではsense_lock()=偽・sense_context()=真
 *  →pol_semだけがE_CTXになり，psnd_dtq/sig_semはそのまま使える。
 *
 *  ★d94fec5回帰の記録：d94fec5はpol_semのE_CTXを一律「保留リング行き」
 *  にしたため，Wi-Fi ISRからの全キュー送信（RX/MLMEイベント）のDTQ投入が
 *  次のタスク文脈キュー操作のflushまで遅延し，受信タスクが即座に起床
 *  できなくなってAP接続がauthから進まなくなった（実機：DISCONNECTED
 *  reason=2 AUTH_EXPIRE/reason=205の繰り返し。e3ce20bベースラインは正常）。
 *  修正：pol_semがE_CTXでもCPUロック状態でなければpsnd_dtqは合法なので，
 *  トークン未消費でスロットを確保（sem_debt++で会計保存，解放時に
 *  sig_semを1回スキップして返済）→psnd_dtqで直接DTQへ送信し，受信タスクを
 *  即起床させる（＝d94fec5前の実質の挙動）。psnd_dtqまでE_CTXになる
 *  CPUロック文脈（BTクリティカルセクション=rsil15保持中）のみ保留リング
 *  へ退避する（BLE経路の生命線，steering §13）。
 */
int32_t
esp_shim_queue_send_from_isr(void *que, void *item)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	uint32_t	slot;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	er = pol_sem(q->semid);
	if (er == E_CTX) {
		/*
		 *  非タスク文脈（Wi-Fi ISR等）またはCPUロック状態：pol_semは
		 *  使えないので，トークン未消費でスロットを確保・コピーし
		 *  （sem_debtで会計保存），まずpsnd_dtqでの直接送信を試みる。
		 */
		slot = shim_que_slot_alloc_debt_copy(q, item);
		if (slot == 0xFFFFFFFFU) {
			/* 真の満杯のみ失敗（非ブロッキング仕様は不変） */
			diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 0U);
			return(0);
		}
		er = psnd_dtq(q->dtqid, (intptr_t)slot);
		if (er == E_CTX) {
			/*
			 *  CPUロック状態（PS.INTLEVEL!=0，BTクリティカルセクション
			 *  等）：psnd_dtqも発行できないため保留リングへ退避し，
			 *  INTLEVEL=0復帰時のflushでDTQへ流し込む。
			 */
			shim_que_pend_push_slot(q, slot);
			diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 1U);
			return(1);
		}
		if (er != E_OK) {
			/* 設計上DTQ満杯は起きない（容量=プールdepth）が防御的に回収 */
			shim_que_slot_free_notify(q, slot);
			diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 0U);
			return(0);
		}
		diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 1U);
		return(1);
	}
	if (er != E_OK) {
		return(0);	/* 満杯（従来通り即時失敗，非ブロッキング仕様は不変） */
	}
	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	SHIM_UNLOCK();
	if (slot == 0xFFFFFFFFU) {
		(void) sig_sem(q->semid);
		diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	if (psnd_dtq(q->dtqid, (intptr_t)slot) != E_OK) {
		shim_que_slot_free_notify(q, slot);
		diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	diag_event(DIAG_EV_Q_SEND_ISR, (uint32_t)(intptr_t)q, 1U);
	return(1);
}

int32_t
esp_shim_queue_recv(void *que, void *item, uint32_t block_time_tick)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	esp_shim_queue_flush_pending();	/* 機会的flush（保留残の滞留防止） */
	er = trcv_dtq(q->dtqid, &data, esp_shim_tick_to_tmo(block_time_tick));
	if (er == E_CTX) {
		/*
		 *  trcv_dtqも「待ちに入り得る」サービスコールのため，ディスパッチ
		 *  保留状態ではデータがあってもE_CTXを返す（steering §13のE_CTX
		 *  問題と同種）。prcv_dtq（待ちに入らない受信）へフォールバック
		 *  し，該当文脈では非ブロッキング受信として振る舞わせる。
		 */
		er = prcv_dtq(q->dtqid, &data);
	}
	if (er != E_OK) {
		diag_event(DIAG_EV_Q_RECV, (uint32_t)(intptr_t)q, 0U);
		return(0);
	}
	memcpy(item, q->pool + (size_t)(uint32_t)data * q->item_size, q->item_size);
	shim_que_slot_free_notify(q, (uint32_t)data);
	diag_event(DIAG_EV_Q_RECV, (uint32_t)(intptr_t)q, 1U);
	return(1);
}

uint32_t
esp_shim_queue_msg_waiting(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	T_RDTQ		rdtq;

	if (q == NULL || ref_dtq(q->dtqid, &rdtq) != E_OK) {
		return(0U);
	}
	return((uint32_t)rdtq.sdtqcnt + q->pend_cnt);	/* flush前の保留分も計上 */
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/thread.cのosi_thread_queue_wait_size
 *  （FreeRTOS uxQueueSpacesAvailable相当）が要求する。プール生成時に固定
 *  した空きスロット数（free_top，SHIM_LOCK下で管理）をそのまま返す。
 */
uint32_t
esp_shim_queue_spaces_available(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	uint32_t	n;

	if (q == NULL) {
		return(0U);
	}
	SHIM_LOCK();
	n = q->free_top;
	SHIM_UNLOCK();
	return(n);
}

/*
 *  キューを空にする（FreeRTOS xQueueReset相当．NimBLE eventq_reset用）．
 *  格納中の全itemをprcv_dtq（ポーリング受信）で取り出す．本shimのキューは
 *  「生成時に固定確保したプールのスロット番号」をDTQで運ぶ方式（追記30の
 *  高負荷ハング対策．esp_shim_queue_create参照）のため，C3のようにitemを
 *  esp_shim_freeするのではなく，スロットをfree_stkへ返却する。
 */
void
esp_shim_queue_reset(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;

	if (q != NULL) {
		esp_shim_queue_flush_pending();	/* 保留分もDTQへ出してから空にする */
		while (prcv_dtq(q->dtqid, &data) == E_OK) {
			shim_que_slot_free_notify(q, (uint32_t)data);
		}
	}
}

/*
 *		タスクプール
 */
typedef struct {
	ID			tskid;
	/*
	 *  ★自タスク終了（ext_tsk）はカーネルオブジェクトを残す。
	 *  del_tsk できるのは休止状態になった後なので、そのIDをここに残し、
	 *  同じスロットを次に使うときに回収する（esp_shim_tsk_reap）。
	 *  回収前にスタックを共有する新タスクを作ってはならない。
	 */
	ID			stale_tskid;
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	void		*thread_sem;	/* _wifi_thread_semphr_get用（遅延生成） */
#if defined(ESP_SHIM_TASK_NOTIFY)
	/*
	 *  ★FreeRTOS の task notification 値（`ulTaskNotifyTake`/`xTaskNotifyGive`）。
	 *  M5Unified の Speaker/Mic がこれだけを使う。
	 *  ★待ちは `slp_tsk`/`tslp_tsk`、起床は `wup_tsk` で実現する。
	 *  この 3 つは本シムの他の場所で**一切使っていない**ので、
	 *  起床要求のラッチが他の待ちを乱す心配がない。
	 */
	uint32_t	notify;
#endif
} SHIM_TSK;

static SHIM_TSK shim_tsk[ESP_SHIM_NUM_TSK];
static void *shim_main_thread_sem;	/* プール外タスク用 */

/*
 *  スロットごとの初期優先度
 *
 *  旧 cfg の CRE_TSK が持っていた構成ごとの分岐をそのまま写した。
 *  index0（旧 SHIM_TSK1）だけ BT コントローラ級で、それ以外は Wi-Fi 級。
 *  acre_tsk の itskpri として実行時に渡す。
 */
static PRI
shim_tsk_slot_pri(uint_t slot)
{
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
	if (slot == 0U) {
		return((PRI) ESP_SHIM_BT_CTRL_TASK_PRI);
	}
#endif
	(void) slot;
	return((PRI) ESP_SHIM_WIFI_TASK_PRI);
}

/*
 *  共通エントリ（exinf=スロット番号。acre_tsk の task として渡す）
 */
void
esp_shim_task_entry(EXINF exinf)
{
	SHIM_TSK	*t = &shim_tsk[(uint_t)exinf];

	t->fn(t->arg);
	/*
	 *  FreeRTOSのタスクはvTaskDelete(NULL)で終わるのが作法だが，
	 *  関数リターンで終わった場合もスロットを解放する
	 */
	SHIM_LOCK();
	t->used = false;
	SHIM_UNLOCK();
	ext_tsk();
}

#ifdef TOPPERS_STACK_PROBE
/*
 *  スタック実使用量計測（-DTOPPERS_STACK_PROBE でオプトイン）用に、
 *  blobが各shimタスクスロットへ要求したスタックサイズを記録する。
 *  target_kernel_impl.c の stack_probe_report()（実使用量）と対で、
 *  ESP_SHIM_TSK_STKSZ の妥当性を実測で判断するための診断コード。
 */
uint32_t	esp_shim_probe_req[ESP_SHIM_NUM_TSK];
const char	*esp_shim_probe_name[ESP_SHIM_NUM_TSK];

void
esp_shim_probe_dump(void)
{
	uint_t	i;

	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (esp_shim_probe_name[i] != NULL) {
			syslog(LOG_NOTICE, "STKPROBE slot%d '%s' req=%d pool=%d",
				   (int_t) i, esp_shim_probe_name[i],
				   (int_t) esp_shim_probe_req[i],
				   (int_t) ESP_SHIM_TSK_STKSZ);
		}
		else {
			syslog(LOG_NOTICE, "STKPROBE slot%d (未使用)", (int_t) i);
		}
	}
}
#endif /* TOPPERS_STACK_PROBE */

/*
 *  ★2026-09-02: 本体を _pinned 側へ移した。
 *
 *  従来はコア指定を受け取らず ESP_SHIM_TASK_NO_AFFINITY を直書きしていた。
 *  プリビルドの BlueDroid は xTaskCreatePinnedToCore() をコア指定つきで
 *  呼ぶため、その指定を捨てずに渡せる入口が要る。従来名は NO_AFFINITY
 *  固定のラッパとして残してあるので、Wi-Fi 側の挙動は変わらない。
 */
int32_t
esp_shim_task_create_pinned(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle,
					 uint32_t core_id)
{
	uint_t		i = ESP_SHIM_NUM_TSK;
	uint_t		slot = ESP_SHIM_NUM_TSK;
	SHIM_TSK	*t = NULL;
	ID			stale = 0;
	ID			tskid;

	if (stack_size > ESP_SHIM_TSK_STKSZ) {
		syslog(LOG_NOTICE, "esp_shim: task '%s' stack %u > pool %u",
			   name, (uint_t)stack_size, (uint_t)ESP_SHIM_TSK_STKSZ);
	}
	SHIM_LOCK();
#ifdef TOPPERS_BT_HOST_NIMBLE
	/*
	 *  index0（SHIM_TSK1）はesp_shim.cfgでESP_SHIM_BT_CTRL_TASK_PRI（高優先度、
	 *  SHIM_TIMER_TSKと同格）で静的生成された専用スロット（BT-4調査：
	 *  esp_shim_cfg.hのコメント参照）。BTコントローラ級タスク（freertos_prioが
	 *  ESP_SHIM_BT_CTRL_FREERTOS_PRIO_MIN以上、実測ではbtController=23のみ該当。
	 *  nimble_host=21は非該当で従来通り）はここを最優先で使い、通常タスクは
	 *  他に空きがある限りindex0を避けてコントローラ用に温存する。
	 */
	if ((uint_t)freertos_prio >= (uint_t)ESP_SHIM_BT_CTRL_FREERTOS_PRIO_MIN) {
		if (!shim_tsk[0].used) {
			i = 0U;
		}
		else {
			for (i = 1U; i < ESP_SHIM_NUM_TSK; i++) {
				if (!shim_tsk[i].used) {
					break;
				}
			}
		}
	}
	else {
		for (i = 1U; i < ESP_SHIM_NUM_TSK; i++) {
			if (!shim_tsk[i].used) {
				break;
			}
		}
		if (i == ESP_SHIM_NUM_TSK && !shim_tsk[0].used) {
			i = 0U;	/* 最後の手段：他が全て埋まっていればindex0も使う */
		}
	}
#else
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (!shim_tsk[i].used) {
			break;
		}
	}
#endif
	if (i < ESP_SHIM_NUM_TSK && !shim_tsk[i].used) {
		shim_tsk[i].used = true;
		/*  生成はロックの外で行う（acre_tsk はサービスコール）。 */
		slot = i;
		stale = shim_tsk[i].stale_tskid;
		shim_tsk[i].tskid = 0;
		shim_tsk[i].fn = entry;
		shim_tsk[i].arg = param;
		t = &shim_tsk[i];
#ifdef TOPPERS_STACK_PROBE
		/*  blobが要求したスタックサイズを記録する（ESP_SHIM_TSK_STKSZを
		 *  実測に基づいて縮小する際、「実使用量」だけでなく「blobの要求値」も
		 *  判断材料に要るため）。ここでsyslogすると起動直後でlogtask未起動の
		 *  ため取りこぼされるので、配列に
		 *  記録しておき esp_shim_probe_dump() で後から出力する。 */
		esp_shim_probe_req[i] = stack_size;
		esp_shim_probe_name[i] = name;
#endif /* TOPPERS_STACK_PROBE */
	}
	SHIM_UNLOCK();

	if (t == NULL) {
		syslog(LOG_ERROR, "esp_shim: task pool exhausted ('%s')", name);
		SHIM_DBG_STR("\r\n<<SHIM TSK POOL EXHAUSTED '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("'>>\r\n");
		return(0);
	}
	/*
	 *  前にこのスロットを使ったタスクが自分で終了していたら、まず回収する。
	 *  まだ休止していなければスタックを共有させられないので、あきらめる。
	 */
	if (stale != 0) {
		if (!esp_shim_tsk_reap(stale)) {
			{
				SHIM_LOCK();
				t->used = false;
				SHIM_UNLOCK();
			}
			syslog(LOG_ERROR,
				   "esp_shim: task '%s' slot %u still busy (tskid %d)",
				   name, (uint_t)slot, (int_t)stale);
			return(0);
		}
		{
			SHIM_LOCK();
			t->stale_tskid = 0;
			SHIM_UNLOCK();
		}
	}

	tskid = esp_shim_tsk_create(slot, esp_shim_task_entry, (EXINF)(intptr_t)slot,
								shim_tsk_slot_pri(slot));
	if (tskid == 0) {
		{
			SHIM_LOCK();
			t->used = false;
			SHIM_UNLOCK();
		}
		syslog(LOG_ERROR, "esp_shim: acre_tsk failed for task '%s'", name);
		SHIM_DBG_STR("<<SHIM acre_tsk FAIL '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("'>>\r\n");
		return(0);
	}
	{
		SHIM_LOCK();
		t->tskid = tskid;
		SHIM_UNLOCK();
	}

	syslog(LOG_NOTICE, "esp_shim: task '%s' -> tskid %d (prio %u)",
		   name, (int_t)t->tskid, (uint_t)freertos_prio);
	SHIM_DBG_STR("\r\n<<SHIM TSK '");
	SHIM_DBG_STR(name);
	SHIM_DBG_STR("' ss=");
	SHIM_DBG_U(stack_size);
	SHIM_DBG_STR(" heapfree=");
	SHIM_DBG_U(esp_shim_heap_free_size());
	SHIM_DBG_BTENV();
	SHIM_DBG_STR(">>\r\n");
	if (!esp_shim_tsk_activate(t->tskid, core_id)) {
		(void) esp_shim_tsk_terminate(t->tskid);
		{
			SHIM_LOCK();
			t->tskid = 0;
			t->used = false;
			SHIM_UNLOCK();
		}
		SHIM_DBG_STR("<<SHIM mact_tsk FAIL '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("'>>\r\n");
		return(0);
	}
	if (task_handle != NULL) {
		*task_handle = (void *)t;
	}
	return(1);
}

int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	return(esp_shim_task_create_pinned(entry, name, stack_size, param,
									   freertos_prio, task_handle,
									   ESP_SHIM_TASK_NO_AFFINITY));
}

void
esp_shim_task_delete(void *task_handle)
{
	SHIM_TSK	*t = (SHIM_TSK *)task_handle;
	ID			self;
	bool_t		deleted;

	(void) get_tid(&self);
	if (t == NULL || t->tskid == self) {
		/* 自タスクの終了 */
		SHIM_LOCK();
		{
			uint_t	i;
			for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
				if (shim_tsk[i].tskid == self) {
					/*  ext_tsk 後もオブジェクトは残る。次にこのスロットを
					 *  使うときに回収させる。 */
					shim_tsk[i].stale_tskid = self;
					shim_tsk[i].tskid = 0;
					shim_tsk[i].used = false;
				}
			}
		}
		SHIM_UNLOCK();
		ext_tsk();
		/* ここには戻らない */
	}
	/*
	 *  他タスクなら ter_tsk が同期的に休止させるので、その場で削除できる。
	 *  消せなかったらスロットを解放してはいけない（スタックを共有させない）。
	 */
	deleted = esp_shim_tsk_terminate(t->tskid);
	{
		SHIM_LOCK();
		if (deleted) {
			t->tskid = 0;
			t->stale_tskid = 0;
		}
		else {
			t->stale_tskid = t->tskid;
			t->tskid = 0;
		}
		t->used = false;
		SHIM_UNLOCK();
	}
}

void
esp_shim_task_delay(uint32_t tick)
{
	(void) dly_tsk((RELTIM)(tick * 1000U));
}

void *
esp_shim_task_get_current(void)
{
	ID		self;
	uint_t	i;

	(void) get_tid(&self);
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (shim_tsk[i].used && shim_tsk[i].tskid == self) {
			return((void *)&shim_tsk[i]);
		}
	}
	return((void *)&shim_main_thread_sem);	/* プール外タスクの代表 */
}

/*
 *  ★★2026-07-26: **golden を黙って変えていたので隔離した。**
 *
 *  この 101 行（task notification 実装）は 2026-07-25 の commit a7ecc9a で
 *  「音声のため」に**この**ファイルへ入った。しかし実測すると:
 *   ★**`A1_VARIANT=m5`（音声を積む唯一の構成）は `esp/shim/esp_shim.c` を
 *     リンクしていない**（`build.ninja` に参照 0 件）。音声が使うのは
 *     `m5/shim/m5_kernel_shim.c` 側の実装である。
 *   ★一方 `esp_shim.c` は **wifi×2・ble** がリンクする。
 *  ⇒ ★**使われない先で golden 3 本（s3-wifi/s3-ble/lx6-wifi）を変えていた。**
 *    実測: s3-ble は golden `b2d322d4…` に対し `d3cdd4b4…` になっていた。
 *    ★これは私が「golden に影響しうるなら sha を確認する」を**怠った**結果である。
 *
 *  ⇒ 既定で**コンパイルしない**ようにして golden を戻す。必要になったら
 *    `-DESP_SHIM_TASK_NOTIFY` を付ける（実装はそのまま残してある）。
 */
#if defined(ESP_SHIM_TASK_NOTIFY)
/*
 *  ★自タスクのプールスロット。プール外タスクなら NULL。
 *  ★`esp_shim_task_get_current()` はプール外だと `&shim_main_thread_sem` を返すので、
 *  それを SHIM_TSK として参照するとメモリを壊す。ここで必ず弾く。
 */
static SHIM_TSK *
shim_self_slot(void)
{
	ID		self;
	uint_t	i;

	if (get_tid(&self) != E_OK) {
		return(NULL);
	}
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (shim_tsk[i].used && shim_tsk[i].tskid == self) {
			return(&shim_tsk[i]);
		}
	}
	return(NULL);
}

/*
 *  ★`ulTaskNotifyTake(clear_on_exit, timeout)` 相当。
 *  通知値が >0 になるまで待ち、抜けるとき clear なら 0 クリア・でなければ 1 減算。
 *  **待つ前の値**を返す（FreeRTOS の意味）。timeout は ms、0xFFFFFFFF で無限待ち。
 *
 *  ★競合の扱い: 値の判定を割込み禁止で行い、**解除してから** `slp_tsk` する。
 *  その隙に `give` が来ても `wup_tsk` が**起床要求をラッチする**ので `slp_tsk` は
 *  即座に返る。2 回来て 2 回目が `E_QOVR` でも、**カウンタ側が両方記録している**。
 */
uint32_t
esp_shim_task_notify_take(int clear_on_exit, uint32_t timeout_ms)
{
	SHIM_TSK	*t = shim_self_slot();
	uint32_t	v;
	ER			er;

	if (t == NULL) {
		return(0U);		/* ★プール外タスクは通知値を持てない */
	}
	for (;;) {
		{
			SHIM_LOCK();
			v = t->notify;
			if (v > 0U) {
				t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
				SHIM_UNLOCK();
				return(v);
			}
			SHIM_UNLOCK();
		}
		if (timeout_ms == 0U) {
			return(0U);		/* ポーリング */
		}
		if (timeout_ms == 0xFFFFFFFFU) {
			er = slp_tsk();
		}
		else {
			er = tslp_tsk((RELTIM)(timeout_ms * 1000U));	/* ms → μs */
		}
		if (er == E_TMOUT) {
			SHIM_LOCK();
			v = t->notify;
			if (v > 0U) {
				t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
			}
			SHIM_UNLOCK();
			return(v);
		}
		/*  E_OK: 起こされた ⇒ ループして値を見る（偽起床にも耐える）  */
	}
}

/*
 *  ★`xTaskNotifyGive(handle)` 相当。対象タスクの通知値を +1 して起こす。
 *  ★`wup_tsk` の `E_QOVR`（既にラッチ済み）と `E_OBJ`（起きている）は正常。
 */
void
esp_shim_task_notify_give(void *task_handle)
{
	SHIM_TSK	*t = (SHIM_TSK *) task_handle;

	if ((t == NULL) || (t == (SHIM_TSK *)&shim_main_thread_sem)) {
		return;			/* ★プール外の代表ハンドルを参照しない */
	}
	SHIM_LOCK();
	t->notify++;
	SHIM_UNLOCK();
	(void) wup_tsk(t->tskid);
}
#endif /* ESP_SHIM_TASK_NOTIFY */

void
esp_shim_task_yield(void)
{
	(void) rot_rdq(TPRI_SELF);
}

/*
 *  スレッド毎セマフォ（_wifi_thread_semphr_get）
 */
void *
esp_shim_thread_semphr_get(void)
{
	void	*cur = esp_shim_task_get_current();

	if (cur == (void *)&shim_main_thread_sem) {
		if (shim_main_thread_sem == NULL) {
			shim_main_thread_sem = esp_shim_sem_create(1U, 0U);
		}
		return(shim_main_thread_sem);
	}
	else {
		SHIM_TSK	*t = (SHIM_TSK *)cur;
		if (t->thread_sem == NULL) {
			t->thread_sem = esp_shim_sem_create(1U, 0U);
		}
		return(t->thread_sem);
	}
}

/*
 *		ets_timer（タイマタスク＋期限ソートリスト）
 */
typedef struct shim_timer {
	struct shim_timer	*next;
	void				*key;			/* blob側のETSTimer* */
	void				(*fn)(void *);
	void				*arg;
	int64_t				deadline_us;	/* 0なら停止中 */
	uint32_t			period_us;		/* 0ならワンショット */
} SHIM_TIMER;

static SHIM_TIMER *shim_timer_list;		/* 全タイマ（生成順） */

static SHIM_TIMER *
shim_timer_find(void *key, bool_t create)
{
	SHIM_TIMER	*t;

	for (t = shim_timer_list; t != NULL; t = t->next) {
		if (t->key == key) {
			return(t);
		}
	}
	if (!create) {
		return(NULL);
	}
	t = (SHIM_TIMER *)esp_shim_calloc(1U, sizeof(SHIM_TIMER));
	if (t != NULL) {
		t->key = key;
		SHIM_LOCK();
		t->next = shim_timer_list;
		shim_timer_list = t;
		SHIM_UNLOCK();
	}
	return(t);
}

void
esp_shim_timer_setfn(void *ptimer, void (*pfunc)(void *), void *parg)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, true);

	if (t != NULL) {
		SHIM_LOCK();
		t->fn = pfunc;
		t->arg = parg;
		t->deadline_us = 0;
		SHIM_UNLOCK();
	}
}

void
esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, true);

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = esp_shim_time_us() + (int64_t)us;
		t->period_us = repeat ? us : 0U;
		SHIM_UNLOCK();
		(void) sig_sem(SHIM_TIMER_SEM);		/* タイマタスクの再計算 */
	}
}

void
esp_shim_timer_disarm(void *ptimer)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, false);

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = 0;
		SHIM_UNLOCK();
	}
}

void
esp_shim_timer_done(void *ptimer)
{
	SHIM_TIMER	*t;
	SHIM_TIMER	**pp;

	SHIM_LOCK();
	for (pp = &shim_timer_list; *pp != NULL; pp = &(*pp)->next) {
		if ((*pp)->key == ptimer) {
			t = *pp;
			*pp = t->next;
			SHIM_UNLOCK();
			esp_shim_free(t);
			return;
		}
	}
	SHIM_UNLOCK();
}

/*
 *  タイマタスク本体（esp_shim.cfgのCRE_TSKで生成・起動）
 */
void
esp_shim_timer_task(EXINF exinf)
{
	for (;;) {
		SHIM_TIMER	*t;
		int64_t		now = esp_shim_time_us();
		int64_t		next = 0;
		void		(*fn)(void *) = NULL;
		void		*arg = NULL;

		/*
		 *  期限到来タイマを1つ選ぶ（コールバックはロック外で実行）
		 */
		SHIM_LOCK();
		for (t = shim_timer_list; t != NULL; t = t->next) {
			if (t->deadline_us == 0) {
				continue;
			}
			if (t->deadline_us <= now) {
				fn = t->fn;
				arg = t->arg;
				if (t->period_us != 0U) {
					t->deadline_us = now + (int64_t)t->period_us;
				}
				else {
					t->deadline_us = 0;
				}
				break;
			}
			if (next == 0 || t->deadline_us < next) {
				next = t->deadline_us;
			}
		}
		SHIM_UNLOCK();

		if (fn != NULL) {
			fn(arg);
			continue;			/* 他の期限到来タイマを続けて処理 */
		}

		if (next == 0) {
			(void) twai_sem(SHIM_TIMER_SEM, TMO_FEVR);
		}
		else {
			int64_t wait = next - now;
			if (wait < 1000) {
				wait = 1000;
			}
			(void) twai_sem(SHIM_TIMER_SEM, (TMO)wait);
		}
	}
}

/*
 *		Wi-Fi割込みディスパッチ
 *
 *  blobは_set_intr（ソース→CPU割込み線のルーティング）と_set_isr
 *  （線番号へのハンドラ登録）を要求する．blobが指定する線番号を
 *  そのまま尊重し（1〜ESP_SHIM_MAX_WIFI_INTNO），cfgで静的に
 *  DEF_INHした共通入口から関数ポインタ表経由で呼び出す．
 */
static struct {
	void	(*fn)(void *);
	void	*arg;
} shim_isr_tbl[ESP_SHIM_MAX_WIFI_INTNO + 1];

void
esp_shim_set_isr(int32_t cpu_intno, void *handler, void *arg)
{
#ifndef TOPPERS_S3_BT_INTR_DIAG
	/*  ★BT割込みsource分離診断（TOPPERS_S3_BT_INTR_DIAG，esp/bt/bt_shim.c
	 *  参照）が有効なビルドでは，esp_intr_alloc()がBTコントローラ初期化
	 *  シーケンス中にesp_shim_set_isr()を呼ぶ．このsyslogは無条件発行のため，
	 *  診断ビルドでBT初期化を不安定化させないよう抑制する（既定ビルドの
	 *  挙動は変更しない＝非診断ビルドは従来通り出力する）。
	 *   §3-4参照。 */
	syslog(LOG_NOTICE, "esp_shim: set_isr intno=%d handler=%p",
		   (int_t)cpu_intno, handler);
#endif
	if (cpu_intno >= 0 && cpu_intno <= ESP_SHIM_MAX_WIFI_INTNO) {
		SHIM_LOCK();
		shim_isr_tbl[cpu_intno].fn = (void (*)(void *))handler;
		shim_isr_tbl[cpu_intno].arg = arg;
		SHIM_UNLOCK();
	}
	else {
		syslog(LOG_ERROR, "esp_shim: set_isr intno %d out of range",
			   (int_t)cpu_intno);
	}
}

volatile uint32_t esp_shim_int_count[ESP_SHIM_MAX_WIFI_INTNO + 1];

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	/*  BTコントローラのLevel-3線（23/27）はISR実行時間と発火間隔も計測する
	 *  （BT-4診断。ISRホットパスのためsyslog禁止＝カウンタ蓄積のみ）。  */
	if (intno == 23 || intno == 27) {
		int			idx = (intno == 27) ? 1 : 0;
		uint32_t	t0 = l3ld_ccount();
		uint32_t	d;

		if (l3ld_isr_last[idx] != 0U) {
			d = t0 - l3ld_isr_last[idx];
			if (d > l3ld_isr_gap_max[idx]) {
				l3ld_isr_gap_max[idx] = d;
			}
		}
		l3ld_isr_last[idx] = t0 | 1U;
		if (shim_isr_tbl[intno].fn != NULL) {
			shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
		}
		d = l3ld_ccount() - t0;
		l3ld_isr_dur_sum[idx] += d;
		if (d > l3ld_isr_dur_max[idx]) {
			l3ld_isr_dur_max[idx] = d;
		}
		return;
	}
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */
	if (shim_isr_tbl[intno].fn != NULL) {
		shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
	}
}

/*
 *  cfg（esp_shim.cfg）でDEF_INHする入口（blobが使う線の分だけ用意）
 */
void esp_shim_inthdr_0(void) { shim_int_dispatch(0); }
void esp_shim_inthdr_1(void) { shim_int_dispatch(1); }
void esp_shim_inthdr_2(void) { shim_int_dispatch(2); }
void esp_shim_inthdr_3(void) { shim_int_dispatch(3); }

/*
 *  無印ESP32 BTコントローラblob（libbtdm_app.a）がxt_set_interrupt_handler()
 *  経由で動的に使うCPU割込み線5・7・8用（2026-07-15、W3調査で実機トレース
 *  （esp_shim: set_isr intno=5/7/8）から確認）。esp_shim_inthdr_0〜3と
 *  同じ仕組み（shim_isr_tbl[]経由のディスパッチ）をそのまま流用する。
 *  従来はこれらの線にDEF_INH登録が無く、shim_isr_tbl[5/7/8]への登録は
 *  カーネルから一切呼ばれないデッドコードだった（未修正時、
 *  UART0がINT5を静的に占有していたため実害が表面化していなかった。
 *   追記2④参照）。
 *
 *  ★S3非回帰ガード：esp_shim.cfgの線5/7/8ブロックと対を成す。
 *  ESP32-S3ではこれらのDEF_INH登録を除外する（線5がUSART_INTNOと衝突）ため、
 *  ここも同じTOPPERS_ESP32_LX6ガードで囲み、S3では未使用関数warningを避ける。
 */
#ifdef TOPPERS_ESP32_LX6
void esp_shim_inthdr_5(void) { shim_int_dispatch(5); }
void esp_shim_inthdr_7(void) { shim_int_dispatch(7); }
void esp_shim_inthdr_8(void) { shim_int_dispatch(8); }
#endif

/*
 *  BTコントローラのLevel-3割込み用（BT-4調査、bt_shim.cのesp_intr_alloc()が
 *  ESP_INTR_FLAG_LEVEL3要求時に配線するCPU割込み線23・27）。
 *  esp_shim_inthdr_0〜3と同じ仕組み（shim_isr_tbl[]経由のディスパッチ）を
 *  そのまま流用する。_kernel_l3int_dispatch（target_timer.c）から
 *  DEF_INHテーブル（esp_shim.cfg）経由で呼ばれる点のみLevel-1用と異なる。
 */
void esp_shim_inthdr_23(void) { shim_int_dispatch(23); }
void esp_shim_inthdr_27(void) { shim_int_dispatch(27); }
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
/*  CPU 割込み 29 = Xtensa SW1。ESP32 の BT コントローラが
 *  ETS_INTERNAL_SW1_INTR_SOURCE として使う。 */
void esp_shim_inthdr_29(void) { shim_int_dispatch(29); }
#endif

/*
 *		初期化
 */
void
esp_shim_initialize(void)
{
	static bool_t initialized = false;

	if (!initialized) {
		initialized = true;
		heap_initialize();
		(void) act_tsk(SHIM_TIMER_TSK);

		/*
		 *  PSA Crypto初期化．
		 *
		 *  esp_supplicant/crypto_mbedtls.cのhmac_vector()（PTK/MIC
		 *  導出のHMAC-SHA1等で使用）はPSA Crypto API（psa_import_key
		 *  /psa_mac_sign_setup等）を直接呼ぶ．本来はESP-IDF起動シーケ
		 *  ンス（esp_system_startup.cのSECONDARY初期化，優先度104＝
		 *  mbedtls/port/esp_psa_crypto_init.cのESP_SYSTEM_INIT_FN経由）
		 *  でpsa_crypto_init()が自動的に呼ばれるが，本ポートはDirect
		 *  Boot（ESP-IDF起動シーケンス非経由）のためこの初期化が走ら
		 *  ない．未初期化のままPSA API群を呼ぶと全て失敗し（PBKDF2は
		 *  レガシーmbedtls_md経路のため無関係で正常動作するが，PTK
		 *  導出のsha1_prf→hmac_sha1_vector→hmac_vectorはPSA経由のため
		 *  全滅），呼び出し元（sha1_prf等）は戻り値未チェックのため
		 *  ptk->kck/kek/tkに未初期化のスタック内容（ポインタ値等）が
		 *  そのまま書き込まれる．結果，STAが送るmsg2のMICが常に不正
		 *  となりAPがmsg1を再送し続ける（4-wayハンドシェイクタイム
		 *  アウト，reason=15）．実機JTAGでptk->kck/kek/tkの中身が
		 *  ポインタらしき値（sm->snonceやsrc_addr等のアドレス）である
		 *  ことを確認して特定．
		 *
		 *  WiFi初期化前（esp_wifi_init呼び出し前）に一度だけ呼ぶ．
		 *
		 *  Bluetooth単体ビルド（ESP32C3_WIFI=OFF）はmbedtls/PSA Cryptoを
		 *  リンクしないため，この初期化自体が不要（WPA2固有の問題）．
		 */
#ifdef TOPPERS_ESP_WIFI_WPA2
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP_WIFI_WPA2 */
	}
}


#if defined(ESP_SHIM_HEAP_STATS)
/*
 *  ★★高水位を**外から読む口**。
 *  ★これが無かったので、記録していた時代ですら誰も値を見られなかった。
 *  「計測しているつもり」で終わらせないために、**読み出しと総量をセットで出す**。
 *
 *  使い方（無線が動く板で 1 回走らせるだけでよい）:
 *      syslog_2(LOG_NOTICE, "[SHIM-HEAP] 総量=%u ピーク使用=%u",
 *               esp_shim_heap_total(), esp_shim_heap_peak_used());
 *  ★`ESP_SHIM_HEAP_SIZE` を削れるかは、この 2 つの数字だけで決まる。
 */
size_t	esp_shim_heap_total(void);
size_t
esp_shim_heap_total(void)
{
	return(sizeof(heap_area));
}

size_t	esp_shim_heap_peak_used(void);
size_t
esp_shim_heap_peak_used(void)
{
	/*  ★一度も malloc されていなければ min_free は初期値のまま。
	 *  その場合は 0 ではなく **総量を返さない**——「測れていない」ことが
	 *  分かるように (size_t)-1 をそのまま外へ出す。
	 *  ★0 を返すと「使っていない」と読まれる。 */
	if (shim_heap_min_free == (size_t)-1) {
		return((size_t)-1);
	}
	return(sizeof(heap_area) - shim_heap_min_free);
}
#endif /* ESP_SHIM_HEAP_STATS */
