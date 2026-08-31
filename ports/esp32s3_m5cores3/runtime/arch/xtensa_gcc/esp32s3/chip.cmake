#
#  ESP32-S3 チップ依存部（chip 層）— classic の Makefile.chip の CMake 翻訳
#
#  target.cmake から読まれ、最後に arch.cmake（コア依存部）を読む。
#  ARCHDIR / CHIPDIR は target.cmake が set 済みである。
#
#  ★S3 と LX6 の非対称はこの層にしか置けない:
#      S3  だけが chip_rom_libc.c を持つ
#      LX6 だけが chip_window_spill.S / chip_l1int_entry.S / chip_vectors.S を持つ
#    arch 層に置くと、もう一方のチップで undefined / 存在しないファイル参照になる。
#
set(COREDIR "${ARCHDIR}/common")

list(APPEND FMP3_COMPILE_OPTIONS -mlongcalls)
list(APPEND FMP3_LINK_OPTIONS    -mlongcalls -nostdlib)
list(APPEND FMP3_COMPILE_DEFS    OMIT_DATA_INIT)

list(APPEND FMP3_INCLUDE_DIRS ${CHIPDIR})

list(APPEND FMP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_ipi.c
    ${CHIPDIR}/chip_rom_libc.c)

#
#  ★FMP3_SYSSVC_TARGET_C_FILES は add_subdirectory() の子スコープで組み立てられ、
#    親スコープへ export されない（fmp3_core.cmake:33-44 が「未対応」と明記）。
#    S3 は SYSSVC_COBJS に chip_serial.o を持つので本変数は非空になり、
#    「非空の syssvc を要求する初の外部ターゲット」になる。
#
#    → 回避策: 最終 ELF を作る側（外部SDK）が chip_serial.c を自分で
#      target_sources() する。どのみち classic の -S "$SOBJS" は約30個を
#      列挙しており fmp3_add_syssvc() の数本では足りない。
#      ここでの APPEND は「classic との対応を記録し、消費側が参照できるようにする」
#      ためのものであって、libfmp3.a に入れることは期待していない。
#
list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${CHIPDIR}/chip_serial.c)

list(APPEND FMP3_START_FILES ${COREDIR}/start.S)

include(${COREDIR}/arch.cmake)
