#
#  A1 段階3+4: seam 最終ELF の XIP 2パス PADDR 自己整合リンク（cmake -P ドライバ）
#
#  classic `esp/boot/build_seam_s3_smp_esp32s3.sh` の [D][E][F] を CMake から
#  そのまま写す。目的は app_xip.bin を classic golden とバイト同一に出すこと。
#
#  呼び出し（repo-root CMakeLists.txt の add_custom_command から）:
#    cmake -DGCC=<xtensa gcc> -DAR=<xtensa gcc-ar>
#          (-DPY=<idf5.5 python> | -DESPTOOL=<standalone esptool>)
#          -DREPO=<repo root>
#          -DSTAGE=<build/xip 出力ディレクトリ>
#          -DLIBFMP3=<libfmp3.a のパス>
#          -DCONSUMER_OBJS=<seam_objs+seam_start の .obj を ; 連結したリスト>
#          -DFLASHCACHE_SRC=<esp/boot/flash_cache_init.c>
#          [-DA1_POISON_PADDR=<hex offset>]   # positive control 用（pass2 PADDR を意図的にずらす）
#          [-DA1_POISON_BANNER=1]             # 予備（未使用）
#          -P cmake/a1_xip_build.cmake
#
#  ★リンク対象オブジェクトの順序は classic の `ls objs/*.o | grep -v cfg1_out`
#    （en_US.UTF-8 collation）に一致させる必要がある。CMake の file(GLOB) は
#    byte-wise sort（C locale 相当）で syslog.o/sys_manage.o の前後が入れ替わり
#    リンク順＝出力バイトが変わるため、bash の ls をそのまま使う。
#

#  ---- 引数チェック ----
foreach(v GCC AR STAGE LIBFMP3 CONSUMER_OBJS FLASHCACHE_SRC XIP_LD ROMLD_ROOT)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "a1_xip_build: 必須引数 ${v} が未定義")
  endif()
endforeach()

if(DEFINED PY AND DEFINED ESPTOOL)
  message(FATAL_ERROR "a1_xip_build: PY と ESPTOOL は同時に指定できない")
elseif(DEFINED PY)
  set(A1_ESPTOOL_CMD "${PY}" -m esptool)
elseif(DEFINED ESPTOOL)
  set(A1_ESPTOOL_CMD "${ESPTOOL}")
else()
  message(FATAL_ERROR "a1_xip_build: PY または ESPTOOL の指定が必要")
endif()

#  ---- チップ依存パラメタ（A1_CHIP で切替。既定 esp32s3＝従来動作） ----
if(NOT DEFINED A1_CHIP)
  set(A1_CHIP esp32s3)
endif()
if(A1_CHIP STREQUAL "esp32s3")
  set(A1_XIP_LD  "${XIP_LD}")
  set(A1_ROMLDS  esp32s3.rom.ld esp32s3.rom.api.ld esp32s3.rom.libc.ld
                 esp32s3.rom.libgcc.ld esp32s3.rom.newlib.ld esp32s3.rom.version.ld)
  set(A1_FLASH_FREQ 80m)
  set(A1_FLASH_SIZE 16MB)
  set(A1_TWO_PASS   ON)   # 2パス PADDR 自己整合（flash_cache_init.o あり）
elseif(A1_CHIP STREQUAL "esp32")
  #  ★呼び出し側の -DXIP_LD を使う（esp32s3 分岐と同じ）。
  #  ここは外部 fmp3_esp_idf ツリーの配置を前提に
  #      "${REPO}/fmp3/target/esp32_devkitc_gcc/esp32_xip.ld"
  #  を組み立てていた。runtime を vendored にした際に取り残され、しかも
  #  現在の CMakeLists は -DREPO を渡さないので ${REPO} は空になり、
  #      cannot open linker script file /fmp3/target/esp32_devkitc_gcc/esp32_xip.ld
  #  で必ず失敗していた。ステージ生成（fmp3_prebuilt）は画像をリンクしない
  #  ので露出せず、LX6 のスケッチリンク経路が初めて走ったときに出た。
  #  リンカスクリプト名は呼び出し側が profile ごとに決めている
  #  （CMakeLists.txt の A1_XIP_LD_NAME: esp32_xip_m5_wifi.ld / esp32_xip_btc.ld）。
  set(A1_XIP_LD  "${XIP_LD}")
  set(A1_ROMLDS  esp32.rom.ld esp32.rom.api.ld esp32.rom.libc-funcs.ld
                 esp32.rom.libgcc.ld esp32.rom.newlib-data.ld esp32.rom.newlib-nano.ld)
  set(A1_FLASH_FREQ 40m)
  set(A1_FLASH_SIZE 4MB)
  set(A1_TWO_PASS   OFF)  # 単パス（esp32_xip.ld が IROM/DROM を固定・flash_cache_init.o 無し）
else()
  message(FATAL_ERROR "a1_xip_build: A1_CHIP は esp32s3 か esp32 のみ（指定=${A1_CHIP}）")
endif()

#  ---- 段階6c: ld 上書き（variant 依存）----
#  LX6-wifi は esp32_xip_wifi.ld（フルDRAM版）＋ rom ld の末尾が
#  esp32.rom.newlib-nano.blexip.ld（.ld でなく .blexip.ld）。S3-BLE は標準 rom ld に
#  bt_funcs.ld / peripherals.ld を追加（下の A1_EXTRA_TSCRIPTS）。
#    A1_XIP_LD_OVERRIDE   : xip.ld のフルパス（未指定なら chip 既定）
#    A1_ROMLDS_OVERRIDE   : rom ld 名の '@@' 連結（未指定なら chip 既定）
#    A1_EXTRA_TSCRIPTS    : rom ld の後に足す -Wl,-T 対象の '@@' 連結
#                           （esp/ld で解決される名前でもフルパスでも可）
if(DEFINED A1_XIP_LD_OVERRIDE AND NOT A1_XIP_LD_OVERRIDE STREQUAL "")
  set(A1_XIP_LD "${A1_XIP_LD_OVERRIDE}")
endif()
if(DEFINED A1_ROMLDS_OVERRIDE AND NOT A1_ROMLDS_OVERRIDE STREQUAL "")
  string(REPLACE "@@" ";" A1_ROMLDS "${A1_ROMLDS_OVERRIDE}")
endif()
set(A1_EXTRA_T "")
if(DEFINED A1_EXTRA_TSCRIPTS AND NOT A1_EXTRA_TSCRIPTS STREQUAL "")
  string(REPLACE "@@" ";" A1_EXTRA_T "${A1_EXTRA_TSCRIPTS}")
endif()

#  positive control 用: elf2image の flash_freq を差し替える（イメージヘッダの
#  バイトが変わる＝比較ハーネスが always-pass でないことの実演。LX6 で使う）。
if(DEFINED A1_POISON_FREQ)
  message(STATUS "a1_xip_build: ★POISON 有効 flash_freq ${A1_FLASH_FREQ} -> ${A1_POISON_FREQ}")
  set(A1_FLASH_FREQ "${A1_POISON_FREQ}")
endif()

set(OBJDIR "${STAGE}/objs")

#  execute_process ラッパ（rc!=0 で FATAL、ログ採取）
function(run LABEL)
  cmake_parse_arguments(A "" "WORKDIR;OUTVAR;LOGFILE" "CMD" ${ARGN})
  if(NOT A_WORKDIR)
    set(A_WORKDIR "${STAGE}")
  endif()
  execute_process(
    COMMAND ${A_CMD}
    WORKING_DIRECTORY "${A_WORKDIR}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    RESULT_VARIABLE _rc)
  if(A_LOGFILE)
    file(WRITE "${A_LOGFILE}" "${_out}${_err}")
  endif()
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "a1_xip_build: ${LABEL} 失敗 (rc=${_rc})\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  if(A_OUTVAR)
    set(${A_OUTVAR} "${_out}" PARENT_SCOPE)
  endif()
endfunction()

#  ---- ステージング: 全 .o を objs/ に classic と同じ basename で並べる ----
file(REMOVE_RECURSE "${OBJDIR}")
file(MAKE_DIRECTORY "${OBJDIR}")

#  (1) libfmp3.a を展開（メンバ名は <base>.c.obj / <base>.S.obj）→ <base>.o に改名
run("ar x libfmp3.a"
    WORKDIR "${OBJDIR}"
    CMD "${AR}" x "${LIBFMP3}")
#  ★fail-closed: basename 衝突は「黙って上書き」＝リンク対象が glob 順の偶然で
#    決まる（M5GFX の Bus_Parallel8.cpp のように同名 TU が複数ディレクトリに実在
#    する）。どちらが生き残るかが CMake 内部の列挙順に依存するのは検証不能なので、
#    衝突を検出したら必ず落とす。回避は「消費側の列挙を衝突しない集合に絞る」
#    （CMakeLists.txt の m5 分岐参照）であって、上書きに任せることではない。
set(_staged_names "")   # 既に配置済みの <base>.o
set(_staged_srcs  "")   # その供給元（診断メッセージ用・_staged_names と同順）

function(a1_stage_claim name src)
  list(FIND _staged_names "${name}" _idx)
  if(NOT _idx EQUAL -1)
    list(GET _staged_srcs ${_idx} _prev)
    message(FATAL_ERROR
      "a1_xip_build: ステージング basename 衝突: '${name}'\n"
      "  既存: ${_prev}\n"
      "  新規: ${src}\n"
      "同名オブジェクトを 1 ディレクトリへ集める方式のため、どちらか一方しか"
      "リンクされない（従来は黙って上書きしていた）。消費側のソース列挙から"
      "不要な方を除外すること。")
  endif()
  list(APPEND _staged_names "${name}")
  list(APPEND _staged_srcs  "${src}")
  set(_staged_names "${_staged_names}" PARENT_SCOPE)
  set(_staged_srcs  "${_staged_srcs}"  PARENT_SCOPE)
endfunction()

file(GLOB _members "${OBJDIR}/*.obj")
foreach(m ${_members})
  get_filename_component(_bn "${m}" NAME)
  string(REGEX REPLACE "\\.(c|cpp|S)\\.obj$" ".o" _nn "${_bn}")
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "a1_xip_build: 予期しないメンバ名 ${_bn}")
  endif()
  a1_stage_claim("${_nn}" "libfmp3.a(${_bn})")
  file(RENAME "${m}" "${OBJDIR}/${_nn}")
endforeach()

#  (2) 消費側オブジェクト（seam_objs + seam_start [+ m5 の C++ obj]）をコピー・改名
foreach(o ${CONSUMER_OBJS})
  get_filename_component(_bn "${o}" NAME)
  if(_bn MATCHES "\\.(c|cpp|S)\\.obj$")
    string(REGEX REPLACE "\\.(c|cpp|S)\\.obj$" ".o" _nn "${_bn}")
  elseif(_bn MATCHES "\\.(c|cpp|S)\\.o$")
    string(REGEX REPLACE "\\.(c|cpp|S)\\.o$" ".o" _nn "${_bn}")
  else()
    set(_nn "${_bn}")
  endif()
  if(_nn STREQUAL _bn)
    message(FATAL_ERROR "a1_xip_build: 予期しない、または衝突し得る消費側obj名 ${_bn}")
  endif()
  a1_stage_claim("${_nn}" "${o}")
  file(COPY_FILE "${o}" "${OBJDIR}/${_nn}")
endforeach()

#  (3) cfg1_out が紛れていないことを保証（classic は grep -v cfg1_out で除外）
if(EXISTS "${OBJDIR}/cfg1_out.o")
  message(FATAL_ERROR "a1_xip_build: cfg1_out.o がステージングに存在（リンク対象外のはず）")
endif()

#  ---- link_xip: classic の link_xip() をそのまま ----
#  OBJS2 = `ls objs/*.o | grep -v cfg1_out`（en_US.UTF-8 collation）を bash で取得。
set(ROMLD_FLAGS "")
foreach(l ${A1_ROMLDS})
  list(APPEND ROMLD_FLAGS "-Wl,-T,${l}")
endforeach()
#  variant 追加の -T（S3-BLE の bt_funcs.ld / peripherals.ld 等）。rom ld の直後に置く。
foreach(l ${A1_EXTRA_T})
  list(APPEND ROMLD_FLAGS "-Wl,-T,${l}")
endforeach()

#  wifi バリアント用リンク追加引数（blob group リンク・-u・-L）。
#  A1_LINK_UFLAGS = -Wl,-u,... 群（rom ld の後、-Map の前＝classic 位置）
#  A1_LINK_LIBGROUP = -L.../-Wl,--start-group ... --end-group 群（$OBJS2 の後、-lgcc の前）
#  いずれも ';' 連結で受け取り、未定義なら空（smp/lx6 は従来どおり）。
#  ★'@@' 区切りで受け取り ';'（CMake リスト）へ戻す（CMakeLists 側コメント参照）。
set(A1_UFLAGS "")
if(DEFINED A1_LINK_UFLAGS)
  string(REPLACE "@@" ";" A1_UFLAGS "${A1_LINK_UFLAGS}")
endif()
set(A1_LIBGROUP "")
if(DEFINED A1_LINK_LIBGROUP)
  string(REPLACE "@@" ";" A1_LIBGROUP "${A1_LINK_LIBGROUP}")
endif()

function(link_xip OUTELF)
  # Release package must not depend on Git Bash. CMake's byte-wise sort gives
  # a deterministic order on every supported Windows host.
  file(GLOB _objlist "${OBJDIR}/*.o")
  list(FILTER _objlist EXCLUDE REGEX "cfg1_out\\.o$")
  list(SORT _objlist)
  if(_objlist STREQUAL "")
    message(FATAL_ERROR "a1_xip_build: link object list is empty")
  endif()
  string(REGEX REPLACE "\\.elf$" ".map" _map "${OUTELF}")
  #  classic の link_xip() を写す。wifi は -u 群（rom ld 後）と blob group（obj 後）を
  #  挟む。空変数を CMD に混ぜると空引数 "" が gcc に渡り得るので、リストを段階的に
  #  組んでから run へ渡す（空要素は入れない）。
  set(_cmd ${GCC} -nostdlib -mlongcalls -Wl,--gc-sections
           -Wl,--allow-multiple-definition
           -Wl,-T,${A1_XIP_LD}
           -L${ROMLD_ROOT} ${ROMLD_FLAGS})
  if(NOT A1_UFLAGS STREQUAL "")
    list(APPEND _cmd ${A1_UFLAGS})
  endif()
  list(APPEND _cmd -Wl,-Map=${_map} -o ${OUTELF} ${_objlist})
  #  ★オンデマンドの Arduino オブジェクト（アーカイブ）。
  #  オブジェクトの後・SDK ライブラリの前。この位置でないと意味が変わる:
  #  後ろに置くのは「この時点でまだ未定義のシンボルのためだけにメンバを取る」
  #  ため、前に置かないのは、そのメンバ自身が参照する SDK シンボルを解決させる
  #  ため。scripts/fmp3_link.py が同じ規則で同じことをしている。
  #  強制リンクにしない理由: ToppersFMP3_WiFi.cpp.o のように、そのプロファイル
  #  には存在しない Wi-Fi シンボルを参照するオブジェクトが混ざるため、minimal
  #  でリンクが止まる。
  if(DEFINED ARDUINO_ARCHIVE AND NOT "${ARDUINO_ARCHIVE}" STREQUAL "")
    if(NOT EXISTS "${ARDUINO_ARCHIVE}")
      message(FATAL_ERROR
          "a1_xip_build: ARDUINO_ARCHIVE が存在しない: ${ARDUINO_ARCHIVE}")
    endif()
    list(APPEND _cmd "${ARDUINO_ARCHIVE}")
  endif()
  if(NOT A1_LIBGROUP STREQUAL "")
    list(APPEND _cmd ${A1_LIBGROUP})
  endif()
  list(APPEND _cmd -lgcc -lc)
  run("link_xip ${OUTELF}"
      LOGFILE "${STAGE}/${OUTELF}.link.log"
      CMD ${_cmd})
  if(NOT EXISTS "${STAGE}/${OUTELF}")
    message(FATAL_ERROR "a1_xip_build: ${OUTELF} が生成されなかった")
  endif()
endfunction()

#  ---- elf2image + image_info から DROM/IROM file_offs を実測 ----
function(elf2image ELF OUTBIN)
  run("elf2image ${OUTBIN}"
      LOGFILE "${STAGE}/${OUTBIN}.elf2image.log"
      CMD ${A1_ESPTOOL_CMD} --chip ${A1_CHIP} elf2image
          --flash_mode dio --flash_freq ${A1_FLASH_FREQ} --flash_size ${A1_FLASH_SIZE}
          -o ${OUTBIN} ${ELF})
  if(NOT EXISTS "${STAGE}/${OUTBIN}")
    message(FATAL_ERROR "a1_xip_build: ${OUTBIN} が生成されなかった")
  endif()
endfunction()

#  image_info をパースして DROM_OFFS/IROM_OFFS を返す
function(image_offs BIN DROM_VAR IROM_VAR)
  run("image_info ${BIN}" OUTVAR _info CMD ${A1_ESPTOOL_CMD} --chip ${A1_CHIP} image_info ${BIN})
  #  esptool 4.x: "... [DROM] ... file_offs 0x..."
  #  esptool 5.x: table row "... <load addr> <file offs> ... DROM"
  set(_drom "")
  set(_irom "")
  string(REPLACE "\n" ";" _lines "${_info}")
  foreach(ln ${_lines})
    if(ln MATCHES "\\[DROM\\]")
      if(ln MATCHES "file_offs (0x[0-9a-fA-F]+)")
        set(_drom "${CMAKE_MATCH_1}")
      endif()
    elseif(ln MATCHES "\\[IROM\\]")
      if(ln MATCHES "file_offs (0x[0-9a-fA-F]+)")
        set(_irom "${CMAKE_MATCH_1}")
      endif()
    elseif(ln MATCHES
        "^[ \t]*[0-9]+[ \t]+0x[0-9a-fA-F]+[ \t]+0x[0-9a-fA-F]+[ \t]+(0x[0-9a-fA-F]+).*[ \t]DROM")
      set(_drom "${CMAKE_MATCH_1}")
    elseif(ln MATCHES
        "^[ \t]*[0-9]+[ \t]+0x[0-9a-fA-F]+[ \t]+0x[0-9a-fA-F]+[ \t]+(0x[0-9a-fA-F]+).*[ \t]IROM")
      set(_irom "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(_drom STREQUAL "" OR _irom STREQUAL "")
    message(FATAL_ERROR "a1_xip_build: ${BIN} の image_info に DROM/IROM が無い\n${_info}")
  endif()
  set(${DROM_VAR} "${_drom}" PARENT_SCOPE)
  set(${IROM_VAR} "${_irom}" PARENT_SCOPE)
endfunction()

#  flash_cache_init.o を PADDR 定数付きでコンパイル（classic と同一フラグ・include無し）
#  flash_cache_init.c へ渡す追加定義。
#  ★A1_XIP_PADDR_RUNTIME は「定義を渡すか」と「単一パスで組むか」の両方を
#    同じ変数で決める（片方だけ効いて PADDR=不定でマップする事故を防ぐ）。
set(A1_FLASHCACHE_EXTRA_DEFS "")
if(A1_XIP_PADDR_RUNTIME)
  list(APPEND A1_FLASHCACHE_EXTRA_DEFS -DTOPPERS_XIP_PADDR_RUNTIME)
endif()
if(A1_XIP_PADDR_PROBE)
  list(APPEND A1_FLASHCACHE_EXTRA_DEFS -DTOPPERS_XIP_PADDR_PROBE)
  message(STATUS "a1_xip_build: ★PoC PADDR probe 有効")
endif()
if(DEFINED A1_XIP_POISON_PAGE AND NOT A1_XIP_POISON_PAGE STREQUAL "")
  if(NOT A1_XIP_PADDR_RUNTIME)
    message(FATAL_ERROR
      "a1_xip_build: A1_XIP_POISON_PAGE は A1_XIP_PADDR_RUNTIME=ON でのみ使える")
  endif()
  list(APPEND A1_FLASHCACHE_EXTRA_DEFS
    "-DTOPPERS_XIP_POISON_PAGE=${A1_XIP_POISON_PAGE}")
  message(STATUS
    "a1_xip_build: ★POISON 有効 実行時PADDR += ${A1_XIP_POISON_PAGE} page(s)")
endif()

function(compile_flashcache DROM_PADDR IROM_PADDR LOGNAME)
  run("compile flash_cache_init (${DROM_PADDR}/${IROM_PADDR})"
      LOGFILE "${STAGE}/${LOGNAME}"
      CMD ${GCC} -c -o objs/flash_cache_init.o -O2 -Wall -g -mlongcalls
          -DXIP_DROM_PADDR=${DROM_PADDR} -DXIP_IROM_PADDR=${IROM_PADDR}
          ${A1_FLASHCACHE_EXTRA_DEFS}
          ${FLASHCACHE_SRC})
  if(NOT EXISTS "${OBJDIR}/flash_cache_init.o")
    message(FATAL_ERROR "a1_xip_build: flash_cache_init.o が生成されなかった")
  endif()
endfunction()

if(A1_XIP_PADDR_RUNTIME)
  #  ================= S3: 単一パス（PADDRは実行時にMMUから取得） =================
  #  flash_cache_init.c が bootloader が張った MMU エントリからページ番号を復元する
  #  ので、ビルド時に PADDR を決める必要がない（＝pass1・image_offs・再コンパイル・
  #  自己整合チェック[F-1][F-2]がすべて不要）。実機確認は
    #  ★下の引数は使われない（TOPPERS_XIP_PADDR_RUNTIME 経路が無視する）。
  #    誤って静的経路でビルドされた場合に「起動しない値」で失敗させるため 0 を渡す。
  compile_flashcache("0x0U" "0x0U" "flashcache_compile.log")
  link_xip("fmp_xip.elf")
  elf2image("fmp_xip.elf" "app_xip.bin")
  message(STATUS "a1_xip_build: 単一パス（実行時PADDR）でリンクした")
elseif(A1_TWO_PASS)
  #  ================= S3: 2パス PADDR 自己整合 =================
  #  ================= パス1: placeholder PADDR =================
  compile_flashcache("0x20000U" "0x30000U" "flashcache_compile1.log")
  link_xip("fmp_xip_pass1.elf")
  elf2image("fmp_xip_pass1.elf" "app_xip_pass1.bin")
  image_offs("app_xip_pass1.bin" DROM_OFFS1 IROM_OFFS1)

  #  PADDR = 0x10000 + file_offs + 8  (partition先頭 + セグfile_offs + セグヘッダ長)
  math(EXPR DROM_PADDR "0x10000 + ${DROM_OFFS1} + 8" OUTPUT_FORMAT HEXADECIMAL)
  math(EXPR IROM_PADDR "0x10000 + ${IROM_OFFS1} + 8" OUTPUT_FORMAT HEXADECIMAL)

  #  --- positive control: pass2 PADDR を意図的にずらす（レイアウト不変・内容だけ変わる） ---
  if(DEFINED A1_POISON_PADDR)
    math(EXPR DROM_PADDR "${DROM_PADDR} + ${A1_POISON_PADDR}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR IROM_PADDR "${IROM_PADDR} + ${A1_POISON_PADDR}" OUTPUT_FORMAT HEXADECIMAL)
    message(STATUS "a1_xip_build: ★POISON 有効 pass2 PADDR += ${A1_POISON_PADDR}")
  endif()

  message(STATUS "a1_xip_build: pass1 DROM_OFFS=${DROM_OFFS1} IROM_OFFS=${IROM_OFFS1} -> DROM_PADDR=${DROM_PADDR} IROM_PADDR=${IROM_PADDR}")

  #  ================= パス2: 実測 PADDR =================
  #  classic は 0x%XU（大文字）で定数化。math の HEXADECIMAL は小文字なので合わせる。
  string(TOUPPER "${DROM_PADDR}" DROM_PADDR_U)
  string(TOUPPER "${IROM_PADDR}" IROM_PADDR_U)
  string(REPLACE "0X" "0x" DROM_PADDR_U "${DROM_PADDR_U}")
  string(REPLACE "0X" "0x" IROM_PADDR_U "${IROM_PADDR_U}")
  compile_flashcache("${DROM_PADDR_U}U" "${IROM_PADDR_U}U" "flashcache_compile2.log")
  link_xip("fmp_xip.elf")
  elf2image("fmp_xip.elf" "app_xip.bin")

  #  ================= [F] 自己整合チェック（★classic は WARN 止まり→ここでは FATAL 化） =================
  image_offs("app_xip.bin" DROM_OFFS2 IROM_OFFS2)
  #  [F-1] レイアウト自己整合: pass1 と pass2 の file_offs が一致（従来チェック）。
  #  PADDR 定数を変えても Xtensa の大即値は literal pool 経由でコードサイズ不変＝
  #  レイアウト不動＝1回で収束、という不変条件が破れた瞬間を捕まえる。
  if(NOT DROM_OFFS2 STREQUAL DROM_OFFS1 OR NOT IROM_OFFS2 STREQUAL IROM_OFFS1)
    message(FATAL_ERROR "a1_xip_build: PADDR 自己整合失敗(レイアウト) pass1(${DROM_OFFS1}/${IROM_OFFS1}) != pass2(${DROM_OFFS2}/${IROM_OFFS2})")
  endif()
  #  [F-2] PADDR 値の直接照合: pass2 の実 file_offs から再計算した期待 PADDR が、
  #  flash_cache_init.o に実際に焼き込んだ PADDR（DROM_PADDR/IROM_PADDR）と一致するか。
  #  ★これが本来の「PADDR 自己整合」の直接検査である。[F-1] はレイアウト不変という
  #  必要条件しか見ないため、A1_POISON_PADDR（レイアウトを変えず PADDR 定数だけ
  #  ずらす positive control）では [F-1] は発火しない＝FATAL 経路の negative control に
  #  ならなかった（レビュー指摘・2026-07-20）。この [F-2] は poison 時に必ず発火するので、
  #  A1_POISON_PADDR が positive control（sha 変化）と negative control（[F-2] FATAL）を兼ねる。
  math(EXPR _drom_expect "0x10000 + ${DROM_OFFS2} + 8" OUTPUT_FORMAT HEXADECIMAL)
  math(EXPR _irom_expect "0x10000 + ${IROM_OFFS2} + 8" OUTPUT_FORMAT HEXADECIMAL)
  if(NOT DROM_PADDR STREQUAL _drom_expect OR NOT IROM_PADDR STREQUAL _irom_expect)
    message(FATAL_ERROR "a1_xip_build: PADDR 自己整合失敗(値) 焼込(${DROM_PADDR}/${IROM_PADDR}) != pass2実測から期待(${_drom_expect}/${_irom_expect})")
  endif()
else()
  #  ================= LX6: 単パス =================
  #  esp32_xip.ld が IROM/DROM を固定アドレスへ置く（S3 のような 64KB ページ MMU
  #  動的割当が無い）ので flash_cache_init.o も 2パス PADDR 決定も不要。
  #  classic `build_seam_s3_smp_esp32.sh` の link_xip → elf2image をそのまま写す。
  #  LX6 には PADDR が無いので positive control は A1_POISON_FREQ（elf2image の
  #  flash_freq を差し替え＝イメージヘッダのバイトが変わる）で行う（下段参照）。
  link_xip("fmp_xip.elf")
  elf2image("fmp_xip.elf" "app_xip.bin")
endif()

file(SHA256 "${STAGE}/app_xip.bin" _sha)
message(STATUS "a1_xip_build: OK app_xip.bin sha256=${_sha}")
