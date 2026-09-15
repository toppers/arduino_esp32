/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ============================================================================
 *  ESP-IDF 由来の portENTER_CRITICAL(mux) 系を 2 コアで実装するための共通部品
 *  ============================================================================
 *
 *  【出自】esp/bt/bt_shim.c:203-354（2026-07 の BT-4、および 2026-08-05 段20）で
 *  実機実証済みの設計を、そのまま持ち上げたものである。設計の詳細な根拠は
 *  bt_shim.c:140-201 のコメントを参照。要点だけ再掲する:
 *
 *    (1) 割込みのネスト状態と退避 PS は「コア単位」で持つ。
 *        大域で持つと、コア0 が区間内のときコア1 の enter がネスト深さを 2 へ
 *        押し上げ、コア1 の exit が「まだ 1 だから」と PS を復元しない。
 *        コア1 は INTLEVEL=15 のまま区間を抜ける（tick も来ない）。
 *    (2) mux は無視せず、実際に再入可能スピンロックとして取得・解放する。
 *        同一コアが同一 mux を再入するのは blob 側の仕様なので
 *        「所有コア + 再入カウント」を持つ（ESP-IDF の spinlock_t と同型）。
 *    (3) CAS の実体は fmp3/arch/xtensa_gcc/common/xtensa_cas.h に 1 本化済み
 *        （フェーズ1 1-3）。その不変条件（SCOMPARE1 を守るため
 *        PS.INTLEVEL=15 か INTENABLE=0 の下でのみ呼ぶ）は、本ヘッダが
 *        esp_shim_int_disable() を先に撃つことで満たしている。
 *
 *  【free の表現が 2 種類ある（段20 の真因。片方を選ぶ実装は成立しない）】
 *  同じビルドに出自の違う mux が同時に実在する:
 *
 *    (a) owner = 0
 *        esp/bt/stub/include/freertos/FreeRTOS.h の
 *        portMUX_INITIALIZER_UNLOCKED = { 0, 0 }。
 *        例: m5/shim/m5_spi_bus_stub_esp32.c:256 の s_m5_spi_dmawa_mux。
 *
 *    (b) owner = 0xB33FFFFF
 *        ESP-IDF の SPINLOCK_INITIALIZER
 *        （esp-idf/components/esp_hw_support/include/spinlock.h:31 SPINLOCK_FREE）。
 *        例: esp/wifi/hal_src/periph_ctrl.c:24 の periph_spinlock、
 *            esp/wifi/hal_src/phy_init.c:94 の s_phy_int_mux。
 *        これらは DEFINE_CRIT_SECTION_LOCK_STATIC 経由で作られる。
 *        名前から推測せず gcc -E で実測して確かめてある:
 *        合成 2 コア構成の periph_ctrl.i で
 *          static ... esp_os_spinlock_t periph_spinlock = {.owner = 0xB33FFFFF,.count = 0};
 *          esp_shim_bt_enter_critical(&periph_spinlock);
 *        （.steering/20260806-phase3-2-btcrit-xcore/logs/PC-preproc-periph.txt）。
 *
 *  片方だけを free として受ける実装は、もう片方を永久にスピンさせる。
 *  実機で因果まで実証済み（段20: ハング中に JTAG で owner へ 0 を書いて resume
 *  すると controller_init まで一気に進んだ）。よって 2 段 CAS とする。
 *
 *  【解放側が owner=0 を書いてよい理由】ESP-IDF 本体の spinlock_acquire /
 *  spinlock_release は本ビルドに 1 件もリンクされない。これは
 *  cmake/a1_spinlock_audit.sh（フェーズ1 1-4）がリンク時に fail-closed で
 *  検査している。したがって 0xB33FFFFF を読む側が居ない。
 *  この監査が外れたら、本ヘッダの前提も同時に崩れる。
 *
 *  【本ヘッダが持たないもの】区間を抜けたあとの後処理（bt_shim.c の
 *  esp_shim_queue_flush_pending / esp_shim_ring_flush_wakes）は呼出し側の
 *  事情なので、ここには入れない。必要な TU が exit のあとで自分で呼ぶ。
 *
 *  【bt_shim.c は本ヘッダへ追随させていない（既知の重複）】
 *  bt_shim.c は seam-s3-ble にリンクされ、その構成は golden 照合が効いている。
 *  配線とクリティカルセクションを同時に動かすと切り分けができなくなるため、
 *  折り込むなら「seam-s3-ble がバイト不変」だけを主張する独立した段で行う。
 */

#ifndef TOPPERS_ESP_SHIM_XCORE_CRIT_H
#define TOPPERS_ESP_SHIM_XCORE_CRIT_H

#ifndef TNUM_PRCID
#error "esp_shim_xcore_crit.h: TNUM_PRCID が見えていない"
#endif

#if TNUM_PRCID >= 2

#ifndef TOPPERS_MACRO_ONLY

#include <xtensa_cas.h>

/*
 *  mux の実体。esp/bt/stub/include/freertos/FreeRTOS.h の portMUX_TYPE、および
 *  ESP-IDF の spinlock_t（spinlock.h）と**同一レイアウト**であること。
 *  どちらも { uint32_t owner; uint32_t count; } である（2026-08-06 実測）。
 *  ヘッダを共有せず独立定義とするのは bt_shim.c と同じ方針（FreeRTOS.h 側は
 *  kernel.h を引き込まない）。
 */
struct esp_shim_xcore_mux {
	volatile uint32_t	owner;
	volatile uint32_t	count;
};

/*  ESP-IDF が「未取得」を表す値（spinlock.h:31 SPINLOCK_FREE と同値）。
 *  同ヘッダは FreeRTOS 前提の宣言を引き込むため literal を持つ。
 *  変更時は spinlock.h 側と両方を追随させること。 */
#define ESP_SHIM_XCORE_MUX_FREE_IDF	0xB33FFFFFU

#define ESP_SHIM_XCORE_MAX_CORES	2U

/*
 *  コア単位のネスト状態。TU ごとに 1 実体を静的に持たせる。
 *  static ではなく「呼出し側が実体を持って渡す」形にしたのは、ヘッダに
 *  static を置くと TU ごとに別実体になり、同じ排他を共有しているつもりの
 *  2 つの TU がネストカウンタを分け合ってしまうため（esp_shim_m5_ext.c の
 *  コメントが同じ罠を記録している）。
 */
struct esp_shim_xcore_crit_state {
	volatile uint32_t	nest[ESP_SHIM_XCORE_MAX_CORES];
	volatile uint32_t	saved[ESP_SHIM_XCORE_MAX_CORES];
	/*  不整合な exit を数える（下の exit のコメント参照）。区間内で syslog や
	 *  assert を撃つと復旧不能になり得るので、記録に留めてテスト側で検査する
	 *  （m5/compat/esp_shim_m5_ext.c の viol カウンタと同じ作法）。
	 *  カウンタを state に入れたのは、ヘッダに大域変数を置くと複数 TU から
	 *  include されたとき多重定義になるため。 */
	volatile uint32_t	viol_exit_owner;
};

#define ESP_SHIM_XCORE_CRIT_STATE_INIT	{ { 0U, 0U }, { 0U, 0U }, 0U }

/*
 *  自コアの 0 起点 index。fmp3/arch/xtensa_gcc/esp32s3/chip_kernel_impl.h の
 *  get_my_prcidx() と同一ロジック（PRID の bit13）。カーネル内部型に依存する
 *  同ヘッダをシム層から include できないため複製する（bt_shim.c と同じ事情）。
 *  レジスタ読み出しのみで待たないので、INTLEVEL=15 の下でも使える。
 */
Inline uint_t
esp_shim_xcore_prcidx(void)
{
	uint32_t	id;

	Asm("rsr.prid %0 \n\t"
	    "extui %0, %0, 13, 1" : "=a"(id));
	return (uint_t) id;
}

/*
 *  クリティカルセクションへ入る。
 *
 *  mux が NULL でなければ、実際にスピンロックを取得する（他コアが保持中は
 *  取得できるまで回る）。同一コアの再入は count を増やすだけで待たない。
 */
Inline void
esp_shim_xcore_crit_enter(struct esp_shim_xcore_crit_state *st, void *mux_raw)
{
	struct esp_shim_xcore_mux	*mux = (struct esp_shim_xcore_mux *) mux_raw;
	uint32_t	state;
	uint_t		core;

	/*  常に自コアの割込みを禁止する。rsil はコアローカルなので他コアには
	 *  触れない。xtensa_cas.h の不変条件（SCOMPARE1 保護）もこれで満たす。 */
	state = esp_shim_int_disable();

	core = esp_shim_xcore_prcidx();
	if (core >= ESP_SHIM_XCORE_MAX_CORES) {
		core = 0U;	/* 安全側フォールバック（構成上到達しない想定） */
	}
	if (st->nest[core] == 0U) {
		st->saved[core] = state;	/*  最外だけ退避 */
	}
	st->nest[core]++;

	if (mux != NULL) {
		uint32_t	me = core + 1U;	/* 0 = 未取得と区別するため +1 */

		if (mux->owner == me) {
			mux->count++;		/*  同一コアの再入。待たない */
		}
		else {
			/*  free の表現が 2 種類ある（本ヘッダ冒頭）。両方を受ける。 */
			/*
			 *  【取得側に memw を置かない理由】（2026-08-06・Codex
			 *  レビュー指摘5 を実測で検証した結論。再指摘を防ぐため記す）
			 *  同じリポジトリの `chip_sil.h` の `TOPPERS_sil_acq_spn()` は
			 *  CAS 成功後に `memw` を置いており、一見すると本実装だけが
			 *  弱く見える。しかし**本実装が満たすべき契約は SIL
			 *  スピンロックではなく、置き換え対象である ESP-IDF の
			 *  `spinlock_acquire()`** である。実測（esp-idf v5.5.4）:
			 *    spinlock.h:123 → esp_cpu_compare_and_set()
			 *      → xt_utils.h の xt_utils_compare_and_set()
			 *    ＝ `WSR SCOMPARE1` + `S32C1I` のみ。**memw は無く、
			 *      `"memory"` clobber すら無い。**
			 *  ⇒ 本実装（`xtensa_core_cas` は `"memory"` clobber を持つ）は
			 *    **ベンダ実装と同等以上**である。bt_shim.c の段20 実機実証
			 *    済み設計もここに memw を置いていない。
			 *  「他実装と違う＝バグ」と決めつけないこと（同型の罠が
			 *    `~/agents_playbook/codex-review.md` §6(c) に記録されている）。
			 */
			for (;;) {
				if (xtensa_core_cas(&mux->owner, 0U, me)) {
					break;
				}
				if (xtensa_core_cas(&mux->owner,
									ESP_SHIM_XCORE_MUX_FREE_IDF, me)) {
					break;
				}
				/*  他コアが保持中。取得できるまでスピンする。 */
			}
			mux->count = 1U;
		}
	}
}

/*
 *  クリティカルセクションから出る。
 *
 *  最外の exit でのみ PS を復元する。ネストが残っていれば INTLEVEL=15 のまま
 *  戻る（外側の区間がまだ続いているので、これが正しい）。
 *
 *  【所有者検査を入れてある（bt_shim.c との意図的な差）】
 *  bt_shim.c:309-317 は `mux->count > 0` だけを見て減算・解放する。1 コアでも
 *  BT の per-core mux でも実害は出ていなかったが、本ヘッダが扱う mux には
 *  periph_ctrl.c / phy_init.c のように**両コアが本当に取り合うもの**が含まれる。
 *  そこで所有者を確かめずに解放すると、不整合な exit（enter していないのに
 *  exit する、別の mux に対して exit する）が**他コアが保持中のロックを
 *  解いてしまう**。
 *
 *  ESP-IDF 本体の spinlock_release（spinlock.h:190）も
 *  `assert(core_owner_id == lock->owner)` で同じ契約を敷いている
 *  （"This is a lock that we didn't acquire, or the lock is corrupt"）。
 *  ⇒ 検査を入れる方が上流の意味論に沿う。組込みの実行時に assert で止めるのは
 *  避け、数えるだけにする。
 *
 *  【ネストの復元は mux と独立に行う】所有者が違って mux を触らなかった場合でも、
 *  自コアのネスト復元は行う。ネスト状態はコア単位で正しく積んであるため。
 */
Inline void
esp_shim_xcore_crit_exit(struct esp_shim_xcore_crit_state *st, void *mux_raw)
{
	struct esp_shim_xcore_mux	*mux = (struct esp_shim_xcore_mux *) mux_raw;
	uint_t		core = esp_shim_xcore_prcidx();

	if (core >= ESP_SHIM_XCORE_MAX_CORES) {
		core = 0U;
	}

	if (mux != NULL) {
		uint32_t	me = (uint32_t) core + 1U;

		if ((mux->owner == me) && (mux->count > 0U)) {
			mux->count--;
			if (mux->count == 0U) {
				Asm("memw" ::: "memory");
				mux->owner = 0U;
			}
		}
		else {
			/*  自コアが持っていない mux への exit。触らずに数える。 */
			st->viol_exit_owner++;
		}
	}

	if (st->nest[core] > 0U) {
		st->nest[core]--;
		if (st->nest[core] == 0U) {
			esp_shim_int_restore(st->saved[core]);
		}
	}
}

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TNUM_PRCID >= 2 */

#endif /* TOPPERS_ESP_SHIM_XCORE_CRIT_H */
