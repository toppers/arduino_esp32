#
#  ESP32-S3-DevKitC-1 用 target.cmake — fmp3_core の CMake フローの受け口
#
#  fmp3_core の汎用 CMakeLists.txt が外部から読むのは
#  ${FMP3_TARGET_DIR}/target.cmake の 1 本だけである（契約）。
#  そこから target -> chip -> arch と本リポジトリ側が駆動する。
#  include 連鎖の向きは classic の Makefile と同じ。
#
#  ★パスは CMAKE_CURRENT_LIST_DIR から自己解決する。
#    手本 C6（asp3_esp_idf）は include(${ASP3_ROOT_DIR}/arch/.../chip.cmake) と
#    書けるが、あれは chip が core 側にあるから成立する。S3 は chip も arch も
#    fmp3_core の外なので FMP3_ROOT_DIR 基準では書けない。
#    FMP3 の既存実例 kria_arm64_gcc/target.cmake と同型。
#
#  ★REALPATH で正規化している。参照先が symlink である環境では、正規化しないと
#    「同じファイルに二通りのパスで到達できる」状態を作ってしまう
#    （引き継ぎ資料 2026-07-19 §6-G）。
#
get_filename_component(TARGETDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
get_filename_component(ARCHDIR   "${TARGETDIR}/../../arch/xtensa_gcc" REALPATH)
set(CHIPDIR "${ARCHDIR}/esp32s3")

#
#  ★ドライバ名の照合（-dumpmachine では検出できない取り違えを塞ぐ）
#
#    esp-14.2.0 では xtensa-esp32s3-elf-gcc も xtensa-esp32-elf-gcc も
#    -dumpmachine が "xtensa-esp-elf" を返すため、fmp3_core の
#    toolchain_check.cmake は S3 と LX6 を区別できない。つまり
#    「LX6 用 driver で S3 をビルドする」事故は上流では検出されない。
#    ここでドライバのファイル名を照合して塞ぐ。
#
if(NOT FMP3_XTENSA_SKIP_DRIVER_CHECK)
    if(NOT DEFINED FMP3_XTENSA_EXPECTED_DRIVER)
        set(FMP3_XTENSA_EXPECTED_DRIVER "xtensa-esp32s3-elf-")
    endif()
    get_filename_component(_fmp3_cc_name "${CMAKE_C_COMPILER}" NAME)
    if(NOT _fmp3_cc_name MATCHES "^${FMP3_XTENSA_EXPECTED_DRIVER}")
        message(FATAL_ERROR
            "target 'esp32s3_devkitc_gcc' expects the '${FMP3_XTENSA_EXPECTED_DRIVER}' "
            "driver, but CMAKE_C_COMPILER is '${_fmp3_cc_name}'.\n"
            "  -dumpmachine cannot catch this: both the S3 and the LX6 driver report "
            "'xtensa-esp-elf', so fmp3_core's toolchain_check cannot tell them apart.\n"
            "  Use -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchain-xtensa-esp32s3.cmake, "
            "or set -DFMP3_XTENSA_SKIP_DRIVER_CHECK=ON to override deliberately.")
    endif()
endif()

#  DUMPOPTS は classic でも空（実測: objdump -s cfg1_out にオプション無し）
set(FMP3_DUMPOPTS "")

list(APPEND FMP3_COMPILE_DEFS TOPPERS_OMIT_TECS)
list(APPEND FMP3_INCLUDE_DIRS ${TARGETDIR})

list(APPEND FMP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
    ${TARGETDIR}/diag_recorder.c)

set(FMP3_LDSCRIPT ${TARGETDIR}/esp32s3_devkitc.ld)

list(APPEND FMP3_CFG_FILES            ${TARGETDIR}/target_kernel.cfg)
list(APPEND FMP3_KERNEL_CFG_TRB_FILES ${TARGETDIR}/target_kernel.py)
list(APPEND FMP3_CLASS_TRB_FILES      ${TARGETDIR}/target_class.py)
list(APPEND FMP3_CHECK_TRB_FILES      ${TARGETDIR}/target_check.py)

include(${CHIPDIR}/chip.cmake)
