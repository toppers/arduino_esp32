/*
 *  newlib syscall hooks for the ESP32-C6 minimal stage
 *  ==========================================================================
 *  New for the arduino_esp32 C6 port (stage 1, Task 3); not taken from the
 *  development repository. The approach is the Xtensa port's
 *  ports/m5stack_xtensa/runtime/arch/xtensa_gcc/esp32s3/chip_rom_libc.c
 *  (its _exit / _kill / _getpid stubs), written for the RISC-V toolchain's
 *  newlib-nano; the file is not shared with the Xtensa port so that a change
 *  on one side cannot silently move the other stage.
 *
 *  Why this exists
 *  --------------------------------------------------------------------------
 *  The M5Stack core compiles every sketch translation unit with
 *  -fstack-protector (esp32c6-libs/flags/{c,cpp}_flags). A sketch with a
 *  protected frame - a local char array is enough - references
 *  __stack_chk_fail and __stack_chk_guard, which the link takes from the
 *  toolchain's libc_nano.a (the tail -lc of the manifest). That
 *  __stack_chk_fail is newlib's: it write()s a message to fd 2 and calls
 *  abort(), which raise()s SIGABRT. The chain ends in five hooks newlib
 *  expects the system to provide:
 *
 *      __stack_chk_fail -> write  -> _write_r  -> __getreent, _write
 *                       -> abort  -> raise     -> _getpid_r -> _getpid
 *                                              -> _kill_r   -> _kill
 *                       -> _exit
 *
 *  Nothing in the C6 stage or in the ESP32-C6 ROM linker scripts defines
 *  them (the ROM exports memcpy, strlen and the like, none of the syscall
 *  layer), so such a sketch failed to link with "undefined reference to
 *  _exit / __getreent / _kill / _getpid / _write". Measured on 2026-09-15
 *  with a sketch whose loop() fills a char[32]; Blink, LibraryInfo and the
 *  two-file sketch have no protected frame and linked without this file.
 *  The Xtensa port met the same chain from LGFXBase::setFont on 2026-08-22.
 *
 *  What these do
 *  --------------------------------------------------------------------------
 *  There is no process, no file descriptor table and no exit on this
 *  kernel, so the hooks are terminal: they say what happened on the kernel
 *  log and stop. _write forwards fd 1 and 2 to the kernel's log port so the
 *  "*** stack smashing detected ***" message reaches the console instead of
 *  vanishing; any other fd fails with -1 and errno = EBADF, the way a
 *  syscall reports a descriptor it does not have (newlib's _write_r copies
 *  errno into the reent it was given, so a caller that looks sees EBADF
 *  rather than whatever errno held before). __getreent returns newlib's
 *  single global reent (_impure_ptr): the minimal stage has no per-task
 *  reent (the Xtensa port embeds one in each task context; the C6 arch
 *  layer, vendored unchanged from the development repository, does not),
 *  and the only newlib code on this path that dereferences it is _write_r's
 *  errno handling.
 *
 *  Defining a hook pulls nothing else in: these are leaves. What pulls the
 *  newlib objects in is the sketch's reference to __stack_chk_fail, and
 *  that happens with or without this file. A sketch without a protected
 *  frame links the same objects as before, and --gc-sections drops these
 *  five functions from its image.
 *
 *  __stack_chk_guard is deliberately left to libc_nano.a: its copy is a
 *  zero-initialised word in .bss. ESP-IDF seeds the guard from the hardware
 *  RNG in esp_system; that is a hardening step for a later stage, not a
 *  link requirement.
 */

#include <stddef.h>
#include <errno.h>
#include <t_syslog.h>
#include "target_syssvc.h"

struct _reent;
extern struct _reent *_impure_ptr;

void _exit(int status) __attribute__((noreturn));

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
	/*  No processes; newlib reaches this from raise() only.  */
	return 1;
}

int
_write(int fd, const void *buffer, size_t length)
{
	const char *text = (const char *) buffer;
	size_t index;

	if (fd != 1 && fd != 2) {
		errno = EBADF;
		return -1;
	}
	for (index = 0; index < length; index++) {
		target_fput_log(text[index]);
	}
	return (int) length;
}

struct _reent *
__getreent(void)
{
	return _impure_ptr;
}
