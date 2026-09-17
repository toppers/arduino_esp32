#
#  CMake toolchain file for the ESP32-P4 (RISC-V RV32IMAFC, dual core, CLIC).
#
#  The compiler is the riscv32-esp-elf gcc that ships with the M5Stack Arduino
#  core (tools/esp-rv32/2601, crosstool-NG esp-14.2.0_20260121 - the version
#  ESP-IDF v5.5.4 pins for riscv32-esp-elf). scripts/build_prebuilt_stages.py
#  puts that toolchain's bin/ first on PATH before configuring, so the names
#  below resolve to it; nothing here spells out where the core is installed.
#
#  Shape: dev fmp3_esp_idf_dev 1d96bcba cmake/toolchain-riscv-esp32p4.cmake,
#  the c6 -> c5 copy of the C6 file, which is the sibling of ports/m5stack_xtensa/runtime/cmake/
#  toolchain-xtensa-esp32s3.cmake. -march/-mabi/-mcmodel/specs are not set
#  here: they are the chip layer's decision (arch/riscv_gcc/esp32p4/chip.cmake).
#
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

#  Bare metal: a try_compile that links cannot succeed.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED RISCV_TOOLCHAIN_PREFIX)
    set(RISCV_TOOLCHAIN_PREFIX riscv32-esp-elf-)
endif()

set(CMAKE_C_COMPILER   ${RISCV_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${RISCV_TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${RISCV_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${RISCV_TOOLCHAIN_PREFIX}ar)
set(CMAKE_NM           ${RISCV_TOOLCHAIN_PREFIX}nm)
set(CMAKE_OBJCOPY      ${RISCV_TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP      ${RISCV_TOOLCHAIN_PREFIX}objdump)

#  fmp3_core's toolchain_check.cmake compares -dumpmachine against this.
if(NOT DEFINED FMP3_EXPECTED_TOOLCHAIN_MACHINE)
    set(FMP3_EXPECTED_TOOLCHAIN_MACHINE "riscv32-esp-elf")
endif()

#
#  Pin the toolchain build at configure time. The P4 port was brought up with
#  esp-14.2.0_20260121 (dev repository and the M5Stack core 3.3.8 agree on
#  it); a riscv32-esp-elf of another build found first on PATH - an ESP-IDF
#  install, say - would configure silently with a different compiler. The
#  Xtensa toolchain files do not need this because their driver name carries
#  the chip; the RISC-V driver name does not.
#
execute_process(COMMAND ${CMAKE_C_COMPILER} --version
                OUTPUT_VARIABLE _p4_gcc_version ERROR_QUIET)
if(NOT _p4_gcc_version MATCHES "esp-14\\.2\\.0_20260121")
    message(FATAL_ERROR
        "The ESP32-P4 stage is built with riscv32-esp-elf esp-14.2.0_20260121 "
        "(M5Stack core tools/esp-rv32/2601).\n"
        "  gcc found on PATH: ${_p4_gcc_version}\n"
        "  Run scripts/build_prebuilt_stages.py --chip esp32p4, which puts the "
        "core's toolchain first on PATH, or put its bin/ first yourself.")
endif()
