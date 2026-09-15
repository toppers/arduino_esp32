/*
 *  attachInterrupt: the CPU interrupt line and its priority (ESP32-C6)
 *
 *  ESP32-C6 (ports/m5stack_riscv) version of
 *  ports/m5stack_xtensa/runtime/arduino/arduino_interrupt.h. Read by both
 *  the cfg (arduino_interrupt.cfg) and the C file (arduino_interrupt.c), so
 *  the two cannot disagree.
 *
 *  Line 19 (decision S3-4, docs/c6-port.md). The C6 port's line map
 *  (target/m5nanoc6_gcc/target_kernel_impl.c, wifi/shim/
 *  esp_shim_intr_intmtx_lines.h):
 *     1..15  Wi-Fi/BT blob (esp_shim_intr_intmtx.cfg, CFG_INT + DEF_INH)
 *     16     tick (INTNO_TIMER: SYSTIMER target0 + FROM_CPU_0)
 *     17     console (INTNO_SIO: USB-Serial/JTAG)
 *     18/20/21  FROM_CPU_1/2/3, routed at boot for the kernel test suite's
 *            INTNO1/2/3 (target_test.h); the tests are not linked here but
 *            the routing is, so those lines are not free
 *     19     routed to nothing at boot (the tests use it as "the
 *            unregistered line", INTNO_UNOPTED; no test is linked in this
 *            port, so it is free here)
 *     22..31 free
 *  All lines are level-typed by intmtx_initialize(), which is what the
 *  dispatch loop in arduino_interrupt.c relies on. arduino_interrupt.c
 *  checks this value against tick / console / the shim range with #error.
 */

#ifndef TOPPERS_ARDUINO_INTERRUPT_H
#define TOPPERS_ARDUINO_INTERRUPT_H

/*  CPU interrupt line (INTNO of the C6 port = the plain line number). */
#define ARD_GPIO_INTNO		19U

/*  CFG_INT priority: TMAX_INTPRI (-1), the kernel's lowest level, one
 *  step below the console (INTPRI_SIO = -2, target_syssvc.h) and the shim
 *  lines (ESP_SHIM_INTMTX_INTR_INTPRI = -2), the same choice as the Xtensa
 *  port. User callbacks must not delay the console or the Wi-Fi blob. */
#define ARD_GPIO_INTPRI		(TMAX_INTPRI)

/*  Core that receives GPIO interrupts. The C6 has one. */
#define ARD_CORE_ID			0U

#endif /* TOPPERS_ARDUINO_INTERRUPT_H */
