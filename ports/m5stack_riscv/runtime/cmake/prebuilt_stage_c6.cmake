#  A cmake -P script has no policy settings and defaults to OLD. OLD CMP0053
#  re-expands `@VAR@` inside strings as variable references, which would turn
#  the `@SDK_LIBRARY_ROOT@` placeholder templatize_item writes back into the
#  build machine's absolute path, in the distributed link-manifest.json
#  (measured with cmake 3.30.0). Declare the same minimum as CMakeLists.txt.
cmake_minimum_required(VERSION 3.23)

#
#  Stage the sketch-independent FMP3 artifacts of the ESP32-C6 (cmake -P).
#
#  The RISC-V counterpart of ports/m5stack_xtensa/runtime/cmake/
#  prebuilt_stage.cmake, kept as a separate file so the Xtensa one stays
#  textually unchanged (it is under the X-check). Same purpose: cfg is fixed
#  per profile and does not depend on the sketch, so the kernel and the
#  runtime are frozen at release time and only the final link happens on the
#  user's machine.
#
#  Output (${STAGE_DIR}):
#    objs/*.o            every member of libfmp3.a plus the consumer objects
#    ld/<xip>.ld         the XIP linker script
#    link-manifest.json  schema 2 (scripts/fmp3_link.py "Manifest schemas")
#    objects.rsp         link order, ordinal (byte-wise) sort
#
#  What differs from the Xtensa script, and why:
#    - No flash_cache_init.o. The C6 image is linked at the virtual addresses
#      the bootloader's MMU mapping gives it (paddrMode "fixed-vma"): esptool
#      lays the segments out from the ELF section headers and there is no
#      runtime PADDR resolution to compile in.
#    - The manifest is schema 2 and carries linkBaseFlags / linkTailFlags:
#      the driver's schema 1 literals are -nostdlib -mlongcalls and -lgcc -lc,
#      and -mlongcalls is not a RISC-V option.
#    - The ROM linker script names come from the caller (ROM_LDS), not from a
#      per-chip table here.
#    - flashSize is 4MB: the M5NanoC6 has a 4 MB flash (M5Stack boards.txt
#      m5stack_nano_c6.build.flash_size).
#
#  Machine-specific paths are recorded as placeholders and expanded by the
#  driver on the user's machine:
#      @SDK_LD_ROOT@        the M5Stack core's ld/ directory (ROM scripts)
#      @SDK_LIBRARY_ROOT@   its lib/ directory
#      @SDK_PERIPHERALS_LD@ <chip>.peripherals.ld when a profile adds it
#      @STAGE@              this stage (for archives shipped inside it; every
#                           .a of the STAGE_LIB_DIRS directories is copied to
#                           lib/, and -L<that directory> becomes -L@STAGE@/lib)
#
#  Called from CMakeLists.txt with
#    cmake -DSTAGE_DIR=... -DAR=... -DGCC=... -DLIBFMP3=... -DCONSUMER_OBJS_FILE=...
#          -DPROFILE=minimal -DA1_CHIP=esp32c6 -DXIP_LD=... -DROMLD_ROOT=...
#          -DROM_LDS=a.ld@@b.ld... -DSDK_LIBRARY_ROOT=... -DSTAGE_LIB_DIRS=dir@@dir...
#          -DOBJCOPY=... -DSTRIP_DEBUG=ON
#          -DLINK_BASE_FLAGS=... -DLINK_UFLAGS=... -DLINK_LIBGROUP=...
#          -DLINK_TAIL_FLAGS=... -DEXTRA_TSCRIPTS=... -DARDUINO_OBJECT_NAMES=...
#          -P cmake/prebuilt_stage_c6.cmake
#

#  The consumer objects arrive in a file (see CMakeLists.txt: the command
#  line form hits the Windows batch line limit). -DCONSUMER_OBJS= still works
#  for a manual run.
if(DEFINED CONSUMER_OBJS_FILE AND NOT CONSUMER_OBJS_FILE STREQUAL "")
  if(NOT EXISTS "${CONSUMER_OBJS_FILE}")
    message(FATAL_ERROR
      "prebuilt_stage_c6: CONSUMER_OBJS_FILE does not exist: ${CONSUMER_OBJS_FILE}")
  endif()
  file(STRINGS "${CONSUMER_OBJS_FILE}" CONSUMER_OBJS)
  list(REMOVE_ITEM CONSUMER_OBJS "")
  if(CONSUMER_OBJS STREQUAL "")
    message(FATAL_ERROR
      "prebuilt_stage_c6: CONSUMER_OBJS_FILE is empty: ${CONSUMER_OBJS_FILE}")
  endif()
endif()

foreach(v STAGE_DIR AR GCC LIBFMP3 CONSUMER_OBJS PROFILE XIP_LD ROMLD_ROOT ROM_LDS
          LINK_BASE_FLAGS LINK_TAIL_FLAGS)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "prebuilt_stage_c6: required argument ${v} is undefined")
  endif()
endforeach()

if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
  set(A1_CHIP esp32c6)
endif()
if(NOT A1_CHIP STREQUAL "esp32c6")
  message(FATAL_ERROR
    "prebuilt_stage_c6: esp32c6 only (got ${A1_CHIP}); the Xtensa chips use prebuilt_stage.cmake")
endif()
if(LINK_BASE_FLAGS STREQUAL "")
  #  A schema 2 manifest requires the list; an empty one would link with no
  #  -march/-mabi and no -nostdlib, which is never what a stage means.
  message(FATAL_ERROR "prebuilt_stage_c6: LINK_BASE_FLAGS is empty")
endif()

set(OBJDIR "${STAGE_DIR}/objs")
set(LDDIR  "${STAGE_DIR}/ld")
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${OBJDIR}")
file(MAKE_DIRECTORY "${LDDIR}")

#  ---- basename collisions are fatal (one directory holds every object) ----
set(_staged_names "")
set(_staged_srcs "")

function(stage_claim name src)
  list(FIND _staged_names "${name}" _idx)
  if(NOT _idx EQUAL -1)
    list(GET _staged_srcs ${_idx} _prev)
    message(FATAL_ERROR
      "prebuilt_stage_c6: basename collision: '${name}'\n  existing: ${_prev}\n  new: ${src}\n"
      "Only one of them would be linked. Remove the one that is not needed "
      "from the consumer object list.")
  endif()
  list(APPEND _staged_names "${name}")
  list(APPEND _staged_srcs "${src}")
  set(_staged_names "${_staged_names}" PARENT_SCOPE)
  set(_staged_srcs "${_staged_srcs}" PARENT_SCOPE)
endfunction()

#  ---- (1) extract libfmp3.a, rename members to <base>.o ----
execute_process(COMMAND "${AR}" x "${LIBFMP3}"
                WORKING_DIRECTORY "${OBJDIR}"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "prebuilt_stage_c6: ar x failed (rc=${_rc})\n${_err}")
endif()

file(GLOB _members "${OBJDIR}/*.obj")
foreach(m ${_members})
  get_filename_component(_bn "${m}" NAME)
  string(REGEX REPLACE "\\.(c|cpp|S)\\.obj$" ".o" _nn "${_bn}")
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "prebuilt_stage_c6: unexpected member name ${_bn}")
  endif()
  stage_claim("${_nn}" "libfmp3.a(${_bn})")
  file(RENAME "${m}" "${OBJDIR}/${_nn}")
endforeach()

#  ---- (2) consumer objects ----
foreach(o ${CONSUMER_OBJS})
  if(o STREQUAL "")
    continue()
  endif()
  get_filename_component(_bn "${o}" NAME)
  string(REGEX REPLACE "\\.(c|cpp|S)\\.(obj|o)$" ".o" _nn "${_bn}")
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "prebuilt_stage_c6: unexpected consumer object name ${_bn}")
  endif()
  stage_claim("${_nn}" "${o}")
  file(COPY_FILE "${o}" "${OBJDIR}/${_nn}")
endforeach()

if(EXISTS "${OBJDIR}/cfg1_out.o")
  message(FATAL_ERROR "prebuilt_stage_c6: cfg1_out.o was staged (it must not be linked)")
endif()

#  ---- (3) strip debug info ----
#
#  Same policy as the Xtensa stages: users do not debug the runtime side, so
#  DWARF is dropped from the distributed objects by default. esptool's
#  elf2image only looks at loadable sections, so the flash image does not
#  change. -DSTRIP_DEBUG=OFF keeps it.
#
if(NOT DEFINED STRIP_DEBUG OR STRIP_DEBUG)
  #  CMAKE_OBJCOPY may arrive empty, and CMAKE_C_COMPILER is a bare name
  #  resolved through PATH in this port; derive objcopy next to gcc. Two
  #  passes instead of a back-reference: CMake validates the replacement
  #  against the groups that actually matched, and an optional (\\.exe)?
  #  group makes \\1 out of range on hosts without the suffix (cmake 3.30.0).
  if(NOT DEFINED OBJCOPY OR NOT EXISTS "${OBJCOPY}")
    get_filename_component(_gcc_dir "${GCC}" DIRECTORY)
    get_filename_component(_gcc_name "${GCC}" NAME)
    set(_objcopy_name "${_gcc_name}")
    string(REGEX REPLACE "gcc$" "objcopy" _objcopy_name "${_objcopy_name}")
    string(REGEX REPLACE "gcc\\.exe$" "objcopy.exe" _objcopy_name "${_objcopy_name}")
    unset(OBJCOPY)
    unset(OBJCOPY CACHE)
    find_program(OBJCOPY NAMES "${_objcopy_name}" HINTS "${_gcc_dir}")
    if(NOT OBJCOPY)
      message(FATAL_ERROR
        "prebuilt_stage_c6: ${_objcopy_name} was not found "
        "(pass -DOBJCOPY=<path> or put the toolchain on PATH)")
    endif()
  endif()

  set(_size_before 0)
  set(_size_after 0)
  file(GLOB _to_strip "${OBJDIR}/*.o")
  foreach(o ${_to_strip})
    file(SIZE "${o}" _sz)
    math(EXPR _size_before "${_size_before} + ${_sz}")
    execute_process(COMMAND "${OBJCOPY}" --strip-debug "${o}"
                    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "prebuilt_stage_c6: strip failed ${o} (rc=${_rc})\n${_err}")
    endif()
    file(SIZE "${o}" _sz)
    math(EXPR _size_after "${_size_after} + ${_sz}")
  endforeach()
  math(EXPR _before_kb "${_size_before} / 1024")
  math(EXPR _after_kb "${_size_after} / 1024")
  message(STATUS
    "prebuilt_stage_c6: stripped debug info (${_before_kb} KB -> ${_after_kb} KB)")
  set(_debug_info "stripped")
else()
  set(_debug_info "full")
endif()

#  ---- (4) linker script ----
get_filename_component(_xip_ld_name "${XIP_LD}" NAME)
file(COPY_FILE "${XIP_LD}" "${LDDIR}/${_xip_ld_name}")

#  ---- (5) link order (byte-wise sort, the driver sorts the same way) ----
file(GLOB _objlist "${OBJDIR}/*.o")
list(FILTER _objlist EXCLUDE REGEX "cfg1_out\\.o$")
list(SORT _objlist)
if(_objlist STREQUAL "")
  message(FATAL_ERROR "prebuilt_stage_c6: nothing to link")
endif()
set(_order "")
foreach(o ${_objlist})
  get_filename_component(_bn "${o}" NAME)
  list(APPEND _order "${_bn}")
endforeach()
list(LENGTH _order _order_count)

#  ---- (6) machine-specific paths to placeholders ----
#
#  Split on '@@' first, then replace: a placeholder ends in '@' and the
#  separator is '@@', so replacing first turns '-L<root>@@-Wl,...' into
#  '@SDK_LIBRARY_ROOT@' + '@@' = '@@@' and the split goes wrong.
#
#  The stage-shipped archive directories are a '@@' list too (wifi-connect
#  has the WPA2 set and lwIP in two directories); each is replaced.
set(_stage_lib_dirs "")
if(DEFINED STAGE_LIB_DIRS AND NOT STAGE_LIB_DIRS STREQUAL "")
  string(REPLACE "@@" ";" _stage_lib_dirs "${STAGE_LIB_DIRS}")
  list(REMOVE_ITEM _stage_lib_dirs "")
endif()

function(templatize_item raw out)
  set(_s "${raw}")
  foreach(_d ${_stage_lib_dirs})
    string(REPLACE "${_d}" "@STAGE@/lib" _s "${_s}")
  endforeach()
  if(DEFINED PERIPHERALS_LD AND NOT PERIPHERALS_LD STREQUAL "")
    string(REPLACE "${PERIPHERALS_LD}" "@SDK_PERIPHERALS_LD@" _s "${_s}")
  endif()
  if(DEFINED SDK_LIBRARY_ROOT AND NOT SDK_LIBRARY_ROOT STREQUAL "")
    string(REPLACE "${SDK_LIBRARY_ROOT}" "@SDK_LIBRARY_ROOT@" _s "${_s}")
  endif()
  if(DEFINED ROMLD_ROOT AND NOT ROMLD_ROOT STREQUAL "")
    string(REPLACE "${ROMLD_ROOT}" "@SDK_LD_ROOT@" _s "${_s}")
  endif()
  set(${out} "${_s}" PARENT_SCOPE)
endfunction()

#  '@@'-joined list -> JSON array, one placeholder pass per item. An item
#  that comes out equal to the one before it is dropped: two -L entries for
#  two STAGE_LIB_DIRS directories both become -L@STAGE@/lib.
function(to_json_array raw out)
  set(_items "")
  set(_prev "")
  if(NOT raw STREQUAL "")
    string(REPLACE "@@" ";" _list "${raw}")
    foreach(i ${_list})
      if(NOT i STREQUAL "")
        templatize_item("${i}" _t)
        if(NOT _t STREQUAL _prev)
          list(APPEND _items "    \"${_t}\"")
        endif()
        set(_prev "${_t}")
      endif()
    endforeach()
  endif()
  if(_items STREQUAL "")
    set(${out} "[]" PARENT_SCOPE)
  else()
    string(REPLACE ";" ",\n" _joined "${_items}")
    set(${out} "[\n${_joined}\n  ]" PARENT_SCOPE)
  endif()
endfunction()

#  ---- (6a) archives shipped inside the stage (none for minimal) ----
#
#  Every directory's .a files land in one lib/; a basename that two
#  directories both hold would be one archive on the link line for two
#  different files, so that is fatal.
#
set(_staged_archives "")
foreach(_d ${_stage_lib_dirs})
  if(NOT IS_DIRECTORY "${_d}")
    message(FATAL_ERROR "prebuilt_stage_c6: STAGE_LIB_DIRS entry does not exist: ${_d}")
  endif()
  file(MAKE_DIRECTORY "${STAGE_DIR}/lib")
  file(GLOB _dir_archives "${_d}/*.a")
  if(_dir_archives STREQUAL "")
    message(FATAL_ERROR "prebuilt_stage_c6: no .a in ${_d}")
  endif()
  foreach(a ${_dir_archives})
    get_filename_component(_an "${a}" NAME)
    if("${_an}" IN_LIST _staged_archives)
      message(FATAL_ERROR
        "prebuilt_stage_c6: archive basename collision in lib/: ${_an} (from ${_d})")
    endif()
    list(APPEND _staged_archives "${_an}")
    file(COPY_FILE "${a}" "${STAGE_DIR}/lib/${_an}")
  endforeach()
endforeach()

#  ---- ROM linker scripts: names relative to the SDK ld/, all must exist ----
string(REPLACE "@@" ";" _romld_list "${ROM_LDS}")
list(REMOVE_ITEM _romld_list "")
if(_romld_list STREQUAL "")
  message(FATAL_ERROR "prebuilt_stage_c6: ROM_LDS is empty")
endif()
foreach(l ${_romld_list})
  if(NOT EXISTS "${ROMLD_ROOT}/${l}")
    message(FATAL_ERROR "prebuilt_stage_c6: ROM linker script is missing: ${ROMLD_ROOT}/${l}")
  endif()
endforeach()

#  The board's flash, not the chip's maximum: M5NanoC6 = 4 MB.
set(_flash_size "4MB")

to_json_array("${LINK_BASE_FLAGS}" _base_json)
to_json_array("${LINK_UFLAGS}" _uflags_json)
to_json_array("${LINK_LIBGROUP}" _libgroup_json)
to_json_array("${LINK_TAIL_FLAGS}" _tail_json)
to_json_array("${EXTRA_TSCRIPTS}" _tscripts_json)
to_json_array("${ROM_LDS}" _romlds_json)

set(_order_items "")
foreach(o ${_order})
  list(APPEND _order_items "    \"${o}\"")
endforeach()
string(REPLACE ";" ",\n" _order_json "${_order_items}")

set(_arduino_items "")
if(DEFINED ARDUINO_OBJECT_NAMES AND NOT ARDUINO_OBJECT_NAMES STREQUAL "")
  string(REPLACE "@@" ";" _ao "${ARDUINO_OBJECT_NAMES}")
  foreach(a ${_ao})
    if(NOT a STREQUAL "")
      list(APPEND _arduino_items "    \"${a}\"")
    endif()
  endforeach()
endif()
if(_arduino_items STREQUAL "")
  set(_arduino_json "[]")
else()
  string(REPLACE ";" ",\n" _aj "${_arduino_items}")
  set(_arduino_json "[\n${_aj}\n  ]")
endif()

file(WRITE "${STAGE_DIR}/link-manifest.json"
"{
  \"schema\": 2,
  \"profile\": \"${PROFILE}\",
  \"chip\": \"${A1_CHIP}\",
  \"paddrMode\": \"fixed-vma\",
  \"debugInfo\": \"${_debug_info}\",
  \"xipLinkerScript\": \"ld/${_xip_ld_name}\",
  \"romLinkerScripts\": ${_romlds_json},
  \"extraLinkerScripts\": ${_tscripts_json},
  \"linkBaseFlags\": ${_base_json},
  \"linkUFlags\": ${_uflags_json},
  \"linkLibGroup\": ${_libgroup_json},
  \"linkTailFlags\": ${_tail_json},
  \"flashMode\": \"dio\",
  \"flashFreq\": \"80m\",
  \"flashSize\": \"${_flash_size}\",
  \"objectCount\": ${_order_count},
  \"objectOrder\": [
${_order_json}
  ],
  \"requiredArduinoObjects\": ${_arduino_json}
}
")

#  ---- (7) response file ----
set(_rsp "")
foreach(o ${_order})
  string(APPEND _rsp "objs/${o}\n")
endforeach()
file(WRITE "${STAGE_DIR}/objects.rsp" "${_rsp}")

#
#  ---- (8) duplicate strong global definitions ----
#
#  The final link always carries -Wl,--allow-multiple-definition, so a
#  duplicate links silently and the survivor is whichever object sorts
#  first. "0 multiple definition messages" says nothing. Fail closed: no nm,
#  no objects, or a failing nm all stop the stage (an audit that did not run
#  is not a pass). Same script and allowlist as the Xtensa stages.
#
if(NOT DEFINED DUPSYM_AUDIT OR DUPSYM_AUDIT)
  if(NOT DEFINED PYTHON_EXECUTABLE OR PYTHON_EXECUTABLE STREQUAL "")
    find_program(PYTHON_EXECUTABLE NAMES python3 python)
  endif()
  if(NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR
      "prebuilt_stage_c6: the duplicate-symbol audit needs python "
      "(-DPYTHON_EXECUTABLE=<path>); it is not skipped. -DDUPSYM_AUDIT=OFF disables it explicitly")
  endif()
  get_filename_component(_gcc_dir2 "${GCC}" DIRECTORY)
  get_filename_component(_gcc_name2 "${GCC}" NAME)
  set(_nm_name "${_gcc_name2}")
  string(REGEX REPLACE "gcc$" "nm" _nm_name "${_nm_name}")
  string(REGEX REPLACE "gcc\\.exe$" "nm.exe" _nm_name "${_nm_name}")
  find_program(TARGET_NM NAMES "${_nm_name}" HINTS "${_gcc_dir2}")
  if(NOT TARGET_NM)
    message(FATAL_ERROR "prebuilt_stage_c6: ${_nm_name} was not found (duplicate-symbol audit)")
  endif()
  #  .../ports/m5stack_riscv/runtime/cmake -> four levels up is the repo root.
  get_filename_component(_runtime_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
  get_filename_component(_port_root "${_runtime_root}" DIRECTORY)
  get_filename_component(_ports_root "${_port_root}" DIRECTORY)
  get_filename_component(_repo_root "${_ports_root}" DIRECTORY)
  if(NOT EXISTS "${_repo_root}/scripts/audit_duplicate_symbols.py")
    message(FATAL_ERROR
      "prebuilt_stage_c6: audit script was not found: "
      "${_repo_root}/scripts/audit_duplicate_symbols.py")
  endif()
  execute_process(
    COMMAND "${PYTHON_EXECUTABLE}"
            "${_repo_root}/scripts/audit_duplicate_symbols.py"
            --nm "${TARGET_NM}" --objects "${OBJDIR}"
            --allow "${_repo_root}/packaging/duplicate-symbol-allowlist.txt"
            --label "${PROFILE}"
    RESULT_VARIABLE _dup_rc OUTPUT_VARIABLE _dup_out ERROR_VARIABLE _dup_err)
  if(NOT _dup_out STREQUAL "")
    string(STRIP "${_dup_out}" _dup_out)
    message(STATUS "${_dup_out}")
  endif()
  if(NOT _dup_rc EQUAL 0)
    message(FATAL_ERROR "prebuilt_stage_c6: ${_dup_err}")
  endif()
endif()

message(STATUS "prebuilt_stage_c6: ${PROFILE} objects=${_order_count} -> ${STAGE_DIR}")
