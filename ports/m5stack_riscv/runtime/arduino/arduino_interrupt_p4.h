/*
 *  attachInterrupt: the CPU interrupt line and its priority (ESP32-P4)
 *
 *  ESP32-P4 (ports/m5stack_riscv, StampP4 plan stage B2c) version of
 *  arduino_interrupt.h (C6) / arduino_interrupt_c5.h (C5). Read by both the
 *  cfg (arduino_interrupt_p4.cfg) and the C file (arduino_interrupt_p4.c),
 *  so the two cannot disagree - the single-source-of-truth shape the Xtensa
 *  port arrived at after its .c and .cfg drifted apart on a line number.
 *  The C6 and C5 files are untouched (their stages are under the X-check).
 *
 *  Line 23. The P4's INTNO is the CLIC line number itself (external lines
 *  16..47; 0..15 are the CLIC's internal lines). The occupied lines are
 *  listed in wifi/shim/esp_shim_intr_clic_lines.h, which is the port's line
 *  table; repeated here only as the reason for the choice, and enforced
 *  below against that header's own macros rather than against a copy:
 *     3      IPI (CLINT msip, internal line)
 *     7      tick (CLINT mtime, internal line)
 *     16     INTNO_SIO, the USB-Serial/JTAG console
 *     17     INTNO1 of the kernel test suite (= ESP-IDF's crosscore slot
 *            in the IDF-linked world; the shim header's "line 17" record)
 *     18     INTNO_UNOPTED, which the tests need to stay unregistered
 *     19     INTNO3 of the test suite
 *     22     the slot ESP-IDF parks disconnected sources on
 *     30..35 the CLIC shim's six slots (ESP_SHIM_CLIC_INTR_LINE0..5);
 *            SDMMC - the SDIO link to the companion C6 - is one of them
 *     40..44 reserved by ESP-IDF's vectors_clic.S
 *     45     INTNO2 of the test suite
 *     46     INTNO_EMAC (Ethernet)
 *     47     SDMMC by convention
 *  Free: 20, 21, 23..29, 36..39. 23 is taken, the same number the C5 port
 *  took, so the two RISC-V Arduino ports read alike in a log.
 *
 *  A CFG_INT without TA_EDGE is a level line on the P4 (chip_initialize
 *  writes ATTR = machine/level to every line; clic_config_int sets the edge
 *  bit only for TA_EDGE), which is what the dispatch loop in
 *  arduino_interrupt_p4.c relies on.
 */

#ifndef TOPPERS_ARDUINO_INTERRUPT_P4_H
#define TOPPERS_ARDUINO_INTERRUPT_P4_H

/*  CPU interrupt line (INTNO of the P4 port = the CLIC line number). */
#define ARD_GPIO_INTNO		23U

/*
 *  CFG_INT priority: TMAX_INTPRI (-1), the kernel's lowest level. The
 *  console (INTPRI_SIO = -4), the CLIC shim's slots
 *  (ESP_SHIM_CLIC_INTR_INTPRI = -4) and the Ethernet line (-4) are all
 *  higher, so a user callback cannot delay the console or the SDIO link
 *  to the companion C6. Same choice as the C6, C5 and Xtensa ports.
 */
#define ARD_GPIO_INTPRI		(TMAX_INTPRI)

/*
 *  Core that receives GPIO interrupts. The P4 has two HP cores and the
 *  Arduino task runs on PRC1 (hart 0), which is also where the interrupt
 *  matrix entry is written (INTMTX_MAP(0, ...)). The value is passed to
 *  gpio_ll_intr_enable_on_core(), which on the P4 ignores it: the SDK's
 *  hal/esp32p4/gpio_ll.h always enables the GPIO_INTR0 signal
 *  ("TODO: IDF-7995" in that header), which is why the matrix source below
 *  is ETS_GPIO_INTR0_SOURCE and not one of the other three.
 */
#define ARD_CORE_ID			0U

#endif /* TOPPERS_ARDUINO_INTERRUPT_P4_H */
