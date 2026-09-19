/*
 *  esp_wifi_sta_get_rsnxe の観測（診断専用・既定 OFF）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ESP32-S3 の STA が reason=17（IE_IN_4WAY_DIFFERS）で落ちる件（F-3）の
 *  切り分け。supplicant 側の診断ビルドで、association 時に記録される AP の
 *  RSNXE が **S3 では空・LX6 では f4 01 20** と分かった。その供給元が
 *  この関数で、実体は blob（libnet80211.a）にある。
 *
 *  supplicant からの呼び出しはアーカイブ間なので --wrap が効く。ここでは
 *    (1) どの BSSID で呼ばれたか
 *    (2) 本物が何を返したか（NULL か、IE なら長さと先頭）
 *  を出す。⇒「鍵（BSSID）が違う」のか「blob が持っていない」のかを分ける。
 *
 *  A1_WIFI_RSNXE_PROBE=ON のときだけビルドへ入る（既定 OFF・出荷物は無改変）。
 */

#include <stdint.h>
#include <stddef.h>
#include <t_syslog.h>

extern uint8_t *__real_esp_wifi_sta_get_rsnxe(uint8_t *bssid);

uint8_t *
__wrap_esp_wifi_sta_get_rsnxe(uint8_t *bssid)
{
	uint8_t	*ie = __real_esp_wifi_sta_get_rsnxe(bssid);

	/*  ★BSSID は生で出さない。知りたいのは「鍵が妥当か」であって値ではなく、
	 *  採取ログは記録に残る。ゼロでない（＝実在の BSSID で引いている）ことと、
	 *  区別用の下位 1 バイトだけにする。
	 *  ★FMP3 の syslog は可変引数 5 個までである（syslog_0..syslog_5）。
	 *  6 個渡すと最後が書式のまま出る——最初の版で実際にそうなった。 */
	if (bssid == NULL) {
		syslog(LOG_NOTICE, "[RSNXE] called with bssid=NULL");
	}
	else {
		uint_t	nonzero = 0U;
		int		i;

		for (i = 0; i < 6; i++) {
			if (bssid[i] != 0U) { nonzero = 1U; }
		}
		syslog(LOG_NOTICE, "[RSNXE] bssid nonzero=%u last=%02x",
			   nonzero, (uint_t) bssid[5]);
	}
	if (ie == NULL) {
		syslog(LOG_NOTICE, "[RSNXE] real returned NULL");
	}
	else {
		syslog(LOG_NOTICE, "[RSNXE] real returned id=%u len=%u data0=%u",
			   (uint_t) ie[0], (uint_t) ie[1], (uint_t) ie[2]);
	}
	return(ie);
}
