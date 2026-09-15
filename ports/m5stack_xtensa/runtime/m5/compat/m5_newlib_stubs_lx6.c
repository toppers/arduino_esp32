/*
 *  newlib syscall stubs for the ESP32 (LX6) m5-unified profile
 *  (_exit / _kill / _getpid)
 *
 *  Why this file exists: the M5Stack core compiles sketches with
 *  -fstack-protector, so any sketch function with a local char array
 *  references __stack_chk_fail. In the m5-unified profile that resolves to
 *  the flash-side newlib's __stack_chk_fail (libc.a stack_protector.o),
 *  which references _exit, and abort() -> raise() -> _kill_r / _getpid_r
 *  reference _kill / _getpid. On the ESP32-S3 the three stubs live in
 *  arch/xtensa_gcc/esp32s3/chip_rom_libc.c (every S3 profile); the ESP32's
 *  wifi-connect profile avoids them with its own __stack_chk_fail
 *  (wifi/net/toppers_lwip_compat.c) and bt-classic carries them in
 *  bt/shim/bt_idf_stubs.c. The ESP32's m5-unified profile had none, so a
 *  sketch with a stack char array failed to link there with
 *  `undefined reference to _exit / _kill / _getpid` (found 2026-09-15 by
 *  examples/GpioInterrupt, which now uses a static buffer to stay
 *  buildable on older packages).
 *
 *  This file is listed for the ESP32 only (runtime/CMakeLists.txt, the
 *  m5-unified block, A1_CHIP == esp32): on the S3 it would duplicate
 *  chip_rom_libc.c, and the ESP32's bt-classic profile has its own copies.
 *  all-in-one (m5-unified + wifi-connect) gets this file through the
 *  m5-unified block and __stack_chk_fail from toppers_lwip_compat.c; the
 *  two sets do not overlap.
 *
 *  Same semantics as the S3 stubs: say so on the console, then stop. The
 *  stubs pull nothing else in (no _GLOBAL_REENT reference).
 */
#include <kernel.h>
#include <t_syslog.h>

#if !defined(TOPPERS_ESP32_LX6)
#error "m5_newlib_stubs_lx6.c is the ESP32 (LX6) file; the S3 has chip_rom_libc.c"
#endif

void
_exit(int status)
{
	syslog(LOG_EMERG, "libc: _exit(%d)", status);
	for (;;) {
	}
}

int
_kill(int pid, int sig)
{
	(void) pid;
	syslog(LOG_EMERG, "libc: _kill(sig=%d)", sig);
	for (;;) {
	}
}

int
_getpid(void)
{
	/*  No processes; newlib only asks on the raise() path. */
	return 1;
}
