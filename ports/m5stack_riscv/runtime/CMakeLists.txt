cmake_minimum_required(VERSION 3.23)

#
#  ESP32-C6 (M5NanoC6, RISC-V RV32IMAC, single core) runtime for the Arduino
#  board package: builds the sketch-independent FMP3 stage.
#
#  This is the RISC-V counterpart of ports/m5stack_xtensa/runtime/CMakeLists.txt
#  and is driven the same way (scripts/build_prebuilt_stages.py, chip row
#  "esp32c6"). It is deliberately a separate file rather than a chip branch of
#  the Xtensa one: the Xtensa file is protected by the X-check (its stages are
#  compared byte for byte against a baseline), and nothing the C6 needs -
#  -march instead of -mlongcalls, a fixed-VMA image instead of runtime PADDR
#  resolution, no flash_cache_init - would fit as a small branch there.
#
#  What it builds (profile "minimal" only; wifi-connect is stage 3):
#    fmp3            libfmp3.a from third_party/fmp3_core with the chip/target
#                    layers in arch/riscv_gcc/esp32c6 and target/m5nanoc6_gcc
#                    (vendored from fmp3_esp_idf_dev c7fef18, IMPORT_PROVENANCE.md)
#    seam_objects    application TU, syssvc, console driver, app descriptor,
#                    CPU clock boost
#    seam_start      chip_start.S (FMP3 entry) and seam_c6_entry.S (image entry)
#    seam_cxx        init_array.cpp (esp_run_init_array for the sketch bridge)
#    fmp3_prebuilt   cmake/prebuilt_stage_c6.cmake -> ${CMAKE_BINARY_DIR}/prebuilt
#
#  Build facts come from the dev repository's cmake/a1_c6_stage1.cmake (the
#  seam-c6-* presets); where this file differs it says so in place.
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
if(NOT FMP3_RUNTIME_PROFILE STREQUAL "minimal")
    message(FATAL_ERROR
        "FMP3_RUNTIME_PROFILE must be minimal for the esp32c6 port "
        "(wifi-connect is not staged yet; got '${FMP3_RUNTIME_PROFILE}')")
endif()

#
#  This runtime is the ESP32-C6 only. build_prebuilt_stages.py always passes
#  A1_CHIP; a missing value is accepted as esp32c6 the way the Xtensa runtime
#  accepts a missing value as esp32s3, and anything else is refused.
#
if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
    set(A1_CHIP esp32c6)
endif()
if(NOT A1_CHIP STREQUAL "esp32c6")
    message(FATAL_ERROR
        "ports/m5stack_riscv/runtime builds the esp32c6 only (got A1_CHIP='${A1_CHIP}')")
endif()
set(A1_CHIP_TARGET_NAME m5nanoc6_gcc)
set(A1_CHIP_ARCH_NAME esp32c6)
set(A1_XIP_LD_NAME esp32c6_xip.ld)
set(A1_CHIP_ROM_LD esp32c6.rom.ld)

#
#  ROM linker scripts the sketch link applies, relative to the SDK's ld/
#  directory (the driver adds -L there). The 13 scripts are what the dev
#  repository's C6 Wi-Fi build links (target.cmake's two plus the eleven of
#  a1_c6_stage1.cmake), taken over unchanged so that the wifi-connect stage
#  of stage 3 links the same set. The dev minimal build links only the first
#  two; the extra scripts bind libc/libgcc/newlib and Wi-Fi symbols to ROM
#  addresses with plain assignments, so what a symbol resolves to in the
#  sketch image is recorded from nm, not assumed (AC-1f). Every name is
#  checked to exist in the SDK at configure time.
#
set(A1_ROM_LDS
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
#  bootloader leaves the PCR at 480/3/2) and seam_c6_entry.S raises it to
#  160 MHz before toppers_start when SEAM_C6_CLK_BOOST is defined
#  (seam/seam_c6_clk.c). 160 MHz is the ESP-IDF default and the setting the
#  dev repository validated Wi-Fi with, so it is the default here (S1-6);
#  CORE_CLK_MHZ is the value the kernel's timing constants are calibrated
#  for and has to say the same thing.
#
set(A1_C6_CPU_FREQ_MHZ "160" CACHE STRING "ESP32-C6 CPU frequency (80 or 160 MHz)")
if(NOT A1_C6_CPU_FREQ_MHZ STREQUAL "80" AND NOT A1_C6_CPU_FREQ_MHZ STREQUAL "160")
    message(FATAL_ERROR
        "A1_C6_CPU_FREQ_MHZ must be 80 or 160 (got '${A1_C6_CPU_FREQ_MHZ}')")
endif()
#  One byte written raw to the USB-Serial/JTAG FIFO at the image entry, before
#  anything of FMP3 runs; tells "the bootloader jumped" from "FMP3 is silent".
#  Empty removes the code. Same default as the dev seam builds.
set(A1_C6_SEAM_ENTRY_MARK "0x53" CACHE STRING
    "ASCII code emitted to USB-Serial/JTAG at the seam entry (empty = none)")

#
#  Definitions for the kernel and every runtime object. TOPPERS_OMIT_TECS,
#  TOPPERS_SUPPORT_TLS and TOPPERS_ESP32C6_CONSOLE_USBJTAG are added by
#  target.cmake / chip.cmake; -march and friends by chip.cmake. All of them
#  reach the runtime objects through the fmp3 target's PUBLIC properties (see
#  seam_common below), so the kernel and the runtime cannot drift apart.
#
set(FMP3_COMPILE_DEFS
    CORE_CLK_MHZ=${A1_C6_CPU_FREQ_MHZ})
if(A1_C6_CPU_FREQ_MHZ STREQUAL "160")
    list(APPEND FMP3_COMPILE_DEFS SEAM_C6_CLK_BOOST=1)
endif()
if(NOT A1_C6_SEAM_ENTRY_MARK STREQUAL "")
    list(APPEND FMP3_COMPILE_DEFS SEAM_C6_ENTRY_MARK=${A1_C6_SEAM_ENTRY_MARK})
endif()
set(FMP3_INCLUDE_DIRS
    "${FMP3_APPLICATION_DIR}"
    #  seam_c6_clk.h for anything that wants the boost result.
    "${RUNTIME_ROOT}/seam")

project(toppers_fmp3_arduino_runtime_c6 C CXX ASM)

set(FMP3_TARGET "${A1_CHIP_TARGET_NAME}")
set(FMP3_TARGET_DIR "${RUNTIME_ROOT}/target/${A1_CHIP_TARGET_NAME}")
set(FMP3_CHIP_DIR "${RUNTIME_ROOT}/arch/riscv_gcc/${A1_CHIP_ARCH_NAME}")
set(A1_XIP_LD "${FMP3_TARGET_DIR}/${A1_XIP_LD_NAME}")
#  The XIP linker script also serves the cfg pass-1 link (cfg1_out), as in
#  the dev seam builds (A1_C6_LDSCRIPT); its ENTRY symbol is absent there,
#  which the linker reports as a warning only.
set(A1_C6_LDSCRIPT "${A1_XIP_LD}")
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
    #  Console: USB-Serial/JTAG (chip.cmake ESP32C6_CONSOLE=usbjtag; the
    #  M5NanoC6 has no UART bridge). These two are what fmp3_add_syssvc()
    #  would add from FMP3_SYSSVC_TARGET_C_FILES in a non-library build.
    "${FMP3_CHIP_DIR}/chip_serial.c"
    "${FMP3_TARGET_DIR}/esp32c6_usbjtag_hal.c"
    #  esp_app_desc at the head of the DROM segment; the bootloader reads it.
    "${RUNTIME_ROOT}/seam/seam_c6_appdesc.c"
    #  80 -> 160 MHz, called from seam_c6_entry.S when SEAM_C6_CLK_BOOST.
    "${RUNTIME_ROOT}/seam/seam_c6_clk.c"
    #  _exit / _kill / _getpid / _write / __getreent for newlib-nano's
    #  __stack_chk_fail path: the M5Stack core builds every sketch with
    #  -fstack-protector, so a sketch with a local char array needs them.
    "${RUNTIME_ROOT}/seam/newlib_syscalls.c")
target_link_libraries(seam_objects PRIVATE seam_common)
add_dependencies(seam_objects generate_cfg_gen_files)

#
#  Startup: chip_start.S is the C6 replacement for fmp3_core's common
#  start.S (chip.cmake swaps it in FMP3_START_FILES; the common one issues
#  FPU instructions the rv32imac assembler rejects) and defines
#  toppers_start. seam_c6_entry.S is the image entry the bootloader jumps to
#  (ENTRY(seam_c6_entry) in esp32c6_xip.ld); it ends in toppers_start.
#
add_library(seam_start OBJECT
    "${FMP3_CHIP_DIR}/chip_start.S"
    "${RUNTIME_ROOT}/seam/seam_c6_entry.S")
target_link_libraries(seam_start PRIVATE seam_common)
target_include_directories(seam_start PRIVATE
    "${FMP3_CORE_ROOT}/kernel")
add_dependencies(seam_start generate_cfg_gen_files)

#
#  esp_run_init_array() for src/bridge/ArduinoSketchBridge.cpp. Chip
#  independent; the copy is the Xtensa runtime's seam/init_array.cpp. The
#  symbols it walks are defined by esp32c6_xip.ld (S1-5).
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
#  Sketch-independent stage: ${CMAKE_BINARY_DIR}/prebuilt (objs/, ld/,
#  link-manifest.json, objects.rsp), built with
#      cmake --build <dir> --target fmp3_prebuilt
#  and consumed by scripts/fmp3_link.py on the user's machine.
#
set(PREBUILT_DIRECTORY "${CMAKE_BINARY_DIR}/prebuilt")
set(PREBUILT_MANIFEST "${PREBUILT_DIRECTORY}/link-manifest.json")

#  Arduino-side objects the driver must force-link (same set as the Xtensa
#  minimal profile).
set(PREBUILT_ARDUINO_OBJECTS "<sketch>.cpp.o@@ArduinoSketchBridge.cpp.o")

set(PREBUILT_STAGE_DEPENDS fmp3 seam_objects seam_start seam_cxx)
set(PREBUILT_CONSUMER_OBJECTS
    "$<TARGET_OBJECTS:seam_objects>;"
    "$<TARGET_OBJECTS:seam_start>;"
    "$<TARGET_OBJECTS:seam_cxx>")
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
string(JOIN "@@" XIP_LINK_BASE_FLAGS ${XIP_LINK_BASE_FLAGS})
set(XIP_LINK_UFLAGS
    -Wl,--undefined=_kernel_mpfinib_table
    -Wl,--undefined=_kernel_tinib_table
    -Wl,--undefined=_kernel_cycinib_table
    -Wl,--undefined=_kernel_alminib_table)
string(JOIN "@@" XIP_LINK_UFLAGS ${XIP_LINK_UFLAGS})
set(XIP_LINK_LIBGROUP "-lstdc++@@-lm")
set(XIP_LINK_TAIL_FLAGS "-lc@@-lgcc")
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
        "-DOBJCOPY=${CMAKE_OBJCOPY}"
        "-DSTRIP_DEBUG=${A1_PREBUILT_STRIP_DEBUG}"
        "-DLINK_BASE_FLAGS=${XIP_LINK_BASE_FLAGS}"
        "-DLINK_UFLAGS=${XIP_LINK_UFLAGS}"
        "-DLINK_LIBGROUP=${XIP_LINK_LIBGROUP}"
        "-DLINK_TAIL_FLAGS=${XIP_LINK_TAIL_FLAGS}"
        "-DEXTRA_TSCRIPTS="
        "-DARDUINO_OBJECT_NAMES=${PREBUILT_ARDUINO_OBJECTS}"
        -P "${RUNTIME_ROOT}/cmake/prebuilt_stage_c6.cmake"
    DEPENDS
        #  Target names alone give order-only dependencies; list the objects
        #  too so a recompiled object re-stages (Xtensa runtime's note).
        ${PREBUILT_STAGE_DEPENDS}
        "$<TARGET_FILE:fmp3>"
        ${PREBUILT_CONSUMER_OBJECTS}
        "${RUNTIME_ROOT}/cmake/prebuilt_stage_c6.cmake"
        "${A1_XIP_LD}"
    COMMENT "Staging sketch-independent ${FMP3_RUNTIME_PROFILE} FMP3 artifacts (esp32c6)"
    VERBATIM)

add_custom_target(fmp3_prebuilt DEPENDS "${PREBUILT_MANIFEST}")
