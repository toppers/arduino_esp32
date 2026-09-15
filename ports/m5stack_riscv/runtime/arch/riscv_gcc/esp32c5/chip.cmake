#
#		チップ依存部の CMake 定義（ESP32-C5 用/FMP3）
#
#  target.cmake から include される。fmp3/arch/riscv_gcc/esp32p4/chip.cmake、
#  esp32c6/chip.cmake と同じ分担: 共通 arch（COREDIR）は fmp3_core 側、
#  チップ依存部（CHIPDIR）は本 repo 側。ARCHDIR / CHIPDIR / TARGETDIR は
#  呼び出し元の target.cmake が設定済み。
#
#  出典: fmp3/arch/riscv_gcc/esp32c6/chip.cmake（C6 段1）を骨格に、CLIC 部分を
#  fmp3/arch/riscv_gcc/esp32p4/chip.cmake から写した。
#  差分（C6 から）: (1) USE_RISCV_DIRECT_TRAP を定義（CLIC 非ベクタ。P4 と同じ）、
#  (2) 共通 clic_kernel_impl.c をリンクする（P4 と同じ。msi_ipi.c / mtimer.c は
#  積まない = 単一コアで IPI 無し、HRT は target 側 systimer）、
#  (3) 名前 c6 -> c5。ISA/ABI（rv32imac_zicsr_zifencei / ilp32、A 拡張は FMP3
#  spinlock の amoswap 用）、nano.specs、TLS、chip_start.S への差替えは C6 と同じ。
#
set(COREDIR ${ARCHDIR}/common)

set(FMP3_RISCV_MARCH "rv32imac_zicsr_zifencei" CACHE STRING
    "RISC-V ISA string for ESP32-C5 (A extension is required by FMP3 spinlocks)")
set(FMP3_RISCV_MABI "ilp32" CACHE STRING "RISC-V ABI for ESP32-C5")

if(NOT DEFINED FMP3_RISCV_SPECS)
    set(FMP3_RISCV_SPECS "--specs=nano.specs")
endif()

list(APPEND FMP3_INCLUDE_DIRS ${CHIPDIR})

list(APPEND FMP3_COMPILE_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    -msmall-data-limit=8
    -mstrict-align
    -mno-save-restore
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    ${FMP3_RISCV_SPECS}
)

list(APPEND FMP3_LINK_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    ${FMP3_RISCV_SPECS}
    -nostartfiles
)

#
#  CLIC はダイレクト（非ベクタ）トラップで使う（P4 chip.cmake / Makefile.chip:49-56）。
#
list(APPEND FMP3_COMPILE_DEFS USE_RISCV_DIRECT_TRAP)

#
#  TLS（chip_asm.inc の init_additional_regs_start_r）。常時有効。
#
list(APPEND FMP3_COMPILE_DEFS TOPPERS_SUPPORT_TLS)

#
#  カーネルに含めるチップ依存ソース。
#  clic_kernel_impl.c は COREDIR（fmp3_core 側、無改変）にあるが、「CLIC を使う」
#  のはチップの決定なので P4 と同じくここで選ぶ。閾値の read/write は chip 側の
#  esp32c5_clic_kernel_impl.h（共通 clic_kernel_impl.h の複製）が差し替える。
#
list(APPEND FMP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_support.S
    ${COREDIR}/clic_kernel_impl.c
)

#
#  非TECS版 SIO ドライバ（コンソール実体の選択）
#    ESP32C5_CONSOLE=usbjtag（既定。M5Stamp-C5 は native USB のみ）| uart0
#  usbjtag の実体は target 側 esp32c5_usbjtag_hal.c（C6 と同じ分担）なので
#  ここでは chip_serial.c だけを積み、target.cmake が実体を足す。
#
if(NOT DEFINED ESP32C5_CONSOLE)
    set(ESP32C5_CONSOLE usbjtag)
endif()
if(ESP32C5_CONSOLE STREQUAL "usbjtag")
    list(APPEND FMP3_COMPILE_DEFS TOPPERS_ESP32C5_CONSOLE_USBJTAG)
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${CHIPDIR}/chip_serial.c)
elseif(ESP32C5_CONSOLE STREQUAL "uart0")
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES
        ${CHIPDIR}/chip_serial.c
        ${CHIPDIR}/esp32c5_uart.c)
else()
    message(FATAL_ERROR "ESP32C5_CONSOLE は usbjtag / uart0 のみ（指定=${ESP32C5_CONSOLE}）")
endif()

#
#  コア依存部（fmp3_core 側/無改変）
#
include(${COREDIR}/arch.cmake)

#
#  スタートアップ（start.S）を C5 専用版へ差し替える（C6 段1 と同じ理由:
#  fmp3_core 共通 start.S の無条件 FPU 初期化が -march=rv32imac で assembler
#  error になる。詳細は chip_start.S 冒頭のコメント）。REMOVE_ITEM が空振り
#  したら（arch.cmake の start.S の綴りが変わった等）黙って両方積んで
#  toppers_start の多重定義になるので、事前に在ることを確かめる（fail-closed）。
#
list(FIND FMP3_START_FILES ${COREDIR}/start.S _a1_c5_start_idx)
if(_a1_c5_start_idx EQUAL -1)
    message(FATAL_ERROR "chip.cmake(esp32c5): FMP3_START_FILES に ${COREDIR}/start.S が無い（arch.cmake の変更?）: ${FMP3_START_FILES}")
endif()
list(REMOVE_ITEM FMP3_START_FILES ${COREDIR}/start.S)
list(APPEND FMP3_START_FILES ${CHIPDIR}/chip_start.S)
