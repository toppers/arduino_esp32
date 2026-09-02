/*
 *  W3(BT Classic/SPP) BlueDroidホスト静的統合：BLE専用として本ポートの
 *  CLASSIC_BT単独運用スコープ（レビュー方針、review-w3-bt-classic-approach.md
 *  「MVPは ESP_BT_MODE_CLASSIC_BT 単独」）では未到達となる残置参照の
 *  最小スタブ集。
 *
 *  背景：本ポートのsdkconfig_override（esp/bt/classic/sdkconfig_override/
 *  sdkconfig.h）はCONFIG_BT_SMP_ENABLE=1を立てている。これはBLEの
 *  Security Manager Protocol用ではなく、BlueDroidの実装がBLE SMと
 *  Classic SSPのペアリング状態管理（tBTM_SEC_CB内の
 *  pairing_state/pairing_bda等）を同じSMP_INCLUDEDゲート配下で共有して
 *  いるためで、btm_sec.c（Classic側）のコンパイルに必須（詳細は
 *  sdkconfig_override/sdkconfig.hのコメント参照）。この副作用として、
 *  SMP_INCLUDED==TRUEでのみ有効になるが本来BLE専用の呼び出し
 *  （BLE_INCLUDED==FALSEのため定義側が存在しない）が3件発生する：
 *    - btc_main.c: btc_storage_get_num_ble_bond_devices()（BLEボンド
 *      デバイス数取得、BLE_PRIVACY_SPT関連の初期化ログでのみ使用）
 *    - btc_main.c: smp_get_state()（BLE SMPステートマシンの現在状態、
 *      ログ出力のみで使用）
 *    - btm_sec.c: SMP_PairCancel()（BLEペアリングキャンセル、Classic側の
 *      BTM_SecBondCancel経路でBLEデバイスに対してのみ呼ばれる分岐、
 *      本ポートはBLEを有効化しないため到達しない）
 *
 *  W3②の完了条件（esp_bluedroid_init/enable完走）はSPPプロファイル・
 *  ペアリング手続き自体を必要としないため、これらは「リンクを通すための
 *  安全なスタブ」で足りる。実際にBLEペアリング機能が必要になった場合は
 *  BLE_INCLUDED経路の本格対応（W2 BLE資産との統合）が別途必要（本スタブは
 *  その代替ではない）。
 */
#include <t_syslog.h>
#include "common/bt_target.h"
#include "stack/bt_types.h"
#include "btc/btc_task.h"
#include "esp_vfs.h"

/*  btc_ble_storage.c（BLEボンドデバイス永続化）は本ポートのSRCLISTから
 *  除外済み（BLE専用APIのみを提供しており、SPP Classicのどの経路からも
 *  呼ばれないため）。そのため実体無しでボンド数0を返す。 */
int
btc_storage_get_num_ble_bond_devices(void)
{
	return(0);
}

/*  smp_main.c/smp_act.c（BLE SMPステートマシン本体）も同様にSRCLISTに
 *  含めていない（BLE_INCLUDED==FALSEで事実上デッドコードのため）。
 *  ログ用途のみのためアイドル相当（0）を返す。 */
uint8_t
smp_get_state(void)
{
	return(0);
}

/*  SMP_PairCancel本来の実装（smp_api.c）はBLE SMPステートマシンに
 *  依存するため未統合。本ポートはBLEデバイスとのペアリングを開始しない
 *  （CLASSIC_BT単独）ため、キャンセル要求自体が来ない想定。万一到達した
 *  場合も「何もキャンセルできなかった」を意味するFALSEを返せば
 *  呼び出し側（btm_sec.c）の異常系処理と整合する。 */
BOOLEAN
SMP_PairCancel(BD_ADDR bd_addr)
{
	(void) bd_addr;
	return(false);
}

/*
 *  [2026-07-15 W3④セッション] SPP本体（btc/profile/std/spp/btc_spp.c）を
 *  SRCLISTへ追加したため、以前ここにあった btc_spp_call_handler/
 *  btc_spp_cb_handler のリンク時プレースホルダは削除した（実体は
 *  btc_spp.cが提供する）。*/

/*
 *  [2026-07-15 W3④] esp_vfs.h（esp/bt/stub/include/、コンパイル専用
 *  スタブ）に対応するVFS登録/解除APIの最小実装。本ポートのSPP MVPは
 *  ESP_SPP_MODE_CB（esp_spp_write/受信コールバック）単独スコープで
 *  ESP_SPP_MODE_VFSは使わない方針のため、btc_spp.cのVFS経路
 *  （btc_spp_vfs_register/unregister、esp_vfs_register_fd等）が実際に
 *  呼ばれることは無い想定。ただしbtc_spp.cはspp_modeの実行時分岐で
 *  これらを呼ぶコードを含むため、リンクには実体が必要。安全に失敗を
 *  返すだけの最小実装とする（本ポートでVFS/POSIXファイルディスクリプタ
 *  経由のSPPを使う場合は、実ESP-IDFのcomponents/vfs/esp_vfs.cの本格
 *  移植が別途必要）。
 */
esp_err_t
esp_vfs_register_fs_with_id(const esp_vfs_fs_ops_t *vfs, int flags, void *ctx,
							 esp_vfs_id_t *vfs_id)
{
	(void) vfs;
	(void) flags;
	(void) ctx;
	/*  common/bt_trace.h（common/bt_target.h経由でinclude済み）が
	 *  LOG_ERROR(format,...)をBT_LOG形式の関数マクロとして再定義するため，
	 *  t_syslog.hのLOG_ERROR（整数値）をsyslog()の第1引数として使うと
	 *  マクロ名衝突で未展開のまま識別子扱いされコンパイルエラーになる
	 *  （2026-07-15 W3④セッションで発見）。ここではBT_LOG形式の
	 *  LOG_ERROR("format", ...)をそのまま使う（esp_log_write()経由で
	 *  最終的にsyslogへ折り返される、W3②で追加済み）。 */
	LOG_ERROR("bluedroid_glue: esp_vfs_register_fs_with_id unimplemented "
			  "(SPP MVP is ESP_SPP_MODE_CB only, VFS mode unsupported)");
	if (vfs_id != NULL) {
		*vfs_id = -1;
	}
	return(ESP_FAIL);
}

esp_err_t
esp_vfs_unregister_with_id(esp_vfs_id_t vfs_id)
{
	(void) vfs_id;
	return(ESP_FAIL);
}

esp_err_t
esp_vfs_register_fd(esp_vfs_id_t vfs_id, int *fd)
{
	(void) vfs_id;
	if (fd != NULL) {
		*fd = -1;
	}
	return(ESP_FAIL);
}

esp_err_t
esp_vfs_unregister_fd(esp_vfs_id_t vfs_id, int fd)
{
	(void) vfs_id;
	(void) fd;
	return(ESP_FAIL);
}

#if defined(TOPPERS_ESPIDF_SUPPLY)
/*  ESP-IDF v5.5.4 btc_spp.c が使う旧VFS API のスタブ（VFSモード未実装＝CBモードのみ）。
 *  master(esp-hal-3rdparty)は新API esp_vfs_register_fs_with_id を使うため上に別途スタブあり。 */
esp_err_t
esp_vfs_register_with_id(const esp_vfs_t *vfs, void *ctx, esp_vfs_id_t *vfs_id)
{
	(void) vfs;
	(void) ctx;
	if (vfs_id != NULL) {
		*vfs_id = -1;
	}
	LOG_ERROR("bluedroid_glue: esp_vfs_register_with_id unimplemented "
			  "(SPP VFSモード非対応、CBモードのみ)");
	return(ESP_FAIL);
}
#endif /* TOPPERS_ESPIDF_SUPPLY */
