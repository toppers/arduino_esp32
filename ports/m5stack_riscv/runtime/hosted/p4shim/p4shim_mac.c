/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: MAC アドレス（`esp_read_mac`）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜ `mac_addr.c` を引かないのか
 *  ============================================================================
 *  `esp_hw_support/mac_addr.c` は `esp_efuse_read_field_blob()` 経由で読むため、
 *  `efuse` コンポーネント一式（`esp_efuse_table.c` の全フィールド表・
 *  `esp_efuse_utility.c`・`esp_efuse_api.c`）を引き込む。読みたいのは
 *  **eFuse の 48 ビット 1 個**である。⇒ レジスタを直読みする。
 *  同型の実装が本 repo の S3 側にもある（`esp/shim/esp_shim_blobglue.c`）。
 *
 *  ============================================================================
 *  P4 と S3 の違い（一次資料で確かめた。**推測ではない**）
 *  ============================================================================
 *  (1) eFuse のベース番地が違う
 *        S3 : `DR_REG_EFUSE_BASE = 0x60007000`（`EFUSE_RD_MAC_SPI_SYS_0/1`）
 *        P4 : `DR_REG_EFUSE_BASE = 0x5012D000`（`EFUSE_RD_MAC_SYS_0/1`）
 *      （`soc/esp32p4/register/hw_ver3/soc/efuse_reg.h`。レジスタ名から
 *        "SPI" が落ちているが、**+0x44 / +0x48 という配置と
 *        「下位 32 ビット / 上位 16 ビット」という分け方は同じ**）
 *  (2) `ESP_MAC_ETH` の導出が違う
 *        `mac_addr.c:401` の `mac[5] += 3` は
 *        `#if SOC_WIFI_SUPPORTED || CONFIG_ESP_MAC_ADDR_UNIVERSE_BT` の下にある。
 *        **P4 はどちらも成立しない**（Wi-Fi 無し・BT 無し。P4 の `Kconfig.mac` は
 *        `ESP32P4_UNIVERSAL_MAC_ADDRESSES_ONE` だけを持ち、これは
 *        `ESP_MAC_ADDR_UNIVERSE_ETH` を select するが `..._BT` は select しない）。
 *        ⇒ **P4 では `ESP_MAC_ETH` ＝ eFuse の素の MAC（オフセット 0）**である。
 *        S3 では `base + 3` になる。**ここを取り違えると、隣の板と MAC が
 *        衝突する形で（同一 LAN 上でだけ）壊れる。**
 *
 *  実機で確かめられる: この板の MAC は `30:ed:a0:ea:b6:3e`（esptool の読み値。
 *  `.steering/20260815-poep4-stageE/README.md` §2）。本実装が返す値が
 *  それと一致することを起動ログに出す（`fmp_eth_sta` は netif に載せた MAC を
 *  DHCP のやり取りで使うので、DHCP が通る＝MAC が妥当、という間接証拠も残る）。
 */

#include <kernel.h>
#include <sil.h>
#include <stdint.h>

#include "p4shim.h"

/*
 *  **IDF の公開ヘッダを include してプロトタイプの照合をコンパイラにさせる。**
 *  怠ると「リンクは通り、ビルドも通り、実機でだけ壊れる」型の失敗になる
 *  ——run1 で実際に踏んだ（`gpio_func_sel` を void で書き、呼び手が
 *  戻り値を見ていたため a0 のゴミが非 0 と読まれ、SMI 初期化が失敗した）。
 */
#include "esp_mac.h"

/*
 *  ESP32-P4 の eFuse MAC レジスタ。
 *  IDF ヘッダ（`soc/efuse_reg.h`）を include してもよいが、この 2 本のためだけに
 *  チップリビジョン依存の `register/hw_ver{1,3}/` の切り替えを持ち込みたくない
 *  ——**両リビジョンで同じ番地**であることを確認したうえで直値にする。
 */
#define P4SHIM_EFUSE_RD_MAC_SYS_0	((uint32_t *) 0x5012D044UL)
#define P4SHIM_EFUSE_RD_MAC_SYS_1	((uint32_t *) 0x5012D048UL)

/*  `esp_mac.h`: ESP_MAC_WIFI_STA=0 … ESP_MAC_ETH=3 …  */
#define P4SHIM_ESP_MAC_ETH			3
#define P4SHIM_ESP_MAC_BASE			4

esp_err_t
esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
	uint32_t	w0;
	uint32_t	w1;

	if (mac == NULL) {
		p4shim_n_mac_fail++;
		return ESP_ERR_INVALID_ARG;
	}
	/*
	 *  P4 で意味を持つのは ETH と BASE だけ（Wi-Fi も BT も無い）。
	 *  それ以外を要求されたら**黙って何かを返さない**。
	 */
	if ((int) type != P4SHIM_ESP_MAC_ETH && (int) type != P4SHIM_ESP_MAC_BASE) {
		p4shim_n_mac_fail++;
		return ESP_ERR_NOT_SUPPORTED;
	}

	w0 = sil_rew_mem(P4SHIM_EFUSE_RD_MAC_SYS_0);
	w1 = sil_rew_mem(P4SHIM_EFUSE_RD_MAC_SYS_1);

	/*
	 *  eFuse の MAC[] フィールド記述（BLK1・ビット 40/32/24/16/8/0 の降順）は
	 *  P4 と S3 でバイト同一。⇒ 並べ替えも同じ。
	 */
	mac[0] = (uint8_t) (w1 >> 8);
	mac[1] = (uint8_t) (w1);
	mac[2] = (uint8_t) (w0 >> 24);
	mac[3] = (uint8_t) (w0 >> 16);
	mac[4] = (uint8_t) (w0 >> 8);
	mac[5] = (uint8_t) (w0);

	/*
	 *  全ゼロは「eFuse が読めていない」ことの分かりやすい徴候である
	 *  （書かれていない板は存在しない）。**黙って 00:00:00:00:00:00 を
	 *  返さない**——それを netif に載せると、リンクは上がるのに DHCP だけが
	 *  通らないという、最も切り分けにくい形で壊れる。
	 */
	if ((w0 | w1) == 0U) {
		p4shim_n_mac_fail++;
		return ESP_FAIL;
	}

	/*
	 *  **P4 では ETH のオフセットは 0**（本ファイル冒頭 (2)）。
	 *  S3 の `+3` をここに書かないこと。
	 */
	return ESP_OK;
}
