#
#  無印 ESP32（Xtensa LX6）用 CMake ツールチェーンファイル
#
#  使い方:
#    cmake -S <repo>/fmp3_core -B <build> -G Ninja \
#      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchain-xtensa-esp32.cmake \
#      -DFMP3_TARGET=esp32_devkitc_gcc \
#      -DFMP3_TARGET_DIR=<repo>/target/esp32_devkitc_gcc \
#      -DFMP3_LIBRARY_ONLY=ON -DFMP3_PRC_NUM=1
#
#  ★FMP3_EXPECTED_TOOLCHAIN_MACHINE は "xtensa-esp-elf" である（"xtensa-esp32s3-elf"
#    ではない）。esp-14.2.0 の xtensa-esp32-elf-gcc は xtensa-esp-elf-gcc への
#    マルチターゲットドライバのエイリアスであり、-dumpmachine は "xtensa-esp-elf"
#    を返す（実測）。引き継ぎ資料 2026-07-19 §6-E の記述はこの点で誤りであり、
#    資料どおり "xtensa-esp32s3-elf" と書くと configure が FATAL_ERROR で止まる。
#
#  ★限界（重要）: -dumpmachine は S3 と LX6 を区別しない（両者とも "xtensa-esp-elf"）。
#    したがって fmp3_core の toolchain_check.cmake は
#    「S3 用 driver で LX6 をビルドする」型の取り違えを原理的に検出できない。
#    これは fmp3_core の欠陥ではなく Xtensa ツールチェーンの性質である。
#    この穴は target.cmake 側のドライバ名照合で塞いでいる
#    （FMP3_XTENSA_EXPECTED_DRIVER / FMP3_XTENSA_SKIP_DRIVER_CHECK）。
#
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)

#  ベアメタルなのでリンクを伴う try_compile は成立しない
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED XTENSA_TOOLCHAIN_PREFIX)
    set(XTENSA_TOOLCHAIN_PREFIX xtensa-esp32-elf-)
endif()

set(CMAKE_C_COMPILER   ${XTENSA_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${XTENSA_TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${XTENSA_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${XTENSA_TOOLCHAIN_PREFIX}ar)
set(CMAKE_NM           ${XTENSA_TOOLCHAIN_PREFIX}nm)
set(CMAKE_OBJCOPY      ${XTENSA_TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP      ${XTENSA_TOOLCHAIN_PREFIX}objdump)

if(NOT DEFINED FMP3_EXPECTED_TOOLCHAIN_MACHINE)
    set(FMP3_EXPECTED_TOOLCHAIN_MACHINE "xtensa-esp-elf")
endif()
