#
#		ターゲット依存部の CMake 定義（M5Stamp-C5 / ESP32-C5/FMP3）
#
#  fmp3_core の CMakeLists.txt が FMP3_TARGET_DIR として include する
#  （fmp3/target/m5nanoc6_gcc/target.cmake と同じ分担）。
#  出典: fmp3/target/m5nanoc6_gcc/target.cmake（C6 段1）。差分:
#   (1) esp32c6 -> esp32c5（IDF の hal/soc/esp_rom ディレクトリ、ROM ld）
#   (2) esp/config/esp32c6/hal_stub_include を積まない（C5 の sdkconfig.h は
#       asp3 の手書きスタブで nuttx/config.h を include しない）
#   (3) 既定 ld は esp32c5_xip.ld（seam 前提。C6 の Direct Boot 用 esp32c6.ld
#       相当は作らない = PLAN.md D2）
#
#  arduino_esp32 port of the file above (dev fmp3_esp_idf_dev 1d96bcba,
#  fmp3/target/m5stampc5_gcc/target.cmake). What changed and why (the same
#  two changes as target/m5nanoc6_gcc/target.cmake, C6 stage 1):
#    - The hal/soc/esp_rom headers and the ROM linker scripts come from the
#      M5Stack Arduino core's esp32c5-libs SDK (ARDUINO_SDK_INCLUDE_ROOT /
#      ARDUINO_SDK_LD_ROOT, passed by scripts/build_prebuilt_stages.py) instead
#      of the dev repository's esp-idf submodule. Both are ESP-IDF v5.5.4; the
#      SDK lays include/<component>/... out the way esp-idf lays
#      components/<component>/... out, so every path is a one-to-one mapping.
#    - sdkconfig.h lives in this runtime's config/esp32c5 (dev:
#      esp/config/esp32c5).
#  Everything else is as in dev. See ../../IMPORT_PROVENANCE.md.
#
get_filename_component(TARGETDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
set(ARCHDIR ${FMP3_ROOT_DIR}/arch/riscv_gcc)
get_filename_component(CHIPDIR "${TARGETDIR}/../../arch/riscv_gcc/esp32c5" REALPATH)
get_filename_component(A1_C5_RUNTIME "${TARGETDIR}/../.." REALPATH)

#
#  ESP-IDF v5.5.4 headers and linker scripts, supplied by the M5Stack core.
#  The two roots are required settings of ports/m5stack_riscv/runtime; the
#  checks here name the first file each root must hold so that a wrong
#  --arduino-data or core version fails at configure time with the path.
#
foreach(required ARDUINO_SDK_INCLUDE_ROOT ARDUINO_SDK_LD_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR
            "target 'm5stampc5_gcc' needs ${required} (the M5Stack core's "
            "esp32c5-libs include/ and ld/ directories)")
    endif()
endforeach()
if(NOT EXISTS ${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c5/include/hal/systimer_ll.h)
    message(FATAL_ERROR
        "ARDUINO_SDK_INCLUDE_ROOT does not look like esp32c5-libs/<ver>/include: "
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c5/include/hal/systimer_ll.h is missing")
endif()
if(NOT EXISTS ${ARDUINO_SDK_LD_ROOT}/esp32c5.rom.ld)
    message(FATAL_ERROR
        "ARDUINO_SDK_LD_ROOT does not look like esp32c5-libs/<ver>/ld: "
        "${ARDUINO_SDK_LD_ROOT}/esp32c5.rom.ld is missing")
endif()

list(APPEND FMP3_COMPILE_DEFS TOPPERS_OMIT_TECS)
list(APPEND FMP3_INCLUDE_DIRS
    ${TARGETDIR}
    ${A1_C5_RUNTIME}/config/esp32c5                  # sdkconfig.h（スタブ）
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c5/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/platform_port/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/esp32c5/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/esp32c5/register
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_common/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/esp32c5/include
)

if(DEFINED A1_C5_DUMP_FORMAT)
    set(FMP3_DUMP_FORMAT ${A1_C5_DUMP_FORMAT})
else()
    set(FMP3_DUMP_FORMAT srec)
endif()

if(DEFINED A1_C5_LDSCRIPT)
    set(FMP3_LDSCRIPT ${A1_C5_LDSCRIPT})
else()
    set(FMP3_LDSCRIPT ${TARGETDIR}/esp32c5_xip.ld)
endif()

list(APPEND FMP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
)
list(APPEND FMP3_CFG_FILES            ${TARGETDIR}/target_kernel.cfg)
list(APPEND FMP3_KERNEL_CFG_TRB_FILES ${TARGETDIR}/target_kernel.py)
list(APPEND FMP3_CLASS_TRB_FILES      ${TARGETDIR}/target_class.py)
list(APPEND FMP3_CHECK_TRB_FILES      ${TARGETDIR}/target_check.py)

include(${CHIPDIR}/chip.cmake)

#
#  コンソール実体（chip.cmake の ESP32C5_CONSOLE に従う）
#
if(ESP32C5_CONSOLE STREQUAL "usbjtag")
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${TARGETDIR}/esp32c5_usbjtag_hal.c)
endif()

#
#  ROM ld は段1 では 2 本（esp32c5.rom.ld / esp32c5.rom.api.ld。
#  esp_rom_set_cpu_ticks_per_us 等の ROM API を解決する）。eco3 は使わない
#  （PLAN.md D14）。残り 11 本は段4（cmake/a1_c5_stage1.cmake の
#  A1_C5_ROM_LD_WIFI_LIST）。
#
list(APPEND FMP3_LINK_OPTIONS
    -Wl,--gc-sections
    -Wl,--build-id=none
    -L${ARDUINO_SDK_LD_ROOT}
    -Wl,-T,${ARDUINO_SDK_LD_ROOT}/esp32c5.rom.ld
    -Wl,-T,${ARDUINO_SDK_LD_ROOT}/esp32c5.rom.api.ld
    -Wl,--undefined=_kernel_mpfinib_table
    -Wl,--undefined=_kernel_tinib_table
    -Wl,--undefined=_kernel_cycinib_table
    -Wl,--undefined=_kernel_alminib_table
)
list(APPEND FMP3_CFG1_OUT_LINK_OPTIONS -Wl,--no-gc-sections)
