#
#  無印 ESP32（LX6）チップ依存部（chip 層）— classic の Makefile.chip の CMake 翻訳
#
#  target.cmake から読まれ、最後に arch.cmake（コア依存部）を読む。
#  ARCHDIR / CHIPDIR は target.cmake が set 済みである。
#
#  S3 との差はソース一覧だけである:
#      LX6 だけが chip_window_spill.S / chip_l1int_entry.S / chip_vectors.S を持つ
#      S3  だけが chip_rom_libc.c を持つ（LX6 は ROM libc を使わない）
#    この非対称が「chip 層を切ったことの正しさ」の実証になっている
#    （arch 層に置いていたら、もう一方で必ず壊れる）。
#
set(COREDIR "${ARCHDIR}/common")

list(APPEND FMP3_COMPILE_OPTIONS -mlongcalls)
list(APPEND FMP3_LINK_OPTIONS    -mlongcalls -nostdlib)
list(APPEND FMP3_COMPILE_DEFS    OMIT_DATA_INIT)

list(APPEND FMP3_INCLUDE_DIRS ${CHIPDIR})

list(APPEND FMP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_ipi.c
    ${CHIPDIR}/chip_window_spill.S
    ${CHIPDIR}/chip_l1int_entry.S
    ${CHIPDIR}/chip_vectors.S)

#  スコープ伝播の制約については esp32s3/chip.cmake の同箇所のコメントを参照
list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${CHIPDIR}/chip_serial.c)

list(APPEND FMP3_START_FILES ${COREDIR}/start.S)

include(${COREDIR}/arch.cmake)
