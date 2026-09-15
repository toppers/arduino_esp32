/*
 *  esp-idf submodule(v5.5.4)供給の Wi-Fi ビルド用 critical section スタブ。
 *
 *  $ESPIDF の esp_wifi_private.h が freertos/FreeRTOS.h を include するため、
 *  Wi-Fi ビルドでも repo の freertos shim(esp/bt/stub/include)を -I に足す。
 *  その shim は portENTER_CRITICAL(mux) を esp_shim_bt_enter_critical(mux) に
 *  マップするが、その実体(BT専用mux実装)は bt_shim.c 側で Wi-Fi ビルドには
 *  リンクされない。periph_ctrl.c / phy_init.c 等が portENTER_CRITICAL を使う
 *  ため、Wi-Fi ビルド専用の最小実装をここで供給する。
 *
 *  Wi-Fi は PRC_NUM=1(単一コア)構成なので、割込み禁止＋ネストカウンタで
 *  排他が成立する(BT のような per-core mux は不要)。TOPPERS_ESPIDF_SUPPLY 時
 *  のみ有効(baseline/BTビルドとは二重定義しない)。
 */
#if defined(TOPPERS_ESPIDF_SUPPLY)
#include <stdint.h>
#include "esp_shim.h"

/*
 *  フェーズ1 1-1（2026-08-06）: fail-closed 検査。
 *
 *  本ファイルの排他は「単一コアなので割込み禁止＋ネストカウンタで足りる」
 *  という前提に立っており（冒頭コメント）、`mux` を無視し
 *  `wc_saved_state`/`wc_nest` を**コア別でない大域**に持つ（C-2）。
 *  esp_shim.c の `SHIM_LOCK`（C-1）と違い、本ファイルには 2 コアで安全な
 *  実装の選択肢自体が無い。⇒ `TNUM_PRCID >= 2` では常に fail-closed にする
 *  （中身の実装を変えるのは 1-1 の射程外）。
 *  合成構成（`A1_M5_WIFI`）で `-DA1_SMP_EXPERIMENT=ON` とすると、本ファイルは
 *  `seam_wifi_objs` へ入り `TNUM_PRCID=2` が見える一方、上記の前提が破れたまま
 *  黙ってリンクが通っていた（C-1 と同じ配線）。
 *
 *  フェーズ3 段3-2（2026-08-06）: 上の「2 コアで安全な実装の選択肢が無い」は
 *  **もう正しくない**。esp/bt/bt_shim.c が per-core ネスト＋再入可能スピン
 *  ロック＋2 段 CAS という実機実証済みの設計を持っており、それを共有ヘッダ
 *  esp/shim/esp_shim_xcore_crit.h へ持ち上げた。本ファイルはそれを使う。
 *  よって `TNUM_PRCID >= 2` の無条件 `#error` は**外した**。
 *
 *  ただし `TNUM_PRCID` そのものが見えていない場合の検査は残す——見えなければ
 *  下の `#if TNUM_PRCID >= 2` が黙って偽になり、2 コアで単一コア実装が
 *  選ばれるという、まさに 1-1 が塞いだ状態へ戻るため。
 */
#ifndef TNUM_PRCID
#error "esp_shim_bt_crit_wifi.c: TNUM_PRCID が見えていない（単一コア前提を検査できない）"
#endif

/*
 *  段3-4h（2026-08-06）: negative control 専用・既定 OFF の opt-in。
 *  段3-2b が実機で報告した「相互排除が効いている」（saw_occupied=0）は、
 *  「プローブがそもそも壊れた実装を 1 として検出できるか」を一度も
 *  確かめていない未反証の主張だった（`verification-that-actually-verifies.md`
 *  「常に PASS する検証は検証ではない」）。`-DA1_XCP_NC_UNSAFE_CRIT=ON` を
 *  渡すと、2 コア構成でも下の「単一コア専用（mux を無視する）」実装を
 *  選ばせ、`m5_xcore_crit_probe.c` が `saw_occupied=1` を報告できることを
 *  実機で確かめる。golden には一切影響しない（既定 OFF）。
 *  詳細 `.steering/20260806-phase3-4h-old-impl-nc/`。
 */
#if (TNUM_PRCID >= 2) && !defined(A1_XCP_NC_UNSAFE_CRIT)

/*
 *  2 コア構成。設計と根拠はすべて共有ヘッダ側のコメントに置いた。
 *
 *  本 TU が扱う mux の出自は 2 種類ある（どちらもこのビルドに実在する）:
 *    owner=0          … esp/bt/stub/include/freertos/FreeRTOS.h の
 *                       portMUX_INITIALIZER_UNLOCKED
 *    owner=0xB33FFFFF … DEFINE_CRIT_SECTION_LOCK_STATIC で作られるもの
 *                       （periph_ctrl.c の periph_spinlock、phy_init.c の
 *                        s_phy_int_mux）。gcc -E で実測確認済み。
 *  共有ヘッダの 2 段 CAS が両方を free として受ける。
 */
#include "esp_shim_xcore_crit.h"

static struct esp_shim_xcore_crit_state	wc_state =
										ESP_SHIM_XCORE_CRIT_STATE_INIT;

void
esp_shim_bt_enter_critical(void *mux)
{
	esp_shim_xcore_crit_enter(&wc_state, mux);
}

void
esp_shim_bt_exit_critical(void *mux)
{
	esp_shim_xcore_crit_exit(&wc_state, mux);
}

/*
 *  段3-2b: `wc_state.viol_exit_owner`（esp_shim_xcore_crit.h 参照。所有者を
 *  持たないコアからの exit を数える）を計装から読むためのアクセサ。
 *  `wc_state` は本 TU に閉じた static なので直接は見えない。
 */
uint32_t
esp_shim_bt_crit_viol_exit_owner(void)
{
	return (uint32_t) wc_state.viol_exit_owner;
}

#else /* (TNUM_PRCID >= 2) && !defined(A1_XCP_NC_UNSAFE_CRIT) */

/*
 *  単一コア構成が真の到達経路（`TNUM_PRCID<2`）のときは、2026-08-06 段3-2
 *  より前と**1 文字も変えていない**（golden 10 構成はすべて 1 コアなので、
 *  ここが動くと golden が動く）。`A1_XCP_NC_UNSAFE_CRIT`（既定 OFF）を
 *  渡すと、`TNUM_PRCID>=2` でも**このコードが選ばれる**——mux を無視する
 *  ため 2 コア構成では相互排除が成立しない。段3-4h の negative control が
 *  意図的に使う経路であり、golden には影響しない（既定 OFF のため）。
 */
static uint32_t	wc_saved_state;
static int32_t	wc_nest;

void
esp_shim_bt_enter_critical(void *mux)
{
	uint32_t	state = esp_shim_int_disable();

	(void) mux;	/* 単一コアのため mux は使わない */
	if (wc_nest++ == 0) {
		wc_saved_state = state;	/* 最外だけ退避 */
	}
}

void
esp_shim_bt_exit_critical(void *mux)
{
	(void) mux;
	if (--wc_nest == 0) {
		esp_shim_int_restore(wc_saved_state);
	}
}

#if (TNUM_PRCID >= 2) && defined(A1_XCP_NC_UNSAFE_CRIT)
/*
 *  段3-4h の negative control 専用。`m5_xcore_crit_probe.c` が
 *  `TNUM_PRCID>=2` ガードの下で無条件に呼ぶため、NC 経路でもリンクを
 *  通す必要がある（意味のある値を持たない——上の実装は mux を見ない
 *  ため所有者違反という概念自体が無い）。真の 1 コア（golden）では
 *  この関数自体が存在しない——バイトを 1 つも増やさない。
 */
uint32_t
esp_shim_bt_crit_viol_exit_owner(void)
{
	return(0U);
}
#endif /* (TNUM_PRCID >= 2) && defined(A1_XCP_NC_UNSAFE_CRIT) */

#endif /* (TNUM_PRCID >= 2) && !defined(A1_XCP_NC_UNSAFE_CRIT) */
#endif /* TOPPERS_ESPIDF_SUPPLY */
