#
#		チップ依存部の CMake 定義（ESP32-P4 用）
#
#  target.cmake から include される（上流 Makefile.chip に相当）。
#  対応する上流は本ディレクトリの Makefile.chip（fmp3_core pristine の写し）で、
#  以下の各項には Makefile.chip の行番号を根拠として付けてある。
#
#  【このファイルだけが本ディレクトリの「新規」である】
#  本ディレクトリの他の 26 ファイルは fmp3_core（submodule）の
#  arch/riscv_gcc/esp32p4/ の **バイト同一の複製** である（統合計画 判断点 B-2）。
#  複製が乖離していないことは cmake/a1_p4_chip_pristine_audit.sh が
#  configure 時に検査し、乖離していれば fail-closed で止まる。
#  chip.cmake と clic_kernel.py は上流に無い新規ファイルなので監査の対象外
#  （監査台本の「repo 側追加ファイル」の明示リストに載っている）。
#
#  【パス解決の規約】（polarfire_soc/chip.cmake と同じ分担）
#   - 共通 arch（arch/riscv_gcc/common）は **fmp3_core 側** ＝ ARCHDIR/common
#     （arch.cmake 自身が ${FMP3_ROOT_DIR} 基準で COREDIR を再設定するので、
#      ARCHDIR は fmp3_core の arch/riscv_gcc を指していなければならない）
#   - チップ依存部（このディレクトリ）は **本 repo 側** ＝ CHIPDIR
#   - ARCHDIR / CHIPDIR / TARGETDIR は呼び出し元の target.cmake が設定済み
#
set(COREDIR ${ARCHDIR}/common)

#
#  ISA と ABI（Makefile.chip:45）
#
#    -march=rv32imafc_zicsr_zifencei_xesppie -mabi=ilp32f
#
#  xesppie は PIE の esp.* 命令（chip_asm.inc の pie_push/pie_pop）用で、
#  IDF esp32p4 と同一指定である（Makefile.chip:42-44）。
#  ilp32f を選ぶ理由は Makefile.chip:27-29 に書かれている——ESP-IDF が ilp32f で
#  ビルドされており、方式(a)（FMP3 を IDF アプリへ静的リンク）では ABI を
#  一致させないと "can't link soft-float modules with single-float modules" になる。
#
set(FMP3_RISCV_MARCH "rv32imafc_zicsr_zifencei_xesppie" CACHE STRING
    "RISC-V ISA string passed to -march (Makefile.chip:45; xesppie is required to assemble the PIE esp.* instructions)")
set(FMP3_RISCV_MABI "ilp32f" CACHE STRING
    "RISC-V ABI passed to -mabi (Makefile.chip:45; must match ESP-IDF for method (a) static linking)")

#
#  C ライブラリの specs（Makefile.chip:24）
#
if(NOT DEFINED FMP3_RISCV_SPECS)
    set(FMP3_RISCV_SPECS "--specs=nano.specs")
endif()

#
#  セクション分割（Makefile.chip:41）
#
#  標準は関数/データ単位に分割して --gc-sections で削減する。
#  方式(a) で IDF へ静的リンクし FMP3 全 text を IRAM へ再配置する場合は
#  obj 毎に単一 .text にしたいので -fno-function-sections へ落とす
#  （svn の build_fmp3_lib.sh:59 が `make SECTION_OPTS=-fno-function-sections`
#   でそうしている）。段2 でその経路を作るときに使う。
#
if(NOT DEFINED FMP3_P4_SECTION_OPTS)
    set(FMP3_P4_SECTION_OPTS -ffunction-sections -fdata-sections)
endif()

list(APPEND FMP3_INCLUDE_DIRS
    ${CHIPDIR}
)

#  Makefile.chip:45-47
list(APPEND FMP3_COMPILE_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    -msmall-data-limit=8
    -mstrict-align
    -mno-save-restore
    -fsigned-char
    ${FMP3_P4_SECTION_OPTS}
    ${FMP3_RISCV_SPECS}
)

#  リンク時にも ISA/ABI/specs を渡す（gcc をリンカドライバとして使うため）
list(APPEND FMP3_LINK_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    ${FMP3_RISCV_SPECS}
    -nostartfiles          #  Makefile.chip:48
)

#
#  CLIC はダイレクト（非ベクタ）トラップで使う（Makefile.chip:49-56）。
#
list(APPEND FMP3_COMPILE_DEFS USE_RISCV_DIRECT_TRAP)

#
#  TLS（Makefile.chip:57-63）。常時有効。
#
list(APPEND FMP3_COMPILE_DEFS TOPPERS_SUPPORT_TLS)

#
#  コプロセッサ（HWLP / PIE）— 判断点 D（ハイブリッド確定）
#
#  上流 Makefile.chip:67-90 は `NO_COPROC=1` を渡さない限り
#  TOPPERS_SUPPORT_HWLP と TOPPERS_SUPPORT_PIE を **既定で定義する**
#  （＝eager。TOPPERS_PIE_LAZY だけが ifdef PIE_LAZY の opt-in）。
#
#  ここでも既定を eager に揃える。理由は判断点 D の確定内容そのもの:
#    段2 は「svn と同条件で等価再現」なので eager でなければ意味がない。
#    段4 以降の seam bring-up は変数を減らすため FMP3_P4_NO_COPROC=ON から積み直す。
#
#  lazy（TOPPERS_PIE_LAZY）は判断点 D で除外のまま。ここには口を用意しない。
#
option(FMP3_P4_NO_COPROC
    "Disable HWLP/PIE coprocessor context management (upstream NO_COPROC=1). OFF = eager, same as svn default"
    OFF)
if(NOT FMP3_P4_NO_COPROC)
    list(APPEND FMP3_COMPILE_DEFS TOPPERS_SUPPORT_HWLP)   #  Makefile.chip:72
    list(APPEND FMP3_COMPILE_DEFS TOPPERS_SUPPORT_PIE)    #  Makefile.chip:76
endif()

#
#  カーネルに含めるチップ依存ソース（Makefile.chip:99,105,107）
#
#  ESP32-P4 は PLIC を持たず CLIC を使うので、polarfire の plic_kernel_impl.c
#  ではなく clic_kernel_impl.c をリンクする（Makefile.chip:97-99）。
#  clic_kernel_impl.c / msi_ipi.c / mtimer.c は COREDIR（fmp3_core 側）にあるが、
#  「CLIC と Machine Timer と MSI-IPI を使う」のはチップの決定なので上流
#  Makefile.chip と同じくここで選ぶ。
#
list(APPEND FMP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_support.S
    ${COREDIR}/clic_kernel_impl.c
    ${COREDIR}/msi_ipi.c
    ${COREDIR}/mtimer.c
)

#
#  非TECS版 SIO ドライバ（Makefile.chip:112）
#  svn の build_fmp3_lib.sh:40 の -S "... chip_serial.o" に対応する。
#  polarfire と違い P4 は mmuart.c を持たない（chip_serial.c に閉じている）。
#
list(APPEND FMP3_SYSSVC_TARGET_C_FILES
    ${CHIPDIR}/chip_serial.c
)

#
#  コア依存部（Makefile.chip:117）
#
include(${COREDIR}/arch.cmake)
