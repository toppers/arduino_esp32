cmake_minimum_required(VERSION 3.23)

#
#  RISC-V (ESP32-C6 M5NanoC6 / ESP32-C5 M5Stamp-C5, RV32IMAC, single core)
#  runtime for the Arduino board package: builds the sketch-independent FMP3
#  stage of the chip A1_CHIP names.
#
#  This is the RISC-V counterpart of ports/m5stack_xtensa/runtime/CMakeLists.txt
#  and is driven the same way (scripts/build_prebuilt_stages.py, chip rows
#  "esp32c6" and "esp32c5"). It is deliberately a separate file rather than a
#  chip branch of the Xtensa one: the Xtensa file is protected by the X-check
#  (its stages are compared byte for byte against a baseline), and nothing the
#  RISC-V chips need - -march instead of -mlongcalls, a fixed-VMA image
#  instead of runtime PADDR resolution, no flash_cache_init - would fit as a
#  small branch there.
#
#  Chips (the "chip table" below, C5 plan decision A2): everything that names
#  a chip - the target and arch directories, the XIP linker script, the ROM
#  linker scripts, the seam sources, the console HAL, the CPU clock variable,
#  the staging script - is one row per chip, chosen by A1_CHIP. The esp32c6
#  row carries exactly the strings this file used to spell inline, so the C6
#  stages are byte-identical to what they were before the table (the X-check
#  of the C5 plan, stage 1 AC-1i, is what proves it). The esp32c5 row is the
#  dev repository's cmake/a1_c5_stage1.cmake seam configuration.
#
#  What it builds (profiles "minimal" and "wifi-connect"):
#    fmp3            libfmp3.a from third_party/fmp3_core with the chip/target
#                    layers in arch/riscv_gcc/<chip> and target/<target>
#                    (vendored from fmp3_esp_idf_dev: C6 c7fef18, C5 1d96bcba;
#                    IMPORT_PROVENANCE.md)
#    seam_objects    application TU, syssvc, console driver, app descriptor,
#                    CPU clock boost
#    seam_start      chip_start.S (FMP3 entry) and seam_<chip>_entry.S (image entry)
#    seam_cxx        init_array.cpp (esp_run_init_array for the sketch bridge)
#    wifi_objects    (wifi-connect) the ESP-IDF OS shim, PHY/Wi-Fi hal sources,
#                    the six esp-idf originals and the lwIP glue under wifi/
#                    (second copy of the dev esp/shim, IMPORT_PROVENANCE.md),
#                    plus the Arduino-facing adapter (wifi/adapter) and
#                    attachInterrupt (arduino/). C6 only until C5 stage 3.
#    fmp3_prebuilt   cmake/prebuilt_stage_<chip>.cmake -> ${CMAKE_BINARY_DIR}/prebuilt
#
#  Build facts come from the dev repository's cmake/a1_c6_stage1.cmake (the
#  seam-c6-* presets; the wifi-connect profile mirrors its if(A1_C6_WIFI)
#  block) and cmake/a1_c5_stage1.cmake (seam-c5-min); where this file differs
#  it says so in place.
#

foreach(required
        FMP3_CORE_ROOT
        FMP3_APPLICATION_DIR
        FMP3_APPLICATION_NAME
        ARDUINO_SDK_LD_ROOT
        ARDUINO_SDK_INCLUDE_ROOT
        ARDUINO_SDK_LIBRARY_ROOT
        A1_ESPTOOL_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Required setting is missing: ${required}")
    endif()
endforeach()

get_filename_component(RUNTIME_ROOT "${CMAKE_CURRENT_LIST_DIR}" REALPATH)

#
#  Keep the builder's directory out of the distributed objects (same reason
#  and same placement rule as the Xtensa runtime: it must precede
#  add_subdirectory(fmp3_core), because add_compile_options only reaches
#  targets created after it, and the kernel's assert macros put __FILE__ into
#  .rodata). The SDK include root is mapped too: unlike the Xtensa minimal
#  profile, every C6 object includes hal/soc headers from the M5Stack core,
#  which lives under the user's Arduino data directory.
#
get_filename_component(FMP3_REPO_ROOT "${RUNTIME_ROOT}/../../.." REALPATH)
add_compile_options("-ffile-prefix-map=${FMP3_REPO_ROOT}=.")
get_filename_component(ARDUINO_SDK_INCLUDE_ROOT
    "${ARDUINO_SDK_INCLUDE_ROOT}" REALPATH)
add_compile_options("-ffile-prefix-map=${ARDUINO_SDK_INCLUDE_ROOT}=sdk/include")

get_filename_component(FMP3_CORE_ROOT "${FMP3_CORE_ROOT}" REALPATH)
get_filename_component(FMP3_APPLICATION_DIR
    "${FMP3_APPLICATION_DIR}" REALPATH)
get_filename_component(ARDUINO_SDK_LD_ROOT
    "${ARDUINO_SDK_LD_ROOT}" REALPATH)
get_filename_component(ARDUINO_SDK_LIBRARY_ROOT
    "${ARDUINO_SDK_LIBRARY_ROOT}" REALPATH)

option(A1_PREBUILT_STRIP_DEBUG
    "Strip debug info from the prebuilt stage objects" ON)

if(NOT DEFINED FMP3_RUNTIME_PROFILE)
    set(FMP3_RUNTIME_PROFILE minimal)
endif()
if(NOT FMP3_RUNTIME_PROFILE MATCHES "^(minimal|wifi-connect)$")
    message(FATAL_ERROR
        "FMP3_RUNTIME_PROFILE must be minimal or wifi-connect for the RISC-V "
        "port (got '${FMP3_RUNTIME_PROFILE}')")
endif()

#
#  ---- chip table ----------------------------------------------------------
#
#  build_prebuilt_stages.py always passes A1_CHIP; a missing value is
#  accepted as esp32c6 the way the Xtensa runtime accepts a missing value as
#  esp32s3 (and as this file did before it had a table), and anything not in
#  the table is refused.
#
#  Per chip:
#    A1_CHIP_TAG            C6 / C5: the infix of the chip-named build
#                           definitions (SEAM_<TAG>_CLK_BOOST,
#                           SEAM_<TAG>_ENTRY_MARK) and cache variables
#                           (A1_<TAG>_CPU_FREQ_MHZ, A1_<TAG>_SEAM_ENTRY_MARK,
#                           the target.cmake override A1_<TAG>_LDSCRIPT)
#    A1_CHIP_TARGET_NAME    target/<name> (FMP3_TARGET)
#    A1_CHIP_ARCH_NAME      arch/riscv_gcc/<name>, also the SDK's ld name stem
#    A1_XIP_LD_NAME         the XIP linker script in the target directory
#    A1_CHIP_ROM_LD         the SDK ld/ file whose presence proves the SDK
#    A1_CHIP_USBJTAG_HAL    the target's USB-Serial/JTAG console HAL
#    A1_CHIP_SEAM_APPDESC / A1_CHIP_SEAM_CLK / A1_CHIP_SEAM_ENTRY
#                           the seam sources under seam/ (dev esp/boot)
#    A1_CHIP_PREBUILT_SCRIPT cmake/prebuilt_stage_<chip>.cmake
#    A1_CHIP_CPU_FREQ_CHOICES / A1_CHIP_CPU_FREQ_DEFAULT / A1_CHIP_CPU_FREQ_BOOST
#                           the MHz values the chip layer is calibrated for,
#                           the default, and the value at which the seam
#                           entry raises the clock (SEAM_<TAG>_CLK_BOOST)
#    A1_CHIP_WIFI           ON when this file can stage wifi-connect for the
#                           chip
#    A1_ROM_LDS_<profile>   the ROM linker scripts per profile (below)
#  Rows the ESP32-P4 added (StampP4 plan, stage A1). Each has a default that
#  is what the C6 and C5 rows did before the field existed, set right below
#  this comment, so those two rows do not spell them and their stages are
#  unchanged (X-check):
#    A1_CHIP_PRC_NUM        FMP3_PRC_NUM (TNUM_PRCID). 1 = single core. The
#                           P4 is 2 (SMP): its target_kernel.h takes 1..4,
#                           and the seam starts core1 (below)
#    A1_CHIP_START_FILE     the startup file linked in front of everything
#                           (seam_start). Empty = the chip's chip_start.S
#                           (C6 / C5: the common start.S issues FPU
#                           instructions their rv32imac assembler rejects).
#                           The P4 (rv32imafc) links fmp3_core's common
#                           start.S itself, as the dev seam does
#    A1_CHIP_SEAM_EXTRA_SOURCES
#                           more seam sources (P4: seam_p4_core1.c and
#                           seam_p4_core1_entry.S, the core1 wake-up)
#    A1_CHIP_EXTRA_DEFS     more FMP3_COMPILE_DEFS, reaching the kernel
#                           library as well as the runtime objects (P4:
#                           TOPPERS_SEAM_P4_CORE1, read by the target's
#                           target_mprc_initialize - dev learned the hard
#                           way that a definition given to the executable
#                           target alone silently leaves that code out)
#    A1_CHIP_CLK_BOOST_DEFINE
#                           the definition that turns the seam clock boost
#                           on. Empty = SEAM_<TAG>_CLK_BOOST=1 (C6 / C5);
#                           the P4 seam entry reads SEAM_P4_CLK360
#    A1_CHIP_CORE_CLK_DEFINE
#                           ON = pass -DCORE_CLK_MHZ=<freq> (C6 / C5, whose
#                           esp32c<N>.h derive their timing from it). OFF
#                           for the P4, whose kit header defines it itself
#                           (m5stamp_esp32p4_kit.h: 360)
#    A1_CHIP_NEWLIB_SYSCALLS
#                           ON = link seam/newlib_syscalls.c (_exit, _kill,
#                           _getpid, _write, __getreent for newlib-nano).
#                           OFF for the P4: its target_kernel_impl.c defines
#                           the same names (and _sbrk) already, and the
#                           stage's duplicate-symbol audit is strict
#    A1_CHIP_LDFRAG_DIR     directory the XIP linker script INCLUDEs its
#                           fragments from (P4: p4_mem_high.ld etc. beside
#                           esp32p4_xip.ld). Added as -Wl,-L for the cfg
#                           pass-1 link; prebuilt_stage_p4.cmake inlines
#                           the fragments into the staged copy so the
#                           sketch link needs no search path
#    A1_CHIP_USBJTAG_HAL    may be empty (P4: chip_serial.c drives the
#                           USB-Serial/JTAG console itself)
#  wifi-connect only (the block "wifi-connect" further down reads them; the
#  C6 values are the strings that block used to spell inline, so the C6
#  wifi-connect stage is byte-identical to what it was before the C5 row):
#    A1_CHIP_WIFI_SDK_INCLUDE_DIRS
#                           the SDK include directories, in the dev
#                           incflags order of the chip
#    A1_CHIP_WIFI_CONFIG_DIRS
#                           the chip's config/<chip> directories (sdkconfig.h
#                           and, C6 only, its hal_stub_include)
#    A1_CHIP_WIFI_COMPILE_OPTIONS
#                           chip-only -D of the dev Wi-Fi block
#    A1_CHIP_WIFI_INTR_CFG / A1_CHIP_WIFI_INTR_SOURCE
#                           the shim's interrupt-line piece (C6: interrupt
#                           matrix, C5: CLIC)
#    A1_CHIP_WIFI_PHY_INIT_DATA / A1_CHIP_WIFI_MODEM_CLOCK_HAL /
#    A1_CHIP_WIFI_EFUSE_HAL  the esp-idf originals of the chip under
#                           wifi/idf_src (D8 / A8), one file each so they
#                           keep their dev position in the source list
#    A1_CHIP_WIFI_ARDUINO_SOURCES
#                           the chip's attachInterrupt / GPIO / LED files
#                           under arduino/
#
if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
    set(A1_CHIP esp32c6)
endif()
#  Defaults of the fields the P4 row added (see the table comment).
set(A1_CHIP_PRC_NUM 1)
set(A1_CHIP_START_FILE "")
set(A1_CHIP_SEAM_EXTRA_SOURCES)
set(A1_CHIP_EXTRA_DEFS)
set(A1_CHIP_CLK_BOOST_DEFINE "")
set(A1_CHIP_CORE_CLK_DEFINE ON)
set(A1_CHIP_NEWLIB_SYSCALLS ON)
set(A1_CHIP_LDFRAG_DIR "")
if(A1_CHIP STREQUAL "esp32c6")
    set(A1_CHIP_TAG C6)
    set(A1_CHIP_TARGET_NAME m5nanoc6_gcc)
    set(A1_CHIP_ARCH_NAME esp32c6)
    set(A1_XIP_LD_NAME esp32c6_xip.ld)
    set(A1_CHIP_ROM_LD esp32c6.rom.ld)
    set(A1_CHIP_USBJTAG_HAL esp32c6_usbjtag_hal.c)
    set(A1_CHIP_SEAM_APPDESC seam_c6_appdesc.c)
    set(A1_CHIP_SEAM_CLK seam_c6_clk.c)
    set(A1_CHIP_SEAM_ENTRY seam_c6_entry.S)
    set(A1_CHIP_PREBUILT_SCRIPT prebuilt_stage_c6.cmake)
    #  80 MHz is what the bootloader leaves; 160 MHz is the ESP-IDF default
    #  and the setting the dev repository validated Wi-Fi with (S1-6).
    set(A1_CHIP_CPU_FREQ_CHOICES 80 160)
    set(A1_CHIP_CPU_FREQ_DEFAULT 160)
    set(A1_CHIP_CPU_FREQ_BOOST 160)
    set(A1_CHIP_WIFI ON)
    #  Which wifi-connect block builds this chip: "native" is the ESP-IDF
    #  blob + PHY + supplicant arrangement of the C6 and C5. The P4's Wi-Fi
    #  is a companion chip over SDIO, which is a different block entirely
    #  ("hosted"), so this is a chip-table row rather than a branch inside
    #  either block.
    set(A1_CHIP_WIFI_KIND native)
    #  The ESP-IDF headers come from the M5Stack core's esp32c6-libs SDK;
    #  the list is the dev esp/boot/build_incflags_esp32c6_espidf.txt with
    #  components/X mapped to include/X (same ESP-IDF v5.5.4). Three of its
    #  directories are not in the SDK: esp_hw_support/port/include and
    #  esp_wifi/wifi_apps/roaming_app/include are not included by any of
    #  these TUs (checked against the dev build's dependency database), and
    #  the lwIP contrib app headers ping.h / tcpecho_raw.h / udpecho_raw.h
    #  that netif_esp32s3.c includes are vendored under
    #  wifi/net/lwip_contrib_include (IMPORT_PROVENANCE.md).
    set(A1_CHIP_WIFI_SDK_INCLUDE_DIRS
        esp_wifi/include
        esp_coex/include
        esp_hw_support/include
        esp_hw_support/port/esp32c6/include
        esp_hw_support/port/esp32c6/private_include
        heap/include
        efuse/include
        efuse/esp32c6/include
        esp_event/include
        log/include
        esp_phy/include
        esp_phy/esp32c6/include
        esp_pm/include
        esp_common/include
        hal/include
        hal/platform_port/include
        hal/esp32c6/include
        esp_rom/include
        esp_rom/esp32c6/include
        esp_rom/esp32c6/include/esp32c6
        soc/include
        soc/esp32c6/include
        soc/esp32c6/register
        esp_system/include
        riscv/include
        esp_timer/include
        esp_security/include
        mbedtls/port/include
        esp_hw_support/include/soc
        esp_hw_support/include/esp_private
        esp_mm/include
        wpa_supplicant/esp_supplicant/include
        wpa_supplicant/include
        mbedtls/mbedtls/include
        lwip/lwip/src/include)
    #  Ordered as in dev's incflags: the C6 config (sdkconfig.h and its
    #  nuttx/config.h; target.cmake adds the same two again later) before
    #  the chip-shared libc/nvs/gpio stubs, which also carry a
    #  nuttx/config.h (same values, different guard). Keeping dev's order
    #  keeps the same file winning.
    set(A1_CHIP_WIFI_CONFIG_DIRS
        "${RUNTIME_ROOT}/config/esp32c6"
        "${RUNTIME_ROOT}/config/esp32c6/hal_stub_include")
    set(A1_CHIP_WIFI_COMPILE_OPTIONS "")
    #  CFG_INT/DEF_INH for the blob's CPU interrupt lines 1..15 through the
    #  interrupt matrix (the S3/LX6 line block of esp_shim.cfg is outside
    #  TOPPERS_ESP32C6).
    set(A1_CHIP_WIFI_INTR_CFG esp_shim_intr_intmtx.cfg)
    set(A1_CHIP_WIFI_INTR_SOURCE esp_shim_intr_intmtx.c)
    #  The chip's esp-idf v5.5.4 originals (the chip efuse_hal.c is renamed
    #  efuse_hal_esp32c6.c because the stage keeps every object in one
    #  directory by basename; the C6 keeps the unsuffixed names of the
    #  other two it was vendored with first).
    set(A1_CHIP_WIFI_PHY_INIT_DATA phy_init_data.c)
    set(A1_CHIP_WIFI_MODEM_CLOCK_HAL modem_clock_hal.c)
    set(A1_CHIP_WIFI_EFUSE_HAL efuse_hal_esp32c6.c)
    #  attachInterrupt on line 19 through the kernel's esp32c6_intmtx_route
    #  (S3-4), pinMode/digitalWrite/digitalRead on hal/gpio_ll.h and
    #  rgbLedWrite on RMT channel 0 (stage 6).
    set(A1_CHIP_WIFI_ARDUINO_SOURCES
        arduino_interrupt.c
        arduino_gpio.c
        arduino_rgb_led.c)
elseif(A1_CHIP STREQUAL "esp32c5")
    set(A1_CHIP_TAG C5)
    set(A1_CHIP_TARGET_NAME m5stampc5_gcc)
    set(A1_CHIP_ARCH_NAME esp32c5)
    set(A1_XIP_LD_NAME esp32c5_xip.ld)
    set(A1_CHIP_ROM_LD esp32c5.rom.ld)
    set(A1_CHIP_USBJTAG_HAL esp32c5_usbjtag_hal.c)
    set(A1_CHIP_SEAM_APPDESC seam_c5_appdesc.c)
    set(A1_CHIP_SEAM_CLK seam_c5_clk.c)
    set(A1_CHIP_SEAM_ENTRY seam_c5_entry.S)
    set(A1_CHIP_PREBUILT_SCRIPT prebuilt_stage_c5.cmake)
    #  80 MHz is what the bootloader leaves (PLL_F240M / 3); 240 MHz is the
    #  M5Stack core's f_cpu for the board and the condition the dev
    #  repository measured Wi-Fi and SIL_DLY under (C5 plan A6, dev D5).
    #  There is no 160: the C5 keeps the PLL_F240M source and only changes
    #  the divider (seam/seam_c5_clk.c).
    set(A1_CHIP_CPU_FREQ_CHOICES 80 240)
    set(A1_CHIP_CPU_FREQ_DEFAULT 240)
    set(A1_CHIP_CPU_FREQ_BOOST 240)
    #  wifi-connect for the C5 (C5 plan stage 3): the dev
    #  cmake/a1_c5_stage1.cmake A1_C5_WIFI block with the C6 mapping above.
    set(A1_CHIP_WIFI ON)
    set(A1_CHIP_WIFI_KIND native)
    #  dev esp/boot/build_incflags_esp32c5_espidf.txt, components/X mapped
    #  to include/X of the M5Stack core's esp32c5-libs SDK (same ESP-IDF
    #  v5.5.4; the two directories the SDK lacks are the same two as for
    #  the C6 and are left out for the same reason). The order differs from
    #  the C6 row in one place, as it does in dev: hal/esp32c5/include
    #  comes before hal/include, because hal/efuse_hal.h reaches the chip's
    #  hal/esp32c5/include/hal/efuse_hal.h with #include_next and the
    #  chip directory has to be searched first for that to resolve (dev
    #  stage 4 Task 3, otherwise efuse_hal_read is an implicit declaration).
    set(A1_CHIP_WIFI_SDK_INCLUDE_DIRS
        esp_wifi/include
        esp_coex/include
        esp_hw_support/include
        esp_hw_support/port/esp32c5/include
        esp_hw_support/port/esp32c5/private_include
        heap/include
        efuse/include
        efuse/esp32c5/include
        esp_event/include
        log/include
        esp_phy/include
        esp_phy/esp32c5/include
        esp_pm/include
        esp_common/include
        hal/esp32c5/include
        hal/include
        hal/platform_port/include
        esp_rom/include
        esp_rom/esp32c5/include
        esp_rom/esp32c5/include/esp32c5
        soc/include
        soc/esp32c5/include
        soc/esp32c5/register
        esp_system/include
        riscv/include
        esp_timer/include
        esp_security/include
        mbedtls/port/include
        esp_hw_support/include/soc
        esp_hw_support/include/esp_private
        esp_mm/include
        wpa_supplicant/esp_supplicant/include
        wpa_supplicant/include
        mbedtls/mbedtls/include
        lwip/lwip/src/include)
    #  The C5 has no chip hal_stub_include (config/esp32c5/sdkconfig.h is
    #  the asp3 stub's copy and does not read nuttx/config.h); dev puts the
    #  chip-shared stubs (wifi/config/hal_stub_include here) alone after
    #  it, which the wifi-connect block does for every chip.
    set(A1_CHIP_WIFI_CONFIG_DIRS
        "${RUNTIME_ROOT}/config/esp32c5")
    #  dev keeps this one of the incflags -D (TOPPERS_ESP32C5 is dropped as
    #  TOPPERS_ESP32C6 is for the C6: chip_stddef.h defines it): the
    #  esp32c5_eco3_rom_ptr_init() strong empty implementation in
    #  wifi/hal_src/wifi_init.c and the C5 branch of wifi_os_adapter.h read
    #  it, and config/esp32c5/sdkconfig.h does not define it.
    set(A1_CHIP_WIFI_COMPILE_OPTIONS -DCONFIG_IDF_TARGET_ESP32C5=1)
    #  CFG_INT/DEF_INH for the blob's intr_num 1..15 on CLIC lines 25..39
    #  (the S3/LX6 line block of esp_shim.cfg is outside TOPPERS_ESP32C5 too).
    set(A1_CHIP_WIFI_INTR_CFG esp_shim_intr_c5.cfg)
    set(A1_CHIP_WIFI_INTR_SOURCE esp_shim_intr_c5.c)
    #  The chip's esp-idf v5.5.4 originals: esp_phy/esp32c5/phy_init_data.c
    #  (the 2.4/5 GHz data; dev checks its bytes against the original, AC-3i
    #  of dev stage 4), hal/esp32c5/modem_clock_hal.c, hal/esp32c5/
    #  efuse_hal.c, each suffixed _esp32c5 beside the C6 files of the same
    #  basename.
    set(A1_CHIP_WIFI_PHY_INIT_DATA phy_init_data_esp32c5.c)
    set(A1_CHIP_WIFI_MODEM_CLOCK_HAL modem_clock_hal_esp32c5.c)
    set(A1_CHIP_WIFI_EFUSE_HAL efuse_hal_esp32c5.c)
    #  attachInterrupt on CLIC line 23 through the kernel's
    #  esp32c5_intmtx_route and pinMode/digitalWrite/digitalRead on the
    #  esp32c5 hal/gpio_ll.h (C5 plan A3). No RGB LED file: the M5Stamp-C5
    #  has a plain blue LED on G28 (BOOT), not a WS2812.
    set(A1_CHIP_WIFI_ARDUINO_SOURCES
        arduino_interrupt_c5.c
        arduino_gpio_c5.c)
elseif(A1_CHIP STREQUAL "esp32p4")
    #  ESP32-P4 (RISC-V RV32IMAFC, dual core, CLIC), M5Stamp-P4. The chip,
    #  target, linker-script and seam files are the dev repository's
    #  fmp3/arch/riscv_gcc/esp32p4, fmp3/target/m5stamp_esp32p4_gcc and
    #  esp/boot/seam_p4_* as they are (StampP4 plan, stage A1; the dev
    #  seam-p4-smp preset booted on the M5Stamp ESP32P4 rev v1.3). Unlike
    #  the C6 / C5 layers the P4 chip layer reads no ESP-IDF header and
    #  links no ROM linker script: esp32p4.h carries the registers it
    #  needs. The SDK (esp32p4_es-libs, the pre-v3-silicon variant the
    #  board's default ChipVariant selects) is still required: the
    #  bootloader the board is flashed with comes from it, and its
    #  esp32p4.rom.ld is the existence proof below.
    set(A1_CHIP_TAG P4)
    set(A1_CHIP_TARGET_NAME m5stamp_esp32p4_gcc)
    set(A1_CHIP_ARCH_NAME esp32p4)
    set(A1_XIP_LD_NAME esp32p4_xip.ld)
    set(A1_CHIP_ROM_LD esp32p4.rom.ld)
    set(A1_CHIP_USBJTAG_HAL "")
    set(A1_CHIP_SEAM_APPDESC seam_p4_appdesc.c)
    set(A1_CHIP_SEAM_CLK seam_p4_clk.c)
    set(A1_CHIP_SEAM_ENTRY seam_p4_entry.S)
    set(A1_CHIP_PREBUILT_SCRIPT prebuilt_stage_p4.cmake)
    #  360 MHz only: the value the dev seam runs at (seam_p4_clk.c raises
    #  the bootloader's 90 MHz to 360; the kernel's MTIMER_FREQ_MHZ in
    #  esp32p4.h is 360 and the kit header's CORE_CLK_MHZ too). No 90 MHz
    #  fallback: dev measured the kernel's time base 4x slow there.
    set(A1_CHIP_CPU_FREQ_CHOICES 360)
    set(A1_CHIP_CPU_FREQ_DEFAULT 360)
    set(A1_CHIP_CPU_FREQ_BOOST 360)
    set(A1_CHIP_CLK_BOOST_DEFINE SEAM_P4_CLK360=1)
    set(A1_CHIP_CORE_CLK_DEFINE OFF)
    set(A1_CHIP_NEWLIB_SYSCALLS OFF)
    set(A1_CHIP_START_FILE "${FMP3_CORE_ROOT}/arch/riscv_gcc/common/start.S")
    set(A1_CHIP_LDFRAG_DIR "${RUNTIME_ROOT}/target/m5stamp_esp32p4_gcc")
    #  SMP: 2 cores (StampP4 plan P4). core1 (hart1 / PRC2) is woken by the
    #  target's target_mprc_initialize when TOPPERS_SEAM_P4_CORE1 is
    #  defined (dev stage 6); seam_p4_core1_entry.S is what it runs first,
    #  and SEAM_P4_CORE1_MARK (0x43 'C') is one raw byte it writes to the
    #  USB-Serial/JTAG FIFO before entering the kernel - the same kind of
    #  evidence as the seam entry's 'S'.
    set(A1_CHIP_PRC_NUM 2)
    set(A1_CHIP_SEAM_EXTRA_SOURCES seam_p4_core1.c seam_p4_core1_entry.S)
    set(A1_CHIP_EXTRA_DEFS TOPPERS_SEAM_P4_CORE1=1 SEAM_P4_CORE1_MARK=0x43)
    #  wifi-connect for the P4 is stage B of the plan: hosted Wi-Fi, where
    #  the radio is a companion ESP32-C6 on the Stamp add-on reached over
    #  SDIO (esp_hosted). None of the native block applies - there is no
    #  blob to link, no PHY to initialise and no supplicant - so the chip
    #  names the other block and the native one is left untouched.
    set(A1_CHIP_WIFI ON)
    set(A1_CHIP_WIFI_KIND hosted)
    #  Components of the M5Stack core's esp32p4_es-libs SDK the hosted
    #  layers compile against, mapped from the development build's -I list
    #  the way the C6 and C5 rows map theirs. lwip/lwip/src/include stands
    #  for the development tree's esp-idf/components/lwip/lwip/src/include.
    #  This list is the starting point of stage B1c and grows only when a
    #  #include fails to resolve - never speculatively.
    set(A1_CHIP_WIFI_SDK_INCLUDE_DIRS
        esp_common/include
        esp_hw_support/include
        esp_hw_support/include/soc
        esp_hw_support/include/esp_private
        esp_hw_support/port/esp32p4/include
        esp_rom/include
        esp_rom/esp32p4/include
        #  <esp32p4/rom/efuse.h> is reached as "rom/efuse.h" by the shim's
        #  GPIO file, so the chip directory itself is on the path too (the
        #  C6 and C5 rows carry the same pair for the same reason).
        esp_rom/esp32p4/include/esp32p4
        #  driver/gpio.h: the shim's p4shim_gpio.c implements a handful of
        #  the legacy GPIO driver's entry points, so it includes that
        #  driver's header for the prototypes and enums.
        esp_driver_gpio/include
        hal/esp32p4/include
        hal/include
        hal/platform_port/include
        heap/include
        log/include
        riscv/include
        sdmmc/include
        soc/include
        soc/esp32p4/include
        soc/esp32p4/register
        #  The register headers that differ between the two silicon
        #  generations live in hw_ver1/ (rev < v3) and hw_ver3/ next to each
        #  other, and only one of the two may be on the path: this port pins
        #  the pre-v3 silicon (the same decision as the esp32p4_es-libs SDK
        #  directory itself), so hw_ver1. soc/sdmmc_struct.h - the SDIO
        #  host's register block - is one of them.
        soc/esp32p4/register/hw_ver1
        esp_system/include
        esp_timer/include
        esp_mm/include
        lwip/lwip/src/include)
    set(A1_CHIP_WIFI_CONFIG_DIRS "")
    set(A1_CHIP_WIFI_COMPILE_OPTIONS "")
else()
    message(FATAL_ERROR
        "ports/m5stack_riscv/runtime builds the esp32c6, the esp32c5 or the esp32p4 (got A1_CHIP='${A1_CHIP}')")
endif()
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND NOT A1_CHIP_WIFI)
    message(FATAL_ERROR
        "the ${A1_CHIP} port cannot stage wifi-connect")
endif()
string(TOLOWER "${A1_CHIP_TAG}" A1_CHIP_TAG_LOWER)

#
#  ROM linker scripts the sketch link applies, relative to the SDK's ld/
#  directory (the driver adds -L there), chosen per profile.
#
#  minimal links exactly the two the dev repository's seam-c6-min links
#  (target/m5nanoc6_gcc/target.cmake): esp32c6.rom.ld (the ROM's exported
#  data/functions the chip layer needs) and esp32c6.rom.api.ld (PROVIDE()d
#  esp_rom_* API). Nothing else, on purpose. The other eleven scripts the
#  dev Wi-Fi build adds (a1_c6_stage1.cmake: rom.libc, rom.libgcc,
#  rom.newlib, rom.libc-suboptimal_for_misaligned_mem, rom.version,
#  riscv rom.api, rom.net80211, rom.pp, rom.phy, rom.systimer, rom.coexist)
#  bind libc/newlib functions to ROM addresses with plain assignments, and
#  under the driver's -Wl,--allow-multiple-definition a plain assignment
#  wins over the toolchain's libc_nano.a silently. The ROM's newlib
#  (atoi, rand, strtol, printf, malloc, ...) reaches the outside world only
#  through syscall_table_ptr and _global_impure_ptr, ROM data that the
#  minimal stage never initializes (no heap, no _sbrk, no reent for the
#  ROM); a sketch that called such a function would link cleanly and fault
#  on a NULL table at run time. With the two scripts a sketch either takes
#  the function from libc_nano.a (newlib objects, honest about what they
#  need, e.g. _sbrk) or fails to link with an undefined reference - never a
#  silent ROM win. Measured with a probe sketch; the results are in
#  docs/c6-port.md (stage 1 record, ROM linker script winners).
#
#  wifi-connect links the thirteen the dev Wi-Fi build links (the two above
#  plus the eleven of a1_c6_stage1.cmake, in the same order): the Wi-Fi
#  blobs and the supplicant reference ROM libc/newlib/libgcc entry points
#  as well as the ROM-resident parts of net80211/pp/phy/systimer/coexist,
#  and the shim's esp_shim_libc.c supplies the heap (malloc/free) and the
#  printf family for what the ROM does not. Which ROM assignment wins over
#  which staged definition (rand, memcpy, ...) is recorded at link time
#  with nm (stage 3 Task 2; docs/c6-port.md). Every name is checked to
#  exist in the SDK at configure time, and the list goes into the manifest
#  as romLinkerScripts, so the driver links the same set for every sketch.
#
#  The C5 minimal set is the same two by name stem (dev seam-c5-min,
#  target/m5stampc5_gcc/target.cmake). Its wifi-connect set is the same
#  thirteen by name stem (dev a1_c5_stage1.cmake A1_C5_ROM_LD_WIFI_LIST 11
#  + target.cmake 2, in the dev order), with the same libc-supply decision
#  as the C6 (esp_shim_libc.c supplies heap and printf; the ROM's rand
#  wins over it, recorded in docs/c5-port.md stage 3). No
#  esp32c5.rom.eco3.ld: dev D14 - the M5Stamp-C5 is chip rev v1.0 (100),
#  ESP-IDF links eco3 only for CONFIG_ESP32C5_REV_MIN_FULL >= 102, and the
#  asp3 port that linked it hit a store fault when the ROM's PHY functions
#  replaced the blob's RAM versions.
#
if(A1_CHIP STREQUAL "esp32c6")
    set(A1_ROM_LDS_minimal
        esp32c6.rom.ld
        esp32c6.rom.api.ld)
    set(A1_ROM_LDS_wifi-connect
        esp32c6.rom.ld
        esp32c6.rom.api.ld
        esp32c6.rom.libc.ld
        esp32c6.rom.libgcc.ld
        esp32c6.rom.newlib.ld
        esp32c6.rom.libc-suboptimal_for_misaligned_mem.ld
        esp32c6.rom.version.ld
        rom.api.ld
        esp32c6.rom.net80211.ld
        esp32c6.rom.pp.ld
        esp32c6.rom.phy.ld
        esp32c6.rom.systimer.ld
        esp32c6.rom.coexist.ld)
elseif(A1_CHIP STREQUAL "esp32c5")
    set(A1_ROM_LDS_minimal
        esp32c5.rom.ld
        esp32c5.rom.api.ld)
    set(A1_ROM_LDS_wifi-connect
        esp32c5.rom.ld
        esp32c5.rom.api.ld
        esp32c5.rom.libc.ld
        esp32c5.rom.libgcc.ld
        esp32c5.rom.newlib.ld
        esp32c5.rom.libc-suboptimal_for_misaligned_mem.ld
        esp32c5.rom.version.ld
        rom.api.ld
        esp32c5.rom.net80211.ld
        esp32c5.rom.pp.ld
        esp32c5.rom.phy.ld
        esp32c5.rom.systimer.ld
        esp32c5.rom.coexist.ld)
elseif(A1_CHIP STREQUAL "esp32p4")
    #  None: the dev seam-p4-smp links no ROM script (the P4 chip layer is
    #  self-contained, and the sketch side takes libc from libc_nano.a - the
    #  minimal-profile reasoning above for the C6 applies with nothing to
    #  bind). The empty list is legal for prebuilt_stage_p4.cmake, which
    #  writes romLinkerScripts as [] for the driver. (set() with no value
    #  unsets the variable, and the check below wants it DEFINED; the empty
    #  string is a defined, empty list.)
    set(A1_ROM_LDS_minimal "")
    #  wifi-connect (hosted): the SDIO host and the shim call ROM functions
    #  (ets_delay_us, the GPIO matrix helpers, esp_rom_* aliases), which the
    #  minimal profile does not. Two scripts, the pair the development build
    #  links for this chip; the peripherals script is not a ROM script and
    #  goes through EXTRA_TSCRIPTS below.
    set(A1_ROM_LDS_wifi-connect
        esp32p4.rom.ld
        esp32p4.rom.api.ld)
endif()
if(NOT DEFINED A1_ROM_LDS_${FMP3_RUNTIME_PROFILE})
    message(FATAL_ERROR
        "no ROM linker script list for profile '${FMP3_RUNTIME_PROFILE}' "
        "(A1_ROM_LDS_${FMP3_RUNTIME_PROFILE} is not set)")
endif()
set(A1_ROM_LDS ${A1_ROM_LDS_${FMP3_RUNTIME_PROFILE}})

foreach(required_path
        "${FMP3_CORE_ROOT}/CMakeLists.txt"
        "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.c"
        "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.cfg"
        "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ROM_LD}"
        "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ARCH_NAME}.peripherals.ld"
        "${ARDUINO_SDK_INCLUDE_ROOT}/riscv/include/riscv/csr.h"
        "${A1_ESPTOOL_EXECUTABLE}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Required file was not found: ${required_path}")
    endif()
endforeach()
foreach(rom_ld IN LISTS A1_ROM_LDS)
    if(NOT EXISTS "${ARDUINO_SDK_LD_ROOT}/${rom_ld}")
        message(FATAL_ERROR
            "ROM linker script is missing from the SDK: ${ARDUINO_SDK_LD_ROOT}/${rom_ld}")
    endif()
endforeach()

#
#  CPU clock. The image is entered at 80 MHz (the ESP-IDF 2nd-stage
#  bootloader leaves the PCR at 480/3/2 on the C6, PLL_F240M/3 on the C5) and
#  seam_<chip>_entry.S raises it to A1_CHIP_CPU_FREQ_BOOST before
#  toppers_start when SEAM_<TAG>_CLK_BOOST is defined (seam/seam_<chip>_clk.c).
#  C6: 160 MHz is the ESP-IDF default and the setting the dev repository
#  validated Wi-Fi with, so it is the default (S1-6). C5: 240 MHz (A6).
#  CORE_CLK_MHZ is the value the kernel's timing constants are calibrated
#  for and has to say the same thing. The cache variable keeps its chip name
#  (A1_C6_CPU_FREQ_MHZ / A1_C5_CPU_FREQ_MHZ) so the documented
#  --cmake-define fallback (docs/c6-port.md, 80 MHz) is unchanged.
#
set(A1_CPU_FREQ_VAR A1_${A1_CHIP_TAG}_CPU_FREQ_MHZ)
string(REPLACE ";" " or " _a1_cpu_freq_choices_text "${A1_CHIP_CPU_FREQ_CHOICES}")
set(${A1_CPU_FREQ_VAR} "${A1_CHIP_CPU_FREQ_DEFAULT}" CACHE STRING
    "${A1_CHIP} CPU frequency (${_a1_cpu_freq_choices_text} MHz)")
set(A1_CPU_FREQ_MHZ "${${A1_CPU_FREQ_VAR}}")
if(NOT A1_CPU_FREQ_MHZ IN_LIST A1_CHIP_CPU_FREQ_CHOICES)
    message(FATAL_ERROR
        "${A1_CPU_FREQ_VAR} must be ${_a1_cpu_freq_choices_text} (got '${A1_CPU_FREQ_MHZ}')")
endif()
#  One byte written raw to the USB-Serial/JTAG FIFO at the image entry, before
#  anything of FMP3 runs; tells "the bootloader jumped" from "FMP3 is silent".
#  Empty removes the code. Same default as the dev seam builds.
set(A1_SEAM_ENTRY_MARK_VAR A1_${A1_CHIP_TAG}_SEAM_ENTRY_MARK)
set(${A1_SEAM_ENTRY_MARK_VAR} "0x53" CACHE STRING
    "ASCII code emitted to USB-Serial/JTAG at the seam entry (empty = none)")
set(A1_SEAM_ENTRY_MARK "${${A1_SEAM_ENTRY_MARK_VAR}}")

#
#  Definitions for the kernel and every runtime object. TOPPERS_OMIT_TECS,
#  TOPPERS_SUPPORT_TLS and TOPPERS_ESP32C6_CONSOLE_USBJTAG (C5:
#  TOPPERS_ESP32C5_CONSOLE_USBJTAG, USE_RISCV_DIRECT_TRAP) are added by
#  target.cmake / chip.cmake; -march and friends by chip.cmake. All of them
#  reach the runtime objects through the fmp3 target's PUBLIC properties (see
#  seam_common below), so the kernel and the runtime cannot drift apart.
#
set(FMP3_COMPILE_DEFS)
if(A1_CHIP_CORE_CLK_DEFINE)
    list(APPEND FMP3_COMPILE_DEFS CORE_CLK_MHZ=${A1_CPU_FREQ_MHZ})
endif()
if(A1_CPU_FREQ_MHZ STREQUAL "${A1_CHIP_CPU_FREQ_BOOST}")
    if(A1_CHIP_CLK_BOOST_DEFINE STREQUAL "")
        list(APPEND FMP3_COMPILE_DEFS SEAM_${A1_CHIP_TAG}_CLK_BOOST=1)
    else()
        list(APPEND FMP3_COMPILE_DEFS ${A1_CHIP_CLK_BOOST_DEFINE})
    endif()
endif()
list(APPEND FMP3_COMPILE_DEFS ${A1_CHIP_EXTRA_DEFS})
if(NOT A1_CHIP_LDFRAG_DIR STREQUAL "")
    #  The XIP linker script's INCLUDE fragments (P4), for the cfg pass-1
    #  link; the staged copy is self-contained (prebuilt_stage_p4.cmake).
    list(APPEND FMP3_LINK_OPTIONS "-Wl,-L,${A1_CHIP_LDFRAG_DIR}")
endif()
if(NOT A1_SEAM_ENTRY_MARK STREQUAL "")
    list(APPEND FMP3_COMPILE_DEFS SEAM_${A1_CHIP_TAG}_ENTRY_MARK=${A1_SEAM_ENTRY_MARK})
endif()
set(FMP3_INCLUDE_DIRS
    "${FMP3_APPLICATION_DIR}"
    #  seam_<chip>_clk.h for anything that wants the boost result.
    "${RUNTIME_ROOT}/seam")

#
#  wifi-connect: what the dev repository's cmake/a1_c6_stage1.cmake
#  (esp32c6) / cmake/a1_c5_stage1.cmake (esp32c5) adds before
#  add_subdirectory(fmp3_core) when A1_<TAG>_WIFI is ON. As there, the
#  include directories, definitions and the interrupt-line cfg go through the
#  fmp3 target's PUBLIC properties to every object (kernel, syssvc, shim), so
#  the stage is built the way the dev Wi-Fi image was: the shim's libc stub
#  headers (wifi/config/hal_stub_include) stand in front of newlib's for
#  every TU, and TCNT_SYSLOG_BUFFER reaches syslog.c. The chip-named inputs
#  (SDK include list and its order, config directories, chip -D, the
#  interrupt-line cfg) are the chip table's A1_CHIP_WIFI_* rows above.
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "native")
    set(WIFI_ROOT "${RUNTIME_ROOT}/wifi")
    set(WIFI_INCLUDE_DIRS)
    foreach(dir IN LISTS A1_CHIP_WIFI_SDK_INCLUDE_DIRS)
        if(NOT IS_DIRECTORY "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
            message(FATAL_ERROR
                "wifi-connect: SDK include directory is missing: "
                "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
        endif()
        list(APPEND WIFI_INCLUDE_DIRS "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
    endforeach()
    list(APPEND WIFI_INCLUDE_DIRS
        #  The chip's config (see the chip table for the order argument)
        #  before the chip-shared libc/nvs/gpio stubs.
        ${A1_CHIP_WIFI_CONFIG_DIRS}
        "${WIFI_ROOT}/config/hal_stub_include"
        "${WIFI_ROOT}/net"
        "${WIFI_ROOT}/net/port/include"
        "${WIFI_ROOT}/net/lwip_contrib_include"
        #  freertos/FreeRTOS.h etc. for the esp-idf originals (modem_clock.c
        #  uses portENTER_CRITICAL_SAFE) and the hal sources.
        "${WIFI_ROOT}/freertos_stub"
        #  cfg INCLUDE("esp_shim.cfg") / INCLUDE("net.cfg") resolve here too.
        "${WIFI_ROOT}/shim"
        "${WIFI_ROOT}/hal_src"
        "${WIFI_ROOT}/net/port"
        #  The Arduino-facing adapter (toppers_wifi_core.h) and the
        #  attachInterrupt cfg piece with its constants header
        #  (arduino_interrupt.cfg / .h, C5: arduino_interrupt_c5.cfg / .h;
        #  cfg INCLUDE resolves here).
        "${WIFI_ROOT}/adapter"
        "${RUNTIME_ROOT}/arduino")
    list(APPEND FMP3_INCLUDE_DIRS ${WIFI_INCLUDE_DIRS})

    #  From build_incflags_esp32c<N>_espidf.txt (TOPPERS_ESP32C<N> dropped:
    #  chip_stddef.h defines it) and the additions of the dev Wi-Fi block,
    #  verbatim; the chip-only -D come from the chip table. The ones only
    #  the dev demo application wifi_sta.c reads (A1_C<N>_WIFI_SCAN,
    #  TOPPERS_APP_HEAP_REPORT, A1_C6_WIFI_BOOT_DELAY_MS, A1_C6_USJ_REARM_PROBE)
    #  and the reset-surviving boot trace (A1_C6_BOOT_TRACE) are not passed.
    list(APPEND FMP3_COMPILE_OPTIONS
        -DESP_PLATFORM
        ${A1_CHIP_WIFI_COMPILE_OPTIONS}
        -DCONFIG_ESP_PHY_ENABLED=1
        -Wno-error=implicit-function-declaration
        -DTOPPERS_ESP_WIFI_WPA2
        -DTOPPERS_BOOT_SEAM
        -DTOPPERS_ESPIDF_SUPPLY
        -DTOPPERS_ESPIDF_WIFI_BLOB
        -Dshared_periph_module_t=periph_module_t
        -Dwifi_nan_sync_config_t=wifi_nan_config_t
        #  PLL temperature tracking off (dev: the asp3 C6 record that reached
        #  scan/DHCP/ping had it off; phy_common.c / phy_init.c read it).
        -DCONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
        #  esp_wifi_init logs more than the 32-entry default ring holds.
        -DTCNT_SYSLOG_BUFFER=256
        #  Shim heap high-water counters (esp_shim.c / esp_shim_tsk.c).
        -DESP_SHIM_HEAP_STATS)

    #  APM access-path filters. The ESP-IDF application startup disables them
    #  (bootloader_init_mem); this image links no cpu_start, so the shim's
    #  adapter does it before the modem is enabled (esp_wifi_adapter.c,
    #  c6_apm_unblock; C5: esp_wifi_adapter_c5.inc, c5_apm_unblock, which
    #  clears FUNC_CTRL of the four controllers HP_APM / LP_APM0 / LP_APM /
    #  CPU_APM and sets the 32 TEE masters to TEE mode - dev A1_C5_APM_UNBLOCK
    #  with A1_C5_APM_FUNC_CTRL and A1_C5_APM_TEE both ON, the ESP-IDF
    #  equivalent; dev measured that either half alone is enough, and ships
    #  both). Without it the modem DMA cannot reach RAM and a scan finds 0
    #  APs (dev measured both arms on both chips). OFF is the control for
    #  stage 4. The option keeps the chip's name (TOPPERS_C6_APM_UNBLOCK /
    #  TOPPERS_C5_APM_UNBLOCK) so the documented --cmake-define control
    #  builds read as written.
    option(TOPPERS_${A1_CHIP_TAG}_APM_UNBLOCK
        "wifi-connect: disable the APM access-path filters before the modem is enabled (default ON)"
        ON)
    if(TOPPERS_${A1_CHIP_TAG}_APM_UNBLOCK)
        list(APPEND FMP3_COMPILE_OPTIONS -DA1_${A1_CHIP_TAG}_APM_UNBLOCK=1)
        if(A1_CHIP STREQUAL "esp32c5")
            list(APPEND FMP3_COMPILE_OPTIONS -DA1_C5_APM_FUNC_CTRL=1 -DA1_C5_APM_TEE=1)
        endif()
    endif()
    #  Verbose per-event readback / NOTICE lines of the shim (dev
    #  A1_C6_WIFI_DIAG / A1_C5_WIFI_DIAG). The one-shot detectors stay on
    #  regardless.
    option(TOPPERS_${A1_CHIP_TAG}_WIFI_DIAG
        "wifi-connect: verbose shim diagnostics on the console (default OFF)"
        OFF)
    if(TOPPERS_${A1_CHIP_TAG}_WIFI_DIAG)
        list(APPEND FMP3_COMPILE_OPTIONS -DA1_${A1_CHIP_TAG}_WIFI_DIAG=1)
    endif()
    #  The dev demo's network diagnostics in the vendored wifi/net/
    #  netif_esp32s3.c: the gateway ping chain started on DHCP bound, the
    #  TCP/UDP echo servers on port 7, and the ip=/gw= text of the
    #  "net: DHCP bound" line (stage 5 decision S5-3; the vendored file
    #  carries the #if blocks, recorded as an R12 exception in
    #  IMPORT_PROVENANCE.md). Stage 4's hardware record was taken with them
    #  ON; the shipped runtime must not listen or ping unasked, so OFF.
    #  OFF also leaves the ping's sys_timeout slot unused (lwipopts.h
    #  MEMP_NUM_SYS_TIMEOUT margin 1 -> 2; the pool size does not change).
    #  The file reads TOPPERS_C6_NET_DIAG; for the C5 it aliases
    #  TOPPERS_C5_NET_DIAG onto it (netif_esp32s3.c, C5 plan A11).
    option(TOPPERS_${A1_CHIP_TAG}_NET_DIAG
        "wifi-connect: dev network diagnostics (gateway ping, port-7 echo servers, ip=/gw= line) (default OFF)"
        OFF)
    if(TOPPERS_${A1_CHIP_TAG}_NET_DIAG)
        list(APPEND FMP3_COMPILE_OPTIONS -DTOPPERS_${A1_CHIP_TAG}_NET_DIAG=1)
    endif()

    #  CFG_INT/DEF_INH for the blob's interrupt lines (chip table). Before
    #  the application cfg, as in dev.
    list(APPEND FMP3_CFG_FILES "${WIFI_ROOT}/shim/${A1_CHIP_WIFI_INTR_CFG}")

    #  The eleven ROM scripts beyond target.cmake's two, for the cfg pass-1
    #  link (the sketch link takes them from the manifest, see A1_ROM_LDS).
    foreach(rom_ld IN LISTS A1_ROM_LDS)
        if(NOT rom_ld IN_LIST A1_ROM_LDS_minimal)
            list(APPEND FMP3_LINK_OPTIONS "-Wl,-T,${ARDUINO_SDK_LD_ROOT}/${rom_ld}")
        endif()
    endforeach()
endif()

#
#  wifi-connect, hosted (ESP32-P4). The radio is a companion ESP32-C6 on the
#  Stamp add-on, reached over SDIO with the esp_hosted protocol, so this
#  block shares nothing with the native one above: no blob, no PHY, no
#  supplicant, no esp_wifi headers. What it does need is the SDIO host, the
#  esp_hosted OS abstraction and transport, the 802.3 data path, lwIP with
#  its own port, and the small ESP-IDF system layer (p4shim) the three of
#  them call. Every source is vendored under hosted/ (IMPORT_PROVENANCE_p4.md).
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "hosted")
    set(HOSTED_ROOT "${RUNTIME_ROOT}/hosted")
    set(HOSTED_INCLUDE_DIRS)
    foreach(dir IN LISTS A1_CHIP_WIFI_SDK_INCLUDE_DIRS)
        if(NOT IS_DIRECTORY "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
            message(FATAL_ERROR
                "wifi-connect (hosted): SDK include directory is missing: "
                "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
        endif()
        list(APPEND HOSTED_INCLUDE_DIRS "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
    endforeach()
    list(APPEND HOSTED_INCLUDE_DIRS
        #  The hosted layers themselves. The order is the development
        #  build's: the port's own lwipopts.h has to be found before the
        #  lwIP tree's default one, which is why the two lwipopts
        #  directories come before lwip_port/include.
        #  hosted/ itself: the vendored sources include their generated
        #  headers as "generated/hosted_rpc_ids.h", so the parent is what
        #  goes on the path.
        "${HOSTED_ROOT}"
        "${HOSTED_ROOT}/sdio"
        "${HOSTED_ROOT}/osi"
        "${HOSTED_ROOT}/rpc"
        "${HOSTED_ROOT}/upstream"
        "${HOSTED_ROOT}/generated"
        "${HOSTED_ROOT}/net"
        "${HOSTED_ROOT}/net/lwipopts_dns"
        #  The lwIP port keeps the development tree's own place
        #  (runtime/eth/lwip_port), because the vendored
        #  hosted/net/lwipopts_dns/lwipopts.h reaches it with a relative
        #  include (../../../eth/lwip_port/include/lwipopts.h) and that file
        #  is carried unchanged.
        "${RUNTIME_ROOT}/eth/lwip_port"
        "${RUNTIME_ROOT}/eth/lwip_port/include"
        "${HOSTED_ROOT}/p4shim"
        #  sdkconfig.h. The M5Stack SDK ships one per flash/PSRAM memory
        #  type beside include/; this board's is qio_qspi (boards.txt
        #  m5stack_stamp_p4: build.boot=qio, build.psram_type=qspi). The
        #  ESP-IDF headers this profile compiles (hal/assert.h and below)
        #  include it by bare name. The C6 and C5 carry a vendored stub
        #  instead (config/<chip>/sdkconfig.h) because their ports predate
        #  this; nothing here depends on which of the two a chip uses.
        "${ARDUINO_SDK_INCLUDE_ROOT}/../qio_qspi/include"
        #  cfg INCLUDE("esp_shim_intr_clic.cfg") resolves here, and the
        #  CLIC shim's own header lives beside it.
        "${RUNTIME_ROOT}/wifi/shim"
        #  The Arduino-facing adapter and the attachInterrupt cfg piece
        #  (stage B2; the directories exist for the cfg INCLUDE path).
        "${RUNTIME_ROOT}/wifi/adapter"
        "${RUNTIME_ROOT}/arduino")
    list(APPEND FMP3_INCLUDE_DIRS ${HOSTED_INCLUDE_DIRS})

    #  The development build's option chain, each define named as there:
    #  P4SDIO (the SDIO host), P4HOSTED (the esp_hosted OS abstraction and
    #  transport), P4HOSTED_RPC (the RPC layer), P4HOSTED_NET (the 802.3
    #  data path and lwIP), P4HOSTED_DNS (the resolver, which is also why
    #  this port's lwIP archive is the DNS variant).
    list(APPEND FMP3_COMPILE_OPTIONS
        -DESP_PLATFORM
        -DTOPPERS_BOOT_SEAM
        -DTOPPERS_ESPIDF_SUPPLY
        -DP4SDIO=1
        -DP4HOSTED=1
        -DP4HOSTED_RPC=1
        -DP4HOSTED_NET=1
        -DP4HOSTED_DNS=1
        -Wno-error=implicit-function-declaration
        #  The hosted layers log more than the 32-entry default ring holds
        #  (the same reason the native block raises it).
        -DTCNT_SYSLOG_BUFFER=256)

    #  Static objects the cfg pass has to see, in the development build's
    #  order: the CLIC interrupt lines first (the SDIO interrupt is one of
    #  them), then the SDIO host's, the hosted transport's, lwIP's pools and
    #  the shim's diagnostics.
    list(APPEND FMP3_CFG_FILES
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_intr_clic.cfg"
        "${HOSTED_ROOT}/sdio/p4sdio.cfg"
        "${HOSTED_ROOT}/osi/p4hosted.cfg"
        "${RUNTIME_ROOT}/eth/lwip_port/fmp3_lwip_pools.cfg"
        "${HOSTED_ROOT}/p4shim/p4shim_diag.cfg")

    #  The linker-script fragments this profile needs are NOT the target
    #  layer's: the hosted build adds RAM_HIGH (0x4FF40000 + 384 KB) for the
    #  driver's buffers, which the minimal fragments do not declare. Same
    #  file names, different contents - hence a different directory, chosen
    #  here rather than in the chip table (the chip table's value is the
    #  minimal one).
    set(A1_CHIP_LDFRAG_DIR "${HOSTED_ROOT}/ldfrag")

    #  esp32p4.peripherals.ld is not a ROM script (it PROVIDEs the register
    #  block base addresses); it goes to the sketch link through the
    #  manifest's extraLinkerScripts, the way the Xtensa runtime passes it.
    set(A1_EXTRA_TSCRIPTS "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ARCH_NAME}.peripherals.ld")
    list(APPEND FMP3_LINK_OPTIONS "-Wl,-T,${A1_EXTRA_TSCRIPTS}")

    #  The ROM scripts of this profile, for the cfg pass-1 link (the sketch
    #  link takes them from the manifest).
    foreach(rom_ld IN LISTS A1_ROM_LDS)
        list(APPEND FMP3_LINK_OPTIONS "-Wl,-T,${ARDUINO_SDK_LD_ROOT}/${rom_ld}")
    endforeach()
endif()

project(toppers_fmp3_arduino_runtime_${A1_CHIP_TAG_LOWER} C CXX ASM)

set(FMP3_TARGET "${A1_CHIP_TARGET_NAME}")
set(FMP3_TARGET_DIR "${RUNTIME_ROOT}/target/${A1_CHIP_TARGET_NAME}")
set(FMP3_CHIP_DIR "${RUNTIME_ROOT}/arch/riscv_gcc/${A1_CHIP_ARCH_NAME}")
set(A1_XIP_LD "${FMP3_TARGET_DIR}/${A1_XIP_LD_NAME}")
#  The XIP linker script also serves the cfg pass-1 link (cfg1_out), as in
#  the dev seam builds (A1_C6_LDSCRIPT / A1_C5_LDSCRIPT, the target.cmake
#  override); its ENTRY symbol is absent there, which the linker reports as
#  a warning only.
set(A1_${A1_CHIP_TAG}_LDSCRIPT "${A1_XIP_LD}")
set(FMP3_LIBRARY_ONLY ON)
#  C6 / C5: single core (their target_kernel.h refuse any other
#  TNUM_PRCID). P4: 2 (chip table).
set(FMP3_PRC_NUM ${A1_CHIP_PRC_NUM})
set(FMP3_APPLDIR "${FMP3_APPLICATION_DIR}")
set(FMP3_APPLNAME "${FMP3_APPLICATION_NAME}")

add_subdirectory("${FMP3_CORE_ROOT}" fmp3_core)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

#
#  The chip layer's compile options are the single source of truth for the
#  ISA flags, and the manifest's linkBaseFlags below have to agree with them
#  (the sketch link is done by the driver with those flags, not by CMake).
#  Read them back from the fmp3 target and fail if any is missing, so a
#  change in chip.cmake cannot leave the manifest behind.
#
get_target_property(_fmp3_public_options fmp3 INTERFACE_COMPILE_OPTIONS)
set(A1_LINK_ISA_FLAGS
    "-march=${FMP3_RISCV_MARCH}"
    "-mabi=${FMP3_RISCV_MABI}"
    "-mcmodel=medany"
    "--specs=nano.specs")
foreach(flag IN LISTS A1_LINK_ISA_FLAGS)
    if(NOT "${flag}" IN_LIST _fmp3_public_options)
        message(FATAL_ERROR
            "chip.cmake no longer compiles with '${flag}'; update A1_LINK_ISA_FLAGS "
            "(the manifest's linkBaseFlags) to match. fmp3 options: ${_fmp3_public_options}")
    endif()
endforeach()

#
#  Usage requirements shared by every runtime object: the kernel's PUBLIC
#  include directories, definitions and compile options (from fmp3_core,
#  target.cmake and chip.cmake), plus the generated cfg headers.
#
add_library(seam_common INTERFACE)
target_link_libraries(seam_common INTERFACE fmp3)
target_include_directories(seam_common INTERFACE
    "${GENERATED_DIR}")

add_library(seam_objects OBJECT
    "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.c"
    "${FMP3_CORE_ROOT}/syssvc/syslog.c"
    "${FMP3_CORE_ROOT}/syssvc/banner.c"
    "${FMP3_CORE_ROOT}/syssvc/serial.c"
    "${FMP3_CORE_ROOT}/syssvc/serial_cfg.c"
    "${FMP3_CORE_ROOT}/syssvc/logtask.c"
    "${FMP3_CORE_ROOT}/library/log_output.c"
    "${FMP3_CORE_ROOT}/library/vasyslog.c"
    "${FMP3_CORE_ROOT}/library/t_perror.c"
    "${FMP3_CORE_ROOT}/library/strerror.c"
    #  Console: USB-Serial/JTAG (chip.cmake ESP32C6_CONSOLE / ESP32C5_CONSOLE
    #  = usbjtag; neither board has a UART bridge). These two are what
    #  fmp3_add_syssvc() would add from FMP3_SYSSVC_TARGET_C_FILES in a
    #  non-library build.
    "${FMP3_CHIP_DIR}/chip_serial.c"
    #  esp_app_desc at the head of the DROM segment; the bootloader reads it.
    "${RUNTIME_ROOT}/seam/${A1_CHIP_SEAM_APPDESC}"
    #  80 -> 160 (C6) / 240 (C5) MHz, called from the seam entry when
    #  SEAM_<TAG>_CLK_BOOST; P4: 90 -> 360 (SEAM_P4_CLK360).
    "${RUNTIME_ROOT}/seam/${A1_CHIP_SEAM_CLK}")
if(NOT A1_CHIP_USBJTAG_HAL STREQUAL "")
    target_sources(seam_objects PRIVATE
        "${FMP3_TARGET_DIR}/${A1_CHIP_USBJTAG_HAL}")
endif()
foreach(src IN LISTS A1_CHIP_SEAM_EXTRA_SOURCES)
    target_sources(seam_objects PRIVATE "${RUNTIME_ROOT}/seam/${src}")
endforeach()
if(A1_CHIP_NEWLIB_SYSCALLS)
    #  _exit / _kill / _getpid / _write / __getreent for newlib-nano's
    #  __stack_chk_fail path: the M5Stack core builds every sketch with
    #  -fstack-protector, so a sketch with a local char array needs them.
    #  Not for the P4 (chip table): its target layer defines them.
    target_sources(seam_objects PRIVATE
        "${RUNTIME_ROOT}/seam/newlib_syscalls.c")
endif()
target_link_libraries(seam_objects PRIVATE seam_common)
add_dependencies(seam_objects generate_cfg_gen_files)

#
#  Startup: chip_start.S is the chip's replacement for fmp3_core's common
#  start.S (chip.cmake swaps it in FMP3_START_FILES; the common one issues
#  FPU instructions the rv32imac assembler rejects) and defines
#  toppers_start. seam_<chip>_entry.S is the image entry the bootloader jumps
#  to; it ends in toppers_start. C6: ENTRY(seam_c6_entry) in esp32c6_xip.ld.
#  C5: the dev linker script keeps ENTRY(toppers_start) and the seam build
#  passes -e (dev a1_c5_stage1.cmake); here the -e goes into the manifest's
#  linkBaseFlags (see XIP_LINK_BASE_FLAGS below) - seam_c5_entry_boost
#  (clock boost, then seam_c5_entry) at 240 MHz, seam_c5_entry at 80.
#
if(A1_CHIP_START_FILE STREQUAL "")
    set(A1_START_FILE "${FMP3_CHIP_DIR}/chip_start.S")
else()
    set(A1_START_FILE "${A1_CHIP_START_FILE}")
endif()
add_library(seam_start OBJECT
    "${A1_START_FILE}"
    "${RUNTIME_ROOT}/seam/${A1_CHIP_SEAM_ENTRY}")
target_link_libraries(seam_start PRIVATE seam_common)
target_include_directories(seam_start PRIVATE
    "${FMP3_CORE_ROOT}/kernel")
add_dependencies(seam_start generate_cfg_gen_files)

#
#  esp_run_init_array() for src/bridge/ArduinoSketchBridge.cpp. Chip
#  independent; the copy is the Xtensa runtime's seam/init_array.cpp. The
#  symbols it walks are defined by the chip's XIP linker script (S1-5).
#
add_library(seam_cxx OBJECT
    "${RUNTIME_ROOT}/seam/init_array.cpp")
target_link_libraries(seam_cxx PRIVATE seam_common)
target_compile_options(seam_cxx PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -std=gnu++17)
add_dependencies(seam_cxx generate_cfg_gen_files)

#
#  wifi-connect: the sources the dev Wi-Fi build adds to fmp (a1_c6_stage1.cmake
#  / a1_c5_stage1.cmake, target_sources(fmp ...) under A1_C<N>_WIFI), same
#  set, same order. Kept in their own object library so the profile's
#  objects are one list in this file; the compile settings are the fmp3
#  PUBLIC ones (see the wifi-connect block above), so nothing here differs
#  from seam_objects in how it is built. The chip-named entries (the
#  interrupt-line shim, the chip's esp-idf originals, the attachInterrupt /
#  GPIO / LED files) are the chip table's rows.
#
#    wifi/shim      the ESP-IDF OS shim with the C6 and C5 branches (second
#                   copy of the dev esp/shim; the Xtensa port's fork under
#                   ports/m5stack_xtensa is a different base and is not
#                   shared). The C5 branch is the dev c7fef18..1d96bcba
#                   delta applied as is (esp_wifi_adapter_c5.inc /
#                   esp_shim_blobglue_c5.inc / esp_shim_intr_c5.*; the
#                   shared files carry same-line-count #if edits only, so
#                   the C6 objects did not move: X-check of C5 stage 3)
#    wifi/hal_src   PHY / Wi-Fi hal sources (periph_ctrl.c and
#                   phy_init_data.c of the S3 set are not used: the RISC-V
#                   chips take the esp-idf originals)
#    wifi/idf_src   the esp-idf v5.5.4 originals (D8 / A8: vendored, a
#                   recorded deviation from BUILDING.md; the chip files are
#                   renamed with a chip suffix because the stage keeps
#                   every object in one directory by basename - C6:
#                   efuse_hal_esp32c6.c; C5: phy_init_data_esp32c5.c,
#                   modem_clock_hal_esp32c5.c, efuse_hal_esp32c5.c).
#                   Reason: the matching members of the core's
#                   esp32c<N>-libs .a files (libesp_hw_support.a / libhal.a)
#                   require vPortEnterCritical/vPortExitCritical/
#                   xPortInIsrContext (FreeRTOS) as undefined references,
#                   which FMP3 does not supply, so these are compiled from
#                   source against a FreeRTOS stub instead (dev repo
#                   INVESTIGATION.md 2-3). Stage 5 decision S5-2: kept as
#                   vendored copies; stage 4's hardware record is with these
#                   files. Re-evaluate when either the "core's .a members +
#                   vPort* shim" route is actually verified, or the pinned
#                   M5Stack core version moves (BUILDING.md exception clause).
#    wifi/net       lwIP glue of the dev type (D7): netif_esp32s3.c and the
#                   sys_arch port; liblwip.a itself is prebuilt. The three
#                   lwIP contrib headers under net/lwip_contrib_include are
#                   the other half of D8 (S5-2: kept); they are included
#                   only when TOPPERS_C6_NET_DIAG is ON
#    target         diag_recorder.c (diag_event() the shim and the netif
#                   call) and target_hrt64.c (target_hrt_get_current64())
#    wifi/adapter   the Arduino-facing adapter (toppers_fmp3_wifi_* for
#                   src/ToppersFMP3_WiFi.cpp), the RISC-V version of the
#                   Xtensa port's wifi/adapter: one shared bring-up
#                   (toppers_wifi_core.c, dev C6 order, no WPA callback
#                   table and no --wrap=esp_supplicant_init = D6), scan,
#                   connect on the dev netif (D7), and the weak stubs. The
#                   C5 differs from the C6 only in the APM readback it calls
#                   (#if in the adapter, C5 plan stage 3)
#    arduino        attachInterrupt through the kernel's intmtx route
#                   (C6: line 19, C5: CLIC line 23; S3-4 / A3),
#                   pinMode/digitalWrite/digitalRead on hal/gpio_ll.h and,
#                   C6 only, rgbLedWrite on RMT channel 0 (stage 6;
#                   wifi-connect only, minimal has no SDK include path)
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "native")
    set(WIFI_ARDUINO_SOURCES)
    foreach(src IN LISTS A1_CHIP_WIFI_ARDUINO_SOURCES)
        list(APPEND WIFI_ARDUINO_SOURCES "${RUNTIME_ROOT}/arduino/${src}")
    endforeach()
    add_library(wifi_objects OBJECT
        "${WIFI_ROOT}/shim/esp_shim_bt_crit_wifi.c"
        "${WIFI_ROOT}/shim/esp_shim.c"
        "${WIFI_ROOT}/shim/esp_shim_mtx.c"
        "${WIFI_ROOT}/shim/esp_shim_sem.c"
        "${WIFI_ROOT}/shim/esp_shim_tsk.c"
        "${WIFI_ROOT}/shim/esp_wifi_adapter.c"
        "${WIFI_ROOT}/shim/esp_shim_libc.c"
        "${WIFI_ROOT}/shim/esp_shim_blobglue.c"
        "${WIFI_ROOT}/shim/esp_event_shim.c"
        "${WIFI_ROOT}/shim/esp_timer_shim.c"
        "${WIFI_ROOT}/shim/wifi_stubs.c"
        "${WIFI_ROOT}/shim/esp_coex_adapter.c"
        "${WIFI_ROOT}/shim/esp_shim_isr_ctx.c"
        "${WIFI_ROOT}/shim/esp_shim_ring.c"
        "${WIFI_ROOT}/shim/${A1_CHIP_WIFI_INTR_SOURCE}"
        "${WIFI_ROOT}/hal_src/phy_common.c"
        "${WIFI_ROOT}/hal_src/phy_init.c"
        "${WIFI_ROOT}/idf_src/${A1_CHIP_WIFI_PHY_INIT_DATA}"
        "${WIFI_ROOT}/hal_src/esp_wifi_regulatory.c"
        "${WIFI_ROOT}/hal_src/wifi_init.c"
        "${WIFI_ROOT}/hal_src/wifi_lib_printf.c"
        "${WIFI_ROOT}/hal_src/phy_lib_printf.c"
        "${WIFI_ROOT}/idf_src/periph_ctrl.c"
        "${WIFI_ROOT}/idf_src/modem_clock.c"
        "${WIFI_ROOT}/idf_src/${A1_CHIP_WIFI_MODEM_CLOCK_HAL}"
        "${WIFI_ROOT}/idf_src/efuse_hal.c"
        "${WIFI_ROOT}/idf_src/${A1_CHIP_WIFI_EFUSE_HAL}"
        "${WIFI_ROOT}/net/netif_esp32s3.c"
        "${WIFI_ROOT}/net/port/sys_arch.c"
        "${FMP3_TARGET_DIR}/diag_recorder.c"
        "${FMP3_TARGET_DIR}/target_hrt64.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_core.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_scan.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_connect.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_optional_stubs.c"
        ${WIFI_ARDUINO_SOURCES})
    target_link_libraries(wifi_objects PRIVATE seam_common)
    add_dependencies(wifi_objects generate_cfg_gen_files)
endif()

#  The hosted profile's objects (ESP32-P4). Same object library name so that
#  everything downstream - the stage's dependency list, the consumer object
#  list - is written once; the sources are the other arrangement's.
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "hosted")
    add_library(wifi_objects OBJECT
        #  SDIO host and the board's pin set (StampP4 + Stamp add-on C6).
        "${HOSTED_ROOT}/sdio/p4sdio_host.c"
        "${HOSTED_ROOT}/sdio/p4sdio_board.c"
        #  esp_hosted: OS abstraction, static pools, transport.
        "${HOSTED_ROOT}/osi/p4hosted_osi.c"
        "${HOSTED_ROOT}/osi/p4hosted_pools.c"
        "${HOSTED_ROOT}/osi/p4hosted_prt.c"
        #  esp_hosted の RPC とトランスポート（開発リポジトリの rpc_probe.c から
        #  切り出したもの。段 B2a）。
        "${HOSTED_ROOT}/rpc/p4hosted_rpc.c"
        #  802.3 data path, netif glue, the reusable network operations and
        #  the resolver.
        "${HOSTED_ROOT}/net/netif_esp_hosted.c"
        "${HOSTED_ROOT}/net/p4hosted_net.c"
        "${HOSTED_ROOT}/net/p4hosted_netops.c"
        "${HOSTED_ROOT}/net/p4hosted_dns.c"
        #  lwIP's FMP3 port (the archive itself is prebuilt).
        "${RUNTIME_ROOT}/eth/lwip_port/sys_arch.c"
        "${RUNTIME_ROOT}/eth/lwip_port/fmp3_lwip_pools.c"
        #  The ESP-IDF system layer the three above call.
        "${HOSTED_ROOT}/p4shim/p4shim_stubs.c"
        "${HOSTED_ROOT}/p4shim/p4shim_misc.c"
        "${HOSTED_ROOT}/p4shim/p4shim_clk.c"
        "${HOSTED_ROOT}/p4shim/p4shim_gpio.c"
        "${HOSTED_ROOT}/p4shim/p4shim_mac.c"
        "${HOSTED_ROOT}/p4shim/p4shim_cache.c"
        "${HOSTED_ROOT}/p4shim/p4shim_diag.c"
        #  CLIC interrupt shim (the SDIO interrupt is one of its lines).
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_intr_clic.c")
    target_link_libraries(wifi_objects PRIVATE seam_common)
    add_dependencies(wifi_objects generate_cfg_gen_files)
endif()

#
#  Sketch-independent stage: ${CMAKE_BINARY_DIR}/prebuilt (objs/, ld/,
#  link-manifest.json, objects.rsp), built with
#      cmake --build <dir> --target fmp3_prebuilt
#  and consumed by scripts/fmp3_link.py on the user's machine.
#
set(PREBUILT_DIRECTORY "${CMAKE_BINARY_DIR}/prebuilt")
set(PREBUILT_MANIFEST "${PREBUILT_DIRECTORY}/link-manifest.json")

#  Arduino-side objects the driver must force-link (same sets as the Xtensa
#  minimal and wifi-connect profiles).
set(PREBUILT_ARDUINO_OBJECTS "<sketch>.cpp.o@@ArduinoSketchBridge.cpp.o")
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect")
    set(PREBUILT_ARDUINO_OBJECTS
        "${PREBUILT_ARDUINO_OBJECTS}@@ToppersFMP3_WiFi.cpp.o")
endif()

set(PREBUILT_STAGE_DEPENDS fmp3 seam_objects seam_start seam_cxx)
set(PREBUILT_CONSUMER_OBJECTS
    "$<TARGET_OBJECTS:seam_objects>;"
    "$<TARGET_OBJECTS:seam_start>;"
    "$<TARGET_OBJECTS:seam_cxx>")
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect")
    list(APPEND PREBUILT_STAGE_DEPENDS wifi_objects)
    list(APPEND PREBUILT_CONSUMER_OBJECTS "$<TARGET_OBJECTS:wifi_objects>")
endif()
#  Passed through a file, not the command line (Windows batch line limit;
#  see the Xtensa runtime's note).
set(PREBUILT_CONSUMER_OBJS_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/prebuilt-consumer-objects.txt")
file(GENERATE OUTPUT "${PREBUILT_CONSUMER_OBJS_FILE}"
     CONTENT "$<JOIN:${PREBUILT_CONSUMER_OBJECTS},\n>\n")

#
#  What the driver puts around the objects (manifest schema 2; the key names
#  are scripts/fmp3_link.py's "Manifest schemas"). Lists are joined with
#  '@@' for the cmake -P call, as in the Xtensa runtime.
#
#    linkBaseFlags  right after gcc. The ISA flags checked above plus the
#                   dev C6 link's -nostartfiles (chip.cmake) and -nostdlib
#                   (fmp3_core arch.cmake), and -Wl,--build-id=none
#                   (target.cmake) so no .note.gnu.build-id section can
#                   appear as a third flash segment. No -mlongcalls: that is
#                   an Xtensa option the RISC-V gcc rejects.
#    linkUFlags     target.cmake's --undefined= for the four kernel tables
#                   that nothing references before cfg pass 3 sees them.
#    linkLibGroup   the Xtensa minimal set.
#    linkTailFlags  -lc -lgcc, the dev C6 order (fmp3_core FMP3_LINK_LIBS).
#
set(XIP_LINK_BASE_FLAGS ${A1_LINK_ISA_FLAGS}
    -nostartfiles -nostdlib -Wl,--build-id=none)
if(A1_CHIP STREQUAL "esp32c5")
    #  The image entry (see seam_start above). GNU ld takes -e over the
    #  script's ENTRY(), which is how the dev seam build selects it too.
    if(A1_CPU_FREQ_MHZ STREQUAL "${A1_CHIP_CPU_FREQ_BOOST}")
        list(APPEND XIP_LINK_BASE_FLAGS -Wl,-e,seam_c5_entry_boost)
    else()
        list(APPEND XIP_LINK_BASE_FLAGS -Wl,-e,seam_c5_entry)
    endif()
endif()
string(JOIN "@@" XIP_LINK_BASE_FLAGS ${XIP_LINK_BASE_FLAGS})
set(XIP_LINK_UFLAGS
    -Wl,--undefined=_kernel_mpfinib_table
    -Wl,--undefined=_kernel_tinib_table
    -Wl,--undefined=_kernel_cycinib_table
    -Wl,--undefined=_kernel_alminib_table)
string(JOIN "@@" XIP_LINK_UFLAGS ${XIP_LINK_UFLAGS})
set(XIP_LINK_LIBGROUP "-lstdc++@@-lm")
set(XIP_LINK_TAIL_FLAGS "-lc@@-lgcc")
#  Archives shipped inside the stage (lib/), none for minimal.
set(PREBUILT_STAGE_LIB_DIRS "")

#
#  wifi-connect link: the dev order (a1_c6_stage1.cmake / a1_c5_stage1.cmake
#  target_link_libraries) with the Xtensa manifest's shape. The six Wi-Fi
#  blobs are the M5Stack core's (libphy.a sits in its ld/ directory, as for
#  the Xtensa chips); the supplicant, the software-crypto mbedTLS pair and
#  lwIP are the archives the dev repository built for FMP3
#  (wifi/prebuilt/{wpa2,lwip}/<chip>, README there), copied into the
#  stage's lib/ and referenced as @STAGE@/lib. All in
#  one --start-group as in dev; -lstdc++ -lm join the group and -lc -lgcc
#  stay at the tail, as in the minimal profile. -u psa_crypto_init is the
#  dev's (the second -u of the S3 build names a symbol no v5.5.4 archive
#  defines and was dropped there). No --wrap=esp_supplicant_init: the C6
#  leaves the supplicant to esp_wifi_init (D6).
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "native")
    set(WIFI_SDK_ARCHIVES
        "${ARDUINO_SDK_LIBRARY_ROOT}/libnet80211.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libpp.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcore.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcoexist.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libmesh.a"
        "${ARDUINO_SDK_LD_ROOT}/libphy.a")
    set(WPA2_PREBUILT_ROOT "${RUNTIME_ROOT}/wifi/prebuilt/wpa2/${A1_CHIP}")
    set(LWIP_PREBUILT_ROOT "${RUNTIME_ROOT}/wifi/prebuilt/lwip/${A1_CHIP}")
    set(WIFI_PREBUILT_ARCHIVES
        "${WPA2_PREBUILT_ROOT}/libsupplicant.a"
        "${WPA2_PREBUILT_ROOT}/libmbedtls.a"
        "${WPA2_PREBUILT_ROOT}/libmbedcrypto.a"
        "${LWIP_PREBUILT_ROOT}/liblwip.a")
    foreach(archive IN LISTS WIFI_SDK_ARCHIVES WIFI_PREBUILT_ARCHIVES)
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR "wifi-connect archive not found: ${archive}")
        endif()
    endforeach()
    set(XIP_LINK_UFLAGS "${XIP_LINK_UFLAGS}@@-Wl,-u,psa_crypto_init")
    set(XIP_LINK_LIBGROUP
        "-L${WPA2_PREBUILT_ROOT}@@"
        "-L${LWIP_PREBUILT_ROOT}@@"
        "-L${ARDUINO_SDK_LIBRARY_ROOT}@@"
        "-L${ARDUINO_SDK_LD_ROOT}@@"
        "-Wl,--start-group@@"
        "-lnet80211@@-lpp@@-lcore@@-lcoexist@@-lmesh@@-lphy@@"
        "-lsupplicant@@-lmbedtls@@-lmbedcrypto@@-llwip@@"
        "-lstdc++@@-lm@@-Wl,--end-group")
    string(JOIN "" XIP_LINK_LIBGROUP ${XIP_LINK_LIBGROUP})
    set(PREBUILT_STAGE_LIB_DIRS "${WPA2_PREBUILT_ROOT}@@${LWIP_PREBUILT_ROOT}")
endif()
#  The hosted profile links one archive, lwIP. There is no supplicant (no
#  WPA handshake on this side: the companion chip does it), no PHY and no
#  Wi-Fi blob, so none of the SDK archives the native block names exist for
#  this chip.
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND A1_CHIP_WIFI_KIND STREQUAL "hosted")
    set(LWIP_PREBUILT_ROOT "${RUNTIME_ROOT}/wifi/prebuilt/lwip/${A1_CHIP}")
    if(NOT EXISTS "${LWIP_PREBUILT_ROOT}/liblwip.a")
        message(FATAL_ERROR
            "wifi-connect (hosted) archive not found: ${LWIP_PREBUILT_ROOT}/liblwip.a")
    endif()
    set(XIP_LINK_LIBGROUP
        "-L${LWIP_PREBUILT_ROOT}@@"
        "-Wl,--start-group@@"
        "-llwip@@-lstdc++@@-lm@@"
        "-Wl,--end-group")
    string(JOIN "" XIP_LINK_LIBGROUP ${XIP_LINK_LIBGROUP})
    set(PREBUILT_STAGE_LIB_DIRS "${LWIP_PREBUILT_ROOT}")
endif()
string(JOIN "@@" A1_ROM_LDS_JOINED ${A1_ROM_LDS})

add_custom_command(
    OUTPUT "${PREBUILT_MANIFEST}"
    COMMAND "${CMAKE_COMMAND}"
        -DSTAGE_DIR=${PREBUILT_DIRECTORY}
        -DAR=${CMAKE_C_COMPILER_AR}
        -DGCC=${CMAKE_C_COMPILER}
        -DLIBFMP3=$<TARGET_FILE:fmp3>
        "-DCONSUMER_OBJS_FILE=${PREBUILT_CONSUMER_OBJS_FILE}"
        -DPROFILE=${FMP3_RUNTIME_PROFILE}
        -DA1_CHIP=${A1_CHIP}
        -DXIP_LD=${A1_XIP_LD}
        -DROMLD_ROOT=${ARDUINO_SDK_LD_ROOT}
        "-DROM_LDS=${A1_ROM_LDS_JOINED}"
        "-DSDK_LIBRARY_ROOT=${ARDUINO_SDK_LIBRARY_ROOT}"
        "-DSTAGE_LIB_DIRS=${PREBUILT_STAGE_LIB_DIRS}"
        "-DOBJCOPY=${CMAKE_OBJCOPY}"
        "-DSTRIP_DEBUG=${A1_PREBUILT_STRIP_DEBUG}"
        "-DLINK_BASE_FLAGS=${XIP_LINK_BASE_FLAGS}"
        "-DLINK_UFLAGS=${XIP_LINK_UFLAGS}"
        "-DLINK_LIBGROUP=${XIP_LINK_LIBGROUP}"
        "-DLINK_TAIL_FLAGS=${XIP_LINK_TAIL_FLAGS}"
        "-DEXTRA_TSCRIPTS=${A1_EXTRA_TSCRIPTS}"
        "-DARDUINO_OBJECT_NAMES=${PREBUILT_ARDUINO_OBJECTS}"
        -P "${RUNTIME_ROOT}/cmake/${A1_CHIP_PREBUILT_SCRIPT}"
    DEPENDS
        #  Target names alone give order-only dependencies; list the objects
        #  too so a recompiled object re-stages (Xtensa runtime's note).
        ${PREBUILT_STAGE_DEPENDS}
        "$<TARGET_FILE:fmp3>"
        ${PREBUILT_CONSUMER_OBJECTS}
        ${WIFI_PREBUILT_ARCHIVES}
        "${RUNTIME_ROOT}/cmake/${A1_CHIP_PREBUILT_SCRIPT}"
        "${A1_XIP_LD}"
    COMMENT "Staging sketch-independent ${FMP3_RUNTIME_PROFILE} FMP3 artifacts (${A1_CHIP})"
    VERBATIM)

add_custom_target(fmp3_prebuilt DEPENDS "${PREBUILT_MANIFEST}")
