/*
 *  Weak defaults for the Wi-Fi API a profile does not provide (ESP32-C6).
 *
 *  ESP32-C6 (ports/m5stack_riscv) copy of
 *  ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_optional_stubs.c;
 *  same symbols, same return values, comments in English.
 *
 *  `ToppersFMP3_WiFi.cpp` calls every API unconditionally, so a profile
 *  that has no implementation for one of them still has to link. These
 *  weak definitions are that landing pad.
 *
 *  Landing here is a FAILURE, not a normal path.
 *
 *  This has bitten before: folding the wifi-scan profile into wifi-connect
 *  was once justified by "the WiFiScan example builds on wifi-connect",
 *  when in fact the weak toppers_fmp3_wifi_scan_networks() below was
 *  silently returning ScanFailed and nothing was scanning. A link that
 *  succeeds proves nothing about behaviour (BUILDING.md).
 *
 *  So a stub reports once through syslog when it is reached. The return
 *  value is unchanged (the API contract stays "failure"); the report is
 *  limited to once per API, because polling callers would otherwise flood
 *  the log.
 */
#include <stdint.h>
#include <kernel.h>
#include <t_syslog.h>

#define WEAK __attribute__((weak))

/*
 *  Report once. `reported` is a separate static per API. Not called from
 *  ISR context (these are Arduino-task APIs).
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
