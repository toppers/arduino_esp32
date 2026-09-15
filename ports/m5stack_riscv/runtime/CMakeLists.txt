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
#                           chip (C5: stage 3 of the C5 plan)
#    A1_ROM_LDS_<profile>   the ROM linker scripts per profile (below)
#
if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
    set(A1_CHIP esp32c6)
endif()
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
    #  wifi-connect for the C5 is stage 3 of the C5 plan (shim CLIC branch,
    #  C5 blobs and prebuilt archives). Refused until then rather than
    #  configured with the C6 lists.
    set(A1_CHIP_WIFI OFF)
else()
    message(FATAL_ERROR
        "ports/m5stack_riscv/runtime builds the esp32c6 or the esp32c5 (got A1_CHIP='${A1_CHIP}')")
endif()
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect" AND NOT A1_CHIP_WIFI)
    message(FATAL_ERROR
        "the ${A1_CHIP} port cannot stage wifi-connect yet (C5 plan stage 3)")
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
#  thirteen (dev a1_c5_stage1.cmake A1_C5_ROM_LD_WIFI_LIST, no
#  esp32c5.rom.eco3.ld: dev D14, the M5Stamp-C5 is chip rev v1.0) and is
#  written at stage 3 with the libc-supply decision, not here.
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
set(FMP3_COMPILE_DEFS
    CORE_CLK_MHZ=${A1_CPU_FREQ_MHZ})
if(A1_CPU_FREQ_MHZ STREQUAL "${A1_CHIP_CPU_FREQ_BOOST}")
    list(APPEND FMP3_COMPILE_DEFS SEAM_${A1_CHIP_TAG}_CLK_BOOST=1)
endif()
if(NOT A1_SEAM_ENTRY_MARK STREQUAL "")
    list(APPEND FMP3_COMPILE_DEFS SEAM_${A1_CHIP_TAG}_ENTRY_MARK=${A1_SEAM_ENTRY_MARK})
endif()
set(FMP3_INCLUDE_DIRS
    "${FMP3_APPLICATION_DIR}"
    #  seam_<chip>_clk.h for anything that wants the boost result.
    "${RUNTIME_ROOT}/seam")

#
#  wifi-connect: what the dev repository's cmake/a1_c6_stage1.cmake adds
#  before add_subdirectory(fmp3_core) when A1_C6_WIFI is ON. As there, the
#  include directories, definitions and the intmtx cfg go through the fmp3
#  target's PUBLIC properties to every object (kernel, syssvc, shim), so the
#  stage is built the way the dev C6 Wi-Fi image was: the shim's libc stub
#  headers (wifi/config/hal_stub_include) stand in front of newlib's for
#  every TU, and TCNT_SYSLOG_BUFFER reaches syslog.c. The ESP-IDF headers
#  come from the M5Stack core's esp32c6-libs SDK; the list is the dev
#  esp/boot/build_incflags_esp32c6_espidf.txt with components/X mapped to
#  include/X (same ESP-IDF v5.5.4). Three of its directories are not in the
#  SDK: esp_hw_support/port/include and esp_wifi/wifi_apps/roaming_app/include
#  are not included by any of these TUs (checked against the dev build's
#  dependency database), and the lwIP contrib app headers ping.h /
#  tcpecho_raw.h / udpecho_raw.h that netif_esp32s3.c includes are vendored
#  under wifi/net/lwip_contrib_include (IMPORT_PROVENANCE.md).
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect")
    set(WIFI_ROOT "${RUNTIME_ROOT}/wifi")
    set(WIFI_SDK_INCLUDE_DIRS
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
    set(WIFI_INCLUDE_DIRS)
    foreach(dir IN LISTS WIFI_SDK_INCLUDE_DIRS)
        if(NOT IS_DIRECTORY "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
            message(FATAL_ERROR
                "wifi-connect: SDK include directory is missing: "
                "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
        endif()
        list(APPEND WIFI_INCLUDE_DIRS "${ARDUINO_SDK_INCLUDE_ROOT}/${dir}")
    endforeach()
    list(APPEND WIFI_INCLUDE_DIRS
        #  Ordered as in dev's incflags: the C6 config (sdkconfig.h and its
        #  nuttx/config.h; target.cmake adds the same two again later) before
        #  the chip-shared libc/nvs/gpio stubs, which also carry a
        #  nuttx/config.h (same values, different guard). Keeping dev's order
        #  keeps the same file winning.
        "${RUNTIME_ROOT}/config/esp32c6"
        "${RUNTIME_ROOT}/config/esp32c6/hal_stub_include"
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
        #  (arduino_interrupt.cfg / .h; cfg INCLUDE resolves here).
        "${WIFI_ROOT}/adapter"
        "${RUNTIME_ROOT}/arduino")
    list(APPEND FMP3_INCLUDE_DIRS ${WIFI_INCLUDE_DIRS})

    #  From build_incflags_esp32c6_espidf.txt (TOPPERS_ESP32C6 dropped: chip_stddef.h
    #  defines it) and the additions of the dev Wi-Fi block, verbatim. The ones
    #  only the dev demo application wifi_sta.c reads (A1_C6_WIFI_SCAN,
    #  TOPPERS_APP_HEAP_REPORT, A1_C6_WIFI_BOOT_DELAY_MS, A1_C6_USJ_REARM_PROBE)
    #  and the reset-surviving boot trace (A1_C6_BOOT_TRACE) are not passed.
    list(APPEND FMP3_COMPILE_OPTIONS
        -DESP_PLATFORM
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
    #  c6_apm_unblock). Without it the modem DMA cannot reach RAM and a scan
    #  finds 0 APs (dev measured both arms). OFF is the control for stage 4.
    option(TOPPERS_C6_APM_UNBLOCK
        "wifi-connect: disable the APM access-path filters before the modem is enabled (default ON)"
        ON)
    if(TOPPERS_C6_APM_UNBLOCK)
        list(APPEND FMP3_COMPILE_OPTIONS -DA1_C6_APM_UNBLOCK=1)
    endif()
    #  Verbose per-event readback / NOTICE lines of the shim (dev
    #  A1_C6_WIFI_DIAG). The one-shot detectors stay on regardless.
    option(TOPPERS_C6_WIFI_DIAG
        "wifi-connect: verbose shim diagnostics on the console (default OFF)"
        OFF)
    if(TOPPERS_C6_WIFI_DIAG)
        list(APPEND FMP3_COMPILE_OPTIONS -DA1_C6_WIFI_DIAG=1)
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
    option(TOPPERS_C6_NET_DIAG
        "wifi-connect: dev network diagnostics (gateway ping, port-7 echo servers, ip=/gw= line) (default OFF)"
        OFF)
    if(TOPPERS_C6_NET_DIAG)
        list(APPEND FMP3_COMPILE_OPTIONS -DTOPPERS_C6_NET_DIAG=1)
    endif()

    #  CFG_INT/DEF_INH for the blob's CPU interrupt lines 1..15 (the S3/LX6
    #  line block of esp_shim.cfg is outside TOPPERS_ESP32C6). Before the
    #  application cfg, as in dev.
    list(APPEND FMP3_CFG_FILES "${WIFI_ROOT}/shim/esp_shim_intr_intmtx.cfg")

    #  The eleven ROM scripts beyond target.cmake's two, for the cfg pass-1
    #  link (the sketch link takes them from the manifest, see A1_ROM_LDS).
    foreach(rom_ld IN LISTS A1_ROM_LDS)
        if(NOT rom_ld IN_LIST A1_ROM_LDS_minimal)
            list(APPEND FMP3_LINK_OPTIONS "-Wl,-T,${ARDUINO_SDK_LD_ROOT}/${rom_ld}")
        endif()
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
#  Single core: target_kernel.h refuses any other TNUM_PRCID.
set(FMP3_PRC_NUM 1)
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
    "${FMP3_TARGET_DIR}/${A1_CHIP_USBJTAG_HAL}"
    #  esp_app_desc at the head of the DROM segment; the bootloader reads it.
    "${RUNTIME_ROOT}/seam/${A1_CHIP_SEAM_APPDESC}"
    #  80 -> 160 (C6) / 240 (C5) MHz, called from the seam entry when
    #  SEAM_<TAG>_CLK_BOOST.
    "${RUNTIME_ROOT}/seam/${A1_CHIP_SEAM_CLK}"
    #  _exit / _kill / _getpid / _write / __getreent for newlib-nano's
    #  __stack_chk_fail path: the M5Stack core builds every sketch with
    #  -fstack-protector, so a sketch with a local char array needs them.
    "${RUNTIME_ROOT}/seam/newlib_syscalls.c")
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
add_library(seam_start OBJECT
    "${FMP3_CHIP_DIR}/chip_start.S"
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
#  wifi-connect: the sources the dev Wi-Fi build adds to fmp (a1_c6_stage1.cmake,
#  target_sources(fmp ...) under A1_C6_WIFI), same set, same order. Kept in
#  their own object library so the profile's objects are one list in this
#  file; the compile settings are the fmp3 PUBLIC ones (see the wifi-connect
#  block above), so nothing here differs from seam_objects in how it is built.
#
#    wifi/shim      the ESP-IDF OS shim with the C6 branch (second copy of
#                   the dev esp/shim; the Xtensa port's fork under
#                   ports/m5stack_xtensa is a different base and is not shared)
#    wifi/hal_src   PHY / Wi-Fi hal sources (periph_ctrl.c and
#                   phy_init_data.c of the S3 set are not used: C6 takes the
#                   esp-idf originals)
#    wifi/idf_src   the six esp-idf v5.5.4 originals (D8: vendored, a
#                   recorded deviation from BUILDING.md; the chip efuse_hal.c
#                   is renamed efuse_hal_esp32c6.c because the stage keeps
#                   every object in one directory by basename). Reason: the
#                   matching members of the core's esp32c6-libs .a files
#                   (libesp_hw_support.a / libhal.a) require
#                   vPortEnterCritical/vPortExitCritical/xPortInIsrContext
#                   (FreeRTOS) as undefined references, which FMP3 does not
#                   supply, so these are compiled from source against a
#                   FreeRTOS stub instead (dev repo INVESTIGATION.md 2-3).
#                   Stage 5 decision S5-2: kept as vendored copies; stage 4's
#                   hardware record is with these files. Re-evaluate when
#                   either the "core's .a members + vPort* shim" route is
#                   actually verified, or the pinned M5Stack core version
#                   moves (BUILDING.md exception clause).
#    wifi/net       lwIP glue of the dev type (D7): netif_esp32s3.c and the
#                   sys_arch port; liblwip.a itself is prebuilt. The three
#                   lwIP contrib headers under net/lwip_contrib_include are
#                   the other half of D8 (S5-2: kept); they are included
#                   only when TOPPERS_C6_NET_DIAG is ON
#    target         diag_recorder.c (diag_event() the shim and the netif
#                   call) and target_hrt64.c (target_hrt_get_current64())
#    wifi/adapter   the Arduino-facing adapter (toppers_fmp3_wifi_* for
#                   src/ToppersFMP3_WiFi.cpp), the C6 version of the Xtensa
#                   port's wifi/adapter: one shared bring-up
#                   (toppers_wifi_core.c, dev C6 order, no WPA callback
#                   table and no --wrap=esp_supplicant_init = D6), scan,
#                   connect on the dev netif (D7), and the weak stubs
#    arduino        attachInterrupt on line 19 through the kernel's
#                   esp32c6_intmtx_route (S3-4), pinMode/digitalWrite/
#                   digitalRead on hal/gpio_ll.h and rgbLedWrite on RMT
#                   channel 0 (stage 6; wifi-connect only, minimal has no
#                   SDK include path)
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect")
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
        "${WIFI_ROOT}/shim/esp_shim_intr_intmtx.c"
        "${WIFI_ROOT}/hal_src/phy_common.c"
        "${WIFI_ROOT}/hal_src/phy_init.c"
        "${WIFI_ROOT}/idf_src/phy_init_data.c"
        "${WIFI_ROOT}/hal_src/esp_wifi_regulatory.c"
        "${WIFI_ROOT}/hal_src/wifi_init.c"
        "${WIFI_ROOT}/hal_src/wifi_lib_printf.c"
        "${WIFI_ROOT}/hal_src/phy_lib_printf.c"
        "${WIFI_ROOT}/idf_src/periph_ctrl.c"
        "${WIFI_ROOT}/idf_src/modem_clock.c"
        "${WIFI_ROOT}/idf_src/modem_clock_hal.c"
        "${WIFI_ROOT}/idf_src/efuse_hal.c"
        "${WIFI_ROOT}/idf_src/efuse_hal_esp32c6.c"
        "${WIFI_ROOT}/net/netif_esp32s3.c"
        "${WIFI_ROOT}/net/port/sys_arch.c"
        "${FMP3_TARGET_DIR}/diag_recorder.c"
        "${FMP3_TARGET_DIR}/target_hrt64.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_core.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_scan.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_connect.c"
        "${WIFI_ROOT}/adapter/toppers_wifi_optional_stubs.c"
        "${RUNTIME_ROOT}/arduino/arduino_interrupt.c"
        "${RUNTIME_ROOT}/arduino/arduino_gpio.c"
        "${RUNTIME_ROOT}/arduino/arduino_rgb_led.c")
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
#  wifi-connect link: the dev order (a1_c6_stage1.cmake target_link_libraries)
#  with the Xtensa manifest's shape. The six Wi-Fi blobs are the M5Stack
#  core's (libphy.a sits in its ld/ directory, as for the Xtensa chips); the
#  supplicant, the software-crypto mbedTLS pair and lwIP are the archives the
#  dev repository built for FMP3 (wifi/prebuilt/{wpa2,lwip}/esp32c6, README
#  there), copied into the stage's lib/ and referenced as @STAGE@/lib. All in
#  one --start-group as in dev; -lstdc++ -lm join the group and -lc -lgcc
#  stay at the tail, as in the minimal profile. -u psa_crypto_init is the
#  dev's (the second -u of the S3 build names a symbol no v5.5.4 archive
#  defines and was dropped there). No --wrap=esp_supplicant_init: the C6
#  leaves the supplicant to esp_wifi_init (D6).
#
if(FMP3_RUNTIME_PROFILE STREQUAL "wifi-connect")
    set(WIFI_SDK_ARCHIVES
        "${ARDUINO_SDK_LIBRARY_ROOT}/libnet80211.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libpp.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcore.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcoexist.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libmesh.a"
        "${ARDUINO_SDK_LD_ROOT}/libphy.a")
    set(WPA2_PREBUILT_ROOT "${RUNTIME_ROOT}/wifi/prebuilt/wpa2/esp32c6")
    set(LWIP_PREBUILT_ROOT "${RUNTIME_ROOT}/wifi/prebuilt/lwip/esp32c6")
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
        "-DEXTRA_TSCRIPTS="
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
