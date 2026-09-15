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
 *    - セマフォ：CRE_SEMプールから割当て
 *    - ミューテックス：動的生成（acre_mtx/del_mtx）．実体は esp_shim_mtx.c
 *      （2026-08-04・段1 でCRE_MTXプールを廃止）
 *    - キュー：**この TU からは撤去した**（2026-08-15・BL-G-3 段3）。
 *      写像先は `esp/shim/esp_shim_ring.c`（シム所有リング・ISR 安全）。
 *      旧実装は CRE_DTQ プール＋ヒープ確保 item だった（下の撤去の注記参照）
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
/*  ISR 文脈からの sem take/give の 3 値 API（`esp_shim_sem_*_from_isr`）。
 *  2026-08-15（段3）: キュー側の 3 値 API（`esp_shim_queue_recv_from_isr` など）は
 *  DTQ 実装の撤去と同時に消えた。残っているのは sem 側だけである。 */
#include "esp_shim_isr_ctx.h"
#ifdef M5_SHIM_AUDIO
#include "esp_shim_audio_names.h"	/* 音声 worker タスク名の単一真実源（C-2-1） */
#endif
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
 *  BT-4診断計装（TOPPERS_S3_BT_L3LAT_DIAG、既定OFF）：
 *  「rsil>=3マスク窓（PS.INTLEVEL>=3でBTのLevel-3割込みが遅延する区間）」
 *  の滞在時間をCCOUNTで実測する。BLE接続イベントのRX窓は150μs(T_IFS)〜
 *  数百μsオーダーであり、80MHzで1.2万cycle（150μs）超のマスク窓が
 *  頻発していれば「ISRは発火するが応答が遅すぎてRX窓を外す」仮説(b)が
 *  確定する（.steering/20260709-ble-bt4-connection/steering.md §11.3）。
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
#if defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* C5 も RISC-V mstatus.MIE（段4 Task 3） */
/*
 *  ESP32-C6（RISC-V）: mstatus.MIE（bit3）を落とし、旧 mstatus を返す。
 *  復元は旧値の MIE ビットだけを見て csrsi する（他のビットは触らない）。
 *  出典: asp3_esp_idf esp/c6/wifi/esp_shim.c:76-96（esp_shim_enter/exit_critical）。
 *  asp3 はネストカウンタを持つが、本ポートの契約は S3 版と同じ
 *  「旧状態を返し、呼出し側が対で restore する」（save/restore 型）なので
 *  カウンタは要らない。段4 Task 3（2026-09-14）で C6 分岐として追加。
 *  S3/LX6 の #else 側は無改変。
 */
uint32_t
esp_shim_int_disable(void)
{
	uint32_t state;

	Asm("csrrci %0, mstatus, 8" : "=r"(state) :: "memory");
	return(state);
}

void
esp_shim_int_restore(uint32_t state)
{
	if ((state & 8U) != 0U) {
		Asm("csrsi mstatus, 8" ::: "memory");
	}
}
#else /* !TOPPERS_ESP32C6: S3/LX6（Xtensa） */
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
#endif /* TOPPERS_ESP32C6 */

/*
 *  SMP注意：SHIM_LOCK/UNLOCKは自コアのPS.INTLEVELマスクのみで、
 *  コア間の相互排他は一切提供しない（esp_shim_malloc/free等のヒープ
 *  臨界区間はコア間非安全）。現状は「BT/WiFi/esp_shim関連タスクは
 *  全てCLS_ALL_PRC1/CLS_PRC1（コア0）に固定する」という運用規約
 *  （esp/bt/bt.cfg・esp_shim.cfg・各アプリcfg参照）でのみ安全性が
 *  成立している——コード上で強制されていない前提であり、将来この
 *  規約を破ってesp_shim_malloc/free呼び出し元をコア1へ動かす場合は
 *  クロスコアスピンロックの追加が必須（P4側`fmp3_newlib_locks.c`の
 *  AMOスピンロック実装参照）。
 */
/*
 *  2026-08-05（段12）: **クロスコア排他の opt-in**
 *
 *  上のコメントが「将来この規約を破る場合はクロスコアスピンロックの追加が
 *  必須」と書いていたもの。`ESP_SHIM_XCORE_LOCK` かつ `TNUM_PRCID >= 2` の
 *  ときだけ、自コアの割込み禁止に**加えて** CAS スピンロックを取る。
 *
 *  2 段ゲートである理由: 既定 OFF なら 10 構成すべてが 1 バイトも変わらない
 *  （golden。段10・段11 と同じ作法）。`TNUM_PRCID` も見るのは、1 コアでは
 *  CAS が純粋な無駄だからである。
 *
 *  順序（自コア閉じる → CAS）が `chip_kernel_impl.h` の `core_cas` の
 *  不変条件 (a)（`PS.INTLEVEL=15` の下でのみ CAS を呼ぶ。SCOMPARE1 を
 *  退避していないため）を自然に満たす。段12 の判定6 で命令列を確認した。
 *
 *  再入深度を持たない理由: `SHIM_LOCK` の入れ子は**実在しない**
 *  （段12 で機械照合: 45 区間すべてネスト 0・区間内 return 0・区間内から
 *  呼ばれる関数はいずれも `SHIM_LOCK` を取らない）。深度を足すのは
 *  「将来入れ子になるかもしれない」という**推測**への備えになってしまう。
 *  ⇒ 入れ子を作るなら、そのとき深度も足すこと。
 *
 *  フェーズ1 1-1（2026-08-06）: **配線側（CMake の対象列挙）を信用しない
 *  fail-closed 検査。**
 *  【何が起きていたか】合成構成（`A1_M5_WIFI`）では本ファイルは `seam_objs` では
 *  なく別 OBJECT lib `seam_wifi_objs` へ入る（CMakeLists.txt の `_seam_wifi_srcs`）。
 *  `ESP_SHIM_XCORE_LOCK` は `target_compile_definitions(seam_objs ...)` にしか
 *  無く、`seam_wifi_objs` には一度も配られていない。段15 が入れた
 *  「2 コアなのに opt-in が OFF なら警告」は `A1_SHIM_XCORE_LOCK` が
 *  （2 コアなら既定 ON なので）ON のまま鳴らず、フラグは ON・警告なし・
 *  実際のロックは無し、という**配線側からは検出できない**状態になっていた。
 *  実測: `-DA1_SMP_EXPERIMENT=ON` で `seam-s3-m5-wifi` を建てると、本ファイルが
 *  `seam_wifi_objs` に入り `ESP_SHIM_XCORE_LOCK` を持たないまま**黙ってリンクが
 *  通る**（`nm` で `shim_xcore_*` シンボルが 0 件）。
 *  ⇒ 配線側の対象列挙をこれ以上増やさず、**ソース側で「2 コアなのに排他が
 *  無効」を検査する**（§10 規則2）。
 */
#ifndef TNUM_PRCID
#error "esp_shim.c: TNUM_PRCID が見えていない（クロスコア排他の要否を検査できない）"
#endif
#if (TNUM_PRCID >= 2) && !defined(ESP_SHIM_XCORE_LOCK) && !defined(ESP_SHIM_ALLOW_XCORE_UNSAFE)
#error "esp_shim.c: 2コアなのにESP_SHIM_XCORE_LOCKが無効（SHIM_LOCKがクロスコア非安全のまま）。NC/意図的ならESP_SHIM_ALLOW_XCORE_UNSAFEを渡すこと"
#endif
#if defined(ESP_SHIM_XCORE_LOCK) && (TNUM_PRCID >= 2)
#include <xtensa_cas.h>		/* 2026-08-06（フェーズ1 1-3）: CAS実体を1本化 */

static volatile uint32_t	shim_xcore_word;	/*  0=空き / 保持者 id  */

Inline bool_t
shim_xcore_cas(volatile uint32_t *p, uint32_t cmp, uint32_t newv)
{
	return xtensa_core_cas(p, cmp, newv);
}

Inline uint32_t
shim_xcore_acquire(void)
{
	uint32_t	ps, me;

	Asm("rsr.prid %0" : "=a"(me));
	me |= 1U;						/*  0 は「空き」なので必ず非 0 にする  */
	ps = esp_shim_int_disable();	/*  先に自コアを閉じる（不変条件 (a)）  */
	while (!shim_xcore_cas(&shim_xcore_word, 0U, me)) {
		/*  他コアが保持中。保持側は割込みを閉じているので待ちは有界  */
	}
	return(ps);
}

Inline void
shim_xcore_release(uint32_t ps)
{
	Asm("memw" ::: "memory");
	shim_xcore_word = 0U;
	esp_shim_int_restore(ps);
}

#define SHIM_LOCK()		uint32_t shim_lock_ = shim_xcore_acquire()
#define SHIM_UNLOCK()	shim_xcore_release(shim_lock_)

#else /* クロスコア排他なし（既定。1 コア構成ではこれで足りる） */

#define SHIM_LOCK()		uint32_t shim_lock_ = esp_shim_int_disable()
#define SHIM_UNLOCK()	esp_shim_int_restore(shim_lock_)

#endif

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
	/*
	 *  S3移植：ESP32-S3はXtensa CCOUNTベースのHRT(us)を使う．
	 *
	 *  2026-07-27（レビュー指摘 esp-1）：ここは以前
	 *      return((int64_t) target_hrt_get_current());
	 *    と書いていたが，target_hrt_get_current() は FMP3 の HRTCNT 契約に
	 *    従う **32bit** 値で 2^32us≒**71.6分**でラップする。それを int64 へ
	 *    ゼロ拡張して返していたため，本関数の契約（ESP-IDF の
	 *    esp_timer_get_time() 相当＝**単調増加する64bitマイクロ秒**）を
	 *    71.6分ごとに破っていた。帰結：
	 *      - one-shot タイマの期限比較が巻き戻り，発火が最大71分遅延する
	 *      - lwIP の sys_now() が逆行し，再送/keepalive のタイマが壊れる
	 *    64bit 累積器は target_timer.h に元々あるので，切り詰めない
	 *    target_hrt_get_current64() へ切り替えるだけで解消する
	 *    （累積器の更新手順は32bit版と同一。詳細は target_timer.h の
	 *    target_hrt_get_current64() のコメント）。
	 */
	return(target_hrt_get_current64());
}

uint32_t
esp_shim_random(void)
{
	/*
	 *  HW乱数生成器（WDEV_RND_REG）．無線が有効になるとRFノイズ由来の
	 *  真性乱数になる（無効時はエントロピー低）．
	 *
	 *  ESP32-S3の正しいアドレスは WDEV_RND_REG = 0x6003507C
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
	 *  チップ差：WDEV_RND_REGのアドレスがS3と無印ESP32で異なる．
	 *   - ESP32-S3 : 0x6003507C（soc/esp32s3/wdev_reg.h）
	 *   - 無印ESP32 : 0x60035144（soc/esp32/wdev_reg.h．AHBバス表現．
	 *     DPORTバス別名では0x3FF75144＝同一レジスタ．esp_random()が読む値）
	 *  誤ると常時0/定数→SNonce不正でWPA2 4-way失敗（reason=15）．
	 */
	/* TOPPERS_ESP32_LX6/ESP32S3 は chip_stddef.h（kernel.h経由で必ず可視）で定義．
	 * esp_shim.c は sdkconfig.h を include しないため CONFIG_IDF_TARGET_* は使えない． */
#if defined(TOPPERS_ESP32_LX6)
#define SHIM_WDEV_RND_REG	((void *) 0x60035144U)	/* WDEV_RND_REG (無印ESP32) */
#elif defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* 番地は esp_shim.h 末尾（C6 +0x8 / C5 +0x28） */
/*  ESP32-C6: WDEV_RND_REG = LPPERI_RNG_DATA_REG = DR_REG_LPPERI_BASE(0x600B2800)+0x8
 *  （esp-idf soc/esp32c6/register/soc/lpperi_reg.h:139、soc/esp32c6/include/soc/
 *  wdev_reg.h:13）。出典: asp3 esp/c6/wifi/esp_shim_chip_regs.h（ESP_SHIM_WDEV_RND_REG）。
 *  段4 Task 3（2026-09-14）。  */
#define SHIM_WDEV_RND_REG	((void *) ESP_SHIM_RISCV_WDEV_RND_REG)	/* WDEV_RND_REG (ESP32-C6: 0x600B2808 / C5: 0x600B2828) */
#else
#define SHIM_WDEV_RND_REG	((void *) 0x6003507CU)	/* WDEV_RND_REG (ESP32-S3) */
#endif

#ifdef ESP_SHIM_RNG_RATELIMIT
	/*
	 *  LM-D-3（候補R・2026-08-08）: 純正と同じ「読み出し間隔の下限」を課す。
	 *  **既定では立たない**（`A1_RNG_RATELIMIT=ON` のときだけ）。golden 非影響。
	 *
	 *  なぜ要るか（純正との差＝実測）:
	 *    WDEV_RND_REG が実装する PRNG は、APB クロックごとに 2 bit ずつしか
	 *    エントロピーを補充されない。純正 `esp_random()`
	 *    （esp-idf/components/esp_hw_support/hw_random.c:51-93）は
	 *    **前回読み出しから `APB_CYCLE_WAIT_NUM` APB サイクル待ち、待つ間の
	 *    読み値を XOR で蓄積する**。この待ち量はチップごとに違い、
	 *      ESP32-S3 = 1778（同 :33。「45KHz が S3 で試した上限」）
	 *      無印ESP32 = 16（同 :47-49 の既定）
	 *    である。本ポートは**待ちも XOR 蓄積も無い生読み 1 発**だったため、
	 *    S3 では純正の 111 分の 1 の間隔で汲み出しうる＝エントロピーが
	 *    枯れた値（直前と相関した値／同一値）を返しうる。
	 *    **LX6 で問題が出ないのは、要求される待ちが 16 APB サイクル
	 *    （80MHz で 200ns）しかなく、関数呼び出しのオーバヘッドだけで
	 *    自然に満たされてしまうからである。**
	 *
	 *  何を疑っているか（未確定）:
	 *    再接続時のリンク層暗号化開始で、スレーブ（本機）は SKDs/IVs を
	 *    生成して LL_ENC_RSP で送る。この生成が RNG に依存するため、
	 *    枯れた RNG が MIC 不一致に効いている可能性がある。
	 *    **ただし「送った値」と「鍵計算に使った値」が食い違う機序は
	 *    まだ示せていない**（送信値は相手にも渡るので、単に質が低いだけ
	 *    なら MIC は合うはず）。⇒ 本フラグは**仮説を潰すための実験装置**で
	 *    あって、修正として提案しているのではない。
	 *
	 *  移植元との対応（S3 は SOC_LP_TIMER_SUPPORTED 未定義なので単純ループ枝）:
	 *    cpu_to_apb_freq_ratio = esp_clk_cpu_freq()/esp_clk_apb_freq()
	 *      → 本ポートでは TCYC_PER_HRT（＝CPU MHz、target_timer.h:356）を
	 *        APB 80MHz で割って求める。CPU 周波数はビルド時に決まる
	 *        （TOPPERS_S3_CPU_FREQ_MHZ、既定 160）。
	 *    esp_cpu_get_cycle_count() → xtensa_get_ccount()（同 :161）
	 *
	 *  `last_ccount` は関数内 static。CCOUNT はコアごとに独立なので
	 *  2 コア構成では意味が薄れるが、**本経路が生きるのは TNUM_PRCID=1 の
	 *  ble 構成**であり、かつ純正も同じ形（ファイル内 static）である。
	 */
	{
		static uint32_t	last_ccount;
		uint32_t		ccount;
		uint32_t		result = 0U;
		const uint32_t	ratio = (TCYC_PER_HRT + 79U) / 80U;	/* CPU/APB。切上げ */
		const uint32_t	wait_cyc = 1778U * ((ratio == 0U) ? 1U : ratio);

		do {
			ccount = xtensa_get_ccount();
			result ^= sil_rew_mem(SHIM_WDEV_RND_REG);
		} while ((ccount - last_ccount) < wait_cyc);
		last_ccount = ccount;
		return(result ^ sil_rew_mem(SHIM_WDEV_RND_REG));
	}
#else /* ESP_SHIM_RNG_RATELIMIT */
	return(sil_rew_mem(SHIM_WDEV_RND_REG));
#endif /* ESP_SHIM_RNG_RATELIMIT */
}

/*
 *  ログ（blobの_log_write系・lwIPのLWIP_PLATFORM_DIAG/ASSERT等，
 *  printf系を持たない呼出し元の共通折返し先）
 */
void
esp_shim_log_write(const char *format, ...)
{
	char	buf[128];
	va_list	args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
#ifdef M5_A4_HANGDIAG
	/*
	 *  2026-08-14 BL-H-8: lwIP の `LWIP_PLATFORM_ASSERT`（port/include/arch/cc.h）は
	 *  ここへ 1 行出してから `for(;;) {}` へ落ちる。ところが本関数は syslog＝
	 *  logtask 経由であり、**LX6 の m5+wifi 構成では実測で 1 行も線に出ない**
	 *  ため、assert が起きても痕跡が残らない（「NET_TSK が CPU を離さない」
	 *  という症状だけが見える）。assert 行だけ同期出力へ回す。
	 */
	if ((buf[0] == 'l') && (buf[1] == 'w') && (buf[2] == 'i')
		&& (buf[3] == 'p') && (buf[4] == ':')) {
		extern void m5_log_now(const char *msg);
		m5_log_now(buf);
	}
#endif
	syslog(LOG_NOTICE, "%s", buf);
}

/*
 *  esp_shim_log_emerg / esp_shim_get_core_id の実装は
 *  m5/compat/esp_shim_m5_ext.c にある（本ファイルではない）。
 *
 *  理由（重要）：本ファイル esp_shim.c は Wi-Fi/BLE golden 構成の
 *  SEAM_OBJS に直接コンパイルされる。このビルドは未使用シンボルを
 *  dead-code-eliminationで除去しない（実測：--gc-sections 非適用）ため，
 *  本ファイルへ関数を1つ追加するだけで，その関数が実際には呼ばれなくても
 *  golden app_xip.bin のバイト同一性が壊れる（実測：seam-s3-ble で確認）。
 *  M5専用の折返し関数はSEAM_OBJSに含まれない別ファイルへ置き，宣言だけを
 *  esp_shim.h/esp_shim_public.h に足す（宣言はコードを生成しないため
 *  golden に影響しない）。
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
#ifdef M5_USE_ESP_SHIM
/*  明示初期化より前に来た確保の回数（0 であるべき。詳細は esp_shim_malloc）。 */
static uint32_t	esp_shim_heap_early_alloc_n;
#endif

static void
heap_initialize(void)
{
	heap_top = (HEAP_BLOCK *)heap_area;
	heap_top->size = sizeof(heap_area);
	heap_top->next = NULL;
	heap_free_total = sizeof(heap_area);
}

#ifdef M5_USE_ESP_SHIM
/*
 *  冪等なヒープ初期化（2026-07-28・段階1(a) の実機 run で顕在化した欠陥への対処）
 *
 *  【何が起きたか（実機・JTAG で確定）】合成構成では
 *    ・設計上「**m5 の AC を全部終えてから** Wi-Fi を起こす」（時間で分ける）
 *    ・設計 (H-β) で「m5 の自前ヒープを畳んで `heap_area` に一本化」
 *  の 2 つを同時に採った。**単体ではどちらも正しい。**
 *  しかし `heap_initialize()` の呼出しは `esp_shim_initialize()` の中にしか無く、
 *  それは `wifi_sta_run()` の中＝**m5 の後**である。
 *  ⇒ m5 が `operator new` を使う時点で `heap_top == NULL` のままで、
 *    `esp_shim_malloc()` が NULL を返し `cxx_alloc()` が `abort()` した。
 *    実機 JTAG: `heap_top=0x0` / `heap_free_total=0` / `sizeof(heap_area)=0x14000`。
 *
 *  **教訓**: リンクが通っても**初期化順序はリンカが検査しない**。
 *
 *  【なぜ「冪等」でなければならないか — これを外すと更に悪い】
 *  m5 が先に初期化した後で `esp_shim_initialize()` が素の `heap_initialize()` を
 *  呼ぶと、**ヒープが作り直されて m5 の確保済みブロック（M5GFX の内部バッファ等）が
 *  一斉に迷子になる**。use-after-free と同じ壊れ方をし、しかも静かである。
 *  ⇒ **2 回目以降は何もしない**ことが必須。
 *
 *  `esp_shim_initialize()` 側もこの関数を呼ぶ（`#ifdef M5_USE_ESP_SHIM` で切替）。
 *  golden 6 構成（本マクロ未定義）では 1 バイトも変わらない。
 */
void	esp_shim_heap_init_once(void);
void
esp_shim_heap_init_once(void)
{
	if (heap_top == NULL) {
		heap_initialize();
	}
}

/*
 *  明示初期化より前に確保が来た回数を読む口（タスク文脈から呼ぶこと）。
 *  **0 が期待値**である。0 でなければ、その分だけ「誰かが我々より先に確保している」
 *  ＝初期化順序の設計が破れている。自己回復しているので落ちはしないが、
 *  **落ちないことと正しいことは別**である。
 */
uint32_t	esp_shim_heap_early_alloc_count(void);
uint32_t
esp_shim_heap_early_alloc_count(void)
{
	return(esp_shim_heap_early_alloc_n);
}
#endif /* M5_USE_ESP_SHIM */

/*
 *  W3④診断計装（同期マーカ、CLASSIC限定）：malloc失敗/タスク生成を
 *  target_fput_log（ポーリングUART0、logtaskを経由しない同期出力）で
 *  出力する。BTU_TASK未生成の直接原因（osi_thread_createのosi_malloc失敗
 *  か task pool枯渇か）を、logtask取りこぼしに影響されず確定するため。
 *  #ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC でCLASSICビルドのみ有効化
 *  （共有ファイルだがS3/W1/W2ビルドは本マクロ未定義＝無効＝非回帰）。
 */
/*
 *  2026-07-28（段階1(a)）: `shim_heap_min_free` の宣言を
 *  `ESP_SHIM_HEAP_STATS` でも有効にする。
 *
 *  【なぜ】更新側（下の `#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC) ||
 *  defined(ESP_SHIM_HEAP_STATS)`）と読み出し側（`esp_shim_heap_peak_used()`）は
 *  既に `ESP_SHIM_HEAP_STATS` を見ているのに、**宣言だけが BlueDroid 限定のまま**
 *  だった。⇒ `-DESP_SHIM_HEAP_STATS` を付けると
 *  `error: 'shim_heap_min_free' undeclared` で**コンパイルが通らない**（実測）。
 *  つまりこの読み出し口は**一度も使えたことがない**。
 *  記録（.steering/20260726-cores3-wireless-psram §8 限界 2）が
 *  「読み出し口を足した（既定 OFF）」と書いていたものは、**動かない状態だった**。
 *  既定（両マクロとも未定義＝golden 6 構成）では 1 バイトも変わらない。
 */
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC) || defined(ESP_SHIM_HEAP_STATS)
static size_t	shim_heap_min_free = (size_t)-1;	/* 空き最小（高水位） */
#endif

#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
extern void target_fput_log(char c);

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

#ifdef M5_USE_ESP_SHIM
	/*
	 *  未初期化での確保を**黙って NULL にしない**（2026-07-28）。
	 *  【なぜ】段階1(a) の欠陥は「NULL → cxx_alloc が abort() → `[HB]` だけが
	 *  出続ける無音停止」で現れ、**ログからは原因が全く分からなかった**
	 *  （JTAG が要った）。⇒ 同じことが二度起きたらログだけで分かるようにする。
	 *
	 *  **ここでは印字しない**（2026-07-28・段階2 で判明した制約）。
	 *  この経路は `software_init_hook`（`esp_mmu_map_init()` が `heap_caps_calloc` を
	 *  呼ぶ＝実測）からも来る。**その時点ではコンソール(USJ)が未初期化**なので、
	 *  ここで `target_fput_log` を回すと**出力が失われるか、最悪ブロックする**。
	 *  ⇒ **`.bss` のカウンタに記録するだけ**にし、報告はタスク文脈から行う
	 *    （`esp_shim_heap_early_alloc_count()`）。
	 *  `.bss` に置けるのは、この経路が **BSS クリアより後**（`software_init_hook`）
	 *    だからである（part5c/5d で確定済み）。
	 *
	 *  自己回復もする——ここで初期化すれば実害は消える。ただし**黙って直さない**：
	 *    カウンタが 0 でなければ「初期化順序の設計が破れている」ので、
	 *    **0 になるように直すこと**（自己回復を当てにしない）。
	 */
	if (heap_top == NULL) {
		esp_shim_heap_early_alloc_n++;
		heap_initialize();
	}
#endif /* M5_USE_ESP_SHIM */

	if (size == 0U) {
		size = 1U;
	}
	need = HB_ALIGN(size) + sizeof(HEAP_BLOCK);

	SHIM_LOCK();
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
 *  2026-07-26: 高水位の記録が **BT Classic のときしか動いていなかった**。
 *  BT Classic は 2026-07-20 に全廃されたので、**現在の木では一度も記録されない**。
 *  しかも `shim_heap_min_free` を**外へ出す口が無い**（誰もログに出していない）。
 *  ⇒ **`ESP_SHIM_HEAP_SIZE = 124KB` は、現在の木では実測の裏づけを持たない数字**である。
 *
 *  【なぜ今これが要るか】CoreS3 で無線を動かすには DRAM が **252,839 バイト足りず**
 *  （`.steering/20260726-cores3-wireless-psram/README.md`）、`heap_area`(126,976) は
 *  その最大の削り代である。削って良いかは**使用量を測らなければ決められない**。
 *
 *  既定は OFF にしてある——golden 5 構成（wifi×2・ble）を**バイト単位で変えない**ため。
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
 *		セマフォ — **生成／削除は別 TU（esp/shim/esp_shim_sem.c）へ移した**
 *
 *  2026-08-04（段2）: 静的プール（CRE_SEM(SHIM_SEM1..96) ＋ shim_sem_id[] /
 *  shim_sem_used[] の空きスロット探索）を廃し、カーネルの動的生成
 *  （acre_sem/del_sem）へ無条件で移した。設計判断・ハンドル表現の注意点は
 *  `esp/shim/esp_shim_sem.c` の冒頭コメントが正本（ここには写さない——
 *  同じ事実を 2 箇所に持つと片方が腐る）。記録: `.steering/20260804-dcre-stage2-sem/`
 *
 *  下の take/give/get_count は**本段で 1 行も変えていない**。プール管理を
 *  含まず、ハンドル（＝セマフォ ID）をサービスコールへ渡すだけであり、
 *  シム自身の静的セマフォ（SHIM_QSEM* 等）にも同じ関数が使われるためである。
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
 *		ミューテックス — **別 TU（esp/shim/esp_shim_mtx.c）へ移した**
 *
 *  2026-08-04（段1）: 静的プール（CRE_MTX ×20 ＋ shim_mtx[] の空きスロット
 *  探索）を廃し、カーネルの動的生成（acre_mtx/del_mtx）へ無条件で移した。
 *  実装・設計判断・ハンドル表現の注意点は `esp/shim/esp_shim_mtx.c` の
 *  冒頭コメントが正本（ここには写さない——同じ事実を 2 箇所に持つと片方が腐る）。
 *  記録: `.steering/20260804-dcre-stage1-mtx/`
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
 *		キュー（DTQ 実装）は**撤去した**（2026-08-15・BL-G-3 段3）
 *
 *  ここには `esp_shim_queue_*`（FMP3 の**データキュー** `acre_dtq`/`trcv_dtq` ＋
 *  スロットプール ＋ 空き数セマフォ `SHIM_QSEM*` ＋ CPU ロック中の保留リング）の
 *  実装 740 行が在った。**利用者が 0 になったので消した。**
 *
 *  【なぜ消したか】この実装は「キュー」の役目を負う 3 系統のうちの 1 本で、
 *  ほかの 2 本（`esp_shim_ring_*`・`m5_que_*`）と違って**受信がタスク文脈専用**
 *  だった（`trcv_dtq`/`prcv_dtq` は `CHECK_TSKCTX_UNL_MYSTATE`＝ISR から呼ぶと
 *  必ず E_CTX。「空」と区別が付かない 0 になる＝H4 の真因）。
 *  段1（2026-08-14）で Wi-Fi 系統を、段2（2026-08-15）で m5 系統を
 *  `esp_shim_ring_*`（`esp/shim/esp_shim_ring.c`＝シム所有リング・ISR 安全）へ
 *  移した結果、**この実装を呼ぶ者が 1 人も居なくなった**（13 preset の
 *  リンク集合を `nm -u` で全走査して確認。`.steering/20260814-ring-unification/
 *  logs/91-nm-queue-users.txt`）。
 *
 *  【一緒に消したもの】
 *   ・`esp/shim/esp_shim_dtq.c`（`acre_dtq`/`del_dtq` のラッパ。呼び手はここだけだった）
 *   ・`esp_shim.cfg` の `CRE_SEM(SHIM_QSEM1..16)` と `CRE_DTQ(SHIM_DTQ_AID_GUARD)`・`AID_DTQ()`
 *   ・`esp_shim_cfg.h` の `ESP_SHIM_NUM_DTQ` / `ESP_SHIM_DTQ_CNT`
 *   ・`esp_shim_isr_ctx.c` の `esp_shim_queue_recv_from_isr()` /
 *     `_msg_waiting_from_isr()` / `_recv_report_ectx()`（この実装の穴を
 *     「黙らせない」ために在った 3 値 API。穴ごと消えたので役目を終えた）
 *
 *  【残したもの】`esp_shim_sem_*`・`esp_shim_mtx_*`・`esp_shim_flg_*`・
 *  `esp_shim_tsk_*` は無関係（本段の射程外）。
 *
 *  復元したいときは `git show <このコミットの親>:esp/shim/esp_shim.c`。
 */

/*
 *		タスクプール
 *
 *  2026-08-04（段4）: **タスクの実体は動的生成（`acre_tsk`/`del_tsk`）へ移した。**
 *  かつてここに在った `tskid_tbl[]`（`SHIM_TSK1..8` の静的 ID 表）と
 *  `esp_shim.cfg` の `CRE_TSK(SHIM_TSK1..8)` は**プールごと廃止**した。
 *  実体は `AID_TSK(ESP_SHIM_NUM_TSK)` ＋ `esp/shim/esp_shim_tsk.c`。
 *  スタックは**シムが供給する**（案B）——`esp_shim_tsk.c` の
 *  `shim_tsk_stack[][]` が旧 `_kernel_stack_SHIM_TSK*` の役目を引き継ぐ。
 *  選んだ理由と却下した選択肢は `esp_shim_tsk.c` 冒頭が正本。
 *  記録: `.steering/20260804-dcre-stage4-tsk/`。
 *
 *  **この表（`shim_tsk[]`）はカーネルオブジェクトではない**——シム自身の帳簿
 *  （スロットの占有・入口関数・引数・`thread_sem`）である。dcre の段3 が
 *  `shim_que[]` を残したのと同じ理由で**静的のまま**にしてある
 *  （その `shim_que[]` はキュー実装ごと 2026-08-15 に消えた）。
 */
typedef struct {
	ID			tskid;			/* 動的 ID（0＝未生成）。旧構造では静的 ID の写し */
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	/*
	 *  段4 で追加: **自己終了して `del_tsk` を待っている**印。
	 *
	 *  `del_tsk` は休止状態のタスクしか消せず、`ext_tsk` は資源を解放しない
	 *  （`fmp3_core/kernel/task_manage.c:262-263`）。⇒ **走行中の自タスクは
	 *  自分の ID を返せない。**回収は移植側の責任である（`fmp3_core/docs/
	 *  qa-esp32s3-20260804.md` R-2）。
	 *  ⇒ 自己終了時は **`used` を保ったまま** `pend_del` を立てて `ext_tsk` し、
	 *    **次の `esp_shim_task_create()` の先頭**で `get_tst`→`TTS_DMT`→`del_tsk`
	 *    と掃除する（`shim_tsk_reap_pending()`）。
	 *  掃除はスロット選択の**前**に走るので、「掃除すれば空くのに枯渇した」は
	 *    構造的に起こらない＝**容量は旧構造から減っていない。**
	 *  却下した選択肢（reaper タスク等）は `esp_shim_tsk.c` 冒頭。
	 */
	bool_t		pend_del;
	void		*thread_sem;	/* _wifi_thread_semphr_get用（遅延生成） */
#if defined(ESP_SHIM_TASK_NOTIFY)
	/*
	 *  FreeRTOS の task notification 値（`ulTaskNotifyTake`/`xTaskNotifyGive`）。
	 *  M5Unified の Speaker/Mic がこれだけを使う（実測: 呼び出し 9 箇所・
	 *  すべてタスク文脈で `*FromISR` は 0 件）。
	 *  待ちは `slp_tsk`/`tslp_tsk`、起床は `wup_tsk` で実現する。
	 *  この 3 つは本シムの他の場所で**一切使っていない**（実測）ので、
	 *  起床要求のラッチが他の待ちを乱す心配がない。
	 */
	uint32_t	notify;
#endif
} SHIM_TSK;

static SHIM_TSK shim_tsk[ESP_SHIM_NUM_TSK];
static void *shim_main_thread_sem;	/* プール外タスク用 */

/*
 *  共通エントリ（esp_shim.cfgのCRE_TSKから呼ばれる．exinf=スロット番号）
 */
void
esp_shim_task_entry(EXINF exinf)
{
	SHIM_TSK	*t = &shim_tsk[(uint_t)exinf];

	t->fn(t->arg);
	/*
	 *  FreeRTOSのタスクはvTaskDelete(NULL)で終わるのが作法だが，
	 *  関数リターンで終わった場合もスロットを解放する。
	 *
	 *  段4: **スロットは解放しない。**`pend_del` を立てるだけにする——
	 *  動的タスクは `del_tsk` されるまで ID を占有し続けるので、
	 *  ここで `used = false` にすると**次の生成が同じスロットへ入り、
	 *  まだ生きている ID を上書きする**（＝ID が回収されないまま迷子になる）。
	 *  回収は次の `esp_shim_task_create()` が `shim_tsk_reap_pending()` で行う。
	 */
	SHIM_LOCK();
	t->pend_del = true;
	SHIM_UNLOCK();
	ext_tsk();
}

#ifdef M5_SHIM_AUDIO
/*
 *  音声（M5Unified Speaker/Mic）専用プール（2026-08-01・台帳5）。
 *  設計 `.steering/20260801-audio-composite/DESIGN.md`。blob用shim_tsk[]とは
 *  別配列で完全に並走させる——既存のBTコントローラ優先スロット等の分岐ロジックは
 *  1行も変えない。
 *
 *  専用の構造体（SHIM_TSKを使い回さない）。理由: task notification
 *  （`ulTaskNotifyTake`/`xTaskNotifyGive`、M5Unified の Speaker/Mic が使う唯一の
 *  同期方式）を持たせる必要があるが、SHIM_TSK の notify フィールドは
 *  `ESP_SHIM_TASK_NOTIFY`（既定 OFF・blob 用）でガードされており、それを有効化すると
 *  blob プールの構造体サイズが変わって golden を動かす。音声専用に、
 *  `m5_kernel_shim.c` の `M5_AUD_TSK` と同じレイアウトの構造体を独立に持つ。
 *
 *  2026-08-05（段7）: **カーネルタスクを動的生成（`acre_tsk`/`del_tsk`）へ移した。**
 *  この構造体（帳簿）と `shim_audio_tsk[]` 配列は**残る**——理由:
 *   ・`shim_audio_tsk_handle_is_valid()` が**この配列に対するポインタ演算**で
 *     FreeRTOS 互換境界のハンドル（`TaskHandle_t`）を検証している。配列を消すと壊れる。
 *   ・`notify`（`ulTaskNotifyTake`/`xTaskNotifyGive` の値）をここに持っている。
 *  ⇒ **動的化したのはカーネルタスクの方**であって、帳簿ではない。
 *  `pend_del` は段4 の blob 側と同じ意味（自己終了したが `del_tsk` がまだ）。
 */
typedef struct {
	ID			tskid;
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	bool_t		pend_del;	/* 段7: 自己終了済み・del_tsk 待ち（blob 側と同義） */
	uint32_t	notify;
} SHIM_AUDIO_TSK;

/*  `SHIM_AUDIO_NTSK` は `esp_shim_cfg.h` へ移した（本数の単一真実源。段7）。 */
static SHIM_AUDIO_TSK shim_audio_tsk[SHIM_AUDIO_NTSK];

void
esp_shim_audio_task_entry(EXINF exinf)
{
	SHIM_AUDIO_TSK	*t = &shim_audio_tsk[(uint_t) exinf];

	t->fn(t->arg);
	/*
	 *  段7: **スロットは解放しない。**`pend_del` を立てるだけにする——
	 *  動的タスクは `del_tsk` されるまで ID を占有し続けるので、ここで
	 *  `used = false` にすると**次の生成が同じスロットへ入り、まだ生きている ID を
	 *  上書きする**（＝ID が回収されないまま迷子になる）。
	 *  回収は次の `shim_audio_task_alloc()` が `shim_audio_reap_pending()` で行う。
	 *  blob 側（`esp_shim_task_entry` の末尾）とまったく同じ形である。
	 *
	 *  なお M5Unified の worker（`spk_task`/`mic_task`）は自分で
	 *  `vTaskDelete(nullptr)` を呼ぶので、通常ここへは戻ってこない
	 *  （＝下の `esp_shim_task_delete(NULL)` 経路を通る）。関数リターンで
	 *  終わった場合の受け皿としてここも同じ扱いにしておく。
	 */
	SHIM_LOCK();
	t->pend_del = true;
	SHIM_UNLOCK();
	ext_tsk();
}

/*
 *  段7: **自己終了して `del_tsk` を待っている音声スロットを掃除する。**
 *
 *  `shim_audio_task_alloc()` の**スロット選択の前**で呼ぶ
 *  （⇒「掃除すれば空くのに枯渇した」が構造的に起こらない＝容量は旧構造から減らない）。
 *  サービスコール（`get_tst`/`del_tsk`）は **`SHIM_LOCK` の外**で呼ぶ
 *  （`CHECK_TSKCTX_UNL`＝タスク文脈かつ CPU ロック外）。
 *
 *  **ここが「回収経路が回った」ことの唯一の出口である**——
 *  `esp_shim_tsk_reap()` は `del_tsk` が `E_OK` を返したときにだけ `true` を返す
 *  （`esp/shim/esp_shim_tsk.c`）。⇒ **スロットが再び使えるようになったという事実は、
 *  `del_tsk` が成功したという事実と同値である。**
 *  （AC `.steering/20260805-dcre-stage7-audio/AC.md` §2-0 の判定はこの同値性に立つ。）
 *
 *  **自己終了直後の窓について**（正直に書く）: `ext_tsk()` が完了する前に
 *  次の `begin()` が走ると `get_tst` は `TTS_DMT` を返さず、スロットは空かない。
 *  現行の呼び方ではこの窓は開かない——M5Unified の `end()` は
 *  `do { vTaskDelay(1); } while (_task_handle);` で待ち、worker の優先度
 *  （`SHIM_AUDIO_TASK_PRI`=9）は呼出し側（MAIN_PRIORITY=10）より**高い**ので、
 *  呼出し側が起きたときには worker は既に休止している。
 *  **待ちを足していない**（時間で誤魔化さない）。窓が開いたら
 *  `audio task pool exhausted` が出る＝**黙って壊れない**。
 */
static void
shim_audio_reap_pending(void)
{
	uint_t	i;

	for (i = 0U; i < SHIM_AUDIO_NTSK; i++) {
		ID		id;

		SHIM_LOCK();
		id = (shim_audio_tsk[i].used && shim_audio_tsk[i].pend_del)
				 ? shim_audio_tsk[i].tskid : (ID) 0;
		SHIM_UNLOCK();
		if (id == (ID) 0) {
			continue;
		}
		if (esp_shim_tsk_reap(id)) {
			SHIM_LOCK();
			shim_audio_tsk[i].tskid = (ID) 0;
			shim_audio_tsk[i].pend_del = false;
			shim_audio_tsk[i].used = false;
			SHIM_UNLOCK();
		}
	}
}

/*
 *  音声プールのハンドル判定。境界整列も見る（blob側 shim_tsk_handle_is_valid と同じ理由）。
 */
static bool_t
shim_audio_tsk_handle_is_valid(const SHIM_AUDIO_TSK *t)
{
	if ((t < &shim_audio_tsk[0]) || (t >= &shim_audio_tsk[SHIM_AUDIO_NTSK])) {
		return(false);
	}
	if ((((uintptr_t) t - (uintptr_t) &shim_audio_tsk[0]) % sizeof(SHIM_AUDIO_TSK)) != (uintptr_t) 0U) {
		return(false);
	}
	return(true);
}

static SHIM_AUDIO_TSK *
shim_audio_self(void)
{
	ID		self;
	uint_t	i;

	if (get_tid(&self) != E_OK) {
		return(NULL);
	}
	for (i = 0U; i < SHIM_AUDIO_NTSK; i++) {
		if (shim_audio_tsk[i].used && (shim_audio_tsk[i].tskid == self)) {
			return(&shim_audio_tsk[i]);
		}
	}
	return(NULL);
}

/*
 *  C-2-2（2026-08-03・T-5）: 「自タスクが音声プールに居ない」を**沈黙させない**。
 *
 *  【何が悪かったか】`esp_shim_task_notify_take()` は `shim_audio_self()` が
 *  NULL のとき **無診断で 0 を返して**いた。0 は「通知が無かった」と同じ値なので、
 *  呼出し側（M5Unified の Speaker/Mic の待ちループ）から見ると
 *  「**まだ来ていない**」と「**この経路では永久に来ない**」が区別できない。
 *  ＝ H4（「取れなかった」を「空だった」に畳む）を**新規コードで再生産**していた。
 *  しかも対称であるべき `esp_shim_task_notify_give()` 側は
 *  ハンドルがプール外なら syslog(LOG_ERROR) を出しており、**片側だけ黙っていた**。
 *
 *  【なぜ戻り値は 0 のままか】`ulTaskNotifyTake()` の戻り値は
 *  `uint32_t`（＝通知カウント）で、FreeRTOS 互換境界に「失敗」を表す値が無い。
 *  `esp_shim_isr_ctx.c` と**同じ判断**（境界の戻り値は変えず、失敗は
 *  **診断ログとカウンタ**で外に出す）を採る。
 *
 *  【いつ起きるか】音声 worker が `strcmp` の振り分けを外れて blob プールへ
 *  落ちたとき（＝C-2-1 の名前ずれ。S3 では blob プールは `TA_NULL`＝FPU 無し）。
 *  つまりこのカウンタが 0 でないことは、**名前の突合が破れた実行時の証拠**である。
 */
volatile uint32_t	esp_shim_audio_notify_take_foreign;

static void
shim_audio_notify_take_report(void)
{
	uint32_t	n;
	ID			self = 0;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	n = esp_shim_audio_notify_take_foreign + 1U;
	esp_shim_audio_notify_take_foreign = n;
	SIL_UNL_INT();

	/*  氾濫を避けて最初の 4 回とその後 1024 回ごとに出す。
	 *  「出す回数を絞る」ことと「出さない」ことは違う——カウンタは常に進む。 */
	if ((n <= 4U) || ((n % 1024U) == 0U)) {
		(void) get_tid(&self);
		syslog(LOG_ERROR,
			   "esp_shim: audio notify_take: caller tskid %d is NOT in the audio pool "
			   "n=%d -> returning 0 (通知が無いのではなく、この経路では永久に来ない)",
			   (int_t) self, (int_t) n);
	}
}

/*
 *  `ulTaskNotifyTake(clear_on_exit, timeout)` 相当（合成構成・音声専用プール向け）。
 *  設計は `m5_kernel_shim.c`（非合成側）の同名関数と同じ:
 *  値の判定を割込み禁止で行い、**解除してから** `slp_tsk`。その隙に give が来ても
 *  `wup_tsk` が起床要求をラッチするので取りこぼさない。
 */
uint32_t
esp_shim_task_notify_take(int clear_on_exit, uint32_t timeout_ms)
{
	SHIM_AUDIO_TSK	*t = shim_audio_self();
	uint32_t		v;
	ER				er;

	if (t == NULL) {
		/*  プール外タスクは通知値を持たない。黙って 0 を返さない（上のコメント）。 */
		shim_audio_notify_take_report();
		return(0U);
	}
	for (;;) {
		SHIM_LOCK();
		v = t->notify;
		if (v > 0U) {
			t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
			SHIM_UNLOCK();
			return(v);
		}
		SHIM_UNLOCK();
		if (timeout_ms == 0U) {
			return(0U);
		}
		er = (timeout_ms == 0xFFFFFFFFU)
			 ? slp_tsk() : tslp_tsk((RELTIM)(timeout_ms * 1000U));	/* ms→μs */
		if (er == E_TMOUT) {
			SHIM_LOCK();
			v = t->notify;
			if (v > 0U) {
				t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
			}
			SHIM_UNLOCK();
			return(v);
		}
	}
}

/*
 *  `xTaskNotifyGive(handle)` 相当。ハンドルの型を検査してから使う
 *  （台帳5・実装中に発見: `m5_kernel_shim.c` 旧実装は検査せず異なる構造体として
 *  読み書きする型混同があった。ここでは繰り返さない）。
 */
void
esp_shim_task_notify_give(void *task_handle)
{
	SHIM_AUDIO_TSK	*t = (SHIM_AUDIO_TSK *) task_handle;

	if ((t == NULL) || !shim_audio_tsk_handle_is_valid(t)) {
		syslog(LOG_ERROR,
			"esp_shim: audio notify_give: handle %p is outside audio pool [%p,%p) - ignored",
			task_handle, (void *) &shim_audio_tsk[0], (void *) &shim_audio_tsk[SHIM_AUDIO_NTSK]);
		return;
	}
	SHIM_LOCK();
	t->notify++;
	SHIM_UNLOCK();
	(void) wup_tsk(t->tskid);	/* E_QOVR（既にラッチ済み）は正常 */
}
#endif /* M5_SHIM_AUDIO */

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

#ifdef M5_SHIM_AUDIO
/*
 *  音声（"spk_task"/"mic_task"）専用の割り当て（台帳5）。
 *  `esp_shim_task_create()` の**先頭**から呼ばれ、名前が一致すれば
 *  `shim_audio_tsk[]` へ割り振って**返る**（下の blob 用ロジックへは進まない）。
 *  一致しなければ NULL を返し、呼び出し側は従来どおり blob プールを使う。
 *  枯渇時はフォールバックしない（fail-closed。素の m5 側 `m5_kernel_shim.c` と同じ方針）。
 */
static SHIM_AUDIO_TSK *
shim_audio_task_alloc(const char *name, void (*entry)(void *), void *param,
					  uint32_t stack_size)
{
	/*  段7: ここに在った静的 ID 表
	 *      `static const ID tskid_tbl[SHIM_AUDIO_NTSK] = { SHIM_AUDIO_TSK1, SHIM_AUDIO_TSK2 };`
	 *  は**廃止した**。ID はカーネルが `acre_tsk` で配る
	 *  （`esp_shim_audio_tsk_create()`／`esp/shim/esp_shim_tsk.c`）。
	 *  段4 が blob 側の `tskid_tbl[]` を消したのと同じ形である。 */
	uint_t				i;
	SHIM_AUDIO_TSK		*t = NULL;
	ID					tskid;

	if ((name == NULL)
			|| ((strcmp(name, ESP_SHIM_AUDIO_TASK_NAME_SPK) != 0)
				&& (strcmp(name, ESP_SHIM_AUDIO_TASK_NAME_MIC) != 0))) {
		return(NULL);
	}
	/*  C-2-4（2026-08-03・T-5）: 要求が枠を超えたら**通さない**（fail-closed）。
	 *  従来は警告だけ出して**そのまま生成**していたが、実際に足りなければ
	 *  タスクスタックが溢れて実機で静かに壊れる（＝壊れ方が診断不能になる）。
	 *  生成を断れば呼出し側（M5Unified の Speaker/Mic の begin()）が失敗を見る。 */
	if (stack_size > SHIM_AUDIO_TSK_STKSZ) {
		syslog(LOG_ERROR, "esp_shim: audio task '%s' stack %u > pool %u -> 生成しない",
			   name, (uint_t) stack_size, (uint_t) SHIM_AUDIO_TSK_STKSZ);
		return(NULL);
	}
	/*
	 *  段7: **自己終了して `del_tsk` を待っているスロットを先に掃除する。**
	 *  スロット選択の**前**に置くのが肝——後ろに置くと「掃除すれば空くのに
	 *  枯渇した」が起きて容量が旧構造より減る。サービスコールを呼ぶので
	 *  `SHIM_LOCK` の**外**（`CHECK_TSKCTX_UNL`）。blob 側の
	 *  `shim_tsk_reap_pending()` とまったく同じ形である。
	 */
	shim_audio_reap_pending();
	SHIM_LOCK();
	for (i = 0U; i < SHIM_AUDIO_NTSK; i++) {
		if (!shim_audio_tsk[i].used) {
			break;
		}
	}
	if (i < SHIM_AUDIO_NTSK) {
		shim_audio_tsk[i].used = true;
		/*  段7: ID はまだ無い（`acre_tsk` はロックの外で撃つ）。
		 *  0 を入れておくのは「まだ配られていない」を表すため——
		 *  `shim_audio_self()` は `used && tskid == self` で引くので、
		 *  0 のあいだは誰にもマッチしない（自タスク ID は必ず 1 以上）。 */
		shim_audio_tsk[i].tskid = (ID) 0;
		shim_audio_tsk[i].pend_del = false;
		shim_audio_tsk[i].fn = entry;
		shim_audio_tsk[i].arg = param;
		/*  C-2-3（2026-08-03・T-5）: **スロット再利用時に notify を 0 へ戻す。**
		 *  従来は前世代の値が残り、`Speaker.end()`→`begin()` で
		 *  **偽の起床**（来ていない通知で待ちが抜ける）が起きる。
		 *  非合成側（`m5/shim/m5_kernel_shim.c`）は元から明示的にクリアしており、
		 *  合成側だけが落ちていた。 */
		shim_audio_tsk[i].notify = 0U;
		t = &shim_audio_tsk[i];
	}
	SHIM_UNLOCK();
	if (t == NULL) {
		/*  段7: ここへ来たら**回収が回っていない**（掃除は上で済ませてある）。
		 *  停止点である（AC §7 の 6）。黙って blob プールへ落とさない。 */
		syslog(LOG_ERROR, "esp_shim: audio task pool exhausted ('%s')", name);
		return(NULL);
	}

	/*
	 *  段7: **ここで動的生成する。**`acre_tsk`（`TA_ACT` なし）→ `mact_tsk`。
	 *  `t->tskid` の記録を 2 つの間に挟むのは blob 側と同じ理由——
	 *  `mact_tsk` で起きたタスクは即座に走り得るので、`shim_audio_self()` が
	 *  自分を見つけられるように**起動より前に**帳簿へ入れておく必要がある。
	 */
	tskid = esp_shim_audio_tsk_create(i, esp_shim_audio_task_entry, (EXINF) i,
									  (PRI) SHIM_AUDIO_TASK_PRI);
	if (tskid == (ID) 0) {
		/*  `E_NOID` 等。診断は `esp_shim_tsk.c` が**生値で**出す（畳まない）。 */
		{
			SHIM_LOCK();
			t->used = false;
			SHIM_UNLOCK();
		}
		syslog(LOG_ERROR, "esp_shim: audio acre_tsk FAIL ('%s')", name);
		return(NULL);
	}
	{
		SHIM_LOCK();
		t->tskid = tskid;
		SHIM_UNLOCK();
	}

	/*
	 *  起動。コアは指定しない——音声は旧 cfg で `CLASS(CLS_PRC1)` 相当の
	 *  時刻管理プロセッサ上に居た。`ESP_SHIM_TASK_NO_AFFINITY` は
	 *  `shim_tsk_prcid()` のポリシーで **PRC1** に落ちる（＝非退行）。
	 *  失敗したら `esp_shim_tsk_activate()` の中で `del_tsk` 済み＝リークしない。
	 */
	if (!esp_shim_tsk_activate(tskid, (uint32_t) ESP_SHIM_TASK_NO_AFFINITY)) {
		{
			SHIM_LOCK();
			t->tskid = (ID) 0;
			t->used = false;
			SHIM_UNLOCK();
		}
		syslog(LOG_ERROR, "esp_shim: audio mact_tsk FAIL ('%s')", name);
		return(NULL);
	}
	syslog(LOG_NOTICE, "esp_shim: audio task '%s' -> tskid %d (pri %d, TA_FPU)",
		   name, (int_t) t->tskid, (int_t) SHIM_AUDIO_TASK_PRI);
	return(t);
}
#endif /* M5_SHIM_AUDIO */

/*
 *  段4: **自己終了して `del_tsk` を待っているスロットを掃除する。**
 *
 *  `esp_shim_task_create()` の**先頭**で呼ぶ（スロット選択の**前**）。
 *  ⇒ 「掃除すれば空くのに枯渇した」が構造的に起こらない＝容量は旧構造から減らない。
 *
 *  サービスコール（`get_tst`/`del_tsk`）は **`SHIM_LOCK` の外**で呼ぶ
 *  （`CHECK_TSKCTX_UNL`＝タスク文脈かつ CPU ロック外。DESIGN-MEMO §7-5b）。
 *  ⇒ ロックは「印を読む」「印を消す」の 2 回だけ、間はロック外である。
 *  この間に別タスクが同じスロットを掃除しても、`del_tsk` は 2 回目が `E_NOEXS` で
 *    落ちるだけ（診断に残る）＝二重解放にはならない。
 */
static void
shim_tsk_reap_pending(void)
{
	uint_t	i;

	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		ID		id;

		SHIM_LOCK();
		id = (shim_tsk[i].used && shim_tsk[i].pend_del) ? shim_tsk[i].tskid : (ID) 0;
		SHIM_UNLOCK();
		if (id == (ID) 0) {
			continue;
		}
		if (esp_shim_tsk_reap(id)) {
			SHIM_LOCK();
			shim_tsk[i].tskid = (ID) 0;
			shim_tsk[i].pend_del = false;
			shim_tsk[i].used = false;
			SHIM_UNLOCK();
		}
	}
}

/*
 *  スロットごとの初期優先度。
 *
 *  **これは旧 `esp_shim.cfg` の `CRE_TSK(SHIM_TSK1, …)` が持っていた分岐を
 *  そのまま写したものである**（静的版／動的版の切替えではない＝A-6 に反しない）。
 *  `acre_tsk` は `itskpri` を実行時に取れるので**構造的には `freertos_prio` から
 *  直接写像できる**が、それは BT-4 調査（接続確立直後の即時切断）で入れた
 *  「コントローラ級タスクを高優先度スロットへ寄せる」振る舞いを変えることになる。
 *  ⇒ **本段（ビルドのみ・実機なし）では変えない。**
 *    記録: `.steering/20260709-ble-bt4-connection/`、段4 AC の §2「入れない」。
 */
static PRI
shim_tsk_slot_pri(uint_t slot)
{
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
	return((slot == 0U) ? (PRI) ESP_SHIM_BT_CTRL_TASK_PRI
						: (PRI) ESP_SHIM_WIFI_TASK_PRI);
#else
	(void) slot;
	return((PRI) ESP_SHIM_WIFI_TASK_PRI);
#endif
}

int32_t
esp_shim_task_create_pinned(void (*entry)(void *), const char *name,
							uint32_t stack_size, void *param,
							uint32_t freertos_prio, void **task_handle,
							uint32_t core_id)
{
#ifdef M5_SHIM_AUDIO
	{
		SHIM_AUDIO_TSK	*at = shim_audio_task_alloc(name, entry, param, stack_size);

		if (at != NULL) {
			if (task_handle != NULL) {
				*task_handle = (void *) at;
			}
			return(1);
		}
		/*  音声名だが枯渇/失敗した場合は、blob プールへフォールバックしない
		 *  （設計どおり）。ここで確実に失敗を返す。 */
		if ((name != NULL)
				&& ((strcmp(name, ESP_SHIM_AUDIO_TASK_NAME_SPK) == 0)
					|| (strcmp(name, ESP_SHIM_AUDIO_TASK_NAME_MIC) == 0))) {
			return(0);
		}
	}
#endif /* M5_SHIM_AUDIO */
	/*  段4: ここに在った `tskid_tbl[]`（静的 ID 表）は**廃止した**。
	 *  ID はカーネルが `acre_tsk` で配る（`esp/shim/esp_shim_tsk.c`）。 */
	uint_t		i = ESP_SHIM_NUM_TSK;
	SHIM_TSK	*t = NULL;
	ID			tskid;

	/*
	 *  C-2-4（3 箇所目・blob プール側）: **fail-closed にした**（2026-08-04）。
	 *
	 *  以前はここで syslog(LOG_NOTICE) を出して**そのまま生成を続けて**いた。
	 *  ⇒ 要求より小さいスタックを黙って渡す＝**いつか必ず壊れるが、壊れた時に
	 *    ここが原因だと分からない**。要求を満たせないなら**断る**方が良い。
	 *
	 *  【保留の理由だった「未実測」は 2026-08-04 に解消した】
	 *  T-5（2026-08-03）は「BT が要求するスタック量が未実測なので、断ると BLE が
	 *  起動しなくなり得る」として保留した。要求量を測った結果は下記のとおりで、
	 *  **現行 7 構成のどれも超過しない**（詳細と生ログ
	 *  `.steering/20260804-blobpool-stack/RESULT.md`）:
	 *
	 *    - Wi-Fi blob の 'wifi'（唯一の生成タスク）= **6656 B**
	 *        S3 : 実機実測（CoreS3 44:1b:f6:e2:73:84 / seam-s3-m5-wifi、プール 6656）
	 *        LX6: 実機実測（78:21:84:a6:5c:64 / seam-lx6-wifi、プール 8192）
	 *        静的上限: `config_get_wifi_task_stack_size()` を両チップとも逆アセンブル
	 *        した結果 返り値は {3072, 3584, 6144, 6656} のいずれか＝**上限 6656**。
	 *    - NimBLE 構成の 'btController' = **4096 B**（= ESP_TASK_BT_CONTROLLER_STACK
	 *        = 3584 + 512）。blob は `cfg->controller_task_stack_size`（u16 @ +8）を
	 *        そのまま `_task_create` へ渡す——`btdm_controller_init` の逆アセンブルで
	 *        +8/+10/+11 が stack/prio/run_cpu に対応することを確認済み。
	 *    - NimBLE 構成の 'nimble_host' = **4096 B**（`esp_nimble_enable` の即値 0x1000。
	 *        = CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE）。
	 *    ⇒ プールは S3 Wi-Fi 6656 / LX6 8192 / BLE 8192。**超過ゼロ**。
	 *      （S3 Wi-Fi は 6656 == 6656 で余裕 0。これは「ぎりぎり」ではなく
	 *        「blob の要求値ちょうどを与える」という設計どおりの状態である。）
	 *
	 *  【追い切れなかったもの＝正直に書く】
	 *    - blob 内部でハードコードされた値は静的には取れない。上の 'wifi' は
	 *      `config_get_wifi_task_stack_size()` が**リンクされる関数**だったので
	 *      逆アセンブルで上限を出せたが、これは幸運であって一般には取れない。
	 *    - BT は**実機で測れていない**（seam-s3-ble は UART0 コンソール構成で、
	 *      手元の CoreS3 には UART ブリッジが無い。変更前の golden バイナリでも
	 *      CoreS3 では advertising しないことが negative control 済み）。
	 *      BT の 2 値は**静的根拠のみ**である。ただし過去の実機ログ（LX6 seam W2 ほか
	 *      複数 run）で NimBLE 構成が作るタスクは 'btController' と 'nimble_host' の
	 *      **2 本だけ**であることは繰り返し観測されている。
	 *
	 *  発火の実演: プールを 4096 へ落とした LX6 ビルドで 'wifi'(6656) の生成が
	 *  実際に断られ、Wi-Fi が起動しないことを実機で確認した（同 RESULT.md §4）。
	 */
	if (stack_size > ESP_SHIM_TSK_STKSZ) {
		syslog(LOG_ERROR, "esp_shim: task '%s' stack %u > pool %u -- refused",
			   name, (uint_t)stack_size, (uint_t)ESP_SHIM_TSK_STKSZ);
		SHIM_DBG_STR("\r\n<<SHIM TSK REFUSED(stack) '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("' req=");
		SHIM_DBG_U(stack_size);
		SHIM_DBG_STR(" pool=");
		SHIM_DBG_U(ESP_SHIM_TSK_STKSZ);
		SHIM_DBG_STR(">>\r\n");
		return(0);
	}
	/*
	 *  段4: **自己終了して `del_tsk` を待っているスロットを先に掃除する。**
	 *  スロット選択の**前**に置くのが肝——後ろに置くと「掃除すれば空くのに
	 *  枯渇した」が起きて容量が旧構造より減る。サービスコールを呼ぶので
	 *  `SHIM_LOCK` の**外**（`CHECK_TSKCTX_UNL`。DESIGN-MEMO §7-5b）。
	 */
	shim_tsk_reap_pending();
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
		shim_tsk[i].tskid = (ID) 0;		/* acre_tsk が成功するまで ID は無い */
		shim_tsk[i].pend_del = false;
		shim_tsk[i].fn = entry;
		shim_tsk[i].arg = param;
		t = &shim_tsk[i];
#ifdef TOPPERS_STACK_PROBE
		/*  blobが要求したスタックサイズを記録する（ESP_SHIM_TSK_STKSZを
		 *  実測に基づいて縮小する際、「実使用量」だけでなく「blobの要求値」も
		 *  判断材料に要るため）。ここでsyslogすると起動直後でlogtask未起動の
		 *  ため取りこぼされる（実測：「37 messages are lost.」）ので、配列に
		 *  記録しておき esp_shim_probe_dump() で後から出力する。 */
		esp_shim_probe_req[i] = stack_size;
		esp_shim_probe_name[i] = name;
#endif /* TOPPERS_STACK_PROBE */
	}
	SHIM_UNLOCK();

	if (t == NULL) {
		/*  (d) **シムのスロット枯渇**。`E_NOID`（カーネルの ID 枯渇）とは
		 *  別の事象なので、別の経路・別の文言のまま残す（畳まない）。 */
		syslog(LOG_ERROR, "esp_shim: task pool exhausted ('%s')", name);
		SHIM_DBG_STR("\r\n<<SHIM TSK POOL EXHAUSTED '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("'>>\r\n");
		return(0);
	}

	/*
	 *  段4: **ここで動的生成する。**`acre_tsk`（`TA_ACT` なし）→ `mact_tsk`。
	 *
	 *  2 つの間に挟むのは `t->tskid` の記録だけで、**サービスコールは挟まない**
	 *  （DESIGN-MEMO §3-2(e)「不可分に連続させる」）。記録が先である理由:
	 *  `mact_tsk` で起きたタスクは即座に走り得るので、`esp_shim_task_get_current()`
	 *  が自分を見つけられるように**起動より前に**表へ入れておく必要がある。
	 *  失敗経路は `esp_shim_tsk_activate()` の中で `del_tsk` して巻き戻す（§3-2(c)）。
	 */
	tskid = esp_shim_tsk_create(i, esp_shim_task_entry, (EXINF) i,
								shim_tsk_slot_pri(i));
	if (tskid == (ID) 0) {
		/*  (a) `E_NOID` 等。診断は `esp_shim_tsk.c` が**生値で**出す。 */
		{
			SHIM_LOCK();
			t->used = false;
			SHIM_UNLOCK();
		}
		SHIM_DBG_STR("\r\n<<SHIM acre_tsk FAIL '");
		SHIM_DBG_STR(name);
		SHIM_DBG_STR("'>>\r\n");
		return(0);
	}
	{
		SHIM_LOCK();
		t->tskid = tskid;
		SHIM_UNLOCK();
	}

	syslog(LOG_NOTICE, "esp_shim: task '%s' -> tskid %d (prio %u core %u)",
		   name, (int_t)t->tskid, (uint_t)freertos_prio, (uint_t)core_id);
	SHIM_DBG_STR("\r\n<<SHIM TSK '");
	SHIM_DBG_STR(name);
	SHIM_DBG_STR("' ss=");
	SHIM_DBG_U(stack_size);
	SHIM_DBG_STR(" heapfree=");
	SHIM_DBG_U(esp_shim_heap_free_size());
	SHIM_DBG_BTENV();
	SHIM_DBG_STR(">>\r\n");
	if (!esp_shim_tsk_activate(tskid, core_id)) {
		/*  (c) `mact_tsk` 失敗。ID は `esp_shim_tsk_activate()` が
		 *  `del_tsk` 済み＝**リークしない**。スロットも返す。 */
		{
			SHIM_LOCK();
			t->tskid = (ID) 0;
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

/*
 *  コア指定のない生成（`xTaskCreate` 相当）。
 *  「コアはどちらでもよい」を **`ESP_SHIM_TASK_NO_AFFINITY`** として
 *  明示的に渡す——**捨てるのではなく、値として渡して方針で解決する**
 *  （方針は `esp/shim/esp_shim_tsk.c` の `shim_tsk_prcid()`）。
 */
int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	return(esp_shim_task_create_pinned(entry, name, stack_size, param,
									   freertos_prio, task_handle,
									   (uint32_t) ESP_SHIM_TASK_NO_AFFINITY));
}

/*
 *  タスクハンドルがプール配列 shim_tsk[] の要素を指しているかの検査
 *  （2026-07-27・レビュー指摘 esp-6）。
 *
 *  本シムの「タスクハンドル」は SHIM_TSK* だが，これを受け取る側は
 *  以下の 2 種類の**プール外**の値を渡してくる可能性がある：
 *    (a) esp_shim_task_get_current() がプール外タスクに対して返す
 *        `(void *)&shim_main_thread_sem`（擬似ハンドル。static void* 変数の
 *        アドレスであって SHIM_TSK ではない）
 *    (b) 上位（ESP-IDF の Wi-Fi/BT blob や NimBLE）が保持していた任意の値
 *  これらを SHIM_TSK* として `t->used = false` すると，SHIM_TSK の
 *  レイアウト上 `used` は先頭から **12バイト目**（tskid=0/fn=4/arg=8）
 *  なので，無関係な静的変数を 12バイト先で破壊する。
 *
 *  esp_shim_task_notify_give() は (a) だけを `t == &shim_main_thread_sem`
 *  で弾いているが，esp-6 の指摘どおりそれでは (b) を通してしまう。
 *  よって**配列範囲＋要素境界整列**で判定する。(a) は shim_tsk[] の外に
 *  ある別の静的変数なので，この判定で自動的に弾かれる。
 */
static bool_t
shim_tsk_handle_is_valid(const SHIM_TSK *t)
{
	if ((t < &shim_tsk[0]) || (t >= &shim_tsk[ESP_SHIM_NUM_TSK])) {
		return(false);
	}
	/*
	 *  範囲内でも要素の先頭を指していなければ不正（構造体の途中を
	 *  指すハンドルでメンバをずらして書き潰すのを防ぐ）。
	 */
	if ((((uintptr_t) t - (uintptr_t) &shim_tsk[0]) % sizeof(SHIM_TSK))
														!= (uintptr_t) 0U) {
		return(false);
	}
	return(true);
}

#ifdef ESP_SHIM_DELTSK_COUNT
/*
 *  BL-E-2（2026-08-14・`.steering/20260814-deltsk-measure/`）: 「製品構成で
 *  `del_tsk` が回る手段が無い」を**実測で確定させる**ための計装。
 *
 *  区別したいのは 3 つで、既存カウンタだけでは足りない:
 *    (A) そもそも `esp_shim_task_delete()` が**呼ばれない**
 *        （blob が `_task_delete` を叩かない）
 *    (B) 呼ばれるが `del_tsk` まで**届かない**
 *        （自己終了で `pend_del` が立ったまま、次の生成が来ないので回収されない）
 *    (C) `del_tsk` が**実際に走る**（`esp_shim_tsk_reaped` が進む＝既存カウンタ）
 *  ⇒ (A)/(B) を分けるには「入口の回数」と「pend_del を立てた回数」が要る。
 *
 *  **opt-in・既定 OFF**（`-DA1_DELTSK_COUNT=ON`）。golden 構成には 1 バイトも
 *  入らない（このブロックごとコンパイルされない）。
 */
volatile uint32_t	esp_shim_deltsk_calls;		/* 入口に来た回数           */
volatile uint32_t	esp_shim_deltsk_self;		/* 自タスク終了として扱った */
volatile uint32_t	esp_shim_deltsk_other;		/* 他タスク削除として扱った */
volatile uint32_t	esp_shim_deltsk_pend_set;	/* pend_del を立てた回数    */
volatile uint32_t	esp_shim_deltsk_badhandle;	/* プール外ハンドルで拒否   */
#endif /* ESP_SHIM_DELTSK_COUNT */

void
esp_shim_task_delete(void *task_handle)
{
	ID			self;
#ifdef ESP_SHIM_DELTSK_COUNT
	esp_shim_deltsk_calls++;
#endif
#ifdef M5_SHIM_AUDIO
	/*
	 *  台帳5・実装中に発見した型混同を避ける: `task_handle` を安易に
	 *  `SHIM_TSK*` へキャストしてから audio 判定に流用しない
	 *  （SHIM_TSK と SHIM_AUDIO_TSK はレイアウトが別物）。
	 *  先に「audio プールの要素か」を専用の型で検査し、そうなら
	 *  audio 専用の削除経路を独立して完結させる。
	 */
	if (task_handle != NULL) {
		SHIM_AUDIO_TSK	*at = (SHIM_AUDIO_TSK *) task_handle;

		if (shim_audio_tsk_handle_is_valid(at)) {
			(void) get_tid(&self);
			if (at->used && (at->tskid == self)) {
				/*  段7: **スロットは返さない。**`pend_del` を立てるだけ
				 *  （走行中の自分は `del_tsk` できない＝休止状態必須）。
				 *  回収は次の生成の `shim_audio_reap_pending()`。 */
				SHIM_LOCK();
				at->pend_del = true;
				SHIM_UNLOCK();
				ext_tsk();
				/* ここには戻らない */
			}
			/*  段7: 他タスクの削除。`ter_tsk` は**同期的に**休止へ落とすので
			 *  そのまま `del_tsk` できる＝**この経路は回収を遅らせない**。
			 *  `del_tsk` が失敗したら**スロットを返さない**（ID がまだ生きているのに
			 *   スロットを再利用すると、次の生成が生きている ID を上書きする）。
			 *  失敗は `esp_shim_tsk.c` が数えて診断に出す＝黙らない。 */
			if (esp_shim_tsk_terminate(at->tskid)) {
				SHIM_LOCK();
				at->tskid = (ID) 0;
				at->pend_del = false;
				at->used = false;
				SHIM_UNLOCK();
			}
			return;
		}
	}
#endif /* M5_SHIM_AUDIO */
	{
		SHIM_TSK	*t = (SHIM_TSK *)task_handle;

		/*
		 *  プール外ハンドルの排除（esp-6）。NULL は「自タスクの終了」を
		 *  意味する正当な呼び方なので通す。NULL 以外は必ずここで検査してから
		 *  デリファレンスする（従来は検査前に t->tskid を読んでいた）。
		 */
		if ((t != NULL) && !shim_tsk_handle_is_valid(t)) {
#ifdef ESP_SHIM_DELTSK_COUNT
			esp_shim_deltsk_badhandle++;
#endif
			syslog(LOG_ERROR,
				"esp_shim: task_delete: handle %p is outside task pool [%p,%p) - ignored",
				task_handle, (void *)&shim_tsk[0],
				(void *)&shim_tsk[ESP_SHIM_NUM_TSK]);
			return;
		}

		(void) get_tid(&self);
		if (t == NULL || t->tskid == self) {
#ifdef ESP_SHIM_DELTSK_COUNT
			esp_shim_deltsk_self++;
#endif
			/*
			 *  自タスクの終了。
			 *
			 *  段4: **スロットは返さない。**`pend_del` を立てるだけ
			 *  （`esp_shim_task_entry` の末尾と同じ理由——動的タスクは
			 *   `del_tsk` されるまで ID を占有し続ける。`del_tsk` は休止状態
			 *   必須なので**走行中の自分は自分を消せない**）。
			 *  回収は次の `esp_shim_task_create()` の `shim_tsk_reap_pending()`。
			 *  段7: **音声プールも同じ扱いになった。**（段4 の時点ではここに
			 *   「音声プールは静的のままなので即座に `used = false` でよい」と
			 *   書いてあったが、**もう当てはまらない。**）
			 */
			SHIM_LOCK();
			{
				uint_t	i;
				for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
					if (shim_tsk[i].used && (shim_tsk[i].tskid == self)) {
						shim_tsk[i].pend_del = true;
#ifdef ESP_SHIM_DELTSK_COUNT
						esp_shim_deltsk_pend_set++;
#endif
					}
				}
#ifdef M5_SHIM_AUDIO
				for (i = 0U; i < SHIM_AUDIO_NTSK; i++) {
					/*  段7: `used &&` を足した。**動的 ID は再利用される**ので、
					 *  解放済みスロットに残った古い `tskid` が、別のタスクへ
					 *  配り直された同じ値と**偶然一致し得る**（`del_tsk` は ID を
					 *  free list の末尾へ返し、`acre_tsk` が先頭から配る＝FIFO。
					 *  `fmp3_core/kernel/task_manage.c`）。
					 *  静的 ID の時代には起こり得なかった事象である。 */
					if (shim_audio_tsk[i].used
							&& (shim_audio_tsk[i].tskid == self)) {
						shim_audio_tsk[i].pend_del = true;
					}
				}
#endif /* M5_SHIM_AUDIO */
			}
			SHIM_UNLOCK();
			ext_tsk();
			/* ここには戻らない */
		}
		/*
		 *  段4: 他タスクの削除。`ter_tsk` は**同期的に**休止へ落とすので、
		 *  そのまま `del_tsk` できる＝**この経路は回収を遅らせない**。
		 *  `del_tsk` が失敗したら**スロットを返さない**（ID がまだ生きているのに
		 *   スロットを再利用すると、次の生成が生きている ID を上書きする）。
		 *   失敗は `esp_shim_tsk.c` が数えて診断に出す＝黙らない。
		 */
#ifdef ESP_SHIM_DELTSK_COUNT
		esp_shim_deltsk_other++;
#endif
		if (esp_shim_tsk_terminate(t->tskid)) {
			SHIM_LOCK();
			t->tskid = (ID) 0;
			t->pend_del = false;
			t->used = false;
			SHIM_UNLOCK();
		}
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
#ifdef M5_SHIM_AUDIO
	/*  台帳5: 音声プールも探す（見つからなければプール外タスクの代表を返す従来どおり）。 */
	for (i = 0U; i < SHIM_AUDIO_NTSK; i++) {
		if (shim_audio_tsk[i].used && shim_audio_tsk[i].tskid == self) {
			return((void *)&shim_audio_tsk[i]);
		}
	}
#endif /* M5_SHIM_AUDIO */
	return((void *)&shim_main_thread_sem);	/* プール外タスクの代表 */
}

/*
 *  2026-07-26: **golden を黙って変えていたので隔離した。**
 *
 *  この 101 行（task notification 実装）は 2026-07-25 の commit a7ecc9a で
 *  「音声のため」に**この**ファイルへ入った。しかし実測すると:
 *   **`A1_VARIANT=m5`（音声を積む唯一の構成）は `esp/shim/esp_shim.c` を
 *     リンクしていない**（`build.ninja` に参照 0 件）。音声が使うのは
 *     `m5/shim/m5_kernel_shim.c` 側の実装である。
 *   一方 `esp_shim.c` は **wifi×2・ble** がリンクする。
 *  ⇒ **使われない先で golden 3 本（s3-wifi/s3-ble/lx6-wifi）を変えていた。**
 *    実測: s3-ble は golden `b2d322d4…` に対し `d3cdd4b4…` になっていた。
 *    これは私が「golden に影響しうるなら sha を確認する」を**怠った**結果である。
 *
 *  ⇒ 既定で**コンパイルしない**ようにして golden を戻す。必要になったら
 *    `-DESP_SHIM_TASK_NOTIFY` を付ける（実装はそのまま残してある）。
 */
#if defined(ESP_SHIM_TASK_NOTIFY)
/*
 *  自タスクのプールスロット。プール外タスクなら NULL。
 *  `esp_shim_task_get_current()` はプール外だと `&shim_main_thread_sem` を返すので、
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
 *  `ulTaskNotifyTake(clear_on_exit, timeout)` 相当。
 *  通知値が >0 になるまで待ち、抜けるとき clear なら 0 クリア・でなければ 1 減算。
 *  **待つ前の値**を返す（FreeRTOS の意味）。timeout は ms、0xFFFFFFFF で無限待ち。
 *
 *  競合の扱い: 値の判定を割込み禁止で行い、**解除してから** `slp_tsk` する。
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
		return(0U);		/* プール外タスクは通知値を持てない */
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
 *  `xTaskNotifyGive(handle)` 相当。対象タスクの通知値を +1 して起こす。
 *  `wup_tsk` の `E_QOVR`（既にラッチ済み）と `E_OBJ`（起きている）は正常。
 */
void
esp_shim_task_notify_give(void *task_handle)
{
	SHIM_TSK	*t = (SHIM_TSK *) task_handle;

	if ((t == NULL) || (t == (SHIM_TSK *)&shim_main_thread_sem)) {
		return;			/* プール外の代表ハンドルを参照しない */
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
	uint64_t			period_us;		/* 0ならワンショット */
	/*  2026-08-14: period_us を uint32→uint64 へ広げた。ms 指定の
	 *  `_timer_arm` は ms を 1000 倍した値が 32bit に収まらないことがある
	 *  （実測: blob は 0xfffffffe ms ≒ 49.7 日を「事実上無期限」の
	 *  番人として使う）。詳細は esp_shim_timer_arm_ms() のコメント。 */
} SHIM_TIMER;

static SHIM_TIMER *shim_timer_list;		/* 全タイマ（生成順） */

/*
 *  タイマタスクの `twai_sem` が E_OK/E_TMOUT 以外を返した回数（2026-08-14）。
 *  「起きてはならないことが起きた」ことを黙らせないための公開カウンタ。
 */
volatile uint32_t	esp_shim_timer_wait_err;

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

/*
 *  2026-08-14: 内部の 64bit 版。ms 指定の `_timer_arm` は us へ直すと
 *  32bit に収まらないことがあるため（下記 esp_shim_timer_arm_ms 参照）、
 *  期限計算は 64bit で行う。us 指定の `_timer_arm_us`（osi の契約が
 *  uint32）は下のラッパから入る。
 */
void
esp_shim_timer_arm_us64(void *ptimer, uint64_t us, bool_t repeat)
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
esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat)
{
	esp_shim_timer_arm_us64(ptimer, (uint64_t) us, repeat);
}

/*
 *  ms 指定（osi の `_timer_arm`）。
 *
 *  **`tmout * 1000U` を 32bit で計算してはならない**（2026-08-14 実機実測）:
 *  Wi-Fi blob は association 時（`state: init -> auth`）に
 *  `_timer_arm(ptimer, 0xfffffffe, false)`（= 4,294,967,294 ms ≒ 49.7 日）を
 *  発行する——「事実上発火させない」という意味の番人である。これを 32bit で
 *  1000 倍すると 0xfffff830 us ＝ **約 4,294.97 秒**へ折り返り、49.7 日の
 *  タイマが 71.6 分のタイマに化ける。しかもその期限は FMP3 の
 *  `TMAX_RELTIM`（4,000,000,000us）を超えるため、タイマタスクの `twai_sem`
 *  が E_PAR で即座に返り続ける＝**優先度 2 のタスクが 40 万回/秒で空転して
 *  全タスクを飢餓させる**という現行バグの引き金になっていた。
 *  記録: `.steering/20260814-wifi-disconnect-hang/`。
 */
void
esp_shim_timer_arm_ms(void *ptimer, uint32_t ms, bool_t repeat)
{
	esp_shim_timer_arm_us64(ptimer, (uint64_t) ms * 1000ULL, repeat);
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
#ifdef M5_A4_HANGDIAG
	/*
	 *  2026-08-14: 「esp_wifi_disconnect() 後に全タスクが止まる」の切り分け。
	 *  本タスクは優先度 2＝logtask(3)・NET_TSK(4)・MAIN_TASK(10) より高いので、
	 *  ここが空転すると**割込みだけが動いてタスクが 1 つも進まない**という
	 *  観測像になる。1 秒窓あたりの周回数を数え、閾値を超えたら「どちらの
	 *  経路で」「何を」回しているのかを同期出力する（.steering/20260814-wifi-disconnect-hang/）。
	 */
	extern void m5_log_now(const char *msg);
	extern void m5_log_now_u32(const char *msg, unsigned int v);
	int64_t		hd_win_t0 = esp_shim_time_us();
	uint32_t	hd_win_iter = 0U;
	uint32_t	hd_prints = 0U;
	void		(*hd_last_fn)(void *) = NULL;
	uint32_t	hd_last_wait = 0U;
	int32_t		hd_last_ercd = 0;
	uint32_t	hd_n_fire = 0U;
	uint32_t	hd_n_wait = 0U;
#endif

	for (;;) {
		SHIM_TIMER	*t;
		int64_t		now = esp_shim_time_us();
		int64_t		next = 0;
		void		(*fn)(void *) = NULL;
		void		*arg = NULL;

#ifdef M5_A4_HANGDIAG
		hd_win_iter++;
		if ((now - hd_win_t0) >= 1000000) {
			if ((hd_win_iter > 2000U) && (hd_prints < 24U)) {
				hd_prints++;
				m5_log_now_u32("[HD] timer_task 1秒あたりの周回数=", hd_win_iter);
				m5_log_now_u32("[HD]   発火経路の回数=", hd_n_fire);
				m5_log_now_u32("[HD]   待ち経路の回数=", hd_n_wait);
				m5_log_now_u32("[HD]   最後に呼んだ fn=",
							   (unsigned int)(uintptr_t) hd_last_fn);
				m5_log_now_u32("[HD]   最後の twai_sem tmout=", hd_last_wait);
				m5_log_now_u32("[HD]   最後の twai_sem ercd=",
							   (unsigned int) hd_last_ercd);
				m5_log_now_u32("[HD]   now_us(下位32)=", (unsigned int) now);
				if (hd_prints <= 2U) {
					SHIM_TIMER	*d;
					unsigned int	i = 0U;

					for (d = shim_timer_list; d != NULL; d = d->next) {
						i++;
						m5_log_now_u32("[HD]   list# ", i);
						m5_log_now_u32("[HD]     key=",
									   (unsigned int)(uintptr_t) d->key);
						m5_log_now_u32("[HD]     fn=",
									   (unsigned int)(uintptr_t) d->fn);
						m5_log_now_u32("[HD]     deadline_hi=",
									   (unsigned int)(d->deadline_us >> 32));
						m5_log_now_u32("[HD]     deadline_lo=",
									   (unsigned int) d->deadline_us);
						m5_log_now_u32("[HD]     period_us=", d->period_us);
					}
					m5_log_now_u32("[HD]   list 件数=", i);
				}
			}
			hd_win_t0 = now;
			hd_win_iter = 0U;
			hd_n_fire = 0U;
			hd_n_wait = 0U;
		}
#endif

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
#ifdef M5_A4_HANGDIAG
			hd_n_fire++;
			hd_last_fn = fn;
#endif
			fn(arg);
			continue;			/* 他の期限到来タイマを続けて処理 */
		}

		{
			TMO	tmo;
			ER	ercd;

			if (next == 0) {
				tmo = TMO_FEVR;
			}
			else {
				int64_t wait = next - now;

				if (wait < 1000) {
					wait = 1000;
				}
				/*
				 *  ---- 上限で頭打ちにする（2026-08-14 の現行バグの根治）----
				 *  FMP3 の `twai_sem` は `tmout > TMAX_RELTIM`（4,000,000,000us
				 *  ＝約 4,000 秒）を **E_PAR で即座に弾く**（`VALID_TMOUT`、
				 *  fmp3_core/kernel/check.h:92）。ここで頭打ちにしないと、
				 *  期限が 4,000 秒より先の**タイマが 1 つでも**残っていて他に
				 *  近い期限が無いとき、本ループは待たずに回り続ける。本タスクは
				 *  優先度 2（ESP_SHIM_TIMER_TASK_PRI）＝ LOGTASK(3)・NET_TSK(4)・
				 *  MAIN_TASK(10) より高いので、**割込みだけが動き、タスクは
				 *  1 つも進まない**という全系停止になる。
				 *  実測（CoreS3・.steering/20260814-wifi-disconnect-hang/）:
				 *    40 万回/秒の空転が 295 秒続き、その間 main_task も logtask も
				 *    NET_TSK も 1 命令も進まなかった。
				 *  頭打ちは意味論を変えない——起きたら `now`/`next` を採り直して
				 *  もう一度待つだけである（長い待ちを分割するだけ）。
				 */
				if (wait > (int64_t) TMAX_RELTIM) {
					wait = (int64_t) TMAX_RELTIM;
				}
				tmo = (TMO) wait;
			}
#ifdef M5_A4_HANGDIAG
			hd_n_wait++;
			hd_last_wait = (uint32_t) tmo;
#endif
			ercd = twai_sem(SHIM_TIMER_SEM, tmo);
#ifdef M5_A4_HANGDIAG
			hd_last_ercd = (int32_t) ercd;
#endif
			/*
			 *  ---- 戻り値を捨てない（同上）----
			 *  正常は E_OK（誰かが sig_sem した）と E_TMOUT（期限まで待った）
			 *  だけ。それ以外は**起きてはならない**が、起きたときに黙って
			 *  回り続けると上記の全系停止に戻る。数え、申告し、そして
			 *  **必ず 1ms は寝る**（この経路が 100% CPU を占めないことを
			 *  構造的に保証する）。
			 */
			if ((ercd != E_OK) && (ercd != E_TMOUT)) {
				esp_shim_timer_wait_err++;
				if ((esp_shim_timer_wait_err <= 4U)
					|| ((esp_shim_timer_wait_err % 1024U) == 0U)) {
					syslog(LOG_ERROR,
						   "esp_shim: timer_task twai_sem ercd=%d tmo=%d n=%d"
						   " -> 1ms 待って継続（空転防止）",
						   (int_t) ercd, (int_t) tmo,
						   (int_t) esp_shim_timer_wait_err);
				}
				(void) dly_tsk(1000U);
			}
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
	/*  BT割込みsource分離診断（TOPPERS_S3_BT_INTR_DIAG，esp/bt/bt_shim.c
	 *  参照）が有効なビルドでは，esp_intr_alloc()がBTコントローラ初期化
	 *  シーケンス中にesp_shim_set_isr()を呼ぶ．このsyslogは無条件発行のため，
	 *  診断ビルドでBT初期化を不安定化させないよう抑制する（既定ビルドの
	 *  挙動は変更しない＝非診断ビルドは従来通り出力する）。
	 *  .steering/20260709-ble-adv-storm-source/steering.md §3-4参照。 */
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

#if defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* C5: esp_shim_intr_c5.c の DEF_INH 入口から（段4 Task 3） */
/*
 *  ESP32-C6: blob 用の線 1..15 の DEF_INH 入口は esp/shim/esp_shim_intr_intmtx.c
 *  （esp_shim_intmtx_inthdr_n）が持ち、そこから本 TU の shim_isr_tbl[] 経由の
 *  ディスパッチ（shim_int_dispatch。static）へ戻ってくるための口。
 *  ディスパッチの中身は S3/LX6 と 1 命令も変えない（esp_shim_int_count[] 等の
 *  既存の診断をそのまま使う）。段4 Task 3（2026-09-14）。
 */
void
esp_shim_wifi_int_dispatch(int intno)
{
	if (intno >= 0 && intno <= ESP_SHIM_MAX_WIFI_INTNO) {
		shim_int_dispatch(intno);
	}
}
#endif /* TOPPERS_ESP32C6 */

/*
 *  cfg（esp_shim.cfg）でDEF_INHする入口（blobが使う線の分だけ用意）
 */
#ifndef ESP_SHIM_DYNISR_WIFI
void esp_shim_inthdr_0(void) { shim_int_dispatch(0); }
void esp_shim_inthdr_1(void) { shim_int_dispatch(1); }
void esp_shim_inthdr_2(void) { shim_int_dispatch(2); }
void esp_shim_inthdr_3(void) { shim_int_dispatch(3); }
#endif /* !ESP_SHIM_DYNISR_WIFI */

/*
 *  無印ESP32 BTコントローラblob（libbtdm_app.a）がxt_set_interrupt_handler()
 *  経由で動的に使うCPU割込み線5・7・8用（2026-07-15、W3調査で実機トレース
 *  （esp_shim: set_isr intno=5/7/8）から確認）。esp_shim_inthdr_0〜3と
 *  同じ仕組み（shim_isr_tbl[]経由のディスパッチ）をそのまま流用する。
 *  従来はこれらの線にDEF_INH登録が無く、shim_isr_tbl[5/7/8]への登録は
 *  カーネルから一切呼ばれないデッドコードだった（未修正時、
 *  UART0がINT5を静的に占有していたため実害が表面化していなかった。
 *  .steering/20260714-esp32-classic-wifi-bt/steering.md 追記2④参照）。
 *
 *  S3非回帰ガード（2026-07-15）：esp_shim.cfgの線5/7/8ブロックと対を成す。
 *  ESP32-S3ではこれらのDEF_INH登録を除外する（線5がUSART_INTNOと衝突）ため、
 *  ここも同じTOPPERS_ESP32_LX6ガードで囲み、S3では未使用関数warningを避ける。
 */
#if defined(TOPPERS_ESP32_LX6) && !defined(ESP_SHIM_DYNISR_LX6)
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
#if defined(ESP_SHIM_DYNISR_L3) || defined(ESP_SHIM_DYNISR_WIFI) \
	|| (defined(ESP_SHIM_DYNISR_LX6) && defined(TOPPERS_ESP32_LX6))
/*
 *  BL-H-6（2026-08-11 段1 / 2026-08-13 段2・段3・opt-in・既定 OFF）:
 *  静的 DEF_INH の入口を動的 ISR（acre_isr）へ載せ替える。
 *
 *  ISR の型は `void (*)(EXINF)` で、DEF_INH の `void (*)(void)` と違い
 *  引数を取る。線番号を EXINF で受けて既存の shim_int_dispatch() へ
 *  渡すだけにする——**ディスパッチの中身は静的版と 1 命令も変えない**
 *  （変えると「動的化のコスト」と「実装差のコスト」が混ざって測れない）。
 *
 *  登録は esp_shim_initialize() から 1 回だけ行う。
 */
static void
esp_shim_dynisr(EXINF exinf)
{
	shim_int_dispatch((int) exinf);
}

/*
 *  対象の線（cfg の ENA_DYNISR と**必ず一致させる**。
 *  ここと cfg がずれると、ずれた線は acre_isr が E_OBJ で失敗するか、
 *  逆に DEF_INH が無いまま誰も拾わない線ができる）。
 */
static const INTNO	shim_dynisr_lines[] = {
#ifdef ESP_SHIM_DYNISR_WIFI
	0, 1, 2, 3,
#endif
#if defined(ESP_SHIM_DYNISR_LX6) && defined(TOPPERS_ESP32_LX6)
	5, 7, 8,
#endif
#ifdef ESP_SHIM_DYNISR_L3
	23, 27,
#endif
};
#define SHIM_DYNISR_NLINE \
			(sizeof(shim_dynisr_lines) / sizeof(shim_dynisr_lines[0]))

/*  acre_isr が返した ID（解放はしない。系の寿命と同じ）  */
static ID	shim_dynisr_id[SHIM_DYNISR_NLINE];

static void
shim_dynisr_setup(void)
{
	T_CISR	cisr;
	uint_t	k;

	for (k = 0U; k < (uint_t) SHIM_DYNISR_NLINE; k++) {
		cisr.isratr = TA_NULL;
		cisr.exinf  = (EXINF) shim_dynisr_lines[k];
		cisr.intno  = shim_dynisr_lines[k];
		cisr.isr    = esp_shim_dynisr;
		cisr.isrpri = 1;			/*  静的版と同じく単一。優先順位の意味は無い  */
		shim_dynisr_id[k] = acre_isr(&cisr);
		/*
		 *  失敗を沈黙させない。ここで失敗するとその線の割込みが
		 *  どこにも届かなくなり、症状は「Wi-Fi/BLE が動かない」になる
		 *  （AID_ISR の個数不足なら E_NOID が返る）。
		 *
		 *  syslog ではなく target_fput_log() の同期出力を使う:
		 *  この登録は esp_shim_initialize() の冒頭＝**起動直後**に走り、
		 *  logtask のドレインが追いつかない局面である（実際、段3 の初回
		 *  走行では syslog 版のこの行が 1 本も出ず「登録されていない」
		 *  ように見えた——ログに `messages are lost` が出ていた）。
		 *  この行は「動的 ISR が張れたか」の唯一の確認口なので、消えては困る。
		 */
		{
			char	buf[] = "[DYNISR] line=..  erid=..\n";
			int		v;

			v = (int) shim_dynisr_lines[k];
			buf[14] = (char) ('0' + ((v / 10) % 10));
			buf[15] = (char) ('0' + (v % 10));
			v = (int) shim_dynisr_id[k];
			buf[23] = (char) ((v < 0) ? '-' : ('0' + ((v / 10) % 10)));
			buf[24] = (char) ('0' + ((v < 0 ? -v : v) % 10));
			for (v = 0; buf[v] != '\0'; v++) {
				target_fput_log(buf[v]);
			}
		}
	}
}
#endif /* ESP_SHIM_DYNISR_L3 || _WIFI || _LX6 */

#ifndef ESP_SHIM_DYNISR_L3
void esp_shim_inthdr_23(void) { shim_int_dispatch(23); }
void esp_shim_inthdr_27(void) { shim_int_dispatch(27); }
#endif /* !ESP_SHIM_DYNISR_L3 */

/*
 *		初期化
 */
void
esp_shim_initialize(void)
{
	static bool_t initialized = false;

	if (!initialized) {
		initialized = true;
#ifdef M5_USE_ESP_SHIM
		/*  合成構成では m5 が先に初期化している可能性がある。
		 *  素の heap_initialize() を呼ぶと**確保済みブロックを全部捨てる**ので、
		 *  必ず冪等版を通すこと（上のコメント参照）。 */
		esp_shim_heap_init_once();
#else
		heap_initialize();
#endif
		(void) act_tsk(SHIM_TIMER_TSK);

#if defined(ESP_SHIM_DYNISR_L3) || defined(ESP_SHIM_DYNISR_WIFI) \
	|| (defined(ESP_SHIM_DYNISR_LX6) && defined(TOPPERS_ESP32_LX6))
		/*
		 *  BL-H-6: 動的 ISR を登録する（段1=線23/27、段2=線5/7/8、段3=線0-3）。
		 *  blob が esp_intr_alloc()（-> esp_shim_set_isr）で
		 *  shim_isr_tbl[] を埋めるより**前**でよい——登録するのは
		 *  「線 -> shim_int_dispatch」の入口だけで、実ハンドラは
		 *  従来どおり shim_isr_tbl[] から引くため。
		 */
		shim_dynisr_setup();
#endif

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
 *  高水位を**外から読む口**（2026-07-26）。
 *  これが無かったので、記録していた時代ですら誰も値を見られなかった。
 *  「計測しているつもり」で終わらせないために、**読み出しと総量をセットで出す**。
 *
 *  使い方（無線が動く板で 1 回走らせるだけでよい）:
 *      syslog_2(LOG_NOTICE, "[SHIM-HEAP] 総量=%u ピーク使用=%u",
 *               esp_shim_heap_total(), esp_shim_heap_peak_used());
 *  `ESP_SHIM_HEAP_SIZE` を削れるかは、この 2 つの数字だけで決まる。
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
	/*  一度も malloc されていなければ min_free は初期値のまま。
	 *  その場合は 0 ではなく **総量を返さない**——「測れていない」ことが
	 *  分かるように (size_t)-1 をそのまま外へ出す。
	 *  0 を返すと「使っていない」と読まれる。 */
	if (shim_heap_min_free == (size_t)-1) {
		return((size_t)-1);
	}
	return(sizeof(heap_area) - shim_heap_min_free);
}
#endif /* ESP_SHIM_HEAP_STATS */
