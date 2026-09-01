#
#  ESP32-DevKitC（無印 ESP32 / Xtensa LX6）用 target.cmake
#
#  S3 版との差は chip の選択・リンカスクリプト名・期待するドライバ名だけである。
#  設計意図と各項の根拠は target/esp32s3_devkitc_gcc/target.cmake のコメントを参照。
#
get_filename_component(TARGETDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
get_filename_component(ARCHDIR   "${TARGETDIR}/../../arch/xtensa_gcc" REALPATH)
set(CHIPDIR "${ARCHDIR}/esp32")

#
#  ドライバ名の照合。classic の Makefile.target が GCC_TARGET = xtensa-esp32-elf を
#    指定しているのに対応する。-dumpmachine は S3 と LX6 を区別しないため、
#    この照合が無いと「S3 用 driver で LX6 をビルドする」事故が素通りする。
#
if(NOT FMP3_XTENSA_SKIP_DRIVER_CHECK)
    if(NOT DEFINED FMP3_XTENSA_EXPECTED_DRIVER)
        set(FMP3_XTENSA_EXPECTED_DRIVER "xtensa-esp32-elf-")
    endif()
    get_filename_component(_fmp3_cc_name "${CMAKE_C_COMPILER}" NAME)
    if(NOT _fmp3_cc_name MATCHES "^${FMP3_XTENSA_EXPECTED_DRIVER}")
        message(FATAL_ERROR
            "target 'esp32_devkitc_gcc' expects the '${FMP3_XTENSA_EXPECTED_DRIVER}' "
            "driver, but CMAKE_C_COMPILER is '${_fmp3_cc_name}'.\n"
            "  -dumpmachine cannot catch this: both the S3 and the LX6 driver report "
            "'xtensa-esp-elf', so fmp3_core's toolchain_check cannot tell them apart.\n"
            "  Use -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchain-xtensa-esp32.cmake, "
            "or set -DFMP3_XTENSA_SKIP_DRIVER_CHECK=ON to override deliberately.")
    endif()
endif()

set(FMP3_DUMPOPTS "")

list(APPEND FMP3_COMPILE_DEFS TOPPERS_OMIT_TECS)
list(APPEND FMP3_INCLUDE_DIRS ${TARGETDIR})

list(APPEND FMP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
    ${TARGETDIR}/diag_recorder.c)

set(FMP3_LDSCRIPT ${TARGETDIR}/esp32_devkitc.ld)

list(APPEND FMP3_CFG_FILES            ${TARGETDIR}/target_kernel.cfg)
list(APPEND FMP3_KERNEL_CFG_TRB_FILES ${TARGETDIR}/target_kernel.py)
list(APPEND FMP3_CLASS_TRB_FILES      ${TARGETDIR}/target_class.py)
list(APPEND FMP3_CHECK_TRB_FILES      ${TARGETDIR}/target_check.py)

include(${CHIPDIR}/chip.cmake)
