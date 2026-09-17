/*
 *  TOPPERS/FMP3 ESP32-P4 移植 — esp-hosted 上流ヘッダが要求する最小の構成定義
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  これは**取込み物ではない**（`IMPORT_PROVENANCE.md` §2 の「新規」）
 *  ============================================================================
 *  `upstream/sdio_reg.h` は先頭で `#include "port_esp_hosted_host_config.h"` する。
 *  出典側の同名ファイルは 200 行超の構成一式（RPC/PS/netif/BT 等）だが、
 *  本段が必要とするのは **`BIT()` と「スレーブは C6 である」** の 2 つだけである。
 *
 *  出典の巨大な構成をまるごと持ってくると、
 *    - 使っていない設定値まで「この repo の決定」に見えてしまう
 *    - どれが効いていてどれが死んでいるのか分からなくなる
 *  ので、**上流ヘッダが実際に参照するものだけ**をここに置く。
 *  上流ヘッダ側は 1 バイトも変えていない（監査ゲート 3 が毎回確かめる）。
 *
 *  出典の該当箇所（読み取り専用で確認）:
 *    `wifi_p4_module/esp_hosted_core/port_fmp3/port_esp_hosted_host_config.h:62`
 *        #define H_SLAVE_TARGET_ESP32C6 1
 *  Tab5 の外付けコプロセッサは ESP32-C6（回路図 U2・段7a §1 で一次確認済み）。
 */

#ifndef P4HOSTED_PORT_ESP_HOSTED_HOST_CONFIG_H
#define P4HOSTED_PORT_ESP_HOSTED_HOST_CONFIG_H

#ifndef BIT
#define BIT(n)						(1UL << (n))
#endif

/*
 *  `sdio_reg.h:58-64` はスレーブ種別で「新規パケット割込みのビット位置」を
 *  選ぶ。C6 以外を選ぶと `#error` になる（＝**fail-closed**。黙って既定へ
 *  落ちない）ので、ここを間違えるとビルドが止まる。
 */
#define H_SLAVE_TARGET_ESP32		0
#define H_SLAVE_TARGET_ESP32C6		1
#define H_SLAVE_TARGET_ESP32C5		0
#define H_SLAVE_TARGET_ESP32C61		0

#endif /* P4HOSTED_PORT_ESP_HOSTED_HOST_CONFIG_H */
