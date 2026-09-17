/*
 *  hosted Wi-Fi のアダプタ (2/3): スキャン
 *
 *  `WiFi.scanNetworks()` から `toppers_fmp3_wifi_scan_networks()` が呼ばれ、
 *  続けて件数ぶん ssid / rssi / channel / auth_mode が引かれる（Arduino の
 *  WiFiScan 例題の形）。相手チップは 1 件ずつ protobuf で返してくるので、
 *  p4hosted_rpc のフックで受けてここに溜める。
 *
 *  記録上限は 20 件（native 側の toppers_wifi_scan.c と同じ数。それを超える
 *  ぶんは捨て、件数は「記録した数」を返す——C6 / C5 で「20 件」が上限への
 *  飽和であることを見落とした前例があるので、溢れたら 1 行残す）。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>

#include "p4hosted_rpc.h"
#include "toppers_wifi_hosted.h"

static struct toppers_hosted_ap	hosted_aps[TOPPERS_HOSTED_MAX_RECORDS];
static uint8_t					hosted_ap_count;

static void
hosted_ap_record(uint32_t idx, const uint8_t *ssid, uint32_t slen,
				 int32_t rssi, uint32_t chan, uint32_t authmode)
{
	struct toppers_hosted_ap	*ap;

	(void) idx;
	if (hosted_ap_count >= (uint8_t) TOPPERS_HOSTED_MAX_RECORDS) {
		return;
	}
	ap = &hosted_aps[hosted_ap_count];
	memset(ap, 0, sizeof(*ap));
	if ((ssid != NULL) && (slen > 0U)) {
		uint32_t	n = slen;

		if (n > (uint32_t)(TOPPERS_HOSTED_SSID_MAX - 1)) {
			n = (uint32_t)(TOPPERS_HOSTED_SSID_MAX - 1);
		}
		memcpy(ap->ssid, ssid, n);
		ap->ssid[n] = '\0';
	}
	ap->rssi = rssi;
	ap->chan = chan;
	ap->authmode = authmode;
	hosted_ap_count++;
}

const struct toppers_hosted_ap *
toppers_hosted_scan_record(uint8_t index)
{
	if (index >= hosted_ap_count) {
		return(NULL);
	}
	return(&hosted_aps[index]);
}

uint8_t
toppers_hosted_scan_count(void)
{
	return(hosted_ap_count);
}

int16_t
toppers_fmp3_wifi_scan_networks(void)
{
	uint32_t	want;

	if (!toppers_hosted_core_ready()) {
		return(-1);
	}
	hosted_ap_count = 0U;
	p4hosted_rpc_set_ap_cb(hosted_ap_record);

	if (!rp_scan_start()) {
		p4hosted_rpc_set_ap_cb(NULL);
		syslog(LOG_WARNING, "[WiFiHosted] scan_start failed");
		return(-1);
	}
	if (!rp_scan_get_ap_num()) {
		p4hosted_rpc_set_ap_cb(NULL);
		syslog(LOG_WARNING, "[WiFiHosted] scan_get_ap_num failed");
		return(-1);
	}
	/*  出典の順序どおり、件数を読んでから記録を読む。要求する件数は
	 *  min(相手の言う件数, 記録上限) に clamp する——出典のプローブが
	 *  min(n, 16) としているのと同じ理由で、持っていない件数を要求しない。
	 *  0 件なら記録の要求そのものを出さない（これも出典と同じ）。 */
	want = p4hosted_rpc_scan_ap_num();
	if (want > (uint32_t) TOPPERS_HOSTED_MAX_RECORDS) {
		want = (uint32_t) TOPPERS_HOSTED_MAX_RECORDS;
	}
	if ((want > 0U) && !rp_scan_get_ap_records(want)) {
		p4hosted_rpc_set_ap_cb(NULL);
		syslog(LOG_WARNING, "[WiFiHosted] scan_get_ap_records failed");
		return(-1);
	}
	p4hosted_rpc_set_ap_cb(NULL);

	if (p4hosted_rpc_scan_ap_num() > (uint32_t) hosted_ap_count) {
		/*  「20 件」が上限への飽和なのか本当に 20 件なのかを、後から
		 *  ログだけで見分けられるようにする。 */
		syslog(LOG_NOTICE,
			   "[WiFiHosted] scan: %u records kept of %u reported (limit %d)",
			   (uint_t) hosted_ap_count,
			   (uint_t) p4hosted_rpc_scan_ap_num(),
			   (int_t) TOPPERS_HOSTED_MAX_RECORDS);
	}
	return((int16_t) hosted_ap_count);
}

const char *
toppers_fmp3_wifi_ssid(uint8_t index)
{
	const struct toppers_hosted_ap	*ap = toppers_hosted_scan_record(index);

	return((ap != NULL) ? ap->ssid : "");
}

int32_t
toppers_fmp3_wifi_rssi(uint8_t index)
{
	const struct toppers_hosted_ap	*ap = toppers_hosted_scan_record(index);

	return((ap != NULL) ? ap->rssi : 0);
}

int32_t
toppers_fmp3_wifi_channel(uint8_t index)
{
	const struct toppers_hosted_ap	*ap = toppers_hosted_scan_record(index);

	return((ap != NULL) ? (int32_t) ap->chan : 0);
}

uint8_t
toppers_fmp3_wifi_auth_mode(uint8_t index)
{
	const struct toppers_hosted_ap	*ap = toppers_hosted_scan_record(index);

	return((ap != NULL) ? (uint8_t) ap->authmode : 0U);
}

void
toppers_fmp3_wifi_scan_delete(void)
{
	hosted_ap_count = 0U;
}
