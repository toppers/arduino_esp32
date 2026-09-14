#
#		ターゲット依存部の CMake 定義（M5NanoC6 / ESP32-C6・FMP3）
#
#  fmp3_core の CMakeLists.txt が FMP3_TARGET_DIR として include する
#  （fmp3/target/m5stamp_esp32p4_gcc/target.cmake と同じ分担）。
#  出典: asp3_esp_idf asp3/target/esp32c6_espidf/target.cmake（590 行）のうち
#  段1 に要る部分だけ（Wi-Fi/BT/lwIP/PMU/cold-boot の option は持ち込まない）。
#
#  arduino_esp32 port of the file above (dev fmp3_esp_idf_dev c7fef18,
#  fmp3/target/m5nanoc6_gcc/target.cmake). What changed and why:
#    - The hal/soc/esp_rom headers and the ROM linker scripts come from the
#      M5Stack Arduino core's esp32c6-libs SDK (ARDUINO_SDK_INCLUDE_ROOT /
#      ARDUINO_SDK_LD_ROOT, passed by scripts/build_prebuilt_stages.py) instead
#      of the dev repository's esp-idf submodule. Both are ESP-IDF v5.5.4; the
#      SDK lays include/<component>/... out the way esp-idf lays
#      components/<component>/... out, so every path is a one-to-one mapping.
#    - sdkconfig.h and the nuttx/config.h stub live in this runtime's
#      config/esp32c6 (dev: esp/config/esp32c6).
#  Everything else is as in dev. See ../../IMPORT_PROVENANCE.md.
#
get_filename_component(TARGETDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
set(ARCHDIR ${FMP3_ROOT_DIR}/arch/riscv_gcc)
get_filename_component(CHIPDIR "${TARGETDIR}/../../arch/riscv_gcc/esp32c6" REALPATH)
get_filename_component(A1_C6_RUNTIME "${TARGETDIR}/../.." REALPATH)

#
#  ESP-IDF v5.5.4 headers and linker scripts, supplied by the M5Stack core.
#  The two roots are required settings of ports/m5stack_riscv/runtime; the
#  checks here name the first file each root must hold so that a wrong
#  --arduino-data or core version fails at configure time with the path.
#
foreach(required ARDUINO_SDK_INCLUDE_ROOT ARDUINO_SDK_LD_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR
            "target 'm5nanoc6_gcc' needs ${required} (the M5Stack core's "
            "esp32c6-libs include/ and ld/ directories)")
    endif()
endforeach()
if(NOT EXISTS ${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c6/include/hal/systimer_ll.h)
    message(FATAL_ERROR
        "ARDUINO_SDK_INCLUDE_ROOT does not look like esp32c6-libs/<ver>/include: "
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c6/include/hal/systimer_ll.h is missing")
endif()
if(NOT EXISTS ${ARDUINO_SDK_LD_ROOT}/esp32c6.rom.ld)
    message(FATAL_ERROR
        "ARDUINO_SDK_LD_ROOT does not look like esp32c6-libs/<ver>/ld: "
        "${ARDUINO_SDK_LD_ROOT}/esp32c6.rom.ld is missing")
endif()

list(APPEND FMP3_COMPILE_DEFS TOPPERS_OMIT_TECS)
list(APPEND FMP3_INCLUDE_DIRS
    ${TARGETDIR}
    ${A1_C6_RUNTIME}/config/esp32c6                  # sdkconfig.h（スタブ）
    #  sdkconfig.h（NuttX 向け生成物の verbatim コピー）が無条件に
    #  #include <nuttx/config.h> するため、esp32(LX6)/esp32s3 と同じ規約で
    #  最小スタブを供給する（実装計画 Task 5 の configure/build で発覚、esp/config/esp32c6/
    #  hal_stub_include/nuttx/config.h 参照）。
    ${A1_C6_RUNTIME}/config/esp32c6/hal_stub_include
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/esp32c6/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/hal/platform_port/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/esp32c6/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/esp32c6/register
    ${ARDUINO_SDK_INCLUDE_ROOT}/soc/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_common/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/include
    ${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/esp32c6/include
)

if(DEFINED A1_C6_DUMP_FORMAT)
    set(FMP3_DUMP_FORMAT ${A1_C6_DUMP_FORMAT})
else()
    set(FMP3_DUMP_FORMAT srec)
endif()

if(DEFINED A1_C6_LDSCRIPT)
    set(FMP3_LDSCRIPT ${A1_C6_LDSCRIPT})
else()
    set(FMP3_LDSCRIPT ${TARGETDIR}/esp32c6.ld)
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
#  コンソール実体（chip.cmake の ESP32C6_CONSOLE に従う）
#
if(ESP32C6_CONSOLE STREQUAL "usbjtag")
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${TARGETDIR}/esp32c6_usbjtag_hal.c)
endif()

list(APPEND FMP3_LINK_OPTIONS
    -Wl,--gc-sections
    -Wl,--build-id=none
    -L${ARDUINO_SDK_LD_ROOT}
    -Wl,-T,${ARDUINO_SDK_LD_ROOT}/esp32c6.rom.ld
    -Wl,-T,${ARDUINO_SDK_LD_ROOT}/esp32c6.rom.api.ld
    -Wl,--undefined=_kernel_mpfinib_table
    -Wl,--undefined=_kernel_tinib_table
    -Wl,--undefined=_kernel_cycinib_table
    -Wl,--undefined=_kernel_alminib_table
)
list(APPEND FMP3_CFG1_OUT_LINK_OPTIONS -Wl,--no-gc-sections)
