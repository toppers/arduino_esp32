/*
 * The executable task body is compiled by Arduino builder and linked as an
 * external object. This translation unit keeps the FMP3 application source
 * contract explicit and gives the configuration a stable application name.
 *
 * ESP32-C6 (ports/m5stack_riscv) copy of ports/m5stack_xtensa/app/phase3/
 * phase3_arduino_app.c. The Xtensa file also carries a TOPPERS_XIP_PADDR_PROBE
 * block that prints what boot/flash_cache_init.c resolved; the C6 image is
 * linked at fixed virtual addresses (no runtime PADDR resolution, no
 * flash_cache_init), so that block has nothing to report here and is left out.
 */
const char toppers_phase3_application[] = "Arduino sketch bridge";
