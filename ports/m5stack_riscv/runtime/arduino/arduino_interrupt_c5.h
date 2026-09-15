/*
 *  attachInterrupt: the CPU interrupt line and its priority (ESP32-C5)
 *
 *  ESP32-C5 (ports/m5stack_riscv, chip branch of the C6 port: C5 plan A3)
 *  version of arduino_interrupt.h. Read by both the cfg
 *  (arduino_interrupt_c5.cfg) and the C file (arduino_interrupt_c5.c), so
 *  the two cannot disagree. The C6 file (arduino_interrupt.h) is untouched:
 *  its stages are under the X-check.
 *
 *  Line 23. The C5 port's INTNO is the CLIC line number itself (external
 *  lines 16..47; 0..15 are the CLIC's internal lines), and the line map of
 *  the dev repository (target/m5stampc5_gcc/target_kernel_impl.c,
 *  esp/shim/esp_shim_intr_c5_lines.h at 1d96bcba) is:
 *     16     tick (INTNO_TIMER: SYSTIMER target0 + FROM_CPU_0)
 *     17     console (INTNO_SIO: USB-Serial/JTAG)
 *     18/20/21  INTNO1/2/3 of the kernel test suite (software-raised edge
 *            lines, no matrix source; the tests are not linked here)
 *     19     INTNO_UNOPTED of the test suite (must stay unregistered there)
 *     22     the number ESP-IDF's interrupt allocator parks disconnected
 *            sources on (INT_MUX_DISABLED_INTNO 6 + 16); kept free
 *     23, 24 free ("for future peripherals" in the shim's line table)
 *     25..39 Wi-Fi/BT blob (esp_shim_intr_c5.cfg, CFG_INT + DEF_INH)
 *     40..44 reserved by ESP-IDF's vectors_clic.S (T1WDT / CACHEERR /
 *            MEMPROT / ASSIST_DEBUG / IPC_ISR); kept free
 *     45..47 free
 *  The C6 port uses 19 with the argument "no test is linked in this port";
 *  the C5 has 23 set aside for exactly this kind of use, so 19 is left as
 *  the test suite's unregistered line and 23 is taken.
 *
 *  A CFG_INT without TA_EDGE is a level line on the C5 (chip_initialize
 *  writes ATTR = machine/level to every line; clic_config_int sets the
 *  edge bit only for TA_EDGE), which is what the dispatch loop in
 *  arduino_interrupt_c5.c relies on. arduino_interrupt_c5.c checks this
 *  value against tick / console / the shim range / the reserved lines
 *  with #error.
 */

#ifndef TOPPERS_ARDUINO_INTERRUPT_C5_H
#define TOPPERS_ARDUINO_INTERRUPT_C5_H

/*  CPU interrupt line (INTNO of the C5 port = the CLIC line number). */
#define ARD_GPIO_INTNO		23U

/*  CFG_INT priority: TMAX_INTPRI (-1), the kernel's lowest level, one
 *  step below the console (INTPRI_SIO = -2, target_syssvc.h) and the shim
 *  lines (ESP_SHIM_C5_WIFI_INTPRI = -2), the same choice as the C6 and
 *  Xtensa ports. User callbacks must not delay the console or the Wi-Fi
 *  blob. */
#define ARD_GPIO_INTPRI		(TMAX_INTPRI)

/*  Core that receives GPIO interrupts. The C5 has one HP core. */
#define ARD_CORE_ID			0U

#endif /* TOPPERS_ARDUINO_INTERRUPT_C5_H */
