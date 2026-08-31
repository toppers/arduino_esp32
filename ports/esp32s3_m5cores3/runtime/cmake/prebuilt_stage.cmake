#
#  スケッチに依存しないFMP3成果物をステージングする（cmake -P）
#
#  目的は「利用者のスケッチビルド時にFMP3をビルドしない」こと。cfgはprofile固定で
#  スケッチに依存しないため、カーネルとruntimeはリリース時に固めて配布できる。
#  スケッチ依存なのは最終リンクだけである。
#
#  出力（${STAGE_DIR}）:
#    objs/*.o            libfmp3.a の全メンバ＋消費側オブジェクト＋flash_cache_init.o
#    ld/<xip>.ld         XIPリンカスクリプト
#    link-manifest.json  リンクに必要な情報（順序・フラグ・要求オブジェクト）
#
#  ★機械依存パスはプレースホルダに置き換えて記録する。展開は利用者環境のドライバ側
#    （Arduinoのrecipe）が行う。
#      @SDK_LD_ROOT@       M5Stack core同梱 ld ディレクトリ（ROM ld置き場）
#      @SDK_LIBRARY_ROOT@  同 lib ディレクトリ（-L と -lsoc 等）
#      @SDK_PERIPHERALS_LD@ m5-unified が追加で -T する peripherals.ld
#
#  呼び出し例（CMakeLists.txt の add_custom_command から）:
#    cmake -DSTAGE_DIR=... -DAR=... -DGCC=... -DLIBFMP3=... -DCONSUMER_OBJS=...
#          -DFLASHCACHE_SRC=... -DPROFILE=minimal -DA1_CHIP=esp32s3
#          -DXIP_LD=... -DROMLD_ROOT=... -DSDK_LIBRARY_ROOT=... -DPERIPHERALS_LD=...
#          -DLINK_UFLAGS=... -DLINK_LIBGROUP=... -DEXTRA_TSCRIPTS=...
#          -DXIP_PADDR_RUNTIME=ON -DARDUINO_OBJECT_NAMES=...
#          -P cmake/prebuilt_stage.cmake
#

#  消費側オブジェクトはファイルで受ける（CMakeLists.txt側の注記を参照。
#  コマンドラインで渡すとWindowsのバッチ行の限界に触れる）。
#  -DCONSUMER_OBJS= での直接指定も、手で叩く場合のために残す。
if(DEFINED CONSUMER_OBJS_FILE AND NOT CONSUMER_OBJS_FILE STREQUAL "")
  if(NOT EXISTS "${CONSUMER_OBJS_FILE}")
    message(FATAL_ERROR
      "prebuilt_stage: CONSUMER_OBJS_FILE が無い: ${CONSUMER_OBJS_FILE}")
  endif()
  file(STRINGS "${CONSUMER_OBJS_FILE}" CONSUMER_OBJS)
  list(REMOVE_ITEM CONSUMER_OBJS "")
  if(CONSUMER_OBJS STREQUAL "")
    message(FATAL_ERROR
      "prebuilt_stage: CONSUMER_OBJS_FILE が空: ${CONSUMER_OBJS_FILE}")
  endif()
endif()

foreach(v STAGE_DIR AR GCC LIBFMP3 CONSUMER_OBJS FLASHCACHE_SRC PROFILE XIP_LD)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "prebuilt_stage: 必須引数 ${v} が未定義")
  endif()
endforeach()

if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
  set(A1_CHIP esp32s3)
endif()
if(NOT A1_CHIP STREQUAL "esp32s3")
  message(FATAL_ERROR "prebuilt_stage: 現状 esp32s3 のみ対応（指定=${A1_CHIP}）")
endif()
if(NOT XIP_PADDR_RUNTIME)
  #  2パス経路はPADDRをビルド時に決めるため、スケッチ非依存に固められない。
  message(FATAL_ERROR
    "prebuilt_stage: A1_XIP_PADDR_RUNTIME=ON が前提（2パス経路は事前ビルドできない）")
endif()

set(OBJDIR "${STAGE_DIR}/objs")
set(LDDIR  "${STAGE_DIR}/ld")
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${OBJDIR}")
file(MAKE_DIRECTORY "${LDDIR}")

#  ---- basename衝突の検出（xip_build.cmake と同じ理由でfail-closed） ----
set(_staged_names "")
set(_staged_srcs "")

function(stage_claim name src)
  list(FIND _staged_names "${name}" _idx)
  if(NOT _idx EQUAL -1)
    list(GET _staged_srcs ${_idx} _prev)
    message(FATAL_ERROR
      "prebuilt_stage: basename衝突: '${name}'\n  既存: ${_prev}\n  新規: ${src}\n"
      "1ディレクトリへ集める方式のため、どちらか一方しかリンクされない。"
      "消費側の列挙から不要な方を除くこと。")
  endif()
  list(APPEND _staged_names "${name}")
  list(APPEND _staged_srcs "${src}")
  set(_staged_names "${_staged_names}" PARENT_SCOPE)
  set(_staged_srcs "${_staged_srcs}" PARENT_SCOPE)
endfunction()

#  ---- (1) libfmp3.a を展開して <base>.o へ改名 ----
execute_process(COMMAND "${AR}" x "${LIBFMP3}"
                WORKING_DIRECTORY "${OBJDIR}"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "prebuilt_stage: ar x 失敗 (rc=${_rc})\n${_err}")
endif()

file(GLOB _members "${OBJDIR}/*.obj")
foreach(m ${_members})
  get_filename_component(_bn "${m}" NAME)
  string(REGEX REPLACE "\\.(c|cpp|S)\\.obj$" ".o" _nn "${_bn}")
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "prebuilt_stage: 予期しないメンバ名 ${_bn}")
  endif()
  stage_claim("${_nn}" "libfmp3.a(${_bn})")
  file(RENAME "${m}" "${OBJDIR}/${_nn}")
endforeach()

#  ---- (2) 消費側オブジェクト ----
foreach(o ${CONSUMER_OBJS})
  if(o STREQUAL "")
    continue()
  endif()
  get_filename_component(_bn "${o}" NAME)
  string(REGEX REPLACE "\\.(c|cpp|S)\\.(obj|o)$" ".o" _nn "${_bn}")
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "prebuilt_stage: 予期しない消費側obj名 ${_bn}")
  endif()
  stage_claim("${_nn}" "${o}")
  file(COPY_FILE "${o}" "${OBJDIR}/${_nn}")
endforeach()

#  ---- (3) flash_cache_init.o ----
#  実行時PADDR経路ではPADDR定数を焼き込まないので、ここで一度コンパイルして
#  そのまま配布できる（従来は2パスのたびに再コンパイルしていた）。
set(_fc_defs "")
if(XIP_PADDR_RUNTIME)
  list(APPEND _fc_defs -DTOPPERS_XIP_PADDR_RUNTIME)
endif()
execute_process(
  COMMAND "${GCC}" -c -o "${OBJDIR}/flash_cache_init.o"
          -O2 -Wall -g -mlongcalls ${_fc_defs} "${FLASHCACHE_SRC}"
  RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "prebuilt_stage: flash_cache_init.c のコンパイル失敗 (rc=${_rc})\n${_err}")
endif()
stage_claim("flash_cache_init.o" "${FLASHCACHE_SRC}")

if(EXISTS "${OBJDIR}/cfg1_out.o")
  message(FATAL_ERROR "prebuilt_stage: cfg1_out.o が混入（リンク対象外のはず）")
endif()

#  ---- (3a) デバッグ情報の削除 ----
#
#  Arduinoの利用者がFMP3ランタイム側をデバッガで追うことはまずないので、
#  配布物からDWARFを落とす（既定）。m5-unified profileは58MB→大幅に縮む。
#
#  `elf2image`はloadableセグメントしか見ないため、**`app_xip.bin`は変わらない**。
#  失うのは`fmp_xip.elf`のランタイム側デバッグ情報だけである
#  （スケッチ側のオブジェクトはArduinoが作るので影響しない）。
#  -DSTRIP_DEBUG=OFF で従来どおり残せる。
#
if(NOT DEFINED STRIP_DEBUG OR STRIP_DEBUG)
  #  CMAKE_OBJCOPY は空で来ることがあり、CMAKE_C_COMPILER もコマンド名だけで
  #  PATH解決に任せている構成がある（このportがそれ）。順に当たる。
  if(NOT DEFINED OBJCOPY OR NOT EXISTS "${OBJCOPY}")
    get_filename_component(_gcc_dir "${GCC}" DIRECTORY)
    get_filename_component(_gcc_name "${GCC}" NAME)
    string(REGEX REPLACE "gcc(\\.exe)?$" "objcopy\\1" _objcopy_name "${_gcc_name}")
    unset(OBJCOPY)
    unset(OBJCOPY CACHE)
    find_program(OBJCOPY NAMES "${_objcopy_name}" HINTS "${_gcc_dir}")
    if(NOT OBJCOPY)
      message(FATAL_ERROR
        "prebuilt_stage: ${_objcopy_name} が見つからない"
        "（-DOBJCOPY=<path> で指定するか、ツールチェーンをPATHへ入れる）")
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
      message(FATAL_ERROR "prebuilt_stage: strip 失敗 ${o} (rc=${_rc})\n${_err}")
    endif()
    file(SIZE "${o}" _sz)
    math(EXPR _size_after "${_size_after} + ${_sz}")
  endforeach()
  math(EXPR _before_kb "${_size_before} / 1024")
  math(EXPR _after_kb "${_size_after} / 1024")
  message(STATUS
    "prebuilt_stage: stripped debug info (${_before_kb} KB -> ${_after_kb} KB)")
  set(_debug_info "stripped")
else()
  set(_debug_info "full")
endif()

#  ---- (4) リンカスクリプト ----
get_filename_component(_xip_ld_name "${XIP_LD}" NAME)
file(COPY_FILE "${XIP_LD}" "${LDDIR}/${_xip_ld_name}")

#  ---- (5) リンク順序（xip_build.cmake と同じ byte-wise sort） ----
file(GLOB _objlist "${OBJDIR}/*.o")
list(FILTER _objlist EXCLUDE REGEX "cfg1_out\\.o$")
list(SORT _objlist)
if(_objlist STREQUAL "")
  message(FATAL_ERROR "prebuilt_stage: リンク対象が空")
endif()
set(_order "")
foreach(o ${_objlist})
  get_filename_component(_bn "${o}" NAME)
  list(APPEND _order "${_bn}")
endforeach()
list(LENGTH _order _order_count)

#  ---- (6) 機械依存パスをプレースホルダへ ----
#
#  ★分割してから置換する順序でなければならない。プレースホルダは末尾が '@' で
#    セパレータが '@@' なので、置換を先にすると '-L<root>@@-Wl,--start-group' が
#    '-L@SDK_LIBRARY_ROOT@' + '@@' = '@@@' となって誤分割する（2026-08-22に踏んだ）。
#
#  リンカが受け取る1引数ごとにプレースホルダへ置き換える。展開はドライバ側。
#      @SDK_LD_ROOT@        M5Stack core同梱 ld ディレクトリ
#      @SDK_LIBRARY_ROOT@   同 lib ディレクトリ
#      @SDK_PERIPHERALS_LD@ m5-unified が追加で -T する peripherals.ld
#      @STAGE@              このステージ自身（同梱アーカイブの -L に使う）
#
function(templatize_item raw out)
  set(_s "${raw}")
  if(DEFINED WPA_LIB_DIR AND NOT WPA_LIB_DIR STREQUAL "")
    string(REPLACE "${WPA_LIB_DIR}" "@STAGE@/lib" _s "${_s}")
  endif()
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

#  '@@' 連結を分割し、要素ごとに置換してJSON配列へ
function(to_json_array raw out)
  set(_items "")
  if(NOT raw STREQUAL "")
    string(REPLACE "@@" ";" _list "${raw}")
    foreach(i ${_list})
      if(NOT i STREQUAL "")
        templatize_item("${i}" _t)
        list(APPEND _items "    \"${_t}\"")
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

#  ---- (6a) ステージ同梱アーカイブ（wifi-connectのWPA2一式） ----
if(DEFINED WPA_LIB_DIR AND NOT WPA_LIB_DIR STREQUAL "")
  if(NOT IS_DIRECTORY "${WPA_LIB_DIR}")
    message(FATAL_ERROR "prebuilt_stage: WPA_LIB_DIR が無い: ${WPA_LIB_DIR}")
  endif()
  file(MAKE_DIRECTORY "${STAGE_DIR}/lib")
  file(GLOB _wpa_archives "${WPA_LIB_DIR}/*.a")
  if(_wpa_archives STREQUAL "")
    message(FATAL_ERROR "prebuilt_stage: ${WPA_LIB_DIR} に .a が無い")
  endif()
  foreach(a ${_wpa_archives})
    get_filename_component(_an "${a}" NAME)
    file(COPY_FILE "${a}" "${STAGE_DIR}/lib/${_an}")
  endforeach()
endif()

to_json_array("${LINK_UFLAGS}" _uflags_json)
to_json_array("${LINK_LIBGROUP}" _libgroup_json)
to_json_array("${EXTRA_TSCRIPTS}" _tscripts_json)

set(_romlds "esp32s3.rom.ld@@esp32s3.rom.api.ld@@esp32s3.rom.libc.ld@@esp32s3.rom.libgcc.ld@@esp32s3.rom.newlib.ld@@esp32s3.rom.version.ld")
to_json_array("${_romlds}" _romlds_json)

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
  \"schema\": 1,
  \"profile\": \"${PROFILE}\",
  \"chip\": \"${A1_CHIP}\",
  \"paddrMode\": \"runtime-mmu\",
  \"debugInfo\": \"${_debug_info}\",
  \"xipLinkerScript\": \"ld/${_xip_ld_name}\",
  \"romLinkerScripts\": ${_romlds_json},
  \"extraLinkerScripts\": ${_tscripts_json},
  \"linkUFlags\": ${_uflags_json},
  \"linkLibGroup\": ${_libgroup_json},
  \"flashMode\": \"dio\",
  \"flashFreq\": \"80m\",
  \"flashSize\": \"16MB\",
  \"objectCount\": ${_order_count},
  \"objectOrder\": [
${_order_json}
  ],
  \"requiredArduinoObjects\": ${_arduino_json}
}
")

#  ---- (7) response file（コマンド長対策。Stage 4の実測で必要と判断） ----
set(_rsp "")
foreach(o ${_order})
  string(APPEND _rsp "objs/${o}\n")
endforeach()
file(WRITE "${STAGE_DIR}/objects.rsp" "${_rsp}")

#
#  ---- (8) 重複した強いグローバル定義の監査----
#
#  最終リンクは常に `-Wl,--allow-multiple-definition` を付ける。
#  ⇒ **多重定義があってもリンクは黙って通り**、ld は先に現れた定義を採る
#    （順序はオブジェクト名の ordinal）。つまり
#    「リンクログに multiple definition が 0 件」は多重定義については無情報である。
#
#  とくに M5＋Wi-Fi の結合 profile は m5 側の重複を
#  `#ifndef M5_USE_ESP_SHIM` で落とす方式なので、**ガードを 1 本付け忘れても
#  ビルドは成功し、どちらが生き残るかはファイル名の綴り次第**になる。ここを塞ぐ。
#
#  fail-closed: nm が無い／.o が 0 個／nm が失敗した、はいずれも落とす
#  （検査が走らなかったことを合格にしない）。
#
if(NOT DEFINED DUPSYM_AUDIT OR DUPSYM_AUDIT)
  if(NOT DEFINED PYTHON_EXECUTABLE OR PYTHON_EXECUTABLE STREQUAL "")
    find_program(PYTHON_EXECUTABLE NAMES python3 python)
  endif()
  if(NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR
      "prebuilt_stage: 重複定義監査に python が必要（-DPYTHON_EXECUTABLE=<path>）。"
      "検査を飛ばして合格にはしない。-DDUPSYM_AUDIT=OFF で明示的に無効化できる")
  endif()
  get_filename_component(_gcc_dir2 "${GCC}" DIRECTORY)
  get_filename_component(_gcc_name2 "${GCC}" NAME)
  string(REGEX REPLACE "gcc(\\.exe)?$" "nm\\1" _nm_name "${_gcc_name2}")
  find_program(TARGET_NM NAMES "${_nm_name}" HINTS "${_gcc_dir2}")
  if(NOT TARGET_NM)
    message(FATAL_ERROR "prebuilt_stage: ${_nm_name} が見つからない（重複定義監査）")
  endif()
  #  .../ports/esp32s3_m5cores3/runtime/cmake から 4 段上がリポジトリ root。
  get_filename_component(_runtime_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
  get_filename_component(_port_root "${_runtime_root}" DIRECTORY)
  get_filename_component(_ports_root "${_port_root}" DIRECTORY)
  get_filename_component(_repo_root "${_ports_root}" DIRECTORY)
  if(NOT EXISTS "${_repo_root}/scripts/audit_duplicate_symbols.py")
    message(FATAL_ERROR
      "prebuilt_stage: 監査スクリプトが見つからない: "
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
    message(FATAL_ERROR "prebuilt_stage: ${_dup_err}")
  endif()
endif()

message(STATUS "prebuilt_stage: ${PROFILE} objects=${_order_count} -> ${STAGE_DIR}")
