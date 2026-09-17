#
#		ターゲット依存部の CMake 定義（M5Stamp ESP32P4 用）
#
#  svn 側 target/m5stamp_esp32p4_gcc/Makefile.target の CMake 版。
#  各項に Makefile.target の行番号を根拠として付けてある。
#  Makefile.target 自身は取り込んでいない（CMake が本 repo の正準ビルドであり、
#  実機書込み/OpenOCD ターゲットは段2 以降で別途用意する）。
#
#  【パス解決の規約】（polarfire_soc_kit_gcc/target.cmake と同じ分担）
#   - 共通 arch（arch/riscv_gcc/common）は **fmp3_core 側** ＝ ${FMP3_ROOT_DIR} 基準。
#     arch.cmake 自身が `set(COREDIR ${FMP3_ROOT_DIR}/arch/riscv_gcc/common)` と
#     書いているので、ARCHDIR をここ以外に向けると壊れる。
#   - チップ依存部・ターゲット依存部は **本 repo 側**（CMAKE_CURRENT_LIST_DIR 相対）。
#     Xtensa 側（fmp3/target/esp32*_devkitc_gcc/target.cmake）と同じ非対称であり、
#     RISC-V では arch/common だけが submodule 側に残る点だけが違う。
#
get_filename_component(TARGETDIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
set(ARCHDIR ${FMP3_ROOT_DIR}/arch/riscv_gcc)
get_filename_component(CHIPDIR "${TARGETDIR}/../../arch/riscv_gcc/esp32p4" REALPATH)

#
#  ボード名（Makefile.target:10-12, :40）
#
if(NOT DEFINED FMP3_BOARD)
    set(FMP3_BOARD M5STAMP_ESP32P4)
endif()
list(APPEND FMP3_COMPILE_DEFS ${FMP3_BOARD})

#  非TECS版システムサービスを使う
list(APPEND FMP3_COMPILE_DEFS TOPPERS_OMIT_TECS)

#  Makefile.target:25
list(APPEND FMP3_INCLUDE_DIRS ${TARGETDIR})

#
#  ROM イメージ形式（cfg の --rom-image が拡張子で srec/dump を判別する）
#
#  **上流（svn classic）の実効値は srec である**——根拠は「どの Makefile も
#  DUMP を上書きしていない」こと:
#    sample/Makefile:133-134  ifndef DUMP -> DUMP = srec
#    arch/riscv_gcc/common/Makefile.core   DUMP への代入なし（grep 0 件）
#    arch/riscv_gcc/esp32p4/Makefile.chip  同上
#    target/m5stamp_esp32p4_gcc/Makefile.target 同上
#  （dump を宣言しているのは arm64_gcc/common/Makefile.core:34 と、
#    本 repo の Xtensa arch.cmake:23 だけである。）
#
#  Xtensa 側は「宣言を忘れると pass2 が黙って違うものを読む」と警告しているので、
#  P4 でも srec/dump の A/B を実測して決着させた（統合計画 未調査 #3）。
#  実測結果は .steering/20260814-p4-stage1/README.md に置いた:
#  **srec と dump で cfg 生成物 4 本（cfg1_out.c / kernel_cfg.c / kernel_cfg.h /
#  offset.h）はバイト同一**であり、P4 ではどちらでも読み違えは起きない。
#  ⇒ 上流の実効値に合わせて **srec** を明示宣言する（既定値と同値だが、
#     「継承した結果たまたま srec」ではなく「測って選んだ srec」であることを残す）。
#
#  A/B を後からでも撃てるように口を残す。**素の -DFMP3_DUMP_FORMAT=dump では効かない**
#  ——fmp3_core/CMakeLists.txt:93-95 が target.cmake を include する**前**に
#  既定値 srec を必ず代入するので、target.cmake 側の `if(NOT DEFINED ...)` は
#  永久に偽になる（Xtensa の arch.cmake:23 が無条件 set にしているのはこのため）。
#  よって別名の knob で受ける。
if(DEFINED A1_P4_DUMP_FORMAT)
    set(FMP3_DUMP_FORMAT ${A1_P4_DUMP_FORMAT})
else()
    set(FMP3_DUMP_FORMAT srec)
endif()

#
#  リンカスクリプト（Makefile.target:47）
#
#  【重要】-T の適用は fmp3_core/CMakeLists.txt の1箇所に集約されている。
#    ここでは値を確定させるだけで -T は積まない（polarfire の同名ファイルの
#    末尾コメントに理由がある。二重指定で ld が fatal error になる）。
#
#  段4（2026-08-14）: seam（実 ESP-IDF bootloader 起動）では flash XIP 版の
#  リンカスクリプトへ差し替える。既定（方式(a)＝段2/段3）は無改変で
#  esp32p4_fmp.ld のまま＝**この if を通らない限りバイト列は変わらない**。
#  差し替えの口を A1_P4_LDSCRIPT という別名にしているのは FMP3_DUMP_FORMAT と
#  同じ理由（fmp3_core/CMakeLists.txt が target.cmake の include 前に
#  既定値を代入する変数は、target 側の `if(NOT DEFINED ...)` では受けられない）。
if(DEFINED A1_P4_LDSCRIPT)
    set(FMP3_LDSCRIPT ${A1_P4_LDSCRIPT})
else()
    set(FMP3_LDSCRIPT ${TARGETDIR}/esp32p4_fmp.ld)
endif()

#
#  FMP3_LDSCRIPT_VIA_DRIVER_T は宣言しない（＝既定 OFF ＝ -Wl,-T,）。
#  polarfire が ON にしているのは picolibc.specs の %{!T:-Tpicolibc.ld} 対策で
#  あって、P4 は newlib 系（--specs=nano.specs、Makefile.chip:24）なので
#  その自動注入を持たない。実測でも -Wl,-T, で esp32p4_fmp.ld の
#  ENTRY(toppers_start) と IRAM 配置がそのまま効いている
#  （統合計画 未調査 #4 の決着。詳細は段1 の記録）。
#

#
#  カーネルに含めるターゲット依存ソース（Makefile.target:53）
#
list(APPEND FMP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
)

#
#  cfg に渡すファイル（Makefile.target 経由で sample/Makefile が渡す TARGET_*_TRB）
#
list(APPEND FMP3_CFG_FILES            ${TARGETDIR}/target_kernel.cfg)
list(APPEND FMP3_KERNEL_CFG_TRB_FILES ${TARGETDIR}/target_kernel.py)
list(APPEND FMP3_CLASS_TRB_FILES      ${TARGETDIR}/target_class.py)
list(APPEND FMP3_CHECK_TRB_FILES      ${TARGETDIR}/target_check.py)

#
#  チップ依存部（Makefile.target:175）
#
include(${CHIPDIR}/chip.cmake)

#
#  最終リンクのオプション
#
#  Makefile.target:32  OBJ_LDFLAGS += -Wl,--gc-sections
#  Makefile.target:35  LDFLAGS     += -Wl,--undefined=_kernel_mpfinib_table
#    （--gc-sections で MP 初期化テーブルが消えると cfg pass3(check) が
#      シンボルを見つけられないため保持する）
#
#  【重要】上流より **保持する表を増やしている**。理由（段1 の実測）:
#    kernel/kernel_check.py は :209/:233/:248/:261 で
#      _kernel_tinib_table / _kernel_mpfinib_table /
#      _kernel_cycinib_table / _kernel_alminib_table
#    の 4 本を **cfgData が空でも無条件に SYMBOL() で引く**。
#    上流が mpfinib だけを挙げているのは、上流の既定アプリで他の 3 本が
#    たまたま参照されて生き残っていたからであって、一般には足りない。
#    実際 test_int1（CRE_CYC が 0 個）を建てると
#      error: E_SYS: symbol '_kernel_cycinib_table' not found
#    で pass3 が落ちる（段1 で実測。sample1 は CRE_CYC を 6 個持つので落ちない）。
#    なお上流 classic はこの穴に気付きにくい——sample/Makefile:256,260 で
#    `all: check ...` が**コメントアウト**されており、既定では check が走らない。
#    CMake 側は fmp3_core が fmp3_cfg_check() を POST_BUILD で常に走らせるので、
#    ここで表を保持しないと「アプリによって落ちたり落ちなかったりする」。
#    （_kernel_mpk と _kernel_istk_table/_kernel_inirtnb_table_* は
#      条件付き参照（:275,:293,:305）なので挙げない。）
#
list(APPEND FMP3_LINK_OPTIONS
    -Wl,--gc-sections
    -Wl,--undefined=_kernel_mpfinib_table
    -Wl,--undefined=_kernel_tinib_table
    -Wl,--undefined=_kernel_cycinib_table
    -Wl,--undefined=_kernel_alminib_table
)

#
#  上流の OBJ_LDFLAGS（最終 obj 専用）と LDFLAGS（cfg1_out にも効く）の区別を
#  CMake で再現する。FMP3_LINK_OPTIONS は fmp と cfg1_out の両方に効くので、
#  cfg1_out 側だけ --no-gc-sections で打ち消す（後着が勝つ）。
#  これをしないと cfg1_out から TOPPERS_magic_number が除去され、pass2 が
#  cfg1_out.syms から見つけられずに止まる（polarfire と同じ理由・同じ対処）。
#
list(APPEND FMP3_CFG1_OUT_LINK_OPTIONS -Wl,--no-gc-sections)
