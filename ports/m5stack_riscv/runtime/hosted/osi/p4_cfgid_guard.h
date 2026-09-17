/*
 *  "kernel_cfg.h was pre-included" guard (ESP32-P4, hosted Wi-Fi)
 *
 *  Written for this repository; it has no counterpart in the development
 *  tree. It is never #included by a source file: the CMake rule that adds
 *  `-include kernel_cfg.h` to the two pool files adds `-include` of THIS
 *  header right after it, so the check runs before the translation unit
 *  itself does.
 *
 *  ---- What it is guarding, and why a guard is needed at all ----
 *
 *  hosted/osi/p4hosted_pools.c and eth/lwip_port/fmp3_lwip_pools.c do not
 *  include kernel_cfg.h. They carry a FALLBACK list of kernel object ids
 *  (`#ifndef HOSTED_TSK1` / `#ifndef FMP3_LWIP_TSK1` ...) that was correct
 *  for the cfg ORDER of the configuration they were written against. This
 *  port's cfg order is different, so the fallbacks are wrong here -
 *  measured on this build, 2026-09-18:
 *
 *      HOSTED_TSK1..8       fallback 2..9    generated 1..8    (+1, all 8)
 *      HOSTED_SEM1..16      fallback 5..20   generated 3..18   (+2, all 16)
 *      FMP3_LWIP_TSK1..8    fallback 11..18  generated 10..17  (+1, all 8)
 *      FMP3_LWIP_SEM1..16   fallback 21..36  generated 19..34  (+2, all 16)
 *      the mutexes and data queues of both files                (equal)
 *
 *  48 wrong ids out of 111, and the ones that happen to agree are what
 *  makes this dangerous: the build links, the code runs, and only some of
 *  it does the wrong thing.
 *
 *  The failure this produced on hardware (stage B4): _h_thread_create()
 *  took pool slot i and called act_tsk(HOSTED_TSK<i+1>), which with the
 *  fallbacks names the NEXT task. That task exists and is dormant, so
 *  **act_tsk returns E_OK**; its trampoline then reads pool slot i+1, which
 *  is unused (start_routine == NULL), does nothing and ends. The caller was
 *  told the thread was created. The receive pump therefore never ran
 *  (pump_loops=0, rx_total=0, tx_ok=0) and DHCP timed out after 30 s with
 *  the STA associated. Checking a return value could not catch this, and
 *  did not.
 *
 *  ---- The check ----
 *
 *  If kernel_cfg.h reached the compiler first, these macros are defined and
 *  this header is inert. If the -include is ever dropped, the fallbacks
 *  would silently take over again; that is what the #error is for. The
 *  negative control is -DA1_P4_HOSTED_NO_CFGID_FIX=ON, which removes the
 *  pre-include: the build must then STOP here rather than produce an image
 *  whose threads do not run.
 */
#ifndef TOPPERS_P4_CFGID_GUARD_H
#define TOPPERS_P4_CFGID_GUARD_H

#if !defined(HOSTED_TSK1) && !defined(FMP3_LWIP_TSK1)
#error "p4_cfgid_guard.h: kernel_cfg.h was not pre-included into a pool file, so its built-in fallback object ids would be used. On this port 48 of them are wrong (see the comment above); act_tsk() then wakes the neighbouring task and still returns E_OK. Restore the -include kernel_cfg.h in ports/m5stack_riscv/runtime/CMakeLists.txt."
#endif

#endif /* TOPPERS_P4_CFGID_GUARD_H */
