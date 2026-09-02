/*
 *  bt_hci_wrap_diag — コントローラへ実際に渡る HCI コマンドを観測する
 *
 *  なぜ要るか（2026-08-07・LM-D-3）:
 *    iOS の暗号化再接続が `reason=0x3D`（MIC Failure）で 100% 失敗する。
 *    本日の実測で次がすべて**白**になった:
 *      割込み／OS レイテンシ／LTK の保存・検索・返却／HCI 送信のタイムアウト／
 *      HCI フロー制御（take・give とも失敗 0）
 *    ⇒ **ホストは正しい鍵を落とさず遅れず渡している。それでも MIC が合わない。**
 *    残るのは「**渡っているバイト列そのもの**」である。
 *
 *  何を見るか:
 *    `LE Long Term Key Request Reply`（opcode 0x201A）の LTK 16 バイト。
 *    H4 コマンドパケットの並びは
 *      [0]=0x01(CMD) [1]=opcode.lo [2]=opcode.hi [3]=param_len
 *      [4..5]=conn_handle [6..21]=LTK
 *
 *  なぜ --wrap か:
 *    `esp_nimble_hci.c` は vendored であり**改変しない方針**である
 *    （ble_hs_smoke.c:157- の store ラッパも同じ理由でアプリ側に置かれている）。
 *    リンク時 `--wrap` なら **vendored を 1 行も触らずに**観測できる。
 *    本リポジトリは M5 のトレースで既に --wrap を使っており前例がある。
 *
 *  鍵そのものは出さない。**指紋（FNV-1a 32bit）だけ**を出す。
 *    store 側と同じ関数を使うので、**同じ鍵なら同じ値**になる。
 *  本ファイルは診断ビルド（TOPPERS_S3_BT_VHCI_DIAG）でしかリンクしない。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <stdint.h>

extern void		__real_esp_vhci_host_send_packet(uint8_t *data, uint16_t len);
extern uint32_t	bt5diag_ltk_fp(const uint8_t *ltk16);

volatile uint32_t	bt_hci_ltkreply_n;		/* LTK Reply を送った回数 */
volatile uint32_t	bt_hci_ltkreply_fp;		/* 最後に送った LTK の指紋 */
volatile uint32_t	bt_hci_cmd_n;			/* コマンド総数（生存確認用） */

void
__wrap_esp_vhci_host_send_packet(uint8_t *data, uint16_t len)
{
	if ((data != NULL) && (len >= 4U) && (data[0] == 0x01U)) {
		bt_hci_cmd_n++;
		/*  opcode 0x201A = LE Long Term Key Request Reply  */
		if ((data[1] == 0x1AU) && (data[2] == 0x20U) && (len >= 22U)) {
			bt_hci_ltkreply_n++;
			bt_hci_ltkreply_fp = bt5diag_ltk_fp(&data[6]);
			syslog(LOG_NOTICE,
				   "ble_hs_smoke: HCIWRAP LTK_REPLY n=%u len=%u ltkfp=%08x",
				   (uint_t) bt_hci_ltkreply_n, (uint_t) len,
				   (uint_t) bt_hci_ltkreply_fp);
		}
	}
	__real_esp_vhci_host_send_packet(data, len);
}
