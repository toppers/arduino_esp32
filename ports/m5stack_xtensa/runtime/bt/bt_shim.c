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
 *  Bluetooth統合（Phase D-1）の周辺プリミティブ実装
 *
 *  bt.c（BTコントローラ本体）が直接呼ぶFreeRTOS API自体は
 *  bt/stub/include/freertos の *.h（esp/esp_shim.cへの委譲）で提供する．
 *  本ファイルはそれ以外の依存（esp_timer/esp_pm/esp_ipc/esp_partition）
 *  をまとめて提供する．設計・経緯はdocs/dev/esp-idf-integration.md
 *  Phase D／docs/bt-shim.md参照．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "kernel_cfg.h"

#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_ipc.h"
#if defined(TOPPERS_ESPIDF_SUPPLY)
/*  ESP-IDF v5.5.4 の esp_ipc.h は CONFIG_FREERTOS_UNICORE 時に esp_ipc_func_t を
 *  提供しない（単一コアではIPC不要のため）。本ファイルは単一コア用の
 *  esp_ipc_call_blocking シムを自前で提供するため、型を補う。
 *  （esp-hal-3rdparty master は CONFIG_ESP_IPC_ENABLE ガードなので型が出る＝
 *   ベースラインでは TOPPERS_ESPIDF_SUPPLY 未定義でこのブロックは無効）  */
typedef void (*esp_ipc_func_t)(void *arg);
#endif
#include "esp_partition.h"
#include "esp_intr_alloc.h"
#include "esp_random.h"

#include "esp_shim.h"
/*  BT 系統のキュー実体（ISR から受信できるリング）。保留起床の flush に使う。
 *  記録: `非公開作業記録/20260804-bthal-ring/` */
#include "esp_shim_ring.h"
#include "bt_cfg.h"

#define BT_LOCK()	uint32_t bt_lock_ = esp_shim_int_disable()
#define BT_UNLOCK()	esp_shim_int_restore(bt_lock_)

/*
 *  ------------------------------------------------------------------
 *  esp_shim_bt_clock_init()：BTベースバンド／MACのクロック有効化＋
 *  リセット解除（esp_bt_controller_init()の直前に呼ぶこと）
 *
 *  背景（C3との比較，導出過程はdocs/bt-shim.md 550-586行相当）：
 *  C3移植ではDirect Bootがesp_perip_clk_init()を通らずBBクロックが
 *  未設定という問題だったが，S3では本ポートのwifi側が既に
 *  esp_wifi_clock_init_pll()（esp/wifi/hal_src/periph_ctrl.c）経由で
 *  WIFI_CLK_EN_REG(0x60026014)へSYSTEM_WIFI_CLK_EN(0x00FB9FCF)を
 *  ORしており，C3で問題になったBBクロック差分ビット(bit6,11,12,16,17=
 *  0x00031840)はこの中に包含済み＝クロック自体は既に足りている。
 *
 *  S3固有の真因は別にある：vendored bt.c（esp_bt_controller_init内）は
 *  periph_module_enable(PERIPH_BT_MODULE)/periph_module_reset(...)で
 *  BT用のリセット解除を行う想定だが，本ポートのesp/wifi/hal_src/periph_ctrl.c
 *  はESP-IDF新方式（modem_clock/PCR）への移行に伴い，これらのレガシー
 *  APIの中身を__PERIPH_CTRL_ALLOW_LEGACY_API未定義でno-op化している
 *  （esp_wifi_adapter.cのperiph_module_reset呼出しコメント参照）。
 *  ＝WIFI_RST_EN_REG(0x60026018)のBT用リセットビット
 *  （BTBB_RST/RW_BTMAC_RST/RW_BTLP_RST/RW_BTMAC_REG_RST/RW_BTLP_REG_RST/
 *  BTBB_REG_RST，periph_ll_enable_clk_clear_rst(PERIPH_BT_MODULE)相当の
 *  リセットマスクと同一）が，esp_phy_enable()経由のphy_module_enable()
 *  （esp_bt_controller_enable内，btdm_controller_enable直前）まで一度も
 *  解除されない。btdm_controller_init（init側）がBB領域へ先にアクセスすると，
 *  リセット保持のままレジスタ書込みがドロップし，C3で見た「BLE assert
 *  emi.c 164」と同型の症状になり得る。
 *
 *  修正：controller_init直前でBTクロック（冪等な追いOR，過剰ビットなし）
 *  とBT用リセット解除を明示する。
 *  ------------------------------------------------------------------
 */
#define BT_SYSCON_WIFI_CLK_EN_REG	0x60026014U
#define BT_SYSCON_WIFI_RST_EN_REG	0x60026018U
/*  BTベースバンドの機能クロック差分ビット（bit6,11,12,16,17）．
 *  C3導出(bt-shim.md 550-586)と同一の実体（SYSTEM_WIFI_CLK_EN_REG＝
 *  SYSCON_WIFI_CLK_EN_REGはC3/S3共通アドレス0x60026014）。 */
#define BT_BB_CLK_EN_MASK			0x00031840U
/*  BT用リセットビット（syscon_reg.h）：
 *  SYSTEM_BTBB_RST(3)|SYSTEM_RW_BTMAC_RST(9)|SYSTEM_RW_BTLP_RST(10)|
 *  SYSTEM_RW_BTMAC_REG_RST(11)|SYSTEM_RW_BTLP_REG_RST(12)|
 *  SYSTEM_BTBB_REG_RST(13)＝periph_ll_get_rst_en_mask(PERIPH_BT_MODULE)と同一 */
#define BT_RST_CLR_MASK \
	((1UL << 3) | (1UL << 9) | (1UL << 10) | (1UL << 11) | (1UL << 12) | (1UL << 13))

void
esp_shim_bt_clock_init(void)
{
	uint32_t	val;

#ifdef TOPPERS_ESP32_LX6
	/*  無印ESP32(LX6/W2)：ペリフェラルベースがS3(0x60000000)と別で
	 *  0x3FF00000（DPORT）。S3のSYSCON_WIFI_CLK_EN(0x60026014)を無印で
	 *  叩くと未マップ領域アクセスでフォルト/ハングするため、esp32は
	 *  DPORTクロックゲート/リセットへ読み替える。
	 *    DPORT_WIFI_CLK_EN_REG  = 0x3FF000CC, BTクロック=DPORT_WIFI_CLK_BT_EN_M(0x61<<11)
	 *    DPORT_CORE_RST_EN_REG  = 0x3FF000D0, BTリセット=BTBB(3)|BTMAC(4)|RW_BTMAC(9)|RW_BTLP(10)
	 *  esp_wifi_clock_init_pll()で共通WIFI_CLKは設定済み。ここはBT分を追いOR
	 *  ＋BTリセット解除（冪等）。PRC_NUM=1のためDPORT読みエラッタは非該当。 */
	#define BT_ESP32_DPORT_WIFI_CLK_EN_REG	0x3FF000CCU
	#define BT_ESP32_DPORT_CORE_RST_EN_REG	0x3FF000D0U
	/*  BTクロック(DPORT_WIFI_CLK_BT_EN_M=0x61<<11=0x30800)＋WiFi/BT共通クロック
	 *  (DPORT_WIFI_CLK_WIFI_BT_COMMON_M=0x3c9)。共通ビットはBTのBB/PHY較正に必須。
	 *  esp32はS3のesp_wifi_clock_init_pll(0x60026014書込み)が非該当のため、
	 *  modem/BBクロックはここでまとめて有効化する（CPU 240MHzはW0で設定済み）。 */
	#define BT_ESP32_WIFI_CLK_BT_EN_M		(0x00030800U | 0x000003C9U)	/* 0x00030BC9 */
	#define BT_ESP32_BT_RST_MASK \
		((1U << 3) | (1U << 4) | (1U << 9) | (1U << 10))
	val = sil_rew_mem((void *)(uintptr_t) BT_ESP32_DPORT_WIFI_CLK_EN_REG);
	sil_wrw_mem((void *)(uintptr_t) BT_ESP32_DPORT_WIFI_CLK_EN_REG,
				val | BT_ESP32_WIFI_CLK_BT_EN_M);
	val = sil_rew_mem((void *)(uintptr_t) BT_ESP32_DPORT_CORE_RST_EN_REG);
	sil_wrw_mem((void *)(uintptr_t) BT_ESP32_DPORT_CORE_RST_EN_REG,
				val & ~BT_ESP32_BT_RST_MASK);
#else
	/*  クロック：BBクロック差分ビットのみ追いOR．esp_wifi_clock_init_pll
	 *  で既に設定済みの想定だが，BT単体ビルド（WiFi非併用）でも独立して
	 *  成立するよう冪等に行う（過剰ビットを避けるため全ビットORはしない）。 */
	val = sil_rew_mem((void *)(uintptr_t) BT_SYSCON_WIFI_CLK_EN_REG);
	sil_wrw_mem((void *)(uintptr_t) BT_SYSCON_WIFI_CLK_EN_REG, val | BT_BB_CLK_EN_MASK);

	/*  リセット解除：periph_module_reset(PERIPH_BT_MODULE)がno-op化されて
	 *  いる分を代替する（periph_ll_enable_clk_clear_rstのBT分と同一マスク）。 */
	val = sil_rew_mem((void *)(uintptr_t) BT_SYSCON_WIFI_RST_EN_REG);
	sil_wrw_mem((void *)(uintptr_t) BT_SYSCON_WIFI_RST_EN_REG, val & ~BT_RST_CLR_MASK);
#endif
}

/*
 *  ------------------------------------------------------------------
 *  portENTER/EXIT_CRITICAL実体（bt/stub/include/freertos/FreeRTOS.hの
 *  portMUX_TYPE／マクロから呼ばれる．SMP対応版）
 *
 *  背景（姉妹プロジェクトC3のbt-shim.md 1039-1087行の引き継ぎメモ）：
 *  C3（単一コア）はPhase D-1で「portENTER_CRITICAL(mux)が退避値を
 *  “mux変数自体”に格納する」実装（`*(mux) = esp_shim_int_disable()`）が
 *  原因のネスト崩壊バグを踏んだ（bt.cのglobal_int_muxをRW/LLDスタックが
 *  深くネスト取得し，内側の退避値0が外側の退避値を上書き→最外解放後も
 *  割込み禁止が残る→block-forever待ちがE_CTX→タスクexit→SWリセット）。
 *  C3の修正は「大域ネストカウンタ＋saved割込み状態，muxは無視」だったが，
 *  これは単一コア前提でSMP(S3, PRC_NUM=2)には流用不可（別コアの状態を
 *  混ぜてしまう／muxによるコア間排他が失われる）。
 *
 *  S3版の設計：
 *    (1) 割込みネスト状態・saved状態は「コア単位」で保持する
 *        （get_my_prcidx()でindex，本ファイルはWiFiのesp_shim.cとは
 *        独立にBT専用のネスト状態を持つ＝WiFi経路は不変）。
 *    (2) muxは無視せず，実際にスピンロックとして取得/解放する。
 *        ただし同一コアがRW/LLDスタックの都合で同一muxを再入する挙動は
 *        blob側の仕様なので，「所有コア＋再入カウント」を持つ再入可能
 *        スピンロック（ESP-IDF本家のspinlock_t同型）とする。
 *    (3) 実スピンロックのCAS実体はarch/xtensa_gcc/esp32s3/
 *        chip_kernel_impl.hのcore_cas（Xtensa S32C1I）と同一ロジックを
 *        bt_core_cas()として複製する（chip_kernel_impl.hはカーネル内部型
 *        (PCB/TCB)に依存しアプリ/シム層から直接includeできないため）。
 *        ATOMCTL未検証の注意はarch側コメント参照（内蔵SRAM限定）。
 *    (4) bt.cfgでBTタスク/ISRは全てCLS_PRC1（コア0固定）としており，
 *        運用上はクロスコア競合自体が発生しない設計とする。スピンロック
 *        自体は「将来コア1からBTを触る可能性」への備えとして正しく
 *        実装しておく（無視しない）。
 *  ------------------------------------------------------------------
 */

/*
 *  段3-4g（2026-08-06）: esp_shim_xcore_crit.h（段3-2a、esp_shim_bt_crit_wifi.c
 *  由来・実機実証済み）へ TNUM_PRCID>=2 の実装を委譲する。同ヘッダ冒頭の
 *  「bt_shim.c は本ヘッダへ追随させていない（既知の重複）」を解消する、
 *  独立した段（同ヘッダのコメントが指定した完了条件どおり）。
 *
 *  TNUM_PRCID<2（`seam-s3-ble` golden の既定）は #else 節が元のコードと
 *  バイト単位で同一であることで保つ——GATE は「seam-s3-ble がバイト不変」
 *  だけを主張する（`esp_shim_xcore_crit.h` 冒頭コメントの完了条件どおり）。
 *
 *  意味論の差（TNUM_PRCID>=2 の未検証パスにのみ影響。bt.cfg は全 BT
 *  タスク/ISR を CLS_PRC1 固定にしており、運用上クロスコア競合は現状
 *  発生しない設計——この差は「今動いているものを壊す」変更ではない）:
 *  旧実装の exit_critical は `mux->count > 0` だけで解放していた
 *  （所有者を見ない）。esp_shim_xcore_crit.h は所有者を確認し、
 *  他コアの mux への exit を触らずに数える（ESP-IDF 本家の
 *  spinlock_release と同じ契約）。他コアが保持中のロックを誤って
 *  解いてしまう既存の潜在的な穴を塞ぐ側の変更である。
 */
#if TNUM_PRCID >= 2

#include "esp_shim_xcore_crit.h"

static struct esp_shim_xcore_crit_state	bt_xcore_crit_state
		= ESP_SHIM_XCORE_CRIT_STATE_INIT;

void
esp_shim_bt_enter_critical(void *mux_raw)
{
	esp_shim_xcore_crit_enter(&bt_xcore_crit_state, mux_raw);
}

void
esp_shim_bt_exit_critical(void *mux_raw)
{
	uint_t		core = esp_shim_xcore_prcidx();
	uint32_t	nest_before;

	if (core >= ESP_SHIM_XCORE_MAX_CORES) {
		core = 0U;
	}
	/*
	 *  BT-4修正（steering §13）由来の flush 条件を、新しい state 構造体
	 *  越しに再現する。
	 *
	 *  2026-08-06（段3-4i・Codex レビュー指摘2 を実測で確認して修正）:
	 *  当初この関数は exit() 後に `nest[core]==0 && saved&0xF==0` だけを
	 *  見ており、**旧実装と等価ではなかった**。旧実装の flush は
	 *  `if (bt_crit_nest[core] > 0U) { ... }` の**内側**にあり、
	 *  「対応する enter があり、この exit で最外から抜けた」ときにしか
	 *  走らない。`nest` が既に 0 の状態で exit された場合（対応する
	 *  enter が無い不整合な呼出し）、旧実装は flush しないが、
	 *  当初の新実装は `nest==0` を満たすため **flush してしまう**
	 *  （`saved[core]` は初期値 0＝INTLEVEL 0 と読めるため条件を通る）。
	 *  ⇒ exit 前の nest を控え、「1 以上だったものが 0 になった」ときだけ
	 *  flush する。これで旧実装と厳密に同じ条件になる。
	 *
	 *  なお `exit()` は最外の exit でだけ nest[core] を 0 へ戻し、
	 *  saved[core] は最後に使った値のまま残す（クリアしない、
	 *  esp_shim_xcore_crit.h 参照）ので、exit() 直後にここで読める。
	 */
	nest_before = bt_xcore_crit_state.nest[core];
	esp_shim_xcore_crit_exit(&bt_xcore_crit_state, mux_raw);

	if ((nest_before > 0U)
			&& (bt_xcore_crit_state.nest[core] == 0U)
			&& ((bt_xcore_crit_state.saved[core] & 0xFU) == 0U)) {
		/*  2026-08-15（BL-G-3 段3）: ここに在った
		 *  `esp_shim_queue_flush_pending()`（DTQ 実装の保留送信を流す）は
		 *  削除した。DTQ 実装そのものが撤去されたためである。
		 *  流す相手はリング側の保留起床だけになった。 */
		esp_shim_ring_flush_wakes();
	}
}

uint32_t
esp_shim_bt_crit_nest_get(void)
{
	uint_t	core = esp_shim_xcore_prcidx();

	if (core >= ESP_SHIM_XCORE_MAX_CORES) {
		core = 0U;
	}
	return((uint32_t) bt_xcore_crit_state.nest[core]);
}

#else /* TNUM_PRCID >= 2 */

/*
 *  自コアindex取得／CAS実体：arch/xtensa_gcc/esp32s3/chip_kernel_impl.h
 *  のget_my_prcidx()／core_cas()と同一ロジックの独立実装。
 *  chip_kernel_impl.hはカーネル内部型（PCB/TCB等）に依存しており，
 *  アプリ/シム層のbt_shim.cから直接includeできないため複製する
 *  （実装の一致はコード同一性で担保．変更時は両方を追随させること）。
 */
Inline uint_t
bt_get_my_prcidx(void)
{
	uint32_t	id;

	Asm("rsr.prid %0 \n\t"
	    "extui %0, %0, 13, 1" : "=a"(id));
	return (uint_t) id;
}

#define BT_CRIT_MAX_CORES	2U

/*  bt/stub/include/freertos/FreeRTOS.hのportMUX_TYPEと同一レイアウト
 *  （owner: 0=未取得，取得中は「コアindex+1」／count: 再入回数）。
 *  ヘッダを共有せず独立定義とする（FreeRTOS.h側はkernel.hを引き込まない
 *  既存方針を踏襲，bt.c側の型はuint32_tのみで完結させる）。 */
struct esp_shim_bt_mux {
	volatile uint32_t	owner;
	volatile uint32_t	count;
};

static volatile uint32_t	bt_crit_nest[BT_CRIT_MAX_CORES];
static volatile uint32_t	bt_crit_saved[BT_CRIT_MAX_CORES];

void
esp_shim_bt_enter_critical(void *mux_raw)
{
	struct esp_shim_bt_mux	*mux = (struct esp_shim_bt_mux *) mux_raw;
	uint32_t	state;
	uint_t		core;

	/*  常に自コアの割込みを禁止する（Xtensaのrsilはコアローカルレジスタ
	 *  操作のため，ここは他コアの状態には触れない）。 */
	state = esp_shim_int_disable();

	core = bt_get_my_prcidx();
	if (core >= BT_CRIT_MAX_CORES) {
		core = 0U;	/* 安全側フォールバック（構成上到達しない想定） */
	}
	if (bt_crit_nest[core] == 0U) {
		bt_crit_saved[core] = state;	/*  最外だけ退避 */
	}
	bt_crit_nest[core]++;

	if (mux != NULL) {
		uint32_t	me = core + 1U;	/* 0=未取得と区別 */

		if (mux->owner == me) {
			mux->count++;		/*  同一コアの再入 */
		}
		else {
			/*  単一コア構成：自コアの割込みは既に禁止済みのため，
			 *  CASを使わない直接代入でも排他は成立する
			 *  （chip_kernel_impl.hのTNUM_PRCID<2分岐にはcore_casが
			 *  無いためコンパイル可能性のためにも分岐が必要）。 */
			mux->owner = me;
			mux->count = 1U;
		}
	}
}

void
esp_shim_bt_exit_critical(void *mux_raw)
{
	struct esp_shim_bt_mux	*mux = (struct esp_shim_bt_mux *) mux_raw;
	uint_t		core = bt_get_my_prcidx();

	if (core >= BT_CRIT_MAX_CORES) {
		core = 0U;
	}

	if (mux != NULL) {
		if (mux->count > 0U) {
			mux->count--;
			if (mux->count == 0U) {
				Asm("memw" ::: "memory");
				mux->owner = 0U;
			}
		}
	}

	if (bt_crit_nest[core] > 0U) {
		bt_crit_nest[core]--;
		if (bt_crit_nest[core] == 0U) {
			esp_shim_int_restore(bt_crit_saved[core]);	/*  最外だけ復元 */
			/*
			 *  BT-4修正（steering §13）：クリティカルセクション内で積まれた
			 *  保留を，INTLEVELが復元されサービスコール発行が可能になった
			 *  この時点で流す。復元後のINTLEVELが0でない場合（外側に別の
			 *  rsil区間がある等）は flush 内のサービスコールが E_CTX で
			 *  弾かれ得るため，flush 呼出し自体は復元INTLEVEL=0のときに
			 *  限る（残った保留は次の送受信の機会的 flush で回収される）。
			 *
			 *  2026-08-15（BL-G-3 段3）: ここに在った
			 *  `esp_shim_queue_flush_pending()`（DTQ 実装側）は削除した。
			 *  DTQ 実装そのものが撤去されたためである。
			 */
			if ((bt_crit_saved[core] & 0xFU) == 0U) {
				/*
				 *  2026-08-04（`非公開作業記録/20260804-bthal-ring/`）:
				 *  BT 系統のキューは `esp/bt/stub/include/freertos/queue.h`
				 *  経由で **`esp_shim_ring_*`（シム所有リング）** へ写した。
				 *  リングは臨界区間の中でもデータを積めるが、
				 *  受信待ちタスクの起床（`wup_tsk`）だけは `CHECK_UNL_MYSTATE`
				 *  ＝**CPU ロック中は E_CTX** なので保留される。
				 *  ⇒ INTLEVEL が 0 に戻ったここで流す（上の DTQ 側 flush と同型）。
				 *  流せなくても保留は消えない（次の send/recv の機会的 flush か
				 *   受信側の安全網刻みで回収される）。
				 */
				esp_shim_ring_flush_wakes();
			}
		}
	}
	/*  ネストが残っている場合はrsilでINTLEVEL=15のまま戻る（正しい：
	 *  外側の臨界区間はまだ継続中）。 */
}

/*
 *  一時診断（BT-4/E_CTX調査）：esp_shim_queue_send()側からbt_crit_nest
 *  （esp_shim_bt_enter_critical/exit_criticalのネスト深さ）を覗くための
 *  最小アクセサ。原因切り分け（A:L3割込みネストカウンタ未計上／
 *  B:タスクがportENTER_CRITICALを保持したままキュー送信）専用。
 *  非公開作業記録/20260709-ble-bt4-connection/steering.md参照。
 */
uint32_t
esp_shim_bt_crit_nest_get(void)
{
	uint_t	core = bt_get_my_prcidx();

	if (core >= BT_CRIT_MAX_CORES) {
		core = 0U;
	}
	return(bt_crit_nest[core]);
}

#endif /* TNUM_PRCID >= 2 */

/*
 *  2026-07-27 レビュー esp-2：esp_timer_* 群とタイマタスク（旧
 *  bt_timer_task）は esp/shim/esp_timer_shim.c へ切り出した（実装無改変・
 *  識別子のみ改名）。Wi-Fi 構成の esp_timer_* が wifi_stubs.c の no-op
 *  のままで PHY PLL トラッキングが走らなかったため，両構成で同じ実体を
 *  リンクする形へ改めたもの。cfg オブジェクトも esp/shim/esp_shim.cfg の
 *  ESP_TIMER_SEM／ESP_TIMER_TSK へ移動。
 */

/*
 *  ------------------------------------------------------------------
 *  esp_pm_lock_*（電源管理．Wi-Fi同様PS_NONE相当＝no-op）
 *  ------------------------------------------------------------------
 */
struct esp_pm_lock {
	int	dummy;
};

static struct esp_pm_lock	bt_pm_lock_dummy;

esp_err_t
esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg,
				   const char *name, esp_pm_lock_handle_t *out_handle)
{
	(void) lock_type; (void) arg; (void) name;
	*out_handle = &bt_pm_lock_dummy;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_delete(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_acquire(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_release(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_ipc_call_blocking（同期直接呼出し）
 *  ------------------------------------------------------------------
 *
 *  [2026-07-16 レビュー指摘#15：コメント更新] 元コメント「ESP32-C3は
 *  単一コアのため」は姉妹プロジェクトC3（本来物理単一コア）から流用した
 *  stale記述だった。本ファイル(bt_shim.c)はESP32-S3（デュアルコアLX7）と
 *  無印ESP32(デュアルコアLX6)の両方のBTビルドで共用されており、どちらも
 *  物理的にはマルチコアである。単純化（cpu_id引数を無視してその場で
 *  func(arg)を同期直接呼ぶだけ）が成立する実際の理由は、両ターゲットとも
 *  BTビルドが-DCONFIG_FREERTOS_NUMBER_OF_CORES=1（esp/build_bt_incflags*.txt）
 *  でBTを単一コア（コア0固定）に強制しているため。ESP-IDF v5.5.4の
 *  esp_ipc.hはCONFIG_FREERTOS_UNICORE時にesp_ipc_func_tを提供しない
 *  （単一コアではIPC不要という前提）のと同じ理由で、cpu_id間IPCという
 *  概念自体が本ポートのBTコンテキストでは発生しない（現状dead code：
 *  実際に別コアへディスパッチする呼び出し元が無い）。
 */
esp_err_t
esp_ipc_call_blocking(uint32_t cpu_id, esp_ipc_func_t func, void *arg)
{
	(void) cpu_id;
	func(arg);
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_partition_*（NVS/較正データ．Wi-Fi shim同様「常に存在しない」
 *  スタブ．CONFIG_BT_CTRL_LE_LOG_STORAGE_EN未定義＝該当コードは実行時
 *  未到達の見込みだがリンクは必要）
 *  ------------------------------------------------------------------
 */
const esp_partition_t *
esp_partition_find_first(esp_partition_type_t type,
						  esp_partition_subtype_t subtype, const char *label)
{
	(void) type; (void) subtype; (void) label;
	return(NULL);
}

esp_err_t
esp_partition_erase_range(const esp_partition_t *partition,
						   uint32_t offset, uint32_t size)
{
	(void) partition; (void) offset; (void) size;
	return(ESP_FAIL);
}

esp_err_t
esp_partition_write(const esp_partition_t *partition, uint32_t dst_offset,
					 const void *src, uint32_t size)
{
	(void) partition; (void) dst_offset; (void) src; (void) size;
	return(ESP_FAIL);
}

esp_err_t
esp_partition_mmap(const esp_partition_t *partition, uint32_t offset,
				   uint32_t size, esp_partition_mmap_memory_t memory,
				   const void **out_ptr, esp_partition_mmap_handle_t *out_handle)
{
	(void) partition; (void) offset; (void) size; (void) memory;
	(void) out_ptr; (void) out_handle;
	return(ESP_ERR_NOT_FOUND);
}

esp_err_t
esp_partition_munmap(esp_partition_mmap_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_random（bt.cが直接呼ぶ公開API名．実体はesp_shim_random）
 *  ------------------------------------------------------------------
 */
/*  bt-classic profile は esp_shim_libc.c を同時にリンクしており、そちらが
 *  同一実装の esp_random を持つ。二重定義になるのでこちらを畳む。 */
#if !defined(TOPPERS_BT_CLASSIC)
uint32_t
esp_random(void)
{
	return(esp_shim_random());
}
#endif /* !TOPPERS_BT_CLASSIC */

/*
 *  ------------------------------------------------------------------
 *  esp_intr_alloc/free/enable/disable（bt.cが直接呼ぶ標準割込み確保
 *  API．esp_wifi_adapter.cのset_intr_wrapper／esp_shim_set_isrと
 *  同じ仕組み（INTMTXルーティング＋esp_shim.cfgでDEF_INH済みの
 *  CPU割込み線）を流用する．
 *
 *  2026-07-09 multi-source修正（BLE advertising storm調査，
 *  非公開作業記録/20260709-ble-adv-storm-source/steering.md）：
 *  interrupt_alloc_wrapper（esp/bt/hal/bt.c）は実際にはesp_intr_allocを
 *  source 8, source 5 の2回呼ぶ（旧コメント「呼出しは1箇所」はstaleだった
 *  ＝静的解析で確認）．旧実装は両sourceを固定でCPU割込み線1へ配線し
 *  esp_shim_set_isr(1, …)を2回呼ぶため，2回目（source5）のhandler/argが
 *  1回目（source8）を上書きしていた．結果，source8発火時にsource5用
 *  handlerが走りsource8のstatusを正しくclearできず，advertising enable後
 *  の割込みストーム（0/秒→約10万/秒）を引き起こす機序が成立する．
 *
 *  修正：単一handleを配列化し，呼出し順で別々のCPU割込み線へ配線する
 *  （1個目→線1，2個目→線2）．esp_shim.cfgでDEF_INH済みの線0〜3は
 *  Wi-Fi/BT非同時ON前提でBT稼働中は空き（esp_shim_cfg.hコメント参照）。
 *  esp_intr_free/enable/disableもhandleから割当線を引いてper-handleに
 *  操作する（旧実装は全て線1決め打ちだった）。
 *  3個目以降の呼出しは想定外だが，クラッシュを避けるため既存の線2へ
 *  安全側フォールバックする（esp_shim_bt_intr_alloc_countが2を超える
 *  値になること自体が異常検知になる）。
 * ------------------------------------------------------------------
 */
/*
 *  割込みマトリクスのソース→CPU線ルーティング。
 *
 *  無印ESP32(LX6)：DPORT系。ソースNのCPU線MAPレジスタは
 *  DPORT_PRO_MAC_INTR_MAP_REG (=DR_REG_DPORT_BASE+0x104=0x3ff00104) を起点に
 *  0x3ff00104 + source*4（esp_wifi_adapter.cのINTMTX_MAP_REGと同じ算出式）。
 *
 *  ESP32-S3：0x600C2000 起点。ソースNのMAPレジスタは 0x600C2000 + source*4
 *  （幅5bit[4:0]・リセット値16・値域0〜31＝Xtensa CPU割込み番号）。
 *  【esp-idf/components/soc/esp32s3/register/soc/interrupt_core0_reg.h】
 *
 *  2026-07-25 訂正（非公開作業記録/20260724-camera-fullframe/
 *     bt-shim-s3-intmtx-evaluation.md）。旧コメントは「S3固有＝0x600C2000ベースに
 *     **一括enableビットマップ＋線別優先度レジスタを持つ**」と書いていたが，
 *     **これは事実と逆**である。S3(Xtensa)にはINTCが無く，enable用ビットマップも
 *     線別優先度レジスタも**存在しない**。0x600C2000 領域にあるのはMAPレジスタ99本と
 *     INTR_STATUS/CLOCK_GATE/DATEだけ。旧実装が叩いていた
 *       ENABLE_REG=+0x104  → 実体は APB_ADC_INT_MAP_REG（source 65）
 *       PRI_REG(n)=+0x114+n*4 → n=23で source 92, n=27で source 96(USB Serial/JTAG)
 *     であり，これらのマクロは **ESP32-C3(RISC-V INTC) のヘッダ**
 *     （soc/esp32c3/.../interrupt_core0_reg.h の CPU_INT_ENABLE_REG=+0x104 /
 *     CPU_INT_PRI_0_REG=+0x114）を S3 へ持ち込んだ誤り（文字列一致で確定）。
 *
 *  正しい仕組み（S3・LX6とも）:
 *   - ルーティング＝ MAP(source) = cpu_line（この1行だけ。下の :alloc 参照）
 *   - enable/disable ＝ Xtensa INTENABLE（ena_int/dis_int）
 *   - 優先度 ＝ CPU割込み線のハードウェア固定レベル（線23/27は
 *     XCHAL_INT{23,27}_LEVEL=3・TYPE=EXTERN_LEVEL。S3/LX6とも同じ）
 *  ⇒ PRI_REG/ENABLE_REG への書込みは不要どころか有害だった。PRI_REG 書込みは
 *     source 92/96 の MAP を線2へ奪い（USJコンソール構成で実害），ENABLE_REG は
 *     線23/27ではRMWのフィールド外で no-op。両者を削除した。
 */
#if defined(TOPPERS_ESP32_LX6)
#define BT_INTMTX_MAP_REG(src)  (0x3FF00104U + (src) * 4U)
#else
#define BT_INTMTX_BASE_ADDR   0x600C2000U
#endif
#define BT_INTR_CPU_LINE      1U	/* 1個目のsource（実測ではsource8）用．flagsがLEVEL3を
									要求しない呼出し向けの旧来Level-1配線（後方互換フォールバック） */
#define BT_INTR_CPU_LINE2     2U	/* 2個目のsource（実測ではsource5）用．線0/3はcfgでDEF_INH済みの予備 */
#define BT_INTR_MAX_SLOT      2U	/* 実測で使用されるsource数（8,5の2つ） */

/*
 *  BT-4修正：実ESP-IDF(esp/bt/hal/bt.c interrupt_alloc_wrapper())は
 *  ESP_INTR_FLAG_LEVEL3を要求するが、旧実装はflags引数を無視しLevel-1固定の
 *  線1/2へ配線していた（ESP32-S3のCPU割込み番号0〜10は11を除きハードウェア
 *  固定でレベル1のため、要求されたレベル3にならず、connection eventの
 *  厳密なタイミングに応えられなかった）。ESP32-S3のレベル3対応番号
 *  {11,15,22,23,27,29}のうちLEVEL型かつSPECIAL予約でない23・27を使う
 *  （esp_shim.cfgのDEF_INH(23/27,...)で登録済み。
 *  .claude/plans/sparkling-forging-taco.md参照）。
 */
#define BT_INTR_CPU_LINE3_A   23U	/* 1個目のsource用Level-3線 */
#define BT_INTR_CPU_LINE3_B   27U	/* 2個目のsource用Level-3線 */

struct intr_handle_data_t {
	int			used;
	int			source;
	uint32_t	cpu_line;
};

static struct intr_handle_data_t	bt_intr_slot[BT_INTR_MAX_SLOT];

/*
 *  BT-3診断：esp_intr_alloc()が割り当てたsource番号・CPU割込み線を記録する
 *  グローバル（C3のBT_BBストーム＝source5＝ETS_BT_BB_INTR_SOURCEと同一
 *  sourceかどうかを実機で照合するため．docs/bt-shim.md（C3側）
 *  1352-1444行参照）．UARTログ確認はmain_task側でesp_intr_alloc()から
 *  戻った後にまとめて出す（esp_intr_alloc()自体の内部でsyslog()を
 *  呼ぶと，実測でBTコントローラ初期化シーケンス内という特殊な文脈のため
 *  まれに以降の初期化が停止する事象を観測した．対照実験で確認．詳細は
 *  JTAG_DEBUG.md追記58．そのため単純な代入のみで安全側に倒す）．
 *  esp_shim_bt_last_intr_line[]は対応するsourceが配線されたCPU割込み線
 *  （ble_hs_smoke.cの診断出力がesp_shim_int_count[line]を読むための
 *  source→line対応表として使う）．
 */
volatile int32_t	esp_shim_bt_last_intr_source[2] = { -1, -1 };
volatile uint32_t	esp_shim_bt_last_intr_line[2];
volatile uint32_t	esp_shim_bt_intr_alloc_count;

#if defined(TOPPERS_BT_AC_INTMTX_DUMP)
/*  AC-btshim2.md B2-5 用（opt-in・既定 OFF＝golden 中立）。実体化した
 *  enable/disable/free を blob が実際に何回呼ぶかを実機で数える。
 *  dis_calls>0 かつ int_count が止まるなら実体化が回帰＝要 revert（AC §4）。 */
volatile uint32_t	esp_shim_bt_ena_calls;
volatile uint32_t	esp_shim_bt_dis_calls;
volatile uint32_t	esp_shim_bt_free_calls;
#endif

#ifdef TOPPERS_S3_BT_INTR_DIAG
/*
 *  診断計装（TOPPERS_S3_BT_INTR_DIAG，既定OFF）．
 *
 *  発火回数自体は追加のカウンタを持たず，esp_shim_int_count[cpu_line]
 *  （esp_shim.cのshim_int_dispatch()がディスパッチ毎に既にインクリメント
 *  している既存カウンタ）をそのまま流用する．本修正でsource:CPU線が
 *  1:1になるため，line別カウンタがそのままsource別カウンタとして読める
 *  ＝ISR側に追加オーバヘッドを載せない．
 *
 *  加えて，ISR起動直前のXtensa CPU割込みpendingビット（rsr.interrupt．
 *  BT_BB等，値が未解決なS3固有レジスタには一切依存しない汎用値）の
 *  sticky ORを，実handlerを挟むトランポリン経由で低オーバヘッド記録する
 *  （count++; sticky|=pend; 方式．target_timer.cのrsr.interrupt読み出しと
 *  同一idiom）．sourceのstatus/mask/clearレジスタ自体はS3で未解決のため
 *  （C3のアドレスを流用してはならない，steering.md §3-(4)），ここでは
 *  読みに行かない＝実機でのレジスタ再解決はT5（実機採取）側の作業とする．
 *
 *  source/cpu_lineはesp_shim_bt_last_intr_source[]/esp_shim_bt_last_intr_line[]
 *  （常時有効）で既に追える値なので，診断専用構造体では重複保持しない
 *  （ble_hs_smoke.c等，他ファイルからは構造体型を介さずこのflat配列だけ
 *  externすればよい＝ヘッダ非公開のローカル型を跨いで公開する必要がない）．
 */
volatile uint32_t	esp_shim_bt_intr_diag_sticky[BT_INTR_MAX_SLOT];

static intr_handler_t	bt_intr_diag_real_fn[BT_INTR_MAX_SLOT];
static void				*bt_intr_diag_real_arg[BT_INTR_MAX_SLOT];

static void
bt_intr_diag_common(uint32_t idx)
{
	uint32_t	pend;

	Asm("rsr.interrupt %0" : "=a"(pend));
	esp_shim_bt_intr_diag_sticky[idx] |= pend;
	if (bt_intr_diag_real_fn[idx] != NULL) {
		(*bt_intr_diag_real_fn[idx])(bt_intr_diag_real_arg[idx]);
	}
}

static void bt_intr_diag_tramp0(void *arg) { (void) arg; bt_intr_diag_common(0U); }
static void bt_intr_diag_tramp1(void *arg) { (void) arg; bt_intr_diag_common(1U); }

static void	(*const bt_intr_diag_tramp[BT_INTR_MAX_SLOT])(void *) = {
	bt_intr_diag_tramp0, bt_intr_diag_tramp1,
};
#endif /* TOPPERS_S3_BT_INTR_DIAG */

esp_err_t
esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
			   intr_handle_t *ret_handle)
{
	uint32_t					idx;
	uint32_t					cpu_line;
	struct intr_handle_data_t	*slot;
	intr_handler_t				isr_fn = handler;
	void						*isr_arg = arg;
	bool_t						level3 = ((flags & ESP_INTR_FLAG_LEVEL3) != 0);

	idx = esp_shim_bt_intr_alloc_count;
	if (idx < 2U) {
		esp_shim_bt_last_intr_source[idx] = source;
	}
	esp_shim_bt_intr_alloc_count++;

	/*
	 *  ★内部ソース（負値）はマトリクスを通らない。
	 *
	 *  ESP-IDF は ETS_INTERNAL_*_INTR_SOURCE を負値で表し、CPU 割込み番号は
	 *  Xtensa 側で固定である（esp_intr_alloc.h）。ESP32 BT コントローラは
	 *  ETS_INTERNAL_SW1_INTR_SOURCE(-5) を要求し、これは CPU 割込み 29
	 *  （SW1・レベル3）に当たる。
	 *
	 *  旧実装は負値を素通しし、LEVEL3 フラグが無いために線 1 へ載せたうえ、
	 *  マトリクスの MAP レジスタを 0x3FF00104 + (-5)*4 = 0x3FF000F0
	 *  （読み出し専用の INTR_STATUS_1）へ書こうとしていた。ハンドラは線 1 に
	 *  付き、コントローラが上げる SW1 は誰も受けない。2026-09-02 実測で
	 *  esp_bt_controller_enable() が返らない状態がこれ。
	 */
	if (source < 0) {
		int32_t	internal_line;

		switch (source) {
		case -1: internal_line = 6;  break;	/* TIMER0 */
		case -2: internal_line = 15; break;	/* TIMER1 */
		case -3: internal_line = 16; break;	/* TIMER2 */
		case -4: internal_line = 7;  break;	/* SW0    */
		case -5: internal_line = 29; break;	/* SW1    */
		case -6: internal_line = 11; break;	/* PROFILING */
		default: internal_line = -1; break;
		}
		if (internal_line < 0) {
			syslog_1(LOG_ERROR, "bt: unknown internal intr source %d",
					 (int_t) source);
			return(-1);
		}
		cpu_line = (uint32_t) internal_line;
		slot = &bt_intr_slot[(idx < 2U) ? idx : 1U];
		if (idx < 2U) {
			esp_shim_bt_last_intr_line[idx] = cpu_line;
		}
		slot->used = 1;
		slot->source = source;
		slot->cpu_line = cpu_line;
		/*  マトリクスは触らない。CPU 側にハンドラを置いて許可するだけ。 */
		esp_shim_set_isr((int32_t) cpu_line, (void *) isr_fn, isr_arg);
		(void) ena_int((INTNO) cpu_line);
		if (ret_handle != NULL) {
			*ret_handle = (intr_handle_t) slot;
		}
		return(ESP_OK);
	}

	if (level3) {
		/*  BTコントローラが要求するESP_INTR_FLAG_LEVEL3を実際にXtensa
		 *  Level-3として配線する（BT_INTR_CPU_LINE3_A/_B定義部参照）。  */
		if (idx == 0U) {
			cpu_line = BT_INTR_CPU_LINE3_A;
			slot = &bt_intr_slot[0];
		}
		else if (idx == 1U) {
			cpu_line = BT_INTR_CPU_LINE3_B;
			slot = &bt_intr_slot[1];
		}
		else {
			/*  3個目以降（想定外呼出し）：クラッシュを避けるため既存の
			 *  線27（スロット1）へ安全側フォールバックする．  */
			cpu_line = BT_INTR_CPU_LINE3_B;
			slot = &bt_intr_slot[1];
		}
	}
	else {
		/*  flagsがLEVEL3を要求しない呼出し（現状は想定していないが、
		 *  安全側に旧来のLevel-1固定配線を残す）．  */
		if (idx == 0U) {
			cpu_line = BT_INTR_CPU_LINE;
			slot = &bt_intr_slot[0];
		}
		else if (idx == 1U) {
			cpu_line = BT_INTR_CPU_LINE2;
			slot = &bt_intr_slot[1];
		}
		else {
			cpu_line = BT_INTR_CPU_LINE2;
			slot = &bt_intr_slot[1];
		}
	}
	if (idx < 2U) {
		esp_shim_bt_last_intr_line[idx] = cpu_line;
	}

#ifdef TOPPERS_S3_BT_INTR_DIAG
	if (idx < 2U) {
		esp_shim_bt_intr_diag_sticky[idx] = 0U;
		bt_intr_diag_real_fn[idx] = handler;
		bt_intr_diag_real_arg[idx] = arg;
		isr_fn = bt_intr_diag_tramp[idx];
		isr_arg = NULL;
	}
#endif

	slot->used = 1;
	slot->source = source;
	slot->cpu_line = cpu_line;

#if defined(TOPPERS_ESP32_LX6)
	/*  無印ESP32：ソース→CPU線ルーティング（DPORT MAPレジスタ）のみ．
	 *  優先度は線23/27のハードウェア固定レベル3で決まるため，別レジスタ
	 *  書込みは不要（上のBT_INTMTX_MAP_REGコメント参照）。 */
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_MAP_REG(source), cpu_line);
#else
	/*  S3：MAP(source)=cpu_line のこの1行がルーティングの本体。
	 *  旧実装はこの後に PRI_REG(cpu_line)=2 を書いていたが，S3 に線別優先度
	 *  レジスタは無く，その番地は source 92/96 の MAP だったため線2へ奪っていた
	 *  （2026-07-25 削除。上のマクロコメント参照）。 */
	sil_wrw_mem((void *)(uintptr_t)(BT_INTMTX_BASE_ADDR + (uint32_t) source * 4U),
				cpu_line);
#if defined(TOPPERS_BT_AC_REINSERT_BUG)
	/*  AC-B5 negative control 専用（既定 OFF／golden には入らない）。
	 *  2026-07-25 に削除した「誤った PRI_REG 書込み」を**実機で再現**し、
	 *  0x600C2170/0x600C2180 が線2へ奪われる（修正前の姿）ことを実測で示す。
	 *  これが無いと「修正後に値が正しい」ことが always-pass かどうか判別できない。 */
	sil_wrw_mem((void *)(uintptr_t)(0x600C2000U + 0x114U + cpu_line * 4U), 2U);
#endif
#endif
	esp_shim_set_isr((int32_t) cpu_line, (void *) isr_fn, isr_arg);
	/*  ESP32-S3(Xtensa)固有：esp_shim_set_isrはINTMTX側のみ設定しXtensaの
	 *  INTENABLEビットを立てないため、CPU割込み線を明示的に許可する（未許可だと
	 *  BTコントローラのISRが発火しない。esp_wifi_adapter.cのset_isr_wrapperと同根）。
	 *  S3/LX6とも enable はこの ena_int が唯一の実体（旧 S3 の ENABLE_REG RMW は
	 *  APB_ADC の MAP を叩く no-op だったので削除した。2026-07-25）。 */
	ena_int((INTNO) cpu_line);

	*ret_handle = slot;
	return(ESP_OK);
}

/*
 *  2026-07-25 実体化（AC-btshim2.md）: S3/LX6 とも Xtensa INTENABLE
 *  （ena_int/dis_int）で per-handle に enable/disable する。以前の S3 分岐は
 *  ENABLE_REG（実体は APB_ADC の MAP）を叩く no-op で、free/disable が実際には
 *  線を落とさなかった。IDF 意味論（esp_intr_disable は本当に無効化する）へ揃えた。
 *  ⇒ 両分岐が同一になったので #if を畳んだ。
 *  alloc は line 857 の ena_int が唯一の enable 実体（S3/LX6 共通）。
 */
esp_err_t
esp_intr_free(intr_handle_t handle)
{
	uint32_t	cpu_line = (handle != NULL) ? handle->cpu_line : BT_INTR_CPU_LINE;

#if defined(TOPPERS_BT_AC_INTMTX_DUMP)
	esp_shim_bt_free_calls++;
#endif
	(void) dis_int((INTNO) cpu_line);
	if (handle != NULL) {
		handle->used = 0;
	}
	return(ESP_OK);
}

esp_err_t
esp_intr_enable(intr_handle_t handle)
{
	uint32_t	cpu_line = (handle != NULL) ? handle->cpu_line : BT_INTR_CPU_LINE;

#if defined(TOPPERS_BT_AC_INTMTX_DUMP)
	esp_shim_bt_ena_calls++;
#endif
	(void) ena_int((INTNO) cpu_line);
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(intr_handle_t handle)
{
	uint32_t	cpu_line = (handle != NULL) ? handle->cpu_line : BT_INTR_CPU_LINE;

#if defined(TOPPERS_BT_AC_INTMTX_DUMP)
	esp_shim_bt_dis_calls++;
#endif
	(void) dis_int((INTNO) cpu_line);
	return(ESP_OK);
}

/*
 *  coex_pti_v2はlibbtbb.a（bt_bb_v2.o）に実体があるため自前実装は
 *  不要（当初ROM関数だと誤認していた．libbtbbをリンクすれば解決）．
 */
