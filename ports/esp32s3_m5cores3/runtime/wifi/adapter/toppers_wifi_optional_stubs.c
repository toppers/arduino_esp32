/*
 *  profile が提供しない Wi-Fi API の weak な既定実装
 *
 *  `ToppersFMP3_WiFi.cpp` は全 API を無条件に呼ぶので、profile が実体を持たない
 *  API があってもリンクが通るように weak の受け皿を置いている。
 *
 *  ★ここに落ちるのは「失敗」であって正常系ではない。★
 *
 *  ★実害を踏んだことがある: wifi-scan profile を wifi-connect へ畳もうとして
 *  「WiFiScan の例題が wifi-connect でもビルドできる」ことを根拠にしたが、
 *  それは下の `toppers_fmp3_wifi_scan_networks()` が黙って `ScanFailed` を
 *  返していただけで、実際にはスキャンしていなかった。**リンクが通ることは
 *  動くことの証明にならない。**
 *
 *  同じ間違いを繰り返さないよう、スタブに落ちたら一度だけ syslog へ出す。
 *  戻り値は変えない（API の契約は「失敗」のままである）。出力は
 *  API ごとに 1 回に抑える——ポーリングで呼ばれるとログが溢れるため。
 */
#include <stdint.h>
#include <kernel.h>
#include <t_syslog.h>

#define WEAK __attribute__((weak))

/*
 *  一度だけ報告する。`reported` は API ごとに別の静的変数を渡す。
 *  ISR 文脈からは呼ばれない（Arduino タスクからの API なので）。
 */
static void
stub_report(uint8_t *reported, const char *name)
{
	if (*reported == 0U) {
		*reported = 1U;
		syslog(LOG_WARNING,
			   "toppers_fmp3_wifi: %s is not provided by this runtime profile; "
			   "returning failure", name);
	}
}

#define STUB_REPORT(name)						\
	do {										\
		static uint8_t reported_;				\
		stub_report(&reported_, (name));		\
	} while (0)

WEAK int16_t toppers_fmp3_wifi_scan_networks(void)
{ STUB_REPORT("scan_networks"); return -2; }
WEAK const char *toppers_fmp3_wifi_ssid(uint8_t index) { (void)index; return ""; }
WEAK int32_t toppers_fmp3_wifi_rssi(uint8_t index) { (void)index; return 0; }
WEAK int32_t toppers_fmp3_wifi_channel(uint8_t index) { (void)index; return 0; }
WEAK uint8_t toppers_fmp3_wifi_auth_mode(uint8_t index) { (void)index; return 0; }
WEAK void toppers_fmp3_wifi_scan_delete(void) {}

WEAK uint8_t toppers_fmp3_wifi_begin(const char *ssid, const char *password)
{ (void)ssid; (void)password; STUB_REPORT("begin"); return 4; }
WEAK uint8_t toppers_fmp3_wifi_status(void) { return 6; }
WEAK void toppers_fmp3_wifi_disconnect(void) {}
WEAK uint32_t toppers_fmp3_wifi_local_ip(void) { return 0; }
WEAK uint32_t toppers_fmp3_wifi_gateway_ip(void) { return 0; }
WEAK uint32_t toppers_fmp3_wifi_subnet_mask(void) { return 0; }
WEAK int toppers_fmp3_wifi_host_by_name(const char *host, uint32_t *address)
{
	(void)host;
	if (address != 0) *address = 0;
	STUB_REPORT("host_by_name");
	return 0;
}
WEAK int toppers_fmp3_wifi_tcp_request(const char *host, uint16_t port,
    const char *request, char *response, uint32_t capacity, uint32_t timeout_ms)
{
    (void)host; (void)port; (void)request; (void)response;
    (void)capacity; (void)timeout_ms;
    STUB_REPORT("tcp_request");
    return -1;
}
