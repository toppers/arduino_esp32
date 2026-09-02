/*
 * ESP-IDF symbols the prebuilt BlueDroid archives reference and this port does
 * not otherwise supply.
 *
 * Two kinds live here, and the difference matters:
 *
 *   - Thin routings onto something real (esp_timer, esp_system_get_time,
 *     the interrupt helpers). These behave.
 *   - Stubs for code paths this profile does not enable: BLE Mesh, the BLE
 *     background-connection API, and SPP's VFS mode. They are referenced
 *     because they sit in archive members pulled in for other reasons, and
 *     none of them is reachable with CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY and
 *     ESP_SPP_MODE_CB. A stub that were ever actually called would be a bug,
 *     so each one says so in the log rather than returning silently.
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"



extern int64_t	esp_shim_time_us(void);
extern uint32_t	esp_shim_int_disable(void);
extern void	esp_shim_int_restore(uint32_t state);

#define BT_UNREACHABLE(name) \
	syslog_0(LOG_ERROR, "bt: " name " reached; this profile does not enable it")

/*
 *  Time and clock.
 */
int64_t
esp_system_get_time(void)
{
	return(esp_shim_time_us());
}

int
clock_gettime(int clk_id, void *tp)
{
	/*  BlueDroid uses this only for its own timestamps. */
	struct {
		int32_t	tv_sec;
		int32_t	tv_nsec;
	}		*ts = tp;
	int64_t		now;

	(void) clk_id;
	if (ts == NULL) {
		return(-1);
	}
	now = esp_shim_time_us();
	ts->tv_sec = (int32_t)(now / 1000000);
	ts->tv_nsec = (int32_t)((now % 1000000) * 1000);
	return(0);
}

/*
 *  Crystal frequency in Hz. The M5Stack Basic runs 40MHz, which is what the
 *  LX6 side of this port assumes throughout.
 *
 *  ★This is a function, not a variable. Defining it as data links, and then
 *  fails at the relocation: the caller sits in flash (IROM) and the datum in
 *  DRAM, so the windowed longcall "crosses 1GB boundary".
 */
int
esp_clk_xtal_freq(void)
{
	return(40000000);
}

/*
 *  esp_timer.
 *
 *  wifi_stubs.c already owns create/delete/stop/start_periodic. Only the
 *  one-shot and the liveness query are missing, and BlueDroid's alarm layer
 *  is the only caller.
 */
extern esp_err_t esp_timer_start_periodic(void *timer, uint64_t period);

esp_err_t
esp_timer_start_once(void *timer, uint64_t timeout_us)
{
	return(esp_timer_start_periodic(timer, timeout_us));
}

bool
esp_timer_is_active(void *timer)
{
	(void) timer;
	return(false);
}

/*
 *  Cross-core call. FMP3 runs the BT stack on one core here, so the callee
 *  runs inline; esp_ipc_call_blocking is bt_shim.c's and does the same.
 */
esp_err_t
esp_ipc_call(uint32_t cpu_id, void (*func)(void *), void *arg)
{
	(void) cpu_id;
	if (func != NULL) {
		func(arg);
	}
	return(ESP_OK);
}

/*
 *  Logging. The archives call esp_log_writev(); route it to the kernel log the
 *  way the rest of this port does rather than dropping it.
 */
void
esp_log_writev(uint32_t level, const char *tag, const char *format,
			   __builtin_va_list args)
{
	(void) level;
	(void) format;
	(void) args;
	if (tag != NULL) {
		syslog_1(LOG_INFO, "bt: %s", (intptr_t) tag);
	}
}

/*
 *  Interrupt helpers the controller blob calls directly.
 */
void
xt_ints_off(uint32_t mask)
{
	uint32_t	state = esp_shim_int_disable();
	uint32_t	enabled;

	__asm__ __volatile__ ("rsr.intenable %0" : "=a" (enabled));
	enabled &= ~mask;
	__asm__ __volatile__ ("wsr.intenable %0; rsync" :: "a" (enabled));
	esp_shim_int_restore(state);
}

/*
 *  The blob asks the scheduler to re-dispatch after an ISR. FMP3 does that on
 *  its own when the ISR returns, so there is nothing to arm.
 */
void
_frxt_setup_switch(void)
{
}

/*
 *  RTC slow clock source. The BT controller reads it to pick its sleep clock;
 *  this port never enables controller sleep, so the internal RC value is the
 *  honest answer (RTC_SLOW_FREQ_RTC == 0).
 */
int
rtc_clk_slow_src_get(void)
{
	return(0);
}

/*
 *  SPP's VFS mode. ESP_SPP_MODE_CB is what toppers_bt_spp.c initialises, so
 *  none of these is reachable; they are referenced from the same archive
 *  member as the callback path.
 */
esp_err_t
esp_vfs_register_fd(void *vfs_id, int *fd)
{
	(void) vfs_id;
	(void) fd;
	BT_UNREACHABLE("esp_vfs_register_fd");
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
esp_vfs_unregister_fd(void *vfs_id, int fd)
{
	(void) vfs_id;
	(void) fd;
	BT_UNREACHABLE("esp_vfs_unregister_fd");
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
esp_vfs_register_with_id(const void *vfs, void *ctx, void *vfs_id)
{
	(void) vfs;
	(void) ctx;
	(void) vfs_id;
	BT_UNREACHABLE("esp_vfs_register_with_id");
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
esp_vfs_unregister_with_id(void *vfs_id)
{
	(void) vfs_id;
	BT_UNREACHABLE("esp_vfs_unregister_with_id");
	return(ESP_ERR_NOT_SUPPORTED);
}

/*
 *  BLE. The controller is configured BR/EDR only, so the BLE background
 *  connection API is never entered.
 */
void
BTM_BleSetBgConnType(uint8_t bg_conn_type, void *p_select_cback)
{
	(void) bg_conn_type;
	(void) p_select_cback;
	BT_UNREACHABLE("BTM_BleSetBgConnType");
}

/*
 *  BLE Mesh. Not enabled; these are referenced from btc members that the
 *  Classic path shares.
 */
void *comp_0;

bool bt_mesh_prov_active(void) { BT_UNREACHABLE("bt_mesh_prov_active"); return(false); }
void bt_mesh_prov_complete(uint16_t a, uint8_t b) { (void) a; (void) b; BT_UNREACHABLE("bt_mesh_prov_complete"); }
void bt_mesh_prov_reset(void) { BT_UNREACHABLE("bt_mesh_prov_reset"); }
int bt_mesh_v11_init(void) { BT_UNREACHABLE("bt_mesh_v11_init"); return(-1); }
int bt_mesh_comp_page_check(void) { BT_UNREACHABLE("bt_mesh_comp_page_check"); return(-1); }
void *bt_mesh_get_comp_data(uint8_t page) { (void) page; BT_UNREACHABLE("bt_mesh_get_comp_data"); return(NULL); }
void bt_mesh_net_adv_xmit_update(void) { BT_UNREACHABLE("bt_mesh_net_adv_xmit_update"); }
bool bt_mesh_tag_immutable_cred(void *tag) { (void) tag; BT_UNREACHABLE("bt_mesh_tag_immutable_cred"); return(false); }
bool bt_mesh_tag_relay(void *tag) { (void) tag; BT_UNREACHABLE("bt_mesh_tag_relay"); return(false); }
bool bt_mesh_tag_send_segmented(void *tag) { (void) tag; BT_UNREACHABLE("bt_mesh_tag_send_segmented"); return(false); }

/*
 *  newlib syscalls.
 *
 *  -lc comes in on this profile (fmp3_link.py links it), and the libc members
 *  BlueDroid pulls reference these four. FMP3 has no process model and no
 *  file descriptors, so they cannot do anything but refuse - the point is that
 *  they refuse visibly instead of leaving the image unlinkable.
 */
void
_exit(int status)
{
	syslog_1(LOG_EMERG, "bt: _exit(%d) called; halting", (intptr_t) status);
	for (;;) {
	}
}

int
_getpid(void)
{
	return(1);
}

int
_kill(int pid, int sig)
{
	(void) pid;
	(void) sig;
	return(-1);
}

int
_write(int fd, const void *buf, int len)
{
	(void) fd;
	(void) buf;
	return(len);
}
