#
#  Xtensa コア依存部（arch 層）— classic の Makefile.core の CMake 翻訳
#
#  include 連鎖の終端。target.cmake -> chip.cmake -> arch.cmake の順に読まれる。
#
#  ★COREDIR は CMAKE_CURRENT_LIST_DIR から自己解決する。
#    arch/arm64_gcc/common/arch.cmake は set(COREDIR ${FMP3_ROOT_DIR}/arch/...) と
#    書いているが、あれは arch が fmp3_core の中にあるから成立する書き方である。
#    Xtensa は arch も chip も fmp3_core の外（本リポジトリ側）にあるので、
#    FMP3_ROOT_DIR 基準で書くと壊れる。
#
#  ★TOOLDIR（arch/gcc: tool_stddef.h 等）だけは fmp3_core 側にしか無いので
#    FMP3_ROOT_DIR 基準でよい。
#
get_filename_component(COREDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
set(TOOLDIR "${FMP3_ROOT_DIR}/arch/gcc")

#
#  ★DUMP = dump（Makefile.core 参照）。宣言を忘れると既定の srec のままになり、
#    objcopy -O srec で出力されたものを cfg pass2 が拡張子 .srec を見て
#    SRecord として読む。「動くが違うものを読む」型の壊れ方をするので必須。
#
set(FMP3_DUMP_FORMAT dump)

list(APPEND FMP3_SYMVAL_TABLES    ${COREDIR}/core_sym.def)
list(APPEND FMP3_OFFSET_TRB_FILES ${COREDIR}/core_offset.py)

list(APPEND FMP3_INCLUDE_DIRS ${COREDIR} ${TOOLDIR})

#  ★FMP3_ARCH_C_FILES は名前に反して .S も入れる（add_library が拡張子で
#    ソース種別を判定するため。kria も psci_support.S をここに入れている）
list(APPEND FMP3_ARCH_C_FILES
    ${COREDIR}/core_kernel_impl.c
    ${COREDIR}/core_support.S)

#
#  ★-lc / -lgcc
#    Makefile.core の字面は LIBS := $(LIBS) -lgcc のみだが、実測のリンク行は
#    「... objs/start.o objs/cfg1_out.o -lc -lgcc」であり、sample/Makefile の
#    SRCLANG=c による -lc の無条件付加が効いている。-nostdlib を付けているので
#    -lc を落とすと memcpy 等で落ちる。riscv/arm_m/arm64 と同じ規約。
#
list(APPEND FMP3_LINK_LIBS c gcc)
