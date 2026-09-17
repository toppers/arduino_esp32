/*
 *  hosted Wi-Fi のアダプタ (1/3): 立ち上げ
 *
 *  SDIO の相手（Stamp AddOn の ESP32-C6）を、Arduino のどの入口から入っても
 *  同じ順序で使える状態にする。順序は開発リポジトリのプローブが実機で通した
 *  ものと同じ（rpc_probe.c の task: 電源/card_init/INIT/slave_config ->
 *  wifi_init -> set_mode(STA) -> wifi_start）。前半は p4hosted_rpc_bringup()
 *  が 1 本にまとめており、ここではその各段を p4hosted_rpc.h 越しに呼ぶだけで、
 *  手順そのものは持ち込んだコードの中にある。
 *
 *  **1 回だけ**通す: Arduino のスケッチは scanNetworks() から始めることも
 *  begin() から始めることもあるので、どちらの入口も最初にこれを通す。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>

#include "p4sdio_host.h"
#include "p4sdio_board.h"
#include "p4hosted_osi.h"
#include "p4hosted_rpc.h"
#include "netif_esp_hosted.h"
#include "toppers_wifi_hosted.h"

/*  esp_hosted の WIFI_MODE_STA。出典の generated/hosted_rpc_ids.h が持つ値と
 *  同じ 1 で、プローブも 1 を渡している。 */
#define HOSTED_WIFI_MODE_STA	1U

static bool	hosted_ready;
static bool	hosted_failed;

bool
toppers_hosted_core_ready(void)
{
	if (hosted_ready) {
		return(true);
	}
	if (hosted_failed) {
		/*  一度落ちたら黙って繰り返さない（相手が居ない板で毎回 30 秒
		 *  待つのを避ける）。リセットで再試行する。 */
		return(false);
	}

	syslog(LOG_NOTICE, "[WiFiHosted] bringing up the SDIO companion");
	if (!p4hosted_rpc_bringup()) {
		syslog(LOG_WARNING, "[WiFiHosted] companion bring-up failed");
		hosted_failed = true;
		return(false);
	}
	if (!rp_wifi_init()) {
		syslog(LOG_WARNING, "[WiFiHosted] wifi_init failed");
		hosted_failed = true;
		return(false);
	}
	if (!rp_wifi_set_mode(HOSTED_WIFI_MODE_STA)) {
		syslog(LOG_WARNING, "[WiFiHosted] set_mode(STA) failed");
		hosted_failed = true;
		return(false);
	}
	if (!rp_wifi_start()) {
		syslog(LOG_WARNING, "[WiFiHosted] wifi_start failed");
		hosted_failed = true;
		return(false);
	}
	hosted_ready = true;
	syslog(LOG_NOTICE, "[WiFiHosted] companion ready (STA)");
	return(true);
}

void
toppers_hosted_core_pump(uint32_t tries)
{
	(void) rp_pump(tries, 0U);
}
